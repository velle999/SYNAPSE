/*
 * taskmgr.c — process + resource monitor (Ctrl+Alt+Del)
 *
 * A compositor-drawn modal panel: a system overview (CPU, RAM, swap, and
 * every GPU gpu.c found) above a process table sorted by CPU, memory or
 * VRAM, with SIGTERM/SIGKILL behind a confirmation line.
 *
 * Sampling is a straight /proc walk on the main thread, driven by a 1 Hz
 * event-loop timer that only runs while the panel is visible. It is a few
 * hundred opens of small procfs files — under a millisecond here — so unlike
 * secfeed and synapd_mon (which block on sockets) it does not need a thread,
 * and staying single-threaded keeps it free of the shutdown races those two
 * had to grow fds and stop flags to handle.
 *
 * CPU% is top's definition: a share of one core, so a busy 8-thread process
 * legitimately reads over 100%. It needs two samples to exist, so the first
 * one after the panel opens falls back to the process's lifetime average —
 * otherwise the table would show a column of zeroes for a second, which reads
 * as "nothing is running" exactly when the user opened it to find out what is.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/util/log.h>

#include "synui.h"

#define TASKMGR_POLL_MS 1000

static long page_kb(void)
{
    static long kb;
    if (!kb) kb = sysconf(_SC_PAGESIZE) / 1024;
    return kb;
}

static long ncpu(void)
{
    static long n;
    if (!n) n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? n : 1;
}

static long hz(void)
{
    static long h;
    if (!h) h = sysconf(_SC_CLK_TCK);
    return h > 0 ? h : 100;
}

/* ── /proc sampling ──────────────────────────────────────── */

/* Total and busy jiffies across all cores, from /proc/stat's first line. */
static void read_cpu_totals(unsigned long long *total, unsigned long long *busy)
{
    *total = *busy = 0;

    FILE *f = fopen("/proc/stat", "r");
    if (!f) return;

    unsigned long long v[10] = {0};
    int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &v[0], &v[1], &v[2], &v[3], &v[4],
                   &v[5], &v[6], &v[7], &v[8], &v[9]);
    fclose(f);
    if (n < 4) return;

    for (int i = 0; i < 10; i++) *total += v[i];
    *busy = *total - v[3] - v[4];   /* minus idle and iowait */
}

static void read_meminfo(syn_taskmgr_t *t)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;

    unsigned long avail = 0, swap_free = 0;
    char key[64];
    unsigned long val;

    t->mem_total_kb = t->swap_total_kb = 0;

    while (fscanf(f, "%63[^:]: %lu kB\n", key, &val) == 2) {
        if      (!strcmp(key, "MemTotal"))     t->mem_total_kb  = val;
        else if (!strcmp(key, "MemAvailable")) avail            = val;
        else if (!strcmp(key, "SwapTotal"))    t->swap_total_kb = val;
        else if (!strcmp(key, "SwapFree"))     swap_free        = val;
    }
    fclose(f);

    /* "Used" as MemTotal - MemAvailable: the number a user recognises, since
     * it counts reclaimable page cache as free rather than as used. */
    t->mem_used_kb  = t->mem_total_kb > avail ? t->mem_total_kb - avail : 0;
    t->swap_used_kb = t->swap_total_kb > swap_free
                        ? t->swap_total_kb - swap_free : 0;
}

static double read_uptime_jiffies(void)
{
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return 0;
    double up = 0;
    if (fscanf(f, "%lf", &up) != 1) up = 0;
    fclose(f);
    return up * (double)hz();
}

/* utime+stime, ppid and RSS out of /proc/<pid>/stat.
 *
 * comm sits in parens and may itself contain spaces and parens ("(Web Content)"),
 * so the fields are counted from the LAST ')' rather than by splitting on
 * whitespace from the start. Field numbers below are as in proc(5); token[0]
 * here is field 3 (state). */
