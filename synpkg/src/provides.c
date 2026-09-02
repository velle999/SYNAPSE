/* provides.c — "I typed a name and this machine does not have it. What would
 * I install to get it?"
 *
 * This is NOT `search` with a different sort. `search` answers a browsing
 * question and answers it exhaustively: `search gimp` returns gimp and the
 * thirty gimp-help-<lang> packages beside it, in database order, because
 * somebody reading a list of results wants all of them. This answers a
 * LAUNCHER's question — one word was typed where a program name was expected,
 * and something has to go on the top row of a menu that is one keystroke from
 * being acted on. So it is ranked, it is capped, and every row says WHY it
 * matched, because "gimp" matching the package called gimp and "gimp" matching
 * the words "for GIMP" in some plugin's description are not the same claim and
 * a caller deciding whether to offer a one-key install has to be able to tell
 * them apart.
 *
 * Two front-ends read it (see also the comment on cmd_provides):
 *   - synui's start menu, when a search matches no installed application
 *   - synui's command bar, when the first word of a line is not on $PATH
 * Both are on a keystroke's latency budget, so this stays entirely LOCAL: the
 * sync databases and nothing else. No AUR round trip, no `flatpak search`. The
 * escape hatch for the wider question is `synpkg gui` on the All-sources tab,
 * which both front-ends offer as the last row rather than paying for a network
 * call on every keystroke.
 *
 * ⚠ THE FILES DATABASE IS DELIBERATELY NOT CONSULTED. `pacman -F` would map a
 * binary to its package exactly ("which package owns /usr/bin/eog"), which is
 * the textbook answer to this question — but the `.files` databases are a
 * second set of databases that only `pacman -Fy` fetches, nothing on SynapseOS
 * syncs them, and a lookup that silently answers nothing on every machine that
 * has not run that command is worse than one that does not claim to. Package
 * name and ALPM provisions cover the overwhelming majority of it (a program
 * called htop comes from a package called htop; `vi` is a provision of vim),
 * and when they do not, the wider search is one row away.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"
#include "i18n.h"

#include <stdlib.h>
#include <string.h>

/* Why a row matched, best first. The ORDER of this enum is the ranking, and
 * the strings are the seventh TSV column — a caller reads them by name, never
 * by the number, so inserting a tier here does not shift anybody's parsing.
 *
 * The distinction that earns its keep is EXACT versus everything below it.
 * synui's command bar offers a one-key install for the top row; it is willing
 * to do that for a name match and refuses for a description one, because
 * "install the thing you named" and "install something whose blurb mentions
 * the word you typed" are different enough that only one of them should be a
 * single keystroke away. */
typedef enum {
	M_EXACT = 0,    /* the package is called that */
	M_PROVIDES,     /* the package declares Provides: that */
	M_WORD,         /* the term is the name's whole first word — obs-studio */
	M_PREFIX,       /* the name merely starts with it — obsidian */
	M_NAME,         /* the package name contains it */
	M_DESC,         /* only the description mentions it */
	M_COUNT
} sp_match_t;

static const char *const match_label[M_COUNT] = {
	"exact", "provides", "word", "prefix", "name", "description"
};

typedef struct {
	alpm_pkg_t *pkg;       /* borrowed from the sync cache; see cmd_provides */
	const char *name;
	const char *version;
	const char *repo;
	const char *desc;
	off_t       size;
	bool        installed;
	sp_match_t  why;
	/* An add-on to something else on this list — see mark_addons(). Sorted
	 * below everything that is not one, within its own tier. */
	bool        addon;
} candidate_t;

/* Case-insensitive substring. strcasestr() is a GNU extension we already have
 * (_GNU_SOURCE above), and a LITERAL match is the point: alpm_db_search() runs
 * the needle as a POSIX regex, so a term with a '+' or a '.' in it — g++, a
 * file extension someone typed — either means something the user did not write
 * or fails to compile. Nobody typing a program name into a launcher is writing
 * a regular expression. */
static bool ci_contains(const char *hay, const char *needle)
{
	return hay && needle && strcasestr(hay, needle) != NULL;
}

