/* Game rules and level data for Grave Reckoning. No SDL or rendering in
 * here, so tools/simtest.cpp can run the real game logic headless. */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <limits>
#include <queue>
#include <random>
#include <string>
#include <vector>

namespace {

/* The board is 21 x 17 cells. The outer ring is wall and is never drawn as
 * tiles: the chapter's arena artwork is that boundary, so the playable
 * interior is 19 x 15 cells of 19 px. */
const int COLS = 21;
const int ROWS = 17;
const int TILE = 19;
const int FB_W = 480;
const int FB_H = 480;
const int BOARD_X = (FB_W - COLS * TILE) / 2;   /* origin of cell (0,0) */
const int BOARD_Y = 108;
const int FPS = 24;

struct Point {
    int x;
    int y;
};

bool same_point(Point a, Point b) { return a.x == b.x && a.y == b.y; }

double distance_between(Point a, Point b) {
    const int dx = a.x - b.x;
    const int dy = a.y - b.y;
    return std::sqrt(static_cast<double>(dx * dx + dy * dy));
}

enum TileType {
    FLOOR = -1,
    REVENANT,
    REVENANT_IN_GRAVE,
    PURSUER,
    STONE,
    WALL,
    OFFERING,
    SEALED,
    MANTRAP,
    GRAVE,
    SKULL
};

enum Direction {
    NORTH,
    SOUTH,
    EAST,
    WEST,
    NORTH_EAST,
    NORTH_WEST,
    SOUTH_EAST,
    SOUTH_WEST,
    NO_DIRECTION
};

Point direction_delta(Direction d) {
    Point p = {0, 0};
    switch (d) {
    case NORTH: p.y = -1; break;
    case SOUTH: p.y = 1; break;
    case EAST: p.x = 1; break;
    case WEST: p.x = -1; break;
    case NORTH_EAST: p.x = 1; p.y = -1; break;
    case NORTH_WEST: p.x = -1; p.y = -1; break;
    case SOUTH_EAST: p.x = 1; p.y = 1; break;
    case SOUTH_WEST: p.x = -1; p.y = 1; break;
    case NO_DIRECTION: break;
    }
    return p;
}

Direction direction_between(Point from, Point to) {
    const int dx = to.x - from.x;
    const int dy = to.y - from.y;
    if (dx < 0) return dy < 0 ? NORTH_WEST : (dy > 0 ? SOUTH_WEST : WEST);
    if (dx > 0) return dy < 0 ? NORTH_EAST : (dy > 0 ? SOUTH_EAST : EAST);
    if (dy < 0) return NORTH;
    if (dy > 0) return SOUTH;
    return NO_DIRECTION;
}

Point moved(Point p, Direction d) {
    const Point delta = direction_delta(d);
    p.x += delta.x;
    p.y += delta.y;
    return p;
}

struct Level {
    std::vector<TileType> tiles;
    int walls;
    int mantraps;
    int graves;
    int stones;
    int skull_frames;
    std::vector<int> pursuers_per_round;
    Point revenant_start;
    /* Wave pacing, filled in per difficulty by Game::configure_level():
     * a new batch of pursuers arrives every wave_seconds of game time whether
     * or not the last batch is dead, and the stage ends after `waves`
     * extra batches once the board is clear. */
    int waves;
    int wave_seconds;

    Level() : tiles(COLS * ROWS, FLOOR), walls(0),
              mantraps(0), graves(0), stones(0), skull_frames(-1),
              pursuers_per_round(), revenant_start({COLS / 2, ROWS / 2}),
              waves(6), wave_seconds(30) {
        for (int x = 0; x < COLS; ++x) {
            set(x, 0, WALL);
            set(x, ROWS - 1, WALL);
        }
        for (int y = 0; y < ROWS; ++y) {
            set(0, y, WALL);
            set(COLS - 1, y, WALL);
        }
    }

    void set(int x, int y, TileType tile) { tiles[y * COLS + x] = tile; }
    TileType get(int x, int y) const { return tiles[y * COLS + x]; }
};

/* A rolling skull: it enters through a crack in the boundary wall (`in_wall`
 * until it first moves), rolls in `heading` until stopped, rests for `pause`
 * frames, then picks a new way. */
struct Skull {
    Point at;
    Direction heading;
    int pause;
    bool in_wall;

    Skull(Point p, int rest) : at(p), heading(NO_DIRECTION), pause(rest), in_wall(true) {}
};

enum GameState { STATE_NEW_GAME, STATE_SETUP, STATE_RUNNING, STATE_PAUSED,
                 STATE_DEAD, STATE_LEVEL_COMPLETE, STATE_GAME_OVER, STATE_WON };

enum DeathCause { DEATH_NONE, DEATH_WALKED_INTO_PURSUER, DEATH_CAUGHT_BY_PURSUER, DEATH_MANTRAP, DEATH_SKULL };

enum Difficulty { DIFFICULTY_SLOW, DIFFICULTY_MEDIUM, DIFFICULTY_FAST };

/* Things that happened during a tick, for the front end to answer with sound and effects. The rules
 * never read them; take_events() hands them over and empties the list (it is capped, so a test that
 * never asks cannot make it grow). */
enum GameEvent { EV_PUSH, EV_SEAL, EV_RISE, EV_FALL, EV_DEATH, EV_OFFERING, EV_WAVE, EV_GRAVE, EV_COMPLETE };

const int STAGES_PER_CHAPTER = 5;
const int CHAPTER_COUNT = 6;
const int LEVEL_COUNT = STAGES_PER_CHAPTER * CHAPTER_COUNT;

int chapter_of(int stage) { return std::max(0, std::min(CHAPTER_COUNT - 1, stage / STAGES_PER_CHAPTER)); }

/* The only input path (the D-pad in main()) sends the four straight
 * directions. If diagonal input is ever added, set this to true so the
 * walled-in check below keeps assuming every move the player can make. */
const bool PLAYER_CAN_MOVE_DIAGONALLY = false;

/* A walled-in revenant must stay walled in this long (game frames) before the
 * game says so. */
const int WALLED_IN_HOLD_FRAMES = 48;

int difficulty_index(Difficulty difficulty) {
    return static_cast<int>(difficulty);
}

struct HighScores {
    int values[3][LEVEL_COUNT];
    std::string path;

    HighScores() : values(), path() { std::memset(values, 0, sizeof(values)); }

    void load(const std::string &filename) {
        path = filename;
        FILE *file = std::fopen(path.c_str(), "rb");
        if (!file || std::fread(values, sizeof(values), 1, file) != 1) {
            std::memset(values, 0, sizeof(values));
        }
        if (file) std::fclose(file);
    }

    void save() const {
        if (path.empty()) return;
        const std::string temporary = path + ".tmp";
        FILE *file = std::fopen(temporary.c_str(), "wb");
        if (!file) return;
        if (std::fwrite(values, sizeof(values), 1, file) == 1) {
            std::fflush(file);
            std::fclose(file);
            std::rename(temporary.c_str(), path.c_str());
        } else {
            std::fclose(file);
        }
    }

    void reset() {
        std::memset(values, 0, sizeof(values));
        save();
    }

    int get(Difficulty difficulty, int stage) const {
        if (stage < 0 || stage >= LEVEL_COUNT) return 0;
        return values[difficulty_index(difficulty)][stage];
    }

    void record(Difficulty difficulty, int stage, int score) {
        if (stage < 0 || stage >= LEVEL_COUNT || score <= 0) return;
        int &best = values[difficulty_index(difficulty)][stage];
        if (score > best) {
            best = score;
            save();
        }
    }
};

struct Progress {
    int unlocked[3];
    std::string path;

