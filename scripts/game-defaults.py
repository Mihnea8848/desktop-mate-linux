#!/usr/bin/env python3
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sets the game's anti-aliasing to MSAA (HQ) once per install; the other modes
# look poor under Proton. Run before the game starts (wineserver stopped), so
# editing user.reg is safe. Later changes in the game are kept: a marker in the
# config folder records that it was done. Nothing happens until the game has
# saved its settings once.
# Prefix: $DM_PREFIX, else $STEAM_COMPAT_DATA_PATH/pfx, else the default Steam library.
import json
import os
import re
import sys

MSAA_HQ = 0
MARKER = "defaults-applied"
VALUE = re.compile(r'("UserSetting_h1363870658"=hex:)((?:[0-9a-f,]*\\\n\s*)*[0-9a-f,]*)')


def prefix_dir() -> str:
    if os.environ.get("DM_PREFIX"):
        return os.environ["DM_PREFIX"]
    if os.environ.get("STEAM_COMPAT_DATA_PATH"):
        return os.path.join(os.environ["STEAM_COMPAT_DATA_PATH"], "pfx")
    return os.path.expanduser("~/.local/share/Steam/steamapps/compatdata/3301060/pfx")


def encode(data: bytes, prefix: str) -> str:
    """hex value wrapped the way Wine writes it"""
    out, line = [], prefix
    for b in data:
        piece = f"{b:02x},"
        if len(line) + len(piece) > 79:
            out.append(line + "\\")
            line = "  "
        line += piece
    out.append(line.rstrip(","))
    return "\n".join(out)


def main() -> int:
    config = os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"), "desktop-mate-linux")
    marker = os.path.join(config, MARKER)
    if os.path.exists(marker):
        return 0
    path = os.path.join(prefix_dir(), "user.reg")
    try:
        text = open(path, encoding="utf-8", errors="surrogateescape").read()
    except OSError:
        return 0
    m = VALUE.search(text)
    if not m:
        return 0
    raw = bytes.fromhex(re.sub(r"[^0-9a-f]", "", m.group(2)))
    nul = raw.endswith(b"\0")
    try:
        settings = json.loads(raw.rstrip(b"\0").decode("utf-8"))
    except ValueError:
        return 0
    if settings.get("AntiAliasingType") != MSAA_HQ:
        settings["AntiAliasingType"] = MSAA_HQ
        data = json.dumps(settings, separators=(",", ":"), ensure_ascii=False).encode("utf-8") + (b"\0" if nul else b"")
        text = text[:m.start()] + encode(data, m.group(1)) + text[m.end():]
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write(text)
        os.replace(tmp, path)
        print("game-defaults: anti-aliasing set to MSAA (HQ)")
    os.makedirs(config, exist_ok=True)
    with open(marker, "w", encoding="utf-8") as f:
        f.write("anti-aliasing\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