static bool read_proc_stat(pid_t pid, char *comm, size_t comm_n,
                           pid_t *ppid, unsigned long long *jiffies,
                           unsigned long *rss_kb, double *starttime)
{
    char path[64], line[1024];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);

    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = fread(line, 1, sizeof(line) - 1, f);
    fclose(f);
    if (n == 0) return false;
    line[n] = '\0';

    char *open = strchr(line, '(');
    char *close = strrchr(line, ')');
    if (!open || !close || close < open) return false;

    size_t len = (size_t)(close - open - 1);
    if (len >= comm_n) len = comm_n - 1;
    memcpy(comm, open + 1, len);
    comm[len] = '\0';

    /* Fields 3.. — see the comment above for the numbering. */
    unsigned long long utime = 0, stime = 0, start = 0;
    unsigned long rss_pages = 0;
    int parent = 0;

    char *p = close + 1;
    int field = 3;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        char *end = p;
        while (*end && *end != ' ') end++;

        switch (field) {
        case 4:  parent    = atoi(p);              break;
        case 14: utime     = strtoull(p, NULL, 10); break;
        case 15: stime     = strtoull(p, NULL, 10); break;
        case 22: start     = strtoull(p, NULL, 10); break;
        case 24: rss_pages = strtoul(p, NULL, 10);  break;
        }
        if (field >= 24) break;

        p = end;
        field++;
    }

    *ppid      = (pid_t)parent;
    *jiffies   = utime + stime;
    *rss_kb    = rss_pages * (unsigned long)page_kb();
    *starttime = (double)start;
    return true;
}

/* argv[0]'s basename, for the rows the panel actually shows. comm is capped at
 * 15 bytes by the kernel, which turns Firefox's content processes into
 * "Isolated Web Co" and every Electron app into a guess; the cmdline is the
 * name the user recognises. Only called for the visible rows — doing it for
 * all ~500 processes every second would be a lot of opens for text that is
 * scrolled off screen. */
static void pretty_name(pid_t pid, char *name, size_t n)
{
    char path[64], buf[256];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", (int)pid);

    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (got == 0) return;   /* kernel thread, or it exited under us */
    buf[got] = '\0';

    /* cmdline is *usually* NUL-separated, so argv[0] is the first C string in
     * it — but Firefox and Chrome set their process titles by overwriting the
     * whole argv area with one flat space-separated string, and then there is
     * no NUL to stop at and the "basename" is the entire command line
     * ("firefox -contentproc -isForBrowser -pre…"). Cut at the first space as
     * well as the first NUL. */
    char *arg0 = buf;
    char *space = strchr(arg0, ' ');
    if (space) *space = '\0';

    char *slash = strrchr(arg0, '/');
    if (slash && slash[1]) arg0 = slash + 1;
    if (!*arg0) return;

    /* An interpreter's argv[0] is the interpreter ("python3", "sh"), which
     * tells the user nothing — keep comm's version in that case. */
    if (!strncmp(arg0, "python", 6) || !strcmp(arg0, "sh") ||
        !strcmp(arg0, "bash") || !strcmp(arg0, "node") ||
        !strcmp(arg0, "perl") || !strcmp(arg0, "ruby"))
        return;

    snprintf(name, n, "%.*s", (int)n - 1, arg0);
}

/* Does this pid own a window synui is managing? Marked in the table so the
 * user can tell "the thing I can see" from its helper processes. */
static bool pid_has_window(syn_server_t *s, pid_t pid)
{
    for (int i = 0; i < WORKSPACE_MAX; i++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[i].windows, link)
            if (view_pid(v) == pid) return true;
    }
    return false;
}

/* Previous poll's jiffie count for a pid, so CPU% is a delta rather than a
 * lifetime average. -1 when we have not seen this pid before (it just spawned,
 * or this is the first sample after the panel opened). */
static long long prev_jiffies(syn_taskmgr_t *t, pid_t pid)
{
    for (int i = 0; i < t->prev_n; i++)
        if (t->prev[i].pid == pid) return (long long)t->prev[i].jiffies;
    return -1;
}

