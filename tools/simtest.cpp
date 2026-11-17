/* Headless simulator for A Mouse's Vengeance.
 *
 * Runs the real game rules (src/game.hpp) with no SDL, in two modes:
 *
 *   fuzz   random input, checks board/cat bookkeeping invariants every tick
 *          and prints the first violation of each kind with a board dump.
 *   bot    a heuristic player works through stages and reports how often it
 *          clears them, how many lives it burns and how long it takes.
 *
 *   simtest fuzz [--runs N] [--ticks N] [--seed S]
 *   simtest bot  [--diff 0|1|2|all] [--stage N|all] [--runs N] [--seed S]
 *                [--act-every F] [--verbose]
 *
 * The bot is a stand-in for a competent but not brilliant player: it moves at
 * most once every --act-every frames (the device repeats a held direction every
 * 5 frames), flees pursuers that get close and otherwise plans block pushes that
 * squeeze pursuers. Treat its numbers as relative difficulty, not as truth. */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <vector>

#define private public
#include "../src/game.hpp"
#undef private

namespace {

char tile_char(TileType t) {
    switch (t) {
    case FLOOR: return '.';
    case REVENANT: return 'M';
    case REVENANT_IN_GRAVE: return 'm';
    case PURSUER: return 'C';
    case STONE: return '#';
    case WALL: return 'W';
    case OFFERING: return 'c';
    case SEALED: return 'z';
    case MANTRAP: return 'T';
    case GRAVE: return 'O';
    case SKULL: return 'y';
    }
    return '?';
}

void dump_board(const Game &g, const char *title) {
    std::printf("  %s\n", title);
    for (int y = 0; y < ROWS; ++y) {
        std::printf("   ");
        for (int x = 0; x < COLS; ++x) std::putchar(tile_char(g.board[y * COLS + x]));
        std::putchar('\n');
    }
}

const char *state_name(GameState s) {
    static const char *names[] = {"NEW_GAME", "SETUP", "RUNNING", "PAUSED", "DEAD",
                                  "LEVEL_COMPLETE", "GAME_OVER", "WON"};
    return names[s];
}

const char *diff_name(Difficulty d) {
    static const char *names[] = {"Easy", "Normal", "Hard"};
    return names[d];
}

int count_tiles(const Game &g, TileType t) {
    return static_cast<int>(std::count(g.board.begin(), g.board.end(), t));
}

/* ---------- invariants ---------- */

struct Violations {
    std::map<std::string, int> counts;
    bool report(const std::string &kind) { return ++counts[kind] == 1; }
};

/* Returns a description of the first problem, or an empty string. */
std::string check_invariants(const Game &g) {
    char buf[200];
    std::set<int> seen;
    for (size_t i = 0; i < g.pursuers.size(); ++i) {
        const Point p = g.pursuers[i];
        const TileType t = g.board[p.y * COLS + p.x];
        if (t != PURSUER && t != SEALED) {
            std::snprintf(buf, sizeof(buf),
                          "stale-cat-entry: pursuers[%zu]=(%d,%d) but the board there is '%c'",
                          i, p.x, p.y, tile_char(t));
            return buf;
        }
        if (!seen.insert(p.y * COLS + p.x).second) {
            std::snprintf(buf, sizeof(buf), "duplicate-cat-entry at (%d,%d)", p.x, p.y);
            return buf;
        }
    }
    const int on_board = count_tiles(g, PURSUER) + count_tiles(g, SEALED);
    if (on_board != static_cast<int>(g.pursuers.size())) {
        std::snprintf(buf, sizeof(buf), "cat-count-mismatch: %d cat tiles on board, %zu in pursuers[]",
                      on_board, g.pursuers.size());
        return buf;
    }
    if (g.pursuers.size() > 8) {
        std::snprintf(buf, sizeof(buf), "too-many-pursuers: %zu", g.pursuers.size());
        return buf;
    }
    if (g.state == STATE_RUNNING) {
        const TileType m = g.board[g.revenant.y * COLS + g.revenant.x];
        if (m != REVENANT && m != REVENANT_IN_GRAVE) {
            std::snprintf(buf, sizeof(buf), "revenant-missing: revenant=(%d,%d) tile '%c'",
                          g.revenant.x, g.revenant.y, tile_char(m));
            return buf;
        }
    }
    return std::string();
}

/* The quiet counters behind the Journal's older pages, summed over a fuzz run (a check that they move). */
static long g_deed_stumbles = 0, g_deed_offerings = 0, g_deed_shut = 0, g_deed_flawless = 0, g_deed_finished = 0;
static int g_deed_rite = 0;
static long g_event_count[16];       /* GameEvent tallies (push seal rise fall death offering wave grave complete) */

/* ---------- bot ---------- */

double AREA_WEIGHT = 1.0;

int cheb(Point a, Point b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

bool in_board(Point p) { return p.x >= 0 && p.x < COLS && p.y >= 0 && p.y < ROWS; }

const Direction ALL_DIRS[8] = {NORTH, SOUTH, EAST, WEST,
                               NORTH_EAST, NORTH_WEST, SOUTH_EAST, SOUTH_WEST};

struct Bot {
    std::deque<Direction> plan;
    int since_plan;
    Bot() : plan(), since_plan(0) {}

    /* Lower is better: how much freedom the pursuers still have. */
    static double cat_freedom(const Game &g) {
        double total = 0;
        for (size_t i = 0; i < g.pursuers.size(); ++i) {
            const Point c = g.pursuers[i];
            if (g.board_at(c) == SEALED && g.open_neighbours(c).empty()) continue;
            total += 150.0 + 10.0 * g.open_neighbours(c).size();
            int open = 0;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) {
                    const Point q = {c.x + dx, c.y + dy};
                    if (in_board(q) && g.board_at(q) == FLOOR) ++open;
                }
            total += open;
            /* Reachable area for this cat. */
            std::vector<bool> vis(COLS * ROWS, false);
            std::queue<Point> fringe;
            fringe.push(c);
            vis[c.y * COLS + c.x] = true;
            int area = 0;
            while (!fringe.empty() && area < 300) {
                const Point p = fringe.front();
                fringe.pop();
                for (int d = 0; d < 8; ++d) {
                    const Point q = moved(p, ALL_DIRS[d]);
                    if (!in_board(q) || vis[q.y * COLS + q.x]) continue;
                    if (!g.pursuer_can_enter(g.board_at(q))) continue;
                    vis[q.y * COLS + q.x] = true;
                    ++area;
                    fringe.push(q);
                }
            }
            total += area * AREA_WEIGHT;
        }
        return total;
    }

    static bool walkable(TileType t) { return t == FLOOR || t == OFFERING; }

    static bool near_cat(const Game &g, Point p, int radius) {
        for (size_t i = 0; i < g.pursuers.size(); ++i)
            if (cheb(g.pursuers[i], p) <= radius) return true;
        return false;
    }

    bool near_skull(const Game &g, Point p) const {
        if (g.skulls.empty()) return false;
        return skull_danger(g)[p.y * COLS + p.x] != 0;
    }

    /* Tiles a skull ball can hit soon: its neighbours, the line it is rolling
     * along, and a few tiles of each line out of one that is about to roll. */
    std::vector<char> skull_danger(const Game &g) const {
        std::vector<char> danger(COLS * ROWS, 0);
        for (size_t i = 0; i < g.skulls.size(); ++i) {
            const Skull &y = g.skulls[i];
            for (int d = 0; d < 8; ++d) {
                const Point near_tile = moved(y.at, ALL_DIRS[d]);
                if (in_board(near_tile)) danger[near_tile.y * COLS + near_tile.x] = 1;
            }
            for (int d = 0; d < 8; ++d) {
                if (y.heading != NO_DIRECTION && ALL_DIRS[d] != y.heading) continue;
                const int reach = y.heading != NO_DIRECTION ? 30 : 4;
                Point p = y.at;
                for (int step = 0; step < reach; ++step) {
                    p = moved(p, ALL_DIRS[d]);
                    if (!in_board(p)) break;
                    const TileType t = g.board_at(p);
                    if (t == STONE || t == WALL || t == PURSUER || t == SEALED) break;
                    danger[p.y * COLS + p.x] = 1;
                }
            }
        }
        return danger;
    }

    bool yarn_rolling(const Game &g) const {
        for (size_t i = 0; i < g.skulls.size(); ++i)
            if (g.skulls[i].heading != NO_DIRECTION) return true;
        return false;
    }

    /* Would stepping in direction d actually move the revenant onto a safe tile? */
    bool step_ok(const Game &g, Direction d, bool allow_push) const {
        const Point dest = moved(g.revenant, d);
        if (!in_board(dest)) return false;
        const TileType t = g.board_at(dest);
        if (walkable(t)) return true;
        if (allow_push && t == STONE) {
            Game probe = g;
            probe.inputs.clear();
            probe.inputs.push_back(d);
            probe.step_revenant();
            return same_point(probe.revenant, dest);
        }
        return false;
    }

    int open_space(const Game &g, Point start) const {
        std::vector<int> dist(COLS * ROWS, -1);
        std::queue<Point> fringe;
        fringe.push(start);
        dist[start.y * COLS + start.x] = 0;
        int count = 0;
        while (!fringe.empty()) {
            const Point p = fringe.front();
            fringe.pop();
            if (dist[p.y * COLS + p.x] >= 7) continue;
            for (int d = 0; d < 8; ++d) {
                const Point q = moved(p, ALL_DIRS[d]);
                if (!in_board(q) || dist[q.y * COLS + q.x] >= 0) continue;
                if (!walkable(g.board_at(q)) && g.board_at(q) != REVENANT) continue;
                if (near_cat(g, q, 1)) continue;
                dist[q.y * COLS + q.x] = dist[p.y * COLS + p.x] + 1;
                ++count;
                fringe.push(q);
            }
        }
        return count;
    }

    bool flee(const Game &g, Direction &out) const {
        double best = -1e9;
        bool found = false;
        const std::vector<char> yd = skull_danger(g);
        for (int i = 0; i < 8; ++i) {
            const Direction d = ALL_DIRS[i];
            if (!step_ok(g, d, true)) continue;
            const Point dest = moved(g.revenant, d);
            int min_cat = 99;
            for (size_t c = 0; c < g.pursuers.size(); ++c)
                min_cat = std::min(min_cat, cheb(g.pursuers[c], dest));
            double score = min_cat * 10.0 + std::min(open_space(g, dest), 40) * 0.5;
            if (yd[dest.y * COLS + dest.x]) score -= 60;
            if (score > best) { best = score; out = d; found = true; }
        }
        return found;
    }

    /* Plan a walk to a position from which one push helps squeeze a cat. */
    void make_plan(const Game &g, std::mt19937 &rng) {
        plan.clear();
        since_plan = 0;
        if (g.pursuers.empty()) return;
        std::vector<int> dist(COLS * ROWS, -1);
        std::vector<int> parent(COLS * ROWS, -1);
        std::vector<Direction> via(COLS * ROWS, NO_DIRECTION);
        std::queue<Point> fringe;
        fringe.push(g.revenant);
        dist[g.revenant.y * COLS + g.revenant.x] = 0;
        const double before = cat_freedom(g);
        const std::vector<char> yd = skull_danger(g);
        double best = 0.0; /* require a real improvement */
        Point best_pos = g.revenant;
        Direction best_push = NO_DIRECTION;
        int evaluated = 0;
        while (!fringe.empty()) {
            const Point p = fringe.front();
            fringe.pop();
            const int d0 = dist[p.y * COLS + p.x];
            if (d0 > 14) continue;
            for (int i = 0; i < 8; ++i) {
                const Direction d = ALL_DIRS[i];
                const Point q = moved(p, d);
                if (!in_board(q)) continue;
                const TileType t = g.board_at(q);
                if (t == STONE && evaluated < 400 && nearest_cat(g, q) <= 7) {
                    /* Candidate push from p in direction d. */
                    Game probe = g;
                    probe.board_at(probe.revenant) = FLOOR;
                    probe.revenant = p;
                    probe.board_at(p) = REVENANT;
                    probe.inputs.clear();
                    probe.inputs.push_back(d);
                    probe.step_revenant();
                    if (same_point(probe.revenant, q)) {
                        ++evaluated;
                        const double gain = before - cat_freedom(probe) - 0.5 * d0;
                        /* Do not trap ourselves against a cat. */
                        if (gain > best && !near_cat(probe, probe.revenant, 1)) {
                            best = gain;
                            best_pos = p;
                            best_push = d;
                        }
                    }
                }
                if (dist[q.y * COLS + q.x] >= 0) continue;
                if (!walkable(t)) continue;
                if (near_cat(g, q, 2) || yd[q.y * COLS + q.x]) continue;
                dist[q.y * COLS + q.x] = d0 + 1;
                parent[q.y * COLS + q.x] = p.y * COLS + p.x;
                via[q.y * COLS + q.x] = d;
                fringe.push(q);
            }
        }
        (void)rng;
        if (best_push == NO_DIRECTION) return;
        std::vector<Direction> reversed;
        int cur = best_pos.y * COLS + best_pos.x;
        while (parent[cur] >= 0) {
            reversed.push_back(via[cur]);
            cur = parent[cur];
        }
        for (size_t i = reversed.size(); i-- > 0;) plan.push_back(reversed[i]);
        plan.push_back(best_push);
    }

    static int nearest_cat(const Game &g, Point p) {
        int best = 99;
        for (size_t i = 0; i < g.pursuers.size(); ++i) best = std::min(best, cheb(g.pursuers[i], p));
        return best;
    }

    /* Returns NO_DIRECTION for "stay put". */
    Direction choose(const Game &g, std::mt19937 &rng) {
        Direction d = NO_DIRECTION;
        if (!g.skulls.empty() && skull_danger(g)[g.revenant.y * COLS + g.revenant.x] &&
            (yarn_rolling(g) || nearest_cat(g, g.revenant) > 2)) {
            plan.clear();
            if (flee(g, d)) return d;
        }
        if (!g.pursuers.empty() && nearest_cat(g, g.revenant) <= 2) {
            plan.clear();
            if (flee(g, d)) return d;
            return NO_DIRECTION;
        }
        if (++since_plan > 10 || plan.empty()) make_plan(g, rng);
        if (!plan.empty()) {
            d = plan.front();
            plan.pop_front();
            const Point dest = moved(g.revenant, d);
            /* The plan may have gone stale; do not walk into trouble. */
            if (in_board(dest) && g.board_at(dest) != MANTRAP && g.board_at(dest) != GRAVE &&
                g.board_at(dest) != PURSUER && !near_skull(g, dest) && !near_cat(g, dest, 1))
                return d;
            plan.clear();
        }
        return NO_DIRECTION;
    }
};


/* Beam search over revenant moves using the real game rules as the forward model.
 * Cats chase the revenant deterministically while a path exists, so a short look-
 * ahead sees most of what they will do. Each search step is one input (the
 * device repeats a held direction every 5 frames); the plan is re-made every
 * couple of inputs. */
struct BeamBot {
    int width;
    int depth;
    int act_every;
    int replan_every;
    std::deque<Direction> plan;
    std::vector<char> yd_cache;

