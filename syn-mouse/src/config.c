/* config.c — the bindings file.
 *
 *     notify = on
 *
 *     [Diablo IV]
 *     app     = steam_app_2344520
 *     back    = toggle 1 every 5
 *     forward = repeat 2 every 0.5
 *     middle  = key shift
 *
 * One section per profile. `app` and `title` are globs on the focused window;
 * a profile with neither applies everywhere. Every other line in a section is
 * a button and what it does.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synmouse.h"
#include "i18n.h"

#include <ctype.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

void config_init(config_t *c)
{
	memset(c, 0, sizeof *c);
	c->notify = true;
}

void config_free(config_t *c)
{
	free(c->p);
	config_init(c);
}

profile_t *config_find(config_t *c, const char *name)
{
	for (int i = 0; i < c->n; i++)
		if (!strcmp(c->p[i].name, name)) return &c->p[i];
	return NULL;
}

profile_t *config_add(config_t *c, const char *name)
{
	profile_t *have = config_find(c, name);
	if (have) return have;
	profile_t *np = realloc(c->p, sizeof *np * (size_t)(c->n + 1));
	if (!np) die("out of memory");
	c->p = np;
	profile_t *p = &c->p[c->n++];
	memset(p, 0, sizeof *p);
	snprintf(p->name, sizeof p->name, "%s", name);
	return p;
}

void config_remove(config_t *c, const char *name)
{
	for (int i = 0; i < c->n; i++) {
		if (strcmp(c->p[i].name, name)) continue;
		memmove(&c->p[i], &c->p[i + 1], sizeof *c->p * (size_t)(c->n - i - 1));
		c->n--;
		return;
	}
}

int profile_nbound(const profile_t *p)
{
	int n = 0;
	for (int i = 0; i < IN_COUNT; i++) n += p->act[i].kind != ACT_NONE;
	return n;
}

bool config_has_app_profiles(const config_t *c)
{
	for (int i = 0; i < c->n; i++)
		if (!profile_everywhere(&c->p[i]) && profile_nbound(&c->p[i])) return true;
	return false;
}

/* ⚠ CASE-FOLDED. An app_id is whatever the toolkit chose — `steam_app_2344520`,
 * `org.qbittorrent.qBittorrent`, `Vivaldi-stable` — and a glob that missed on
 * case would be a profile that silently never comes on. */
static bool glob_hit(const char *pat, const char *s)
{
	if (!pat[0]) return true;
	if (!s) return false;
	return fnmatch(pat, s, FNM_CASEFOLD) == 0;
}

int config_match(const config_t *c, const char *app, const char *title)
{
	if (app) {
		for (int i = 0; i < c->n; i++) {
			const profile_t *p = &c->p[i];
			if (profile_everywhere(p)) continue;
			if (glob_hit(p->app, app) && glob_hit(p->title, title)) return i;
		}
	}
	for (int i = 0; i < c->n; i++)
		if (profile_everywhere(&c->p[i])) return i;
	return -1;
}

/* ── seconds ─────────────────────────────────────────────────────────────── */

void seconds_format(unsigned ms, char *buf, size_t n)
{
	if (ms % 1000 == 0) { snprintf(buf, n, "%u", ms / 1000); return; }
	snprintf(buf, n, "%u.%03u", ms / 1000, ms % 1000);
	/* 0.500 -> 0.5; the file and the window both read what a person wrote. */
	char *e = buf + strlen(buf) - 1;
	while (*e == '0') *e-- = '\0';
}

/* ⚠ PARSED BY HAND, NOT strtod(). strtod reads the decimal point from
 * LC_NUMERIC, and although main() pins that to C, this is also what reads the
 * bindings FILE — a value written on one machine has to mean the same seconds
 * on every other, whatever any of them has set. */
int seconds_parse(const char *s, unsigned *ms)
{
	unsigned long whole = 0, frac = 0, scale = 1000;
	const char *p = s;
	bool any = false;

	while (isdigit((unsigned char)*p)) {
		whole = whole * 10 + (unsigned long)(*p++ - '0');
		any = true;
		if (whole > 100000) return -1;
	}
	if (*p == '.' || *p == ',') {
		p++;
		while (isdigit((unsigned char)*p)) {
			if (scale > 1) { scale /= 10; frac += (unsigned long)(*p - '0') * scale; }
			p++;
			any = true;
		}
	}
	if (!any) return -1;

	unsigned long total = whole * 1000 + frac;
	if (!strcasecmp(p, "ms")) {
		if (frac) return -1;
		total = whole;
	} else if (*p && strcasecmp(p, "s")) {
		return -1;
	}
	*ms = (unsigned)total;
	return 0;
}

