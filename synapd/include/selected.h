#ifndef SYNAPD_SELECTED_H
#define SYNAPD_SELECTED_H
#include <stddef.h>
#include "synapd.h"

/* The boundary that makes SYN_MSG_RELOAD safe: a bare filename inside
 * SYNAPD_MODEL_DIR, or a refusal naming the rule that was broken. Never a path.
 * Returns 0 and fills out[] on success, -1 with *why set otherwise. */
int  synapd_model_resolve(const char *name, char *out, size_t out_len,
                          const char **why);

/* ── The model chosen at runtime ──────────────────────────────
 *
 * A SYN_MSG_RELOAD switch used to last exactly as long as the process: the
 * ExecStart drop-in still named the old model, so picking one in the control
 * panel and rebooting put the previous one back with nothing to say why. These
 * two make the choice outlive the daemon.
 *
 * load() writes the bare filename into out[] and returns 1 when the file names
 * a model that is STILL resolvable inside SYNAPD_MODEL_DIR; 0 when there is no
 * choice on record, and also when the recorded one has since been deleted —
 * a stale pick must never be able to stop the daemon starting. */
void synapd_selected_save(const char *bare_name);
int  synapd_selected_load(char *out, size_t out_len);

#endif
