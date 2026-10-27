#!/bin/sh
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Steam launch options: /path/to/desktop-mate-linux/launch.sh %command%
# Settings: environment variables, local.conf next to this file or
# ~/.config/desktop-mate-linux/local.conf (read last), see local.conf.example.
#
# Everything runs inside main(), so the shell has read the whole file before
# the game starts and the file can be replaced while the game runs.

RUN=/dev/shm/ghost-bridge
LOG=$RUN/launch.log
GAME_RE='^[A-Z]:.*\\DesktopMate\.exe'
GHOST_RE='^[A-Z]:.*\\ghost\.exe'

log() { echo "$*" >>"$LOG"; }

env_summary() {
    log "session: ${XDG_SESSION_TYPE:-?} ${XDG_CURRENT_DESKTOP:-?}"
    command -v plasmashell >/dev/null && log "$(env -u LD_LIBRARY_PATH -u LD_PRELOAD plasmashell --version 2>&1 | head -n 1)"
    command -v nvidia-smi >/dev/null &&
        log "gpu: $(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null | head -n 1)"
    log "xwayland scale: $XSCALE"
    [ -f /.flatpak-info ] && log "running inside Flatpak: the bridge needs qdbus and /dev/shm from the host"
}

kwin_script() {
    method=$1
    shift
    "$QDBUS" org.kde.KWin /Scripting "org.kde.kwin.Scripting.$method" "$@"
}

game_running() { pgrep -f "$GAME_RE" >/dev/null; }
ghost_running() { pgrep -f "$GHOST_RE" >/dev/null; }

stop_tree() {
    for c in $(pgrep -P "$1"); do
        stop_tree "$c"
    done
    kill "$1" 2>/dev/null
}

# Starts ghost.exe once the game runs and restarts it (up to 3 times) when it
# disappears while the game still runs. The ghost.exe process itself is
# watched: Proton's "run" wrapper does not always exit when ghost.exe dies.
supervise_ghost() {
    i=0
    until game_running; do
        i=$((i + 1))
        [ $i -gt 120 ] && return
        sleep 1
    done
    sleep 3
    tries=0
    while game_running && [ $tries -lt 4 ]; do
        tries=$((tries + 1))
        echo "starting ghost.exe via $PROTON (try $tries)"
        WINE_LAYERED_OVERLAY_ALPHA='' WINE_LAYERED_OVERLAY_SHAPE_INTERVAL='' \
        DXVK_FRAME_RATE='' VKD3D_FRAME_RATE='' WINEDEBUG=-all GHOST_SCALE="$XSCALE" \
        GHOST_GAME_TRANSPARENT="${DM_GAME_TRANSPARENT:-1}" \
            python3 "$PROTON" run "$DIR/bin/ghost.exe" </dev/null &
        runner=$!
        seen=0 missing=0
        while game_running; do
            sleep 2
            if ghost_running; then
                seen=1 missing=0
            else
                missing=$((missing + 1))
            fi
            [ $seen = 1 ] && [ $missing -ge 2 ] && break
            [ $seen = 0 ] && [ $missing -ge 15 ] && break
        done
        stop_tree "$runner"
        echo "ghost.exe ended"
    done
}

# Any "proton run" without the game's Steam environment (a tool, protontricks)
# deletes the prefix's s: drive the game runs from. The game then fails to read
# its own files, treats that as tampering and unloads the character, which
# looks like a frozen image. Put the link back when it disappears.
keep_game_drive() {
    link=$STEAM_COMPAT_DATA_PATH/pfx/dosdevices/s:
    i=0
    until game_running && [ -L "$link" ]; do
        i=$((i + 1))
        [ $i -gt 120 ] && return
        sleep 1
    done
    target=$(readlink "$link")
    while game_running; do
        if [ ! -L "$link" ]; then
            ln -s "$target" "$link" && log "s: drive was removed, restored"
        fi
        sleep 2
    done
}

# shellcheck disable=SC2329 # called by the EXIT trap
cleanup() {
    trap - EXIT INT TERM HUP
    if [ -n "$BRIDGE" ]; then
        kwin_script unloadScript ghostbridge >/dev/null 2>&1
        [ -n "$WPID" ] && kill "$WPID" 2>/dev/null
        [ -n "$DPID" ] && kill "$DPID" 2>/dev/null
    fi
    [ -n "$KPID" ] && kill "$KPID" 2>/dev/null
    python3 -I "$DIR/scripts/sync-autostart.py" >>"$LOG" 2>&1
    log "exit $(date '+%F %T')"
}

