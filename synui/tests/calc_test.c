/*
 * calc_test — the calculator (src/calc.c)
 *
 * Two halves, and they are worth testing for different reasons.
 *
 * THE EVALUATOR is the half that can be quietly wrong. A panel that fails to
 * open is a bug report in ten seconds; a parser that gets -2^2 or 2^3^2 the
 * wrong way round answers confidently, plausibly, and incorrectly, and nobody
 * checks a calculator's arithmetic by hand — that is what it is for. So the
 * precedence cases below are not decoration: they are the whole reason the
 * grammar in calc.c is shaped the way it is, and the shape is easy to "tidy"
 * into something that still compiles and still returns a number.
 *
 * THE PANEL is driven through calc_key() and calc_click() exactly as input.c's
 * chains call them, because nothing can synthesise input into a headless synui
 * — same approach as emoji_test and ctlpanel_choice_test, and the stubs are
 * theirs. The geometry the pointer tests use is written by hand with render.c's
 * own numbers, as panel_pointer_test does, since synui_render_calc() is stubbed
 * out here.
 *
 * It writes only into its own mkdtemp scratch home, so the live desktop's
 * calc.history is untouched.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "synui.h"

static int failures;

#define CHECK(cond, ...)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            failures++;                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                       \
            fprintf(stderr, "\n");                              \
        }                                                       \
    } while (0)

/* ── The compositor, stubbed ─────────────────────────────── */

static int  renders;
static char last_spawn[512];
static int  spawn_count;

void synui_render_calc(syn_server_t *s) { (void)s; renders++; }
void ctlpanel_child_closed(syn_server_t *s, const char *a) { (void)s; (void)a; }

void synui_spawn(const char *cmd)
{
    spawn_count++;
    snprintf(last_spawn, sizeof(last_spawn), "%s", cmd ? cmd : "");
}

static char scratch[256];

bool syn_config_path(char *buf, size_t n, const char *leaf)
{
    snprintf(buf, n, "%s/%s", scratch, leaf);
    return true;
}
void syn_config_ensure_dir(void) { mkdir(scratch, 0700); }

/* ── Helpers ─────────────────────────────────────────────── */

static syn_server_t *srv;

/* A panel with no memory of the previous case — including the one on DISK.
 *
 * calc_show() reloads calc.history, so zeroing the struct alone is not a fresh
 * start: every case that opened the panel would inherit the tape of the ones
 * before it, and the counts below would drift as cases were added. The one
 * place that must NOT wipe is the persistence check, which is exactly the case
 * that cares what is in the file. */
static void panel_reset_keeping_history(void)
{
    memset(&srv->calc, 0, sizeof(srv->calc));
    srv->calc.recall = -1;
    srv->calc.hover  = -1;
}

static void panel_reset(void)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/calc.history", scratch);
    unlink(path);
    panel_reset_keeping_history();
}

/* Feed a string in one keystroke at a time, as a keyboard would. The main-row
 * keysyms for printable ASCII ARE their character codes, which is what lets the
 * expressions below read as expressions. */
static void type(const char *text)
{
    for (const char *p = text; *p; p++)
        calc_key(srv, (xkb_keysym_t)(unsigned char)*p, 0);
}

/* Nothing here should ever be off by more than the last bit of a double. */
static bool near(double a, double b)
{
    return fabs(a - b) <= 1e-9 * (fabs(b) > 1.0 ? fabs(b) : 1.0);
}

static void expect_value(const char *expr, double want)
{
    double got = 0.0;
    const char *err = NULL;
    if (!calc_eval(expr, 0.0, &got, &err)) {
        failures++;
        fprintf(stderr, "FAIL %s: refused (%s)\n", expr, err ? err : "?");
        return;
    }
    CHECK(near(got, want), "%s = %.17g, expected %.17g", expr, got, want);
}

static void expect_error(const char *expr)
{
    double got = 0.0;
    const char *err = NULL;
    CHECK(!calc_eval(expr, 0.0, &got, &err), "%s should not evaluate (got %g)",
          expr, got);
    CHECK(err != NULL && err[0], "%s failed with no reason to show", expr);
}

/* ── The evaluator ───────────────────────────────────────── */

