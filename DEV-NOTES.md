# Grave Reckoning: developer notes

Internal notes for anyone who wants to build, test, rebalance or extend the game.
The player-facing description is in [README.md](README.md); the story's rules are in
[Notes/LORE-CANON.md](Notes/LORE-CANON.md) (read it before writing any story or Journal text).

## Layout of the project

| Path | What it is |
|---|---|
| `src/game.hpp` | All the rules, header-only C++11, no SDL. Levels, pursuers, waves, skulls, the walled-in proof, deed counters, `Options` (saved settings and counters). |
| `src/gravereckoning.cpp` | The SDL2 front end: software-rendered RGB565 480x480 framebuffer at 24 FPS, menus, comic viewer, Journal, effects, hints, main loop. |
| `src/hwaudio.c`, `music.c`, `sfxmix.c`, `sfx.c` | Audio: the hardware thread, the Ogg music player, the sound-effect voice mixer and the WAV loader. |
| `src/hwdisp.c`, `joykey.c` | Display and input wrappers for the SF3500's firmware. |
| `assets/` | Everything the game loads: `Art`, `Arenas`, `Title`, `Story`, `Music`, `Sfx`, `Fonts`, `Vera.ttf`. |
| `tools/` | Test harness (`simtest.cpp`), asset pipelines (Python), screenshot and audit scripts. |
| `Extras/Concepts/` | The concept art and comic pages the game's art is cut from (CC0, the owner's). |
| `Notes/LORE-CANON.md` | Canon rules for the story, the Journal's voices and the "book shows you" bridge. |
| `docs/` | Screenshots used by the README. |

## Building and installing

```sh
./build_mips.sh
```

Cross-compiles with the SF3500 Buildroot toolchain (paths are at the top of the script and
can be overridden with `TOOLCHAIN_ROOT` and `SDL_FW`) and assembles `build/gravereckoning/`
(binary, assets, `libogg`/`libvorbisidec`, licence files, `launch.sh` as
`gravereckoning.sh`) plus `build/Grave Reckoning.sh`. The script prints a harmless
`rg: command not found` at the end if ripgrep is missing.

Install: copy `build/gravereckoning/` to `/roms/ports/gravereckoning/` and
`build/Grave Reckoning.sh` into `/roms/ports/`. Copying only `gravereckoning.mipsel` over an
existing install keeps the saves (`highscores.dat`, `progress.dat`, `options.dat`).

A personal title font can be dropped next to the game as `TitleFont.ttf`; it replaces the
bundled blackletter.

## Rules, in detail

- The board is 21x17 cells; the outer ring is the arena wall, so play happens on 19x15.
  Stones are interchangeable; walls are fixed.
- Pursuers path to the revenant down a flood-fill distance field and step 8 ways, so diagonal
  gaps are open. A step is checked against the live board; the field is only a guide.
- Touching a pursuer costs a life (three to start). A grave settled adds 10 toward an extra
  life, awarded at 100 (the counter starts at 50).
- A pursuer with no free neighbour is `SEALED`. When every pursuer on the board is sealed,
  after `seal_delay_cycles` further steps the bell tolls and they become `OFFERING`s
  (+100 to collect). A shifted stone that opens a gap wakes a sealed pursuer.
- A pursuer pushed into an open grave by a chain of stones falls in and leaves nothing. Each
  step a pursuer also has a `carelessness_percent()` chance (Easy 30, Normal 18, Hard 8) of
  following a field that ignores graves.
- Open graves hold the revenant for a few seconds; mantraps and rolling skulls kill.
- `walled_in_now()` is a proof, not a heuristic: closed region, unpushable stones, nothing
  able to enter (pursuers, skulls, graves, mantraps, future spawns). Any doubt returns false.
  After `WALLED_IN_HOLD_FRAMES` the game offers a restart.
- Layouts are fixed per grave and vigil (seeded); only pursuer spawn squares vary.

### Difficulty numbers

All in `Game::configure_level()`, `Game::set_pursuer_period()` and `make_levels()` in
`src/game.hpp`.

| | Easy: The Mourners | Normal: The Watch | Hard: The Faithful |
|---|---|---|---|
| Pursuers per wave | 1-3 (one at a time on graves 1-6) | 1-6 | 2-7 |
| Wave gap | 54 s falling to 35 s | 36 s to 27 s | 24 s to 19 s |
| Pursuer step | 57 frames (2.4 s) | 38 frames (1.6 s) | 26 frames (1.1 s) |
| Seal delay (steps) | 0 | 1 | 2 |
| Careless chance | 30% | 18% | 8% |
| Stones | 130 at grave 1, thinning to 78 | 118 to 66 | 110 to 58 |
| Hazards | late and few | scaled by grave | extra, scaled |

