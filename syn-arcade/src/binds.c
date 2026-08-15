/*
 * binds.c — the two compositor shortcuts that drive the overlay.
 *
 * ── Why a `bind =` line and not binds.state ─────────────────────────────────
 *
 * synui has two places a shortcut can live, and they are not interchangeable:
 *
 *   synuirc `bind =`   the baseline. Read at every config load, and synui
 *                      never rewrites this file.
 *   binds.state        the user's own rebinds, written by Super+/ (the
 *                      Shortcuts palette) and re-applied over the baseline at
 *                      the END of every config load.
 *
 * These go in synuirc, for one reason: binds.state is the palette's file, and
 * Ctrl+Shift+R in that palette resets it wholesale. A shortcut this package
 * installed there would vanish the first time somebody reset their keybinds,
 * with nothing to say why. In synuirc it is a default like any other, and the
 * palette can still rebind it on top — which is exactly the layering the two
 * files exist to provide.
 *
 * ── The trap: synui reads exactly ONE synuirc ───────────────────────────────
 *
 * config.c walks three candidates and returns on the first that OPENS:
 *
 *     $SYNUI_CONFIG  →  ~/.config/synui/synuirc  →  /etc/synui/synuirc
 *
 * There is no merging. So on a machine whose config lives in /etc, creating a
 * user synuirc containing nothing but two bind lines does not ADD two
 * shortcuts — it silently replaces the entire system configuration with a
 * two-line file, and the desktop comes back on stock defaults with the user's
 * terminal, autostarts, gaps and theme all gone.
 *
 * So `binds install` SEEDS: if the effective file is not already the user's, it
 * copies it there first and appends to the copy. Same shape as `hud adopt`, and
 * the same reason — a one-file-wins precedence chain is a thing you take
 * ownership of deliberately, never one you edit by halves.
 *
 * ── Why nothing is reloaded automatically ───────────────────────────────────
 *
 * A written bind does nothing until synui reloads its config, and this package
 * does NOT do that for you unless asked with --reload.
 *
 * synui has exactly three reload triggers — SIGHUP, Ctrl+Shift+R, and
 * `wallpaper_reload` (Super+Shift+W) — and all three run the same
 * synui_config_reload(), which replaces the config wholesale and repaints the
 * wallpaper. That is a bigger event than installing a keybind asked for, and it
 * has twice been the thing that reset state the config did not carry (the theme
 * in pkgrel 294, the CRT filters in 335; both fixed, both found the same way).
 * Printing the one command to run is honest about the size of what it does.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "arcade.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Sentinels around the block this package owns. Matched at the start of a line
 * and never translated — they are markers, not prose. */
#define BLOCK_BEGIN "# >>> syn-arcade"
#define BLOCK_END   "# <<< syn-arcade"

#define DEFAULT_TOGGLE "super+F11"
#define DEFAULT_CYCLE  "super+F12"

/* ── which synuirc ───────────────────────────────────────────────────────── */

static bool rc_user_path(char *buf, size_t n)
{
	return config_path(buf, n, "synui/synuirc");
}

/* synui's own precedence, reimplemented. */
static bool rc_effective_path(char *buf, size_t n)
{
	const char *env = getenv("SYNUI_CONFIG");
	if (env && *env)
		return snprintf(buf, n, "%s", env) < (int)n;

	char user[4096];
	if (rc_user_path(user, sizeof(user)) && file_exists(user))
		return snprintf(buf, n, "%s", user) < (int)n;

	if (file_exists("/etc/synui/synuirc"))
		return snprintf(buf, n, "/etc/synui/synuirc") < (int)n;

	/* Nothing exists yet: the user's path is where one should be made. */
	return rc_user_path(buf, n);
}

/* ── the managed block ───────────────────────────────────────────────────── */

/*
 * Everything in `text` with our block cut out of it.
 *
 * An unterminated BLOCK_BEGIN — somebody deleted the end marker by hand — is
 * treated as running to end of file rather than left in place. The alternative
 * is appending a second block after it and having synui apply whichever came
 * last, which is a shortcut that cannot be removed by the command whose job
 * that is.
 */
