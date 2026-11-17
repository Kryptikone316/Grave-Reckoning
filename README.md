# Grave Reckoning — SF3500 native game

A Victorian Gothic arcade puzzle for the SF3500 handheld (C++/SDL2, software
RGB565 through the `hwdisp` wrapper; no GPU or runtime dependencies).

You are a revenant. Death has already happened: you are here to settle an
account. The living have come back to the cemetery with their lanterns because
they recognise you. Push the cemetery's own stones around them until each one
is sealed in. A sealed pursuer leaves an offering behind. Clear every wave and
the grave is settled.

The game plays the way *Rodent's Revenge* (Microsoft, 1991) did: push blocks,
pin the chasers against walls and each other, avoid the hazards.

## Controls

- D-pad: move (four directions); navigate menus
- L1/R1: switch tabs in Options, vigils in the Book of the Interred
- A or START: select
- START in a grave: pause menu ("Between Graves")
- B: back
- L1/R1: change vigil (difficulty) in the Book of the Interred
- SELECT+START: exit immediately

## How a grave plays

- The board is 21×17 cells; the outer ring is the arena's own wall, so you
  play on 19×15. Stones are movable and all behave the same; walls are fixed.
- Pursuers walk straight at you and cut corners, so a diagonal gap is an open
  door. Touching one costs a return (a life): you are dragged back under and
  rise again. You have three returns.
- A pursuer with no free square around it is sealed in (interred: the stones
  are the tomb). When every pursuer on the board is sealed, the bell tolls and
  they become blue flames, each a memory of the night of the burial (+100 to
  collect). A stone that shifts and opens a gap wakes a sealed pursuer.
- Pursuers pushed into an open grave by a chain of stones fall in and are
  gone, with no flame. The living are also careless: each step, a pursuer
  has a small chance to ignore where it is going and walk straight into an
  open grave (Mourners 30%, the Watch 18%, the Faithful 8%). Either way they
  were never sealed, so nothing is left behind.
- Open graves trap you for a few seconds; mantraps kill; rolling skulls kill.
- If you are sealed in and nothing you can do will ever change the board, the
  game says "Sealed In" after two seconds and offers a restart or the menu.
  The check only fires on proof (closed region, unpushable stones, nothing
  able to get in); any doubt means keep playing.
- Each completed grave adds 10 toward an extra return, awarded at 100 (the
  counter starts at 50).

## Chapters and vigils

Thirty graves per vigil, six chapters of five, each with its own architecture
and arena:

| Chapter | Place |
|---|---|
| I | The Forgotten Grave: flooded burial ground |
| II | The Old Cemetery: headstones and fenced plots |
| III | The Ossuary: niches, pillars and a bone pit |
| IV | The Family Crypt: aisles, pews and a tomb in the nave |
| V | The Chapel Grounds: a ruined chapel, walls broken by time |
| VI | The Ancestral Mausoleum: balustrades and a tomb house |

| | Easy: The Mourners | Normal: The Watch | Hard: The Faithful |
|---|---|---|---|
| Pursuers per wave | 1–3 (one at a time on graves 1–6) | 1–6 | 2–7 |
| Wave gap | 54 s down to 35 s | 36 s down to 27 s | 24 s down to 19 s |
| Pursuer speed | 1 step per 2.4 s | 1 per 1.6 s | 1 per 1.1 s |
| Stones on the board | 130 at grave 1, thinning to 78 | 118 thinning to 66 | 110 thinning to 58 |
| Hazards | late and few | scaled by grave | extra, scaled |

Layouts are fixed per grave and vigil (seeded), so a retry is the same
puzzle; only the pursuers' spawn squares vary. The numbers live in
`Game::configure_level()` and `make_levels()` in `src/game.hpp`.

## Testing without the device

`tools/simtest.cpp` runs the real rules (`src/game.hpp`) headless:
`g++ -std=c++11 -O2 -o build/sim/simtest tools/simtest.cpp`, then
`simtest check` (every layout reachable), `simtest view --diff 1 --stage 8`
(print a board), `simtest selftest` (walled-in detector), `simtest fuzz`
(bookkeeping invariants), `simtest bot` (a look-ahead bot clears stages and
reports deaths and stalls; `--width 20 --depth 10` selects the beam bot) and
`tools/simall.sh` (parallel sweep). The bot is weaker than a person against
slow pursuers, so read its numbers as a ranking of stages, not as difficulty.

