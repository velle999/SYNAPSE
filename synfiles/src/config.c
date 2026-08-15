/* config.c — remembered settings.
 *
 * The GUI does not write this file itself, and that is deliberate. quickshell's
 * FileView SILENTLY DROPS setText() when the path does not exist yet — no
 * error, no saved signal, nothing — so a QML component that owned its own
 * settings file would lose everything until somebody happened to create the
 * file by other means. That already cost synui its desktop post-it on a fresh
 * install. Writing through the binary avoids the whole class of problem, and
 * keeps disk state where the rest of it already lives.
 *
 * Every key is DECLARED, with a type and a range. An unknown key is refused
 * rather than written, because a settings file that accepts anything silently
 * accumulates typos that never read back — the value looks saved and simply
 * has no effect, which is the most annoying kind of bug to chase.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum { T_INT, T_BOOL, T_ENUM } kind_t;

typedef struct {
	const char *key;
	kind_t kind;
	const char *def;
	long lo, hi;            /* T_INT */
	const char *choices;    /* T_ENUM, "|"-separated */
} setting_t;

/* The whole surface. Adding a remembered setting is a line here and a line in
 * the QML that reads it. */
static const setting_t settings[] = {
	{ "icon_size", T_INT,  "20",   16, 128, NULL },
	{ "previews",  T_BOOL, "1",     0, 1,   NULL },
	{ "tree",      T_BOOL, "0",     0, 1,   NULL },
	{ "hidden",    T_BOOL, "0",     0, 1,   NULL },
	{ "reverse",   T_BOOL, "0",     0, 1,   NULL },
	{ "sort",      T_ENUM, "name",  0, 0,   "name|size|mtime|type" },
	/* "auto" is the default because it is what the window did before there
	 * was a setting: the layout followed the icon size, list below 48px and
	 * a grid above it. Nobody's window changes shape on upgrade, and the
	 * first deliberate pick replaces the rule with an answer. */
	{ "view",      T_ENUM, "auto",  0, 0,   "auto|icons|compact|details" },
	/* A percentage, because the settings file holds integers. 100 is the
	 * size the window was drawn at before there was a slider. */
	{ "text_scale", T_INT, "100",  75, 175, NULL },
	/* Two panes side by side. Remembered because it is a way of working —
	 * somebody who files things for an hour wants both halves back on the
	 * next launch — and off by default because one pane is what a file
	 * manager is until you ask for two. The divider's position is NOT
	 * remembered: that is a gesture about what is on screen right now. */
	{ "split",     T_BOOL, "0",     0, 1,   NULL },
	/* The left panel — Recent, Trash, Places, and the folder tree when it is
	 * on. ON by default, because that is the window everybody already has;
	 * remembered because hiding it is a decision about the shape of the
	 * window, exactly like `split`, and a panel that comes back on every
	 * launch is a panel that was never really hidden.
	 *
	 * It hides the WHOLE strip. The tree is a section inside it, so `tree`
	 * stays a separate setting: turning the panel off and on again must not
	 * silently lose which sections were open in it. */
	{ "sidebar",   T_BOOL, "1",     0, 1,   NULL },
};

static const setting_t *find_setting(const char *key)
{
	for (size_t i = 0; i < sizeof settings / sizeof *settings; i++)
		if (!strcmp(settings[i].key, key))
			return &settings[i];
	return NULL;
}

static bool enum_ok(const setting_t *s, const char *v)
{
	size_t n = strlen(v);
	for (const char *p = s->choices; p && *p; ) {
		const char *bar = strchr(p, '|');
		size_t len = bar ? (size_t)(bar - p) : strlen(p);
		if (len == n && !strncmp(p, v, n))
			return true;
		p = bar ? bar + 1 : NULL;
	}
	return false;
}

/* Returns a malloc'd, VALID value, or NULL if the input cannot be made into
 * one. Numbers are clamped rather than refused: a slider that reports 200 on a
 * machine with a different scale should land at the maximum, not fail. */
static char *validate(const setting_t *s, const char *value)
{
	switch (s->kind) {
	case T_INT: {
		char *end = NULL;
		long n = strtol(value, &end, 10);
		if (end == value || (end && *end))
			return NULL;
		if (n < s->lo) n = s->lo;
		if (n > s->hi) n = s->hi;
		return xasprintf("%ld", n);
	}
	case T_BOOL:
		if (!strcmp(value, "1") || !strcmp(value, "true"))  return xstrdup("1");
		if (!strcmp(value, "0") || !strcmp(value, "false")) return xstrdup("0");
		return NULL;
	case T_ENUM:
		return enum_ok(s, value) ? xstrdup(value) : NULL;
	}
	return NULL;
}

static char *config_path(void)
{
	const char *env = getenv("SYNFILES_CONFIG");
	if (env && *env)
		return xstrdup(env);

	const char *xdg = getenv("XDG_CONFIG_HOME");
	char *base = (xdg && *xdg) ? xstrdup(xdg)
	                           : xasprintf("%s/.config", home_dir());
	char *dir = xasprintf("%s/synfiles", base);
	mkdir(base, 0700);
	mkdir(dir, 0700);
	char *p = xasprintf("%s/settings", dir);
	free(dir);
	free(base);
	return p;
}

