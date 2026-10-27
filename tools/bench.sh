#!/bin/sh
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# bench.sh [label] [seconds]: CPU of the game and the desktop processes, GPU
# load and game FPS while the game runs. Prints one Markdown table row.
LABEL=${1:-run}
SECS=${2:-10}
DIR="$(dirname "$(readlink -f "$0")")"

pid_of() { pgrep -f "$1" | head -n 1; }

GAME=$(pid_of '^[A-Z]:.*\\DesktopMate\.exe')
[ -n "$GAME" ] || { echo "game not running" >&2; exit 1; }
XW=$(pgrep -x Xwayland | head -n 1)
KW=$(pgrep -o -x kwin_wayland)
WS=$(pid_of 'bin/wineserver')

# second sample of top -d covers the whole interval
cpu() {
    top -b -n 2 -d "$SECS" -p "$1" | awk -v p="$1" '$1 == p { v = $9 } END { gsub(",", ".", v); print v }'
}

T=$(mktemp -d)
cpu "$GAME" >"$T/game" &
cpu "$XW" >"$T/xw" &
cpu "$KW" >"$T/kw" &
[ -n "$WS" ] && cpu "$WS" >"$T/ws" &
FPS=$(DISPLAY=${DISPLAY:-:0} python3 -I "$DIR/xwin.py" fps "$SECS" 2>/dev/null || echo "?")
wait
GPU="?"
command -v nvidia-smi >/dev/null && GPU="$(nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits | head -n 1)%"

printf '| %s | %s | %s%% | %s%% | %s%% | %s%% | %s |\n' "$LABEL" "$FPS" \
    "$(cat "$T/game")" "$(cat "$T/xw")" "$(cat "$T/kw")" "$(cat "$T/ws" 2>/dev/null || echo "?")" "$GPU"
rm -rf "$T"