`tools/textaudit.sh` renders every screen and state and reports any text that leaves the
panel, button, caption or cell it sits in (the game's `--audit` mode); it should print
only its summary line. `tools/shot.sh out.bmp --screen game --diff 1 --stage 8 --ticks 200` renders a
screen of the MIPS build under qemu (`--screen main|play|stage|scores|options|
help|exit|card|pause|game|story --page NAME --ticks PANELS`).

## Music

Each chapter has its own two tracks (`assets/Music/Ch1 - ...` to `Ch6 - ...`) and the
title, menus, Journal and Options play the `Ch0` pair. The game plays a chapter's
tracks in random order while you are in one of its graves or reading its story pages,
and fades (half a second) when the music changes. The soundtrack is album-normalised:
every track is at about -15 LUFS with a true peak at or below -1 dBTP, by ReplayGain tags
that the player applies, with no re-encoding (`tools/normalize_music.py`).

Playback is built to avoid clicks: the music thread keeps about a quarter of a second
queued ahead of the audio thread, the audio thread runs on an absolute-time schedule, and
if it ever runs dry it fades instead of cutting to silence (underruns are counted in
`log.txt` when the game closes). The SF3500's speakers do not play the left channel (the
right channel plays through both), so on the speakers the player blends left and right,
detecting the headphone jack from the device tree and a read-only GPIO register;
headphones always get stereo. The Sound tab can switch the blend off.
`tools/musictest/` checks the player's switching, fades and cushion under qemu.

## Title screens

The game opens on a title picture (letterboxed, A or START to continue) before the menu.
`Moonlit Abbey Ruins` is always there. `The Mausoleum` unlocks when the third chapter is
settled and `Mausoleum II` when the last grave is settled; the Story tab of Options
chooses which one is shown. (`tools/make_titles.py` resizes the concept art in
`Extras/Concepts/Title Screen/`.)

## Options and the pause menu

Options has three tabs (L1/R1 to switch): **Sound** (music volume, mute, speaker mix),
**Look** (wall colour: each chapter's own, or one of ten) and **Story** (comic
pages at the start of a chapter: every chapter, first time only, or off; and
a **Title** picture and "Replay the Prologue", which unlocks once the prologue has been seen in play). A value
row works as: A to start changing it, LEFT/RIGHT to change, A to confirm, B to cancel;
switches (mute, speaker mix) and actions need only A. The pause menu ("Between Graves", START) carries what
matters mid-grave: resume, music volume, mute, begin the grave anew, and
return to the main menu.

## Polish: sound effects, screen effects, hints

Everything here has a switch in **Options** (Sound: *Sound Effects*; Look: *Screen Effects*, *Hints*).

- **Sound effects** (`assets/Sfx/`, mono 48 kHz WAVs made by `tools/make_sfx.py`, CC0): a stone scraping
  as it is pushed, a heavy settling as a pursuer is sealed, a lantern guttering and then a bell when the
  flames rise, a chime for each flame gathered, a dull fall into a grave, a toll when the revenant is
  caught, two notes as a grave is settled, a low pulse for a new wave, and quiet ticks and a paper
  rustle in the menus and the comic pages. They are mixed into the audio thread 10 ms at a time
  (`src/sfxmix.c`), so they land on the beat of the action and never wait behind the music queue.
  Their level is independent of the music volume. Edit or replace any WAV (same format).
- **Screen effects:** a short fade between screens, a flash and shake when the flames rise (and a
  red one when the revenant falls), dust off a pushed stone, lanterns that waver and go dark when
  their bearer is sealed in, comic strips that draw in from the left, and a living title (drifting
  mist, falling leaves, flickering braziers).
- **Hints:** four one-line reminders in the very first grave, each put away the moment the player
  does the thing it mentions (turning Hints back on plays them again).