main() {
    DIR="$(dirname "$(readlink -f "$0")")"
    CONF=${XDG_CONFIG_HOME:-$HOME/.config}/desktop-mate-linux/local.conf
    # shellcheck source=/dev/null
    [ -f "$DIR/local.conf" ] && . "$DIR/local.conf"
    # shellcheck source=/dev/null
    [ -f "$CONF" ] && . "$CONF"

    mkdir -p "$RUN/cmd"
    rm -f "$RUN"/cmd/* "$RUN/windows.tsv" "$RUN/cursor.txt"
    echo "launch $(date '+%F %T')" >"$LOG"

    XSCALE=$(kreadconfig6 --file kwinrc --group Xwayland --key Scale 2>/dev/null)
    XSCALE=${XSCALE:-1}
    env_summary

    python3 -I "$DIR/scripts/ensure-prefix.py" >>"$LOG" 2>&1
    python3 -I "$DIR/scripts/sync-autostart.py" >>"$LOG" 2>&1

    # GE-Proton layered overlay patch: ARGB window, click-through shape from alpha
    export WINE_LAYERED_OVERLAY_ALPHA=1
    export WINE_LAYERED_OVERLAY_SHAPE_INTERVAL="${DM_SHAPE_INTERVAL:-200}"
    # builds from tools/build-ge-proton.sh: present straight into the window
    # instead of copying each frame (stock GE-Proton ignores it)
    export WINE_LAYERED_OVERLAY_DIRECT="${DM_DIRECT:-1}"

    # the game limits itself to 60 FPS; DXVK's limiter costs CPU, so only use
    # it for a lower cap
    if [ -n "$DM_FPS" ] && [ "$DM_FPS" != 0 ]; then
        export DXVK_FRAME_RATE="$DM_FPS" VKD3D_FRAME_RATE="$DM_FPS"
    fi

    if [ -n "$DM_PROTON" ] && [ -x "$DM_PROTON/proton" ]; then
        n=$#
        while [ $n -gt 0 ]; do
            a=$1
            shift
            n=$((n - 1))
            case "$a" in */proton) a=$DM_PROTON/proton ;; esac
            set -- "$@" "$a"
        done
        log "using $DM_PROTON"
    fi

    # Unity's own log (the game turns it off in boot.config)
    if [ -n "$DM_UNITY_LOG" ] && [ "$DM_UNITY_LOG" != 0 ]; then
        set -- "$@" -logFile 'Z:\dev\shm\ghost-bridge\unity.log'
        log "unity log: $RUN/unity.log"
    fi

    PROTON=''
    for a in "$@"; do
        case "$a" in */proton) PROTON=$a; break ;; esac
    done
    log "proton: ${PROTON:-none}"

    QDBUS=$(command -v qdbus6 || command -v qdbus-qt6 || command -v qdbus)

    BRIDGE=1
    if [ -n "$DM_NO_BRIDGE" ]; then
        BRIDGE=''
    elif [ ! -x "$DIR/bin/ghost-bridged" ] || [ ! -f "$DIR/bin/ghost.exe" ]; then
        log "bridge not built, run build.sh; starting without it"
        BRIDGE=''
    elif [ -z "$QDBUS" ] || [ -z "$PROTON" ]; then
        log "qdbus or proton not found; starting without the bridge"
        BRIDGE=''
    fi

    DPID='' WPID='' KPID=''
    trap cleanup EXIT
    trap 'exit 143' INT TERM HUP

    if [ -n "$STEAM_COMPAT_DATA_PATH" ]; then
        keep_game_drive &
        KPID=$!
    fi

    if [ -n "$BRIDGE" ]; then
        "$DIR/bin/ghost-bridged" >>"$LOG" 2>&1 &
        DPID=$!
        kwin_script unloadScript ghostbridge >/dev/null 2>&1
        kwin_script loadScript "$DIR/kwin/ghost-bridge.js" ghostbridge >/dev/null 2>>"$LOG"
        kwin_script start >/dev/null 2>>"$LOG"
        supervise_ghost >>"$LOG" 2>&1 &
        WPID=$!
    fi

    "$@"
    return $?
}

main "$@"
exit
