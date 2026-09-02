/* config.c — the options, and the file they persist in.
 *
 * One table. Every option is named once, with its type, its bounds and its
 * default, and everything else — :set, the CLI, the settings file, the list
 * the GUI draws — walks that table. synfiles learned this the expensive way:
 * a settings file that accepts any key accumulates typos that never read back,
 * and looks identical to a settings file that is not being read at all.
 *
 * So: an unknown key is REFUSED, an out-of-range number is CLAMPED, and a bad
 * enum is refused. A stored value that is invalid falls back to the default
 * rather than failing the load — a file edited by hand should cost you that
 * one setting, not the ability to start the editor.
 *
 * ⚠ Written by the BINARY, never by the GUI. quickshell's FileView silently
 * drops setText() on a path that does not exist yet, which is exactly a fresh
 * install — see [[reference_quickshell_fileview_missing_path]].
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-edit.h"
#include "i18n.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum { O_BOOL, O_INT } otype;

typedef struct {
	const char *key;
	const char *alias;      /* vim's abbreviation, e.g. "ts" for tabstop */
	otype type;
	size_t off;             /* into opts_t */
	long lo, hi;            /* for O_INT */
	long def;
} odef;

#define OFF(f) offsetof(opts_t, f)

static const odef OPTS[] = {
	{ "tabstop",    "ts",  O_INT,  OFF(tabstop),    1, 16,  4 },
	{ "shiftwidth", "sw",  O_INT,  OFF(shiftwidth), 1, 16,  4 },
	{ "expandtab",  "et",  O_BOOL, OFF(expandtab),  0, 1,   0 },
	{ "autoindent", "ai",  O_BOOL, OFF(autoindent), 0, 1,   1 },
	{ "number",     "nu",  O_BOOL, OFF(number),     0, 1,   1 },
	{ "ignorecase", "ic",  O_BOOL, OFF(ignorecase), 0, 1,   1 },
	{ "smartcase",  "scs", O_BOOL, OFF(smartcase),  0, 1,   1 },
	{ "wrap",       "",    O_BOOL, OFF(wrap),       0, 1,   0 },
	{ "hlsearch",   "hls", O_BOOL, OFF(hlsearch),   0, 1,   1 },
	{ "tabbar",     "",    O_BOOL, OFF(showtabs),   0, 1,   1 },
	{ "tree",       "",    O_BOOL, OFF(tree),       0, 1,   1 },
	/* The sidebar's width in PIXELS, dragged rather than typed — the GUI's
	 * `set` writes it down (see serve.c), so a panel sized once stays sized.
	 * The floor is not cosmetic: a panel narrower than its own two-line rows
	 * shows a file name elided to nothing, which reads as an empty list. */
	{ "treewidth",  "",    O_INT,  OFF(tree_width), 150, 480, 230 },
	{ "text_scale", "",    O_INT,  OFF(text_scale), 75, 175, 100 },
};

static const size_t NOPTS = sizeof OPTS / sizeof *OPTS;

size_t opts_count(void) { return NOPTS; }
const char *opts_key_at(size_t i) { return i < NOPTS ? OPTS[i].key : ""; }

static const odef *find_opt(const char *key)
{
	for (size_t i = 0; i < NOPTS; i++) {
		if (strcmp(OPTS[i].key, key) == 0)
			return &OPTS[i];
		if (*OPTS[i].alias && strcmp(OPTS[i].alias, key) == 0)
			return &OPTS[i];
	}
	return NULL;
}

void opts_defaults(opts_t *o)
{
	memset(o, 0, sizeof *o);
	for (size_t i = 0; i < NOPTS; i++) {
		const odef *d = &OPTS[i];
		if (d->type == O_BOOL)
			*(bool *)((char *)o + d->off) = (d->def != 0);
		else
			*(int *)((char *)o + d->off) = (int)d->def;
	}
}

bool opts_get(const opts_t *o, const char *key, char *out, size_t n)
{
	const odef *d = find_opt(key);
	if (!d)
		return false;
	if (d->type == O_BOOL)
		snprintf(out, n, "%s", *(const bool *)((const char *)o + d->off)
		                       ? "true" : "false");
	else
		snprintf(out, n, "%d", *(const int *)((const char *)o + d->off));
	return true;
}

bool opts_set(opts_t *o, const char *key, const char *val, char **err)
{
	const odef *d = find_opt(key);
	if (!d) {
		if (err)
			*err = xasprintf("unknown option: %s", key);
		return false;
	}

	if (d->type == O_BOOL) {
		bool v;
		if (!strcmp(val, "true") || !strcmp(val, "1") || !strcmp(val, "on")
		 || !strcmp(val, "yes"))
			v = true;
		else if (!strcmp(val, "false") || !strcmp(val, "0") || !strcmp(val, "off")
		      || !strcmp(val, "no"))
			v = false;
		else {
			if (err)
				*err = xasprintf("%s takes true or false, not '%s'", d->key, val);
			return false;
		}
		*(bool *)((char *)o + d->off) = v;
		return true;
	}

	char *end = NULL;
	long v = strtol(val, &end, 10);
	if (end == val || (end && *end)) {
		if (err)
			*err = xasprintf("%s takes a number, not '%s'", d->key, val);
		return false;
	}
	/* Clamped rather than refused: somebody who asks for tabstop=200 wants a
	 * big tab, and refusing leaves them with the old value and a message they
	 * may not have read. A refusal is right for a nonsense TYPE, not for a
	 * number that is merely out of range. */
	if (v < d->lo)
		v = d->lo;
	if (v > d->hi)
		v = d->hi;
	*(int *)((char *)o + d->off) = (int)v;
	return true;
}