- **In the menus:** stage select and the Book of the Interred mark a settled grave with a brass dot
  and one settled without losing a life with a blue flame, and **Continue** at the top of the main menu
  picks up the first unsettled grave of the vigil last played.

`--autoplay N` (with `--diff` and `--stage`) plays the live loop headless for N frames with scripted
input and prints the events it saw: a quick check that nothing in the grave's event handling crashes.

## Story pages

Cut-scenes are still comic art, no animation or sound. `assets/Story/story.txt`
describes the pages in plain text and the game reads it at start-up, so the story can
be reordered or reworded without touching the code.

- **Comic pages** (layout `comic`): the whole text-free comic page (`assets/Story/Comic/`,
  456 px wide, made by `tools/slice_comic.py` from `Extras/Concepts/Story/Chapters/`) is
  revealed one row of panels at a time with A. The words (a caption and any spoken line)
  sit in a band under the art, never over it. Each page lists its rows as `beat x y w h`
  lines, which the tool prints.
- **Panel pages** (layouts `1`, `2`, `2s`, `3`, `4`): a few cropped panels from
  `assets/Story/Panels/` with a caption box and speech balloon over the art. Used by the
  prologue, the two ending plates and chapter 6's keeper page (`tools/slice_story.py`).
- **Parchment** (`epilogue`): plain lettering on parchment.

Pages are looked up by name: `intro` (the first time the game is played); `ch1`..`ch6`
at the start of each chapter; `ch1b`..`ch5b` at the start of the *next* chapter, before
its own pages (the lead-up pages that bridge two chapters, so the first thing chapter 3
shows is the walk into the ossuary); and `end`..`end4` (plus `end2b`) then `epilogue` after the last
grave. Pages with the same name play in file order. START or B skips. Replace any PNG
with art of the same size and the game uses it.

## The Journal

The main menu's Journal is the casebook of an unnamed Victorian investigator
who is following the story, probably to their own grave. It is a gallery of
pages (`assets/Story/journal.txt`, in story order): clippings and registers
the investigator collected, and notes in their own hand. Pages start sealed
and are turned over as chapters are settled (and, for the notes, as graves
are settled in total; a few clippings and sayings as lanterns are put out). There are 36
pages in the casebook, five to a row, and the grid scrolls.

The Journal is a second book too, **Earlier Hands** (L1 / R1 to turn to it): pages from the
keepers who held the book before the player, bound in because of one detail or another. It is
a community's book, lent from a teacher to a protege, and its flyleaf says so. The four
hands are the four years the oldest registers mark: 1264 (a chronicle of a distant atrocity,
copied in for a single marginal word), 1587 (a deposition and a physician who took it for
witchcraft), 1741 (a naturalist who took it for a distemper of the blood) and 1864 (the
keeper before ours). Each writes in the manner of its century, each is wrong in its own terms,
and each ends badly. There is no achievements screen: a page comes loose quietly when the
player has done something the game already notices (pursuers who stumble into open graves,
a company laid to rest together, a grave settled without losing a life, flames gathered,
the way proved shut, chapters settled on the hardest vigil). A locked slot shows one line
in the writer's voice as the only hint. The Journal's first pages give the game its frame: the flyleaf says the book "does not tell
you what happened. It shows you. Read a grave, and you will stand in it", so each grave is a vigil
the investigator is made to live, and the Journal is the waking record. **The Record of the
Vigils** is the investigator's own running tally, filled in from the save (graves settled on each
vigil, lamps put out, flames gathered, graves taken untouched, the careless, the most laid down
together, the way shut, chapters kept with the Faithful), with a closing line that sours as the
hand frays. The Account Settled screen says "Entered in the book." The cost of progress is the
fraying handwriting itself.