static bool ci_prefix(const char *hay, const char *needle)
{
	if (!hay || !needle)
		return false;
	return strncasecmp(hay, needle, strlen(needle)) == 0;
}

/* Arch package names are words joined by these. `obs-studio` is "obs" and
 * "studio"; `obsidian` is one word that merely begins the same way. */
static bool is_sep(char c)
{
	return c == '-' || c == '_' || c == '.' || c == '+';
}

/* Which tier this package matches the term at, or -1 for no match at all. */
static int classify(alpm_pkg_t *p, const char *term)
{
	const char *name = alpm_pkg_get_name(p);

	if (name && strcasecmp(name, term) == 0)
		return M_EXACT;

	/* Provisions, so `vi` finds vim and `sh` finds bash. Only an EXACT
	 * provision counts: a substring match against a provision list would put
	 * every package that provides libfoo.so on the list for "foo". */
	for (alpm_list_t *i = alpm_pkg_get_provides(p); i; i = i->next) {
		const alpm_depend_t *d = i->data;
		if (d && d->name && strcasecmp(d->name, term) == 0)
			return M_PROVIDES;
	}

	if (ci_prefix(name, term)) {
		/* The term as a whole WORD of the name beats the term as the first
		 * few letters of a longer one. Typing "obs" means obs-studio and
		 * obs-vaapi, not obsidian — the hyphen is the evidence, and it is
		 * evidence the package name itself carries rather than a table of
		 * special cases somebody has to maintain. */
		return is_sep(name[strlen(term)]) ? M_WORD : M_PREFIX;
	}
	if (ci_contains(name, term))
		return M_NAME;
	if (ci_contains(alpm_pkg_get_desc(p), term))
		return M_DESC;
	return -1;
}

/* Rank, then the thing itself before its add-ons, then SHORTEST NAME, then
 * alphabetical.
 *
 * The length tiebreak is doing real work and is most of why the result of
 * `provides gimp` is not what `search gimp` gives you. Within one tier the
 * shorter name is the more general package almost without exception: gimp
 * before gimp-help-bg, obs-studio before obs-studio-tuna, vlc before
 * vlc-plugin-ffmpeg. No suffix blacklist is needed to get that right, and a
 * blacklist would be a list to keep up to date for as long as the
 * distribution exists.
 *
 * Length alone is not enough, though, and `obs` is the case that proves it:
 * obs-vaapi is SHORTER than obs-studio and is a plugin FOR it. That is what
 * `addon` is for — see mark_addons(). */
static int cmp_candidate(const void *va, const void *vb)
{
	const candidate_t *a = va, *b = vb;
	if (a->why != b->why)
		return (int)a->why - (int)b->why;
	if (a->addon != b->addon)
		return a->addon ? 1 : -1;

	size_t la = strlen(a->name), lb = strlen(b->name);
	if (la != lb)
		return la < lb ? -1 : 1;
	return strcmp(a->name, b->name);
}

/* Ordering with `addon` left out, so the pre-pass that decides `addon` has a
 * stable list to reason over. */
static int cmp_prepass(const void *va, const void *vb)
{
	const candidate_t *a = va, *b = vb;
	if (a->why != b->why)
		return (int)a->why - (int)b->why;
	size_t la = strlen(a->name), lb = strlen(b->name);
	if (la != lb)
		return la < lb ? -1 : 1;
	return strcmp(a->name, b->name);
}

/* How far down the list add-on detection looks. Everything past this is
 * description-tier noise that no front-end will draw, and the pass is
 * quadratic — bounding it here keeps a one-syllable term that matches four
 * thousand descriptions from costing anything measurable. */
#define ADDON_WINDOW 400

