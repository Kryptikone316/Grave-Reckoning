/* Grave Reckoning: renderer, menus and main loop. The game rules live in
 * game.hpp (no SDL there, so the simulator can run them headless). */
#include "hwdisp.h"
#include "joykey.h"
#include "music.h"
#include "sfx.h"

#include <sys/resource.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <limits>
#include <queue>
#include <random>
#include <string>
#include <vector>

#include "game.hpp"

namespace {

/* ------------------------------------------------------------ palette */
struct Rgb { Uint8 r, g, b; };
const Rgb C_PANEL = {26, 20, 34};
const Rgb C_PANEL_HI = {44, 35, 58};
const Rgb C_EDGE = {176, 134, 70};      /* brass */
const Rgb C_TEXT = {236, 230, 212};     /* moonlight */
const Rgb C_DIM = {178, 170, 158};
const Rgb C_BRASS = {222, 178, 98};
const Rgb C_SELECT = {206, 160, 80};
const Rgb C_SELECT_TEXT = {24, 16, 22};
const Rgb C_LOCKED = {22, 18, 28};
const Rgb C_BLOOD = {176, 52, 56};

SDL_Color sdl_color(Rgb c) { return SDL_Color{c.r, c.g, c.b, 255}; }

/* ------------------------------------------------------------ assets */
const int PURSUER_KINDS = 4;
const int BLOCK_STYLES = 3;   /* crack patterns; the material comes from the chapter */

struct Assets {
    SDL_Surface *revenant[4][2];       /* down, up, left, right */
    SDL_Surface *revenant_in_grave;
    SDL_Surface *pursuer[PURSUER_KINDS][2][2];   /* kind, step, mirrored */
    SDL_Surface *entombed;
    SDL_Surface *offering;
    SDL_Surface *mantrap;
    SDL_Surface *grave;
    SDL_Surface *skull;
    SDL_Surface *block[CHAPTER_COUNT][BLOCK_STYLES];
    SDL_Surface *floor[CHAPTER_COUNT];
    SDL_Surface *wall;                 /* recoloured copy, RGBA */
    unsigned char wall_role[TILE * TILE];
    int wall_chapter;
    int wall_theme;
    std::vector<SDL_Surface *> death;
    SDL_Surface *arena[CHAPTER_COUNT + 1];       /* empty floor, by chapter 1..6 */
    SDL_Surface *arena_art[CHAPTER_COUNT + 1];   /* with objects, dimmed for menus */
    SDL_Surface *arena_art_bright[CHAPTER_COUNT + 1];
    SDL_Surface *board_bg;
    int board_bg_chapter;
    SDL_Surface *title[3];                  /* loaded when first shown */
    SDL_Surface *glow;                      /* a soft warm blob, for lantern light (made when first needed) */
    SDL_Surface *fog;                       /* a tileable strip of drifting mist for the title (ditto) */
    TTF_Font *small_font;
    TTF_Font *medium_font;
    TTF_Font *large_font;
    TTF_Font *number_font;   /* plain face for digits: the blackletter zero is hard to read */
    TTF_Font *tiny_font;     /* for page titles on the small Journal cards */
    TTF_Font *scrawl_font;   /* the investigator's handwriting */
    TTF_Font *scrawl_big;
    TTF_Font *scrawl_small;  /* a smaller, older hand for the notes other keepers left */
    std::string base;

    Assets() : revenant_in_grave(NULL), entombed(NULL), offering(NULL), mantrap(NULL), grave(NULL),
               skull(NULL), wall(NULL), wall_chapter(-1), wall_theme(-1), death(), board_bg(NULL),
               board_bg_chapter(-1), glow(NULL), fog(NULL), small_font(NULL), medium_font(NULL), large_font(NULL), number_font(NULL), tiny_font(NULL), scrawl_font(NULL), scrawl_big(NULL), scrawl_small(NULL), base() {
        std::memset(revenant, 0, sizeof(revenant));
        std::memset(pursuer, 0, sizeof(pursuer));
        std::memset(block, 0, sizeof(block));
        std::memset(floor, 0, sizeof(floor));
        std::memset(arena, 0, sizeof(arena));
        std::memset(arena_art, 0, sizeof(arena_art));
        std::memset(arena_art_bright, 0, sizeof(arena_art_bright));
        std::memset(title, 0, sizeof(title));
        std::memset(wall_role, 0, sizeof(wall_role));
    }
};

SDL_Surface *load_png(const std::string &path) {
    SDL_Surface *raw = IMG_Load(path.c_str());
    if (!raw) { std::fprintf(stderr, "asset: %s: %s\n", path.c_str(), IMG_GetError()); return NULL; }
    /* Match the framebuffer so every blit is a straight copy. */
    SDL_Surface *fast = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGB565, 0);
    SDL_FreeSurface(raw);
    return fast;
}

/* Sprites keep their transparency as a colour key, so they stand on the floor
 * with no square backdrop and every blit is still a plain copy. */
SDL_Surface *load_sprite(const std::string &path) {
    SDL_Surface *raw = IMG_Load(path.c_str());
    if (!raw) { std::fprintf(stderr, "asset: %s: %s\n", path.c_str(), IMG_GetError()); return NULL; }
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(raw);
    if (!rgba) return NULL;
    SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, rgba->w, rgba->h, 16, SDL_PIXELFORMAT_RGB565);
    if (!out) { SDL_FreeSurface(rgba); return NULL; }
    const Uint32 key = SDL_MapRGB(out->format, 255, 0, 255);
    for (int y = 0; y < rgba->h; ++y) {
        const Uint8 *from = static_cast<const Uint8 *>(rgba->pixels) + y * rgba->pitch;
        Uint16 *to = reinterpret_cast<Uint16 *>(static_cast<Uint8 *>(out->pixels) + y * out->pitch);
        for (int x = 0; x < rgba->w; ++x)
            to[x] = from[x * 4 + 3] < 128 ? static_cast<Uint16>(key)
                                         : static_cast<Uint16>(SDL_MapRGB(out->format, from[x * 4], from[x * 4 + 1], from[x * 4 + 2]));
    }
    SDL_FreeSurface(rgba);
    SDL_SetColorKey(out, SDL_TRUE, key);
    return out;
}

SDL_Surface *art(const Assets &a, const std::string &name) { return load_sprite(a.base + "/Art/" + name); }

SDL_Surface *mirrored(SDL_Surface *src) {
    if (!src) return NULL;
    SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 16, SDL_PIXELFORMAT_RGB565);
    if (!out) return NULL;
    Uint32 key = 0;
    if (SDL_GetColorKey(src, &key) == 0) SDL_SetColorKey(out, SDL_TRUE, key);
    for (int y = 0; y < src->h; ++y) {
        const Uint16 *from = reinterpret_cast<const Uint16 *>(static_cast<const Uint8 *>(src->pixels) + y * src->pitch);
        Uint16 *to = reinterpret_cast<Uint16 *>(static_cast<Uint8 *>(out->pixels) + y * out->pitch);
        for (int x = 0; x < src->w; ++x) to[x] = from[src->w - 1 - x];
    }
    return out;
}

/* Darkens a surface once so menu text stays readable over the art. */
SDL_Surface *dimmed(SDL_Surface *src, int percent_dark) {
    if (!src) return NULL;
    SDL_Surface *out = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGB565, 0);
    if (!out) return NULL;
    const int keep = 100 - percent_dark;
    for (int y = 0; y < out->h; ++y) {
        Uint16 *row = reinterpret_cast<Uint16 *>(static_cast<Uint8 *>(out->pixels) + y * out->pitch);
        for (int x = 0; x < out->w; ++x) {
            const Uint16 p = row[x];
            const int r = ((p >> 11) & 31) * keep / 100;
            const int g = ((p >> 5) & 63) * keep / 100;
            const int b = (p & 31) * keep / 100;
            row[x] = static_cast<Uint16>((r << 11) | (g << 5) | b);
        }
    }
    return out;
}

/* Wall colours: mortar, lit brick, shaded brick. */
struct WallTheme { const char *name; Rgb mortar, light, dark; };

const int WALL_STYLE_COUNT = 10;
const WallTheme WALL_STYLES[WALL_STYLE_COUNT] = {
    {"Crypt Stone", {44, 46, 58}, {108, 112, 132}, {72, 76, 96}},
    {"Bone Ash", {66, 62, 56}, {188, 178, 156}, {132, 124, 108}},
    {"Oxblood", {52, 28, 32}, {150, 50, 56}, {100, 30, 38}},
    {"Old Brass", {52, 40, 26}, {180, 130, 62}, {122, 84, 38}},
    {"Mossy Vault", {32, 44, 36}, {88, 130, 92}, {54, 86, 60}},
    {"Midnight", {26, 30, 52}, {68, 84, 170}, {40, 52, 114}},
    {"Violet Vigil", {44, 32, 58}, {142, 98, 190}, {94, 60, 136}},
    {"Plague Green", {38, 44, 26}, {138, 152, 70}, {92, 104, 44}},
    {"Moonstone", {58, 64, 74}, {208, 220, 230}, {148, 162, 176}},
    {"Ember", {54, 32, 22}, {210, 104, 48}, {140, 64, 28}},
};
/* What each chapter's masonry looks like unless the player picks a colour. */
const int CHAPTER_WALL_STYLE[CHAPTER_COUNT] = {4, 0, 1, 6, 7, 2};

const char *wall_option_name(int theme) {
    return theme == 0 ? "Chapter's Own" : WALL_STYLES[theme - 1].name;
}

void apply_wall_theme(Assets &a, int theme, int chapter) {
    if (!a.wall) return;
    a.wall_theme = theme;
    a.wall_chapter = chapter;
    const WallTheme &t = WALL_STYLES[theme == 0 ? CHAPTER_WALL_STYLE[chapter] : theme - 1];
    const Rgb colour[3] = {t.mortar, t.light, t.dark};
    for (int y = 0; y < TILE; ++y) {
        Uint8 *to = static_cast<Uint8 *>(a.wall->pixels) + y * a.wall->pitch;
        for (int x = 0; x < TILE; ++x) {
            const Rgb c = colour[a.wall_role[y * TILE + x] % 3];
            to[x * 4] = c.r; to[x * 4 + 1] = c.g; to[x * 4 + 2] = c.b; to[x * 4 + 3] = 255;
        }
    }
}

/* April 1st: the other blackletter, in which every "k" looks like an "f". (Book of the Interred
 * becomes something else entirely.) --date MM-DD forces a date for testing. */
std::string g_forced_date;

bool is_april_first() {
    if (!g_forced_date.empty()) return g_forced_date == "04-01";
    const time_t now = std::time(NULL);
    struct tm local;
    if (!localtime_r(&now, &local)) return false;
    return local.tm_mon == 3 && local.tm_mday == 1;
}

bool load_assets(Assets &a, const std::string &base) {
    a.base = base;
    static const char *revenant_name[4] = {"Down", "Up", "Left", "Right"};
    for (int d = 0; d < 4; ++d)
        for (int f = 0; f < 2; ++f) {
            char name[48];
            std::snprintf(name, sizeof(name), "Revenant%s%d.png", revenant_name[d], f);
            a.revenant[d][f] = art(a, name);
        }
    a.revenant_in_grave = art(a, "RevenantInGrave.png");
    static const char *pursuer_name[PURSUER_KINDS] = {"Constable", "Tophat", "Mourner", "Digger"};
    for (int k = 0; k < PURSUER_KINDS; ++k)
        for (int f = 0; f < 2; ++f) {
            char name[48];
            std::snprintf(name, sizeof(name), "%s%d.png", pursuer_name[k], f);
            a.pursuer[k][f][0] = art(a, name);
            a.pursuer[k][f][1] = mirrored(a.pursuer[k][f][0]);
        }
    a.entombed = art(a, "Entomb/frame4.png");
    a.offering = art(a, "Offering.png");
    a.mantrap = art(a, "Mantrap.png");
    a.grave = art(a, "Grave.png");
    a.skull = art(a, "Skull.png");
    for (int c = 0; c < CHAPTER_COUNT; ++c)
        for (int i = 0; i < BLOCK_STYLES; ++i)
            a.block[c][i] = art(a, "Block" + std::to_string(c + 1) + "_" + std::to_string(i) + ".png");
    for (int c = 0; c < CHAPTER_COUNT; ++c) a.floor[c] = art(a, "Chapters/Floor" + std::to_string(c + 1) + ".png");
    for (int i = 1; i <= 7; ++i) a.death.push_back(art(a, "Death/frame" + std::to_string(i) + ".png"));
    for (int c = 1; c <= CHAPTER_COUNT; ++c) {
        a.arena[c] = load_png(base + "/Arenas/Arena" + std::to_string(c) + ".png");
        SDL_Surface *bright = load_png(base + "/Arenas/Arena" + std::to_string(c) + "Art.png");
        a.arena_art_bright[c] = bright;
        a.arena_art[c] = dimmed(bright, 55);
    }

    SDL_Surface *roles = IMG_Load((base + "/Art/Wall.png").c_str());
    if (roles) {
        SDL_Surface *rgba = SDL_ConvertSurfaceFormat(roles, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(roles);
        if (rgba && rgba->w == TILE && rgba->h == TILE) {
            for (int y = 0; y < TILE; ++y)
                for (int x = 0; x < TILE; ++x)
                    a.wall_role[y * TILE + x] = static_cast<Uint8 *>(rgba->pixels)[y * rgba->pitch + x * 4];
            a.wall = rgba;
        } else {
            SDL_FreeSurface(rgba);
        }
    }
    /* Blackletter for titles and menu labels, a plain face for small text. A
     * file called TitleFont.ttf next to the game, if present, replaces the
     * bundled blackletter (handy for a personal copy with a licensed font). */
    const std::string body_path = base + "/Vera.ttf";
    std::string title_path = base + "/TitleFont.ttf";
    if (FILE *probe = std::fopen(title_path.c_str(), "rb")) std::fclose(probe);
    else title_path = base + (is_april_first() ? "/Fonts/UnifrakturCook-Bold.ttf" : "/Fonts/PirataOne-Regular.ttf");
    a.small_font = TTF_OpenFont(body_path.c_str(), 14);
    a.medium_font = TTF_OpenFont(title_path.c_str(), 22);
    a.large_font = TTF_OpenFont(title_path.c_str(), 32);
    a.number_font = TTF_OpenFont(body_path.c_str(), 20);
    a.tiny_font = TTF_OpenFont(body_path.c_str(), 11);
    a.scrawl_font = TTF_OpenFont((base + "/Fonts/CoveredByYourGrace.ttf").c_str(), 20);
    a.scrawl_big = TTF_OpenFont((base + "/Fonts/CoveredByYourGrace.ttf").c_str(), 24);
    a.scrawl_small = TTF_OpenFont((base + "/Fonts/CoveredByYourGrace.ttf").c_str(), 17);
    if (!a.small_font || !a.medium_font || !a.large_font || !a.number_font || !a.tiny_font || !a.scrawl_font || !a.scrawl_big ||
        !a.scrawl_small) {
        std::fprintf(stderr, "font: %s\n", TTF_GetError());
        return false;
    }
    return a.revenant[0][0] && a.pursuer[0][0][0] && a.wall && a.block[0][0] && a.arena[1] && a.arena_art[1];
}

/* ------------------------------------------------------------ drawing */
void fill_rect(SDL_Surface *s, int x, int y, int w, int h, Rgb c) {
    SDL_Rect r = {x, y, w, h};
    SDL_FillRect(s, &r, SDL_MapRGB(s->format, c.r, c.g, c.b));
}

/* A rectangle laid over what is already there, alpha 0 (clear) to 255 (solid). */
void fill_rect_alpha(SDL_Surface *s, int x, int y, int w, int h, Rgb c, int alpha) {
    if (w <= 0 || h <= 0 || alpha <= 0) return;
    if (s->format->format == SDL_PIXELFORMAT_RGB565) {
        /* the screen is 16-bit: blend straight into the pixels, no temporary surface */
        const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(s->w, x + w), y1 = std::min(s->h, y + h);
        const int a = std::min(255, alpha) + (alpha > 127 ? 1 : 0), inv = 256 - a;
        const int cr = (c.r >> 3) * a, cg = (c.g >> 2) * a, cb = (c.b >> 3) * a;
        for (int row = y0; row < y1; ++row) {
            Uint16 *p = reinterpret_cast<Uint16 *>(static_cast<Uint8 *>(s->pixels) + row * s->pitch) + x0;
            for (int col = x0; col < x1; ++col, ++p) {
                const unsigned v = *p;
                const unsigned r = (((v >> 11) & 31) * inv + cr) >> 8, g = (((v >> 5) & 63) * inv + cg) >> 8, b = ((v & 31) * inv + cb) >> 8;
                *p = static_cast<Uint16>((r << 11) | (g << 5) | b);
            }
        }
        return;
    }
    SDL_Surface *layer = SDL_CreateRGBSurface(0, w, h, 32, 0xFF0000, 0x00FF00, 0x0000FF, 0);
    if (!layer) return;
    SDL_FillRect(layer, NULL, SDL_MapRGB(layer->format, c.r, c.g, c.b));
    SDL_SetSurfaceBlendMode(layer, SDL_BLENDMODE_BLEND);
    SDL_SetSurfaceAlphaMod(layer, static_cast<Uint8>(alpha));
    SDL_Rect d = {x, y, w, h};
    SDL_BlitSurface(layer, NULL, s, &d);
    SDL_FreeSurface(layer);
}

void blit(SDL_Surface *target, SDL_Surface *asset, int x, int y) {
    if (!asset) return;
    SDL_Rect d = {x, y, asset->w, asset->h};
    SDL_BlitSurface(asset, NULL, target, &d);
}

void blit_scaled(SDL_Surface *target, SDL_Surface *asset, int x, int y, int w, int h) {
    if (!asset) return;
    SDL_Rect d = {x, y, w, h};
    SDL_BlitScaled(asset, NULL, target, &d);
}

int text_width(TTF_Font *font, const std::string &text) {
    int w = 0, h = 0;
    if (font) TTF_SizeUTF8(font, text.c_str(), &w, &h);
    return w;
}

/* Text audit (run with --audit): every panel, button, caption and cell registers its
 * rectangle, and any text whose centre is inside one must fit inside it. Anything that
 * does not is reported on stderr, so overflow is found by running every screen
 * (tools/textaudit.sh), not by eye. */
bool g_audit = false;
bool g_effects = true;          /* the Screen Effects option: fades, flashes, shake, dust, flicker, the living title */
unsigned g_frame = 0;           /* the frame counter, for things that move by themselves */
std::vector<SDL_Rect> g_boxes;

void audit_box(int x, int y, int w, int h) {
    if (g_audit) g_boxes.push_back(SDL_Rect{x, y, w, h});
}

void audit_text(const std::string &text, int x, int y, int w, int h) {
    if (x < 0 || y < 0 || x + w > FB_W || y + h > FB_H) {
        std::fprintf(stderr, "TEXT-OFF-SCREEN: '%s' at (%d,%d) %dx%d\n", text.c_str(), x, y, w, h);
        return;
    }
    const int cx = x + w / 2, cy = y + h / 2;
    const SDL_Rect *best = NULL;
    for (size_t i = 0; i < g_boxes.size(); ++i) {
        const SDL_Rect &b = g_boxes[i];
        if (cx < b.x || cx > b.x + b.w || cy < b.y || cy > b.y + b.h) continue;
        if (!best || b.w * b.h < best->w * best->h) best = &b;
    }
    if (best && (x < best->x - 1 || x + w > best->x + best->w + 1 || y < best->y - 7 || y + h > best->y + best->h + 7))
        std::fprintf(stderr, "TEXT-OVERFLOW: '%s' is %dx%d at (%d,%d), its box is %dx%d at (%d,%d)\n", text.c_str(), w, h, x, y,
                     best->w, best->h, best->x, best->y);
}

/* A caption or balloon must stay inside the comic panel it belongs to. */
void audit_inside(const char *what, int x, int y, int w, int h, const SDL_Rect &outer) {
    if (g_audit && (x < outer.x || y < outer.y || x + w > outer.x + outer.w || y + h > outer.y + outer.h))
        std::fprintf(stderr, "TEXT-BOX-OUTSIDE-PANEL: %s %dx%d at (%d,%d) leaves its panel %dx%d at (%d,%d)\n", what, w, h, x, y,
                     outer.w, outer.h, outer.x, outer.y);
}

/* Shortens text with "..." until it fits. */
std::string ellipsize(TTF_Font *font, const std::string &text, int max_width) {
    if (!font || text_width(font, text) <= max_width) return text;
    std::string t = text;
    while (!t.empty() && text_width(font, t + "...") > max_width) t.erase(t.size() - 1);
    return t + "...";
}

void draw_text_at(SDL_Surface *target, TTF_Font *font, const std::string &text, int x, int baseline_y, Rgb c) {
    if (!font || text.empty()) return;
    SDL_Surface *s = TTF_RenderUTF8_Blended(font, text.c_str(), sdl_color(c));
    if (!s) return;
    if (g_audit) audit_text(text, x, baseline_y - s->h, s->w, s->h);
    SDL_Rect d = {x, baseline_y - s->h, s->w, s->h};
    SDL_BlitSurface(s, NULL, target, &d);
    SDL_FreeSurface(s);
}

void draw_text(SDL_Surface *target, TTF_Font *font, const std::string &text, int center_x, int baseline_y, Rgb c) {
    draw_text_at(target, font, text, center_x - text_width(font, text) / 2, baseline_y, c);
}

void draw_text_right(SDL_Surface *target, TTF_Font *font, const std::string &text, int right_x, int baseline_y, Rgb c) {
    draw_text_at(target, font, text, right_x - text_width(font, text), baseline_y, c);
}

/* Title text with a dark shadow so it stays legible on any artwork. */
void draw_title(SDL_Surface *target, TTF_Font *font, const std::string &text, int center_x, int baseline_y, Rgb c) {
    draw_text(target, font, text, center_x + 2, baseline_y + 2, {8, 6, 12});
    draw_text(target, font, text, center_x, baseline_y, c);
}

void frame_rect(SDL_Surface *s, int x, int y, int w, int h, Rgb edge, int t) {
    fill_rect(s, x, y, w, t, edge);
    fill_rect(s, x, y + h - t, w, t, edge);
    fill_rect(s, x, y, t, h, edge);
    fill_rect(s, x + w - t, y, t, h, edge);
}

void draw_panel(SDL_Surface *target, int x, int y, int w, int h) {
    audit_box(x + 4, y + 4, w - 8, h - 8);
    fill_rect(target, x - 3, y - 3, w + 6, h + 6, {8, 6, 12});
    fill_rect(target, x, y, w, h, C_PANEL);
    frame_rect(target, x, y, w, h, C_EDGE, 2);
    frame_rect(target, x + 4, y + 4, w - 8, h - 8, {96, 74, 44}, 1);
}

void draw_menu_item(SDL_Surface *target, TTF_Font *font, const std::string &label, int y, bool selected,
                    int x = 70, int w = 340) {
    fill_rect(target, x - 3, y - 31, w + 6, 44, {8, 6, 12});
    fill_rect(target, x, y - 28, w, 38, selected ? C_SELECT : C_PANEL_HI);
    frame_rect(target, x, y - 28, w, 38, selected ? C_BRASS : C_EDGE, selected ? 3 : 1);
    audit_box(x + 3, y - 25, w - 6, 32);
    draw_text(target, font, ellipsize(font, label, w - 20), x + w / 2, y, selected ? C_SELECT_TEXT : C_TEXT);
}

const char *const CHAPTER_NAME[CHAPTER_COUNT] = {
    "The Forgotten Grave", "The Old Cemetery", "The Ossuary",
    "The Family Crypt", "The Chapel Grounds", "The Ancestral Mausoleum"};
const char *const CHAPTER_SHORT[CHAPTER_COUNT] = {
    "Forgotten Grave", "Old Cemetery", "Ossuary", "Family Crypt", "Chapel Grounds", "Ancestral Mausoleum"};
const char *const ROMAN[CHAPTER_COUNT] = {"I", "II", "III", "IV", "V", "VI"};
/* Two short lines at the start of each chapter. Small hints, nothing more. */
const char *const CHAPTER_STORY[CHAPTER_COUNT][2] = {
    {"In the oldest corner of the grounds,", "a grave long neglected stirred at last..."},
    {"The living found the disturbance before dawn,", "and came with purpose..."},
    {"Beneath the grounds lies the house of many bones,", "where even the dead are accounted for..."},
    {"Some graves are not for the common dead,", "but for those whose names must never be spoken again..."},
    {"At the chapel, the faithful gather,", "not for mourning, but for containment..."},
    {"At last, the heart of the grounds, where the", "oldest names still watch... and the truth remains sealed."},
};

const char *difficulty_name(Difficulty d) {
    switch (d) {
    case DIFFICULTY_SLOW: return "THE MOURNERS";
    case DIFFICULTY_FAST: return "THE FAITHFUL";
    default: return "THE WATCH";
    }
}

const char *difficulty_label(Difficulty d) {
    switch (d) {
    case DIFFICULTY_SLOW: return "Easy: The Mourners";
    case DIFFICULTY_FAST: return "Hard: The Faithful";
    default: return "Normal: The Watch";
    }
}

/* Menu backdrop: the dimmed art of one chapter's arena. */
void draw_backdrop(SDL_Surface *target, const Assets &a, int chapter) {
    chapter = std::max(0, std::min(CHAPTER_COUNT - 1, chapter));
    SDL_Surface *s = a.arena_art[chapter + 1];
    if (s) blit(target, s, 0, 0);
    else fill_rect(target, 0, 0, FB_W, FB_H, C_PANEL);
}

void draw_hint(SDL_Surface *target, const Assets &a, const std::string &text, int y = 462) {
    const int w = text_width(a.small_font, text) + 24;
    audit_box(FB_W / 2 - w / 2, y - 18, w, 24);
    fill_rect(target, FB_W / 2 - w / 2, y - 18, w, 24, {12, 9, 18});
    draw_text(target, a.small_font, text, FB_W / 2, y, C_TEXT);
}

/* ------------------------------------------------------------ board */
void ensure_chapter_assets(Assets &a, int chapter, int wall_theme) {
    if (a.wall_chapter != chapter || a.wall_theme != wall_theme) apply_wall_theme(a, wall_theme, chapter);
    if (a.board_bg_chapter != chapter) {
        if (a.board_bg) SDL_FreeSurface(a.board_bg);
        a.board_bg = SDL_CreateRGBSurfaceWithFormat(0, (COLS - 2) * TILE, (ROWS - 2) * TILE, 16, SDL_PIXELFORMAT_RGB565);
        if (a.board_bg && a.floor[chapter]) {
            for (int y = 0; y < ROWS - 2; ++y)
                for (int x = 0; x < COLS - 2; ++x) blit(a.board_bg, a.floor[chapter], x * TILE, y * TILE);
        }
        a.board_bg_chapter = chapter;
    }
}

int revenant_row(Direction d) {
    switch (d) {
    case NORTH: return 1;
    case WEST: return 2;
    case EAST: return 3;
    default: return 0;
    }
}

/* ---- small effects ---- */
unsigned noise_hash(unsigned a) {
    a ^= a << 13; a ^= a >> 17; a ^= a << 5;
    a *= 2654435761u;
    return a ^ (a >> 15);
}

/* A soft round blob of warm light that adds to whatever is under it. */
void ensure_glow(Assets &a) {
    if (a.glow) return;
    const int n = 40;
    a.glow = SDL_CreateRGBSurface(0, n, n, 32, 0xFF0000, 0x00FF00, 0x0000FF, 0xFF000000);
    if (!a.glow) return;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const double dx = (x - n / 2 + 0.5) / (n / 2), dy = (y - n / 2 + 0.5) / (n / 2);
            double v = 1.0 - std::sqrt(dx * dx + dy * dy);
            v = v < 0 ? 0 : v * v;
            const Uint32 alpha = static_cast<Uint32>(v * 255);
            static_cast<Uint32 *>(a.glow->pixels)[y * (a.glow->pitch / 4) + x] = (alpha << 24) | (255u << 16) | (176u << 8) | 84u;
        }
    SDL_SetSurfaceBlendMode(a.glow, SDL_BLENDMODE_ADD);
}

