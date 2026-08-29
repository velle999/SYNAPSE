/*
 * clock.c — Clock & Time settings panel + Calendar popup
 *
 * Two compositor-drawn panels that share this file because they share the
 * date/time helpers:
 *
 *   Clock & Time (clock_toggle, "clock" action, "Date & Time" on the Settings
 *   menu) — the bar clock's 12/24-hour and seconds format, a world clock, and
 *   the system time zone + NTP sync. The format toggles are persisted to
 *   clock.state, which the waybar custom/clock module (synui-clock) reads on
 *   its 1 Hz tick — so flipping 12/24-hour here changes the bar within a
 *   second, no waybar reload needed.
 *
 *   Calendar (calendar_toggle, "calendar" action) — a month grid opened by
 *   the "c" key here, or a click on the bar clock (quickshell's
 *   modules/Clock.qml runs `synctl dispatch calendar`). It had Super+Shift+T
 *   until 2026-07-31, when T went to `retile`; the click is how it is actually
 *   reached, so no default bind replaced it.
 *
 * Time zone and NTP changes shell out to timedatectl, which authenticates
 * through polkit (polkit-gnome is the session agent); a denial or failure is
 * reported on the panel's status line rather than swallowed.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/util/log.h>

#include "synui.h"

#define CLOCK_POLL_MS 1000

/* ── clock.state (shared with the synui-clock waybar script) ─── */

static bool clock_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "clock.state");
}

/* ── date layouts ─────────────────────────────────────────────
 *
 * The list synui-clock renders from, kept here so the panel can cycle. It is a
 * COPY, deliberately: the two live in one repo and ship in one package, and the
 * cross-package answer — the one syn-settings uses — comes from
 * `synui-clock --layouts` so there is exactly one authority outside synui.
 *
 * `off` is a real answer. Some people want a clock, not a calendar.
 */
static const struct {
    const char *id;
    const char *label;
    const char *fmt;      /* NULL = draw no date at all */
    const char *lng;      /* the spelled-out form, for this panel's header */
} g_date_layouts[] = {
    { "iso",      "ISO 8601",           "%Y-%m-%d",   "%A, %-d %B %Y" },
    { "dmy",      "Day first",          "%d/%m/%Y",   "%A, %-d %B %Y" },
    { "mdy",      "Month first",        "%m/%d/%Y",   "%A, %B %-d %Y" },
    { "dmy-text", "Day first, named",   "%-d %b %Y",  "%A, %-d %B %Y" },
    { "mdy-text", "Month first, named", "%b %-d, %Y", "%A, %B %-d %Y" },
    { "locale",   "Follow the locale",  "%x",         "%A, %x"        },
    { "off",      "No date",            NULL,         "%A, %-d %B %Y" },
};
#define CLOCK_DATE_LAYOUTS ((int)(sizeof(g_date_layouts) / sizeof(g_date_layouts[0])))

/* Index of an id, or -1 for one this build has never heard of. */
static int clock_date_index(const char *id)
{
    for (int i = 0; i < CLOCK_DATE_LAYOUTS; i++)
        if (!strcmp(g_date_layouts[i].id, id)) return i;
    return -1;
}

void clock_state_save(syn_server_t *s)
{
    syn_clock_t *c = &s->clock;
    char path[256];
    if (!clock_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: clock: cannot write '%s': %s",
                path, strerror(errno));
        snprintf(c->status, sizeof(c->status), "save failed: %s",
                 strerror(errno));
        return;
    }
    fprintf(f, "format=%s\n",  c->fmt24 ? "24" : "12");
    fprintf(f, "seconds=%d\n", c->seconds ? 1 : 0);
    fprintf(f, "date=%s\n",   c->date[0] ? c->date : "iso");
    fprintf(f, "zones=");
    for (int i = 0; i < c->nzones; i++)
        fprintf(f, "%s%s", i ? "|" : "", c->zones[i]);
    fprintf(f, "\n");
    fclose(f);
    wlr_log(WLR_INFO, "synui: clock: saved to %s", path);
}