    BeamBot(int w, int d, int every) : width(w), depth(d), act_every(every), replan_every(2), plan(), yd_cache() {}

    struct Node {
        Game g;
        double score;
        std::vector<char> actions;
        Node(const Game &game) : g(game), score(0), actions() {}
    };

    static unsigned long long hash_state(const Game &g) {
        unsigned long long h = 1469598103934665603ULL;
        for (size_t i = 0; i < g.board.size(); ++i) {
            h ^= static_cast<unsigned long long>(g.board[i] + 2);
            h *= 1099511628211ULL;
        }
        h ^= static_cast<unsigned long long>(g.pursuer_clock + 7);
        h *= 1099511628211ULL;
        return h;
    }

    static double cheap_freedom(const Game &g) {
        double total = 0;
        for (size_t i = 0; i < g.pursuers.size(); ++i) {
            const Point c = g.pursuers[i];
            if (g.board_at(c) == SEALED && g.open_neighbours(c).empty()) continue;
            total += 150.0 + 12.0 * g.open_neighbours(c).size();
            int open = 0;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) {
                    const Point q = {c.x + dx, c.y + dy};
                    if (in_board(q) && g.board_at(q) == FLOOR) ++open;
                }
            total += open;
        }
        return total;
    }

    double evaluate(const Game &g, const std::vector<char> &skull) const {
        if (g.state == STATE_DEAD || g.state == STATE_GAME_OVER) return -1e6;
        if (g.state == STATE_LEVEL_COMPLETE) return 1e6;
        double score = -cheap_freedom(g);
        if (g.pursuers.empty()) score += 400;
        int nearest = 99;
        for (size_t i = 0; i < g.pursuers.size(); ++i) nearest = std::min(nearest, cheb(g.pursuers[i], g.revenant));
        if (nearest <= 1) score -= 300;
        else if (nearest == 2) score -= 60;
        if (g.trapped_frames > 0) score -= 150;
        if (!skull.empty() && skull[g.revenant.y * COLS + g.revenant.x]) score -= 200;
        return score;
    }

