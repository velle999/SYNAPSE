/*
 * offload.c — give VRAM back when something else needs it, and take it again.
 *
 * pressure.c holds the policy and is pure so it can be tested without a GPU;
 * this is the half that reads the real card and carries the answer out. See
 * pressure.h for why the shed and restore thresholds are asymmetric — that
 * asymmetry is the whole reason this does not thrash.
 *
 * ⛔ EVERY MOVE IS A RELOAD. llama.cpp fixes n_gpu_layers when the model is
 * created and has no API to migrate a layer, so "move it to RAM" is destroy +
 * load: tens of seconds for a multi-GB model, and the KV cache is gone with
 * it. That is the cost this thread is spending, which is why it polls in
 * minutes rather than seconds and why the policy has a dwell time on top.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "synapd.h"
#include "offload.h"
#include "pressure.h"
#include "inference.h"
#include "log.h"

/* The reserve detect_gpu_layers() already keeps for the KV cache and compute
 * buffers, in MiB. ⚠ THE TWO HAVE TO AGREE. If this is smaller, the policy
 * thinks there is room the loader will refuse to use, and it re-fits on every
 * poll trying to reach a state the loader will not produce. */
#define OFFLOAD_RESERVE_MIB 1024

/*
 * Re-fit the model to `target` GPU layers.
 *
 * ⚠ THE WRITE LOCK IS WHAT MAKES THIS SAFE. Queries hold the read lock for
 * their whole duration, so this blocks until in-flight work has finished
 * instead of freeing the model underneath it — the same rule handle_sleep()
 * follows, and for the same reason: without it a re-fit during a generation is
 * a use-after-free rather than a slow answer.
 */
static void offload_refit(synapd_state_t *s, int target, const char *why)
{
    syn_log(LOG_INFO, "offload: re-fitting to %d GPU layers — %s", target, why);

    atomic_store(&s->offload_cap, target);

    pthread_rwlock_wrlock(&s->model_rw);
    atomic_store(&s->model_loading, 1);

    inference_destroy(s);
    int rc = inference_init(s);

    atomic_store(&s->model_loading, 0);
    pthread_rwlock_unlock(&s->model_rw);

    if (rc < 0) {
        /*
         * ⛔ THE CAP COMES OFF ON FAILURE. A cap that survives a failed load is
         * a daemon that will keep failing to load at the same number for the
         * rest of the session, with the journal blaming the model. Dropping it
         * means the next attempt is the ordinary auto-detect one, which is the
         * behaviour a box with no offload policy would have had.
         */
        atomic_store(&s->offload_cap, -1);
        syn_log(LOG_WARNING, "offload: re-fit to %d layers FAILED — the cap is "
                "lifted and the next load will auto-detect as usual", target);
        return;
    }

    syn_log(LOG_INFO, "offload: now holding %d GPU layers",
            atomic_load(&s->offload_resident));
}

const char *offload_set_demand(synapd_state_t *s, int high)
{
    int was = atomic_exchange(&s->demand_high, high ? 1 : 0);

    if (!s->config.auto_offload)
        return high ? "noted, but automatic offload is off — nothing will move"
                    : "noted, but automatic offload is off";

    if (was == (high ? 1 : 0))
        return high ? "already at high demand" : "already at normal demand";

    /*
     * ⚠ THE THREAD DOES THE WORK, NOT THIS CALL. A caller declaring high
     * demand is about to want the card, which is exactly when it must not be
     * made to wait: re-fitting here would block its IPC for the tens of seconds
     * a reload takes. The next poll picks it up, and the poll interval is the
     * worst case for how long it waits.
     *
     * ⚠ NO SENDER IN THE TREE as of synui 601 — game mode releases the model
     * outright, because shedding a layer moves it into RAM and onto the CPU
     * rather than freeing it. This stays for the case it was right for: a
     * desktop under GPU pressure where somebody still wants an answer.
     */
    syn_log(LOG_INFO, "offload: demand is now %s", high ? "HIGH" : "normal");
    return high ? "high demand noted — VRAM will be released at the next poll"
                : "normal demand noted — VRAM will be reclaimed as it frees up";
}

