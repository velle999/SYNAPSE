/*
 * saver_state.c — the screensaver's vocabulary and its state file.
 *
 * Split out of saver.c for the reason imgdec.c was split out of wallpaper.c:
 * so something can link the REAL reader without pulling in the compositor.
 * Here that something is tests/state_reload_test.c, which exists because this
 * tree keeps shipping the same bug — a setting loaded once at startup from a
 * .state file, and therefore silently reset by every config reload (theme.state
 * was the first, filters.state and uifx.state the next two). A test that
 * reimplemented the file format would pass while production read a different
 * one, so the test has to link this file itself.
 *
 * Nothing here touches the scene graph, a timer or a cairo surface. Everything
 * that does stayed in saver.c.
 *
 * The MODE and BACKGROUND vocabularies live here too, not because they are
 * state, but because the state file is written in them: the parser, the panel
 * and the config file all resolve names through these two tables, and a second
 * copy of either is a second thing to keep in step.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

#include "synui.h"

/* Slideshow interval bounds, shared with the panel's stepper in saver.c. Below
 * the floor the crossfade never finishes before the next image starts. */
#define SAVER_INTERVAL_MIN 5
#define SAVER_INTERVAL_MAX 600

/* Indexed by syn_saver_mode_t — keep in step with the enum in synui.h. */
const char *const syn_saver_mode_names[SYN_SAVER_MODE_COUNT] = {
    [SYN_SAVER_BLANK]     = "blank",
    [SYN_SAVER_CLOCK]     = "clock",
    [SYN_SAVER_STARFIELD] = "starfield",
    [SYN_SAVER_SLIDESHOW] = "slideshow",
    [SYN_SAVER_MATRIX]    = "matrix",
};

int saver_mode_from_name(const char *name)
{
    for (int i = 0; i < SYN_SAVER_MODE_COUNT; i++)
        if (strcmp(name, syn_saver_mode_names[i]) == 0) return i;
    return -1;
}

/* Indexed by syn_lock_bg_t. Lives here rather than in lock.c because the panel
 * that edits it is here, and the parser wants one home for the vocabulary. */
const char *const syn_lock_bg_names[SYN_LOCK_BG_COUNT] = {
    [SYN_LOCK_BG_DESKTOP] = "desktop",
    [SYN_LOCK_BG_BLACK]   = "black",
    [SYN_LOCK_BG_IMAGE]   = "image",
};

int lock_bg_from_name(const char *name)
{
    for (int i = 0; i < SYN_LOCK_BG_COUNT; i++)
        if (strcmp(name, syn_lock_bg_names[i]) == 0) return i;
    return -1;
}

/* Indexed by syn_lock_layout_t — when the lock screen shows the keyboard
 * layout chip. Here for the same reason the two tables above are: the panel,
 * the parser and the state file all resolve the word through one list. */
const char *const syn_lock_layout_names[SYN_LOCK_LAYOUT_COUNT] = {
    [SYN_LOCK_LAYOUT_AUTO] = "auto",
    [SYN_LOCK_LAYOUT_ON]   = "on",
    [SYN_LOCK_LAYOUT_OFF]  = "off",
};

int lock_layout_from_name(const char *name)
{
    for (int i = 0; i < SYN_LOCK_LAYOUT_COUNT; i++)
        if (strcmp(name, syn_lock_layout_names[i]) == 0) return i;
    return -1;
}

/* ── State file ──────────────────────────────────────────── */

static bool saver_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "saver.state");
}

