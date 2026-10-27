# GE-Proton patches

Patches for the Wine in GE-Proton, applied by `tools/build-ge-proton.sh`. Each
folder is named after the GE-Proton release it was made for.

- `layered-overlay-wine.patch` replaces GE-Proton's own patch of that name
  (`patches/game-patches/` in proton-ge-custom). It adds one change: the
  click-through readback reads the offscreen window's composite pixmap, so the
  fast MIT-SHM path also works for windows larger than the screen.
- `layered-overlay-direct-present.patch` adds `WINE_LAYERED_OVERLAY_DIRECT=1`,
  which lets the game present straight into its ARGB window instead of being
  copied from an offscreen window after every frame.
- `wine-extra-commits` lists Wine commits that GE's prep script reverts; the
  build fetches them.

These are changes to Wine and GE-Proton code, so they are under the GNU Lesser
General Public License, version 2.1 or later, like Wine. The layered overlay
patch was written by thaylorz for GE-Proton, based on an earlier patch by
Solarisfire; the changes described above are by Mihnea Gîrbăcică (Mihnea8848)
and are meant to be sent upstream.