    Progress() : path() { unlocked[0] = unlocked[1] = unlocked[2] = 1; }

    void load(const std::string &filename) {
        path = filename;
        FILE *file = std::fopen(path.c_str(), "rb");
        if (!file || std::fread(unlocked, sizeof(unlocked), 1, file) != 1) {
            unlocked[0] = unlocked[1] = unlocked[2] = 1;
        }
        if (file) std::fclose(file);
        for (int i = 0; i < 3; ++i)
            unlocked[i] = std::max(1, std::min(LEVEL_COUNT, unlocked[i]));
    }

    void save() const {
        if (path.empty()) return;
        const std::string temporary = path + ".tmp";
        FILE *file = std::fopen(temporary.c_str(), "wb");
        if (!file) return;
        if (std::fwrite(unlocked, sizeof(unlocked), 1, file) == 1) {
            std::fflush(file);
            std::fclose(file);
            std::rename(temporary.c_str(), path.c_str());
        } else {
            std::fclose(file);
        }
    }

    bool is_unlocked(Difficulty difficulty, int stage) const {
        return stage >= 0 && stage < unlocked[difficulty_index(difficulty)];
    }

    void unlock_next(Difficulty difficulty, int completed_stage) {
        const int index = difficulty_index(difficulty);
        const int next = completed_stage + 1;
        if (next >= 0 && next < LEVEL_COUNT && unlocked[index] <= next) {
            unlocked[index] = next + 1;
            save();
        }
    }
};

const int WALL_THEME_COUNT = 11;   /* 0 = each chapter's own masonry, then ten colours */

struct Options {
    int volume;
    bool muted;
    int wall_theme; /* 0 = each chapter's own masonry, then the ten colours in the main program */
    int story_seen; /* bit 0: the prologue has been shown; bit c+1: chapter c+1's page has been shown */
    int story_mode; /* 0 = a page at the start of every chapter, 1 = first time only, 2 = off */
    int title_screen; /* which title picture: 0 = Abbey Ruins, 1 and 2 are unlocked by progress */
    int speaker_mix; /* 0 = blend left and right on the speakers, 1 = always stereo */
    int journal;    /* bit c: chapter c+1 was settled; bit 6: the last grave was settled */
    int lanterns;   /* pursuers laid to rest in total, across every grave and vigil */
    /* What the investigators before you noticed, counted quietly as you play (they unlock the
     * older pages of the Journal; nothing on screen says so). */
    int stumbles;   /* pursuers who walked into an open grave */
    int offerings;  /* flames gathered */
    int flawless;   /* graves settled without losing a life */
    int shut;       /* times the way was proved shut ("The Way Is Shut") */
    int rite;       /* the most pursuers laid to rest together in one rite */
    int hard;       /* bit c: chapter c+1's last grave was settled on the hardest vigil */
    int vigils;     /* bit d: the thirtieth grave of vigil d (0 easy, 1 normal, 2 hard) was settled */
    /* Polish, each with its switch in the options */
    bool sfx;       /* sound effects */
    bool effects;   /* screen fades, shake, flashes, dust, flicker and the living title */
    bool hints;     /* the one-line hints in the first grave */
    int hints_seen; /* bit h: hint h has been shown */
    int untouched[3];   /* per vigil, bit s: grave s+1 was settled without losing a life */
    int last_vigil; /* 0 = none yet, else the vigil last played plus one (for Continue) */
    std::string path;

    Options() : volume(80), muted(false), wall_theme(0), story_seen(0), story_mode(0), title_screen(0), speaker_mix(0), journal(0), lanterns(0),
                stumbles(0), offerings(0), flawless(0), shut(0), rite(0), hard(0), vigils(0),
                sfx(true), effects(true), hints(true), hints_seen(0), last_vigil(0), path() {
        untouched[0] = untouched[1] = untouched[2] = 0;
    }

    /* The file is a line of numbers, in this order. Older files hold fewer; the rest keep their defaults. */
    static const int FIELDS = 24;

    void load(const std::string &filename) {
        path = filename;
        FILE *file = std::fopen(path.c_str(), "r");
        int v[FIELDS];
        int fields = 0;
        if (file) {
            while (fields < FIELDS && std::fscanf(file, "%d", &v[fields]) == 1) ++fields;
            std::fclose(file);
        }
        if (fields < 2) return;
        auto clamp = [](int x, int lo, int hi) { return std::max(lo, std::min(hi, x)); };
        volume = clamp(v[0], 0, 100);
        muted = v[1] != 0;
        wall_theme = fields >= 3 ? clamp(v[2], 0, WALL_THEME_COUNT - 1) : 0;
        story_seen = fields >= 4 ? v[3] & 0x7F : 0;
        journal = fields >= 5 ? v[4] & 0x7F : 0;
        lanterns = fields >= 6 ? clamp(v[5], 0, 99999) : 0;
        story_mode = fields >= 7 ? clamp(v[6], 0, 2) : 0;
        speaker_mix = fields >= 8 ? (v[7] != 0 ? 1 : 0) : 0;
        title_screen = fields >= 9 ? clamp(v[8], 0, 2) : 0;
        stumbles = fields >= 10 ? clamp(v[9], 0, 99999) : 0;
        offerings = fields >= 11 ? clamp(v[10], 0, 99999) : 0;
        flawless = fields >= 12 ? clamp(v[11], 0, 99999) : 0;
        shut = fields >= 13 ? clamp(v[12], 0, 99999) : 0;
        rite = fields >= 14 ? clamp(v[13], 0, 99) : 0;
        hard = fields >= 15 ? v[14] & 0x3F : 0;
        vigils = fields >= 16 ? v[15] & 7 : 0;
        sfx = fields >= 17 ? v[16] != 0 : true;
        effects = fields >= 18 ? v[17] != 0 : true;
        hints = fields >= 19 ? v[18] != 0 : true;
        hints_seen = fields >= 20 ? v[19] & 0xFF : 0;
        for (int d = 0; d < 3; ++d) untouched[d] = fields >= 21 + d ? v[20 + d] & 0x3FFFFFFF : 0;
        last_vigil = fields >= 24 ? clamp(v[23], 0, 3) : 0;
    }

    void save() const {
        if (path.empty()) return;
        const std::string temporary = path + ".tmp";
        FILE *file = std::fopen(temporary.c_str(), "w");
        if (!file) return;
        std::fprintf(file, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", volume, muted ? 1 : 0, wall_theme,
                     story_seen, journal, lanterns, story_mode, speaker_mix, title_screen, stumbles, offerings, flawless, shut,
                     rite, hard, vigils, sfx ? 1 : 0, effects ? 1 : 0, hints ? 1 : 0, hints_seen, untouched[0], untouched[1],
                     untouched[2], last_vigil);
        std::fflush(file);
        std::fclose(file);
        std::rename(temporary.c_str(), path.c_str());
    }
};

/* Rules of the cemetery. One Game is one grave: the revenant, the walking
 * pursuers, the fixed stones, graves, mantraps and rolling skulls.
 *
 * The board is a grid of TileType cells; `pursuers` is the list of pursuer
 * positions and must always agree with the PURSUER / SEALED cells (the
 * simulator checks this every tick). `style` carries a cosmetic variant for
 * each stone and pursuer and travels with it when it is pushed or walks. */
class Game {
public:
    explicit Game(unsigned seed = 0xA5539u)
        : rng(seed),
          levels(std::make_shared<const std::vector<Level>>(make_levels())),
          board(COLS * ROWS, FLOOR),
          style(COLS * ROWS, 0) {
        begin_stage();
        state = STATE_NEW_GAME;
    }

