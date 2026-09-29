/* finding.c — one record shape, whatever engine produced it.
 *
 * The reason this file exists: clamscan prints one format, rkhunter another,
 * chkrootkit a third, and a user staring at three of them has to learn three.
 * Everything upstream of here normalises into finding_t, and everything
 * downstream — terminal, TUI, window — reads only that.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "config.h"
#include "synscan.h"
#include "i18n.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ⛔ NEVER TRANSLATED. The window matches on these and colours a row by the
 * result; syn-scan quarantine takes them back on the command line. */
const char *verdict_id(verdict_t v)
{
	switch (v) {
	case VERDICT_INFECTED: return "infected";
	case VERDICT_SUSPECT:  return "suspect";
	case VERDICT_ERROR:    return "error";
	case VERDICT_INCOMPLETE: return "incomplete";
	case VERDICT_CLEAN:
	default:               return "clean";
	}
}

/* The word a person reads. Marked with N_() rather than _() because a record
 * carries it to the window, which translates at the draw site. */
const char *verdict_label(verdict_t v)
{
	switch (v) {
	case VERDICT_INFECTED: return N_("Infected");
	case VERDICT_SUSPECT:  return N_("Suspicious");
	case VERDICT_ERROR:    return N_("Unreadable");
	case VERDICT_INCOMPLETE: return N_("Did not finish");
	case VERDICT_CLEAN:
	default:               return N_("Clean");
	}
}

void findings_init(findings_t *f)
{
	f->head = f->tail = NULL;
	f->n = 0;
}

static char *dup_or_empty(const char *s)
{
	char *d = strdup(s ? s : "");
	if (!d) die("out of memory");
	return d;
}

finding_t *findings_add(findings_t *f, const char *engine, verdict_t v,
                        const char *path, const char *detail)
{
	finding_t *n = calloc(1, sizeof *n);
	if (!n) die("out of memory");

	n->engine  = dup_or_empty(engine);
	n->path    = dup_or_empty(path);
	n->detail  = dup_or_empty(detail);
	n->verdict = v;
	n->when    = time(NULL);

	if (f->tail) f->tail->next = n;
	else         f->head = n;
	f->tail = n;
	f->n++;
	return n;
}

size_t findings_outstanding(const findings_t *f)
{
	size_t n = 0;
	for (const finding_t *p = f->head; p; p = p->next)
		if (p->verdict == VERDICT_INFECTED || p->verdict == VERDICT_SUSPECT)
			n++;
	return n;
}

void findings_free(findings_t *f)
{
	finding_t *p = f->head;
	while (p) {
		finding_t *next = p->next;
		free(p->engine); free(p->path); free(p->detail);
		free(p);
		p = next;
	}
	findings_init(f);
}

/* ⚠ A path can contain anything a filename can, including a tab, which would
 * split one record into two columns and hand the window a path that does not
 * exist. Tabs and newlines are escaped; the window unescapes. */
static void put_field(FILE *out, const char *s)
{
	for (const char *p = s; *p; p++) {
		switch (*p) {
		case '\t': fputs("\\t", out); break;
		case '\n': fputs("\\n", out); break;
		case '\\': fputs("\\\\", out); break;
		default:   fputc(*p, out);
		}
	}
}

void findings_write_rec(FILE *out, const findings_t *f)
{
	for (const finding_t *p = f->head; p; p = p->next) {
		fputs("finding\t", out);
		put_field(out, p->engine); fputc('\t', out);
		fputs(verdict_id(p->verdict), out); fputc('\t', out);
		put_field(out, p->path); fputc('\t', out);
		put_field(out, p->detail); fputc('\t', out);
		fprintf(out, "%lld\n", (long long)p->when);
	}
}

static void print_rec(const findings_t *f)
{
	/* ⛔ Column names are the protocol. data/syn-scan.qml reads this row. */
	puts("#finding\tengine\tverdict\tpath\tdetail\twhen");
	findings_write_rec(stdout, f);
}

/* ── a row a person reads ─────────────────────────────────────────────────
 *
 * ⚠ THE VERDICT COLUMN IS MEASURED, IN COLUMNS. It was `%-11s`, which pads by
 * BYTES and fits the English words: "Nicht lesbar" is 12 wide and pushed its
 * path one column right of the row above, and 感染 is 6 bytes and 4 columns,
 * so its path started two columns early. The column is as wide as this
 * language's widest row label and never narrower than the English 11, so the
 * English output is what it always was.
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

static int verdict_cols(void)
{
	static int w = 0;
	if (w) return w;
	/* The three a row can carry: clean is never listed, and an engine that
	 * did not finish is a warning line of its own. */
	static const verdict_t listed[] = { VERDICT_INFECTED, VERDICT_SUSPECT, VERDICT_ERROR };
	w = 11;
	for (size_t i = 0; i < sizeof listed / sizeof *listed; i++) {
		int c = text_cols(_(verdict_label(listed[i])));
		if (c > w) w = c;
	}
	return w;
}

void finding_print_row(const char *engine, verdict_t v, const char *path,
                       const char *detail)
{
	const char *label = _(verdict_label(v));
	int pad = verdict_cols() - text_cols(label);
	printf("  %-10s %s%*s %s\n", engine, label, pad > 0 ? pad : 0, "", path);
	if (*detail)
		printf("  %-10s %*s   %s\n", "", verdict_cols(), "", detail);
}

static void print_row(const finding_t *p)
{
	finding_print_row(p->engine, p->verdict, p->path, p->detail);
}

static void print_human(const findings_t *f)
{
	size_t bad = 0, unread = 0;

	for (const finding_t *p = f->head; p; p = p->next) {
		if (p->verdict == VERDICT_ERROR) unread++;
		if (p->verdict != VERDICT_INFECTED && p->verdict != VERDICT_SUSPECT)
			continue;
		bad++;
		print_row(p);
	}
	/* What the engine could not read, apart and uncounted — see
	 * findings_outstanding(). */
	if (unread) {
		printf(P_("\n%zu file could not be scanned:\n",
		          "\n%zu files could not be scanned:\n", unread), unread);
		for (const finding_t *p = f->head; p; p = p->next)
			if (p->verdict == VERDICT_ERROR) print_row(p);
	}
	/* An engine's own trouble, apart: it says the scan is not whole, and it
	 * says nothing about the machine. */
	for (const finding_t *p = f->head; p; p = p->next)
		if (p->verdict == VERDICT_INCOMPLETE)
			warn(_("%s did not finish: %s"), p->engine, p->detail);

	if (bad == 0) {
		info("%s", _("Nothing found."));
		return;
	}
	printf(P_("\n%zu thing needs a look.\n",
	          "\n%zu things need a look.\n", bad), bad);

	/* ⛔ The program does not offer to delete, here or anywhere — but it must
	 * not promise it left things alone when it is about to move them. */
	if (g_will_quarantine)
		info("%s", _("Moving these aside now; `syn-scan quarantine restore` "
		             "puts any of them back."));
	else
		info("%s", _("Nothing has been moved. `syn-scan quarantine take <path>` "
		             "puts a file aside; it can always be restored."));
}

void findings_print(const findings_t *f)
{
	if (g_out == OUT_REC) print_rec(f);
	else                  print_human(f);
}
