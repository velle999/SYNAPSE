/*
 * eq.c — the equalizer panel (Control panel ▸ Sound ▸ Equalizer)
 *
 * A 10-band graphic equalizer: an on/off row, a preset, a preamp and one row
 * per band. The DSP is not here — synui-eq(1) owns the PipeWire filter chain,
 * the state file and every decision about what an equalizer is. This panel
 * reads that state and asks the script to change it, exactly as the Super+S
 * sound panel does with synui-sound.
 *
 * WHY THE PANEL NEVER RUNS THE SCRIPT SYNCHRONOUSLY
 *
 * It reads ~/.config/synui/eq.state directly and never popen()s `synui-eq
 * status`. A synchronous popen() on a draw or key path would block the
 * wl_event_loop, which is the one thing no panel here is allowed to do — the
 * cursor picker's header says the same and for the same reason. Changes go out
 * as a fire-and-forget synui_spawn().
 *
 * THE OPTIMISTIC CACHE, AND THE TRAP IN IT
 *
 * Because the spawn is async, the panel keeps its own copy of the values and
 * draws from that: re-reading the file straight after a write would draw the
 * PREVIOUS value under the cursor and the slider would appear to lag a step
 * behind. The trap synui-sound already hit is that a copy left stale by someone
 * running `synui-eq` in a terminal gets written back over the newer value. So
 * the cache is refreshed by MTIME at the top of the key handler — before acting,
 * never after — and the mtime is deliberately NOT zeroed after our own write:
 * an unchanged mtime means the child has not landed yet and the optimistic
 * value is the better answer.
 *
 * THE FREQUENCIES COME FROM THE STATE FILE
 *
 * Not from a table here. synui-eq writes `freqs=31,62,…` precisely so this
 * panel can label its rows without carrying a second copy that would be free to
 * drift the day a band moves — and the symptom of that drift is a row labelled
 * 500 Hz that adjusts 250, which nobody would spot by reading either file.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

#include <wlr/util/log.h>

#include "synui.h"

/* Must match synui-eq's clamp. Duplicated because the panel has to stop the
 * cursor at the ends of the range rather than sending values the script will
 * silently clamp — a slider that keeps moving while the number stays put reads
 * as a broken slider. */
#define EQ_GAIN_MIN (-12)
#define EQ_GAIN_MAX  ( 12)

/* Row layout. Fixed order; the render walks it and so does the key handler. */
enum {
    EQ_ROW_ENABLED = 0,
    EQ_ROW_PRESET,
    EQ_ROW_PREAMP,
    EQ_ROW_FIRST_BAND,
};

/* Preset names, in the order synui-eq lists them. One of the few things
 * genuinely duplicated between the two — the script cannot be asked without a
 * blocking popen, and the panel has to draw the name before any spawn returns.
 * `custom` is not offered: it is what the script REPORTS once a band has been
 * moved by hand, not something you can select. */
static const char *const eq_preset_names[] = {
    "flat", "bass", "treble", "vocal", "rock", "classical", "loudness",
};
#define EQ_PRESET_COUNT ((int)(sizeof(eq_preset_names) / sizeof(eq_preset_names[0])))

/* ── State ───────────────────────────────────────────────── */

static bool eq_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "eq.state");
}

/* Re-read eq.state if it has changed on disk since we last looked.
 *
 * Returns without touching anything when the mtime is unchanged, which is what
 * protects the optimistic cache — see the header. */
void eq_state_refresh(syn_server_t *s)
{
    char path[256];
    if (!eq_state_path(path, sizeof(path))) return;

    /* OPEN FIRST, THEN ASK THE DESCRIPTOR — stat(path) then fopen(path) is one
     * name resolved twice, and the mtime this cache turns on would belong to
     * whichever file the name meant first, not to the bands actually read. */
    FILE *f = fopen(path, "r");
    if (!f) {
        /* No file yet: the script has never run. Defaults, and a zero mtime so
         * the first real write is seen. */
        if (s->eq.mtime != 0) {
            s->eq.mtime = 0;
            s->eq.enabled = 0;
            s->eq.preamp = 0;
            snprintf(s->eq.preset, sizeof(s->eq.preset), "flat");
            for (int i = 0; i < EQ_BANDS; i++) s->eq.gain[i] = 0;
        }
        return;
    }

    struct stat st;
    if (fstat(fileno(f), &st) != 0) { fclose(f); return; }
    if (st.st_mtime == s->eq.mtime) { fclose(f); return; }
    s->eq.mtime = st.st_mtime;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *k = line, *v = eq + 1;

        if (strcmp(k, "enabled") == 0) {
            s->eq.enabled = (strcmp(v, "on") == 0);
        } else if (strcmp(k, "preset") == 0) {
            snprintf(s->eq.preset, sizeof(s->eq.preset), "%s", v);
        } else if (strcmp(k, "preamp") == 0) {
            s->eq.preamp = atoi(v);
        } else if (strcmp(k, "freqs") == 0) {
            /* The band labels, straight from the script's own table. */
            int n = 0;
            for (const char *p = v; *p && n < EQ_BANDS; ) {
                s->eq.freq[n++] = atoi(p);
                const char *c = strchr(p, ',');
                if (!c) break;
                p = c + 1;
            }
            s->eq.freq_count = n;
        } else if (strncmp(k, "band", 4) == 0) {
            int n = atoi(k + 4);
            if (n >= 1 && n <= EQ_BANDS) s->eq.gain[n - 1] = atoi(v);
        }
    }
    fclose(f);
}

