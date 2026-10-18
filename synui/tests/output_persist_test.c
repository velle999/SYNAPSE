/*
 * output_persist_test.c — a saved monitor setting belongs to the MONITOR.
 *
 * ⛔ THE BUG. outputs.conf was keyed by connector name alone. A laptop's one
 * HDMI port had a 4K TV on it one evening, and the entry it saved said
 * `HDMI-A-1 width=3840 height=2160`. The next day a 1440p LG UltraGear went
 * into the same port: its preferred mode, 2560x1440, was committed first —
 * and output_persist_apply() then replaced it with the TV's 4K, which the LG
 * also lists (it accepts 4K input and downscales). So the desk monitor came up
 * soft, at 60Hz, every time it was plugged in, and nothing said why. From that
 * laptop's journal:
 *
 *     connector HDMI-A-1: Modesetting with 2560x1440 @ 99.946 Hz
 *     connector HDMI-A-1: Modesetting with 3840x2160 @ 60.000 Hz
 *
 * ⚠ WHY THIS IS A UNIT TEST AND NOT A RIG. The headless backend's outputs carry
 * no EDID — no make, model or serial — and the one thing the fix keys on is two
 * DIFFERENT monitors arriving on the SAME connector. So output_persist.c is
 * linked alone and the outputs are made by hand, exactly as server_new_output()
 * hands them over: the preferred mode already committed, the output not yet in
 * s->outputs.
 *
 * What it pins, none of which the build catches:
 *
 *   - a different monitor on a connector does NOT get the last one's mode
 *   - both monitors keep their own line, and each gets its own back
 *   - a line from before the change still applies (the desk keeps its
 *     layout through the upgrade) and is claimed by the monitor it lands on
 *   - a monitor with a serial that moved port keeps its settings, as ONE line
 *   - two identical serial-less monitors are told apart by their ports
 *   - a screen with no EDID is still matched by connector, and its line is not
 *     mistaken for a legacy one
 *   - primary is per monitor, not per connector name
 *   - a full table makes room by forgetting the oldest unplugged monitor
 *
 * Run as:
 *     ninja -C build output_persist_test && ./build/output_persist_test
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>

#include "synui.h"

static int failures;
static int checks;

#define CHECK(cond, ...) do {                                    \
        checks++;                                                \
        if (cond) { } else {                                     \
            failures++;                                          \
            printf("  FAIL — ");                                 \
            printf(__VA_ARGS__); printf("\n");                   \
        }                                                        \
    } while (0)

/* ── The compositor, stubbed ─────────────────────────────────
 *
 * The wlr_output_state_* setters are the real ones — they only fill a struct.
 * The two calls that would reach a backend are replaced, and the commit is
 * what moves the fake output into the mode it was asked for, so "which mode
 * is this monitor in" is read off the output the way the compositor would.
 */

bool wlr_output_test_state(struct wlr_output *o,
                           const struct wlr_output_state *st)
{
    (void)o; (void)st;
    return true;
}

bool wlr_output_commit_state(struct wlr_output *o,
                             const struct wlr_output_state *st)
{
    if (st->committed & WLR_OUTPUT_STATE_MODE) {
        if (st->mode_type == WLR_OUTPUT_STATE_MODE_FIXED && st->mode) {
            o->current_mode = st->mode;
            o->width   = st->mode->width;
            o->height  = st->mode->height;
            o->refresh = st->mode->refresh;
        } else {
            o->current_mode = NULL;
            o->width   = st->custom_mode.width;
            o->height  = st->custom_mode.height;
            o->refresh = st->custom_mode.refresh;
        }
    }
    if (st->committed & WLR_OUTPUT_STATE_SCALE)   o->scale   = st->scale;
    if (st->committed & WLR_OUTPUT_STATE_ENABLED) o->enabled = st->enabled;
    return true;
}

/* Where each output was put, so a save reads it back. */
#define FAKE_MAX 48
static struct { struct wlr_output *wo; int x, y; } placed[FAKE_MAX];
static int nplaced;
static struct wlr_output_layout_output dummy_l_output;