static void test_arithmetic(void)
{
    expect_value("1+1", 2);
    expect_value("7-9", -2);
    expect_value("6*7", 42);
    expect_value("1/8", 0.125);
    expect_value("7%3", 1);
    expect_value("  12  +  30  ", 42);
    expect_value(".5+.25", 0.75);
    expect_value("1e3", 1000);
}

/* The cases the grammar exists for. Every one of these is something a parser
 * can get wrong while still returning a number. */
static void test_precedence(void)
{
    expect_value("2+3*4", 14);          /* not 20 */
    expect_value("(2+3)*4", 20);
    expect_value("2*3+4*5", 26);
    expect_value("100/10/2", 5);        /* left-assoc: not 20 */
    expect_value("10-3-2", 5);          /* left-assoc: not 9 */

    /* ^ binds tighter than * and RIGHT rather than left. */
    expect_value("2*3^2", 18);          /* not 36 */
    expect_value("2^3^2", 512);         /* not 64 */

    /* The pair that pins unary above power. Both are wrong the moment the two
     * are swapped, and they are wrong in opposite directions, so no single
     * mistaken ordering satisfies both. */
    expect_value("-2^2", -4);           /* minus applies to the RESULT */
    expect_value("2^-1", 0.5);          /* minus applies to the EXPONENT */

    expect_value("--3", 3);
    expect_value("-(4-9)", 5);
}

static void test_names(void)
{
    expect_value("pi", M_PI);
    expect_value("e", M_E);
    expect_value("sqrt(16)", 4);
    expect_value("abs(-3)", 3);
    expect_value("ln(e)", 1);
    expect_value("log(1000)", 3);
    expect_value("log2(1024)", 10);
    expect_value("floor(2.7)", 2);
    expect_value("ceil(2.1)", 3);
    expect_value("round(2.5)", 3);
    expect_value("sqrt(2)^2", 2);

    /* Trigonometry is in radians, and deg()/rad() are the conversion — the
     * reason there is no mode switch. See calc.c. */
    expect_value("sin(rad(30))", 0.5);
    expect_value("deg(pi)", 180);

    /* Case-folded on the way in, so a capital typed by accident still works. */
    expect_value("SQRT(9)", 3);
    expect_value("PI", M_PI);

    /* Names are compared whole. `sine` is not `sin` with letters after it. */
    expect_error("sine(1)");

    /* Every function name the panel offers to teach must actually parse: the
     * help line is built from the same table this dispatches on, and a name in
     * one and not the other is exactly what building it from the table is
     * meant to prevent. */
    const char *hint = calc_func_hint();
    CHECK(hint && hint[0], "the function hint should not be empty");
    CHECK(strstr(hint, "sqrt") != NULL, "the hint should name sqrt");

    char name[32];
    size_t n = 0;
    for (const char *p = hint; ; p++) {
        if (*p != ' ' && *p != '\0') {
            if (n + 1 < sizeof(name)) name[n++] = *p;
            continue;
        }
        name[n] = '\0';
        if (n) {
            char call[64];
            snprintf(call, sizeof(call), "%s(1)", name);
            double v;
            const char *err = NULL;
            CHECK(calc_eval(call, 0.0, &v, &err),
                  "the hint names '%s' but it does not parse (%s)",
                  name, err ? err : "?");
        }
        n = 0;
        if (*p == '\0') break;
    }
}

static void test_ans(void)
{
    double got = 0.0;
    CHECK(calc_eval("ans*2", 21.0, &got, NULL), "ans should resolve");
    CHECK(near(got, 42), "ans*2 with ans=21 = %g", got);

    /* Unset, `ans` is zero rather than an error: the panel passes 0 until
     * something has been worked out, and "0" is a usable answer where "unknown
     * name: ans" would be a wall. */
    CHECK(calc_eval("ans+1", 0.0, &got, NULL), "ans should resolve when unset");
    CHECK(near(got, 1), "ans+1 with no answer yet = %g", got);
}

