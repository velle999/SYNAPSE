/* curated.c — the suggested-software catalogue.
 *
 * The catalogue is DATA, not code: /usr/share/synpkg/curated.tsv. Adding an
 * application is a one-line edit that needs no rebuild, which is the only way a
 * curated list stays curated rather than ossifying at whatever was popular the
 * week it was compiled in.
 *
 * Format, one entry per line, five tab-separated fields:
 *   category <TAB> id <TAB> source <TAB> label <TAB> description
 * where source is repo|aur|flatpak. Lines starting with # are comments.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"
#include "i18n.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
	char *category, *id, *source, *label, *desc;
} entry_t;

/* Run-from-source: resolve next to the binary when the package is not
 * installed, so the catalogue is editable and testable without packaging. */
static char *catalogue_path(void)
{
	const char *env = getenv("SYNPKG_CURATED");
	if (env && *env)
		return xstrdup(env);

	char *installed = xasprintf("%s/curated.tsv", SYNPKG_DATADIR);
	if (access(installed, R_OK) == 0)
		return installed;
	free(installed);

	return xstrdup("data/curated.tsv");
}

char *sp_curated_path(void)
{
	return catalogue_path();
}

static entry_t *load(size_t *count, char **backing)
{
	char *path = catalogue_path();
	FILE *f = fopen(path, "r");
	if (!f)
		die(_("cannot read the suggestion catalogue at %s"), path);
	free(path);

	size_t cap = 4096, len = 0;
	char *text = xmalloc(cap);
	for (;;) {
		if (len + 1 >= cap) {
			cap *= 2;
			text = xrealloc(text, cap);
		}
		size_t n = fread(text + len, 1, cap - len - 1, f);
		if (n == 0)
			break;
		len += n;
	}
	text[len] = '\0';
	fclose(f);

	size_t nlines = 0;
	char **lines = split(text, '\n', &nlines);

	entry_t *out = xmalloc((nlines ? nlines : 1) * sizeof *out);
	size_t k = 0;

	for (size_t i = 0; i < nlines; i++) {
		if (!*lines[i] || lines[i][0] == '#')
			continue;
		size_t nf = 0;
		char **f = split(lines[i], '\t', &nf);
		/* A short line is a typo in the catalogue, not user input — say so
		 * rather than rendering a row with empty columns. */
		if (nf < 5)
			warn(_("catalogue line %zu has %zu fields, expected 5"), i + 1, nf);
		else
			out[k++] = (entry_t){ f[0], f[1], f[2], f[3], f[4] };
		free(f);
	}

	free(lines);
	*backing = text;   /* entries point into it; caller frees last */
	*count = k;
	return out;
}

/* One `flatpak list` for the whole catalogue rather than one `flatpak info` per
 * row: the second shape turns a 60-entry list into 60 process spawns. */
static char *flatpak_installed_ids(void)
{
	if (!have_cmd("flatpak"))
		return xstrdup("");
	char *argv[] = { (char *)"flatpak", (char *)"list", (char *)"--app",
	                 (char *)"--columns=application", NULL };
	int st = 0;
	char *out = run_capture(argv, &st, true);
	if (st != 0) {
		free(out);
		return xstrdup("");
	}
	return out;
}

