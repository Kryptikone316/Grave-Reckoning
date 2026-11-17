#!/usr/bin/env python3
"""Draws the Grave Reckoning tile set (19x19 px) from hand-written pixel grids.

Art direction: Victorian Gothic. The colours and cast follow the project's
concept sheet (Extras/Concepts, CC0); every sprite here is redrawn at tile
size, flatter and more top-down than the sheet. All output is CC0.

Run:  python3 tools/make_art.py [out-dir]        (needs Pillow)
Writes opaque tiles (backdrop baked in) plus preview boards.
"""
import os
import sys
from PIL import Image

S = 19

PAL = {
    "k": (20, 14, 24),        # ink outline
    # revenant
    "H": (118, 88, 62), "h": (74, 54, 44), "L": (160, 126, 94),
    "G": (128, 164, 104), "g": (80, 112, 72), "E": (255, 176, 64),
    "R": (158, 40, 50), "r": (96, 26, 36),
    # constable
    "C": (72, 90, 120), "c": (42, 48, 70),
    # top-hat gent
    "T": (46, 38, 54), "t": (84, 70, 92),
    # mourner
    "M": (138, 122, 132), "m": (86, 74, 92), "V": (52, 40, 60),
    # skin
    "S": (222, 178, 134), "s": (166, 120, 96),
    # gravedigger
    "P": (128, 98, 70), "p": (136, 140, 120), "q": (96, 100, 88),
    # lantern / brass
    "Y": (255, 232, 150), "y": (244, 164, 64), "B": (204, 156, 74), "b": (124, 88, 42),
    # iron
    "I": (84, 90, 104), "i": (156, 162, 176),
    # bone
    "W": (236, 228, 200), "w": (168, 158, 132),
    # soul flame
    "F": (120, 236, 255), "f": (44, 150, 222), "z": (220, 252, 255),
    # stone
    "D": (176, 172, 154), "d": (128, 122, 110), "e": (86, 80, 74), "u": (98, 124, 72),
    "n": (112, 76, 50), "N": (66, 44, 32),   # coffin wood
    # earth
    "m2": (0, 0, 0),
    "o": (82, 58, 46), "O": (54, 40, 34),
    # pit
    "v": (8, 6, 12),
}
PAL.pop("m2")

BACK_PURSUER = (70, 50, 26)      # warm lamplight pool
BACK_ENTOMB = (26, 50, 62)
BACK_OFFER = (22, 44, 62)
BACK_ROLL = (64, 26, 36)


def rows_fit(rows):
    out = []
    for r in rows:
        while len(r) > S and r.endswith("."):
            r = r[:-1]
        assert len(r) <= S, "row too wide (%d): %r" % (len(r), r)
        out.append(r.ljust(S, "."))
    while len(out) < S:
        out.append("." * S)
    assert len(out) == S, "too many rows"
    return out


def sprite(rows):
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    for y, row in enumerate(rows_fit(rows)):
        for x, ch in enumerate(row):
            if ch != ".":
                im.putpixel((x, y), PAL[ch] + (255,))
    return im


def ground_pool(colour, cx=9, cy=16, rx=7, ry=2):
    """A small lit patch of floor under a figure: it grounds the sprite
    without turning it into a coloured square."""
    pool = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    px = pool.load()
    for y in range(S):
        for x in range(S):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                px[x, y] = tuple(colour) + (255,)
    return pool


def over(im, back=None, pool=None):
    """Sprites stay transparent so they stand on whatever floor the chapter
    has. `pool` is an optional ground glow drawn under the figure."""
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    if pool is not None:
        base.alpha_composite(pool)
    base.alpha_composite(im)
    return base


def mirror(im):
    return im.transpose(Image.FLIP_LEFT_RIGHT)


