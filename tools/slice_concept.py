#!/usr/bin/env python3
"""Cuts the hand-made concept sheet (CC0, by the project owner) into sprites and
shrinks them to game-tile size with a clean pixel-art look.

Usage: python3 tools/slice_concept.py [out-dir]
The sheet lives in Extras/Concepts/. Output goes to art-src/slices-<S>/.
"""
import os
import sys
import numpy as np
from PIL import Image
from scipy import ndimage

SHEET = "Extras/Concepts/Grave Reckoning Pixel Art Sprite Sheet.png"
S = int(os.environ.get("TILE", "19"))

# Grid of the character block at the top of the sheet.
XS = [27, 122, 213, 296, 426, 512, 595, 677, 792, 881, 968, 1056, 1171, 1260, 1346, 1432]
YS = [11, 140, 248, 355]
CW, CH = 92, 112


def load():
    return Image.open(SHEET).convert("RGB")


def cut(im, x, y, w, h, dark=30):
    """Crop a window and make the black backdrop transparent."""
    c = np.asarray(im.crop((x, y, x + w, y + h))).astype(int)
    mask = c.max(axis=2) > dark
    # keep only the biggest blob plus things touching it, to drop crumbs
    d = ndimage.binary_dilation(mask, iterations=3)
    lab, n = ndimage.label(d)
    if n > 1:
        sizes = ndimage.sum(mask, lab, range(1, n + 1))
        keep = 1 + int(np.argmax(sizes))
        mask = mask & (lab == keep)
    ys, xs = np.where(mask)
    if len(ys) == 0:
        return None
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    rgb = c[y0:y1, x0:x1].astype(np.uint8)
    a = (mask[y0:y1, x0:x1] * 255).astype(np.uint8)
    out = np.dstack([rgb, a])
    return Image.fromarray(out, "RGBA")


def shrink(sprite, box_w, box_h, colours=14, thresh=110):
    """Scale to fit inside box, quantise to a small palette, crisp alpha."""
    w, h = sprite.size
    k = min(box_w / w, box_h / h)
    nw, nh = max(1, round(w * k)), max(1, round(h * k))
    # premultiply by alpha so the black backdrop does not bleed in
    arr = np.asarray(sprite).astype(float)
    a = arr[:, :, 3:4] / 255.0
    pm = Image.fromarray(np.dstack([arr[:, :, :3] * a, arr[:, :, 3:4]]).astype(np.uint8), "RGBA")
    small = pm.resize((nw, nh), Image.LANCZOS)
    sa = np.asarray(small).astype(float)
    alpha = sa[:, :, 3]
    rgb = np.where(alpha[:, :, None] > 0, sa[:, :, :3] / np.maximum(alpha[:, :, None] / 255.0, 1e-3), 0)
    rgb = np.clip(rgb, 0, 255).astype(np.uint8)
    solid = alpha >= thresh
    # quantise the solid pixels to a compact palette
    solid_img = Image.fromarray(rgb, "RGB")
    q = solid_img.quantize(colors=colours, method=Image.MEDIANCUT, dither=Image.NONE).convert("RGB")
    qa = np.asarray(q)
    out = np.dstack([qa, (solid * 255).astype(np.uint8)])
    return Image.fromarray(out, "RGBA")


def place(sprite, size=S, bottom_pad=1):
    """Centre horizontally, rest on the bottom of an S x S tile."""
    tile = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    x = (size - sprite.width) // 2
    y = size - sprite.height - bottom_pad
    tile.alpha_composite(sprite, (x, y))
    return tile


def character_frames(im, col0, row, n=4, box=None):
    box = box or (S - 2, S - 2)
    frames = []
    for i in range(n):
        c = cut(im, XS[col0 + i], YS[row], CW, CH)
        frames.append(place(shrink(c, *box)) if c else None)
    return frames


if __name__ == "__main__":
    im = load()
    out = sys.argv[1] if len(sys.argv) > 1 else "art-src/slices-%d" % S
    os.makedirs(out, exist_ok=True)
    groups = {
        "revenant_right": (0, 0), "revenant_left": (4, 0),
        "revenant_front": (8, 0), "revenant_back": (12, 0),
        "constable": (0, 1), "tophat": (4, 1), "mourner": (8, 1), "digger": (12, 1),
    }
    Z = 6
    sheet = Image.new("RGB", (len(groups) * 4 * (S + 2) * Z // 2 + 8, 2 * (S + 2) * Z // 2 + 8), (34, 38, 52))
    for gi, (name, (c0, r)) in enumerate(groups.items()):
        frames = character_frames(im, c0, r)
        for fi, f in enumerate(frames):
            if f is None:
                continue
            f.save(os.path.join(out, "%s_%d.png" % (name, fi)))
        for fi, f in enumerate(frames):
            if f is None:
                continue
            big = f.resize((S * Z // 2, S * Z // 2), Image.NEAREST)
            sheet.paste(big, (4 + (gi * 4 + fi) * (S + 2) * Z // 2 // 1, 4), big)
    sheet.save(os.path.join(out, "_characters.png"))
    print("wrote", out)
