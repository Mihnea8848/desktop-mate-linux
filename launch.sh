#!/bin/sh
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Steam launch options: /path/to/desktop-mate-linux/launch.sh %command%
# Settings: environment variables or local.conf, see local.conf.example.

DIR="$(dirname "$(readlink -f "$0")")"
# shellcheck source=/dev/null
[ -f "$DIR/local.conf" ] && . "$DIR/local.conf"

RUN=/dev/shm/ghost-bridge
LOG=$RUN/launch.log
mkdir -p "$RUN/cmd"
rm -f "$RUN"/cmd/* "$RUN/windows.tsv"
echo "launch $(date '+%F %T')" >"$LOG"

python3 -I "$DIR/scripts/ensure-prefix.py" >>"$LOG" 2>&1

# GE-Proton layered overlay patch: ARGB window, click-through shape from alpha
export WINE_LAYERED_OVERLAY_ALPHA=1
export WINE_LAYERED_OVERLAY_SHAPE_INTERVAL="${DM_SHAPE_INTERVAL:-100}"

FPS=${DM_FPS:-60}
if [ "$FPS" != 0 ]; then
    export DXVK_FRAME_RATE="$FPS" VKD3D_FRAME_RATE="$FPS"
fi

if [ -n "$DM_PROTON" ] && [ -x "$DM_PROTON/proton" ]; then
    n=$#
    while [ $n -gt 0 ]; do
        a=$1; shift; n=$((n - 1))
        case "$a" in */proton) a=$DM_PROTON/proton ;; esac
        set -- "$@" "$a"
    done
    echo "using $DM_PROTON" >>"$LOG"
fi

PROTON=
for a in "$@"; do
    case "$a" in */proton) PROTON=$a; break ;; esac
done

QDBUS=$(command -v qdbus6 || command -v qdbus-qt6 || command -v qdbus)

BRIDGE=1
if [ -n "$DM_NO_BRIDGE" ]; then
    BRIDGE=
elif [ ! -x "$DIR/bin/ghost-bridged" ] || [ ! -f "$DIR/bin/ghost.exe" ]; then
    echo "bridge not built, run build.sh; starting without it" >>"$LOG"
    BRIDGE=
elif [ -z "$QDBUS" ] || [ -z "$PROTON" ]; then
    echo "qdbus or proton not found; starting without the bridge" >>"$LOG"
    BRIDGE=
fi

kwin_script() {
    method=$1
    shift
    "$QDBUS" org.kde.KWin /Scripting "org.kde.kwin.Scripting.$method" "$@"
}

DPID='' WPID=''
if [ -n "$BRIDGE" ]; then
    "$DIR/bin/ghost-bridged" >>"$LOG" 2>&1 &
    DPID=$!

    kwin_script unloadScript ghostbridge >/dev/null 2>&1
    kwin_script loadScript "$DIR/kwin/ghost-bridge.js" ghostbridge >>"$LOG" 2>&1
    kwin_script start >>"$LOG" 2>&1

    # ghost.exe needs the game's wineserver; it exits when the game exits
    (
        i=0
        until pgrep -f '^[A-Z]:.*\\DesktopMate\.exe' >/dev/null; do
            i=$((i + 1))
            [ $i -gt 120 ] && exit 0
            sleep 1
        done
        sleep 3
        echo "starting ghost.exe via $PROTON"
        WINE_LAYERED_OVERLAY_ALPHA='' WINE_LAYERED_OVERLAY_SHAPE_INTERVAL='' \
        DXVK_FRAME_RATE='' VKD3D_FRAME_RATE='' WINEDEBUG=-all \
            python3 "$PROTON" run "$DIR/bin/ghost.exe" </dev/null
        echo "ghost.exe ended"
    ) >>"$LOG" 2>&1 &
    WPID=$!
fi

"$@"
RC=$?

if [ -n "$BRIDGE" ]; then
    kwin_script unloadScript ghostbridge >/dev/null 2>&1
    kill "$DPID" "$WPID" 2>/dev/null
fi
exit $RC