static void test_errors(void)
{
    expect_error("");
    expect_error("(1+2");
    expect_error("1/0");
    expect_error("5%0");
    expect_error("2+");
    expect_error("*3");
    expect_error("wibble");
    expect_error("sqrt 4");        /* a function needs its bracket */
    expect_error("2 3");           /* no implicit multiplication */
    expect_error("2pi");           /* …including this spelling of it */
    expect_error("$");

    /* Domain errors and overflow arrive as non-finite doubles, and a
     * calculator that answers "inf" or "nan" has handed the user something to
     * interpret instead of something to act on. */
    expect_error("sqrt(-1)");
    expect_error("ln(0)");
    expect_error("1e308*10");

    /* A failure must not smuggle a value out. */
    double got = 12345.0;
    CHECK(!calc_eval("(1+2", 0.0, &got, NULL), "an unbalanced ( should fail");
    CHECK(got == 12345.0, "a failed evaluation wrote to *out");
}

static void test_format(void)
{
    char b[CALC_RESULT_MAX];

    calc_format(42.0, b, sizeof(b));
    CHECK(strcmp(b, "42") == 0, "42 formatted as '%s'", b);

    calc_format(0.25, b, sizeof(b));
    CHECK(strcmp(b, "0.25") == 0, "0.25 formatted as '%s'", b);

    calc_format(-7.5, b, sizeof(b));
    CHECK(strcmp(b, "-7.5") == 0, "-7.5 formatted as '%s'", b);

    /* -0.0 is arithmetically correct and reads as a bug. */
    calc_format(-0.0, b, sizeof(b));
    CHECK(strcmp(b, "0") == 0, "negative zero formatted as '%s'", b);

    /* Twelve significant digits, not seventeen: 0.1+0.2 must not come out as
     * 0.30000000000000004, which is true and is not the question. */
    calc_format(0.1 + 0.2, b, sizeof(b));
    CHECK(strcmp(b, "0.3") == 0, "0.1+0.2 formatted as '%s'", b);
}

/* ── The panel ───────────────────────────────────────────── */

static void test_typing_and_enter(void)
{
    panel_reset();
    calc_show(srv);

    type("12+30");
    CHECK(strcmp(srv->calc.entry, "12+30") == 0,
          "typing built '%s'", srv->calc.entry);
    CHECK(srv->calc.entry_len == 5, "entry_len is %d", srv->calc.entry_len);

    calc_key(srv, XKB_KEY_Return, 0);
    CHECK(strcmp(srv->calc.result, "42") == 0, "12+30 gave '%s'", srv->calc.result);
    CHECK(srv->calc.has_ans, "an answer should be recorded");
    CHECK(srv->calc.entry_len == 0, "a successful sum should clear the line");

    /* …and the answer is what `ans` now means. */
    type("ans/2");
    calc_key(srv, XKB_KEY_Return, 0);
    CHECK(strcmp(srv->calc.result, "21") == 0, "ans/2 gave '%s'", srv->calc.result);

    /* Backspace, and Escape as a clear. */
    type("999");
    calc_key(srv, XKB_KEY_BackSpace, 0);
    CHECK(strcmp(srv->calc.entry, "99") == 0, "backspace left '%s'", srv->calc.entry);
    calc_key(srv, XKB_KEY_Escape, 0);
    CHECK(srv->calc.entry_len == 0, "Escape should clear a half-typed line");
    CHECK(srv->calc.visible, "the FIRST Escape should not close the panel");
    calc_key(srv, XKB_KEY_Escape, 0);
    CHECK(!srv->calc.visible, "Escape on an empty line should close the panel");
}

/* A bad expression keeps what was typed. Losing twenty correct characters to
 * punish one wrong one is the behaviour this is here to prevent. */
static void test_error_keeps_the_line(void)
{
    panel_reset();
    calc_show(srv);

    type("(1+2");
    calc_key(srv, XKB_KEY_Return, 0);

    CHECK(strcmp(srv->calc.entry, "(1+2") == 0,
          "a failed sum threw the line away (left '%s')", srv->calc.entry);
    CHECK(srv->calc.status[0], "a failed sum said nothing about why");
    CHECK(srv->calc.status_err, "a failed sum should be flagged as an error");
    CHECK(!srv->calc.has_ans, "a failed sum should not produce an answer");

    /* The next edit clears the complaint. */
    type(")");
    CHECK(!srv->calc.status[0], "typing did not clear the error line");
    calc_key(srv, XKB_KEY_Return, 0);
    CHECK(strcmp(srv->calc.result, "3") == 0, "(1+2) gave '%s'", srv->calc.result);
}

