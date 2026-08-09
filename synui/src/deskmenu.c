/*
 * deskmenu.c — the desktop right-click menu, and optional desktop icons.
 *
 * Right-clicking the wallpaper (no window, no panel, no dock under the cursor)
 * opens a context menu of the things you can only otherwise reach from a
 * keybind: a terminal, the wallpaper picker, display settings, and so on. It
 * is the same idiom as dock.c's dockmenu_* — pointer-driven, modal while up,
 * Escape closes — because a second context menu that behaved differently would
 * be its own bug.
 *
 * Desktop icons are off by default (`desktop_icons = on` in synuirc). They are
 * read from ~/Desktop: .desktop files resolve through the same icons.c lookup
 * the dock uses, and anything else is opened with xdg-open. Drawing them is
 * render.c's job (synui_render_deskicons); this file owns the model, the
 * hit-testing, and the drag-to-move that puts an icon where the user wants it.
 *
 * An icon dragged to a new cell becomes `placed`: the auto-grid stops laying it
 * out and its cell is written to ~/.config/synui/deskicons.state, so it comes
 * back to the same spot next login. Everything not dragged still flows into the
 * free cells in name order.
 *
 * The cell the user chose is kept in `pin_x/pin_y` and never overwritten by a
 * layout; x/y is only where this layout put it. The two differ whenever the
 * usable box is not the one the pin was made against — and it usually is not,
 * because the bar reserves its strip after synui has started, so the first
 * layout of a session runs against the whole output. Every re-grid re-snaps the
 * pin, and it is the pin that is persisted; snapping x/y instead folded each
 * transient box into the placement, and one layout on a smaller grid clamped an
 * icon's row away for good.
 *
 * That state file also carries the two settings the menu can flip — whether
 * icons are shown at all, and the arrange mode — because a runtime toggle that
 * only lived in s->config would be undone by the next synui_config_reload, let
 * alone a logout. deskicons_state_load() lays them back over synuirc on every
 * config load, the same precedence launcher.state and wallpaper.state use.
 * Delete deskicons.state to hand the desktop back to synuirc.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "synui.h"

#define DESKMENU_ITEM_H 30
#define DESKMENU_W      210
#define DESKMENU_SEP_H  9      /* separator rows are shorter than items */

/* Cursor travel that turns a press on an icon into a drag rather than a click.
 * Same order as the titlebar's grab slop: small enough that a deliberate drag
 * starts at once, large enough that a shaky double-click still opens. */
#define DESKICON_DRAG_SLOP 6.0

/* Defined with the rest of the persistence, below; the menu's icons-on/off row
 * has to write the state file too, and that row is handled up here. */
static void deskicons_state_save(syn_server_t *s);

const char *deskact_label(syn_deskact_t a)
{
    switch (a) {
    case SYN_DESKACT_TERMINAL:  return "Open Terminal";
    case SYN_DESKACT_FILES:     return "Open File Manager";
    case SYN_DESKACT_APPS:      return "Applications…";
    case SYN_DESKACT_SEP:       return "";
    case SYN_DESKACT_WALLPAPER: return "Change Wallpaper…";
    case SYN_DESKACT_THEME:     return "Appearance…";
    case SYN_DESKACT_DISPLAY:   return "Display Settings…";
    case SYN_DESKACT_ICONS:     return "Show Desktop Icons";
    case SYN_DESKACT_ARRANGE_NAME: return "Arrange by Name";
    case SYN_DESKACT_ARRANGE_TYPE: return "Arrange by Type";
    case SYN_DESKACT_ARRANGE_SIZE: return "Arrange by Size";
    case SYN_DESKACT_ARRANGE_DATE: return "Arrange by Date";
    case SYN_DESKACT_TASKMGR:   return "Task Manager";
    }
    return "";
}

/* The arrange mode a row selects, or -1 if the row is not an arrange row. */
static int deskact_arrange(syn_deskact_t a)
{
    switch (a) {
    case SYN_DESKACT_ARRANGE_NAME: return SYN_ARRANGE_NAME;
    case SYN_DESKACT_ARRANGE_TYPE: return SYN_ARRANGE_TYPE;
    case SYN_DESKACT_ARRANGE_SIZE: return SYN_ARRANGE_SIZE;
    case SYN_DESKACT_ARRANGE_DATE: return SYN_ARRANGE_DATE;
    default: return -1;
    }
}

/* Separators are not selectable and are drawn as a rule, not a row. */
static bool deskact_is_sep(syn_deskact_t a) { return a == SYN_DESKACT_SEP; }

/* Row height for item i — separators are short. */
static int deskmenu_row_h(syn_server_t *s, int i)
{
    return deskact_is_sep(s->deskmenu.actions[i]) ? DESKMENU_SEP_H
                                                  : DESKMENU_ITEM_H;
}