struct wlr_output_layout_output *wlr_output_layout_add(
        struct wlr_output_layout *layout, struct wlr_output *wo, int x, int y)
{
    (void)layout;
    for (int i = 0; i < nplaced; i++)
        if (placed[i].wo == wo) { placed[i].x = x; placed[i].y = y;
                                  return &dummy_l_output; }
    if (nplaced >= FAKE_MAX) {
        printf("  FAIL — the test's layout table is full; raise FAKE_MAX\n");
        exit(1);
    }
    placed[nplaced].wo = wo; placed[nplaced].x = x; placed[nplaced].y = y;
    nplaced++;
    return &dummy_l_output;
}

void wlr_output_layout_get_box(struct wlr_output_layout *layout,
                               struct wlr_output *wo, struct wlr_box *box)
{
    (void)layout;
    *box = (struct wlr_box){ 0, 0, wo->width, wo->height };
    for (int i = 0; i < nplaced; i++)
        if (placed[i].wo == wo) { box->x = placed[i].x; box->y = placed[i].y; }
}

bool vdisplay_is(syn_output_t *o) { (void)o; return false; }
int  hdr_white_clamp(int nits)    { return nits; }
int  dispcfg_set_deep_color(syn_server_t *s, syn_output_t *o, int enable)
{
    (void)s; o->deep_color = enable; return 1;
}
bool syn_config_path(char *buf, size_t n, const char *name)
{
    (void)name; if (n) buf[0] = '\0'; return false;
}

/* ── A desk, made by hand ────────────────────────────────── */

#define FX_MAX 1024
static void *fx_owned[FX_MAX];
static int fx_n;

static void *fx_keep(void *p)
{
    if (!p) { perror("alloc"); exit(1); }
    if (fx_n >= FX_MAX) {
        printf("  FAIL — the test's allocation table is full; raise FX_MAX\n");
        exit(1);
    }
    fx_owned[fx_n++] = p;
    return p;
}

static char conf[256];

/* A new login: a fresh desk, and outputs.conf read from disk again. */
static syn_server_t *login(const char *file_text)
{
    if (file_text) {
        FILE *f = fopen(conf, "w");
        if (!f) { perror(conf); exit(1); }
        fputs(file_text, f);
        fclose(f);
    }
    output_persist_adopt_file(conf);
    nplaced = 0;
    syn_server_t *s = fx_keep(calloc(1, sizeof(*s)));
    wl_list_init(&s->outputs);
    return s;
}

typedef struct { int w, h, r; } mode_t_;

static const mode_t_ TV_MODES[] = { {3840, 2160, 30000}, {3840, 2160, 60000},
                                    {1920, 1080, 60000}, {0, 0, 0} };
static const mode_t_ LG_MODES[] = { {2560, 1440, 99946}, {3840, 2160, 60000},
                                    {2560, 1440, 59951}, {1920, 1080, 120000},
                                    {0, 0, 0} };
static const mode_t_ DELL_MODES[] = { {1920, 1200, 59950}, {1920, 1080, 60000},
                                      {0, 0, 0} };

/* server_new_output(), as far as this file is concerned: the PREFERRED mode
 * (the first one listed) is already committed, and the output is not in
 * s->outputs yet when output_persist_apply() runs. Returns whether a saved
 * entry was applied. */
static syn_output_t *plug(syn_server_t *s, const char *conn, const char *make,
                          const char *model, const char *serial,
                          const mode_t_ *modes, int *applied)
{
    syn_output_t *o = fx_keep(calloc(1, sizeof(*o)));
    struct wlr_output *wo = fx_keep(calloc(1, sizeof(*wo)));
    wo->name   = fx_keep(strdup(conn));
    wo->make   = make   ? fx_keep(strdup(make))   : NULL;
    wo->model  = model  ? fx_keep(strdup(model))  : NULL;
    wo->serial = serial ? fx_keep(strdup(serial)) : NULL;
    wo->scale  = 1.0f;
    wo->enabled = true;
    wl_list_init(&wo->modes);
    for (const mode_t_ *m = modes; m->w; m++) {
        struct wlr_output_mode *wm = fx_keep(calloc(1, sizeof(*wm)));
        wm->width = m->w; wm->height = m->h; wm->refresh = m->r;
        wm->preferred = (m == modes);
        wl_list_insert(wo->modes.prev, &wm->link);
    }
    struct wlr_output_mode *pref =
        wl_container_of(wo->modes.next, pref, link);
    wo->current_mode = pref;
    wo->width = pref->width; wo->height = pref->height;
    wo->refresh = pref->refresh;

    o->wlr_output = wo;
    o->server = s;

    struct wlr_output_layout_output *l = output_persist_apply(s, o);
    if (applied) *applied = l != NULL;
    if (!l) wlr_output_layout_add(NULL, wo, 0, 0);   /* add_auto, roughly */
    wl_list_insert(s->outputs.prev, &o->link);
    return o;
}