/* Newest first, no duplicates, and the tape reads oldest-to-newest downward. */
static void test_history(void)
{
    panel_reset();
    calc_show(srv);

    type("1+1");   calc_key(srv, XKB_KEY_Return, 0);
    type("2+2");   calc_key(srv, XKB_KEY_Return, 0);
    type("3+3");   calc_key(srv, XKB_KEY_Return, 0);

    CHECK(srv->calc.hist_count == 3, "history has %d entries", srv->calc.hist_count);
    CHECK(strcmp(srv->calc.hist[0].expr, "3+3") == 0,
          "newest should be first, got '%s'", srv->calc.hist[0].expr);
    CHECK(strcmp(srv->calc.hist[0].result, "6") == 0,
          "the stored answer is '%s'", srv->calc.hist[0].result);

    /* Working the same sum out again is how you CHECK it; it must not fill the
     * tape with copies. */
    type("1+1");   calc_key(srv, XKB_KEY_Return, 0);
    CHECK(srv->calc.hist_count == 3, "a repeat added a duplicate (%d entries)",
          srv->calc.hist_count);
    CHECK(strcmp(srv->calc.hist[0].expr, "1+1") == 0,
          "a repeat should move to the front, got '%s'", srv->calc.hist[0].expr);

    /* The tape: newest LAST, against the expression box. */
    const char *expr = NULL, *res = NULL;
    CHECK(calc_tape_row(srv, CALC_TAPE_ROWS - 1, &expr, &res) == 1,
          "the bottom tape row should be filled");
    CHECK(strcmp(expr, "1+1") == 0, "the bottom tape row is '%s'", expr);

    CHECK(calc_tape_row(srv, CALC_TAPE_ROWS - 2, &expr, &res) == 1,
          "the row above it should be filled");
    CHECK(strcmp(expr, "3+3") == 0, "the row above the newest is '%s'", expr);

    /* Three entries in five rows: the top two are blank, so the newest still
     * sits against the box rather than the tape floating at the top. */
    CHECK(calc_tape_row(srv, 0, &expr, &res) == 0,
          "a short history should leave the top rows empty");
}

/* Up walks back through the tape, Down walks out of it — a shell's history. */
static void test_recall(void)
{
    panel_reset();
    calc_show(srv);

    type("1+1");   calc_key(srv, XKB_KEY_Return, 0);
    type("2+2");   calc_key(srv, XKB_KEY_Return, 0);

    calc_key(srv, XKB_KEY_Up, 0);
    CHECK(strcmp(srv->calc.entry, "2+2") == 0,
          "Up should recall the newest, got '%s'", srv->calc.entry);

    calc_key(srv, XKB_KEY_Up, 0);
    CHECK(strcmp(srv->calc.entry, "1+1") == 0,
          "a second Up should go further back, got '%s'", srv->calc.entry);

    /* Past the end it stops rather than wrapping round to the newest. */
    calc_key(srv, XKB_KEY_Up, 0);
    CHECK(strcmp(srv->calc.entry, "1+1") == 0,
          "Up past the oldest wrapped or ran away ('%s')", srv->calc.entry);

    calc_key(srv, XKB_KEY_Down, 0);
    CHECK(strcmp(srv->calc.entry, "2+2") == 0,
          "Down should come back forward, got '%s'", srv->calc.entry);

    /* Down past the newest gives the empty line back: there is always a way
     * out of the walk. */
    calc_key(srv, XKB_KEY_Down, 0);
    CHECK(srv->calc.entry_len == 0, "Down past the newest left '%s'", srv->calc.entry);

    /* A recalled line can be re-run, and pushing it must not read the very slot
     * it is rotating — the trap emoji_recent_push() documents. */
    calc_key(srv, XKB_KEY_Up, 0);
    calc_key(srv, XKB_KEY_Up, 0);
    CHECK(strcmp(srv->calc.entry, "1+1") == 0, "recall lost its place");
    calc_key(srv, XKB_KEY_Return, 0);
    CHECK(strcmp(srv->calc.hist[0].expr, "1+1") == 0,
          "re-running a recalled line stored '%s'", srv->calc.hist[0].expr);
    CHECK(strcmp(srv->calc.result, "2") == 0, "re-running gave '%s'", srv->calc.result);
}

