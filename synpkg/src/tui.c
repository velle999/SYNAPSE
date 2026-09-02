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
#include "i18n.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

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

/*
 * ⛔ THE BOX IS DRAWN, NOT WRITTEN. A fixed-width frame with a hand-counted
 * run of ─ and a hand-counted run of spaces is right in exactly one language:
 * the first translation longer or shorter than the English pushes the right
 * edge off and the box stops closing. The rules and the padding are computed
 * from the text now, so the words are free to be any length — which is also
 * what lets them be marked at all.
 *
 * ⚠ IN COLUMNS, NOT BYTES. "Software" is 8 of each; ソフトウェア is 18 bytes
 * and 12 columns, and Arabic is neither. mbstowcs + wcswidth is what the frame
 * has to be measured in, and it is the only reason this looks like more work
 * than a printf.
 */
static int text_cols(const char *s)
{
	size_t need = mbstowcs(NULL, s, 0);
	if (need == (size_t)-1) return (int)strlen(s);   /* not this locale's encoding */
	wchar_t *w = calloc(need + 1, sizeof *w);
	if (!w) return (int)strlen(s);
	mbstowcs(w, s, need + 1);
	int cols = wcswidth(w, need);
	free(w);
	return cols < 0 ? (int)strlen(s) : cols;
}

static void rule(const char *left, const char *right, int cols)
{
	printf("%s%s", C_ACCENT(), left);
	for (int i = 0; i < cols; i++) fputs("─", stdout);
	printf("%s%s\n", right, C_RESET());
}

#define TUI_BOX_W 60

static void banner(void)
{
	const char *title = _("SYNAPSE Software");
	const char *sub   = _("packages, arsenal, and the system itself");
	int tw = text_cols(title), sw = text_cols(sub);

	printf("\n%s╭─ %s ", C_ACCENT(), title);
	for (int i = 0; i < TUI_BOX_W - tw - 3; i++) fputs("─", stdout);
	printf("╮%s\n", C_RESET());

	/* ⚠ synpkg is the program's NAME and stays; the em dash and the phrase
	 * after it are the sentence. */
	printf("%s│%s  %ssynpkg%s — %s", C_ACCENT(), C_RESET(), C_BOLD(), C_RESET(), sub);
	for (int i = 0; i < TUI_BOX_W - sw - 11; i++) putchar(' ');
	printf("%s│%s\n", C_ACCENT(), C_RESET());

	rule("╰", "╯", TUI_BOX_W);
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
			printf("  %s%s%s\n", C_DIM(), _("nothing here"), C_RESET());
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
		/* ⚠ THE BRACKETED LETTERS ARE KEYS. [n], [p], [q] and the bare
		 * i/r/s are what the prompt below reads back, so a translated one
		 * names a key that does nothing. The words around them are ours. */
		printf("\n  %s", C_DIM());
		printf(_("page %zu/%zu · [n]ext [p]rev · i <num> install · "
		         "r <num> remove · s <num> info · [q]back"), page + 1, pages);
		printf("%s\n", C_RESET());

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
			printf("  %s%s%s\n", C_WARN(), _("unrecognised — try i/r/s <number>, n, p or q"), C_RESET());
			continue;
		}

		long num = strtol(in + 1, NULL, 10);
		if (num < 1 || (size_t)num > l->n) {
			printf("  %s", C_WARN());
			printf(_("no row %ld"), num);
			printf("%s\\n", C_RESET());
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
		printf("\n  %s%s%s\n", C_OK(), _("everything is up to date"), C_RESET());
		list_free(&l);
		return;
	}

	printf("\n  ");
	printf(P_("%zu package can be upgraded.\n",
	          "%zu packages can be upgraded.\n", l.n), l.n);
	if (confirm(_("  upgrade the whole system now?"))) {
		list_free(&l);
		cmd_upgrade(0, NULL);
		return;
	}
	browse(&l, "Available updates");
	list_free(&l);
}