static void unplug(syn_output_t *o) { wl_list_remove(&o->link); }

/* The connector's mode, as WxH@mHz. */
static const char *mode_of(syn_output_t *o)
{
    static char buf[4][48];
    static int k;
    char *b = buf[k++ & 3];
    snprintf(b, 48, "%dx%d@%d", o->wlr_output->width, o->wlr_output->height,
             o->wlr_output->refresh);
    return b;
}

/* How many `output` lines the file has, and how many mention all of `a`, `b`. */
static int lines_with(const char *a, const char *b)
{
    FILE *f = fopen(conf, "r");
    if (!f) return -1;
    char line[1024];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "output ", 7)) continue;
        if (a && !strstr(line, a)) continue;
        if (b && !strstr(line, b)) continue;
        n++;
    }
    fclose(f);
    return n;
}

static void dump(void)
{
    FILE *f = fopen(conf, "r");
    char line[1024];
    while (f && fgets(line, sizeof(line), f))
        if (!strncmp(line, "output ", 7)) printf("        %s", line);
    if (f) fclose(f);
}

#define TV   "SONY", "KD-55X80J", "6011052"
#define LG   "LG Electronics", "LG ULTRAGEAR", "000NTAB1C234"
#define TV_ID "id=SONY/KD-55X80J/6011052"
#define LG_ID "id=LG_Electronics/LG_ULTRAGEAR/000NTAB1C234"

/* ── 1. the bug: a different monitor on the same port ────── */
static void test_different_monitor_same_port(void)
{
    printf("persist: the TV's mode is not handed to the monitor after it\n");

    syn_server_t *s = login(
        "output HDMI-A-1 " TV_ID " enabled=1 width=3840 height=2160 "
        "refresh=60000 transform=0 scale=1.000000 grid_x=1 grid_y=0 x=0 y=0 "
        "primary=0 deep_color=0 hdr=0 hdr_white=203\n");

    int applied = -1;
    syn_output_t *lg = plug(s, "HDMI-A-1", LG, LG_MODES, &applied);
    CHECK(!applied, "nothing saved for the LG, but an entry was applied");
    CHECK(!strcmp(mode_of(lg), "2560x1440@99946"),
          "the LG keeps its preferred 2560x1440@99946, is at %s", mode_of(lg));

    /* The user sets it; the save keeps BOTH screens. */
    lg->wlr_output->refresh = 59951;
    output_persist_save(s);
    CHECK(lines_with("HDMI-A-1", TV_ID) == 1 &&
          lines_with(TV_ID, "width=3840") == 1,
          "the TV's line survived the LG's save");
    CHECK(lines_with("HDMI-A-1", LG_ID) == 1 &&
          lines_with(LG_ID, "refresh=59951") == 1,
          "the LG has a line of its own");
    if (failures) dump();

    /* Next login, the TV comes back on the port… */
    s = login(NULL);
    syn_output_t *tv = plug(s, "HDMI-A-1", TV, TV_MODES, &applied);
    CHECK(applied && !strcmp(mode_of(tv), "3840x2160@60000"),
          "the TV gets its own 3840x2160@60000 back, is at %s", mode_of(tv));
    unplug(tv);

    /* …and the LG after it. */
    lg = plug(s, "HDMI-A-1", LG, LG_MODES, &applied);
    CHECK(applied && !strcmp(mode_of(lg), "2560x1440@59951"),
          "the LG gets its own 2560x1440@59951 back, is at %s", mode_of(lg));
}

