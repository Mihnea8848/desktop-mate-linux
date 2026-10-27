# How desktop-mate-linux works

This page has the technical details behind the [README](../README.md): what
breaks under Proton, how each part works around it, and what I measured.

## The problems and the fixes

Under plain Proton the game has three big problems and a few smaller ones.

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

Game drive. Proton maps the Steam library to drive `S:` through a link in
the prefix, and every `proton run` sets that link up again. Without the game's
Steam environment (`STEAM_COMPAT_INSTALL_PATH` and `STEAM_COMPAT_LIBRARY_PATHS`)
it deletes the link instead. The game re-reads its own files from time to time;
when that fails it unloads the character and stops rendering, which looks like
a frozen image. `launch.sh` watches the link and restores it, and
`tools/run-in-game.sh` starts test programs with the right environment.

## The Linux section in the game's settings

`ghost.exe` loads `dmhook.dll` into the game process. The game is protected
against mods, so a few things are needed before the library can do anything:

- The il2cpp API functions are exported under random names.
  `scripts/il2cpp-map.py` recovers the real names from `UnityPlayer.dll`, which
  looks them up in the fixed order of Unity's API list. `launch.sh` writes the
  map to `/dev/shm/ghost-bridge/il2cpp-map.txt` on every start, so it follows
  game updates.
- Most of those functions crash the game when called from outside
  `GameAssembly.dll` or `UnityPlayer.dll`. The check keeps the allowed address
  ranges in two variables; `dmhook.dll` finds them in the code of one export and
  widens the GameAssembly range.
- The game's own classes are renamed as well, so the library only uses Unity's
  classes (UI Toolkit, `Application`) and the element names from the game's
  layout, such as `common-section`.

All calls into the game run on its main thread: the library subclasses the
game window and handles a message it posts to itself twice a second. The
section is built from Unity `Toggle` and `Label` elements with the game's own
style classes; the check boxes are drawn with inline styles, because the
game's check boxes are images with the caption baked in. Changes are saved to
`game.conf`; the battery saver also sets `Application.targetFrameRate` right
away. While the character is dragged (the left button is held; clicks only
reach the game on the character), the library raises the frame rate to the
screen's refresh rate so she keeps up with the pointer, and sets it back to 60
on release. If anything is not found after a game update, the library writes the
reason to `dmhook.log` and does nothing else.

## Smooth animation

GE-Proton draws the game into a hidden window and copies each frame to the
visible one. The copy often runs before the new frame has arrived, so some
frames are shown twice and others are skipped: of 60 frames rendered per
second, about 45 different ones reach the screen.

The patches in `patches/ge-proton` add `WINE_LAYERED_OVERLAY_DIRECT=1`, which
lets the game present straight into its ARGB window, so there is no copy to
race. The click-through shape is then read from the window's composite pixmap,
which also works for a window taller than the screen. `launch.sh` sets the
variable by default; stock GE-Proton ignores it. I measured the difference by
hashing the character's pixels about 700 times a second: with stock GE-Proton
11-11 about 45 different frames per second reach the screen, with the patched
build all 60 do, evenly spaced.

## Performance

Measured with `tools/bench.sh` on my machine (CachyOS, Plasma 6.8.1, RTX 4060
Laptop, driver 615.78), with the character idle on the desktop. CPU is in
percent of one core.

| Setup | FPS | Game | Xwayland | KWin |
| --- | --- | --- | --- | --- |
| GE-Proton 11-5 | 33 | 90% | 38% | |
| GE-Proton 11-7, DXVK cap at 60, shape every 100 ms | 59 | 112% | 16% | 18% |
| GE-Proton 11-7, defaults (no DXVK cap, shape every 200 ms) | 60 | 90% | 10% | 19% |
| GE-Proton 11-7, `DM_FPS=30` | 30 | 75% | 15% | 16% |
| GE-Proton 11-11, defaults | 60 | 88% | 10% | 22% |
| GE-Proton 11-11 from `tools/build-ge-proton.sh` | 60 | 88% | 3% | 19% |

Unity job workers, measured later from `/proc` over 20 s for the whole game
process, GE-Proton 11-11 from `tools/build-ge-proton.sh`:

| Unity arguments | Game | FPS |
| --- | --- | --- |
| none (19 job workers on a 20-thread CPU) | 140% | 60 |
| `-job-worker-count 4` | 104% | 60 |
| `-job-worker-count 2` (default) | 95% | 60 |

What I found:

- The game limits itself to 60 FPS. A DXVK frame cap on top of that only adds
  a busy thread, so `launch.sh` sets none unless `DM_FPS` asks for a lower rate.
- The in-game graphics preset and anti-aliasing setting make no measurable
  difference to CPU or GPU use. Ultra with SMAA looks best and costs the same
  as Low.
- Most of the game's CPU time is spent by the game itself, not by the frame
  copy: drawing straight into the window (`tools/build-ge-proton.sh`) leaves
  it unchanged and only lowers Xwayland.
- With stock GE-Proton, Xwayland's share is mostly the click-through readback:
  the 2304x1296 window is taller than the screen, so the fast shared-memory
  read fails and every sample goes over the X socket. The patched build reads
  the window's pixmap instead.
- Unity starts one job worker thread per CPU thread. Under Wine the idle
  workers spin, and 19 of them cost 0.6 of a core. `launch.sh` passes
  `-job-worker-count 2`, which keeps 60 FPS with even frame pacing.
- The window bridge (KWin script, relay and `ghost.exe`) costs about 1 to 2%.
- Running the game on Wine's native Wayland driver (`PROTON_ENABLE_WAYLAND=1`)
  does not work yet: the window becomes a managed fullscreen window, the
  character is not drawn, and CPU use goes up.
