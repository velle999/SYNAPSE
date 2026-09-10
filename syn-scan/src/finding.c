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

/* ⛔ NEVER TRANSLATED. The window matches on these and colours a row by the
 * result; syn-scan quarantine takes them back on the command line. */
const char *verdict_id(verdict_t v)
{
	switch (v) {
	case VERDICT_INFECTED: return "infected";
	case VERDICT_SUSPECT:  return "suspect";
	case VERDICT_ERROR:    return "error";
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

static void print_rec(const findings_t *f)
{
	/* ⛔ Column names are the protocol. data/syn-scan.qml reads this row. */
	puts("#finding\tengine\tverdict\tpath\tdetail\twhen");
	for (const finding_t *p = f->head; p; p = p->next) {
		fputs("finding\t", stdout);
		put_field(stdout, p->engine); fputc('\t', stdout);
		fputs(verdict_id(p->verdict), stdout); fputc('\t', stdout);
		put_field(stdout, p->path); fputc('\t', stdout);
		put_field(stdout, p->detail); fputc('\t', stdout);
		printf("%lld\n", (long long)p->when);
	}
}

static void print_human(const findings_t *f)
{
	size_t bad = 0;

	for (const finding_t *p = f->head; p; p = p->next) {
		if (p->verdict == VERDICT_CLEAN) continue;
		bad++;
		printf("  %-10s %-11s %s\n",
		       p->engine, _(verdict_label(p->verdict)), p->path);
		if (*p->detail)
			printf("  %-10s %-11s   %s\n", "", "", p->detail);
	}

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
