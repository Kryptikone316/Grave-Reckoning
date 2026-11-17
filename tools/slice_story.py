#!/usr/bin/env python3
"""Cuts the older concept comic pages (Extras/Concepts/Story/Chapters, CC0, by the
project owner) into the game's text-free panels. Chapters 1-5 are now shown from
the newer full pages by tools/slice_comic.py; this tool still makes the prologue
(intro_a, intro_b), the two ending plates (end_a, end_b) and chapter 6's keeper
page:

    ch6_a  wide establishing scene       456 x 212
    ch6_b  the revenant, close-up        224 x 212
    ch6_c  the speaker or the object     224 x 212

The concept pages have their narration note and speech balloon painted into
the art, which would be about 8 px tall on the 480 px screen, so each crop
is taken to leave them out. The game draws captions and balloons itself
(assets/Story/story.txt). When final panel art exists, save it over these
files with the same names and sizes (or any size: it is cropped to fit).

Run:  python3 tools/slice_story.py [out-dir]       (needs Pillow)
"""
import glob
import os
import sys
from PIL import Image

SRC = "Extras/Concepts/Story/Chapters"
FILES = {      # chapters 1-5 now use the full comic pages (tools/slice_comic.py); only chapter 6's keeper page is cut
    6: "6/Ch6 - The Ancestral Mausoleum Confrontation.png",
}
WIDE = (456, 212)
SQUARE = (224, 212)

# Boxes (left, top, right, bottom) in the 1536 x 1024 source. They are widened
# or narrowed to the panel's aspect ratio around their centre.
# The narration note sits in the top-left of the wide scene; the balloon in the
# middle of the lower row. Each box stays clear of both.
BOXES = {
    6: {"a": (440, 122, 1500, 528), "b": (60, 560, 536, 1000), "c": (880, 560, 1356, 1000)},
}


def crop_to(im, box, size):
    l, t, r, b = box
    cx, cy = (l + r) / 2, (t + b) / 2
    w, h = r - l, b - t
    want = size[0] / size[1]
    if w / h > want:
        w = h * want
    else:
        h = w / want
    region = im.crop((int(cx - w / 2), int(cy - h / 2), int(cx + w / 2), int(cy + h / 2)))
    return region.resize(size, Image.LANCZOS)


BIG = (456, 432)

# Pages that are not the six chapter pages. (file, output name, box, size)
SPECIAL = [
    ("0/Ch0 - The Funeral_ A Gothic Comic Prologue.png", "intro_a", (30, 120, 930, 528), WIDE),
    ("0/Ch0 - The Funeral_ A Gothic Comic Prologue.png", "intro_b", (460, 548, 1400, 996), WIDE),
    # the same tomb twice: the near burial date (1864) and the older one (1264)
    ("End/Ending*1864*.png", "end_a", (330, 118, 1290, 990), BIG),
    ("End/Ending*1264*.png", "end_b", (330, 118, 1290, 990), BIG),
]


def main(out):
    os.makedirs(out, exist_ok=True)
    sheet = Image.new("RGB", (6 * 240, 2 * 226 + 6), (60, 60, 70))
    for chapter, name in FILES.items():
        im = Image.open(os.path.join(SRC, name)).convert("RGB")
        for key, size in (("a", WIDE), ("b", SQUARE), ("c", SQUARE)):
            panel = crop_to(im, BOXES[chapter][key], size)
            panel.save(os.path.join(out, "ch%d_%s.png" % (chapter, key)), optimize=True)
            if key == "a":
                sheet.paste(panel.resize((228, 106)), ((chapter - 1) * 240 + 6, 6))
            else:
                sheet.paste(panel.resize((114, 106)), ((chapter - 1) * 240 + 6 + (0 if key == "b" else 114), 118))
    for name, target, box, size in SPECIAL:
        matches = sorted(glob.glob(os.path.join(SRC, name)))
        if matches:
            crop_to(Image.open(matches[0]).convert("RGB"), box, size).save(
                os.path.join(out, target + ".png"), optimize=True)
        else:
            print("missing", name)
    sheet.crop((0, 0, 6 * 240, 232)).save(os.path.join(out, "_story_contact.png"))
    print("wrote", out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "assets/Story/Panels")
