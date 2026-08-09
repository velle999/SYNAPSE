/* tui.c — the terminal browser.
 *
 * Line-oriented on purpose: no ncurses, no alternate screen, no raw mode. A
 * crashed full-screen TUI leaves the terminal in mouse-reporting mode and the
 * user's shell unusable, and this is the front-end most likely to be run over
 * SSH into a half-broken machine. Everything here degrades to a scrollback log.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PAGE 20

typedef struct {
	char *name, *version, *desc;
	bool installed;
} row_t;

typedef struct {
	row_t *rows;
	size_t n, cap;
} list_t;

static void list_add(list_t *l, const char *name, bool installed,
                     const char *version, const char *desc)
{
	if (l->n + 1 >= l->cap) {
		l->cap = l->cap ? l->cap * 2 : 64;
		l->rows = xrealloc(l->rows, l->cap * sizeof *l->rows);
	}
	l->rows[l->n++] = (row_t){ xstrdup(name), xstrdup(version ? version : ""),
	                           xstrdup(desc ? desc : ""), installed };
}

static void list_free(list_t *l)
{
	for (size_t i = 0; i < l->n; i++) {
		free(l->rows[i].name);
		free(l->rows[i].version);
		free(l->rows[i].desc);
	}
	free(l->rows);
	*l = (list_t){0};
}

static char *prompt(const char *text)
{
	static char line[256];
	fputs(text, stdout);
	fflush(stdout);
	if (!fgets(line, sizeof line, stdin))
		return NULL;
	strip_trailing_newline(line);
	return line;
}

static void banner(void)
{
	printf("\n%s╭─ SYNAPSE Software ─────────────────────────────────────────╮%s\n",
	       C_ACCENT(), C_RESET());
	printf("%s│%s  %ssynpkg%s — packages, arsenal, and the system itself       %s│%s\n",
	       C_ACCENT(), C_RESET(), C_BOLD(), C_RESET(), C_ACCENT(), C_RESET());
	printf("%s╰────────────────────────────────────────────────────────────╯%s\n",
	       C_ACCENT(), C_RESET());
}

/* ── package pane ───────────────────────────────────────────────────────── */

/* Returns when the user backs out. `l` is consumed. */
static void browse(list_t *l, const char *title)
{
	size_t page = 0;

	for (;;) {
		size_t start = page * PAGE;
		if (start >= l->n && l->n)
			start = page = 0;

		printf("\n%s%s%s  %s(%zu)%s\n", C_BOLD(), title, C_RESET(), C_DIM(),
		       l->n, C_RESET());

		if (!l->n) {
			printf("  %snothing here%s\n", C_DIM(), C_RESET());
			return;
		}

		for (size_t i = start; i < l->n && i < start + PAGE; i++) {
			row_t *r = &l->rows[i];
			printf("  %s%3zu%s %s %s%-28.28s%s %s%.44s%s\n",
			       C_DIM(), i + 1, C_RESET(),
			       r->installed ? "✓" : " ",
			       C_ACCENT(), r->name, C_RESET(),
			       C_DIM(), r->desc, C_RESET());
		}

		size_t pages = (l->n + PAGE - 1) / PAGE;
		printf("\n  %spage %zu/%zu · [n]ext [p]rev · i <num> install · "
		       "r <num> remove · s <num> info · [q]back%s\n",
		       C_DIM(), page + 1, pages, C_RESET());

		char *in = prompt("  > ");
		if (!in || !strcmp(in, "q") || !strcmp(in, ""))
			return;

		if (!strcmp(in, "n")) {
			if (page + 1 < pages)
				page++;
			continue;
		}
		if (!strcmp(in, "p")) {
			if (page)
				page--;
			continue;
		}

		char verb = in[0];
		if (verb != 'i' && verb != 'r' && verb != 's') {
			printf("  %sunrecognised — try i/r/s <number>, n, p or q%s\n",
			       C_WARN(), C_RESET());
			continue;
		}

		long num = strtol(in + 1, NULL, 10);
		if (num < 1 || (size_t)num > l->n) {
			printf("  %sno row %ld%s\n", C_WARN(), num, C_RESET());
			continue;
		}
		row_t *r = &l->rows[num - 1];

		char *one[] = { r->name, NULL };
		if (verb == 's') {
			cmd_info(1, one);
		} else if (verb == 'i') {
			if (cmd_install(1, one) == 0)
				r->installed = true;
		} else {
			if (cmd_remove(1, one) == 0)
				r->installed = false;
		}
	}
}

