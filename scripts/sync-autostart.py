#!/usr/bin/env python3
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Mirrors the game's "Launch at startup" setting to a desktop autostart entry.
# The game writes HKCU\Software\Microsoft\Windows\CurrentVersion\Run\DesktopMate,
# which does nothing on Linux. Run after the game exits (registry saved).
# Prefix: $DM_PREFIX, else $STEAM_COMPAT_DATA_PATH/pfx, else the default Steam library.
import os
import re
import sys

MARKER = "X-DesktopMateLinux=true"
ENTRY = f"""[Desktop Entry]
Type=Application
Name=Desktop Mate
Comment=Started because "Launch at startup" is on in Desktop Mate's settings
Exec=steam -silent steam://rungameid/3301060
Icon=steam
Terminal=false
{MARKER}
"""


def prefix_dir() -> str:
    if os.environ.get("DM_PREFIX"):
        return os.environ["DM_PREFIX"]
    if os.environ.get("STEAM_COMPAT_DATA_PATH"):
        return os.path.join(os.environ["STEAM_COMPAT_DATA_PATH"], "pfx")
    return os.path.expanduser("~/.local/share/Steam/steamapps/compatdata/3301060/pfx")


def startup_enabled(user_reg: str) -> bool:
    with open(user_reg, encoding="utf-8", errors="surrogateescape") as f:
        text = f.read()
    m = re.search(r"^\[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run\][^\n]*\n(.*?)(?=^\[|\Z)",
                  text, re.M | re.S)
    return bool(m and re.search(r'^"DesktopMate"=', m.group(1), re.M))


def main() -> int:
    user_reg = os.path.join(prefix_dir(), "user.reg")
    if not os.path.exists(user_reg):
        return 0
    config = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    path = os.path.join(config, "autostart", "desktop-mate-linux.desktop")
    ours = os.path.exists(path) and MARKER in open(path, encoding="utf-8").read()

    if startup_enabled(user_reg):
        if not ours or open(path, encoding="utf-8").read() != ENTRY:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as f:
                f.write(ENTRY)
            print(f"autostart: enabled ({path})")
    elif ours:
        os.remove(path)
        print("autostart: disabled")
    return 0


if __name__ == "__main__":
    sys.exit(main())