/* The tape survives a close, which is the whole reason it is a file. */
static void test_history_persists(void)
{
    panel_reset();
    calc_show(srv);
    type("111+111");
    calc_key(srv, XKB_KEY_Return, 0);
    calc_hide(srv);

    panel_reset_keeping_history();
    calc_show(srv);
    CHECK(srv->calc.hist_count >= 1, "the tape did not survive a reopen");
    CHECK(strcmp(srv->calc.hist[0].expr, "111+111") == 0,
          "the reloaded tape starts with '%s'", srv->calc.hist[0].expr);
    CHECK(strcmp(srv->calc.hist[0].result, "222") == 0,
          "the reloaded answer is '%s'", srv->calc.hist[0].result);
}

/* Ctrl+C is the one modified combo the panel claims; everything else with a
 * modifier held falls through so there is always a way out. */
static void test_copy(void)
{
    panel_reset();
    calc_show(srv);

    spawn_count = 0;
    calc_key(srv, XKB_KEY_c, WLR_MODIFIER_CTRL);
    CHECK(spawn_count == 0, "copying with no answer yet still ran something");
    CHECK(srv->calc.status_err, "copying with no answer should say so");

    type("6*7");
    calc_key(srv, XKB_KEY_Return, 0);

    spawn_count = 0;
    CHECK(calc_key(srv, XKB_KEY_c, WLR_MODIFIER_CTRL) == 1,
          "Ctrl+C should be swallowed, not passed to the window underneath");
    CHECK(spawn_count == 1, "Ctrl+C ran %d commands", spawn_count);
    CHECK(strstr(last_spawn, "wl-copy") != NULL,
          "Ctrl+C ran '%s', which does not reach the clipboard", last_spawn);
    CHECK(strstr(last_spawn, "'42'") != NULL,
          "Ctrl+C copied something other than the answer: '%s'", last_spawn);
    CHECK(!srv->calc.status_err, "a successful copy should not read as an error");

    /* Super and the other Ctrl combos are how you leave. */
    CHECK(calc_key(srv, XKB_KEY_c, WLR_MODIFIER_LOGO) == 0,
          "the panel swallowed Super+C and trapped the user in it");
    CHECK(calc_key(srv, XKB_KEY_z, WLR_MODIFIER_CTRL) == 0,
          "the panel swallowed a Ctrl combo that is not its own");
}

/* ── The pointer ─────────────────────────────────────────────
 *
 * render.c's own geometry, written by hand because synui_render_calc() is
 * stubbed here. Same approach as panel_pointer_test. */

#define PANEL_X     700
#define PANEL_Y     300
#define CALC_PAD     20
#define CELL_W       76
#define CELL_H       44
#define KEYPAD_TOP  244
#define PANEL_W  (CALC_PAD * 2 + CALC_COLS * CELL_W)
#define PANEL_H  (KEYPAD_TOP + CALC_ROWS * CELL_H + 50)

/* render.c's PANEL_CLOSE_SZ / PANEL_CLOSE_INSET. */
#define CLOSE_SZ    20
#define CLOSE_INSET 10

static void panel_place(void)
{
    hit_set_panel(&srv->calc.hit, PANEL_X, PANEL_Y, PANEL_W, PANEL_H);
    hit_set_grid(&srv->calc.hit, CALC_PAD, KEYPAD_TOP, CELL_W, CELL_H,
                 CALC_COLS, CALC_ROWS);
    hit_set_first(&srv->calc.hit, 0);
    /* Only when the panel would be drawing one, exactly as render.c does — so
     * hit_in_close answers false in click-off mode without anyone clearing it. */
    if (srv->config.panel_close == SYN_PANEL_CLOSE_BUTTON)
        hit_set_close(&srv->calc.hit, PANEL_W - CLOSE_INSET - CLOSE_SZ,
                      CLOSE_INSET, CLOSE_SZ, CLOSE_SZ);
}

