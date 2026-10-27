# Contributing

Bug reports, test results from other setups and pull requests are all useful.
This project has only been tested on one machine, so a report that it works
(or does not) on your hardware already helps.

## Reporting a problem

Open an issue with the bug report form. Attach the output of:

```sh
tools/report.sh
```

It collects your distribution, Plasma and Xwayland versions, GPU and driver,
installed Proton builds, and the last lines of `launch.log` and `ghost.log`.

## Building

```sh
./build.sh
```

You need gcc, clang, lld, llvm, the MinGW-w64 headers and CRT, and the systemd
development files. On Arch Linux:

```sh
sudo pacman -S --needed base-devel clang lld llvm mingw-w64-headers mingw-w64-crt
```

The build writes everything to `bin/`, which is not tracked by git.

## Testing a change

1. Point the Steam launch options at your checkout's `launch.sh`.
2. Start the game and run `python3 tools/selftest.py`. It drags the character
   onto a test window, moves the window, minimizes it and opens the tray menu,
   and needs `ydotoold`, `python-xlib`, `Pillow`, `kdialog` and `spectacle`.
   It moves the real pointer, so do not use the desktop while it runs. Also
   check the right-click menu by hand.
3. For changes that may affect performance, run `tools/bench.sh <label>` before
   and after with the game idle, and put both rows in the pull request.

`tools/xwin.py` shows the frame rate of the game window and can draw the
click-through area over a screenshot. The programs in `bin/tools/` inspect the
Win32 side; the README explains how to run them.

## Pull requests

- Fork the repository, create a branch, and open a pull request against `main`.
- CI builds the project and runs shellcheck and syntax checks. It must pass.
- Pull requests are merged with squash, so the branch history does not matter.
- Keep one topic per pull request.

## Releases and the Arch package

Pushing a `v*` tag builds the release tarball. `packaging/aur/desktop-mate-linux-bin`
has a PKGBUILD that installs that tarball to `/opt/desktop-mate-linux`. It is
not on the AUR; build it with `makepkg -si` in that directory. After a release,
set `pkgver`, reset `pkgrel` to 1 and run `updpkgsums`.

## Code style

- Every source file starts with a copyright line and
  `SPDX-License-Identifier: GPL-3.0-or-later`. Add your own copyright line to
  files you write; keep the existing ones.
- C: C11, four spaces, no warnings with `-Wall` (and `-Wextra` for
  `ghost-bridged`).
- Shell: POSIX `sh`, clean under shellcheck.
- Comments explain what is not obvious from the code, briefly. No change logs
  or version notes in comments; that is what git is for.
- Documentation: plain, specific sentences. Say what something does and how
  to use it.

## License

By contributing you agree that your work is published under the GNU General
Public License, version 3 or later, like the rest of the project. You keep the
copyright to your changes.
