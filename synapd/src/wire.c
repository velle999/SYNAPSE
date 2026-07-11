/*
 * wire.c — synapd wire-protocol validation
 *
 * The one function here is the daemon's entire attacker-facing input gate:
 * every frame off the socket passes through synapd_header_valid() before a work
 * item is built or a byte of payload is read. It is deliberately pure and
 * carries no daemon state, no llama, no sockets — so socket_server_test.c can
 * hammer it with malformed frames directly, and so the rule for what synapd
 * will act on lives in exactly one place.
 *
 * SynapseOS Project — GPLv2
 * https://github.com/velle999/SYNAPSE
 */

#include "socket_server.h"

bool synapd_header_valid(const syn_msg_header_t *hdr, size_t recvd)
{
    if (recvd != sizeof(*hdr))               return false;  /* short/partial read */
    if (hdr->magic != SYN_MAGIC)             return false;
    if (hdr->version != SYNAPD_PROTOCOL_VER) return false;
    if (hdr->payload_len > SYN_MAX_PAYLOAD)  return false;  /* DoS / overflow guard */
    return true;
}
