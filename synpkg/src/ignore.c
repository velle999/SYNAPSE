/* ignore.c — holding a package back, and letting it go again.
 *
 * WHERE THE LIST LIVES, AND WHY IT IS NOT OURS
 *
 * In /etc/pacman.conf, as IgnorePkg. Not in synpkg.conf beside the other
 * preferences, even though that file exists and needs no root.
 *
 * Because an ignore that only synpkg honoured would be a lie. The machine has
 * pacman on it. Somebody who holds a package back here and then runs
 * `pacman -Syu` — or `yay`, or anything else built on libalpm — must not have
 * it upgraded out from under them by the tool they did not think to configure.
 * IgnorePkg is the mechanism every one of those already reads, and synpkg has
 * read it since the beginning (alpmctx.c feeds it into libalpm). This file
 * only adds the ability to WRITE what was already being obeyed.
 *
 * The cost is that changing it needs root, which is why `ignore` and
 * `unignore` escalate the same way install and remove do.
 *
 * WHAT SYNPKG OWNS
 *
 * IgnorePkg accumulates across lines and across Include files, so a package
 * can be ignored by a line synpkg did not write: a hand-edited pacman.conf, a
 * glob like `linux*`, or a drop-in from somewhere else. synpkg owns exactly
 * ONE line, marked by the comment above it, and never rewrites anybody else's.
 *
 * That distinction has to reach the user, because the alternative is
 * `synpkg unignore foo` printing success and changing nothing. Removal checks
 * both lists: gone from ours, still in pacman's resolved answer, means the
 * ignore came from elsewhere and we say so.
 *
 * ⚠ pacman.conf HAS NO TRAILING COMMENTS. `IgnorePkg = foo # mine` does not
 * mean "foo, annotated" — pacman splits the whole line on whitespace and takes
 * `#`, and `mine`, as package names. The marker is therefore a comment on a
 * LINE OF ITS OWN, and anything this file writes onto a directive line is a
 * package name whether it looks like one or not.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"
#include "i18n.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The marker. Matched by prefix, so the human-readable tail can be reworded in
 * a later release without orphaning the line it labels on machines that
 * already have one. */
#define IGN_MARK "#synpkg-ignore"
#define IGN_MARK_LINE \
	IGN_MARK " — held back by `synpkg ignore`. `synpkg ignore` lists them.\n"

/* The file synpkg WRITES. Reads go through pacman-conf, which resolves Include
 * globs and would be wrong to duplicate here; but a writer has to name a real
 * file, and pacman's own default is this one.
 *
 * The environment variable is a test seam and is documented as one. It must
 * agree with whatever pacman-conf is answering from, which is why the suite
 * shims pacman-conf on PATH rather than pointing only this at a fixture — the
 * two halves disagreeing is precisely the bug it would hide. */
static const char *conf_file(void)
{
	const char *p = getenv("SYNPKG_PACMAN_CONF");
	return (p && *p) ? p : "/etc/pacman.conf";
}

/* Whether this process can do the write itself.
 *
 * ⚠ THE TEST IS THE FILE, NOT THE UID, and that is not a style preference: a
 * uid test here was a bug that edited the wrong file on a live machine.
 *
 * pkexec SANITISES THE ENVIRONMENT. SYNPKG_PACMAN_CONF does not survive it, so
 * an unprivileged run pointed at a fixture escalated, and the root child —
 * having lost the variable — fell back to the default and wrote
 * /etc/pacman.conf instead. It reported success, because from the child's point
 * of view nothing was wrong. Asking whether the TARGET is writable makes a
 * fixture in a temp directory need no escalation at all, so the question never
 * arises; and it is the honest question anyway, because the requirement was
 * never "be root", it was "be able to write this file".
 *
 * The refusal below closes the rest of it — any path where a redirected config
 * would still have to escalate is refused outright rather than silently
 * becoming a write to the system file. */
