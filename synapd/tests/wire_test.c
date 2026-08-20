/*
 * wire_test.c — synapd_header_valid() unit tests
 *
 * ⚠ synapd does NOT run as root — it runs as its own unprivileged `synapd`
 * user with an empty CapabilityBoundingSet, and systemd owns the socket
 * (0660 root:synapse). This comment used to say it did. That matters for
 * reading these tests: the frame header check is not the last line before
 * root, it is the last line before a daemon that parses attacker-controlled
 * text and talks to a language model — which is still worth pinning down. These cases are the ones
 * that would actually hurt if the gate regressed: an oversized payload_len (the
 * malloc/DoS guard), a wrong magic or version (a confused or hostile client),
 * and a short read (a truncated frame). A zero-length payload is explicitly
 * allowed here — the handlers treat a NULL payload as "no data" — so this
 * documents that it is intentional, not an oversight.
 *
 * The second half of this file is synapd_attributed_pid(), which answers "who
 * sent this". It is here because the answer used to come from the sender.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "socket_server.h"

static int failures;

static void checkpid(const char *name, pid_t got, pid_t want)
{
    if (got == want) {
        printf("  ok    %s\n", name);
    } else {
        printf("  FAIL  %s — expected pid %d, got %d\n", name, (int)want, (int)got);
        failures++;
    }
}

static void check(const char *name, bool got, bool want)
{
    if (got == want) {
        printf("  ok   - %s\n", name);
    } else {
        printf("  FAIL - %s (got %s, want %s)\n",
               name, got ? "valid" : "invalid", want ? "valid" : "invalid");
        failures++;
    }
}

/* A header that passes, as a baseline to mutate one field at a time from. */
static syn_msg_header_t good(void)
{
    syn_msg_header_t h = {0};
    h.magic       = SYN_MAGIC;
    h.version     = SYNAPD_PROTOCOL_VER;
    h.msg_type    = SYN_MSG_STATUS;
    h.payload_len = 0;
    return h;
}

int main(void)
{
    syn_msg_header_t h;

    printf("synapd_header_valid:\n");

    h = good();
    check("well-formed STATUS frame", synapd_header_valid(&h, sizeof(h)), true);

    h = good();
    h.payload_len = SYN_MAX_PAYLOAD;
    check("payload_len exactly at the cap", synapd_header_valid(&h, sizeof(h)), true);

    h = good();
    h.payload_len = SYN_MAX_PAYLOAD + 1;
    check("payload_len one past the cap → rejected",
          synapd_header_valid(&h, sizeof(h)), false);

    h = good();
    h.payload_len = 0xFFFFFFFFu;   /* the classic "huge length" overflow probe */
    check("payload_len = UINT32_MAX → rejected",
          synapd_header_valid(&h, sizeof(h)), false);

    h = good();
    h.magic = 0xDEADBEEFu;
    check("bad magic → rejected", synapd_header_valid(&h, sizeof(h)), false);

    h = good();
    h.version = SYNAPD_PROTOCOL_VER + 1;
    check("wrong protocol version → rejected",
          synapd_header_valid(&h, sizeof(h)), false);

    h = good();
    check("short read (one byte less than a header) → rejected",
          synapd_header_valid(&h, sizeof(h) - 1), false);

    h = good();
    check("zero bytes read → rejected", synapd_header_valid(&h, 0), false);


    /* ── WHO SENT IT ─────────────────────────────────────────────────────
     *
     * The rule: the kernel's answer wins, always. A claim is advisory, an
     * absent claim is not a lie, and no credentials means no attribution
     * rather than falling back to the claim — which would hand the decision
     * straight back to the sender by another route.
     */
    printf("\nsynapd_attributed_pid:\n");
    {
        int lied = -1;

        /* The ordinary case: a client that told the truth. */
        checkpid("a truthful claim is attributed to the kernel's pid",
                 synapd_attributed_pid(4242, true, 4242, &lied), 4242);
        check("…and is not flagged as a lie", lied == 0, true);

        /* THE BUG. A client claiming somebody else's PID must not get it. */
        lied = -1;
        checkpid("a claim that contradicts the kernel is IGNORED",
                 synapd_attributed_pid(4242, true, 1, &lied), 4242);
        check("…and IS flagged, so the attempt is visible", lied == 1, true);

        /* Not filling the field in is every client's right. */
        lied = -1;
        checkpid("an absent claim still attributes to the kernel's pid",
                 synapd_attributed_pid(4242, true, 0, &lied), 4242);
        check("…and is not a lie", lied == 0, true);

        /* ⚠ The one that would quietly undo all of it: falling back to the
         * claim when SO_PEERCRED fails. That is the sender deciding again. */
        lied = -1;
        checkpid("no credentials attributes to NOBODY, not to the claim",
                 synapd_attributed_pid(0, false, 1, &lied), 0);
        check("…and does not report a lie it cannot know about", lied == 0, true);

        /* NULL is allowed — the caller may not care. */
        checkpid("a NULL lied pointer is accepted",
                 synapd_attributed_pid(7, true, 9, NULL), 7);
    }

    if (failures) {
        printf("FAIL: %d case(s)\n", failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
