/* query.c — everything that only reads the databases.
 *
 * All of these run unprivileged: the local and sync databases are world
 * readable, so the GUI never has to escalate merely to draw a list.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── shared row renderer ────────────────────────────────────────────────── */

void emit_pkg_header(void)
{
	if (g_out == OUT_TSV)
		tsv_row(6, "name", "installed", "version", "repo", "size", "description");
}

void emit_pkg(const char *name, bool installed, const char *version,
              const char *repo, off_t size, const char *desc)
{
	if (g_out == OUT_TSV) {
		char *sz = xasprintf("%lld", (long long)size);
		tsv_row(6, name, installed ? "1" : "0", version ? version : "",
		        repo ? repo : "", sz, desc ? desc : "");
		free(sz);
		return;
	}

	printf("%s%s/%s%s%s %s%s%s", C_DIM(), repo ? repo : "?", C_RESET(),
	       C_BOLD(), name, C_ACCENT(), version ? version : "", C_RESET());
	if (installed)
		printf(" %s[installed]%s", C_OK(), C_RESET());
	putchar('\n');
	if (desc && *desc)
		printf("    %s\n", desc);
}

/* A package's "repo" is its db name; for a local-only package there is no sync
 * db and pacman shows "local". Callers get the same string either way. */
static const char *pkg_repo(alpm_pkg_t *pkg)
{
	alpm_db_t *db = alpm_pkg_get_db(pkg);
	return db ? alpm_db_get_name(db) : "local";
}

static bool is_installed(alpm_handle_t *h, const char *name)
{
	return alpm_db_get_pkg(alpm_get_localdb(h), name) != NULL;
}

/* ── search ─────────────────────────────────────────────────────────────── */

int cmd_search(int argc, char **argv)
{
	bool local_only = false, aur = false;
	alpm_list_t *needles = NULL;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--installed"))
			local_only = true;
		else if (!strcmp(argv[i], "--aur"))
			aur = true;
		else
			needles = alpm_list_add(needles, argv[i]);
	}
	if (!needles)
		die("search: need a search term");

	alpm_handle_t *h = sp_alpm_init(false);
	emit_pkg_header();

	int hits = 0;
	if (local_only) {
		alpm_list_t *res = NULL;
		if (alpm_db_search(alpm_get_localdb(h), needles, &res) == 0) {
			for (alpm_list_t *i = res; i; i = i->next) {
				alpm_pkg_t *p = i->data;
				emit_pkg(alpm_pkg_get_name(p), true, alpm_pkg_get_version(p),
				         pkg_repo(p), alpm_pkg_get_isize(p),
				         alpm_pkg_get_desc(p));
				hits++;
			}
			alpm_list_free(res);
		}
	} else {
		for (alpm_list_t *d = sp_syncdbs(h); d; d = d->next) {
			alpm_db_t *db = d->data;
			alpm_list_t *res = NULL;
			if (alpm_db_search(db, needles, &res) != 0)
				continue;
			for (alpm_list_t *i = res; i; i = i->next) {
				alpm_pkg_t *p = i->data;
				const char *name = alpm_pkg_get_name(p);
				emit_pkg(name, is_installed(h, name), alpm_pkg_get_version(p),
				         alpm_db_get_name(db), alpm_pkg_get_size(p),
				         alpm_pkg_get_desc(p));
				hits++;
			}
			alpm_list_free(res);
		}
	}

	sp_alpm_free(h);

	/* --aur appends real results rather than printing "try this other
	 * command": a flag that only tells you to run something else is a flag
	 * that should not exist. It comes last because it is the slow half — one
	 * network round trip — so the local hits are already on screen.
	 *
	 * Only the first term is sent. The AUR RPC's search takes one string, and
	 * quietly searching for just one of several words the user typed would be
	 * worse than saying so. */
	if (aur) {
		if (needles->next)
			warn("the AUR search takes one term — using '%s'",
			     (const char *)needles->data);
		aur_search_term(needles->data);
	}
	alpm_list_free(needles);

	/* An empty result is not an error — a caller piping this wants exit 0 and
	 * no rows — but a human typing it wants to know nothing matched. */
	if (!hits && !aur && g_out == OUT_HUMAN)
		info("nothing matched");
	return 0;
}

/* ── info ───────────────────────────────────────────────────────────────── */