static bool line_present(const char *haystack, const char *id)
{
	size_t n = strlen(id);
	for (const char *p = haystack; p && *p; ) {
		if (!strncmp(p, id, n) && (p[n] == '\n' || p[n] == '\0'))
			return true;
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	return false;
}

int cmd_suggest(int argc, char **argv)
{
	const char *want_category = NULL;
	bool list_categories = false, only_missing = false;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "categories"))
			list_categories = true;
		else if (!strcmp(argv[i], "--missing"))
			only_missing = true;
		else if (argv[i][0] == '-')
			die(_("suggest: unknown option '%s'"), argv[i]);
		else
			want_category = argv[i];
	}

	char *backing = NULL;
	size_t n = 0;
	entry_t *entries = load(&n, &backing);

	alpm_handle_t *h = sp_alpm_init(false);
	alpm_db_t *local = alpm_get_localdb(h);
	char *fp = flatpak_installed_ids();

	if (list_categories) {
		/* Same three columns as `arsenal categories`, deliberately: the GUI's
		 * category pane is one component serving both, and a pane that had to
		 * know which source it was rendering would drift the moment either
		 * side gained a column. The installed count is what makes the pane a
		 * progress view rather than a menu — you can see at a glance which
		 * categories you have already worked through. */
		if (g_out == OUT_TSV)
			tsv_row(4, "category", "total", "installed", "label");

		/* Preserve catalogue order — it is an editorial ordering, and sorting
		 * it alphabetically would put "Accessories" above "Browsers". */
		for (size_t i = 0; i < n; i++) {
			bool seen = false;
			for (size_t j = 0; j < i && !seen; j++)
				seen = !strcmp(entries[j].category, entries[i].category);
			if (seen)
				continue;

			int total = 0, have = 0;
			for (size_t j = 0; j < n; j++) {
				if (strcmp(entries[j].category, entries[i].category))
					continue;
				total++;
				have += !strcmp(entries[j].source, "flatpak")
				            ? line_present(fp, entries[j].id)
				            : alpm_db_get_pkg(local, entries[j].id) != NULL;
			}

			if (g_out == OUT_TSV) {
				char *t = xasprintf("%d", total);
				char *c = xasprintf("%d", have);
				/* The catalogue's category name is already the display name,
				 * so label repeats it — a column that is sometimes redundant
				 * beats a pane that has to know which source it is showing. */
				tsv_row(4, entries[i].category, t, c, entries[i].category);
				free(t);
				free(c);
			} else {
				printf("%s%-20s%s %s%d%s", C_BOLD(), entries[i].category,
				       C_RESET(), C_DIM(), total, C_RESET());
				if (have)
					printf("  %s%d installed%s", C_OK(), have, C_RESET());
				putchar('\n');
			}
		}
		free(fp);
		sp_alpm_free(h);
		goto done;
	}

	if (g_out == OUT_TSV)
		tsv_row(6, "category", "id", "source", "label", "installed", "description");

	const char *heading = NULL;
	for (size_t i = 0; i < n; i++) {
		entry_t *e = &entries[i];
		if (want_category && strcasecmp(e->category, want_category))
			continue;

		bool installed = !strcmp(e->source, "flatpak")
		                     ? line_present(fp, e->id)
		                     : alpm_db_get_pkg(local, e->id) != NULL;
		if (only_missing && installed)
			continue;

		if (g_out == OUT_TSV) {
			tsv_row(6, e->category, e->id, e->source, e->label,
			        installed ? "1" : "0", e->desc);
			continue;
		}

		if (!heading || strcmp(heading, e->category)) {
			printf("%s%s%s%s\n", heading ? "\n" : "", C_BOLD(), e->category,
			       C_RESET());
			heading = e->category;
		}
		printf("  %s %s%-22s%s %s%s%s\n",
		       installed ? "✓" : " ",
		       C_ACCENT(), e->label, C_RESET(),
		       C_DIM(), e->desc, C_RESET());
		if (g_verbose)
			printf("      %s%s: %s%s\n", C_DIM(), e->source, e->id, C_RESET());
	}

	free(fp);
	sp_alpm_free(h);

done:
	free(entries);
	free(backing);
	return 0;
}

/* Parses the catalogue purely to count it. Callers must check the file is
 * readable first — load() dies on a missing catalogue, which is right for
 * `suggest` and wrong for an About pane whose whole job is to report that the
 * file is gone. */
size_t sp_curated_count(void)
{
	size_t n = 0;
	char *backing = NULL;
	entry_t *entries = load(&n, &backing);
	free(entries);
	free(backing);
	return n;
}
