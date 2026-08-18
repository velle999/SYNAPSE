/*
 * settings.c — settings.state, the control panel's half of the config.
 *
 * synui has always had two ways for a setting to be true: a line in synuirc,
 * which the user writes, and a `.state` file, which a panel writes when the
 * user changes something live. wallpaper.state, dock.state, power.state,
 * theme.state, welcome.state, record.state, deskicons.state — seven of them by
 * the time the control panel grew past the handful of rows each of those was
 * built for. Each knows one subject, hand-rolls its own reader and writer, and
 * has to be remembered in synui_config_load()'s tail.
 *
 * An eighth of those per-subject files was not the answer to a panel that
 * exposes every key synuirc has. This one is general: it stores `key = value`
 * lines in exactly synuirc's language, and it is read back through
 * config_parse_kv() — the same function that reads synuirc itself. That
 * matters more than the file format does. A settings panel whose writer and
 * whose config parser disagree about a key produces the specific bug where a
 * change sticks for the rest of the session, survives the save, and is gone at
 * the next login, looking the whole time like it worked. Sharing the parser
 * makes that unrepresentable: if config_parse_kv() cannot read the key back,
 * synuirc could not have set it either, and the key does not exist.
 *
 * It is loaded LAST, after synuirc and after the seven older state files, on
 * the same principle they were: the most recent explicit intent wins, and a
 * value someone changed in a panel a minute ago is more recent than the line
 * they wrote in synuirc last year. Deleting the file hands control back to
 * synuirc, which is the documented escape hatch for all of these.
 *
 * Only keys the user has actually touched are in here. That is the difference
 * between this and dumping the whole config: an untouched key stays whatever
 * synuirc (or a future synui's default) says, so upgrading does not freeze a
 * desktop at the defaults that happened to be current the first time somebody
 * opened the control panel.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "synui.h"

/* The overrides, held in memory so a change can rewrite the file without
 * re-reading it. 128 is well past the ~106 keys synuirc has; a config where
 * every single key has been changed by hand in the panel still fits. */
#define SETTINGS_MAX 128

static struct {
    char key[48];
    char val[192];
} g_over[SETTINGS_MAX];

static int g_over_n;

/*
 * Keys this file may still CONTAIN but no longer means anything by.
 *
 * A removed setting leaves its line behind in everybody's settings.state, and
 * because load() remembers every line it reads so the next save does not drop
 * it, a dead key would otherwise be rewritten forever — and re-logged by the
 * parser's "obsolete" branch at every login, which reads as a complaint about a
 * file the user never wrote. Listed here it is dropped on the way in, so the
 * first row anyone changes writes the file without it.
 *
 * Only for keys the parser has stopped acting on. A key that still works must
 * never appear here: this silently discards the user's value for it.
 */
static const char *const g_obsolete[] = {
    /* Removed with the "Super+Space opens" row: a second way to declare a
     * keybinding, which put back what the shortcuts palette rebound. */
    "super_space",
};

static int settings_key_obsolete(const char *key)
{
    for (size_t i = 0; i < sizeof(g_obsolete) / sizeof(g_obsolete[0]); i++)
        if (strcmp(key, g_obsolete[i]) == 0) return 1;
    return 0;
}

static int settings_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "settings.state") ? 1 : 0;
}

/* Where the key sits in g_over, or -1. */
static int settings_find(const char *key)
{
    for (int i = 0; i < g_over_n; i++)
        if (strcmp(g_over[i].key, key) == 0) return i;
    return -1;
}

/*
 * Write the whole table out.
 *
 * Through a temp file and rename(): the alternative is truncating the real one
 * and writing into it, which loses every setting the user has ever changed if
 * the compositor dies in the middle. rename() within a directory is atomic, so
 * a reader either sees the old file or the new one.
 */
static void settings_state_write(void)
{
    char path[256];
    if (!settings_path(path, sizeof(path))) return;

    char tmp[280];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: settings.state: %s not writable", tmp);
        return;
    }

    fprintf(f,
            "# synui settings.state — written by the control panel.\n"
            "#\n"
            "# These are synuirc keys, in synuirc's syntax, and they are read\n"
            "# by the same parser. They are applied AFTER synuirc, so a key\n"
            "# here overrides the same key there. Delete a line to hand that\n"
            "# setting back to synuirc; delete the file to hand back the lot.\n"
            "#\n"
            "# Only settings changed from their default appear here.\n\n");

    for (int i = 0; i < g_over_n; i++)
        fprintf(f, "%s = %s\n", g_over[i].key, g_over[i].val);

    fclose(f);

    if (rename(tmp, path) != 0) {
        wlr_log(WLR_ERROR, "synui: settings.state: rename failed");
        unlink(tmp);
    }
}