void saver_state_save(syn_server_t *s)
{
    char path[256];
    if (!saver_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: saver: cannot write '%s': %s",
                path, strerror(errno));
        snprintf(s->saver.status, sizeof(s->saver.status),
                 "save failed: %s", strerror(errno));
        return;
    }

    /* Modes are NAMES, not indices — the enum will grow and a saved 3 must not
     * silently become a different mode after it does. */
    fprintf(f, "mode=%s\n",     syn_saver_mode_names[s->config.saver_mode]);
    fprintf(f, "timeout=%d\n",  s->config.saver_timeout);
    fprintf(f, "lock=%d\n",     s->config.saver_lock ? 1 : 0);
    fprintf(f, "interval=%d\n", s->config.saver_interval);
    if (s->config.saver_dir[0]) fprintf(f, "dir=%s\n", s->config.saver_dir);
    fprintf(f, "lock_bg=%s\n",  syn_lock_bg_names[s->config.lock_bg]);
    if (s->config.lock_bg_image[0])
        fprintf(f, "lock_bg_image=%s\n", s->config.lock_bg_image);
    fprintf(f, "lock_dim=%d\n",   s->config.lock_bg_dim);
    fprintf(f, "lock_blur=%d\n",  s->config.lock_bg_blur);
    fprintf(f, "lock_follow=%d\n", s->config.lock_theme_follow ? 1 : 0);
    fprintf(f, "lock_media=%d\n",  s->config.lock_media ? 1 : 0);
    fprintf(f, "weather=%d\n", s->config.weather ? 1 : 0);
    fprintf(f, "weather_unit=%c\n", s->config.weather_unit_f ? 'F' : 'C');
    fprintf(f, "lock_layout=%s\n", syn_lock_layout_names[s->config.lock_layout]);
    fclose(f);

    s->saver.dirty = 0;
    snprintf(s->saver.status, sizeof(s->saver.status), "saved to saver.state");
}

void saver_state_load(syn_config_t *cfg)
{
    char path[256];
    if (!saver_state_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;   /* no persisted choice — synuirc stands */

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line, *sval = eq + 1;
        int val = atoi(sval);

        if (strcmp(key, "mode") == 0) {
            int m = saver_mode_from_name(sval);
            if (m >= 0) cfg->saver_mode = m;
        } else if (strcmp(key, "timeout") == 0) {
            cfg->saver_timeout = val < 0 ? 0 : val;
        } else if (strcmp(key, "lock") == 0) {
            cfg->saver_lock = val ? 1 : 0;
        } else if (strcmp(key, "interval") == 0) {
            if (val >= SAVER_INTERVAL_MIN && val <= SAVER_INTERVAL_MAX)
                cfg->saver_interval = val;
        } else if (strcmp(key, "dir") == 0) {
            snprintf(cfg->saver_dir, sizeof(cfg->saver_dir), "%s", sval);
        } else if (strcmp(key, "lock_bg") == 0) {
            int b = lock_bg_from_name(sval);
            if (b >= 0) cfg->lock_bg = b;
        } else if (strcmp(key, "lock_bg_image") == 0) {
            snprintf(cfg->lock_bg_image, sizeof(cfg->lock_bg_image), "%s", sval);
        } else if (strcmp(key, "lock_dim") == 0) {
            cfg->lock_bg_dim = val < 0 ? 0 : (val > 100 ? 100 : val);
        } else if (strcmp(key, "lock_blur") == 0) {
            cfg->lock_bg_blur = val < 0 ? 0 : (val > 64 ? 64 : val);
        } else if (strcmp(key, "lock_follow") == 0) {
            cfg->lock_theme_follow = val ? 1 : 0;
        } else if (strcmp(key, "lock_media") == 0) {
            cfg->lock_media = val ? 1 : 0;
        /* `lock_weather` is what this row wrote until the bar module and the
         * desktop widget started reading the same engine. Every machine that
         * ever turned the weather on has the old spelling on disk, and the file
         * is rewritten with the new one the next time the panel saves. */
        } else if (strcmp(key, "weather") == 0 ||
                   strcmp(key, "lock_weather") == 0) {
            cfg->weather = val ? 1 : 0;
        } else if (strcmp(key, "weather_unit") == 0 ||
                   strcmp(key, "lock_weather_unit") == 0) {
            /* A letter, not a flag: `unit=1` would be a file nobody could read
             * and the two units are one character each. */
            cfg->weather_unit_f = (sval[0] == 'F' || sval[0] == 'f');
        } else if (strcmp(key, "lock_layout") == 0) {
            int l = lock_layout_from_name(sval);
            if (l >= 0) cfg->lock_layout = l;
        }
    }
    fclose(f);
}
