#!/bin/sh
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: MIT
#
# Builds bin/ghost-bridged (Linux) and bin/ghost.exe plus the test tools (Windows).
# Needs gcc, systemd-libs, clang, lld, llvm-ar, mingw-w64-headers, mingw-w64-crt.
set -e
cd "$(dirname "$(readlink -f "$0")")"
mkdir -p bin/tools bin/stub

gcc -O2 -Wall -Wextra -o bin/ghost-bridged src/ghost-bridged.c -lsystemd

# empty libgcc stubs: clang links against libgcc names, chkstk.S provides the one symbol used
llvm-ar rcs bin/stub/libgcc.a
llvm-ar rcs bin/stub/libgcc_eh.a

win() {
    clang --target=x86_64-w64-mingw32 -fuse-ld=lld -O2 -Wall -mwindows -Lbin/stub \
        "$@" src/chkstk.S -luser32 -lgdi32
}
win -o bin/ghost.exe src/ghost.c
for t in enumtest fgprobe wbtest; do
    win -o "bin/tools/$t.exe" "tools/$t.c"
done
echo "built: bin/ghost-bridged bin/ghost.exe bin/tools/"
