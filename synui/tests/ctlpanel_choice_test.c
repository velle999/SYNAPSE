/*
 * ctlpanel_choice_test.c — the AI-model row: a choice you can back out of.
 *
 * The control panel grew a row kind whose Left/Right pick a value instead of
 * changing column (CTL_KIND_CHOICE), and the only row that is one picks which
 * GGUF synapd loads. That makes it the one setting on the panel where a
 * mis-keypress costs several gigabytes of disk read and leaves the AI unable to
 * answer for as long as it takes — so the interesting behaviour is not "does it
 * load a model", it is everything that must NOT load one:
 *
 *   - Left/Right move the pick and send nothing. The request waits for the
 *     cursor to settle, or cycling past a model would load it on the way by and
 *     a key repeat would work through the whole directory.
 *   - Moving off the row, Esc, closing the panel and opening the picker each
 *     drop a pick that was still settling. Leaving is how you say no.
 *   - A settled pick sends exactly one reload, for the file the row named.
 *   - Cycling is refused while a switch is in flight — synapd would reject the
 *     second request anyway, and a queue would mean the row you let go of is not
 *     the model you end up with.
 *
 * Driven by calling ctlpanel_key() with keysyms exactly as input.c does, for the
 * reason panel_pointer_test.c gives at length: there is no way to synthesise
 * input into a headless synui, and uinput would be picked up by the LIVE
 * session. The settle timer is stepped by moving its deadline into the past
 * rather than by sleeping, so the test is deterministic and instant.
 *
 * The model directory is a handful of empty files under the build dir, pointed
 * at with -DAIMODEL_DIR the way lid_test.c redirects SYNUI_POWER_SUPPLY_DIR.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon.h>

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

/* ── The compositor, stubbed ─────────────────────────────────
 * ctlpanel.c and aimodel.c are linked alone; everything either of them reaches
 * for is a one-liner here. Only the three that the tests actually assert on
 * record anything. */

static int  renders;
static char last_action[64];         /* synui_binding_execute */
static char last_reload[128];        /* synmon_send_reload */
static int  reload_count;
static int  reload_refuse;           /* make synapd say no */

void synui_render_ctlpanel(syn_server_t *s)  { (void)s; renders++; }
void synui_render_aimodel(syn_server_t *s)   { (void)s; }
void synmon_want_refresh(syn_server_t *s)    { (void)s; }
/* aimodel.c forks the downloader; input.c owns the real one. */
void synui_child_reset_signals(void)         { }

void synui_binding_execute(syn_server_t *s, const char *action, const char *arg)
{
    (void)s; (void)arg;
    snprintf(last_action, sizeof(last_action), "%s", action ? action : "");
}

int synmon_send_reload(const char *model_name, char *out, size_t out_len)
{
    if (reload_refuse) {
        snprintf(out, out_len, "synapd: model not in its directory");
        return -1;
    }
    reload_count++;
    snprintf(last_reload, sizeof(last_reload), "%s", model_name);
    (void)out; (void)out_len;
    return 0;
}

void deco_toggle_titlebars(syn_server_t *s)          { (void)s; }
void dock_rebuild(syn_server_t *s)                   { (void)s; }
void dock_relayout(syn_server_t *s)                  { (void)s; }
void dock_state_save(syn_server_t *s)                { (void)s; }
void dock_wake(syn_server_t *s)                      { (void)s; }
void game_toggle(syn_server_t *s)                    { (void)s; }
void launcher_toggle_style(syn_server_t *s)          { (void)s; }
/* config.c is not linked here; CTL_APPLY_BINDS calls into it. */
void synui_config_apply_launcher_binds(syn_config_t *c)     { (void)c; }
void nightlight_toggle(syn_server_t *s)              { (void)s; }
void record_audio_toggle(syn_server_t *s)            { (void)s; }
void sound_state_refresh(syn_server_t *s)            { (void)s; }
void transparency_set_enabled(syn_server_t *s, int on)      { (void)s; (void)on; }
void transparency_set_opacity(syn_server_t *s, float o)     { (void)s; (void)o; }
const char *layout_label(syn_layout_t l)             { (void)l; return "stack"; }
const char *theme_name(syn_theme_t t)                { (void)t; return "gruvbox"; }
syn_workspace_t *server_active_workspace(syn_server_t *s)   { (void)s; return NULL; }
bool syn_config_path(char *buf, size_t n, const char *leaf)
{
    (void)buf; (void)n; (void)leaf;
    return false;   /* no config dir under test: widgets_label() reads "off" */
}

