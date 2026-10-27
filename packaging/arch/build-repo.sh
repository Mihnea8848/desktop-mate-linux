#!/bin/sh
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# build-repo.sh TAG FINGERPRINT [DIR]: builds the Arch package from
# dist/desktop-mate-linux-TAG.tar.gz, signs it and adds it to the pacman
# repository in DIR (default build/repo), updating a database already there.
# Run as root in an Arch container; makepkg runs as the user "builder".
# Used by the release workflow and by CI.
set -eu
cd "$(dirname "$(dirname "$(dirname "$(readlink -f "$0")")")")"
tag=$1
fpr=$2
repo=${3:-build/repo}
tarball=desktop-mate-linux-$tag.tar.gz

id builder >/dev/null 2>&1 || useradd -m builder
rm -rf build/pkg
mkdir -p build/pkg "$repo"
cp packaging/arch/PKGBUILD packaging/arch/desktop-mate-linux.install "dist/$tarball" build/pkg/
sum=$(sha256sum "dist/$tarball" | cut -d' ' -f1)
sed -i "s/^pkgver=.*/pkgver=${tag#v}/; s/^pkgrel=.*/pkgrel=1/; s/^sha256sums=.*/sha256sums=('$sum')/" build/pkg/PKGBUILD
chown -R builder build/pkg
su builder -c "cd build/pkg && makepkg --nodeps --noconfirm"
pkg=$(cd build/pkg && ls desktop-mate-linux-*.pkg.tar.zst)
mv "build/pkg/$pkg" "$repo/"

cd "$repo"
gpg --batch --yes --local-user "$fpr" --detach-sign "$pkg"
repo-add --sign --key "$fpr" --remove desktop-mate-linux.db.tar.gz "$pkg"
# pacman reads desktop-mate-linux.db; release assets cannot be symlinks
for f in db files; do
    rm -f "desktop-mate-linux.$f" "desktop-mate-linux.$f.sig"
    cp "desktop-mate-linux.$f.tar.gz" "desktop-mate-linux.$f"
    cp "desktop-mate-linux.$f.tar.gz.sig" "desktop-mate-linux.$f.sig"
done
echo "$pkg"