Other keepers have written in the margins too, in their own hands (a teacher in blue-black
ink, an elder in faded brown, a sceptic in pencil): a few short, light notes that question or
argue with the pages and with the investigator's own notes, and settle nothing. They are the
`margin <hand> <words>` lines in `journal.txt` and are always there, whatever the player has
done. The counters are kept in `options.dat` (24 numbers
now, which also hold the settings; older files load with the new ones at their defaults) and the rules are the `unlock deed`
lines in `assets/Story/journal.txt`. The investigator's handwriting frays as the player
advances: underlines, circled words and jots crowd the margins of every
page, single words are struck through, and by the end the pen strikes through
whole lines. Pages can also carry authored redactions in `journal.txt`:
`[[struck through, still readable]]` and `{{blacked out}}` (an ink bar; the
words inside only set its length). A page of tally marks,
the Count, adds one stroke for every lantern put out (stored in `options.dat`).
Handwriting font: Covered By Your Grace (SIL OFL, `assets/Fonts/`).

## Art pipeline

- `tools/make_art.py`: draws every 19×19 sprite from pixel grids (Pillow).
- `tools/make_arenas.py`: cuts the six arenas out of the concept sheets in
  `Extras/Concepts/`.
- `tools/slice_comic.py`: crops the full comic pages for the comic viewer and prints their `beat` lines.
- `tools/slice_story.py`: cuts the older concept pages into the few panel-layout pages still used.
- `tools/make_panels.py`: fallback placeholder panels (not used while the concept art is present).
- `tools/make_backdrops.py`: early procedural backdrops (unused by the game).
- `Extras/Concepts/`: the concept sheets the look was drawn from.

## Building and installing

```sh
./build_mips.sh
```

Copy `build/gravereckoning/` to `/roms/ports/gravereckoning/` and
`build/Grave Reckoning.sh` into `/roms/ports/`. Copying only the new
`gravereckoning.mipsel` over an existing install keeps the saved files
(`highscores.dat`, `progress.dat`, `options.dat`).

To use a different title font on a personal copy, put a file called
`TitleFont.ttf` next to the game; it replaces the bundled blackletter.

## Credits and licensing

- **Public domain (CC0 1.0):** all art (sprites, tiles, arenas, comic pages
  and concept sheets), the story, lore and Journal text, the soundtrack and the level designs
  are the project owner's own work, dedicated to the public domain under CC0 1.0
  as a community offering for handheld gaming. No credit is required. See
  `LICENSE-ASSETS.txt`. The source code is under the MIT licence (`LICENSE`).
- **Music:** the soundtrack, composed by the project owner, CC0, one pair of tracks per
  chapter plus the title (see `assets/Music/README.txt`).
- **Title font:** Pirata One, SIL Open Font License 1.1
  (`assets/Fonts/PirataOne-OFL.txt`). On April 1st only, the game switches to
  UnifrakturCook Bold (SIL OFL 1.1, `assets/Fonts/UnifrakturCook-OFL.txt`), in which
  every lowercase "k" is drawn like an "f". It is an Easter egg; the rest of the year
  "Book of the Interred" is spelled the way it reads.
- **Handwriting font** (the Journal): Covered By Your Grace, SIL Open Font License 1.1
  (`assets/Fonts/CoveredByYourGrace-OFL.txt`).
- **Body font:** Bitstream Vera (`assets/Vera-COPYRIGHT.TXT`).
- **Inspiration:** *Rodent's Revenge* (Microsoft Entertainment Pack, 1991),
  and Maxx Boehme's Java remake *A Mouse's Vengeance*
  (<https://github.com/maxxboehme/A_Mouses_Vengeance>), which has no licence
  file. This game contains none of their art or level data.
- **Provenance:** this is a new game in the spirit of *Rodent's Revenge*
  and shares its design lineage: push blocks, pin pursuers that cut corners,
  hazards, waves. The rules module began as a C++ port of the Java game, and
  its pursuer movement, block pushing, rolling-skull handling, walled-in proof,
  level construction and naming have since been rewritten from scratch (see
  `tools/simtest.cpp` for the tests that hold the rules in place). Some
  numbers and conventions still descend from that port: the cap of eight
  pursuers, the 8-way pursuer step, the order of the `Direction` enum and the
  three difficulty names in the code. The art, levels, text, story and name are
  original.

## Known issues

- On the device the process segfaults at exit after the audio shutdown
  (`sound_driver_deinit`). It happens after the game has already quit.
- Nothing here has been tested on the device yet.
