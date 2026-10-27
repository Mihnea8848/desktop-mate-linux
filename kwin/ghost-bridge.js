// Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// KWin side of the bridge. Sends the window list to ghost-bridged (Update)
// and applies the commands ghost.exe sends back (Poll).
// Snapshot line, bottom to top: id rank x y w h active kind(normal|dock) caption

function lc(s) { return (s || "").toString().toLowerCase(); }

// the game and the ghost.exe mirrors (class depends on the Proton build)
function isWine(w) {
    var c = lc(w.resourceClass);
    return c === "steam_app_3301060" || c === "steam_proton" || c.indexOf("desktopmate") !== -1;
}

function onCurrentDesktop(w) {
    if (w.onAllDesktops) return true;
    var ds = w.desktops || [];
    for (var i = 0; i < ds.length; ++i)
        if (ds[i] === workspace.currentDesktop) return true;
    return ds.length === 0;
}

function wanted(w) {
    if (w.deleted || w.minimized || isWine(w) || !onCurrentDesktop(w)) return false;
    if (w.dock) return !w.hidden;
    return w.normalWindow && !w.skipTaskbar && (w.caption || "").length > 0;
}

var lastSnapshot = "";
function push() {
    var lines = [];
    var stack = workspace.stackingOrder;
    var rank = 0;
    for (var i = 0; i < stack.length; ++i) {
        var w = stack[i];
        if (!wanted(w)) continue;
        var g = w.frameGeometry;
        lines.push([
            w.internalId, rank++,
            Math.round(g.x), Math.round(g.y), Math.round(g.width), Math.round(g.height),
            w.active ? 1 : 0,
            w.dock ? "dock" : "normal",
            (w.caption || "").replace(/[\t\n\r]/g, " ") || "Plasma Panel"
        ].join("\t"));
    }
    var snap = lines.join("\n");
    if (snap !== lastSnapshot) {
        lastSnapshot = snap;
        callDBus("org.ghostbridge", "/", "org.ghostbridge", "Update", snap);
    }
    if (!polling || Date.now() - pollStarted > 30000) poll();
}

function findById(id) {
    var all = workspace.windowList();
    for (var i = 0; i < all.length; ++i)
        if (all[i].internalId.toString() === id) return all[i];
    return null;
}

function apply(line) {
    var f = line.split("\t");
    var w = findById(f[1]);
    if (!w || isWine(w)) return;
    if (f[0] === "move" && f.length >= 6 && w.moveable) {
        var g = w.frameGeometry;
        var wd = w.resizeable ? parseInt(f[4]) : g.width;
        var ht = w.resizeable ? parseInt(f[5]) : g.height;
        w.frameGeometry = { x: parseInt(f[2]), y: parseInt(f[3]), width: wd, height: ht };
    } else if (f[0] === "minimize" && w.minimizable) {
        w.minimized = true;
    } else if (f[0] === "activate") {
        workspace.activeWindow = w;
    }
}

// Only one poll chain may exist: ghost-bridged releases the older poll when a
// newer one arrives, so two chains would release each other in a loop.
var polling = false, pollStarted = 0, pollGen = 0;
function poll() {
    var gen = ++pollGen;
    polling = true;
    pollStarted = Date.now();
    callDBus("org.ghostbridge", "/", "org.ghostbridge", "Poll", function (cmds) {
        if (gen !== pollGen) return;
        polling = false;
        var lines = (cmds || "").toString().split("\n");
        for (var i = 0; i < lines.length; ++i)
            if (lines[i].length) apply(lines[i]);
        poll();
    });
}

function hook(w) {
    if (isWine(w)) return;
    w.frameGeometryChanged.connect(push);
    w.minimizedChanged.connect(push);
    w.captionChanged.connect(push);
    if (w.desktopsChanged) w.desktopsChanged.connect(push);
    if (w.hiddenChanged) w.hiddenChanged.connect(push);
    push();
}

workspace.windowAdded.connect(hook);
workspace.windowRemoved.connect(push);
workspace.windowActivated.connect(push);
workspace.currentDesktopChanged.connect(push);
if (workspace.stackingOrderChanged) workspace.stackingOrderChanged.connect(push);
workspace.windowList().forEach(hook);
push();