static char *strip_block(const char *text)
{
	char *out = xmalloc(strlen(text) + 1);
	out[0] = '\0';

	const char *p = text;
	char *w = out;
	bool inside = false;

	while (*p) {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p + 1) : strlen(p);

		if (!inside && strncmp(p, BLOCK_BEGIN, strlen(BLOCK_BEGIN)) == 0) {
			inside = true;
		} else if (inside && strncmp(p, BLOCK_END, strlen(BLOCK_END)) == 0) {
			inside = false;
			p += len;
			continue;
		}

		if (!inside) {
			memcpy(w, p, len);
			w += len;
		}
		p += len;
		if (!nl) break;
	}
	*w = '\0';
	return out;
}

/* Is there a block in there at all? */
static bool has_block(const char *text)
{
	const char *p = text;
	while (p) {
		if (strncmp(p, BLOCK_BEGIN, strlen(BLOCK_BEGIN)) == 0)
			return true;
		p = strchr(p, '\n');
		if (p) p++;
	}
	return false;
}

/*
 * Validate a combo the way synui will read it.
 *
 * Only the shapes that would fail SILENTLY are rejected. synui parses a bind it
 * cannot understand by ignoring the line, so a typo is a key that does nothing
 * with no message anywhere — which is indistinguishable from the feature being
 * broken, and is the failure this check exists to convert into a sentence.
 *
 * ⚠ The one non-obvious rule, straight out of synuirc's own comments: the key
 * must be an XKB keysym NAME, so `=` has to be spelled `equal`. `super+=` parses
 * as a bind whose key is the empty string and is dropped on the floor.
 */
static bool combo_ok(const char *combo)
{
	if (!combo || !*combo) {
		fputs("syn-arcade: empty key combination\n", stderr);
		return false;
	}
	if (strchr(combo, ' ') || strchr(combo, '\t')) {
		fprintf(stderr, "syn-arcade: '%s' — a combo has no spaces in it "
				"(super+F11, not super + F11)\n", combo);
		return false;
	}
	if (!strchr(combo, '+')) {
		fprintf(stderr, "syn-arcade: '%s' has no modifier. Use "
				"super+, ctrl+, alt+ or shift+\n", combo);
		return false;
	}

	const char *key = strrchr(combo, '+') + 1;
	if (!*key) {
		fprintf(stderr, "syn-arcade: '%s' ends in '+' with no key\n", combo);
		return false;
	}
	if (strcmp(key, "=") == 0) {
		fputs("syn-arcade: spell '=' as 'equal' — synui wants the XKB\n"
		      "keysym name, and 'super+=' is silently ignored\n", stderr);
		return false;
	}
	return true;
}

static char *make_block(const char *toggle, const char *cycle)
{
	size_t cap = 2048;
	char *out = xmalloc(cap);
	snprintf(out, cap,
BLOCK_BEGIN "  — the gaming shortcuts. Do not edit between the markers;\n"
"#   `syn-arcade binds install` rewrites this block, and\n"
"#   `syn-arcade binds remove` deletes it. Everything outside it is yours.\n"
"#\n"
"# These rewrite MangoHud's config file, which MangoHud watches with inotify —\n"
"# so they change the overlay inside a game that is already running.\n"
"bind = %s spawn syn-arcade hud toggle\n"
"bind = %s spawn syn-arcade hud cycle\n"
BLOCK_END "\n", toggle, cycle);
	return out;
}

/* ── commands ────────────────────────────────────────────────────────────── */