# ====================================================== revenant (3 views)
REV_DOWN = [
    "",
    "......kkkkkkk......",
    ".....kHHHHHHHk.....",
    "....kHLLHHHHHhk....",
    "...kHLHkkkkkHHhk...",
    "...kHHkGGGGGkHhk...",
    "...kHhkGEGEGkhhk...",
    "...kHhkGGGGGkhhk...",
    "....kHkkGgGkkhk....",
    ".....kkRRRRRkk.....",
    "....kHHRRrRRHHk....",
    "...kHHHhRRRhHHHk...",
    "..kGkHHhhhhhHHkGk..",
    "..kGkHHhHhHhHHkGk..",
    "...kkHhHhHHhHHkk...",
    "....kHhHhHhHhHk....",
]
REV_DOWN_LEGS = [
    ["....kHkHhkHhkHk....", "...kkh.kk.kk.hkk...", "...kk..k...k..kk..."],
    ["....kHkHhkHhkHk....", "....khk.kk.kkhk.....", "....kk..k...kk......"],
]

REV_UP = [
    "",
    "......kkkkkkk......",
    ".....kHHHHHHHk.....",
    "....kHHHLLHHHHk....",
    "...kHHHHLLHHHHhk...",
    "...kHHHHHHHHHHhk...",
    "...kHHHHhhHHHHhk...",
    "...kHHhHHhhHHhhk...",
    "....kHHhHHhHHhk....",
    ".....kRRRRRRRk.....",
    "....kHHRRrRRRHk....",
    "...kHHHhRRRhHHHk...",
    "..kGkHHhhhhhHHkGk..",
    "..kGkHHhHhHhHHkGk..",
    "...kkHhHhHHhHHkk...",
    "....kHhHhHhHhHk....",
]
REV_UP_LEGS = REV_DOWN_LEGS

REV_RIGHT = [
    "",
    "......kkkkkk.......",
    ".....kHHHHHHk......",
    "....kHLLHHHHHk.....",
    "...kHLHHkkkkHhk....",
    "...kHHHkGGGGkhk....",
    "...kHHhkGGEGkhk....",
    "....kHhkGGGGkk.....",
    "....kHhkkGgkk......",
    ".....kHhRRRRk......",
    "....kHHhRRrRHk.....",
    "....kHHHhRRHHHkk...",
    "....kHHhhhHHHkGGk..",
    "....kHHhHhHHHkGGk..",
    ".....kHhHhHHhkkk...",
    ".....kHhHhHhHk.....",
]
REV_RIGHT_LEGS = [
    ["....kHkHhkhHk......", "....khh.kkkhk......", "....kk...kk.kk....."],
    ["....kHhkHhkHk......", ".....khkhhk.kk.....", "......kk..kk......."],
]


def with_legs(body, legs):
    return sprite(body + legs)


GLOW = (92, 214, 190)   # the revenant's cold outline: it must be findable in a crowded board


def glow_outline(im):
    """Replace the revenant's outer outline with a pale teal rim so the player
    can always find it among the blocks."""
    src = im.load()
    out = im.copy()
    px = out.load()
    for y in range(S):
        for x in range(S):
            if src[x, y][3] and src[x, y][:3] == PAL["k"]:
                edge = False
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if nx < 0 or ny < 0 or nx >= S or ny >= S or src[nx, ny][3] == 0:
                        edge = True
                if edge:
                    px[x, y] = GLOW + (255,)
    return out


def revenant_frames():
    out = {}
    for name, body, legs in (("Down", REV_DOWN, REV_DOWN_LEGS), ("Up", REV_UP, REV_UP_LEGS),
                             ("Right", REV_RIGHT, REV_RIGHT_LEGS)):
        for i in range(2):
            out["Revenant%s%d" % (name, i)] = with_legs(body, legs[i])
    out["RevenantLeft0"] = mirror(out["RevenantRight0"])
    out["RevenantLeft1"] = mirror(out["RevenantRight1"])
    return {k: glow_outline(v) for k, v in out.items()}