void deskmenu_open(syn_server_t *s, double lx, double ly)
{
    int n = 0;
    s->deskmenu.actions[n++] = SYN_DESKACT_TERMINAL;
    s->deskmenu.actions[n++] = SYN_DESKACT_FILES;
    s->deskmenu.actions[n++] = SYN_DESKACT_APPS;
    s->deskmenu.actions[n++] = SYN_DESKACT_SEP;
    s->deskmenu.actions[n++] = SYN_DESKACT_WALLPAPER;
    s->deskmenu.actions[n++] = SYN_DESKACT_THEME;
    s->deskmenu.actions[n++] = SYN_DESKACT_DISPLAY;
    s->deskmenu.actions[n++] = SYN_DESKACT_ICONS;
    /* The arrange rows only exist while there is a desktop to arrange —
     * otherwise they are four rows that visibly do nothing. */
    if (s->config.desktop_icons) {
        s->deskmenu.actions[n++] = SYN_DESKACT_SEP;
        s->deskmenu.actions[n++] = SYN_DESKACT_ARRANGE_NAME;
        s->deskmenu.actions[n++] = SYN_DESKACT_ARRANGE_TYPE;
        s->deskmenu.actions[n++] = SYN_DESKACT_ARRANGE_SIZE;
        s->deskmenu.actions[n++] = SYN_DESKACT_ARRANGE_DATE;
    }
    s->deskmenu.actions[n++] = SYN_DESKACT_SEP;
    s->deskmenu.actions[n++] = SYN_DESKACT_TASKMGR;
    s->deskmenu.action_count = n;

    int h = 8;
    for (int i = 0; i < n; i++) h += deskmenu_row_h(s, i);
    int w = DESKMENU_W;

    /* Drop down-and-right of the cursor like every other desktop, then clamp
     * inside the output under it so a right-click near an edge stays whole. */
    int x = (int)lx, y = (int)ly;
    struct wlr_output *wo =
        wlr_output_layout_output_at(s->output_layout, lx, ly);
    if (wo && wo->data) {
        struct wlr_box ob;
        output_box_of(s, (syn_output_t *)wo->data, &ob);
        if (x + w > ob.x + ob.width)  x = ob.x + ob.width  - w;
        if (y + h > ob.y + ob.height) y = ob.y + ob.height - h;
        if (x < ob.x) x = ob.x;
        if (y < ob.y) y = ob.y;
    }

    s->deskmenu.x = x; s->deskmenu.y = y;
    s->deskmenu.w = w; s->deskmenu.h = h;
    s->deskmenu.selected = -1;
    s->deskmenu.visible = 1;
    synui_render_deskmenu(s);
}

/* Item index under (lx,ly), or -1 if outside the menu or over a separator. */
static int deskmenu_item_at(syn_server_t *s, double lx, double ly)
{
    if (lx < s->deskmenu.x || lx >= s->deskmenu.x + s->deskmenu.w ||
        ly < s->deskmenu.y || ly >= s->deskmenu.y + s->deskmenu.h)
        return -1;

    int rel = (int)(ly - s->deskmenu.y - 4);
    int top = 0;
    for (int i = 0; i < s->deskmenu.action_count; i++) {
        int rh = deskmenu_row_h(s, i);
        if (rel >= top && rel < top + rh)
            return deskact_is_sep(s->deskmenu.actions[i]) ? -1 : i;
        top += rh;
    }
    return -1;
}

/* Pixel offset of item i from the top of the menu — render.c needs the same
 * walk to draw the rows, so it lives here with the geometry that defines it. */
int deskmenu_row_top(syn_server_t *s, int i)
{
    int top = 4;
    for (int k = 0; k < i; k++) top += deskmenu_row_h(s, k);
    return top;
}

int deskmenu_row_height(syn_server_t *s, int i) { return deskmenu_row_h(s, i); }

bool deskmenu_row_checked(syn_server_t *s, int i)
{
    if (i < 0 || i >= s->deskmenu.action_count) return false;

    syn_deskact_t a = s->deskmenu.actions[i];
    if (a == SYN_DESKACT_ICONS) return s->config.desktop_icons;

    int mode = deskact_arrange(a);
    return mode >= 0 && (syn_arrange_t)mode == s->config.desktop_icon_arrange;
}

void deskmenu_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->deskmenu.visible) return;
    int idx = deskmenu_item_at(s, lx, ly);
    if (idx != s->deskmenu.selected) {
        s->deskmenu.selected = idx;
        synui_render_deskmenu(s);
    }
}

void deskmenu_close(syn_server_t *s)
{
    if (!s->deskmenu.visible) return;
    s->deskmenu.visible = 0;
    synui_render_deskmenu(s);
}