/* ── 2. a line from before the change ────────────────────── */
static void test_legacy_line(void)
{
    printf("persist: a line with no id still applies, once, to who is there\n");

    syn_server_t *s = login(
        "output HDMI-A-1 enabled=1 width=2560 height=1440 refresh=59951 "
        "transform=0 scale=1.250000 grid_x=1 grid_y=0 x=0 y=0 primary=1\n");

    int applied = -1;
    syn_output_t *lg = plug(s, "HDMI-A-1", LG, LG_MODES, &applied);
    CHECK(applied && !strcmp(mode_of(lg), "2560x1440@59951"),
          "the desk keeps its layout through the upgrade, LG at %s",
          mode_of(lg));
    CHECK(lg->wlr_output->scale == 1.25f, "…scale included, got %.2f",
          (double)lg->wlr_output->scale);
    CHECK(lg->primary == 1, "…and primary");

    output_persist_save(s);
    CHECK(lines_with("HDMI-A-1", NULL) == 1 && lines_with(LG_ID, NULL) == 1,
          "the legacy line was CLAIMED (one line, now with the LG's id), not "
          "duplicated");
    if (failures) dump();

    /* Claimed: a different monitor on that port no longer gets it. */
    s = login(NULL);
    syn_output_t *tv = plug(s, "HDMI-A-1", TV, TV_MODES, &applied);
    CHECK(!applied && !strcmp(mode_of(tv), "3840x2160@30000"),
          "the TV is not handed the LG's claimed line, is at %s", mode_of(tv));
}

/* ── 3. a monitor with a serial changes port ─────────────── */
static void test_moved_port(void)
{
    printf("persist: a monitor moved to another port keeps its settings\n");

    syn_server_t *s = login(
        "output HDMI-A-1 " LG_ID " enabled=1 width=2560 height=1440 "
        "refresh=59951 transform=0 scale=1.500000 grid_x=1 grid_y=0 x=0 y=0 "
        "primary=0\n");

    int applied = -1;
    syn_output_t *lg = plug(s, "DP-1", LG, LG_MODES, &applied);
    CHECK(applied && lg->wlr_output->scale == 1.5f,
          "the LG on DP-1 got its scale 1.5 from HDMI-A-1's line (%.2f)",
          (double)lg->wlr_output->scale);

    output_persist_save(s);
    CHECK(lines_with(LG_ID, NULL) == 1 && lines_with("output DP-1 ", LG_ID) == 1,
          "ONE line for the LG, and it is DP-1's now");
    if (failures) dump();
}

/* ── 4. two identical monitors with no serial ────────────── */
static void test_serial_less_twins(void)
{
    printf("persist: identical serial-less monitors are told apart by port\n");

    syn_server_t *s = login(
        "output DP-1 id=DEL/DELL_U2415/ enabled=1 width=1920 height=1200 "
        "refresh=59950 transform=0 scale=1.500000 grid_x=1 grid_y=0 x=0 y=0\n"
        "output DP-2 id=DEL/DELL_U2415/ enabled=1 width=1920 height=1080 "
        "refresh=60000 transform=0 scale=1.000000 grid_x=2 grid_y=0 x=1280 y=0\n");

    int applied = -1;
    syn_output_t *b = plug(s, "DP-2", "DEL", "DELL U2415", NULL, DELL_MODES,
                           &applied);
    CHECK(applied && !strcmp(mode_of(b), "1920x1080@60000") &&
          b->wlr_output->scale == 1.0f,
          "DP-2 got DP-2's line, not the first one with that id (%s, %.2f)",
          mode_of(b), (double)b->wlr_output->scale);

    syn_output_t *c = plug(s, "DP-3", "DEL", "DELL U2415", NULL, DELL_MODES,
                           &applied);
    CHECK(!applied && c->wlr_output->scale == 1.0f,
          "a third on DP-3 claims neither line — without a serial the port "
          "has to agree");
}

/* ── 5. clones that report the same serial ───────────────── */
static void test_cloned_serial(void)
{
    printf("persist: two monitors with the same serial do not swap lines\n");

    syn_server_t *s = login(
        "output DP-1 " LG_ID " enabled=1 width=2560 height=1440 "
        "refresh=59951 transform=0 scale=1.500000 grid_x=1 grid_y=0 x=0 y=0\n");

    int applied = -1;
    plug(s, "DP-1", LG, LG_MODES, &applied);
    CHECK(applied, "the first one is on its own port and gets its line");

    syn_output_t *twin = plug(s, "DP-2", LG, LG_MODES, &applied);
    CHECK(!applied && !strcmp(mode_of(twin), "2560x1440@99946"),
          "the clone on DP-2 does not take DP-1's line as a 'moved' monitor");
}