/* ── screens ────────────────────────────────────────────────────────────── */

static void screen_updates(void)
{
	alpm_handle_t *h = sp_alpm_init(false);
	alpm_list_t *syncdbs = sp_syncdbs(h);
	list_t l = {0};

	for (alpm_list_t *i = alpm_db_get_pkgcache(alpm_get_localdb(h)); i; i = i->next) {
		alpm_pkg_t *old = i->data;
		alpm_pkg_t *new = alpm_sync_get_new_version(old, syncdbs);
		if (!new)
			continue;
		char *what = xasprintf("%s -> %s", alpm_pkg_get_version(old),
		                       alpm_pkg_get_version(new));
		list_add(&l, alpm_pkg_get_name(old), true, alpm_pkg_get_version(new), what);
		free(what);
	}
	sp_alpm_free(h);

	if (!l.n) {
		printf("\n  %severything is up to date%s\n", C_OK(), C_RESET());
		list_free(&l);
		return;
	}

	printf("\n  %zu package%s can be upgraded.\n", l.n, l.n == 1 ? "" : "s");
	if (confirm("  upgrade the whole system now?")) {
		list_free(&l);
		cmd_upgrade(0, NULL);
		return;
	}
	browse(&l, "Available updates");
	list_free(&l);
}

static void screen_search(void)
{
	char *term = prompt("\n  search: ");
	if (!term || !*term)
		return;
	char *copy = xstrdup(term);

	alpm_handle_t *h = sp_alpm_init(false);
	alpm_list_t *needles = alpm_list_add(NULL, copy);
	list_t l = {0};

	for (alpm_list_t *d = sp_syncdbs(h); d; d = d->next) {
		alpm_list_t *res = NULL;
		if (alpm_db_search(d->data, needles, &res) != 0)
			continue;
		for (alpm_list_t *i = res; i; i = i->next) {
			alpm_pkg_t *p = i->data;
			list_add(&l, alpm_pkg_get_name(p),
			         alpm_db_get_pkg(alpm_get_localdb(h), alpm_pkg_get_name(p)) != NULL,
			         alpm_pkg_get_version(p), alpm_pkg_get_desc(p));
		}
		alpm_list_free(res);
	}
	alpm_list_free(needles);
	sp_alpm_free(h);

	char *title = xasprintf("Results for \"%s\"", copy);
	browse(&l, title);
	free(title);
	free(copy);
	list_free(&l);
}

static void screen_suggest(void)
{
	/* The catalogue renderer already knows how to lay itself out, and it is
	 * the same data the GUI shows. Re-listing it here would be a second
	 * ordering to keep in step. */
	char *args[] = { (char *)"--missing" };
	printf("\n  %sSuggested software you do not have yet%s\n", C_BOLD(), C_RESET());
	cmd_suggest(1, args);

	char *pick = prompt("\n  install which? (name, or blank to go back): ");
	if (pick && *pick) {
		char *one[] = { pick, NULL };
		cmd_install(1, one);
	}
}