static char *settings_path(void)
{
	const char *ov = getenv("SYN_EDIT_CONFIG");
	if (ov && *ov)
		return xstrdup(ov);
	char *dir = config_dir();
	char *p = xasprintf("%s/settings", dir);
	free(dir);
	return p;
}

void opts_load(opts_t *o)
{
	opts_defaults(o);
	char *path = settings_path();
	char *text = slurp(path);
	free(path);
	if (!text)
		return;

	for (char *line = text; line && *line; ) {
		char *nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';
		char *hash = strchr(line, '#');
		if (hash)
			*hash = '\0';
		char *eq = strchr(line, '=');
		if (eq) {
			*eq = '\0';
			char *k = line, *v = eq + 1;
			while (*k == ' ' || *k == '\t')
				k++;
			for (char *t = k + strlen(k); t > k && (t[-1] == ' ' || t[-1] == '\t'); )
				*--t = '\0';
			while (*v == ' ' || *v == '\t')
				v++;
			for (char *t = v + strlen(v); t > v && (t[-1] == ' ' || t[-1] == '\t'); )
				*--t = '\0';
			/* A stored value that no longer parses is DROPPED, not fatal. */
			char *err = NULL;
			if (!opts_set(o, k, v, &err) && g_verbose)
				warn("%s", err ? err : "bad setting");
			free(err);
		}
		line = nl ? nl + 1 : NULL;
	}
	free(text);
}

/* Written whole and renamed into place, so two quick writes cannot interleave
 * and leave half a file. */
bool opts_save(const opts_t *o, char **err)
{
	char *path = settings_path();
	char *slash = strrchr(path, '/');
	if (slash) {
		char *dir = xstrndup(path, (size_t)(slash - path));
		mkdir_p(dir);
		free(dir);
	}

	char *tmp = xasprintf("%s.new", path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		if (err)
			*err = xasprintf("cannot write %s", path);
		free(tmp);
		free(path);
		return false;
	}
	fputs("# syn-edit settings — written by `syn-edit config set`\n", f);
	for (size_t i = 0; i < NOPTS; i++) {
		char val[64];
		opts_get(o, OPTS[i].key, val, sizeof val);
		fprintf(f, "%s = %s\n", OPTS[i].key, val);
	}
	bool ok = (fflush(f) == 0);
	if (ok)
		ok = (fsync(fileno(f)) == 0);
	if (fclose(f) != 0)
		ok = false;
	if (ok && rename(tmp, path) != 0)
		ok = false;
	if (!ok) {
		unlink(tmp);
		if (err)
			*err = xasprintf("cannot write %s", path);
	}
	free(tmp);
	free(path);
	return ok;
}

/* ── the CLI ────────────────────────────────────────────────────────────── */

int cmd_config(int argc, char **argv)
{
	opts_t o;
	opts_load(&o);

	const char *sub = argc > 0 ? argv[0] : "list";

	if (!strcmp(sub, "list")) {
		if (g_out == OUT_REC)
			rec_row(3, "key", "value", "default");
		for (size_t i = 0; i < NOPTS; i++) {
			char val[64];
			opts_get(&o, OPTS[i].key, val, sizeof val);
			char def[64];
			if (OPTS[i].type == O_BOOL)
				snprintf(def, sizeof def, "%s", OPTS[i].def ? "true" : "false");
			else
				snprintf(def, sizeof def, "%ld", OPTS[i].def);
			if (g_out == OUT_REC)
				rec_row(3, OPTS[i].key, val, def);
			else
				printf("  %s%-14s%s %-8s %s(default %s)%s\n", C_DIM(),
				       OPTS[i].key, C_RESET(), val, C_DIM(), def, C_RESET());
		}
		return 0;
	}

	if (!strcmp(sub, "get")) {
		if (argc < 2)
			die(_("config get: needs a key"));
		char val[64];
		if (!opts_get(&o, argv[1], val, sizeof val))
			die(_("unknown option: %s"), argv[1]);
		printf("%s\n", val);
		return 0;
	}

	if (!strcmp(sub, "set")) {
		if (argc < 3)
			die(_("config set: needs a key and a value"));
		char *err = NULL;
		if (!opts_set(&o, argv[1], argv[2], &err))
			die("%s", err ? err : "bad value");
		if (!opts_save(&o, &err))
			die("%s", err ? err : "could not save");
		char val[64];
		opts_get(&o, argv[1], val, sizeof val);
		printf("%s = %s\n", argv[1], val);
		return 0;
	}

	if (!strcmp(sub, "reset")) {
		opts_defaults(&o);
		char *err = NULL;
		if (!opts_save(&o, &err))
			die("%s", err ? err : "could not save");
		printf("%s\n", _("settings reset to defaults"));
		return 0;
	}

	die(_("config: unknown subcommand '%s' (list, get, set, reset)"), sub);
}