void deskmenu_click(syn_server_t *s, double lx, double ly)
{
    if (!s->deskmenu.visible) return;
    int idx = deskmenu_item_at(s, lx, ly);
    if (idx < 0) { deskmenu_close(s); return; }   /* outside → dismiss */

    syn_deskact_t act = s->deskmenu.actions[idx];
    deskmenu_close(s);   /* before acting: panels below raise over the menu */

    switch (act) {
    case SYN_DESKACT_TERMINAL:
        synui_spawn(s->config.terminal[0] ? s->config.terminal : "kitty");
        break;
    case SYN_DESKACT_FILES:
        /* Not "dolphin": it is an optdepend, and a Minimal install has none,
         * which made this entry a click that did nothing. The helper finds
         * whatever file manager exists and says so when there is none. */
        synui_spawn("synui-open-folder");
        break;
    case SYN_DESKACT_APPS:
        /* The start menu is the bar's now, so this goes out the same door the
         * Super tap does rather than reaching into a panel synui no longer
         * draws. */
        synui_start_menu_open(s);
        break;
    case SYN_DESKACT_WALLPAPER:
        wppick_toggle(s);
        break;
    case SYN_DESKACT_THEME:
        theme_toggle(s);
        break;
    case SYN_DESKACT_DISPLAY:
        dispcfg_toggle(s);
        break;
    case SYN_DESKACT_ICONS:
        s->config.desktop_icons = !s->config.desktop_icons;
        /* Save on whichever side of the rescan the model is populated, or the
         * dragged cells go out with the toggle: turning icons OFF zeroes
         * deskicon_count, and turning them ON runs before the rescan has read
         * the cells back in — a save on the wrong side writes a file with no
         * pos= lines at all. */
        if (s->config.desktop_icons) {
            deskicons_reload(s);
            deskicons_state_save(s);
        } else {
            deskicons_state_save(s);
            deskicons_reload(s);
        }
        break;
    case SYN_DESKACT_ARRANGE_NAME:
        deskicons_arrange(s, SYN_ARRANGE_NAME);
        break;
    case SYN_DESKACT_ARRANGE_TYPE:
        deskicons_arrange(s, SYN_ARRANGE_TYPE);
        break;
    case SYN_DESKACT_ARRANGE_SIZE:
        deskicons_arrange(s, SYN_ARRANGE_SIZE);
        break;
    case SYN_DESKACT_ARRANGE_DATE:
        deskicons_arrange(s, SYN_ARRANGE_DATE);
        break;
    case SYN_DESKACT_TASKMGR:
        taskmgr_toggle(s);
        break;
    case SYN_DESKACT_SEP:
        break;
    }
}

/* ── Desktop icons ───────────────────────────────────────── */

/*
 * Name order, and the tie-break every other mode falls back to. Two icons can
 * share a label — a .desktop is named by its Name= key, not its filename — so
 * the path decides after that, or qsort would be free to swap them on every
 * reload and the desktop would shuffle by itself.
 */
static int cmp_name(const syn_deskicon_t *x, const syn_deskicon_t *y)
{
    int r = strcasecmp(x->label, y->label);
    return r ? r : strcmp(x->path, y->path);
}

/* Extension of the file on disk, "" for none. Deliberately the filename's, not
 * the label's: a .desktop's Name= has no extension to group by. */
static const char *icon_ext(const syn_deskicon_t *ic)
{
    const char *base = strrchr(ic->path, '/');
    base = base ? base + 1 : ic->path;
    const char *dot = strrchr(base, '.');
    /* A leading dot is a hidden file, not an extension — those never reach the
     * scan, but the rule is the same one every file manager uses. */
    return (dot && dot != base) ? dot + 1 : "";
}

/*
 * Compare under the current mode. qsort() has no user-data argument, and
 * glibc's qsort_r takes its comparator arguments in the opposite order from
 * the BSD one, so the mode rides in a file-static instead: synui sorts the
 * desktop from one thread, in one call, right below.
 */
static syn_arrange_t sort_mode = SYN_ARRANGE_NAME;

static int icon_cmp(const void *a, const void *b)
{
    const syn_deskicon_t *x = a, *y = b;

    switch (sort_mode) {
    case SYN_ARRANGE_TYPE:
        /* Folders first — they are the one "type" that is not an extension. */
        if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
        if (!x->is_dir) {
            int r = strcasecmp(icon_ext(x), icon_ext(y));
            if (r) return r;
        }
        break;

    case SYN_ARRANGE_SIZE:
        /* Folders again first: st_size for a directory is the size of the
         * directory entry itself, which would sort them among the tiny files
         * as if that number meant something. Largest first, as everywhere
         * else that sorts by size. */
        if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
        if (!x->is_dir && x->size != y->size) return x->size > y->size ? -1 : 1;
        break;

    case SYN_ARRANGE_DATE:
        /* Newest first: on a desktop the thing you just saved is the thing you
         * are looking for. */
        if (x->mtime != y->mtime) return x->mtime > y->mtime ? -1 : 1;
        break;

    case SYN_ARRANGE_NAME:
        break;
    }

    return cmp_name(x, y);
}

/* The output the icon field lives on. Icons are a single-output affair (they
 * are drawn into one buffer over its usable area), so layout, hit-testing and
 * drops all have to agree on which output that is. */
static syn_output_t *deskicon_output(syn_server_t *s)
{
    syn_output_t *o = server_primary_output(s);
    if (!o) o = server_focused_output(s);
    return o;
}

/* Columns and rows of whole cells that fit inside usable area `a`. */
static void deskicon_grid(const struct wlr_box *a, int *cols, int *rows)
{
    int c = (a->width  - 2 * SYN_DESKICON_PAD) / SYN_DESKICON_W;
    int r = (a->height - 2 * SYN_DESKICON_PAD) / SYN_DESKICON_H;
    *cols = c < 1 ? 1 : c;
    *rows = r < 1 ? 1 : r;
}

