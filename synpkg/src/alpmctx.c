/* alpmctx.c — libalpm handle setup and the transaction callbacks.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *sp_alpm_err(alpm_handle_t *h)
{
	return alpm_strerror(alpm_errno(h));
}

alpm_list_t *sp_syncdbs(alpm_handle_t *h)
{
	return alpm_get_syncdbs(h);
}

/* ── callbacks ──────────────────────────────────────────────────────────── */

static void cb_event(void *ctx, alpm_event_t *event)
{
	(void)ctx;
	switch (event->type) {
	case ALPM_EVENT_CHECKDEPS_START:
		info("checking dependencies");
		break;
	case ALPM_EVENT_FILECONFLICTS_START:
		info("checking for file conflicts");
		break;
	case ALPM_EVENT_RESOLVEDEPS_START:
		info("resolving dependencies");
		break;
	case ALPM_EVENT_INTERCONFLICTS_START:
		info("looking for conflicting packages");
		break;
	case ALPM_EVENT_INTEGRITY_START:
		info("checking package integrity");
		break;
	case ALPM_EVENT_KEYRING_START:
		info("checking keyring");
		break;
	case ALPM_EVENT_PACKAGE_OPERATION_START: {
		alpm_event_package_operation_t *op = &event->package_operation;
		const char *verb = NULL;
		alpm_pkg_t *pkg = op->newpkg ? op->newpkg : op->oldpkg;
		switch (op->operation) {
		case ALPM_PACKAGE_INSTALL:   verb = "installing";   break;
		case ALPM_PACKAGE_UPGRADE:   verb = "upgrading";    break;
		case ALPM_PACKAGE_REINSTALL: verb = "reinstalling"; break;
		case ALPM_PACKAGE_DOWNGRADE: verb = "downgrading";  break;
		case ALPM_PACKAGE_REMOVE:    verb = "removing";     break;
		}
		if (verb && pkg)
			info("%s %s", verb, alpm_pkg_get_name(pkg));
		break;
	}
	case ALPM_EVENT_PACNEW_CREATED:
		/* Loud on purpose: a .pacnew nobody merges is how a config silently
		 * stops matching the package that owns it. */
		warn("new config saved as %s.pacnew — merge it",
		     event->pacnew_created.file);
		break;
	case ALPM_EVENT_PACSAVE_CREATED:
		warn("config saved as %s.pacsave", event->pacsave_created.file);
		break;
	case ALPM_EVENT_SCRIPTLET_INFO:
		fputs(event->scriptlet_info.line, stderr);
		break;
	case ALPM_EVENT_DB_RETRIEVE_FAILED:
		warn("failed to retrieve some databases");
		break;
	case ALPM_EVENT_PKG_RETRIEVE_FAILED:
		warn("failed to retrieve some packages");
		break;
	case ALPM_EVENT_OPTDEP_REMOVAL:
		warn("%s optionally requires %s, which is being removed",
		     alpm_pkg_get_name(event->optdep_removal.pkg),
		     event->optdep_removal.optdep->name);
		break;
	default:
		break;
	}
}

/* Every question gets an explicit answer. Defaulting by leaving `answer`
 * untouched means alpm takes the "no" branch silently, which turns "replace
 * package X with Y" into a transaction that quietly does not do what was asked.
 */
