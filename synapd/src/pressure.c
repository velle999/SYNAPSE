/*
 * pressure.c — the offload policy. See pressure.h for why it is shaped this way.
 *
 * Deliberately dependency-free: no llama, no ggml, no sockets, no daemon
 * state, so every rule here is exercised by pressure_test without a GPU on the
 * machine running it. Same split as profile.c and wire.c.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "pressure.h"

/* Round up, without floating point — a deficit of one byte still costs a
 * layer, and truncating here would shed one too few and leave us under the
 * floor for another whole poll. */
static size_t div_up(size_t a, size_t b) {
    return b ? (a + b - 1) / b : 0;
}

void syn_pressure_decide(const syn_pressure_in_t *in, syn_pressure_out_t *out)
{
    out->target_layers = in->layers_resident;
    out->change        = 0;
    out->why           = "no change";

    /*
     * Nothing to reason about. detect_gpu_layers() has the same guard and
     * answers GPU_LAYERS_ALL there; here the safe answer is the opposite —
     * leave it exactly as it is. A daemon that reshuffles a model whose
     * geometry it could not read is a daemon reloading on a guess.
     */
    if (in->n_layer <= 0 || in->model_mib == 0) {
        out->why = "model geometry unknown — not moving anything";
        return;
    }

    /*
     * ⚠ THE DWELL IS CHECKED BEFORE ANYTHING ELSE, INCLUDING GAME MODE. Every
     * change destroys and reloads a multi-GB model and throws away the KV
     * cache, so the floor on how often that may happen is not a tuning knob
     * that some path gets to skip. A game starting inside the dwell window is
     * simply handled at the next poll.
     */
    if (in->since_change_s < in->dwell_s) {
        out->why = "within the dwell window since the last move";
        return;
    }

    const size_t floor = in->demand_high && in->game_floor_mib > in->floor_mib
                       ? in->game_floor_mib
                       : in->floor_mib;

    /* Layers are close enough to equal-sized to divide the weights by the
     * block count — the same approximation detect_gpu_layers() makes, and it
     * has to be the same one or the two disagree about what fits. */
    size_t per_layer = in->model_mib / (size_t)in->n_layer;
    if (per_layer == 0) per_layer = 1;

    /* ── Shed ──────────────────────────────────────────────────────────── */
    if (in->vram_free < floor) {
        if (in->layers_resident <= 0) {
            /*
             * Already entirely in RAM and the card is STILL short. Nothing
             * here can help — said plainly rather than logged as a decision,
             * because the reader's next question is whether synapd is the
             * problem, and this is the line that answers no.
             */
            out->why = "below the floor with no layers left to give back";
            return;
        }
        size_t deficit = floor - in->vram_free;
        int    drop    = (int)div_up(deficit, per_layer);
        int    target  = in->layers_resident - drop;
        if (target < 0) target = 0;

        out->target_layers = target;
        out->change        = 1;
        out->why           = in->demand_high
                           ? "high demand declared — making room"
                           : "free VRAM is under the floor";
        return;
    }

    /* ── Restore ───────────────────────────────────────────────────────── */
    /*
     * ⛔ MEASURED AGAINST WHAT THE RESTORE WOULD TAKE, NOT AGAINST WHAT IS
     * FREE. `vram_free` is large precisely BECAUSE we are small; spending all
     * of it puts us straight back under the floor and the next poll sheds
     * again. So the room a restore may use is what is left after the floor AND
     * a margin, and the margin is what makes the band asymmetric.
     *
     * The margin is half the floor rather than a new constant: it has to scale
     * with the floor or a big reserve on a small card leaves no band at all.
     */
    if (in->layers_resident >= in->n_layer) {
        out->why = "fully offloaded already";
        return;
    }

    const size_t margin  = floor / 2;
    const size_t wanted  = floor + margin + in->reserve_mib;
    if (in->vram_free <= wanted) {
        out->why = "free VRAM has not recovered past the restore margin";
        return;
    }

    size_t usable = in->vram_free - wanted;
    int    add    = (int)(usable / per_layer);
    if (add <= 0) {
        out->why = "not enough recovered VRAM for even one layer";
        return;
    }

    int target = in->layers_resident + add;
    if (target > in->n_layer) target = in->n_layer;

    out->target_layers = target;
    out->change        = 1;
    out->why           = "VRAM recovered — taking layers back";
}
