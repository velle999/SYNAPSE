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
 * ⛔ AND THAT IS ALSO WHY A LAYER COUNT IS NOT ALWAYS THE ANSWER. "Move it to
 * RAM" means the weights are IN RAM and computed on the CPU — this subsystem
 * relieves the card by spending the other two resources. When the shortage is
 * memory or cores there is no count that helps, so the policy has one further
 * move: release the model outright and take it back when the machine is quiet.
 * sysload.c is where those two are read; pressure.c decides.
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
#include "sysload.h"
#include "inference.h"
#include "log.h"

/* The reserve detect_gpu_layers() already keeps for the KV cache and compute
 * buffers, in MiB. ⚠ THE TWO HAVE TO AGREE. If this is smaller, the policy
 * thinks there is room the loader will refuse to use, and it re-fits on every
 * poll trying to reach a state the loader will not produce. */
#define OFFLOAD_RESERVE_MIB 1024

/*
 * Let the model go entirely — VRAM, the RAM its weights sit in, and any cores
 * it would have spent answering.
 *
 * ⛔ THIS IS THE ANSWER WHEN A LAYER COUNT IS NOT ONE. Shedding a layer moves
 * it into RAM and onto the CPU, so a machine short of either cannot be helped
 * by re-fitting — only by the model not being there. pressure.c decides;
 * this carries it out.
 *
 * ⚠ IT SETS model_sleeping, so a query in this window is refused with the same
 * message a suspend or a game produces, rather than queueing behind a model
 * that is not coming back until the machine is quiet. And it sets
 * offload_released beside it, which is what tells the watcher the release was
 * ITS OWN and may be reversed — a client's SLEEP must not be.
 */
static void offload_release(synapd_state_t *s, const char *why)
{
    syn_log(LOG_INFO, "offload: releasing the model — %s", why);

    atomic_store(&s->model_sleeping, 1);
    atomic_store(&s->offload_released, 1);

    pthread_rwlock_wrlock(&s->model_rw);
    if (s->model_loaded) inference_destroy(s);
    pthread_rwlock_unlock(&s->model_rw);

    /* ⚠ The cap goes with it. Whatever layer count we were holding described a
     * card that has since been asked for; the reload is an ordinary
     * auto-detect against whatever is free at that moment. */
    atomic_store(&s->offload_cap, -1);
    syn_log(LOG_INFO, "offload: model released");
}

/* And take it back. */
static void offload_reload(synapd_state_t *s, const char *why)
{
    syn_log(LOG_INFO, "offload: reloading the model — %s", why);

    pthread_rwlock_wrlock(&s->model_rw);
    atomic_store(&s->model_loading, 1);
    int rc = inference_init(s);
    atomic_store(&s->model_loading, 0);
    pthread_rwlock_unlock(&s->model_rw);

    if (rc < 0) {
        /*
         * ⛔ STAY RELEASED, AND SAY SO. Clearing the flags on a failed load
         * would leave the daemon claiming a model it does not have, and every
         * query would fail with something less useful than "released". The
         * next poll tries again, which is the right cadence for a load that
         * failed because the machine is still busy.
         */
        syn_log(LOG_WARNING, "offload: reload FAILED — staying released, will "
                "try again at the next poll");
        return;
    }

    atomic_store(&s->model_sleeping, 0);
    atomic_store(&s->offload_released, 0);
    syn_log(LOG_INFO, "offload: model is back, holding %d GPU layers",
            atomic_load(&s->offload_resident));
}

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

    /* ⚠ EVERY THRESHOLD, ON ONE LINE, AT STARTUP. The first question about a
     * daemon that unloaded itself is what it thought it was defending, and the
     * answer has to be in the journal before the decision it explains. */
    syn_log(LOG_INFO, "offload: watching VRAM — floor %u MiB (%u while demand is "
            "high), poll %us, dwell %us",
            s->config.offload_floor_mib, s->config.offload_game_mib,
            s->config.offload_poll_s, s->config.offload_dwell_s);
    if (s->config.offload_ram_floor_mib || s->config.offload_psi_limit_pct) {
        syn_sysload_t probe;
        syn_sysload_read(NULL, &probe);
        syn_log(LOG_INFO, "offload: ...and the machine — release below %u MiB "
                "available%s, stall limit %u%% (some avg60)%s",
                s->config.offload_ram_floor_mib,
                probe.mem_total_mib ? "" : " (memory could not be read)",
                s->config.offload_psi_limit_pct,
                probe.psi_available ? "" : " — NO PSI on this kernel, the floor "
                                           "decides alone");
    } else {
        syn_log(LOG_INFO, "offload: ...RAM and core pressure are off — only the "
                "card is watched");
    }

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
        /*
         * ⚠ RELEASED BY US IS NOT THE SAME AS ASLEEP BY SOMEBODY ELSE. A
         * client's SLEEP — the suspend hook, synui's game mode — is a decision
         * this thread does not second-guess and does not undo: it skips, and
         * the client's WAKE is what brings the model back. A release THIS
         * policy made is ours to reverse, and reversing it is the whole point
         * of measuring whether the pressure went away.
         */
        const int ours = atomic_load(&s->offload_released);
        if (atomic_load(&s->model_sleeping) && !ours) continue;  /* someone else's */
        if (atomic_load(&s->model_loading))           continue;  /* load in flight */
        if (!s->model_loaded && !ours)                continue;  /* nothing to move */

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

        syn_sysload_t sys;
        syn_sysload_read(NULL, &sys);

        syn_pressure_in_t in = {
            .vram_free       = vram_free,
            .vram_total      = vram_total,
            .model_mib       = model_mib,
            .n_layer         = n_layer,
            .layers_resident = atomic_load(&s->offload_resident),
            .demand_high     = atomic_load(&s->demand_high),
            .floor_mib       = s->config.offload_floor_mib,
            .game_floor_mib  = s->config.offload_game_mib,

            .mem_avail_mib   = sys.mem_avail_mib,
            .mem_total_mib   = sys.mem_total_mib,
            .ram_floor_mib   = s->config.offload_ram_floor_mib,
            /*
             * ⚠ THE WHOLE MODEL, DELIBERATELY PESSIMISTIC. What a reload would
             * actually cost in RAM depends on how many layers land on the card,
             * which is decided by the loader at the moment it runs — so the
             * only figure available here is the worst case, and the worst case
             * is the safe direction: it makes coming back harder, never easier,
             * and the failure it prevents is a reload that puts the machine
             * straight back under the floor.
             */
            .model_ram_mib   = model_mib,
            .psi_mem_pct     = sys.psi_mem_pct,
            .psi_cpu_pct     = sys.psi_cpu_pct,
            .psi_limit_pct   = s->config.offload_psi_limit_pct,
            .psi_available   = sys.psi_available,
            /* A generation saturates n_threads and is indistinguishable from a
             * build to a stall counter. See `busy` in pressure.h. */
            .busy            = atomic_load(&s->requests_active) > 0,
            .released        = atomic_load(&s->offload_released),
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

        switch (out.act) {
        case SYN_PRESSURE_HOLD:
            break;
        case SYN_PRESSURE_REFIT:
            offload_refit(s, out.target_layers, out.why);
            last_change = time(NULL);
            break;
        case SYN_PRESSURE_RELEASE:
            offload_release(s, out.why);
            last_change = time(NULL);
            break;
        case SYN_PRESSURE_RELOAD:
            offload_reload(s, out.why);
            last_change = time(NULL);
            break;
        }
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