/* ── 6. a screen with no EDID ────────────────────────────── */
static void test_no_identity(void)
{
    printf("persist: a screen with no EDID is still matched by connector\n");

    syn_server_t *s = login(
        "output HEADLESS-1 enabled=1 width=1024 height=768 refresh=0 scale=1\n");

    int applied = -1;
    syn_output_t *h = plug(s, "HEADLESS-1", NULL, NULL, NULL, DELL_MODES,
                           &applied);
    CHECK(applied && h->wlr_output->width == 1024,
          "the rigs' seeded HEADLESS-1 line still applies (%s)", mode_of(h));

    output_persist_save(s);
    CHECK(lines_with("output HEADLESS-1 id=- ", NULL) == 1,
          "an identity-less screen is written down as id=-");

    s = login(NULL);
    plug(s, "HEADLESS-1", NULL, NULL, NULL, DELL_MODES, &applied);
    CHECK(applied, "…and read back for the same screen");

    /* id=- is a real answer, not a legacy line a monitor may claim. */
    s = login(NULL);
    syn_output_t *lg = plug(s, "HEADLESS-1", LG, LG_MODES, &applied);
    CHECK(!applied && !strcmp(mode_of(lg), "2560x1440@99946"),
          "a monitor WITH an EDID does not claim the id=- line (%s)",
          mode_of(lg));
}

/* ── 7. primary is per monitor ───────────────────────────── */
static void test_primary_per_monitor(void)
{
    printf("persist: the primary flag goes to the monitor, not the port\n");

    syn_server_t *s = login(
        "output HDMI-A-1 " TV_ID " enabled=1 width=3840 height=2160 "
        "refresh=60000 transform=0 scale=1.000000 grid_x=1 grid_y=0 x=0 y=0 "
        "primary=1\n");

    syn_output_t *lg = plug(s, "HDMI-A-1", LG, LG_MODES, NULL);
    lg->primary = 1;
    output_persist_save(s);
    CHECK(lines_with(LG_ID, "primary=1") == 1, "the LG is the primary");
    CHECK(lines_with(TV_ID, "primary=0") == 1,
          "the TV, same connector name, is not");
    if (failures) dump();
}

/* ── 8. a full table ─────────────────────────────────────── */
static void test_full_table(void)
{
    printf("persist: a full table forgets the OLDEST unplugged monitor\n");

    char text[32 * 200] = "";
    for (int i = 0; i < 32; i++) {
        char line[200];
        snprintf(line, sizeof(line),
                 "output DP-9 id=ACME/M%02d/S%02d enabled=1 width=1920 "
                 "height=1080 refresh=60000 transform=0 scale=1.000000 "
                 "grid_x=1 grid_y=0 x=0 y=0\n", i, i);
        strcat(text, line);
    }
    syn_server_t *s = login(text);

    plug(s, "HDMI-A-1", LG, LG_MODES, NULL);
    output_persist_save(s);
    CHECK(lines_with(NULL, NULL) == 32, "still 32 lines, got %d",
          lines_with(NULL, NULL));
    CHECK(lines_with(LG_ID, NULL) == 1, "the new monitor was saved");
    CHECK(lines_with("id=ACME/M00/", NULL) == 0, "the oldest went");
    CHECK(lines_with("id=ACME/M01/", NULL) == 1 &&
          lines_with("id=ACME/M31/", NULL) == 1, "the rest stayed");
}

int main(void)
{
    char dir[] = "/tmp/synui-persist.XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    snprintf(conf, sizeof(conf), "%s/outputs.conf", dir);

    test_different_monitor_same_port();
    test_legacy_line();
    test_moved_port();
    test_serial_less_twins();
    test_cloned_serial();
    test_no_identity();
    test_primary_per_monitor();
    test_full_table();

    unlink(conf);
    rmdir(dir);
    while (fx_n > 0) free(fx_owned[--fx_n]);

    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
