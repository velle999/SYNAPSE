/* trans.c — the mutating half: refresh, install, remove, full upgrade.
 *
 * Privilege model
 * ---------------
 * synpkg runs unprivileged and re-execs ITSELF through pkexec when a command
 * needs the database lock. The alternative — a long-lived root helper on a
 * socket — is a bigger attack surface than the thing it protects, and the
 * desktop already escalates this way (synui-iso-mount, syn-model, the old
 * arsenal-query). What pkexec grants is "run the package manager as root",
 * which is exactly the authority the action is asking for; it does not widen
 * to arbitrary commands.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool is_root(void)
{
	return geteuid() == 0;
}

int escalate(const char *verb, int argc, char **argv)
{
	if (!have_cmd("pkexec"))
		die("this needs root and pkexec is not installed — re-run with sudo");

	/* /proc/self/exe, not argv[0]: argv[0] may be a relative path, and pkexec
	 * resets the working directory. It also pins the escalation to the binary
	 * actually running rather than whatever the PATH resolves to as root. */
	char self[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
	if (n <= 0)
		die("cannot determine my own path: %s", strerror(errno));
	self[n] = '\0';

	/* pkexec, self, up to three global flags, verb, argc args, NULL. */
	char **child = xmalloc((size_t)(argc + 7) * sizeof *child);
	int k = 0;
	child[k++] = (char *)"pkexec";
	child[k++] = self;
	/* The flags have already been consumed from argv by main(), so they are
	 * re-applied from the globals or the child would prompt on a stdin the
	 * GUI does not have. */
	if (g_out == OUT_TSV)
		child[k++] = (char *)"--tsv";
	if (g_noconfirm)
		child[k++] = (char *)"--noconfirm";
	if (g_verbose)
		child[k++] = (char *)"--verbose";
	child[k++] = (char *)verb;
	for (int i = 0; i < argc; i++)
		child[k++] = argv[i];
	child[k] = NULL;

	info("this needs root — authenticating");
	int rc = run(child, false);
	free(child);

	/* pkexec's own "not authorised"/"dismissed" exit code. Translate it: the
	 * bare 126/127 reads like the command was missing. */
	if (rc == 126)
		die("authentication failed or was dismissed");
	if (rc == 127)
		die("pkexec could not run %s", self);
	return rc;
}

/* ── prepare/commit error reporting ─────────────────────────────────────── */

/* alpm hands back a typed list on failure and the type depends on errno. Not
 * draining it means the user sees "could not prepare transaction" with no hint
 * which dependency was missing — the single most common way a package manager
 * becomes useless at the exact moment it matters. */
static void report_trans_error(alpm_handle_t *h, alpm_list_t *data)
{
	alpm_errno_t err = alpm_errno(h);

	switch (err) {
	case ALPM_ERR_PKG_INVALID_ARCH:
		for (alpm_list_t *i = data; i; i = i->next)
			warn("package %s does not have a valid architecture", (char *)i->data);
		break;
	case ALPM_ERR_UNSATISFIED_DEPS:
		for (alpm_list_t *i = data; i; i = i->next) {
			alpm_depmissing_t *m = i->data;
			char *dep = alpm_dep_compute_string(m->depend);
			warn("%s: requires %s", m->target, dep);
			free(dep);
		}
		break;
	case ALPM_ERR_CONFLICTING_DEPS:
		for (alpm_list_t *i = data; i; i = i->next) {
			alpm_conflict_t *c = i->data;
			warn("%s and %s are in conflict",
			     alpm_pkg_get_name(c->package1), alpm_pkg_get_name(c->package2));
		}
		break;
	case ALPM_ERR_FILE_CONFLICTS:
		for (alpm_list_t *i = data; i; i = i->next) {
			alpm_fileconflict_t *c = i->data;
			if (c->type == ALPM_FILECONFLICT_TARGET)
				warn("%s and %s both own %s", c->target, c->ctarget, c->file);
			else
				warn("%s: %s exists and is owned by %s", c->target, c->file,
				     c->ctarget && *c->ctarget ? c->ctarget : "no package");
		}
		break;
	default:
		break;
	}

	warn("transaction failed: %s", alpm_strerror(err));
}