static void cb_question(void *ctx, alpm_question_t *question)
{
	(void)ctx;
	switch (question->type) {
	case ALPM_QUESTION_INSTALL_IGNOREPKG:
		/* Honour IgnorePkg — the user put it there. */
		question->install_ignorepkg.install = 0;
		break;
	case ALPM_QUESTION_REPLACE_PKG: {
		alpm_question_replace_t *q = &question->replace;
		info("replacing %s with %s/%s", alpm_pkg_get_name(q->oldpkg),
		     alpm_db_get_name(q->newdb), alpm_pkg_get_name(q->newpkg));
		q->replace = 1;
		break;
	}
	case ALPM_QUESTION_CONFLICT_PKG: {
		alpm_question_conflict_t *q = &question->conflict;
		/* Never resolve a conflict by removing something behind the user's
		 * back — that is how a package manager eats a working system. Refuse
		 * and let prepare() fail with a message naming both packages. */
		const char *p1 = alpm_pkg_get_name(q->conflict->package1);
		const char *p2 = alpm_pkg_get_name(q->conflict->package2);
		warn("%s conflicts with %s", p1, p2);
		q->remove = g_noconfirm ? 0 : confirm("  remove %s?", p2);
		break;
	}
	case ALPM_QUESTION_CORRUPTED_PKG: {
		alpm_question_corrupted_t *q = &question->corrupted;
		warn("%s is corrupted (%s) — deleting", q->filepath,
		     alpm_strerror(q->reason));
		q->remove = 1;
		break;
	}
	case ALPM_QUESTION_REMOVE_PKGS:
		/* Unresolvable deps. Skipping the packages is what pacman offers;
		 * refuse and surface it instead of half-completing. */
		question->remove_pkgs.skip = 0;
		break;
	case ALPM_QUESTION_SELECT_PROVIDER: {
		alpm_question_select_provider_t *q = &question->select_provider;
		/* First provider, matching pacman's non-interactive behaviour. */
		q->use_index = 0;
		break;
	}
	case ALPM_QUESTION_IMPORT_KEY: {
		alpm_question_import_key_t *q = &question->import_key;
		/* Importing a signing key is a trust decision. Auto-yes here would
		 * make every SigLevel setting on the box decorative. */
		q->import = confirm("import PGP key %s, \"%s\"?", q->fingerprint,
		                    q->uid);
		break;
	}
	default:
		break;
	}
}

/* alpm's progress callback fires per percent; in human mode we redraw one line,
 * in TSV mode we stay silent so the GUI's stdout carries records only. */
static void cb_progress(void *ctx, alpm_progress_t kind, const char *pkg,
                        int percent, size_t howmany, size_t current)
{
	(void)ctx;
	(void)kind;
	if (g_out == OUT_TSV || !pkg)
		return;
	fprintf(stderr, "\r%s(%zu/%zu)%s %-32.32s %3d%%",
	        C_DIM(), current, howmany, C_RESET(), pkg, percent);
	if (percent == 100)
		fputc('\n', stderr);
	fflush(stderr);
}

static void cb_download(void *ctx, const char *filename,
                        alpm_download_event_type_t type, void *data)
{
	(void)ctx;
	(void)data;
	if (g_out == OUT_TSV || !g_verbose)
		return;
	if (type == ALPM_DOWNLOAD_COMPLETED)
		fprintf(stderr, "  %sdownloaded%s %s\n", C_DIM(), C_RESET(), filename);
}

/* ── handle ─────────────────────────────────────────────────────────────── */

static void register_repos(alpm_handle_t *h)
{
	size_t nrepos = 0;
	char **repos = pconf_repo_list(&nrepos);

	for (size_t i = 0; i < nrepos; i++) {
		alpm_db_t *db = alpm_register_syncdb(h, repos[i], pconf_siglevel(repos[i]));
		if (!db) {
			warn("could not register repo %s: %s", repos[i], sp_alpm_err(h));
			continue;
		}

		char *servers = pconf_repo(repos[i], "Server");
		size_t nsrv = 0;
		char **srv = split(servers, '\n', &nsrv);
		for (size_t j = 0; j < nsrv; j++)
			if (*srv[j])
				alpm_db_add_server(db, srv[j]);
		if (nsrv == 0)
			warn("repo %s has no servers configured", repos[i]);
		free(srv);
		free(servers);

		/* Usage gates what a repo may be used FOR (Sync/Search/Install/
		 * Upgrade). A repo the user restricted to Search must not become an
		 * upgrade source just because we registered it. */
		char *usage = pconf_repo(repos[i], "Usage");
		int flags = 0;
		size_t nu = 0;
		char **uw = split(usage, '\n', &nu);
		for (size_t j = 0; j < nu; j++) {
			if (!strcmp(uw[j], "All"))          flags |= ALPM_DB_USAGE_ALL;
			else if (!strcmp(uw[j], "Sync"))    flags |= ALPM_DB_USAGE_SYNC;
			else if (!strcmp(uw[j], "Search"))  flags |= ALPM_DB_USAGE_SEARCH;
			else if (!strcmp(uw[j], "Install")) flags |= ALPM_DB_USAGE_INSTALL;
			else if (!strcmp(uw[j], "Upgrade")) flags |= ALPM_DB_USAGE_UPGRADE;
		}
		alpm_db_set_usage(db, flags ? flags : ALPM_DB_USAGE_ALL);
		free(uw);
		free(usage);
	}

	pconf_free_list(repos, nrepos);
}

