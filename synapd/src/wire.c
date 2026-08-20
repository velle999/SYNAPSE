/*
 * wire.c — synapd wire-protocol validation, and who a request is FROM
 *
 * Two rules about untrusted input, both pure and both carrying no daemon
 * state, no llama and no sockets — so wire_test.c can hammer them directly and
 * so each rule lives in exactly one place.
 *
 * synapd_header_valid() is the daemon's entire attacker-facing input gate:
 * every frame off the socket passes through it before a work item is built or
 * a byte of payload is read.
 *
 * synapd_attributed_pid() is the answer to "who sent this", and it exists
 * because that question used to be answered by the sender.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
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

/*
 * Which PID a request is attributed to. See socket_server.h.
 *
 * ⚠ THIS USED TO BE `w->client_pid = hdr.client_pid` — identity supplied by
 * the thing being observed, in the daemon whose whole job is security
 * telemetry. The socket is 0660 root:synapse so it was never open to the
 * world, but any member of that group could claim to be any PID, and the claim
 * reached two places that matter: context_push() files the event under that
 * PID in the rolling context the model is later shown, and the anomaly log
 * names it. Attacker-controlled text could therefore be filed against another
 * process and read back as that process's history.
 *
 * The kernel's answer (SO_PEERCRED, taken at connect() time) is not forgeable
 * by the peer. It CAN be stale — the process may have exited and its PID been
 * reused since — which is a far smaller problem than a forgeable field, and is
 * the same PID-reuse race synguard already re-verifies against before acting.
 * Nothing on this path kills anything; the value is for attribution, and
 * attribution wants the kernel's answer.
 */
pid_t synapd_attributed_pid(pid_t cred_pid, bool cred_ok,
                            uint32_t claimed, int *lied)
{
    if (lied) *lied = 0;

    /* No credentials, no attribution. ⚠ NOT a fallback to the claim: an
     * unverifiable identity and a self-asserted one are the same thing, and
     * the entire point here is that the second is not evidence. 0 is
     * "unknown", and it is honest. */
    if (!cred_ok) return 0;

    /* A claim of 0 is "did not fill it in", which every client may do and
     * which contradicts nothing. Only a claim that DISAGREES is a lie. */
    if (claimed != 0 && (pid_t)claimed != cred_pid && lied)
        *lied = 1;

    return cred_pid;
}
