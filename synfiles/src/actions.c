/* actions.c — "Open With" and the service menus, from the desktop's own data.
 *
 * The right-click menu in a file manager is mostly other people's software.
 * Dolphin's Extract, Set as Wallpaper and Mount ISO entries are not built into
 * Dolphin: they are .desktop files in $XDG_DATA_DIRS/kio/servicemenus, and on
 * SynapseOS five of them are synui's own. Reading that directory means synfiles
 * inherits every one of them, plus anything installed later, without a line of
 * duplicated logic — and means a helper only has to be written once for both
 * file managers.
 *
 * Two sources, two shapes:
 *
 *   - Open With comes from mimeinfo.cache, the index update-desktop-database
 *     already maintains. Scanning every .desktop file to find which ones claim
 *     a mime type is what that cache exists to avoid.
 *   - Service menus are Desktop Entry files with Actions=, where each action is
 *     its own [Desktop Action id] group with a Name and an Exec.
 *
 * The GUI never builds a command line. It asks for the applicable actions, and
 * then asks THIS to run one by name — so Exec parsing, %F/%f/%U substitution
 * and quoting live in one testable place instead of in QML string handling.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ── the XDG search path ────────────────────────────────────────────────── */

/* $XDG_DATA_HOME first, then $XDG_DATA_DIRS in order — the user's own copy of
 * a service menu must win over the system one, which is how somebody disables
 * or replaces a shipped helper. */
static char **data_dirs(size_t *n)
{
	const char *env = getenv("SYNFILES_DATA_DIRS");
	if (!env || !*env)
		env = getenv("XDG_DATA_DIRS");
	if (!env || !*env)
		env = "/usr/local/share:/usr/share";

	char *home = xdg_data_home();
	char *joined = xasprintf("%s:%s", home, env);
	free(home);

	size_t count = 0;
	char **parts = split(joined, ':', &count);

	char **out = xmalloc((count ? count : 1) * sizeof *out);
	size_t k = 0;
	for (size_t i = 0; i < count; i++)
		if (*parts[i])
			out[k++] = xstrdup(parts[i]);

	free(parts);
	free(joined);
	*n = k;
	return out;
}

static void free_list(char **list, size_t n)
{
	for (size_t i = 0; i < n; i++)
		free(list[i]);
	free(list);
}

/* ── minimal Desktop Entry reading ──────────────────────────────────────────
 *
 * Not a general INI parser. Desktop Entry files are line-oriented with
 * [Group] headers and Key=Value, and the only subtlety that matters here is
 * that a key must be read from the RIGHT group — Name appears in the entry and
 * again in every action, and taking the wrong one labels every action after
 * the application instead of the thing it does.
 */
static char *entry_get(const char *text, const char *group, const char *key)
{
	size_t klen = strlen(key);
	bool in_group = false;
	char *result = NULL;

	char *copy = xstrdup(text);
	size_t nlines = 0;
	char **lines = split(copy, '\n', &nlines);

	for (size_t i = 0; i < nlines && !result; i++) {
		char *l = lines[i];
		while (*l == ' ' || *l == '\t')
			l++;
		if (*l == '#' || !*l)
			continue;

		if (*l == '[') {
			char *end = strchr(l, ']');
			if (!end)
				continue;
			*end = '\0';
			in_group = !strcmp(l + 1, group);
			continue;
		}

		if (!in_group)
			continue;

		/* Exactly "Key=" — otherwise "Name" matches "Name[de]" and the menu
		 * comes out in whichever translation sorted first. */
		if (strncmp(l, key, klen) || l[klen] != '=')
			continue;
		result = xstrdup(l + klen + 1);
	}

	free(lines);
	free(copy);
	return result;
}

/* ── mime aliases and subclasses ────────────────────────────────────────────
 *
 * A mime type has more than one name, and a service menu is written against
 * whichever one its author had in front of them.
 *
 * This is not theoretical. synui's own Mount ISO menu declares
 * application/x-cd-image and application/x-iso9660-image; shared-mime-info's
 * glob table answers application/vnd.efi.iso for *.iso, and
 * /usr/share/mime/aliases records the first two as ALIASES of the third. A
 * matcher that compares the two strings directly finds nothing, so the menu
 * silently never appears — which is exactly what happened, and why Dolphin
 * showed the entry and synfiles did not.
 *
 * Subclasses matter for the same reason in the other direction: text/x-csrc is
 * a subclass of text/plain, so a menu declared for text/plain should apply to
 * a .c file. Following the chain is what makes "Open with a text editor" work
 * on source code.
 */