static bool can_write_conf(void)
{
	return access(conf_file(), W_OK) == 0;
}

/* Escalate, unless doing so would change WHICH FILE gets written. */
static int ignore_escalate(const char *verb, int argc, char **argv)
{
	const char *redirect = getenv("SYNPKG_PACMAN_CONF");
	if (redirect && *redirect)
		die(_("SYNPKG_PACMAN_CONF points at %s, which this user cannot write.\n"
		    "  pkexec would not carry that setting to the root process, so the\n"
		    "  escalated write would land on /etc/pacman.conf instead. Re-run as\n"
		    "  a user who can write the file you named."), redirect);
	return escalate(verb, argc, argv);
}

/* ── reading ────────────────────────────────────────────────────────────── */

/* Everything pacman considers ignored, from every line and every Include. */
size_t sp_ignore_list(char ***out)
{
	char *raw = pconf("IgnorePkg");
	size_t n = 0;
	char **fields = split(raw, '\n', &n);

	char **list = xmalloc((n ? n : 1) * sizeof *list);
	size_t kept = 0;
	for (size_t i = 0; i < n; i++)
		if (*fields[i])
			list[kept++] = xstrdup(fields[i]);

	free(fields);
	free(raw);
	*out = list;
	return kept;
}

/* Cached for the life of the process.
 *
 * ⚠ sp_ignore_list() FORKS pacman-conf. The AUR pass asks this question once
 * per foreign package while deciding what to rebuild, so the uncached version
 * was one subprocess per package on a machine that may have dozens — an
 * invisible cost, paid on the slowest command in the program.
 *
 * Safe to cache because nothing inside one synpkg run edits IgnorePkg while
 * also reading it: `ignore` and `unignore` write and exit, and every reader is
 * a listing. The one path that does both is cmd_ignore's typo warning, which
 * runs BEFORE the rewrite. */
static char **cached_ign;
static size_t cached_n;
static bool   cached_done;

bool sp_ignore_has(const char *name)
{
	if (!cached_done) {
		cached_n = sp_ignore_list(&cached_ign);
		cached_done = true;
	}
	for (size_t i = 0; i < cached_n; i++)
		if (!strcmp(cached_ign[i], name))
			return true;
	return false;
}

/* ── the line synpkg owns ───────────────────────────────────────────────── */

/* Read the whole config. NULL when it cannot be read at all, which is a
 * different answer from an empty one and is reported as such. */