void draw_glow(SDL_Surface *target, const Assets &a, int cx, int cy, int size, int strength) {
    if (!a.glow || strength <= 0) return;
    SDL_SetSurfaceAlphaMod(a.glow, static_cast<Uint8>(std::min(255, strength)));
    SDL_Rect to = {cx - size / 2, cy - size / 2, size, size};
    SDL_BlitScaled(a.glow, NULL, target, &to);
}

/* Slow mist, tileable sideways: smooth value noise, clear towards the top and bottom edges. */
void ensure_fog(Assets &a) {
    if (a.fog) return;
    const int w = 480, h = 64, cell_x = 40, cell_y = 16;
    const int gx = w / cell_x, gy = h / cell_y + 2;
    std::vector<double> lattice(static_cast<size_t>(gx * gy));
    for (int j = 0; j < gy; ++j)
        for (int i = 0; i < gx; ++i) lattice[static_cast<size_t>(j * gx + i)] = (noise_hash(static_cast<unsigned>(j * 131 + i * 17 + 5)) & 1023) / 1023.0;
    a.fog = SDL_CreateRGBSurface(0, w, h, 32, 0xFF0000, 0x00FF00, 0x0000FF, 0xFF000000);
    if (!a.fog) return;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int ix = x / cell_x, iy = y / cell_y;
            double tx = (x % cell_x) / static_cast<double>(cell_x), ty = (y % cell_y) / static_cast<double>(cell_y);
            tx = tx * tx * (3 - 2 * tx);
            ty = ty * ty * (3 - 2 * ty);
            const double v00 = lattice[static_cast<size_t>(iy * gx + ix % gx)], v10 = lattice[static_cast<size_t>(iy * gx + (ix + 1) % gx)];
            const double v01 = lattice[static_cast<size_t>((iy + 1) * gx + ix % gx)], v11 = lattice[static_cast<size_t>((iy + 1) * gx + (ix + 1) % gx)];
            double v = (v00 * (1 - tx) + v10 * tx) * (1 - ty) + (v01 * (1 - tx) + v11 * tx) * ty;
            const double edge = std::sin(3.14159265 * (y + 0.5) / h);
            v = v * v * edge;
            const Uint32 alpha = static_cast<Uint32>(std::min(1.0, v * 2.0) * 150);
            static_cast<Uint32 *>(a.fog->pixels)[y * (a.fog->pitch / 4) + x] = (alpha << 24) | (156u << 16) | (170u << 8) | (196u);
        }
    SDL_SetSurfaceBlendMode(a.fog, SDL_BLENDMODE_BLEND);
}

/* Dust kicked up when a stone is shoved: short-lived specks that drift and fade. */
struct Mote { double x, y, vx, vy; int life, total; };

void draw_motes(SDL_Surface *target, std::vector<Mote> &motes) {
    for (size_t i = 0; i < motes.size();) {
        Mote &m = motes[i];
        m.x += m.vx; m.y += m.vy; m.vy += 0.04;
        if (--m.life <= 0) { motes[i] = motes.back(); motes.pop_back(); continue; }
        const int alpha = 150 * m.life / m.total;
        fill_rect_alpha(target, static_cast<int>(m.x), static_cast<int>(m.y), 2, 2, Rgb{176, 164, 140}, alpha);
        ++i;
    }
}

/* Over the whole screen: fade in from black, a coloured flash, and a shake (the picture moved a few
 * pixels, edges filled with black). */
void apply_screen_effects(SDL_Surface *fb, int fade, int fade_total, int flash, Rgb flash_colour, int shake) {
    if (shake > 0) {
        SDL_Surface *copy = SDL_ConvertSurface(fb, fb->format, 0);
        if (copy) {
            const int dx = static_cast<int>(noise_hash(g_frame * 7u + 1u) % 5) - 2, dy = static_cast<int>(noise_hash(g_frame * 13u + 3u) % 5) - 2;
            const int amp = std::max(1, shake);                       /* eases off */
            fill_rect(fb, 0, 0, FB_W, FB_H, {0, 0, 0});
            SDL_Rect to = {dx * amp / 3, dy * amp / 3, FB_W, FB_H};
            SDL_BlitSurface(copy, NULL, fb, &to);
            SDL_FreeSurface(copy);
        }
    }
    if (flash > 0) fill_rect_alpha(fb, 0, 0, FB_W, FB_H, flash_colour, std::min(160, flash * 22));
    if (fade > 0) fill_rect_alpha(fb, 0, 0, FB_W, FB_H, Rgb{0, 0, 0}, 255 * fade / std::max(1, fade_total));
}

void draw_cell(SDL_Surface *target, const Assets &a, const Game &game, int chapter, TileType type, int cx, int cy,
               int px, int py, int beat) {
    switch (type) {
    case WALL:
        if (a.wall) blit(target, a.wall, px, py);
        break;
    case STONE:
        blit(target, a.block[chapter][game.style_at_cell(cx, cy) % BLOCK_STYLES], px, py);
        break;
    case PURSUER: {
        const int kind = game.style_at_cell(cx, cy) % PURSUER_KINDS;
        const bool face_left = cx > game.get_revenant().x;
        if (g_effects) {                 /* a lantern that wavers; it goes dark when they are sealed in */
            const unsigned w = noise_hash(static_cast<unsigned>(cx * 31 + cy * 57) + g_frame / 2u);
            draw_glow(target, a, px + (face_left ? 5 : TILE - 5), py + 9, 32, 84 + static_cast<int>(w % 56));
        }
        blit(target, a.pursuer[kind][((cx + cy + beat) & 1)][face_left ? 1 : 0], px, py);
        break;
    }
    case SEALED: blit(target, a.entombed, px, py); break;
    case REVENANT: {
        const int walk = game.get_steps() & 1;
        blit(target, a.revenant[revenant_row(game.get_facing())][walk], px, py);
        break;
    }
    case REVENANT_IN_GRAVE: blit(target, a.revenant_in_grave, px, py); break;
    case OFFERING: blit(target, a.offering, px, py); break;
    case MANTRAP: blit(target, a.mantrap, px, py); break;
    case GRAVE: blit(target, a.grave, px, py); break;
    case SKULL: blit(target, a.skull, px, py); break;
    default: break;
    }
}

/* The playable interior, drawn over the chapter's arena. The outer ring of
 * wall cells is not drawn: the arena artwork is the boundary. */
void draw_board(SDL_Surface *target, Assets &a, const Game &game, int wall_theme, int beat) {
    const int chapter = chapter_of(game.get_stage_index());
    ensure_chapter_assets(a, chapter, wall_theme);
    ensure_glow(a);
    blit(target, a.arena[chapter + 1], 0, 0);
    const int x0 = BOARD_X + TILE, y0 = BOARD_Y + TILE;
    fill_rect(target, x0 - 4, y0 - 4, (COLS - 2) * TILE + 8, (ROWS - 2) * TILE + 8, {8, 6, 12});
    frame_rect(target, x0 - 3, y0 - 3, (COLS - 2) * TILE + 6, (ROWS - 2) * TILE + 6, {96, 74, 44}, 1);
    blit(target, a.board_bg, x0, y0);
    const std::vector<TileType> &board = game.get_board();
    for (int y = 1; y < ROWS - 1; ++y)
        for (int x = 1; x < COLS - 1; ++x) {
            const TileType t = board[y * COLS + x];
            if (t != FLOOR) draw_cell(target, a, game, chapter, t, x, y, BOARD_X + x * TILE, BOARD_Y + y * TILE, beat);
        }
}

/* ------------------------------------------------------------ screens */
/* The title pictures. The first is always there; the others are earned. */
const char *const TITLE_NAME[3] = {"Abbey Ruins", "The Mausoleum", "Mausoleum II"};

bool title_unlocked(int index, int chapters_mask) {
    if (index == 0) return true;
    if (index == 1) return (chapters_mask & (1 << 2)) != 0;     /* the third chapter is settled */
    return (chapters_mask & (1 << 6)) != 0;                     /* the last grave is settled */
}

void render_title(SDL_Surface *target, Assets &a, int index, bool prompt, unsigned frame = 0) {
    index = std::max(0, std::min(2, index));
    if (!a.title[index]) a.title[index] = load_png(a.base + "/Title/Title" + std::to_string(index + 1) + ".png");
    fill_rect(target, 0, 0, FB_W, FB_H, {10, 8, 14});
    SDL_Surface *picture = a.title[index];
    const int h = picture ? picture->h : 270;
    const int y = std::max(8, (FB_H - h) / 2 - 22);
    if (picture) {
        frame_rect(target, 0, y - 3, FB_W, h + 6, C_EDGE, 1);
        blit(target, picture, 0, y);
        if (g_effects) {
            /* a living title: mist drifting in two layers, leaves coming down, braziers that waver */
            ensure_fog(a);
            ensure_glow(a);
            SDL_Rect area = {0, y, FB_W, h};
            SDL_SetClipRect(target, &area);
            if (a.fog) {
                for (int layer = 0; layer < 2; ++layer) {
                    const int width = a.fog->w;
                    const int shift = layer == 0 ? static_cast<int>((frame * 3u / 10u) % width) : width - static_cast<int>((frame * 3u / 20u) % width);
                    SDL_SetSurfaceAlphaMod(a.fog, layer == 0 ? 230 : 150);
                    for (int copy = -1; copy <= 1; ++copy) {
                        SDL_Rect to = {copy * width + shift - (layer == 0 ? 0 : width / 3), y + (layer == 0 ? h * 5 / 8 : h - 52), width, a.fog->h};
                        SDL_BlitSurface(a.fog, NULL, target, &to);
                    }
                }
            }
            for (int i = 0; i < 22; ++i) {
                const unsigned r = noise_hash(static_cast<unsigned>(i) * 977u + 11u);
                const double speed = 0.35 + (r & 31) / 70.0;
                const int lx = static_cast<int>(std::fmod((r >> 5) % 540 + frame * (0.12 + (r & 15) / 120.0) + 12.0 * std::sin(frame * 0.05 + i), 540.0)) - 30;
                const int ly = y + static_cast<int>(std::fmod(((r >> 12) % h) + frame * speed, static_cast<double>(h + 16))) - 8;
                const Rgb leaf = (r & 64) ? Rgb{198, 62, 36} : Rgb{222, 132, 40};
                fill_rect_alpha(target, lx, ly, 4, 3, leaf, 220);
            }
            if (index == 0) {                    /* the braziers by the steps in the first picture */
                static const int fire[3][2] = {{335, 179}, {363, 153}, {407, 195}};
                for (int i = 0; i < 3; ++i)
                    draw_glow(target, a, fire[i][0], y + fire[i][1], 44, 72 + static_cast<int>(noise_hash(frame / 2u + static_cast<unsigned>(i) * 41u) % 56));
            }
            SDL_SetClipRect(target, NULL);
        }
    }
    draw_text(target, a.small_font, "The Unquiet Inheritance", FB_W / 2, std::min(FB_H - 60, y + h + 30), C_BRASS);
    if (prompt) draw_text(target, a.small_font, "A: Begin", FB_W / 2, FB_H - 22, C_TEXT);
}