static char *g_alias_backing, *g_sub_backing;
static char **g_alias_from, **g_alias_to;   size_t g_nalias;
static char **g_sub_child, **g_sub_parent;  size_t g_nsub;

static void load_pairs(const char *path, char ***a, char ***b, size_t *n,
                       char **backing)
{
	*backing = slurp(path);
	if (!*backing)
		return;

	size_t nlines = 0;
	char **lines = split(*backing, '\n', &nlines);
	*a = xmalloc((nlines ? nlines : 1) * sizeof **a);
	*b = xmalloc((nlines ? nlines : 1) * sizeof **b);

	for (size_t i = 0; i < nlines; i++) {
		if (!*lines[i])
			continue;
		char *sp = strchr(lines[i], ' ');
		if (!sp)
			continue;
		*sp = '\0';
		(*a)[*n] = lines[i];
		(*b)[*n] = sp + 1;
		(*n)++;
	}
	free(lines);
}

static void load_mime_tables(void)
{
	static bool done;
	if (done)
		return;
	done = true;

	const char *env = getenv("SYNFILES_MIME_DIR");
	const char *dir = (env && *env) ? env : "/usr/share/mime";

	char *ap = xasprintf("%s/aliases", dir);
	char *sp = xasprintf("%s/subclasses", dir);
	load_pairs(ap, &g_alias_from, &g_alias_to, &g_nalias, &g_alias_backing);
	load_pairs(sp, &g_sub_child, &g_sub_parent, &g_nsub, &g_sub_backing);
	free(ap);
	free(sp);
}

static bool set_has(const char *set, const char *s)
{
	char *w = xasprintf("\n%s\n", s);
	bool hit = strstr(set, w) != NULL;
	free(w);
	return hit;
}

static char *set_add(char *set, const char *s)
{
	if (set_has(set, s))
		return set;
	char *grown = xasprintf("%s%s\n", set, s);
	free(set);
	return grown;
}

/* Every name this type answers to: itself, whatever it is an alias OF, every
 * alias pointing AT it, and the whole chain of parents. Returned as
 * "\ntype\ntype\n" so membership is a whole-line test. */
static char *mime_equivalents(const char *mime)
{
	load_mime_tables();

	char *set = xasprintf("\n%s\n", mime);

	/* If we were handed an alias, add what it resolves to. */
	for (size_t i = 0; i < g_nalias; i++)
		if (!strcmp(g_alias_from[i], mime))
			set = set_add(set, g_alias_to[i]);

	/* And every alias that resolves to anything already in the set — this is
	 * the direction that fixes the ISO menu. */
	for (int pass = 0; pass < 2; pass++)
		for (size_t i = 0; i < g_nalias; i++)
			if (set_has(set, g_alias_to[i]))
				set = set_add(set, g_alias_from[i]);

	/* Parents, transitively. Bounded rather than recursive: the table is data
	 * and a cycle in it must not become an infinite loop here. */
	for (int depth = 0; depth < 8; depth++) {
		size_t before = strlen(set);
		for (size_t i = 0; i < g_nsub; i++)
			if (set_has(set, g_sub_child[i]))
				set = set_add(set, g_sub_parent[i]);
		if (strlen(set) == before)
			break;
	}

	return set;
}

/* Is any name this type answers to in a ";"-separated MimeType= list?
 * Whole-entry match: a substring test puts every application/x-tar handler on
 * application/x-tar-gz. */
static bool mime_listed(const char *list, const char *mime)
{
	if (!list || !mime || !*mime)
		return false;

	char *equiv = mime_equivalents(mime);

	bool hit = false;
	for (const char *p = list; p && *p && !hit; ) {
		const char *semi = strchr(p, ';');
		size_t len = semi ? (size_t)(semi - p) : strlen(p);
		if (len) {
			char *one = xstrndup(p, len);
			hit = set_has(equiv, one);
			free(one);
		}
		p = semi ? semi + 1 : NULL;
	}

	free(equiv);
	return hit;
}

/* ── emitting ───────────────────────────────────────────────────────────── */

static void action_header(void)
{
	if (g_out == OUT_REC)
		rec_row(5, "kind", "desktop", "action", "label", "icon");
}

static void action_row(const char *kind, const char *desktop, const char *action,
                       const char *label, const char *icon)
{
	if (g_out == OUT_REC) {
		rec_row(5, kind, desktop, action ? action : "", label, icon ? icon : "");
		return;
	}
	printf("  %s%-10s%s %s%-28s%s %s%s%s\n", C_DIM(), kind, C_RESET(),
	       C_ACCENT(), label, C_RESET(), C_DIM(), desktop, C_RESET());
}

/* ── Open With ──────────────────────────────────────────────────────────── */