    Direction choose(const Game &g, const Bot &helper) {
        if (!plan.empty()) {
            const Direction d = plan.front();
            plan.pop_front();
            return d;
        }
        /* Nothing to hunt: stand still unless a skull ball is about to hit. */
        const std::vector<char> skull = g.skulls.empty() ? std::vector<char>() : helper.skull_danger(g);
        if (g.pursuers.empty() && (skull.empty() || !skull[g.revenant.y * COLS + g.revenant.x])) return NO_DIRECTION;

        std::vector<Node> beam;
        beam.push_back(Node(g));
        double best_score = -1e18;
        std::vector<char> best_actions;
        for (int level = 0; level < depth; ++level) {
            std::vector<Node> next;
            std::set<unsigned long long> seen;
            for (size_t n = 0; n < beam.size(); ++n) {
                for (int a = 0; a < 9; ++a) {
                    Node child(beam[n].g);
                    const Direction d = a < 8 ? ALL_DIRS[a] : NO_DIRECTION;
                    if (d != NO_DIRECTION) child.g.turn(d);
                    for (int t = 0; t < act_every && child.g.state == STATE_RUNNING; ++t) child.g.tick();
                    child.score = evaluate(child.g, child.g.skulls.empty() ? std::vector<char>() : helper.skull_danger(child.g));
                    if (child.score <= -1e5) continue;
                    if (!seen.insert(hash_state(child.g)).second) continue;
                    child.actions = beam[n].actions;
                    child.actions.push_back(static_cast<char>(a));
                    if (child.score > best_score || (child.score == best_score && child.actions.size() < best_actions.size())) {
                        best_score = child.score;
                        best_actions = child.actions;
                    }
                    if (child.score >= 1e5) { plan.clear(); goto done; }
                    next.push_back(child);
                }
            }
            if (next.empty()) break;
            std::sort(next.begin(), next.end(), [](const Node &x, const Node &y) { return x.score > y.score; });
            if (static_cast<int>(next.size()) > width) next.erase(next.begin() + width, next.end());
            beam.swap(next);
        }
    done:
        if (best_actions.empty()) return NO_DIRECTION;
        for (size_t i = 0; i < best_actions.size() && static_cast<int>(i) < replan_every; ++i)
            plan.push_back(best_actions[i] < 8 ? ALL_DIRS[static_cast<int>(best_actions[i])] : NO_DIRECTION);
        const Direction d = plan.front();
        plan.pop_front();
        return d;
    }
};

/* ---------- runs ---------- */

struct StageResult {
    bool cleared;
    bool game_over;
    bool timed_out;
    int deaths;
    int frames;
    int max_cats;
    int score;
    int causes[5];
    int waves_cleared;
};

struct RunConfig {
    int act_every;
    int max_ticks;
    bool verbose;
    bool use_bot;
    int trace_every;
    int beam_width;
    int beam_depth;
    int cat_slow; /* experiment: multiply the cat period (1 = the real game) */
};