void render_main_menu(SDL_Surface *target, const Assets &a, int selected, bool can_continue) {
    draw_backdrop(target, a, 1);
    draw_panel(target, 44, 28, 392, 90);
    draw_title(target, a.large_font, "Grave Reckoning", FB_W / 2, 84, C_BRASS);
    draw_text(target, a.small_font, "The Unquiet Inheritance", FB_W / 2, 108, C_DIM);
    static const char *items[] = {"Continue", "Rise", "Book of the Interred", "Journal", "Options", "Revenant's Notes",
                                  "Return to the Earth"};
    const int first = can_continue ? 0 : 1, count = can_continue ? 7 : 6;
    const int step = can_continue ? 41 : 46, top = can_continue ? 160 : 168;
    for (int i = 0; i < count; ++i) draw_menu_item(target, a.medium_font, items[first + i], top + i * step, i == selected);
    draw_hint(target, a, "A: Select");
}

void render_play_menu(SDL_Surface *target, const Assets &a, int selected) {
    draw_backdrop(target, a, 2);
    draw_panel(target, 44, 36, 392, 84);
    draw_title(target, a.large_font, "Choose Your Reckoning", FB_W / 2, 84, C_BRASS);
    draw_text(target, a.small_font, "Thirty graves in every vigil. Earn each one.", FB_W / 2, 108, C_DIM);
    draw_menu_item(target, a.medium_font, "Easy: The Mourners", 190, selected == 0);
    draw_menu_item(target, a.medium_font, "Normal: The Watch", 252, selected == 1);
    draw_menu_item(target, a.medium_font, "Hard: The Faithful", 314, selected == 2);
    draw_hint(target, a, "A: Begin    B: Retreat");
}

const int GRID_X = 208, GRID_Y = 112, CELL_W = 44, CELL_H = 38, CELL_GAP_X = 5, ROW_STEP = 50;

void draw_stage_grid(SDL_Surface *target, const Assets &a, Difficulty difficulty, int selected,
                     const Progress *progress, const HighScores *scores, const Options *options = NULL) {
    draw_panel(target, 14, 96, 452, 322);
    for (int c = 0; c < CHAPTER_COUNT; ++c) {
        const int y = GRID_Y + c * ROW_STEP;
        draw_text_at(target, a.small_font, ROMAN[c], 26, y + 24, C_BRASS);
        draw_text_at(target, a.small_font, CHAPTER_SHORT[c], 54, y + 24, C_TEXT);
        for (int s = 0; s < STAGES_PER_CHAPTER; ++s) {
            const int stage = c * STAGES_PER_CHAPTER + s;
            const int x = GRID_X + s * (CELL_W + CELL_GAP_X);
            const bool available = !progress || progress->is_unlocked(difficulty, stage);
            const bool highlighted = stage == selected;
            fill_rect(target, x - 2, y - 2, CELL_W + 4, CELL_H + 4, {8, 6, 12});
            audit_box(x, y, CELL_W, CELL_H);
            fill_rect(target, x, y, CELL_W, CELL_H, highlighted ? C_SELECT : (available ? C_PANEL_HI : C_LOCKED));
            frame_rect(target, x, y, CELL_W, CELL_H, highlighted ? C_BRASS : (available ? C_EDGE : Rgb{56, 46, 70}),
                       highlighted ? 3 : 1);
            std::string label;
            if (scores) {
                const int best = scores->get(difficulty, stage);
                label = best > 0 ? std::to_string(best) : "-";
            } else {
                label = available ? std::to_string(stage + 1) : "-";
            }
            draw_text(target, a.small_font, label, x + CELL_W / 2, y + 25,
                      highlighted ? C_SELECT_TEXT : (available ? C_TEXT : C_DIM));
            if (options) {
                /* a brass dot: settled. A blue flame: settled without losing a life. */
                const int di = difficulty_index(difficulty);
                bool settled = false;
                if (progress) settled = stage < progress->unlocked[di] - 1 || (stage == LEVEL_COUNT - 1 && (options->vigils & (1 << di)));
                else if (scores) settled = scores->get(difficulty, stage) > 0;
                const bool untouched = settled && (options->untouched[di] & (1 << stage)) != 0;
                if (settled) fill_rect(target, x + 5, y + 5, 5, 5, highlighted ? C_SELECT_TEXT : C_BRASS);
                if (untouched && a.offering) blit_scaled(target, a.offering, x + CELL_W - 17, y + 2, 14, 14);
            }
        }
    }
}

void render_stage_select(SDL_Surface *target, const Assets &a, const Progress &progress, Difficulty difficulty,
                         int selected, const Options &options) {
    draw_backdrop(target, a, chapter_of(selected));
    draw_title(target, a.large_font, difficulty_label(difficulty), FB_W / 2, 50, C_BRASS);
    draw_text(target, a.small_font, "Locked graves stay sealed until the one before is settled.", FB_W / 2, 80, C_TEXT);
    draw_stage_grid(target, a, difficulty, selected, &progress, NULL, &options);
    draw_text(target, a.small_font, "Brass: settled.   Blue flame: settled without losing a life.", FB_W / 2, 436, C_DIM);
    draw_hint(target, a, "A: Play    D-pad: Choose    B: Back");
}

void render_scores(SDL_Surface *target, const Assets &a, const HighScores &scores, Difficulty difficulty,
                   bool reset_selected, const Options &options) {
    draw_backdrop(target, a, 2);
    draw_title(target, a.large_font, "Book of the Interred", FB_W / 2, 50, C_BRASS);
    draw_text(target, a.small_font, difficulty_label(difficulty), FB_W / 2, 80, C_TEXT);
    draw_stage_grid(target, a, difficulty, -1, NULL, &scores, &options);
    draw_menu_item(target, a.small_font, "Burn the Evidence", 454, reset_selected, 140, 200);
    draw_text(target, a.small_font, "L1/R1: Vigil", 76, 446, C_TEXT);
}

/* A strip of real tiles, enlarged, so a wall colour can be judged beside the
 * stone blocks and the revenant before leaving the menu. */
void draw_wall_preview(SDL_Surface *target, Assets &a, int chapter, int y) {
    static const char kinds[] = "WWWWWBRBWWWW";
    const int size = 28;
    const int left = (FB_W - 12 * size) / 2;
    for (int i = 0; i < 12; ++i) {
        const int x = left + i * size;
        SDL_Surface *s = kinds[i] == 'W' ? a.wall : kinds[i] == 'B' ? a.block[chapter][i % BLOCK_STYLES] : a.revenant[0][0];
        if (kinds[i] != 'W') fill_rect(target, x, y, size, size, {34, 38, 52});
        if (s) blit_scaled(target, s, x, y, size, size);
    }
}

const int OPTION_TAB_COUNT = 3;
const char *const OPTION_TAB_NAME[OPTION_TAB_COUNT] = {"Sound", "Look", "Story"};
/* How many rows each tab has. */
int option_rows(int tab) { return tab == 0 ? 4 : (tab == 1 ? 3 : 3); }

const char *story_mode_name(int mode) {
    return mode == 0 ? "Every chapter" : (mode == 1 ? "First time only" : "Off");
}

/* A row is picked with up/down, started with A, changed with left/right and
 * confirmed with A (B cancels). Two-way switches (mute, speaker mix) and
 * actions need only A. */
void draw_option_row(SDL_Surface *target, const Assets &a, const std::string &label, int y, bool selected,
                     bool editing, bool dim = false) {
    if (editing) {
        const int x = 70, w = 340;
        fill_rect(target, x - 3, y - 31, w + 6, 44, {8, 6, 12});
        fill_rect(target, x, y - 28, w, 38, {236, 196, 110});
        frame_rect(target, x, y - 28, w, 38, {255, 240, 190}, 3);
        audit_box(x + 3, y - 25, w - 6, 32);
        draw_text(target, a.medium_font, ellipsize(a.medium_font, "<  " + label + "  >", w - 20), x + w / 2, y, C_SELECT_TEXT);
    } else if (dim) {
        const int x = 70, w = 340;
        fill_rect(target, x - 3, y - 31, w + 6, 44, {8, 6, 12});
        fill_rect(target, x, y - 28, w, 38, {30, 25, 38});
        frame_rect(target, x, y - 28, w, 38, {70, 58, 86}, 1);
        audit_box(x + 3, y - 25, w - 6, 32);
        draw_text(target, a.medium_font, ellipsize(a.medium_font, label, w - 20), x + w / 2, y, {120, 108, 130});
    } else {
        draw_menu_item(target, a.medium_font, label, y, selected);
    }
}

void render_options(SDL_Surface *target, Assets &a, const Options &options, int tab, int selected, bool editing,
                    int chapter, bool has_music, bool prologue_seen, int chapters_mask) {
    draw_backdrop(target, a, 3);
    ensure_chapter_assets(a, chapter, options.wall_theme);
    draw_title(target, a.large_font, "Options", FB_W / 2, 58, C_BRASS);
    for (int t = 0; t < OPTION_TAB_COUNT; ++t) {
        const int w = 120, x = (FB_W - (OPTION_TAB_COUNT * w + (OPTION_TAB_COUNT - 1) * 8)) / 2 + t * (w + 8);
        const bool on = t == tab;
        fill_rect(target, x - 2, 80, w + 4, 38, {8, 6, 12});
        audit_box(x, 82, w, 34);
        fill_rect(target, x, 82, w, 34, on ? C_SELECT : C_PANEL_HI);
        frame_rect(target, x, 82, w, 34, on ? C_BRASS : C_EDGE, on ? 3 : 1);
        draw_text(target, a.medium_font, OPTION_TAB_NAME[t], x + w / 2, 108, on ? C_SELECT_TEXT : C_TEXT);
    }
    draw_panel(target, 36, 134, 408, 270);
    if (tab == 0) {
        draw_option_row(target, a, has_music ? "Music Volume: " + std::to_string(options.volume) + "%" : std::string("Music Volume: (none yet)"),
                        170, selected == 0, editing && selected == 0, !has_music);
        draw_option_row(target, a, !has_music ? std::string("Music: none installed") : (options.muted ? "Music: Silent" : "Music: Mournful"),
                        216, selected == 1, false, !has_music);
        draw_option_row(target, a, std::string("Speaker Mix: ") + (options.speaker_mix ? "Stereo" : "Blended"), 262, selected == 2,
                        false, !has_music);
        draw_option_row(target, a, std::string("Sound Effects: ") + (options.sfx ? "On" : "Off"), 308, selected == 3, false, !has_music);
        draw_text(target, a.small_font,
                  "The speakers lack a left channel: the mix is blended.", FB_W / 2, 358, C_DIM);
        draw_text(target, a.small_font, selected == 3 ? "Stones, bells and lanterns. Music is separate." : "Headphones always play stereo.",
                  FB_W / 2, 380, C_DIM);
    } else if (tab == 1) {
        draw_option_row(target, a, std::string("Walls: ") + wall_option_name(options.wall_theme), 170, selected == 0,
                        editing && selected == 0);
        fill_rect(target, 96, 190, 288, 36, {8, 6, 12});
        draw_wall_preview(target, a, chapter, 194);
        draw_option_row(target, a, std::string("Screen Effects: ") + (options.effects ? "On" : "Off"), 262, selected == 1, false);
        draw_option_row(target, a, std::string("Hints: ") + (options.hints ? "On" : "Off"), 308, selected == 2, false);
        draw_text(target, a.small_font, selected == 1 ? "Fades, flashes, shake, dust and flickering lanterns."
                                                       : (selected == 2 ? "One-line reminders in the very first grave." : "Each chapter has its own masonry by default."),
                  FB_W / 2, 358, C_DIM);
    } else {
        draw_option_row(target, a, std::string("Story Pages: ") + story_mode_name(options.story_mode), 176, selected == 0,
                        editing && selected == 0);
        draw_option_row(target, a, std::string("Title: ") + TITLE_NAME[options.title_screen], 230, selected == 1,
                        editing && selected == 1);
        draw_option_row(target, a, prologue_seen ? "Replay the Prologue" : "Replay the Prologue (unseen)", 284, selected == 2,
                        false, !prologue_seen);
        std::string note1 = "The comic pages shown as a chapter begins.";
        std::string note2 = prologue_seen ? "START skips a page at any time." : "The prologue unlocks once you have played it.";
        if (selected == 1 && !title_unlocked(1, chapters_mask)) note2 = "A second title picture unlocks after chapter three.";
        else if (selected == 1 && !title_unlocked(2, chapters_mask)) note2 = "A third unlocks when the last grave is settled.";
        draw_text(target, a.small_font, note1, FB_W / 2, 336, C_DIM);
        draw_text(target, a.small_font, note2, FB_W / 2, 356, C_DIM);
    }
    draw_hint(target, a, editing ? "LEFT/RIGHT: Change    A: Confirm    B: Cancel"
                                 : "UP/DOWN: Row    A: Select    L1/R1: Tab    B: Back", 452);
}

void render_help(SDL_Surface *target, const Assets &a) {
    draw_backdrop(target, a, 4);
    draw_panel(target, 24, 22, 432, 420);
    draw_title(target, a.large_font, "Revenant's Notes", FB_W / 2, 68, C_BRASS);
    static const char *lines[] = {
        "You have risen. They have not forgotten you.",
        "Shove the stones. Seal them in. Settle the account.",
        "Seal every last one and the measure is complete:",
        "their lanterns go dark and a flame is left behind.",
        "Gaps matter: a diagonal one is an open door.",
        "The living are careless. Some will walk into",
        "an open grave. That settles nothing: no flame.",
        "Mantraps and rolling skulls kill. Mind your pushing.",
        "Settle one graveyard to unlock the next.",
        "Mourners tire. The Watch is steady. The Faithful look down."};
    for (int i = 0; i < 10; ++i) draw_text(target, a.small_font, lines[i], FB_W / 2, 108 + i * 30, C_TEXT);
    draw_hint(target, a, "B: Back");
}

/* One of these is picked each time the exit prompt opens. */
const char *const EXIT_QUOTES[][2] = {
    {"The grave is patient. So are they.", "Lie down again?"},
    {"Unfinished business does not bury itself.", "Walk away regardless?"},
    {"The living will sleep soundly tonight.", "Is that what you want?"},
    {"Rest is for the settled.", "You are not settled."},
    {"The earth will take you back. It always does.", "Shall we go quietly?"},
    {"Every lantern out there is still lit.", "Leave them to it?"},
    {"Even the dead get tired of waiting.", "Retire to the dark?"},
    {"The account stays open. It always does.", "Close the lid for now?"}};
const int EXIT_QUOTE_COUNT = sizeof(EXIT_QUOTES) / sizeof(EXIT_QUOTES[0]);

void render_exit_confirm(SDL_Surface *target, const Assets &a, int quote) {
    draw_backdrop(target, a, 5);
    draw_panel(target, 38, 108, 404, 236);
    draw_title(target, a.large_font, "Return to the Earth?", FB_W / 2, 160, C_BRASS);
    draw_text(target, a.small_font, EXIT_QUOTES[quote][0], FB_W / 2, 204, C_TEXT);
    draw_text(target, a.small_font, EXIT_QUOTES[quote][1], FB_W / 2, 234, C_TEXT);
    draw_menu_item(target, a.medium_font, "Yes, Lie Down", 300, true);
    draw_hint(target, a, "A: Yes    B: No");
}

void render_chapter_card(SDL_Surface *target, const Assets &a, int chapter) {
    SDL_Surface *s = a.arena_art_bright[chapter + 1];
    if (s) blit(target, s, 0, 0);
    else fill_rect(target, 0, 0, FB_W, FB_H, C_PANEL);
    draw_panel(target, 30, 300, 420, 130);
    draw_text(target, a.small_font, std::string("CHAPTER ") + ROMAN[chapter], FB_W / 2, 326, C_DIM);
    draw_title(target, a.large_font, CHAPTER_NAME[chapter], FB_W / 2, 364, C_BRASS);
    draw_text(target, a.small_font, CHAPTER_STORY[chapter][0], FB_W / 2, 392, C_TEXT);
    draw_text(target, a.small_font, CHAPTER_STORY[chapter][1], FB_W / 2, 412, C_TEXT);
    draw_hint(target, a, "A: Continue    B: Back");
}

void render_level_complete(SDL_Surface *target, const Assets &a, const Game &game, bool proceed_selected,
                           int journal_news) {
    draw_panel(target, 38, 150, 404, 214);
    draw_title(target, a.large_font, "Account Settled", FB_W / 2, 200, C_BRASS);
    draw_text(target, a.small_font, "Sealed in stone. The offerings are yours.", FB_W / 2, 230, C_TEXT);
    draw_text(target, a.small_font, "Entered in the book.   Grave score: " + std::to_string(game.get_stage_score()), FB_W / 2, 254, C_DIM);
    /* bit 0: a page for the casebook; bit 1: an older page has come loose */
    if (journal_news == 3) draw_text(target, a.small_font, "New pages have been added to the Journal.", FB_W / 2, 276, C_BRASS);
    else if (journal_news & 1) draw_text(target, a.small_font, "A page has been added to the Journal.", FB_W / 2, 276, C_BRASS);
    else if (journal_news & 2) draw_text(target, a.small_font, "An older page has come loose in the Journal.", FB_W / 2, 276, C_BRASS);
    draw_menu_item(target, a.small_font, "Onward", 306, proceed_selected, 90, 300);
    draw_menu_item(target, a.small_font, "Return to the Earth", 348, !proceed_selected, 90, 300);
}

/* The pause menu carries the settings that matter mid-grave. */
void render_pause_menu(SDL_Surface *target, const Assets &a, const Options &options, int selected, bool has_music) {
    draw_panel(target, 38, 70, 404, 340);
    draw_title(target, a.large_font, "Between Graves", FB_W / 2, 118, C_BRASS);
    draw_menu_item(target, a.medium_font, "Resume the Vigil", 168, selected == 0);
    draw_menu_item(target, a.medium_font,
                   has_music ? "Music Volume: " + std::to_string(options.volume) + "%" : std::string("Music Volume: (none yet)"),
                   214, selected == 1);
    draw_menu_item(target, a.medium_font,
                   !has_music ? std::string("Music: none installed") : (options.muted ? "Music: Silent" : "Music: Mournful"),
                   260, selected == 2);
    draw_menu_item(target, a.medium_font, "Begin the Vigil Anew", 306, selected == 3);
    draw_menu_item(target, a.medium_font, "Return to the Earth", 352, selected == 4);
    draw_text(target, a.small_font, "A/START: Select    LEFT/RIGHT: Volume    B: Resume", FB_W / 2, 394, C_DIM);
}

void draw_banner(SDL_Surface *target, int x, int y, int w, int h) { draw_panel(target, x, y, w, h); }

