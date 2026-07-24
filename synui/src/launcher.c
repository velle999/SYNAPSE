/*
 * launcher.c — the start button's SETTING. The button itself is the bar's.
 *
 * This file used to draw "◢ SYNAPSE" with cairo into a scene tree pinned to
 * every output's top-left corner, placed just above the top layer, with clicks
 * hit-tested in server_cursor_button ahead of forwarding. That made sense while
 * the bar was waybar — a foreign process synui could not put a button inside.
 *
 * It stopped making sense when the bar learned to auto-hide. quickshell slides
 * the bar's CONTENT inside a window that stays mapped and full height, so there
 * is nothing in the surface geometry for the compositor to read: the button
 * could not hide with the bar, and being above the TOP layer it sat over
 * ordinary windows. Both are structural, not bugs to patch — a button drawn by
 * one process cannot track a panel drawn by another.
 *
 * So the drawing, the positioning, the fullscreen rule and the hit test all
 * moved to quickshell/modules/Launcher.qml, and what is left here is the part
 * that was always the compositor's: the setting.
 *
 * launcher_style lives in syn_config_t and synui_config_reload replaces
 * s->config wholesale — so a bare flip would be undone by the next reload. The
 * dock/wallpaper/power settings all solve this the same way: the runtime choice
 * is written to its own state file, and synui_config_load() lays that file back
 * over synuirc on every load. The toggle then survives both a reload and a
 * logout, while synuirc's `launcher_style` stays the fallback for a box that
 * never toggled. Delete launcher.state to hand control back to synuirc.
 *
 * The bar watches launcher.state (quickshell/LauncherStyle.qml) and mirrors that
 * same precedence, so `synctl dispatch launcher_style`, the control panel row
 * and the start menu's Settings row all still work — they just repaint a QML
 * item now instead of a cairo buffer.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <wlr/util/log.h>

#include "synui.h"

/* ── Runtime style toggle + persistence (~/.config/synui/launcher.state) ── */
static bool launcher_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "launcher.state");
}

void launcher_state_load(syn_config_t *cfg)
{
    char path[256];
    if (!launcher_state_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "r");
    if (!f) return;   /* no persisted choice — synuirc's launcher_style stands */

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "style=", 6) == 0) {
            const char *v = line + 6;
            if      (strcmp(v, "text") == 0) cfg->launcher_style = SYN_LAUNCHER_TEXT;
            else if (strcmp(v, "logo") == 0) cfg->launcher_style = SYN_LAUNCHER_LOGO;
        }
    }
    fclose(f);
}

static void launcher_state_save(syn_server_t *s)
{
    char path[256];
    if (!launcher_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();
    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: launcher: cannot write '%s': %s",
                path, strerror(errno));
        return;
    }
    fprintf(f, "style=%s\n",
            s->config.launcher_style == SYN_LAUNCHER_LOGO ? "logo" : "text");
    fclose(f);
}

/* Flip text↔logo and persist. Nothing is redrawn here any more: writing the
 * state file IS the update, because the bar watches it. That is also why the
 * write must stay unconditional — it is no longer just persistence, it is the
 * only signal the button ever gets. */
void launcher_toggle_style(syn_server_t *s)
{
    s->config.launcher_style =
        (s->config.launcher_style == SYN_LAUNCHER_LOGO)
            ? SYN_LAUNCHER_TEXT : SYN_LAUNCHER_LOGO;
    launcher_state_save(s);
    wlr_log(WLR_INFO, "synui: launcher style -> %s",
            s->config.launcher_style == SYN_LAUNCHER_LOGO ? "logo" : "text");
}