# ============================================================ pursuers
CONSTABLE = [
    "",
    "......kkkkkkk......",
    ".....kCCCCCCCk.....",
    "....kCCCCCCCCCk....",
    "....kkBBkBBBkkk....",
    ".....kSSSSSSSk.....",
    ".....kSkSSSkSk.....",
    ".....kSSSsSSSk.....",
    "......kSSSSSk......",
    ".....kCCCCCCCk.....",
    "....kCCCBCCCCCk....",
    "...kCCCCBCCCCCCk...",
    "...kCCCCBCCCCCkyk..",
    "...kSkCCBCCCCkYyyk.",
    "....kkCCBCCCCkyyyk.",
    ".....kCCCCCCCk.kk..",
]
CONSTABLE_LEGS = [
    [".....kcCCkcCCk.....", ".....kckk.kckk.....", "....kkk....kkk....."],
    [".....kcCCkcCCk.....", "......kck.kck......", "......kk...kk......"],
]

TOPHAT = [
    "",
    ".......kkkkk.......",
    "......kTTTTTk......",
    "......kTTTTTk......",
    ".....kkkkkkkkk.....",
    ".....kSSSSSSSk.....",
    ".....kSkSSSkSk.....",
    ".....kSsTTTsSk.....",
    "......kSSSSSk......",
    ".....kRRRRRRRk.....",
    "....kTTtRRRtTTk....",
    "...kTTTTtRtTTTTk...",
    "...kTTTTTtTTTTkBk..",
    "...kSkTTTtTTTkBbk..",
    "....kkTTTtTTTkBk...",
    ".....kTTTTTTTkk....",
]
TOPHAT_LEGS = CONSTABLE_LEGS

MOURNER = [
    "",
    "......kkkkkkk......",
    ".....kMMMMMMMk.....",
    "....kMMMMMMMMMk....",
    "....kMMkVVVkMMk....",
    "....kMkVSSSVkMk....",
    "....kMkVSsSVkMk....",
    "....kMMkVVVkMMk....",
    ".....kMMMMMMMk.....",
    "....kMMmMMMmMMk....",
    "...kMMMmMMMmMMMk...",
    "...kMMMmMMMmMMMk...",
    "...kSkMmMBMmMkSk...",
    "...kkMMmMMMmMMkk...",
    "...kMMMmMMMmMMMk...",
    "..kMMMMmMMMmMMMMk..",
]
MOURNER_LEGS = [
    [".kMMMMmMMMMmMMMMk.", ".kmmmmmmmmmmmmmmmk.", "..kkkkkkkkkkkkkkk.."],
    ["..kMMMMmMMMmMMMk..", "..kmmmmmmmmmmmmmk..", "...kkkkkkkkkkkkk..."],
]

DIGGER = [
    "",
    "......kkkkkkk......",
    ".....kPPPPPPPPk....",
    "....kPPPPPPPPPPk...",
    "....kkkkkkkkkkk....",
    ".....kSSSSSSSk.....",
    ".....kSkSSSkSk.....",
    ".....kSSSsSSSk.....",
    "......kSSSSSk......",
    ".....kppppppk.i....",
    "....kppqppqpk.i....",
    "...kppppqpppkkIi...",
    "...kppqppppqkkSIi..",
    "...kSkppqpqpk.kIi..",
    "....kkppppppk..Ii..",
    ".....kqppppqk..kk..",
]
DIGGER_LEGS = [
    [".....kqqkkqqk.....", ".....kPPk.kPPk.....", "....kkkk...kkkk...."],
    [".....kqqkkqqk.....", "......kPk.kPk......", "......kk...kk......"],
]


def pursuer_frames():
    out = {}
    for name, body, legs in (("Constable", CONSTABLE, CONSTABLE_LEGS), ("Tophat", TOPHAT, TOPHAT_LEGS),
                             ("Mourner", MOURNER, MOURNER_LEGS), ("Digger", DIGGER, DIGGER_LEGS)):
        for i in range(2):
            out["%s%d" % (name, i)] = with_legs(body, legs[i])
    return out


