#ifndef PRESSURE_H
#define PRESSURE_H
/*
 * pressure.h — when to hand VRAM back, and how much.
 *
 * ⛔ THE THING THAT MAKES THIS HARD IS THAT WE ARE PART OF WHAT WE MEASURE.
 *
 * The signal is free VRAM. The model is the largest single thing on the card
 * (sleep_hook.c measured 7300 MiB resident with synapd up against 2655 MiB
 * without it), so the moment we shed layers the free figure JUMPS — by exactly
 * the amount we released. Read naively, that reads as "the pressure is over",
 * so the next poll reloads, which puts the pressure straight back. A model
 * that reloads on a loop is worse than one that never moved: every cycle costs
 * tens of seconds of load and throws away the KV cache.
 *
 * So the rule below is deliberately ASYMMETRIC:
 *
 *   shed    when   free < floor
 *   restore when   free - (what we would take back) >= floor + margin
 *
 * The dead band between those two is `margin + per_layer` wide and it is the
 * whole anti-thrash mechanism. Restoring has to clear a bar that shedding does
 * not, measured against the VRAM the restore would itself consume — never
 * against what happens to be free while we are small.
 *
 * ⚠ AND WHY THERE IS NO LIVE MIGRATION. llama.cpp fixes n_gpu_layers when the
 * model is created; there is no API to move a layer between VRAM and RAM on a
 * live context. "Offloading to RAM" is therefore always destroy + reload at a
 * different count, which is why the dwell time below exists and why the
 * decision is worth this much care: each change is expensive and visible.
 *
 * Pure on purpose — no llama, no ggml, no sockets, no daemon state — so the
 * arithmetic can be unit-tested without a GPU. offload.c is the part that
 * reads the real device and acts on the answer. Same split as profile.c.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stddef.h>

/* Everything the decision needs, all in MiB except the counts. */
typedef struct {
    size_t vram_free;        /* free RIGHT NOW, with our layers already resident */
    size_t vram_total;       /* the card's size; 0 when unknown */
    size_t model_mib;        /* the chat model's weights */
    int    n_layer;          /* the model's block count */
    int    layers_resident;  /* how many of them we are holding on the GPU */

    /*
     * A client said demand is high, over SYN_MSG_DEMAND. ⚠ NOT game mode any
     * more — that releases the model outright, because shedding a layer moves
     * it to RAM and the CPU rather than freeing it.
     * ⚠ It does NOT mean "go to CPU". It swaps `floor` for `game_floor` and
     * lets the same arithmetic below decide, so there is ONE policy rather
     * than a watermark path and a special case that can disagree with it.
     */
    int    demand_high;

    size_t floor_mib;        /* headroom to leave for everything else */
    size_t game_floor_mib;   /* the same, while demand_high */
    /*
     * KV cache and compute buffers. The file size accounts for the weights and
     * nothing else, so a layer count that fits the weights exactly still does
     * not fit — this is what detect_gpu_layers() already reserves at load, and
     * the two have to agree or the daemon oscillates around its own load-time
     * answer the first time this runs.
     */
    size_t reserve_mib;

    /* ── The other two things a machine runs out of ─────────────────────
     *
     * ⛔ SHEDDING A LAYER DOES NOT FREE IT. llama.cpp has no live migration, so
     * a layer that comes off the card is reloaded into SYSTEM RAM and computed
     * on the CPU. Everything above is therefore a way of relieving the card BY
     * SPENDING the other two — which is the right trade when the GPU is the
     * bottleneck and the exact wrong one when RAM or the cores are.
     *
     * So when the shortage is RAM or CPU there is no layer count that helps,
     * and the policy has one other move: let the model go entirely, and take
     * it back when the machine is quiet again. That is a real answer rather
     * than a smaller version of the same problem.
     *
     * ⚠ ZERO MEANS "NOT MEASURED", for every field here, and the policy will
     * not release on an input it did not get. A daemon that unloads itself
     * because a /proc file could not be read is worse than one that stays put.
     */
    size_t mem_avail_mib;    /* MemAvailable — NOT MemFree, which means nothing */
    size_t mem_total_mib;    /* 0 = memory was not measured; nothing is decided */
    size_t ram_floor_mib;    /* release below this much available */
    size_t model_ram_mib;    /* what a release would actually give back */

    /*
     * Pressure Stall Information, `some avg60` as a whole percent — the
     * minute, because what this decides costs tens of seconds to undo.
     *
     * ⚠ PSI, NOT A FREE-BYTES THRESHOLD, because "free memory" on Linux is not
     * a measure of shortage — the page cache makes it small on a machine with
     * nothing wrong. PSI says something quite different and exactly right:
     * how much time real tasks SPENT STALLED waiting for the resource. A
     * kernel built without it reports psi_available = 0 and the floors above
     * carry the decision alone.
     */
    unsigned psi_mem_pct;
    unsigned psi_cpu_pct;
    unsigned psi_limit_pct;   /* sustained above this is a shortage */
    int      psi_available;

    /*
     * ⛔ WHETHER THE CPU PRESSURE IS OURS. A generation runs n_threads flat
     * out, so synapd answering a question looks exactly like a build to a
     * stall counter — and releasing the model because we are USING it would be
     * a loop with a person waiting at the end of it. The watcher passes 1 here
     * whenever a query holds the read lock, and CPU is not a reason to release
     * while that is true.
     */
    int      busy;

    /* Already released BY THIS POLICY (not by a client's SLEEP — that one is
     * somebody else's decision and the watcher does not second-guess it). */
    int      released;

    /* Seconds since the last change, and the minimum the policy will allow.
     * Passed in rather than read from a clock so the dwell rule is testable. */
    unsigned since_change_s;
    unsigned dwell_s;
} syn_pressure_in_t;

typedef enum {
    SYN_PRESSURE_HOLD = 0,   /* leave everything exactly as it is */
    SYN_PRESSURE_REFIT,      /* move to target_layers */
    SYN_PRESSURE_RELEASE,    /* let the model go: VRAM, RAM and cores */
    SYN_PRESSURE_RELOAD,     /* the machine is quiet again — take it back */
} syn_pressure_act_t;

typedef struct {
    int                target_layers;  /* what we should be running, for REFIT */
    syn_pressure_act_t act;
    const char        *why;            /* for the journal; never NULL */
} syn_pressure_out_t;

/*
 * Decide. Never fails: an input it cannot reason about produces change = 0 and
 * a `why` saying so, because refusing to act is always safe and reloading on a
 * guess is not.
 */
void syn_pressure_decide(const syn_pressure_in_t *in, syn_pressure_out_t *out);

#endif