    void start_stage(Difficulty chosen, int stage) {
        difficulty = chosen;
        level = std::max(0, std::min(LEVEL_COUNT - 1, stage));
        set_pursuer_period();
        reset_game();
    }

    void restart_stage() { reset_game(); }

    void pause() {
        if (state == STATE_RUNNING) state = STATE_PAUSED;
        else if (state == STATE_PAUSED) state = STATE_RUNNING;
    }

    void proceed_to_next_level() {
        if (state != STATE_LEVEL_COMPLETE) return;
        if (++level >= static_cast<int>(levels->size())) {
            state = STATE_WON;
            return;
        }
        begin_stage();
        state = STATE_SETUP;
    }

    /* One queued step at a time: a held direction is re-sent by the caller. */
    void turn(Direction d) {
        if (state == STATE_RUNNING && inputs.empty()) inputs.push_back(d);
    }

    void tick() {
        switch (state) {
        case STATE_SETUP:
            if (--setup_time <= 0) state = STATE_RUNNING;
            return;
        case STATE_DEAD:
            if (--dead_time <= 0) {
                if (lives <= 0) state = STATE_GAME_OVER;
                else respawn();
            }
            return;
        case STATE_RUNNING:
            break;
        default:
            walled_in_frames = 0;
            return;
        }
        advance_clock();
        if (settle_if_done()) return;
        send_next_wave();
        move_revenant();
        move_pursuers();
        run_skulls();
        restore_fixtures();
        watch_for_walled_in();
        const bool shut_now = is_walled_in();
        if (shut_now && !was_shut) ++shut_total;
        was_shut = shut_now;
    }

    GameState get_state() const { return state; }
    const std::vector<TileType> &get_board() const { return board; }
    Point get_revenant() const { return revenant; }
    int get_score() const { return score; }
    int get_stage_score() const { return stage_score; }
    int get_lives() const { return lives; }
    int get_level() const { return std::min(level + 1, LEVEL_COUNT); }
    int get_stage_index() const { return level; }
    Difficulty get_difficulty() const { return difficulty; }
    Direction get_facing() const { return facing; }
    int get_steps() const { return steps_taken; }
    int get_wave() const { return round + 1; }
    /* Pursuers laid to rest (flames raised) since this Game was made. */
    int get_sealed_total() const { return sealed_total; }
    /* Counted quietly for the Journal's older pages (since this Game was made, except the
     * deaths, which are for the grave now in play). */
    int get_stumble_total() const { return stumble_total; }
    int get_offering_total() const { return offering_total; }
    int get_best_rite() const { return best_rite; }
    int get_shut_total() const { return shut_total; }
    int get_stage_deaths() const { return stage_deaths; }
    /* The events since the last call, oldest first. */
    std::vector<int> take_events() {
        std::vector<int> out;
        out.swap(events);
        return out;
    }
    int get_wave_total() const { return current_waves + 1; }
    unsigned char style_at_cell(int x, int y) const { return style[y * COLS + x]; }
    /* 0..6 while the re-burial plays, -1 otherwise */
    int get_death_phase() const {
        return state == STATE_DEAD ? std::min(6, (28 - dead_time) * 7 / 28) : -1;
    }
    bool is_walled_in() const { return state == STATE_RUNNING && walled_in_frames >= WALLED_IN_HOLD_FRAMES; }

    /* True only when it is PROVEN that nothing the player can do will ever
     * change the board again. The revenant must be confined to a closed
     * region (one cell or many) whose every exit is a wall, or a stone chain
     * that can never move; nothing may be able to enter it (no pursuer,
     * skull, grave or mantrap beside it) and no pursuer may still be able to
     * spawn inside it. Any doubt answers false, which just means "keep
     * playing". Pure: it never changes the game. */
    bool walled_in_now() const {
        if (board_at(revenant) != REVENANT || trapped_frames > 0) return false;
        std::vector<char> inside(COLS * ROWS, 0);
        std::vector<Point> region(1, revenant);
        inside[revenant.y * COLS + revenant.x] = 1;
        const int reach = PLAYER_CAN_MOVE_DIAGONALLY ? 8 : 4;
        for (size_t head = 0; head < region.size(); ++head) {
            const Point here = region[head];
            for (int i = 0; i < reach; ++i) {
                const Direction d = static_cast<Direction>(i);
                const Point next = moved(here, d);
                if (!in_bounds(next)) continue;
                const TileType t = board_at(next);
                if (t == OFFERING) return false;          /* collecting it is still something to do */
                if (t == FLOOR) {
                    if (!inside[next.y * COLS + next.x]) {
                        inside[next.y * COLS + next.x] = 1;
                        region.push_back(next);
                    }
                } else if (t == STONE) {
                    if (stones_could_shift(here, d)) return false;
                } else if (t != WALL && t != REVENANT) {
                    return false;                         /* pursuer, grave, mantrap, skull ... */
                }
            }
        }
        /* Nothing may reach the region either. Pursuers step diagonally, so
         * every one of the eight neighbours of every region cell counts. */
        for (size_t i = 0; i < region.size(); ++i)
            for (int d = 0; d < 8; ++d) {
                const Point next = moved(region[i], static_cast<Direction>(d));
                if (!in_bounds(next) || inside[next.y * COLS + next.x]) continue;
                const TileType t = board_at(next);
                if (t != WALL && t != STONE) return false;
            }
        /* A skull can roll in from a crack in the boundary wall, so a region
         * that touches the boundary is never safe while skulls are on. */
        if (skull_gap > 0 || !skulls.empty())
            for (size_t i = 0; i < region.size(); ++i)
                for (int d = 0; d < 8; ++d) {
                    const Point next = moved(region[i], static_cast<Direction>(d));
                    if (in_bounds(next) && !in_interior(next)) return false;
                }
        /* A pursuer can still spawn inside a region with two cells three or
         * more apart until the last wave has arrived. */
        if (round < current_waves)
            for (size_t i = 0; i < region.size(); ++i)
                for (size_t j = i + 1; j < region.size(); ++j)
                    if (distance_between(region[i], region[j]) >= 3.0) return false;
        return true;
    }

    /* For screenshots and the text audit only: puts the game in a given state so every banner can be drawn. */
    void debug_force_state(GameState forced, bool walled_in) {
        state = forced;
        walled_in_frames = walled_in ? WALLED_IN_HOLD_FRAMES : 0;
    }