/* The .desktop file's own Name and Icon, looked up across the search path. */
static char *find_desktop(const char *id)
{
	size_t ndirs = 0;
	char **dirs = data_dirs(&ndirs);

	char *found = NULL;
	for (size_t i = 0; i < ndirs && !found; i++) {
		char *p = xasprintf("%s/applications/%s", dirs[i], id);
		if (access(p, R_OK) == 0)
			found = p;
		else
			free(p);
	}

	free_list(dirs, ndirs);
	return found;
}

static void emit_open_with_one(const char *mime, char **seen, char **dirs,
                               size_t ndirs)
{
	for (size_t i = 0; i < ndirs; i++) {
		char *cache = xasprintf("%s/applications/mimeinfo.cache", dirs[i]);
		char *text = slurp(cache);
		free(cache);
		if (!text)
			continue;

		char *want = xasprintf("\n%s=", mime);
		char *hit = strstr(text, want);
		/* The first line is "[MIME Cache]", so a match at offset 0 is
		 * impossible and the leading newline is always present. */
		free(want);

		if (hit) {
			hit += strlen(mime) + 2;
			char *eol = strchr(hit, '\n');
			if (eol)
				*eol = '\0';

			size_t nids = 0;
			char **ids = split(hit, ';', &nids);
			for (size_t k = 0; k < nids; k++) {
				if (!*ids[k])
					continue;

				char *marker = xasprintf("\n%s\n", ids[k]);
				if (strstr(*seen, marker)) {
					free(marker);
					continue;
				}
				char *grown = xasprintf("%s%s\n", *seen, ids[k]);
				free(*seen);
				*seen = grown;
				free(marker);

				char *path = find_desktop(ids[k]);
				if (!path)
					continue;
				char *dtext = slurp(path);
				if (dtext) {
					char *nodisplay = entry_get(dtext, "Desktop Entry", "NoDisplay");
					char *name = entry_get(dtext, "Desktop Entry", "Name");
					char *icon = entry_get(dtext, "Desktop Entry", "Icon");

					/* NoDisplay means "not for menus" — it is how a
					 * helper .desktop hides itself, and showing it
					 * offers the user something nobody meant to. */
					if (name && (!nodisplay || strcmp(nodisplay, "true")))
						action_row("open-with", ids[k], "", name, icon);

					free(nodisplay);
					free(name);
					free(icon);
					free(dtext);
				}
				free(path);
			}
			free(ids);
		}
		free(text);
	}
}

/* mimeinfo.cache is keyed by whatever name each application declared, so an
 * app registered for application/x-cd-image is invisible to a lookup of
 * application/vnd.efi.iso even though they are the same type. Every
 * equivalent name is tried, and the seen-list keeps one application from
 * appearing once per spelling. */
static void emit_open_with(const char *mime)
{
	size_t ndirs = 0;
	char **dirs = data_dirs(&ndirs);

	/* Seen-list, because the same application is listed in both the user's
	 * cache and the system one on any machine where a .desktop was
	 * overridden — and now also once per alias. */
	char *seen = xstrdup("\n");

	char *equiv = mime_equivalents(mime);
	size_t nnames = 0;
	char *copy = xstrdup(equiv);
	char **names = split(copy, '\n', &nnames);
	for (size_t i = 0; i < nnames; i++)
		if (*names[i])
			emit_open_with_one(names[i], &seen, dirs, ndirs);

	free(names);
	free(copy);
	free(equiv);
	free(seen);
	free_list(dirs, ndirs);
}

/* ── service menus ──────────────────────────────────────────────────────── */

static void emit_service_menus(const char *mime)
{
	size_t ndirs = 0;
	char **dirs = data_dirs(&ndirs);

	for (size_t i = 0; i < ndirs; i++) {
		char *dir = xasprintf("%s/kio/servicemenus", dirs[i]);
		DIR *d = opendir(dir);
		if (!d) {
			free(dir);
			continue;
		}

		struct dirent *e;
		while ((e = readdir(d))) {
			const char *dot = strrchr(e->d_name, '.');
			if (!dot || strcmp(dot, ".desktop"))
				continue;

			char *path = xasprintf("%s/%s", dir, e->d_name);
			char *text = slurp(path);
			if (!text) {
				free(path);
				continue;
			}

			char *mimes = entry_get(text, "Desktop Entry", "MimeType");
			if (mime_listed(mimes, mime)) {
				char *actions = entry_get(text, "Desktop Entry", "Actions");
				char *entry_icon = entry_get(text, "Desktop Entry", "Icon");

				if (actions) {
					size_t nact = 0;
					char **ids = split(actions, ';', &nact);
					for (size_t k = 0; k < nact; k++) {
						if (!*ids[k])
							continue;
						char *group = xasprintf("Desktop Action %s", ids[k]);
						char *name = entry_get(text, group, "Name");
						char *icon = entry_get(text, group, "Icon");
						if (name)
							action_row("service", e->d_name, ids[k], name,
							           icon ? icon : entry_icon);
						free(name);
						free(icon);
						free(group);
					}
					free(ids);
				} else {
					/* An entry with no Actions= is itself one action. */
					char *name = entry_get(text, "Desktop Entry", "Name");
					if (name)
						action_row("service", e->d_name, "", name, entry_icon);
					free(name);
				}

				free(actions);
				free(entry_icon);
			}

			free(mimes);
			free(text);
			free(path);
		}

		closedir(d);
		free(dir);
	}

	free_list(dirs, ndirs);
}

