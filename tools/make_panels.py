#!/usr/bin/env python3
"""Placeholder comic panels for the story pages (CC0).

Backgrounds are the chapter arenas turned into manga-style screentone:
greyscale, ink-line edges and a 45-degree halftone dot screen. Figures are the
game's own pixel sprites enlarged with a spot of colour. Replace any of the
output PNGs with real art of the same name and the game picks it up; the
panel sizes are W (456x224, wide) and S (224x224, square).

Run:  python3 tools/make_panels.py [out-dir]     (needs Pillow and numpy)
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageOps

INK = (26, 20, 34)
PAPER = (238, 232, 214)
TEAL = (92, 214, 190)
AMBER = (255, 196, 84)
W_SIZE = (456, 224)
S_SIZE = (224, 224)


def cover(im, size):
    """Scale and crop to fill size."""
    k = max(size[0] / im.width, size[1] / im.height)
    r = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
    x, y = (r.width - size[0]) // 2, (r.height - size[1]) // 2
    return r.crop((x, y, x + size[0], y + size[1]))


def screentone(im, size, dot=5.6, contrast=1.0, lines=True):
    """Turn a picture into black-and-paper manga tone with ink edges."""
    g = ImageOps.autocontrast(cover(im, size).convert("L").filter(ImageFilter.GaussianBlur(1.4)), cutoff=3)
    g = g.point(lambda v: int(255 * (v / 255.0) ** (1.0 / contrast)))
    a = np.asarray(g).astype(float) / 255.0
    h, w = a.shape
    yy, xx = np.mgrid[0:h, 0:w]
    u = (xx + yy) / math.sqrt(2.0)
    v = (xx - yy) / math.sqrt(2.0)
    screen = (np.cos(2 * math.pi * u / dot) + np.cos(2 * math.pi * v / dot)) / 4.0 + 0.5
    tone = (a > screen * 0.95 + 0.02).astype(np.uint8) * 255
    out = Image.fromarray(tone, "L")
    if lines:
        edges = g.filter(ImageFilter.GaussianBlur(1.2)).filter(ImageFilter.FIND_EDGES)
        e = np.asarray(edges).astype(float)
        out = Image.fromarray(np.where(e > 44, 0, np.asarray(out)).astype(np.uint8), "L")
    rgb = ImageOps.colorize(out, INK, PAPER)
    return rgb.convert("RGBA")


def sprite(name, scale):
    s = Image.open("assets/Art/%s.png" % name).convert("RGBA")
    return s.resize((s.width * scale, s.height * scale), Image.NEAREST)


def outline(im, colour=INK, grow=4):
    """Thick ink outline round a sprite, like a cut-out in a comic."""
    alpha = im.split()[3]
    big = alpha.filter(ImageFilter.MaxFilter(grow * 2 + 1))
    base = Image.new("RGBA", im.size, colour + (0,))
    base.putalpha(big)
    base.alpha_composite(im)
    return base


def speed_lines(size, centre, colour=INK, count=46, inner=0.18, seed=3):
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    rnd = np.random.RandomState(seed)
    cx, cy = centre
    reach = math.hypot(size[0], size[1])
    for _ in range(count):
        ang = rnd.uniform(0, 2 * math.pi)
        r0 = reach * rnd.uniform(inner, inner + 0.25)
        r1 = reach * rnd.uniform(0.7, 1.0)
        wid = rnd.uniform(0.012, 0.03)
        p0 = (cx + math.cos(ang) * r0, cy + math.sin(ang) * r0)
        a1, a2 = ang - wid, ang + wid
        d.polygon([p0, (cx + math.cos(a1) * r1, cy + math.sin(a1) * r1),
                   (cx + math.cos(a2) * r1, cy + math.sin(a2) * r1)], fill=colour + (255,))
    return layer


def glow(size, centre, radius, colour):
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.ellipse((centre[0] - radius, centre[1] - radius, centre[0] + radius, centre[1] + radius),
              fill=colour + (200,))
    return layer.filter(ImageFilter.GaussianBlur(radius * 0.35))


def paste_center(base, fg, cx, cy):
    base.alpha_composite(fg, (int(cx - fg.width / 2), int(cy - fg.height / 2)))


def arena_panel(chapter, size, focus=(0.5, 0.55), zoom=1.0):
    im = Image.open("assets/Arenas/Arena%dArt.png" % chapter).convert("RGB")
    if zoom != 1.0:
        w, h = im.size
        cw, ch = int(w / zoom), int(h / zoom)
        x = int((w - cw) * focus[0])
        y = int((h - ch) * focus[1])
        im = im.crop((x, y, x + cw, y + ch))
    return screentone(im, size)


def figure_panel(name, size, scale, chapter=None, bg_dark=True, spot=None, lines=True):
    if chapter:
        base = arena_panel(chapter, size, focus=(0.5, 0.35), zoom=2.4)
        shade = Image.new("RGBA", size, INK + (120,))
        base.alpha_composite(shade)
    else:
        base = Image.new("RGBA", size, INK + (255,))
    if lines:
        base.alpha_composite(speed_lines(size, (size[0] / 2, size[1] * 0.52), PAPER if bg_dark else INK, count=38))
    if spot:
        base.alpha_composite(glow(size, (size[0] * 0.62, size[1] * 0.56), 46, spot))
    fig = outline(sprite(name, scale), INK, 4)
    paste_center(base, fig, size[0] / 2, size[1] * 0.56)
    return base


def eye_panel(size):
    base = Image.new("RGBA", size, INK + (255,))
    base.alpha_composite(speed_lines(size, (size[0] / 2, size[1] / 2), (60, 52, 76), count=60, inner=0.12))
    d = ImageDraw.Draw(base)
    cx, cy = size[0] // 2, size[1] // 2
    d.ellipse((cx - 70, cy - 30, cx + 70, cy + 30), fill=(14, 10, 20), outline=PAPER, width=4)
    base.alpha_composite(glow(size, (cx, cy), 34, TEAL))
    d.ellipse((cx - 22, cy - 22, cx + 22, cy + 22), fill=TEAL)
    d.ellipse((cx - 8, cy - 14, cx + 8, cy + 14), fill=INK)
    d.ellipse((cx + 8, cy - 14, cx + 16, cy - 6), fill=PAPER)
    return base


def flame_panel(size):
    base = Image.new("RGBA", size, INK + (255,))
    base.alpha_composite(speed_lines(size, (size[0] / 2, size[1] * 0.6), (56, 70, 96), count=50))
    base.alpha_composite(glow(size, (size[0] / 2, size[1] * 0.55), 70, (80, 180, 240)))
    fig = outline(sprite("Offering", 9), INK, 3)
    paste_center(base, fig, size[0] / 2, size[1] * 0.56)
    return base


PURSUER_FOR_CHAPTER = {1: "Digger0", 2: "Constable0", 3: "Mourner0", 4: "Tophat0", 5: "Mourner1", 6: "Tophat1"}


def save(im, out, name):
    im.convert("RGB").save(os.path.join(out, name + ".png"), optimize=True)


def build(out):
    os.makedirs(out, exist_ok=True)
    # prologue: four square panels
    save(arena_panel(1, S_SIZE, (0.3, 0.2), 1.6), out, "intro_1")
    save(eye_panel(S_SIZE), out, "intro_2")
    save(figure_panel("Constable0", S_SIZE, 9, None, spot=AMBER), out, "intro_3")
    save(figure_panel("RevenantUp0", S_SIZE, 9, 1), out, "intro_4")
    # one page per chapter: a wide establishing panel and two close-ups
    for c in range(1, 7):
        save(arena_panel(c, W_SIZE, (0.5, 0.45), 1.15), out, "ch%d_a" % c)
        save(figure_panel("RevenantDown0", S_SIZE, 9, c), out, "ch%d_b" % c)
        save(figure_panel(PURSUER_FOR_CHAPTER[c], S_SIZE, 9, c, spot=AMBER), out, "ch%d_c" % c)
    # ending
    save(arena_panel(6, W_SIZE, (0.5, 0.15), 1.1), out, "end_a")
    save(figure_panel("RevenantRight0", S_SIZE, 9, 6), out, "end_b")
    save(flame_panel(S_SIZE), out, "end_c")


def contact_sheet(out):
    names = sorted(f for f in os.listdir(out) if f.endswith(".png") and not f.startswith("_"))
    cols = 6
    thumbs = []
    for n in names:
        im = Image.open(os.path.join(out, n))
        thumbs.append((n, im.resize((im.width // 2, im.height // 2))))
    rows = (len(thumbs) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * 240, rows * 126), (60, 60, 70))
    for i, (n, t) in enumerate(thumbs):
        sheet.paste(t, ((i % cols) * 240 + 6, (i // cols) * 126 + 6))
    sheet.save(os.path.join(out, "_contact.png"))


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/Story/Panels"
    build(out)
    contact_sheet(out)
    print("wrote", out)
