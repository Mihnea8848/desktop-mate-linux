# Changelog

Each section is also the text of the matching GitHub release.

## v0.5.1

Custom characters: your own VRM models in the character menu, worn by the
characters you own. Also MSAA as the default anti-aliasing.

### Added

- Custom characters. The character menu has a "Mods" list above the game's
  characters: "Add a VRM model" opens a file dialog, and every model in
  `~/.local/share/desktop-mate-linux/characters` gets a card. A mod is worn by
  a character you own and uses her animations and voice; the gear on its card
  changes its name and that character, or deletes it. Faces follow blinking,
  expressions and lip sync, models are scaled to her height and shaded like
  the game's characters. With Multi-Character each character out can wear a
  different mod; a mod's character comes out already wearing it and leaves
  wearing it. Choices are kept in `characters.conf`.

### Changed

- The first start sets the game's anti-aliasing to MSAA (HQ), once. The other
  modes look poor under Proton; MSAA costs about 2 W and no frame rate. A mode
  picked in the game afterwards is kept.
- The GE-Proton patches follow the review on
  GloriousEggroll/proton-ge-custom#897: the click-through readback no longer
  retries the fast path on every sample after it failed once, and the
  toplevel readback is only used with `WINE_LAYERED_OVERLAY_DIRECT=1`. Run
  `desktop-mate-linux build-proton` again to pick them up.

## v0.5.0

Easier to install and keep up to date, and one place for this project's
settings inside the game.

### Changed

- What this project adds to the game's settings is now in a "Linux Patch
  Settings" section at the bottom of both tabs: the brightness slider for the
  current character under Character Settings, the battery saver and the
  switches under Global Settings.
- The README lists the additions over the Windows version up front.

### Added

- One-command install for any distribution:
  `curl -fsSL https://raw.githubusercontent.com/Mihnea8848/desktop-mate-linux/main/get.sh | sh`.
  It verifies the release signature, installs to
  `~/.local/share/desktop-mate-linux` without root and runs the setup.
- Installs made that way update themselves: at most once a day, when the
  game starts, a newer signed release is installed for the next start and the
  previous version is kept. `DM_UPDATE=off` turns it off. Package installs
  are updated by the package manager instead.
- `desktop-mate-linux update` and `desktop-mate-linux uninstall`.
- CI installs the test release with `get.sh` on every pull request.

## v0.4.1

The pacman repository is live with this release.

### Fixed

- Publishing the pacman repository. In v0.4.0 the release ran up to the last
  step and then stopped: `repo-add` writes `desktop-mate-linux.db` as a link to
  the database archive, and copying the archive onto that link failed. The
  tarball and checksums of v0.4.0 were published, the repository was not.

### Changed

- CI now runs the whole release on every pull request with a throwaway key:
  it builds the tarball, builds and signs the Arch package, creates the
  repository and installs the package from it with `pacman`. A broken release
  step now fails the pull request instead of the release.
- Release notes come from this changelog, with update and verification
  instructions under each release. A release without a changelog entry fails.

## v0.4.0

Packaging release: Desktop Mate on Linux can now be installed and updated
with `pacman`, and there is one command for everything.

### Added

- `desktop-mate-linux` command with `setup`, `remove`, `build-proton` and
  `version`. `install.sh` still works.
- Signed pacman repository for Arch Linux, CachyOS and other Arch-based
  systems, so updates arrive with `pacman -Syu`. The setup is in the README.
- Every release has a `VERSION` file and a `SHA256SUMS` file signed with a
  dedicated release key (`packaging/keys/release-signing-key.asc`).
- Brightness slider per character, 25% to 150%, at the bottom of Character
  Settings. It scales the light the characters are shaded with, applies
  immediately and is remembered for each character. The characters are not
  brighter on Linux than on Windows (checked against the store screenshots);
  this is for taste, mostly against dark desktops.

### Changed

- The Arch package is called `desktop-mate-linux` and lives in
  `packaging/arch`; it is no longer meant for the AUR.

## v0.3.1

