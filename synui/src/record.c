/*
 * record.c — the screen recorder's AUDIO setting. The recording is
 * synui-record's; only the switch is the compositor's.
 *
 * Super+Shift+R has always captured video and nothing else, because
 * wf-recorder's bare `-a` is not "record the sound" — it hands ffmpeg's pulse
 * demuxer no device, and that resolves to the default *source*, i.e. the
 * microphone. A screen recorder that silently opens the mic is a privacy
 * problem, so audio stayed off and the flag stayed undocumented.
 *
 * What people actually want is the default sink's MONITOR: the game, the video,
 * the call — sound the screen is already making, carrying nothing the recording
 * does not already show. synui-record resolves that device itself (--audio, or
 * --audio=mic for the explicit other case); this file decides whether synui
 * passes the flag at all.
 *
 * Why a setting and not a second keybind: the two are the same action with the
 * same stop path, and a separate "record with audio" bind means every stop is a
 * guess about which one started it — pkill -x wf-recorder ends whichever is
 * running, so a mismatched pair would look like the wrong key working. One
 * bind, one recorder, and a switch that says what it will capture.
 *
 * record_audio lives in syn_config_t and synui_config_reload replaces s->config
 * wholesale, so a bare flip would be undone by the next reload. Same answer as
 * launcher.c/dock.c: the runtime choice goes to its own state file and
 * synui_config_load lays it back over synuirc every time. Delete record.state
 * to hand control back to the synuirc `record_audio` line.
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

/* ── Persistence (~/.config/synui/record.state) ──────────── */
static bool record_state_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "record.state");
}

void record_audio_state_load(syn_config_t *cfg)
{
    char path[256];
    if (!record_state_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "r");
    if (!f) return;   /* no persisted choice — synuirc's record_audio stands */

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "audio=", 6) == 0)
            cfg->record_audio = strcmp(line + 6, "on") == 0;
    }
    fclose(f);
}

static void record_state_save(syn_server_t *s)
{
    char path[256];
    if (!record_state_path(path, sizeof(path))) return;
    syn_config_ensure_dir();
    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: record: cannot write '%s': %s",
                path, strerror(errno));
        return;
    }
    fprintf(f, "audio=%s\n", s->config.record_audio ? "on" : "off");
    fclose(f);
}

/* Flip and persist. Nothing else to update: the flag is read at the moment the
 * `record` action spawns synui-record, so a recording already in flight keeps
 * whatever it started with — changing a running capture's tracks mid-take is
 * not something wf-recorder can do, and pretending otherwise would produce a
 * file that disagrees with the toast that announced it. */
void record_audio_toggle(syn_server_t *s)
{
    s->config.record_audio = !s->config.record_audio;
    record_state_save(s);
    wlr_log(WLR_INFO, "synui: record audio -> %s",
            s->config.record_audio ? "on" : "off");
}