void settings_state_set(const char *key, const char *val)
{
    if (!key || !*key || !val) return;

    int i = settings_find(key);
    if (i < 0) {
        if (g_over_n >= SETTINGS_MAX) {
            wlr_log(WLR_ERROR, "synui: settings.state full, dropping '%s'", key);
            return;
        }
        i = g_over_n++;
        snprintf(g_over[i].key, sizeof(g_over[i].key), "%s", key);
    }
    snprintf(g_over[i].val, sizeof(g_over[i].val), "%s", val);
    settings_state_write();
}

/*
 * Forget a key — what "reset this row to its default" writes.
 *
 * Deliberately not "store the default value": storing it would pin the setting
 * to today's default for good, so a later synui that changed that default
 * would not reach a desktop whose owner had once pressed reset on the row.
 * Absent means absent.
 */
void settings_state_clear(const char *key)
{
    int i = settings_find(key);
    if (i < 0) return;

    g_over[i] = g_over[--g_over_n];
    settings_state_write();
}

/*
 * The pin set, and its settings.state line, written together.
 *
 * ONE writer, because the bitmask and the `glass_pinned` line have to move as a
 * unit: a pin set in memory and not written comes back released at the next
 * login — a row that quietly rejoins the slider some days after it was taken
 * off it. That was ctlpanel.c's rule and ctlpanel.c's alone, which is exactly
 * how transparency_set_opacity() came to edit a driven row without claiming it.
 */
void synui_glass_pins_store(syn_config_t *cfg, int pins)
{
    if (!cfg || cfg->glass_pins == pins) return;
    cfg->glass_pins = pins;

    char buf[256];
    syn_glass_pins_format(pins, buf, sizeof(buf));
    /* Empty is DROPPED, not written as an empty value — both parse back the
     * same, and a key that only appears when it has something to say is the
     * difference between a state file you can read and one you have to. */
    if (buf[0]) settings_state_set("glass_pinned", buf);
    else        settings_state_clear("glass_pinned");
}

void settings_state_load(syn_config_t *cfg)
{
    g_over_n = 0;

    char path[256];
    if (!settings_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;   /* nothing changed in a panel yet — synuirc stands */

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        s[strcspn(s, "\r\n")] = '\0';
        if (!*s || *s == '#') continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';

        char *key = s;
        char *end = eq - 1;
        while (end >= key && (*end == ' ' || *end == '\t')) *end-- = '\0';

        char *val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;

        if (!*key || g_over_n >= SETTINGS_MAX) continue;

        if (settings_key_obsolete(key)) {
            wlr_log(WLR_INFO, "synui: settings.state: dropping obsolete '%s'",
                    key);
            continue;
        }

        /* A key too long for the table would be stored truncated and would then
         * match nothing — the row would look un-overridden while its line sat
         * in the file overriding it. Drop it instead; no synuirc key is close
         * to this long, so anything that trips it is corruption. */
        size_t klen = strlen(key);
        if (klen >= sizeof(g_over[0].key)) {
            wlr_log(WLR_ERROR, "synui: settings.state: key too long, ignored");
            continue;
        }

        /* Remember it as well as apply it: the panel rewrites the whole file on
         * every change, so anything not read back here would be dropped by the
         * first save of the session. */
        int i = settings_find(key);
        if (i < 0) {
            i = g_over_n++;
            memcpy(g_over[i].key, key, klen + 1);   /* length checked above */
        }
        snprintf(g_over[i].val, sizeof(g_over[i].val), "%s", val);

        /* config_parse_kv takes a mutable value (the bind case splits it in
         * place), and it must not be handed the copy we just stored. */
        char scratch[192];
        snprintf(scratch, sizeof(scratch), "%s", g_over[i].val);
        config_parse_kv(cfg, key, scratch);
    }
    fclose(f);

    if (g_over_n)
        wlr_log(WLR_INFO, "synui: settings.state: %d override%s",
                g_over_n, g_over_n == 1 ? "" : "s");
}

int settings_state_has(const char *key)
{
    return settings_find(key) >= 0;
}