static int cmp_procs(const void *a, const void *b, void *arg)
{
    const syn_tm_proc_t *x = a, *y = b;
    syn_tm_sort_t sort = *(syn_tm_sort_t *)arg;

    switch (sort) {
    case TM_SORT_MEM:
        if (x->rss_kb != y->rss_kb) return y->rss_kb > x->rss_kb ? 1 : -1;
        break;
    case TM_SORT_GPU:
        if (x->vram_kb != y->vram_kb) return y->vram_kb > x->vram_kb ? 1 : -1;
        break;
    case TM_SORT_PID:
        return x->pid - y->pid;
    case TM_SORT_CPU:
    default:
        if (x->cpu != y->cpu) return y->cpu > x->cpu ? 1 : -1;
        break;
    }
    /* Same key value — order by pid so rows do not shuffle between polls. */
    return x->pid - y->pid;
}

void taskmgr_sample(syn_server_t *s)
{
    syn_taskmgr_t *t = &s->taskmgr;

    gpu_sample(s);
    read_meminfo(t);

    unsigned long long total = 0, busy = 0;
    read_cpu_totals(&total, &busy);

    unsigned long long dtotal = total > t->prev_total ? total - t->prev_total : 0;
    if (dtotal && t->prev_total)
        t->cpu_pct = 100.0 * (double)(busy - t->prev_busy) / (double)dtotal;

    double now_jiffies = read_uptime_jiffies();
    uid_t me = getuid();
    int n = 0;

    DIR *d = opendir("/proc");
    if (!d) return;

    struct dirent *e;
    while ((e = readdir(d)) && n < TASKMGR_MAX_PROCS) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        pid_t pid = (pid_t)atoi(e->d_name);
        if (pid <= 0) continue;

        char path[64];
        snprintf(path, sizeof(path), "/proc/%d", (int)pid);
        struct stat st;
        if (stat(path, &st) != 0) continue;   /* exited between readdir and now */

        if (t->own_only && st.st_uid != me) continue;

        syn_tm_proc_t *p = &t->procs[n];
        pid_t ppid;
        double start;
        char comm[40] = {0};

        if (!read_proc_stat(pid, comm, sizeof(comm), &ppid,
                            &p->jiffies, &p->rss_kb, &start))
            continue;

        /* Kernel threads: children of kthreadd, no address space, cannot be
         * killed and have no VRAM. They would be a hundred rows of noise. */
        if (pid == 2 || ppid == 2) continue;

        p->pid = pid;
        p->uid = st.st_uid;
        snprintf(p->name, sizeof(p->name), "%s", comm);

        long long prev = prev_jiffies(t, pid);
        if (prev >= 0 && dtotal > 0) {
            long long dj = (long long)p->jiffies - prev;
            if (dj < 0) dj = 0;
            p->cpu = 100.0 * (double)dj / (double)dtotal * (double)ncpu();
        } else {
            /* First sight of this pid — lifetime average (what ps prints). */
            double life = now_jiffies - start;
            p->cpu = life > 0 ? 100.0 * (double)p->jiffies / life : 0.0;
        }
        if (p->cpu < 0) p->cpu = 0;

        p->vram_kb    = gpu_proc_vram_kb(s, pid);
        p->has_window = pid_has_window(s, pid);
        n++;
    }
    closedir(d);

    t->n = n;
    t->prev_total = total;
    t->prev_busy  = busy;

    /* This poll's jiffies become the next poll's baseline. */
    t->prev_n = n;
    for (int i = 0; i < n; i++) {
        t->prev[i].pid     = t->procs[i].pid;
        t->prev[i].jiffies = t->procs[i].jiffies;
    }

    qsort_r(t->procs, (size_t)n, sizeof(t->procs[0]), cmp_procs, &t->sort);

    /* Once the user has picked a process, the selection follows that process
     * and not the row: with a CPU sort the rows reshuffle every second, and an
     * index-based cursor would wander off what they were watching — and, worse,
     * the kill confirmation could end up naming a different process than the
     * one they aimed at.
     *
     * sel_pid is 0 until they actually move the cursor, and then the selection
     * just tracks the top row. Pinning it to row 0 of the *opening* sample
     * would be pinning it to a lie: that sample's CPU% is a lifetime average,
     * so a process that was busy minutes ago sorts first, and a second later
     * the cursor visibly leaps down the table as the real rates come in. */
    if (t->sel_pid) {
        int found = -1;
        for (int i = 0; i < n; i++)
            if (t->procs[i].pid == t->sel_pid) { found = i; break; }
        if (found >= 0) t->selected = found;
        else            t->sel_pid = 0;   /* it exited — fall back to the index */
    }
    if (t->selected >= n) t->selected = n ? n - 1 : 0;
    if (t->selected < 0)  t->selected = 0;

    for (int i = 0; i < n && i < TASKMGR_ROWS + t->scroll; i++)
        if (i >= t->scroll) pretty_name(t->procs[i].pid, t->procs[i].name,
                                        sizeof(t->procs[i].name));
}