static void print_list(const char *label, alpm_list_t *list, bool is_dep)
{
	printf("%s%-16s%s", C_DIM(), label, C_RESET());
	if (!list) {
		printf("None\n");
		return;
	}
	for (alpm_list_t *i = list; i; i = i->next) {
		if (is_dep) {
			char *s = alpm_dep_compute_string(i->data);
			printf("%s%s", s, i->next ? "  " : "");
			free(s);
		} else {
			printf("%s%s", (char *)i->data, i->next ? "  " : "");
		}
	}
	putchar('\n');
}

static void print_time(const char *label, alpm_time_t t)
{
	if (!t) {
		printf("%s%-16s%sUnknown\n", C_DIM(), label, C_RESET());
		return;
	}
	char buf[64];
	time_t tt = (time_t)t;
	struct tm tm;
	localtime_r(&tt, &tm);
	strftime(buf, sizeof buf, "%a %d %b %Y %H:%M:%S %Z", &tm);
	printf("%s%-16s%s%s\n", C_DIM(), label, C_RESET(), buf);
}

int cmd_info(int argc, char **argv)
{
	if (argc < 1)
		die("info: need a package name");

	alpm_handle_t *h = sp_alpm_init(false);
	alpm_db_t *local = alpm_get_localdb(h);
	int missing = 0;

	for (int a = 0; a < argc; a++) {
		const char *name = argv[a];
		alpm_pkg_t *pkg = alpm_db_get_pkg(local, name);
		bool installed = pkg != NULL;

		if (!pkg) {
			for (alpm_list_t *d = sp_syncdbs(h); d && !pkg; d = d->next)
				pkg = alpm_db_get_pkg(d->data, name);
		}
		if (!pkg) {
			warn("package '%s' was not found", name);
			missing++;
			continue;
		}

		if (g_out == OUT_TSV) {
			char *isize = human_size(alpm_pkg_get_isize(pkg));
			char *dsize = human_size(alpm_pkg_get_size(pkg));
			tsv_row(9, alpm_pkg_get_name(pkg), installed ? "1" : "0",
			        alpm_pkg_get_version(pkg), pkg_repo(pkg),
			        alpm_pkg_get_desc(pkg),
			        alpm_pkg_get_url(pkg) ? alpm_pkg_get_url(pkg) : "",
			        alpm_pkg_get_packager(pkg) ? alpm_pkg_get_packager(pkg) : "",
			        dsize, isize);
			free(isize);
			free(dsize);
			continue;
		}

		char *isize = human_size(alpm_pkg_get_isize(pkg));
		printf("%s%-16s%s%s%s%s\n", C_DIM(), "Name", C_RESET(), C_BOLD(),
		       alpm_pkg_get_name(pkg), C_RESET());
		printf("%s%-16s%s%s\n", C_DIM(), "Version", C_RESET(),
		       alpm_pkg_get_version(pkg));
		printf("%s%-16s%s%s\n", C_DIM(), "Repository", C_RESET(), pkg_repo(pkg));
		printf("%s%-16s%s%s\n", C_DIM(), "Description", C_RESET(),
		       alpm_pkg_get_desc(pkg) ? alpm_pkg_get_desc(pkg) : "");
		printf("%s%-16s%s%s\n", C_DIM(), "URL", C_RESET(),
		       alpm_pkg_get_url(pkg) ? alpm_pkg_get_url(pkg) : "None");
		print_list("Licences", alpm_pkg_get_licenses(pkg), false);
		print_list("Groups", alpm_pkg_get_groups(pkg), false);
		print_list("Provides", alpm_pkg_get_provides(pkg), true);
		print_list("Depends On", alpm_pkg_get_depends(pkg), true);
		print_list("Optional Deps", alpm_pkg_get_optdepends(pkg), true);
		printf("%s%-16s%s%s\n", C_DIM(), "Installed Size", C_RESET(), isize);
		printf("%s%-16s%s%s\n", C_DIM(), "Packager", C_RESET(),
		       alpm_pkg_get_packager(pkg) ? alpm_pkg_get_packager(pkg) : "Unknown");
		print_time("Build Date", alpm_pkg_get_builddate(pkg));
		if (installed) {
			print_time("Install Date", alpm_pkg_get_installdate(pkg));
			printf("%s%-16s%s%s\n", C_DIM(), "Install Reason", C_RESET(),
			       alpm_pkg_get_reason(pkg) == ALPM_PKG_REASON_EXPLICIT
			           ? "Explicitly installed"
			           : "Installed as a dependency");
		} else {
			printf("%s%-16s%s%s\n", C_DIM(), "Install Reason", C_RESET(),
			       "Not installed");
		}
		free(isize);
		if (a + 1 < argc)
			putchar('\n');
	}

	sp_alpm_free(h);
	return missing ? 1 : 0;
}

