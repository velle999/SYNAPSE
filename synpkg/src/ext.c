/* ext.c — the sources that are not ALPM: syn-update, Flatpak, the AUR.
 *
 * Each of these is owned by something else that already works. synpkg's job is
 * to put them behind one command and one pane, not to reimplement them:
 *   - syn-update knows how to rebuild SynapseOS components from git;
 *   - flatpak owns its own transactions and its own permissions model;
 *   - the AUR is makepkg, which refuses to run as root and must not be coaxed.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* ── syn-update ─────────────────────────────────────────────────────────── */

/* syn-update prints a table:  "  <component> <installed> -> <available>".
 * Parsing another program's human output is normally a mistake; it is
 * tolerable here for the same reason syn-update's own QML does it — and the
 * failure mode is bounded, because an unrecognised line is passed through
 * rather than dropped. */
static int system_check(void)
{
	if (!have_cmd("syn-update")) {
		warn("syn-update is not installed — SynapseOS components cannot be checked");
		return 1;
	}

	if (g_out == OUT_HUMAN) {
		char *argv[] = { (char *)"syn-update", (char *)"check", NULL };
		return run(argv, false);
	}

	char *argv[] = { (char *)"syn-update", (char *)"check", NULL };
	int st = 0;
	char *out = run_capture(argv, &st, false);

	tsv_row(3, "component", "installed", "available");

	size_t n = 0;
	char **lines = split(out, '\n', &n);
	for (size_t i = 0; i < n; i++) {
		char *line = lines[i];
		char *arrow = strstr(line, " -> ");
		if (!arrow || strncmp(line, "  ", 2))
			continue;

		*arrow = '\0';
		char *avail = arrow + 4;
		while (*avail == ' ')
			avail++;

		/* Split the left half on whitespace: component, then version. */
		char *comp = line + 2;
		while (*comp == ' ')
			comp++;
		char *ver = comp;
		while (*ver && !isspace((unsigned char)*ver))
			ver++;
		if (*ver)
			*ver++ = '\0';
		while (*ver == ' ')
			ver++;

		/* syn-update prints a header row in the same shape. */
		if (!strcmp(comp, "COMPONENT"))
			continue;
		tsv_row(3, comp, ver, avail);
	}

	free(lines);
	free(out);
	return st;
}

int cmd_system(int argc, char **argv)
{
	const char *sub = argc > 0 ? argv[0] : "check";

	if (!strcmp(sub, "check"))
		return system_check();

	if (!strcmp(sub, "apply") || !strcmp(sub, "status")) {
		if (!have_cmd("syn-update"))
			die("syn-update is not installed");
		/* `apply` is deliberately NOT run through the GUI's stdout: it drives
		 * build-all.sh, which calls `sudo pacman -U` mid-build, and sudo with
		 * no controlling terminal cannot prompt. The GUI opens a terminal for
		 * this; here we already have one. */
		char *child[] = { (char *)"syn-update", (char *)sub, NULL };
		return run(child, false);
	}

	die("system: unknown subcommand '%s' — try check, apply, status", sub);
}

/* ── Flatpak / Flathub ──────────────────────────────────────────────────────
 *
 * Flatpak is a first-class SOURCE here, not just a passthrough verb, because
 * the GUI gives it a tab of its own and a tab that cannot search is furniture.
 * What is still passed through is the transaction: flatpak owns its own
 * permission model and its own polkit integration, and re-deriving either of
 * those would be a second implementation of the part that has to be right.
 *
 * Rows carry a seventh column, `title`. A Flatpak's identity is its
 * application id — `org.mozilla.firefox` is what `install` needs — but nobody
 * reads an id, so the human name travels alongside it rather than replacing
 * it. Every other source emits six columns and the GUI keys off the header.
 */

bool sp_flatpak_present(void)
{
	return have_cmd("flatpak");
}

static void flatpak_required(void)
{
	if (!sp_flatpak_present())
		die("flatpak is not installed — synpkg install flatpak");
}

/* Every configured remote name, as "\nname\nname\n". */
static char *flatpak_remote_names(void)
{
	char *argv[] = { (char *)"flatpak", (char *)"remotes",
	                 (char *)"--columns=name", NULL };
	int st = 0;
	char *out = run_capture(argv, &st, true);
	if (st != 0) {
		free(out);
		return xstrdup("\n");
	}
	char *names = xasprintf("\n%s\n", out);
	free(out);
	return names;
}

/* Membership against a "\na\nb\n" haystack. A bare strstr would report
 * org.gnome.Boxes installed because org.gnome.BoxesDevel is. */
static bool line_present(const char *haystack, const char *needle)
{
	char *wrapped = xasprintf("\n%s\n", needle);
	bool hit = strstr(haystack, wrapped) != NULL;
	free(wrapped);
	return hit;
}

bool sp_flathub_enabled(void)
{
	if (!sp_flatpak_present())
		return false;
	char *names = flatpak_remote_names();
	bool hit = line_present(names, SYNPKG_FLATHUB_NAME);
	free(names);
	return hit;
}