/* Summarise before committing. A package manager that starts writing without
 * telling you what it is about to write is how an unrelated 400-package
 * upgrade rides along with `install vim`.
 *
 * FALSE from here means one of two opposite things, and every caller resolves
 * it the same way: `rc = confirm_possible() ? 0 : 1`.
 *
 *   · A person answered "n". That is a decision, the transaction did exactly
 *     what was asked of it, and the exit code is 0.
 *   · There was no terminal to ask in — a front-end forgot --noconfirm. That
 *     is a bug in the CALLER, and reporting it as success is what let
 *     syn-settings' "Make bootable" authenticate through polkit three times
 *     in one sitting and install nothing, each time reporting that it had
 *     worked. Exit 1 so the front-end's own error path fires.
 */
static bool confirm_transaction(alpm_handle_t *h)
{
	alpm_list_t *add = alpm_trans_get_add(h);
	alpm_list_t *rem = alpm_trans_get_remove(h);

	if (!add && !rem) {
		info("nothing to do");
		return false;
	}

	off_t download = 0, delta = 0;
	int nadd = 0, nrem = 0;

	for (alpm_list_t *i = add; i; i = i->next, nadd++) {
		alpm_pkg_t *p = i->data;
		download += alpm_pkg_download_size(p);
		delta += alpm_pkg_get_isize(p);
	}
	for (alpm_list_t *i = rem; i; i = i->next, nrem++)
		delta -= alpm_pkg_get_isize(i->data);

	if (g_out == OUT_HUMAN) {
		if (nadd) {
			printf("\n%sPackages (%d)%s", C_BOLD(), nadd, C_RESET());
			for (alpm_list_t *i = add; i; i = i->next)
				printf("  %s-%s", alpm_pkg_get_name(i->data),
				       alpm_pkg_get_version(i->data));
			putchar('\n');
		}
		if (nrem) {
			printf("\n%sRemoving (%d)%s", C_WARN(), nrem, C_RESET());
			for (alpm_list_t *i = rem; i; i = i->next)
				printf("  %s-%s", alpm_pkg_get_name(i->data),
				       alpm_pkg_get_version(i->data));
			putchar('\n');
		}

		char *dl = human_size(download);
		char *dd = human_size(delta < 0 ? -delta : delta);
		printf("\nDownload: %s   Disk: %s%s\n\n", dl, delta < 0 ? "-" : "+", dd);
		free(dl);
		free(dd);
	}

	return g_noconfirm || confirm("Proceed?");
}

/* ── refresh ────────────────────────────────────────────────────────────── */

static int do_refresh(alpm_handle_t *h, bool force)
{
	alpm_list_t *dbs = sp_syncdbs(h);
	if (!dbs) {
		warn("no repositories are configured");
		return 1;
	}

	info("synchronising package databases");
	int rc = alpm_db_update(h, dbs, force);
	if (rc < 0) {
		warn("failed to synchronise databases: %s", sp_alpm_err(h));
		return 1;
	}
	/* rc == 1 means every db was already up to date. Not an error. */
	return 0;
}

int cmd_refresh(int argc, char **argv)
{
	bool force = false;
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--force"))
			force = true;
		else
			die("refresh: unknown argument '%s'", argv[i]);
	}

	if (!is_root())
		return escalate("refresh", argc, argv);

	alpm_handle_t *h = sp_alpm_init(true);
	int rc = do_refresh(h, force);
	sp_alpm_free(h);
	return rc;
}

/* ── install ────────────────────────────────────────────────────────────── */

/* Resolve a target across the sync dbs, honouring Usage=Install: a repo the
 * user restricted to Search must not become an install source. */
static alpm_pkg_t *find_sync_pkg(alpm_handle_t *h, const char *name)
{
	for (alpm_list_t *d = sp_syncdbs(h); d; d = d->next) {
		alpm_db_t *db = d->data;
		int usage = 0;
		alpm_db_get_usage(db, &usage);
		if (!(usage & ALPM_DB_USAGE_INSTALL))
			continue;
		alpm_pkg_t *p = alpm_db_get_pkg(db, name);
		if (p)
			return p;
	}

	/* Not a package name — try it as a virtual/provider, which is how
	 * `install java-runtime` or a renamed package has to work. Usage is
	 * checked here too: a repo restricted to Search must not become an install
	 * source through the back door of providing something. */
	for (alpm_list_t *d = sp_syncdbs(h); d; d = d->next) {
		int usage = 0;
		alpm_db_get_usage(d->data, &usage);
		if (!(usage & ALPM_DB_USAGE_INSTALL))
			continue;
		alpm_pkg_t *p = alpm_find_satisfier(alpm_db_get_pkgcache(d->data), name);
		if (p)
			return p;
	}
	return NULL;
}

