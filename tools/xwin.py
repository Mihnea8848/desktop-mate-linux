#!/usr/bin/env python3
# Copyright (c) 2026 Mihnea Gîrbăcică (Mihnea8848)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Inspects the game's X11 window (needs python-xlib, run with DISPLAY set).
#   xwin.py fps [seconds]          frames per second, counted from X Damage events
#   xwin.py shape <in.png> <out>   draws the click-through input shape over a screenshot
import sys
import time

from Xlib import display
from Xlib.ext import damage, shape


def game_window(d):
    for c in d.screen().root.query_tree().children:
        try:
            if c.get_wm_name() == "DesktopMate" and c.get_attributes().map_state == 2:
                return c
        except Exception:
            pass
    sys.exit("game window not found")


def fps(seconds: float) -> None:
    d = display.Display()
    win = game_window(d)
    d.damage_query_version()
    win.damage_create(damage.DamageReportNonEmpty)
    frames, start = 0, time.time()
    while time.time() - start < seconds:
        while d.pending_events():
            ev = d.next_event()
            if ev.type == d.extension_event.DamageNotify:
                frames += 1
                d.damage_subtract(ev.damage, 0, 0)
        time.sleep(0.002)
    print(f"{frames / seconds:.1f}")


def shape_overlay(src: str, dst: str) -> None:
    from PIL import Image, ImageDraw
    d = display.Display()
    win = game_window(d)
    g = win.get_geometry()
    rects = win.shape_get_rectangles(shape.SK.Input).rectangles
    img = Image.open(src).convert("RGBA")
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    for r in rects:
        x, y = g.x + r.x, g.y + r.y
        draw.rectangle([x, y, x + r.width - 1, y + r.height - 1], fill=(0, 255, 0, 110))
    Image.alpha_composite(img, layer).save(dst)
    print(f"{len(rects)} rectangles, {sum(r.width * r.height for r in rects)} px")


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "fps":
        fps(float(sys.argv[2]) if len(sys.argv) > 2 else 5)
    elif len(sys.argv) == 4 and sys.argv[1] == "shape":
        shape_overlay(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__ or "usage: xwin.py fps [seconds] | shape <in.png> <out.png>")