void render_frame(SDL_Surface *target, Assets &a, const Game &game, int wall_theme, int beat) {
    draw_board(target, a, game, wall_theme, beat);

    /* HUD along the bottom: returns, grave and wave, score. */
    audit_box(0, 420, FB_W, 60);
    fill_rect(target, 0, 420, FB_W, 60, {12, 9, 18});
    fill_rect(target, 0, 420, FB_W, 2, C_EDGE);
    for (int i = 0; i < game.get_lives() && i < 7; ++i) blit(target, a.revenant[0][0], 12 + i * 22, 430);
    draw_text_at(target, a.small_font, difficulty_name(game.get_difficulty()), 12, 472, C_DIM);
    const int chapter = chapter_of(game.get_stage_index());
    draw_text(target, a.small_font, CHAPTER_NAME[chapter], 300, 444, C_BRASS);
    draw_text(target, a.small_font,
              "Grave " + std::to_string(game.get_level()) + "   Wave " + std::to_string(game.get_wave()) + "/" +
                  std::to_string(game.get_wave_total()),
              300, 466, C_TEXT);
    draw_text_right(target, a.number_font, std::to_string(game.get_score()), 468, 456, C_TEXT);

    const GameState state = game.get_state();
    const int cx = FB_W / 2, cy = BOARD_Y + (ROWS * TILE) / 2;
    if (state == STATE_NEW_GAME || state == STATE_PAUSED || state == STATE_GAME_OVER || state == STATE_WON) {
        std::string large, small;
        if (state == STATE_NEW_GAME) { large = "The Vigil Begins"; small = "A/START: Rise"; }
        else if (state == STATE_PAUSED) { large = "Between Graves"; small = "START: Menu"; }
        else if (state == STATE_GAME_OVER) { large = "Buried Again"; small = "A/START: Rise Again"; }
        else { large = "The Account Is Settled"; small = "A/START: Return"; }
        draw_banner(target, 52, cy - 70, 376, 128);
        draw_title(target, a.large_font, large, cx, cy - 22, C_BRASS);
        draw_text(target, a.medium_font, small, cx, cy + 32, C_TEXT);
    } else if (state == STATE_SETUP) {
        draw_banner(target, 130, cy - 54, 220, 62);
        draw_title(target, a.large_font, "Grave " + std::to_string(game.get_level()), cx, cy - 12, C_BRASS);
    } else if (state == STATE_RUNNING && game.is_walled_in()) {
        const int top = cy - 88;
        draw_banner(target, 52, top, 376, 170);
        draw_title(target, a.large_font, "The Way Is Shut", cx, top + 44, C_BRASS);
        draw_text(target, a.small_font, "No way forward. Not even a whisper.", cx, top + 74, C_TEXT);
        draw_text(target, a.medium_font, "A: Begin Anew", cx, top + 112, C_TEXT);
        draw_text(target, a.medium_font, "B: Return to the Earth", cx, top + 142, C_TEXT);
    } else if (state == STATE_DEAD) {
        const int phase = game.get_death_phase();
        const Point m = game.get_revenant();
        if (phase >= 0 && phase < static_cast<int>(a.death.size()))
            blit(target, a.death[phase], BOARD_X + m.x * TILE, BOARD_Y + m.y * TILE);
    }
}

/* ------------------------------------------------------------ story pages */
/* Still comic panels used as cut-scenes: no animation, no sound. The pages
 * are described in Story/story.txt (see the header comment there), so the
 * story and the panel art can be changed without touching the code. */
const Rgb C_PAPER = {240, 233, 214};
const Rgb C_INK = {26, 20, 34};

struct StoryPanel {
    std::string image;
    std::string caption;
    std::string say;
    SDL_Surface *surface;
    bool tried;
    SDL_Rect beat;                 /* comic pages: the strip of the page this beat reveals */
    std::string speaker;           /* comic pages: who speaks the "say" line (shown on its plate) */
    bool plate_right;              /* comic pages: which side of the strip the plate sits */
    StoryPanel() : image(), caption(), say(), surface(NULL), tried(false), beat(), speaker(), plate_right(true) {}
};

struct StoryPage {
    std::string name;
    std::string layout;
    std::string art;               /* comic pages: the whole page, one PNG */
    SDL_Surface *art_surface;
    bool art_tried;
    std::vector<StoryPanel> panels;
    StoryPage() : name(), layout(), art(), art_surface(NULL), art_tried(false), panels() {}
};

struct Story {
    std::vector<StoryPage> pages;
    std::string dir;

    const StoryPage *find(const std::string &name) const {
        for (size_t i = 0; i < pages.size(); ++i)
            if (pages[i].name == name) return &pages[i];
        return NULL;
    }
};

std::string trim(const std::string &s) {
    const std::string::size_type a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

void load_story(Story &story, const std::string &base) {
    story.dir = base + "/Story";
    std::ifstream in((story.dir + "/story.txt").c_str());
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::string::size_type space = line.find(' ');
        const std::string word = space == std::string::npos ? line : line.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : trim(line.substr(space + 1));
        if (word == "page") {
            StoryPage page;
            const std::string::size_type gap = rest.find(' ');
            page.name = gap == std::string::npos ? rest : rest.substr(0, gap);
            page.layout = gap == std::string::npos ? "1" : trim(rest.substr(gap + 1));
            story.pages.push_back(page);
        } else if (story.pages.empty()) {
            continue;
        } else if (word == "panel") {
            StoryPanel panel;
            panel.image = rest;
            story.pages.back().panels.push_back(panel);
        } else if (word == "art") {
            story.pages.back().art = rest;
        } else if (word == "beat") {
            StoryPanel panel;
            int x = 0, y = 0, w = 0, h = 0;
            if (std::sscanf(rest.c_str(), "%d %d %d %d", &x, &y, &w, &h) == 4) {
                panel.beat = SDL_Rect{x, y, w, h};
                story.pages.back().panels.push_back(panel);
            }
        } else if (word == "speaker" && !story.pages.back().panels.empty()) {
            story.pages.back().panels.back().speaker = rest;
        } else if (word == "at" && !story.pages.back().panels.empty()) {
            story.pages.back().panels.back().plate_right = rest != "left";
        } else if (!story.pages.back().panels.empty()) {
            StoryPanel &panel = story.pages.back().panels.back();
            std::string &target = word == "say" ? panel.say : panel.caption;
            if (word == "say" || word == "caption") target += (target.empty() ? "" : " ") + rest;
        }
    }
}

/* Where each panel of a page goes. The page area is 456 x 432 with 8 px gutters. */
std::vector<SDL_Rect> story_rects(const std::string &layout, size_t count) {
    const int x = 12, y = 12, w = 456, h = 432, gap = 8;
    const int half_w = (w - gap) / 2, half_h = (h - gap) / 2;
    std::vector<SDL_Rect> r;
    if (layout == "2") {
        r.push_back(SDL_Rect{x, y, w, half_h});
        r.push_back(SDL_Rect{x, y + half_h + gap, w, half_h});
    } else if (layout == "2s") {
        r.push_back(SDL_Rect{x, y, half_w, h});
        r.push_back(SDL_Rect{x + half_w + gap, y, half_w, h});
    } else if (layout == "3") {
        r.push_back(SDL_Rect{x, y, w, half_h});
        r.push_back(SDL_Rect{x, y + half_h + gap, half_w, half_h});
        r.push_back(SDL_Rect{x + half_w + gap, y + half_h + gap, half_w, half_h});
    } else if (layout == "4") {
        for (int i = 0; i < 4; ++i)
            r.push_back(SDL_Rect{x + (i % 2) * (half_w + gap), y + (i / 2) * (half_h + gap), half_w, half_h});
    } else {
        r.push_back(SDL_Rect{x, y, w, h});
    }
    if (r.size() > count && count > 0) r.resize(count);
    return r;
}

std::vector<std::string> wrap_text(TTF_Font *font, const std::string &text, int max_width) {
    std::vector<std::string> lines;
    std::string line;
    std::string::size_type pos = 0;
    while (pos <= text.size()) {
        std::string::size_type end = text.find(' ', pos);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(pos, end - pos);
        pos = end + 1;
        if (word.empty()) continue;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && text_width(font, trial) > max_width) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

/* Scales the picture to cover the rectangle, cropping the overflow. */
void blit_cover(SDL_Surface *target, SDL_Surface *src, SDL_Rect dst) {
    if (!src) return;
    SDL_Rect from = {0, 0, src->w, src->h};
    if (src->w * dst.h > src->h * dst.w) {          /* source is wider: crop the sides */
        from.w = src->h * dst.w / dst.h;
        from.x = (src->w - from.w) / 2;
    } else {                                        /* source is taller: crop top and bottom */
        from.h = src->w * dst.h / dst.w;
        from.y = (src->h - from.h) / 2;
    }
    SDL_BlitScaled(src, &from, target, &dst);
}

void draw_caption_box(SDL_Surface *target, TTF_Font *font, const std::string &text, SDL_Rect panel) {
    const std::vector<std::string> lines = wrap_text(font, text, panel.w - 34);
    if (lines.empty()) return;
    int w = 0;
    for (size_t i = 0; i < lines.size(); ++i) w = std::max(w, text_width(font, lines[i]));
    w += 14;
    const int h = static_cast<int>(lines.size()) * 17 + 10;
    const int x = panel.x + 7, y = panel.y + 7;
    audit_box(x, y, w, h);
    audit_inside("caption", x, y, w, h, panel);
    fill_rect(target, x - 2, y - 2, w + 4, h + 4, C_INK);
    fill_rect(target, x, y, w, h, {246, 240, 224});
    for (size_t i = 0; i < lines.size(); ++i)
        draw_text_at(target, font, lines[i], x + 7, y + 15 + static_cast<int>(i) * 17, C_INK);
}

void draw_balloon(SDL_Surface *target, TTF_Font *font, const std::string &text, SDL_Rect panel) {
    const std::vector<std::string> lines = wrap_text(font, text, panel.w * 3 / 5);
    if (lines.empty()) return;
    int w = 0;
    for (size_t i = 0; i < lines.size(); ++i) w = std::max(w, text_width(font, lines[i]));
    w += 24;
    const int h = static_cast<int>(lines.size()) * 17 + 16;
    const int x = panel.x + panel.w - w - 12;
    const int y = panel.y + panel.h - h - 26;
    audit_box(x, y, w, h);
    audit_inside("balloon", x, y, w + 4, h + 14 + 4, panel);
    /* ink outline, then the white fill, each with its corners cut for a round look */
    fill_rect(target, x - 2, y + 2, w + 4, h - 4, C_INK);
    fill_rect(target, x + 2, y - 2, w - 4, h + 4, C_INK);
    fill_rect(target, x - 1, y, w + 2, h, C_INK);
    fill_rect(target, x, y + 2, w, h - 4, {250, 246, 236});
    fill_rect(target, x + 2, y, w - 4, h, {250, 246, 236});
    const int tail_x = x + w / 4;
    for (int row = 0; row < 14; ++row) {
        const int span = 14 - row;
        fill_rect(target, tail_x - row / 2 - 2, y + h + row, span + 4, 1, C_INK);
    }
    for (int row = 0; row < 12; ++row) {
        const int span = 12 - row;
        fill_rect(target, tail_x - row / 2, y + h - 1 + row, span, 1, {250, 246, 236});
    }
    for (size_t i = 0; i < lines.size(); ++i)
        draw_text_at(target, font, lines[i], x + 12, y + 17 + static_cast<int>(i) * 17, C_INK);
}

/* A page of plain lettering on parchment, like the last page of a storybook:
 * each panel of the page is one paragraph, revealed in turn. A paragraph that
 * starts with "# " is set in the large title size. */
void render_parchment(SDL_Surface *target, Assets &a, StoryPage &page, int revealed, bool last_page) {
    fill_rect(target, 0, 0, FB_W, FB_H, {226, 206, 164});
    frame_rect(target, 14, 14, FB_W - 28, FB_H - 28, C_INK, 3);
    frame_rect(target, 22, 22, FB_W - 44, FB_H - 44, {120, 88, 52}, 1);
    audit_box(26, 26, FB_W - 52, FB_H - 52);
    int y = 96;
    for (size_t i = 0; i < page.panels.size() && static_cast<int>(i) < revealed; ++i) {
        std::string text = page.panels[i].caption;
        const bool title = text.size() > 2 && text[0] == '#' && text[1] == ' ';
        if (title) text = text.substr(2);
        TTF_Font *font = title ? a.large_font : a.medium_font;
        const std::vector<std::string> lines = wrap_text(font, text, 380);
        const int step = title ? 40 : 28;
        if (title) y += 22;
        for (size_t k = 0; k < lines.size(); ++k) {
            draw_text(target, font, lines[k], FB_W / 2, y, title ? Rgb{96, 30, 40} : C_INK);
            y += step;
        }
        y += title ? 20 : 16;
    }
    const bool more = revealed < static_cast<int>(page.panels.size());
    draw_text(target, a.small_font, more ? "A: Continue" : (last_page ? "A: Continue" : "A: Next page"), FB_W / 2, 452, C_INK);
}

/* A whole comic page (text-free art, see tools/slice_comic.py) revealed a strip of panels at a
 * time. The words go in a band under the page, never over the art: the latest caption and the
 * latest spoken line seen so far. */
void render_comic(SDL_Surface *target, Assets &a, const Story &story, StoryPage &page, int revealed,
                  bool last_page, int age) {
    fill_rect(target, 0, 0, FB_W, FB_H, C_PAPER);
    if (!page.art_tried) {
        page.art_tried = true;
        page.art_surface = load_png(story.dir + "/" + page.art);
    }
    const int px = 12, py = 12;
    const int page_w = 456;
    const int page_h = page.art_surface ? page.art_surface->h : 300;
    fill_rect(target, px - 3, py - 3, page_w + 6, page_h + 6, C_INK);
    fill_rect(target, px, py, page_w, page_h, {236, 220, 178});      /* the unrevealed paper */
    std::string caption;
    const int shown = std::min(revealed, static_cast<int>(page.panels.size()));
    for (int i = 0; i < shown; ++i) {
        const StoryPanel &panel = page.panels[i];
        if (page.art_surface) {
            SDL_Rect from = panel.beat;
            /* the newest strip is drawn across from the left over a few frames */
            if (g_effects && i == shown - 1 && age < 7) from.w = std::max(1, from.w * (age + 1) / 7);
            SDL_Rect to = {px + from.x, py + from.y, from.w, from.h};
            SDL_BlitSurface(page.art_surface, &from, target, &to);
        }
        if (!panel.caption.empty()) caption = panel.caption;
    }
    if (shown > 0) {                                                  /* mark the newest strip */
        const SDL_Rect b = page.panels[shown - 1].beat;
        const int bw = (g_effects && age < 7) ? std::max(2, b.w * (age + 1) / 7) : b.w;
        frame_rect(target, px + b.x, py + b.y, bw, b.h, C_BRASS, 2);
    }
    if (shown > 0 && !page.panels[shown - 1].say.empty() && !(g_effects && age < 7)) {
        /* A line spoken by one of the living, aimed at him: a dark plate, slightly see-through,
         * at the foot of the newest strip on the speaker's side. */
        const StoryPanel &panel = page.panels[shown - 1];
        const std::vector<std::string> lines = wrap_text(a.small_font, "\"" + panel.say + "\"", 250);
        int text_w = panel.speaker.empty() ? 0 : text_width(a.tiny_font, panel.speaker);
        for (size_t k = 0; k < lines.size(); ++k) text_w = std::max(text_w, text_width(a.small_font, lines[k]));
        const int w = text_w + 26;
        const int h = (panel.speaker.empty() ? 0 : 16) + static_cast<int>(lines.size()) * 18 + 14;
        const SDL_Rect strip = {px + panel.beat.x, py + panel.beat.y, panel.beat.w, panel.beat.h};
        const int x = panel.plate_right ? strip.x + strip.w - w - 8 : strip.x + 8;
        const int y = std::max(py + 4, strip.y + strip.h - h - 6);
        const SDL_Rect plate = {x, y, w, h};
        audit_box(x, y, w, h);
        fill_rect_alpha(target, x, y, w, h, C_PANEL, 205);
        frame_rect(target, x, y, w, h, C_EDGE, 1);
        int ty = y + 15;
        if (!panel.speaker.empty()) {
            audit_inside("plate speaker", x + 13, ty - 10, text_width(a.tiny_font, panel.speaker), 12, plate);
            draw_text_at(target, a.tiny_font, panel.speaker, x + 13, ty, C_BRASS);
            ty += 18;
        }
        for (size_t k = 0; k < lines.size(); ++k, ty += 18) {
            audit_inside("plate line", x + 13, ty - 12, text_width(a.small_font, lines[k]), 16, plate);
            draw_text_at(target, a.small_font, lines[k], x + 13, ty, C_TEXT);
        }
    }
    const int band_y = py + page_h + 12;
    const int band_h = 458 - band_y;
    const SDL_Rect band = {px, band_y, page_w, band_h};
    audit_box(band.x, band.y, band.w, band.h);
    int y = band_y + 14;
    if (!caption.empty()) {
        const std::vector<std::string> lines = wrap_text(a.small_font, caption, page_w - 20);
        for (size_t k = 0; k < lines.size(); ++k, y += 18) {
            audit_inside("comic caption", px + 8, y - 12, text_width(a.small_font, lines[k]), 16, band);
            draw_text_at(target, a.small_font, lines[k], px + 8, y, C_INK);
        }
        y += 4;
    }
    const bool more = revealed < static_cast<int>(page.panels.size());
    draw_text(target, a.small_font,
              more ? "A: Next panel    START: Skip" : (last_page ? "A: Continue    START: Skip" : "A: Next page    START: Skip"),
              FB_W / 2, 472, C_INK);
}

void render_story(SDL_Surface *target, Assets &a, const Story &story, StoryPage &page, int revealed,
                  bool last_page, int age = 99) {
    if (page.layout == "parchment") { render_parchment(target, a, page, revealed, last_page); return; }
    if (page.layout == "comic") { render_comic(target, a, story, page, revealed, last_page, age); return; }
    fill_rect(target, 0, 0, FB_W, FB_H, C_PAPER);
    const std::vector<SDL_Rect> rects = story_rects(page.layout, page.panels.size());
    for (size_t i = 0; i < rects.size() && i < page.panels.size() && static_cast<int>(i) < revealed; ++i) {
        StoryPanel &panel = page.panels[i];
        if (!panel.tried) {
            panel.tried = true;
            panel.surface = load_png(story.dir + "/" + panel.image);
        }
        const SDL_Rect r = rects[i];
        fill_rect(target, r.x - 3, r.y - 3, r.w + 6, r.h + 6, C_INK);
        if (panel.surface) blit_cover(target, panel.surface, r);
        else fill_rect(target, r.x, r.y, r.w, r.h, {58, 50, 74});
    }
    for (size_t i = 0; i < rects.size() && i < page.panels.size() && static_cast<int>(i) < revealed; ++i) {
        draw_caption_box(target, a.small_font, page.panels[i].caption, rects[i]);
        draw_balloon(target, a.small_font, page.panels[i].say, rects[i]);
    }
    const bool more = revealed < static_cast<int>(page.panels.size());
    draw_text(target, a.small_font,
              more ? "A: Next panel    START: Skip" : (last_page ? "A: Continue    START: Skip" : "A: Next page    START: Skip"),
              FB_W / 2, 468, C_INK);
}

/* ------------------------------------------------------------ journal */
/* The Journal is the casebook of an unnamed investigator who is following the
 * story. The printed pages (clippings, letters, ledgers) are what they
 * collected; the scrawl pages, the marks in every margin and the Count are
 * their own hand, which frays as the player settles graves. Everything is
 * described in Story/journal.txt, in story order. */
struct JournalEntry {
    std::string id, style, title, source, unlock, hint;
    int book;       /* 0 = the investigator's casebook, 1 = the earlier hands bound into it */
    int era;        /* 0, or the year an earlier hand wrote in: sets the paper and the ink */
    std::vector<std::string> text;
    std::vector<std::pair<std::string, std::string> > margins;   /* (hand, words) other keepers wrote beside the page */
    JournalEntry() : id(), style(), title(), source(), unlock(), hint(), book(0), era(0), text(), margins() {}
};

struct Journal {
    std::vector<JournalEntry> entries;
};

void load_journal(Journal &journal, const std::string &base) {
    std::ifstream in((base + "/Story/journal.txt").c_str());
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const std::string::size_type space = line.find(' ');
        const std::string word = space == std::string::npos ? line : line.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : trim(line.substr(space + 1));
        if (word == "entry") {
            JournalEntry e;
            e.id = rest;
            e.style = "parchment";
            journal.entries.push_back(e);
        } else if (journal.entries.empty()) {
            continue;
        } else {
            JournalEntry &e = journal.entries.back();
            if (word == "style") e.style = rest;
            else if (word == "title") e.title = rest;
            else if (word == "source") e.source = rest;
            else if (word == "unlock") e.unlock = rest;
            else if (word == "hint") e.hint = rest;
            else if (word == "book") e.book = rest == "earlier" ? 1 : 0;
            else if (word == "era") e.era = std::atoi(rest.c_str());
            else if (word == "text") e.text.push_back(rest);
            else if (word == "margin") {
                const std::string::size_type gap = rest.find(' ');
                if (gap != std::string::npos) e.margins.push_back(std::make_pair(rest.substr(0, gap), trim(rest.substr(gap + 1))));
            }
        }
    }
}