/* Split targets into "a repository has it" and "no repository has it".
 *
 * Runs UNPRIVILEGED, against the databases already on disk. Those can be
 * stale, which is why a name that misses here is only ever routed to the AUR
 * when the AUR separately confirms it exists — a stale database then produces
 * today's "not found" rather than a surprise source build. A package in BOTH a
 * repository and the AUR resolves here first and never reaches the fallback.
 */
static size_t split_repo_vs_foreign(char **names, size_t n,
                                    char ***repo, size_t *nrepo,
                                    char ***foreign)
{
	alpm_handle_t *h = sp_alpm_init(false);

	*repo = xmalloc(n * sizeof **repo);
	*foreign = xmalloc(n * sizeof **foreign);
	*nrepo = 0;
	size_t nf = 0;

	for (size_t i = 0; i < n; i++) {
		if (find_sync_pkg(h, names[i]))
			(*repo)[(*nrepo)++] = names[i];
		else
			(*foreign)[nf++] = names[i];
	}

	sp_alpm_free(h);
	return nf;
}

int cmd_install(int argc, char **argv)
{
	bool refresh = true, use_aur = true;
	alpm_list_t *targets = NULL;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--no-refresh"))
			refresh = false;
		else if (!strcmp(argv[i], "--no-aur"))
			use_aur = false;
		else if (argv[i][0] == '-')
			die("install: unknown option '%s'", argv[i]);
		else
			targets = alpm_list_add(targets, argv[i]);
	}
	if (!targets)
		die("install: need at least one package");

	if (!is_root()) {
		/* ── THE AUR FALLBACK ────────────────────────────────────────────
		 *
		 * It has to happen HERE, before escalate(), because makepkg refuses
		 * to run as root and the escalated child is root for its whole life.
		 * Falling back on the far side of pkexec is not possible: that
		 * process cannot drop back to a user it would have to guess at.
		 *
		 * Without this, a package that exists only in the AUR reported "not
		 * found in any repository" and stopped — which is what made
		 * `synpkg install limine-mkinitcpio-hook` fail on limine machines
		 * installed before the local repo carried it, and with it
		 * syn-settings' "Make bootable".
		 */
		size_t n = alpm_list_count(targets);
		char **names = xmalloc(n * sizeof *names);
		size_t k = 0;
		for (alpm_list_t *t = targets; t; t = t->next)
			names[k++] = t->data;
		alpm_list_free(targets);

		char **repo = NULL, **foreign = NULL;
		size_t nrepo = 0;
		size_t nf = split_repo_vs_foreign(names, n, &repo, &nrepo, &foreign);

		char **aur = NULL;
		size_t naur = 0;
		if (nf && use_aur)
			naur = aur_filter_existing(foreign, nf, &aur);

		/* Named, in no repository, and not in the AUR either. Reported here
		 * rather than after an authentication prompt for a transaction that
		 * cannot succeed. */
		int rc = 0;
		for (size_t i = 0; i < nf; i++) {
			bool found = false;
			for (size_t j = 0; j < naur && !found; j++)
				found = !strcmp(foreign[i], aur[j]);
			if (!found) {
				warn("package '%s' was not found in any repository%s",
				     foreign[i], use_aur ? " or in the AUR" : "");
				rc = 1;
			}
		}

		/* Repository packages first: they are fast, and an AUR build that
		 * depends on one then finds it already present. */
		if (nrepo && rc == 0) {
			int sub_argc = (int)nrepo + (refresh ? 0 : 1);
			char **sub = xmalloc((size_t)(sub_argc + 1) * sizeof *sub);
			int s = 0;
			if (!refresh)
				sub[s++] = (char *)"--no-refresh";
			for (size_t i = 0; i < nrepo; i++)
				sub[s++] = repo[i];
			sub[s] = NULL;
			rc = escalate("install", s, sub);
			free(sub);
		}

		if (naur && rc == 0) {
			/* Never silent. An AUR build is arbitrary code from the
			 * internet, and the user asked for what looked like an ordinary
			 * install — so say where it is coming from before it starts. */
			for (size_t i = 0; i < naur; i++)
				info("%s is not in any repository; building it from the AUR",
				     aur[i]);
			rc = aur_build_install(aur, naur);
		}

		for (size_t i = 0; i < naur; i++)
			free(aur[i]);
		free(aur);
		free(repo);
		free(foreign);
		free(names);
		return rc;
	}

	alpm_handle_t *h = sp_alpm_init(true);
	int rc = 1;

	if (refresh && do_refresh(h, false) != 0)
		warn("continuing with the databases already on disk");

	/* NEEDED so re-installing something already at the target version is a
	 * no-op. The GUI's Install button is one click away from a stray
	 * reinstall, and a reinstall is not what anyone means by it. */
	if (alpm_trans_init(h, ALPM_TRANS_FLAG_NEEDED) != 0) {
		warn("cannot start a transaction: %s", sp_alpm_err(h));
		goto out;
	}

	int missing = 0;
	for (alpm_list_t *t = targets; t; t = t->next) {
		const char *name = t->data;
		alpm_pkg_t *p = find_sync_pkg(h, name);
		if (!p) {
			warn("package '%s' was not found in any repository", name);
			missing++;
			continue;
		}
		if (alpm_add_pkg(h, p) != 0) {
			warn("cannot install %s: %s", name, sp_alpm_err(h));
			missing++;
		}
	}
	if (missing) {
		alpm_trans_release(h);
		goto out;
	}

	alpm_list_t *data = NULL;
	if (alpm_trans_prepare(h, &data) != 0) {
		report_trans_error(h, data);
		FREELIST(data);
		alpm_trans_release(h);
		goto out;
	}

	if (!confirm_transaction(h)) {
		alpm_trans_release(h);
		rc = confirm_possible() ? 0 : 1;
		goto out;
	}

	data = NULL;
	if (alpm_trans_commit(h, &data) != 0) {
		report_trans_error(h, data);
		FREELIST(data);
		alpm_trans_release(h);
		goto out;
	}

	alpm_trans_release(h);
	rc = 0;
	info("done");