/* Seed the world clock with a sensible spread of zones; overridden by
 * clock.state and, at startup, by synuirc's clock_zones. */
static void clock_seed_zones(syn_clock_t *c)
{
    static const char *def[] = {
        "America/New_York", "Europe/London", "Asia/Tokyo",
    };
    c->nzones = (int)(sizeof(def) / sizeof(def[0]));
    for (int i = 0; i < c->nzones; i++)
        snprintf(c->zones[i], sizeof(c->zones[i]), "%s", def[i]);
}

static void clock_set_zones(syn_clock_t *c, const char *list)
{
    if (!list || !*list) return;
    c->nzones = 0;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", list);
    for (char *tok = strtok(buf, "|,");
         tok && c->nzones < CLOCK_ZONES_MAX;
         tok = strtok(NULL, "|,")) {
        while (*tok == ' ') tok++;
        if (*tok) snprintf(c->zones[c->nzones++], sizeof(c->zones[0]), "%s", tok);
    }
    if (c->nzones == 0) clock_seed_zones(c);
}

void clock_state_load(syn_server_t *s)
{
    syn_clock_t *c = &s->clock;
    char path[256];
    if (!clock_state_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;   /* never saved — keep defaults */

    char line[600];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line, *val = eq + 1;
        if      (!strcmp(key, "format"))  c->fmt24   = !strcmp(val, "24");
        else if (!strcmp(key, "seconds")) c->seconds = atoi(val) ? 1 : 0;
        else if (!strcmp(key, "zones"))   clock_set_zones(c, val);
        /* Stored verbatim. An id from a newer synui-clock than this build
         * knows about survives being loaded and saved again; only cycling the
         * row replaces it. */
        else if (!strcmp(key, "date"))    snprintf(c->date, sizeof(c->date), "%s", val);
    }
    fclose(f);
}

/* ── System time-zone / NTP status ───────────────────────── */

/* /etc/localtime is a symlink into the zoneinfo tree; the zone name is the
 * tail after "zoneinfo/". Cheaper and more reliable than parsing timedatectl. */
static void clock_read_tz(syn_clock_t *c)
{
    char target[256];
    ssize_t n = readlink("/etc/localtime", target, sizeof(target) - 1);
    snprintf(c->tz, sizeof(c->tz), "%s", "unknown");
    if (n <= 0) return;
    target[n] = '\0';
    char *z = strstr(target, "zoneinfo/");
    if (z) snprintf(c->tz, sizeof(c->tz), "%s", z + strlen("zoneinfo/"));
}

/* One quick timedatectl read, only on panel open and after a change. */
static void clock_read_ntp(syn_clock_t *c)
{
    FILE *p = popen("timedatectl show -p NTP --value 2>/dev/null", "r");
    if (!p) return;
    char v[32] = {0};
    if (fgets(v, sizeof(v), p)) c->ntp = !strncmp(v, "yes", 3);
    pclose(p);
}

/* ── Time formatting ─────────────────────────────────────── */

/* strftime a time in an arbitrary zone by swapping $TZ around localtime_r. */
static void fmt_in_zone(const char *zone, const char *fmt,
                        char *out, size_t n, time_t now)
{
    char *saved = getenv("TZ");
    char keep[128] = {0};
    if (saved) snprintf(keep, sizeof(keep), "%s", saved);

    setenv("TZ", zone, 1);
    tzset();

    struct tm tm;
    localtime_r(&now, &tm);
    strftime(out, n, fmt, &tm);

    if (saved) setenv("TZ", keep, 1);
    else       unsetenv("TZ");
    tzset();
}

/* ── Clock & Time panel ──────────────────────────────────── */

static int clock_tick(void *data)
{
    syn_server_t *s = data;
    if (!s->clock.visible) return 0;
    synui_render_clock(s);
    wl_event_source_timer_update(s->clock.timer, CLOCK_POLL_MS);
    return 0;
}