/* How far the player has got, for deciding which pages are turned over. */
struct JournalProgress {
    int chapters;   /* bit mask: bit c = chapter c+1 settled, bit 6 = the last grave settled */
    int graves;     /* most graves settled on any vigil */
    int lanterns;   /* lanterns put out, in total */
    int stumbles, offerings, flawless, shut, rite, hard;   /* what the earlier hands noticed (Options) */
    int settled[3];                                        /* graves settled on each vigil, for the Record */
    JournalProgress() : chapters(0), graves(0), lanterns(0), stumbles(0), offerings(0), flawless(0), shut(0), rite(0), hard(0) {
        settled[0] = settled[1] = settled[2] = 0;
    }
};

bool journal_unlocked(const JournalEntry &e, const JournalProgress &p) {
    if (e.unlock == "free" || e.unlock.empty()) return true;
    if (e.unlock == "finale") return (p.chapters & (1 << 6)) != 0;
    if (e.unlock.compare(0, 8, "chapter ") == 0) {
        const int c = std::atoi(e.unlock.c_str() + 8);
        return c >= 1 && c <= CHAPTER_COUNT && (p.chapters & (1 << (c - 1))) != 0;
    }
    if (e.unlock.compare(0, 7, "graves ") == 0) return p.graves >= std::atoi(e.unlock.c_str() + 7);
    if (e.unlock.compare(0, 9, "lanterns ") == 0) return p.lanterns >= std::atoi(e.unlock.c_str() + 9);
    if (e.unlock.compare(0, 5, "deed ") == 0) {
        /* deed <kind> <count>: quiet counters kept as the player plays */
        const std::string rest = e.unlock.substr(5);
        const std::string::size_type gap = rest.find(' ');
        const std::string kind = gap == std::string::npos ? rest : rest.substr(0, gap);
        const int n = gap == std::string::npos ? 1 : std::atoi(rest.c_str() + gap + 1);
        if (kind == "stumbles") return p.stumbles >= n;
        if (kind == "offerings") return p.offerings >= n;
        if (kind == "flawless") return p.flawless >= n;
        if (kind == "shut") return p.shut >= n;
        if (kind == "rite") return p.rite >= n;
        if (kind == "hard") {
            int chapters = 0;
            for (int c = 0; c < CHAPTER_COUNT; ++c) if (p.hard & (1 << c)) ++chapters;
            return chapters >= n;
        }
    }
    return false;
}

/* Pages found; book -1 counts both books. */
int journal_found(const Journal &journal, const JournalProgress &p, int book = -1) {
    int n = 0;
    for (size_t i = 0; i < journal.entries.size(); ++i)
        if ((book < 0 || journal.entries[i].book == book) && journal_unlocked(journal.entries[i], p)) ++n;
    return n;
}

/* The entries of one book, in order (indices into journal.entries). */
std::vector<int> journal_book_ids(const Journal &journal, int book) {
    std::vector<int> ids;
    for (size_t i = 0; i < journal.entries.size(); ++i) if (journal.entries[i].book == book) ids.push_back(static_cast<int>(i));
    return ids;
}

/* Paper and ink of an earlier hand's page, by the year it was written. */
void era_colours(int era, Rgb &paper, Rgb &ink, Rgb &heading) {
    if (era <= 1300)      { paper = {218, 208, 180}; ink = {44, 30, 24};  heading = {128, 30, 32}; }   /* vellum, rubric red */
    else if (era <= 1600) { paper = {204, 176, 128}; ink = {58, 36, 20};  heading = {70, 40, 20}; }    /* brown rag paper */
    else if (era <= 1800) { paper = {232, 224, 202}; ink = {28, 32, 58};  heading = {28, 32, 58}; }    /* laid paper, blue-black */
    else                  { paper = {222, 220, 208}; ink = {36, 32, 44};  heading = {96, 34, 44}; }    /* a Victorian notebook */
}

/* How frayed the investigator's hand is, 0 (steady) to 5 (barely a hand). */
int scribble_level(const JournalProgress &p) {
    int level = p.graves * 5 / 30;
    if (p.chapters & (1 << 6)) level = 5;
    return std::max(0, std::min(5, level));
}

unsigned scramble(unsigned a) {
    a ^= a << 13;
    a ^= a >> 17;
    a ^= a << 5;
    return a;
}

int pick(unsigned seed, int count) { return static_cast<int>(scramble(seed * 2654435761u + 97u) % static_cast<unsigned>(count)); }

/* A sheet of paper with ragged, torn edges. */
void draw_torn_paper(SDL_Surface *target, int x, int y, int w, int h, Rgb colour, unsigned seed) {
    for (int r = 0; r < h; ++r) {
        int left = static_cast<int>(scramble(seed + r * 7919u) % 4);
        int right = static_cast<int>(scramble(seed * 3u + r * 104729u) % 4);
        if (r < 3 || r >= h - 3) {
            const int edge = r < 3 ? 3 - r : r - (h - 4);
            left += edge * static_cast<int>(scramble(seed + 11u * r) % 4);
            right += edge * static_cast<int>(scramble(seed + 13u * r) % 4);
        }
        fill_rect(target, x + left, y + r, std::max(1, w - left - right), 1, colour);
    }
}

/* ---- the investigator's pen ---- */
void pen_line(SDL_Surface *t, int x0, int y0, int x1, int y1, unsigned seed, Rgb c, int thick = 2) {
    const int dx = x1 - x0, dy = y1 - y0;
    const int steps = std::max(std::abs(dx), std::abs(dy));
    int jx = 0, jy = 0;
    for (int i = 0; i <= steps; ++i) {
        if (i % 7 == 0) {
            jx = static_cast<int>(scramble(seed + i * 13u) % 3) - 1;
            jy = static_cast<int>(scramble(seed + i * 29u + 5u) % 3) - 1;
        }
        fill_rect(t, x0 + dx * i / std::max(1, steps) + jx, y0 + dy * i / std::max(1, steps) + jy, thick, thick, c);
    }
}

void pen_loop(SDL_Surface *t, int cx, int cy, int rx, int ry, unsigned seed, Rgb c) {
    int px = cx + rx, py = cy;
    for (int k = 1; k <= 40; ++k) {
        const double ang = 6.9 * k / 40.0;
        const double grow = 1.0 + 0.16 * k / 40.0;
        const int x = cx + static_cast<int>(std::cos(ang) * rx * grow) + static_cast<int>(scramble(seed + k) % 3) - 1;
        const int y = cy + static_cast<int>(std::sin(ang) * ry * grow) + static_cast<int>(scramble(seed + k * 3u) % 3) - 1;
        pen_line(t, px, py, x, y, seed + k, c, 2);
        px = x; py = y;
    }
}

/* Tally marks in fives: four strokes and a slash. Returns the bottom edge. */
int pen_tally(SDL_Surface *t, int x, int y, int count, int per_row, unsigned seed, Rgb c) {
    const int group_w = 30, group_h = 22;
    int rows = 0;
    for (int g = 0; g * 5 < count; ++g) {
        const int gx = x + (g % per_row) * (group_w + 6);
        const int gy = y + (g / per_row) * (group_h + 8);
        rows = std::max(rows, g / per_row + 1);
        const int marks = std::min(5, count - g * 5);
        for (int m = 0; m < std::min(4, marks); ++m)
            pen_line(t, gx + m * 6, gy, gx + m * 6 + static_cast<int>(scramble(seed + g * 5 + m) % 3) - 1, gy + group_h,
                     seed + g * 17u + m, c, 2);
        if (marks == 5) pen_line(t, gx - 3, gy + group_h - 4, gx + 26, gy + 4, seed + g * 31u, c, 2);
    }
    return y + rows * (group_h + 8);
}

/* Text in the investigator's hand: each word sits a little off the line. */
void pen_text(SDL_Surface *t, TTF_Font *font, const std::string &text, int x, int baseline, unsigned seed, Rgb c) {
    std::string::size_type pos = 0;
    int cursor = x;
    int word = 0;
    while (pos < text.size()) {
        std::string::size_type end = text.find(' ', pos);
        if (end == std::string::npos) end = text.size();
        const std::string w = text.substr(pos, end - pos);
        if (!w.empty()) {
            draw_text_at(t, font, w, cursor, baseline + static_cast<int>(scramble(seed + word * 7u) % 5) - 2, c);
            cursor += text_width(font, w + " ");
        }
        pos = end + 1;
        ++word;
    }
}

/* Marks written into the page text: [[crossed out, still legible]], {{blacked out}} (the words
 * inside only set the length of the ink bar) and ((worn away)): the words are simply gone, with a
 * faint stain where they were, the book's own wear (the words inside only set the gap). */
enum SpanKind { SPAN_STRUCK = 0, SPAN_BLACK = 1, SPAN_WORN = 2 };
struct Span { size_t a, b; int kind; };

void parse_marks(const std::string &raw, std::string &plain, std::vector<Span> &spans) {
    plain.clear();
    spans.clear();
    size_t open = 0;
    bool inside = false;
    int kind = SPAN_STRUCK;
    for (size_t i = 0; i < raw.size();) {
        if (!inside && i + 1 < raw.size() && ((raw[i] == '[' && raw[i + 1] == '[') || (raw[i] == '{' && raw[i + 1] == '{') ||
                                              (raw[i] == '(' && raw[i + 1] == '('))) {
            kind = raw[i] == '{' ? SPAN_BLACK : raw[i] == '(' ? SPAN_WORN : SPAN_STRUCK;
            open = plain.size();
            inside = true;
            i += 2;
        } else if (inside && i + 1 < raw.size() && ((raw[i] == ']' && raw[i + 1] == ']') || (raw[i] == '}' && raw[i + 1] == '}') ||
                                                     (raw[i] == ')' && raw[i + 1] == ')'))) {
            spans.push_back(Span{open, plain.size(), kind});
            inside = false;
            i += 2;
        } else {
            plain += raw[i++];
        }
    }
}

struct LineSpan { size_t start, len; };

std::vector<LineSpan> wrap_spans(TTF_Font *font, const std::string &text, int max_width) {
    std::vector<LineSpan> lines;
    size_t line_start = 0, line_end = 0;
    bool have = false;
    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && text[pos] == ' ') ++pos;
        if (pos >= text.size()) break;
        size_t end = text.find(' ', pos);
        if (end == std::string::npos) end = text.size();
        if (have && text_width(font, text.substr(line_start, end - line_start)) > max_width) {
            lines.push_back(LineSpan{line_start, line_end - line_start});
            line_start = pos;
        } else if (!have) {
            line_start = pos;
        }
        line_end = end;
        have = true;
        pos = end;
    }
    if (have) lines.push_back(LineSpan{line_start, line_end - line_start});
    return lines;
}

struct PenLine { int x, base, w; std::string text; };

/* A heavy ink bar over a word: the investigator's primitive redaction. */
void pen_redact(SDL_Surface *t, int x, int base, int w, unsigned seed) {
    const Rgb ink = {26, 18, 20};
    for (int k = 0; k < 5; ++k)
        pen_line(t, x - 1, base - 13 + k * 3 + static_cast<int>(scramble(seed + k) % 2), x + w + 1,
                 base - 12 + k * 3 + static_cast<int>(scramble(seed + k * 5u) % 3), seed + k * 7u, ink, 4);
}

/* A strike through legible words: two quick, slightly crossed lines. */
void pen_strike(SDL_Surface *t, int x, int base, int w, unsigned seed) {
    const Rgb ink = {52, 26, 24};
    pen_line(t, x - 2, base - 6, x + w + 2, base - 7, seed, ink, 2);
    pen_line(t, x, base - 9, x + w, base - 3, seed + 3u, ink, 2);
}

/* Draws one wrapped line of a page, honouring the marks. `hand` uses the
 * investigator's off-the-line handwriting. */
void draw_marked_line(SDL_Surface *t, TTF_Font *font, const std::string &plain, const LineSpan &ls,
                      const std::vector<Span> &spans, int x, int base, bool hand, unsigned seed, Rgb ink) {
    size_t pos = ls.start;
    const size_t end = ls.start + ls.len;
    while (pos < end) {
        const Span *in = NULL;
        size_t next = end;
        for (size_t i = 0; i < spans.size(); ++i) {
            if (spans[i].a <= pos && pos < spans[i].b) { in = &spans[i]; next = std::min(end, spans[i].b); break; }
            if (spans[i].a > pos) next = std::min(next, spans[i].a);
        }
        const std::string seg = plain.substr(pos, next - pos);
        const int sx = x + text_width(font, plain.substr(ls.start, pos - ls.start));
        const int sw = text_width(font, seg);
        if (in && in->kind == SPAN_BLACK) {
            pen_redact(t, sx, base, sw, seed + static_cast<unsigned>(pos));
        } else if (in && in->kind == SPAN_WORN) {
            /* gone: a water-stain the colour of weak tea, wider than the words, and nothing else */
            const unsigned h = scramble(seed + static_cast<unsigned>(pos) * 29u);
            fill_rect_alpha(t, sx - 5, base - 17, sw + 10, 22, Rgb{150, 112, 70}, 34);
            fill_rect_alpha(t, sx - 1 + static_cast<int>(h % 5), base - 14, std::max(4, sw - 4), 16, Rgb{128, 92, 54}, 30);
            fill_rect_alpha(t, sx + static_cast<int>((h >> 4) % std::max(1, sw)), base - 19 + static_cast<int>((h >> 9) % 4), 5, 3,
                            Rgb{96, 66, 38}, 60);
        } else {
            if (hand) pen_text(t, font, seg, sx, base, seed + static_cast<unsigned>(pos), ink);
            else draw_text_at(t, font, seg, sx, base, ink);
            if (in) pen_strike(t, sx, base, sw, seed + static_cast<unsigned>(pos) * 3u);
        }
        pos = next;
    }
}

const char *const MARGIN_NOTES[] = {
    "who paid?", "check this", "again", "not an accident", "I was there", "he knew", "they all knew",
    "who is counting?", "the tree", "why now?", "I am watched", "NO", "listen", "one stone ahead",
    "whose hand?", "the window", "it is not finished", "keep the count"};
const int MARGIN_NOTE_COUNT = sizeof(MARGIN_NOTES) / sizeof(MARGIN_NOTES[0]);

/* Marks the investigator has made on a page, more of them the further the
 * player has got. The page body is described by `lines`; free space below it
 * is where jots go. */
/* A note in another keeper's hand. The same face as the investigator's, but each hand has its own
 * ink, size and tilt (the text is sheared a few degrees, column by column), so the book reads as
 * having passed through several of them: the teacher (blue-black, brisk, rising), an elder
 * keeper (faded brown, small, sagging) and a sceptic in pencil (grey, level). */
void draw_hand_note(SDL_Surface *t, const Assets &a, const std::string &hand, const std::string &text, int x, int base) {
    TTF_Font *font = hand == "elder" ? a.scrawl_small : a.scrawl_font;
    const Rgb colour = hand == "teacher" ? Rgb{34, 46, 98} : hand == "elder" ? Rgb{128, 92, 58} : Rgb{108, 108, 118};
    const double slope = hand == "teacher" ? -0.030 : hand == "elder" ? 0.020 : -0.012;
    if (!font || text.empty()) return;
    SDL_Surface *s = TTF_RenderUTF8_Blended(font, text.c_str(), sdl_color(colour));
    if (!s) return;
    const int rise = static_cast<int>(std::fabs(slope) * s->w);
    if (g_audit) audit_text(text, x, base - s->h - (slope < 0 ? rise : 0), s->w, s->h + rise);
    for (int cx = 0; cx < s->w; ++cx) {
        const int dy = static_cast<int>(std::floor(slope * cx + 0.5));
        SDL_Rect from = {cx, 0, 1, s->h};
        SDL_Rect to = {x + cx, base - s->h + dy, 1, s->h};
        SDL_BlitSurface(s, &from, t, &to);
    }
    SDL_FreeSurface(s);
}

void draw_marginalia(SDL_Surface *t, const Assets &a, TTF_Font *body, int page, int level, int lanterns, int free_y,
                     const std::vector<PenLine> &lines, bool scrawl_page) {
    const Rgb ink = {52, 26, 24};
    const Rgb red = {132, 34, 40};
    const unsigned seed = static_cast<unsigned>(page) * 977u + 41u;
    if (level >= 1 || scrawl_page) {
        const int marks = std::min(15, 2 + lanterns / 5);
        pen_tally(t, 44, 400, marks, 6, seed, ink);
    }
    if (level >= 2 && !lines.empty()) {
        const PenLine &l = lines[pick(seed, static_cast<int>(lines.size()))];
        pen_line(t, l.x - 2, l.base + 4, l.x + l.w + 2, l.base + 5, seed + 3u, red, 2);
    }
    /* single words struck out: a second thought, or a name the investigator would rather not keep */
    const int struck = level >= 5 ? 3 : (level >= 3 ? level - 2 : 0);
    for (int n = 0; n < struck && !lines.empty(); ++n) {
        const PenLine &l = lines[pick(seed + 71u + n * 13u, static_cast<int>(lines.size()))];
        std::vector<std::pair<size_t, size_t> > words;
        for (size_t i = 0; i < l.text.size();) {
            while (i < l.text.size() && l.text[i] == ' ') ++i;
            const size_t b = i;
            while (i < l.text.size() && l.text[i] != ' ') ++i;
            if (i > b) words.push_back(std::make_pair(b, i));
        }
        if (words.empty()) continue;
        const std::pair<size_t, size_t> w = words[pick(seed + 83u + n * 17u, static_cast<int>(words.size()))];
        const int wx = l.x + text_width(body, l.text.substr(0, w.first));
        pen_strike(t, wx, l.base, text_width(body, l.text.substr(w.first, w.second - w.first)), seed + 97u + n);
    }
    if (level >= 3 && lines.size() > 1) {
        const PenLine &l = lines[1 + pick(seed + 9u, static_cast<int>(lines.size()) - 1)];
        pen_loop(t, l.x + std::min(l.w / 2, 70), l.base - 5, std::min(l.w / 2 + 6, 80), 13, seed + 21u, red);
    }
    if (level >= 5 && lines.size() > 2) {      /* only at the very end does the pen strike through the text */
        const PenLine &l = lines[pick(seed + 33u, static_cast<int>(lines.size()))];
        pen_line(t, l.x, l.base - 5, l.x + l.w, l.base - 4, seed + 39u, ink, 3);
    }
    /* jots in the free space below the text */
    const int notes = level >= 5 ? 4 : (level >= 4 ? 3 : (level >= 2 ? level - 1 : 0));
    int y = std::max(free_y + 22, 330);
    for (int n = 0; n < notes && y < 392; ++n) {
        const char *note = MARGIN_NOTES[(pick(seed, MARGIN_NOTE_COUNT) + n * 5) % MARGIN_NOTE_COUNT];   /* never the same jot twice */
        pen_text(t, a.scrawl_font, note, 60 + pick(seed + n * 7u, 150), y, seed + n * 101u, n % 2 ? red : ink);
        y += 24;
    }
    if (level >= 5) {                                   /* by the end the hand is all but gone */
        const int top = std::min(free_y + 8, 380);       /* keep to the free space below the text */
        for (int k = 0; k < 7 && top < 392; ++k) {
            const int x0 = 40 + pick(seed + k * 3u, 300), y0 = top + pick(seed + k * 5u, 392 - top);
            pen_line(t, x0, y0, x0 + 40 + pick(seed + k, 80), y0 + pick(seed + k * 11u, 20) - 10, seed + k * 19u, ink, 2);
        }
    }
}

