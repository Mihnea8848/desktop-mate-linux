#!/bin/sh
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# build-ge-proton.sh [GE tool dir]: rebuilds winex11.so of an installed GE-Proton
# with the patches in patches/ge-proton/<version> and installs the result as a
# new compatibility tool "<version>-desktopmate" next to it.
# Default tool: compatibilitytools.d/Proton-GE Latest. Needs git, gcc, make,
# flex, bison and the X11 development headers; about 1 GB in $GE_BUILD_DIR
# (default ~/.cache/desktop-mate-linux/ge-build) and 10 to 15 minutes.
set -eu

DIR="$(dirname "$(dirname "$(readlink -f "$0")")")"
TOOLS=${STEAM_DIR:-$HOME/.local/share/Steam}/compatibilitytools.d
SRC=${1:-$TOOLS/Proton-GE Latest}
VER=$(sed -n 's/.*\(GE-Proton[0-9]*-[0-9]*\).*/\1/p' "$SRC/version")
PATCHES=$DIR/patches/ge-proton/$VER
B=${GE_BUILD_DIR:-$HOME/.cache/desktop-mate-linux/ge-build}/$VER
DEST=$TOOLS/$VER-desktopmate

[ -n "$VER" ] || { echo "no GE-Proton version in $SRC/version" >&2; exit 1; }
[ -d "$PATCHES" ] || { echo "no patches for $VER; supported: $(ls "$DIR/patches/ge-proton")" >&2; exit 1; }

if [ ! -f "$B/build64/dlls/winex11.drv/winex11.so" ]; then
    rm -rf "$B"
    git clone -q --depth 1 --branch "$VER" --filter=blob:none --sparse \
        https://github.com/GloriousEggroll/proton-ge-custom.git "$B"
    cd "$B"
    git sparse-checkout set patches wineopenxr
    for m in wine wine-staging; do
        sha=$(git ls-tree HEAD "$m" | awk '{print $3}')
        git init -q "$m"
        git -C "$m" remote add origin "$(git config -f .gitmodules "submodule.$m.url")"
        git -C "$m" fetch -q --depth 1 origin "$sha"
        git -C "$m" checkout -q FETCH_HEAD
    done
    # commits GE's prep script reverts; reverting needs them and their parents
    if [ -f "$PATCHES/wine-extra-commits" ]; then
        while read -r c; do git -C wine fetch -q --depth 2 origin "$c"; done < "$PATCHES/wine-extra-commits"
    fi
    cp "$PATCHES/layered-overlay-wine.patch" patches/game-patches/

    # only the wine part of GE's prep script
    { sed -n '1,14p' patches/protonprep-valve-staging.sh
      sed -n '/^### (2) WINE PATCHING ###/,/^### END WINE PATCHING ###/p' patches/protonprep-valve-staging.sh
    } > prep-wine.sh
    bash prep-wine.sh > prep-wine.log 2>&1 || true
    if grep -qE 'FAILED|saving rejects|malformed' prep-wine.log; then
        echo "GE's patches did not apply, see $B/prep-wine.log" >&2
        exit 1
    fi
    for p in "$PATCHES"/*.patch; do
        [ "$(basename "$p")" = layered-overlay-wine.patch ] && continue
        patch -d wine -p1 -s < "$p"
    done
    (cd wine && XDG_CACHE_HOME="$B/cache" dlls/winevulkan/make_vulkan -x vk.xml -X video.xml > /dev/null)
    (cd wine && tools/make_specfiles > /dev/null) || true

    mkdir -p build64 && cd build64
    ../wine/configure --enable-win64 --without-mingw --disable-tests --with-wayland \
        CFLAGS="-O2 -Wno-discarded-qualifiers" > configure.log 2>&1
    for d in HAVE_LIBXSHAPE HAVE_LIBXXSHM SONAME_LIBXCOMPOSITE; do
        grep -q "define $d" include/config.h || { echo "configure did not find $d" >&2; exit 1; }
    done
    make -j"$(nproc)" dlls/winex11.drv/winex11.so > make.log 2>&1 ||
        { echo "build failed, see $B/build64/make.log" >&2; exit 1; }
fi

rm -rf "$DEST"
cp -a --reflink=auto "$SRC" "$DEST"
lib=$DEST/files/lib/wine/x86_64-unix
chmod u+w "$lib" "$lib/winex11.so"
strip -o "$lib/winex11.so" "$B/build64/dlls/winex11.drv/winex11.so"
sed -i "s/\"[^\"]*\" *\/\/ Internal name of this tool/\"$VER-desktopmate\" \/\/ Internal name of this tool/; \
        s/\"display_name\" *\"[^\"]*\"/\"display_name\" \"$VER-desktopmate\"/" "$DEST/compatibilitytool.vdf"
echo "installed $DEST"
echo "Restart Steam and select $VER-desktopmate for Desktop Mate (or run install.sh)."