static double close_x(void) { return PANEL_X + PANEL_W - CLOSE_INSET - CLOSE_SZ / 2.0; }
static double close_y(void) { return PANEL_Y + CLOSE_INSET + CLOSE_SZ / 2.0; }

/* A point in the middle of keypad cell `i`. */
static double key_x(int i)
{
    return PANEL_X + CALC_PAD + (i % CALC_COLS) * CELL_W + CELL_W / 2.0;
}
static double key_y(int i)
{
    return PANEL_Y + KEYPAD_TOP + (i / CALC_COLS) * CELL_H + CELL_H / 2.0;
}

/* Where a labelled key sits, so the cases below name buttons rather than
 * indices — the table can be rearranged without silently retargeting a test. */
static int key_index(const char *label)
{
    for (int i = 0; i < calc_button_count(); i++)
        if (strcmp(calc_button_label(i), label) == 0) return i;
    failures++;
    fprintf(stderr, "FAIL no keypad key labelled '%s'\n", label);
    return 0;
}

static void test_pointer(void)
{
    /* A shut panel must take nothing: the chain in input.c relies on that
     * answer to reach the desktop at all. */
    panel_reset();
    hit_clear(&srv->calc.hit);
    CHECK(calc_motion(srv, key_x(0), key_y(0)) == 0, "a hidden panel claimed a motion");
    CHECK(calc_click(srv, key_x(0), key_y(0), BTN_LEFT, 0) == 0,
          "a hidden panel claimed a click");
    CHECK(calc_scroll(srv, key_x(0), key_y(0), 1.0) == 0,
          "a hidden panel claimed a scroll");

    panel_reset();
    calc_show(srv);
    panel_place();

    /* Hover IS the cursor, and it clears off the keypad rather than sticking on
     * the last key crossed. */
    int seven = key_index("7");
    CHECK(calc_motion(srv, key_x(seven), key_y(seven)) == 1, "an open panel refused a motion");
    CHECK(srv->calc.hover == seven, "hover landed on %d, expected %d",
          srv->calc.hover, seven);
    CHECK(srv->calc.entry_len == 0, "hovering a key typed it");

    CHECK(calc_motion(srv, PANEL_X + 40, PANEL_Y + 10) == 1,
          "the panel refused a motion over its own header");
    CHECK(srv->calc.hover == -1, "hovering the chrome left a key looking armed");

    /* Clicking types, and it acts on where it LANDED — a tap arrives with no
     * motion before it. */
    srv->calc.hover = -1;
    int four = key_index("4");
    CHECK(calc_click(srv, key_x(four), key_y(four), BTN_LEFT, 0) == 1,
          "a click on a key was not taken");
    CHECK(strcmp(srv->calc.entry, "4") == 0,
          "clicking '4' with no prior motion left '%s'", srv->calc.entry);

    calc_click(srv, key_x(key_index("+")), key_y(key_index("+")), BTN_LEFT, 0);
    calc_click(srv, key_x(key_index("6")), key_y(key_index("6")), BTN_LEFT, 0);
    CHECK(strcmp(srv->calc.entry, "4+6") == 0, "clicking built '%s'", srv->calc.entry);

    /* The function keys bring their own bracket: a button that leaves you to
     * type "(" is a button that does most of a job. */
    calc_click(srv, key_x(key_index("sqrt")), key_y(key_index("sqrt")), BTN_LEFT, 0);
    CHECK(strcmp(srv->calc.entry, "4+6sqrt(") == 0,
          "the sqrt key inserted '%s'", srv->calc.entry);

    /* del takes one back, C takes the lot. */
    for (int i = 0; i < 5; i++)
        calc_click(srv, key_x(key_index("del")), key_y(key_index("del")), BTN_LEFT, 0);
    CHECK(strcmp(srv->calc.entry, "4+6") == 0, "del left '%s'", srv->calc.entry);

    calc_click(srv, key_x(key_index("=")), key_y(key_index("=")), BTN_LEFT, 0);
    CHECK(strcmp(srv->calc.result, "10") == 0, "the = key gave '%s'", srv->calc.result);

    /* A click on the chrome is swallowed but does nothing, and does not close
     * the panel out from under the pointer. */
    type("5");
    CHECK(calc_click(srv, PANEL_X + 40, PANEL_Y + 10, BTN_LEFT, 0) == 1,
          "a click on the header was not swallowed");
    CHECK(strcmp(srv->calc.entry, "5") == 0, "a click on the header changed the line");
    CHECK(srv->calc.visible, "a click on the header closed the panel");

    /* Right-click on a key does nothing: there is no "step it back" here, and
     * a second meaning for the second button would be one nobody could guess. */
    calc_click(srv, key_x(seven), key_y(seven), BTN_RIGHT, 0);
    CHECK(strcmp(srv->calc.entry, "5") == 0,
          "a right click on a key typed something ('%s')", srv->calc.entry);
}

