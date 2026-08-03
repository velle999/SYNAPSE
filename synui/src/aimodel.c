/*
 * aimodel.c — the AI model picker (control panel ▸ System ▸ AI model)
 *
 * Two jobs, and the first is the one that matters:
 *
 *   - Report what synapd DETECTED about the model it is running: the name out
 *     of the GGUF, the prompt format it resolved, the sampling profile that
 *     matched, and the values in force. None of that is inferable from a
 *     filename, and all of it fails silently. A model fed the wrong turn
 *     format still answers fluently, which is exactly how synapd spent its
 *     whole life framing Mistral prompts as Zephyr with nothing to show for
 *     it. The panel reads these from SYN_MSG_STATUS rather than working them
 *     out again here — otherwise it would report what synui PREDICTS, and the
 *     one bug it exists to expose is the two disagreeing.
 *
 *   - Switch models, over SYN_MSG_RELOAD. synapd confines that to its own
 *     models directory and refuses anything with a '/' in it, so this panel
 *     sends a bare filename and never a path.
 *
 * The switch is not instant and is not pretended to be: synapd acknowledges
 * at once and loads several GB on its own thread. The row shows "loading…"
 * until a status poll says a model is up again, so the panel is following the
 * daemon rather than counting down its own guess.
 *
 * Keys follow filters.c/power.c (Up/Down select, Enter/Space activate, Esc
 * close) because a panel that worked its own way would be its own bug.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "synui.h"

/* Must match synapd's SYNAPD_MODEL_DIR. synapd is the one that enforces it —
 * this is only where the list is read from, so a mismatch shows an empty panel
 * rather than letting anything out of the directory.
 *
 * Overridable only so the test can point it at a directory of stub files, the
 * way lid_test does with SYNUI_POWER_SUPPLY_DIR. Nothing in the build sets it. */
#ifndef AIMODEL_DIR
#define AIMODEL_DIR  "/var/lib/synapd/models"
#endif

/* ── Model list ──────────────────────────────────────────── */

static int aimodel_cmp(const void *a, const void *b)
{
    const syn_aimodel_entry_t *x = a, *y = b;
    return strcasecmp(x->name, y->name);
}

/*
 * Read the models directory.
 *
 * Every .gguf is listed, including the embedding model, because hiding files
 * on a guess about their purpose would be synui deciding what synapd may load.
 * A GGUF's role is not in its filename, and a picker that silently omitted the
 * one you were looking for would be worse than one that lists something odd.
 */