/* ── installed ──────────────────────────────────────────────────────────── */

/* This is the view syn-arsenal never had: "what is on this machine, and let me
 * take it off". Filtering by group is what makes it useful for BlackArch —
 * `synpkg installed --group blackarch` is the uninstall list for the arsenal. */
int cmd_installed(int argc, char **argv)
{
	bool explicit_only = false, native_only = false, foreign_only = false;
	const char *group = NULL, *from_repo = NULL;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--explicit"))
			explicit_only = true;
		else if (!strcmp(argv[i], "--native"))
			native_only = true;
		else if (!strcmp(argv[i], "--foreign"))
			foreign_only = true;
		else if (!strcmp(argv[i], "--group") && i + 1 < argc)
			group = argv[++i];
		else if (!strcmp(argv[i], "--repo") && i + 1 < argc)
			from_repo = argv[++i];
		else
			die("installed: unknown argument '%s'", argv[i]);
	}

	/* Both at once is empty by definition, and an empty pane that looks like
	 * "you have nothing installed" is worth an error instead. */
	if (native_only && foreign_only)
		die("installed: --native and --foreign are opposites");

	alpm_handle_t *h = sp_alpm_init(false);
	emit_pkg_header();

	for (alpm_list_t *i = alpm_db_get_pkgcache(alpm_get_localdb(h)); i; i = i->next) {
		alpm_pkg_t *p = i->data;

		if (explicit_only &&
		    alpm_pkg_get_reason(p) != ALPM_PKG_REASON_EXPLICIT)
			continue;

		/* Group match is a PREFIX so `--group blackarch` covers every
		 * blackarch-* category in one pass; an exact match would force the
		 * caller to enumerate all 50-odd groups. */
		if (group) {
			bool hit = false;
			for (alpm_list_t *g = alpm_pkg_get_groups(p); g && !hit; g = g->next)
				hit = strncmp(g->data, group, strlen(group)) == 0;
			if (!hit)
				continue;
		}

		/* The local db has no repo, so "which repo did this come from" means
		 * "which sync db still offers this name". A package no sync db
		 * offers is FOREIGN: built from the AUR, built by hand, or from a
		 * repository that has since been disabled. */
		const char *repo = NULL;
		for (alpm_list_t *d = sp_syncdbs(h); d; d = d->next) {
			if (alpm_db_get_pkg(d->data, alpm_pkg_get_name(p))) {
				repo = alpm_db_get_name(d->data);
				break;
			}
		}
		if (native_only && !repo)
			continue;
		if (foreign_only && repo)
			continue;
		if (!repo)
			repo = "local";
		if (from_repo && strcmp(repo, from_repo))
			continue;

		emit_pkg(alpm_pkg_get_name(p), true, alpm_pkg_get_version(p), repo,
		         alpm_pkg_get_isize(p), alpm_pkg_get_desc(p));
	}

	sp_alpm_free(h);
	return 0;
}

/* ── updates ────────────────────────────────────────────────────────────── */

/* Reports what the CURRENT sync databases offer. It does not refresh them —
 * refreshing needs root, and a read-only "what's new" that silently demands a
 * password is the thing that makes people stop checking. `synpkg refresh`
 * (or any upgrade) does the -Sy half. */
int cmd_updates(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	alpm_handle_t *h = sp_alpm_init(false);
	alpm_list_t *syncdbs = sp_syncdbs(h);

	if (g_out == OUT_TSV)
		tsv_row(5, "name", "installed_version", "new_version", "repo", "size");

	int n = 0;
	off_t total = 0;
	for (alpm_list_t *i = alpm_db_get_pkgcache(alpm_get_localdb(h)); i; i = i->next) {
		alpm_pkg_t *old = i->data;
		alpm_pkg_t *new = alpm_sync_get_new_version(old, syncdbs);
		if (!new)
			continue;

		off_t size = alpm_pkg_get_size(new);
		total += size;
		n++;

		if (g_out == OUT_TSV) {
			char *sz = xasprintf("%lld", (long long)size);
			tsv_row(5, alpm_pkg_get_name(old), alpm_pkg_get_version(old),
			        alpm_pkg_get_version(new), pkg_repo(new), sz);
			free(sz);
		} else {
			printf("%s%-30s%s %s%s%s -> %s%s%s\n", C_BOLD(),
			       alpm_pkg_get_name(old), C_RESET(), C_DIM(),
			       alpm_pkg_get_version(old), C_RESET(), C_ACCENT(),
			       alpm_pkg_get_version(new), C_RESET());
		}
	}

	if (g_out == OUT_HUMAN) {
		if (!n) {
			printf("%severything is up to date%s\n", C_OK(), C_RESET());
		} else {
			char *sz = human_size(total);
			printf("\n%d package%s to upgrade, %s to download\n", n,
			       n == 1 ? "" : "s", sz);
			free(sz);
		}
	}

	sp_alpm_free(h);
	/* Exit 0 with updates, 100 with none — a cron/bar poller wants to branch
	 * on this without parsing. Chosen well outside pacman's own exit codes. */
	return n ? 0 : 100;
}

