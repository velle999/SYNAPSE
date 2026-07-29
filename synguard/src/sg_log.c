/*
 * sg_log.c — the one definition of synguard's log level
 *
 * Exists solely so sg_log_level is a single object shared by every module,
 * rather than one private copy per translation unit. See the comment in
 * sg_log.h for what that cost: --debug that only worked in main.c, and
 * sg_log(LOG_DEBUG, ...) that was unreachable everywhere else.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <syslog.h>

/* LOG_INFO until sg_log_init() says otherwise — the daemon logs from several
 * threads before options are parsed, and silence there would be worse than a
 * little noise. */
int sg_log_level = LOG_INFO;