static void screen_arsenal(void)
{
	alpm_handle_t *h = sp_alpm_init(false);
	alpm_db_t *ba = NULL;
	for (alpm_list_t *d = sp_syncdbs(h); d && !ba; d = d->next)
		if (!strcmp(alpm_db_get_name(d->data), "blackarch"))
			ba = d->data;

	if (!ba) {
		sp_alpm_free(h);
		printf("\n  %sBlackArch is not enabled.%s\n", C_WARN(), C_RESET());
		if (confirm("  enable it now? (downloads and verifies upstream's bootstrap)")) {
			char *args[] = { (char *)"enable-repo" };
			cmd_arsenal(1, args);
		}
		return;
	}

	/* Category list, then packages in one. */
	list_t cats = {0};
	alpm_db_t *local = alpm_get_localdb(h);
	for (alpm_list_t *g = alpm_db_get_groupcache(ba); g; g = g->next) {
		alpm_group_t *grp = g->data;
		if (strncmp(grp->name, "blackarch-", 10))
			continue;
		int total = 0, have = 0;
		for (alpm_list_t *p = grp->packages; p; p = p->next) {
			total++;
			have += alpm_db_get_pkg(local, alpm_pkg_get_name(p->data)) != NULL;
		}
		char *desc = xasprintf("%d tools%s", total, have ? ", some installed" : "");
		list_add(&cats, grp->name, have > 0, "", desc);
		free(desc);
	}

	for (;;) {
		printf("\n  %sBlackArch categories%s %s(%zu)%s\n", C_BOLD(), C_RESET(),
		       C_DIM(), cats.n, C_RESET());
		for (size_t i = 0; i < cats.n; i++)
			printf("  %s%3zu%s %s %-32s %s%s%s\n", C_DIM(), i + 1, C_RESET(),
			       cats.rows[i].installed ? "✓" : " ", cats.rows[i].name,
			       C_DIM(), cats.rows[i].desc, C_RESET());

		char *in = prompt("\n  category number ([i] installed tools, [q] back): ");
		if (!in || !*in || !strcmp(in, "q"))
			break;

		if (!strcmp(in, "i")) {
			list_t inst = {0};
			for (alpm_list_t *i = alpm_db_get_pkgcache(local); i; i = i->next) {
				alpm_pkg_t *p = i->data;
				bool is_ba = false;
				for (alpm_list_t *g = alpm_pkg_get_groups(p); g && !is_ba; g = g->next)
					is_ba = !strncmp(g->data, "blackarch", 9);
				if (is_ba)
					list_add(&inst, alpm_pkg_get_name(p), true,
					         alpm_pkg_get_version(p), alpm_pkg_get_desc(p));
			}
			browse(&inst, "Installed BlackArch tools");
			list_free(&inst);
			continue;
		}

		long num = strtol(in, NULL, 10);
		if (num < 1 || (size_t)num > cats.n) {
			printf("  %sno category %ld%s\n", C_WARN(), num, C_RESET());
			continue;
		}

		alpm_group_t *grp = alpm_db_get_group(ba, cats.rows[num - 1].name);
		list_t pkgs = {0};
		for (alpm_list_t *p = grp ? grp->packages : NULL; p; p = p->next) {
			alpm_pkg_t *pkg = p->data;
			list_add(&pkgs, alpm_pkg_get_name(pkg),
			         alpm_db_get_pkg(local, alpm_pkg_get_name(pkg)) != NULL,
			         alpm_pkg_get_version(pkg), alpm_pkg_get_desc(pkg));
		}
		browse(&pkgs, cats.rows[num - 1].name);
		list_free(&pkgs);
	}

	list_free(&cats);
	sp_alpm_free(h);
}

static void screen_installed(void)
{
	alpm_handle_t *h = sp_alpm_init(false);
	list_t l = {0};
	for (alpm_list_t *i = alpm_db_get_pkgcache(alpm_get_localdb(h)); i; i = i->next) {
		alpm_pkg_t *p = i->data;
		if (alpm_pkg_get_reason(p) != ALPM_PKG_REASON_EXPLICIT)
			continue;
		list_add(&l, alpm_pkg_get_name(p), true, alpm_pkg_get_version(p),
		         alpm_pkg_get_desc(p));
	}
	sp_alpm_free(h);
	browse(&l, "Installed (explicitly)");
	list_free(&l);
}

/* ── main loop ──────────────────────────────────────────────────────────── */

int cmd_tui(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	if (!isatty(STDIN_FILENO))
		die("the terminal browser needs an interactive terminal");

	banner();

	for (;;) {
		printf("\n  %s1%s  Updates          %s2%s  Search           %s3%s  Suggested apps\n",
		       C_ACCENT(), C_RESET(), C_ACCENT(), C_RESET(), C_ACCENT(), C_RESET());
		printf("  %s4%s  Arsenal          %s5%s  Installed        %s6%s  SynapseOS itself\n",
		       C_ACCENT(), C_RESET(), C_ACCENT(), C_RESET(), C_ACCENT(), C_RESET());
		printf("  %sq%s  Quit\n", C_ACCENT(), C_RESET());

		char *in = prompt("\n  > ");
		if (!in || !*in || !strcmp(in, "q"))
			break;

		switch (in[0]) {
		case '1': screen_updates(); break;
		case '2': screen_search(); break;
		case '3': screen_suggest(); break;
		case '4': screen_arsenal(); break;
		case '5': screen_installed(); break;
		case '6': {
			char *args[] = { (char *)"check" };
			cmd_system(1, args);
			if (confirm("\n  apply SynapseOS updates now? (opens a build)")) {
				char *ap[] = { (char *)"apply" };
				cmd_system(1, ap);
			}
			break;
		}
		default:
			printf("  %sunrecognised choice%s\n", C_WARN(), C_RESET());
		}
	}

	printf("\n");
	return 0;
}
