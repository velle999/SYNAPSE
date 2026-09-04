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
     * A client said demand is high — synui's game mode, over SYN_MSG_DEMAND.
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

    /* Seconds since the last change, and the minimum the policy will allow.
     * Passed in rather than read from a clock so the dwell rule is testable. */
    unsigned since_change_s;
    unsigned dwell_s;
} syn_pressure_in_t;

typedef struct {
    int         target_layers;  /* what we should be running */
    int         change;         /* 1 = act on target_layers */
    const char *why;            /* for the journal; never NULL */
} syn_pressure_out_t;

/*
 * Decide. Never fails: an input it cannot reason about produces change = 0 and
 * a `why` saying so, because refusing to act is always safe and reloading on a
 * guess is not.
 */
void syn_pressure_decide(const syn_pressure_in_t *in, syn_pressure_out_t *out);

#endif
