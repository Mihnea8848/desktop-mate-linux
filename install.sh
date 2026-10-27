#!/bin/sh
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sets up Desktop Mate in Steam to start through this copy of launch.sh.
#   ./install.sh [--tool NAME]   set launch options, compatibility tool, prefix
#   ./install.sh --remove        remove the launch options and autostart entry
# Steam must be closed. Settings: STEAM_DIR (default ~/.local/share/Steam).

DIR="$(dirname "$(readlink -f "$0")")"
STEAM=${STEAM_DIR:-$HOME/.local/share/Steam}

if [ ! -d "$STEAM/userdata" ]; then
    if [ -d "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/userdata" ]; then
        echo "Only Flatpak Steam was found, which this project does not support yet." >&2
    else
        echo "No Steam installation in $STEAM. Set STEAM_DIR if it is elsewhere." >&2
    fi
    exit 1
fi

if [ "$1" = --remove ]; then
    python3 -I "$DIR/scripts/steam-setup.py" --steam "$STEAM" --remove || exit 1
    f=${XDG_CONFIG_HOME:-$HOME/.config}/autostart/desktop-mate-linux.desktop
    grep -qs X-DesktopMateLinux=true "$f" && rm -f "$f" && echo "autostart entry removed"
    exit 0
fi

if [ ! -x "$DIR/bin/ghost-bridged" ] || [ ! -f "$DIR/bin/ghost.exe" ]; then
    if [ -x "$DIR/build.sh" ] && [ -d "$DIR/src" ]; then
        "$DIR/build.sh" || exit 1
    else
        echo "bin/ is missing. Download a release or run build.sh." >&2
        exit 1
    fi
fi

# installs from get.sh keep each version in versions/<tag>; Steam gets the
# "current" link, so updates never need setup again
LAUNCH=$DIR/launch.sh
case "$DIR" in
    */desktop-mate-linux/versions/*) LAUNCH=${DIR%/versions/*}/current/launch.sh ;;
esac

out=$(python3 -I "$DIR/scripts/steam-setup.py" --steam "$STEAM" --launch "$LAUNCH" "$@") || exit 1
echo "$out"
prefix=$(echo "$out" | sed -n 's/^prefix: //p')

if [ -f "$prefix/user.reg" ]; then
    DM_PREFIX=$prefix python3 -I "$DIR/scripts/ensure-prefix.py"
    echo "Done. Start Steam and then Desktop Mate."
else
    echo "Done. Start Steam, then start Desktop Mate twice: the first start creates"
    echo "the Wine prefix, and the window settings are written to it on the second."
fi
