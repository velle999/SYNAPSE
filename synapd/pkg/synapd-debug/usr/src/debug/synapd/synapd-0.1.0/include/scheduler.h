#ifndef SCHEDULER_H
#define SCHEDULER_H
#include "synapd.h"

int  scheduler_init(synapd_state_t *s);
void scheduler_destroy(synapd_state_t *s);
void scheduler_apply_hint(pid_t pid, int nice_val, int sched_class);
void scheduler_read_events(synapd_state_t *s);
void scheduler_write_heartbeat(synapd_state_t *s);
void scheduler_write_status(synapd_state_t *s, const char *status);
void scheduler_heartbeat(synapd_state_t *s);
#endif