StageResult play_stage(Difficulty diff, int stage, unsigned seed, const RunConfig &cfg,
                       Violations *violations) {
    Game g(seed);
    g.start_stage(diff, stage);
    if (cfg.cat_slow > 1) g.pursuer_period *= cfg.cat_slow;
    struct DeedTally {              /* adds this run's quiet counters to the totals, however the run ends */
        const Game &game;
        explicit DeedTally(const Game &gm) : game(gm) {}
        ~DeedTally() {
            g_deed_stumbles += game.stumble_total;
            g_deed_offerings += game.offering_total;
            g_deed_shut += game.shut_total;
            g_deed_rite = std::max(g_deed_rite, game.best_rite);
            if (game.state == STATE_LEVEL_COMPLETE) { ++g_deed_finished; if (game.stage_deaths == 0) ++g_deed_flawless; }
        }
    } deed_tally(g);
    Bot bot;
    BeamBot beam(cfg.beam_width > 0 ? cfg.beam_width : 1, cfg.beam_depth, cfg.act_every);
    std::mt19937 rng(seed * 7919u + 13u);
    StageResult r = {false, false, false, 0, 0, 0, 0, {0, 0, 0, 0, 0}, 0};
    Game previous = g;
    bool watching = false;
    Point watch_mouse = {0, 0};
    std::vector<char> watch_region;     /* the closed region the revenant was proven to be confined to */
    int watch_lives = 0;
    std::vector<TileType> watch_blocks;
    Direction held = NO_DIRECTION;
    int held_for = 0;
    GameState last_state = g.state;
    const int fuzz_cats_before = 0;
    (void)fuzz_cats_before;
    for (int tick = 0; tick < cfg.max_ticks; ++tick) {
        if (g.state == STATE_RUNNING) {
            if (cfg.use_bot) {
                if (tick % cfg.act_every == 0) {
                    const Direction d = cfg.beam_width > 0 ? beam.choose(g, bot) : bot.choose(g, rng);
                    if (d != NO_DIRECTION) g.turn(d);
                }
            } else {
                /* Random walker: hold a direction for a few frames, then change. */
                if (held_for <= 0) {
                    held = static_cast<Direction>(rng() % 9);
                    held_for = 1 + static_cast<int>(rng() % 12);
                }
                --held_for;
                if (held != NO_DIRECTION && tick % 5 == 0) g.turn(held);
            }
        }
        if (cfg.trace_every > 0 && tick % cfg.trace_every == 0 && g.state == STATE_RUNNING) {
            char title[120];
            std::snprintf(title, sizeof(title), "tick %d, %zu pursuers, round %d, lives %d, plan %zu:", tick,
                          g.pursuers.size(), g.round, g.lives, bot.plan.size());
            dump_board(g, title);
        }
        previous = g;
        g.tick();
        { const std::vector<int> ev = g.take_events(); for (size_t i = 0; i < ev.size(); ++i) if (ev[i] >= 0 && ev[i] < 16) ++g_event_count[ev[i]]; }
        ++r.frames;
        {   /* debugging aid: SIM_TRACE_FROM=<tick> prints every tick from there on */
            static const char *from = std::getenv("SIM_TRACE_FROM");
            if (from && tick >= std::atoi(from) && tick < std::atoi(from) + 6) {
                char t[80];
                std::snprintf(t, sizeof(t), "after tick %d (skulls %zu, pursuers %zu):", tick, g.skulls.size(), g.pursuers.size());
                dump_board(g, t);
                for (size_t i = 0; i < g.skulls.size(); ++i)
                    std::printf("    skull %zu at (%d,%d) heading %d pause %d in_wall %d\n", i, g.skulls[i].at.x,
                                g.skulls[i].at.y, static_cast<int>(g.skulls[i].heading), g.skulls[i].pause, g.skulls[i].in_wall);
            }
        }
        if (g.state == STATE_DEAD && last_state != STATE_DEAD) {
            ++r.deaths;
            ++r.causes[g.death_cause];
            if (cfg.verbose) {
                static const char *names[] = {"none", "walked into cat", "caught by cat", "trap", "skull"};
                std::printf("\nDEATH %s: stage %d (%s) seed %u tick %d, %zu pursuers, revenant (%d,%d)\n",
                            names[g.death_cause], stage + 1, diff_name(diff), seed, tick,
                            g.pursuers.size(), g.revenant.x, g.revenant.y);
                dump_board(previous, "board before:");
                dump_board(g, "board after:");
            }
        }
        last_state = g.state;
        if (violations && g.is_walled_in()) {
            if (!watching) {
                watching = true;
                watch_mouse = g.revenant;
                watch_region.assign(COLS * ROWS, 0);
                { std::vector<Point> q(1, g.revenant);      /* walking about inside its own cell block is not a violation */
                  watch_region[g.revenant.y * COLS + g.revenant.x] = 1;
                  for (size_t h = 0; h < q.size(); ++h)
                      for (int dy = -1; dy <= 1; ++dy)
                          for (int dx = -1; dx <= 1; ++dx) {
                              const int nx = q[h].x + dx, ny = q[h].y + dy;
                              if (nx < 0 || ny < 0 || nx >= COLS || ny >= ROWS || watch_region[ny * COLS + nx]) continue;
                              const TileType t = g.board[ny * COLS + nx];
                              if (t == WALL || t == STONE) continue;
                              watch_region[ny * COLS + nx] = 1;
                              q.push_back(Point{nx, ny});
                          } }
                watch_lives = g.lives;
                watch_blocks = g.board;
                for (size_t i = 0; i < watch_blocks.size(); ++i) if (watch_blocks[i] != STONE) watch_blocks[i] = FLOOR;
                ++violations->counts["(info) walled-in declared"];
                if (cfg.verbose) {
                    std::printf("\nWALLED IN declared: stage %d (%s) seed %u tick %d\n", stage + 1, diff_name(diff), seed, tick);
                    dump_board(g, "board:");
                }
            }
        }
        if (watching && violations) {
            std::vector<TileType> blocks = g.board;
            for (size_t i = 0; i < blocks.size(); ++i) if (blocks[i] != STONE) blocks[i] = FLOOR;
            if (g.state == STATE_RUNNING && (!watch_region[g.revenant.y * COLS + g.revenant.x] || blocks != watch_blocks)) {
                if (violations->report("FALSE-POSITIVE walled-in: the board changed afterwards")) {
                    std::printf("\nFALSE POSITIVE (walled in, then it moved): stage %d (%s) seed %u tick %d\n", stage + 1, diff_name(diff), seed, tick);
                    dump_board(g, "board:");
                }
                return r;
            }
            if (g.state == STATE_DEAD || g.lives != watch_lives || g.state == STATE_LEVEL_COMPLETE) {
                ++violations->counts["(info) walled-in ended by death or completion"];
                watching = false;
            }
        }
        if (!previous.pursuers.empty() && g.pursuers.empty()) ++r.waves_cleared;
        r.max_cats = std::max(r.max_cats, static_cast<int>(g.pursuers.size()));
        if (violations) {
            const std::string problem = check_invariants(g);
            if (!problem.empty()) {
                const std::string kind = problem.substr(0, problem.find(':'));
                if (violations->report(kind)) {
                    std::printf("\nVIOLATION %s\n  %s | stage %d (%s) seed %u tick %d state %s, pursuers %zu\n",
                                kind.c_str(), problem.c_str(), stage + 1, diff_name(diff), seed, tick,
                                state_name(g.state), g.pursuers.size());
                    dump_board(previous, "board before the tick:");
                    dump_board(g, "board after the tick:");
                }
                /* Reconcile so one bug does not bury later, different ones. */
                return r;
            }
        }
        if (g.state == STATE_LEVEL_COMPLETE) {
            r.cleared = true;
            r.score = g.stage_score;
            return r;
        }
        if (g.state == STATE_GAME_OVER) {
            r.game_over = true;
            return r;
        }
    }
    r.timed_out = true;
    return r;
}

