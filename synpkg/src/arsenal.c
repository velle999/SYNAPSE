/* arsenal.c — the BlackArch half, absorbed from syn-arsenal.
 *
 * The browse side is native now: syn-arsenal shelled out to `pacman -Sg` and
 * `pacman -Si` and paid a process per pane. libalpm already holds the group
 * cache in memory once the handle is open, so the same listing costs no
 * subprocesses at all.
 *
 * The bootstrap is NOT rewritten. arsenal-enable-repo.sh pins BlackArch's
 * master key fingerprint, drives upstream's strap.sh, and repairs the
 * single-mirror state that blocked every `-Syu` on 2026-08-07. That is
 * incident-earned behaviour wrapped around curl/tar/pacman; porting it to C
 * would risk all of it and buy nothing. synpkg execs it.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"
#include "i18n.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BA_REPO "blackarch"

/* The bare "blackarch" group is the union of every other group — around 5000
 * packages. Listing it beside the real categories invites one click that
 * renders the entire repository into a single pane. */
static bool is_category(const char *group)
{
	return !strncmp(group, BA_REPO "-", sizeof BA_REPO);
}

static alpm_db_t *blackarch_db(alpm_handle_t *h)
{
	for (alpm_list_t *d = sp_syncdbs(h); d; d = d->next)
		if (!strcmp(alpm_db_get_name(d->data), BA_REPO))
			return d->data;
	return NULL;
}

/* ── status ─────────────────────────────────────────────────────────────── */

/* Three distinct states, because the fix differs for each: no repo at all
 * (bootstrap it), a configured repo that was never synced (refresh), and a
 * working repo. Collapsing the middle one into "disabled" sends people to
 * re-run a bootstrap that will tell them it is already enabled.
 *
 * `records` is false when a LISTING delegates here for the message and the
 * exit code. A listing has already written its own header by then, and this
 * one's is three columns of state — see the comment on emit_category_header.
 * The state still reaches a caller that wants it: it is the return value, and
 * `arsenal status` is the command that reports it. */
static int arsenal_status(alpm_handle_t *h, bool records)
{
	bool rows = records && g_out == OUT_TSV;

	alpm_db_t *db = blackarch_db(h);
	if (!db) {
		if (rows)
			tsv_row(3, "disabled", "0", "");
		else if (g_out != OUT_TSV)
			printf("BlackArch is %snot enabled%s — run: synpkg arsenal enable-repo\n",
			       C_WARN(), C_RESET());
		return 2;
	}

	int n = alpm_list_count(alpm_db_get_pkgcache(db));
	if (n == 0) {
		if (rows)
			tsv_row(3, "unsynced", "0", "");
		else if (g_out != OUT_TSV)
			printf("BlackArch is configured but %snever synced%s — run: synpkg refresh\n",
			       C_WARN(), C_RESET());
		return 3;
	}

	/* The keyring PACKAGE being absent is not cosmetic. strap.sh imports the
	 * signing keys into pacman's keyring directly, so installs keep working
	 * and nothing looks wrong — but key ROTATIONS only ever arrive as an
	 * upgrade of blackarch-keyring. Without it the repo silently ages out of
	 * trust. */
	bool keyring = alpm_db_get_pkg(alpm_get_localdb(h), "blackarch-keyring") != NULL;

	if (rows) {
		char *c = xasprintf("%d", n);
		tsv_row(3, "enabled", c, keyring ? "ok" : "missing");
		free(c);
	} else if (g_out != OUT_TSV) {
		printf("BlackArch is %senabled%s — %d packages available.\n",
		       C_OK(), C_RESET(), n);
		if (!keyring)
			warn(_("blackarch-keyring is not installed — signing key rotations "
			     "will never reach this machine.\n"
			     "  fix: synpkg install blackarch-keyring"));
	}
	return 0;
}

/* ── categories ─────────────────────────────────────────────────────────── */

/* `label` is what the pane displays, `category` is what `arsenal packages`
 * takes back. Every group here is "blackarch-<thing>" and the prefix is noise
 * repeated fifty times down a pane, but stripping it in the GUI would mean the
 * GUI knowing this source's naming scheme — and it renders the suggestion and
 * Flathub panes too. Strip it where the names are known instead.
 *
 * ⚠ FOUR sources feed one pane, and the pane keys its fields BY HEADER NAME.
 * This header is therefore owed on every path out of the listing, including
 * the ones that answer nothing. With BlackArch not configured this used to
 * emit `disabled 0` as the first line instead — three columns of state where
 * the pane reads four columns of data, so the pane keyed `total` off a field
 * that was not there. It rendered acceptably only because the row count was
 * zero and the empty state took over; any row at all and it would have drawn
 * blanks, or sent the wrong string back as the category to open.
 *
 * An empty table is a complete answer. The reason is not lost: the exit code
 * carries it, and `arsenal status` is the command that names it. */
static void emit_category_header(void)
{
	if (g_out == OUT_TSV)
		tsv_row(4, "category", "total", "installed", "label");
}