void clock_init(syn_server_t *s)
{
    syn_clock_t *c = &s->clock;
    if (c->nzones == 0) clock_seed_zones(c);
    /* Must match synui-clock's default, or the bar and the panel disagree
     * before the panel has ever saved — the same rule clock_seed_zones()
     * already follows. */
    if (!c->date[0]) snprintf(c->date, sizeof(c->date), "%s", "iso");
    clock_state_load(s);

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    c->timer = wl_event_loop_add_timer(loop, clock_tick, s);
}

void clock_finish(syn_server_t *s)
{
    if (s->clock.timer) {
        wl_event_source_remove(s->clock.timer);
        s->clock.timer = NULL;
    }
}

void clock_show(syn_server_t *s)
{
    syn_clock_t *c = &s->clock;
    /* Re-read, every time. clock.state has a SECOND writer now — syn-settings'
     * Date & Time rows — and this panel used to load the file once at startup.
     * Opening it later would have shown stale values and, worse, saving from
     * it would have written those stale values back over somebody's change. */
    clock_state_load(s);
    c->visible   = 1;
    c->selected  = 0;
    c->status[0] = '\0';
    clock_read_tz(c);
    clock_read_ntp(c);
    synui_render_clock(s);
    wl_event_source_timer_update(c->timer, CLOCK_POLL_MS);
}

void clock_hide(syn_server_t *s)
{
    s->clock.visible = 0;
    if (s->clock.timer) wl_event_source_timer_update(s->clock.timer, 0);
    synui_render_clock(s);
    ctlpanel_child_closed(s, "clock");
}

void clock_toggle(syn_server_t *s)
{
    if (s->clock.visible) clock_hide(s);
    else                  clock_show(s);
}

/* Apply a settings row's toggle. Rows: 0 format, 1 seconds, 2 date, 3 NTP. */
static void clock_activate(syn_server_t *s)
{
    syn_clock_t *c = &s->clock;
    switch (c->selected) {
    case 0:
        c->fmt24 = !c->fmt24;
        clock_state_save(s);
        snprintf(c->status, sizeof(c->status),
                 "clock format: %s", c->fmt24 ? "24-hour" : "12-hour");
        break;
    case 1:
        c->seconds = !c->seconds;
        clock_state_save(s);
        snprintf(c->status, sizeof(c->status),
                 "seconds: %s", c->seconds ? "on" : "off");
        break;
    case 2: {
        /* Cycle. An id this build does not recognise starts the cycle at the
         * beginning rather than being treated as index 0 and silently
         * becoming ISO. */
        int i = clock_date_index(c->date);
        i = (i < 0) ? 0 : (i + 1) % CLOCK_DATE_LAYOUTS;
        snprintf(c->date, sizeof(c->date), "%s", g_date_layouts[i].id);
        clock_state_save(s);
        snprintf(c->status, sizeof(c->status),
                 "date layout: %s", g_date_layouts[i].label);
        break;
    }
    case 3: {
        /* timedatectl authenticates via polkit; the agent may prompt. We flip
         * our idea of the state optimistically and re-read after. */
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "timedatectl set-ntp %s",
                 c->ntp ? "false" : "true");
        synui_spawn(cmd);
        snprintf(c->status, sizeof(c->status),
                 "NTP sync %s (may prompt for auth)", c->ntp ? "off" : "on");
        c->ntp = !c->ntp;   /* reflected now; corrected on next open */
        break;
    }
    }
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * See the panel pointer contract in synui.h. Every row here is a toggle or a
 * cycle that Enter, Space and Left/Right all drive the same way, so a click is
 * simply clock_activate() on the row under it. */

int clock_motion(syn_server_t *s, double lx, double ly)
{
    syn_clock_t *c = &s->clock;
    if (!c->visible) return 0;

    int row = hit_row_at(&c->hit, lx, ly);
    if (row < 0 || row == c->selected) return 1;
    c->selected = row;
    synui_render_clock(s);
    return 1;
}