/*
 * An add-on is a package that only makes sense because ANOTHER package on this
 * same list exists. Two shapes, and both are needed because each misses the
 * other's case:
 *
 *   - it DEPENDS on another candidate. obs-vaapi depends on obs-studio, and is
 *     shorter than it, so nothing about the name would have demoted it.
 *   - its NAME extends another candidate's at a word boundary. gimp-help-bg
 *     depends on nothing at all — a directory of translated help files has no
 *     reason to — so only the name gives it away.
 *
 * Either way the answer to "what do I install to get <term>" is the thing being
 * extended, not the extension. Offering someone a Bulgarian help catalogue or a
 * VAAPI encoder plugin because they typed the name of an application would be a
 * joke at their expense.
 *
 * Both directions are checked only within the window, against the window: a
 * parent that is not itself near the top of a ranked list is not a parent worth
 * demoting anything for.
 */
static void mark_addons(candidate_t *c, size_t n)
{
	size_t win = n < ADDON_WINDOW ? n : ADDON_WINDOW;

	/* ⚠ EVERYTHING PAST THE WINDOW IS AN ADD-ON BY DEFAULT, and that default
	 * is the whole reason the window is safe to have. Leaving the tail
	 * unmarked put a CLIFF at the window's edge: `provides firefox` looked at
	 * the first N names, marked every one of them an add-on of firefox, and
	 * then floated firefox-developer-edition-i18n-en-ca — too far down to
	 * have been examined, and therefore still "not an add-on" — above all of
	 * them. A row nobody looked at outranking a row that was inspected and
	 * demoted is worse than either answer on its own. The tail is already
	 * ranked below the window by the pre-pass, so demoting it changes no
	 * relative order; it only stops it jumping the queue. */
	for (size_t i = win; i < n; i++)
		c[i].addon = true;

	for (size_t i = 0; i < win; i++) {
		size_t li = strlen(c[i].name);

		/* Shape one: the name extends another candidate's at a word
		 * boundary. A parent is strictly SHORTER than its extension, which
		 * is also what stops this being symmetric — two names cannot each
		 * extend the other, so no pair can demote both halves and leave
		 * neither on top. */
		for (size_t j = 0; j < win && !c[i].addon; j++) {
			if (j == i)
				continue;
			size_t lj = strlen(c[j].name);
			if (lj >= li)
				continue;
			if (!strncmp(c[i].name, c[j].name, lj) && is_sep(c[i].name[lj]))
				c[i].addon = true;
		}
		if (c[i].addon)
			continue;

		/* Shape two: it depends on another candidate. */
		for (alpm_list_t *d = alpm_pkg_get_depends(c[i].pkg);
		     d && !c[i].addon; d = d->next) {
			const alpm_depend_t *dep = d->data;
			if (!dep || !dep->name)
				continue;
			for (size_t j = 0; j < win; j++) {
				if (j != i && !strcmp(dep->name, c[j].name)) {
					c[i].addon = true;
					break;
				}
			}
		}
	}
}

static void emit_header(void)
{
	if (g_out == OUT_TSV)
		tsv_row(7, "name", "installed", "version", "repo", "size",
		        "description", "match");
}

static void emit_row(const candidate_t *c)
{
	if (g_out == OUT_TSV) {
		char *sz = xasprintf("%lld", (long long)c->size);
		tsv_row(7, c->name, c->installed ? "1" : "0",
		        c->version ? c->version : "", c->repo ? c->repo : "",
		        sz, c->desc ? c->desc : "", match_label[c->why]);
		free(sz);
		return;
	}

	printf("%s%s/%s%s%s %s%s%s", C_DIM(), c->repo ? c->repo : "?", C_RESET(),
	       C_BOLD(), c->name, C_ACCENT(), c->version ? c->version : "",
	       C_RESET());
	if (c->installed)
		printf(" %s[installed]%s", C_OK(), C_RESET());
	printf(" %s(%s)%s\n", C_DIM(), match_label[c->why], C_RESET());
	if (c->desc && *c->desc)
		printf("    %s\n", c->desc);
}

/*
 * `synpkg provides <term> [--limit N]`
 *
 * ⚠ THE HEADER IS PRINTED EVEN WHEN NOTHING MATCHES, and the "nothing" is
 * carried by the exit status (100, this program's "nothing to do") rather than
 * by the absence of output. An empty table is a complete answer; a caller that
 * keys its fields by header name and gets zero bytes cannot tell a miss from a
 * crash. That rule was learnt the hard way — see `arsenal categories` in
 * synpkg's history, which delegated its empty case to a function emitting a
 * different column count and passed every test on the one machine that had
 * something to report.
 */