/* Click off to close, on all four sides and with every button. */
static void test_click_off_closes(void)
{
    const struct { const char *where; double x, y; } outside[] = {
        { "above the panel",    PANEL_X + 100,     PANEL_Y - 1       },
        { "below the panel",    PANEL_X + 100,     PANEL_Y + PANEL_H },
        { "left of the panel",  PANEL_X - 1,       PANEL_Y + 100     },
        { "right of the panel", PANEL_X + PANEL_W, PANEL_Y + 100     },
        { "the far corner",     0,                 0                 },
    };
    const uint32_t buttons[] = { BTN_LEFT, BTN_RIGHT, BTN_MIDDLE };

    for (size_t i = 0; i < sizeof(outside) / sizeof(outside[0]); i++) {
        for (size_t b = 0; b < sizeof(buttons) / sizeof(buttons[0]); b++) {
            panel_reset();
            calc_show(srv);
            panel_place();

            CHECK(calc_click(srv, outside[i].x, outside[i].y, buttons[b], 0) == 1,
                  "a click %s was not taken", outside[i].where);
            CHECK(!srv->calc.visible, "a click %s did not close the panel (button %u)",
                  outside[i].where, buttons[b]);
        }
    }
}

/* The wheel scrolls the TAPE — the one place this panel diverges from the
 * pointer contract's "the wheel moves the selection", and the divergence is
 * only defensible if it actually stays inside the tape. */
static void test_scroll(void)
{
    panel_reset();
    calc_show(srv);
    panel_place();

    for (int i = 0; i < CALC_TAPE_ROWS + 3; i++) {
        char expr[16];
        snprintf(expr, sizeof(expr), "%d+0", i);
        type(expr);
        calc_key(srv, XKB_KEY_Return, 0);
    }
    int total = srv->calc.hist_count;
    CHECK(total == CALC_TAPE_ROWS + 3, "expected %d entries, got %d",
          CALC_TAPE_ROWS + 3, total);

    int hover_before = srv->calc.hover;
    CHECK(calc_scroll(srv, key_x(0), key_y(0), 1.0) == 1, "scroll not taken");
    CHECK(srv->calc.scroll == 1, "one notch scrolled to %d", srv->calc.scroll);
    CHECK(srv->calc.hover == hover_before, "scrolling moved the keypad selection");
    CHECK(srv->calc.entry_len == 0, "scrolling retyped the line");

    /* Stop at the ends, do not wrap. */
    for (int i = 0; i < 50; i++) calc_scroll(srv, key_x(0), key_y(0), 1.0);
    CHECK(srv->calc.scroll == total - CALC_TAPE_ROWS,
          "scrolling off the end reached %d, expected %d",
          srv->calc.scroll, total - CALC_TAPE_ROWS);

    for (int i = 0; i < 50; i++) calc_scroll(srv, key_x(0), key_y(0), -1.0);
    CHECK(srv->calc.scroll == 0, "scrolling back off the top reached %d",
          srv->calc.scroll);
}

/* ── Main ────────────────────────────────────────────────── */

/*
 * The dismissal switch (syn_panel_close_t).
 *
 * The calculator is the panel this setting exists for: a keypad is thirty small
 * targets, and in click-off mode a near-miss lands on the desktop and bins the
 * expression. So the two modes are pinned in opposite directions — click-off
 * must still close, and button mode must NOT — because a setting that silently
 * does nothing looks exactly like one that works.
 */
