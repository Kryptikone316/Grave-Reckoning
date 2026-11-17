#!/usr/bin/env python3
"""Resizes the three title screens (Extras/Concepts/Title Screen, CC0, by the
project owner) to the game: 480 px wide, whatever height keeps their shape.

  Title1.png  Moonlit Abbey Ruins   (the default title screen)
  Title2.png  Moonlit Mausoleum     (unlocked after the third chapter)
  Title3.png  Moonlit Mausoleum II  (unlocked after the last grave)

Run:  python3 tools/make_titles.py [out-dir]      (needs Pillow)
"""
import glob
import os
import sys
from PIL import Image

SRC = "Extras/Concepts/Title Screen"
PICKS = [("Title1.png", "*Abbey Ruins*"), ("Title2.png", "*Mausoleum- render 1*"), ("Title3.png", "*Mausoleum- render 2*")]

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/Title"
    os.makedirs(out, exist_ok=True)
    for name, pattern in PICKS:
        matches = sorted(glob.glob(os.path.join(SRC, pattern)))
        if not matches:
            print("missing", pattern)
            continue
        im = Image.open(matches[0]).convert("RGB")
        h = round(im.height * 480 / im.width)
        im.resize((480, h), Image.LANCZOS).save(os.path.join(out, name), optimize=True)
        print(name, 480, h)
