#ifndef IPC_H
#define IPC_H
#include "synsh.h"
#include <stdint.h>
int  synapd_connect(synsh_state_t *s);
void synapd_disconnect(synsh_state_t *s);
int  synapd_query(synsh_state_t *s, const char *prompt, char *out_buf, size_t out_len);
int  synapd_status(synsh_state_t *s, char *out_buf, size_t out_len);
#endif
