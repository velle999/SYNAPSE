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

/* ── the super search's row shape ───────────────────────────────────────────
 *
 * The argument for it is in synpkg.h. What is here is the emitter, and the one
 * thing worth pointing at is the guard: the header is written ONCE for the
 * whole run, by whichever emitter is reached first, so the three sources can be
 * called in any order and none of them has to know it went first.
 */
void sp_super_header(void)
{
	static bool done = false;
	if (g_out != OUT_TSV || done)
		return;
	done = true;
	tsv_row(9, "name", "installed", "version", "repo", "size", "description",
	        "title", "votes", "flag");
}

void sp_super_row(const char *name, bool installed, const char *version,
                  const char *repo, off_t size, const char *desc,
                  const char *title, const char *votes, const char *flag)
{
	if (g_out == OUT_TSV) {
		sp_super_header();
		char *sz = xasprintf("%lld", (long long)size);
		tsv_row(9, name, installed ? "1" : "0", version ? version : "",
		        repo ? repo : "", sz, desc ? desc : "",
		        title ? title : "", votes ? votes : "", flag ? flag : "");
		free(sz);
		return;
	}

	/* Human mode keeps each source looking like itself — the repo prefix IS
	 * the label, and it is the same one `synpkg search` has always printed. */
	printf("%s%s/%s%s%s%s", C_DIM(), repo ? repo : "?", C_RESET(),
	       C_BOLD(), title && *title ? title : name, C_RESET());
	if (version && *version)
		printf(" %s%s%s", C_ACCENT(), version, C_RESET());
	if (votes && *votes)
		printf(" %s(%s votes)%s", C_DIM(), votes, C_RESET());
	if (installed)
		printf(" %s[installed]%s", C_OK(), C_RESET());
	if (flag && *flag)
		printf(" %s[%s]%s", C_WARN(), flag, C_RESET());
	putchar('\n');
	/* A Flatpak's id is not its name and is what the command line takes, so
	 * it stays on screen under the title exactly as `flatpak search` shows it. */
	if (title && *title && strcmp(title, name) != 0)
		printf("    %s%s%s\n", C_DIM(), name, C_RESET());
	if (desc && *desc)
		printf("    %s\n", desc);
}

void emit_pkg_header(void)
{
	if (g_super) {
		sp_super_header();
		return;
	}
	if (g_out == OUT_TSV)
		tsv_row(6, "name", "installed", "version", "repo", "size", "description");
}

