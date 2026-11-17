#!/usr/bin/env python3
"""Cuts the six arena panels out of the concept collection sheets (CC0, by the
project owner) and resizes them to the 480x480 screen.

Writes Arena<N>.png (empty floor, used behind the board) and Arena<N>Art.png
(with objects, used on menu and chapter screens) into the output directory.

Chapter order: 1 swamp, 2 gated graveyard, 3 catacombs, 4 cathedral hall (the
Family Crypt: banners, angels, candles), 5 overgrown ruins (the Chapel Grounds:
a ruined chapel), 6 castle terrace. The sheet panels are numbered left to right, top to
bottom: gated graveyard 1, cathedral 2, ruins 3, terrace 4, catacombs 5,
swamp 6.
"""
import os
import sys
from PIL import Image

EMPTY = "Extras/Concepts/Cemetery Map Collection - no objects render.png"
ART = "Extras/Concepts/Cemetery Map Collection - objects render.png"
PANEL_FOR_CHAPTER = {1: 6, 2: 1, 3: 5, 4: 2, 5: 3, 6: 4}


def panel(im, n):
    x, y = ((n - 1) % 3) * 512, ((n - 1) // 3) * 512
    return im.crop((x, y, x + 512, y + 512)).resize((480, 480), Image.LANCZOS)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/Arenas"
    os.makedirs(out, exist_ok=True)
    empty, art = Image.open(EMPTY).convert("RGB"), Image.open(ART).convert("RGB")
    for chapter, n in PANEL_FOR_CHAPTER.items():
        panel(empty, n).save(os.path.join(out, "Arena%d.png" % chapter), optimize=True)
        panel(art, n).save(os.path.join(out, "Arena%dArt.png" % chapter), optimize=True)
    print("wrote", out)
