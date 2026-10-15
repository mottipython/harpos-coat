/*
 * harpos-coat: a cartoon screensaver. Harpo Marx, on a vaudeville stage, pulls one
 * thing after another out of his long coat (a candle burning at both ends, a cup of
 * hot coffee, a fish, a blowtorch, a live duck, a ladder far too long for any coat),
 * shows each one off and tosses it onto a growing heap. Now and then he honks, or
 * pulls a face.
 *
 *   harpos-coat            full screen on every display; any key or mouse movement quits
 *   harpos-coat --window   in a window; Esc or Q quits
 *   HarposCoat.scr /s      Windows screensaver conventions
 */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The pictures (drawn with Gemini, cut by tools/make_parts.py) are built into the
 * program as PNG and JPEG files and decoded at start-up. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS /* else Windows builds need libwinpthread-1.dll */
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if defined(__APPLE__)
/* Mach-O: its own section names, and C names carry a leading underscore */
#define ART_SECTION ".section __DATA,__const\n"
#define ART_SYM "_art_"
#elif defined(_WIN32)
#define ART_SECTION ".section .rdata,\"dr\"\n"
#define ART_SYM "art_"
#else
#define ART_SECTION ".section .rodata\n"
#define ART_SYM "art_"
#endif
#define ART(name, file) \
    __asm__(ART_SECTION ".globl " ART_SYM #name "\n" ART_SYM #name ":\n.incbin \"assets/art/" file "\"\n" \
            ".globl " ART_SYM #name "_end\n" ART_SYM #name "_end:\n.text\n"); \
    extern const unsigned char art_##name[], art_##name##_end[];
/* Every picture, by name and file (src/art.h): Harpo in his twelve poses, the
 * sixteen things, and the stage. */
#include "art.h"
#define X(name, file) ART(name, file)
SPRITES
#undef X
#define X(name, file) SP_##name,
enum { SPRITES NSPRITE };
#undef X
#define X(name, file) {art_##name, art_##name##_end},
static struct { const unsigned char *file, *end; int w, h; unsigned char *px; } sprite[NSPRITE] = { SPRITES };
#undef X
static SDL_Texture **cur_tex; /* the pictures, for the renderer being drawn to */

static void load_sprites(void)
{
    for (int i = 0; i < NSPRITE; i++) {
        int n;
        sprite[i].px = stbi_load_from_memory(sprite[i].file, (int)(sprite[i].end - sprite[i].file), &sprite[i].w, &sprite[i].h, &n, 4);
        if (!sprite[i].px) { sprite[i].w = sprite[i].h = 1; }
    }
}
static void make_textures(SDL_Renderer *r, SDL_Texture **tex)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    for (int i = 0; i < NSPRITE; i++) {
        tex[i] = NULL;
        if (!sprite[i].px) continue;
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sprite[i].w, sprite[i].h);
        if (!tex[i]) continue;
        SDL_UpdateTexture(tex[i], NULL, sprite[i].px, sprite[i].w * 4);
        SDL_SetTextureBlendMode(tex[i], SDL_BLENDMODE_BLEND);
    }
}