int clock_click(syn_server_t *s, double lx, double ly, uint32_t button,
                  uint32_t time_msec)
{
    (void)time_msec;   /* only the pickers need it, for their double click */
    syn_clock_t *c = &s->clock;
    if (!c->visible) return 0;

    if (!hit_in_panel(&c->hit, lx, ly)) {
        clock_hide(s);
        return 1;
    }

    clock_motion(s, lx, ly);

    if (button != BTN_LEFT) return 1;
    if (hit_row_at(&c->hit, lx, ly) < 0) return 1;   /* chrome / world clocks */

    clock_activate(s);
    synui_render_clock(s);
    return 1;
}

int clock_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    syn_clock_t *c = &s->clock;
    if (!c->visible) return 0;
    if (delta == 0) return 1;

    int next = c->selected + (delta > 0 ? 1 : -1);
    if (next < 0 || next >= CLOCK_SETTING_ROWS) return 1;
    c->selected = next;
    synui_render_clock(s);
    return 1;
}

int clock_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    syn_clock_t *c = &s->clock;
    if (!c->visible) return 0;

    /* Modified combos fall through to the global bind table. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        clock_hide(s);
        return 1;
    case XKB_KEY_Up:
    case XKB_KEY_k:
        if (c->selected > 0) c->selected--;
        break;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        if (c->selected < CLOCK_SETTING_ROWS - 1) c->selected++;
        break;
    case XKB_KEY_Left:
    case XKB_KEY_Right:
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_space:
        clock_activate(s);
        break;
    case XKB_KEY_c:
        clock_hide(s);
        calendar_toggle(s);
        return 1;
    default:
        return 1;   /* modal: swallow other unmodified keys */
    }

    synui_render_clock(s);
    return 1;
}

/* Row helpers used by render.c so the panel and the input map never drift. */
const char *clock_row_label(int row)
{
    switch (row) {
    case 0: return "Clock format";
    case 1: return "Show seconds";
    case 2: return "Date layout";
    case 3: return "Sync clock (NTP)";
    default: return "";
    }
}

void clock_row_value(syn_server_t *s, int row, char *out, size_t n)
{
    syn_clock_t *c = &s->clock;
    switch (row) {
    case 0: snprintf(out, n, "%s", c->fmt24 ? "24-hour" : "12-hour"); break;
    case 1: snprintf(out, n, "%s", c->seconds ? "on" : "off"); break;
    case 2: {
        /* The label AND today in it. "Day first" and "Month first" are the
         * same words to somebody who has to guess which order they mean; a
         * worked example is the only description of a date format that
         * cannot be misread. */
        int i = clock_date_index(c->date);
        if (i < 0) { snprintf(out, n, "%s (unknown to this build)", c->date); break; }
        if (!g_date_layouts[i].fmt) { snprintf(out, n, "%s", g_date_layouts[i].label); break; }
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        char ex[64];
        strftime(ex, sizeof(ex), g_date_layouts[i].fmt, &tm);
        snprintf(out, n, "%s  (%s)", g_date_layouts[i].label, ex);
        break;
    }
    case 3: snprintf(out, n, "%s", c->ntp ? "on" : "off"); break;
    default: out[0] = '\0';
    }
}

/* Local time string for the panel's header, honouring the format toggle. */
void clock_local_string(syn_server_t *s, char *out, size_t n)
{
    syn_clock_t *c = &s->clock;
    const char *fmt = c->fmt24
        ? (c->seconds ? "%H:%M:%S" : "%H:%M")
        : (c->seconds ? "%I:%M:%S %p" : "%I:%M %p");
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    /* The header spells the date out, in the ORDER the chosen layout implies.
     * It said "%A, %B %-d %Y" for everyone, so picking Day first left this
     * panel — the one you picked it on — still reading month first. */
    int di = clock_date_index(c->date);
    const char *lng = di < 0 ? "%A, %-d %B %Y" : g_date_layouts[di].lng;
    char t[64], d[64];
    strftime(t, sizeof(t), fmt, &tm);
    strftime(d, sizeof(d), lng, &tm);
    snprintf(out, n, "%s   %s", t, d);
}