/* Layout coords of cell (col,row). */
static void deskicon_cell_origin(const struct wlr_box *a, int col, int row,
                                 int *x, int *y)
{
    *x = a->x + SYN_DESKICON_PAD + col * SYN_DESKICON_W;
    *y = a->y + SYN_DESKICON_PAD + row * SYN_DESKICON_H;
}

/* The cell a free-floating origin snaps to, clamped into the grid. */
static void deskicon_cell_at(const struct wlr_box *a, int cols, int rows,
                             int x, int y, int *col, int *row)
{
    int c = (int)lround((double)(x - a->x - SYN_DESKICON_PAD) / SYN_DESKICON_W);
    int r = (int)lround((double)(y - a->y - SYN_DESKICON_PAD) / SYN_DESKICON_H);
    if (c < 0) c = 0;
    if (r < 0) r = 0;
    if (c > cols - 1) c = cols - 1;
    if (r > rows - 1) r = rows - 1;
    *col = c; *row = r;
}

/* Does an icon already settled by this layout pass own cell origin (x,y)? */
static bool deskicon_cell_taken(syn_server_t *s, const char *settled,
                               int x, int y)
{
    for (int i = 0; i < s->deskicon_count; i++)
        if (settled[i] && s->deskicons[i].x == x && s->deskicons[i].y == y)
            return true;
    return false;
}

/* ── Persistence ─────────────────────────────────────────── */

/* Resolve ~/.config/synui/deskicons.state; false if $HOME is unset. */
static bool deskicons_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "deskicons.state");
}

/* The names the arrange mode is written and read as, in enum order. Also what
 * synuirc's desktop_icon_arrange accepts, so there is one spelling of each. */
static const char *const arrange_names[] = { "name", "type", "size", "date" };

const char *syn_arrange_name(syn_arrange_t a)
{
    return (a >= 0 && a < (int)(sizeof(arrange_names) / sizeof(*arrange_names)))
           ? arrange_names[a] : "name";
}

bool syn_arrange_parse(const char *s, syn_arrange_t *out)
{
    for (unsigned i = 0; i < sizeof(arrange_names) / sizeof(*arrange_names); i++)
        if (strcmp(s, arrange_names[i]) == 0) { *out = (syn_arrange_t)i; return true; }
    return false;
}

/*
 * Write the toggle and the arrange mode, then one `pos=x,y,name` line per
 * dragged icon. Keyed on the basename rather than the full path — every entry
 * lives in ~/Desktop by definition, so the placement survives a $HOME that
 * moves. The name goes last because a filename may itself contain commas.
 */
static void deskicons_state_save(syn_server_t *s)
{
    char path[256];
    if (!deskicons_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: deskicons: cannot write '%s': %s",
                path, strerror(errno));
        return;
    }

    fprintf(f, "icons=%s\n", s->config.desktop_icons ? "on" : "off");
    fprintf(f, "arrange=%s\n", syn_arrange_name(s->config.desktop_icon_arrange));

    for (int i = 0; i < s->deskicon_count; i++) {
        syn_deskicon_t *ic = &s->deskicons[i];
        if (!ic->placed) continue;

        const char *base = strrchr(ic->path, '/');
        base = base ? base + 1 : ic->path;

        /* A newline is a legal byte in a Linux filename and would forge a
         * second line on the way back in; drop the entry instead of writing a
         * file we cannot re-read. */
        if (strchr(base, '\n') || strchr(base, '\r')) continue;

        /* The pin, not x/y: x/y is where this layout put the icon against the
         * usable box in force right now, and writing that back would bake a bar
         * strip or a since-departed monitor into the placement. */
        fprintf(f, "pos=%d,%d,%s\n", ic->pin_x, ic->pin_y, base);
    }
    fclose(f);
}

/*
 * The `icons=` toggle, read at config-load time.
 *
 * This cannot live in deskicons_state_apply below: that runs from
 * deskicons_reload, which returns early while desktop_icons is off, so the very
 * setting that would turn the desktop back on would never be read. Same
 * precedence as launcher.state — the last thing you chose from the menu wins
 * over synuirc, and an absent file leaves synuirc's `desktop_icons` standing.
 */
void deskicons_state_load(syn_config_t *cfg)
{
    char path[256];
    if (!deskicons_state_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[768];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "icons=", 6) != 0) continue;

        const char *v = p + 6;
        if      (strcmp(v, "on")  == 0) cfg->desktop_icons = true;
        else if (strcmp(v, "off") == 0) cfg->desktop_icons = false;
    }
    fclose(f);
}

/*
 * Apply saved cells to the icons we just scanned. Lines for files that are no
 * longer on the desktop simply match nothing (and the next save drops them);
 * the coordinates are re-snapped and clamped by deskicons_layout, because the
 * monitor layout may have changed since they were written.
 */
