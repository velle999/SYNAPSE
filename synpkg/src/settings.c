/* settings.c — the handful of preferences synpkg remembers.
 *
 * WHY THERE IS A FILE AT ALL
 *
 * `upgrade` has three passes: the repositories, the AUR, and SynapseOS's own
 * components. The third is minutes of compiling, and it is entirely reasonable
 * to want the first two on a schedule and the third only when you ask —
 * velle's case: "in case they don't want the SOS updates but still want to use
 * the update all button".
 *
 * A flag (--no-system) already covers one run. What it cannot do is stick, and
 * the button in SYNAPSE Software runs a fixed command.
 *
 * WHY THE SETTING LIVES HERE AND NOT IN THE WINDOW
 *
 * Because `synpkg upgrade` typed into a terminal has to mean the same thing as
 * the button that runs `synpkg upgrade`. A preference the GUI held privately
 * would make the two disagree, and the one you would trust is whichever you
 * used last. So the binary owns it, the window reads and writes it through
 * `synpkg config`, and the CLI honours it without knowing a GUI exists.
 *
 * The file is INI-less on purpose: `key = value`, one per line, # comments.
 * There are two settings. A parser with sections would be more code than the
 * thing it configures.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "synpkg.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Every setting, its default, and what it means. ONE table: the CLI's listing,
 * the validator and the GUI's rows all read it, so a setting cannot exist in
 * one of those and not the others. */
static const struct {
	const char *key;
	const char *def;
	const char *desc;
} SETTINGS[] = {
	{ "upgrade_system", "yes",
	  "include SynapseOS components in `synpkg upgrade` (minutes of compiling)" },
	{ "upgrade_aur", "yes",
	  "rebuild the AUR packages synpkg installed during `synpkg upgrade`" },
	/* On by default, and it should stay that way for anyone who has not
	 * deliberately turned it off: the news is the ONLY announcement of a manual
	 * step, and the cost of the check is one HTTPS request against an upgrade
	 * that is about to download hundreds of megabytes. */
	{ "upgrade_news", "yes",
	  "show Arch news published since your last upgrade, and ask, before "
	  "`synpkg upgrade` touches anything" },
};
static const size_t N_SETTINGS = sizeof SETTINGS / sizeof SETTINGS[0];

static char *conf_path(void)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg)
		return xasprintf("%s/synpkg/synpkg.conf", xdg);
	const char *home = getenv("HOME");
	if (!home || !*home)
		return NULL;
	return xasprintf("%s/.config/synpkg/synpkg.conf", home);
}

static const char *setting_default(const char *key)
{
	for (size_t i = 0; i < N_SETTINGS; i++)
		if (!strcmp(SETTINGS[i].key, key))
			return SETTINGS[i].def;
	return NULL;
}

/* Trim in place, both ends. */
static char *trim(char *s)
{
	while (*s && isspace((unsigned char)*s))
		s++;
	if (!*s)
		return s;
	char *e = s + strlen(s) - 1;
	while (e > s && isspace((unsigned char)*e))
		*e-- = '\0';
	return s;
}

/* The stored value, or the default when the file has no opinion. Never NULL
 * for a known key: a caller asking about a setting that exists should not have
 * to decide what "absent" means — that is what the table above is for. */
char *sp_setting(const char *key)
{
	const char *def = setting_default(key);
	char *out = def ? xstrdup(def) : NULL;

	char *path = conf_path();
	if (!path)
		return out;

	FILE *f = fopen(path, "r");
	free(path);
	if (!f)
		return out;

	char line[512];
	while (fgets(line, sizeof line, f)) {
		char *p = strchr(line, '#');
		if (p)
			*p = '\0';
		char *eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		char *k = trim(line), *v = trim(eq + 1);
		if (strcmp(k, key))
			continue;
		free(out);
		out = xstrdup(v);
	}
	fclose(f);
	return out;
}

bool sp_setting_bool(const char *key)
{
	char *v = sp_setting(key);
	if (!v)
		return true;
	bool on = !strcmp(v, "yes") || !strcmp(v, "true") || !strcmp(v, "1");
	free(v);
	return on;
}