void emit_pkg(const char *name, bool installed, const char *version,
              const char *repo, off_t size, const char *desc)
{
	/* A repository hit carries no title, no votes and no flag — those three
	 * belong to the other two sources — so the union row is this row with
	 * three empty columns after it. */
	if (g_super) {
		sp_super_row(name, installed, version, repo, size, desc, "", "", "");
		return;
	}
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

/* Is a repository with this name configured? The super search says so when
 * BlackArch is not, because "no results from BlackArch" and "BlackArch was
 * never asked" look identical and only one of them is worth acting on. */
static bool have_repo(alpm_handle_t *h, const char *name)
{
	for (alpm_list_t *d = sp_syncdbs(h); d; d = d->next)
		if (!strcmp(alpm_db_get_name(d->data), name))
			return true;
	return false;
}

int cmd_search(int argc, char **argv)
{
	bool local_only = false, aur = false, all = false;
	alpm_list_t *needles = NULL;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--installed"))
			local_only = true;
		else if (!strcmp(argv[i], "--aur"))
			aur = true;
		/* `--all` is the super search: every source, one table, each row
		 * labelled by where it came from. See the note in synpkg.h. */
		else if (!strcmp(argv[i], "--all"))
			all = true;
		else
			needles = alpm_list_add(needles, argv[i]);
	}
	if (!needles)
		die("search: need a search term");

	/*
	 * ⚠ --all AND --installed ARE OPPOSITES AND SAYING BOTH IS A MISTAKE.
	 * One means "everything this machine could install" and the other "only
	 * what it already has", and silently letting either win would answer a
	 * question nobody asked. --all implies --aur, so the two are not a
	 * conflict — they are the same instruction said twice.
	 */
	if (all && local_only)
		die("search: --all and --installed ask opposite questions");
	if (all) {
		aur    = true;
		g_super = true;
	}

	alpm_handle_t *h = sp_alpm_init(false);
	emit_pkg_header();

	/*
	 * BlackArch is a sync db like any other, so the loop below already
	 * searches it and already labels its rows "blackarch" — but only once the
	 * repository has been added. Until then there is nothing to search and the
	 * result is indistinguishable from "BlackArch has nothing like that",
	 * which is the one failure worth naming out loud on a super search.
	 *
	 * A note and never a failure: a box that does not want 5000 security tools
	 * in its database is not misconfigured.
	 */
	if (all && !have_repo(h, "blackarch") && g_out == OUT_HUMAN)
		info("blackarch is not enabled — run: synpkg blackarch enable");

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
	/*
	 * ⛔ AND curl IS ASKED FOR BEFORE THE AUR HALF RUNS, because aur_search_term()
	 * DIES without it — "curl is required for AUR access" — which is right for
	 * `synpkg search --aur`, where the AUR is the thing that was asked for, and
	 * wrong here, where it is one source of several. A box with no curl would
	 * lose the repository rows it had already printed, to an exit status.
	 */
	if (aur) {
		if (!have_cmd("curl")) {
			if (g_out == OUT_HUMAN)
				warn("curl is not installed — skipping the AUR");
		} else {
			if (needles->next)
				warn("the AUR search takes one term — using '%s'",
				     (const char *)needles->data);
			aur_search_term(needles->data);
		}
	}

	/*
	 * ── Flathub, last, and never fatal ─────────────────────────────────────
	 *
	 * ⚠ EVERY SOURCE IN A SUPER SEARCH FAILS ALONE. flatpak_search() returns
	 * non-zero when no remote is configured and when the appstream index has
	 * never been fetched — both ordinary on a machine that simply does not use
	 * Flatpak. Letting either decide the exit status would make `search --all`
	 * fail on a box where the repositories answered perfectly well, and a GUI
	 * reading the exit code would throw away rows it had already been given.
	 *
	 * ⛔ AND WHEN flatpak IS NOT INSTALLED AT ALL IT DOES NOT RETURN — it calls
	 * flatpak_required(), which dies. This comment used to claim otherwise and
	 * the `(void)` cast below made it look handled, so `search --all` exited 1
	 * on every machine without Flatpak while printing every repository row
	 * first. It failed nowhere on a developer's box, because a developer's box
	 * has flatpak; it failed in a VM, inside synpkg's own suite, on the
	 * assertion written to prevent exactly this.
	 *
	 * Dying is RIGHT for `synpkg flatpak search`, where flatpak is the thing
	 * that was asked for. It is wrong here, so the question is asked first.
	 *
	 * Ordered fast to slow on purpose — local databases, then one AUR round
	 * trip, then flatpak's own search — so the rows a human is most likely to
	 * want are printed while the network halves are still working.
	 */
	if (all && sp_flatpak_present())
		(void)flatpak_search((const char *)needles->data);

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

	/* Held-back packages are LISTED AND MARKED, not filtered out.
	 *
	 * Hiding them would be the obvious reading of "ignore", and it is the
	 * wrong one: an update you are holding back is the single thing you most
	 * need to be reminded of, because the reason for the hold was almost
	 * always temporary — a regression to wait out, a rebuild to schedule. A
	 * list that silently omits them is how a package stays pinned for a year.
	 *
	 * They do not count towards the total or the download size either, because
	 * the upgrade is not going to fetch them. */
	char **held = NULL;
	size_t n_held = sp_ignore_list(&held);

	if (g_out == OUT_TSV)
		tsv_row(6, "name", "installed_version", "new_version", "repo", "size",
		        "ignored");

	int n = 0, n_ignored = 0;
	off_t total = 0;
	for (alpm_list_t *i = alpm_db_get_pkgcache(alpm_get_localdb(h)); i; i = i->next) {
		alpm_pkg_t *old = i->data;
		alpm_pkg_t *new = alpm_sync_get_new_version(old, syncdbs);
		if (!new)
			continue;

		const char *name = alpm_pkg_get_name(old);
		bool ignored = false;
		for (size_t k = 0; k < n_held && !ignored; k++)
			ignored = !strcmp(held[k], name);

		off_t size = alpm_pkg_get_size(new);
		if (ignored) {
			n_ignored++;
		} else {
			total += size;
			n++;
		}

		if (g_out == OUT_TSV) {
			char *sz = xasprintf("%lld", (long long)size);
			tsv_row(6, name, alpm_pkg_get_version(old),
			        alpm_pkg_get_version(new), pkg_repo(new), sz,
			        ignored ? "1" : "0");
			free(sz);
		} else if (ignored) {
			printf("%s%-30s%s %s%s%s -> %s%s%s %s(held back)%s\n", C_DIM(),
			       name, C_RESET(), C_DIM(),
			       alpm_pkg_get_version(old), C_RESET(), C_DIM(),
			       alpm_pkg_get_version(new), C_RESET(), C_WARN(), C_RESET());
		} else {
			printf("%s%-30s%s %s%s%s -> %s%s%s\n", C_BOLD(),
			       name, C_RESET(), C_DIM(),
			       alpm_pkg_get_version(old), C_RESET(), C_ACCENT(),
			       alpm_pkg_get_version(new), C_RESET());
		}
	}

	if (g_out == OUT_HUMAN) {
		if (!n && !n_ignored) {
			printf("%severything is up to date%s\n", C_OK(), C_RESET());
		} else {
			char *sz = human_size(total);
			printf("\n%d package%s to upgrade, %s to download\n", n,
			       n == 1 ? "" : "s", sz);
			free(sz);
			if (n_ignored)
				printf("%s%d held back — `synpkg ignore` lists them, "
				       "`synpkg unignore <package>` releases one%s\n",
				       C_DIM(), n_ignored, C_RESET());
		}
	}

	pconf_free_list(held, n_held);
	sp_alpm_free(h);
	/* Exit 0 with updates, 100 with none — a cron/bar poller wants to branch
	 * on this without parsing. Chosen well outside pacman's own exit codes.
	 *
	 * A held-back update is NOT something to do, so a machine whose only
	 * pending changes are held reports 100. The bar would otherwise show a
	 * permanent badge for updates that will never be taken. */
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

	/* AFTER the handle is freed: this opens one of its own to resolve each
	 * kernel's package version, and one live read handle at a time is a cheaper
	 * rule to keep than a shared one threaded through. */
	sp_kernel_status();
	sp_boot_status();
	return 0;
}

