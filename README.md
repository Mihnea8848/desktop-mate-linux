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
- GE-Proton 11-7
- Desktop Mate 1.16.1dw

Expect rough edges. Reports from other setups are welcome.

### What works

- Transparent background with real per-pixel alpha
- Clicking through empty parts of the window to the apps below
- The right-click menu, settings, dragging the character
- Sitting on the top edge of a window and moving with it
- Falling down when that window is minimized, moved away or closed
- Moving and minimizing real windows when the game does so

### Known problems

- The tray icon appears, but its menu does not open. The same entries are in
  the character's right-click menu.
- Wine only knows where the cursor is while it is over an X11 window. Over
  native Wayland apps the game sees the last known position, so anything that
  follows the cursor everywhere only works over X11 windows and the character.
- CPU use is high. See [Performance](#performance).
- "Launch at startup" in the game's settings has no effect on Linux.
- Only KDE Plasma is supported, because the window list comes from a KWin script.

## Requirements

- Steam with Desktop Mate installed
- KDE Plasma 6 on Wayland (Xwayland must be available)
- GE-Proton 11-7 or newer. Older GE-Proton versions work but run at about 33
  FPS. The overlay patch this relies on is not part of Valve's Proton or
  proton-cachyos.
- Build tools: gcc, clang, lld, llvm, the MinGW-w64 headers and CRT, systemd
  development files, Python 3, and `qdbus6` (or `qdbus-qt6`)

On Arch Linux and derivatives:

```sh
sudo pacman -S --needed base-devel clang lld llvm mingw-w64-headers mingw-w64-crt qt6-tools python
```

GE-Proton can be installed with [ProtonPlus](https://github.com/Vysp3r/ProtonPlus)
or [ProtonUp-Qt](https://github.com/DavidoTek/ProtonUp-Qt).

## Installation

1. Clone the repository and build the helpers:

   ```sh
   git clone https://github.com/Mihnea8848/desktop-mate-linux.git
   cd desktop-mate-linux
   ./build.sh
   ```

2. In Steam, open the properties of Desktop Mate. Under Compatibility, force
   GE-Proton 11-7 or newer.

3. Under General, set the launch options to the full path of `launch.sh`:

   ```
   /home/you/desktop-mate-linux/launch.sh %command%
   ```

4. Start the game, quit it, and start it again. The first start creates the
   Wine prefix, and the window settings this project needs are written to it
   from the second start on.

## Configuration

`launch.sh` reads environment variables, either from the launch options
(`DM_FPS=30 /home/you/desktop-mate-linux/launch.sh %command%`) or from a
`local.conf` file next to it. `local.conf.example` lists them.

| Variable | Default | Meaning |
| --- | --- | --- |
| `DM_FPS` | 60 | Frame rate cap. 0 removes the cap. |
| `DM_SHAPE_INTERVAL` | 100 | How often, in milliseconds, the click-through area is updated. |
| `DM_PROTON` | | Path of a Proton build to use instead of the one chosen in Steam. |
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
they are never drawn and never receive input. `ghost.exe` also keeps the game
as Wine's foreground window, because the game ignores its right-click menu
otherwise, and it exits when the game exits.

## Performance

Measured on the setup above with the graphics preset on Ultra. CPU is in
percent of one core.

| Setup | FPS | Game | Xwayland |
| --- | --- | --- | --- |
| GE-Proton 11-5 | 33 | 90% | 38% |
| GE-Proton 11-7, 60 FPS cap, 100 ms shape updates | 59 | 118% | 14% |
| GE-Proton 11-7, 30 FPS cap, 100 ms shape updates | 30 | 80% | 15% |

The NVIDIA X11 Vulkan driver cannot present a window with alpha, so GE-Proton
copies every frame through the CPU. A lower graphics preset in the game or
`DM_FPS=30` reduces the load.

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
`tools/bench.sh <label>` measures the frame rate and the CPU use of the game,
Xwayland, KWin and wineserver while the game runs.

If the character renders on a black rectangle, the game is not running with a
GE-Proton build that has the overlay patch. If she is blurry and the click
areas are offset, the prefix setting from step 4 of the installation is
missing.

## Roadmap

- Lower CPU use
- Settings for this project inside the game's own menus
- Support for other compositors (GNOME, Hyprland)
- Packages, starting with the AUR

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