/* Installed application ids, same "\nid\nid\n" shape. */
static char *flatpak_installed_ids(void)
{
	char *argv[] = { (char *)"flatpak", (char *)"list", (char *)"--app",
	                 (char *)"--columns=application", NULL };
	int st = 0;
	char *out = run_capture(argv, &st, true);
	if (st != 0) {
		free(out);
		return xstrdup("\n");
	}
	char *ids = xasprintf("\n%s\n", out);
	free(out);
	return ids;
}

static void flatpak_row_header(void)
{
	tsv_row(7, "name", "installed", "version", "repo", "size", "description",
	        "title");
}

/* One row, in either renderer. `title` may be empty. */
static void flatpak_row(const char *id, bool installed, const char *version,
                        const char *origin, const char *title, const char *desc)
{
	if (g_out == OUT_TSV) {
		tsv_row(7, id, installed ? "1" : "0", version ? version : "",
		        origin && *origin ? origin : "flatpak", "0",
		        desc ? desc : "", title ? title : "");
		return;
	}

	printf("%s%s/%s%s%s%s", C_DIM(), origin && *origin ? origin : "flatpak",
	       C_RESET(), C_BOLD(), title && *title ? title : id, C_RESET());
	if (version && *version)
		printf(" %s%s%s", C_ACCENT(), version, C_RESET());
	if (installed)
		printf(" %s[installed]%s", C_OK(), C_RESET());
	putchar('\n');
	printf("    %s%s%s\n", C_DIM(), id, C_RESET());
	if (desc && *desc)
		printf("    %s\n", desc);
}

static int flatpak_list(void)
{
	flatpak_required();

	char *argv[] = { (char *)"flatpak", (char *)"list", (char *)"--app",
	                 (char *)"--columns=application,version,origin,name",
	                 NULL };
	int st = 0;
	char *out = run_capture(argv, &st, false);

	if (g_out == OUT_TSV)
		flatpak_row_header();

	int shown = 0;
	size_t n = 0;
	char **lines = split(out, '\n', &n);
	for (size_t i = 0; i < n; i++) {
		if (!*lines[i])
			continue;
		size_t nf = 0;
		char **f = split(lines[i], '\t', &nf);
		if (nf >= 1 && *f[0]) {
			/* Installed apps have no description in `list` output; the
			 * name is the only human string available without one
			 * `flatpak info` per row. */
			flatpak_row(f[0], true, nf >= 2 ? f[1] : "",
			            nf >= 3 ? f[2] : "", nf >= 4 ? f[3] : "", "");
			shown++;
		}
		free(f);
	}
	free(lines);
	free(out);

	if (g_out == OUT_HUMAN && !shown)
		info("no Flatpak applications installed");
	return shown ? 0 : (st == 0 ? 100 : st);
}

/* ── browsing Flathub by category ───────────────────────────────────────────
 *
 * The freedesktop main categories, in the order the pane shows them, with the
 * label each one is displayed under. Two separate strings on purpose: "Game"
 * is what the catalogue records and what `flatpak category` takes, "Games" is
 * what a person reads, and collapsing them would mean either an odd-looking
 * pane or a command that takes a label it then has to translate back.
 *
 * This is a CHERRY-PICK, not the full freedesktop list. The catalogue carries
 * a hundred-odd category names, most of them subcategories (ArcadeGame,
 * RasterGraphics, IDE) that would make the pane longer than the results it
 * filters, plus toolkit tags (Qt, GTK, GNOME) that describe how an app was
 * built rather than what it does. Ordering is editorial: what people install
 * a Flatpak FOR comes first, and Utility — the biggest and vaguest bucket —
 * comes last.
 */
static const struct { const char *key, *label; } fh_categories[] = {
	{ "Game",        "Games" },
	{ "AudioVideo",  "Audio & Video" },
	{ "Graphics",    "Graphics" },
	{ "Office",      "Office" },
	{ "Development", "Development" },
	{ "Network",     "Internet" },
	{ "Education",   "Education" },
	{ "Science",     "Science" },
	{ "System",      "System" },
	{ "Utility",     "Utilities" },
};

/* An index is missing on any machine whose Flathub remote was added but never
 * had `flatpak update --appstream` run against it — the exact state
 * enable-flathub exists to prevent. Saying so beats an empty pane, which reads
 * as "Flathub has no games". */
static void no_index(void)
{
	warn("no Flathub application index on this machine — run: "
	     "synpkg flatpak enable-flathub");
}

static int flatpak_categories(void)
{
	flatpak_required();

	size_t n = 0;
	sp_as_app *apps = sp_appstream_load(&n);
	if (!apps) {
		if (g_out == OUT_TSV) {
			tsv_row(4, "category", "total", "installed", "label");
			return 100;
		}
		no_index();
		return 2;
	}

	char *ids = flatpak_installed_ids();

	/* Same four columns as `arsenal categories` and `suggest categories`: one
	 * pane in the GUI renders all three, and it can only stay one pane if
	 * they cannot drift apart. */
	if (g_out == OUT_TSV)
		tsv_row(4, "category", "total", "installed", "label");

	for (size_t c = 0; c < sizeof fh_categories / sizeof *fh_categories; c++) {
		int total = 0, have = 0;
		for (size_t i = 0; i < n; i++) {
			if (!sp_appstream_in(&apps[i], fh_categories[c].key))
				continue;
			total++;
			have += line_present(ids, apps[i].id);
		}
		if (!total)
			continue;

		if (g_out == OUT_TSV) {
			char *t = xasprintf("%d", total);
			char *h = xasprintf("%d", have);
			tsv_row(4, fh_categories[c].key, t, h, fh_categories[c].label);
			free(t);
			free(h);
		} else {
			printf("%s%-20s%s %s%5d%s", C_BOLD(), fh_categories[c].label,
			       C_RESET(), C_DIM(), total, C_RESET());
			if (have)
				printf("  %s%d installed%s", C_OK(), have, C_RESET());
			putchar('\n');
		}
	}

	free(ids);
	sp_appstream_free(apps, n);
	return 0;
}

