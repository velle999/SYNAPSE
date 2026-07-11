#ifndef SOCKET_SERVER_H
#define SOCKET_SERVER_H
#include <stdbool.h>
#include "synapd.h"
int  socket_server_start(synapd_state_t *s);
void socket_server_stop(synapd_state_t *s);

/* Is this a frame header we are willing to act on? Rejects a short read, a bad
 * magic or protocol version, and a payload_len past SYN_MAX_PAYLOAD. Pulled out
 * of the recv loop so it can be unit-tested without a running daemon (this is
 * the daemon's whole attacker-facing input gate, so it is the thing most worth
 * a test). `recvd` is how many bytes were actually read into *hdr. */
bool synapd_header_valid(const syn_msg_header_t *hdr, size_t recvd);
#endif