/* ---- options ---- */
static int opt_window = 0;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb BG = {10, 9, 12}, INK = {30, 24, 20}, PAPER = {236, 226, 200}, GOLD = {205, 168, 92};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double a)
{
    SDL_Color o = {c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c); vert(x1, y1, c); vert(x2, y2, c); vert(x3, y3, c);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
static void rect(double x, double y, double w, double h, SDL_Color c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 4 ? 10 : r < 30 ? 28 : 56;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(cx + r * cos(i * 2 * M_PI / n), cy + r * sin(i * 2 * M_PI / n), rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
/* a stroke with round ends */
static void line(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy);
    if (l > 1e-9) {
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    if (w > 2.5) { disc(x0, y0, w / 2, c, c); disc(x1, y1, w / 2, c, c); }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

static SDL_Renderer *ren; /* where the current frame is being drawn */

/* ---- lettering: pen strokes on a 4 x 6 grid, drawn with a round nib ---- */
static const char *GLYPH[] = {
    "A06 02 20 42 46|04 44", "B00 06 36 45 44 33 03|33 42 41 30 00", "C41 30 10 01 05 16 36 45",
    "D00 06 26 44 42 20 00", "E40 00 06 46|03 33", "F40 00 06|03 33", "G41 30 10 01 05 16 36 45 43 23",
    "H00 06|40 46|03 43", "I20 26|10 30|16 36", "J40 45 36 16 05", "K00 06|40 03 46", "L00 06 46",
    "M06 00 23 40 46", "N06 00 46 40", "O10 30 41 45 36 16 05 01 10", "P06 00 30 41 42 33 03",
    "Q10 30 41 45 36 16 05 01 10|24 46", "R06 00 30 41 42 33 03|23 46", "S41 30 10 01 02 13 33 44 45 36 16 05",
    "T00 40|20 26", "U00 05 16 36 45 40", "V00 26 40", "W00 16 23 36 40", "X00 46|40 06", "Y00 23 40|23 26",
    "Z00 40 06 46", "010 30 41 45 36 16 05 01 10", "111 20 26|16 36", "201 10 30 41 42 06 46",
    "301 10 30 41 42 33 23|33 44 45 36 16 05", "436 30 04 44", "540 00 03 33 44 45 36 16 05",
    "630 10 01 05 16 36 45 44 33 03", "700 40 16", "813 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33",
    "943 13 02 01 10 30 41 45 36 16", ",25 26 17", ".26 26", ":22 22|25 25", "'20 21", "?01 10 30 41 42 24|26 26",
    "-13 33", "!20 24|26 26", "+03 43|21 25", "=02 42|04 44",
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPH / sizeof *GLYPH; i++)
        if (GLYPH[i][0] == ch) return GLYPH[i] + 1;
    return NULL;
}
static double text_width(const char *str, double s) { return strlen(str) * 6.2 * s - 2.2 * s; }
/* text centred on cx, top at y; s is one grid step, so letters are 6s tall */
static void text(const char *str, double cx, double y, double s, double weight, SDL_Color c)
{
    double x = cx - text_width(str, s) / 2;
    for (; *str; str++, x += 6.2 * s) {
        const char *g = glyph(*str);
        if (!g) continue;
        int have = 0; double lx = 0, ly = 0;
        for (; *g; g++) {
            if (*g == '|') { have = 0; continue; }
            if (*g == ' ') continue;
            double gx = x + (g[0] - '0') * s, gy = y + (g[1] - '0') * s;
            g++;
            if (have) line(lx, ly, gx, gy, s * weight, c);
            disc(gx, gy, s * weight * (have && lx == gx && ly == gy ? 0.62 : 0.5), c, c); /* the nib is round */
            lx = gx; ly = gy; have = 1;
        }
    }
}

/* ---- randomness and timing helpers ---- */
static uint64_t seed_state;
static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double frand(void) { return (next_rand() >> 11) * (1.0 / 9007199254740992.0); }
static double ramp(double t, double a, double b) { return t <= a ? 0 : t >= b ? 1 : (t - a) / (b - a); }
static double ease(double x) { return x * x * (3 - 2 * x); }


/* ---- the act ----
 *
 * Harpo pulls the sixteen things out of his coat one at a time, in a different order
 * every time round (but always the anchor, and then the ladder, last). Each turn he
 * reaches in, pulls the thing out and holds it up to show us, tosses it over to the
 * heap, and then honks, pulls a face, tips his hat or just waits. Then he looks into his coat, shrugs, the stage goes
 * dark, and he begins again. Everything is worked out from T, the act's own clock, so
 * the sound and the picture always agree. */
static double T, anim_t;

static double hash01(uint64_t n)
{
    n = (n + 0x9E3779B97F4A7C15ULL) * 0xBF58476D1CE4E5B9ULL;
    n ^= n >> 31; n *= 0x94D049BB133111EBULL; n ^= n >> 29;
    return (n >> 11) * (1.0 / 9007199254740992.0);
}

enum { CANDLE, COFFEE, FISH, TORCH, MALLET, DUCK, SCISSORS, SANDWICH, TELESCOPE, ALARM, ICECREAM, BOAT,
       FLOWERS, LAMP, LADDER, ANCHOR, NTHING };
typedef struct {
    int pic;
    double size;          /* its longest side, in Harpo's heights (as big as it is in his hand) */
    double gx, gy;        /* where he holds it, as part of its picture */
    int land;             /* the noise it makes on the heap */
} Thing;
enum { L_THUD, L_CLATTER, L_SOFT, L_QUACK, L_CRASH };
/* Harpo holding each one up (04-holding), and where his feet are in it */
#include "hold.h"
static const int HOLDING[NTHING] = {
    SP_hold_candle, SP_hold_coffee, SP_hold_fish, SP_hold_torch, SP_hold_mallet, SP_hold_duck, SP_hold_scissors,
    SP_hold_sandwich, SP_hold_telescope, SP_hold_alarm, SP_hold_icecream, SP_hold_boat, SP_hold_flowers,
    SP_hold_lamp, SP_hold_ladder, SP_hold_anchor};
static Thing thing[NTHING];
static void make_things(void)
{
    const Thing t[NTHING] = {
        [CANDLE] = {SP_candle, 0.45, 0.50, 0.62, L_SOFT},     [COFFEE] = {SP_coffee, 0.26, 0.50, 0.92, L_CLATTER},
        [FISH] = {SP_fish, 0.62, 0.50, 0.06, L_SOFT},         [TORCH] = {SP_torch, 0.33, 0.73, 0.62, L_CLATTER},
        [MALLET] = {SP_mallet, 0.60, 0.12, 0.90, L_THUD},     [DUCK] = {SP_duck, 0.62, 0.42, 0.30, L_QUACK},
        [SCISSORS] = {SP_scissors, 0.52, 0.12, 0.55, L_CLATTER}, [SANDWICH] = {SP_sandwich, 0.20, 0.30, 0.75, L_SOFT},
        [TELESCOPE] = {SP_telescope, 0.50, 0.22, 0.78, L_THUD}, [ALARM] = {SP_alarm, 0.38, 0.50, 0.08, L_CLATTER},
        [ICECREAM] = {SP_icecream, 0.34, 0.50, 0.92, L_SOFT}, [BOAT] = {SP_boat, 0.42, 0.50, 0.88, L_THUD},
        [FLOWERS] = {SP_flowers, 0.52, 0.50, 0.90, L_SOFT},   [LAMP] = {SP_lamp, 0.48, 0.50, 0.95, L_CLATTER},
        [LADDER] = {SP_ladder, 1.30, 0.50, 0.55, L_CRASH},    [ANCHOR] = {SP_anchor, 0.62, 0.50, 0.06, L_THUD},
    };
    memcpy(thing, t, sizeof t);
}

/* Harpo's poses (tools/make_parts.py, in the order of the sheet), and where the hand
 * that holds things is in each, in the pose's own pixels */
enum { POSE_REACH, POSE_PULL, POSE_OFFER, POSE_LOOK, POSE_GOOKIE, POSE_TOSS, POSE_PEER, POSE_HEAVE, POSE_HONK, POSE_WAIT, POSE_HAT, POSE_SHRUG };
static const double HAND[12][2] = {
    [POSE_REACH] = {205, 300}, [POSE_PULL] = {335, 125}, [POSE_OFFER] = {395, 235}, [POSE_LOOK] = {322, 220},
    [POSE_GOOKIE] = {360, 235}, [POSE_TOSS] = {50, 105}, [POSE_HEAVE] = {262, 300},
};
#define HARPO_TALL 570.0                 /* Harpo's height in the pictures */
/* the pictures of the poses (the list of pictures is in the order of their names, so
 * harpo10 comes before harpo2) */
static const int HARPO[12] = {SP_harpo0, SP_harpo1, SP_harpo2, SP_harpo3, SP_harpo4, SP_harpo5,
                              SP_harpo6, SP_harpo7, SP_harpo8, SP_harpo9, SP_harpo10, SP_harpo11};
static double foot_x[12];               /* the middle of his feet in each pose, found when loaded */
static void find_feet(void)
{
    for (int p = 0; p < 12; p++) {
        int s = HARPO[p], w = sprite[s].w, h = sprite[s].h;
        double sum = 0, n = 0;
        for (int y = h * 92 / 100; y < h; y++)
            for (int x = 0; x < w; x++)
                if (sprite[s].px && sprite[s].px[(y * w + x) * 4 + 3] > 128) { sum += x; n++; }
        foot_x[p] = n ? sum / n : w / 2.0;
    }
}

#define BEAT 6.6                         /* one thing, start to finish */
#define LADDER_BEAT 9.0
#define ENDING 5.0
#define ROUND (14 * BEAT + BEAT + LADDER_BEAT + ENDING)

/* the order of the things in round r */
static void order_of(long r, int *ord)
{
    int k = 0;
    for (int i = 0; i < NTHING; i++) if (i != LADDER && i != ANCHOR) ord[k++] = i;
    for (int i = 13; i > 0; i--) {
        int j = (int)(hash01((uint64_t)r * 97 + i) * (i + 1));
        int t = ord[i]; ord[i] = ord[j]; ord[j] = t;
    }
    ord[14] = ANCHOR; ord[15] = LADDER;
}
/* where in the act time t is: the round, which turn (0 .. 15, or 16 for the ending),
 * the time into that turn, and the time at which that turn began */
typedef struct { long round; int turn, what; double u, start; int ord[NTHING]; } Moment;
static Moment moment(double t)
{
    Moment m;
    m.round = (long)floor(t / ROUND);
    double s = t - m.round * ROUND;
    order_of(m.round + (long)(opt_seed % 1000), m.ord);
    m.turn = 0;
    while (m.turn < NTHING) {
        double len = m.ord[m.turn] == LADDER ? LADDER_BEAT : BEAT;
        if (s < len) break;
        s -= len; m.turn++;
    }
    m.u = s;
    m.what = m.turn < NTHING ? m.ord[m.turn] : -1;
    m.start = t - s;
    return m;
}
/* the face he pulls, and what he does after the toss, for a turn */
static int after_for(const Moment *m)
{
    static const int A[] = {POSE_WAIT, POSE_HONK, POSE_HAT, POSE_SHRUG, POSE_HONK, POSE_GOOKIE, POSE_LOOK};
    return A[(int)(hash01((uint64_t)m->round * 37 + m->turn) * 7) % 7];
}

/* the timings of a turn: when he pulls it out, shows it, throws it, lets go, and when
 * it lands and he does what he does after */
typedef struct { double pull, show, toss, let_go, land, after; } Times;
static Times times_for(int what)
{
    if (what == LADDER) return (Times){0.9, 3.6, 6.2, 6.3, 7.15, 6.8};
    return (Times){0.9, 1.7, 4.6, 4.7, 5.45, 5.2};
}

static void update(double dt)
{
    anim_t += dt;
    T += dt * opt_speed;
    void sound_step_(void); sound_step_();
}
static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    if (!opt_seed) opt_seed = 1 + next_rand() % 100000;
    anim_t = 0;
    T = 0;
    make_things();
    find_feet();
}

/* ---- drawing ---- */
static double unit;
static int sw, sh;
static double rx0, ry0, rw, rh;       /* where the current scene's picture is on the screen */

/* a picture covering the screen, cut to fit, and pushed in a little over `push` */
static void backdrop(int which, double push)
{
    double s = fmax((double)sw / sprite[which].w, (double)sh / sprite[which].h) * (1 + 0.04 * push);
    rw = sprite[which].w * s; rh = sprite[which].h * s;
    rx0 = (sw - rw) / 2; ry0 = (sh - rh) / 2 - rh * 0.012 * push;
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    SDL_FRect d = {(float)rx0, (float)ry0, (float)rw, (float)rh};
    SDL_RenderCopyF(ren, cur_tex[which], NULL, &d);
}
static void old_film(void)
{
    double f = 0.03 + 0.02 * sin(anim_t * 37) * sin(anim_t * 13);
    rect(0, 0, sw, sh, col(BG, f));
    uint64_t keep = seed_state;
    seed_state = 1 + (uint64_t)(anim_t * 24);
    for (int i = 0; i < 3; i++) {                  /* a scratch or two */
        if (frand() < 0.5) continue;
        double x = frand() * sw;
        line(x, 0, x + (frand() - 0.5) * unit, sh, unit * 0.08, col(PAPER, 0.12));
    }
    for (int i = 0; i < 40; i++) {                 /* dust */
        double x = frand() * sw, y = frand() * sh, r = unit * (0.05 + frand() * 0.12);
        disc(x, y, r, col(frand() < 0.5 ? INK : PAPER, 0.25), col(INK, 0));
    }
    seed_state = keep;
    flush(ren);
}

static void put(int which, double x, double y, double ax, double ay, double scale, double angle, double alpha, int flip)
{
    flush(ren);
    if (which < 0 || !cur_tex || !cur_tex[which] || alpha <= 0.01) return;
    SDL_FRect d = {(float)(x - ax * scale), (float)(y - ay * scale), (float)(sprite[which].w * scale), (float)(sprite[which].h * scale)};
    SDL_FPoint c = {(float)(ax * scale), (float)(ay * scale)};
    SDL_SetTextureAlphaMod(cur_tex[which], (Uint8)(fmin(alpha, 1) * 255));
    SDL_RenderCopyExF(ren, cur_tex[which], NULL, &d, angle, &c, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(cur_tex[which], 255);
}
/* ---- sound ----
 *
 * Every noise is made here when the clock starts , and played when its moment comes round: each frame, the clock's time
 * since the last frame is looked over, and any noise due in it is started. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
#undef L
#undef R
#undef C
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define RATE 22050
typedef struct { float *s; int n; } Snd;
static Snd snd_new(double secs)
{
    Snd s;
    s.n = (int)(secs * RATE);
    s.s = calloc(s.n > 0 ? s.n : 1, sizeof *s.s);
    return s;
}
static double nz(void) { return frand() * 2 - 1; }
static void snd_norm(Snd *s, double peak)
{
    double m = 1e-9;
    for (int i = 0; i < s->n; i++) m = fmax(m, fabs(s->s[i]));
    for (int i = 0; i < s->n; i++) s->s[i] *= peak / m;
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"   /* each clock uses only some of the noises */
#endif
/* a click and a short ring: the tick of a clock */
static Snd make_tick(double f, double ring)
{
    Snd s = snd_new(0.08);
    for (int i = 0; i < s.n; i++) {
        double t = (double)i / RATE;
        s.s[i] = nz() * exp(-t * 900) * 0.7 + sin(2 * M_PI * f * t) * exp(-t * ring) * 0.6;
    }
    snd_norm(&s, 1);
    return s;
}
/* a whistle sliding through the frequencies in fs (evenly in time), as a swanee
 * whistle does, with a little vibrato and breath */
static Snd make_slide(const double *fs, int nf, double dur)
{
    Snd s = snd_new(dur);
    double ph = 0;
    for (int i = 0; i < s.n; i++) {
        double t = (double)i / RATE, u = t / dur * (nf - 1);
        int k = (int)fmin(u, nf - 2);
        double fr = u - k, f = fs[k] * pow(fs[k + 1] / fs[k], fr);
        f *= 1 + 0.02 * sin(2 * M_PI * 6 * t);
        ph += 2 * M_PI * f / RATE;
        double env = fmin(1, t / 0.02) * fmin(1, (dur - t) / 0.06);
        s.s[i] = (sin(ph) + 0.15 * sin(2 * ph) + 0.04 * nz()) * env;
    }
    snd_norm(&s, 1);
    return s;
}
/* a spring: a jaw-harp twang that wobbles down */
static Snd make_boing(double base, double dur)
{
    Snd s = snd_new(dur);
    double ph = 0;
    for (int i = 0; i < s.n; i++) {
        double t = (double)i / RATE;
        double f = base * (1 + 0.7 * exp(-t * 7)) * (1 + 0.22 * sin(2 * M_PI * 13 * t) * exp(-t * 2.5));
        ph += 2 * M_PI * f / RATE;
        s.s[i] = (sin(ph) + 0.45 * sin(2 * ph) + 0.2 * sin(3 * ph)) * exp(-t * 3.2) * fmin(1, t / 0.004);
    }
    snd_norm(&s, 1);
    return s;
}
/* a cork pop */
static Snd make_pop(void)
{
    Snd s = snd_new(0.12);
    double ph = 0;
    for (int i = 0; i < s.n; i++) {
        double t = (double)i / RATE, f = 120 + 1000 * exp(-t * 40);
        ph += 2 * M_PI * f / RATE;
        s.s[i] = sin(ph) * exp(-t * 35) + nz() * exp(-t * 600) * 0.5;
    }
    snd_norm(&s, 1);
    return s;
}
/* air rushing past: noise through a band that sweeps up and down */
static Snd make_whoosh(double dur)
{
    Snd s = snd_new(dur);
    double lo = 0, hi = 0;
    for (int i = 0; i < s.n; i++) {
        double t = (double)i / RATE, q = t / dur, a = 0.02 + 0.25 * sin(M_PI * q);
        double x = nz();
        hi += (x - hi) * a; lo += (hi - lo) * a * 0.3;
        double env = sin(M_PI * q); env *= env;
        s.s[i] = (hi - lo) * env;
    }
    snd_norm(&s, 1);
    return s;
}
/* a little run of bells, for love */
static Snd make_twinkle(void)
{
    static const double NOTE[] = {2093, 2637, 3136, 4186, 3136, 4186};
    Snd s = snd_new(1.1);
    for (int k = 0; k < 6; k++) {
        int at = (int)(k * 0.09 * RATE);
        for (int i = 0; at + i < s.n; i++) {
            double t = (double)i / RATE;
            s.s[at + i] += (sin(2 * M_PI * NOTE[k] * t) + 0.3 * sin(2 * M_PI * NOTE[k] * 2.76 * t)) * exp(-t * 7) * 0.5;
        }
    }
    snd_norm(&s, 1);
    return s;
}
/* a trombone going wah, wah, wah, waaah */
static Snd make_wah(void)
{
    static const double NOTE[] = {233, 220, 208, 185}, LEN[] = {0.3, 0.3, 0.3, 1.0};
    Snd s = snd_new(2.0);
    int at = 0;
    double ph = 0;
    for (int k = 0; k < 4; k++) {
        int n = (int)(LEN[k] * RATE);
        for (int i = 0; i < n && at + i < s.n; i++) {
            double t = (double)i / RATE, u = t / LEN[k];
            double f = NOTE[k] * (k == 3 ? 1 + 0.03 * sin(2 * M_PI * 5.5 * t) * fmin(1, t * 2) : 1);
            ph += 2 * M_PI * f / RATE;
            double bright = sin(M_PI * fmin(u * 1.4, 1)), v = 0;          /* the mute opening and closing */
            for (int h = 1; h <= 9; h++) v += sin(h * ph) / h * exp(-h * (1.3 - 1.0 * bright));
            double env = fmin(1, t / 0.03) * fmin(1, (LEN[k] - t) / 0.05);
            s.s[at + i] += v * env;
        }
        at += n;
    }
    snd_norm(&s, 1);
    return s;
}
/* a bell: the partials of a church bell, struck */
static void add_bell(Snd *s, double at, double f, double gain)
{
    static const double P[] = {0.5, 1, 1.19, 1.5, 2, 2.5, 3}, A[] = {0.4, 1, 0.5, 0.35, 0.3, 0.15, 0.1};
    int a = (int)(at * RATE);
    for (int i = 0; a + i < s->n; i++) {
        double t = (double)i / RATE, v = 0;
        for (int p = 0; p < 7; p++) v += A[p] * sin(2 * M_PI * f * P[p] * t) * exp(-t * (1.2 + p * 0.9));
        s->s[a + i] += v * gain * fmin(1, t / 0.002);
    }
}
static Snd make_chime(void)
{
    Snd s = snd_new(2.6);
    add_bell(&s, 0, 659.3, 1);
    add_bell(&s, 0.55, 523.3, 1);
    snd_norm(&s, 1);
    return s;
}
/* a match struck, flaring and crackling */
static Snd make_match(void)
{
    Snd s = snd_new(0.7);
    double lo = 0;
    for (int i = 0; i < s.n; i++) {
        double t = (double)i / RATE, x = nz();
        lo += (x - lo) * 0.35;
        double strike = (x - lo) * exp(-t * 30) * 1.2;
        double flare = lo * 0.5 * exp(-pow((t - 0.12) / 0.12, 2));
        double crackle = (frand() < 0.004 ? nz() * 0.8 : 0) * exp(-t * 3);
        s.s[i] = strike + flare + crackle;
    }
    snd_norm(&s, 1);
    return s;
}
/* a soft puff of ash, or of smoke */
static Snd make_pff(double dur)
{
    Snd s = snd_new(dur);
    double lo = 0;
    for (int i = 0; i < s.n; i++) {
        double t = (double)i / RATE;
        lo += (nz() - lo) * 0.12;
        s.s[i] = lo * sin(M_PI * t / dur);
    }
    snd_norm(&s, 1);
    return s;
}
/* a recording built into the program, decoded */
static Snd decode(const unsigned char *a, const unsigned char *b)
{
    Snd s = {NULL, 0};
    int ch, rate;
    short *pcm;
    int n = stb_vorbis_decode_memory(a, (int)(b - a), &ch, &rate, &pcm);
    if (n <= 0) return snd_new(0.01);
    s = snd_new((double)n / rate);
    for (int i = 0; i < s.n; i++) {
        double src = (double)i * rate / RATE;
        int j = (int)src;
        if (j + 1 >= n) break;
        double fr = src - j;
        s.s[i] = (pcm[j * ch] * (1 - fr) + pcm[(j + 1) * ch] * fr) / 32768.0;
    }
    free(pcm);
    snd_norm(&s, 1);
    return s;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* ---- the mixer ---- */
typedef struct { const Snd *s; int pos; float gain; } Voice;
static Voice voice[24];
static SDL_AudioDeviceID audio_dev;
static int audio_on, opt_mute;
static const char *opt_wav;
static void play(const Snd *s, double gain)
{
    if (!audio_on || !s->n) return;
    if (audio_dev) SDL_LockAudioDevice(audio_dev);
    for (int i = 0; i < 24; i++)
        if (!voice[i].s) { voice[i] = (Voice){s, 0, (float)gain}; break; }
    if (audio_dev) SDL_UnlockAudioDevice(audio_dev);
}
static void mix(short *out, int n)
{
    for (int k = 0; k < n; k++) {
        float v = 0;
        for (int i = 0; i < 24; i++) {
            Voice *vc = &voice[i];
            if (!vc->s) continue;
            v += vc->s->s[vc->pos++] * vc->gain;
            if (vc->pos >= vc->s->n) vc->s = NULL;
        }
        v = tanhf(v * 0.9f);                                 /* never harsh, however many at once */
        out[k] = (short)(v * 30000);
    }
}
static void audio_cb(void *u, Uint8 *stream, int len) { (void)u; mix((short *)stream, len / 2); }
static void start_audio(void)
{
    SDL_AudioSpec want = {0}, got;
    want.freq = RATE; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = audio_cb;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!audio_dev) return;
    audio_on = 1;
    SDL_PauseAudioDevice(audio_dev, 0);
}

/* the clock's time last frame, and the noises due between then and now */
static double last_T = -1;
static void cues(double t0, double t1);
static void make_sounds(void);
void sound_step_(void)
{
    if (audio_on && last_T >= 0 && T > last_T && T - last_T < 2) cues(last_T, T);
    last_T = T;
}

/* --wav FILE: the clock's sound for --time seconds from --clock, as a WAV file, to
 * listen to without waiting for the moment */
static int write_wav(double secs)
{
    make_sounds();
    audio_on = 1;
    start_show();
    int n = (int)(secs * RATE), done = 0;
    short *buf = calloc(n, sizeof *buf);
    while (done < n) {
        update(1.0 / 60);
        int k = (int)fmin(RATE / 60, n - done);
        mix(buf + done, k);
        done += k;
    }
    FILE *f = fopen(opt_wav, "wb");
    if (!f) return 1;
    unsigned int data = n * 2, riff = 36 + data, rate = RATE, brate = RATE * 2, fmtlen = 16;
    unsigned short pcm = 1, ch = 1, align = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtlen, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(buf, 2, n, f);
    fclose(f);
    printf("%s: %.1f s\n", opt_wav, secs);
    return 0;
}


/* ---- Harpo's Coat's noises ---- */
/* a heavy thing landing: a low thump */
static Snd make_thud(double f, double dur)
{
    Snd s = snd_new(dur);
    double lo = 0;
    for (int i = 0; i < s.n; i++) {
        double t = (double)i / RATE;
        lo += (nz() - lo) * 0.08;
        s.s[i] = sin(2 * M_PI * f * t * (1 - 0.3 * t / dur)) * exp(-t * 14) + lo * 2.5 * exp(-t * 25);
    }
    snd_norm(&s, 1);
    return s;
}
/* metal things landing: a handful of pings and a rattle */
static Snd make_clatter(int hits, double dur)
{
    Snd s = snd_new(dur);
    for (int k = 0; k < hits; k++) {
        int at = (int)(pow(frand(), 1.6) * (dur - 0.15) * RATE);
        double f = 1800 + frand() * 3600, a = 0.4 + frand() * 0.6;
        for (int i = 0; at + i < s.n; i++) {
            double t = (double)i / RATE;
            s.s[at + i] += (a * sin(2 * M_PI * f * t) + 0.5 * a * sin(2 * M_PI * f * 1.53 * t)) * exp(-t * 28) + nz() * a * exp(-t * 400) * 0.6;
        }
    }
    snd_norm(&s, 1);
    return s;
}
/* an alarm clock going off: a hammer between two bells */
static Snd make_ring(double dur)
{
    Snd s = snd_new(dur);
    for (int k = 0; k * 0.045 < dur - 0.05; k++) {
        int at = (int)(k * 0.045 * RATE);
        double f = k & 1 ? 2600 : 2950;
        for (int i = 0; at + i < s.n && i < RATE * 0.2; i++) {
            double t = (double)i / RATE;
            s.s[at + i] += (sin(2 * M_PI * f * t) + 0.4 * sin(2 * M_PI * f * 2.1 * t)) * exp(-t * 30);
        }
    }
    snd_norm(&s, 1);
    return s;
}
/* a blowtorch: a roaring hiss */
static Snd make_hiss(double dur)
{
    Snd s = snd_new(dur);
    double lo = 0;
    for (int i = 0; i < s.n; i++) {
        double t = (double)i / RATE, x = nz();
        lo += (x - lo) * 0.5;
        s.s[i] = (x - lo * 0.7) * fmin(1, t / 0.08) * fmin(1, (dur - t) / 0.2) * (0.85 + 0.15 * sin(t * 90));
    }
    snd_norm(&s, 1);
    return s;
}

ART(s_honk1, "s_honk1.ogg") ART(s_honk3, "s_honk3.ogg") ART(s_quack1, "s_quack1.ogg") ART(s_quacks, "s_quacks.ogg")
static Snd rustle, pop, up_whistle, long_up, down_whistle, whoosh, boing, twinkle, thud, big_thud, clatter, soft,
           smash, ring, hiss, honk, honk2, quack, quacks;
static void make_sounds(void)
{
    uint64_t keep = seed_state;
    seed_state = 0xC0A7ULL;
    static const double UP[] = {600, 1600}, LONG[] = {400, 700, 1100, 1700, 2400}, DOWN[] = {1400, 500};
    rustle = make_whoosh(0.45); pop = make_pop();
    up_whistle = make_slide(UP, 2, 0.32); long_up = make_slide(LONG, 5, 2.4); down_whistle = make_slide(DOWN, 2, 0.6);
    whoosh = make_whoosh(0.5); boing = make_boing(140, 0.7); twinkle = make_twinkle();
    thud = make_thud(85, 0.35); big_thud = make_thud(55, 0.7); clatter = make_clatter(7, 0.6); soft = make_pff(0.25);
    smash = make_clatter(22, 1.6); ring = make_ring(1.3); hiss = make_hiss(1.1);
    honk = decode(art_s_honk1, art_s_honk1_end); honk2 = decode(art_s_honk3, art_s_honk3_end);
    quack = decode(art_s_quack1, art_s_quack1_end); quacks = decode(art_s_quacks, art_s_quacks_end);
    seed_state = keep;
}
static void cues(double t0, double t1)
{
    Moment m = moment(t1), p = moment(t0);
    double u0 = (p.round == m.round && p.turn == m.turn) ? p.u : -1, u1 = m.u;
    #define DUE(x) ((x) > u0 && (x) <= u1)
    if (m.what < 0) {                                     /* the ending */
        if (DUE(0.2)) play(&rustle, 0.4);
        if (DUE(1.7)) play(&down_whistle, 0.4);
        return;
    }
    Times tm = times_for(m.what);
    if (DUE(0.1)) play(&rustle, 0.45);
    if (DUE(tm.pull)) {
        play(&pop, 0.45);
        play(m.what == LADDER ? &long_up : &up_whistle, 0.35);
    }
    if (DUE(tm.show + 0.1)) {                             /* and its own noise, as he shows it */
        switch (m.what) {
        case DUCK: play(&quacks, 0.6); break;
        case ALARM: play(&ring, 0.35); break;
        case TORCH: play(&hiss, 0.35); break;
        case FLOWERS: case ICECREAM: case CANDLE: play(&twinkle, 0.3); break;
        case ANCHOR: case LADDER: play(&boing, 0.5); break;
        default: play(&boing, 0.25); break;
        }
    }
    if (DUE(tm.let_go)) play(&whoosh, 0.4);
    if (DUE(tm.land)) {
        switch (thing[m.what].land) {
        case L_THUD: play(m.what == ANCHOR ? &big_thud : &thud, 0.7); break;
        case L_CLATTER: play(&clatter, 0.45); break;
        case L_SOFT: play(&soft, 0.4); break;
        case L_QUACK: play(&soft, 0.3); play(&quack, 0.6); break;
        case L_CRASH: play(&smash, 0.6); play(&big_thud, 0.5); break;
        }
    }
    int after = after_for(&m);
    if (after == POSE_HONK && DUE(tm.after + 0.25)) play(hash01((uint64_t)m.round * 41 + m.turn) < 0.5 ? &honk : &honk2, 0.55);
    if (after == POSE_SHRUG && DUE(tm.after + 0.15)) play(&down_whistle, 0.3);
    #undef DUE
}

/* ---- the stage ---- */
static double hx0, hy0, hs;              /* where his feet are on the screen, and his scale */
static int pose_now;
static double thing_scale(int what) { return thing[what].size * HARPO_TALL * hs / fmax(sprite[thing[what].pic].w, sprite[thing[what].pic].h); }
/* a point of a pose, on the screen */
static void on_harpo(int pose, double px, double py, double *x, double *y)
{
    *x = hx0 + (px - foot_x[pose]) * hs;
    *y = hy0 + (py - sprite[HARPO[pose]].h) * hs;
}
static void draw_thing(int what, double x, double y, double angle, double scale)
{
    const Thing *t = &thing[what];
    put(t->pic, x, y, t->gx * sprite[t->pic].w, t->gy * sprite[t->pic].h, thing_scale(what) * scale, angle, 1, 0);
}

/* The heap, to the right of him and a step upstage (so a little smaller): each thing lands on whatever is already there, at a
 * place and a tilt of its own. It is worked out afresh from the first thing each time,
 * so it never forgets. */
#define BINS 32
#define HEAP 0.72                         /* how big things are on the heap, back there */
typedef struct { double x, y, angle; } Spot;       /* the middle of a thing on the heap, and its tilt */
static void heap_spots(const Moment *m, int upto, Spot *spot)
{
    double heap[BINS] = {0}, floor_y = hy0 - sh * 0.035, x0 = sw * 0.58, x1 = sw * 0.99, bw = (x1 - x0) / BINS;
    for (int k = 0; k < upto; k++) {
        int what = m->ord[k];
        double r = hash01((uint64_t)m->round * 53 + k), r2 = hash01((uint64_t)m->round * 59 + k);
        double s = thing_scale(what) * HEAP, w = sprite[thing[what].pic].w * s, h = sprite[thing[what].pic].h * s;
        double angle = (r2 - 0.5) * 60;
        if (what == LADDER) angle = 76;                           /* it comes down flat, in front of the heap */
        if (what == ANCHOR) angle = (r2 - 0.5) * 16;
        double a = angle * M_PI / 180, rw = fabs(w * cos(a)) + fabs(h * sin(a)), rh = fabs(w * sin(a)) + fabs(h * cos(a));
        /* it rolls to wherever the heap is lowest, of a few places it might come down */
        double x = 0, under = 1e9;
        for (int c = 0; c < 8; c++) {
            double rc = c ? hash01((uint64_t)m->round * 61 + k * 7 + c) : r;
            double xc = what == LADDER ? (x0 + x1) / 2 : x0 + rw * 0.4 + (x1 - x0 - rw * 0.8) * rc, uc = 0;
            int c0 = (int)((xc - rw * 0.4 - x0) / bw), c1 = (int)((xc + rw * 0.4 - x0) / bw);
            for (int i = c0; i <= c1; i++) if (i >= 0 && i < BINS) uc = fmax(uc, heap[i]);
            if (uc < under) { under = uc; x = xc; }
            if (what == LADDER) break;
        }
        int b0 = (int)((x - rw * 0.4 - x0) / bw), b1 = (int)((x + rw * 0.4 - x0) / bw);
        if (what == LADDER) { spot[k] = (Spot){x, floor_y - rh * 0.34, angle}; continue; }
        spot[k] = (Spot){x, floor_y - under - rh * 0.5 + rh * 0.25, angle};      /* sunk in a little */
        for (int i = b0; i <= b1; i++) if (i >= 0 && i < BINS) heap[i] = under + rh * 0.3;
    }
}
/* where his grip on a thing is, when it lies on the heap at a spot */
static void grip_on_heap(int what, const Spot *sp, double *x, double *y)
{
    double s = thing_scale(what) * HEAP, w = sprite[thing[what].pic].w * s, h = sprite[thing[what].pic].h * s;
    double dx = (thing[what].gx - 0.5) * w, dy = (thing[what].gy - 0.5) * h, a = sp->angle * M_PI / 180;
    *x = sp->x + dx * cos(a) - dy * sin(a); *y = sp->y + dx * sin(a) + dy * cos(a);
}

static void draw_scene(SDL_Renderer *r, int w, int h)
{
    ren = r; sw = w; sh = h;
    unit = (w < h ? w : h) * 0.0125;
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    backdrop(SP_stage, 0);
    rect(0, 0, sw, sh, col(BG, 0.2));
    disc(sw * 0.38, sh * 0.62, sh * 0.62, col((Rgb){255, 214, 150}, 0.18), col((Rgb){255, 214, 150}, 0));   /* the spotlight */
    flush(ren);

    hs = sh * 0.64 / HARPO_TALL;
    hx0 = sw * 0.34; hy0 = sh * 0.965;
    Moment m = moment(T);
    Spot spot[NTHING];
    heap_spots(&m, m.turn < NTHING ? m.turn + 1 : NTHING, spot);

    /* what he is doing, and what he has in his hand */
    int pose = POSE_WAIT, held = -1, flying = 0, showing = -1;
    double hold_x = 0, hold_y = 0, shown_for = 0;
    double fade = 0;
    if (m.what >= 0) {
        Times tm = times_for(m.what);
        double u = m.u;
        if (u < tm.pull) pose = POSE_REACH;
        else if (u < tm.show) {
            pose = m.what == LADDER ? POSE_HEAVE : POSE_PULL;
            held = m.what;
            /* coming up out of his coat */
            double rise = m.what == LADDER ? ease(ramp(u, tm.pull, tm.show - 0.2)) : ease(ramp(u, tm.pull, tm.pull + 0.35));
            double cx, cy, hx, hy;
            on_harpo(pose, 230, 330, &cx, &cy);
            on_harpo(pose, HAND[pose][0], HAND[pose][1], &hx, &hy);
            if (m.what == LADDER) {
                /* he heaves, and it keeps on coming */
                double len = sprite[SP_ladder].h * thing_scale(LADDER);
                hold_x = hx + sin(anim_t * 20) * unit * 0.4; hold_y = cy + len * 0.45 - len * 1.0 * rise;
            } else { hold_x = cx + (hx - cx) * rise; hold_y = cy + (hy - cy) * rise; }
            /* and once it is out, he holds it up */
            if (m.what != LADDER && u >= tm.pull + 0.35) { showing = m.what; shown_for = u - tm.pull - 0.35; held = -1; }
        }
        else if (u < tm.toss) { showing = m.what; shown_for = u - (m.what == LADDER ? tm.show : tm.pull + 0.35); }
        else if (u < tm.after) {
            pose = POSE_TOSS;
            if (u < tm.let_go) held = m.what;
        }
        else pose = after_for(&m);
        if (held >= 0 && u >= tm.show) on_harpo(pose, HAND[pose][0], HAND[pose][1], &hold_x, &hold_y);
        flying = u >= tm.let_go && u < tm.land;
    } else {
        double u = m.u;
        pose = u < 1.6 ? POSE_PEER : POSE_SHRUG;
        fade = ramp(u, ENDING - 2.0, ENDING - 0.5);
    }
    pose_now = pose;

    /* the heap: everything already thrown */
    int landed = m.turn;
    if (m.what >= 0 && m.u >= times_for(m.what).land) landed = m.turn + 1;
    /* The one that has just come down bounces and rocks till it settles, a light one
     * more than a heavy one; a heavy one shakes the whole heap and raises some dust. */
    double since = landed > m.turn && m.what >= 0 ? m.u - times_for(m.what).land : -1;
    int heavy = since >= 0 && (thing[m.what].land == L_THUD || thing[m.what].land == L_CRASH);
    for (int k = 0; k < landed && k < NTHING; k++) {
        int what = m.ord[k];
        double s = thing_scale(what) * HEAP, dy = 0, tilt = 0;
        double size = fmax(sprite[thing[what].pic].w, sprite[thing[what].pic].h) * s;
        if (k == m.turn) {
            double q = since;
            dy = -size * (heavy ? 0.1 : 0.28) * exp(-q * 5) * fabs(sin(q * 11));
            tilt = (heavy ? 5 : 16) * exp(-q * 5) * sin(q * 14);
        } else if (heavy) {
            double q = since;
            dy = unit * (m.what == LADDER ? 1.6 : 0.9) * exp(-q * 8) * sin(q * 48);
        }
        put(thing[what].pic, spot[k].x, spot[k].y + dy, sprite[thing[what].pic].w / 2.0, sprite[thing[what].pic].h / 2.0, s, spot[k].angle + tilt, 1, 0);
    }
    if (heavy && since < 0.8) {
        double q = since / 0.8, a = spot[m.turn].angle * M_PI / 180;
        double s = thing_scale(m.what) * HEAP, pw = sprite[thing[m.what].pic].w * s, ph = sprite[thing[m.what].pic].h * s;
        double rw = fabs(pw * cos(a)) + fabs(ph * sin(a)), rh = fabs(pw * sin(a)) + fabs(ph * cos(a)), bx = spot[m.turn].x, by = spot[m.turn].y + rh * 0.4;
        for (int i = 0; i < 7; i++) {
            double side = (i - 3) / 3.0, r = unit * (2.5 + 5 * q) * (1 - 0.3 * fabs(side));
            disc(bx + side * rw * (0.4 + 0.5 * ease(q)), by - unit * 2 * q * (1 - fabs(side) * 0.5), r,
                 col((Rgb){205, 185, 150}, 0.5 * (1 - q)), col((Rgb){190, 170, 140}, 0));
        }
    }
    if (showing >= 0) {
        /* him holding it up: he pops up with it, then shows it off, rocking on his heels */
        const double *at = HOLD_AT[showing];
        double pop = 1 + 0.07 * exp(-shown_for * 7) * sin(shown_for * 22);
        put(HOLDING[showing], hx0, hy0, at[0], at[1], hs * HARPO_TALL / at[2] * pop, sin(anim_t * 3.1) * 1.6, 1, 0);
    } else {
        /* him, and the thing coming out of his coat or leaving his hand */
        put(HARPO[pose], hx0, hy0, foot_x[pose], sprite[HARPO[pose]].h, hs, 0, 1, 0);
        if (held >= 0) draw_thing(held, hold_x, hold_y, 0, 1);
    }
    /* and the thing in the air, spinning over to the heap */
    if (flying) {
        Times tm = times_for(m.what);
        double q = ramp(m.u, tm.let_go, tm.land), sx, sy;
        on_harpo(POSE_TOSS, HAND[POSE_TOSS][0], HAND[POSE_TOSS][1], &sx, &sy);
        /* it flies by its middle, the way a thrown thing turns, over and over once on the way */
        const Thing *t = &thing[m.what];
        double s0 = thing_scale(m.what), sc = s0 * (1 + (HEAP - 1) * q);
        double cx = sx + (0.5 - t->gx) * sprite[t->pic].w * s0, cy = sy + (0.5 - t->gy) * sprite[t->pic].h * s0;
        const Spot *e = &spot[m.turn];
        double x = cx + (e->x - cx) * q, y = cy + (e->y - cy) * q - sh * 0.3 * 4 * q * (1 - q);
        double angle = e->angle * q + 360 * q;
        if (t->land == L_THUD || m.what == LADDER) angle = e->angle * q + (m.what == LADDER ? 12 : 70) * sin(q * M_PI);   /* heavy ones lurch over, they don't spin */
        put(t->pic, x, y, sprite[t->pic].w / 2.0, sprite[t->pic].h / 2.0, sc, angle, 1, 0);
    }
    if (fade > 0) rect(0, 0, sw, sh, col(BG, fade));
    old_film();
}

typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *scene, *tex[NSPRITE]; int w, h; } View;

static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2; /* drawn at twice the size and scaled down, for smooth edges */
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || (info.flags & SDL_RENDERER_SOFTWARE)) ss = 0;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 0;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    v->scene = NULL; v->w = ow; v->h = oh;
    if (!ss) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow * ss, oh * ss);
    if (v->scene) { v->w = ow * ss; v->h = oh * ss; }
}
static void render(View *v)
{
    if (v->scene) SDL_SetRenderTarget(v->ren, v->scene);
    cur_tex = v->tex;
    draw_scene(v->ren, v->w, v->h);
    if (v->scene) { SDL_SetRenderTarget(v->ren, NULL); SDL_RenderCopy(v->ren, v->scene, NULL, NULL); }
    SDL_RenderPresent(v->ren);
}
static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("Harpo's Coat", x, x, 1200, 750, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    make_textures(v->ren, v->tex);
    return 1;
}