    /* Hands over the grave just completed, once. */
    bool take_completed_stage(int &stage, int &completed_score) {
        if (last_completed_stage < 0) return false;
        stage = last_completed_stage;
        completed_score = last_completed_score;
        last_completed_stage = -1;
        last_completed_score = 0;
        return true;
    }

private:
    /* ---- state ---------------------------------------------------------- */
    std::mt19937 rng;
    GameState state = STATE_NEW_GAME;
    int score = 0;
    int stage_score = 0;
    int life_score = 50;
    int lives = 3;
    int level = 0;
    int round = 0;              /* waves sent so far */
    int round_minutes = 0;
    int wave_clock = 0;         /* game seconds since the last wave */
    int current_waves = 6;
    int current_wave_seconds = 30;
    int game_seconds = 0;
    int game_frames = 0;
    int pursuer_clock = 0;
    int pursuer_period = FPS;
    int seal_delay_cycles = 2;  /* pursuer steps everyone must stay sealed before the flames rise */
    int seal_wait_cycles = 0;
    int trapped_frames = 0;     /* revenant stuck in an open grave */
    int time_till_skull = -1;
    int skull_gap = -1;
    int setup_time = 0;
    int dead_time = 28;
    int walled_in_frames = 0;
    int walled_in_ticks = 0;
    int steps_taken = 0;
    int sealed_total = 0;
    int stumble_total = 0;      /* pursuers who walked into an open grave */
    int offering_total = 0;     /* flames gathered */
    int best_rite = 0;          /* most pursuers laid to rest in a single rite */
    int shut_total = 0;         /* times the way was proved shut */
    bool was_shut = false;
    int stage_deaths = 0;       /* lives lost in the grave now in play (kept across restarts of a life) */
    std::vector<int> events;    /* see GameEvent */
    void emit(GameEvent e) { if (events.size() < 64) events.push_back(static_cast<int>(e)); }
    Direction facing = SOUTH;
    DeathCause death_cause = DEATH_NONE;
    Difficulty difficulty = DIFFICULTY_MEDIUM;
    int last_completed_stage = -1;
    int last_completed_score = 0;
    Point revenant = {COLS / 2, ROWS / 2};
    std::shared_ptr<const std::vector<Level>> levels;
    std::vector<TileType> board;
    std::vector<unsigned char> style;
    std::deque<Direction> inputs;
    std::vector<Point> pursuers;
    std::vector<Point> mantraps;
    std::vector<Point> graves;
    std::vector<Skull> skulls;

    /* ---- small helpers ---------------------------------------------------- */
    static bool in_bounds(Point p) { return p.x >= 0 && p.x < COLS && p.y >= 0 && p.y < ROWS; }
    static bool in_interior(Point p) { return p.x > 0 && p.x < COLS - 1 && p.y > 0 && p.y < ROWS - 1; }
    static int cell(Point p) { return p.y * COLS + p.x; }

    TileType &board_at(Point p) { return board[cell(p)]; }
    TileType board_at(Point p) const { return board[cell(p)]; }

    int random_int(int low, int high) {
        if (high <= low) return low;
        return std::uniform_int_distribution<int>(low, high)(rng);
    }

    static void forget(std::vector<Point> &list, Point p) {
        list.erase(std::remove_if(list.begin(), list.end(), [p](Point q) { return same_point(p, q); }),
                   list.end());
    }

    void add_score(int points) {
        score += points;
        stage_score += points;
    }

    /* Frames between pursuer steps. Easy about 2.4 s, Normal 1.6 s, Hard 1.1 s:
     * slow enough to read the board and plan a rite. The seal delay is counted
     * in steps, so it shrinks as the steps lengthen and the wait before the
     * flames rise stays about three seconds. */
    void set_pursuer_period() {
        if (difficulty == DIFFICULTY_SLOW) { pursuer_period = FPS * 12 / 5; seal_delay_cycles = 0; }
        else if (difficulty == DIFFICULTY_FAST) { pursuer_period = FPS * 11 / 10; seal_delay_cycles = 2; }
        else { pursuer_period = FPS * 8 / 5; seal_delay_cycles = 1; }
    }

    /* Chance, per step, that a pursuer does not look where it is going and
     * walks straight into an open grave. The grieving are the most distracted;
     * the faithful keep their eyes on the ground. */
    int carelessness_percent() const {
        if (difficulty == DIFFICULTY_SLOW) return 30;
        if (difficulty == DIFFICULTY_FAST) return 8;
        return 18;
    }

    /* A cell a pursuer standing next to it could step into. */
    static bool pursuer_can_enter(TileType t) {
        return t == FLOOR || t == REVENANT || t == REVENANT_IN_GRAVE;
    }

    std::vector<Point> open_neighbours(Point p) const {
        std::vector<Point> out;
        for (int d = 0; d < 8; ++d) {
            const Point q = moved(p, static_cast<Direction>(d));
            if (in_bounds(q) && pursuer_can_enter(board_at(q))) out.push_back(q);
        }
        return out;
    }

    bool has_open_neighbour(Point p) const {
        for (int d = 0; d < 8; ++d) {
            const Point q = moved(p, static_cast<Direction>(d));
            if (in_bounds(q) && pursuer_can_enter(board_at(q))) return true;
        }
        return false;
    }

    /* ---- stage set-up ----------------------------------------------------- */
    void reset_game() {
        score = 0;
        stage_score = 0;
        life_score = 50;
        lives = 3;
        last_completed_stage = -1;
        last_completed_score = 0;
        round = 0;
        begin_stage();
        state = STATE_SETUP;
    }

    /* After a death the revenant rises somewhere away from the pursuers. */
    void respawn() {
        std::vector<Point> spots;
        for (int y = 1; y < ROWS - 1; ++y)
            for (int x = 1; x < COLS - 1; ++x) {
                const Point p = {x, y};
                bool clear = board_at(p) == FLOOR;
                for (size_t i = 0; clear && i < pursuers.size(); ++i)
                    if (distance_between(pursuers[i], p) < 3.0) clear = false;
                if (clear) spots.push_back(p);
            }
        if (!spots.empty()) revenant = spots[random_int(0, static_cast<int>(spots.size()) - 1)];
        board_at(revenant) = REVENANT;
        state = STATE_SETUP;
        setup_time = FPS;
        trapped_frames = 0;
        walled_in_frames = 0;
        pursuer_clock = 0;
        inputs.clear();
    }

    void place_tiles(std::mt19937 &gen, int count, TileType type, std::vector<Point> *record) {
        if (count <= 0) return;
        std::vector<Point> spots;
        for (int y = 1; y < ROWS - 1; ++y)
            for (int x = 1; x < COLS - 1; ++x)
                if (board_at({x, y}) == FLOOR) spots.push_back({x, y});
        std::shuffle(spots.begin(), spots.end(), gen);
        const int n = std::min(count, static_cast<int>(spots.size()));
        for (int i = 0; i < n; ++i) {
            board_at(spots[i]) = type;
            if (record) record->push_back(spots[i]);
        }
    }

    /* A grave is a designed puzzle: its walls, stones, graves and mantraps are
     * laid out from a seed fixed by the grave and the vigil, so a retry is the
     * same puzzle and only the pursuers' spawn squares vary. The layout must
     * leave every open cell reachable and the start roomy, so retry with a new
     * seed until it does (almost always the first try). */
    void begin_stage() {
        stage_deaths = 0;
        Level l = (*levels)[level];
        configure_level(l);
        for (int attempt = 0; attempt < 64; ++attempt) {
            std::mt19937 layout(static_cast<unsigned>(level * 7919 + static_cast<int>(difficulty) * 104729 +
                                                      attempt * 1000003 + 17));
            board = l.tiles;
            mantraps.clear();
            graves.clear();
            place_tiles(layout, l.walls, WALL, NULL);
            place_tiles(layout, l.graves, GRAVE, &graves);
            place_tiles(layout, l.mantraps, MANTRAP, &mantraps);
            place_tiles(layout, l.stones, STONE, NULL);
            for (int d = 0; d < 5; ++d) {    /* never wall in or booby-trap the start */
                const Point q = d < 4 ? moved(l.revenant_start, static_cast<Direction>(d)) : l.revenant_start;
                const TileType t = board_at(q);
                if (t == WALL || t == GRAVE || t == MANTRAP) board_at(q) = FLOOR;
            }
            prune_missing(graves, GRAVE);
            prune_missing(mantraps, MANTRAP);
            if (all_open_cells_reachable(l.revenant_start) && start_is_roomy(l.revenant_start)) {
                std::fill(style.begin(), style.end(), 0);
                for (size_t i = 0; i < board.size(); ++i)
                    if (board[i] == STONE) style[i] = static_cast<unsigned char>(layout() % 6);
                break;
            }
        }
        revenant = l.revenant_start;
        board_at(revenant) = REVENANT;
        pursuers.clear();
        spawn_pursuers(l.pursuers_per_round.empty() ? 0 : l.pursuers_per_round[0]);
        skulls.clear();
        skull_gap = l.skull_frames;
        time_till_skull = l.skull_frames;
        round = 0;
        round_minutes = 0;
        wave_clock = 0;
        current_waves = l.waves;
        current_wave_seconds = l.wave_seconds;
        stage_score = 0;
        game_seconds = 0;
        game_frames = 0;
        pursuer_clock = 0;
        seal_wait_cycles = 0;
        trapped_frames = 0;
        walled_in_frames = 0;
        setup_time = FPS;
        dead_time = 28;
        inputs.clear();
    }