static void screen_search(void)
{
	char *term = prompt(_("\n  search: "));
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

	char *title = xasprintf(_("Results for \"%s\""), copy);
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
	printf("\n  %s%s%s\n", C_BOLD(), _("Suggested software you do not have yet"), C_RESET());
	cmd_suggest(1, args);

	char *pick = prompt(_("\n  install which? (name, or blank to go back): "));
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
		printf("\n  %s%s%s\n", C_WARN(), _("BlackArch is not enabled."), C_RESET());
		if (confirm(_("  enable it now? (downloads and verifies upstream's bootstrap)"))) {
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
		char *desc = have
		    ? xasprintf(P_("%d tool, some installed",
		                   "%d tools, some installed", total), total)
		    : xasprintf(P_("%d tool", "%d tools", total), total);
		list_add(&cats, grp->name, have > 0, "", desc);
		free(desc);
	}

	for (;;) {
		printf("\n  %s%s%s %s(%zu)%s\n", C_BOLD(), _("BlackArch categories"), C_RESET(), C_BOLD(), C_RESET(),
		       C_DIM(), cats.n, C_RESET());
		for (size_t i = 0; i < cats.n; i++)
			printf("  %s%3zu%s %s %-32s %s%s%s\n", C_DIM(), i + 1, C_RESET(),
			       cats.rows[i].installed ? "✓" : " ", cats.rows[i].name,
			       C_DIM(), cats.rows[i].desc, C_RESET());

		char *in = prompt(_("\n  category number ([i] installed tools, [q] back): "));
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
			printf("  %s", C_WARN());
			printf(_("no category %ld"), num);
			printf("%s\n", C_RESET());
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

/* The AUR and Flathub screens deliberately do NOT go through browse(). That
 * helper's install path is cmd_install(), which is pacman — pointing it at an
 * AUR name would report "target not found" and pointing it at an application
 * id would be worse. Each source installs with its own tool or not at all. */
static void screen_aur(void)
{
	char *term = prompt(_("\n  search the AUR: "));
	if (!term || !*term)
		return;

	char *copy = xstrdup(term);
	printf("\n");
	aur_search_term(copy);
	free(copy);

	char *pick = prompt(_("\n  build which? (name, or blank to go back): "));
	if (!pick || !*pick)
		return;

	/* aur install shows the PKGBUILD and prompts before makepkg runs, so
	 * there is nothing to confirm here that it does not confirm better. */
	char *args[] = { (char *)"install", pick };
	cmd_aur(2, args);
}

static void screen_flathub(void)
{
	if (!sp_flatpak_present()) {
		printf("\n  %s%s%s\n", C_WARN(), _("flatpak is not installed."), C_RESET());
		if (confirm(_("  install it now?"))) {
			char *one[] = { (char *)"flatpak" };
			cmd_install(1, one);
		}
		return;
	}

	if (!sp_flathub_enabled()) {
		printf("\n  %s%s%s\n", C_WARN(), _("No Flathub remote is configured."), C_RESET());
		printf("  %s%s%s\n", C_DIM(), _("Searching without one silently returns nothing."), C_RESET());
		if (confirm(_("  enable Flathub now? (adds the remote and fetches its index)"))) {
			char *args[] = { (char *)"enable-flathub" };
			cmd_flatpak(1, args);
		}
		return;
	}

	char *term = prompt(_("\n  search Flathub: "));
	if (!term || !*term)
		return;

	char *copy = xstrdup(term);
	printf("\n");
	char *args[] = { (char *)"search", copy };
	cmd_flatpak(2, args);
	free(copy);

	char *pick = prompt(_("\n  install which? (application id, or blank to go back): "));
	if (pick && *pick) {
		char *ins[] = { (char *)"install", pick };
		cmd_flatpak(2, ins);
	}
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
		/* Grouped by SOURCE, in the same order the GUI's nav lists them:
		 * repositories, the AUR, Flathub, BlackArch, SynapseOS. Two
		 * front-ends that disagree about where software comes from is the
		 * confusion this program exists to remove. */
		/* ⛔ LAID OUT, NOT TYPED. Three columns of hand-counted padding are
		 * right in exactly one language — the first translation of a
		 * different length shears the grid. The column is as wide as the
		 * widest label, measured in COLUMNS (see text_cols), so the same
		 * code draws English, ソフトウェア and العربية.
		 *
		 * ⚠ The DIGIT is what the prompt reads back and is not a word. */
		static const char *const menu[] = {
			N_("Updates"),   N_("Repositories"), N_("Suggested apps"),
			N_("AUR"),       N_("Flathub"),      N_("Arsenal"),
			N_("Installed"), N_("SynapseOS itself"), N_("About"),
		};
		int wide = 0;
		for (size_t k = 0; k < sizeof menu / sizeof *menu; k++) {
			int w = text_cols(_(menu[k]));
			if (w > wide) wide = w;
		}
		putchar('\n');
		for (size_t k = 0; k < sizeof menu / sizeof *menu; k++) {
			if (k % 3 == 0) fputs("  ", stdout);
			printf("%s%zu%s  %s", C_ACCENT(), k + 1, C_RESET(), _(menu[k]));
			if (k % 3 == 2) putchar('\n');
			else for (int pad = text_cols(_(menu[k])); pad <= wide; pad++)
				putchar(' ');
		}
		printf("  %sq%s  %s\n", C_ACCENT(), C_RESET(), _("Quit"));

		char *in = prompt("\n  > ");
		if (!in || !*in || !strcmp(in, "q"))
			break;

		switch (in[0]) {
		case '1': screen_updates(); break;
		case '2': screen_search(); break;
		case '3': screen_suggest(); break;
		case '4': screen_aur(); break;
		case '5': screen_flathub(); break;
		case '6': screen_arsenal(); break;
		case '7': screen_installed(); break;
		case '9': cmd_about(0, NULL); break;
		case '8': {
			char *args[] = { (char *)"check" };
			cmd_system(1, args);
			if (confirm(_("\n  apply SynapseOS updates now? (opens a build)"))) {
				char *ap[] = { (char *)"apply" };
				cmd_system(1, ap);
			}
			break;
		}
		default:
			printf("  %s%s%s\n", C_WARN(), _("unrecognised choice"), C_RESET());
		}
	}

	printf("\n");
	return 0;
}