/* "09:41 Mon" style for one world-clock zone. */
void clock_zone_string(syn_server_t *s, int i, char *out, size_t n)
{
    syn_clock_t *c = &s->clock;
    if (i < 0 || i >= c->nzones) { out[0] = '\0'; return; }
    const char *fmt = c->fmt24 ? "%H:%M %a" : "%I:%M %p %a";
    fmt_in_zone(c->zones[i], fmt, out, n, time(NULL));
}

/* ── Calendar popup ──────────────────────────────────────── */

void calendar_show(syn_server_t *s)
{
    syn_cal_t *cal = &s->cal;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    cal->visible = 1;
    cal->year    = tm.tm_year + 1900;
    cal->mon     = tm.tm_mon;
    cal->sel     = tm.tm_mday;
    /* ⚠ ASKED FOR BEFORE THE DRAW, AND THE DRAW DOES NOT WAIT. calevents_fetch
     * spawns and returns; the panel appears now, with the events arriving a few
     * milliseconds later and a second render behind them. Blocking here would
     * freeze every window on the machine for as long as syn-cal took. */
    calevents_fetch(s, cal->year, cal->mon);
    synui_render_calendar(s);
}

void calendar_hide(syn_server_t *s)
{
    s->cal.visible = 0;
    /* Nothing is waiting for the answer any more, and a read left armed on a
     * closed panel is a wl_event_source that outlives its reason. */
    calevents_cancel();
    synui_render_calendar(s);
}

void calendar_toggle(syn_server_t *s)
{
    if (s->cal.visible) calendar_hide(s);
    else                calendar_show(s);
}

/* ⚠ cal_step_month TAKES THE STRUCT, NOT THE SERVER, so it cannot fetch. Its
 * callers do — see cal_after_move(), which every key and click path goes
 * through. Adding the fetch here would mean giving this function the server
 * for one line and letting two other paths forget to call it. */
static void cal_step_month(syn_cal_t *cal, int delta)
{
    cal->mon += delta;
    while (cal->mon < 0)  { cal->mon += 12; cal->year--; }
    while (cal->mon > 11) { cal->mon -= 12; cal->year++; }
    int dim = calendar_days_in_month(cal->year, cal->mon);
    if (cal->sel > dim) cal->sel = dim;
    if (cal->sel < 1)   cal->sel = 1;
}

static void cal_step_day(syn_cal_t *cal, int delta)
{
    cal->sel += delta;
    int dim = calendar_days_in_month(cal->year, cal->mon);
    if (cal->sel < 1)    { cal_step_month(cal, -1);
                           cal->sel = calendar_days_in_month(cal->year, cal->mon); }
    else if (cal->sel > dim) { cal_step_month(cal, +1); cal->sel = 1; }
}

/* One place for "the view moved": ask for the new month's events if it is a
 * different month, and redraw. Every key and pointer path below ends here, so
 * there is no way to move the calendar and forget one half of it. */
static void cal_after_move(syn_server_t *s)
{
    syn_cal_t *cal = &s->cal;
    if (cal->loaded_year != cal->year || cal->loaded_mon != cal->mon)
        calevents_fetch(s, cal->year, cal->mon);
    synui_render_calendar(s);
}

/* ── Calendar pointer ────────────────────────────────────────
 *
 * The one panel whose rows are a grid rather than a list, so it does its own
 * two-axis test on top of the shared row band: render.c records the band as all
 * seven day columns at once, and the cell width falls out of dividing it by
 * seven — which keeps the number in one file instead of two that can disagree.
 *
 * A click picks a day; it does not close the popup. Picking a date and having
 * the calendar vanish is the behaviour of a date picker being used to fill in a
 * field, and this one is a calendar you look at. */