/* ── Poll timer ──────────────────────────────────────────── */

static int taskmgr_tick(void *data)
{
    syn_server_t *s = data;
    if (!s->taskmgr.visible) return 0;

    taskmgr_sample(s);
    synui_render_taskmgr(s);
    wl_event_source_timer_update(s->taskmgr.timer, TASKMGR_POLL_MS);
    return 0;
}

void taskmgr_init(syn_server_t *s)
{
    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->taskmgr.timer = wl_event_loop_add_timer(loop, taskmgr_tick, s);
    s->taskmgr.sort  = TM_SORT_CPU;
    gpu_init(s);
}

void taskmgr_finish(syn_server_t *s)
{
    if (s->taskmgr.timer) {
        wl_event_source_remove(s->taskmgr.timer);
        s->taskmgr.timer = NULL;
    }
    gpu_finish(s);
}

/* ── Panel ───────────────────────────────────────────────── */

void taskmgr_show(syn_server_t *s)
{
    syn_taskmgr_t *t = &s->taskmgr;

    t->visible   = 1;
    t->confirm   = TM_CONFIRM_NONE;
    t->status[0] = '\0';

    /* Open at the top of the table, not wherever the cursor was left last time
     * (that process may be long gone, and the stale scroll offset would open
     * the panel showing rows 40-54). */
    t->selected = 0;
    t->scroll   = 0;
    t->sel_pid  = 0;

    /* Drop the previous session's baselines: they are however many minutes
     * stale, and differencing against them would print a nonsense CPU% for
     * one poll. Starting empty makes the first sample take the lifetime-average
     * path instead. */
    t->prev_n = 0;
    t->prev_total = t->prev_busy = 0;

    taskmgr_sample(s);
    synui_render_taskmgr(s);
    wl_event_source_timer_update(t->timer, TASKMGR_POLL_MS);
}

void taskmgr_hide(syn_server_t *s)
{
    s->taskmgr.visible = 0;
    s->taskmgr.confirm = TM_CONFIRM_NONE;
    wl_event_source_timer_update(s->taskmgr.timer, 0);
    synui_render_taskmgr(s);
    ctlpanel_child_closed(s, "taskmgr");
}

void taskmgr_toggle(syn_server_t *s)
{
    if (s->taskmgr.visible) taskmgr_hide(s);
    else                    taskmgr_show(s);
}

const char *taskmgr_sort_label(syn_tm_sort_t sort)
{
    switch (sort) {
    case TM_SORT_CPU: return "cpu";
    case TM_SORT_MEM: return "mem";
    case TM_SORT_GPU: return "gpu";
    case TM_SORT_PID: return "pid";
    default:          return "?";
    }
}

/* Keep the cursor inside the visible window of rows. */
static void taskmgr_scroll_to_selection(syn_taskmgr_t *t)
{
    if (t->selected < t->scroll)
        t->scroll = t->selected;
    if (t->selected >= t->scroll + TASKMGR_ROWS)
        t->scroll = t->selected - TASKMGR_ROWS + 1;
    if (t->scroll < 0) t->scroll = 0;
}