static char *slurp(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return NULL;

	size_t cap = 8192, len = 0;
	char *buf = xmalloc(cap);
	size_t got;
	while ((got = fread(buf + len, 1, cap - len - 1, f)) > 0) {
		len += got;
		if (len + 1 >= cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
	}
	buf[len] = '\0';
	fclose(f);
	return buf;
}

static bool is_mark(const char *line)
{
	while (*line && isspace((unsigned char)*line))
		line++;
	return !strncmp(line, IGN_MARK, strlen(IGN_MARK));
}

/* Does this line set IgnorePkg? Leading space allowed, `=` optional — pacman
 * accepts `IgnorePkg = a b`, `IgnorePkg=a b` and `IgnorePkg a b` alike, so a
 * matcher that only knew the first form would miss a line and then write a
 * second one that silently unions with it. */
static const char *ignorepkg_value(const char *line)
{
	while (*line && isspace((unsigned char)*line))
		line++;
	if (strncasecmp(line, "IgnorePkg", 9))
		return NULL;
	line += 9;
	while (*line && isspace((unsigned char)*line))
		line++;
	if (*line == '=')
		line++;
	while (*line && isspace((unsigned char)*line))
		line++;
	return line;
}

/* Is this line a section header, and if so which? Returns a malloc'd name. */
static char *section_of(const char *line)
{
	while (*line && isspace((unsigned char)*line))
		line++;
	if (*line != '[')
		return NULL;
	const char *end = strchr(line, ']');
	if (!end)
		return NULL;
	char *s = xmalloc((size_t)(end - line));
	memcpy(s, line + 1, (size_t)(end - line - 1));
	s[end - line - 1] = '\0';
	return s;
}

/* The names on synpkg's own line. Empty when there is no such line. */
static size_t owned_names(const char *conf, char ***out)
{
	char **list = xmalloc(sizeof *list);
	size_t n = 0, cap = 1;

	char *copy = xstrdup(conf);
	size_t nlines = 0;
	char **lines = split(copy, '\n', &nlines);

	for (size_t i = 0; i + 1 < nlines; i++) {
		if (!is_mark(lines[i]))
			continue;
		const char *val = ignorepkg_value(lines[i + 1]);
		if (!val)
			continue;
		char *vcopy = xstrdup(val);
		for (char *tok = strtok(vcopy, " \t"); tok; tok = strtok(NULL, " \t")) {
			if (n == cap)
				list = xrealloc(list, (cap *= 2) * sizeof *list);
			list[n++] = xstrdup(tok);
		}
		free(vcopy);
		break;
	}

	free(lines);
	free(copy);
	*out = list;
	return n;
}

/* Rewrite the config with synpkg's marker line and its IgnorePkg line replaced
 * by `names` (removed entirely when there are none).
 *
 * ⚠ The new line goes at the END of [options], not at the top of the file and
 * not wherever the old one was if that was somehow outside a section. An
 * IgnorePkg under a repository header is not an error pacman reports — it is a
 * directive in the wrong scope, silently doing nothing. */
static int conf_rewrite(char **names, size_t n)
{
	const char *path = conf_file();
	char *conf = slurp(path);
	if (!conf)
		die(_("cannot read %s: %s"), path, strerror(errno));

	/* Where [options] ends: the line before the next section header, or the
	 * end of the file. */
	size_t nlines = 0;
	char *copy = xstrdup(conf);
	char **lines = split(copy, '\n', &nlines);

	bool in_options = false, seen_options = false;
	size_t opt_end = 0;
	for (size_t i = 0; i < nlines; i++) {
		char *sec = section_of(lines[i]);
		if (sec) {
			if (in_options)
				opt_end = i;      /* first line PAST the section */
			in_options = !strcmp(sec, "options");
			if (in_options)
				seen_options = true;
			free(sec);
			continue;
		}
		if (in_options)
			opt_end = i + 1;
	}
	if (!seen_options) {
		free(lines); free(copy); free(conf);
		die(_("%s has no [options] section — refusing to guess where an "
		    "IgnorePkg belongs"), path);
	}

	/* Build the replacement, dropping the old marker and the line under it. */
	char *out = xstrdup("");
	for (size_t i = 0; i < nlines; i++) {
		/* split() consumed the newlines; the last field is the tail after the
		 * final one and is empty for a file that ends properly. */
		bool last = (i + 1 == nlines);
		if (last && !*lines[i])
			break;

		if (is_mark(lines[i]) && i + 1 < nlines && ignorepkg_value(lines[i + 1])) {
			i++;               /* skip the marker AND the directive under it */
			continue;
		}

		if (i == opt_end && n > 0) {
			char *j = xasprintf("%s%s", out, IGN_MARK_LINE);
			free(out);
			out = j;
			char *val = xstrdup("IgnorePkg =");
			for (size_t k = 0; k < n; k++) {
				char *v2 = xasprintf("%s %s", val, names[k]);
				free(val);
				val = v2;
			}
			j = xasprintf("%s%s\n", out, val);
			free(out); free(val);
			out = j;
		}

		char *j = xasprintf("%s%s\n", out, lines[i]);
		free(out);
		out = j;
	}
	/* [options] running to the end of the file: the insertion point above is
	 * never reached, because there is no line at index opt_end. */
	if (opt_end >= nlines && n > 0) {
		char *val = xstrdup("IgnorePkg =");
		for (size_t k = 0; k < n; k++) {
			char *v2 = xasprintf("%s %s", val, names[k]);
			free(val);
			val = v2;
		}
		char *j = xasprintf("%s%s%s\n", out, IGN_MARK_LINE, val);
		free(out); free(val);
		out = j;
	}

	free(lines);
	free(copy);
	free(conf);

	/* Temp file BESIDE the original, so the rename is on one filesystem and is
	 * therefore atomic. A half-written pacman.conf is worse than no change at
	 * all, because it parses: a truncated file loses every repository below
	 * the cut and the next upgrade cheerfully proceeds without them. */
	char *tmp = xasprintf("%s.synpkg-new", path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		int e = errno;
		free(tmp); free(out);
		die(_("cannot write beside %s: %s"), path, strerror(e));
	}
	fputs(out, f);

	/* 0644, which is what pacman.conf is. fopen made it 0600-ish under root's
	 * umask, and a pacman.conf nobody but root can read breaks every
	 * unprivileged `pacman -Ss` and `pacman-conf` on the machine — including
	 * synpkg's own reads.
	 *
	 * ⚠ fchmod ON THE DESCRIPTOR, never chmod on the name. This runs as root
	 * and writes into /etc: re-resolving the name after opening it is a window
	 * in which the path can become something else, and the something else then
	 * gets the mode. The descriptor is already the file we wrote; there is no
	 * second lookup to lose a race with. */
	if (fchmod(fileno(f), 0644) != 0)
		warn(_("could not set the mode on the new %s"), path);

	if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
		fclose(f); unlink(tmp);
		free(tmp); free(out);
		die(_("cannot flush the new %s"), path);
	}
	fclose(f);
	free(out);

	if (rename(tmp, path) != 0) {
		int e = errno;
		unlink(tmp);
		free(tmp);
		die(_("cannot replace %s: %s"), path, strerror(e));
	}
	free(tmp);
	return 0;
}

