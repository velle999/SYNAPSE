/*
 * pressure_test.c — the offload policy, without a GPU.
 *
 * pressure.c is pure (no llama, no ggml, no sockets, no daemon state), so this
 * links just it — same shape as wire_test and profile_test. That is the point
 * of the split: the rule that matters here is a feedback loop, and a feedback
 * loop is exactly what cannot be checked by looking at a card once.
 *
 * The case this suite exists for is OSCILLATION. synapd is part of what it
 * measures: shedding layers raises free VRAM by the amount shed, which on a
 * naive reading says the pressure is over. The loop test below drives the
 * policy round and round with the freed VRAM fed back in, and asserts it
 * SETTLES. Everything else here is a boundary around that.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <string.h>

#include "pressure.h"

static int failures = 0;

static void check(const char *what, int ok) {
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

/* A 4.4 GB model of 40 layers on an 8 GB card — synapd's real shape. */
static syn_pressure_in_t base(void) {
    syn_pressure_in_t in;
    memset(&in, 0, sizeof(in));
    in.vram_total      = 8192;
    in.model_mib       = 4400;
    in.n_layer         = 40;
    in.layers_resident = 40;
    in.floor_mib       = 1024;
    in.game_floor_mib  = 4096;
    in.reserve_mib     = 1024;
    in.since_change_s  = 600;
    in.dwell_s         = 60;
    in.vram_free       = 3000;   /* plenty spare, nothing else running */
    return in;
}

