/* synpkg — the CachyOS repository.
 *
 * The Kernel pane in SYNAPSE Settings offers the linux-cachyos kernels, and
 * they live in a repository Arch does not carry. This is the one place that
 * knows how it gets added, so the pane can ask "is it there?" and "put it
 * there" without growing its own idea of what a pacman repository is.
 *
 * The work itself is in synpkg-enable-cachyos, beside the BlackArch helper and
 * for the same reason: it is orchestration of curl, pacman-key and pacman.
 * Read its header before changing anything here — in particular, it does NOT
 * run upstream's cachyos-repo.sh, because that installs a fork of pacman over
 * the system one and adds the -march-optimised v3/v4 repositories AHEAD of
 * [core]. Neither is implied by wanting to try a kernel.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PACMAN_CONF "/etc/pacman.conf"

/* Is [cachyos] configured?
 *
 * Matched as a SECTION HEADER, not as a substring. `grep cachyos` also matches
 * the Include line, a comment, and the comment block the helper writes above
 * the section — so a substring test reports "enabled" on a machine where the
 * section was commented out, and the install that follows fails with "not
 * found in any repository" for a package the pane just said was available.
 */
bool cachyos_repo_enabled(void)
{
	FILE *f = fopen(PACMAN_CONF, "re");
	if (!f)
		return false;

	char line[512];
	bool found = false;
	while (!found && fgets(line, sizeof line, f)) {
		const char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (strncmp(p, "[cachyos]", 9) != 0)
			continue;
		/* Exactly [cachyos] — never [cachyos-v3] or [cachyos-v4], which are
		 * whole-system repositories this never adds and must not claim. */
		p += 9;
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
		found = (*p == '\0');
	}
	fclose(f);
	return found;
}

/* ⚠ TWO NAMESPACES, and they are not the same words.
 *
 *   subcmd — synpkg's own subcommand: "enable-repo" / "disable-repo"
 *   action — the helper script's argument: "enable" / "disable"
 *
 * escalate() re-execs SYNPKG, not the script, so what it is handed must be a
 * subcommand cmd_cachyos() below actually accepts. Passing the script's word
 * made the pkexec'd child run `synpkg cachyos enable`, which is not a
 * subcommand — so every install of a Cachy kernel prompted for a password and
 * then died on synpkg's own usage message, with run_quiet() swallowing it.
 *
 * Both are passed explicitly rather than derived from one another, so the one
 * place that knows both namespaces states both.
 */
static int cachyos_helper(const char *subcmd, const char *action)
{
	if (!is_root()) {
		char *a[] = { (char *)subcmd, NULL };
		return escalate("cachyos", 1, a);
	}

	/* Installed path first, then the source tree, so the helper is runnable
	 * before the package exists — same rule as the BlackArch bootstrap. */
	static const char *candidates[] = {
		"/usr/lib/synpkg/synpkg-enable-cachyos",
		"data/synpkg-enable-cachyos.sh",
	};

	for (size_t i = 0; i < sizeof candidates / sizeof *candidates; i++) {
		if (access(candidates[i], X_OK) != 0)
			continue;
		char *argv[] = { (char *)candidates[i], (char *)action, NULL };
		return run(argv, false);
	}

	die(_("the CachyOS bootstrap helper is missing "
	    "(/usr/lib/synpkg/synpkg-enable-cachyos)"));
	return 1;   /* not reached; die() exits */
}

static int cachyos_status(void)
{
	bool on = cachyos_repo_enabled();

	if (g_out == OUT_TSV) {
		tsv_row(2, on ? "enabled" : "disabled", on ? "1" : "0");
		return 0;
	}

	if (on) {
		printf("CachyOS is %senabled%s.\n", C_ACCENT(), C_RESET());
		printf("  the Kernel pane in SYNAPSE Settings can install "
		       "linux-cachyos, -lts and -hardened\n");
		printf("  remove it with: %ssynpkg cachyos disable-repo%s\n",
		       C_ACCENT(), C_RESET());
	} else {
		printf("CachyOS is %snot enabled%s — run: "
		       "%ssynpkg cachyos enable-repo%s\n",
		       C_ACCENT(), C_RESET(), C_ACCENT(), C_RESET());
	}
	return 0;
}

int cmd_cachyos(int argc, char **argv)
{
	const char *sub = argc > 0 ? argv[0] : "status";

	if (!strcmp(sub, "status"))
		return cachyos_status();
	if (!strcmp(sub, "enable-repo"))
		return cachyos_helper("enable-repo", "enable");
	if (!strcmp(sub, "disable-repo"))
		return cachyos_helper("disable-repo", "disable");

	die(_("cachyos: unknown subcommand '%s'\n"
	    "  try: status, enable-repo, disable-repo"), sub);
	return 2;   /* not reached */
}