int arg_int(int argc, char **argv, const char *name, int fallback) {
    for (int i = 2; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], name)) return std::atoi(argv[i + 1]);
    return fallback;
}

const char *arg_str(int argc, char **argv, const char *name, const char *fallback) {
    for (int i = 2; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], name)) return argv[i + 1];
    return fallback;
}

bool arg_flag(int argc, char **argv, const char *name) {
    for (int i = 2; i < argc; ++i)
        if (!std::strcmp(argv[i], name)) return true;
    return false;
}

int run_fuzz(int argc, char **argv) {
    const int runs = arg_int(argc, argv, "--runs", 30);
    const int ticks = arg_int(argc, argv, "--ticks", 20000);
    const unsigned seed0 = static_cast<unsigned>(arg_int(argc, argv, "--seed", 1));
    const bool use_bot = arg_flag(argc, argv, "--bot");
    RunConfig cfg = {5, ticks, false, use_bot, 0, 0, 0, 1};
    Violations violations;
    int total = 0;
    int max_cats = 0;
    for (int diff = 0; diff < 3; ++diff)
        for (int stage = 0; stage < LEVEL_COUNT; ++stage)
            for (int run = 0; run < runs; ++run) {
                const StageResult r = play_stage(static_cast<Difficulty>(diff), stage,
                                                 seed0 + static_cast<unsigned>(run * 131 + stage * 17 + diff),
                                                 cfg, &violations);
                ++total;
                max_cats = std::max(max_cats, r.max_cats);
            }
    std::printf("\nfuzz: %d runs (%s), max pursuers seen %d\n", total, use_bot ? "bot" : "random walker", max_cats);
    std::printf("events: push %ld, seal %ld, rise %ld, fall %ld, death %ld, offering %ld, wave %ld, grave %ld, complete %ld\n",
                g_event_count[EV_PUSH], g_event_count[EV_SEAL], g_event_count[EV_RISE], g_event_count[EV_FALL], g_event_count[EV_DEATH],
                g_event_count[EV_OFFERING], g_event_count[EV_WAVE], g_event_count[EV_GRAVE], g_event_count[EV_COMPLETE]);
        std::printf("deeds: stumbles %ld, offerings %ld, shut %ld, best rite %d, graves settled %ld (flawless %ld)\n",
                g_deed_stumbles, g_deed_offerings, g_deed_shut, g_deed_rite, g_deed_finished, g_deed_flawless);
    if (violations.counts.empty()) std::printf("no invariant violations\n");
    for (std::map<std::string, int>::const_iterator it = violations.counts.begin();
         it != violations.counts.end(); ++it)
        std::printf("  %-24s %d run(s)\n", it->first.c_str(), it->second);
    return violations.counts.empty() ? 0 : 1;
}

int run_bot(int argc, char **argv) {
    const int runs = arg_int(argc, argv, "--runs", 12);
    const unsigned seed0 = static_cast<unsigned>(arg_int(argc, argv, "--seed", 1));
    const char *diff_arg = arg_str(argc, argv, "--diff", "all");
    const char *stage_arg = arg_str(argc, argv, "--stage", "all");
    RunConfig cfg = {arg_int(argc, argv, "--act-every", 5), arg_int(argc, argv, "--ticks", 40000),
                     arg_flag(argc, argv, "--verbose"), true, arg_int(argc, argv, "--trace", 0),
                     arg_int(argc, argv, "--width", 0), arg_int(argc, argv, "--depth", 12),
                     arg_int(argc, argv, "--catslow", 1)};
    Violations violations;
    std::printf("%-7s %-5s %6s %7s %8s %9s %8s | %6s %6s %5s %5s %5s %12s\n", "diff", "stage", "clear%", "deaths",
                "seconds", "max pursuers", "gameover", "walked", "caught", "trap", "skull", "stall", "cleared-waves");
    for (int diff = 0; diff < 3; ++diff) {
        if (std::strcmp(diff_arg, "all") && std::atoi(diff_arg) != diff) continue;
        for (int stage = 0; stage < LEVEL_COUNT; ++stage) {
            if (std::strcmp(stage_arg, "all") && std::atoi(stage_arg) - 1 != stage) continue;
            int cleared = 0, over = 0, deaths = 0, frames = 0, max_cats = 0, stalled = 0, waves_cleared = 0;
            int causes[5] = {0, 0, 0, 0, 0};
            for (int run = 0; run < runs; ++run) {
                const StageResult r = play_stage(static_cast<Difficulty>(diff), stage,
                                                 seed0 + static_cast<unsigned>(run * 131 + stage * 17 + diff),
                                                 cfg, &violations);
                cleared += r.cleared;
                over += r.game_over;
                deaths += r.deaths;
                stalled += r.timed_out;
                waves_cleared += r.waves_cleared;
                for (int c = 0; c < 5; ++c) causes[c] += r.causes[c];
                frames += r.frames;
                max_cats = std::max(max_cats, r.max_cats);
            }
            std::printf("%-7s %-5d %5d%% %7.1f %8.0f %9d %7d%% | %6d %6d %5d %5d %5d %12.1f\n",
                        diff_name(static_cast<Difficulty>(diff)),
                        stage + 1, cleared * 100 / runs, static_cast<double>(deaths) / runs,
                        static_cast<double>(frames) / runs / FPS, max_cats, over * 100 / runs,
                        causes[DEATH_WALKED_INTO_PURSUER], causes[DEATH_CAUGHT_BY_PURSUER], causes[DEATH_MANTRAP],
                        causes[DEATH_SKULL], stalled, static_cast<double>(waves_cleared) / runs);
            std::fflush(stdout);
        }
    }
    for (std::map<std::string, int>::const_iterator it = violations.counts.begin();
         it != violations.counts.end(); ++it)
        std::printf("INVARIANT %s hit in %d run(s)\n", it->first.c_str(), it->second);
    return 0;
}

}  // namespace