static int flatpak_category(const char *want)
{
	flatpak_required();

	/* Resolve against the table rather than passing the string through: an
	 * unknown category would otherwise render as a silent empty pane, which
	 * is indistinguishable from a category Flathub genuinely has nothing in. */
	const char *key = NULL;
	for (size_t c = 0; c < sizeof fh_categories / sizeof *fh_categories; c++)
		if (!strcasecmp(want, fh_categories[c].key)
		    || !strcasecmp(want, fh_categories[c].label))
			key = fh_categories[c].key;
	if (!key)
		die("flatpak: '%s' is not a Flathub category — "
		    "try: synpkg flatpak categories", want);

	size_t n = 0;
	sp_as_app *apps = sp_appstream_load(&n);
	if (!apps) {
		if (g_out == OUT_TSV) {
			flatpak_row_header();
			return 100;
		}
		no_index();
		return 2;
	}

	char *ids = flatpak_installed_ids();

	if (g_out == OUT_TSV)
		flatpak_row_header();

	int shown = 0;
	for (size_t i = 0; i < n; i++) {
		if (!sp_appstream_in(&apps[i], key))
			continue;
		flatpak_row(apps[i].id, line_present(ids, apps[i].id),
		            apps[i].version, SYNPKG_FLATHUB_NAME, apps[i].name,
		            apps[i].summary);
		shown++;
	}

	free(ids);
	sp_appstream_free(apps, n);
	return shown ? 0 : 100;
}

static int flatpak_search(const char *term)
{
	flatpak_required();

	if (!sp_flathub_enabled()) {
		/* Not fatal — another remote may be configured — but the empty
		 * result that follows on a box with no remotes is indistinguishable
		 * from "Flathub has nothing", so say it before it happens. */
		char *names = flatpak_remote_names();
		bool any = strlen(names) > 1;
		free(names);
		if (!any) {
			if (g_out == OUT_TSV) {
				flatpak_row_header();
				return 100;
			}
			warn("no Flatpak remotes are configured — run: "
			     "synpkg flatpak enable-flathub");
			return 2;
		}
	}

	char *argv[] = { (char *)"flatpak", (char *)"search",
	                 (char *)"--columns=application,version,remotes,name,description",
	                 (char *)term, NULL };
	/* `flatpak search` exits non-zero and prints "No matches found" on an
	 * empty result, which is not an error worth relaying into the GUI's
	 * stderr. In human mode the message is the answer, so it stays. */
	int st = 0;
	char *out = run_capture(argv, &st, g_out == OUT_TSV);

	char *ids = flatpak_installed_ids();

	if (g_out == OUT_TSV)
		flatpak_row_header();

	int shown = 0;
	size_t n = 0;
	char **lines = split(out, '\n', &n);
	for (size_t i = 0; i < n; i++) {
		if (!*lines[i])
			continue;
		size_t nf = 0;
		char **f = split(lines[i], '\t', &nf);
		if (nf >= 1 && *f[0] && strchr(f[0], '.')) {
			flatpak_row(f[0], line_present(ids, f[0]),
			            nf >= 2 ? f[1] : "", nf >= 3 ? f[2] : "",
			            nf >= 4 ? f[3] : "", nf >= 5 ? f[4] : "");
			shown++;
		}
		free(f);
	}
	free(lines);
	free(out);
	free(ids);

	if (g_out == OUT_HUMAN && !shown)
		info("nothing matched");
	return shown ? 0 : 100;
}

/* The 5-column updates shape every other source uses:
 *   name \t installed_version \t new_version \t repo \t size
 *
 * `flatpak remote-ls --updates` has no version column — the man page's column
 * list has none — so the new version is genuinely not knowable here without
 * one `remote-info` round trip per application. It is emitted empty rather
 * than filled with a commit hash pretending to be a version; the GUI renders
 * an empty new version as "update available". */