    void prune_missing(std::vector<Point> &list, TileType expected) {
        for (size_t i = 0; i < list.size();)
            if (board_at(list[i]) != expected) list.erase(list.begin() + i); else ++i;
    }

    /* At least nine of the 25 cells round the start must be open floor. */
    bool start_is_roomy(Point start) const {
        int open = 0;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                const Point q = {start.x + dx, start.y + dy};
                if (in_bounds(q) && board_at(q) == FLOOR) ++open;
            }
        return open >= 9;
    }

    /* Every cell that is not a wall can be reached from the start by plain
     * orthogonal steps (stones count as passable: they can be pushed). */
    bool all_open_cells_reachable(Point start) const {
        std::vector<char> seen(COLS * ROWS, 0);
        std::vector<Point> pending(1, start);
        seen[cell(start)] = 1;
        int reached = 0;
        while (!pending.empty()) {
            const Point p = pending.back();
            pending.pop_back();
            ++reached;
            for (int d = 0; d < 4; ++d) {
                const Point q = moved(p, static_cast<Direction>(d));
                if (!in_bounds(q) || seen[cell(q)] || board_at(q) == WALL) continue;
                seen[cell(q)] = 1;
                pending.push_back(q);
            }
        }
        int open = 0;
        for (size_t i = 0; i < board.size(); ++i) if (board[i] != WALL) ++open;
        return reached == open;
    }

    /* Each grave's layout is fixed (see make_levels); the vigil changes how
     * many pursuers come, how fast, how often and how many hazards there are.
     * Easy is meant to be relaxing: slow pursuers, long gaps between waves,
     * hazards only late. Normal is the standard game. Hard is for players who
     * want a real fight: more, faster pursuers, extra hazards, extra stones
     * to build with. `stage` runs 0..29 across six chapters. */
    void configure_level(Level &l) const {
        const int stage = level;
        if (difficulty == DIFFICULTY_SLOW) {
            l.graves = stage < 9 ? 0 : std::min(l.graves / 2, 1 + stage / 6);
            l.mantraps = stage < 15 ? 0 : std::min(l.mantraps / 2, 1 + stage / 8);
            if (stage < 12) l.skull_frames = -1;
            else if (l.skull_frames > 0) l.skull_frames *= 2;
            const int base = 1 + stage / 15;
            l.stones += 12;
            if (stage < 6) l.pursuers_per_round = {base};          /* the first graves: one at a time */
            else l.pursuers_per_round = {base, base, base + 1};
            l.waves = 5;
            l.wave_seconds = 54 - stage * 2 / 3;
        } else if (difficulty == DIFFICULTY_MEDIUM) {
            const int base = 1 + (stage >= 3 ? 1 : 0) + stage / 9;
            l.pursuers_per_round = {base, base + 1};
            l.waves = 6 + stage / 7;
            l.wave_seconds = 36 - stage / 3;
        } else {
            l.walls += stage / 6;
            l.graves += 2 + stage / 6;
            l.mantraps += 1 + stage / 6;
            l.stones -= 8;
            if (l.skull_frames < 0) l.skull_frames = FPS * 14;
            else l.skull_frames = std::max(FPS * 3, l.skull_frames * 3 / 4);
            const int base = 2 + stage / 7;
            l.pursuers_per_round = {base, base + 1, base + 1};
            l.waves = 8 + stage / 6;
            l.wave_seconds = 24 - stage / 5;
        }
    }

    void die(DeathCause cause) {
        if (state == STATE_DEAD) return;
        death_cause = cause;
        ++stage_deaths;
        emit(EV_DEATH);
        --lives;
        dead_time = 28;
        state = STATE_DEAD;
    }

    /* ---- the clock and the waves ------------------------------------------ */
    /* The clock runs ten times faster while nobody is left on the board, so a
     * cleared wave does not leave the player waiting for the next. */
    void advance_clock() {
        game_frames += pursuers.empty() ? FPS * 10 : 1;
        while (game_frames >= FPS) {
            game_frames -= FPS;
            ++wave_clock;
            if (++game_seconds >= 60) {
                game_seconds = 0;
                ++round_minutes;
                add_score(1);
            }
        }
    }

    /* The grave is settled when the last wave has come and nobody is left. */
    bool settle_if_done() {
        const bool last_wave = round >= current_waves;
        if (last_wave && pursuers.empty()) {
            add_score(10);
            last_completed_stage = level;
            last_completed_score = stage_score;
            life_score += 10;
            while (life_score >= 100) { ++lives; life_score -= 100; }
            state = STATE_LEVEL_COMPLETE;
            emit(EV_COMPLETE);
            return true;
        }
        if (last_wave && round_minutes > 0) {       /* dawdling costs a point a minute */
            if (score > 0) --score;
            if (stage_score > 0) --stage_score;
            round_minutes = 0;
        }
        return false;
    }

    void send_next_wave() {
        if (round >= current_waves || wave_clock < current_wave_seconds) return;
        ++round;
        wave_clock = 0;
        spawn_pursuers(wave_size(round));
        emit(EV_WAVE);
    }

    int wave_size(int wave) const {
        Level l = (*levels)[level];
        configure_level(l);
        return l.pursuers_per_round.empty() ? 0 : l.pursuers_per_round[wave % l.pursuers_per_round.size()];
    }

    void spawn_pursuers(int count) {
        for (int i = 0; i < count && pursuers.size() < 8; ++i) {   /* eight is the most the board can bear */
            std::vector<Point> spots;
            for (int y = 1; y < ROWS - 1; ++y)
                for (int x = 1; x < COLS - 1; ++x) {
                    const Point p = {x, y};
                    if (board_at(p) == FLOOR && distance_between(revenant, p) >= 3.0) spots.push_back(p);
                }
            if (spots.empty()) return;
            const Point p = spots[random_int(0, static_cast<int>(spots.size()) - 1)];
            board_at(p) = PURSUER;
            style[cell(p)] = static_cast<unsigned char>(random_int(0, 3));
            pursuers.push_back(p);
        }
    }

    /* ---- the revenant ----------------------------------------------------- */
    void move_revenant() {
        if (trapped_frames > 0) {
            --trapped_frames;
            inputs.clear();
            return;
        }
        const Point before = revenant;
        const TileType met = step_revenant();
        if (!same_point(before, revenant)) ++steps_taken;
        switch (met) {
        case PURSUER: die(DEATH_WALKED_INTO_PURSUER); break;
        case MANTRAP: die(DEATH_MANTRAP); break;
        case GRAVE:
            forget(graves, revenant);               /* the grave is spent once climbed out of */
            trapped_frames = FPS * 6;
            board_at(revenant) = REVENANT_IN_GRAVE;
            emit(EV_GRAVE);
            break;
        case OFFERING: add_score(100); ++offering_total; emit(EV_OFFERING); break;
        default: break;
        }
    }