/* ---------- walled-in detector tests ---------- */

/* An empty arena: wall border, revenant in the middle, no pursuers. */
Game blank_game() {
    Game g(1);
    g.state = STATE_RUNNING;
    std::fill(g.board.begin(), g.board.end(), FLOOR);
    for (int x = 0; x < COLS; ++x) { g.board[x] = WALL; g.board[(ROWS - 1) * COLS + x] = WALL; }
    for (int y = 0; y < ROWS; ++y) { g.board[y * COLS] = WALL; g.board[y * COLS + COLS - 1] = WALL; }
    g.pursuers.clear();
    g.mantraps.clear();
    g.graves.clear();
    g.skulls.clear();
    g.revenant = {11, 11};
    g.board[11 * COLS + 11] = REVENANT;
    g.round = 0;
    g.current_waves = 6;
    return g;
}

void put(Game &g, int x, int y, TileType t) { g.board[y * COLS + x] = t; }

/* Surround (x, y) on the four sides and the four diagonals with t. */
void ring(Game &g, int x, int y, TileType t) {
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy) put(g, x + dx, y + dy, t);
}

int selftest_failures = 0;
void expect(const char *name, bool got, bool want) {
    if (got != want) {
        ++selftest_failures;
        std::printf("FAIL  %-58s got %d, want %d\n", name, got, want);
    } else {
        std::printf("ok    %s\n", name);
    }
}

/* Independent check using the real revenant code: from this position, can ANY
 * sequence of the four moves change anything except where the revenant stands?
 * (A push, a trap, a hole or a cat all change the board or end the run.) */
bool brute_force_can_change_anything(const Game &start) {
    std::set<int> seen;
    std::deque<Point> frontier;
    frontier.push_back(start.revenant);
    seen.insert(start.revenant.y * COLS + start.revenant.x);
    std::vector<TileType> base = start.board;
    base[start.revenant.y * COLS + start.revenant.x] = FLOOR;
    while (!frontier.empty()) {
        const Point at = frontier.front();
        frontier.pop_front();
        for (int i = 0; i < 4; ++i) {
            Game probe = start;
            probe.board[probe.revenant.y * COLS + probe.revenant.x] = FLOOR;
            probe.revenant = at;
            probe.board[at.y * COLS + at.x] = REVENANT;
            probe.inputs.clear();
            probe.inputs.push_back(static_cast<Direction>(i));
            const TileType hit = probe.step_revenant();
            if (hit == PURSUER || hit == MANTRAP || hit == GRAVE || hit == OFFERING || hit == SEALED ||
                hit == SKULL)
                return true;
            std::vector<TileType> now = probe.board;
            now[probe.revenant.y * COLS + probe.revenant.x] = FLOOR;
            if (now != base) return true; /* a block moved */
            const int key = probe.revenant.y * COLS + probe.revenant.x;
            if (seen.insert(key).second) frontier.push_back(probe.revenant);
        }
    }
    return false;
}

/* Rules that must hold whatever the implementation: pushing, falling, sealing. */
void rule_tests() {
    auto stones_on_board = [](const Game &g) { return count_tiles(g, STONE); };
    { Game g = blank_game(); g.revenant = {5, 8}; put(g, 11, 11, FLOOR); put(g, 5, 8, REVENANT);
      put(g, 6, 8, STONE); put(g, 7, 8, STONE); put(g, 8, 8, GRAVE);
      const int before = stones_on_board(g);
      g.inputs.push_back(EAST); g.step_revenant();
      expect("push a line of two stones into a grave: the front one is gone",
             g.revenant.x == 6 && g.board[8 * COLS + 7] == STONE && g.board[8 * COLS + 8] == GRAVE &&
                 stones_on_board(g) == before - 1, true); }
    { Game g = blank_game(); g.revenant = {5, 8}; put(g, 11, 11, FLOOR); put(g, 5, 8, REVENANT);
      put(g, 6, 8, STONE); put(g, 7, 8, PURSUER); g.pursuers.push_back({7, 8}); put(g, 8, 8, GRAVE);
      g.inputs.push_back(EAST); g.step_revenant();
      expect("a pursuer at the front of the line falls in and leaves the list",
             g.pursuers.empty() && g.board[8 * COLS + 7] == STONE && check_invariants(g).empty(), true); }
    { Game g = blank_game(); g.revenant = {5, 8}; put(g, 11, 11, FLOOR); put(g, 5, 8, REVENANT);
      put(g, 6, 8, STONE); put(g, 7, 8, WALL);
      g.inputs.push_back(EAST); g.step_revenant();
      expect("a stone against a wall does not move, nor does the revenant", g.revenant.x == 5, true); }
    { Game g = blank_game(); g.revenant = {5, 8}; put(g, 11, 11, FLOOR); put(g, 5, 8, REVENANT);
      put(g, 6, 8, STONE); put(g, 7, 8, OFFERING);
      g.inputs.push_back(EAST); g.step_revenant();
      expect("a stone crushes an offering", g.revenant.x == 6 && g.board[8 * COLS + 7] == STONE, true); }
    { Game g = blank_game(); g.revenant = {5, 8}; put(g, 11, 11, FLOOR); put(g, 5, 8, REVENANT);
      put(g, 6, 8, STONE); put(g, 7, 8, SEALED); g.pursuers.push_back({7, 8});
      g.inputs.push_back(EAST); g.step_revenant();
      expect("a sealed pursuer in the line is carried and its entry follows",
             g.pursuers.size() == 1 && g.pursuers[0].x == 8 && g.board[8 * COLS + 8] == SEALED &&
                 check_invariants(g).empty(), true); }
    { Game g = blank_game(); g.revenant = {5, 8}; put(g, 11, 11, FLOOR); put(g, 5, 8, REVENANT);
      put(g, 6, 8, SEALED); g.pursuers.push_back({6, 8});
      g.inputs.push_back(EAST); g.step_revenant();
      expect("the revenant cannot push a pursuer directly", g.revenant.x == 5, true); }
    { Game g = blank_game(); g.difficulty = DIFFICULTY_SLOW; g.set_pursuer_period();
      for (int y = 1; y < ROWS - 1; ++y) put(g, 7, y, GRAVE);
      g.pursuers.push_back({4, 8}); put(g, 4, 8, PURSUER);
      int cycles = 0;
      while (!g.pursuers.empty() && cycles < 400) { g.pursuer_clock = g.pursuer_period; g.move_pursuers(); ++cycles; }
      expect("a careless pursuer behind a wall of graves walks into one", g.pursuers.empty(), true);
      expect("the grave it fell into is still there", g.board[8 * COLS + 7] == GRAVE || count_tiles(g, GRAVE) > 0, true); }
    { Game g = blank_game(); g.difficulty = DIFFICULTY_SLOW; g.set_pursuer_period();
      put(g, 4, 8, PURSUER); g.pursuers.push_back({4, 8}); ring(g, 4, 8, STONE);
      g.pursuer_clock = g.pursuer_period; g.move_pursuers();
      expect("a pursuer boxed in on eight sides is sealed, then becomes an offering (Easy: no delay)",
             g.pursuers.empty() && g.board[8 * COLS + 4] == OFFERING, true); }
    { Game g = blank_game(); g.difficulty = DIFFICULTY_MEDIUM; g.set_pursuer_period();
      put(g, 4, 8, PURSUER); g.pursuers.push_back({4, 8}); ring(g, 4, 8, STONE);
      g.pursuer_clock = g.pursuer_period; g.move_pursuers();
      expect("on Normal the sealed pursuer waits before the flame rises",
             g.pursuers.size() == 1 && g.board[8 * COLS + 4] == SEALED, true); }
    { Game g = blank_game(); g.difficulty = DIFFICULTY_MEDIUM; g.set_pursuer_period();
      put(g, 4, 8, PURSUER); g.pursuers.push_back({4, 8}); ring(g, 4, 8, STONE);
      g.pursuer_clock = g.pursuer_period; g.move_pursuers();
      put(g, 3, 8, FLOOR);                               /* a stone shifts: a gap opens */
      g.pursuer_clock = g.pursuer_period; g.move_pursuers();
      expect("a gap wakes a sealed pursuer", g.board[8 * COLS + 4] != SEALED || g.pursuers[0].x != 4, true); }
}