void render_journal_grid(SDL_Surface *target, const Assets &a, const Journal &journal, const JournalProgress &p,
                         int cursor, int book) {
    draw_backdrop(target, a, 3);
    draw_title(target, a.large_font, "The Journal", FB_W / 2, 50, C_BRASS);
    const std::vector<int> ids = journal_book_ids(journal, book);
    /* two books: the investigator's casebook, and the older hands bound into it (L1 / R1) */
    const bool any_earlier = !journal_book_ids(journal, 1).empty();
    const std::string tally = std::to_string(journal_found(journal, p, book)) + " of " + std::to_string(ids.size());
    if (any_earlier) {
        draw_text(target, a.small_font, "< The Casebook", FB_W / 2 - 96, 74, book == 0 ? C_BRASS : C_DIM);
        draw_text(target, a.small_font, "Earlier Hands >", FB_W / 2 + 96, 74, book == 1 ? C_BRASS : C_DIM);
        draw_text(target, a.small_font, tally, FB_W / 2, 74, C_TEXT);
    } else {
        draw_text(target, a.small_font, "Pages found: " + tally, FB_W / 2, 74, C_TEXT);
    }
    const int cols = 5, cell_w = 84, cell_h = 62, gap = 7;
    const int left = (FB_W - (cols * cell_w + (cols - 1) * gap)) / 2;
    const int level = scribble_level(p);
    /* five rows show at a time; the grid scrolls to keep the chosen page in view */
    const int visible_rows = 5;
    const int total_rows = (static_cast<int>(ids.size()) + cols - 1) / cols;
    const int first_row = std::max(0, std::min(cursor / cols - 2, total_rows - visible_rows));
    for (size_t k = 0; k < ids.size(); ++k) {
        const size_t i = static_cast<size_t>(ids[k]);
        const JournalEntry &e = journal.entries[i];
        const int row = static_cast<int>(k / cols) - first_row;
        if (row < 0 || row >= visible_rows) continue;
        const int x = left + static_cast<int>(k % cols) * (cell_w + gap);
        const int y = 86 + row * (cell_h + gap);
        const bool open = journal_unlocked(e, p);
        const bool chosen = static_cast<int>(k) == cursor;
        fill_rect(target, x - 3, y - 3, cell_w + 6, cell_h + 6, chosen ? C_BRASS : Rgb{8, 6, 12});
        if (open) {
            Rgb paper = e.style == "clipping" ? Rgb{222, 218, 202}
                              : (e.style == "scrawl" || e.style == "tally" || e.style == "record") ? Rgb{232, 226, 208} : Rgb{226, 206, 164};
            if (e.era) { Rgb unused_ink, unused_heading; era_colours(e.era, paper, unused_ink, unused_heading); }
            audit_box(x, y, cell_w, cell_h);
            draw_torn_paper(target, x, y, cell_w, cell_h, paper, static_cast<unsigned>(i) * 977u + 5u);
            if (e.style == "scrawl" || e.style == "tally" || e.style == "record") {
                /* the investigator's pages: a few jotted strokes instead of a heading */
                for (int k = 0; k < 4; ++k)
                    pen_line(target, x + 10, y + 14 + k * 11, x + 20 + pick(static_cast<unsigned>(i) * 5u + k, 52), y + 13 + k * 11,
                             static_cast<unsigned>(i) * 61u + k, Rgb{52, 26, 24}, 1);
                if (e.style == "tally") pen_tally(target, x + 10, y + 12, 10, 3, static_cast<unsigned>(i), Rgb{52, 26, 24});
            } else {
                TTF_Font *title_font = a.small_font;
                std::vector<std::string> lines = wrap_text(title_font, e.title, cell_w - 12);
                bool too_wide = false;
                for (size_t k = 0; k < lines.size(); ++k) if (text_width(title_font, lines[k]) > cell_w - 12) too_wide = true;
                if (too_wide || lines.size() > 3) {            /* a long word or title: use the small print */
                    title_font = a.tiny_font;
                    lines = wrap_text(title_font, e.title, cell_w - 12);
                }
                if (lines.size() > 3) { lines.resize(3); lines[2] = ellipsize(title_font, lines[2] + "...", cell_w - 12); }
                for (size_t k = 0; k < lines.size(); ++k)
                    draw_text(target, title_font, lines[k], x + cell_w / 2, y + 20 + static_cast<int>(k) * 14, C_INK);
            }
            if (level >= 2)       /* the investigator has been at this page */
                pen_line(target, x + 8, y + cell_h - 8, x + cell_w - 12, y + cell_h - 9, static_cast<unsigned>(i) + 3u, Rgb{132, 34, 40}, 1);
        } else {
            fill_rect(target, x, y, cell_w, cell_h, {30, 24, 38});
            frame_rect(target, x + 4, y + 4, cell_w - 8, cell_h - 8, {70, 56, 86}, 1);
            draw_text(target, a.medium_font, "?", x + cell_w / 2, y + cell_h / 2 + 8, {96, 80, 112});
        }
    }
    if (first_row > 0)                                   /* more pages above and below */
        for (int k = 0; k < 6; ++k) fill_rect(target, FB_W - 22 - k, 80 + k, 2 * k + 1, 1, C_BRASS);
    if (first_row + visible_rows < total_rows)
        for (int k = 0; k < 6; ++k) fill_rect(target, FB_W - 22 - (5 - k), 434 + k, 2 * (5 - k) + 1, 1, C_BRASS);
    std::string caption = "A page is missing.";
    if (cursor >= 0 && cursor < static_cast<int>(ids.size())) {
        const JournalEntry &chosen = journal.entries[ids[cursor]];
        if (journal_unlocked(chosen, p)) caption = chosen.title;
        else if (!chosen.hint.empty()) caption = chosen.hint;      /* the older hands leave a hint, in their own voice */
    }
    draw_hint(target, a, caption, 450);
    draw_text(target, a.small_font, any_earlier ? "A: Read    L1/R1: Other book    B: Back" : "A: Read    B: Back",
              FB_W / 2, 474, C_DIM);
}

void render_journal_page(SDL_Surface *target, Assets &a, const Journal &journal, const JournalProgress &p,
                         int index) {
    draw_backdrop(target, a, 3);
    if (index < 0 || index >= static_cast<int>(journal.entries.size())) return;
    const JournalEntry &e = journal.entries[index];
    const bool clipping = e.style == "clipping";
    const bool record = e.style == "record";
    const bool scrawl = e.style == "scrawl" || record;
    const bool tally = e.style == "tally";
    Rgb paper = clipping ? Rgb{224, 220, 204} : (scrawl || tally) ? Rgb{234, 228, 210} : Rgb{226, 206, 164};
    Rgb ink = clipping ? Rgb{24, 22, 28} : (scrawl || tally) ? Rgb{44, 28, 26} : Rgb{62, 42, 28};
    Rgb heading = ink;
    if (e.era) era_colours(e.era, paper, ink, heading);
    const unsigned seed = static_cast<unsigned>(index) * 31u + 1u;
    draw_torn_paper(target, 28, 24, 432, 424, Rgb{8, 6, 12}, seed);
    audit_box(24, 20, 432, 424);
    draw_torn_paper(target, 24, 20, 432, 424, paper, seed);
    if (scrawl || tally)        /* ruled notebook paper */
        for (int ly = 90; ly < 420; ly += 26) fill_rect(target, 40, ly, 400, 1, {198, 206, 214});
    std::vector<PenLine> lines;
    int y = 70;
    if (clipping) {
        draw_text(target, a.medium_font, e.source, FB_W / 2 - 2, y, ink);
        fill_rect(target, 50, y + 8, 380, 2, ink);
        fill_rect(target, 50, y + 13, 380, 1, ink);
        y += 46;
        const std::vector<std::string> head = wrap_text(a.medium_font, e.title, 380);
        for (size_t k = 0; k < head.size(); ++k) { draw_text(target, a.medium_font, head[k], FB_W / 2 - 2, y, ink); y += 26; }
        fill_rect(target, 50, y - 8, 380, 1, ink);
        y += 14;
    } else if (scrawl || tally) {
        pen_text(target, a.scrawl_big, e.title, 52, y, seed, Rgb{132, 34, 40});
        draw_text_at(target, a.small_font, e.source, 52, y + 20, Rgb{110, 84, 56});
        y += 58;
    } else {
        draw_text(target, a.medium_font, e.title, FB_W / 2 - 2, y, heading);
        draw_text(target, a.small_font, e.source, FB_W / 2 - 2, y + 24, Rgb{110, 84, 56});
        fill_rect(target, 150, y + 34, 170, 1, Rgb{110, 84, 56});
        y += 66;
    }
    TTF_Font *body = (scrawl || tally) ? a.scrawl_big : a.small_font;
    const int step = (scrawl || tally) ? 26 : 19;
    for (size_t pa = 0; pa < e.text.size(); ++pa) {
        std::string plain;
        std::vector<Span> spans;
        parse_marks(e.text[pa], plain, spans);
        const std::vector<LineSpan> wrapped = wrap_spans(body, plain, 364);
        for (size_t k = 0; k < wrapped.size(); ++k) {
            const int lx = (scrawl || tally) ? 54 : 58;
            draw_marked_line(target, body, plain, wrapped[k], spans, lx, y, scrawl || tally,
                             seed + static_cast<unsigned>(pa * 17 + k), ink);
            const std::string line_text = plain.substr(wrapped[k].start, wrapped[k].len);
            lines.push_back(PenLine{lx, y, text_width(body, line_text), line_text});
            y += step;
        }
        y += (scrawl || tally) ? 8 : 11;
    }
    if (record) {
        /* The Record of the Vigils: the investigator's own running tally, filled in from the save. */
        static const char *const VIGIL[3] = {"The Mourners", "The Watch", "The Faithful"};
        std::vector<std::string> rows;
        for (int d = 0; d < 3; ++d) rows.push_back(std::string(VIGIL[d]) + ":  " + std::to_string(p.settled[d]) + " of " + std::to_string(LEVEL_COUNT) + " graves");
        int faithful = 0;
        for (int c = 0; c < CHAPTER_COUNT; ++c) if (p.hard & (1 << c)) ++faithful;
        rows.push_back("Lamps put out: " + std::to_string(p.lanterns) + "     Flames gathered: " + std::to_string(p.offerings));
        rows.push_back("Taken untouched: " + std::to_string(p.flawless) + "     The careless: " + std::to_string(p.stumbles));
        rows.push_back("Most laid down together: " + std::to_string(p.rite) + "     Shut in: " + std::to_string(p.shut));
        rows.push_back("Chapters kept with the Faithful: " + std::to_string(faithful) + " of " + std::to_string(CHAPTER_COUNT));
        static const char *const CLOSING[6] = {
            "All in order. I do not see why I was warned.",
            "It is a sound method. The numbers help.",
            "The numbers are still mine. I check.",
            "I write them down so that I know it is I writing.",
            "I have stopped being sure whose count this is.",
            "The count is the book's now."};
        for (size_t r = 0; r < rows.size(); ++r) {
            pen_text(target, a.scrawl_font, rows[r], 54, y, seed + static_cast<unsigned>(r) * 13u, ink);
            lines.push_back(PenLine{54, y, text_width(a.scrawl_font, rows[r]), rows[r]});
            y += 24;
        }
        y += 6;
        const std::string closing = CLOSING[scribble_level(p)];
        pen_text(target, a.scrawl_font, closing, 54, y, seed + 977u, Rgb{132, 34, 40});
        lines.push_back(PenLine{54, y, text_width(a.scrawl_font, closing), closing});
        y += 24;
    }
    if (tally) {
        /* one stroke for every lantern put out */
        const int shown = std::min(p.lanterns, 5 * 6 * 9);
        pen_tally(target, 56, y + 6, shown, 9, seed, ink);
        draw_text_at(target, a.scrawl_font, std::to_string(p.lanterns), 56, 424, Rgb{132, 34, 40});
    }
    if (g_audit && !lines.empty() && lines.back().base > 404)        /* the tally marks and the footer live below this */
        std::fprintf(stderr, "TEXT-BODY: page \"%s\" runs to y=%d, into the footer\n", e.id.c_str(), lines.back().base);
    /* notes left in the margins by the keepers before: always there, whatever the player has done */
    int notes_end = y;
    int note_base = y + 12;
    for (size_t m = 0; m < e.margins.size(); ++m) {
        const bool beside_tally = note_base > 394 && note_base <= 414;        /* a full page: tuck it right of the tally marks */
        if (note_base > 414) {
            if (g_audit) std::fprintf(stderr, "TEXT-NOTE: margin note dropped, no room on page \"%s\"\n", e.id.c_str());
            break;
        }
        const int shift = e.margins[m].first == "teacher" ? 0 : e.margins[m].first == "elder" ? 38 : 16;
        int note_x = 62 + shift + pick(seed + static_cast<unsigned>(m) * 3u, 24);
        if (beside_tally) note_x = std::max(note_x, 150);
        draw_hand_note(target, a, e.margins[m].first, e.margins[m].second, note_x, note_base);
        notes_end = note_base + 8;
        note_base += 24;
    }
    draw_marginalia(target, a, body, index, scribble_level(p), p.lanterns, notes_end, lines, scrawl || tally);
    draw_text(target, a.small_font, "LEFT/RIGHT: Turn the page    B: Back", FB_W / 2, 436, ink);
}