static void aimodel_scan(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    am->count = 0;

    DIR *d = opendir(AIMODEL_DIR);
    if (!d) {
        snprintf(am->status, sizeof(am->status),
                 "cannot read %s", AIMODEL_DIR);
        return;
    }

    struct dirent *e;
    while ((e = readdir(d)) && am->count < AIMODEL_MAX) {
        const char *n = e->d_name;
        size_t len = strlen(n);
        if (len < 6 || strcmp(n + len - 5, ".gguf") != 0) continue;
        if (n[0] == '.') continue;

        /* Skipped rather than truncated: a shortened name is a name synapd
         * would refuse, so listing it would offer a row that cannot load. */
        if (len >= sizeof(am->models[0].name)) {
            wlr_log(WLR_INFO, "synui: model name too long to list: %s", n);
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", AIMODEL_DIR, n);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        syn_aimodel_entry_t *m = &am->models[am->count++];
        /* Bounded explicitly. The length guard above already rules truncation
         * out; the precision is what lets the compiler see that. */
        snprintf(m->name, sizeof(m->name), "%.*s", (int)sizeof(m->name) - 1, n);
        m->bytes = (long long)st.st_size;
    }
    closedir(d);

    qsort(am->models, am->count, sizeof(am->models[0]), aimodel_cmp);

    if (am->count == 0)
        snprintf(am->status, sizeof(am->status),
                 "no .gguf models in %s", AIMODEL_DIR);
}

/*
 * Work out which listed model is the loaded one.
 *
 * Matched on the FILENAME synapd reports, not the name inside the GGUF — those
 * are unrelated by design ("synapse.gguf" holds "Mistral Nemo Instruct 2407"),
 * so a name comparison would be a guess dressed as a fact.
 *
 * -1 when nothing matches, which is correct and not a failure: synapd may be
 * running a model from outside this directory because an ExecStart flag named
 * one, and marking a row anyway would be a lie about which file is live.
 */
static void aimodel_mark_loaded(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    const char *file = s->overlay.model_file;

    /* A switch in flight owns the marker: synapd is still reporting the OLD
     * file until the new one is resident, and letting that win would bounce
     * the "loading …" tag back to the previous row mid-load. */
    if (am->switching) return;

    am->loaded_idx = -1;
    if (!file || !*file) return;

    for (int i = 0; i < am->count; i++) {
        if (strcmp(am->models[i].name, file) == 0) {
            am->loaded_idx = i;
            return;
        }
    }
}

/* ── Panel ───────────────────────────────────────────────── */

void aimodel_show(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    am->visible   = 1;
    am->switching = 0;
    am->status[0] = '\0';
    am->loaded_idx = -1;

    aimodel_scan(s);
    if (am->selected >= am->count) am->selected = 0;
    if (am->selected < 0)          am->selected = 0;
    aimodel_mark_loaded(s);

    /* The detail lines come from the status poller, which only runs while
     * something wants it. Ask for it, or the panel opens on stale numbers and
     * fills in a second later for no reason the user can see. */
    synmon_want_refresh(s);

    synui_render_aimodel(s);
}

void aimodel_hide(syn_server_t *s)
{
    s->aimodel.visible = 0;

    /* Release the poller unless something else still wants it — the overlay, or
     * the control panel, whose AI-model row follows the daemon the same way.
     * Leaving it armed would keep a socket round-trip running once a second for
     * a panel nobody is looking at. */
    synmon_want_refresh(s);

    synui_render_aimodel(s);
    ctlpanel_child_closed(s, "aimodel");
}

void aimodel_toggle(syn_server_t *s)
{
    if (s->aimodel.visible) aimodel_hide(s);
    else                    aimodel_show(s);
}

/*
 * Fold the newest status poll into the picker's state.
 *
 * This is how a switch finishes. synapd reports model=loading while the swap
 * runs and model=loaded when it is done, so the switch clears its own
 * "switching" flag on the daemon's word rather than on a timer — a 12 GB model
 * on a cold cache takes as long as it takes.
 *
 * Shared by the panel and the control-panel row: a switch fired from the row
 * has to finish the same way, and a second copy of "is it done yet" is exactly
 * the kind of thing that would come to disagree.
 */
static void aimodel_settle(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    int valid_sel = (am->selected >= 0 && am->selected < am->count);

    if (am->switching && valid_sel &&
        strcmp(s->overlay.model, "loaded") == 0 &&
        strcmp(s->overlay.model_file, am->models[am->selected].name) == 0) {
        /* Both conditions matter. "loaded" alone is still true for the OLD
         * model in the moment between the request being accepted and synapd
         * taking the write lock, so waiting on the filename too is what stops
         * the panel calling a switch done before it has begun. */
        am->switching = 0;
        snprintf(am->status, sizeof(am->status), "loaded");
    }

    aimodel_mark_loaded(s);
}

/*
 * A status poll landed.
 *
 * Two consumers now — the picker, and the control panel's AI-model row, which
 * shows the same fact in one line. Neither is up most of the time, and the poll
 * only runs while one of them is, so the early return is the common case.
 */
void aimodel_status_changed(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    if (!am->visible && !s->ctlpanel.visible) return;

    aimodel_settle(s);

    if (am->visible) {
        synui_render_aimodel(s);
        return;
    }

    /* Row only. Follow the daemon: if synapd is running something else — the
     * picker loaded it, a `synctl` did, or synapd restarted onto its default —
     * the row says so rather than sitting on a stale pick.
     *
     * Not while a pick is settling or a switch is in flight: those ARE the
     * user's cursor, and snapping it back to the model still resident would
     * undo the choice being made a keypress at a time. */
    if (!am->switching && s->ctlpanel.model_commit_at == 0.0 &&
        am->loaded_idx >= 0)
        am->selected = am->loaded_idx;

    ctlpanel_refresh(s);
}

/* ── Activation ──────────────────────────────────────────── */

/*
 * Ask synapd for whatever the cursor is on.
 *
 * Returns 1 if a request actually went out, 0 otherwise — refused, already
 * loaded, or nothing to load. Either way am->status carries the reason, which
 * is what both callers put on screen; neither renders here, because the panel
 * and the control-panel row draw different things.
 */
static int aimodel_load_selected(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;
    if (am->count == 0) return 0;
    if (am->selected < 0 || am->selected >= am->count) return 0;

    if (am->switching) {
        snprintf(am->status, sizeof(am->status),
                 "still loading \xc2\xb7 wait for it to finish");
        return 0;
    }

    const char *name = am->models[am->selected].name;

    /* Already running it: say so rather than making synapd unload and reload
     * several GB to arrive back where it started. */
    if (am->loaded_idx == am->selected) {
        snprintf(am->status, sizeof(am->status), "already loaded");
        return 0;
    }

    char reply[160] = {0};
    if (synmon_send_reload(name, reply, sizeof(reply)) != 0) {
        /* synapd's refusal names the rule that was broken — show it verbatim
         * instead of a generic failure that sends you to the logs. */
        snprintf(am->status, sizeof(am->status), "%s",
                 reply[0] ? reply : "synapd refused the switch");
        wlr_log(WLR_ERROR, "synui: model switch to %s failed: %s", name, reply);
        return 0;
    }

    am->switching  = 1;
    am->loaded_idx = am->selected;
    snprintf(am->status, sizeof(am->status), "loading \xe2\x80\xa6");
    wlr_log(WLR_INFO, "synui: switching synapd to %s", name);
    return 1;
}

static void aimodel_activate(syn_server_t *s)
{
    aimodel_load_selected(s);
    synui_render_aimodel(s);
}

/* ── The control-panel row ───────────────────────────────── */

void aimodel_row_sync(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    /* Not while the picker owns the state: it has its own cursor on screen and
     * the control panel is only stepping aside for it. */
    if (am->visible) return;

    aimodel_scan(s);
    aimodel_mark_loaded(s);

    /* switching is deliberately NOT cleared here. A switch fired from the row
     * outlives the panel — Esc while a 7 GB model loads is normal — and
     * clearing it would let the row show a pick as settled, and a second
     * request go out, while synapd is still holding the write lock. It clears
     * when the daemon says so (aimodel_settle), or when the picker is opened,
     * which is the escape hatch if synapd died mid-load and never will.
     *
     * Only the leftover text goes, since it describes a switch that has
     * finished; a live "loading …" is still true. */
    if (!am->switching) am->status[0] = '\0';

    /* Start on the loaded model. -1 means synapd has not said yet (the poll is
     * one round trip behind the panel opening) or is running something outside
     * the directory — leave the cursor where it was; the next poll to land
     * moves it, via aimodel_status_changed(). */
    if (am->loaded_idx >= 0)      am->selected = am->loaded_idx;
    else if (am->selected >= am->count) am->selected = 0;
}

void aimodel_row_value(syn_server_t *s, char *buf, size_t n)
{
    syn_aimodel_t *am = &s->aimodel;

    if (am->count == 0)                                  { snprintf(buf, n, "none"); return; }
    if (am->selected < 0 || am->selected >= am->count)   { snprintf(buf, n, "none"); return; }

    const char *name = am->models[am->selected].name;

    /* Every entry ends in .gguf, so the suffix distinguishes nothing and the
     * row has one line to spend. The picker shows the full filename — that is
     * where you go when you need to know exactly which file. */
    size_t len = strlen(name);
    if (len > 5 && strcmp(name + len - 5, ".gguf") == 0) len -= 5;

    if (am->switching) snprintf(buf, n, "%.*s \xc2\xb7 loading", (int)len, name);
    else               snprintf(buf, n, "%.*s", (int)len, name);
}

int aimodel_row_cycle(syn_server_t *s, int dir)
{
    syn_aimodel_t *am = &s->aimodel;

    /* The directory is not watched, and the control panel may have been open
     * since before a model was dropped in. An empty list is worth a second look
     * before reporting it as one. */
    if (am->count == 0) aimodel_scan(s);
    if (am->count == 0) return 0;

    /* Refused rather than queued: synapd rejects a second reload while the
     * first is resident-loading anyway, and a queue would mean the row you let
     * go of is not the model you end up with. */
    if (am->switching) {
        snprintf(am->status, sizeof(am->status),
                 "still loading \xc2\xb7 wait for it to finish");
        return 0;
    }

    /* Clamped before stepping: a rescan can shrink the list under a cursor that
     * was valid when it was last set. */
    if (am->selected < 0 || am->selected >= am->count) am->selected = 0;

    am->selected = (am->selected + dir + am->count) % am->count;
    am->status[0] = '\0';
    return 1;
}

int aimodel_row_commit(syn_server_t *s)
{
    syn_aimodel_t *am = &s->aimodel;

    /* Landing back on the model already loaded is the ordinary way out of a
     * cycle you did not mean to start — say nothing rather than "already
     * loaded", which would read as a failure. */
    if (am->loaded_idx == am->selected) {
        am->status[0] = '\0';
        return 0;
    }
    return aimodel_load_selected(s);
}

const char *aimodel_status_text(syn_server_t *s)
{
    return s->aimodel.status;
}

/* ── Input ───────────────────────────────────────────────── */

int aimodel_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    (void)mods;
    syn_aimodel_t *am = &s->aimodel;
    if (!am->visible) return 0;

    switch (sym) {
    case XKB_KEY_Escape:
        aimodel_hide(s);
        return 1;

    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (am->count) am->selected = (am->selected - 1 + am->count) % am->count;
        synui_render_aimodel(s);
        return 1;

    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (am->count) am->selected = (am->selected + 1) % am->count;
        synui_render_aimodel(s);
        return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_space:
        aimodel_activate(s);
        return 1;

    case XKB_KEY_r:
        /* The directory is not watched — a model dropped in while the panel is
         * open should not need a close and reopen to appear. */
        aimodel_scan(s);
        if (am->selected >= am->count) am->selected = am->count ? am->count - 1 : 0;
        snprintf(am->status, sizeof(am->status), "rescanned");
        synui_render_aimodel(s);
        return 1;

    default:
        break;
    }

    /* Swallow everything while up, exactly as the other panels do: a stray key
     * reaching the desktop from an open modal is its own surprise. */
    return 1;
}