int eq_total_rows(syn_server_t *s)
{
    /* However many bands the script actually declared. Before the state file
     * exists there are none, and the panel is then three rows that still work —
     * turning it on writes the file and the bands appear. */
    return EQ_ROW_FIRST_BAND + (s->eq.freq_count > 0 ? s->eq.freq_count : 0);
}

/* Row label + the value shown on the right. render.c draws; the text lives here
 * so the panel and the state cannot drift apart. */
void eq_row(syn_server_t *s, int row, char *label, size_t ln, char *value, size_t vn)
{
    label[0] = value[0] = '\0';

    switch (row) {
    case EQ_ROW_ENABLED:
        snprintf(label, ln, "Equalizer");
        snprintf(value, vn, "%s", s->eq.enabled ? "on" : "off");
        return;
    case EQ_ROW_PRESET:
        snprintf(label, ln, "Preset");
        snprintf(value, vn, "%s", s->eq.preset);
        return;
    case EQ_ROW_PREAMP:
        snprintf(label, ln, "Preamp");
        snprintf(value, vn, "%+d dB", s->eq.preamp);
        return;
    default:
        break;
    }

    int b = row - EQ_ROW_FIRST_BAND;
    if (b < 0 || b >= s->eq.freq_count) return;

    /* kHz above 1000, because "16000 Hz" in a narrow column is four characters
     * of noise for one of meaning. */
    if (s->eq.freq[b] >= 1000)
        snprintf(label, ln, "%g kHz", s->eq.freq[b] / 1000.0);
    else
        snprintf(label, ln, "%d Hz", s->eq.freq[b]);
    snprintf(value, vn, "%+d dB", s->eq.gain[b]);
}

/* Gain of a band row as a 0..1 fraction, for the bar render.c draws. */
double eq_row_frac(syn_server_t *s, int row)
{
    int b = row - EQ_ROW_FIRST_BAND;
    int v;
    if (row == EQ_ROW_PREAMP)          v = s->eq.preamp;
    else if (b >= 0 && b < s->eq.freq_count) v = s->eq.gain[b];
    else return -1.0;   /* not a slider row */

    return (double)(v - EQ_GAIN_MIN) / (double)(EQ_GAIN_MAX - EQ_GAIN_MIN);
}

/* ── Show / hide ─────────────────────────────────────────── */

void eq_show(syn_server_t *s)
{
    /* Force a re-read: the file may have changed since the panel last closed,
     * and opening onto stale values is exactly the disagreement this panel
     * must not have with the script. */
    s->eq.mtime = 0;
    eq_state_refresh(s);

    s->eq.visible  = 1;
    s->eq.selected = 0;
    s->eq.scroll   = 0;
    synui_render_eq(s);
}

void eq_hide(syn_server_t *s)
{
    s->eq.visible = 0;
    synui_render_eq(s);
    ctlpanel_child_closed(s, "equalizer");
}

void eq_toggle(syn_server_t *s)
{
    if (s->eq.visible) eq_hide(s);
    else               eq_show(s);
}

/* ── Acting ──────────────────────────────────────────────── */