/* ── The table-driven rows' machinery ────────────────────────
 *
 * This test is about the AI-model CHOICE row and nothing else, but ctlpanel.c
 * now also carries the generic value path — apply hooks, the defaults snapshot
 * and settings.state — so those symbols have to resolve for it to link. None of
 * them is exercised here; the round trip they implement is what
 * ctlpanel_table_test covers, with config.c and settings.c linked for real.
 */
void uifx_apply(syn_server_t *s)              { (void)s; }
void input_reload_config(syn_server_t *s)     { (void)s; }
void deco_refresh_all(syn_server_t *s)        { (void)s; }
void nightlight_apply(syn_server_t *s)        { (void)s; }
void cursor_reload(syn_server_t *s)           { (void)s; }
void deskicons_reload(syn_server_t *s)        { (void)s; }
void layout_apply(syn_server_t *s, syn_workspace_t *ws) { (void)s; (void)ws; }

void settings_state_set(const char *k, const char *v) { (void)k; (void)v; }
void settings_state_clear(const char *k)              { (void)k; }
int  settings_state_has(const char *k)                { (void)k; return 0; }

/* Zeroed, not the real defaults: nothing here reads a value out of it, and
 * building one would mean linking config.c, whose syn_config_path would fight
 * the stub above. */
const syn_config_t *synui_config_defaults(void)
{
    static syn_config_t def;
    return &def;
}

/* ── The model directory ─────────────────────────────────── */

static const char *const models[] = {
    "alpha.gguf",          /* sorted: alpha, mistral, zephyr */
    "mistral.gguf",
    "zephyr.gguf",
};
#define MODEL_COUNT ((int)(sizeof(models) / sizeof(models[0])))

static void models_write(void)
{
    mkdir(AIMODEL_DIR, 0755);
    for (int i = 0; i < MODEL_COUNT; i++) {
        char p[512];
        snprintf(p, sizeof(p), "%s/%s", AIMODEL_DIR, models[i]);
        FILE *f = fopen(p, "w");
        if (f) { fputs("not really a gguf", f); fclose(f); }
    }
}

static void models_clear(void)
{
    for (int i = 0; i < MODEL_COUNT; i++) {
        char p[512];
        snprintf(p, sizeof(p), "%s/%s", AIMODEL_DIR, models[i]);
        unlink(p);
    }
}

/* ── The panel, on the AI-model row ──────────────────────── */

/* What synapd would be reporting: mistral.gguf resident and settled. */
static void daemon_says_loaded(syn_server_t *s, const char *file)
{
    s->overlay.mon_online = 1;
    snprintf(s->overlay.model, sizeof(s->overlay.model), "loaded");
    snprintf(s->overlay.model_file, sizeof(s->overlay.model_file), "%s", file);
}

/* Put the cursor on the AI-model row. Found by walking the item table rather
 * than by index: a row added to the System category above it would silently
 * move this test onto some other setting. */
static void open_on_model_row(syn_server_t *s)
{
    ctlpanel_show_cat(s, "System");

    int rows[CTL_CAT_ITEMS_MAX];
    int n = ctlpanel_cat_items(CTL_CAT_SYSTEM, rows, CTL_CAT_ITEMS_MAX);
    int at = -1;
    for (int i = 0; i < n; i++) if (rows[i] == CTL_ROW_AI_MODEL) at = i;
    CHECK(at >= 0, "no AI model row in the System category");
    s->ctlpanel.item = at < 0 ? 0 : at;
}

