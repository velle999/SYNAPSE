/*
 * sg_log.h — synguard logging
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once
#include <syslog.h>
#include <stdio.h>

/*
 * ONE level for the whole daemon. This was `static int sg_log_level` in this
 * header, which gives every translation unit that includes it a PRIVATE copy —
 * and sg_log_init() is inline, so it only ever set the caller's. main.c called
 * it, so main.c honoured --debug and config.log_level; every other module kept
 * the LOG_INFO default forever.
 *
 * Two consequences, both silent. --debug mirrored only main.c to stderr, so
 * secfeed, rule_engine, isolation and event_processor looked mute when they
 * were not. Worse, `sg_log(LOG_DEBUG, ...)` outside main.c was DEAD CODE:
 * LOG_DEBUG(7) <= LOG_INFO(6) is false, so it never reached syslog either.
 * Debug logging could not be switched on in exactly the modules worth
 * debugging. Syslog at INFO and above always worked, which is why the journal
 * looked healthy and this went unnoticed.
 *
 * Defined once in sg_log.c — every target that links a file using sg_log()
 * must link that too.
 */
extern int sg_log_level;

static inline void sg_log_init(int level) { sg_log_level = level; }

#define sg_log(level, fmt, ...) do { \
    if ((level) <= sg_log_level) { \
        syslog((level), fmt, ##__VA_ARGS__); \
        if (sg_log_level >= LOG_DEBUG) \
            fprintf(stderr, "[synguard] " fmt "\n", ##__VA_ARGS__); \
    } \
} while (0)
