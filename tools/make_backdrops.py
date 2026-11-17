#!/usr/bin/env python3
"""Six chapter backdrops (480x480), deliberately low contrast: a dark night
gradient with a moon and a skyline of silhouettes that sit only a shade above
the sky, so sprites and text on top stay easy to read. CC0.

Run:  python3 tools/make_backdrops.py [out-dir]
"""
import math
import os
import sys
from PIL import Image, ImageDraw, ImageFilter

W = H = 480


def gradient(top, bottom):
    im = Image.new("RGB", (W, H))
    px = im.load()
    for y in range(H):
        t = y / (H - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(W):
            px[x, y] = c
    return im


def moon(im, cx, cy, r, colour, glow):
    g = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(g)
    d.ellipse((cx - r * 3, cy - r * 3, cx + r * 3, cy + r * 3), fill=glow)
    g = g.filter(ImageFilter.GaussianBlur(r * 1.2))
    px, gp = im.load(), g.load()
    for y in range(H):
        for x in range(W):
            a, b = px[x, y], gp[x, y]
            px[x, y] = tuple(min(255, a[i] + b[i]) for i in range(3))
    ImageDraw.Draw(im).ellipse((cx - r, cy - r, cx + r, cy + r), fill=colour)


def tint(c, k):
    return tuple(max(0, min(255, int(v + k))) for v in c)


def headstone(d, x, base, w, h, col):
    d.rectangle((x, base - h + w // 2, x + w, base), fill=col)
    d.pieslice((x, base - h, x + w, base - h + w), 180, 360, fill=col)


def cross(d, x, base, h, col, lean=0):
    w = max(3, h // 8)
    d.rectangle((x - w // 2 + lean, base - h, x + w // 2 + lean, base), fill=col)
    d.rectangle((x - h // 3 + lean, base - h + h // 4, x + h // 3 + lean, base - h + h // 4 + w), fill=col)


def tree(d, x, base, h, col):
    d.line((x, base, x, base - h), fill=col, width=5)
    for (dx, dy, l, a) in ((0, 0.55, 0.45, -40), (0, 0.7, 0.4, 40), (0, 0.4, 0.35, 25), (0, 0.85, 0.3, -30)):
        y0 = base - h * dy
        d.line((x, y0, x + h * l * math.sin(math.radians(a * 2)), y0 - h * l * 0.7), fill=col, width=3)
    d.line((x, base - h, x - h * 0.2, base - h * 1.15), fill=col, width=3)
    d.line((x, base - h, x + h * 0.18, base - h * 1.12), fill=col, width=3)


def fence(d, y, col, x0=0, x1=W, step=14, h=26):
    d.line((x0, y, x1, y), fill=col, width=3)
    for x in range(x0, x1, step):
        d.line((x, y, x, y - h), fill=col, width=2)
        d.polygon(((x - 3, y - h), (x + 3, y - h), (x, y - h - 7)), fill=col)


def arch(d, cx, base, w, h, col):
    d.rectangle((cx - w // 2, base - h + w // 2, cx + w // 2, base), fill=col)
    d.pieslice((cx - w // 2, base - h, cx + w // 2, base - h + w), 180, 360, fill=col)


def column(d, x, base, h, w, col):
    d.rectangle((x - w // 2, base - h, x + w // 2, base), fill=col)
    d.rectangle((x - w // 2 - 3, base - h - 6, x + w // 2 + 3, base - h), fill=col)
    d.rectangle((x - w // 2 - 3, base - 6, x + w // 2 + 3, base), fill=col)


def spire(d, cx, base, w, h, col):
    d.rectangle((cx - w // 2, base - h * 0.55, cx + w // 2, base), fill=col)
    d.polygon(((cx - w // 2 - 2, base - h * 0.55), (cx + w // 2 + 2, base - h * 0.55), (cx, base - h)), fill=col)


def gargoyle(d, cx, base, s, col):
    d.rectangle((cx - s, base - s, cx + s, base), fill=col)
    d.polygon(((cx - s, base - s), (cx - s * 1.8, base - s * 2), (cx - s * 0.4, base - s * 1.3)), fill=col)
    d.polygon(((cx + s, base - s), (cx + s * 1.8, base - s * 2), (cx + s * 0.4, base - s * 1.3)), fill=col)
    d.ellipse((cx - s * 0.7, base - s * 1.9, cx + s * 0.7, base - s * 0.7), fill=col)


def glow_dot(im, x, y, r, colour):
    g = Image.new("RGB", (W, H), (0, 0, 0))
    ImageDraw.Draw(g).ellipse((x - r, y - r, x + r, y + r), fill=colour)
    g = g.filter(ImageFilter.GaussianBlur(r))
    px, gp = im.load(), g.load()
    for yy in range(max(0, y - r * 4), min(H, y + r * 4)):
        for xx in range(max(0, x - r * 4), min(W, x + r * 4)):
            a, b = px[xx, yy], gp[xx, yy]
            px[xx, yy] = tuple(min(255, a[i] + b[i]) for i in range(3))


def mist(im, y, strength):
    over = Image.new("RGB", (W, H), (0, 0, 0))
    d = ImageDraw.Draw(over)
    for k in range(6):
        yy = y + k * 9
        d.ellipse((-60 + k * 40, yy, 260 + k * 60, yy + 28), fill=(strength, strength, strength))
    over = over.filter(ImageFilter.GaussianBlur(14))
    px, op = im.load(), over.load()
    for yy in range(H):
        for xx in range(W):
            a, b = px[xx, yy], op[xx, yy]
            px[xx, yy] = tuple(min(255, a[i] + b[i] // 3) for i in range(3))


def stars(im, seed, n=40, colour=(70, 74, 96)):
    px = im.load()
    s = seed * 7919
    for _ in range(n):
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        x = s % W
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        y = (s % (H // 2))
        px[x, y] = colour


def chapter(n):
    pass


def make(n):
    cfg = {
        1: ((14, 20, 22), (24, 34, 32), (96, 110, 108), (14, 22, 20)),
        2: ((16, 18, 32), (30, 32, 50), (170, 176, 196), (18, 20, 34)),
        3: ((24, 20, 20), (44, 36, 32), (190, 176, 150), (22, 17, 16)),
        4: ((22, 16, 30), (40, 30, 52), (178, 160, 200), (20, 14, 28)),
        5: ((12, 18, 36), (28, 38, 66), (170, 190, 230), (12, 16, 32)),
        6: ((28, 14, 20), (52, 26, 34), (210, 160, 150), (24, 12, 16)),
    }
    top, bot, moon_col, sil_dark = cfg[n]
    im = gradient(top, bot)
    stars(im, n)
    glow = tuple(int(c * 0.22) for c in moon_col)
    moon(im, 380 if n % 2 else 110, 70, 22, tuple(int(c * 0.78) for c in moon_col), glow)
    sil = tint(bot, 8)
    sil2 = tint(bot, 18)
    d = ImageDraw.Draw(im)
    base = 470
    if n == 1:
        tree(d, 70, base, 170, sil)
        tree(d, 420, base, 130, sil)
        headstone(d, 215, base, 40, 70, sil2)
        cross(d, 290, base, 55, sil, lean=6)
        d.rectangle((0, base, W, H), fill=sil)
    elif n == 2:
        fence(d, base - 4, sil2, 0, W)
        for i, x in enumerate(range(16, W, 52)):
            if i % 3 == 0:
                cross(d, x, base - 4, 46 + (i % 2) * 10, sil)
            else:
                headstone(d, x - 10, base - 4, 22, 40 + (i % 3) * 8, sil)
        d.rectangle((0, base, W, H), fill=sil)
    elif n == 3:
        for x in range(30, W, 90):
            arch(d, x, base, 56, 120, sil)
        for x in range(75, W, 90):
            column(d, x, base, 130, 14, sil2)
        d.rectangle((0, base, W, H), fill=sil)
    elif n == 4:
        for x in range(40, W, 100):
            column(d, x, base, 190, 20, sil2)
        d.polygon(((120, 220), (360, 220), (240, 160)), fill=sil)
        d.rectangle((120, 220, 360, base), fill=sil)
        arch(d, 240, base, 70, 110, tint(bot, 2))
        d.rectangle((0, base, W, H), fill=sil)
    elif n == 5:
        spire(d, 90, base, 40, 250, sil)
        spire(d, 400, base, 30, 190, sil)
        d.rectangle((140, 330, 340, base), fill=sil)
        d.polygon(((130, 330), (350, 330), (240, 270)), fill=sil)
        arch(d, 240, base, 60, 90, tint(bot, -2))
        glow_dot(im, 240, 390, 10, (44, 34, 14))
        fence(d, base - 2, sil2, 0, 130, 12, 22)
        fence(d, base - 2, sil2, 350, W, 12, 22)
        d.rectangle((0, base, W, H), fill=sil)
    elif n == 6:
        d.pieslice((140, 250, 340, 420), 180, 360, fill=sil)
        d.rectangle((150, 330, 330, base), fill=sil)
        column(d, 160, base, 140, 18, sil2)
        column(d, 320, base, 140, 18, sil2)
        gargoyle(d, 60, base, 20, sil2)
        gargoyle(d, 420, base, 20, sil2)
        arch(d, 240, base, 60, 100, tint(bot, 2))
        glow_dot(im, 240, 430, 9, (50, 18, 12))
        d.rectangle((0, base, W, H), fill=sil)
    mist(im, 395, 20)
    return im


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "build/art/Backdrops"
    os.makedirs(out, exist_ok=True)
    thumbs = []
    for n in range(1, 7):
        im = make(n)
        im.save(os.path.join(out, "Backdrop%d.png" % n))
        t = im.copy()
        d = ImageDraw.Draw(t)
        # where the board and HUD will sit (19px tiles, 23 columns)
        d.rectangle((21, 43, 21 + 437, 43 + 437), outline=(255, 80, 80))
        thumbs.append(t.resize((320, 320), Image.LANCZOS))
    sheet = Image.new("RGB", (3 * 320 + 4 * 6, 2 * 320 + 3 * 6), (10, 10, 12))
    for i, t in enumerate(thumbs):
        sheet.paste(t, (6 + (i % 3) * 326, 6 + (i // 3) * 326))
    sheet.save(os.path.join(out, "_backdrops.png"))
    print("wrote", out)