int aimodel_motion(syn_server_t *s, double lx, double ly)
{
    syn_aimodel_t *am = &s->aimodel;
    if (!am->visible) return 0;

    int row = hit_row_at(&am->hit, lx, ly);
    if (row >= 0 && row < am->count && row != am->selected) {
        am->selected = row;
        synui_render_aimodel(s);
    }
    return hit_in_panel(&am->hit, lx, ly);
}

int aimodel_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    syn_aimodel_t *am = &s->aimodel;
    if (!am->visible) return 0;

    /* Moves the cursor, never loads: a wheel is how you look down a list, and
     * a model swap is not something to trip into with a flick. */
    if (am->count) {
        int dir = delta > 0 ? 1 : -1;
        am->selected = (am->selected + dir + am->count) % am->count;
        synui_render_aimodel(s);
    }
    return 1;
}

int aimodel_click(syn_server_t *s, double lx, double ly, uint32_t button,
                  uint32_t time_msec)
{
    (void)time_msec;   /* only the pickers need it, for their double click */
    syn_aimodel_t *am = &s->aimodel;
    if (!am->visible) return 0;

    /* Clicking away closes, as every other modal here does. */
    if (!hit_in_panel(&am->hit, lx, ly)) {
        aimodel_hide(s);
        return 1;
    }

    aimodel_motion(s, lx, ly);            /* act on the row pointed at */

    if (hit_row_at(&am->hit, lx, ly) < 0) return 1;   /* chrome */
    if (button != BTN_LEFT) return 1;

    aimodel_activate(s);
    return 1;
}