static void deskicons_state_apply(syn_server_t *s)
{
    char path[256];
    if (!deskicons_state_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "r");
    if (!f) return;   /* never dragged anything — the auto-grid stands */

    char line[768];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        /* The mode the user last chose from the menu wins over synuirc's
         * default, the same way wallpaper.state overrides the configured
         * wallpaper: the setting you changed from the desktop is the one you
         * expect to still be in effect. An unknown name is left alone rather
         * than silently reset — a newer synui may have written it. */
        if (strncmp(p, "arrange=", 8) == 0) {
            syn_arrange_t mode;
            if (syn_arrange_parse(p + 8, &mode))
                s->config.desktop_icon_arrange = mode;
            continue;
        }

        if (strncmp(p, "pos=", 4) != 0) continue;
        p += 4;

        char *c1 = strchr(p, ',');
        if (!c1) continue;
        char *c2 = strchr(c1 + 1, ',');
        if (!c2) continue;
        *c1 = *c2 = '\0';

        int x = atoi(p), y = atoi(c1 + 1);
        const char *name = c2 + 1;
        if (!*name) continue;

        for (int i = 0; i < s->deskicon_count; i++) {
            const char *base = strrchr(s->deskicons[i].path, '/');
            base = base ? base + 1 : s->deskicons[i].path;
            if (strcmp(base, name) != 0) continue;
            s->deskicons[i].pin_x  = x;
            s->deskicons[i].pin_y  = y;
            s->deskicons[i].x      = x;
            s->deskicons[i].y      = y;
            s->deskicons[i].placed = 1;
            break;
        }
    }
    fclose(f);
}

/*
 * Rescan ~/Desktop into s->deskicons. Cheap enough to redo whenever the
 * setting is flipped or an output changes; there is no inotify watch, so a
 * file added behind our back appears on the next reload.
 */
void deskicons_reload(syn_server_t *s)
{
    /* A rescan renumbers everything, so any drag in flight is now pointing at
     * whichever file landed on that index — cancel it rather than move the
     * wrong icon. */
    s->deskicon_drag.active = 0;
    s->deskicon_drag.moved  = 0;
    s->deskicon_drag.idx    = -1;

    s->deskicon_count = 0;
    if (!s->config.desktop_icons) { synui_render_deskicons(s); return; }

    const char *home = getenv("HOME");
    if (!home || !home[0]) { synui_render_deskicons(s); return; }

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/Desktop", home);

    DIR *d = opendir(dir);
    if (!d) { synui_render_deskicons(s); return; }

    struct dirent *de;
    while ((de = readdir(d)) && s->deskicon_count < SYN_DESKICON_MAX) {
        if (de->d_name[0] == '.') continue;   /* dotfiles stay hidden */

        syn_deskicon_t *ic = &s->deskicons[s->deskicon_count];
        memset(ic, 0, sizeof(*ic));
        snprintf(ic->path, sizeof(ic->path), "%s/%s", dir, de->d_name);

        struct stat st;
        if (stat(ic->path, &st) != 0) continue;
        ic->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        ic->size   = st.st_size;
        ic->mtime  = st.st_mtime;

        size_t len = strlen(de->d_name);
        const syn_icon_entry_t *ent = NULL;
        if (!ic->is_dir && len > 8 &&
            strcmp(de->d_name + len - 8, ".desktop") == 0)
            ent = icon_lookup_desktop_path(ic->path);

        if (ent) {
            /* A .desktop names and draws itself; icons.c did the parsing and
             * the icon-theme search. */
            ic->is_desktop = 1;
            snprintf(ic->label, sizeof(ic->label), "%s", ent->display_name);
            snprintf(ic->exec,  sizeof(ic->exec),  "%s", ent->exec);
            ic->icon_surface = ent->icon_surface;
        } else {
            snprintf(ic->label, sizeof(ic->label), "%s", de->d_name);
        }

        s->deskicon_count++;
    }
    closedir(d);

    /* Before the sort: the state file carries the arrange mode as well as the
     * dragged cells, and the mode is what the sort is about to use. Matching an
     * icon by name does not care what order the array is in. */
    deskicons_state_apply(s);

    /* Stable, human order: readdir returns them in whatever order the
     * filesystem feels like, which would reshuffle the desktop every reload. */
    sort_mode = s->config.desktop_icon_arrange;
    qsort(s->deskicons, s->deskicon_count, sizeof(s->deskicons[0]), icon_cmp);

    deskicons_layout(s);
    synui_render_deskicons(s);
}

/*
 * Switch the sort order and re-flow the desktop.
 *
 * Dragged icons lose their cells: an arrange that only reordered the icons the
 * user had never touched would leave the desktop in neither order. That is what
 * every other shell's "arrange by" does too, and the drop that pinned an icon
 * is easy to repeat — so the placements are cleared, and the empty state is
 * written before the rescan so the reload cannot load them straight back in.
 */
void deskicons_arrange(syn_server_t *s, syn_arrange_t mode)
{
    s->config.desktop_icon_arrange = mode;

    for (int i = 0; i < s->deskicon_count; i++)
        s->deskicons[i].placed = 0;

    deskicons_state_save(s);   /* drops every pos= line, keeps the new mode */
    deskicons_reload(s);
}