static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "harpos-coat: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Texture *tex[NSPRITE];
    make_textures(r, tex);
    cur_tex = tex;
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 60) update(1.0 / 60);
    draw_scene(r, opt_w, opt_h);
    SDL_RenderPresent(r);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "harpos-coat: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Harpo's Coat",
        "Harpo's Coat screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --settings      turn the sound on or off (also the Settings button)\n"
        "  --mute          no sound\n"
        "  --wav FILE.wav  write the sound for --time seconds and exit\n"
        "  --speed X       how fast the act goes, 1 = normal\n"
        "  --seed N        repeatable sequence\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n", NULL);
}

/* ---- the one setting: sound on or off ----
 * Kept in a little file in the user's own settings folder (on Linux in
 * ~/.local/share/marx-screensavers/NAME, on Windows in AppData), and changed from the
 * screen saver settings: Windows' Settings button (/c), or screensaver-service's
 * (--settings). --mute still turns the sound off whatever it says. */
#define SAVER_NAME "harpos-coat"
#define SAVER_TITLE "Harpo's Coat"
static char *settings_file(void)
{
    char *dir = SDL_GetPrefPath("marx-screensavers", SAVER_NAME);
    if (!dir) return NULL;
    size_t n = strlen(dir) + 16;
    char *p = malloc(n);
    if (p) snprintf(p, n, "%ssettings.txt", dir);
    SDL_free(dir);
    return p;
}
static int sound_setting(void)                  /* 1: sound on (the default), 0: off */
{
    char *p = settings_file(), line[64];
    int on = 1;
    FILE *f = p ? fopen(p, "r") : NULL;
    if (f) {
        while (fgets(line, sizeof line, f))
            if (!strncmp(line, "sound=", 6)) on = line[6] != '0';
        fclose(f);
    }
    free(p);
    return on;
}
static void settings_dialog(void)
{
    int on = sound_setting();
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
        {on ? 0 : SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Sound on"},
        {on ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0, 2, "Sound off"},
    };
    const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, NULL, SAVER_TITLE,
        on ? "The sound is on." : "The sound is off.", 3, buttons, NULL};
    int choice = 0;
    if (SDL_ShowMessageBox(&box, &choice) != 0 || choice == 0) return;
    char *p = settings_file();
    FILE *f = p ? fopen(p, "w") : NULL;
    if (f) { fprintf(f, "sound=%d\n", choice == 1); fclose(f); }
    free(p);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            settings_dialog();
            return 0;
        }
        else if (!strcmp(a, "--settings")) { settings_dialog(); return 0; }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt_speed = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) opt_seed = SDL_strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--mute")) opt_mute = 1;
        else if (!strcmp(a, "--wav") && i + 1 < argc) opt_wav = argv[++i];
        else if (!strcmp(a, "--time") && i + 1 < argc) opt_time = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *x = SDL_strchr(argv[++i], 'x');
            opt_w = SDL_atoi(argv[i]); opt_h = x ? SDL_atoi(x + 1) : 0;
            if (opt_w < 64 || opt_h < 64) { usage(); return 1; }
        }
        else { usage(); return 1; }
    }
    if (opt_speed < 0.05) opt_speed = 0.05;
    if (!opt_snapshot && !sound_setting()) opt_mute = 1;
    load_sprites();

    if (opt_wav) return write_wav(opt_time);
    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO | (opt_mute ? 0 : SDL_INIT_AUDIO)) != 0) { fprintf(stderr, "harpos-coat: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "harpos-coat: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);
    start_show();
    if (!opt_mute) { make_sounds(); start_audio(); }

    Uint64 freqc = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freqc;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                if (!opt_window || e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = 0;
            } else if (!opt_window && since > 1.0) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERDOWN) running = 0;
                else if (e.type == SDL_MOUSEMOTION) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) running = 0;
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freqc;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}

