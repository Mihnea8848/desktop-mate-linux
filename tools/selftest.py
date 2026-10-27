#!/usr/bin/env python3
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# End-to-end test against a running game. Moves the real pointer, so do not
# use the desktop while it runs. Needs: ydotoold running, python-xlib, Pillow,
# kdialog, qdbus6, spectacle. Run from the repository root:
#   python3 tools/selftest.py
import os
import subprocess
import sys
import tempfile
import time

from PIL import Image, ImageChops
from Xlib import display
from Xlib.ext import shape

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUN = "/dev/shm/ghost-bridge"
TMP = tempfile.mkdtemp(prefix="dm-selftest-")
TITLE = "Desktop Mate selftest window"
ENV = dict(os.environ, YDOTOOL_SOCKET=os.environ.get(
    "YDOTOOL_SOCKET", f"/run/user/{os.getuid()}/.ydotool_socket"), DISPLAY=os.environ.get("DISPLAY", ":0"))
results = []


def sh(*args, check=False, **kw):
    return subprocess.run(args, env=ENV, capture_output=True, text=True, check=check, **kw)


def report(name, ok, detail=""):
    results.append(ok)
    print(f"{'PASS' if ok else 'FAIL'}  {name}{'  (' + detail + ')' if detail else ''}", flush=True)


def kwin_run(js, name="selftest"):
    path = os.path.join(TMP, name + ".js")
    with open(path, "w") as f:
        f.write(js)
    q = ["qdbus6", "org.kde.KWin", "/Scripting"]
    sh(*q, "org.kde.kwin.Scripting.unloadScript", name)
    sh(*q, "org.kde.kwin.Scripting.loadScript", path, name)
    sh(*q, "org.kde.kwin.Scripting.start")
    time.sleep(0.4)
    sh(*q, "org.kde.kwin.Scripting.unloadScript", name)


def cursor():
    diag = os.path.join(RUN, "diag.txt")
    if os.path.exists(diag):
        os.remove(diag)
    kwin_run('var p = workspace.cursorPos; callDBus("org.ghostbridge", "/", "org.ghostbridge", '
             '"Diag", Math.round(p.x) + " " + Math.round(p.y));', "selftest_cursor")
    for _ in range(20):
        if os.path.exists(diag) and os.path.getsize(diag):
            x, y = open(diag).read().split()
            return int(x), int(y)
        time.sleep(0.05)
    raise RuntimeError("no cursor position from KWin")


def move_to(tx, ty, step=40):
    for _ in range(60):
        x, y = cursor()
        dx, dy = tx - x, ty - y
        if abs(dx) <= 1 and abs(dy) <= 1:
            return
        dx = max(-step, min(step, dx))
        dy = max(-step, min(step, dy))
        sh("ydotool", "mousemove", "-x", str(dx), "-y", str(dy))
        time.sleep(0.03)


def press(button=0):
    sh("ydotool", "click", hex(0x40 + button))


def release(button=0):
    sh("ydotool", "click", hex(0x80 + button))


def screenshot(name):
    path = os.path.join(TMP, name + ".png")
    sh("spectacle", "-b", "-n", "-p", "-o", path)
    return Image.open(path).convert("RGB")


def game_shape():
    """Input-shape rectangles of the game window in root coordinates."""
    d = display.Display()
    for c in d.screen().root.query_tree().children:
        try:
            if c.get_wm_name() == "DesktopMate" and c.get_attributes().map_state == 2:
                g = c.get_geometry()
                return [(g.x + r.x, g.y + r.y, r.width, r.height)
                        for r in c.shape_get_rectangles(shape.SK.Input).rectangles]
        except Exception:
            pass
    return []


def game_box():
    """Bounding box of the game's click-through shape, i.e. the character."""
    rs = game_shape()
    if not rs:
        return None
    return (min(r[0] for r in rs), min(r[1] for r in rs),
            max(r[0] + r[2] for r in rs), max(r[1] + r[3] for r in rs))