/*
 * Place the icons on the primary output's grid, inside its usable area so the
 * dock and bars do not sit on top of them.
 *
 * Two passes: icons the user has dragged claim their saved cell first, then
 * everything else flows top-down, left-to-right into the cells that are left.
 * Without the two passes a dragged icon would either be overwritten by the flow
 * or be buried under an icon flowed onto its cell.
 */
void deskicons_layout(syn_server_t *s)
{
    if (s->deskicon_count <= 0) return;

    syn_output_t *o = deskicon_output(s);
    if (!o) return;

    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    int cols, rows;
    deskicon_grid(&area, &cols, &rows);

    /* Which icons have a final cell this pass; also the occupancy map, since
     * an icon's own x/y is where it sits. */
    char settled[SYN_DESKICON_MAX] = {0};

    /* Pass 1: dragged icons. The pinned coords are re-snapped and clamped onto
     * this grid — the output may have been resized, or the position may have
     * been written against a monitor that is no longer here. Always from the
     * pin: snapping x/y would compound, so a bar strip appearing after login,
     * or a grid that briefly lost a row to a smaller monitor, would move the
     * icon permanently instead of just for as long as the box is that shape.
     * Two icons landing on the same cell is settled in array (name) order; the
     * loser flows with the rest rather than hiding underneath. */
    for (int i = 0; i < s->deskicon_count; i++) {
        syn_deskicon_t *ic = &s->deskicons[i];
        if (!ic->placed) continue;

        int col, row, cx, cy;
        deskicon_cell_at(&area, cols, rows, ic->pin_x, ic->pin_y, &col, &row);
        deskicon_cell_origin(&area, col, row, &cx, &cy);
        if (deskicon_cell_taken(s, settled, cx, cy)) continue;

        ic->x = cx; ic->y = cy;
        settled[i] = 1;
    }

    /* Pass 2: everything else, into the first free cell. `next` only ever
     * advances, so the flow keeps its name order. */
    int next = 0;
    for (int i = 0; i < s->deskicon_count; i++) {
        if (settled[i]) continue;

        int cx, cy;
        deskicon_cell_origin(&area, 0, 0, &cx, &cy);
        for (; next < cols * rows; next++) {
            deskicon_cell_origin(&area, next / rows, next % rows, &cx, &cy);
            if (!deskicon_cell_taken(s, settled, cx, cy)) { next++; break; }
        }
        /* A full grid stacks the remainder on the last cell — visibly wrong,
         * but on-screen, which beats scrolling them off the bottom. */
        s->deskicons[i].x = cx;
        s->deskicons[i].y = cy;
        settled[i] = 1;
    }
}

/* Icon index under (lx,ly), or -1. */
int deskicon_at(syn_server_t *s, double lx, double ly)
{
    if (!s->config.desktop_icons) return -1;
    for (int i = 0; i < s->deskicon_count; i++) {
        syn_deskicon_t *ic = &s->deskicons[i];
        if (lx >= ic->x && lx < ic->x + SYN_DESKICON_W &&
            ly >= ic->y && ly < ic->y + SYN_DESKICON_H)
            return i;
    }
    return -1;
}

/* Open icon i — its Exec for a .desktop, xdg-open for anything else. */
void deskicon_activate(syn_server_t *s, int i)
{
    if (i < 0 || i >= s->deskicon_count) return;
    syn_deskicon_t *ic = &s->deskicons[i];

    if (ic->is_desktop && ic->exec[0]) {
        synui_spawn(ic->exec);
        return;
    }

    /* Quote the path: ~/Desktop routinely holds names with spaces, and this
     * string goes through /bin/sh. Single quotes with the standard '\'' escape
     * so even an apostrophe in a filename cannot break out. */
    char cmd[1200];
    size_t n = 0;
    n += snprintf(cmd + n, sizeof(cmd) - n, "xdg-open '");
    for (const char *p = ic->path; *p && n + 8 < sizeof(cmd); p++) {
        if (*p == '\'') n += snprintf(cmd + n, sizeof(cmd) - n, "'\\''");
        else            cmd[n++] = *p;
    }
    snprintf(cmd + n, sizeof(cmd) - n, "'");
    synui_spawn(cmd);
}

void deskicon_select(syn_server_t *s, int i)
{
    if (s->deskicon_selected == i) return;
    s->deskicon_selected = i;
    synui_render_deskicons(s);
}

/* ── Delete: the desktop's own Delete key ────────────────── */

/*
 * Trash, never unlink. The desktop is a folder like any other and Delete there
 * means what Delete means in the file browser: recoverable. `gio trash` is the
 * XDG trash implementation glib already ships, so this does not grow a second
 * one inside the compositor — and it is refused outright when it is missing,
 * because the alternative is deleting the file for real.
 *
 * The reload waits for the child. There is no inotify watch on ~/Desktop, so
 * rescanning before `gio` has finished would find the file still there and
 * leave an icon for something that is on its way out — the same reason
 * deskdrop.c waits on a pipe hangup rather than reloading straight away.
 */
struct trash_watch {
    syn_server_t           *server;
    struct wl_event_source *ev;
    int                     fd;
};