/* ── the commands ───────────────────────────────────────────────────────── */

static bool in_list(char **list, size_t n, const char *name)
{
	for (size_t i = 0; i < n; i++)
		if (!strcmp(list[i], name))
			return true;
	return false;
}

/* What `ignore` with no arguments prints: everything held back, whether it has
 * an update waiting, and where the ignore came from.
 *
 * The update column is the point of the whole listing. "linux is ignored" is
 * not what somebody coming back to this wants to know — they want to know
 * whether they are holding back anything, and a row with no pending version is
 * a hold that is currently costing nothing. */
static int ignore_show(void)
{
	char **all = NULL;
	size_t n = sp_ignore_list(&all);

	char *conf = slurp(conf_file());
	char **ours = NULL;
	size_t n_ours = conf ? owned_names(conf, &ours) : 0;

	alpm_handle_t *h = sp_alpm_init(false);
	alpm_list_t *syncdbs = sp_syncdbs(h);
	alpm_db_t *local = alpm_get_localdb(h);

	if (g_out == OUT_TSV)
		tsv_row(5, "name", "installed_version", "new_version", "source", "installed");

	for (size_t i = 0; i < n; i++) {
		alpm_pkg_t *old = alpm_db_get_pkg(local, all[i]);
		const char *iv = old ? alpm_pkg_get_version(old) : "";
		const char *nv = "";
		if (old) {
			alpm_pkg_t *new = alpm_sync_get_new_version(old, syncdbs);
			if (new)
				nv = alpm_pkg_get_version(new);
		}
		const char *src = in_list(ours, n_ours, all[i]) ? "synpkg" : "pacman.conf";

		if (g_out == OUT_TSV) {
			tsv_row(5, all[i], iv, nv, src, old ? "1" : "0");
		} else if (!old) {
			/* Ignoring something not installed is legal and is how you stop a
			 * package arriving as a dependency. Saying so beats a blank row. */
			printf("  %s%-30s%s %snot installed%s\n", C_BOLD(), all[i], C_RESET(),
			       C_DIM(), C_RESET());
		} else if (*nv) {
			printf("  %s%-30s%s %s%s%s -> %s%s%s %sheld%s\n", C_BOLD(), all[i],
			       C_RESET(), C_DIM(), iv, C_RESET(), C_WARN(), nv, C_RESET(),
			       C_WARN(), C_RESET());
		} else {
			printf("  %s%-30s%s %s%s (no update waiting)%s\n", C_BOLD(), all[i],
			       C_RESET(), C_DIM(), iv, C_RESET());
		}
	}

	/* Flatpak, from its own mechanism.
	 *
	 * ONE listing over THREE sources, because "what am I holding back" is one
	 * question and the user asking it does not care that repositories, the AUR
	 * and Flathub are held by two different files and a third program. The
	 * source column is what tells them which command releases it.
	 *
	 * The SynapseOS components are the third, and they are NOT here: they are
	 * held in syn-update's manifest and released with `syn-update unignore`.
	 * They are pointed at rather than merged in, because listing them would
	 * mean shelling out to syn-update on every `synpkg ignore` — and because a
	 * component's hold is answered by a different command, which a merged list
	 * would obscure. */
	char **masks = NULL;
	size_t n_masks = sp_flatpak_masks(&masks);
	for (size_t i = 0; i < n_masks; i++) {
		if (g_out == OUT_TSV)
			tsv_row(5, masks[i], "", "", "flatpak", "1");
		else
			printf("  %s%-30s%s %sflatpak%s\n", C_BOLD(), masks[i], C_RESET(),
			       C_DIM(), C_RESET());
	}

	if (g_out == OUT_HUMAN) {
		if (!n && !n_masks) {
			printf("%snothing is being held back%s\n", C_OK(), C_RESET());
			printf("  %shold a package with: synpkg ignore <package>%s\n",
			       C_DIM(), C_RESET());
			printf("  %sor a SynapseOS component with: syn-update ignore <component>%s\n",
			       C_DIM(), C_RESET());
		} else {
			printf("\n%zu package%s held back\n", n + n_masks,
			       (n + n_masks) == 1 ? "" : "s");
			printf("  %srelease one with: synpkg unignore <package>%s\n",
			       C_DIM(), C_RESET());
			if (n_masks)
				printf("  %sa flatpak row: synpkg flatpak unignore <application>%s\n",
				       C_DIM(), C_RESET());
			/* Only worth saying when there IS one, and it explains in advance
			 * why `unignore` may decline to act on that row. */
			for (size_t i = 0; i < n; i++) {
				if (in_list(ours, n_ours, all[i]))
					continue;
				printf("  %ssome rows say pacman.conf: those were not written by "
				       "synpkg and it will not edit them%s\n", C_DIM(), C_RESET());
				break;
			}
		}
	}

	/* Named, not queried. A pointer costs nothing and is right even when
	 * syn-update is not installed; running it here would make every listing
	 * pay for a component scan it was not asked for. */
	if (g_out == OUT_HUMAN && have_cmd("syn-update"))
		printf("  %sSynapseOS components are held separately: syn-update ignored%s\n",
		       C_DIM(), C_RESET());

	sp_alpm_free(h);
	pconf_free_list(masks, n_masks);
	pconf_free_list(ours, n_ours);
	pconf_free_list(all, n);
	free(conf);
	/* Same convention as `updates`: 100 is "nothing", so a poller branches
	 * without parsing. */
	return (n || n_masks) ? 0 : 100;
}