static int flatpak_updates(void)
{
	if (!sp_flatpak_present()) {
		if (g_out == OUT_TSV)
			tsv_row(5, "name", "installed_version", "new_version", "repo",
			        "size");
		return 100;
	}

	char *cur_argv[] = { (char *)"flatpak", (char *)"list", (char *)"--app",
	                     (char *)"--columns=application,version", NULL };
	int st = 0;
	char *cur = run_capture(cur_argv, &st, true);

	/* Split the installed list ONCE, up front. split() writes NULs into the
	 * buffer it is given, so re-scanning `cur` per update row would truncate
	 * it at the first match and silently blank every version after that. */
	size_t ncur = 0;
	char **cur_lines = split(cur, '\n', &ncur);
	char **cur_ver = xmalloc((ncur ? ncur : 1) * sizeof *cur_ver);
	for (size_t i = 0; i < ncur; i++) {
		char *tab = strchr(cur_lines[i], '\t');
		cur_ver[i] = tab ? tab + 1 : (char *)"";
		if (tab)
			*tab = '\0';
	}

	char *upd_argv[] = { (char *)"flatpak", (char *)"remote-ls",
	                     (char *)"--updates", (char *)"--app",
	                     (char *)"--columns=application,origin", NULL };
	char *upd = run_capture(upd_argv, &st, g_out == OUT_TSV);

	if (g_out == OUT_TSV)
		tsv_row(5, "name", "installed_version", "new_version", "repo", "size");

	int shown = 0;
	size_t n = 0;
	char **lines = split(upd, '\n', &n);
	for (size_t i = 0; i < n; i++) {
		if (!*lines[i])
			continue;
		size_t nf = 0;
		char **f = split(lines[i], '\t', &nf);
		if (nf < 1 || !*f[0] || !strchr(f[0], '.')) {
			free(f);
			continue;
		}

		/* Current version, looked up in the already-split `list` output
		 * rather than a second flatpak call per row. */
		const char *have = "";
		for (size_t j = 0; j < ncur; j++) {
			if (!strcmp(cur_lines[j], f[0])) {
				have = cur_ver[j];
				break;
			}
		}

		shown++;
		if (g_out == OUT_TSV)
			tsv_row(5, f[0], have, "", nf >= 2 ? f[1] : "flatpak", "0");
		else
			printf("%s%-40s%s %s%s%s -> %supdate available%s\n", C_BOLD(),
			       f[0], C_RESET(), C_DIM(), have, C_RESET(), C_ACCENT(),
			       C_RESET());
		free(f);
	}
	free(lines);
	free(upd);
	free(cur_lines);
	free(cur_ver);
	free(cur);

	if (g_out == OUT_HUMAN && !shown)
		printf("%sFlatpak applications are up to date%s\n", C_OK(), C_RESET());
	return shown ? 0 : 100;
}

static int flatpak_remotes(void)
{
	flatpak_required();

	char *argv[] = { (char *)"flatpak", (char *)"remotes",
	                 (char *)"--columns=name,url,options", NULL };
	if (g_out == OUT_HUMAN)
		return run(argv, false);

	int st = 0;
	char *out = run_capture(argv, &st, false);
	tsv_row(3, "remote", "url", "options");

	size_t n = 0;
	char **lines = split(out, '\n', &n);
	for (size_t i = 0; i < n; i++) {
		if (!*lines[i])
			continue;
		size_t nf = 0;
		char **f = split(lines[i], '\t', &nf);
		if (nf >= 1 && *f[0])
			tsv_row(3, f[0], nf >= 2 ? f[1] : "", nf >= 3 ? f[2] : "");
		free(f);
	}
	free(lines);
	free(out);
	return st;
}

/* Adding the remote is a system-wide change, so it goes through the same
 * pkexec path install does rather than relying on a polkit agent being present
 * — `synpkg flatpak enable-flathub` has to work over SSH too. */
static int flatpak_enable_flathub(void)
{
	flatpak_required();

	if (sp_flathub_enabled()) {
		info("Flathub is already enabled");
		return 0;
	}
	if (!is_root())
		return escalate("flatpak", 1, (char *[]){ (char *)"enable-flathub" });

	/* The .flatpakrepo carries Flathub's GPG key, which is why the URL is
	 * the repo file and not the bare repository: adding the latter would
	 * configure a remote with no signature verification at all. */
	char *add[] = { (char *)"flatpak", (char *)"remote-add",
	                (char *)"--if-not-exists", (char *)SYNPKG_FLATHUB_NAME,
	                (char *)SYNPKG_FLATHUB_URL, NULL };
	int rc = run(add, false);
	if (rc != 0) {
		warn("could not add the Flathub remote");
		return rc;
	}

	/* A newly added remote has NO appstream index, and `flatpak search`
	 * against one returns zero rows without erroring. Skipping this leaves a
	 * Flathub tab that looks enabled and finds nothing, forever. */
	info("fetching Flathub's application index — this takes a minute");
	char *as[] = { (char *)"flatpak", (char *)"update", (char *)"--appstream",
	               (char *)"--noninteractive", NULL };
	run(as, false);

	info("Flathub is enabled");
	return 0;
}

/* install/remove stay flatpak's own: it prompts for the permissions an
 * application asks for, and a wrapper that answered those prompts on the
 * user's behalf would be strictly worse than the tool it replaced. */
static int flatpak_transact(const char *verb, int argc, char **argv)
{
	flatpak_required();
	/* `update` with no target is a whole-system update and is meaningful;
	 * install and uninstall with no target are not. */
	if (argc < 1 && strcmp(verb, "update"))
		die("flatpak %s: need an application id", verb);

	char **child = xmalloc((size_t)(argc + 4) * sizeof *child);
	int k = 0;
	child[k++] = (char *)"flatpak";
	child[k++] = (char *)verb;
	if (g_noconfirm)
		child[k++] = (char *)"--noninteractive";
	for (int i = 0; i < argc; i++)
		child[k++] = argv[i];
	child[k] = NULL;

	int rc = run(child, false);
	free(child);
	return rc;
}

