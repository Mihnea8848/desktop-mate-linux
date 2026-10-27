# Desktop Mate on Linux

Run [Desktop Mate](https://store.steampowered.com/app/3301060/Desktop_Mate/),
the desktop mascot from Steam, on Linux. She is drawn with a real transparent
background, clicks go through to your apps, and she sits on your windows,
rides along when you move them and falls when you minimize them. It works on
KDE Plasma (Wayland) through GE-Proton, without touching the game files.

On plain Proton the game shows a black box that covers the screen, and the
character never notices a single window. This project fixes all of that.

It also goes further than the Windows version. It adds things the official app
does not have, right inside the game's own settings:

- Brightness per character
- A battery saver, and smoother dragging on high refresh rate screens
- Switches for window sitting and smooth animation
- **Custom characters (in development):** load your own VRM models, listed in
  the character menu above the DLC, and pick which character's animations and
  voice they use

## Why I made this

I made this out of frustration. Desktop Mate keeps moving further away from the
people who use it: instead of making the app better for its users, the
developers obfuscated the code to lock out mods and put their effort into new
ways to make money from it. Linux was never supported, and the attempts I
found never got past a black box or a heavy screen-streaming workaround.

People who bought characters for it deserve to use what they own, on the system
they choose. This repository is for the Linux users.

## What works

- Transparent background with real per-pixel alpha
- Clicking through empty space to the apps below
- Right-click menu, settings, dragging her around
- Sitting on the top edge of any window, following it when it moves, falling
  when it is minimized or closed
- Reacting to the cursor anywhere on the screen, also over native Wayland apps
- Tray icon and its menu
- "Launch at startup" from the game's settings
- A "Linux Patch Settings" section at the bottom of both tabs of the game's
  own settings page: brightness for each character under Character Settings,
  and a battery saver and switches for window sitting and smooth animation
  under Global Settings

I run it every day on CachyOS, KDE Plasma 6.8.1 (Wayland), an NVIDIA RTX 4060
and GE-Proton 11-11. Other setups are untested, so I would love to hear how it
goes on yours, good or bad.

## Known problems

- Only KDE Plasma is supported, because the window list comes from a KWin
  script. Hyprland and sway could be added; see the roadmap.
- The tray menu does not close when you click somewhere else. It closes when
  you switch windows, or after 8 seconds.
- CPU use is high: about one core on my laptop.
- Flatpak Steam is untested.

## Install

You need Steam with Desktop Mate installed, KDE Plasma 6 on Wayland, and
GE-Proton 11-7 or newer. Install GE-Proton with
[ProtonPlus](https://github.com/Vysp3r/ProtonPlus) or
[ProtonUp-Qt](https://github.com/DavidoTek/ProtonUp-Qt).

### Arch Linux, CachyOS and other Arch-based systems

The package comes from a signed pacman repository and updates with the rest
of your system. Trust the signing key once:

```sh
curl -fsSL https://raw.githubusercontent.com/Mihnea8848/desktop-mate-linux/main/packaging/keys/release-signing-key.asc | sudo pacman-key --add -
sudo pacman-key --lsign-key F8C7376657A5D82B72F10B9515F891FB0894DEF1
```

Add the repository at the end of `/etc/pacman.conf`:

```ini
[desktop-mate-linux]
Server = https://github.com/Mihnea8848/desktop-mate-linux/releases/download/repo
```

Then install it, quit Steam completely (Steam menu, Exit) and set it up:

```sh
sudo pacman -Syu desktop-mate-linux
desktop-mate-linux setup
```

### Other distributions

Quit Steam completely (Steam menu, Exit) and run:

```sh
curl -fsSL https://raw.githubusercontent.com/Mihnea8848/desktop-mate-linux/main/get.sh | sh
```

It downloads the latest release, checks its signature, installs it to
`~/.local/share/desktop-mate-linux`, points the game's launch options at it
and picks your newest GE-Proton. It needs no root. From then on it updates
itself: once a day, when the game starts, it looks for a new release and
installs it for the next start, keeping the previous version. Set
`DM_UPDATE=off` to stop that, or run `desktop-mate-linux update` by hand.
`desktop-mate-linux uninstall` removes it again.

You can also download a tarball from the
[releases page](https://github.com/Mihnea8848/desktop-mate-linux/releases),
extract it somewhere it can stay and run `./desktop-mate-linux setup` in that
folder.

### Then

Start Steam and then Desktop Mate. If you had never started the game before,
start it, quit it and start it once more. To undo the setup, run
`desktop-mate-linux remove` with Steam closed.

Building from source instead is one command on Arch Linux:

```sh
sudo pacman -S --needed base-devel clang lld llvm mingw-w64-headers mingw-w64-crt
git clone https://github.com/Mihnea8848/desktop-mate-linux.git
cd desktop-mate-linux && ./build.sh
```

### Smoother animation (optional)

Stock GE-Proton loses about a quarter of the frames on the way to the screen,
so she moves at about 45 FPS. `tools/build-ge-proton.sh` rebuilds one library
of your GE-Proton with my fix and installs it as a separate compatibility tool,
for example `GE-Proton11-11-desktopmate`. With it all 60 frames arrive and the
desktop uses less CPU.

```sh
sudo pacman -S --needed git base-devel flex bison libx11 libxext libxcomposite wayland
tools/build-ge-proton.sh
```

Then restart Steam and run `desktop-mate-linux setup` again. I am working on getting this
fix into GE-Proton itself.

## Settings

Set these in the launch options (`DM_FPS=30 /path/to/launch.sh %command%`) or
in `~/.config/desktop-mate-linux/local.conf`.

| Variable | Default | What it does |
| --- | --- | --- |
| `DM_FPS` | | Cap below 60 FPS, for example 30 to save battery |
| `DM_PROTON` | | Use this Proton folder instead of the one chosen in Steam |
| `DM_SHAPE_INTERVAL` | 200 | How often, in ms, the clickable area is updated |
| `DM_NO_BRIDGE` | | Set to 1 to turn off window sitting |
| `DM_HOOK` | 1 | Set to 0 to leave the game's settings page alone |
| `DM_UPDATE` | on | Set to `off` to stop installs from `get.sh` updating themselves |
| `DM_UNITY_ARGS` | `-job-worker-count 2` | Extra arguments for the game's Unity player |

The Linux Patch Settings section in the game's settings saves to
`~/.config/desktop-mate-linux/game.conf`, which is read after `local.conf`.

`local.conf.example` lists a few more for troubleshooting.

The first start also sets the game's anti-aliasing to **MSAA (HQ)**. In my
testing the other anti-aliasing modes look poor under Proton, and MSAA costs
almost nothing (about 2 W and no frame rate on my laptop). This happens once;
if you pick another mode in the game's settings afterwards, it stays.

## Troubleshooting

Logs are in `/dev/shm/ghost-bridge/`: `launch.log` from the start script and
`ghost.log` from the window bridge.

- Black rectangle around her: the game is not running on GE-Proton 11-7 or
  newer.
- She is blurry or clicks land in the wrong place: start the game once more,
  or run `desktop-mate-linux setup` again with Steam closed.
- She stops moving and the image freezes: something ran `proton run` in the
  game's prefix (protontricks, for example), which removes the drive the game
  runs from. `launch.sh` puts it back within two seconds, but if the game
  noticed first, restart it.
- Title bars of all windows disappear: a KWin window rule for the game was
  changed while it ran. Quit the game and do not add KWin rules for it.

For a bug report, attach the output of `tools/report.sh`.

## How it works

A KWin script sends the real window list and cursor position to a small helper
that runs inside the game's Wine session. The helper creates an invisible copy
of each window there, so the game sees your desktop as if it were on Windows,
and sends the game's moves and minimizes back to KWin. GE-Proton's overlay
patch provides the transparency, and a small library loaded into the game adds
the Linux Patch Settings section to its settings page. The details, and
everything I measured, are in [docs/how-it-works.md](docs/how-it-works.md).

## Roadmap

- Custom characters from your own VRM files, in a "Mods" category of the
  character menu, using the animations and voice of a character you own
- Hyprland and sway support
- The GE-Proton fixes merged upstream, so no custom build is needed

## Contributing

Bug reports, test results from other hardware and pull requests are all
welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848). Free software under the GNU
General Public License, version 3 or later: you can use, change and share it,
and anything you distribute based on it must stay under the same license, with
source code. See [LICENSE](LICENSE).

Desktop Mate is made by infiniteloop Co., Ltd. This project is not affiliated
with them and contains no game files.
