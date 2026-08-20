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

/* WHICH PID A REQUEST IS ATTRIBUTED TO.
 *
 * The kernel's answer, or 0 when there is not one. `cred_pid` is what
 * SO_PEERCRED reported and `cred_ok` whether that call succeeded; `claimed` is
 * the sender's own hdr.client_pid, which is advisory and never believed.
 *
 * Pulled out of the accept loop for the same reason synapd_header_valid() was:
 * the part that can regress is the POLICY — whose answer wins, what an absent
 * claim means, when a disagreement is worth reporting — and none of that needs
 * a socket to test. The getsockopt() call itself is one line and cannot be
 * meaningfully unit-tested; this is the decision it feeds.
 *
 * *lied, when non-NULL, is set to 1 for a claim that CONTRADICTS the kernel —
 * not merely one that is absent. A client that left the field at 0 has not
 * lied about anything. */
pid_t synapd_attributed_pid(pid_t cred_pid, bool cred_ok,
                            uint32_t claimed, int *lied);

/* The model-name boundary and the remembered choice live in selected.h — pure,
 * and tested on their own by tests/selected_test.c. */
#endif