static int binds_show(bool rec)
{
	char path[4096];
	if (!rc_effective_path(path, sizeof(path)))
		return EX_FAIL;

	char *text = read_file(path);
	bool installed = text && has_block(text);

	/* Read the combos back out of the file rather than reprinting the
	 * defaults — the whole point of --toggle is that they may not match,
	 * and a status line that lies about which key is bound is worse than
	 * none. */
	char toggle[128] = "", cycle[128] = "";
	if (installed) {
		char *save = NULL;
		for (char *ln = strtok_r(text, "\n", &save); ln;
		     ln = strtok_r(NULL, "\n", &save)) {
			char combo[128], verb[64];
			if (sscanf(ln, "bind = %127s spawn syn-arcade hud %63s",
				   combo, verb) != 2)
				continue;
			if (strcmp(verb, "toggle") == 0)
				snprintf(toggle, sizeof(toggle), "%s", combo);
			else if (strcmp(verb, "cycle") == 0)
				snprintf(cycle, sizeof(cycle), "%s", combo);
		}
	}

	if (rec) {
		rec_row(3, "field", "value", "action");
		rec_row(3, "installed", installed ? "yes" : "no",
			installed ? "action:remove" : "action:install");
		rec_row(3, "toggle overlay", toggle[0] ? toggle : "(not bound)",
			"set:toggle");
		rec_row(3, "move overlay", cycle[0] ? cycle : "(not bound)",
			"set:cycle");
		rec_row(3, "config", path, "detail");
	} else if (installed) {
		printf("installed in %s\n", path);
		printf("  %-14s toggle the overlay\n",
		       toggle[0] ? toggle : "(unbound)");
		printf("  %-14s move it around the screen\n",
		       cycle[0] ? cycle : "(unbound)");
	} else {
		printf("not installed (%s)\n", path);
		puts("`syn-arcade binds install` adds them.");
	}

	free(text);
	return EX_OK;
}

static int binds_install(const char *toggle, const char *cycle, bool reload)
{
	if (!combo_ok(toggle) || !combo_ok(cycle))
		return EX_USAGE;

	if (strcasecmp(toggle, cycle) == 0) {
		fprintf(stderr, "syn-arcade: both shortcuts are '%s' — synui "
				"would apply whichever it read last\n", toggle);
		return EX_USAGE;
	}

	char eff[4096], user[4096];
	if (!rc_effective_path(eff, sizeof(eff)) ||
	    !rc_user_path(user, sizeof(user))) {
		fputs("syn-arcade: cannot resolve a synui config path "
		      "(is HOME set?)\n", stderr);
		return EX_FAIL;
	}

	/* ⚠ The seeding step. See the header comment: appending to /etc needs
	 * root and changes it for everyone, and writing a fresh user file with
	 * only our block in it DISCARDS the whole system config, because synui
	 * reads exactly one of them. */
	char *text = read_file(eff);
	bool seeded = false;

	if (strcmp(eff, user) != 0) {
		if (!text) {
			text = xstrdup(
"# synui configuration.\n"
"#\n"
"# Created by syn-arcade because there was no config file to add a keybind to.\n"
"# Everything synui understands can go in here; see /etc/synui/synuirc or the\n"
"# SynapseOS wiki for the full list.\n");
		}
		seeded = true;
	}

	if (!text)
		text = xstrdup("");

	char *base = strip_block(text);
	free(text);

	char *block = make_block(toggle, cycle);

	size_t n = strlen(base);
	/* Exactly one blank line between whatever was there and the block. */
	while (n > 0 && (base[n - 1] == '\n' || base[n - 1] == ' ' ||
			 base[n - 1] == '\t'))
		n--;

	size_t total = n + strlen(block) + 8;
	char *out = xmalloc(total);
	memcpy(out, base, n);
	out[n] = '\0';
	if (n) strcat(out, "\n\n");
	strcat(out, block);

	free(base);
	free(block);

	int rc = write_file_inplace(user, out);
	free(out);

	if (rc < 0) {
		fprintf(stderr, "syn-arcade: cannot write %s: %s\n",
			user, strerror(-rc));
		return EX_FAIL;
	}

	if (seeded)
		printf("copied %s → %s (synui reads only one, so the whole file "
		       "had to move)\n", eff, user);
	printf("installed in %s\n", user);
	printf("  %-14s toggle the overlay\n", toggle);
	printf("  %-14s move it around the screen\n", cycle);

	/* The overlay config has to EXIST before a game starts or MangoHud's
	 * inotify watch never attaches — a keybind installed without it would
	 * be dead in every game already running. */
	char hudpath[4096];
	if (hud_effective_config(hudpath, sizeof(hudpath)) && !file_exists(hudpath))
		puts("\nRun `syn-arcade hud ensure` too — MangoHud needs the config\n"
		     "file to exist before a game starts, or it never watches it.");

	if (reload)
		return binds_reload();

	puts("\nNot active yet. synui re-reads its config on Super+Shift+W,\n"
	     "on Ctrl+Shift+R in the Super+/ palette, or on:");
	puts("    syn-arcade binds reload");
	return EX_OK;
}

