#!/usr/bin/env python3
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: MIT
#
# Sets Managed=N for DesktopMate.exe and ghost.exe in the game's Wine prefix.
# Run before the game starts (wineserver stopped), so editing user.reg is safe.
# Prefix: $DM_PREFIX, else $STEAM_COMPAT_DATA_PATH/pfx, else the default Steam library.
import os
import sys
import time

APPS = ("DesktopMate.exe", "ghost.exe")


def prefix_dir() -> str:
    if os.environ.get("DM_PREFIX"):
        return os.environ["DM_PREFIX"]
    if os.environ.get("STEAM_COMPAT_DATA_PATH"):
        return os.path.join(os.environ["STEAM_COMPAT_DATA_PATH"], "pfx")
    return os.path.expanduser("~/.local/share/Steam/steamapps/compatdata/3301060/pfx")


def ensure(text: str, app: str) -> tuple[str, bool]:
    header = f"[Software\\\\Wine\\\\AppDefaults\\\\{app}\\\\X11 Driver]"
    start = text.find(header)
    if start == -1:
        if not text.endswith("\n\n"):
            text += "\n" if text.endswith("\n") else "\n\n"
        return text + f'{header} {int(time.time())}\n"Managed"="N"\n\n', True

    end = text.find("\n[", start + 1)
    if '"Managed"="N"' in text[start:end if end != -1 else len(text)]:
        return text, False

    # insert after the header line and its optional #time= line
    pos = text.find("\n", start) + 1
    if text.startswith("#time=", pos):
        pos = text.find("\n", pos) + 1
    return text[:pos] + '"Managed"="N"\n' + text[pos:], True


def main() -> int:
    user_reg = os.path.join(prefix_dir(), "user.reg")
    if not os.path.exists(user_reg):
        print(f"ensure-prefix: {user_reg} not found, skipping")
        return 0
    with open(user_reg, encoding="utf-8", errors="surrogateescape") as f:
        text = f.read()

    changed = []
    for app in APPS:
        text, did = ensure(text, app)
        if did:
            changed.append(app)
    if not changed:
        print("ensure-prefix: ok")
        return 0

    tmp = user_reg + ".dm-tmp"
    with open(tmp, "w", encoding="utf-8", errors="surrogateescape") as f:
        f.write(text)
    os.replace(tmp, user_reg)
    print("ensure-prefix: set Managed=N for " + ", ".join(changed))
    return 0


if __name__ == "__main__":
    sys.exit(main())
