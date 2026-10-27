#!/bin/sh
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# report.sh: prints system details and the bridge logs, for bug reports.
RUN=/dev/shm/ghost-bridge

section() { printf '\n## %s\n' "$1"; }

section system
grep -E '^(PRETTY_NAME|VERSION_ID)=' /etc/os-release 2>/dev/null
uname -r
echo "session: ${XDG_SESSION_TYPE:-unknown} ${XDG_CURRENT_DESKTOP:-}"
command -v plasmashell >/dev/null && plasmashell --version 2>/dev/null
command -v kwin_wayland >/dev/null && kwin_wayland --version 2>/dev/null
command -v Xwayland >/dev/null && Xwayland -version 2>&1 | head -n 1
[ -f "$HOME/.config/kwinrc" ] && grep -A2 '^\[Xwayland\]' "$HOME/.config/kwinrc"
[ -f /.flatpak-info ] && echo "running inside Flatpak"

section gpu
if command -v nvidia-smi >/dev/null; then
    nvidia-smi --query-gpu=name,driver_version --format=csv,noheader
fi
command -v glxinfo >/dev/null && glxinfo -B 2>/dev/null | grep -E 'renderer string|version string'

section proton
for f in "$HOME"/.local/share/Steam/compatibilitytools.d/*/version; do
    [ -f "$f" ] && echo "$(basename "$(dirname "$f")"): $(cat "$f")"
done

for f in launch.log ghost.log; do
    section "$f"
    tail -n 200 "$RUN/$f" 2>/dev/null || echo "(missing)"
done