The step lengths were slowed on 2026-11-17 (were 1.5 / 1.0 / 0.67 s) after a playtester could
not clear Easy 1, and the seal delay (counted in steps) was cut so the wait before the flames
rise stays about three seconds. Waves arrive on the wave clock whether or not the last batch is
dead, so slow pursuers pile up if the player does not clear them; the clock runs ten times faster
while the board is empty. Balance is still a human-playtest question (the bot's weak spots are
Normal 26 and 30 and Hard 23, 26 and 30).

## Saves

`options.dat` is a plain array of 24 numbers: volume, muted, wall_theme, story_seen, journal,
lanterns, story_mode, speaker_mix, title_screen, stumbles, offerings, flawless, shut, rite, hard,
vigils, sfx, effects, hints, hints_seen, untouched[3], last_vigil. Loading an older (shorter)
file keeps the new fields at their defaults. `progress.dat` holds per-vigil unlocks and
`highscores.dat` the best scores. The deed counters (`stumbles`, `offerings`, `flawless`, `shut`,
`rite`, `hard`) feed the Journal's `unlock deed` rules and the Record of the Vigils page.

## Events, sound and effects

`Game` emits `GameEvent`s (`EV_PUSH`, `EV_SEAL`, `EV_RISE`, `EV_FALL`, `EV_DEATH`, `EV_OFFERING`,
`EV_WAVE`, `EV_GRAVE`, `EV_COMPLETE`); the front end drains them with `take_events()` (capped at 64
per frame) to play sounds and trigger effects, so the rules stay free of presentation.

- **Sound effects** (`assets/Sfx/*.wav`, 14 mono 48 kHz files from `tools/make_sfx.py`, CC0) are
  mixed in the audio thread by `sfxmix_into()` just before `sound_play`, 10 ms at a time, into 8
  voices (the oldest is stolen). Per-effect gains are in `src/sfx.c`. They are independent of the
  music volume. Replace a WAV with one of the same format to change a sound.
- **Screen effects** (all behind Options toggles): fades between screens, flash and shake on the
  rite and on a fall, dust off pushed stones, wavering lanterns, comic strips that draw in, a
  living title. Helpers: `fill_rect_alpha`, `ensure_glow`/`draw_glow`, `ensure_fog`, `draw_motes`,
  `apply_screen_effects`.
- **Hints:** four one-liners in the first grave, each dismissed when the player does the thing.

## Music

Each chapter has two tracks in `assets/Music/Ch<N> - <name>/`; `Ch0` covers the title, menus,
Journal and Options. Tracks play in random order with a half-second fade on change. The set is
album-normalised to about -15 LUFS with true peak at or below -1 dBTP via ReplayGain tags the
player applies, without re-encoding (`tools/normalize_music.py`). See `assets/Music/README.txt`.

Playback: the music thread keeps about 250 ms queued ahead of the audio thread; the audio thread
runs on an absolute-time schedule (65536-frame ring, 10 ms chunks) and fades rather than cutting
if it runs dry. Underruns are counted in `log.txt` at exit (the most recent device log shows 10;
an earlier one showed 44, so it is worth re-checking by ear). The SF3500 speakers do not
play the left channel, so on speakers the player blends L and R (headphone jack detected from the
device tree and a read-only GPIO register); headphones always get stereo. `tools/musictest/`
exercises switching, fades and the cushion under qemu.

## Story pages

`assets/Story/story.txt` describes every cut-scene in plain text and is read at start-up, so the
story can be reordered or reworded without touching code.

- **Comic pages** (`comic` layout): a whole text-free page from `assets/Story/Comic/` (456 px wide,
  made by `tools/slice_comic.py` from `Extras/Concepts/Story/Chapters/`) revealed a row of panels at
  a time. Words sit in a band under the art, never over it. Rows are `beat x y w h` lines (the tool
  prints them; staggered pages list extra cuts in `CUTS`). A `speaker` plate shows human dialogue
  *at* the revenant, who never answers.
- **Panel pages** (layouts `1`, `2`, `2s`, `3`, `4`): cropped panels from `assets/Story/Panels/`
  with a caption box and balloon (prologue, endings, the keeper page; `tools/slice_story.py`).
- **Parchment** (`epilogue`): plain lettering.

Pages are looked up by name: `intro`; `ch1`..`ch6`; `ch1b`..`ch5b` (lead-ups shown at the start of
the *next* chapter); `end`..`end4`, `end2b`, then `epilogue`. Pages sharing a name play in file
order. START or B skips. Replace a PNG with one of the same size and the game uses it.

## The Journal

`assets/Story/journal.txt`: 48 entries, 37 in the casebook and 11 in **Earlier Hands** (L1/R1
switches book). Each entry has keywords `entry`, `style` (parchment, clipping, scrawl, tally,
record), `title`, `source`, `unlock`, `hint`, `book`, `era`, `margin` and `text`.