static void row_value(syn_server_t *s, char *buf, size_t n)
{
    ctlpanel_row_value(s, CTL_ROW_AI_MODEL, buf, n);
}

/* Step the settle timer without waiting for it: any deadline in the past does,
 * and CLOCK_MONOTONIC is never below 1.0 on a machine that has finished
 * booting. */
static void settle_now(syn_server_t *s) { s->ctlpanel.model_commit_at = 1.0; }

static void key(syn_server_t *s, xkb_keysym_t sym) { ctlpanel_key(s, sym, 0); }

static syn_server_t *server_new(void)
{
    syn_server_t *s = calloc(1, sizeof(*s));
    if (!s) { fprintf(stderr, "out of memory\n"); exit(1); }
    wl_list_init(&s->outputs);
    reload_count  = 0;
    reload_refuse = 0;
    last_reload[0] = last_action[0] = '\0';
    return s;
}

int main(void)
{
    char v[64];

    models_write();

    /* ── The row reports what the DAEMON says, not the first file ── */
    {
        syn_server_t *s = server_new();
        daemon_says_loaded(s, "mistral.gguf");
        open_on_model_row(s);

        CHECK(ctlpanel_row_kind(CTL_ROW_AI_MODEL) == CTL_KIND_CHOICE,
              "AI model row is not a CHOICE row");

        row_value(s, v, sizeof(v));
        CHECK(strcmp(v, "mistral") == 0,
              "row opened on '%s', expected the loaded model with .gguf dropped", v);
        free(s);
    }

    /* ── Left/Right move the pick and send NOTHING yet ── */
    {
        syn_server_t *s = server_new();
        daemon_says_loaded(s, "mistral.gguf");
        open_on_model_row(s);

        key(s, XKB_KEY_Right);
        row_value(s, v, sizeof(v));
        CHECK(strcmp(v, "zephyr") == 0, "Right went to '%s', expected zephyr", v);
        CHECK(reload_count == 0, "Right sent a reload before the pick settled");
        CHECK(s->ctlpanel.model_commit_at != 0.0, "Right armed no settle timer");
        CHECK(s->ctlpanel.focus == CTL_FOCUS_ITEMS,
              "Right left the row pane — a choice row must keep the column");

        /* Cycling on past the end wraps, and STILL sends nothing: this is the
         * case that would have loaded every model in the directory. */
        key(s, XKB_KEY_Right);
        row_value(s, v, sizeof(v));
        CHECK(strcmp(v, "alpha") == 0, "Right wrapped to '%s', expected alpha", v);
        key(s, XKB_KEY_Left);
        row_value(s, v, sizeof(v));
        CHECK(strcmp(v, "zephyr") == 0, "Left went to '%s', expected zephyr", v);
        CHECK(reload_count == 0, "cycling sent %d reloads, expected none",
              reload_count);
        free(s);
    }

    /* ── A settled pick sends exactly one reload, for the file it named ── */
    {
        syn_server_t *s = server_new();
        daemon_says_loaded(s, "mistral.gguf");
        open_on_model_row(s);

        key(s, XKB_KEY_Right);          /* → zephyr */
        CHECK(ctlpanel_tick(s) == 1, "tick did not ask for frames while settling");
        CHECK(reload_count == 0, "tick loaded before the deadline");

        settle_now(s);
        ctlpanel_tick(s);
        CHECK(reload_count == 1, "settled pick sent %d reloads, expected 1",
              reload_count);
        CHECK(strcmp(last_reload, "zephyr.gguf") == 0,
              "asked synapd for '%s', expected zephyr.gguf", last_reload);
        CHECK(s->ctlpanel.model_commit_at == 0.0,
              "the settle timer is still armed after firing");

        /* Bare filename, never a path: synapd refuses anything with a '/' and
         * this is the only caller that could build one. */
        CHECK(strchr(last_reload, '/') == NULL,
              "sent a path ('%s') where synapd wants a filename", last_reload);

        /* In flight now, and the row says so. */
        row_value(s, v, sizeof(v));
        CHECK(strstr(v, "loading") != NULL,
              "row reads '%s' mid-switch, expected it to say loading", v);

        /* A second pick is refused while the first is loading. */
        key(s, XKB_KEY_Right);
        CHECK(reload_count == 1, "a second reload went out mid-switch");
        CHECK(s->ctlpanel.model_commit_at == 0.0,
              "a refused cycle armed the settle timer anyway");

        /* The daemon confirms; the row settles on what it asked for. */
        daemon_says_loaded(s, "zephyr.gguf");
        aimodel_status_changed(s);
        row_value(s, v, sizeof(v));
        CHECK(strcmp(v, "zephyr") == 0,
              "after the switch landed the row reads '%s', expected zephyr", v);
        free(s);
    }

    /* ── Everything that must NOT load: leaving the row ── */
    {
        /* Down, onto the next row. */
        syn_server_t *s = server_new();
        daemon_says_loaded(s, "mistral.gguf");
        open_on_model_row(s);
        key(s, XKB_KEY_Right);
        key(s, XKB_KEY_Down);
        CHECK(s->ctlpanel.model_commit_at == 0.0, "Down left the pick armed");
        settle_now(s);
        ctlpanel_tick(s);
        CHECK(reload_count == 0, "a pick fired after the cursor left the row");
        free(s);

        /* Esc, back to the category column. */
        s = server_new();
        daemon_says_loaded(s, "mistral.gguf");
        open_on_model_row(s);
        key(s, XKB_KEY_Right);
        key(s, XKB_KEY_Escape);
        CHECK(s->ctlpanel.model_commit_at == 0.0, "Esc left the pick armed");
        row_value(s, v, sizeof(v));
        CHECK(strcmp(v, "mistral") == 0,
              "Esc left the row naming '%s', expected the loaded model back", v);
        settle_now(s);
        ctlpanel_tick(s);
        CHECK(reload_count == 0, "a pick fired after Esc");
        free(s);

        /* Closing the panel outright. */
        s = server_new();
        daemon_says_loaded(s, "mistral.gguf");
        open_on_model_row(s);
        key(s, XKB_KEY_Right);
        ctlpanel_hide(s);
        CHECK(s->ctlpanel.model_commit_at == 0.0, "hiding left the pick armed");
        settle_now(s);
        CHECK(ctlpanel_tick(s) == 0, "tick still working for a hidden panel");
        CHECK(reload_count == 0, "a pick fired after the panel closed");
        free(s);

        /* Enter opens the picker, and drops the pick rather than loading it on
         * the way: the panel is where you go to see what a model IS. */
        s = server_new();
        daemon_says_loaded(s, "mistral.gguf");
        open_on_model_row(s);
        key(s, XKB_KEY_Right);
        key(s, XKB_KEY_Return);
        CHECK(strcmp(last_action, "aimodel") == 0,
              "Enter fired '%s', expected the aimodel panel", last_action);
        CHECK(s->ctlpanel.model_commit_at == 0.0, "Enter left the pick armed");
        settle_now(s);
        ctlpanel_tick(s);
        CHECK(reload_count == 0, "a pick fired when the picker was opened");
        free(s);
    }

    /* ── synapd's refusal reaches the status line verbatim ── */
    {
        syn_server_t *s = server_new();
        daemon_says_loaded(s, "mistral.gguf");
        open_on_model_row(s);
        reload_refuse = 1;

        key(s, XKB_KEY_Right);
        settle_now(s);
        ctlpanel_tick(s);
        CHECK(strstr(s->ctlpanel.status, "not in its directory") != NULL,
              "status reads '%s', expected synapd's own words", s->ctlpanel.status);
        free(s);
    }

    /* ── An empty directory is a row you cannot get stuck in ── */
    {
        models_clear();
        syn_server_t *s = server_new();
        open_on_model_row(s);

        row_value(s, v, sizeof(v));
        CHECK(strcmp(v, "none") == 0, "empty directory reads '%s', expected none", v);

        key(s, XKB_KEY_Right);
        CHECK(reload_count == 0, "cycling an empty list sent a reload");
        CHECK(s->ctlpanel.model_commit_at == 0.0,
              "cycling an empty list armed the settle timer");
        CHECK(s->ctlpanel.status[0] != '\0',
              "cycling an empty list said nothing at all");
        free(s);
        models_write();
    }

    /*
     * ── A switch that FAILS must end ────────────────────────────────────
     *
     * The case velle hit: a model downloaded through the picker that this
     * build of llama.cpp cannot load. synapd restores the previous model, so
     * every field it reports goes back to exactly what it said before the
     * request — and settling only ever tested for SUCCESS, so `switching`
     * stayed set forever. The row sat on "loading …" and refused every later
     * pick with "still loading · wait for it to finish", with nothing to wait
     * for. Two ways out, and both must work: synapd was seen mid-load, or
     * enough time passed that an unchanged daemon can only mean it is over.
     */
    for (int variant = 0; variant < 2; variant++) {
        const int via_loading = (variant == 0);

        models_clear();
        models_write();
        syn_server_t *s = server_new();
        daemon_says_loaded(s, "mistral.gguf");
        open_on_model_row(s);

        /* Pick something else and let the settle timer fire, exactly as the
         * working cases above do. */
        key(s, XKB_KEY_Right);
        s->ctlpanel.model_commit_at = -1.0;
        ctlpanel_tick(s);
        CHECK(s->aimodel.switching == 1, "[%d] no switch in flight", variant);

        const char *asked = last_reload;

        if (via_loading) {
            /* synapd takes the write lock. */
            snprintf(s->overlay.model, sizeof(s->overlay.model), "loading");
            aimodel_status_changed(s);
            CHECK(s->aimodel.switching == 1,
                  "[%d] gave up while synapd was still loading", variant);
        } else {
            /* Never observed mid-load — a small model can finish between two
             * one-second polls. The clock is what ends it instead. */
            s->aimodel.switch_at -= 60.0;
        }

        /* The load failed: synapd is back on the old model and says why. */
        daemon_says_loaded(s, "mistral.gguf");
        snprintf(s->overlay.switch_file, sizeof(s->overlay.switch_file),
                 "%s", asked);
        snprintf(s->overlay.switch_err, sizeof(s->overlay.switch_err),
                 "unknown pre-tokenizer type: 'minicpm5'");
        aimodel_status_changed(s);

        CHECK(s->aimodel.switching == 0,
              "[%d] a failed switch left the row stuck on 'loading'", variant);
        CHECK(strstr(s->aimodel.status, "minicpm5") != NULL,
              "[%d] the failure said '%s' — llama's reason never reached the "
              "user", variant, s->aimodel.status);

        /* And the row is usable again, which is the point of all of it. */
        reload_count = 0;
        key(s, XKB_KEY_Right);
        s->ctlpanel.model_commit_at = -1.0;
        ctlpanel_tick(s);
        CHECK(reload_count == 1,
              "[%d] the row refused the next pick after a failure", variant);

        free(s);
    }

    /*
     * The pre-lock window is NOT a failure. synapd answers the reload before
     * it takes the write lock, so for a moment it still reports the old model
     * as loaded — and reading that as "it failed" would report every switch as
     * broken a fraction of a second before it worked.
     */
    {
        models_clear();
        models_write();
        syn_server_t *s = server_new();
        daemon_says_loaded(s, "mistral.gguf");
        open_on_model_row(s);

        key(s, XKB_KEY_Right);
        s->ctlpanel.model_commit_at = -1.0;
        ctlpanel_tick(s);

        daemon_says_loaded(s, "mistral.gguf");   /* not started yet */
        aimodel_status_changed(s);
        CHECK(s->aimodel.switching == 1,
              "called a switch failed before synapd had begun it");
        free(s);
    }

    models_clear();
    rmdir(AIMODEL_DIR);

    if (failures) {
        fprintf(stderr, "ctlpanel_choice_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("ctlpanel_choice_test: OK\n");
    return 0;
}