int main(void) {
    syn_pressure_in_t  in;
    syn_pressure_out_t out;

    printf("synapd offload policy\n\n");

    /* ── Nothing happening ─────────────────────────────────────────────── */
    in = base();
    syn_pressure_decide(&in, &out);
    check("an idle card with everything offloaded is left alone", !out.change);

    /* ── Something else takes the card ─────────────────────────────────── */
    in = base();
    in.vram_free = 400;                       /* a game just allocated */
    syn_pressure_decide(&in, &out);
    check("free VRAM under the floor sheds layers", out.change &&
          out.target_layers < in.layers_resident);
    /* 1024 - 400 = 624 MiB short, at 110 MiB a layer = 6 layers. */
    check("...enough of them to clear the deficit and no more",
          out.target_layers == 34);

    /* ── ⛔ THE OSCILLATION CASE ───────────────────────────────────────── */
    /*
     * Drive the loop the way reality does: every layer we give back becomes
     * free VRAM at the next poll. A policy that compares that recovered figure
     * against the floor alone will bounce for ever. This asserts it stops.
     */
    {
        const size_t per_layer = 4400 / 40;      /* 110 MiB */
        const size_t others    = 7000;           /* what the game is holding */
        int          resident  = 40;
        int          moves     = 0;
        int          settled   = 0;

        size_t free_at_rest = 0;

        for (int poll = 0; poll < 50; poll++) {
            syn_pressure_in_t p = base();
            p.layers_resident = resident;
            /* Free = the card, minus the other process, minus what WE hold. */
            size_t ours = (size_t)resident * per_layer;
            p.vram_free = 8192 > (others + ours) ? 8192 - (others + ours) : 0;

            syn_pressure_decide(&p, &out);
            if (!out.change) { settled = 1; free_at_rest = p.vram_free; break; }
            resident = out.target_layers;
            moves++;
        }
        check("the shed/restore loop settles instead of oscillating", settled);
        check("...in a handful of moves, not fifty", moves <= 8);
        /*
         * ⚠ THE INVARIANT, NOT A LAYER COUNT. What the policy promises is the
         * floor, not a particular number of layers — and asserting the number
         * is how a test starts dictating the arithmetic instead of checking
         * it. Settling at 1 layer with 1082 MiB free is a correct answer to
         * "leave 1024 free": over-shedding to zero would give the model away
         * for nothing.
         */
        check("...having actually cleared the floor it was defending",
              free_at_rest >= 1024);
        check("...without giving away more than it had to", resident > 0);
    }

    /* The same squeeze WITH game mode declared reaches the floor of the card:
     * the bigger floor cannot be met while holding anything at all. */
    {
        const size_t per_layer = 110;
        const size_t others    = 7000;
        int          resident  = 40;
        int          settled   = 0;

        for (int poll = 0; poll < 50; poll++) {
            syn_pressure_in_t p = base();
            p.demand_high     = 1;
            p.layers_resident = resident;
            size_t ours = (size_t)resident * per_layer;
            p.vram_free = 8192 > (others + ours) ? 8192 - (others + ours) : 0;

            syn_pressure_decide(&p, &out);
            if (!out.change) { settled = 1; break; }
            resident = out.target_layers;
        }
        check("under a declared-high-demand squeeze it settles too", settled);
        check("...at zero layers, the whole model in RAM", resident == 0);
    }

    /* And the same loop with the pressure GONE must climb back and stop. */
    {
        const size_t per_layer = 110;
        const size_t others    = 200;            /* the game exited */
        int          resident  = 0;
        int          settled   = 0;

        for (int poll = 0; poll < 50; poll++) {
            syn_pressure_in_t p = base();
            p.layers_resident = resident;
            size_t ours = (size_t)resident * per_layer;
            p.vram_free = 8192 > (others + ours) ? 8192 - (others + ours) : 0;

            syn_pressure_decide(&p, &out);
            if (!out.change) { settled = 1; break; }
            resident = out.target_layers;
        }
        check("with the pressure gone it climbs back and settles", settled);
        check("...all the way to fully offloaded", resident == 40);
    }

    /* ── The asymmetry itself ──────────────────────────────────────────── */
    /*
     * One reading, two answers, and that is deliberate: at exactly the floor a
     * shed is not called for, and a restore is not permitted either. Without
     * this band the two rules meet at a point and the model reloads across it
     * on every poll.
     */
    in = base();
    in.vram_free       = 1024;   /* exactly the floor */
    in.layers_resident = 20;
    syn_pressure_decide(&in, &out);
    check("at exactly the floor, nothing moves in either direction", !out.change);

    in = base();
    in.vram_free       = 1023;   /* one MiB under */
    in.layers_resident = 20;
    syn_pressure_decide(&in, &out);
    check("one MiB under the floor does shed", out.change &&
          out.target_layers == 19);

    /* Restore needs floor + margin + reserve = 1024 + 512 + 1024 = 2560,
     * and then a whole layer on top of it. */
    in = base();
    in.vram_free       = 2560;
    in.layers_resident = 20;
    syn_pressure_decide(&in, &out);
    check("at the restore bar exactly, it still does not take VRAM back",
          !out.change);

    in = base();
    in.vram_free       = 2560 + 110;
    in.layers_resident = 20;
    syn_pressure_decide(&in, &out);
    check("one layer's worth above the bar takes exactly one layer",
          out.change && out.target_layers == 21);

    /* ── The dwell ─────────────────────────────────────────────────────── */
    in = base();
    in.vram_free      = 100;     /* badly short */
    in.since_change_s = 10;
    in.dwell_s        = 60;
    syn_pressure_decide(&in, &out);
    check("a move inside the dwell window is refused, however short the card",
          !out.change);

    in = base();
    in.vram_free      = 100;
    in.since_change_s = 60;      /* exactly the dwell */
    syn_pressure_decide(&in, &out);
    check("...and allowed once the dwell has elapsed", out.change);

    /* ── Game mode ─────────────────────────────────────────────────────── */
    /*
     * ⚠ It raises the floor; it is not a separate path to zero. Same
     * arithmetic, bigger reserve — so there is one policy to reason about and
     * the two cannot drift apart.
     */
    in = base();
    in.vram_free   = 3000;       /* comfortable by the ordinary floor */
    in.demand_high = 0;
    syn_pressure_decide(&in, &out);
    check("3000 MiB free is comfortable when nothing declared demand",
          !out.change);

    in = base();
    in.vram_free   = 3000;
    in.demand_high = 1;
    syn_pressure_decide(&in, &out);
    check("...and is a deficit once game mode declares high demand",
          out.change && out.target_layers < 40);

    /* A game floor below the ordinary floor must not quietly LOWER the bar. */
    in = base();
    in.vram_free      = 900;
    in.demand_high    = 1;
    in.game_floor_mib = 256;     /* misconfigured, smaller than floor_mib */
    syn_pressure_decide(&in, &out);
    check("a game floor smaller than the normal one cannot weaken the policy",
          out.change);

    /* ── Refusing to act ───────────────────────────────────────────────── */
    in = base();
    in.n_layer = 0;
    in.vram_free = 10;
    syn_pressure_decide(&in, &out);
    check("unknown geometry moves nothing rather than guessing", !out.change);

    in = base();
    in.model_mib = 0;
    in.vram_free = 10;
    syn_pressure_decide(&in, &out);
    check("a model of unknown size likewise", !out.change);

    in = base();
    in.vram_free       = 10;
    in.layers_resident = 0;
    syn_pressure_decide(&in, &out);
    check("already in RAM and still short: nothing to give, and it says so",
          !out.change && strstr(out.why, "no layers left") != NULL);

    in = base();
    in.layers_resident = 40;
    in.vram_free       = 8000;
    syn_pressure_decide(&in, &out);
    check("fully offloaded with room to spare stays put", !out.change);

    /* A model with more layers than MiB would divide to zero per layer. */
    in = base();
    in.model_mib       = 10;
    in.n_layer         = 40;
    in.vram_free       = 100;
    in.layers_resident = 40;
    syn_pressure_decide(&in, &out);
    check("a per-layer size that rounds to zero does not divide by zero",
          out.change && out.target_layers >= 0);

    printf("\n%s\n", failures ? "FAILURES" : "all pressure tests passed");
    return failures ? 1 : 0;
}
