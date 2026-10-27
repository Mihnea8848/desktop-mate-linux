#!/usr/bin/env python3
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sets Desktop Mate's launch options and compatibility tool in Steam's config.
# Steam must be closed: it rewrites these files when it exits.
#   steam-setup.py --launch /path/to/launch.sh [--tool NAME] [--steam DIR]
#   steam-setup.py --remove [--steam DIR]
# Setting up prints the game's Wine prefix on the last line ("prefix: ...").
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

APP = "3301060"
MIN_GE = (11, 7)
TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|([{}])|//[^\n]*|\s+')


# Steam's text KeyValues. Strings stay escaped as in the file, so unchanged
# values are written back byte for byte.
def parse(text):
    root, stack, key = [], [], None
    cur = root
    for m in TOKEN.finditer(text):
        s, brace = m.group(1), m.group(2)
        if brace == "{":
            node = []
            cur.append([key, node])
            stack.append(cur)
            cur, key = node, None
        elif brace == "}":
            cur = stack.pop()
        elif s is not None:
            if key is None:
                key = s
            else:
                cur.append([key, s])
                key = None
    return root


def dump(node, depth=0):
    out = []
    for k, v in node:
        tabs = "\t" * depth
        if isinstance(v, list):
            out.append(f'{tabs}"{k}"\n{tabs}{{\n{dump(v, depth + 1)}{tabs}}}\n')
        else:
            out.append(f'{tabs}"{k}"\t\t"{v}"\n')
    return "".join(out)


def child(node, key, create=True):
    for k, v in node:
        if k.lower() == key.lower() and isinstance(v, list):
            return v
    if not create:
        return None
    v = []
    node.append([key, v])
    return v


def set_value(node, key, value):
    for item in node:
        if item[0].lower() == key.lower():
            item[1] = value
            return
    node.append([key, value])


def path(node, keys, create=True):
    for k in keys:
        node = child(node, k, create)
        if node is None:
            return None
    return node


def escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def write(file, tree):
    shutil.copy2(file, file + ".bak")
    tmp = file + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(dump(tree))
    os.replace(tmp, file)


def ge_version(tool_dir):
    try:
        text = open(os.path.join(tool_dir, "version"), encoding="utf-8").read()
    except OSError:
        return None
    m = re.search(r"GE-Proton(\d+)-(\d+)", text)
    return (int(m.group(1)), int(m.group(2))) if m else None


def tool_name(tool_dir):
    try:
        text = open(os.path.join(tool_dir, "compatibilitytool.vdf"), encoding="utf-8").read()
    except OSError:
        return None
    tools = path(parse(text), ["compatibilitytools", "compat_tools"], create=False)
    return tools[0][0] if tools else None


def pick_tool(steam):
    """Newest GE-Proton 11-7+; a ProtonPlus "Latest" build wins a tie, it updates itself."""
    best = None
    for d in glob.glob(os.path.join(steam, "compatibilitytools.d", "*")):
        v, name = ge_version(d), tool_name(d)
        if not v or not name or v < MIN_GE:
            continue
        rank = (v, "latest" in name.lower())
        if not best or rank > best[0]:
            best = (rank, name)
    return best[1] if best else None


def game_library(steam):
    libs = [steam]
    try:
        text = open(os.path.join(steam, "steamapps", "libraryfolders.vdf"), encoding="utf-8").read()
        for _, lib in path(parse(text), ["libraryfolders"], create=False) or []:
            if isinstance(lib, list):
                libs += [v.replace("\\\\", "\\") for k, v in lib if k == "path"]
    except OSError:
        pass
    for lib in libs:
        if os.path.exists(os.path.join(lib, "steamapps", f"appmanifest_{APP}.acf")):
            return lib
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--launch")
    ap.add_argument("--tool")
    ap.add_argument("--remove", action="store_true")
    ap.add_argument("--steam", default=os.path.expanduser("~/.local/share/Steam"))
    a = ap.parse_args()
    if not a.remove and not a.launch:
        ap.error("--launch or --remove is required")

    steam = os.path.realpath(a.steam)
    if not os.path.isdir(os.path.join(steam, "userdata")):
        sys.exit(f"no Steam installation in {steam}")
    if subprocess.run(["pgrep", "-x", "steam"], capture_output=True).returncode == 0:
        sys.exit("Steam is running. Quit it first (Steam menu, Exit), then run this again.")
    lib = game_library(steam)
    if not lib:
        sys.exit("Desktop Mate is not installed in any Steam library")

    configs = glob.glob(os.path.join(steam, "userdata", "*", "config", "localconfig.vdf"))
    for file in configs:
        tree = parse(open(file, encoding="utf-8").read())
        apps = path(tree, ["UserLocalConfigStore", "Software", "Valve", "Steam", "apps"])
        if a.remove:
            game = child(apps, APP, create=False)
            if game and any(k == "LaunchOptions" and "launch.sh" in v for k, v in game):
                game[:] = [kv for kv in game if kv[0] != "LaunchOptions"]
                write(file, tree)
                print(f"launch options removed: {file}")
            continue
        launch = a.launch if " " not in a.launch else f'"{a.launch}"'
        set_value(child(apps, APP), "LaunchOptions", escape(f"{launch} %command%"))
        write(file, tree)
        print(f"launch options set: {file}")

    if not a.remove:
        tool = a.tool or pick_tool(steam)
        if not tool:
            sys.exit("no GE-Proton 11-7 or newer in compatibilitytools.d; install one with ProtonPlus "
                     "or ProtonUp-Qt and run this again")
        file = os.path.join(steam, "config", "config.vdf")
        tree = parse(open(file, encoding="utf-8").read())
        mapping = path(tree, ["InstallConfigStore", "Software", "Valve", "Steam", "CompatToolMapping"])
        entry = child(mapping, APP)
        entry[:] = [["name", escape(tool)], ["config", ""], ["priority", "250"]]
        write(file, tree)
        print(f"compatibility tool: {tool}")
        print(f"prefix: {os.path.join(lib, 'steamapps', 'compatdata', APP, 'pfx')}")


if __name__ == "__main__":
    main()