def stone_overlay(im, rows_covered, cap=False):
    """Stack stones up the pursuer from the bottom: the entombment frames."""
    px = im.load()
    for y in range(S - 1, S - 1 - rows_covered, -1):
        for x in range(1, S - 1):
            band = (S - 1 - y) // 3
            lit = ((x + band * 2) % 6)
            col = PAL["D"] if lit < 2 else (PAL["d"] if lit < 5 else PAL["e"])
            if (S - 1 - y) % 3 == 0:
                col = PAL["e"]
            px[x, y] = col + (255,)
    if cap:
        for y in range(2, S - 1):
            for x in range(1, S - 1):
                px[x, y] = PAL["d"] + (255,)
        for x in range(1, S - 1):
            px[x, 2] = PAL["D"] + (255,)
            px[x, 3] = PAL["D"] + (255,)
        for y in range(4, 15):
            px[9, y] = PAL["W"] + (255,)
        for x in range(6, 13):
            px[x, 7] = PAL["W"] + (255,)
    return im


def entomb_frames(base_sprite):
    frames = []
    for n, cap in ((5, False), (9, False), (14, False)):
        im = over(base_sprite, pool=ground_pool((66, 46, 22)))
        frames.append(stone_overlay(im, n, cap))
    frames.append(over(sprite(HUMAN_ENTOMBED)))   # sealed: a coffin with a bone cross
    return frames


# An entombed pursuer is shown as a sealed coffin with a bone cross.
_COFFIN = [
    "...................",
    ".......kkkkk.......",
    "......knnnnnk......",
    ".....knnnnnnnk.....",
    "....krnnnnnnnrk....",
    "....knnnnWnnnnk....",
    "....knnnWWWnnnk....",
    "....knnnnWnnnnk....",
    "....knnnnWnnnnk....",
    "....knnnnWnnnnk....",
    ".....knnnWnnnk.....",
    "....krnnnnnnnrk....",
    "......knnnnnk......",
    ".......kkkkk.......",
]
HUMAN_ENTOMBED = []
for _r in _COFFIN:
    if "n" in _r:
        i = _r.rindex("n")
        _r = _r[:i] + "N" + _r[i + 1:]
    HUMAN_ENTOMBED.append(_r)


# ============================================================ objects
OFFERING = [
    "",
    "",
    ".........f.........",
    "........fFf........",
    ".......fFzFf.......",
    ".......fFzFf.......",
    "......fFFzFFf......",
    "......fFzzzFf......",
    ".......fFzFf.......",
    "........fFf........",
    ".......kBBBk.......",
    ".....kkBBBBBkk.....",
    "....kBBbbbbbBBk....",
    ".....kBBBBBBBk.....",
    "......kkbbbkk......",
]

MANTRAP = [
    "",
    "",
    "",
    "..kkkkkkkkkkkkkkk..",
    "..kiiiiiiiiiiiiik..",
    "..kIIIIIIIIIIIIIk..",
    "...kikikikikikik...",
    "....k.k.k.k.k.k....",
    "",
    "....kkkkkkkkkkk....",
    "....kBBbbbbbbBk....",
    "....kkkkkkkkkkk....",
    "",
    "....k.k.k.k.k.k....",
    "...kikikikikikik...",
    "..kIIIIIIIIIIIIIk..",
    "..kiiiiiiiiiiiiik..",
    "..kkkkkkkkkkkkkkk..",
]

SKULL = [
    "",
    "",
    ".......kkkkk.......",
    ".....kkWWWWWkk.....",
    "....kWWWWWWWWWk....",
    "...kWWWWWWWWWWWk...",
    "...kWWWWWWWWWWwk...",
    "...kWkkkWWWkkkWk...",
    "...kWkkkWWWkkkwk...",
    "...kWkkkWWWkkkwk...",
    "...kWWWWWkWWWWwk...",
    "....kWWWWkWWWwk....",
    ".....kWWWWWWwk.....",
    "......kWkWkWk......",
    "......kWkWkWk......",
    ".......kkkkk.......",
]