static void taskmgr_move(syn_server_t *s, int delta)
{
    syn_taskmgr_t *t = &s->taskmgr;
    if (t->n == 0) return;

    t->selected += delta;
    if (t->selected < 0)       t->selected = 0;
    if (t->selected >= t->n)   t->selected = t->n - 1;
    t->sel_pid = t->procs[t->selected].pid;
    taskmgr_scroll_to_selection(t);
}

/* Signals the pid alone, never its process group: killing the group would take
 * down a whole shell session when the user aimed at one background job in it. */
static void taskmgr_do_kill(syn_server_t *s)
{
    syn_taskmgr_t *t = &s->taskmgr;
    int sig = (t->confirm == TM_CONFIRM_KILL) ? SIGKILL : SIGTERM;
    pid_t pid = t->confirm_pid;

    t->confirm = TM_CONFIRM_NONE;

    if (kill(pid, sig) != 0) {
        snprintf(t->status, sizeof(t->status), "%s %d failed: %s",
                 sig == SIGKILL ? "SIGKILL" : "SIGTERM",
                 (int)pid, strerror(errno));
        wlr_log(WLR_ERROR, "synui: taskmgr: kill(%d, %d): %s",
                (int)pid, sig, strerror(errno));
        return;
    }

    snprintf(t->status, sizeof(t->status), "sent %s to %s (%d)",
             sig == SIGKILL ? "SIGKILL" : "SIGTERM", t->confirm_name, (int)pid);
    wlr_log(WLR_INFO, "synui: taskmgr: sent %s to pid %d (%s)",
            sig == SIGKILL ? "SIGKILL" : "SIGTERM", (int)pid, t->confirm_name);

    /* Resample now so the row disappears (or visibly does not) immediately —
     * waiting up to a second for the next tick reads as the key not working. */
    taskmgr_sample(s);
}

/* Arm the confirmation line. Nothing is signalled until the user answers it. */
static void taskmgr_ask_kill(syn_server_t *s, syn_tm_confirm_t what)
{
    syn_taskmgr_t *t = &s->taskmgr;
    if (t->n == 0 || t->selected >= t->n) return;

    syn_tm_proc_t *p = &t->procs[t->selected];

    /* Two pids we refuse outright. init because the kernel panics if it dies;
     * ourselves because a compositor that SIGKILLs itself takes the session and
     * every window in it down with no chance to ask "are you sure". */
    if (p->pid == 1) {
        snprintf(t->status, sizeof(t->status), "refusing to signal init (pid 1)");
        return;
    }
    if (p->pid == getpid()) {
        snprintf(t->status, sizeof(t->status),
                 "refusing to signal synui \xc2\xb7 use Super+Shift+Q to quit");
        return;
    }

    t->confirm     = what;
    t->confirm_pid = p->pid;
    snprintf(t->confirm_name, sizeof(t->confirm_name), "%s", p->name);
    t->status[0] = '\0';
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * See the panel pointer contract in synui.h — including the rule that a panel
 * with a destructive row does not put it on a click. THIS IS THAT PANEL. x and
 * Shift+X kill processes and they stay on the keyboard, where they are spelled
 * out and confirmed; a click here picks the process and nothing more. A mouse
 * that could SIGKILL something by being clicked twice near the wrong row is not
 * a feature anybody asked for.
 *
 * A click while a kill is pending answers nothing: the panel is asking y/n and
 * a click is neither, so it is swallowed. Same reason the key handler refuses
 * everything but y/n there. */

int taskmgr_motion(syn_server_t *s, double lx, double ly)
{
    syn_taskmgr_t *t = &s->taskmgr;
    if (!t->visible) return 0;
    if (t->confirm != TM_CONFIRM_NONE) return 1;

    int i = hit_index_at(&t->hit, lx, ly);
    if (i < 0 || i >= t->n || i == t->selected) return 1;
    t->selected = i;
    t->sel_pid  = t->procs[i].pid;
    synui_render_taskmgr(s);
    return 1;
}

int taskmgr_click(syn_server_t *s, double lx, double ly, uint32_t button,
                  uint32_t time_msec)
{
    (void)button; (void)time_msec;
    syn_taskmgr_t *t = &s->taskmgr;
    if (!t->visible) return 0;

    if (!hit_in_panel(&t->hit, lx, ly)) {
        /* A pending kill has to be answered, not clicked away — the same reason
         * the Bluetooth panel refuses to close under a pairing prompt. Cancel
         * it, which is the safe reading of "the user looked somewhere else". */
        if (t->confirm != TM_CONFIRM_NONE) {
            t->confirm = TM_CONFIRM_NONE;
            snprintf(t->status, sizeof(t->status), "cancelled");
            synui_render_taskmgr(s);
            return 1;
        }
        taskmgr_hide(s);
        return 1;
    }
    return taskmgr_motion(s, lx, ly);
}

int taskmgr_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    syn_taskmgr_t *t = &s->taskmgr;
    if (!t->visible) return 0;
    if (delta == 0 || t->confirm != TM_CONFIRM_NONE) return 1;

    /* taskmgr_move() drags the window along with the selection, so there is one
     * notion of where the table is rather than two that can disagree. */
    taskmgr_move(s, delta > 0 ? 3 : -3);
    synui_render_taskmgr(s);
    return 1;
}

