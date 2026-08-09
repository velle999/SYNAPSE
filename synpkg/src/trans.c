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
 * upgrade rides along with `install vim`. */
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

int cmd_install(int argc, char **argv)
{
	bool refresh = true;
	alpm_list_t *targets = NULL;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--no-refresh"))
			refresh = false;
		else if (argv[i][0] == '-')
			die("install: unknown option '%s'", argv[i]);
		else
			targets = alpm_list_add(targets, argv[i]);
	}
	if (!targets)
		die("install: need at least one package");

	if (!is_root()) {
		alpm_list_free(targets);
		return escalate("install", argc, argv);
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
		rc = 0;
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
		rc = 0;
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

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--allow-downgrade"))
			downgrade = true;
		else if (!strcmp(argv[i], "--no-refresh"))
			refresh = false;
		else
			die("upgrade: unknown argument '%s'", argv[i]);
	}

	if (!is_root())
		return escalate("upgrade", argc, argv);

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
		rc = 0;
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