out:
	alpm_list_free(targets);
	sp_alpm_free(h);
	return rc;
}

/* ── remove ─────────────────────────────────────────────────────────────── */

int cmd_remove(int argc, char **argv)
{
	/* -Rns semantics by default: take the unneeded dependencies and the
	 * config files with it. That is what "uninstall" means to a user and
	 * leaving orphans behind is how an Arch install rots. */
	int flags = ALPM_TRANS_FLAG_RECURSE | ALPM_TRANS_FLAG_NOSAVE;
	bool cascade = false;
	alpm_list_t *targets = NULL;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--keep-deps"))
			flags &= ~ALPM_TRANS_FLAG_RECURSE;
		else if (!strcmp(argv[i], "--keep-config"))
			flags &= ~ALPM_TRANS_FLAG_NOSAVE;
		else if (!strcmp(argv[i], "--cascade"))
			cascade = true;
		else if (argv[i][0] == '-')
			die("remove: unknown option '%s'", argv[i]);
		else
			targets = alpm_list_add(targets, argv[i]);
	}
	if (!targets)
		die("remove: need at least one package");
	if (cascade)
		flags |= ALPM_TRANS_FLAG_CASCADE;

	if (!is_root()) {
		alpm_list_free(targets);
		return escalate("remove", argc, argv);
	}

	alpm_handle_t *h = sp_alpm_init(true);
	alpm_db_t *local = alpm_get_localdb(h);
	int rc = 1;

	if (alpm_trans_init(h, flags) != 0) {
		warn("cannot start a transaction: %s", sp_alpm_err(h));
		goto out;
	}

	int missing = 0;
	for (alpm_list_t *t = targets; t; t = t->next) {
		const char *name = t->data;
		alpm_pkg_t *p = alpm_db_get_pkg(local, name);
		if (!p) {
			warn("package '%s' is not installed", name);
			missing++;
			continue;
		}
		if (alpm_remove_pkg(h, p) != 0) {
			warn("cannot remove %s: %s", name, sp_alpm_err(h));
			missing++;
		}
	}
	if (missing) {
		alpm_trans_release(h);
		goto out;
	}

	alpm_list_t *data = NULL;
	if (alpm_trans_prepare(h, &data) != 0) {
		report_trans_error(h, data);
		FREELIST(data);
		alpm_trans_release(h);
		goto out;
	}

	if (!confirm_transaction(h)) {
		alpm_trans_release(h);
		rc = confirm_possible() ? 0 : 1;
		goto out;
	}

	data = NULL;
	if (alpm_trans_commit(h, &data) != 0) {
		report_trans_error(h, data);
		FREELIST(data);
		alpm_trans_release(h);
		goto out;
	}

	alpm_trans_release(h);
	rc = 0;
	info("done");