int taskmgr_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    syn_taskmgr_t *t = &s->taskmgr;
    if (!t->visible) return 0;

    /* Modified combos (Super+…) still reach the global bind table, so the user
     * can switch workspace or quit with the panel up. Shift is the exception:
     * Shift+X is this panel's SIGKILL, and no global bind uses a bare Shift. */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    /* While a kill is pending the panel answers only y/n: any other key would
     * be ambiguous about whether it meant "yes". */
    if (t->confirm != TM_CONFIRM_NONE) {
        switch (sym) {
        case XKB_KEY_y:
        case XKB_KEY_Y:
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
            taskmgr_do_kill(s);
            break;
        default:
            t->confirm = TM_CONFIRM_NONE;
            snprintf(t->status, sizeof(t->status), "cancelled");
            break;
        }
        synui_render_taskmgr(s);
        return 1;
    }

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        taskmgr_hide(s);
        return 1;

    case XKB_KEY_Up:
    case XKB_KEY_k:
        taskmgr_move(s, -1);
        break;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        taskmgr_move(s, +1);
        break;
    case XKB_KEY_Page_Up:
        taskmgr_move(s, -TASKMGR_ROWS);
        break;
    case XKB_KEY_Page_Down:
        taskmgr_move(s, +TASKMGR_ROWS);
        break;
    case XKB_KEY_Home:
        taskmgr_move(s, -t->n);
        break;
    case XKB_KEY_End:
        taskmgr_move(s, +t->n);
        break;

    case XKB_KEY_c:
        t->sort = TM_SORT_CPU;
        goto resort;
    case XKB_KEY_m:
        t->sort = TM_SORT_MEM;
        goto resort;
    case XKB_KEY_g:
        t->sort = TM_SORT_GPU;
        goto resort;
    case XKB_KEY_p:
        t->sort = TM_SORT_PID;
        goto resort;

    case XKB_KEY_u:
        t->own_only = !t->own_only;
        snprintf(t->status, sizeof(t->status), "showing %s processes",
                 t->own_only ? "your" : "all");
        goto resort;

    case XKB_KEY_x:
        taskmgr_ask_kill(s, TM_CONFIRM_TERM);
        break;
    case XKB_KEY_X:
        taskmgr_ask_kill(s, TM_CONFIRM_KILL);
        break;

    case XKB_KEY_r:
        taskmgr_sample(s);
        snprintf(t->status, sizeof(t->status), "refreshed");
        break;

    default:
        return 1;   /* modal: swallow other unmodified keys while open */
    }

    synui_render_taskmgr(s);
    return 1;

resort:
    /* Re-sort now rather than at the next tick: the sort key changing without
     * the table moving for up to a second looks like the key was ignored. */
    taskmgr_sample(s);
    taskmgr_scroll_to_selection(t);
    synui_render_taskmgr(s);
    return 1;
}