int run_selftest() {
    rule_tests();
    /* Hand-built cases. */
    { Game g = blank_game(); ring(g, 11, 11, WALL);
      expect("1-tile pocket, all eight neighbours walls", g.walled_in_now(), true); }
    { Game g = blank_game(); ring(g, 11, 11, STONE);
      expect("1-tile pocket, neighbours are blocks with open floor behind", g.walled_in_now(), false); }
    { Game g = blank_game(); ring(g, 11, 11, STONE);
      for (int d = -2; d <= 2; ++d) { put(g, 11 + d, 9, WALL); put(g, 11 + d, 13, WALL); put(g, 9, 11 + d, WALL); put(g, 13, 11 + d, WALL); }
      expect("blocks all round, a wall right behind each", g.walled_in_now(), true); }
    { Game g = blank_game(); ring(g, 11, 11, STONE);
      for (int d = -2; d <= 2; ++d) { put(g, 11 + d, 9, WALL); put(g, 11 + d, 13, WALL); put(g, 9, 11 + d, WALL); put(g, 13, 11 + d, WALL); }
      put(g, 11, 9, FLOOR);
      expect("same, but one push has an empty tile behind it", g.walled_in_now(), false); }
    { Game g = blank_game(); ring(g, 11, 11, WALL); put(g, 11, 10, GRAVE);
      expect("hole next to the revenant (it can step in)", g.walled_in_now(), false); }
    { Game g = blank_game(); ring(g, 11, 11, WALL); put(g, 12, 11, MANTRAP);
      expect("trap next to the revenant (it can step on it)", g.walled_in_now(), false); }
    { Game g = blank_game(); ring(g, 11, 11, WALL); put(g, 10, 11, OFFERING);
      expect("cheese next to the revenant (it can step there)", g.walled_in_now(), false); }
    { Game g = blank_game(); g.revenant = {1, 8}; put(g, 11, 11, FLOOR); put(g, 1, 8, REVENANT);
      put(g, 2, 7, WALL); put(g, 2, 8, WALL); put(g, 2, 9, WALL); put(g, 1, 7, WALL); put(g, 1, 9, WALL);
      g.round = g.current_waves; g.skull_gap = -1;
      expect("pocket against the boundary, skulls off: safe", g.walled_in_now(), true);
      g.skull_gap = 96;
      expect("same pocket with skulls on: one could roll in from the boundary", g.walled_in_now(), false); }
    { Game g = blank_game(); ring(g, 11, 11, WALL); put(g, 11, 12, PURSUER); g.pursuers.push_back({11, 12});
      expect("cat next to the revenant", g.walled_in_now(), false); }
    { Game g = blank_game(); ring(g, 11, 11, WALL); put(g, 10, 10, SKULL);
      expect("skull ball diagonal to the revenant", g.walled_in_now(), false); }
    { Game g = blank_game(); ring(g, 11, 11, WALL); put(g, 10, 10, FLOOR);
      expect("open tile on a diagonal (pursuers step diagonally)", g.walled_in_now(), false); }
    { Game g = blank_game(); ring(g, 11, 11, WALL); put(g, 11, 10, STONE); put(g, 11, 9, STONE); put(g, 11, 8, PURSUER); g.pursuers.push_back({11, 8});
      expect("block chain with a cat at its end", g.walled_in_now(), false); }
    { Game g = blank_game(); ring(g, 11, 11, WALL); put(g, 11, 10, STONE); put(g, 11, 9, STONE); put(g, 11, 8, MANTRAP);
      expect("two-block chain ending at a revenant trap", g.walled_in_now(), true); }
    { Game g = blank_game(); ring(g, 11, 11, WALL); put(g, 11, 10, STONE); put(g, 11, 9, STONE); put(g, 11, 8, GRAVE);
      expect("two-block chain ending at a hole (block drops in)", g.walled_in_now(), false); }
    { Game g = blank_game(); g.trapped_frames = 100; g.board[11 * COLS + 11] = REVENANT_IN_GRAVE; ring(g, 11, 11, WALL);
      expect("revenant sitting in a hole", g.walled_in_now(), false); }
    { Game g = blank_game();
      for (int y = 6; y <= 16; ++y) for (int x = 6; x <= 16; ++x)
          if (x == 6 || x == 16 || y == 6 || y == 16) put(g, x, y, WALL);
      expect("big closed room, waves still to come (a cat could spawn)", g.walled_in_now(), false);
      g.round = 6;
      expect("big closed room, last wave already in", g.walled_in_now(), true); }
    { Game g = blank_game();
      for (int y = 6; y <= 16; ++y) for (int x = 6; x <= 16; ++x)
          if (x == 6 || x == 16 || y == 6 || y == 16) put(g, x, y, WALL);
      put(g, 8, 8, PURSUER); g.pursuers.push_back({8, 8}); g.round = 6;
      expect("closed room with a cat inside", g.walled_in_now(), false); }
    { Game g = blank_game();
      for (int y = 6; y <= 16; ++y) for (int x = 6; x <= 16; ++x)
          if (x == 6 || x == 16 || y == 6 || y == 16) put(g, x, y, WALL);
      put(g, 16, 10, FLOOR); g.round = 6; put(g, 18, 10, PURSUER); g.pursuers.push_back({18, 10});
      expect("closed room, gap in the wall, a cat outside in the open", g.walled_in_now(), false); }
    { Game g = blank_game(); g.round = 6; put(g, 10, 11, STONE); put(g, 9, 11, WALL);
      put(g, 12, 11, STONE); put(g, 13, 11, WALL); put(g, 11, 10, WALL); put(g, 11, 12, WALL);
      ring(g, 11, 11, WALL); put(g, 10, 11, STONE); put(g, 12, 11, STONE);
      expect("sealed with the two side blocks against walls", g.walled_in_now(), true); }

    /* Random boards: the detector must never say "walled in" where the real
     * revenant code can still change something. Cats, traps and holes are
     * sprinkled in, and boards range from open to packed. */
    std::mt19937 rng(12345);
    long boards = 0, claimed = 0, truly_stuck = 0, false_positive = 0, missed = 0;
    for (int n = 0; n < 400000; ++n) {
        Game g = blank_game();
        const int density = 20 + static_cast<int>(rng() % 70);
        const int wall_share = static_cast<int>(rng() % 100);
        const int final_round = static_cast<int>(rng() % 2);
        g.round = final_round ? 6 : 0;
        g.revenant = {1 + static_cast<int>(rng() % (COLS - 2)), 1 + static_cast<int>(rng() % (ROWS - 2))};
        for (int y = 1; y < ROWS - 1; ++y)
            for (int x = 1; x < COLS - 1; ++x) {
                const int roll = static_cast<int>(rng() % 100);
                if (roll >= density) continue;
                const int kind = static_cast<int>(rng() % 100);
                TileType t = kind < wall_share ? WALL : STONE;
                if (kind > 94) t = GRAVE;
                else if (kind > 92) t = MANTRAP;
                else if (kind > 90) t = OFFERING;
                else if (kind == 90) { t = PURSUER; g.pursuers.push_back({x, y}); }
                else if (kind == 89) t = SKULL;
                put(g, x, y, t);
            }
        put(g, g.revenant.x, g.revenant.y, REVENANT);
        g.pursuers.erase(std::remove_if(g.pursuers.begin(), g.pursuers.end(), [&](Point c) { return g.board[c.y * COLS + c.x] != PURSUER; }),
                     g.pursuers.end());
        ++boards;
        const bool claim = g.walled_in_now();
        const bool can_change = brute_force_can_change_anything(g);
        if (claim) ++claimed;
        if (!can_change) ++truly_stuck;
        if (claim && can_change) {
            ++false_positive;
            if (false_positive <= 3) {
                std::printf("FALSE POSITIVE on random board %d:\n", n);
                dump_board(g, "board:");
            }
        }
        if (!claim && !can_change && g.board_at(g.revenant) == REVENANT) ++missed;
    }
    std::printf("\nrandom boards: %ld, detector said walled-in: %ld, brute force found nothing to do: %ld\n"
                "  false positives (must be 0): %ld\n"
                "  walled-in cases the detector declined (fine, it is conservative): %ld\n",
                boards, claimed, truly_stuck, false_positive, missed);
    if (false_positive) ++selftest_failures;
    std::printf("\nselftest: %s\n", selftest_failures ? "FAILED" : "all passed");
    return selftest_failures ? 1 : 0;
}