/* Rewrite the file with one key changed.
 *
 * Read-modify-write over the whole file rather than an append: appending a
 * second `upgrade_system =` line would work (last wins, above) right up until
 * someone opens the file and reads the first one. */
static int setting_write(const char *key, const char *val)
{
	char *path = conf_path();
	if (!path)
		die("neither XDG_CONFIG_HOME nor HOME is set — nowhere to save settings");

	/* mkdir -p on the parent. */
	char *slash = strrchr(path, '/');
	if (slash) {
		*slash = '\0';
		for (char *s = path + 1; *s; s++) {
			if (*s != '/')
				continue;
			*s = '\0';
			mkdir(path, 0755);
			*s = '/';
		}
		mkdir(path, 0755);
		*slash = '/';
	}

	/* Existing lines, minus any setting this key. */
	char *kept = xstrdup("");
	FILE *f = fopen(path, "r");
	if (f) {
		char line[512];
		while (fgets(line, sizeof line, f)) {
			char copy[512];
			snprintf(copy, sizeof copy, "%s", line);
			char *hash = strchr(copy, '#');
			if (hash)
				*hash = '\0';
			char *eq = strchr(copy, '=');
			bool drop = false;
			if (eq) {
				*eq = '\0';
				drop = !strcmp(trim(copy), key);
			}
			if (!drop) {
				char *j = xasprintf("%s%s", kept, line);
				free(kept);
				kept = j;
			}
		}
		fclose(f);
	}

	char *tmp = xasprintf("%s.new", path);
	FILE *o = fopen(tmp, "w");
	if (!o) {
		free(kept); free(tmp); free(path);
		warn("cannot write the settings file");
		return 1;
	}
	fputs("# synpkg settings. `synpkg config` lists them.\n", o);
	fputs(kept, o);
	fprintf(o, "%s = %s\n", key, val);
	fclose(o);
	free(kept);

	/* Rename over the original: a half-written settings file is worse than an
	 * absent one, because it parses. */
	if (rename(tmp, path) != 0) {
		unlink(tmp);
		free(tmp); free(path);
		warn("cannot replace the settings file");
		return 1;
	}
	free(tmp); free(path);
	return 0;
}

int cmd_config(int argc, char **argv)
{
	/* No arguments: list. key, value, default, description — the shape every
	 * other synpkg listing uses, so the GUI parses it with the same code. */
	if (argc == 0) {
		if (g_out == OUT_TSV)
			tsv_row(4, "key", "value", "default", "description");
		for (size_t i = 0; i < N_SETTINGS; i++) {
			char *v = sp_setting(SETTINGS[i].key);
			if (g_out == OUT_TSV) {
				tsv_row(4, SETTINGS[i].key, v ? v : "",
				        SETTINGS[i].def, SETTINGS[i].desc);
			} else {
				printf("  %-16s %-6s %s(default %s)%s\n",
				       SETTINGS[i].key, v ? v : "",
				       C_DIM(), SETTINGS[i].def, C_RESET());
				printf("  %-16s %s%s%s\n", "", C_DIM(),
				       SETTINGS[i].desc, C_RESET());
			}
			free(v);
		}
		return 0;
	}

	const char *key = argv[0];
	if (!setting_default(key)) {
		/* Named, with the alternatives. A settings command that accepts a
		 * misspelling writes a key nothing will ever read. */
		fprintf(stderr, "synpkg: unknown setting '%s'\n", key);
		fprintf(stderr, "  known settings:");
		for (size_t i = 0; i < N_SETTINGS; i++)
			fprintf(stderr, " %s", SETTINGS[i].key);
		fprintf(stderr, "\n");
		return 1;
	}

	if (argc == 1) {
		char *v = sp_setting(key);
		printf("%s\n", v ? v : "");
		free(v);
		return 0;
	}

	const char *val = argv[1];
	if (strcmp(val, "yes") && strcmp(val, "no"))
		die("config %s: expected yes or no, got '%s'", key, val);

	return setting_write(key, val);
}