static void eq_run(const char *fmt, ...)
{
    char cmd[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    synui_spawn(cmd);
}

/* Adjust the row under the cursor by `step` dB (or toggle / cycle it).
 *
 * The cache is updated here rather than waiting for the script, so the panel
 * redraws instantly; the mtime is left alone so the next refresh still sees the
 * child's write when it lands. See the header. */
static void eq_adjust(syn_server_t *s, int row, int step)
{
    switch (row) {
    case EQ_ROW_ENABLED:
        s->eq.enabled = !s->eq.enabled;
        eq_run("synui-eq %s", s->eq.enabled ? "on" : "off");
        return;

    case EQ_ROW_PRESET: {
        /* Find where we are in the list. A preset of "custom" is not in it, so
         * stepping from custom lands on the first or last entry rather than
         * refusing to move. */
        int cur = -1;
        for (int i = 0; i < EQ_PRESET_COUNT; i++)
            if (strcmp(eq_preset_names[i], s->eq.preset) == 0) { cur = i; break; }

        int next;
        if (cur < 0) next = step > 0 ? 0 : EQ_PRESET_COUNT - 1;
        else         next = (cur + step + EQ_PRESET_COUNT) % EQ_PRESET_COUNT;

        snprintf(s->eq.preset, sizeof(s->eq.preset), "%s", eq_preset_names[next]);
        eq_run("synui-eq preset %s", eq_preset_names[next]);
        /* The preset rewrites every band, and this panel cannot know the curve
         * without duplicating the table. Drop the mtime so the next refresh
         * re-reads the file the script is about to write — the one case where
         * the optimistic cache genuinely cannot answer. */
        s->eq.mtime = 0;
        return;
    }

    case EQ_ROW_PREAMP: {
        int v = s->eq.preamp + step;
        if (v < EQ_GAIN_MIN) v = EQ_GAIN_MIN;
        if (v > EQ_GAIN_MAX) v = EQ_GAIN_MAX;
        if (v == s->eq.preamp) return;
        s->eq.preamp = v;
        eq_run("synui-eq preamp %d", v);
        return;
    }

    default: break;
    }

    int b = row - EQ_ROW_FIRST_BAND;
    if (b < 0 || b >= s->eq.freq_count) return;

    int v = s->eq.gain[b] + step;
    if (v < EQ_GAIN_MIN) v = EQ_GAIN_MIN;
    if (v > EQ_GAIN_MAX) v = EQ_GAIN_MAX;
    if (v == s->eq.gain[b]) return;

    s->eq.gain[b] = v;
    /* Moving a band makes the curve no longer the preset it came from, and the
     * script says so in the state file. Reflected immediately so the label does
     * not keep claiming "rock" until the child lands. */
    snprintf(s->eq.preset, sizeof(s->eq.preset), "custom");
    eq_run("synui-eq band %d %d", b + 1, v);
}

/* ── Pointer ─────────────────────────────────────────────── */

int eq_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->eq.visible) return 0;
    int i = hit_index_at(&s->eq.hit, lx, ly);
    if (i >= 0 && i < eq_total_rows(s) && i != s->eq.selected) {
        s->eq.selected = i;
        synui_render_eq(s);
    }
    return 1;
}

int eq_click(syn_server_t *s, double lx, double ly, uint32_t button,
             uint32_t time_msec)
{
    (void)time_msec;
    if (!s->eq.visible) return 0;

    if (!hit_in_panel(&s->eq.hit, lx, ly)) { eq_hide(s); return 1; }
    if (button != BTN_LEFT) return 1;

    int i = hit_index_at(&s->eq.hit, lx, ly);
    if (i < 0 || i >= eq_total_rows(s)) return 1;

    eq_state_refresh(s);
    s->eq.selected = i;
    /* A click on a slider row nudges it up; the wheel and the arrows are the
     * fine control. Clicking the on/off row toggles, which is the obvious
     * meaning of clicking a switch — and the ENABLED case ignores `step`
     * entirely, so this is ONE call, not a toggle followed by a nudge. */
    eq_adjust(s, i, 1);
    synui_render_eq(s);
    return 1;
}

int eq_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    if (!s->eq.visible) return 0;
    if (!hit_in_panel(&s->eq.hit, lx, ly)) return 1;

    int i = hit_index_at(&s->eq.hit, lx, ly);
    if (i < 0 || i >= eq_total_rows(s) || i == EQ_ROW_ENABLED) return 1;

    eq_state_refresh(s);
    s->eq.selected = i;
    eq_adjust(s, i, delta > 0 ? -1 : 1);
    synui_render_eq(s);
    return 1;
}

/* ── Keys ────────────────────────────────────────────────── */

int eq_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    (void)mods;
    if (!s->eq.visible) return 0;

    /* BEFORE acting, never after — see the header. */
    eq_state_refresh(s);

    int total = eq_total_rows(s);

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        eq_hide(s);
        return 1;

    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (s->eq.selected > 0) s->eq.selected--;
        synui_render_eq(s);
        return 1;

    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (s->eq.selected < total - 1) s->eq.selected++;
        synui_render_eq(s);
        return 1;

    case XKB_KEY_Left:
    case XKB_KEY_h:
        eq_adjust(s, s->eq.selected, -1);
        synui_render_eq(s);
        return 1;

    case XKB_KEY_Right:
    case XKB_KEY_l:
        eq_adjust(s, s->eq.selected, 1);
        synui_render_eq(s);
        return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_space:
        /* Enter on the switch toggles it; on a slider it does nothing, because
         * there is nothing for "activate" to mean on a continuous value. */
        if (s->eq.selected == EQ_ROW_ENABLED)
            eq_adjust(s, EQ_ROW_ENABLED, 0);
        synui_render_eq(s);
        return 1;

    /* Flat, and the fastest way out of a curve you have made a mess of. */
    case XKB_KEY_0:
    case XKB_KEY_r:
        eq_run("synui-eq reset");
        s->eq.mtime = 0;   /* the script rewrites every band; re-read it */
        snprintf(s->eq.preset, sizeof(s->eq.preset), "flat");
        s->eq.preamp = 0;
        for (int i = 0; i < EQ_BANDS; i++) s->eq.gain[i] = 0;
        synui_render_eq(s);
        return 1;

    default:
        return 1;   /* modal, as every other panel is */
    }
}
