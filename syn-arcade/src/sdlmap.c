/*
 * sdlmap.c — SDL controller mapping overrides.
 *
 * ── What this fixes ─────────────────────────────────────────────────────────
 *
 * SDL ships a database that maps each known controller's raw buttons and axes
 * onto the abstract "gamepad" every game actually programs against (A/B/X/Y,
 * two sticks, two triggers). A pad that is not in it, or is in it wrongly,
 * comes out with the face buttons rotated, the triggers as buttons, or the
 * right stick reading as a hat — and no game has a setting for that, because
 * from the game's side nothing is wrong.
 *
 * The override is a database of your own that SDL reads first.
 *
 * ── The mechanism, checked against what is installed ────────────────────────
 *
 * SDL reads two environment variables:
 *
 *     SDL_GAMECONTROLLERCONFIG        one or more mapping strings, inline
 *     SDL_GAMECONTROLLERCONFIG_FILE   a path to a file of them
 *
 * Both names survived the SDL2 → SDL3 rename, which is not obvious given how
 * much else did not. Verified against this machine's SDL3 (3.4.14) rather than
 * assumed:
 *
 *     strings /usr/lib/libSDL3.so.0 | grep SDL_GAMECONTROLLERCONFIG
 *     → SDL_GAMECONTROLLERCONFIG
 *     → SDL_GAMECONTROLLERCONFIG_FILE
 *
 * So one variable, set once in the session, covers SDL2 games (through
 * sdl2-compat), SDL3 games, and every engine that embeds either.
 *
 * This package uses the FILE form. The inline one has to hold every mapping in
 * one variable, which means the session has to be restarted to add one; a file
 * is re-read by each game as it starts, so a mapping fixed now works in the
 * next game launched without touching the session.
 *
 * ── This file is the DATABASE. sdlwiz.c is where a mapping comes from ───────
 *
 * ⚠ THIS COMMENT USED TO ARGUE THAT MAPPINGS ARE PASTED AND NOT GENERATED —
 * that producing one means watching somebody press seventeen controls in a
 * known order, that this is a wizard, that antimicrox and the SDL project's
 * own tool both do it well, and that a worse copy of a solved thing is not
 * worth writing.
 *
 * Every clause of that was true and the conclusion was still wrong, because of
 * where it left the person holding the pad: the one tab in this application
 * that exists for a broken controller could list mappings and remove them, and
 * to ADD one it named a program this desktop does not ship. See sdlwiz.c,
 * which is the wizard, and which asks SDL for every number in the line rather
 * than reimplementing SDL's internals.
 *
 * What lives HERE is everything around that: the file SDL is told to read, a
 * way to see what is in it, the refusals that catch a mapping SDL would
 * silently ignore, and a way to take one back out when it turns out to be
 * wrong. The wizard ends by calling map_add_line() below, so a learned mapping
 * and a pasted one go through exactly the same door.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "arcade.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ── the override database ───────────────────────────────────────────────── */

static bool map_path(char *buf, size_t n)
{
	const char *env = getenv("SDL_GAMECONTROLLERCONFIG_FILE");
	if (env && *env)
		return snprintf(buf, n, "%s", env) < (int)n;
	return config_path(buf, n, "syn-arcade/gamecontrollerdb.txt");
}

/* The path this package's session profile points SDL at, whatever the
 * environment currently says. `map path` needs both to explain a mismatch. */
static bool map_own_path(char *buf, size_t n)
{
	return config_path(buf, n, "syn-arcade/gamecontrollerdb.txt");
}

/*
 * A mapping line, split far enough to talk about it.
 *
 * The format is comma separated: GUID,Name,binding:target,binding:target,…
 * Only the first two fields have fixed meanings, and everything after them is
 * a bag of bindings whose order does not matter — so this parses exactly the
 * two fields it needs and leaves the rest as one opaque string. A parser that
 * understood every binding would be a parser that rejects a mapping SDL would
 * have accepted, the next time SDL adds a control.
 */
typedef struct {
	char guid[64];
	char name[256];
	const char *rest;
} mapping_t;