    /* Applies the queued step, if any, and returns what was in the cell
     * stepped towards (FLOOR when nothing was there or nothing happened). */
    TileType step_revenant() {
        if (inputs.empty()) {
            board_at(revenant) = REVENANT;
            return FLOOR;
        }
        const Direction d = inputs.front();
        inputs.pop_front();
        facing = d;
        const Point to = moved(revenant, d);
        if (!in_bounds(to)) return FLOOR;
        const TileType there = board_at(to);
        switch (there) {
        case FLOOR:
        case OFFERING:
            board_at(revenant) = FLOOR;
            revenant = to;
            board_at(revenant) = REVENANT;
            break;
        case GRAVE:
            board_at(revenant) = FLOOR;
            revenant = to;
            board_at(revenant) = REVENANT_IN_GRAVE;
            break;
        case MANTRAP:
            board_at(revenant) = FLOOR;
            revenant = to;
            board_at(revenant) = FLOOR;
            break;
        case PURSUER:                       /* walking into one is fatal; the cell stays theirs */
            board_at(revenant) = FLOOR;
            revenant = to;
            break;
        case STONE:
            if (shove(to, d)) {
                emit(EV_PUSH);
                board_at(revenant) = FLOOR;
                revenant = to;
                board_at(revenant) = REVENANT;
            }
            break;
        default:                            /* walls, sealed pursuers, skulls: no way through */
            break;
        }
        return there;
    }

    /* Pushes the line of stones, pursuers and sealed pursuers that starts at
     * `first` one cell along `d`. The push fails if the line ends at a wall,
     * a mantrap, a skull, the revenant or the edge. It succeeds if it ends at
     * open floor or an offering (which is crushed), or at an open grave, in
     * which case the item at the front of the line falls in and is gone (a
     * pursuer that falls leaves no offering). */
    bool shove(Point first, Direction d) {
        std::vector<Point> line;
        Point end = first;
        while (in_bounds(end) && is_pushable(board_at(end))) {
            line.push_back(end);
            end = moved(end, d);
        }
        if (line.empty() || !in_bounds(end)) return false;
        const TileType beyond = board_at(end);
        const bool drops = beyond == GRAVE;
        if (!drops && beyond != FLOOR && beyond != OFFERING) return false;
        for (size_t n = line.size(); n-- > 0;) {
            const Point from = line[n];
            const Point to = moved(from, d);
            const TileType item = board_at(from);
            const bool is_pursuer = item == PURSUER || item == SEALED;
            if (drops && n + 1 == line.size()) {
                if (is_pursuer) { forget(pursuers, from); emit(EV_FALL); }
            } else {
                board_at(to) = item;
                style[cell(to)] = style[cell(from)];
                if (is_pursuer) relocate_pursuer(from, to);
            }
            board_at(from) = FLOOR;
        }
        return true;
    }

    static bool is_pushable(TileType t) { return t == STONE || t == PURSUER || t == SEALED; }

    void relocate_pursuer(Point from, Point to) {
        for (size_t i = 0; i < pursuers.size(); ++i)
            if (same_point(pursuers[i], from)) { pursuers[i] = to; return; }
    }

    /* Pure twin of shove() for the walled-in proof. It answers "never" only
     * for a line made purely of stones that ends at a wall, a mantrap or the
     * board edge: that can never change. Anything else (a pursuer in the
     * line, a skull at the end, a grave or open floor) answers "could". */
    bool stones_could_shift(Point from, Direction d) const {
        Point q = moved(from, d);
        for (;;) {
            if (!in_bounds(q)) return false;
            const TileType t = board_at(q);
            if (t == STONE) { q = moved(q, d); continue; }
            return !(t == WALL || t == MANTRAP);
        }
    }

    /* ---- the pursuers ----------------------------------------------------- */
    /* Distance, in pursuer steps, from the revenant to every cell a pursuer
     * could walk through (-1 where there is no way). One flood serves every
     * pursuer. Graves count as walkable only for a careless pursuer. */
    void flood_from_revenant(std::vector<int> &dist, bool through_graves) const {
        dist.assign(COLS * ROWS, -1);
        std::vector<int> queue(1, cell(revenant));
        queue.reserve(COLS * ROWS);
        dist[cell(revenant)] = 0;
        for (size_t head = 0; head < queue.size(); ++head) {
            const Point p = {queue[head] % COLS, queue[head] / COLS};
            for (int d = 0; d < 8; ++d) {
                const Point q = moved(p, static_cast<Direction>(d));
                if (!in_bounds(q) || dist[cell(q)] >= 0) continue;
                const TileType t = board_at(q);
                if (!pursuer_can_enter(t) && !(through_graves && t == GRAVE)) continue;
                dist[cell(q)] = dist[queue[head]] + 1;
                queue.push_back(cell(q));
            }
        }
    }

    enum StepOutcome { STEP_STAYED, STEP_MOVED, STEP_SEALED, STEP_FELL };

    void move_pursuers() {
        if (++pursuer_clock < pursuer_period) return;
        pursuer_clock = 0;
        std::vector<int> careful, careless;
        flood_from_revenant(careful, false);
        bool careless_ready = false;
        std::vector<size_t> fallen;      /* by index: a later pursuer may have taken the cell */
        for (size_t i = 0; i < pursuers.size(); ++i) {
            if (board_at(pursuers[i]) == SEALED && has_open_neighbour(pursuers[i]))
                board_at(pursuers[i]) = PURSUER;           /* a stone shifted: the rite is broken */
            const bool careless_now = random_int(0, 99) < carelessness_percent();
            if (careless_now && !careless_ready) { flood_from_revenant(careless, true); careless_ready = true; }
            TileType met = FLOOR;
            const StepOutcome outcome = step_pursuer(pursuers[i], careless_now ? careless : careful, careless_now, met);
            if (outcome == STEP_FELL) fallen.push_back(i);
            if (met == REVENANT || met == REVENANT_IN_GRAVE) die(DEATH_CAUGHT_BY_PURSUER);
        }
        stumble_total += static_cast<int>(fallen.size());
        for (size_t i = 0; i < fallen.size(); ++i) emit(EV_FALL);
        for (size_t i = fallen.size(); i-- > 0;) pursuers.erase(pursuers.begin() + fallen[i]);
        size_t sealed = 0;
        for (size_t i = 0; i < pursuers.size(); ++i) if (board_at(pursuers[i]) == SEALED) ++sealed;
        if (!pursuers.empty() && sealed == pursuers.size()) {
            if (++seal_wait_cycles > seal_delay_cycles) {      /* the bell tolls: flames rise */
                for (size_t i = 0; i < pursuers.size(); ++i) board_at(pursuers[i]) = OFFERING;
                sealed_total += static_cast<int>(pursuers.size());
                best_rite = std::max(best_rite, static_cast<int>(pursuers.size()));
                emit(EV_RISE);
                pursuers.clear();
                seal_wait_cycles = 0;
            }
        } else {
            seal_wait_cycles = 0;
        }
    }