PIT = [
    "",
    "",
    ".....kkkkkkkkk.....",
    "...kkddDDDDDddkk...",
    "..kdDDddddddddDdk..",
    "..kdDdvvvvvvvdDdk..",
    ".kdDddvvvvvvvddDdk.",
    ".kdDdvvvvvvvvvdDdk.",
    ".kdDdvvvvvvvvvdDdk.",
    ".kdDdvvvvvvvvvdDdk.",
    ".kdDdvvvvvvvvvdDdk.",
    ".kdDddvvvvvvvddDdk.",
    "..kdDddvvvvvddDdk..",
    "..keddDDddddDDdek..",
    "...keeddDDDddeek...",
    ".....kkeeeeekk.....",
]

REV_GRAVE = [
    "",
    "",
    "",
    "",
    "",
    ".......kkkkk.......",
    "......kHHHHHk......",
    ".....kHkkkkkHk.....",
    "....kkHkGEGkHkk....",
    "...kddkHkGGGkHkdd..",
    "..kdDdkkHkkkHkDdk..",
    "..kdDdvvkkkkvvDdk..",
    "..kdDdvvvvvvvvDdk..",
    "...kdddvvvvvdddk...",
    "....keeddddddeek...",
    ".....kkkeeeekkk....",
]


# ============================================================ blocks
# Every movable block is the same plain stone: no emblems, because a pushable
# thing must never read as a collectible or a matching icon. A chapter only
# changes the material (moss, slate, bone, ivy, marble, dark stone) and each
# block gets one of three crack patterns so a field of them is not a stamp.
BLOCK_MATERIALS = [
    # light, mid, dark, speckle
    ((156, 174, 132), (108, 128, 96), (66, 82, 62), (78, 132, 72)),     # 1 mossy
    ((184, 184, 198), (132, 132, 150), (90, 90, 108), (156, 156, 172)),  # 2 slate
    ((220, 210, 180), (172, 160, 130), (122, 110, 88), (196, 186, 156)), # 3 bone
    ((224, 226, 236), (170, 174, 190), (120, 124, 142), (196, 200, 214)),# 4 marble (the family crypt)
    ((156, 156, 140), (114, 114, 102), (78, 78, 70), (70, 112, 64)),     # 5 ivy (the ruined chapel)
    ((156, 140, 156), (108, 94, 112), (72, 62, 78), (140, 120, 144)),    # 6 dark stone
]
CRACKS = [
    [(5, 4), (6, 5), (6, 6), (7, 7), (7, 8), (8, 9)],
    [(11, 3), (11, 4), (10, 5), (10, 6), (9, 7), (9, 8), (8, 9), (8, 10)],
    [(4, 10), (5, 10), (5, 11), (6, 12), (7, 12), (8, 13), (13, 6), (13, 7), (12, 8)],
]


def block_tile(material, crack):
    light, mid, dark, speck = material
    ink = PAL["k"]
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    px = im.load()
    # the stone occupies 17 x 17 inside the 19 x 19 cell, so neighbours show a seam
    x0, y0, x1, y1 = 1, 1, S - 3, S - 3
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            edge = x in (x0, x1) or y in (y0, y1)
            if edge:
                px[x, y] = ink + (255,)
            elif y <= y0 + 2 or x <= x0 + 2:
                px[x, y] = light + (255,)
            elif y >= y1 - 2 or x >= x1 - 2:
                px[x, y] = dark + (255,)
            else:
                px[x, y] = mid + (255,)
    # a slab seam across the middle
    for x in range(x0 + 3, x1 - 2):
        px[x, 8] = dark + (255,)
    for cx, cy in CRACKS[crack % 3]:
        px[cx + 1, cy + 1] = dark + (255,)
    for sx, sy in ((4, 13), (12, 12), (13, 4), (6, 3)):
        px[sx + crack, sy] = speck + (255,)
    # drop shadow along the bottom and right, on the floor
    for i in range(x0 + 1, x1 + 2):
        px[i, y1 + 1] = (10, 8, 14, 255)
    for i in range(y0 + 1, y1 + 2):
        px[x1 + 1, i] = (10, 8, 14, 255)
    return im


