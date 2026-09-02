/* pconf.c — pacman.conf access, delegated to pacman-conf.
 *
 * Deliberately NOT a parser. /etc/pacman.conf has Include globs, mirrorlist
 * indirection, per-repo SigLevel that inherits from the global section, and
 * Usage flags; every one of those is a place a hand-rolled parser diverges from
 * pacman and installs from a repo the user disabled. pacman-conf resolves all
 * of it and ships inside the pacman package, which libalpm already requires.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"
#include "i18n.h"

#include <stdlib.h>
#include <string.h>

char *pconf(const char *directive)
{
	char *argv[] = { (char *)"pacman-conf", (char *)directive, NULL };
	int st = 0;
	char *out = run_capture(argv, &st, true);
	if (st != 0) {
		free(out);
		return xstrdup("");
	}
	strip_trailing_newline(out);
	return out;
}

char *pconf_repo(const char *repo, const char *directive)
{
	char *flag = xasprintf("--repo=%s", repo);
	char *argv[] = { (char *)"pacman-conf", flag, (char *)directive, NULL };
	int st = 0;
	char *out = run_capture(argv, &st, true);
	free(flag);
	if (st != 0) {
		free(out);
		return xstrdup("");
	}
	strip_trailing_newline(out);
	return out;
}

char **pconf_repo_list(size_t *n)
{
	char *argv[] = { (char *)"pacman-conf", (char *)"--repo-list", NULL };
	int st = 0;
	char *out = run_capture(argv, &st, true);
	strip_trailing_newline(out);

	size_t count = 0;
	char **fields = split(out, '\n', &count);

	/* Hand back owned copies and free the capture buffer: callers keep the
	 * list well past this call and pointing them into a freed buffer is the
	 * kind of bug that only shows up under a different allocator. */
	char **list = xmalloc((count ? count : 1) * sizeof *list);
	size_t kept = 0;
	for (size_t i = 0; i < count; i++)
		if (*fields[i])
			list[kept++] = xstrdup(fields[i]);

	free(fields);
	free(out);
	*n = kept;
	return list;
}

void pconf_free_list(char **list, size_t n)
{
	for (size_t i = 0; i < n; i++)
		free(list[i]);
	free(list);
}

/* pacman-conf prints one SigLevel word per line, already resolved against the
 * global section. The mapping mirrors pacman's own process_siglevel(). */
int pconf_siglevel(const char *repo)
{
	char *raw = pconf_repo(repo, "SigLevel");
	if (!*raw) {
		free(raw);
		return ALPM_SIG_USE_DEFAULT;
	}

	int level = 0;
	size_t n = 0;
	char **words = split(raw, '\n', &n);

	for (size_t i = 0; i < n; i++) {
		const char *w = words[i];

		if (!strcmp(w, "PackageNever")) {
			level &= ~ALPM_SIG_PACKAGE;
		} else if (!strcmp(w, "PackageOptional")) {
			level |= ALPM_SIG_PACKAGE | ALPM_SIG_PACKAGE_OPTIONAL;
		} else if (!strcmp(w, "PackageRequired")) {
			level |= ALPM_SIG_PACKAGE;
			level &= ~ALPM_SIG_PACKAGE_OPTIONAL;
		} else if (!strcmp(w, "PackageTrustedOnly")) {
			level &= ~(ALPM_SIG_PACKAGE_MARGINAL_OK | ALPM_SIG_PACKAGE_UNKNOWN_OK);
		} else if (!strcmp(w, "PackageTrustAll")) {
			level |= ALPM_SIG_PACKAGE_MARGINAL_OK | ALPM_SIG_PACKAGE_UNKNOWN_OK;
		} else if (!strcmp(w, "DatabaseNever")) {
			level &= ~ALPM_SIG_DATABASE;
		} else if (!strcmp(w, "DatabaseOptional")) {
			level |= ALPM_SIG_DATABASE | ALPM_SIG_DATABASE_OPTIONAL;
		} else if (!strcmp(w, "DatabaseRequired")) {
			level |= ALPM_SIG_DATABASE;
			level &= ~ALPM_SIG_DATABASE_OPTIONAL;
		} else if (!strcmp(w, "DatabaseTrustedOnly")) {
			level &= ~(ALPM_SIG_DATABASE_MARGINAL_OK | ALPM_SIG_DATABASE_UNKNOWN_OK);
		} else if (!strcmp(w, "DatabaseTrustAll")) {
			level |= ALPM_SIG_DATABASE_MARGINAL_OK | ALPM_SIG_DATABASE_UNKNOWN_OK;
		} else if (g_verbose) {
			warn(_("unrecognised SigLevel '%s' for repo %s"), w, repo);
		}
	}

	free(words);
	free(raw);
	return level;
}
