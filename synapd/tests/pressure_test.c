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
    check("an idle card with everything offloaded is left alone", (out.act == SYN_PRESSURE_HOLD));

    /* ── Something else takes the card ─────────────────────────────────── */
    in = base();
    in.vram_free = 400;                       /* a game just allocated */
    syn_pressure_decide(&in, &out);
    check("free VRAM under the floor sheds layers", (out.act == SYN_PRESSURE_REFIT) &&
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
            if ((out.act == SYN_PRESSURE_HOLD)) { settled = 1; free_at_rest = p.vram_free; break; }
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
            if ((out.act == SYN_PRESSURE_HOLD)) { settled = 1; break; }
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
            if ((out.act == SYN_PRESSURE_HOLD)) { settled = 1; break; }
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
    check("at exactly the floor, nothing moves in either direction", (out.act == SYN_PRESSURE_HOLD));

    in = base();
    in.vram_free       = 1023;   /* one MiB under */
    in.layers_resident = 20;
    syn_pressure_decide(&in, &out);
    check("one MiB under the floor does shed", (out.act == SYN_PRESSURE_REFIT) &&
          out.target_layers == 19);

    /* Restore needs floor + margin + reserve = 1024 + 512 + 1024 = 2560,
     * and then a whole layer on top of it. */
    in = base();
    in.vram_free       = 2560;
    in.layers_resident = 20;
    syn_pressure_decide(&in, &out);
    check("at the restore bar exactly, it still does not take VRAM back",
          (out.act == SYN_PRESSURE_HOLD));

    in = base();
    in.vram_free       = 2560 + 110;
    in.layers_resident = 20;
    syn_pressure_decide(&in, &out);
    check("one layer's worth above the bar takes exactly one layer",
          (out.act == SYN_PRESSURE_REFIT) && out.target_layers == 21);

    /* ── The dwell ─────────────────────────────────────────────────────── */
    in = base();
    in.vram_free      = 100;     /* badly short */
    in.since_change_s = 10;
    in.dwell_s        = 60;
    syn_pressure_decide(&in, &out);
    check("a move inside the dwell window is refused, however short the card",
          (out.act == SYN_PRESSURE_HOLD));

    in = base();
    in.vram_free      = 100;
    in.since_change_s = 60;      /* exactly the dwell */
    syn_pressure_decide(&in, &out);
    check("...and allowed once the dwell has elapsed", (out.act == SYN_PRESSURE_REFIT));

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
          (out.act == SYN_PRESSURE_HOLD));

    in = base();
    in.vram_free   = 3000;
    in.demand_high = 1;
    syn_pressure_decide(&in, &out);
    check("...and is a deficit once game mode declares high demand",
          (out.act == SYN_PRESSURE_REFIT) && out.target_layers < 40);

    /* A game floor below the ordinary floor must not quietly LOWER the bar. */
    in = base();
    in.vram_free      = 900;
    in.demand_high    = 1;
    in.game_floor_mib = 256;     /* misconfigured, smaller than floor_mib */
    syn_pressure_decide(&in, &out);
    check("a game floor smaller than the normal one cannot weaken the policy",
          (out.act == SYN_PRESSURE_REFIT));

    /* ── Refusing to act ───────────────────────────────────────────────── */
    in = base();
    in.n_layer = 0;
    in.vram_free = 10;
    syn_pressure_decide(&in, &out);
    check("unknown geometry moves nothing rather than guessing", (out.act == SYN_PRESSURE_HOLD));

    in = base();
    in.model_mib = 0;
    in.vram_free = 10;
    syn_pressure_decide(&in, &out);
    check("a model of unknown size likewise", (out.act == SYN_PRESSURE_HOLD));

    in = base();
    in.vram_free       = 10;
    in.layers_resident = 0;
    syn_pressure_decide(&in, &out);
    check("already in RAM and still short: nothing to give, and it says so",
          (out.act == SYN_PRESSURE_HOLD) && strstr(out.why, "no layers left") != NULL);

    in = base();
    in.layers_resident = 40;
    in.vram_free       = 8000;
    syn_pressure_decide(&in, &out);
    check("fully offloaded with room to spare stays put", (out.act == SYN_PRESSURE_HOLD));

    /* A model with more layers than MiB would divide to zero per layer. */
    in = base();
    in.model_mib       = 10;
    in.n_layer         = 40;
    in.vram_free       = 100;
    in.layers_resident = 40;
    syn_pressure_decide(&in, &out);
    check("a per-layer size that rounds to zero does not divide by zero",
          (out.act == SYN_PRESSURE_REFIT) && out.target_layers >= 0);

    /* ══ RAM AND CPU ═══════════════════════════════════════════════════
     *
     * ⛔ THE RULE THAT MAKES THESE DIFFERENT FROM VRAM: there is no layer count
     * that relieves them. Shedding a layer MOVES it into RAM and onto the
     * cores, so answering a memory shortage with the VRAM rule deepens it. The
     * only move that helps is letting the model go, and the only question
     * worth testing is when it may come back — which is the same feedback loop
     * as the VRAM one, in a second resource.
     */

    /* A machine of 32 GB with the model costing 4.4 GB of it. */
    #define WITH_MEM(i)  do {                  \
        (i).mem_total_mib  = 32000;            \
        (i).mem_avail_mib  = 16000;            \
        (i).ram_floor_mib  = 2048;             \
        (i).model_ram_mib  = 4400;             \
        (i).psi_available  = 1;                \
        (i).psi_limit_pct  = 20;               \
        (i).psi_mem_pct    = 0;                \
        (i).psi_cpu_pct    = 0;                \
    } while (0)

    /* ⛔ THE DEFAULT IS SILENCE. Every case above this point left all of these
     * zero, and not one of them released — an unmeasured resource must never
     * be a reason to unload, or a daemon on a kernel with no PSI and an
     * unreadable /proc/meminfo would unload itself at the first poll. */
    in = base();
    in.mem_avail_mib = 0;          /* nothing measured at all */
    syn_pressure_decide(&in, &out);
    check("memory that was never measured releases nothing",
          (out.act == SYN_PRESSURE_HOLD));

    in = base();
    WITH_MEM(in);
    in.mem_avail_mib = 1000;                  /* under the 2048 floor */
    syn_pressure_decide(&in, &out);
    check("available RAM under the floor releases the model",
          out.act == SYN_PRESSURE_RELEASE);

    in = base();
    WITH_MEM(in);
    in.psi_mem_pct = 40;                      /* stalling on memory */
    syn_pressure_decide(&in, &out);
    check("...and so does real memory stall, with RAM still looking free",
          out.act == SYN_PRESSURE_RELEASE);

    in = base();
    WITH_MEM(in);
    in.psi_cpu_pct = 40;                      /* a build, say */
    syn_pressure_decide(&in, &out);
    check("cores stalled on other work release it too",
          out.act == SYN_PRESSURE_RELEASE);

    /* ⛔ THE ONE THAT WOULD HAVE BITTEN. A generation runs n_threads flat out,
     * so synapd ANSWERING looks exactly like a build to a stall counter —
     * and releasing then unloads the model out from under the person waiting
     * for the answer, every time anybody asks anything. */
    in = base();
    WITH_MEM(in);
    in.psi_cpu_pct = 90;
    in.busy        = 1;
    syn_pressure_decide(&in, &out);
    check("...but not when the load is our own generation",
          (out.act == SYN_PRESSURE_HOLD));

    /* ⛔ ORDER: a card under its floor AND a machine short of memory. The VRAM
     * rule would shed — into the RAM that is the actual shortage. */
    in = base();
    WITH_MEM(in);
    in.vram_free     = 400;                   /* would shed 6 layers */
    in.mem_avail_mib = 1000;                  /* but RAM is what is short */
    syn_pressure_decide(&in, &out);
    check("short of both, it releases rather than shedding into RAM",
          out.act == SYN_PRESSURE_RELEASE);

    /* ── Coming back: the same feedback loop, in a second resource ──────── */
    in = base();
    WITH_MEM(in);
    in.released      = 1;
    in.mem_avail_mib = 5000;   /* looks fine — but 4400 of it is only free
                                  BECAUSE we are gone */
    syn_pressure_decide(&in, &out);
    check("released, RAM free only because we left, does NOT reload",
          (out.act == SYN_PRESSURE_HOLD));

    in = base();
    WITH_MEM(in);
    in.released      = 1;
    in.mem_avail_mib = 2048 + 1024 + 4400 + 1;   /* floor + margin + us */
    syn_pressure_decide(&in, &out);
    check("...and does reload once there is room for it and the floor",
          out.act == SYN_PRESSURE_RELOAD);

    in = base();
    WITH_MEM(in);
    in.released      = 1;
    in.mem_avail_mib = 20000;                 /* memory is fine */
    in.psi_cpu_pct   = 40;                    /* the build is still going */
    syn_pressure_decide(&in, &out);
    check("...but not while the cores are still stalled",
          (out.act == SYN_PRESSURE_HOLD));

    /* ⚠ A kernel with no PSI decides on the floors alone, and must still be
     * able to both release and come back. */
    in = base();
    WITH_MEM(in);
    in.psi_available = 0;
    in.psi_cpu_pct   = 90;                    /* would have released, if read */
    syn_pressure_decide(&in, &out);
    check("no PSI: a stall figure that cannot be trusted is not acted on",
          (out.act == SYN_PRESSURE_HOLD));

    in = base();
    WITH_MEM(in);
    in.psi_available = 0;
    in.mem_avail_mib = 1000;
    syn_pressure_decide(&in, &out);
    check("...while the RAM floor still works without it",
          out.act == SYN_PRESSURE_RELEASE);

    /* ⛔ AND IT SETTLES. Release, hand back the memory the release freed, and
     * the policy must not immediately take it again — the whole failure this
     * file exists for, now in RAM as well as VRAM. */
    {
        syn_pressure_in_t p = base();
        WITH_MEM(p);
        p.mem_avail_mib = 1500;               /* under the floor */
        int flips = 0, i;
        for (i = 0; i < 20; i++) {
            syn_pressure_out_t o;
            syn_pressure_decide(&p, &o);
            if (o.act == SYN_PRESSURE_RELEASE) {
                p.released = 1;
                p.mem_avail_mib += p.model_ram_mib;   /* what we gave back */
                flips++;
            } else if (o.act == SYN_PRESSURE_RELOAD) {
                p.released = 0;
                p.mem_avail_mib -= p.model_ram_mib;   /* what we took again */
                flips++;
            } else {
                break;
            }
        }
        check("a release settles instead of oscillating", flips <= 1 && i < 20);
    }

    /* The dwell covers a release exactly as it covers a re-fit: unloading and
     * reloading a multi-GB model every poll is the same expense either way. */
    in = base();
    WITH_MEM(in);
    in.mem_avail_mib  = 1000;
    in.since_change_s = 5;
    in.dwell_s        = 60;
    syn_pressure_decide(&in, &out);
    check("a release waits out the dwell like everything else",
          (out.act == SYN_PRESSURE_HOLD));

    printf("\n%s\n", failures ? "FAILURES" : "all pressure tests passed");
    return failures ? 1 : 0;
}
