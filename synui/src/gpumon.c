/*
 * gpumon.c — publishing GPU telemetry to the desktop widget.
 *
 * Kept out of gpu.c deliberately. gpu.c is a self-contained reader of NVML and
 * amdgpu sysfs, and tests/gpu_fdinfo_test.c LINKS IT ALONE — the moment the
 * sampling code called into widgets.c to ask "is the widget on", that test
 * stopped linking. Reading the card and deciding when to publish are two
 * concerns with two dependency sets, so they are two files.
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
#include <unistd.h>

#include <wlr/util/log.h>

#include "synui.h"

/* ── Publishing for the desktop widget ───────────────────── */
/*
 * The SYS://MONITOR widget is quickshell QML: it reads /proc/stat and
 * /proc/meminfo with a FileView and cannot open an NVML handle at all, which is
 * why it showed CPU and memory and no GPU. So the figures are pushed to a file
 * it CAN read, exactly as game.c publishes for the waybar indicator — under
 * XDG_RUNTIME_DIR, written to a temporary and renamed, so a reader never sees
 * half a sample.
 *
 * ⚠ Gated on the widget actually being switched on. That widget's own rule is
 * that a desktop widget nobody is looking at should not be the reason a core
 * wakes up; if the compositor sampled regardless, the cost would simply move
 * from quickshell to here and the rule would be satisfied only on paper.
 *
 * Device figures only — gpu_sample_devices(), never gpu_sample() — because the
 * per-process table is the expensive half and nothing here reads it.
 */
#define GPU_PUBLISH_MS 3000   /* the widget's own tick; faster would be thrown away */

static void gpu_publish(syn_server_t *s)
{
    const char *rtdir = getenv("XDG_RUNTIME_DIR");
    if (!rtdir || !*rtdir) return;   /* headless test rig: nothing reads this */

    char path[256], tmp[256];
    snprintf(path, sizeof(path), "%s/synui-gpu", rtdir);
    snprintf(tmp,  sizeof(tmp),  "%s/synui-gpu.tmp", rtdir);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: gpu: cannot write '%s': %s",
                tmp, strerror(errno));
        return;
    }

    fprintf(f, "count=%d\n", s->gpu_n);
    for (int i = 0; i < s->gpu_n; i++) {
        const syn_gpu_t *g = &s->gpu[i];
        /* -1 means "this back end cannot report that figure" and goes out AS
         * -1: the widget has to be able to tell an unavailable reading from a
         * real zero, or an idle card and a silent one look the same. */
        fprintf(f, "%d.name=%s\n",           i, g->name);
        fprintf(f, "%d.util=%d\n",           i, g->util);
        fprintf(f, "%d.temp_c=%d\n",         i, g->temp_c);
        fprintf(f, "%d.power_w=%d\n",        i, g->power_w);
        fprintf(f, "%d.vram_used_kb=%lu\n",  i, g->vram_used_kb);
        fprintf(f, "%d.vram_total_kb=%lu\n", i, g->vram_total_kb);
    }

    if (fclose(f) != 0) {          /* a short write must not be renamed over */
        wlr_log(WLR_ERROR, "synui: gpu: write failed '%s': %s",
                tmp, strerror(errno));
        unlink(tmp);
        return;
    }

    if (rename(tmp, path) != 0) {
        wlr_log(WLR_ERROR, "synui: gpu: cannot rename onto '%s': %s",
                path, strerror(errno));
        unlink(tmp);
    }
}

static int gpu_mon_tick(void *data)
{
    syn_server_t *s = data;

    if (widget_state_is_on("sysmon")) {
        gpu_sample_devices(s);
        gpu_publish(s);
    }

    wl_event_source_timer_update(s->gpu_mon_timer, GPU_PUBLISH_MS);
    return 0;
}

void gpu_mon_init(syn_server_t *s)
{
    /* No back end, nothing to say. A machine with no GPU must not pay a wakeup
     * every three seconds to write a file that would always read count=0.
     * ⚠ Depends on gpu_init() having run, which taskmgr_init() does. */
    if (s->gpu_n == 0) return;

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    s->gpu_mon_timer = wl_event_loop_add_timer(loop, gpu_mon_tick, s);
    if (s->gpu_mon_timer)
        wl_event_source_timer_update(s->gpu_mon_timer, GPU_PUBLISH_MS);
}

void gpu_mon_finish(syn_server_t *s)
{
    if (s->gpu_mon_timer) {
        wl_event_source_remove(s->gpu_mon_timer);
        s->gpu_mon_timer = NULL;
    }

    /* Leave no stale sample behind. A synui that exits under load would
     * otherwise leave a file insisting the card is at 90%, and the next shell
     * to start would draw that until the first tick replaced it — the same
     * trap game.c documents for its own "on" file. */
    const char *rtdir = getenv("XDG_RUNTIME_DIR");
    if (rtdir && *rtdir) {
        char path[256];
        snprintf(path, sizeof(path), "%s/synui-gpu", rtdir);
        unlink(path);
    }
}