/* Day number under (lx,ly), or -1 for the header, the footer and the blank
 * cells before the 1st and after the last. */
static int cal_day_at(const syn_cal_t *cal, double lx, double ly)
{
    int row = hit_row_at(&cal->hit, lx, ly);
    if (row < 0) return -1;

    int cell_w = cal->hit.row_w / 7;
    if (cell_w <= 0) return -1;
    int col = (int)((lx - cal->hit.row_x) / cell_w);
    if (col < 0 || col > 6) return -1;

    /* Invert the draw: day 1 sits at column calendar_first_weekday(), and the
     * grid runs on from there. */
    int day = row * 7 + col - calendar_first_weekday(cal->year, cal->mon) + 1;
    if (day < 1 || day > calendar_days_in_month(cal->year, cal->mon)) return -1;
    return day;
}

int calendar_motion(syn_server_t *s, double lx, double ly)
{
    (void)lx; (void)ly;
    /* Deliberately no hover highlight. The selected day is also the day the
     * arrow keys are on, and having it chase the pointer across the grid would
     * make the panel flicker under any cursor merely passing over it. */
    return s->cal.visible ? 1 : 0;
}

int calendar_click(syn_server_t *s, double lx, double ly, uint32_t button,
                  uint32_t time_msec)
{
    (void)time_msec;   /* only the pickers need it, for their double click */
    syn_cal_t *cal = &s->cal;
    if (!cal->visible) return 0;

    if (!hit_in_panel(&cal->hit, lx, ly)) {
        calendar_hide(s);
        return 1;
    }

    if (button != BTN_LEFT) return 1;

    int day = cal_day_at(cal, lx, ly);
    if (day < 0) return 1;                 /* header, footer, a blank cell */
    if (day == cal->sel) return 1;

    cal->sel = day;
    synui_render_calendar(s);
    return 1;
}

int calendar_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    syn_cal_t *cal = &s->cal;
    if (!cal->visible) return 0;
    if (delta == 0) return 1;

    /* The wheel is the month, not the day: a calendar's one long axis is the
     * months, and Page Up/Down (which the footer names) already mean that. */
    cal_step_month(cal, delta > 0 ? 1 : -1);
    cal_after_move(s);
    return 1;
}

int calendar_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    syn_cal_t *cal = &s->cal;
    if (!cal->visible) return 0;

    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        calendar_hide(s);
        return 1;
    case XKB_KEY_Left:  case XKB_KEY_h: cal_step_day(cal, -1); break;
    case XKB_KEY_Right: case XKB_KEY_l: cal_step_day(cal, +1); break;
    case XKB_KEY_Up:    case XKB_KEY_k: cal_step_day(cal, -7); break;
    case XKB_KEY_Down:  case XKB_KEY_j: cal_step_day(cal, +7); break;
    case XKB_KEY_Page_Up:              cal_step_month(cal, -1); break;
    case XKB_KEY_Page_Down:            cal_step_month(cal, +1); break;
    case XKB_KEY_t:                    calendar_show(s); return 1;  /* today */
    case XKB_KEY_s:                    calendar_hide(s); clock_toggle(s); return 1;
    default:
        return 1;
    }
    /* ⚠ THE ARROW KEYS CROSS MONTHS TOO. cal_step_day rolls into the next month
     * at the end of this one, so this path is not "only the day changed" — and
     * cal_after_move asks again only when the month actually moved. */
    cal_after_move(s);
    return 1;
}

/* ── Shared calendar math (also used by render.c) ────────── */

int calendar_days_in_month(int year, int mon /*0-11*/)
{
    static const int dim[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (mon == 1) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return dim[mon];
}

/* Day-of-week (0=Sun) of the 1st of the given month, via Zeller-free mktime. */
int calendar_first_weekday(int year, int mon /*0-11*/)
{
    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon  = mon;
    tm.tm_mday = 1;
    tm.tm_hour = 12;   /* midday: never trips a DST midnight edge */
    mktime(&tm);
    return tm.tm_wday;
}