int cmd_flatpak(int argc, char **argv)
{
	const char *sub = argc > 0 ? argv[0] : "list";

	if (!strcmp(sub, "list") || !strcmp(sub, "installed"))
		return flatpak_list();
	if (!strcmp(sub, "search")) {
		if (argc < 2)
			die("flatpak search: need a term");
		return flatpak_search(argv[1]);
	}
	if (!strcmp(sub, "categories"))
		return flatpak_categories();
	if (!strcmp(sub, "category")) {
		if (argc < 2)
			die("flatpak category: need a category — "
			    "try: synpkg flatpak categories");
		return flatpak_category(argv[1]);
	}
	if (!strcmp(sub, "updates"))
		return flatpak_updates();
	if (!strcmp(sub, "remotes"))
		return flatpak_remotes();
	if (!strcmp(sub, "enable-flathub"))
		return flatpak_enable_flathub();
	if (!strcmp(sub, "install"))
		return flatpak_transact("install", argc - 1, argv + 1);
	if (!strcmp(sub, "remove") || !strcmp(sub, "uninstall"))
		return flatpak_transact("uninstall", argc - 1, argv + 1);
	/* `update` goes through the same wrapper purely so --noconfirm reaches it
	 * as --noninteractive. The GUI's update button has nowhere to answer a
	 * prompt, and flatpak asks one per application by default. */
	if (!strcmp(sub, "update"))
		return flatpak_transact("update", argc - 1, argv + 1);

	/* Anything unrecognised is still handed to flatpak. Its CLI is larger
	 * than the part worth wrapping, and `synpkg flatpak permissions foo`
	 * should not need a release of this program to work. */
	flatpak_required();
	char **child = xmalloc((size_t)(argc + 2) * sizeof *child);
	child[0] = (char *)"flatpak";
	for (int i = 0; i < argc; i++)
		child[i + 1] = argv[i];
	child[argc + 1] = NULL;

	int rc = run(child, false);
	free(child);
	return rc;
}

/* ── AUR ────────────────────────────────────────────────────────────────── */

/* A JSON reader small enough to audit, rather than a dependency. It only has
 * to survive the AUR RPC's schema, which is a flat array of flat objects. Any
 * value it does not recognise is skipped structurally, so a new upstream field
 * cannot desynchronise the parse. */

static void json_skip_ws(const char **p)
{
	while (**p && isspace((unsigned char)**p))
		(*p)++;
}

/* Reads a JSON string starting at the opening quote. Returns malloc'd, with
 * escapes resolved; \uXXXX becomes UTF-8. */
