#!/bin/sh
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# make-dist.sh TAG: release tarball and SHA256SUMS in dist/, from a tree that
# build.sh has built. Used by the release workflow and by CI.
set -eu
cd "$(dirname "$(dirname "$(readlink -f "$0")")")"
tag=$1
name=desktop-mate-linux-$tag

rm -rf "dist/$name"
mkdir -p "dist/$name/bin"
cp -r bin/ghost.exe bin/ghost-bridged bin/dmhook.dll bin/tools "dist/$name/bin/"
cp -r launch.sh install.sh get.sh desktop-mate-linux kwin patches scripts tools local.conf.example \
    README.md CHANGELOG.md LICENSE "dist/$name/"
find "dist/$name" -name __pycache__ -prune -exec rm -rf {} +
echo "$tag" >"dist/$name/VERSION"
tar -C dist -czf "dist/$name.tar.gz" "$name"
(cd dist && sha256sum "$name.tar.gz" >SHA256SUMS)
