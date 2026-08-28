/*
 * chrome_layout_test.c — which titlebar button is where, per chrome style.
 *
 * Every style before the Mac ones put the same three buttons in the same place:
 * minimize, maximize, close, square, right-aligned. The Mac styles do not, and
 * they do not agree with each other either — Aqua and Tahoe put close/minimize/
 * zoom at the LEFT, Platinum puts the close box at the left and leaves collapse
 * and zoom at the right.
 *
 * The failure this guards is the worst kind a decoration can have and it is
 * SILENT: if the painter and the hit test disagree about a slot, the window
 * closes when the user aimed at minimize. Nothing crashes, nothing logs, and
 * the pixels look perfect. So the layout is one set of functions in synui.h
 * (chrome_btn_x / chrome_btn_region / chrome_btn_at) and what is asserted here
 * is that the round trip through them holds: for every style, at every pixel of
 * the bar, the button the hit test names is the button the painter drew there.
 *
 * Pure geometry — no compositor, no surface. It does need synui.h, which drags
 * in wlroots and cairo for their types; same arrangement as hit_test.c.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>

#include "synui.h"

static int failures;

#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            failures++;                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                       \
            fprintf(stderr, "\n");                              \
        }                                                       \
    } while (0)

/* A real titlebar's numbers: a 600px-wide bar 30px high, which is the shipped
 * titlebar_height and a window wide enough that no slot overlaps another. */
#define W  600
#define TH  30

static const char *rname(syn_deco_region_t r)
{
    switch (r) {
    case DECO_BTN_MIN:   return "min";
    case DECO_BTN_MAX:   return "max";
    case DECO_BTN_CLOSE: return "close";
    case DECO_TITLEBAR:  return "titlebar";
    default:             return "?";
    }
}

static syn_config_t cfg_for(syn_chrome_t chrome)
{
    syn_config_t c = {0};
    c.chrome = chrome;
    return c;
}

/* ── The three layouts, spelled out ──────────────────────── */
/* Asserted as absolute pixel positions rather than by re-deriving them, so a
 * change to the formula has to be a deliberate change to this table too. */
static void test_windows_layout(void)
{
    static const syn_chrome_t styles[] = {
        SYN_CHROME_FLAT, SYN_CHROME_LUNA, SYN_CHROME_BEVEL
    };
    for (unsigned s = 0; s < sizeof(styles) / sizeof(styles[0]); s++) {
        syn_config_t c = cfg_for(styles[s]);
        CHECK(chrome_btn_x(&c, W, TH, 0) == W - 3 * TH, "style %d slot 0", styles[s]);
        CHECK(chrome_btn_x(&c, W, TH, 1) == W - 2 * TH, "style %d slot 1", styles[s]);
        CHECK(chrome_btn_x(&c, W, TH, 2) == W - 1 * TH, "style %d slot 2", styles[s]);
        CHECK(chrome_btn_region(&c, 0) == DECO_BTN_MIN,   "style %d", styles[s]);
        CHECK(chrome_btn_region(&c, 1) == DECO_BTN_MAX,   "style %d", styles[s]);
        CHECK(chrome_btn_region(&c, 2) == DECO_BTN_CLOSE, "style %d", styles[s]);
        /* The historical answers, which no Mac preset may quietly move. */
        CHECK(chrome_btn_at(&c, W, TH, W - 1) == DECO_BTN_CLOSE,
              "style %d: the right edge closes", styles[s]);
        CHECK(chrome_btn_at(&c, W, TH, 0) == DECO_TITLEBAR,
              "style %d: the left edge is the bar, not a button", styles[s]);
    }
}

static void test_mac_left_lights(void)
{
    static const syn_chrome_t styles[] = { SYN_CHROME_LIQUID, SYN_CHROME_AQUA };
    for (unsigned s = 0; s < sizeof(styles) / sizeof(styles[0]); s++) {
        syn_config_t c = cfg_for(styles[s]);
        CHECK(chrome_btn_x(&c, W, TH, 0) == 0,      "style %d slot 0", styles[s]);
        CHECK(chrome_btn_x(&c, W, TH, 1) == TH,     "style %d slot 1", styles[s]);
        CHECK(chrome_btn_x(&c, W, TH, 2) == 2 * TH, "style %d slot 2", styles[s]);
        /* Red, yellow, green — left to right, and in that order. Getting the
         * ORDER wrong is the same class of bug as getting the side wrong. */
        CHECK(chrome_btn_at(&c, W, TH, 5)  == DECO_BTN_CLOSE, "style %d", styles[s]);
        CHECK(chrome_btn_at(&c, W, TH, 40) == DECO_BTN_MIN,   "style %d", styles[s]);
        CHECK(chrome_btn_at(&c, W, TH, 70) == DECO_BTN_MAX,   "style %d", styles[s]);
        /* And nothing at the right edge, where every other style has three. */
        CHECK(chrome_btn_at(&c, W, TH, W - 1) == DECO_TITLEBAR,
              "style %d: the right edge is bare", styles[s]);
    }
}

