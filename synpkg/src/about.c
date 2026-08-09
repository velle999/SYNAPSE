/* about.c — what this program is, and which of its sources are actually wired
 * up on THIS machine.
 *
 * This is not a credits screen. Every source synpkg can install from is
 * optional at runtime — Flathub needs a remote added, BlackArch needs a
 * bootstrap, the AUR needs curl, SynapseOS components need syn-update — and
 * until now the only way to discover that one of them was missing was to open
 * its tab and find it empty. An empty pane reads as "there is nothing here",
 * which is a different claim from "this source is switched off".
 *
 * So every row carries a STATE as well as a value, and the front-ends colour
 * it: ok, off (present but not enabled), missing (not installed at all), info.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void about_header(void)
{
	if (g_out == OUT_TSV)
		tsv_row(4, "item", "state", "value", "detail");
}

static void about_row(const char *item, const char *state, const char *value,
                      const char *detail)
{
	if (g_out == OUT_TSV) {
		tsv_row(4, item, state, value, detail ? detail : "");
		return;
	}

	const char *colour = !strcmp(state, "ok")      ? C_OK()
	                   : !strcmp(state, "off")     ? C_WARN()
	                   : !strcmp(state, "missing") ? C_DIM()
	                                               : C_ACCENT();

	printf("  %s%-14s%s %s%s%s\n", C_DIM(), item, C_RESET(), colour, value,
	       C_RESET());
	if (detail && *detail)
		printf("  %-14s %s%s%s\n", "", C_DIM(), detail, C_RESET());
}

/* Which sync databases are registered, joined for the detail column. Reads
 * pacman.conf through pacman-conf like everything else here, so a repo the
 * user commented out does not appear. */
static char *repo_summary(size_t *count)
{
	size_t n = 0;
	char **repos = pconf_repo_list(&n);
	*count = n;

	size_t len = 1;
	for (size_t i = 0; i < n; i++)
		len += strlen(repos[i]) + 2;

	char *out = xmalloc(len);
	size_t k = 0;
	for (size_t i = 0; i < n; i++)
		k += (size_t)snprintf(out + k, len - k, "%s%s", i ? "  " : "", repos[i]);
	out[k] = '\0';

	pconf_free_list(repos, n);
	return out;
}

int cmd_about(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	if (g_out == OUT_HUMAN)
		printf("\n  %ssynpkg%s %s— the SynapseOS package manager%s\n\n", C_BOLD(),
		       C_RESET(), C_DIM(), C_RESET());

	about_header();

	about_row("Version", "info", SYNPKG_VERSION, "GPL-2.0-or-later");
	about_row("Project", "info", "SynapseOS",
	          "https://github.com/velle999/SYNAPSE");
	about_row("libalpm", "info", alpm_version(),
	          "packages, dependencies and transactions");

	/* ── the four sources, in the order the front-ends list them ─────────── */

	size_t nrepos = 0;
	char *repos = repo_summary(&nrepos);
	char *repo_value = xasprintf("%zu configured", nrepos);
	about_row("Repositories", nrepos ? "ok" : "off", repo_value, repos);
	free(repo_value);
	free(repos);

	if (have_cmd("curl"))
		about_row("AUR", "ok", "available",
		          have_cmd("makepkg")
		              ? "aur.archlinux.org — makepkg builds locally"
		              : "aur.archlinux.org — install base-devel to build");
	else
		about_row("AUR", "missing", "curl is not installed",
		          "synpkg install curl");

	if (!sp_flatpak_present())
		about_row("Flathub", "missing", "flatpak is not installed",
		          "synpkg install flatpak");
	else if (sp_flathub_enabled() && !sp_appstream_present())
		/* The state that looks exactly like a working Flathub with nothing in
		 * it: the remote is there, so nothing reports an error, but every
		 * search and every category comes back empty. Any remote added by
		 * anything other than enable-flathub starts here. */
		about_row("Flathub", "off", "enabled, but no application index",
		          "synpkg flatpak enable-flathub");
	else if (sp_flathub_enabled())
		about_row("Flathub", "ok", "enabled", SYNPKG_FLATHUB_URL);
	else
		about_row("Flathub", "off", "no Flathub remote",
		          "synpkg flatpak enable-flathub");

	/* BlackArch is a sync db like any other, so its presence is an ALPM
	 * question rather than a command-on-PATH one. */
	alpm_handle_t *h = sp_alpm_init(false);
	alpm_db_t *ba = NULL;
	for (alpm_list_t *d = sp_syncdbs(h); d && !ba; d = d->next)
		if (!strcmp(alpm_db_get_name(d->data), "blackarch"))
			ba = d->data;

	if (ba) {
		int n = alpm_list_count(alpm_db_get_pkgcache(ba));
		char *value = xasprintf("enabled, %d tools", n);
		about_row("BlackArch", n ? "ok" : "off", value,
		          n ? "" : "configured but never synced — synpkg refresh");
		free(value);
	} else {
		about_row("BlackArch", "off", "not enabled",
		          "synpkg arsenal enable-repo");
	}
	sp_alpm_free(h);

	if (have_cmd("syn-update"))
		about_row("SynapseOS", "ok", "syn-update present",
		          "rebuilds this system's own components from git");
	else
		about_row("SynapseOS", "missing", "syn-update is not installed",
		          "SynapseOS components cannot be checked");

	/* ── the rest ────────────────────────────────────────────────────────── */

	char *cat = sp_curated_path();
	if (access(cat, R_OK) == 0) {
		char *value = xasprintf("%zu suggestions", sp_curated_count());
		about_row("Catalogue", "ok", value, cat);
		free(value);
	} else {
		about_row("Catalogue", "missing", "not found", cat);
	}
	free(cat);

	about_row("Front-ends", "info", "CLI, terminal browser, quickshell",
	          have_cmd("quickshell") ? "synpkg gui" : "install quickshell for the GUI");

	/* A detail beginning https:// is a LINK, and the front-ends open it in a
	 * browser rather than handing it to a shell the way they do a
	 * "synpkg ..." detail. Keeping those two apart is why the GUI has
	 * separate openable/runnable tests rather than one "clickable". */
	about_row("Support", "info", "Buy me a coffee", SYNAPSE_DONATE_URL);

	if (g_out == OUT_HUMAN)
		putchar('\n');
	return 0;
}