std::string executable_directory(const char *argv0) {
    std::string path = argv0 ? argv0 : ".";
    const std::string::size_type slash = path.find_last_of('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

enum AppScreen { SCREEN_MAIN, SCREEN_PLAY, SCREEN_SCORES, SCREEN_OPTIONS,
                 SCREEN_HELP, SCREEN_EXIT_CONFIRM, SCREEN_PAUSE_MENU,
                 SCREEN_STAGE_SELECT, SCREEN_GAME, SCREEN_CHAPTER_CARD, SCREEN_STORY,
                 SCREEN_JOURNAL, SCREEN_JOURNAL_PAGE, SCREEN_TITLE };

enum CardNext { CARD_START_STAGE, CARD_NEXT_STAGE };

/* What happens once the story pages being shown have finished. */
enum StoryAfter { AFTER_START_STAGE, AFTER_NEXT_STAGE, AFTER_MAIN_MENU, AFTER_OPTIONS };

} // namespace

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    /* --screenshot FILE [--screen NAME] [--diff D] [--stage N] [--ticks N]
     * renders one screen without the device, for checking the layout. */
    bool screenshot = false;
    std::string screenshot_path = "gravereckoning.bmp";
    std::string shot_screen = "main";
    std::string shot_page = "intro";
    int shot_state = -1;
    bool shot_walled = false, shot_news = false;
    int shot_diff = 1, shot_stage = 1, shot_ticks = 0, shot_cursor = 0, shot_graves = 0, shot_lanterns = 0, shot_deeds = 0, shot_frame = 0, shot_hint = -1;
    bool shot_noeffects = false;
    int autoplay_frames = 0;            /* debug: play the live loop headless with scripted input */
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--screenshot") {
            screenshot = true;
            if (i + 1 < argc) screenshot_path = argv[++i];
        } else if (arg == "--screen" && i + 1 < argc) shot_screen = argv[++i];
        else if (arg == "--diff" && i + 1 < argc) shot_diff = std::atoi(argv[++i]);
        else if (arg == "--stage" && i + 1 < argc) shot_stage = std::atoi(argv[++i]);
        else if (arg == "--ticks" && i + 1 < argc) shot_ticks = std::atoi(argv[++i]);
        else if (arg == "--cursor" && i + 1 < argc) shot_cursor = std::atoi(argv[++i]);
        else if (arg == "--page" && i + 1 < argc) shot_page = argv[++i];
        else if (arg == "--graves" && i + 1 < argc) shot_graves = std::atoi(argv[++i]);
        else if (arg == "--audit") g_audit = true;
        else if (arg == "--date" && i + 1 < argc) g_forced_date = argv[++i];
        else if (arg == "--state" && i + 1 < argc) shot_state = std::atoi(argv[++i]);
        else if (arg == "--walled") shot_walled = true;
        else if (arg == "--news") shot_news = true;
        else if (arg == "--lanterns" && i + 1 < argc) shot_lanterns = std::atoi(argv[++i]);
        else if (arg == "--deeds" && i + 1 < argc) shot_deeds = std::atoi(argv[++i]);
        else if (arg == "--frame" && i + 1 < argc) shot_frame = std::atoi(argv[++i]);
        else if (arg == "--hint" && i + 1 < argc) shot_hint = std::atoi(argv[++i]);
        else if (arg == "--noeffects") shot_noeffects = true;
        else if (arg == "--autoplay" && i + 1 < argc) autoplay_frames = std::atoi(argv[++i]);
    }

    const std::string base = executable_directory(argv[0]);
    const bool hardware = !screenshot && hwdisp_init() == 0;
    if (!hardware && !screenshot) std::fprintf(stderr, "display: hardware path unavailable\n");
    if (hardware) {
        /* driver_sf3500.so performs a one-time internal framebuffer switch on
         * its first present. Do this before SDL, fonts, images, and Game have
         * allocated heap state; otherwise the closed driver can crash during
         * that reinitialization. */
        static unsigned short warmup[FB_W * FB_H];
        std::memset(warmup, 0, sizeof(warmup));
        hwdisp_present(warmup, FB_W, FB_H, FB_W * static_cast<int>(sizeof(unsigned short)));
        std::fprintf(stderr, "display: RGB565 %dx%d (driver warmed)\n", FB_W, FB_H);
    }

    /* Rendering is entirely software-backed; SDL must not open the vendor
     * framebuffer in addition to hwdisp. SDL_Init(0) is the same startup
     * convention used by the working SF3500 SDL ports. */
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (SDL_Init(0) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        if (hardware) hwdisp_deinit();
        return 1;
    }
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        std::fprintf(stderr, "IMG_Init: %s\n", IMG_GetError());
        SDL_Quit();
        if (hardware) hwdisp_deinit();
        return 1;
    }
    if (TTF_Init() != 0) {
        std::fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        IMG_Quit(); SDL_Quit();
        if (hardware) hwdisp_deinit();
        return 1;
    }

    SDL_Surface *framebuffer = SDL_CreateRGBSurfaceWithFormat(0, FB_W, FB_H, 16, SDL_PIXELFORMAT_RGB565);
    Assets assets;
    if (!framebuffer || !load_assets(assets, base)) {
        std::fprintf(stderr, "startup: framebuffer or assets unavailable\n");
        SDL_FreeSurface(framebuffer);
        TTF_Quit(); IMG_Quit(); SDL_Quit();
        if (hardware) hwdisp_deinit();
        return 1;
    }

    HighScores scores;
    scores.load(base + "/highscores.dat");
    scores.save();
    Progress progress;
    progress.load(base + "/progress.dat");
    progress.save();
    Options options;
    options.load(base + "/options.dat");
    g_effects = options.effects && !shot_noeffects;
    Story story;
    load_story(story, base);
    Journal journal;
    load_journal(journal, base);

    Game game(static_cast<unsigned>(SDL_GetTicks()) ^ 0xA5539u);
    joykey_init();
    bool music_started = false;
    if (!screenshot && music_init((base + "/Music").c_str()) == 0) {
        music_started = true;
        music_set_volume(options.volume);
        music_set_muted(options.muted ? 1 : 0);
        music_set_speaker_mix(options.speaker_mix);
        if (sfx_init((base + "/Sfx").c_str()) > 0) sfx_set_enabled(options.sfx ? 1 : 0);
    }
    setpriority(PRIO_PROCESS, 0, -10);   /* a late frame must never starve the audio thread; ignored if not permitted */

    AppScreen screen = screenshot ? SCREEN_MAIN : SCREEN_TITLE;   /* the title picture first, then the menu */
    if (screenshot && shot_graves > 0) options.last_vigil = 2;       /* so a screenshot can show Continue */
    int main_cursor = 0;
    int journal_cursor = 0;
    int journal_view = 0;
    int journal_news = 0;               /* bit 0: new casebook page, bit 1: new older page */
    int journal_book = 0;               /* which book the Journal grid shows */
    int pages_seen[2] = {0, 0};         /* pages found as of the last time the player was told */
    int deeds_seen_stumbles = 0, deeds_seen_offerings = 0, deeds_seen_shut = 0;
    int sealed_seen = 0;
    /* polish: fades, flashes, shake, dust, hints */
    int prev_screen = -1, prev_stage_index = -1;
    int fade_left = 0;
    const int fade_total = 6;
    int flash_left = 0, shake_left = 0;
    Rgb flash_colour = {255, 255, 255};
    std::vector<Mote> motes;
    int story_age = 99;                 /* frames since the newest comic strip was revealed */
    int hint_id = -1, hint_timer = 0;   /* the one-line hint on screen, if any */
    int hint_gap = 0;                   /* a pause between hints */
    static const char *const HINTS[4] = {"Push the stones. They yield to your touch.",
                                         "Wall each one in. A diagonal gap is an open door.",
                                         "Seal every one at once: the flames rise when none walks.",
                                         "Step onto the flames to gather them."};
    int graves_override = 0;   /* only for --screenshot */
    int exit_quote = 0;
    int play_cursor = 1;
    Difficulty play_difficulty = DIFFICULTY_MEDIUM;
    int stage_cursor = 0;
    int score_cursor = 0;
    int options_cursor = 0;
    int options_tab = 0;
    bool options_editing = false;
    int options_backup = 0;
    Difficulty score_difficulty = DIFFICULTY_MEDIUM;
    bool proceed_selected = true;
    int pause_cursor = 0;
    CardNext card_next = CARD_START_STAGE;
    int card_chapter = 0;
    unsigned previous_keys = 0;
    Direction held_direction = NO_DIRECTION;
    int direction_repeat = 0;
    bool running = true;
    unsigned frame = static_cast<unsigned>(std::max(0, shot_frame));

    auto progress_now = [&]() {
        JournalProgress jp;
        jp.chapters = options.journal;
        jp.graves = graves_override;
        for (int d = 0; d < 3; ++d) jp.graves = std::max(jp.graves, progress.unlocked[d] - 1);
        if (options.journal & (1 << 6)) jp.graves = LEVEL_COUNT;
        jp.lanterns = options.lanterns;
        jp.stumbles = options.stumbles;
        jp.offerings = options.offerings;
        jp.flawless = options.flawless;
        jp.shut = options.shut;
        jp.rite = options.rite;
        jp.hard = options.hard;
        for (int d = 0; d < 3; ++d) {
            int done = std::max(0, progress.unlocked[d] - 1);
            if (options.vigils & (1 << d)) done = LEVEL_COUNT;
            else if (done == LEVEL_COUNT - 1 && options.vigils == 0 && (options.journal & (1 << 6))) done = LEVEL_COUNT;   /* an older save */
            jp.settled[d] = std::max(done, std::min(LEVEL_COUNT, graves_override));
        }
        return jp;
    };
    for (int b = 0; b < 2; ++b) pages_seen[b] = journal_found(journal, progress_now(), b);
    std::vector<size_t> story_queue;     /* indices into story.pages */
    size_t story_index = 0;
    int story_revealed = 1;
    StoryAfter story_after = AFTER_MAIN_MENU;
    auto begin_story = [&](const std::vector<std::string> &names, StoryAfter after) -> bool {
        story_queue.clear();
        for (size_t n = 0; n < names.size(); ++n)
            for (size_t i = 0; i < story.pages.size(); ++i)
                if (story.pages[i].name == names[n] && !story.pages[i].panels.empty()) story_queue.push_back(i);
        if (story_queue.empty()) return false;
        story_index = 0;
        story_revealed = 1;
        story_age = 0;
        story_after = after;
        screen = SCREEN_STORY;
        return true;
    };
    auto finish_story = [&]() {
        if (story_after == AFTER_START_STAGE) { game.start_stage(play_difficulty, stage_cursor); screen = SCREEN_GAME; }
        else if (story_after == AFTER_NEXT_STAGE) { game.proceed_to_next_level(); screen = SCREEN_GAME; }
        else if (story_after == AFTER_OPTIONS) screen = SCREEN_OPTIONS;
        else screen = SCREEN_MAIN;
    };
    /* The pages shown at the start of a chapter, as the Story Pages option allows. The very
     * first time the game is played the prologue comes first. */
    auto chapter_pages = [&](int chapter) {
        std::vector<std::string> names;
        if (options.story_mode == 2) {                     /* off: nothing is shown, but note it was skipped */
            options.story_seen |= 1 | (1 << (chapter + 1));
            options.save();
            return names;
        }
        if (chapter == 0 && !(options.story_seen & 1)) names.push_back("intro");
        const int bit = 1 << (chapter + 1);
        if (options.story_mode == 0 || !(options.story_seen & bit)) {
            if (chapter > 0) names.push_back("ch" + std::to_string(chapter) + "b");   /* the lead-up from the chapter before */
            names.push_back("ch" + std::to_string(chapter + 1));
        }
        options.story_seen |= 1 | bit;
        options.save();
        return names;
    };
    /* Starts a chapter's pages, then whatever follows; with no pages to show it goes straight on. */
    auto enter_chapter = [&](int chapter, StoryAfter after) {
        const std::vector<std::string> names = chapter_pages(chapter);
        if (names.empty()) { story_after = after; finish_story(); }
        else if (!begin_story(names, after)) screen = SCREEN_CHAPTER_CARD;
    };
    /* The first grave of a vigil not yet settled (where Continue and the stage select start). */
    auto next_unsettled = [&](Difficulty d) -> int {
        const int di = difficulty_index(d);
        if (options.vigils & (1 << di)) return 0;                       /* finished: start the vigil again */
        return std::max(0, std::min(LEVEL_COUNT - 1, progress.unlocked[di] - 1));
    };
    /* Starts the grave under the stage cursor: a chapter's first grave opens with its pages. */
    auto begin_selected_grave = [&]() {
        proceed_selected = true;
        if (stage_cursor % STAGES_PER_CHAPTER == 0) {
            card_next = CARD_START_STAGE;
            card_chapter = chapter_of(stage_cursor);
            enter_chapter(card_chapter, AFTER_START_STAGE);
        } else {
            game.start_stage(play_difficulty, stage_cursor);
            screen = SCREEN_GAME;
        }
    };

    if (screenshot) {
        if (shot_screen == "play") screen = SCREEN_PLAY;
        else if (shot_screen == "stage") {
            screen = SCREEN_STAGE_SELECT; play_difficulty = static_cast<Difficulty>(shot_diff); stage_cursor = shot_stage - 1;
            if (shot_graves > 0) {                      /* pretend that many graves are settled, some untouched */
                for (int d = 0; d < 3; ++d) {
                    progress.unlocked[d] = std::min(LEVEL_COUNT, shot_graves + 1);
                    options.untouched[d] = 0x15B6D5B & ((1 << std::min(30, shot_graves)) - 1);
                }
                if (shot_graves >= LEVEL_COUNT) options.vigils = 7;
            }
        }
        else if (shot_screen == "scores") { screen = SCREEN_SCORES; score_difficulty = static_cast<Difficulty>(shot_diff); }
        else if (shot_screen == "options") { screen = SCREEN_OPTIONS; options_tab = std::max(0, std::min(OPTION_TAB_COUNT - 1, shot_cursor)); }
        else if (shot_screen == "help") screen = SCREEN_HELP;
        else if (shot_screen == "exit") { screen = SCREEN_EXIT_CONFIRM; exit_quote = std::max(0, std::min(EXIT_QUOTE_COUNT - 1, shot_cursor)); }
        else if (shot_screen == "title") { screen = SCREEN_TITLE; options.title_screen = std::max(0, std::min(2, shot_cursor)); }
        else if (shot_screen == "journal" || shot_screen == "journalpage") {
            graves_override = shot_graves;
            options.lanterns = shot_lanterns;
            options.stumbles = options.offerings = options.flawless = options.shut = options.rite = shot_deeds;
            options.hard = shot_deeds > 0 ? 0x3F : 0;
            options.vigils = shot_graves >= LEVEL_COUNT ? 7 : 0;
            journal_book = shot_page == "earlier" ? 1 : 0;
            for (int c = 0; c < CHAPTER_COUNT; ++c) if (shot_graves >= (c + 1) * STAGES_PER_CHAPTER) options.journal |= 1 << c;
            if (shot_graves >= LEVEL_COUNT) options.journal |= 1 << 6;
            screen = shot_screen == "journal" ? SCREEN_JOURNAL : SCREEN_JOURNAL_PAGE;
            journal_cursor = journal_view = std::max(0, shot_cursor);
        }
        else if (shot_screen == "story") { begin_story(std::vector<std::string>(1, shot_page), AFTER_MAIN_MENU); story_revealed = std::max(1, shot_ticks);
            story_age = shot_frame > 0 ? shot_frame : 99;
            if (shot_cursor > 0 && !story_queue.empty()) story_index = std::min<size_t>(shot_cursor, story_queue.size() - 1); }
        else if (shot_screen == "card") { screen = SCREEN_CHAPTER_CARD; card_chapter = std::max(0, std::min(5, shot_stage - 1)); }
        else if (shot_screen == "pause" || shot_screen == "game" || shot_screen == "complete") {
            game.start_stage(static_cast<Difficulty>(shot_diff), shot_stage - 1);
            for (int i = 0; i < FPS + 2 + shot_ticks; ++i) game.tick();
            if (shot_state >= 0) game.debug_force_state(static_cast<GameState>(shot_state), shot_walled);
            journal_news = shot_news ? 3 : 0;
            if (shot_hint >= 0 && shot_hint < 4) { hint_id = shot_hint; hint_timer = 999; }
            screen = shot_screen == "pause" ? SCREEN_PAUSE_MENU : SCREEN_GAME;
        }
    }

    int autoplay_events[16] = {0};
    if (autoplay_frames > 0) {
        game.start_stage(static_cast<Difficulty>(shot_diff), shot_stage - 1);
        screen = SCREEN_GAME;
    }
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_QUIT) running = false;

        unsigned keys = joykey_read();
        if (autoplay_frames > 0) {                       /* scripted: hold a direction a few frames, now and then A */
            const unsigned r = noise_hash(frame / 6u + 17u);
            const unsigned dirs[5] = {1u << JK_UP, 1u << JK_DOWN, 1u << JK_LEFT, 1u << JK_RIGHT, 0u};
            keys = dirs[r % 5];
            if (screen != SCREEN_GAME && frame % 9 == 0) keys |= 1u << JK_A;
            if (frame >= static_cast<unsigned>(autoplay_frames)) running = false;
        }
        const unsigned pressed = keys & ~previous_keys;
        const unsigned activate = pressed & ((1u << JK_A) | (1u << JK_START));
        const bool back = (pressed & (1u << JK_B)) != 0;
        const bool up = (pressed & (1u << JK_UP)) != 0;
        const bool down = (pressed & (1u << JK_DOWN)) != 0;
        const bool left = (pressed & (1u << JK_LEFT)) != 0;
        const bool right = (pressed & (1u << JK_RIGHT)) != 0;

        /* little sounds for the menus; the grave itself answers to game events below */
        if (music_started) {
            const bool in_grave = screen == SCREEN_GAME && game.get_state() != STATE_LEVEL_COMPLETE &&
                                  game.get_state() != STATE_GAME_OVER && game.get_state() != STATE_WON;
            if (screen == SCREEN_JOURNAL_PAGE) { if (left || right) sfx_play(SFX_PAGE); else if (back) sfx_play(SFX_BACK); }
            else if (screen == SCREEN_STORY) { if (pressed & (1u << JK_A)) sfx_play(SFX_PAGE); }
            else if (!in_grave) {
                if (up || down || left || right) sfx_play(SFX_TICK);
                else if (activate) sfx_play(SFX_SELECT);
                else if (back) sfx_play(SFX_BACK);
            }
        }

        if ((keys & (1u << JK_START)) && (keys & (1u << JK_SELECT))) {
            running = false;
        } else if (screen == SCREEN_TITLE) {
            if (activate) screen = SCREEN_MAIN;
        } else if (screen == SCREEN_MAIN) {
            const bool can_continue = options.last_vigil > 0;
            const int items = can_continue ? 7 : 6;
            if (main_cursor >= items) main_cursor = 0;
            if (up) main_cursor = (main_cursor + items - 1) % items;
            if (down) main_cursor = (main_cursor + 1) % items;
            if (activate && can_continue && main_cursor == 0) {
                /* pick up where the last vigil left off: the first grave not yet settled */
                play_difficulty = static_cast<Difficulty>(std::max(0, std::min(2, options.last_vigil - 1)));
                stage_cursor = next_unsettled(play_difficulty);
                begin_selected_grave();
            } else if (activate) {
                const int choice = main_cursor - (can_continue ? 1 : 0);
                if (choice == 0) { play_cursor = std::max(0, std::min(2, options.last_vigil - 1 < 0 ? 1 : options.last_vigil - 1)); screen = SCREEN_PLAY; }
                else if (choice == 1) screen = SCREEN_SCORES;
                else if (choice == 2) {
                    screen = SCREEN_JOURNAL;
                    journal_book = 0;
                    journal_cursor = 0;
                    for (int b = 0; b < 2; ++b) pages_seen[b] = journal_found(journal, progress_now(), b);
                }
                else if (choice == 3) {
                    screen = SCREEN_OPTIONS;
                }
                else if (choice == 4) screen = SCREEN_HELP;
                else {
                    exit_quote = (exit_quote + 1 + static_cast<int>(SDL_GetTicks() % (EXIT_QUOTE_COUNT - 1))) % EXIT_QUOTE_COUNT;
                    screen = SCREEN_EXIT_CONFIRM;
                }
            }
        } else if (screen == SCREEN_PLAY) {
            if (up) play_cursor = (play_cursor + 2) % 3;
            if (down) play_cursor = (play_cursor + 1) % 3;
            if (back) screen = SCREEN_MAIN;
            if (activate) {
                play_difficulty = static_cast<Difficulty>(play_cursor);
                stage_cursor = next_unsettled(play_difficulty);
                screen = SCREEN_STAGE_SELECT;
            }
        } else if (screen == SCREEN_STAGE_SELECT) {
            if (back) screen = SCREEN_PLAY;
            const int per = STAGES_PER_CHAPTER;
            if (left) stage_cursor = (stage_cursor + LEVEL_COUNT - 1) % LEVEL_COUNT;
            if (right) stage_cursor = (stage_cursor + 1) % LEVEL_COUNT;
            if (up) stage_cursor = (stage_cursor + LEVEL_COUNT - per) % LEVEL_COUNT;
            if (down) stage_cursor = (stage_cursor + per) % LEVEL_COUNT;
            if (activate && progress.is_unlocked(play_difficulty, stage_cursor)) begin_selected_grave();
        } else if (screen == SCREEN_JOURNAL) {
            if ((pressed & ((1u << JK_L1) | (1u << JK_R1))) && !journal_book_ids(journal, 1).empty()) {
                journal_book = 1 - journal_book;                 /* the casebook <-> the earlier hands */
                journal_cursor = 0;
            }
            const std::vector<int> ids = journal_book_ids(journal, journal_book);
            const int n = static_cast<int>(ids.size());
            if (n > 0) {
                if (journal_cursor >= n) journal_cursor = 0;
                if (left) journal_cursor = (journal_cursor + n - 1) % n;
                if (right) journal_cursor = (journal_cursor + 1) % n;
                if (up) journal_cursor = (journal_cursor + n - 5) % n;
                if (down) journal_cursor = (journal_cursor + 5) % n;
                if (activate && journal_unlocked(journal.entries[ids[journal_cursor]], progress_now())) {
                    journal_view = ids[journal_cursor];
                    screen = SCREEN_JOURNAL_PAGE;
                }
            }
            if (back) screen = SCREEN_MAIN;
        } else if (screen == SCREEN_JOURNAL_PAGE) {
            const JournalProgress jp = progress_now();
            const std::vector<int> ids = journal_book_ids(journal, journal.entries[journal_view].book);
            const int n = static_cast<int>(ids.size());
            int at = 0;
            for (int k = 0; k < n; ++k) if (ids[k] == journal_view) at = k;
            for (int turn = 0; turn < 2; ++turn) {
                const int dir = turn == 0 ? (left ? -1 : 0) : (right ? 1 : 0);
                if (!dir) continue;
                for (int step = 1; step <= n; ++step) {
                    const int candidate = ((at + dir * step) % n + n) % n;
                    if (journal_unlocked(journal.entries[ids[candidate]], jp)) { journal_view = ids[candidate]; break; }
                }
            }
            if (back) {
                journal_book = journal.entries[journal_view].book;
                journal_cursor = 0;
                for (int k = 0; k < n; ++k) if (ids[k] == journal_view) journal_cursor = k;
                screen = SCREEN_JOURNAL;
            }
        } else if (screen == SCREEN_STORY) {
            const StoryPage &page = story.pages[story_queue[story_index]];
            if (pressed & (1u << JK_A)) {
                story_age = 0;
                if (story_revealed < static_cast<int>(page.panels.size())) ++story_revealed;
                else if (story_index + 1 < story_queue.size()) { ++story_index; story_revealed = 1; }
                else finish_story();
            } else if ((pressed & (1u << JK_START)) || back) {
                finish_story();
            }
        } else if (screen == SCREEN_CHAPTER_CARD) {
            if (activate) {
                if (card_next == CARD_START_STAGE) game.start_stage(play_difficulty, stage_cursor);
                else game.proceed_to_next_level();
                screen = SCREEN_GAME;
            } else if (back) {
                screen = card_next == CARD_START_STAGE ? SCREEN_STAGE_SELECT : SCREEN_MAIN;
            }
        } else if (screen == SCREEN_SCORES) {
            if (back) screen = SCREEN_MAIN;
            if (up || down) score_cursor = 1 - score_cursor;
            if (pressed & (1u << JK_L1)) {
                score_difficulty = static_cast<Difficulty>(std::max(0, difficulty_index(score_difficulty) - 1));
            }
            if (pressed & (1u << JK_R1)) {
                score_difficulty = static_cast<Difficulty>(std::min(2, difficulty_index(score_difficulty) + 1));
            }
            if ((activate && score_cursor == 1) || (pressed & (1u << JK_Y))) scores.reset();
        } else if (screen == SCREEN_OPTIONS) {
            /* Value rows: A starts editing, LEFT/RIGHT change it, A confirms, B cancels. */
            const bool prologue_seen = (options.story_seen & 1) != 0;
            const int rows = option_rows(options_tab);
            auto row_value = [&]() -> int * {
                if (options_tab == 0 && options_cursor == 0) return &options.volume;
                if (options_tab == 1 && options_cursor == 0) return &options.wall_theme;
                if (options_tab == 2 && options_cursor == 0) return &options.story_mode;
                if (options_tab == 2 && options_cursor == 1 && title_unlocked(1, options.journal)) return &options.title_screen;
                return NULL;
            };
            if (options_editing) {
                int *value = row_value();
                if (!value) options_editing = false;
                else if (back) {                                  /* cancel: put it back */
                    *value = options_backup;
                    if (value == &options.volume) music_set_volume(options.volume);
                    options_editing = false;
                } else if (activate) {                            /* confirm */
                    options.save();
                    options_editing = false;
                } else if (left || right) {
                    const int dir = right ? 1 : -1;
                    if (value == &options.volume) {
                        options.volume = std::max(0, std::min(100, options.volume + dir * 5));
                        music_set_volume(options.volume);
                    } else if (value == &options.wall_theme) {
                        options.wall_theme = (options.wall_theme + WALL_THEME_COUNT + dir) % WALL_THEME_COUNT;
                    } else if (value == &options.title_screen) {
                        for (int step = 1; step <= 3; ++step) {            /* skip the ones not yet earned */
                            const int candidate = (options.title_screen + 3 + dir * step) % 3;
                            if (title_unlocked(candidate, options.journal)) { options.title_screen = candidate; break; }
                        }
                    } else {
                        options.story_mode = (options.story_mode + 3 + dir) % 3;
                    }
                }
            } else {
                if (back) screen = SCREEN_MAIN;
                if (pressed & (1u << JK_L1)) { options_tab = (options_tab + OPTION_TAB_COUNT - 1) % OPTION_TAB_COUNT; options_cursor = 0; }
                if (pressed & (1u << JK_R1)) { options_tab = (options_tab + 1) % OPTION_TAB_COUNT; options_cursor = 0; }
                if (up) options_cursor = (options_cursor + rows - 1) % rows;
                if (down) options_cursor = (options_cursor + 1) % rows;
                if (options_cursor >= option_rows(options_tab)) options_cursor = 0;
                if (activate) {
                    int *value = row_value();
                    if (value && !(value == &options.volume && !music_started)) {
                        options_backup = *value;
                        options_editing = true;
                    } else if (options_tab == 0 && options_cursor == 1 && music_started) {
                        options.muted = !options.muted;
                        options.save();
                        music_set_muted(options.muted ? 1 : 0);
                    } else if (options_tab == 0 && options_cursor == 2 && music_started) {
                        options.speaker_mix = options.speaker_mix ? 0 : 1;
                        options.save();
                        music_set_speaker_mix(options.speaker_mix);
                    } else if (options_tab == 0 && options_cursor == 3 && music_started) {
                        options.sfx = !options.sfx;
                        options.save();
                        sfx_set_enabled(options.sfx ? 1 : 0);
                        sfx_play(SFX_SELECT);
                    } else if (options_tab == 1 && options_cursor == 1) {
                        options.effects = !options.effects;
                        g_effects = options.effects;
                        options.save();
                    } else if (options_tab == 1 && options_cursor == 2) {
                        options.hints = !options.hints;
                        if (options.hints) options.hints_seen = 0;          /* turned back on: they play again */
                        options.save();
                    } else if (options_tab == 2 && options_cursor == 2 && prologue_seen) {
                        begin_story(std::vector<std::string>(1, "intro"), AFTER_OPTIONS);
                    }
                }
            }
        } else if (screen == SCREEN_HELP) {
            if (back || activate) screen = SCREEN_MAIN;
        } else if (screen == SCREEN_EXIT_CONFIRM) {
            if (activate) running = false;
            else if (back) screen = SCREEN_MAIN;
        } else if (screen == SCREEN_PAUSE_MENU) {
            if (up) pause_cursor = (pause_cursor + 4) % 5;
            if (down) pause_cursor = (pause_cursor + 1) % 5;
            if (pause_cursor == 1 && (left || right) && music_started) {
                options.volume = std::max(0, std::min(100, options.volume + (right ? 5 : -5)));
                options.save();
                music_set_volume(options.volume);
            }
            if (pause_cursor == 2 && (left || right || activate) && music_started) {
                options.muted = !options.muted;
                options.save();
                music_set_muted(options.muted ? 1 : 0);
            } else if (back || (activate && pause_cursor == 0)) {
                game.pause();
                screen = SCREEN_GAME;
            } else if (activate && pause_cursor == 3) {
                game.restart_stage();
                screen = SCREEN_GAME;
            } else if (activate && pause_cursor == 4) {
                screen = SCREEN_MAIN;
            }
        } else if (screen == SCREEN_GAME) {
            const GameState state = game.get_state();
            if (state == STATE_RUNNING && (pressed & (1u << JK_START))) {
                game.pause();
                pause_cursor = 0;
                screen = SCREEN_PAUSE_MENU;
            } else if (state == STATE_LEVEL_COMPLETE) {
                if (left || right || up || down) proceed_selected = !proceed_selected;
                if (activate) {
                    if (proceed_selected) {
                        const int next = game.get_stage_index() + 1;
                        if (next < LEVEL_COUNT && next % STAGES_PER_CHAPTER == 0) {
                            card_next = CARD_NEXT_STAGE;
                            card_chapter = chapter_of(next);
                            enter_chapter(card_chapter, AFTER_NEXT_STAGE);
                        } else {
                            game.proceed_to_next_level();
                        }
                    } else {
                        screen = SCREEN_MAIN;
                    }
                } else if (back) {
                    screen = SCREEN_MAIN;
                }
            } else if (state == STATE_RUNNING && game.is_walled_in()) {
                if (activate) game.restart_stage();
                else if (back) screen = SCREEN_MAIN;
            } else if (state == STATE_GAME_OVER) {
                if (activate) game.restart_stage();
                else if (back) screen = SCREEN_MAIN;
            } else if (state == STATE_WON) {
                if ((activate || back) && (options.story_mode == 2 ||
                                           !begin_story(std::vector<std::string>{"end", "end2", "end2b", "end3", "end4", "epilogue"}, AFTER_MAIN_MENU)))
                    screen = SCREEN_MAIN;
            } else {
                Direction input_direction = NO_DIRECTION;
                if (keys & (1u << JK_UP)) input_direction = NORTH;
                else if (keys & (1u << JK_DOWN)) input_direction = SOUTH;
                else if (keys & (1u << JK_LEFT)) input_direction = WEST;
                else if (keys & (1u << JK_RIGHT)) input_direction = EAST;
                if (input_direction != NO_DIRECTION) {
                    if (input_direction != held_direction || direction_repeat <= 0) {
                        game.turn(input_direction);
                        direction_repeat = 4;
                    } else {
                        --direction_repeat;
                    }
                    held_direction = input_direction;
                } else {
                    held_direction = NO_DIRECTION;
                    direction_repeat = 0;
                }
            }
        }

        previous_keys = keys;
        if (screen == SCREEN_GAME && !screenshot) {
            game.tick();
            const std::vector<int> events = game.take_events();
            bool played[16] = {false};          /* each sound once per frame, however many things happened at once */
            for (size_t e = 0; e < events.size(); ++e) {
                if (autoplay_frames > 0 && events[e] >= 0 && events[e] < 16) ++autoplay_events[events[e]];
                const Point at = game.get_revenant();
                switch (events[e]) {
                case EV_PUSH: {
                    if (!played[EV_PUSH]) sfx_play(SFX_PUSH);
                    played[EV_PUSH] = true;
                    if (g_effects) {                       /* a few specks off the stone, ahead of the revenant */
                        const Point d = {game.get_facing() == EAST ? 1 : game.get_facing() == WEST ? -1 : 0,
                                         game.get_facing() == SOUTH ? 1 : game.get_facing() == NORTH ? -1 : 0};
                        const double cx = BOARD_X + (at.x + d.x) * TILE + TILE / 2.0, cy = BOARD_Y + (at.y + d.y) * TILE + TILE / 2.0;
                        for (int k = 0; k < 5; ++k) {
                            const unsigned r = noise_hash(g_frame * 5u + static_cast<unsigned>(k) * 97u);
                            motes.push_back(Mote{cx + static_cast<int>(r % 9) - 4, cy + static_cast<int>((r >> 8) % 7) - 2,
                                                 d.x * 0.5 + (static_cast<int>((r >> 12) % 100) - 50) / 90.0,
                                                 d.y * 0.5 - 0.4 + (static_cast<int>((r >> 20) % 100) - 50) / 140.0, 8 + static_cast<int>(r % 6), 12});
                        }
                    }
                    break;
                }
                case EV_SEAL: if (!played[EV_SEAL]) sfx_play(SFX_SEAL); played[EV_SEAL] = true; break;
                case EV_RISE:
                    sfx_play(SFX_EXTINGUISH);
                    sfx_play(SFX_BELL);
                    if (g_effects) { flash_left = 8; flash_colour = Rgb{150, 196, 255}; shake_left = 5; }
                    break;
                case EV_FALL: if (!played[EV_FALL]) sfx_play(SFX_FALL); played[EV_FALL] = true; break;
                case EV_DEATH:
                    sfx_play(SFX_TOLL);
                    if (g_effects) { flash_left = 7; flash_colour = Rgb{132, 18, 24}; shake_left = 4; }
                    break;
                case EV_OFFERING: if (!played[EV_OFFERING]) sfx_play(SFX_CHIME); played[EV_OFFERING] = true; break;
                case EV_WAVE: sfx_play(SFX_WAVE); break;
                case EV_GRAVE: sfx_play(SFX_GRAVE); break;
                case EV_COMPLETE: sfx_play(SFX_SETTLED); break;
                default: break;
                }
                if (options.hints && !(options.hints_seen & 4) && events[e] == EV_SEAL && game.get_stage_index() == 0) { hint_id = 2; hint_timer = FPS * 5; }
                if (options.hints && !(options.hints_seen & 8) && events[e] == EV_RISE && game.get_stage_index() == 0) { hint_id = 3; hint_timer = FPS * 5; }
                /* doing the thing the hint is about puts it away */
                if ((events[e] == EV_PUSH && hint_id == 0) || (events[e] == EV_SEAL && hint_id == 1) ||
                    (events[e] == EV_RISE && hint_id == 2) || (events[e] == EV_OFFERING && hint_id == 3))
                    hint_timer = std::min(hint_timer, 1);
            }
            if (game.get_state() == STATE_RUNNING || game.get_state() == STATE_SETUP) {
                if (hint_timer > 0) {
                    if (--hint_timer == 0) {
                        if (hint_id >= 0) { options.hints_seen |= 1 << hint_id; options.save(); }
                        hint_id = -1;
                        hint_gap = FPS;
                    }
                } else if (hint_gap > 0) {
                    --hint_gap;
                } else if (options.hints && game.get_stage_index() == 0) {
                    int pursuers_on_board = 0;
                    const std::vector<TileType> &cells = game.get_board();
                    for (size_t c = 0; c < cells.size(); ++c) if (cells[c] == PURSUER) ++pursuers_on_board;
                    if (!(options.hints_seen & 1)) { hint_id = 0; hint_timer = FPS * 6; }
                    else if (!(options.hints_seen & 2) && pursuers_on_board > 0) { hint_id = 1; hint_timer = FPS * 6; }
                }
            }
            int completed_stage = -1;
            int completed_score = 0;
            if (game.take_completed_stage(completed_stage, completed_score)) {
                scores.record(game.get_difficulty(), completed_stage, completed_score);
                progress.unlock_next(game.get_difficulty(), completed_stage);
                if (completed_stage % STAGES_PER_CHAPTER == STAGES_PER_CHAPTER - 1)
                    options.journal |= 1 << chapter_of(completed_stage);
                if (completed_stage == LEVEL_COUNT - 1) {
                    options.journal |= 1 << 6;
                    options.vigils |= 1 << (game.get_difficulty() == DIFFICULTY_SLOW ? 0 : game.get_difficulty() == DIFFICULTY_FAST ? 2 : 1);
                }
                if (game.get_stage_deaths() == 0) {
                    options.flawless = std::min(99999, options.flawless + 1);
                    options.untouched[difficulty_index(game.get_difficulty())] |= 1 << completed_stage;
                }
                if (game.get_difficulty() == DIFFICULTY_FAST && completed_stage % STAGES_PER_CHAPTER == STAGES_PER_CHAPTER - 1)
                    options.hard |= 1 << chapter_of(completed_stage);
                options.save();
                journal_news = 0;                                  /* tell the player what has been found since the last time */
                for (int b = 0; b < 2; ++b) {
                    const int found = journal_found(journal, progress_now(), b);
                    if (found > pages_seen[b]) journal_news |= 1 << b;
                    pages_seen[b] = found;
                }
            }
            if (game.get_sealed_total() > sealed_seen) {   /* lanterns put out: the investigator's count */
                options.lanterns = std::min(99999, options.lanterns + game.get_sealed_total() - sealed_seen);
                sealed_seen = game.get_sealed_total();
                options.save();
            }
            /* the quiet counters the earlier hands read: the same way, as they happen */
            if (game.get_stumble_total() > deeds_seen_stumbles || game.get_offering_total() > deeds_seen_offerings ||
                game.get_shut_total() > deeds_seen_shut || game.get_best_rite() > options.rite) {
                options.stumbles = std::min(99999, options.stumbles + std::max(0, game.get_stumble_total() - deeds_seen_stumbles));
                options.offerings = std::min(99999, options.offerings + std::max(0, game.get_offering_total() - deeds_seen_offerings));
                options.shut = std::min(99999, options.shut + std::max(0, game.get_shut_total() - deeds_seen_shut));
                options.rite = std::max(options.rite, std::min(99, game.get_best_rite()));
                deeds_seen_stumbles = game.get_stumble_total();
                deeds_seen_offerings = game.get_offering_total();
                deeds_seen_shut = game.get_shut_total();
                options.save();
            }
            if (game.get_state() != STATE_LEVEL_COMPLETE) journal_news = 0;
            if (game.get_state() == STATE_GAME_OVER)
                scores.record(game.get_difficulty(), game.get_level() - 1, game.get_stage_score());
        }

        if (music_started) {
            /* the chapter's own music while in a grave or on its story pages; the title music elsewhere */
            int group = 0;
            if (screen == SCREEN_GAME || screen == SCREEN_PAUSE_MENU) group = chapter_of(game.get_stage_index()) + 1;
            else if (screen == SCREEN_CHAPTER_CARD) group = card_chapter + 1;
            else if (screen == SCREEN_STORY && story_index < story_queue.size()) {
                const std::string &name = story.pages[story_queue[story_index]].name;
                if (name.size() >= 3 && name.compare(0, 2, "ch") == 0 && name[2] >= '1' && name[2] <= '6') group = name[2] - '0';
                else if (name.compare(0, 3, "end") == 0 || name == "epilogue") group = 6;
            }
            music_set_group(group);
        }
        g_frame = frame;
        if (story_age < 99) ++story_age;
        const int beat = static_cast<int>(frame / 8);
        const int current_chapter = chapter_of(game.get_stage_index());
        g_boxes.clear();
        if (screen == SCREEN_TITLE) render_title(framebuffer, assets,
                                                      (screenshot || title_unlocked(options.title_screen, options.journal)) ? options.title_screen : 0,
                                                      (frame / 12) % 3 != 2, frame);
        else if (screen == SCREEN_MAIN) render_main_menu(framebuffer, assets, main_cursor, options.last_vigil > 0);
        else if (screen == SCREEN_PLAY) render_play_menu(framebuffer, assets, play_cursor);
        else if (screen == SCREEN_STAGE_SELECT)
            render_stage_select(framebuffer, assets, progress, play_difficulty, stage_cursor, options);
        else if (screen == SCREEN_JOURNAL) render_journal_grid(framebuffer, assets, journal, progress_now(), journal_cursor, journal_book);
        else if (screen == SCREEN_JOURNAL_PAGE) render_journal_page(framebuffer, assets, journal, progress_now(), journal_view);
        else if (screen == SCREEN_STORY)
            render_story(framebuffer, assets, story, story.pages[story_queue[story_index]], story_revealed,
                         story_index + 1 == story_queue.size(), story_age);
        else if (screen == SCREEN_CHAPTER_CARD) render_chapter_card(framebuffer, assets, card_chapter);
        else if (screen == SCREEN_SCORES)
            render_scores(framebuffer, assets, scores, score_difficulty, score_cursor == 1, options);
        else if (screen == SCREEN_OPTIONS) render_options(framebuffer, assets, options, options_tab, options_cursor, options_editing, current_chapter, music_started,
                                  (options.story_seen & 1) != 0, options.journal);
        else if (screen == SCREEN_HELP) render_help(framebuffer, assets);
        else if (screen == SCREEN_EXIT_CONFIRM) render_exit_confirm(framebuffer, assets, exit_quote);
        else if (screen == SCREEN_PAUSE_MENU) {
            render_frame(framebuffer, assets, game, options.wall_theme, beat);
            render_pause_menu(framebuffer, assets, options, pause_cursor, music_started);
        }
        else {
            render_frame(framebuffer, assets, game, options.wall_theme, beat);
            if (game.get_state() == STATE_LEVEL_COMPLETE)
                render_level_complete(framebuffer, assets, game, proceed_selected, journal_news);
        }

        /* a hint in the very first grave, in the clear space above the board */
        if (screen == SCREEN_GAME && hint_id >= 0 && hint_timer > 0 && options.hints) {
            const std::string text = HINTS[hint_id];
            const int w = text_width(assets.small_font, text) + 32;
            const SDL_Rect plate = {(FB_W - w) / 2, 80, w, 30};
            audit_box(plate.x, plate.y, plate.w, plate.h);
            fill_rect_alpha(framebuffer, plate.x, plate.y, plate.w, plate.h, C_PANEL, 214);
            frame_rect(framebuffer, plate.x, plate.y, plate.w, plate.h, C_EDGE, 1);
            draw_text(framebuffer, assets.small_font, text, FB_W / 2, plate.y + 20, C_TEXT);
        }

        /* fades between screens, and the flash, shake and dust of the grave */
        if (screen != prev_screen) {
            const bool within_grave = (screen == SCREEN_GAME || screen == SCREEN_PAUSE_MENU) &&
                                      (prev_screen == SCREEN_GAME || prev_screen == SCREEN_PAUSE_MENU);
            if (g_effects && prev_screen >= 0 && !within_grave) fade_left = fade_total;
            if (screen == SCREEN_GAME && prev_screen != SCREEN_PAUSE_MENU && !screenshot) {
                options.last_vigil = difficulty_index(game.get_difficulty()) + 1;     /* what Continue picks up */
                options.save();
            }
            prev_screen = screen;
        }
        if (screen == SCREEN_GAME) {
            if (g_effects && prev_stage_index >= 0 && game.get_stage_index() != prev_stage_index) fade_left = fade_total;
            prev_stage_index = game.get_stage_index();
        }
        if (g_effects && screen == SCREEN_GAME) draw_motes(framebuffer, motes);
        else motes.clear();
        if (g_effects) apply_screen_effects(framebuffer, fade_left, fade_total, flash_left, flash_colour, shake_left);
        if (fade_left > 0) --fade_left;
        if (flash_left > 0) --flash_left;
        if (shake_left > 0) --shake_left;

        if (screenshot) {
            if (SDL_SaveBMP(framebuffer, screenshot_path.c_str()) != 0)
                std::fprintf(stderr, "screenshot: %s\n", SDL_GetError());
            running = false;
        } else if (hardware) {
            hwdisp_present(framebuffer->pixels, FB_W, FB_H, framebuffer->pitch);
        }

        ++frame;
        const Uint32 target = frame * (1000 / FPS);
        const Uint32 now = SDL_GetTicks();
        if (target > now && autoplay_frames <= 0) SDL_Delay(target - now);
    }

    if (autoplay_frames > 0)
        std::fprintf(stderr, "autoplay: %u frames; events push %d, seal %d, rise %d, fall %d, death %d, offering %d, wave %d, grave %d, complete %d; "
                             "hints seen %d; motes now %d; fade/flash/shake ran; stage %d state %d\n",
                     frame, autoplay_events[EV_PUSH], autoplay_events[EV_SEAL], autoplay_events[EV_RISE], autoplay_events[EV_FALL],
                     autoplay_events[EV_DEATH], autoplay_events[EV_OFFERING], autoplay_events[EV_WAVE], autoplay_events[EV_GRAVE],
                     autoplay_events[EV_COMPLETE], options.hints_seen, static_cast<int>(motes.size()), game.get_stage_index() + 1,
                     static_cast<int>(game.get_state()));
    if (music_started) music_deinit();
    SDL_FreeSurface(framebuffer);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    if (hardware) hwdisp_deinit();
    return 0;
}