/* ── one binding ─────────────────────────────────────────────────────────── */

void action_format(const action_t *a, char *buf, size_t n)
{
	char keys[128], secs[24];
	combo_format(&a->combo, keys, sizeof keys);
	seconds_format(a->interval_ms, secs, sizeof secs);
	switch (a->kind) {
	case ACT_KEY:    snprintf(buf, n, "key %s", keys); break;
	case ACT_LATCH:  snprintf(buf, n, "latch %s", keys); break;
	case ACT_REPEAT: snprintf(buf, n, "repeat %s every %s", keys, secs); break;
	case ACT_TOGGLE: snprintf(buf, n, "toggle %s every %s", keys, secs); break;
	case ACT_OFF:    snprintf(buf, n, "off"); break;
	default:         snprintf(buf, n, "default"); break;
	}
}

const char *action_parse(int argc, char **argv, action_t *a)
{
	memset(a, 0, sizeof *a);
	if (argc < 1) return _("say what the button should do: key, repeat, toggle, latch, off or default");

	const char *mode = argv[0];
	if (!strcasecmp(mode, "default") || !strcasecmp(mode, "none")) {
		if (argc > 1) return _("default takes nothing after it");
		a->kind = ACT_NONE;
		return NULL;
	}
	act_kind_t k = act_from_name(mode);
	if (k == ACT_NONE) return _("unknown mode — use key, repeat, toggle, latch, off or default");
	a->kind = k;

	if (k == ACT_OFF) {
		if (argc > 1) return _("off takes nothing after it");
		return NULL;
	}
	if (argc < 2) return _("which key? for example: shift, 1, f5, space, mouse1");

	const char *bad; size_t badlen;
	if (combo_parse(argv[1], &a->combo, &bad, &badlen) != 0) {
		static char why[160];
		snprintf(why, sizeof why, _("'%.*s' is not a key name — `syn-mouse keys` lists them"),
		         (int)badlen, bad);
		return why;
	}

	int i = 2;
	a->interval_ms = INTERVAL_DEF_MS;
	if (act_has_interval(k) && i < argc && !strcasecmp(argv[i], "every")) {
		if (i + 1 >= argc) return _("every how many seconds?");
		if (seconds_parse(argv[i + 1], &a->interval_ms) != 0)
			return _("not a number of seconds — for example 5, 0.5 or 250ms");
		i += 2;
	}
	if (i < argc) return _("unexpected words after the binding");
	if (!act_has_interval(k)) a->interval_ms = 0;
	return NULL;
}

/* What is wrong with the binding itself, whatever profile it is in. */
static const char *shape_refusal(const action_t *a)
{
	if (a->kind == ACT_NONE) return NULL;
	if (act_has_keys(a->kind) && a->combo.n == 0)
		return _("which key? for example: shift, 1, f5, space, mouse1");
	if (act_has_interval(a->kind) &&
	    (a->interval_ms < INTERVAL_MIN_MS || a->interval_ms > INTERVAL_MAX_MS))
		return _("the interval has to be between 0.05 and 3600 seconds");
	return NULL;
}

const char *binding_refusal(const profile_t *p, int in, const action_t *a)
{
	if (a->kind == ACT_NONE) return NULL;
	/* ⛔ THE LEFT BUTTON IS HOW YOU WOULD UNDO IT. A profile that applies
	 * everywhere is in force over this program's own window, so rebinding the
	 * left button there takes away the click that would put it back. Inside
	 * an app's profile it is safe: leave the app and the button is yours. */
	if (in == IN_LEFT && profile_everywhere(p))
		return _("the left button can only be rebound in a profile for an app — "
		         "in one that applies everywhere you could no longer click to undo it");
	return shape_refusal(a);
}

/* ── the file ────────────────────────────────────────────────────────────── */

static char *trim(char *s)
{
	while (isspace((unsigned char)*s)) s++;
	char *e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
	return s;
}

static int split_words(char *s, char **argv, int max)
{
	int n = 0;
	for (char *t = strtok(s, " \t"); t && n < max; t = strtok(NULL, " \t"))
		argv[n++] = t;
	return n;
}