/* ── orphans ────────────────────────────────────────────────────────────── */

int cmd_orphans(int argc, char **argv)
{
	bool do_remove = false;
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--remove"))
			do_remove = true;
		else
			die("orphans: unknown argument '%s'", argv[i]);
	}

	alpm_handle_t *h = sp_alpm_init(false);
	emit_pkg_header();

	int n = 0, argc_out = 0;
	char **names = NULL;
	size_t cap = 0;

	for (alpm_list_t *i = alpm_db_get_pkgcache(alpm_get_localdb(h)); i; i = i->next) {
		alpm_pkg_t *p = i->data;
		if (alpm_pkg_get_reason(p) != ALPM_PKG_REASON_DEPEND)
			continue;

		alpm_list_t *req = alpm_pkg_compute_requiredby(p);
		alpm_list_t *opt = alpm_pkg_compute_optionalfor(p);
		bool orphan = !req && !opt;
		FREELIST(req);
		FREELIST(opt);
		if (!orphan)
			continue;

		emit_pkg(alpm_pkg_get_name(p), true, alpm_pkg_get_version(p), "local",
		         alpm_pkg_get_isize(p), alpm_pkg_get_desc(p));
		n++;

		if (do_remove) {
			if ((size_t)argc_out + 1 >= cap) {
				cap = cap ? cap * 2 : 16;
				names = xrealloc(names, cap * sizeof *names);
			}
			names[argc_out++] = xstrdup(alpm_pkg_get_name(p));
		}
	}
	sp_alpm_free(h);

	if (g_out == OUT_HUMAN && !n)
		printf("%sno orphaned packages%s\n", C_OK(), C_RESET());

	int rc = 0;
	if (do_remove && argc_out) {
		/* Hand the names to the remove path rather than re-deriving them
		 * there: the two must never disagree about what "orphan" means. */
		rc = cmd_remove(argc_out, names);
		for (int i = 0; i < argc_out; i++)
			free(names[i]);
	}
	free(names);
	return rc;
}

/* ── status ─────────────────────────────────────────────────────────────── */

int cmd_status(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	alpm_handle_t *h = sp_alpm_init(false);

	int installed = 0;
	off_t footprint = 0;
	for (alpm_list_t *i = alpm_db_get_pkgcache(alpm_get_localdb(h)); i; i = i->next) {
		installed++;
		footprint += alpm_pkg_get_isize(i->data);
	}

	char *fp = human_size(footprint);

	if (g_out == OUT_TSV) {
		char *cnt = xasprintf("%d", installed);
		tsv_row(4, "packages", cnt, fp, "");
		free(cnt);
		for (alpm_list_t *d = sp_syncdbs(h); d; d = d->next) {
			alpm_db_t *db = d->data;
			int n = alpm_list_count(alpm_db_get_pkgcache(db));
			char *c = xasprintf("%d", n);
			tsv_row(4, "repo", alpm_db_get_name(db), c, n ? "synced" : "unsynced");
			free(c);
		}
	} else {
		printf("%s%-16s%s%d packages, %s on disk\n", C_DIM(), "Installed",
		       C_RESET(), installed, fp);
		for (alpm_list_t *d = sp_syncdbs(h); d; d = d->next) {
			alpm_db_t *db = d->data;
			int n = alpm_list_count(alpm_db_get_pkgcache(db));
			printf("%s%-16s%s%-14s %s%d packages%s%s\n", C_DIM(), "Repository",
			       C_RESET(), alpm_db_get_name(db), C_DIM(), n, C_RESET(),
			       n ? "" : "  — never synced");
		}
	}

	free(fp);
	sp_alpm_free(h);
	return 0;
}