static int binds_remove(bool reload)
{
	char user[4096];
	if (!rc_user_path(user, sizeof(user)))
		return EX_FAIL;

	char *text = read_file(user);
	if (!text) {
		printf("nothing to remove (%s does not exist)\n", user);
		return EX_OK;
	}
	if (!has_block(text)) {
		free(text);
		printf("nothing to remove (no syn-arcade block in %s)\n", user);
		return EX_OK;
	}

	char *base = strip_block(text);
	free(text);

	int rc = write_file_inplace(user, base);
	free(base);

	if (rc < 0) {
		fprintf(stderr, "syn-arcade: cannot write %s: %s\n",
			user, strerror(-rc));
		return EX_FAIL;
	}

	printf("removed the syn-arcade block from %s\n", user);
	/* Deliberately NOT deleting a file this package seeded. By now it is
	 * the only synui config on the machine, and the rest of it is the
	 * user's. */
	if (reload)
		return binds_reload();
	puts("The shortcuts stay live until synui reloads: "
	     "`syn-arcade binds reload`.");
	return EX_OK;
}

/*
 * Ask the running compositor to re-read its config.
 *
 * `synctl dispatch wallpaper_reload` is the verb — input.c maps that action
 * straight to synui_config_reload(). The name is a historical accident (it
 * reloads the wallpaper AND the config, and got named for the visible half),
 * which is worth a sentence here so nobody "fixes" this to something that
 * sounds more correct and does nothing.
 */
int binds_reload(void)
{
	if (!getenv("WAYLAND_DISPLAY") && !getenv("SYNUI_SOCKET")) {
		fputs("syn-arcade: no synui session to reload (not in a "
		      "desktop session?)\n", stderr);
		return EX_FAIL;
	}

	int rc = system("synctl dispatch wallpaper_reload >/dev/null 2>&1");
	if (rc != 0) {
		fputs("syn-arcade: could not reach synui. The shortcuts are\n"
		      "written and will be active at your next login.\n", stderr);
		return EX_FAIL;
	}
	puts("synui reloaded — the shortcuts are live.");
	return EX_OK;
}

/* ── dispatch ────────────────────────────────────────────────────────────── */

static const char *opt_str(int argc, char **argv, const char *name,
			   const char *fallback)
{
	size_t n = strlen(name);
	for (int i = 0; i < argc; i++) {
		if (strncmp(argv[i], name, n) != 0 || argv[i][n] != '=')
			continue;
		return argv[i] + n + 1;
	}
	return fallback;
}

static bool opt_has(int argc, char **argv, const char *name)
{
	for (int i = 0; i < argc; i++)
		if (strcmp(argv[i], name) == 0)
			return true;
	return false;
}

int cmd_binds(int argc, char **argv)
{
	if (argc < 1)
		return binds_show(false);

	const char *sub = argv[0];
	int rest_c = argc - 1;
	char **rest = argv + 1;

	if (strcmp(sub, "--rec") == 0)	return binds_show(true);
	if (strcmp(sub, "show") == 0)
		return binds_show(opt_has(rest_c, rest, "--rec"));
	if (strcmp(sub, "reload") == 0)	return binds_reload();

	if (strcmp(sub, "install") == 0)
		return binds_install(opt_str(rest_c, rest, "--toggle", DEFAULT_TOGGLE),
				     opt_str(rest_c, rest, "--cycle", DEFAULT_CYCLE),
				     opt_has(rest_c, rest, "--reload"));

	if (strcmp(sub, "remove") == 0)
		return binds_remove(opt_has(rest_c, rest, "--reload"));

	fprintf(stderr, "syn-arcade: unknown binds command '%s'\n", sub);
	return EX_USAGE;
}