static void *offload_thread(void *arg)
{
    synapd_state_t *s = arg;
    time_t last_change = time(NULL);

    syn_log(LOG_INFO, "offload: watching VRAM — floor %u MiB (%u while demand is "
            "high), poll %us, dwell %us",
            s->config.offload_floor_mib, s->config.offload_game_mib,
            s->config.offload_poll_s, s->config.offload_dwell_s);

    while (!atomic_load(&s->offload_stop)) {
        /* Sliced, so a shutdown does not wait out a whole poll interval. */
        for (unsigned i = 0; i < s->config.offload_poll_s * 4; i++) {
            if (atomic_load(&s->offload_stop)) return NULL;
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 250L * 1000 * 1000 };
            nanosleep(&ts, NULL);
        }
        if (atomic_load(&s->offload_stop)) break;

        /*
         * ⚠ EVERY ONE OF THESE IS A REASON TO DO NOTHING, and they are checked
         * before the card is read rather than after. A re-fit on top of a
         * suspend or a model switch would be two unloads racing for the same
         * write lock, and the one that lost would reload a model the other had
         * just decided to replace.
         */
        if (atomic_load(&s->model_sleeping)) continue;   /* suspended */
        if (atomic_load(&s->model_loading))  continue;   /* a load in flight */
        if (!s->model_loaded)                continue;   /* nothing to move */

        size_t vram_free = 0, vram_total = 0;
        inference_vram(&vram_free, &vram_total);
        if (vram_total == 0) {
            /*
             * No usable GPU. Not an error and not a reason to shed: the model
             * is already entirely in RAM, which is the state this whole
             * subsystem exists to reach. Stop looking.
             */
            syn_log(LOG_INFO, "offload: no usable GPU — nothing to manage, "
                    "the watcher is standing down");
            return NULL;
        }

        size_t model_mib = 0;
        int    n_layer   = 0;
        inference_geometry(s->config.model_path, &model_mib, &n_layer);

        syn_pressure_in_t in = {
            .vram_free       = vram_free,
            .vram_total      = vram_total,
            .model_mib       = model_mib,
            .n_layer         = n_layer,
            .layers_resident = atomic_load(&s->offload_resident),
            .demand_high     = atomic_load(&s->demand_high),
            .floor_mib       = s->config.offload_floor_mib,
            .game_floor_mib  = s->config.offload_game_mib,
            .reserve_mib     = OFFLOAD_RESERVE_MIB,
            .since_change_s  = (unsigned)(time(NULL) - last_change),
            .dwell_s         = s->config.offload_dwell_s,
        };

        syn_pressure_out_t out;
        syn_pressure_decide(&in, &out);

        if (s->debug)
            syn_log(LOG_DEBUG, "offload: vram %zu/%zu MiB free, holding %d/%d "
                    "layers, demand %s — %s",
                    vram_free, vram_total, in.layers_resident, n_layer,
                    in.demand_high ? "high" : "normal", out.why);

        if (!out.change) continue;

        offload_refit(s, out.target_layers, out.why);
        last_change = time(NULL);
    }
    return NULL;
}

void offload_start(synapd_state_t *s)
{
    if (!s->config.auto_offload) {
        syn_log(LOG_INFO, "offload: automatic offload is off — the layer count "
                "is decided once at load, as it always was");
        return;
    }

    /*
     * ⚠ NOT CHECKED HERE. Whether there is a usable GPU is the thread's own
     * first question, because at startup this runs BEFORE the first model load
     * and ggml has not necessarily enumerated anything yet — the same race
     * that had synapd answering from RAM for a whole session when it probed
     * CUDA before the device nodes existed. Asking too early would stand the
     * watcher down on a machine that does have a card.
     */
    atomic_store(&s->offload_stop, 0);
    if (pthread_create(&s->offload_thread, NULL, offload_thread, s) != 0) {
        syn_log(LOG_WARNING, "offload: cannot start the watcher: %s — the layer "
                "count will be whatever the load decided", strerror(errno));
        return;
    }
    s->offload_running = 1;
}

void offload_stop(synapd_state_t *s)
{
    if (!s->offload_running) return;
    atomic_store(&s->offload_stop, 1);
    pthread_join(s->offload_thread, NULL);
    s->offload_running = 0;
}