static void add_lines(alpm_handle_t *h, const char *directive,
                      int (*add)(alpm_handle_t *, const char *))
{
	char *raw = pconf(directive);
	size_t n = 0;
	char **words = split(raw, '\n', &n);
	for (size_t i = 0; i < n; i++)
		if (*words[i])
			add(h, words[i]);
	free(words);
	free(raw);
}

alpm_handle_t *sp_alpm_init(bool for_write)
{
	char *root   = pconf("RootDir");
	char *dbpath = pconf("DBPath");
	if (!*root) {
		free(root);
		root = xstrdup("/");
	}
	if (!*dbpath) {
		free(dbpath);
		dbpath = xstrdup("/var/lib/pacman/");
	}

	alpm_errno_t err = 0;
	alpm_handle_t *h = alpm_initialize(root, dbpath, &err);
	if (!h) {
		/* The common cause by far is the db lock held by a running pacman,
		 * so say that rather than only echoing alpm's terse string. */
		die("cannot open the package database (%s)\n"
		    "  is another package manager running?", alpm_strerror(err));
	}
	free(root);
	free(dbpath);

	add_lines(h, "CacheDir",    alpm_option_add_cachedir);
	add_lines(h, "Architecture", alpm_option_add_architecture);
	add_lines(h, "IgnorePkg",   alpm_option_add_ignorepkg);
	add_lines(h, "IgnoreGroup", alpm_option_add_ignoregroup);
	add_lines(h, "NoUpgrade",   alpm_option_add_noupgrade);
	add_lines(h, "NoExtract",   alpm_option_add_noextract);

	char *gpgdir = pconf("GPGDir");
	if (*gpgdir)
		alpm_option_set_gpgdir(h, gpgdir);
	free(gpgdir);

	char *logfile = pconf("LogFile");
	if (*logfile)
		alpm_option_set_logfile(h, logfile);
	free(logfile);

	/* pacman's own default; without it every download is serialised and a
	 * full -Syu feels an order of magnitude slower than pacman for no reason. */
	char *par = pconf("ParallelDownloads");
	alpm_option_set_parallel_downloads(h, *par ? (unsigned)atoi(par) : 5);
	free(par);

	/* The sandbox user drops privileges for the download half of a
	 * transaction. Skipping it would silently make synpkg less safe than the
	 * pacman it replaces.
	 *
	 * The directive is "DownloadUser" in pacman.conf and the libalpm setter is
	 * "sandboxuser"; asking pacman-conf for the libalpm name gets a bare
	 * "unknown directive" warning and no user, which is exactly the silent
	 * downgrade this is here to prevent. */
	char *sandbox = pconf("DownloadUser");
	if (*sandbox)
		alpm_option_set_sandboxuser(h, sandbox);
	free(sandbox);

	register_repos(h);

	if (for_write) {
		alpm_option_set_eventcb(h, cb_event, NULL);
		alpm_option_set_questioncb(h, cb_question, NULL);
		alpm_option_set_progresscb(h, cb_progress, NULL);
		alpm_option_set_dlcb(h, cb_download, NULL);
	}

	return h;
}

void sp_alpm_free(alpm_handle_t *h)
{
	if (h)
		alpm_release(h);
}