Smoother dragging and about a third less CPU.

### Fixed

- Dragging the character stuttered. The game clears its click-through flag
  while the pointer is on her, and `ghost.exe` put it back 10 to 30 times a
  second during a drag; every change made Wine reset her clickable area. The
  flag is now left alone while a mouse button is held.

### Changed

- While you hold the character, the game renders at your screen's refresh
  rate (for example 180 Hz) and goes back to 60 FPS when you let go, so she
  keeps up with the pointer. Battery saver keeps 30 FPS.
- Unity starts one job worker thread per CPU thread, and under Wine the idle
  ones spin. `launch.sh` now limits it to two (`-job-worker-count 2`): the
  game went from about 140% to 95% of a core at the same 60 FPS.
  `DM_UNITY_ARGS` overrides it.

## v0.3.0

Settings for this project inside the game itself.

### Added

- A section at the bottom of Settings, Global Settings, in the game's own
  style: battery saver (30 FPS, applies immediately), sit on windows and smooth
  animation. Saved to `~/.config/desktop-mate-linux/game.conf`.
- `dmhook.dll`, loaded into the game by `ghost.exe`, which adds that section.
  The game renames its internals and blocks mods, so the library recovers the
  real function names from `UnityPlayer.dll` on every start and only uses
  Unity's own classes. If a game update breaks something it logs why and does
  nothing. `DM_HOOK=0` turns it off.
- The self-test checks that the section was added.

## v0.2.1

Fixes the frozen character and makes 60 FPS possible.

### Fixed

- The character sometimes froze in place while the game kept running. Any
  `proton run` without the game's Steam environment (my own test tools,
  protontricks) deletes the drive the game runs from. The game re-reads its
  own files now and then and, when that fails, unloads the character or
  crashes. `launch.sh` now restores the drive within two seconds, and
  `tools/run-in-game.sh` starts test programs correctly.

### Added

- `tools/build-ge-proton.sh` builds a copy of GE-Proton with two fixes: the
  game draws straight into its window, so all 60 frames reach the screen
  instead of about 45, and Xwayland uses about a third of the CPU. Both are
  sent upstream as GloriousEggroll/proton-ge-custom#897.
- `DM_UNITY_LOG=1` writes Unity's own log to `/dev/shm/ghost-bridge/unity.log`.

### Changed

- The README was rewritten and shortened; technical details moved to
  `docs/how-it-works.md`.

## v0.2.0

The character now behaves like on Windows in a few more places.

### Added

- Cursor tracking over native Wayland apps. Wine only saw the pointer over
  X11 windows, so the game thought the cursor was still on the character and
  she did not attach to the window under her.
- Tray icon and menu (GE-Proton 11-11 and newer); the menu closes when you
  switch windows or after 8 seconds.
- "Launch at startup" from the game's settings now works on Linux.
- `install.sh` sets the launch options and the compatibility tool for you.
- `tools/selftest.py` tests a running game end to end.

## v0.1.1

### Fixed

- With GE-Proton 11-7 and newer the character stood on windows without
  attaching to them. `ghost.exe` now keeps the game window's click-through flag
  set (`DM_GAME_TRANSPARENT`).

## v0.1.0

First release: Desktop Mate runs on KDE Plasma Wayland through GE-Proton
with a transparent background, click-through and window sitting.

### Added

- Transparency and click-through with GE-Proton's layered overlay patch.
- Full-size game window: the prefix keeps KWin from shrinking it.
- Window bridge: a KWin script, a small D-Bus relay and `ghost.exe` copy your
  real windows into Wine, so the character can sit on them, follow them and
  move or minimize them.
- Stability work: `launch.sh` can be updated while the game runs, cleans up
  on exit, restarts `ghost.exe` if it stops; the relay replaces a stale
  instance.
- About a quarter less CPU: no extra frame cap on top of the game's own 60
  FPS, slower click-through sampling, `ghost.exe` waits for changes instead of
  polling.
- Release tarballs built by CI, GPL-3.0-or-later.