    /* One step for one pursuer, heading down the distance field towards the
     * revenant. A pursuer with nowhere to step is sealed in. `met` receives
     * what it stepped onto. The field is only a guide: every candidate cell is
     * checked against the board as it is now. */
    StepOutcome step_pursuer(Point &at, const std::vector<int> &dist, bool through_graves, TileType &met) {
        if (!has_open_neighbour(at)) {
            if (board_at(at) != SEALED) emit(EV_SEAL);          /* only the moment it happens */
            board_at(at) = SEALED;
            return STEP_SEALED;
        }
        Point best = at;
        int best_dist = -1;
        double best_straight = 0.0;
        int ties = 0;
        for (int d = 0; d < 8; ++d) {
            const Point q = moved(at, static_cast<Direction>(d));
            if (!in_bounds(q) || dist[cell(q)] < 0) continue;
            /* the field was drawn at the start of the cycle: pursuers that moved
             * since may have taken the cell */
            const TileType there = board_at(q);
            if (!pursuer_can_enter(there) && !(through_graves && there == GRAVE)) continue;
            const double straight = distance_between(q, revenant);
            const bool better = best_dist < 0 || dist[cell(q)] < best_dist ||
                                (dist[cell(q)] == best_dist && straight < best_straight - 1e-9);
            if (better) {
                best = q; best_dist = dist[cell(q)]; best_straight = straight; ties = 1;
            } else if (dist[cell(q)] == best_dist && std::fabs(straight - best_straight) < 1e-9 &&
                       random_int(0, ties++) == 0) {
                best = q;
            }
        }
        if (best_dist < 0) {
            /* No way through to the revenant: drift, mostly towards it. */
            const std::vector<Point> open = open_neighbours(at);
            if (open.empty()) return STEP_STAYED;
            if (random_int(0, 2) == 0) {
                best = open[random_int(0, static_cast<int>(open.size()) - 1)];
            } else {
                double nearest = std::numeric_limits<double>::max();
                for (size_t i = 0; i < open.size(); ++i) {
                    const double s = distance_between(open[i], revenant);
                    if (s < nearest) { nearest = s; best = open[i]; }
                }
            }
        }
        met = board_at(best);
        const unsigned char look = style[cell(at)];
        board_at(at) = FLOOR;
        if (met == GRAVE) return STEP_FELL;        /* the grave stays; the pursuer does not */
        at = best;
        board_at(at) = PURSUER;
        style[cell(at)] = look;
        return STEP_MOVED;
    }

    /* ---- rolling skulls --------------------------------------------------- */
    /* A skull rolls out of a crack in the boundary wall, travels in a straight
     * line until something stops it (another skull stops it too), rests, and
     * tries again. It passes over graves and mantraps, and it kills the
     * revenant on contact. */
    enum SkullFate { SKULL_ROLLS_ON, SKULL_VANISHES, SKULL_KILLS };

    void spawn_skull() {
        std::vector<Point> cracks;
        for (int x = 0; x < COLS; ++x) { cracks.push_back({x, 0}); cracks.push_back({x, ROWS - 1}); }
        for (int y = 1; y < ROWS - 1; ++y) { cracks.push_back({0, y}); cracks.push_back({COLS - 1, y}); }
        skulls.push_back(Skull(cracks[random_int(0, static_cast<int>(cracks.size()) - 1)], FPS));
    }

    SkullFate advance_skull(Skull &s) {
        const TileType home = s.in_wall ? WALL : FLOOR;
        if (s.heading == NO_DIRECTION) {
            if (s.pause > 0) {
                --s.pause;
                board_at(s.at) = SKULL;
                return SKULL_ROLLS_ON;
            }
            if (!s.in_wall && random_int(0, 5) == 0) {         /* sometimes it just fades */
                board_at(s.at) = FLOOR;
                return SKULL_VANISHES;
            }
            const std::vector<Point> open = open_neighbours(s.at);
            if (open.empty()) {
                board_at(s.at) = home;
                return SKULL_VANISHES;
            }
            s.heading = direction_between(s.at, open[random_int(0, static_cast<int>(open.size()) - 1)]);
            board_at(s.at) = SKULL;
            return SKULL_ROLLS_ON;
        }
        const Point next = moved(s.at, s.heading);
        const TileType ahead = in_interior(next) ? board_at(next) : WALL;
        if (ahead == FLOOR || ahead == GRAVE || ahead == MANTRAP) {
            board_at(s.at) = home;
            s.in_wall = false;
            s.at = next;
            board_at(s.at) = SKULL;
            return SKULL_ROLLS_ON;
        }
        if (ahead == REVENANT || ahead == REVENANT_IN_GRAVE) {
            board_at(s.at) = home;
            s.in_wall = false;
            s.at = next;
            board_at(s.at) = FLOOR;
            return SKULL_KILLS;
        }
        s.heading = NO_DIRECTION;                              /* blocked: rest, then pick a new way */
        s.pause = FPS;
        board_at(s.at) = SKULL;
        return SKULL_ROLLS_ON;
    }

    void run_skulls() {
        if (time_till_skull == 0) {
            spawn_skull();
            time_till_skull = skull_gap > 0 ? random_int(skull_gap, skull_gap * 2 - 1) : -1;
        } else if (time_till_skull > 0) {
            --time_till_skull;
        }
        for (size_t i = 0; i < skulls.size();) {
            const SkullFate fate = advance_skull(skulls[i]);
            if (fate == SKULL_ROLLS_ON) { ++i; continue; }
            skulls.erase(skulls.begin() + i);
            if (fate == SKULL_KILLS) die(DEATH_SKULL);
        }
    }

    /* Graves and mantraps are fixtures: put them back once a skull or
     * pursuer has moved off. */
    void restore_fixtures() {
        for (size_t i = 0; i < mantraps.size(); ++i)
            if (board_at(mantraps[i]) == FLOOR) board_at(mantraps[i]) = MANTRAP;
        for (size_t i = 0; i < graves.size(); ++i)
            if (board_at(graves[i]) == FLOOR) board_at(graves[i]) = GRAVE;
    }

    /* The full proof is cheap but pointless every frame. */
    void watch_for_walled_in() {
        if ((++walled_in_ticks & 3) == 0)
            walled_in_frames = walled_in_now() ? walled_in_frames + 4 : 0;
    }