static int trash_watch_cb(int fd, uint32_t mask, void *data)
{
    struct trash_watch *w = data;
    (void)fd; (void)mask;

    deskicons_reload(w->server);

    if (w->ev) wl_event_source_remove(w->ev);
    if (w->fd >= 0) close(w->fd);
    free(w);
    return 0;
}

void deskicon_trash_selected(syn_server_t *s)
{
    int i = s->deskicon_selected;
    if (i < 0 || i >= s->deskicon_count) return;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s", s->deskicons[i].path);
    const char *slash = strrchr(path, '/');
    const char *name  = slash ? slash + 1 : path;

    int pfd[2];
    if (pipe2(pfd, O_CLOEXEC) != 0) return;

    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return; }
    if (pid == 0) {
        setsid();
        synui_child_reset_signals();
        fcntl(pfd[1], F_SETFD, 0);      /* the child holds the write end */
        close(pfd[0]);

        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }
        execlp("gio", "gio", "trash", "--", path, (char *)NULL);
        _exit(127);
    }
    close(pfd[1]);

    struct trash_watch *w = calloc(1, sizeof(*w));
    if (!w) { close(pfd[0]); return; }
    w->server = s;
    w->fd     = pfd[0];

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    w->ev = wl_event_loop_add_fd(loop, pfd[0], WL_EVENT_READABLE,
                                 trash_watch_cb, w);
    if (!w->ev) { close(pfd[0]); free(w); return; }

    /* Said out loud, because the icon does not disappear until `gio` returns
     * and a Delete that looks like nothing happened invites a second press. */
    notif_post(s, "Desktop", "Moved to Trash", name,
               NOTIF_URGENCY_LOW, -1, 0);

    s->deskicon_selected = -1;
}

/* ── Drag to move ────────────────────────────────────────── */


void deskicon_drag_begin(syn_server_t *s, int idx, double lx, double ly)
{
    if (idx < 0 || idx >= s->deskicon_count) return;

    s->deskicon_drag.active  = 1;
    s->deskicon_drag.moved   = 0;
    s->deskicon_drag.idx     = idx;
    s->deskicon_drag.start_x = lx;
    s->deskicon_drag.start_y = ly;
    s->deskicon_drag.orig_x  = s->deskicons[idx].x;
    s->deskicon_drag.orig_y  = s->deskicons[idx].y;
}

void deskicon_drag_motion(syn_server_t *s, double lx, double ly)
{
    if (!s->deskicon_drag.active) return;

    int i = s->deskicon_drag.idx;
    if (i < 0 || i >= s->deskicon_count) { s->deskicon_drag.active = 0; return; }

    double dx = lx - s->deskicon_drag.start_x;
    double dy = ly - s->deskicon_drag.start_y;
    bool lifted = false;
    if (!s->deskicon_drag.moved) {
        if (hypot(dx, dy) < DESKICON_DRAG_SLOP) return;
        s->deskicon_drag.moved = 1;
        lifted = true;
    }

    /* Free-floating while the button is down — the drop is what snaps. Clamped
     * to the usable area because render.c draws the icons into a buffer that
     * covers exactly that box: a cell dragged past its edge would be cropped
     * away mid-drag instead of following the cursor. */
    int x = s->deskicon_drag.orig_x + (int)dx;
    int y = s->deskicon_drag.orig_y + (int)dy;

    syn_output_t *o = deskicon_output(s);
    if (o) {
        struct wlr_box area;
        output_usable_box_of(s, o, &area);
        int max_x = area.x + area.width  - SYN_DESKICON_W;
        int max_y = area.y + area.height - SYN_DESKICON_H;
        if (x < area.x) x = area.x;
        if (y < area.y) y = area.y;
        if (x > max_x)  x = max_x;
        if (y > max_y)  y = max_y;
    }

    /* A high-polling-rate mouse sends motion far faster than a pixel of travel,
     * so most events land on the cell's current position and have nothing to
     * do at all. */
    if (!lifted && s->deskicons[i].x == x && s->deskicons[i].y == y) return;

    s->deskicons[i].x = x;
    s->deskicons[i].y = y;

    /* Crossing the slop is the one motion that repaints: it lifts the icon out
     * of the desktop buffer and into the drag layer. Every motion after that is
     * a node move. */
    if (lifted) synui_render_deskicons(s);
    else        synui_move_deskicon_drag(s);
}

