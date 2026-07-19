/*
 * wire_test.c — synapd_header_valid() unit tests
 *
 * synapd runs as root behind a socket any local process can open, so the frame
 * header check is the thing most worth pinning down. These cases are the ones
 * that would actually hurt if the gate regressed: an oversized payload_len (the
 * malloc/DoS guard), a wrong magic or version (a confused or hostile client),
 * and a short read (a truncated frame). A zero-length payload is explicitly
 * allowed here — the handlers treat a NULL payload as "no data" — so this
 * documents that it is intentional, not an oversight.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "socket_server.h"

static int failures;

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

    if (failures) {
        printf("FAIL: %d case(s)\n", failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