int config_load(config_t *c, const char *path)
{
	config_init(c);
	FILE *f = fopen(path, "re");
	if (!f) return errno == ENOENT ? 0 : -1;

	char line[512];
	int lineno = 0;
	profile_t *cur = NULL;

	while (fgets(line, sizeof line, f)) {
		lineno++;
		char *s = trim(line);
		if (!*s || *s == '#') continue;

		if (*s == '[') {
			char *e = strrchr(s, ']');
			if (!e || e == s + 1) {
				warn(_("%s:%d: a section needs a name in [brackets]"), path, lineno);
				cur = NULL;
				continue;
			}
			*e = '\0';
			cur = config_add(c, trim(s + 1));
			continue;
		}

		char *eq = strchr(s, '=');
		if (!eq) {
			warn(_("%s:%d: expected name = value"), path, lineno);
			continue;
		}
		*eq = '\0';
		char *key = trim(s), *val = trim(eq + 1);

		if (!cur) {
			if (!strcasecmp(key, "notify"))
				c->notify = !(!strcasecmp(val, "off") || !strcasecmp(val, "no") ||
				              !strcmp(val, "0") || !strcasecmp(val, "false"));
			else if (!strcasecmp(key, "device"))
				snprintf(c->device, sizeof c->device, "%s", val);
			else
				warn(_("%s:%d: unknown setting '%s'"), path, lineno, key);
			continue;
		}

		if (!strcasecmp(key, "app"))   { snprintf(cur->app, sizeof cur->app, "%s", val); continue; }
		if (!strcasecmp(key, "title")) { snprintf(cur->title, sizeof cur->title, "%s", val); continue; }

		int in = input_from_name(key);
		if (in < 0) {
			warn(_("%s:%d: '%s' is not a button name"), path, lineno, key);
			continue;
		}
		char *argv[16];
		int argc = split_words(val, argv, 16);
		action_t a;
		const char *why = action_parse(argc, argv, &a);
		if (!why) why = shape_refusal(&a);
		if (why) {
			warn("%s:%d: %s", path, lineno, why);
			continue;
		}
		cur->act[in] = a;
	}
	fclose(f);

	/* ⚠ THE LEFT-BUTTON RULE NEEDS THE WHOLE SECTION. Whether a profile is
	 * for an app is only known once its `app =` line has been read, and a
	 * hand-edited file may put that below `left = …` — so the rule is applied
	 * here, with every section complete, and not line by line. */
	for (int i = 0; i < c->n; i++) {
		profile_t *p = &c->p[i];
		if (p->act[IN_LEFT].kind != ACT_NONE && profile_everywhere(p)) {
			warn("%s: [%s]: %s", path, p->name,
			     binding_refusal(p, IN_LEFT, &p->act[IN_LEFT]));
			memset(&p->act[IN_LEFT], 0, sizeof p->act[IN_LEFT]);
		}
	}
	return 0;
}

static int mkdir_p(const char *dir)
{
	char buf[4096];
	snprintf(buf, sizeof buf, "%s", dir);
	for (char *p = buf + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
		*p = '/';
	}
	return mkdir(buf, 0755) != 0 && errno != EEXIST ? -1 : 0;
}

int config_save(const config_t *c, const char *path)
{
	char dir[4096];
	snprintf(dir, sizeof dir, "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash) { *slash = '\0'; if (mkdir_p(dir) != 0) return -1; }

	/* ⚠ WRITTEN BESIDE AND RENAMED OVER. The daemon watches this directory
	 * and reloads on the rename; a file rewritten in place could be read half
	 * written, and half a profile is a binding that vanished mid-game. */
	char *tmp = xasprintf("%s.tmp.%d", path, (int)getpid());
	FILE *f = fopen(tmp, "we");
	if (!f) { free(tmp); return -1; }

	fprintf(f,
	        "# syn-mouse — what each mouse button does, per game.\n"
	        "#\n"
	        "# Written by `syn-mouse` and its window. Editing by hand is fine, but\n"
	        "# comments are not kept the next time either of them saves.\n"
	        "\n"
	        "notify = %s\n", c->notify ? "on" : "off");
	if (c->device[0]) fprintf(f, "device = %s\n", c->device);

	for (int i = 0; i < c->n; i++) {
		const profile_t *p = &c->p[i];
		fprintf(f, "\n[%s]\n", p->name);
		if (p->app[0])   fprintf(f, "app = %s\n", p->app);
		if (p->title[0]) fprintf(f, "title = %s\n", p->title);
		for (int in = 0; in < IN_COUNT; in++) {
			if (p->act[in].kind == ACT_NONE) continue;
			char buf[200];
			action_format(&p->act[in], buf, sizeof buf);
			fprintf(f, "%s = %s\n", input_name(in), buf);
		}
	}

	bool ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
	ok &= fclose(f) == 0;
	if (!ok || rename(tmp, path) != 0) {
		unlink(tmp);
		free(tmp);
		return -1;
	}
	free(tmp);
	return 0;
}