static char *json_string(const char **p)
{
	if (**p != '"')
		return NULL;
	(*p)++;

	size_t cap = 32, len = 0;
	char *out = xmalloc(cap);

	while (**p && **p != '"') {
		unsigned char c = (unsigned char)**p;
		if (len + 5 >= cap) {
			cap *= 2;
			out = xrealloc(out, cap);
		}
		if (c != '\\') {
			out[len++] = (char)c;
			(*p)++;
			continue;
		}
		(*p)++;
		switch (**p) {
		case 'n': out[len++] = '\n'; (*p)++; break;
		case 't': out[len++] = '\t'; (*p)++; break;
		case 'r': out[len++] = '\r'; (*p)++; break;
		case 'b': out[len++] = '\b'; (*p)++; break;
		case 'f': out[len++] = '\f'; (*p)++; break;
		case 'u': {
			char hex[5] = {0};
			for (int i = 0; i < 4 && (*p)[1 + i]; i++)
				hex[i] = (*p)[1 + i];
			unsigned cp = (unsigned)strtoul(hex, NULL, 16);
			*p += 5;
			/* Surrogate pairs are not decoded; the AUR does not emit
			 * astral-plane text and a lone replacement char beats a
			 * mis-sized buffer. */
			if (cp < 0x80) {
				out[len++] = (char)cp;
			} else if (cp < 0x800) {
				out[len++] = (char)(0xC0 | (cp >> 6));
				out[len++] = (char)(0x80 | (cp & 0x3F));
			} else {
				out[len++] = (char)(0xE0 | (cp >> 12));
				out[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
				out[len++] = (char)(0x80 | (cp & 0x3F));
			}
			break;
		}
		default:
			out[len++] = **p;
			(*p)++;
			break;
		}
	}
	if (**p == '"')
		(*p)++;
	out[len] = '\0';
	return out;
}

static void json_skip_value(const char **p)
{
	json_skip_ws(p);
	if (**p == '"') {
		free(json_string(p));
		return;
	}
	if (**p == '{' || **p == '[') {
		char open = **p, close = open == '{' ? '}' : ']';
		int depth = 0;
		while (**p) {
			if (**p == '"') {
				free(json_string(p));
				continue;
			}
			if (**p == open)
				depth++;
			else if (**p == close && --depth == 0) {
				(*p)++;
				return;
			}
			(*p)++;
		}
		return;
	}
	while (**p && **p != ',' && **p != '}' && **p != ']')
		(*p)++;
}

typedef struct {
	char *name, *version, *desc, *maintainer;
	long votes;
	bool outofdate;
} aur_pkg_t;

static void aur_pkg_free(aur_pkg_t *p)
{
	free(p->name);
	free(p->version);
	free(p->desc);
	free(p->maintainer);
	*p = (aur_pkg_t){0};
}

/* Walks "results":[ ... ] and calls `fn` per object. Returns the count, or -1
 * if the payload was not a well-formed AUR response. */
static int aur_each(const char *json, void (*fn)(aur_pkg_t *, void *), void *ctx)
{
	const char *p = strstr(json, "\"results\"");
	if (!p)
		return -1;
	p += strlen("\"results\"");
	json_skip_ws(&p);
	if (*p != ':')
		return -1;
	p++;
	json_skip_ws(&p);
	if (*p != '[')
		return -1;
	p++;

	int count = 0;
	for (;;) {
		json_skip_ws(&p);
		if (*p == ']' || !*p)
			break;
		if (*p == ',') {
			p++;
			continue;
		}
		if (*p != '{')
			break;
		p++;

		aur_pkg_t pkg = {0};
		for (;;) {
			json_skip_ws(&p);
			if (*p == '}' || !*p) {
				if (*p)
					p++;
				break;
			}
			if (*p == ',') {
				p++;
				continue;
			}
			char *key = json_string(&p);
			if (!key)
				break;
			json_skip_ws(&p);
			if (*p == ':')
				p++;
			json_skip_ws(&p);

			if (!strcmp(key, "Name") && *p == '"')
				pkg.name = json_string(&p);
			else if (!strcmp(key, "Version") && *p == '"')
				pkg.version = json_string(&p);
			else if (!strcmp(key, "Description") && *p == '"')
				pkg.desc = json_string(&p);
			else if (!strcmp(key, "Maintainer") && *p == '"')
				pkg.maintainer = json_string(&p);
			else if (!strcmp(key, "NumVotes") && (isdigit((unsigned char)*p) || *p == '-'))
				pkg.votes = strtol(p, (char **)&p, 10);
			else if (!strcmp(key, "OutOfDate")) {
				pkg.outofdate = *p != 'n';   /* null when maintained */
				json_skip_value(&p);
			} else {
				json_skip_value(&p);
			}
			free(key);
		}

		if (pkg.name) {
			fn(&pkg, ctx);
			count++;
		}
		aur_pkg_free(&pkg);
	}
	return count;
}

/* Percent-encode everything outside the unreserved set. The search term goes
 * into a URL handed to curl; letting a `&` or a shell-significant character
 * through is how a search box becomes a request-forgery. */
static char *url_encode(const char *s)
{
	size_t len = strlen(s);
	char *out = xmalloc(len * 3 + 1);
	size_t k = 0;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			out[k++] = (char)c;
		else
			k += (size_t)sprintf(out + k, "%%%02X", c);
	}
	out[k] = '\0';
	return out;
}

static char *aur_rpc(const char *url)
{
	if (!have_cmd("curl"))
		die("curl is required for AUR access");
	char *argv[] = { (char *)"curl", (char *)"-fsS", (char *)"--proto",
	                 (char *)"=https", (char *)"--tlsv1.2", (char *)"--max-time",
	                 (char *)"20", (char *)url, NULL };
	int st = 0;
	char *out = run_capture(argv, &st, false);
	if (st != 0) {
		free(out);
		return NULL;
	}
	return out;
}

struct aur_render_ctx {
	alpm_handle_t *h;
	int shown;
};

static void aur_render(aur_pkg_t *p, void *vctx)
{
	struct aur_render_ctx *ctx = vctx;
	bool installed = alpm_db_get_pkg(alpm_get_localdb(ctx->h), p->name) != NULL;
	ctx->shown++;

	if (g_out == OUT_TSV) {
		char *votes = xasprintf("%ld", p->votes);
		tsv_row(7, p->name, installed ? "1" : "0", p->version ? p->version : "",
		        "aur", votes, p->desc ? p->desc : "",
		        p->outofdate ? "out-of-date" : (p->maintainer ? "" : "orphaned"));
		free(votes);
		return;
	}

	printf("%saur/%s%s%s %s%s%s", C_DIM(), C_RESET(), C_BOLD(), p->name,
	       C_ACCENT(), p->version ? p->version : "", C_RESET());
	printf(" %s(%ld votes)%s", C_DIM(), p->votes, C_RESET());
	if (installed)
		printf(" %s[installed]%s", C_OK(), C_RESET());
	if (p->outofdate)
		printf(" %s[out of date]%s", C_WARN(), C_RESET());
	if (!p->maintainer)
		printf(" %s[orphaned]%s", C_WARN(), C_RESET());
	putchar('\n');
	if (p->desc && *p->desc)
		printf("    %s\n", p->desc);
}

