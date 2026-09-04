#ifndef OFFLOAD_H
#define OFFLOAD_H
#include "synapd.h"

/*
 * offload.h — the thread that acts on the offload policy.
 *
 * pressure.c decides; this reads the real card, calls it, and carries out the
 * answer. The split is so the decision can be unit-tested on a machine with no
 * GPU, which is where the rule that matters — the shed/restore feedback loop —
 * is actually checkable.
 *
 * ⚠ CARRYING OUT THE ANSWER MEANS RELOADING THE MODEL. llama.cpp fixes
 * n_gpu_layers at model creation and offers no way to move a layer afterwards,
 * so every change is destroy + load, tens of seconds, and the KV cache goes
 * with it. That cost is why the policy has a dwell time and why this thread
 * polls in minutes rather than seconds.
 */

/* Start the watcher. No-op (and says so) when auto_offload is off or the box
 * has no usable GPU. Safe to call when no model is loaded yet. */
void offload_start(synapd_state_t *s);

/* Ask the watcher to stop and join it. Safe if it never started. */
void offload_stop(synapd_state_t *s);

/*
 * A client declared what it needs — SYN_MSG_DEMAND. `high` raises the floor
 * the policy defends; anything else drops it back.
 *
 * ⚠ IT DOES NOT NAME A LAYER COUNT and must not be given the chance to. The
 * caller knows a game started; it does not know how much VRAM the game wants,
 * how big the model is, or what card this is. Handing it a number would put
 * the policy in a client — and a second client with a different idea would
 * fight the first.
 *
 * Returns a short sentence for the reply, never NULL.
 */
const char *offload_set_demand(synapd_state_t *s, int high);

#endif