out:
	alpm_list_free(targets);
	sp_alpm_free(h);
	return rc;
}

/* ── upgrade ────────────────────────────────────────────────────────────── */

int cmd_upgrade(int argc, char **argv)
{
	bool downgrade = false, refresh = true;

	/* The SAVED answer is the starting point, and a flag on the command line
	 * overrides it for this run only.
	 *
	 * The button in SYNAPSE Software runs a fixed `synpkg upgrade`, so without
	 * a stored preference there was no way to say "the repositories and the
	 * AUR, but not the twenty minutes of compiling" and have it stick. Reading
	 * it HERE rather than in the window is what keeps the button and the same
	 * command typed into a terminal meaning the same thing. See settings.c. */
	bool use_aur = sp_setting_bool("upgrade_aur");
	bool use_system = sp_setting_bool("upgrade_system");

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--allow-downgrade"))
			downgrade = true;
		else if (!strcmp(argv[i], "--no-refresh"))
			refresh = false;
		else if (!strcmp(argv[i], "--no-aur"))
			use_aur = false;
		/* Symmetric with --no-aur, and wanted for the same reason: a
		 * component rebuild is minutes of compiling, and someone who only
		 * wants their repository packages current should be able to say so. */
		else if (!strcmp(argv[i], "--no-system"))
			use_system = false;
		else
			die("upgrade: unknown argument '%s'", argv[i]);
	}

	if (!is_root()) {
		/* ── THE OTHER HALF OF AN UPGRADE ────────────────────────────────
		 *
		 * alpm_sync_sysupgrade() compares the local database against the
		 * SYNC databases, so a package no sync database contains is
		 * structurally invisible to it — it is not skipped by choice, it can
		 * never be a candidate. Every AUR package therefore sat at whatever
		 * version it was built at, forever, while `upgrade` reported success
		 * and "everything is up to date".
		 *
		 * Same root constraint as the install fallback: makepkg refuses to
		 * run as root, so this runs in the UNPRIVILEGED pass, after the
		 * escalated repository upgrade returns. Doing it inside the pkexec'd
		 * child is not possible.
		 *
		 * Repository first, deliberately: an AUR package is built against
		 * the libraries on the system, so building it before the system
		 * upgrade would link it against versions being replaced minutes
		 * later.
		 */
		int rc = escalate("upgrade", argc, argv);

		/* A failed repository upgrade stops everything: both passes below
		 * build against the libraries it was in the middle of replacing. */
		if (rc != 0)
			return rc;

		/* `!use_aur` used to return HERE, which quietly took the SynapseOS
		 * pass below with it: `upgrade --no-aur` skipped the components too,
		 * and nothing said so. Two independent switches need two independent
		 * skips, so this is a branch around the AUR block rather than a
		 * return out of the function. */
		if (use_aur) {
			char **out = NULL;
			size_t n = aur_outdated_names(&out);

			/* Rebuild only what synpkg installed. "Foreign" also covers every
			 * locally built SynapseOS package, and the names really do
			 * collide — davinci-resolve is installed here from Blackmagic's
			 * own installer and the AUR has one too. Rebuilding that from a
			 * PKGBUILD during a routine upgrade would replace a hand-managed
			 * install with an unrelated one. Anything unowned is NAMED rather
			 * than skipped in silence, so an update nobody applies is still an
			 * update you saw. */
			char **mine = xmalloc((n ? n : 1) * sizeof *mine);
			size_t nmine = 0, nother = 0;
			for (size_t i = 0; i < n; i++) {
				if (aur_have_checkout(out[i])) {
					mine[nmine++] = out[i];
				} else {
					nother++;
					warn("%s has a newer version in the AUR, but synpkg did not "
					     "install it — leaving it alone (synpkg aur install %s)",
					     out[i], out[i]);
				}
			}

			if (nmine) {
				for (size_t i = 0; i < nmine; i++)
					info("%s has a newer version in the AUR", mine[i]);
				rc = aur_build_install(mine, nmine);
			} else if (!nother && g_out == OUT_HUMAN) {
				printf("%sAUR packages are up to date%s\n", C_OK(), C_RESET());
			}

			free(mine);
			for (size_t i = 0; i < n; i++)
				free(out[i]);
			free(out);
		}

		/* ── THE THIRD HALF OF AN UPGRADE ────────────────────────────────
		 *
		 * SynapseOS's own components are in no sync database and are not in
		 * the AUR either, so neither pass above can see them: `upgrade`
		 * reported the machine current while the compositor, the daemons and
		 * this program itself sat at whatever revision they were built at.
		 * The one place that listed them was a tab nobody opens to answer
		 * "is anything out of date?".
		 *
		 * Here, in the UNPRIVILEGED pass, for the same reason the AUR is:
		 * syn-update refuses to run as root (need_not_root — makepkg will not
		 * either) and calls sudo itself where it needs it.
		 *
		 * LAST, also deliberately: a component is built against the libraries
		 * on the system, so building it before the repository upgrade would
		 * link it against versions being replaced minutes later. Same
		 * argument the AUR pass above makes, one step further along.
		 *
		 * Its failure is reported and does not become the exit status of the
		 * whole upgrade: the repositories and the AUR really were upgraded,
		 * and returning failure for them would be a lie about what happened. */
		if (use_system) {
			if (!have_cmd("syn-update")) {
				info("syn-update is not installed — SynapseOS components not checked");
			} else {
				char *sy[] = { (char *)"syn-update", (char *)"apply", NULL };
				if (run(sy, false) != 0)
					warn("SynapseOS components did not finish updating "
					     "(syn-update apply) — the repository upgrade above was fine");
			}
		}
		return rc;
	}

	alpm_handle_t *h = sp_alpm_init(true);
	int rc = 1;

	/* A sysupgrade without -Sy compares against databases that may be weeks
	 * old; that is a partial upgrade waiting to happen, so refresh is the
	 * default and turning it off is explicit. */
	if (refresh && do_refresh(h, false) != 0)
		goto out;

	if (alpm_trans_init(h, 0) != 0) {
		warn("cannot start a transaction: %s", sp_alpm_err(h));
		goto out;
	}

	if (alpm_sync_sysupgrade(h, downgrade) != 0) {
		warn("cannot prepare an upgrade: %s", sp_alpm_err(h));
		alpm_trans_release(h);
		goto out;
	}

	alpm_list_t *data = NULL;
	if (alpm_trans_prepare(h, &data) != 0) {
		report_trans_error(h, data);
		FREELIST(data);
		alpm_trans_release(h);
		goto out;
	}

	if (!alpm_trans_get_add(h) && !alpm_trans_get_remove(h)) {
		if (g_out == OUT_HUMAN)
			printf("%severything is up to date%s\n", C_OK(), C_RESET());
		alpm_trans_release(h);
		rc = 0;
		goto out;
	}

	if (!confirm_transaction(h)) {
		alpm_trans_release(h);
		rc = confirm_possible() ? 0 : 1;
		goto out;
	}

	data = NULL;
	if (alpm_trans_commit(h, &data) != 0) {
		report_trans_error(h, data);
		FREELIST(data);
		alpm_trans_release(h);
		goto out;
	}

	alpm_trans_release(h);
	rc = 0;
	info("system upgraded");

out:
	sp_alpm_free(h);
	return rc;
}
