#ifndef SCHEDULER_H
#define SCHEDULER_H
#include "synapd.h"

int  scheduler_init(synapd_state_t *s);
void scheduler_destroy(synapd_state_t *s);
int  scheduler_write_hint(synapd_state_t *s, pid_t pid,
                           int nice_delta, const char *sched_class);
void scheduler_write_status(synapd_state_t *s, const char *status);
void scheduler_heartbeat(synapd_state_t *s);
#endif