# ============================================================ walls
def wall_roles():
    roles = [[0] * S for _ in range(S)]
    for y in range(S):
        r, row_in = divmod(y, 4)
        if row_in == 3:
            continue
        off = 0 if r % 2 == 0 else 5
        for x in range(S):
            xx = (x + off) % 10
            if xx == 9:
                continue
            roles[y][x] = 1 if (row_in == 0 or xx == 0) else 2
    return roles


ROLES = wall_roles()


def wall_tile(m, l, d):
    im = Image.new("RGBA", (S, S))
    for y in range(S):
        for x in range(S):
            im.putpixel((x, y), tuple((m, l, d)[ROLES[y][x]]) + (255,))
    return im


def wall_png():
    im = Image.new("RGB", (S, S))
    for y in range(S):
        for x in range(S):
            im.putpixel((x, y), (ROLES[y][x], 0, 0))
    return im


# (name, mortar, lit brick, shaded brick)
WALL_THEMES = [
    ("Crypt Stone", (44, 46, 58), (108, 112, 132), (72, 76, 96)),
    ("Bone Ash", (66, 62, 56), (188, 178, 156), (132, 124, 108)),
    ("Oxblood", (52, 28, 32), (150, 50, 56), (100, 30, 38)),
    ("Old Brass", (52, 40, 26), (180, 130, 62), (122, 84, 38)),
    ("Mossy Vault", (32, 44, 36), (88, 130, 92), (54, 86, 60)),
    ("Midnight", (26, 30, 52), (68, 84, 170), (40, 52, 114)),
    ("Violet Vigil", (44, 32, 58), (142, 98, 190), (94, 60, 136)),
    ("Plague Green", (38, 44, 26), (138, 152, 70), (92, 104, 44)),
    ("Moonstone", (58, 64, 74), (208, 220, 230), (148, 162, 176)),
    ("Ember", (54, 32, 22), (210, 104, 48), (140, 64, 28)),
]

# ============================================================ chapters
# floor colour, a lighter seam, and speckle colour for each chapter.
CHAPTERS = [
    ("The Forgotten Grave", (34, 40, 34), (42, 50, 42), (28, 33, 28)),
    ("The Old Cemetery", (38, 40, 52), (47, 50, 64), (31, 33, 44)),
    ("The Ossuary", (50, 42, 38), (60, 52, 46), (42, 35, 32)),
    ("The Family Crypt", (42, 34, 48), (52, 43, 59), (35, 28, 40)),
    ("The Chapel Grounds", (34, 40, 38), (43, 50, 47), (28, 33, 31)),
    ("The Ancestral Mausoleum", (48, 30, 34), (59, 38, 42), (40, 25, 28)),
]


def floor_tile(base, hi, lo, seed):
    im = Image.new("RGBA", (S, S), base + (255,))
    px = im.load()
    for i in range(S):
        px[i, 0] = hi + (255,)
        px[0, i] = hi + (255,)
    n = seed * 7 + 3
    for k in range(7):
        x = (n * (k + 3) * 5) % (S - 2) + 1
        y = (n * (k + 5) * 3) % (S - 2) + 1
        px[x, y] = lo + (255,)
    return im


# ============================================================ re-burial
def death_frames(rev):
    frames = []
    base = rev.copy()
    for f in range(7):
        im = base.copy()
        px = im.load()
        cover = min(19, 3 * (f + 1))
        for y in range(S - 1, S - 1 - cover, -1):
            for x in range(S):
                if f < 6 or px[x, y][3]:
                    px[x, y] = (PAL["O"] if (x + y + f) % 7 == 0 else PAL["o"]) + (255,)
        if f == 6:
            im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
            px = im.load()
            for y in range(12, S):
                for x in range(3, 16):
                    if y >= 12 + abs(x - 9) // 3:
                        px[x, y] = PAL["o"] + (255,)
            for y in range(4, 12):
                px[9, y] = PAL["D"] + (255,)
            for x in range(7, 12):
                px[x, 6] = PAL["D"] + (255,)
        frames.append(im)
    return frames