int cmd_ignore(int argc, char **argv)
{
	if (argc == 0)
		return ignore_show();

	if (!can_write_conf())
		return ignore_escalate("ignore", argc, argv);

	char *conf = slurp(conf_file());
	if (!conf)
		die(_("cannot read %s: %s"), conf_file(), strerror(errno));

	char **ours = NULL;
	size_t n_ours = owned_names(conf, &ours);

	/* Grown from ours, so the rewrite preserves what is already held. */
	size_t cap = n_ours + (size_t)argc + 1;
	char **want = xmalloc(cap * sizeof *want);
	size_t n_want = 0;
	for (size_t i = 0; i < n_ours; i++)
		want[n_want++] = xstrdup(ours[i]);

	int added = 0;
	for (int i = 0; i < argc; i++) {
		if (in_list(want, n_want, argv[i])) {
			info("%s is already held back", argv[i]);
			continue;
		}
		/* Ignoring a name nothing knows about is almost always a typo, and the
		 * result is an entry that sits in pacman.conf forever doing nothing.
		 * Warned, not refused: pre-ignoring a package before installing it is
		 * a real thing to want. */
		if (!sp_ignore_has(argv[i])) {
			alpm_handle_t *h = sp_alpm_init(false);
			bool known = alpm_db_get_pkg(alpm_get_localdb(h), argv[i]) != NULL;
			if (!known) {
				for (alpm_list_t *d = sp_syncdbs(h); d && !known; d = d->next)
					known = alpm_db_get_pkg(d->data, argv[i]) != NULL;
			}
			sp_alpm_free(h);
			if (!known)
				warn(_("no package called '%s' is installed or in any repository"), argv[i]);
		}
		want[n_want++] = xstrdup(argv[i]);
		added++;
	}

	int rc = 0;
	if (added) {
		rc = conf_rewrite(want, n_want);
		if (rc == 0)
			info("held back %d package%s — `synpkg ignore` lists them",
			     added, added == 1 ? "" : "s");
	}

	pconf_free_list(want, n_want);
	pconf_free_list(ours, n_ours);
	free(conf);
	return rc;
}