int cmd_actions(int argc, char **argv)
{
	if (argc < 1)
		die("actions: need a path");

	/* Every selected file must share a type for an action to apply to the
	 * whole selection — offering "Extract" for a set that is half archives
	 * and half photographs would run it on the photographs too. */
	struct stat st;
	if (lstat(argv[0], &st) != 0)
		die("cannot stat %s: %s", argv[0], strerror(errno));
	bool is_dir = S_ISDIR(st.st_mode);
	const char *first = mime_for(sf_basename(argv[0]), is_dir);
	char *mime = xstrdup(first);

	for (int i = 1; i < argc; i++) {
		struct stat s2;
		if (lstat(argv[i], &s2) != 0)
			continue;
		const char *m = mime_for(sf_basename(argv[i]), S_ISDIR(s2.st_mode));
		if (strcmp(m, mime)) {
			free(mime);
			mime = NULL;
			break;
		}
	}

	action_header();
	if (mime) {
		emit_open_with(mime);
		emit_service_menus(mime);
		free(mime);
	}
	return 0;
}

/* ── running one ────────────────────────────────────────────────────────── */

/* Exec= field codes, per the Desktop Entry spec. %f/%u take one path and %F/%U
 * take the whole list; everything else in the spec is either deprecated or
 * about icons and menu names, and is dropped rather than passed through as a
 * literal "%k" argument.
 *
 * Quoting is NOT done, because nothing is handed to a shell: the argv is built
 * directly and exec'd. That is the whole reason this lives in C — a filename
 * containing a quote or a space is just another byte in an argv slot, with no
 * escaping rule to get wrong. */
static char **build_argv(const char *exec, char **paths, int npaths, size_t *out_n)
{
	size_t cap = 16, n = 0;
	char **argv = xmalloc(cap * sizeof *argv);

	char *copy = xstrdup(exec);
	size_t nwords = 0;
	char **words = split(copy, ' ', &nwords);

	for (size_t i = 0; i < nwords; i++) {
		char *w = words[i];
		if (!*w)
			continue;

		/* Strip the quotes the spec allows around an argument; the shell
		 * that would have removed them is not in this picture. */
		size_t wl = strlen(w);
		if (wl >= 2 && ((w[0] == '"' && w[wl - 1] == '"')
		                || (w[0] == '\'' && w[wl - 1] == '\''))) {
			w[wl - 1] = '\0';
			w++;
		}

		bool many = !strcmp(w, "%F") || !strcmp(w, "%U");
		bool one  = !strcmp(w, "%f") || !strcmp(w, "%u");

		if (many || one) {
			int take = many ? npaths : (npaths > 0 ? 1 : 0);
			for (int p = 0; p < take; p++) {
				if (n + 2 >= cap) {
					cap *= 2;
					argv = xrealloc(argv, cap * sizeof *argv);
				}
				argv[n++] = xstrdup(paths[p]);
			}
			continue;
		}

		/* Any other field code is dropped. */
		if (w[0] == '%' && wl == 2)
			continue;

		if (n + 2 >= cap) {
			cap *= 2;
			argv = xrealloc(argv, cap * sizeof *argv);
		}
		argv[n++] = xstrdup(w);
	}

	free(words);
	free(copy);

	argv[n] = NULL;
	*out_n = n;
	return argv;
}

static char *find_servicemenu(const char *name)
{
	size_t ndirs = 0;
	char **dirs = data_dirs(&ndirs);

	char *found = NULL;
	for (size_t i = 0; i < ndirs && !found; i++) {
		char *p = xasprintf("%s/kio/servicemenus/%s", dirs[i], name);
		if (access(p, R_OK) == 0)
			found = p;
		else
			free(p);
	}

	free_list(dirs, ndirs);
	return found;
}