    /* ---- level construction -------------------------------------------
     * The playable interior is x 1..COLS-2, y 1..ROWS-2 and the player starts
     * at its centre, so every pattern below keeps that spot open. */
    static void wall_rect(Level &l, int x0, int y0, int x1, int y1) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) l.set(x, y, WALL);
    }
    static void wall_ring(Level &l, int x0, int y0, int x1, int y1) {
        for (int x = x0; x <= x1; ++x) { l.set(x, y0, WALL); l.set(x, y1, WALL); }
        for (int y = y0; y <= y1; ++y) { l.set(x0, y, WALL); l.set(x1, y, WALL); }
    }
    static void wall_h(Level &l, int y, int x0, int x1, std::initializer_list<int> gaps = {}) {
        for (int x = x0; x <= x1; ++x) l.set(x, y, WALL);
        for (int g : gaps) l.set(g, y, FLOOR);
    }
    static void wall_v(Level &l, int x, int y0, int y1, std::initializer_list<int> gaps = {}) {
        for (int y = y0; y <= y1; ++y) l.set(x, y, WALL);
        for (int g : gaps) l.set(x, g, FLOOR);
    }
    static void wall_dot(Level &l, int x, int y) { l.set(x, y, WALL); }

    /* Broken stretches of wall of length 2-4, scattered with a fixed seed. */
    static void fragments(Level &l, unsigned seed, int count) {
        std::mt19937 g(seed);
        for (int n = 0; n < count; ++n) {
            const int len = 2 + static_cast<int>(g() % 3);
            const bool horizontal = (g() & 1) != 0;
            const int x = 2 + static_cast<int>(g() % (COLS - 5));
            const int y = 2 + static_cast<int>(g() % (ROWS - 5));
            for (int i = 0; i < len; ++i) {
                const int px = x + (horizontal ? i : 0);
                const int py = y + (horizontal ? 0 : i);
                if (px < COLS - 1 && py < ROWS - 1) l.set(px, py, WALL);
            }
        }
    }

    /* Six chapters of five stages. Each chapter has its own architecture:
     * swamp pools, cemetery plots, ossuary niches, ruined walls, a chapel
     * nave, and a terrace with a mausoleum. Stones are scattered afterwards
     * with a fixed seed (see start_level). */
    std::vector<Level> make_levels() {
        std::vector<Level> result(LEVEL_COUNT);
        for (int s = 0; s < LEVEL_COUNT; ++s) {
            Level &l = result[s];
            l.pursuers_per_round = {1, 2};
            /* Easy to pick up, hard to master: the early graves are crowded with
             * stones, so there is always something to trap a pursuer against (but not so many
             * that the smaller board feels packed), and the field thins as the graves go on. The vigil shifts the whole curve. */
            l.stones = 118 - 52 * s / (LEVEL_COUNT - 1);
            l.graves = s < 3 ? 0 : 2 + s / 3;
            l.mantraps = s < 7 ? 0 : 1 + (s - 7) / 3;
            l.skull_frames = s < 5 ? -1 : FPS * (14 - s / 3);
            const int v = s % STAGES_PER_CHAPTER;
            switch (s / STAGES_PER_CHAPTER) {
            case 0:  /* The Forgotten Grave: a flooded burial ground */
                if (v == 1) { wall_rect(l, 4, 3, 5, 4); wall_rect(l, 15, 3, 16, 4);
                              wall_rect(l, 4, 12, 5, 13); wall_rect(l, 15, 12, 16, 13); }
                if (v == 2) { wall_h(l, 2, 9, 11); wall_h(l, 3, 8, 12); wall_h(l, 4, 9, 11);
                              wall_h(l, 12, 9, 11); wall_h(l, 13, 8, 12); wall_h(l, 14, 9, 11); }
                if (v == 3) { l.walls = 14; }
                if (v == 4) { wall_h(l, 5, 3, 9); wall_h(l, 11, 11, 17);
                              wall_rect(l, 15, 3, 16, 4); wall_rect(l, 4, 12, 5, 13); }
                break;
            case 1:  /* The Old Cemetery: rows of headstones and fenced plots */
                if (v == 0) { for (int x = 4; x <= 16; x += 4) for (int y = 3; y <= 13; y += 4) wall_dot(l, x, y); }
                if (v == 1) { wall_h(l, 5, 2, 18, {6, 14}); wall_h(l, 11, 2, 18, {6, 14}); }
                if (v == 2) { wall_rect(l, 7, 2, 13, 4); l.set(10, 4, FLOOR); l.walls = 8; }
                if (v == 3) { for (int x = 2; x <= 18; x += 4) for (int y = 3; y <= 13; y += 2) wall_dot(l, x, y); }
                if (v == 4) { wall_ring(l, 3, 3, 8, 5); l.set(6, 5, FLOOR); wall_ring(l, 12, 3, 17, 5); l.set(14, 5, FLOOR);
                              wall_ring(l, 3, 11, 8, 13); l.set(6, 11, FLOOR); wall_ring(l, 12, 11, 17, 13); l.set(14, 11, FLOOR); }
                break;
            case 2:  /* The Ossuary: niches, pillars and a bone pit */
                if (v == 0) { wall_h(l, 3, 2, 18); wall_h(l, 13, 2, 18);
                              for (int x = 4; x <= 16; x += 3) { l.set(x, 3, FLOOR); l.set(x, 13, FLOOR); } }
                if (v == 1) { for (int x = 4; x <= 16; x += 4) { wall_v(l, x, 4, 5); wall_v(l, x, 11, 12); } }
                if (v == 2) { wall_ring(l, 6, 5, 14, 11); l.set(6, 8, FLOOR); l.set(14, 8, FLOOR); l.set(10, 5, FLOOR); }
                if (v == 3) { wall_h(l, 5, 1, 15, {7, 8}); wall_h(l, 11, 5, 19, {12, 13}); }
                if (v == 4) { wall_v(l, 10, 2, 5); wall_v(l, 10, 11, 14); wall_h(l, 8, 2, 6); wall_h(l, 8, 14, 18); }
                break;
            case 3:  /* The Family Crypt: a chamber of aisles, pews and a tomb in the nave */
                if (v == 0) { for (int y = 3; y <= 13; y += 2) { wall_dot(l, 5, y); wall_dot(l, 15, y); } }
                if (v == 1) { wall_v(l, 6, 2, 6); wall_v(l, 6, 10, 14); wall_v(l, 14, 2, 6); wall_v(l, 14, 10, 14); }
                if (v == 2) { wall_h(l, 8, 1, 7); wall_h(l, 8, 13, 19); wall_v(l, 10, 1, 4); wall_v(l, 10, 12, 15); }
                if (v == 3) { wall_h(l, 4, 3, 8); wall_h(l, 4, 12, 17); wall_h(l, 6, 3, 8); wall_h(l, 6, 12, 17);
                              wall_h(l, 10, 3, 8); wall_h(l, 10, 12, 17); wall_h(l, 12, 3, 8); wall_h(l, 12, 12, 17); }
                if (v == 4) { wall_ring(l, 3, 3, 17, 13);
                              for (int g = 8; g <= 12; ++g) { l.set(g, 3, FLOOR); l.set(g, 13, FLOOR); }
                              for (int g = 6; g <= 10; ++g) { l.set(3, g, FLOOR); l.set(17, g, FLOOR); }
                              for (int x = 6; x <= 14; x += 4) for (int y = 6; y <= 10; y += 4) wall_dot(l, x, y); }
                break;
            case 4:  /* The Chapel Grounds: a ruin, walls broken by time */
                { const int n[5] = {6, 8, 10, 7, 11};
                  fragments(l, 1000u + s, n[v]);
                  if (v == 3) { for (int x = 4; x <= 16; x += 6) for (int y = 4; y <= 12; y += 4) wall_dot(l, x, y); } }
                break;
            case 5:  /* The Ancestral Mausoleum: balustrades and a tomb house */
                if (v == 0) { wall_h(l, 3, 2, 8); wall_h(l, 3, 12, 18); wall_h(l, 13, 2, 8); wall_h(l, 13, 12, 18); }
                if (v == 1) { wall_rect(l, 7, 4, 13, 5); wall_rect(l, 7, 11, 13, 12); }
                if (v == 2) { for (int i = 0; i < 4; ++i) { const int x = i < 2 ? 3 : 16; const int y = (i & 1) ? 12 : 3; wall_rect(l, x, y, x + 1, y + 1); }
                              wall_h(l, 8, 8, 12, {10}); }
                if (v == 3) { wall_v(l, 4, 2, 11, {6}); wall_v(l, 8, 5, 14, {10}); wall_v(l, 12, 2, 11, {6}); wall_v(l, 16, 5, 14, {10}); }
                if (v == 4) { wall_ring(l, 2, 2, 18, 14);
                              for (int g = 9; g <= 11; ++g) { l.set(g, 2, FLOOR); l.set(g, 14, FLOOR); }
                              for (int g = 7; g <= 9; ++g) { l.set(2, g, FLOOR); l.set(18, g, FLOOR); }
                              for (int x = 5; x <= 15; x += 5) for (int y = 5; y <= 11; y += 6) wall_dot(l, x, y); }
                break;
            }
            for (int d = 0; d < 5; ++d) {   /* the start tile and its neighbours stay open */
                const Point q = d < 4 ? moved(l.revenant_start, static_cast<Direction>(d)) : l.revenant_start;
                l.set(q.x, q.y, FLOOR);
            }
        }
        return result;
    }

};

}  // namespace