/* ── groups: a category pane for the Repositories tab ───────────────────────
 *
 * ALPM has no notion of "category". What it has is GROUPS, and a cherry-picked
 * subset of them is genuinely the shape the Arsenal pane already uses — the
 * Arsenal tab is nothing but the blackarch-* groups rendered as categories.
 *
 * Cherry-picked, and not `pacman -Sg`, because the 160 groups on a stock Arch
 * are three different kinds of thing wearing one name:
 *
 *   - browsable collections, which is what a category pane is for;
 *   - dependency stacks (kf6, qt6, alpm, python-build-backend) that exist so
 *     other packages can depend on them, and that nobody installs on purpose;
 *   - build scaffolding (base-devel, reproducible-faketools).
 *
 * Showing all three sends somebody looking for a photo editor into a list of
 * 300 Qt libraries. blackarch-* is excluded for a different reason: it has its
 * own tab, with its own installed counts and its own enable button.
 *
 * xorg* is excluded deliberately rather than by oversight — SynapseOS ships no
 * X11 session, so those groups install things that cannot run here.
 *
 * A group absent from this machine's databases is skipped silently, so the
 * pane shows what is actually installable rather than what Arch has somewhere.
 */
static const struct { const char *group, *label; } repo_groups[] = {
	/* Desktop environments and their application sets. */
	{ "plasma",               "KDE Plasma" },
	{ "kde-applications",     "KDE applications" },
	{ "gnome",                "GNOME" },
	{ "gnome-extra",          "GNOME extras" },
	{ "gnome-circle",         "GNOME Circle" },
	{ "xfce4",                "Xfce" },
	{ "xfce4-goodies",        "Xfce goodies" },
	{ "lxqt",                 "LXQt" },
	{ "mate",                 "MATE" },
	{ "cosmic",               "COSMIC" },
	{ "budgie",               "Budgie" },
	/* KDE's application set, split the way its own menu splits it. */
	{ "kde-graphics",         "KDE graphics" },
	{ "kde-multimedia",       "KDE multimedia" },
	{ "kde-network",          "KDE network" },
	{ "kde-office",           "KDE office" },
	{ "kde-utilities",        "KDE utilities" },
	{ "kde-games",            "KDE games" },
	{ "kde-education",        "KDE education" },
	{ "kde-system",           "KDE system" },
	{ "kde-sdk",              "KDE development" },
	{ "kde-pim",              "KDE mail and calendar" },
	{ "kde-accessibility",    "KDE accessibility" },
	/* Audio production. These are the reason `pro-audio` exists as a group at
	 * all, and they are exactly the sort of thing nobody finds by search
	 * because you have to know a plugin's name to search for it. */
	{ "pro-audio",            "Pro audio" },
	{ "lv2-plugins",          "LV2 plugins" },
	{ "vst-plugins",          "VST plugins" },
	{ "vst3-plugins",         "VST3 plugins" },
	{ "clap-plugins",         "CLAP plugins" },
	{ "ladspa-plugins",       "LADSPA plugins" },
	{ "soundfonts",           "SoundFonts" },
	/* Toolchains worth browsing as a set. */
	{ "vulkan-devel",         "Vulkan development" },
	{ "mingw-w64",            "MinGW-w64 (Windows cross)" },
	{ "dlang",                "D toolchain" },
	{ "risc-v",               "RISC-V toolchain" },
	{ "kubernetes-tools",     "Kubernetes tools" },
	{ "linux-tools",          "Kernel tools" },
	{ "archlinux-tools",      "Arch tools" },
	/* Editor ecosystems. */
	{ "neovim-plugins",       "Neovim plugins" },
	{ "vim-plugins",          "Vim plugins" },
	{ "tree-sitter-grammars", "Tree-sitter grammars" },
	/* Fonts, input methods, codecs, add-ons. */
	{ "nerd-fonts",           "Nerd Fonts" },
	{ "ipa-fonts",            "Japanese fonts" },
	{ "fcitx5-im",            "Fcitx5 input methods" },
	{ "gstreamer-plugins",    "GStreamer plugins" },
	{ "libretro",             "libretro cores" },
	{ "kodi-addons",          "Kodi add-ons" },
	{ "texlive",              "TeX Live" },
	{ "firefox-addons",       "Firefox add-ons" },
	{ "thunderbird-addons",   "Thunderbird add-ons" },
};

