#ifndef SOCKET_SERVER_H
#define SOCKET_SERVER_H
#include "synapd.h"
int  socket_server_start(synapd_state_t *s);
void socket_server_stop(synapd_state_t *s);
#endif
