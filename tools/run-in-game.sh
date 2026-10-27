#!/bin/sh
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# run-in-game.sh PROGRAM [ARGS]: runs a Windows program in the running game's
# Wine session, with the Steam environment of the game's launch.sh. Without it
# Proton deletes the game's s: drive (see keep_game_drive in launch.sh).
pid=$(pgrep -f 'launch\.sh .*steam-launch-wrapper' | head -n 1)
[ -n "$pid" ] || { echo "the game is not running through launch.sh" >&2; exit 1; }
proton=$(sed -n 's/^proton: //p' /dev/shm/ghost-bridge/launch.log)
prog=$(readlink -f "$1")
shift
env_of() { tr '\0' '\n' < "/proc/$pid/environ" | sed -n "s/^$1=//p"; }
STEAM_COMPAT_DATA_PATH=$(env_of STEAM_COMPAT_DATA_PATH) \
STEAM_COMPAT_CLIENT_INSTALL_PATH=$(env_of STEAM_COMPAT_CLIENT_INSTALL_PATH) \
STEAM_COMPAT_INSTALL_PATH=$(env_of STEAM_COMPAT_INSTALL_PATH) \
STEAM_COMPAT_LIBRARY_PATHS=$(env_of STEAM_COMPAT_LIBRARY_PATHS) \
WINEDEBUG=-all exec python3 "$proton" run "$prog" "$@"