int cmd_unignore(int argc, char **argv)
{
	if (argc == 0)
		die(_("unignore: name a package. `synpkg ignore` lists what is held back"));

	if (!can_write_conf())
		return ignore_escalate("unignore", argc, argv);

	char *conf = slurp(conf_file());
	if (!conf)
		die(_("cannot read %s: %s"), conf_file(), strerror(errno));

	char **ours = NULL;
	size_t n_ours = owned_names(conf, &ours);

	char **keep = xmalloc((n_ours ? n_ours : 1) * sizeof *keep);
	size_t n_keep = 0;
	int removed = 0, elsewhere = 0;

	for (size_t i = 0; i < n_ours; i++) {
		bool drop = false;
		for (int a = 0; a < argc && !drop; a++)
			drop = !strcmp(ours[i], argv[a]);
		if (drop)
			removed++;
		else
			keep[n_keep++] = xstrdup(ours[i]);
	}

	/* Named but not ours. Reported per name and NOT counted as success: the
	 * whole failure this guards against is printing "released" and changing
	 * nothing, leaving somebody to discover at the next upgrade that the
	 * package is still pinned. */
	for (int a = 0; a < argc; a++) {
		if (in_list(ours, n_ours, argv[a]))
			continue;
		if (sp_ignore_has(argv[a])) {
			warn(_("%s is held back by a line synpkg did not write — edit %s by hand"),
			     argv[a], conf_file());
			elsewhere++;
		} else {
			info("%s was not being held back", argv[a]);
		}
	}

	int rc = 0;
	if (removed) {
		rc = conf_rewrite(keep, n_keep);
		if (rc == 0)
			info("released %d package%s — the next upgrade will offer %s",
			     removed, removed == 1 ? "" : "s", removed == 1 ? "it" : "them");
	}

	pconf_free_list(keep, n_keep);
	pconf_free_list(ours, n_ours);
	free(conf);
	/* A request that reached nothing it could act on is a failure, even though
	 * no step of it errored. */
	return rc ? rc : (removed ? 0 : (elsewhere ? 1 : 0));
}