int cmd_action(int argc, char **argv)
{
	if (argc < 2)
		die("action: need a .desktop file and a path\n"
		    "  usage: synfiles action <file.desktop> [action-id] -- <path>...");

	const char *desktop = argv[0];

	/* A basename, never a path. Accepting "../../etc/something" would let a
	 * caller point this at any file on the system and run whatever Exec= it
	 * found there. */
	if (strchr(desktop, '/'))
		die("action: '%s' must be a .desktop NAME, not a path", desktop);

	int i = 1;
	const char *action_id = "";
	if (strcmp(argv[i], "--")) {
		action_id = argv[i];
		i++;
	}
	if (i < argc && !strcmp(argv[i], "--"))
		i++;

	if (i >= argc)
		die("action: need at least one path");

	char **paths = argv + i;
	int npaths = argc - i;

	/* A service menu first, then an application — the two live in different
	 * directories and only the caller knows which it asked for, so try both
	 * rather than making the GUI say. */
	char *path = find_servicemenu(desktop);
	if (!path)
		path = find_desktop(desktop);
	if (!path)
		die("action: no such desktop entry: %s", desktop);

	char *text = slurp(path);
	if (!text)
		die("action: cannot read %s", path);

	char *group = *action_id ? xasprintf("Desktop Action %s", action_id)
	                         : xstrdup("Desktop Entry");
	char *exec = entry_get(text, group, "Exec");
	char *term = entry_get(text, "Desktop Entry", "Terminal");
	free(group);

	if (!exec)
		die("action: %s has no Exec for '%s'", desktop,
		    *action_id ? action_id : "Desktop Entry");

	size_t n = 0;
	char **child = build_argv(exec, paths, npaths, &n);
	if (n == 0)
		die("action: %s has an empty Exec", desktop);

	if (g_verbose) {
		fprintf(stderr, "synfiles: exec");
		for (size_t k = 0; k < n; k++)
			fprintf(stderr, " %s", child[k]);
		fputc('\n', stderr);
	}

	/* Terminal=true means the entry expects a terminal to have been provided.
	 * Rather than guess at one, say so — a helper that silently did nothing
	 * because it printed into a void is worse than one that explains. */
	if (term && !strcmp(term, "true") && !getenv("SYNFILES_IN_TERMINAL"))
		warn("%s wants a terminal; running it without one", desktop);

	free(term);
	free(exec);
	free(text);
	free(path);

	/* Detached: the file manager must not sit waiting for GIMP to close.
	 *
	 * ⚠ And detached from this process's STDIO, which is the harder half.
	 * setsid() alone is not enough. Launched from the GUI, stdout and stderr
	 * are pipes owned by quickshell's Process, and quickshell closes its read
	 * ends as soon as THIS process exits — which is immediately, that being
	 * the whole point. The application we just started then writes its first
	 * line of startup chatter into a pipe with no reader and is killed by
	 * SIGPIPE before it ever maps a window.
	 *
	 * That is exactly how "Open with Text Editor" read as a dead menu entry:
	 * correct argv, exit status 0, nothing in any log, no window. It worked
	 * perfectly from a terminal, where stderr is a tty that nobody closes.
	 *
	 * The rule: a launcher hands its child NOTHING the caller owns. */
	int errfd = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 10);

	pid_t pid = fork();
	if (pid < 0)
		die("fork: %s", strerror(errno));
	if (pid == 0) {
		setsid();

		int null = open("/dev/null", O_RDWR);
		if (null >= 0) {
			dup2(null, STDIN_FILENO);
			dup2(null, STDOUT_FILENO);
			dup2(null, STDERR_FILENO);
			if (null > STDERR_FILENO)
				close(null);
		}

		execvp(child[0], child);

		/* Reached only when Exec= names something that is not installed.
		 * errfd is close-on-exec, so it exists ONLY on this path and is
		 * never inherited by an application that actually started — which
		 * is the point, since it is the very descriptor the comment above
		 * says not to pass on. Silence here is how a missing helper used
		 * to look identical to a working one. */
		if (errfd >= 0) {
			char msg[256];
			int len = snprintf(msg, sizeof msg,
			                   "synfiles: cannot run %s: %s\n",
			                   child[0], strerror(errno));
			if (len > 0 && write(errfd, msg, (size_t)len) < 0) {
				/* The caller is already gone. Nothing left to
				 * say, and nowhere left to say it. */
			}
		}
		_exit(127);
	}

	if (errfd >= 0)
		close(errfd);

	if (g_out == OUT_REC)
		rec_row(2, "started", child[0]);
	return 0;
}
