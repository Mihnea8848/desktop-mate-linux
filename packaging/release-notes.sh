#!/bin/sh
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# release-notes.sh TAG: the CHANGELOG.md section for TAG, followed by how to
# update and verify. Fails when CHANGELOG.md has no section for TAG, so every
# release gets written notes.
set -eu
cd "$(dirname "$(dirname "$(readlink -f "$0")")")"
tag=$1

notes=$(awk -v t="## $tag" '
    $0 == t || index($0, t " ") == 1 { on = 1; next }
    on && /^## / { exit }
    on { print }
' CHANGELOG.md)
if [ -z "$(printf '%s' "$notes" | tr -d '[:space:]')" ]; then
    echo "CHANGELOG.md has no section \"## $tag\"" >&2
    exit 1
fi

printf '%s\n' "$notes" | sed -e '/./,$!d'
cat <<'END'

## Updating

- Arch-based systems with the pacman repository: `sudo pacman -Syu`.
- Tarball installs: extract the new tarball over the old folder, or into a new one and run `./desktop-mate-linux setup` again with Steam closed.

## Verifying the download

`SHA256SUMS` lists the tarball's checksum and `SHA256SUMS.asc` is its signature by the release key (`packaging/keys/release-signing-key.asc`, fingerprint `F8C7376657A5D82B72F10B9515F891FB0894DEF1`):

```sh
gpg --import release-signing-key.asc
gpg --verify SHA256SUMS.asc SHA256SUMS
sha256sum -c SHA256SUMS
```
END