static const char *group_label(const char *group)
{
	for (size_t i = 0; i < sizeof repo_groups / sizeof *repo_groups; i++)
		if (!strcmp(repo_groups[i].group, group))
			return repo_groups[i].label;
	return NULL;
}

/* A group can span repositories — `pro-audio` has members in extra and in
 * multilib — so membership comes from alpm_find_group_pkgs() across every sync
 * db rather than from one db's group cache. */
static int groups_list(alpm_handle_t *h)
{
	alpm_list_t *dbs = sp_syncdbs(h);
	alpm_db_t *local = alpm_get_localdb(h);

	if (g_out == OUT_TSV)
		tsv_row(4, "category", "total", "installed", "label");

	for (size_t i = 0; i < sizeof repo_groups / sizeof *repo_groups; i++) {
		alpm_list_t *pkgs = alpm_find_group_pkgs(dbs, repo_groups[i].group);
		if (!pkgs)
			continue;   /* not in this machine's repositories */

		int total = 0, have = 0;
		for (alpm_list_t *p = pkgs; p; p = p->next) {
			total++;
			have += alpm_db_get_pkg(local, alpm_pkg_get_name(p->data)) != NULL;
		}
		alpm_list_free(pkgs);

		if (g_out == OUT_TSV) {
			char *t = xasprintf("%d", total);
			char *c = xasprintf("%d", have);
			tsv_row(4, repo_groups[i].group, t, c, repo_groups[i].label);
			free(t);
			free(c);
		} else {
			printf("%s%-26s%s %s%4d%s", C_BOLD(), repo_groups[i].label,
			       C_RESET(), C_DIM(), total, C_RESET());
			if (have)
				printf("  %s%d installed%s", C_OK(), have, C_RESET());
			putchar('\n');
		}
	}
	return 0;
}

static int groups_packages(alpm_handle_t *h, const char *group)
{
	/* Restricted to the curated set on purpose. `synpkg groups kf6` would
	 * otherwise render 300 libraries into a pane that offers an Install
	 * button on each, and none of them is a thing to install by hand. */
	if (!group_label(group))
		die("groups: '%s' is not a browsable group — "
		    "try: synpkg groups", group);

	alpm_list_t *pkgs = alpm_find_group_pkgs(sp_syncdbs(h), group);
	if (!pkgs) {
		if (g_out == OUT_TSV)
			emit_pkg_header();
		else
			warn("no packages in %s — is the repository that provides it "
			     "enabled and synced?", group);
		return 100;
	}

	alpm_db_t *local = alpm_get_localdb(h);
	emit_pkg_header();

	for (alpm_list_t *p = pkgs; p; p = p->next) {
		alpm_pkg_t *pkg = p->data;
		const char *name = alpm_pkg_get_name(pkg);
		alpm_db_t *db = alpm_pkg_get_db(pkg);
		emit_pkg(name, alpm_db_get_pkg(local, name) != NULL,
		         alpm_pkg_get_version(pkg),
		         db ? alpm_db_get_name(db) : "", alpm_pkg_get_size(pkg),
		         alpm_pkg_get_desc(pkg));
	}

	alpm_list_free(pkgs);
	return 0;
}

int cmd_groups(int argc, char **argv)
{
	alpm_handle_t *h = sp_alpm_init(false);
	int rc = argc > 0 ? groups_packages(h, argv[0]) : groups_list(h);
	sp_alpm_free(h);
	return rc;
}
