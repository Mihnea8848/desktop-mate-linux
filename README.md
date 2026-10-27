# desktop-mate-linux

Scripts and small helper programs that make the Steam game
[Desktop Mate](https://store.steampowered.com/app/3301060/Desktop_Mate/) usable
on Linux with KDE Plasma on Wayland. The character is drawn with a transparent
background, clicks pass through empty areas, and she can sit on and interact
with your normal desktop windows.

Desktop Mate is a Windows game built with Unity. Under Proton it starts, but the
background is black, the window covers the screen and the character cannot see
any other window. This project works around those three problems without
modifying the game files.

## Status

Early and experimental. It runs every day on the author's machine, but it has
only been tested on one setup:

- CachyOS, KDE Plasma 6.8.1, Wayland session
- NVIDIA RTX 4060 Laptop GPU, driver 615.78
- GE-Proton 11-11
- Desktop Mate 1.16.1dw

Expect rough edges. Reports from other setups are welcome.

### What works

- Transparent background with real per-pixel alpha
- Clicking through empty parts of the window to the apps below
- The right-click menu, settings, dragging the character
- Sitting on the top edge of a window and moving with it
- Falling down when that window is minimized, moved away or closed
- Moving and minimizing real windows when the game does so
- The cursor position, also over native Wayland apps
- The tray icon and its menu (GE-Proton 11-11 and newer)
- "Launch at startup" in the game's settings

### Known problems

- The tray menu does not close when you click somewhere else, because those
  clicks never reach Wine. It closes when another window is activated, or
  after 8 seconds.
- CPU use is high. See [Performance](#performance).
- Only KDE Plasma is supported, because the window list comes from a KWin script.
- Steam installed as a Flatpak is untested. `launch.sh` would run inside the
  Flatpak sandbox, where `qdbus6` is usually missing; in that case the game
  starts without the window bridge and `launch.log` says so.

## Requirements

- Steam with Desktop Mate installed
- KDE Plasma 6 on Wayland (Xwayland must be available)
- GE-Proton 11-7 or newer. Older GE-Proton versions work but run at about 33
  FPS. The overlay patch this relies on is not part of Valve's Proton or
  proton-cachyos.
- Python 3, `qdbus6` (or `qdbus-qt6`) and `kreadconfig6`. On Plasma 6 these are
  usually installed already; on Arch Linux they come from `python`, `qt6-tools`
  and `kconfig`.

GE-Proton can be installed with [ProtonPlus](https://github.com/Vysp3r/ProtonPlus)
or [ProtonUp-Qt](https://github.com/DavidoTek/ProtonUp-Qt).

## Installation

1. Get the files, either as a release or by building from source.

   Release: download `desktop-mate-linux-<version>.tar.gz` from the
   [releases page](https://github.com/Mihnea8848/desktop-mate-linux/releases)
   and extract it:

   ```sh
   tar -xzf desktop-mate-linux-v0.1.0.tar.gz
   ```

   From source: you also need gcc, clang, lld, llvm, the MinGW-w64 headers and
   CRT, and the systemd development files. On Arch Linux:

   ```sh
   sudo pacman -S --needed base-devel clang lld llvm mingw-w64-headers mingw-w64-crt
   git clone https://github.com/Mihnea8848/desktop-mate-linux.git
   cd desktop-mate-linux
   ./build.sh
   ```

2. Quit Steam completely (Steam menu, Exit) and run:

   ```sh
   ./install.sh
   ```

   It sets the game's launch options to this copy of `launch.sh`, selects the
   newest GE-Proton 11-7 or newer from `compatibilitytools.d` as the game's
   compatibility tool (a "Latest" build from ProtonPlus is preferred, since it
   updates itself), and writes the window settings to the game's Wine prefix.
   Steam's config files are backed up as `.bak` next to the originals.
   `--tool NAME` picks a specific tool; `./install.sh --remove` removes the
   launch options again.

3. Start Steam and then Desktop Mate. If the game had never been started
   before, start it, quit it and start it again: the first start creates the
   Wine prefix, and the window settings are written to it on the second.

To set it up by hand instead: in the game's properties in Steam, force
GE-Proton 11-7 or newer under Compatibility, and under General set the launch
options to the full path of `launch.sh`:

```
/home/you/desktop-mate-linux/launch.sh %command%
```

Then start the game twice as in step 3.

## Configuration

`launch.sh` reads environment variables, either from the launch options
(`DM_FPS=30 /home/you/desktop-mate-linux/launch.sh %command%`), from a
`local.conf` file next to it, or from `~/.config/desktop-mate-linux/local.conf`,
which is read last. `local.conf.example` lists them.

| Variable | Default | Meaning |
| --- | --- | --- |
| `DM_FPS` | | Frame rate cap below the game's own 60 FPS, for example 30 to save power. |
| `DM_SHAPE_INTERVAL` | 200 | How often, in milliseconds, the click-through area is updated. |
| `DM_PROTON` | | Path of a Proton build to use instead of the one chosen in Steam. |
| `DM_GAME_TRANSPARENT` | 1 | Keep the click-through flag on the game window. See below. |
| `DM_NO_BRIDGE` | | Set to 1 to run without the window bridge. |

## How it works

There are three separate problems.

Transparency. The game asks Windows to extend the window frame over the whole
window (`DwmExtendFrameIntoClientArea`), which Wine does not implement. GE-Proton
has a patch that gives such windows a 32-bit visual with an alpha channel and
updates an X11 input shape from the opaque pixels. `launch.sh` enables it with
`WINE_LAYERED_OVERLAY_ALPHA=1`.

Window size. The game makes its window 20% larger than the monitor so the
character can walk past the screen edges. KWin treats it as a fullscreen window
and shrinks it to the monitor size, which blurs the character and shifts every
coordinate. The prefix gets `Managed=N` for `DesktopMate.exe`, so the window is
not managed by KWin and keeps the size the game asked for. `ensure-prefix.py`
writes that setting on every launch.

Other windows. The game finds windows with Win32 calls such as `EnumWindows`
and `GetWindowRect`. Under Wine these only return Wine windows. The bridge
copies the real desktop into Wine:

```
KWin script  --(window list)-->  ghost-bridged  --(windows.tsv)-->  ghost.exe
KWin script  --(cursor)------->  ghost-bridged  --(cursor.txt)--->  ghost.exe
KWin script  <--(commands)-----  ghost-bridged  <--(cmd files)----  ghost.exe
```

- `kwin/ghost-bridge.js` runs inside KWin and sends the position, size, title
  and stacking order of every normal window and visible panel.
- `ghost-bridged` is a small D-Bus service that passes data between KWin and
  Wine through files in `/dev/shm/ghost-bridge`.
- `ghost.exe` runs in the game's Wine session and creates one invisible window
  for each real window, with the same rectangle and title. Panels become
  `Shell_TrayWnd` windows. When the game moves or minimizes one of these
  windows, `ghost.exe` sends a command back and the KWin script applies it to
  the real window.

The invisible windows have an empty window region and are also unmanaged, so
they are never drawn and never receive input.

Cursor. Xwayland only receives pointer motion while the pointer is over an X11
window, so Wine's cursor position stops where the pointer left the character.
The KWin script sends the real position (at most 30 times a second) and
`ghost.exe` moves Wine's cursor there, unless X is already reporting motion or
a mouse button is held. Without this the game thinks the cursor is still on the
character, keeps its window from being click-through and does not attach to
the window she sits on.

Click-through flag. On Windows the game marks its window as click-through
(`WS_EX_TRANSPARENT`) whenever the cursor is not over the character, and window
lookups then see past it to the window under the character. GE-Proton 11-7 and
newer compute the click-through area from the pixels instead, and the flag no
longer stays on the window. The game then only finds itself and the character
stands on a window without attaching to it. `ghost.exe` puts the flag back.
Input still follows the pixels, so clicking the character works as before. `ghost.exe` also keeps the game
as Wine's foreground window, because the game ignores its right-click menu
otherwise, and it exits when the game exits.

Launch at startup. The game stores this setting in the Windows registry
(`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`), which nothing reads
on Linux. `scripts/sync-autostart.py` runs when the game starts and exits and
creates or removes `~/.config/autostart/desktop-mate-linux.desktop` to match.

## Performance

Measured with `tools/bench.sh` on the setup above, character idle on the
desktop. CPU is in percent of one core.

| Setup | FPS | Game | Xwayland | KWin |
| --- | --- | --- | --- | --- |
| GE-Proton 11-5 | 33 | 90% | 38% | |
| GE-Proton 11-7, DXVK cap at 60, shape every 100 ms | 59 | 112% | 16% | 18% |
| GE-Proton 11-7, defaults (no DXVK cap, shape every 200 ms) | 60 | 90% | 10% | 19% |
| GE-Proton 11-7, `DM_FPS=30` | 30 | 75% | 15% | 16% |

What we found:

- The game limits itself to 60 FPS. A DXVK frame cap on top of that only adds
  a busy thread, so `launch.sh` sets none unless `DM_FPS` asks for a lower rate.
- The in-game graphics preset and anti-aliasing setting make no measurable
  difference to CPU or GPU use. Ultra with SMAA looks best and costs the same
  as Low.
- Most of the remaining cost is the frame copy. The NVIDIA X11 Vulkan driver
  cannot present a window with alpha, so GE-Proton copies every frame through
  the CPU, and KWin then composites a 2304x1296 window.
- The window bridge (KWin script, relay and `ghost.exe`) costs about 1 to 2%.
- Running the game on Wine's native Wayland driver (`PROTON_ENABLE_WAYLAND=1`)
  does not work yet: the window becomes a managed fullscreen window, the
  character is not drawn, and CPU use goes up.

## Troubleshooting

Logs are written to `/dev/shm/ghost-bridge/`:

- `launch.log` shows what `launch.sh` did and whether the bridge started.
- `ghost.log` lists every window copied into Wine and every command sent back.
- `windows.tsv` is the current window list from KWin.

`build.sh` also builds three test programs in `bin/tools/`. Run them inside
the game's Wine session while the game is running:

```sh
STEAM_COMPAT_DATA_PATH=~/.local/share/Steam/steamapps/compatdata/3301060 \
STEAM_COMPAT_CLIENT_INSTALL_PATH=~/.local/share/Steam \
python3 "/path/to/GE-Proton/proton" run "$PWD/bin/tools/enumtest.exe"
```

- `enumtest.exe` writes all Win32 top-level windows to `enum.txt`.
- `fgprobe.exe` writes the foreground window, mouse buttons and cursor
  position to `fg.txt`.
- `wbtest.exe move|min "<window title>"` moves or minimizes a copied window
  the way the game does.

`tools/report.sh` prints system details and the logs for a bug report.
`tools/selftest.py` checks a running game end to end: the bridge, sitting on
and following a window, a game-driven minimize and the tray menu. It moves the
real pointer through `ydotool`, so leave the desktop alone while it runs.
`tools/bench.sh <label>` measures the frame rate and the CPU use of the game,
Xwayland, KWin and wineserver while the game runs.

If the character renders on a black rectangle, the game is not running with a
GE-Proton build that has the overlay patch. If she is blurry and the click
areas are offset, the prefix setting is missing: start the game once more,
or run `./install.sh` again with Steam closed.

If the title bars of all windows disappear while the game runs, a KWin window
rule that matches the game (`steam_app_3301060`) was changed or reloaded. KWin
tries to move the unmanaged game window and stops drawing decorations. Quitting
the game restores them. Do not add KWin rules for the game.

`ghost.exe` is restarted automatically (up to three times) if it stops while
the game is running. `launch.log` lists each start.

## Roadmap

- Lower CPU use
- Settings for this project inside the game's own menus
- Support for other compositors (GNOME, Hyprland)

## Contributing

Issues and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md)
for how to build, test and report problems.

## License

Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848).

This project is free software under the GNU General Public License, version 3
or later. You can use, change and share it, but anything you distribute that is
based on it must be released under the same license, with source code. See
[LICENSE](LICENSE).

Desktop Mate is developed by infiniteloop Co., Ltd. This project is not
affiliated with or endorsed by them and does not contain any game files.