- **Unlock rules:** `free`, `chapter N`, `finale`, `graves N`, `lanterns N`, `deed KIND N` where
  KIND is `stumbles`, `offerings`, `flawless`, `shut`, `rite` or `hard`. A locked slot shows its
  `hint` line in the writer's voice. The deed thresholds (hard 2, hard 4, flawless 10) are guesses.
- **Marks in text:** `[[struck, still readable]]`, `{{blacked out}}` (an ink bar sized by its words),
  `((worn away))` (sparse missing text).
- **Hands:** the investigator's scrawl frays as the player advances (underlines, circled words,
  crowded margins, struck lines); other keepers write `margin <hand> <words>` notes in their own ink
  (`teacher` blue-black, `elder` faded brown, `pencil`), always present.
- **Record of the Vigils** is filled in from the save; the Count is a tally of lanterns put out.
  The Account Settled screen says "Entered in the book."
- **Fonts:** the investigator's hand is Covered By Your Grace in three sizes (the smallest is the
  older hand other keepers use); see `THIRD-PARTY-NOTICES.txt`.

Voice and canon rules (who says "it", who says "you", what stays off the page) are in
`Notes/LORE-CANON.md`.

## Testing without the device

`tools/simtest.cpp` runs the real rules headless:

```sh
g++ -std=c++11 -O2 -o build/sim/simtest tools/simtest.cpp
build/sim/simtest selftest                 # rule tests and the walled-in detector
build/sim/simtest check                    # every layout reachable (90 of 90)
build/sim/simtest fuzz [--bot] [--runs N]  # invariants under a random walker or the bot
build/sim/simtest bot [--diff D --stage S] # look-ahead bot; --width 20 --depth 10 for the beam bot
build/sim/simtest view --diff 1 --stage 8  # print a board
```

`fuzz` also prints event tallies and deed counters. `tools/simall.sh` runs a parallel sweep. The bot
is weaker than a person against slow pursuers: read its numbers as a ranking of stages, not as
difficulty. The walled-in watcher in the fuzz allows the revenant to walk inside the proven region;
only leaving it or a stone changing is a false positive.

`tools/textaudit.sh` renders every screen and state (241) and reports text that leaves its panel, a
Journal body running under the footer, or a margin note that drops (rules TEXT-OVERFLOW, TEXT-NOTE,
TEXT-BODY). It should print only its summary line. `tools/shot.sh <ABSOLUTE-PATH>.bmp --screen game
--diff 1 --stage 8 --ticks 200` renders a screen of the MIPS build under qemu; the output path must be
absolute because the script changes directory. Screens: `main play stage scores options help exit
card pause game story title journal journalpage`; other flags: `--page --cursor --ticks --graves
--lanterns --deeds --frame --hint --noeffects --news --state`. `--autoplay N [--diff D --stage S]`
runs the live loop headless with scripted input and prints the events it saw.

## Art pipeline

- `tools/make_art.py`: draws every 19x19 sprite from pixel grids (Pillow).
- `tools/make_arenas.py`: cuts the six arenas from the concept sheets in `Extras/Concepts/`.
- `tools/make_titles.py`: resizes the title concept art.
- `tools/slice_comic.py`, `tools/slice_story.py`: comic and panel pages (see above).
- `tools/make_sfx.py`: synthesises the sound effects.
- `tools/make_panels.py`, `tools/make_backdrops.py`: fallbacks and early experiments, unused while the
  concept art is present.

## Licensing and provenance

Code is MIT (`LICENSE`); art, story, Journal text, music, sound effects and level designs are CC0
(`LICENSE-ASSETS.txt`). Third-party fonts and the two bundled libraries are listed with their
licences in `THIRD-PARTY-NOTICES.txt`, which ships in the game folder. Do not add a font or asset
without recording its licence there; `Extras/Fonts/` is kept out of the repo because its fonts are
not cleared for redistribution.

The game is in the spirit of *Rodent's Revenge* (Microsoft Entertainment Pack, 1991) and descends
from a C++ port of Maxx Boehme's Java remake *A Mouse's Vengeance*
(<https://github.com/maxxboehme/A_Mouses_Vengeance>, no licence file). Its pursuer movement, block
pushing, skull handling, walled-in proof, level construction and naming have been rewritten from
scratch; some numbers and conventions still descend from the port (the cap of eight pursuers, the
8-way pursuer step, the order of the `Direction` enum, the three difficulty names in the code). The
art, levels, text, story and name are original and contain nothing from either game.

## Open items

- Device playtest of everything since the polish pass: music scratchiness (underruns), sound-effect
  levels, shake feel, frame rate with effects on, the new live audio-thread mixer.
- Balance after the pursuer slowdown, and the Journal deed thresholds.
- The April 1 font egg depends on the device date (the SF3500 has no real-time clock).
- A credits screen is deferred until playtesting settles; credits live in the README for now.
- The "Revenant's Notes" help screen does not mention the medals, Continue or hints.
- Final panel art; the keeper page still uses a balloon.