/* The stored value, or NULL. Unknown and malformed lines are ignored rather
 * than dropped on the next write — a settings file edited by hand should not
 * lose the comment somebody put in it. */
static char *stored(const char *text, const char *key)
{
	if (!text)
		return NULL;

	char *copy = xstrdup(text);
	size_t n = 0;
	char **lines = split(copy, '\n', &n);
	char *found = NULL;

	for (size_t i = 0; i < n; i++) {
		if (!*lines[i] || lines[i][0] == '#')
			continue;
		char *tab = strchr(lines[i], '\t');
		if (!tab)
			continue;
		*tab = '\0';
		if (!strcmp(lines[i], key)) {
			free(found);
			found = pct_decode(tab + 1);   /* last one wins */
		}
	}

	free(lines);
	free(copy);
	return found;
}

/* Effective value: stored if valid, otherwise the declared default. A stored
 * value that no longer validates — an old enum member, a range that shrank —
 * falls back rather than propagating. */
static char *effective(const char *text, const setting_t *s)
{
	char *raw = stored(text, s->key);
	if (raw) {
		char *ok = validate(s, raw);
		free(raw);
		if (ok)
			return ok;
	}
	return xstrdup(s->def);
}

static int write_all(const char *path, const char *text, const char *key,
                     const char *value)
{
	/* Rewrite whole, preserving every OTHER key exactly as it was so two
	 * settings written in quick succession cannot lose one another. */
	char *out = xstrdup("# synfiles settings\n");

	for (size_t i = 0; i < sizeof settings / sizeof *settings; i++) {
		const setting_t *s = &settings[i];
		char *v;
		if (key && !strcmp(s->key, key))
			v = xstrdup(value);
		else {
			char *raw = stored(text, s->key);
			if (!raw)
				continue;              /* never set: leave it out */
			char *ok = validate(s, raw);
			free(raw);
			if (!ok)
				continue;
			v = ok;
		}
		char *enc = pct_encode(v, false);
		char *grown = xasprintf("%s%s\t%s\n", out, s->key, enc);
		free(out);
		out = grown;
		free(enc);
		free(v);
	}

	char *tmp = xasprintf("%s.XXXXXX", path);
	int fd = mkstemp(tmp);
	if (fd < 0) {
		warn("cannot write settings: %s", strerror(errno));
		free(tmp);
		free(out);
		return 1;
	}

	size_t len = strlen(out);
	bool wrote = write(fd, out, len) == (ssize_t)len && fsync(fd) == 0;
	/* The mode is set on the DESCRIPTOR, before the rename, so the file is
	 * never briefly readable under its final name and no chmod ever resolves
	 * a path a second time — mkstemp already gives 0600, this states it. */
	if (fchmod(fd, 0600) != 0)
		wrote = false;
	close(fd);

	int rc = 0;
	if (!wrote || rename(tmp, path) != 0) {
		warn("cannot save settings: %s", strerror(errno));
		unlink(tmp);
		rc = 1;
	}

	free(tmp);
	free(out);
	return rc;
}

int cmd_config(int argc, char **argv)
{
	char *path = config_path();
	char *text = slurp(path);

	const char *sub = argc > 0 ? argv[0] : "list";

	if (!strcmp(sub, "list")) {
		if (g_out == OUT_REC)
			rec_row(2, "key", "value");
		for (size_t i = 0; i < sizeof settings / sizeof *settings; i++) {
			char *v = effective(text, &settings[i]);
			if (g_out == OUT_REC)
				rec_row(2, settings[i].key, v);
			else
				printf("  %s%-12s%s %s\n", C_DIM(), settings[i].key,
				       C_RESET(), v);
			free(v);
		}
		free(text);
		free(path);
		return 0;
	}

	if (!strcmp(sub, "get")) {
		if (argc < 2)
			die("config get: need a key");
		const setting_t *s = find_setting(argv[1]);
		if (!s)
			die("config: unknown setting '%s'", argv[1]);
		char *v = effective(text, s);
		printf("%s\n", v);
		free(v);
		free(text);
		free(path);
		return 0;
	}

	if (!strcmp(sub, "set")) {
		if (argc < 3)
			die("config set: need a key and a value");
		const setting_t *s = find_setting(argv[1]);
		if (!s)
			die("config: unknown setting '%s'", argv[1]);

		char *v = validate(s, argv[2]);
		if (!v)
			die("config: '%s' is not a valid %s for %s", argv[2],
			    s->kind == T_BOOL ? "boolean"
			    : s->kind == T_INT ? "number" : "choice", s->key);

		int rc = write_all(path, text, s->key, v);
		if (rc == 0 && g_out == OUT_REC)
			rec_row(2, s->key, v);
		free(v);
		free(text);
		free(path);
		return rc;
	}

	if (!strcmp(sub, "reset")) {
		unlink(path);
		free(text);
		free(path);
		if (g_out == OUT_HUMAN)
			printf("settings reset to defaults\n");
		return 0;
	}

	free(text);
	free(path);
	die("config: unknown subcommand '%s' — try list, get <key>, set <key> <value>, reset",
	    sub);
}
