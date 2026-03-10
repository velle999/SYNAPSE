/*
 * sg_log.h — synguard logging
 * SynapseOS Project — GPLv2
 */
#pragma once
#include <syslog.h>
#include <stdio.h>

static int sg_log_level = LOG_INFO;

static inline void sg_log_init(int level) { sg_log_level = level; }

#define sg_log(level, fmt, ...) do { \
    if ((level) <= sg_log_level) { \
        syslog((level), fmt, ##__VA_ARGS__); \
        if (sg_log_level >= LOG_DEBUG) \
            fprintf(stderr, "[synguard] " fmt "\n", ##__VA_ARGS__); \
    } \
} while (0)