static int arsenal_categories(alpm_handle_t *h)
{
	alpm_db_t *db = blackarch_db(h);
	if (!db) {
		emit_category_header();
		return arsenal_status(h, false);
	}

	emit_category_header();

	alpm_db_t *local = alpm_get_localdb(h);

	for (alpm_list_t *g = alpm_db_get_groupcache(db); g; g = g->next) {
		alpm_group_t *grp = g->data;
		if (!is_category(grp->name))
			continue;

		/* The installed count per category is what turns the browser into an
		 * uninstall surface: you can see which categories you have pulled
		 * things from without opening all 50. */
		int total = 0, have = 0;
		for (alpm_list_t *p = grp->packages; p; p = p->next) {
			total++;
			have += alpm_db_get_pkg(local, alpm_pkg_get_name(p->data)) != NULL;
		}

		if (g_out == OUT_TSV) {
			char *t = xasprintf("%d", total);
			char *i = xasprintf("%d", have);
			tsv_row(4, grp->name, t, i, grp->name + sizeof BA_REPO);
			free(t);
			free(i);
		} else {
			printf("%s%-32s%s %s%4d%s", C_BOLD(), grp->name, C_RESET(),
			       C_DIM(), total, C_RESET());
			if (have)
				printf("  %s%d installed%s", C_OK(), have, C_RESET());
			putchar('\n');
		}
	}
	return 0;
}

/* ── packages in a category ─────────────────────────────────────────────── */

static int arsenal_packages(alpm_handle_t *h, const char *group)
{
	if (!is_category(group))
		die(_("arsenal: '%s' is not a blackarch category"), group);

	alpm_db_t *db = blackarch_db(h);
	if (!db) {
		/* Same contract, same reason: the package pane keys its fields by
		 * name too, and a listing that cannot answer still owes its header.
		 * Untested until `categories` was caught doing it — they delegated
		 * to the same place. */
		emit_pkg_header();
		return arsenal_status(h, false);
	}

	alpm_group_t *grp = alpm_db_get_group(db, group);
	if (!grp) {
		warn(_("no such category: %s"), group);
		return 1;
	}

	alpm_db_t *local = alpm_get_localdb(h);
	emit_pkg_header();

	for (alpm_list_t *p = grp->packages; p; p = p->next) {
		alpm_pkg_t *pkg = p->data;
		const char *name = alpm_pkg_get_name(pkg);
		emit_pkg(name, alpm_db_get_pkg(local, name) != NULL,
		         alpm_pkg_get_version(pkg), BA_REPO, alpm_pkg_get_size(pkg),
		         alpm_pkg_get_desc(pkg));
	}
	return 0;
}

/* ── installed ──────────────────────────────────────────────────────────── */

/* The view syn-arsenal never had. Without it, removing a tool means remembering
 * which of ~50 categories it came from and navigating back to it, which is why
 * "there is no uninstall" was a fair description of the old app even though the
 * per-row Remove button existed.
 *
 * Membership is decided by the group recorded in the LOCAL database, not by
 * looking the package up in the sync db: a tool stays listed here after
 * BlackArch is disabled or a mirror drops the package, which is exactly when
 * you most want to be able to take it off. */
static int arsenal_installed(alpm_handle_t *h)
{
	emit_pkg_header();

	int n = 0;
	for (alpm_list_t *i = alpm_db_get_pkgcache(alpm_get_localdb(h)); i; i = i->next) {
		alpm_pkg_t *p = i->data;

		bool ba = false;
		for (alpm_list_t *g = alpm_pkg_get_groups(p); g && !ba; g = g->next)
			ba = !strncmp(g->data, BA_REPO, sizeof BA_REPO - 1);
		if (!ba)
			continue;

		emit_pkg(alpm_pkg_get_name(p), true, alpm_pkg_get_version(p), BA_REPO,
		         alpm_pkg_get_isize(p), alpm_pkg_get_desc(p));
		n++;
	}

	if (g_out == OUT_HUMAN) {
		if (!n)
			printf("No BlackArch tools are installed.\n");
		else
			printf("\n%d installed — remove one with: %ssynpkg remove <name>%s\n",
			       n, C_ACCENT(), C_RESET());
	}
	return 0;
}

/* ── enable-repo ────────────────────────────────────────────────────────── */

static int arsenal_enable_repo(void)
{
	if (!is_root())
		return escalate("arsenal", 1, (char *[]){ (char *)"enable-repo" });

	/* Installed path first, then the source tree, so the helper is runnable
	 * before the package exists — same rule as the catalogue. */
	static const char *candidates[] = {
		"/usr/lib/synpkg/synpkg-enable-blackarch",
		"data/synpkg-enable-blackarch.sh",
	};

	for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++) {
		if (access(candidates[i], X_OK) != 0)
			continue;
		char *argv[] = { (char *)candidates[i], NULL };
		return run(argv, false);
	}

	die(_("the BlackArch bootstrap helper is missing "
	    "(/usr/lib/synpkg/synpkg-enable-blackarch)"));
}

/* ── dispatch ───────────────────────────────────────────────────────────── */

int cmd_arsenal(int argc, char **argv)
{
	const char *sub = argc > 0 ? argv[0] : "categories";

	/* Handled before the alpm handle is opened: it escalates and re-execs, and
	 * opening the database first would take the lock we are about to need. */
	if (!strcmp(sub, "enable-repo"))
		return arsenal_enable_repo();

	alpm_handle_t *h = sp_alpm_init(false);
	int rc;

	if (!strcmp(sub, "status"))
		rc = arsenal_status(h, true);
	else if (!strcmp(sub, "categories"))
		rc = arsenal_categories(h);
	else if (!strcmp(sub, "installed"))
		rc = arsenal_installed(h);
	else if (!strcmp(sub, "packages")) {
		if (argc < 2)
			die(_("arsenal packages: need a category"));
		rc = arsenal_packages(h, argv[1]);
	} else {
		sp_alpm_free(h);
		die(_("arsenal: unknown subcommand '%s'\n"
		    "  try: status, categories, packages <category>, installed, enable-repo"),
		    sub);
	}

	sp_alpm_free(h);
	return rc;
}