static bool mapping_parse(const char *line, mapping_t *m)
{
	if (!line || !*line || *line == '#')
		return false;

	const char *c1 = strchr(line, ',');
	if (!c1) return false;
	const char *c2 = strchr(c1 + 1, ',');
	if (!c2) return false;

	size_t glen = (size_t)(c1 - line);
	size_t nlen = (size_t)(c2 - c1 - 1);
	if (glen == 0 || glen >= sizeof(m->guid)) return false;
	if (nlen == 0 || nlen >= sizeof(m->name)) return false;

	memcpy(m->guid, line, glen);
	m->guid[glen] = '\0';
	memcpy(m->name, c1 + 1, nlen);
	m->name[nlen] = '\0';
	m->rest = c2 + 1;
	return true;
}

/*
 * Reject what SDL would silently ignore.
 *
 * SDL does not report a bad mapping — it declines to use it and the controller
 * behaves exactly as it did before, which is indistinguishable from the file
 * not being read at all. Everything checked here is a shape that fails that
 * way, and the message names the specific fault rather than "invalid".
 */
static bool mapping_ok(const char *line)
{
	mapping_t m;

	if (!mapping_parse(line, &m)) {
		fputs("syn-arcade: that is not a mapping line. The format is\n"
		      "  <32-hex-GUID>,<Name>,a:b0,b:b1,…,platform:Linux\n"
		      "\n"
		      "If you do not have one, do not go and find one:\n"
		      "      syn-arcade map learn\n"
		      "presses through the controls with you and writes it.\n",
		      stderr);
		return false;
	}

	/* ⚠ `xinput` is a real GUID, not a typo: SDL takes it as a wildcard
	 * matching any XInput-class pad rather than one specific device. It
	 * turns up in the platform:Linux section of the community
	 * gamecontrollerdb, so a plain "32 hex characters" check rejects a
	 * mapping SDL would have used. */
	size_t glen = strlen(m.guid);
	if (strcmp(m.guid, "xinput") != 0) {
		if (glen != 32) {
			fprintf(stderr, "syn-arcade: the GUID is %zu characters, "
					"not 32 — '%s'\n", glen, m.guid);
			return false;
		}
		for (const char *p = m.guid; *p; p++) {
			if (isxdigit((unsigned char)*p)) continue;
			fprintf(stderr, "syn-arcade: '%c' is not a hex digit — a "
					"GUID is 32 hex characters\n", *p);
			return false;
		}
	}

	/*
	 * ⚠ The one that actually bites. SDL only uses a mapping whose platform
	 * matches the one it is running on, and every mapping copied off a forum
	 * or out of a Windows tool says platform:Windows. It parses, it loads, it
	 * is never applied, and nothing anywhere says why.
	 */
	if (!strstr(line, "platform:Linux")) {
		const char *other = strstr(line, "platform:");
		if (other) {
			fprintf(stderr,
			 "syn-arcade: this mapping is for another platform (%.20s…)\n"
			 "SDL loads it and never applies it. Change it to "
			 "platform:Linux.\n", other);
		} else {
			fputs("syn-arcade: no `platform:` field. Add "
			      "`platform:Linux,` to the end.\n", stderr);
		}
		return false;
	}

	if (strchr(line, '\n')) {
		fputs("syn-arcade: a mapping is one line\n", stderr);
		return false;
	}
	return true;
}

/* ── commands ────────────────────────────────────────────────────────────── */