static void test_close_button(void)
{
    /* Click-off mode: unchanged, and still swallowed. */
    panel_reset();
    srv->config.panel_close = SYN_PANEL_CLOSE_CLICKOFF;
    calc_show(srv);
    panel_place();
    CHECK(!hit_in_close(&srv->calc.hit, close_x(), close_y()),
          "click-off mode recorded a close button");
    CHECK(calc_click(srv, PANEL_X - 40, PANEL_Y - 40, BTN_LEFT, 0) == 1,
          "a click off the panel was not taken");
    CHECK(!srv->calc.visible, "click-off mode did not close on a click off");

    /* Button mode: a click off is swallowed and changes NOTHING — the line you
     * were typing survives, which is the entire point. */
    panel_reset();
    srv->config.panel_close = SYN_PANEL_CLOSE_BUTTON;
    calc_show(srv);
    panel_place();
    type("12+3");

    CHECK(calc_click(srv, PANEL_X - 40, PANEL_Y - 40, BTN_LEFT, 0) == 1,
          "button mode did not swallow a click off the panel");
    CHECK(srv->calc.visible, "button mode closed on a click off the panel");
    CHECK(strcmp(srv->calc.entry, "12+3") == 0,
          "a click off the panel disturbed the line ('%s')", srv->calc.entry);

    /* …on every side, since "click off does nothing" that only holds below the
     * panel is not the setting doing its job. */
    const struct { const char *where; double x, y; } outside[] = {
        { "above",  PANEL_X + 100,     PANEL_Y - 1       },
        { "below",  PANEL_X + 100,     PANEL_Y + PANEL_H },
        { "left",   PANEL_X - 1,       PANEL_Y + 100     },
        { "right",  PANEL_X + PANEL_W, PANEL_Y + 100     },
    };
    for (size_t i = 0; i < sizeof(outside) / sizeof(outside[0]); i++) {
        CHECK(calc_click(srv, outside[i].x, outside[i].y, BTN_LEFT, 0) == 1,
              "a click %s was not taken", outside[i].where);
        CHECK(srv->calc.visible, "a click %s closed the panel in button mode",
              outside[i].where);
    }

    /* The button itself closes. */
    CHECK(hit_in_close(&srv->calc.hit, close_x(), close_y()),
          "the close button was not recorded in button mode");
    CHECK(calc_click(srv, close_x(), close_y(), BTN_LEFT, 0) == 1,
          "the close button did not take the click");
    CHECK(!srv->calc.visible, "the close button did not close the panel");

    /* Esc still closes in button mode — the guaranteed way out if the button
     * ever fails to draw. */
    panel_reset();
    srv->config.panel_close = SYN_PANEL_CLOSE_BUTTON;
    calc_show(srv);
    panel_place();
    calc_key(srv, XKB_KEY_Escape, 0);
    CHECK(!srv->calc.visible, "Escape did not close the panel in button mode");

    /* The button is chrome, not a key: it must not sit on a keypad cell. */
    panel_reset();
    srv->config.panel_close = SYN_PANEL_CLOSE_BUTTON;
    calc_show(srv);
    panel_place();
    CHECK(hit_index_at(&srv->calc.hit, close_x(), close_y()) < 0,
          "the close button overlaps a keypad key");

    srv->config.panel_close = SYN_PANEL_CLOSE_CLICKOFF;   /* leave it as found */
}

int main(void)
{
    char tmpl[] = "/tmp/calc_test.XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 1; }
    snprintf(scratch, sizeof(scratch), "%s", tmpl);

    srv = calloc(1, sizeof(*srv));
    if (!srv) { fprintf(stderr, "out of memory\n"); return 1; }

    test_arithmetic();
    test_precedence();
    test_names();
    test_ans();
    test_errors();
    test_format();

    test_typing_and_enter();
    test_error_keeps_the_line();
    test_history();
    test_recall();
    test_history_persists();
    test_copy();

    test_pointer();
    test_close_button();
    test_click_off_closes();
    test_scroll();

    CHECK(renders > 0, "the panel never asked to be redrawn");

    free(srv);

    char path[512];
    snprintf(path, sizeof(path), "%s/calc.history", scratch);
    unlink(path);
    rmdir(scratch);

    if (failures) {
        fprintf(stderr, "calc_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("calc_test: ok\n");
    return 0;
}