void deskicon_drag_end(syn_server_t *s, double lx, double ly)
{
    /* The icon already tracks the cursor, so its own x/y is the drop point;
     * the release coords are only here to match dock_drag_end's shape. */
    (void)lx; (void)ly;
    if (!s->deskicon_drag.active) return;

    int  i      = s->deskicon_drag.idx;
    bool moved  = s->deskicon_drag.moved;
    int  orig_x = s->deskicon_drag.orig_x;
    int  orig_y = s->deskicon_drag.orig_y;

    s->deskicon_drag.active = 0;
    s->deskicon_drag.moved  = 0;
    s->deskicon_drag.idx    = -1;

    if (i < 0 || i >= s->deskicon_count) return;
    if (!moved) return;   /* a press that never travelled: a click, not a drop */

    syn_output_t *o = deskicon_output(s);
    if (o) {
        struct wlr_box area;
        output_usable_box_of(s, o, &area);

        int cols, rows, col, row, cx, cy;
        deskicon_grid(&area, &cols, &rows);
        deskicon_cell_at(&area, cols, rows,
                         s->deskicons[i].x, s->deskicons[i].y, &col, &row);
        deskicon_cell_origin(&area, col, row, &cx, &cy);

        /* Dropping onto an occupied cell swaps the two: the sitting icon takes
         * the cell this drag started from. Swapping rather than bumping keeps
         * the drop where the user aimed it, and nothing ends up underneath
         * anything else. The displaced icon becomes `placed` too, or the flow
         * in deskicons_layout would just move it straight back. */
        for (int k = 0; k < s->deskicon_count; k++) {
            if (k == i) continue;
            if (s->deskicons[k].x != cx || s->deskicons[k].y != cy) continue;
            s->deskicons[k].pin_x  = orig_x;
            s->deskicons[k].pin_y  = orig_y;
            s->deskicons[k].x      = orig_x;
            s->deskicons[k].y      = orig_y;
            s->deskicons[k].placed = 1;
            break;
        }

        /* The snapped cell is the pin: it is where the user aimed, expressed on
         * the grid that was on screen when they aimed at it. */
        s->deskicons[i].pin_x = cx;
        s->deskicons[i].pin_y = cy;
        s->deskicons[i].x     = cx;
        s->deskicons[i].y     = cy;
    } else {
        /* No output to snap against (the monitor went away mid-drag): the drop
         * point itself is the pin, and the next layout will grid it. */
        s->deskicons[i].pin_x = s->deskicons[i].x;
        s->deskicons[i].pin_y = s->deskicons[i].y;
    }

    s->deskicons[i].placed = 1;
    deskicons_layout(s);        /* re-snap, and reflow whatever is not placed */
    deskicons_state_save(s);
    synui_render_deskicons(s);
}

/* ── Drops from other applications (deskdrop.c) ──────────── */

/* The icon whose file is called `name`, or -1. Matched on the last path
 * component because that is all the copy knew it was creating. */
static int deskicon_by_name(syn_server_t *s, const char *name)
{
    for (int i = 0; i < s->deskicon_count; i++) {
        const char *slash = strrchr(s->deskicons[i].path, '/');
        const char *base  = slash ? slash + 1 : s->deskicons[i].path;
        if (strcmp(base, name) == 0) return i;
    }
    return -1;
}

/* Is any icon but `except` sitting on cell origin (x,y) right now? Unlike
 * deskicon_cell_taken() this asks about the layout as it stands, not about one
 * pass in progress — a drop lands on a desktop that is already laid out. */
static bool deskicon_cell_busy(syn_server_t *s, int except, int x, int y)
{
    for (int i = 0; i < s->deskicon_count; i++)
        if (i != except && s->deskicons[i].x == x && s->deskicons[i].y == y)
            return true;
    return false;
}

/*
 * Put files that just arrived from another application where they were dropped.
 *
 * Without this a drop would obey the same rule as any other new file — flow to
 * the first free cell, column-major — and land in the top-left corner of the
 * screen no matter where the user aimed. A drop is a placement, so it pins,
 * exactly like dragging an icon does, and is persisted the same way.
 *
 * Occupied cells are stepped over rather than swapped through: a drag inside
 * the desktop has a cell of its own to give away, and this does not. A drop of
 * several files fills the cells after the first, so a multi-file drag stays
 * together instead of piling up on one square.
 */
void deskicons_place_dropped(syn_server_t *s, const char *const *names, int n,
                             int lx, int ly)
{
    if (n <= 0 || s->deskicon_count == 0) return;

    syn_output_t *o = deskicon_output(s);
    if (!o) return;

    struct wlr_box area;
    output_usable_box_of(s, o, &area);

    int cols, rows, col, row;
    deskicon_grid(&area, &cols, &rows);
    /* The cursor is the middle of where the user meant the icon to be, and a
     * cell is addressed by its top-left, so aim the cell at the cursor. */
    deskicon_cell_at(&area, cols, rows,
                     lx - SYN_DESKICON_W / 2, ly - SYN_DESKICON_H / 2,
                     &col, &row);

    int total = cols * rows;
    int next  = col * rows + row;   /* column-major, as deskicons_layout flows */
    bool any  = false;

    for (int k = 0; k < n; k++) {
        int i = deskicon_by_name(s, names[k]);
        if (i < 0) continue;        /* the copy failed, or something ate it */

        for (int t = 0; t < total; t++) {
            int cell = (next + t) % total;
            int cx, cy;
            deskicon_cell_origin(&area, cell / rows, cell % rows, &cx, &cy);
            if (deskicon_cell_busy(s, i, cx, cy)) continue;

            s->deskicons[i].pin_x  = cx;
            s->deskicons[i].pin_y  = cy;
            s->deskicons[i].x      = cx;
            s->deskicons[i].y      = cy;
            s->deskicons[i].placed = 1;
            next = (cell + 1) % total;
            any  = true;
            break;
        }
    }

    if (!any) return;
    deskicons_layout(s);
    deskicons_state_save(s);
    synui_render_deskicons(s);
}