static int map_list(bool rec)
{
	char path[4096];
	if (!map_path(path, sizeof(path)))
		return EX_FAIL;

	char *text = read_file(path);
	if (!text) {
		if (rec)
			rec_row(3, "guid", "name", "bindings");
		else
			printf("no mappings yet (%s)\n", path);
		return EX_EMPTY;
	}

	if (rec)
		rec_row(3, "guid", "name", "bindings");

	int n = 0;
	char *save = NULL;
	for (char *ln = strtok_r(text, "\n", &save); ln;
	     ln = strtok_r(NULL, "\n", &save)) {
		mapping_t m;
		if (!mapping_parse(trim(ln), &m))
			continue;

		/* How many controls it actually binds — the one number that says
		 * whether a mapping is complete at a glance. */
		int binds = 1;
		for (const char *p = m.rest; *p; p++)
			if (*p == ',') binds++;

		char nbuf[32];
		snprintf(nbuf, sizeof(nbuf), "%d", binds);

		if (rec) {
			rec_row(3, m.guid, m.name, nbuf);
		} else {
			printf("%s\n", m.name);
			printf("  %s   (%d bindings)\n", m.guid, binds);
		}
		n++;
	}
	free(text);

	if (n == 0) {
		if (!rec) printf("no mappings in %s\n", path);
		return EX_EMPTY;
	}
	return EX_OK;
}

static int map_add(const char *line)
{
	if (!mapping_ok(line))
		return EX_USAGE;

	mapping_t m;
	mapping_parse(line, &m);

	char path[4096];
	if (!map_own_path(path, sizeof(path)))
		return EX_FAIL;

	char *old = read_file(path);

	/* Replace any existing mapping for the same GUID rather than appending
	 * a second. SDL takes the LAST one it reads, so two entries would work
	 * — but the file would then disagree with itself, and `map remove`
	 * would take one away and appear to do nothing. */
	size_t cap = (old ? strlen(old) : 0) + strlen(line) + 256;
	char *neu = xmalloc(cap);
	neu[0] = '\0';
	bool replaced = false;

	if (old) {
		char *save = NULL;
		for (char *ln = strtok_r(old, "\n", &save); ln;
		     ln = strtok_r(NULL, "\n", &save)) {
			char *t = trim(ln);
			mapping_t e;
			if (mapping_parse(t, &e) &&
			    strcasecmp(e.guid, m.guid) == 0) {
				replaced = true;
				continue;
			}
			strcat(neu, t);
			strcat(neu, "\n");
		}
		free(old);
	} else {
		strcat(neu,
"# SDL controller mappings, read by every SDL2 and SDL3 game through\n"
"# SDL_GAMECONTROLLERCONFIG_FILE. Managed by `syn-arcade map`.\n"
"#\n"
"# One mapping per line. They must say platform:Linux or SDL ignores them.\n");
	}

	strcat(neu, line);
	strcat(neu, "\n");

	int rc = write_file_inplace(path, neu);
	free(neu);

	if (rc < 0) {
		fprintf(stderr, "syn-arcade: cannot write %s: %s\n",
			path, strerror(-rc));
		return EX_FAIL;
	}

	printf("%s %s\n", replaced ? "replaced" : "added", m.name);
	printf("  %s\n", m.guid);

	const char *env = getenv("SDL_GAMECONTROLLERCONFIG_FILE");
	if (!env || !*env)
		printf("\n⚠ SDL_GAMECONTROLLERCONFIG_FILE is not set in this shell, so\n"
		       "  nothing will read this yet. Log out and back in, or:\n"
		       "      export SDL_GAMECONTROLLERCONFIG_FILE=%s\n", path);
	else
		puts("\nGames started from now on will use it.");
	return EX_OK;
}

/* Exposed for the wizard in sdlwiz.c: it assembles a line and this writes it,
 * so the refusals, the replace-don't-append rule and the file header have one
 * implementation rather than two that drift. */
int map_add_line(const char *line)
{
	return map_add(line);
}