int cmd_provides(int argc, char **argv)
{
	const char *term = NULL;
	long limit = 8;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--limit")) {
			if (++i >= argc)
				die(_("provides: --limit needs a number"));
			char *end = NULL;
			limit = strtol(argv[i], &end, 10);
			if (!end || *end || limit < 1)
				die(_("provides: --limit takes a positive number, not '%s'"),
				    argv[i]);
		} else if (!term) {
			term = argv[i];
		} else {
			/* One term, not a list. This answers "what is the thing
			 * called <word>", and two words are two questions — the
			 * caller that wants a phrase wants `search`. */
			die(_("provides: one term at a time (got '%s' as well)"), argv[i]);
		}
	}
	if (!term || !*term)
		die(_("provides: need a name to look up"));

	/* A single letter matches most of the repositories and ranks them by
	 * name length, which is a list of the shortest package names on the
	 * system and an answer to nobody's question. The front-ends debounce and
	 * gate on length too; this is the floor that does not depend on them
	 * remembering to. */
	if (strlen(term) < 2) {
		emit_header();
		return 100;
	}

	alpm_handle_t *h = sp_alpm_init(false);
	alpm_db_t *local = alpm_get_localdb(h);

	candidate_t *cand = NULL;
	size_t n = 0, cap = 0;

	for (alpm_list_t *d = sp_syncdbs(h); d; d = d->next) {
		alpm_db_t *db = d->data;
		const char *repo = alpm_db_get_name(db);

		for (alpm_list_t *i = alpm_db_get_pkgcache(db); i; i = i->next) {
			alpm_pkg_t *p = i->data;
			int why = classify(p, term);
			if (why < 0)
				continue;

			const char *name = alpm_pkg_get_name(p);

			/* One row per package name. The sync dbs are walked in
			 * pacman.conf order, which IS the repository precedence
			 * pacman itself installs by, so the first copy seen is the
			 * one an install would actually get — and listing the same
			 * package once per repository would spend the whole cap on
			 * a single answer. Linear because `limit` bounds nothing
			 * here: this scan is over every match, not every package,
			 * and a term that matches thousands of packages is a term
			 * the caller has already refused to look up. */
			bool dup = false;
			for (size_t k = 0; k < n; k++)
				if (!strcmp(cand[k].name, name)) { dup = true; break; }
			if (dup)
				continue;

			if (n == cap) {
				cap = cap ? cap * 2 : 64;
				cand = xrealloc(cand, cap * sizeof *cand);
			}
			cand[n++] = (candidate_t){
				.pkg       = p,
				.name      = name,
				.version   = alpm_pkg_get_version(p),
				.repo      = repo,
				.desc      = alpm_pkg_get_desc(p),
				.size      = alpm_pkg_get_size(p),
				.installed = alpm_db_get_pkg(local, name) != NULL,
				.why       = (sp_match_t)why,
			};
		}
	}

	if (n > 1) {
		/* Two sorts, and the order matters: mark_addons() reasons about the
		 * TOP of the list, so the list has to be in rank order before it
		 * runs — and its answer is then a sort key, so the list has to be
		 * sorted again once it has one. */
		qsort(cand, n, sizeof *cand, cmp_prepass);
		mark_addons(cand, n);
		qsort(cand, n, sizeof *cand, cmp_candidate);
	}

	emit_header();
	size_t shown = n < (size_t)limit ? n : (size_t)limit;
	for (size_t k = 0; k < shown; k++)
		emit_row(&cand[k]);

	if (!n && g_out == OUT_HUMAN)
		info("nothing in the repositories is called '%s' — "
		     "try: synpkg gui all", term);

	free(cand);
	/* The strings above point INTO the package cache, so the handle outlives
	 * every use of them and is released only here. */
	sp_alpm_free(h);

	return n ? 0 : 100;
}
