/*
 * config.c — Parse synuirc configuration
 *
 * Reads ~/.config/synui/synuirc or /etc/synui/synuirc.
 * Format: key = value (one per line), # comments.
 *
 * SynapseOS Project — GPLv2
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "synui.h"

static char *strip(char *s)
{
    while (isspace(*s)) s++;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace(*e)) *e-- = '\0';
    return s;
}

void synui_config_load(syn_config_t *cfg)
{
    /* Defaults */
    strncpy(cfg->terminal, "foot", sizeof(cfg->terminal) - 1);
    cfg->autostart_count = 1;
    strncpy(cfg->autostart[0], "foot", sizeof(cfg->autostart[0]) - 1);
    cfg->border_width = 2;
    cfg->gap = 8;
    cfg->master_factor = 0.60f;
    cfg->ai_layout = 1;
    cfg->ai_ctx_decor = 1;
    cfg->start_overlay = 0;

    /* Try user config, then system-wide */
    const char *paths[2] = { NULL, "/etc/synui/synuirc" };
    char user_path[256] = {0};
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg)
        snprintf(user_path, sizeof(user_path), "%s/synui/synuirc", xdg);
    else if (home)
        snprintf(user_path, sizeof(user_path), "%s/.config/synui/synuirc", home);
    paths[0] = user_path;

    FILE *f = NULL;
    for (int i = 0; i < 2; i++) {
        if (!paths[i] || !paths[i][0]) continue;
        f = fopen(paths[i], "r");
        if (f) break;
    }
    if (!f) return;

    /* Config file found — reset autostart so file entries replace defaults */
    cfg->autostart_count = 0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char *s = strip(line);
        if (!*s) continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = strip(s);
        char *val = strip(eq + 1);

        if (strcmp(key, "terminal") == 0)
            strncpy(cfg->terminal, val, sizeof(cfg->terminal) - 1);
        else if (strcmp(key, "autostart") == 0 && cfg->autostart_count < SYN_AUTOSTART_MAX)
            strncpy(cfg->autostart[cfg->autostart_count++], val, 127);
        else if (strcmp(key, "border_width") == 0)
            cfg->border_width = atoi(val);
        else if (strcmp(key, "gap") == 0)
            cfg->gap = atoi(val);
        else if (strcmp(key, "master_factor") == 0)
            cfg->master_factor = strtof(val, NULL);
        else if (strcmp(key, "ai_layout") == 0)
            cfg->ai_layout = strcmp(val, "on") == 0;
        else if (strcmp(key, "ai_ctx_decor") == 0)
            cfg->ai_ctx_decor = strcmp(val, "on") == 0;
        else if (strcmp(key, "start_overlay") == 0)
            cfg->start_overlay = strcmp(val, "on") == 0;
    }

    fclose(f);
}