static void test_platinum_split(void)
{
    syn_config_t c = cfg_for(SYN_CHROME_PLATINUM);
    CHECK(chrome_btn_x(&c, W, TH, 0) == 0,          "close box hard left");
    CHECK(chrome_btn_x(&c, W, TH, 1) == W - 2 * TH, "collapse, right");
    CHECK(chrome_btn_x(&c, W, TH, 2) == W - 1 * TH, "zoom, far right");
    CHECK(chrome_btn_at(&c, W, TH, 5) == DECO_BTN_CLOSE,   "left is close");
    CHECK(chrome_btn_at(&c, W, TH, W - 45) == DECO_BTN_MIN, "collapse");
    CHECK(chrome_btn_at(&c, W, TH, W - 5) == DECO_BTN_MAX,  "zoom");
    /* The gap in the middle really is bar: this is the style where a click
     * between the two groups must drag the window, not press anything. */
    CHECK(chrome_btn_at(&c, W, TH, W / 2) == DECO_TITLEBAR, "the middle drags");
}

/* ── The round trip, at every pixel ──────────────────────── */
/* The property that actually matters, checked exhaustively rather than at
 * sampled points: whatever the hit test names at x, the painter put THERE.
 * Both directions are the same functions, so what this really pins is that the
 * cells are consistent — no slot claims a pixel it does not cover, and no
 * covered pixel answers "titlebar". */
static void test_round_trip(void)
{
    for (int style = SYN_CHROME_FLAT; style <= SYN_CHROME_PLATINUM; style++) {
        syn_config_t c = cfg_for((syn_chrome_t)style);
        for (int x = 0; x < W; x++) {
            syn_deco_region_t r = chrome_btn_at(&c, W, TH, x);
            if (r == DECO_TITLEBAR) {
                /* No slot may cover a pixel the hit test calls bare. */
                for (int i = 0; i < SYN_TITLEBAR_BTNS; i++) {
                    int bx = chrome_btn_x(&c, W, TH, i);
                    CHECK(!(x >= bx && x < bx + TH),
                          "style %d: x=%d is inside slot %d but hit-tests as bar",
                          style, x, i);
                }
                continue;
            }
            /* …and a named button has to own that pixel. */
            int found = 0;
            for (int i = 0; i < SYN_TITLEBAR_BTNS; i++) {
                int bx = chrome_btn_x(&c, W, TH, i);
                if (x >= bx && x < bx + TH && chrome_btn_region(&c, i) == r)
                    found = 1;
            }
            CHECK(found, "style %d: x=%d hit-tests as %s, drawn nowhere",
                  style, x, rname(r));
        }
    }
}

/* A window narrower than its own buttons — the two layouts degrade in opposite
 * directions, and both have to stay sane.
 *
 * Right-aligned, the slots run off the LEFT edge and overlap each other in what
 * is left, so the hit test has to answer with the one drawn LAST (the one the
 * user can actually see). Without the backwards walk it returns the button
 * underneath, which is how a click on the visible close button minimizes.
 *
 * Mac-aligned, the slots run off the RIGHT edge instead: they never overlap, and
 * the button that survives the squeeze is close — the first one, at x 0. */
static void test_narrow_window(void)
{
    int nw = TH + 4;                       /* room for one button and a sliver */

    syn_config_t win = cfg_for(SYN_CHROME_FLAT);
    CHECK(chrome_btn_at(&win, nw, TH, nw - 1) == DECO_BTN_CLOSE,
          "narrow flat window: the visible button is close");
    CHECK(chrome_btn_at(&win, nw, TH, 0) == DECO_BTN_MAX,
          "narrow flat window: the overlapped slot answers with the top one");

    syn_config_t mac = cfg_for(SYN_CHROME_LIQUID);
    CHECK(chrome_btn_at(&mac, nw, TH, 2) == DECO_BTN_CLOSE,
          "narrow mac window: close keeps its place at the left");
    CHECK(chrome_btn_at(&mac, nw, TH, TH + 2) == DECO_BTN_MIN,
          "narrow mac window: the second light, clipped but still itself");

    /* Zero-width and zero-height bars must not name a button at all — this is
     * the degenerate case view_deco_titlebar() can hand over mid-resize. */
    CHECK(chrome_btn_at(&win, 0, 0, 0) == DECO_TITLEBAR, "empty bar hits nothing");
}

/* Out-of-range slots answer DECO_NONE rather than reading off the end of the
 * table. Nothing calls it that way today; the guard is why that stays true. */
static void test_slot_bounds(void)
{
    syn_config_t c = cfg_for(SYN_CHROME_AQUA);
    CHECK(chrome_btn_region(&c, -1) == DECO_NONE, "slot -1");
    CHECK(chrome_btn_region(&c, SYN_TITLEBAR_BTNS) == DECO_NONE, "slot 3");
}