int aur_search_term(const char *term)
{
	char *enc = url_encode(term);
	char *url = xasprintf(
	    "https://aur.archlinux.org/rpc/v5/search/%s?by=name-desc", enc);
	free(enc);

	char *json = aur_rpc(url);
	free(url);
	if (!json) {
		warn("could not reach the AUR");
		return 1;
	}

	alpm_handle_t *h = sp_alpm_init(false);
	struct aur_render_ctx ctx = { h, 0 };

	if (g_out == OUT_TSV)
		tsv_row(7, "name", "installed", "version", "repo", "votes",
		        "description", "flag");

	int n = aur_each(json, aur_render, &ctx);
	sp_alpm_free(h);
	free(json);

	if (n < 0) {
		warn("the AUR returned something this does not understand");
		return 1;
	}
	if (!ctx.shown && g_out == OUT_HUMAN)
		info("nothing matched");
	return 0;
}

/* Every locally-installed package that no sync database offers. That is the
 * AUR-and-friends set; there is no marker on a package saying where it came
 * from, so this is the only honest way to compute it. */
static char **foreign_packages(alpm_handle_t *h, size_t *n)
{
	size_t cap = 32, k = 0;
	char **out = xmalloc(cap * sizeof *out);

	for (alpm_list_t *i = alpm_db_get_pkgcache(alpm_get_localdb(h)); i; i = i->next) {
		const char *name = alpm_pkg_get_name(i->data);
		bool known = false;
		for (alpm_list_t *d = sp_syncdbs(h); d && !known; d = d->next)
			known = alpm_db_get_pkg(d->data, name) != NULL;
		if (known)
			continue;
		if (k + 1 >= cap) {
			cap *= 2;
			out = xrealloc(out, cap * sizeof *out);
		}
		out[k++] = xstrdup(name);
	}
	*n = k;
	return out;
}

struct aur_update_ctx {
	alpm_handle_t *h;
	int n;
};

static void aur_update_row(aur_pkg_t *p, void *vctx)
{
	struct aur_update_ctx *ctx = vctx;
	alpm_pkg_t *local = alpm_db_get_pkg(alpm_get_localdb(ctx->h), p->name);
	if (!local || !p->version)
		return;

	/* alpm_pkg_vercmp, not strcmp: "0.1.0-203" sorts below "0.1.0-99" as
	 * text, and that mistake silently declines to offer an update. */
	if (alpm_pkg_vercmp(p->version, alpm_pkg_get_version(local)) <= 0)
		return;

	ctx->n++;
	if (g_out == OUT_TSV)
		tsv_row(5, p->name, alpm_pkg_get_version(local), p->version, "aur", "0");
	else
		printf("%s%-30s%s %s%s%s -> %s%s%s\n", C_BOLD(), p->name, C_RESET(),
		       C_DIM(), alpm_pkg_get_version(local), C_RESET(), C_ACCENT(),
		       p->version, C_RESET());
}

static int aur_updates(void)
{
	alpm_handle_t *h = sp_alpm_init(false);
	size_t nf = 0;
	char **foreign = foreign_packages(h, &nf);

	if (!nf) {
		sp_alpm_free(h);
		free(foreign);
		if (g_out == OUT_HUMAN)
			printf("%sno foreign packages installed%s\n", C_OK(), C_RESET());
		return 100;
	}

	/* One multiinfo call for every foreign package: the RPC takes repeated
	 * arg[] parameters, so this is a single request rather than nf of them. */
	size_t len = 64;
	for (size_t i = 0; i < nf; i++)
		len += strlen(foreign[i]) * 3 + 8;
	char *url = xmalloc(len);
	int k = sprintf(url, "https://aur.archlinux.org/rpc/v5/info?");
	for (size_t i = 0; i < nf; i++) {
		char *enc = url_encode(foreign[i]);
		k += sprintf(url + k, "%sarg[]=%s", i ? "&" : "", enc);
		free(enc);
	}

	char *json = aur_rpc(url);
	free(url);
	for (size_t i = 0; i < nf; i++)
		free(foreign[i]);
	free(foreign);

	if (!json) {
		sp_alpm_free(h);
		warn("could not reach the AUR");
		return 1;
	}

	if (g_out == OUT_TSV)
		tsv_row(5, "name", "installed_version", "new_version", "repo", "size");

	struct aur_update_ctx ctx = { h, 0 };
	int parsed = aur_each(json, aur_update_row, &ctx);
	free(json);
	sp_alpm_free(h);

	if (parsed < 0) {
		warn("the AUR returned something this does not understand");
		return 1;
	}
	if (g_out == OUT_HUMAN && !ctx.n)
		printf("%sAUR packages are up to date%s\n", C_OK(), C_RESET());
	return ctx.n ? 0 : 100;
}

/* makepkg refuses to run as root, and for good reason: it executes a PKGBUILD
 * fetched from the internet. So this path never escalates — if we are already
 * root because the user reached it through pkexec, refuse rather than try to
 * drop back down to a user we would have to guess at. */
