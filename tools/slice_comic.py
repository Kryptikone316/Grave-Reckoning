#!/usr/bin/env python3
"""Prepares the full comic pages (Extras/Concepts/Story/Chapters, CC0, by the
project owner) for the game's comic viewer.

Each page is cropped to the inside of its ornate frame, scaled to 456 px wide
and saved as assets/Story/Comic/<name>.png. The pages are text-free, so the game
shows them whole and reveals them one row of panels at a time (layout "comic" in
assets/Story/story.txt). This tool also prints the row bands as ready-to-paste
"beat x y w h" lines, found from the cream gutters between the rows.

A page whose panels are staggered (no gutter runs the full width) lists extra
cut heights in CUTS below, in source pixels.

Run:  python3 tools/slice_comic.py [out-dir]       (needs Pillow and numpy)
"""
import os
import sys

import numpy as np
from PIL import Image

SRC = "Extras/Concepts/Story/Chapters"
# game page name -> source file
PAGES = {
    "ch1_1": "1/Ch1 - The Forgotten Grave.png",
    "ch1_2": "1/Ch1 - The Revenant’s Moonlit Awakening.png",
    "ch2_1": "2/Ch2 - 01 - Pretext - Gothic Cemetery Revenant Confrontation.png",
    "ch2_2": "2/Ch2 - 02 - Moonlit Mausoleum_ The Revenant’s Stone Enclosure.png",
    "ch2_3": "2/Ch2 - 03 - Leadup - The Revenant’s Ossuary Ledger.png",
    "ch3_1": "3/Ch3 - 01 - Ossuary Secrets_ The Revenant’s Awakening.png",
    "ch3_2": "3/Ch3 - 02 - The Ossuary Ledger_ A Gothic Revelation.png",
    "ch3_3": "3/Ch3 - 03  Erased Bloodlines in the Family Crypt.png",
    "ch4_1": "4/Ch4 - 01 - Gothic Crypt Conspiracy_ Ancestral Reckoning.png",
    "ch4_2": "4/Ch4 - 02 - Manor Secrets_ Journey to the Chapel.png",
    "ch5_0": "5/Ch5 - 00 - Ghostly Memories - Recap.png",
    "ch5_1": "5/Ch5 - 01 - Gothic Chapel and the Sealed Crypt.png",
    "ch5_2": "5/Ch5 - 02 -  Descent into Memory.png",
    "ch6_0": "6/Ch6 - 00 - Gothic Revenant_ Echoes Through Time.png",
    "ch6_1": "6/Ch6 - 01 -  Crypt of the Ancestors.png",
    "ch6_2": "6/Ch6 - 02 - Revenant’s Spiral of Forgotten Lives.png",
    "ch6_3": "6/Ch6 - 03 - Gothic Mausoleum of Rebirth.png",
    "ch6_4": "6/Ch6 - 04 - The Revenant’s Endless Return.png",
    "ch0_legacy": "0/Ch0 - Moonlit Revenant_ A Gothic Family Legacy.png",
}
CUTS = {"ch0_legacy": [709]}      # name -> extra cut heights (source pixels), for pages whose gutters are slanted
BOX = (20, 16, 1516, 1008)      # inside the frame, in the 1536 x 1024 source
WIDTH = 456
MIN_ROW = 90                    # source pixels: a band thinner than this is not a row


def cream(a):
    return (a[..., 0] > 205) & (a[..., 1] > 180) & (a[..., 2] > 120) & (a[..., 0] - a[..., 2] < 110)


def gutters(im, frac):
    """Horizontal gutter runs across the whole page: (start, end) in source pixels."""
    a = np.asarray(im).astype(int)[BOX[1]:BOX[3], BOX[0]:BOX[2]]
    prof = cream(a).mean(axis=1)
    runs, i, n = [], 0, len(prof)
    while i < n:
        if prof[i] >= frac:
            j = i
            while j < n and prof[j] >= frac:
                j += 1
            if j - i >= 5 and i > 30 and j < n - 30:
                runs.append((i + BOX[1], j + BOX[1]))
            i = j
        else:
            i += 1
    return runs


def bands(name, im):
    cuts = [(a + b) // 2 for a, b in gutters(im, 0.9)]
    cuts += CUTS.get(name, [])
    cuts = sorted(cuts)
    edges = [BOX[1]] + cuts + [BOX[3]]
    return [(edges[k], edges[k + 1]) for k in range(len(edges) - 1) if edges[k + 1] - edges[k] >= MIN_ROW]


def main(out):
    os.makedirs(out, exist_ok=True)
    for name, rel in PAGES.items():
        im = Image.open(os.path.join(SRC, rel)).convert("RGB")
        scale = WIDTH / float(BOX[2] - BOX[0])
        page = im.crop(BOX)
        page = page.resize((WIDTH, int(round(page.height * scale))), Image.LANCZOS)
        page.save(os.path.join(out, name + ".png"), optimize=True)
        rows = bands(name, im)
        print("# %s  (%dx%d)" % (name, page.width, page.height))
        for k, (a, b) in enumerate(rows):
            y0 = int(round((a - BOX[1]) * scale))
            y1 = int(round((b - BOX[1]) * scale))
            print("beat 0 %d %d %d     # row %d" % (y0, WIDTH, y1 - y0, k + 1))
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "assets/Story/Comic")