/* ── The rules the chrome overrides ──────────────────────── */
/* chrome_square() is what the radius override, the bar's square_chrome export
 * and the GTK rule pushed at self-decorating clients all read, so the three
 * cannot disagree — but only if each style is on the right side of it. */
static void test_square_and_radius(void)
{
    struct { syn_chrome_t chrome; int square; } want[] = {
        { SYN_CHROME_FLAT,     0 },
        { SYN_CHROME_LUNA,     1 },
        { SYN_CHROME_BEVEL,    1 },
        { SYN_CHROME_LIQUID,   0 },
        { SYN_CHROME_AQUA,     1 },
        { SYN_CHROME_PLATINUM, 1 },
    };
    for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        syn_config_t c = cfg_for(want[i].chrome);
        c.corner_radius = 12;
        c.shadow = 1;
        CHECK(!chrome_square(&c) == !want[i].square,
              "style %d square=%d", want[i].chrome, chrome_square(&c));
        CHECK(chrome_corner_radius(&c) == (want[i].square ? 0 : 12) ||
              want[i].chrome == SYN_CHROME_LIQUID,
              "style %d radius %d", want[i].chrome, chrome_corner_radius(&c));
    }

    /* Tahoe's floor: a smaller setting is raised to it, a bigger one is kept.
     * It is a floor and not a fixed value on purpose — the user's taste still
     * wins in the direction the theme is arguing for. */
    syn_config_t liq = cfg_for(SYN_CHROME_LIQUID);
    liq.corner_radius = 0;
    CHECK(chrome_corner_radius(&liq) == CHROME_LIQUID_RADIUS_MIN, "0 is floored");
    liq.corner_radius = 24;
    CHECK(chrome_corner_radius(&liq) == 24, "24 survives the floor");

    /* Shadows: 95 and Platinum sat flat, Aqua is the OS that made the big soft
     * shadow famous and keeps it. */
    struct { syn_chrome_t chrome; int shadow; } sh[] = {
        { SYN_CHROME_FLAT,     1 },
        { SYN_CHROME_LUNA,     1 },
        { SYN_CHROME_BEVEL,    0 },
        { SYN_CHROME_LIQUID,   1 },
        { SYN_CHROME_AQUA,     1 },
        { SYN_CHROME_PLATINUM, 0 },
    };
    for (unsigned i = 0; i < sizeof(sh) / sizeof(sh[0]); i++) {
        syn_config_t c = cfg_for(sh[i].chrome);
        c.shadow = 1;
        CHECK(chrome_shadow(&c) == sh[i].shadow,
              "style %d shadow %d", sh[i].chrome, chrome_shadow(&c));
    }
}

int main(void)
{
    test_windows_layout();
    test_mac_left_lights();
    test_platinum_split();
    test_round_trip();
    test_narrow_window();
    test_slot_bounds();
    test_square_and_radius();
    /*
     * ⛔ THE CAPTION IS CENTRED ON THE WINDOW, NOT ON THE GAP BESIDE THE
     * BUTTONS. Centring inside [lo, hi] puts the title off the centreline by
     * half the controls' width — ~40px on a 420-wide window, which is exactly
     * near enough to read as sloppy rather than as a choice.
     */
    {
        const int w = 420, th = 30;
        const double lo = SYN_TITLEBAR_BTNS * th + 8;   /* past the traffic lights */
        const double hi = w - 8;

        /* A caption that fits sits on the window's own centreline. */
        double adv = 120;
        double x = chrome_caption_x(w, lo, hi, adv);
        CHECK(x + adv / 2.0 == w / 2.0,
              "caption centre %.1f, window centre %d", x + adv / 2.0, w / 2);
        CHECK(x >= lo, "caption at %.1f runs under the buttons (lo %.1f)", x, lo);

        /* Wide enough that the true centre would overlap the buttons: it is
         * pushed clear of them rather than drawn underneath. */
        adv = hi - lo - 4;
        x = chrome_caption_x(w, lo, hi, adv);
        CHECK(x >= lo, "wide caption at %.1f is under the buttons", x);
        CHECK(x + adv <= hi, "wide caption ends at %.1f, past hi %.1f", x + adv, hi);

        /* Too wide to centre at all: left at lo for the caller to clip, which
         * is how a long title has always degraded. */
        x = chrome_caption_x(w, lo, hi, hi - lo + 50);
        CHECK(x == lo, "overlong caption starts at %.1f, expected lo %.1f", x, lo);

        /* Platinum has buttons on BOTH sides, so the window centre is already
         * inside its bounds and nothing should move. */
        const double plo = th + 6, phi = w - 2 * th - 6;
        adv = 100;
        x = chrome_caption_x(w, plo, phi, adv);
        CHECK(x + adv / 2.0 == w / 2.0,
              "platinum caption centre %.1f, window centre %d",
              x + adv / 2.0, w / 2);
    }



    if (failures) {
        fprintf(stderr, "chrome_layout_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("chrome_layout_test: all checks passed\n");
    return 0;
}