static int aur_install(int argc, char **argv)
{
	if (is_root())
		die("AUR builds must not run as root — run `synpkg aur install` as "
		    "your own user\n  (makepkg will be asked for the root password "
		    "itself when it installs)");

	if (!have_cmd("git"))
		die("git is required to build from the AUR");
	if (!have_cmd("makepkg"))
		die("makepkg is required to build from the AUR — synpkg install base-devel");

	const char *home = getenv("HOME");
	if (!home || !*home)
		die("HOME is not set");
	char *base = xasprintf("%s/.cache/synpkg/aur", home);

	/* mkdir -p, one component at a time. */
	for (char *s = base + 1; *s; s++) {
		if (*s != '/')
			continue;
		*s = '\0';
		mkdir(base, 0755);
		*s = '/';
	}
	mkdir(base, 0755);

	int rc = 0;
	for (int i = 0; i < argc; i++) {
		const char *pkg = argv[i];
		/* The name goes into a path and a git URL. Anything outside the AUR's
		 * own permitted character set is refused rather than escaped. */
		for (const char *c = pkg; *c; c++) {
			if (!isalnum((unsigned char)*c) && !strchr("@._+-", *c))
				die("'%s' is not a valid package name", pkg);
		}

		char *dir = xasprintf("%s/%s", base, pkg);
		char *url = xasprintf("https://aur.archlinux.org/%s.git", pkg);

		if (access(dir, F_OK) == 0) {
			info("updating %s", pkg);
			char *pull[] = { (char *)"git", (char *)"-C", dir,
			                 (char *)"pull", (char *)"--ff-only", NULL };
			if (run(pull, false) != 0)
				warn("could not update %s — building the existing checkout", pkg);
		} else {
			info("cloning %s", pkg);
			char *clone[] = { (char *)"git", (char *)"clone", (char *)"--depth=1",
			                  url, dir, NULL };
			if (run(clone, false) != 0) {
				warn("could not clone %s — is it in the AUR?", pkg);
				rc = 1;
				free(dir);
				free(url);
				continue;
			}
		}

		/* Show the PKGBUILD before building it. An AUR package is arbitrary
		 * code running as you; a manager that hides that is worse than the
		 * manual process it replaces. */
		char *pkgbuild = xasprintf("%s/PKGBUILD", dir);
		if (!g_noconfirm && isatty(STDIN_FILENO)) {
			if (confirm("review the PKGBUILD for %s?", pkg)) {
				const char *pager = getenv("PAGER");
				char *view[] = { (char *)(pager && *pager ? pager : "less"),
				                 pkgbuild, NULL };
				if (run(view, false) != 0) {
					char *fallback[] = { (char *)"cat", pkgbuild, NULL };
					run(fallback, false);
				}
			}
			if (!confirm("build and install %s?", pkg)) {
				info("skipped %s", pkg);
				free(pkgbuild);
				free(dir);
				free(url);
				continue;
			}
		}
		free(pkgbuild);

		char *mk[] = { (char *)"makepkg", (char *)"-si",
		               g_noconfirm ? (char *)"--noconfirm" : (char *)"--needed",
		               NULL };
		/* makepkg must run inside the checkout; fork so the cwd change does
		 * not leak into the rest of the process. */
		pid_t pid = fork();
		if (pid == 0) {
			if (chdir(dir) != 0)
				_exit(1);
			execvp(mk[0], mk);
			_exit(127);
		}
		int st = 0;
		if (pid > 0)
			waitpid(pid, &st, 0);
		if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
			warn("build failed for %s", pkg);
			rc = 1;
		}

		free(dir);
		free(url);
	}

	free(base);
	return rc;
}

/* What the AUR tab shows before you have typed anything: every installed
 * package no sync database offers. It is the same "foreign" set aur_updates
 * computes.
 *
 * These rows are labelled `local`, NOT `aur`, and that distinction is the
 * whole point of giving each source its own tab. Nothing on disk records where
 * a package came from, so this list is AUR builds mixed with anything else
 * built by hand — on a SynapseOS box that includes synpkg itself. Stamping
 * `aur` on all of it would put a confident, wrong source badge on the
 * program's own row. Confirming membership would mean an AUR round trip per
 * package on tab open, which is `aur updates`' job, not this one's. */
static int aur_installed(void)
{
	alpm_handle_t *h = sp_alpm_init(false);
	emit_pkg_header();

	int shown = 0;
	for (alpm_list_t *i = alpm_db_get_pkgcache(alpm_get_localdb(h)); i; i = i->next) {
		alpm_pkg_t *p = i->data;
		const char *name = alpm_pkg_get_name(p);

		bool known = false;
		for (alpm_list_t *d = sp_syncdbs(h); d && !known; d = d->next)
			known = alpm_db_get_pkg(d->data, name) != NULL;
		if (known)
			continue;

		emit_pkg(name, true, alpm_pkg_get_version(p), "local",
		         alpm_pkg_get_isize(p), alpm_pkg_get_desc(p));
		shown++;
	}
	sp_alpm_free(h);

	if (g_out == OUT_HUMAN && !shown)
		info("no foreign packages installed");
	return shown ? 0 : 100;
}

int cmd_aur(int argc, char **argv)
{
	const char *sub = argc > 0 ? argv[0] : "";

	if (!strcmp(sub, "installed") || !strcmp(sub, "list"))
		return aur_installed();
	if (!strcmp(sub, "search")) {
		if (argc < 2)
			die("aur search: need a term");
		return aur_search_term(argv[1]);
	}
	if (!strcmp(sub, "updates"))
		return aur_updates();
	if (!strcmp(sub, "install")) {
		if (argc < 2)
			die("aur install: need a package");
		return aur_install(argc - 1, argv + 1);
	}

	die("aur: unknown subcommand '%s' — try search, install, installed, updates",
	    sub);
}