int run_levels() {
    std::printf("%-7s %-5s %6s %6s %6s %6s %5s %5s %5s %6s\n", "diff", "stage", "blocks", "walls", "holes",
                "traps", "waves", "wsec", "pursuers", "skull-s");
    for (int diff = 0; diff < 3; ++diff)
        for (int stage = 0; stage < LEVEL_COUNT; ++stage) {
            Game g(1);
            g.start_stage(static_cast<Difficulty>(diff), stage);
            int walls_inside = 0;
            for (int y = 1; y < ROWS - 1; ++y)
                for (int x = 1; x < COLS - 1; ++x)
                    if (g.board[y * COLS + x] == WALL) ++walls_inside;
            const Level &l = (*g.levels)[stage];
            Level cfg = l;
            g.configure_level(cfg);
            std::printf("%-7s %-5d %6d %6d %6d %6d %5d %5d %5d %6.1f\n", diff_name(static_cast<Difficulty>(diff)),
                        stage + 1, count_tiles(g, STONE), walls_inside, static_cast<int>(g.graves.size()),
                        static_cast<int>(g.mantraps.size()), cfg.waves, cfg.wave_seconds,
                        cfg.pursuers_per_round.empty() ? 0 : cfg.pursuers_per_round[0],
                        cfg.skull_frames > 0 ? static_cast<double>(cfg.skull_frames) / FPS : -1.0);
        }
    return 0;
}

/* Prints one stage's opening board, and reports any stage whose open cells
 * could not all be reached from the start. */
int run_view(int argc, char **argv) {
    const int diff = arg_int(argc, argv, "--diff", 1);
    const int stage = arg_int(argc, argv, "--stage", 1) - 1;
    Game g(1);
    g.start_stage(static_cast<Difficulty>(diff), stage);
    char title[80];
    std::snprintf(title, sizeof(title), "%s stage %d (chapter %d)", diff_name(static_cast<Difficulty>(diff)),
                  stage + 1, chapter_of(stage) + 1);
    dump_board(g, title);
    return 0;
}

int run_check_levels() {
    int bad = 0;
    for (int diff = 0; diff < 3; ++diff)
        for (int stage = 0; stage < LEVEL_COUNT; ++stage) {
            Game g(1);
            g.start_stage(static_cast<Difficulty>(diff), stage);
            if (!g.all_open_cells_reachable(g.revenant)) {
                std::printf("NOT CONNECTED: %s stage %d\n", diff_name(static_cast<Difficulty>(diff)), stage + 1);
                ++bad;
            }
        }
    std::printf("level check: %d of %d stage layouts not connected\n", bad, 3 * LEVEL_COUNT);
    return bad ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && !std::strcmp(argv[1], "view")) return run_view(argc, argv);
    if (argc >= 2 && !std::strcmp(argv[1], "check")) return run_check_levels();
    if (argc >= 2 && !std::strcmp(argv[1], "levels")) return run_levels();
    if (argc >= 2 && !std::strcmp(argv[1], "selftest")) return run_selftest();
    if (argc >= 2 && !std::strcmp(argv[1], "fuzz")) return run_fuzz(argc, argv);
    if (argc >= 2 && !std::strcmp(argv[1], "bot")) return run_bot(argc, argv);
    std::fprintf(stderr,
                 "usage: simtest fuzz [--runs N] [--ticks N] [--seed S] [--bot]\n"
                 "       simtest bot  [--diff 0|1|2|all] [--stage N|all] [--runs N] [--seed S]"
                 " [--act-every F] [--ticks N]\n");
    return 2;
}