static int map_remove(const char *what)
{
	char path[4096];
	if (!map_own_path(path, sizeof(path)))
		return EX_FAIL;

	char *old = read_file(path);
	if (!old) {
		printf("nothing to remove (%s does not exist)\n", path);
		return EX_OK;
	}

	char *neu = xmalloc(strlen(old) + 1);
	neu[0] = '\0';
	int removed = 0;

	char *save = NULL;
	for (char *ln = strtok_r(old, "\n", &save); ln;
	     ln = strtok_r(NULL, "\n", &save)) {
		char *t = trim(ln);
		mapping_t e;

		/* By GUID or by any fragment of the name, same as `pads`. */
		if (mapping_parse(t, &e) &&
		    (strcasecmp(e.guid, what) == 0 || strcasestr(e.name, what))) {
			removed++;
			continue;
		}
		strcat(neu, t);
		strcat(neu, "\n");
	}
	free(old);

	if (removed == 0) {
		free(neu);
		fprintf(stderr, "syn-arcade: no mapping matches '%s'\n", what);
		return EX_FAIL;
	}

	int rc = write_file_inplace(path, neu);
	free(neu);

	if (rc < 0) {
		fprintf(stderr, "syn-arcade: cannot write %s: %s\n",
			path, strerror(-rc));
		return EX_FAIL;
	}
	printf("removed %d mapping%s\n", removed, removed == 1 ? "" : "s");
	return EX_OK;
}

/*
 * Where the file is, and whether anything will read it.
 *
 * The second half is the useful one. A mapping database that no game is told
 * about is the failure mode here, it produces no error anywhere, and it is
 * entirely invisible from `map list` — which happily prints a file SDL has
 * never heard of.
 */
static int map_status(void)
{
	char own[4096], eff[4096];
	map_own_path(own, sizeof(own));
	map_path(eff, sizeof(eff));

	const char *env = getenv("SDL_GAMECONTROLLERCONFIG_FILE");
	const char *inline_env = getenv("SDL_GAMECONTROLLERCONFIG");

	printf("database    %s%s\n", own,
	       file_exists(own) ? "" : "   (does not exist yet)");
	printf("SDL reads   %s\n", (env && *env) ? env : "(nothing — variable unset)");

	if (!env || !*env) {
		printf("\n⚠ SDL_GAMECONTROLLERCONFIG_FILE is unset, so no game will read\n"
		       "  any of this. It is set by this package's session profile at\n"
		       "  /etc/profile.d/syn-arcade.sh — log out and back in, or set it\n"
		       "  by hand for one shell.\n");
	} else if (strcmp(env, own) != 0) {
		printf("\n⚠ SDL is pointed at a DIFFERENT file from the one `syn-arcade\n"
		       "  map add` writes. Mappings added here will not be read.\n");
	}

	if (inline_env && *inline_env)
		printf("\nNote: SDL_GAMECONTROLLERCONFIG is also set, with %zu bytes of\n"
		       "inline mappings. Those are read as well.\n", strlen(inline_env));
	return EX_OK;
}

/* ── dispatch ────────────────────────────────────────────────────────────── */

int cmd_map(int argc, char **argv)
{
	if (argc < 1)
		return map_list(false);

	const char *sub = argv[0];

	if (strcmp(sub, "--rec") == 0)	return map_list(true);
	if (strcmp(sub, "path") == 0)	return map_status();

	if (strcmp(sub, "list") == 0) {
		bool rec = argc > 1 && strcmp(argv[1], "--rec") == 0;
		return map_list(rec);
	}

	if (strcmp(sub, "add") == 0) {
		if (argc < 2) {
			fputs("syn-arcade: map add '<mapping string>'\n"
			      "\n"
			      "Quote it — a mapping is full of semicolons and commas\n"
			      "that a shell would otherwise eat.\n", stderr);
			return EX_USAGE;
		}
		return map_add(argv[1]);
	}

	/* ⚠ The wizard is the FIRST thing offered, not a footnote under `add`.
	 * Somebody reaching for `map` has a pad that misbehaves; they do not
	 * have a mapping string, and telling them to go and produce one
	 * elsewhere is what this command used to do. */
	if (strcmp(sub, "learn") == 0)
		return map_learn(argc - 1, argv + 1);

	if (strcmp(sub, "remove") == 0) {
		if (argc < 2) {
			fputs("syn-arcade: map remove <guid or name>\n", stderr);
			return EX_USAGE;
		}
		return map_remove(argv[1]);
	}

	fprintf(stderr, "syn-arcade: unknown map command '%s'\n", sub);
	return EX_USAGE;
}
