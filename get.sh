#!/bin/sh
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs or updates Desktop Mate on Linux for the current user:
#   curl -fsSL https://raw.githubusercontent.com/Mihnea8848/desktop-mate-linux/main/get.sh | sh
# Downloads the latest release, checks it against the release key below and
# installs it to ~/.local/share/desktop-mate-linux/versions/<tag>, with a
# "current" link that Steam starts from. The previous version is kept.
#
#   get.sh               install, then run "desktop-mate-linux setup"
#   get.sh --update      install a newer release if there is one, no setup
#   get.sh --uninstall   undo setup and remove the installed files
# Settings: DML_TAG (a release instead of the latest). For CI only: DML_BASE
# (download location) and DML_KEY (a test key file instead of the release key).

main() {
    set -eu
    root=${XDG_DATA_HOME:-$HOME/.local/share}/desktop-mate-linux
    bindir=$HOME/.local/bin
    base=${DML_BASE:-https://github.com/Mihnea8848/desktop-mate-linux/releases/download}
    mode=${1:-install}

    if [ "$mode" = --uninstall ]; then
        uninstall
        return
    fi
    for c in curl tar gpgv gpg sha256sum python3; do
        command -v "$c" >/dev/null || fail "$c is required"
    done

    tag=${DML_TAG:-$(latest_tag)}
    [ -n "$tag" ] || fail "cannot find the latest release"
    installed=$(cat "$root/current/VERSION" 2>/dev/null || true)
    if [ "$mode" = --update ] && [ "$installed" = "$tag" ]; then
        return
    fi

    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    name=desktop-mate-linux-$tag.tar.gz
    for f in "$name" SHA256SUMS SHA256SUMS.asc; do
        curl -fsSL --retry 2 -o "$tmp/$f" "$base/$tag/$f" || fail "download of $f failed"
    done
    key | gpg --batch --dearmor >"$tmp/key.gpg" 2>/dev/null
    gpgv --keyring "$tmp/key.gpg" "$tmp/SHA256SUMS.asc" "$tmp/SHA256SUMS" >/dev/null 2>&1 ||
        fail "SHA256SUMS is not signed by the release key"
    (cd "$tmp" && sha256sum -c --quiet SHA256SUMS) || fail "$name does not match SHA256SUMS"

    mkdir -p "$root/versions/$tag"
    tar -xzf "$tmp/$name" -C "$root/versions/$tag" --strip-components=1
    ln -sfn "versions/$tag" "$root/current.tmp"
    mv -T "$root/current.tmp" "$root/current"
    mkdir -p "$bindir"
    ln -sfn "$root/current/desktop-mate-linux" "$bindir/desktop-mate-linux"
    # keep this version and the one it replaces
    for old in "$root"/versions/*; do
        case "${old##*/}" in "$tag" | "$installed") ;; *) rm -rf "$old" ;; esac
    done

    if [ "$mode" = --update ]; then
        echo "updated to $tag${installed:+ (from $installed)}"
        command -v notify-send >/dev/null &&
            notify-send -a "Desktop Mate on Linux" "Updated to $tag" "The update applies the next time Desktop Mate starts." || true
        return
    fi
    echo "installed $tag in $root"
    case ":$PATH:" in *":$bindir:"*) ;; *) echo "note: $bindir is not in PATH" ;; esac
    if pgrep -x steam >/dev/null; then
        echo "Steam is running. Quit it (Steam menu, Exit), then run: desktop-mate-linux setup"
    else
        "$root/current/desktop-mate-linux" setup
    fi
}

latest_tag() {
    curl -fsSL https://api.github.com/repos/Mihnea8848/desktop-mate-linux/releases/latest |
        python3 -I -c 'import json, sys; print(json.load(sys.stdin)["tag_name"])'
}

uninstall() {
    root=${XDG_DATA_HOME:-$HOME/.local/share}/desktop-mate-linux
    pgrep -x steam >/dev/null && fail "Steam is running. Quit it (Steam menu, Exit), then run this again"
    if [ -x "$root/current/desktop-mate-linux" ]; then
        "$root/current/desktop-mate-linux" remove || echo "the Steam launch options were not changed"
    fi
    rm -rf "$root"
    [ -L "$HOME/.local/bin/desktop-mate-linux" ] && rm -f "$HOME/.local/bin/desktop-mate-linux"
    echo "Desktop Mate on Linux removed. Settings in ~/.config/desktop-mate-linux are kept."
}

fail() {
    echo "desktop-mate-linux: $*" >&2
    exit 1
}

key() {
    if [ -n "${DML_KEY:-}" ]; then
        cat "$DML_KEY"
        return
    fi
    cat <<'KEY'
-----BEGIN PGP PUBLIC KEY BLOCK-----

mDMEauEJphYJKwYBBAHaRw8BAQdAXdM7oljeczDoI4Ada62rNCJM+VQNFJcKwLWa
fXS7Ysu0UWRlc2t0b3AtbWF0ZS1saW51eCByZWxlYXNlIHNpZ25pbmcgPDU5NzEz
MzczK01paG5lYTg4NDhAdXNlcnMubm9yZXBseS5naXRodWIuY29tPoiQBBMWCgA4
FiEE+Mc3Zlel2Cty8QuVFfiR+wiU3vEFAmrhCaYCGwMFCwkIBwIGFQoJCAsCBBYC
AwECHgECF4AACgkQFfiR+wiU3vFuBwD+MlcFdhydjmVqXReKc0C6PPPh75LK664r
6ofZcTj/ml8A/1pOuyNs75kSoPp53gWvul4yQA/bmBmw6w3PORpViXQM
=xrmy
-----END PGP PUBLIC KEY BLOCK-----
KEY
}

main "$@"