def body_point(frac):
    """Middle of the widest solid span of the shape, frac of the way down."""
    rs = game_shape()
    if not rs:
        return None
    top = min(r[1] for r in rs)
    y = top + int(frac * (max(r[1] + r[3] for r in rs) - top))
    spans = sorted((r[0], r[0] + r[2]) for r in rs if r[1] <= y < r[1] + r[3])
    merged = []
    for a, b in spans:
        if merged and a <= merged[-1][1] + 1:
            merged[-1][1] = max(merged[-1][1], b)
        else:
            merged.append([a, b])
    if not merged:
        return None
    a, b = max(merged, key=lambda m: m[1] - m[0])
    return (a + b) // 2, y


def tie(img, box):
    """Lower half of the red tie inside box; also finds her when she lies down."""
    px = img.load()
    x0, y0, x1, y1 = max(box[0], 0), max(box[1], 0), min(box[2], img.width), min(box[3], img.height)
    pts = [(x, y) for y in range(y0, y1, 2) for x in range(x0, x1, 2)
           if px[x, y][0] > 180 and px[x, y][1] < 60 and px[x, y][2] < 60]
    if not pts:
        return None
    pts.sort(key=lambda q: q[1])
    low = pts[len(pts) // 2:]
    return sum(q[0] for q in low) // len(low), sum(q[1] for q in low) // len(low)


def animating():
    box = game_box()
    if not box:
        return False, "no game window"
    a, _, b = screenshot("a1"), time.sleep(1), screenshot("a2")
    crop = (max(box[0], 0), max(box[1], 0), box[2], box[3])
    diff = ImageChops.difference(a.crop(crop).convert("L"), b.crop(crop).convert("L"))
    changed = sum(1 for v in diff.get_flattened_data() if v > 20)
    return changed > 500, f"{changed} px changed"


def frame_pacing(seconds=4):
    """Share of on-screen frame intervals that are doubled (two refreshes or more)."""
    import hashlib
    d = display.Display()
    win = next((c for c in d.screen().root.query_tree().children
                if c.get_wm_name() == "DesktopMate" and c.get_attributes().map_state == 2), None)
    box = game_box()
    if not win or not box:
        return None
    g = win.get_geometry()
    x, y, w, h = box[0] - g.x, box[1] - g.y, box[2] - box[0], box[3] - box[1]
    last, changes, start = None, [], time.perf_counter()
    while time.perf_counter() - start < seconds:
        digest = hashlib.blake2b(win.get_image(x, y, w, h, 2, 0xffffffff).data, digest_size=8).digest()
        if digest != last:
            changes.append(time.perf_counter())
            last = digest
    # reads that catch a frame half written give near-zero gaps; skip them
    gaps = [b - a for a, b in zip(changes, changes[1:]) if b - a > 0.010]
    return sum(1 for v in gaps if v > 0.025) / len(gaps) if gaps else None


def snapshot_rect(title):
    for line in open(os.path.join(RUN, "windows.tsv"), encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) >= 9 and f[8].startswith(title):
            return tuple(int(v) for v in f[2:6])
    return None


def wine_run(exe, *args):
    subprocess.run([os.path.join(REPO, "tools", "run-in-game.sh"), os.path.join(REPO, "bin", "tools", exe), *args],
                   env=ENV, capture_output=True, timeout=90)


def main():
    running = sh("pgrep", "-f", r"^[A-Z]:.*\\DesktopMate\.exe").returncode == 0
    report("game running", running)
    if not running:
        return
    report("ghost.exe running", sh("pgrep", "-f", r"^[A-Z]:.*\\ghost\.exe").returncode == 0)
    report("relay on the session bus", "org.ghostbridge" in sh("qdbus6").stdout)
    loaded = sh("qdbus6", "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting.isScriptLoaded", "ghostbridge")
    report("KWin script loaded", loaded.stdout.strip() == "true")
    ok, detail = animating()
    report("character animates", ok, detail)
    doubled = frame_pacing()
    report("animation reaches the screen at the full frame rate",
           doubled is not None and doubled < 0.05,
           f"{doubled:.0%} of frames shown twice; see tools/build-ge-proton.sh" if doubled is not None else "")

    dialog = subprocess.Popen(["kdialog", "--title", TITLE, "--msgbox", "selftest"], env=ENV)
    try:
        rect = None
        for _ in range(30):
            time.sleep(0.2)
            rect = snapshot_rect(TITLE)
            if rect:
                break
        report("test window mirrored", rect is not None, str(rect))
        if not rect:
            return
        time.sleep(1)

        reached = None
        for _ in range(3):
            p = body_point(0.3)
            move_to(*p)
            sh("ydotool", "mousemove", "-x", "1", "-y", "0")
            time.sleep(0.5)
            k = cursor()
            q = display.Display().screen().root.query_pointer()
            reached = abs(q.root_x - k[0]) <= 2 and abs(q.root_y - k[1]) <= 2
            if reached:
                break
        if not reached:
            report("pointer reaches the game", False,
                   f"KWin {k}, X {(q.root_x, q.root_y)}; try systemctl --user restart ydotool")
            return
        # the shape is not resampled while the button is down, so check where
        # she lands: on the window, its top edge crosses her
        seated = None
        for frac in (0.43, "tie", 0.4, 0.5):
            if frac == "tie":
                box = game_box()
                p = tie(screenshot("tie"), box) if box else None
            else:
                p = body_point(frac)
            if not p:
                continue
            move_to(*p)
            sh("ydotool", "mousemove", "-x", "1", "-y", "0")
            time.sleep(0.6)
            press()
            time.sleep(0.25)
            for _ in range(12):
                sh("ydotool", "mousemove", "-x", "0", "-y", "-10")
                time.sleep(0.05)
            move_to(rect[0] + rect[2] // 2, rect[1] - 30, step=20)
            time.sleep(0.5)
            release()
            time.sleep(4)
            box = game_box()
            screenshot(f"drop-{frac}")
            print(f"  drop {frac} grab {p} -> {box}", flush=True)
            seated = box and rect[0] - 80 < (box[0] + box[2]) // 2 < rect[0] + rect[2] + 80 \
                and box[1] < rect[1] < box[3] and box[3] - box[1] < 600
            if seated:
                break
        report("character sits on the test window", bool(seated), str(game_box()))
        if not seated:
            return
        # park the pointer on a Wayland window; over her the game turns its
        # click-through off and stops seeing the window it stands on
        move_to(max(rect[0] - 300, 10), rect[1] + rect[2], step=40)
        time.sleep(2)
        before = game_box()
        screenshot("seated")

        # move the window in steps, like a user drag
        for _ in range(10):
            kwin_run('var a = workspace.windowList(); for (var i = 0; i < a.length; ++i) {'
                     f' var w = a[i]; if ((w.caption || "").indexOf("{TITLE}") === 0) {{'
                     ' var g = w.frameGeometry; w.frameGeometry = {x: g.x + 20, y: g.y + 15,'
                     ' width: g.width, height: g.height}; } }', "selftest_move")
        time.sleep(3)
        screenshot("moved")
        after = game_box()
        # centres, since she may change pose while riding along
        moved = before and after and \
            abs((after[0] + after[2] - before[0] - before[2]) / 2 - 200) < 80 and \
            abs((after[1] + after[3] - before[1] - before[3]) / 2 - 150) < 80
        report("character follows a moved window", bool(moved), f"{before} -> {after}")

        wine_run("wbtest.exe", "min", TITLE)
        time.sleep(2)
        report("game-driven minimize reaches KWin", snapshot_rect(TITLE) is None)
    finally:
        dialog.kill()

    items = sh("qdbus6", "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
               "org.kde.StatusNotifierWatcher.RegisteredStatusNotifierItems").stdout.split()
    tray = None
    for item in items:
        svc, _, obj = item.partition("/")
        r = sh("gdbus", "call", "--session", "-d", svc, "-o", "/" + obj, "-m",
               "org.freedesktop.DBus.Properties.Get", "org.kde.StatusNotifierItem", "Id")
        if "wine-" in r.stdout:
            tray = (svc, "/" + obj)
    report("tray icon registered", tray is not None)
    if tray:
        sh("gdbus", "call", "--session", "-d", tray[0], "-o", tray[1], "-m",
           "org.kde.StatusNotifierItem.ContextMenu", "1700", "1000")
        time.sleep(10)
        closed = "closed game menu" in open(os.path.join(RUN, "ghost.log"), encoding="utf-8", errors="replace").read()
        report("tray menu opened and was closed again", closed)


if __name__ == "__main__":
    try:
        main()
    finally:
        print(f"\n{results.count(True)} passed, {results.count(False)} failed (screenshots in {TMP})")
    sys.exit(0 if results and all(results) else 1)
