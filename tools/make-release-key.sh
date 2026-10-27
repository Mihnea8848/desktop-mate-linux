#!/bin/sh
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# make-release-key.sh [--keep FILE]: for the maintainer. Creates the key that
# signs releases and the pacman repository, writes the public key to
# packaging/keys/release-signing-key.asc and stores the private key in the
# repository's Actions secrets (RELEASE_SIGNING_KEY, RELEASE_SIGNING_FPR).
# The private key is not kept on this machine unless --keep FILE is given.
set -eu

DIR="$(dirname "$(dirname "$(readlink -f "$0")")")"
REPO=Mihnea8848/desktop-mate-linux
KEEP=''
[ "${1:-}" = --keep ] && KEEP=${2:?--keep needs a file}

GNUPGHOME=$(mktemp -d)
export GNUPGHOME
trap 'rm -rf "$GNUPGHOME"' EXIT

gpg --batch --quiet --passphrase '' --quick-gen-key \
    'desktop-mate-linux release signing <59713373+Mihnea8848@users.noreply.github.com>' ed25519 sign never
fpr=$(gpg --list-secret-keys --with-colons | awk -F: '/^fpr/ { print $10; exit }')

mkdir -p "$DIR/packaging/keys"
gpg --armor --export "$fpr" >"$DIR/packaging/keys/release-signing-key.asc"
gpg --armor --export-secret-keys "$fpr" | gh secret set RELEASE_SIGNING_KEY --repo "$REPO"
printf '%s' "$fpr" | gh secret set RELEASE_SIGNING_FPR --repo "$REPO"
if [ -n "$KEEP" ]; then
    (umask 077 && gpg --armor --export-secret-keys "$fpr" >"$KEEP")
    echo "private key copy: $KEEP"
fi

echo "fingerprint: $fpr"
echo "public key:  packaging/keys/release-signing-key.asc (commit it)"