# ============================================================ output
def save(im, out, name):
    """Floors are opaque; everything else keeps its transparency."""
    if name.startswith("Chapters/"):
        im = im.convert("RGB")
    im.save(os.path.join(out, name + ".png"))


def build(out):
    os.makedirs(os.path.join(out, "Chapters"), exist_ok=True)
    os.makedirs(os.path.join(out, "Entomb"), exist_ok=True)
    os.makedirs(os.path.join(out, "Death"), exist_ok=True)
    tiles = {}
    rev = revenant_frames()
    for k, v in rev.items():
        tiles[k] = over(v, pool=ground_pool((22, 56, 56)))
    pur = pursuer_frames()
    for k, v in pur.items():
        tiles[k] = over(v, pool=ground_pool((66, 46, 22)))
    tiles["Offering"] = over(sprite(OFFERING), pool=ground_pool((20, 52, 76), 9, 12, 8, 6))
    tiles["Mantrap"] = over(sprite(MANTRAP))
    tiles["Skull"] = over(sprite(SKULL), pool=ground_pool((70, 24, 34), 9, 16, 6, 2))
    tiles["Grave"] = over(sprite(PIT))
    tiles["RevenantInGrave"] = over(glow_outline(sprite(REV_GRAVE)))
    for c, mat in enumerate(BLOCK_MATERIALS):
        for v in range(3):
            tiles["Block%d_%d" % (c + 1, v)] = block_tile(mat, v)
    for k, v in tiles.items():
        v.save(os.path.join(out, k + ".png"))
    for i, (cname, base, hi, lo) in enumerate(CHAPTERS):
        save(floor_tile(base, hi, lo, i + 1), out, "Chapters/Floor%d" % (i + 1))
    wall_png().save(os.path.join(out, "Wall.png"))
    for i, f in enumerate(entomb_frames(pur["Constable0"]), 1):
        save(f, out, "Entomb/frame%d" % i)
    for i, f in enumerate(death_frames(rev["RevenantDown0"]), 1):
        save(f, out, "Death/frame%d" % i)
    return tiles


def stand(floor, sprite_im, x, y, canvas):
    canvas.paste(floor, (x * S, y * S))
    canvas.alpha_composite(sprite_im, (x * S, y * S))


def preview(out, tiles):
    """One row per chapter: the chapter's floor with its blocks, walls, the
    revenant, the four pursuers and the hazards standing on it."""
    Z = 4
    cols = 16
    sheet = Image.new("RGBA", (cols * S, 6 * S), (0, 0, 0, 255))
    for c in range(6):
        floor = Image.open(os.path.join(out, "Chapters", "Floor%d.png" % (c + 1))).convert("RGBA")
        row = [("Wall", None), ("Block%d_0" % (c + 1), None), ("Block%d_1" % (c + 1), None),
               ("Block%d_2" % (c + 1), None), ("RevenantDown0", None), ("RevenantRight1", None),
               ("Constable0", None), ("Tophat1", None), ("Mourner0", None), ("Digger1", None),
               ("Offering", None), ("Mantrap", None), ("Grave", None), ("Skull", None),
               ("entomb", None), ("RevenantInGrave", None)]
        for x, (name, _) in enumerate(row):
            if name == "Wall":
                t = wall_tile(*WALL_THEMES[[4, 0, 1, 7, 8, 2][c]][1:])
            elif name == "entomb":
                t = Image.open(os.path.join(out, "Entomb", "frame4.png")).convert("RGBA")
            else:
                t = tiles[name]
            stand(floor, t, x, c, sheet)
    sheet.resize((cols * S * Z, 6 * S * Z), Image.NEAREST).convert("RGB").save(os.path.join(out, "preview-sheet.png"))


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "build/art"
    os.makedirs(out, exist_ok=True)
    t = build(out)
    preview(out, t)
    print("wrote", out)
