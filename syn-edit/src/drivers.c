/* drivers.c — running the engine with no terminal and no window.
 *
 * `syn-edit run --keys 'ggdG' file` is the whole vim layer exercised from a
 * shell script. That is not a debugging aid bolted on afterwards: it is how
 * every test in this component works, and it exists because the alternative —
 * testing a modal editor by driving a pty — is so awkward that in practice
 * nobody does it and the editing engine ships untested.
 *
 * It is also genuinely useful. `syn-edit ex -c '%s/foo/bar/g' -w file` is sed
 * with vim's regex flavour and an atomic write, and `run --keys` can do the
 * things sed cannot, like "join every line that follows a bullet".
 *
 * ⚠ Nothing here writes to a file unless -w was given. A driver that saved by
 * default would make an experiment indistinguishable from an edit.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "edit_internal.h"
#include "i18n.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Reads the whole of stdin into a buffer, for `-` as a file name. */
static void load_stdin(ed_t *e)
{
	size_t cap = 65536, len = 0;
	char *data = xmalloc(cap);
	for (;;) {
		if (len + 1 >= cap) {
			cap *= 2;
			data = xrealloc(data, cap);
		}
		ssize_t n = read(STDIN_FILENO, data + len, cap - len - 1);
		if (n <= 0)
			break;
		len += (size_t)n;
	}
	data[len] = '\0';

	buf_t *b = ed_buf(e);
	size_t start = 0;
	char **lines = NULL;
	size_t nl = 0, lcap = 0;
	for (size_t i = 0; i <= len; i++) {
		if (i == len || data[i] == '\n') {
			if (i == len && start == len && len > 0)
				break;
			if (nl == lcap) {
				lcap = lcap ? lcap * 2 : 64;
				lines = xrealloc(lines, lcap * sizeof *lines);
			}
			lines[nl++] = xstrndup(data + start, i - start);
			start = i + 1;
		}
	}
	if (nl == 0) {
		free(lines);
		free(data);
		return;
	}
	/* Replaces the buffer's single empty line. Not journalled as an edit —
	 * see buf_load for why loading must not be undoable. */
	buf_splice(b, 0, b->n, lines, nl, 0, 0);
	b->modified = false;
	b->seq = 0;
	b->saved_at = 0;
	for (size_t i = 0; i < nl; i++)
		free(lines[i]);
	free(lines);
	free(data);
}

static void print_state(ed_t *e)
{
	buf_t *b = ed_buf(e);
	char n[32];

	rec_row(2, "field", "value");
	rec_row(2, "mode", ed_mode_name(e));
	snprintf(n, sizeof n, "%zu", e->cy + 1);
	rec_row(2, "line", n);
	snprintf(n, sizeof n, "%zu", e->cx + 1);
	rec_row(2, "col", n);
	snprintf(n, sizeof n, "%zu", b->n);
	rec_row(2, "lines", n);
	rec_row(2, "file", buf_name(b));
	rec_row(2, "language", syn_lang_name(b->lang));
	rec_row(2, "modified", b->modified ? "yes" : "no");
	rec_row(2, "eol", b->eol == EOL_CRLF ? "crlf" : "lf");
	if (b->no_eol)
		rec_row(2, "final-newline", "missing");
	if (b->binary)
		rec_row(2, "binary", "yes");
	if (e->msg[0])
		rec_row(2, e->msg_err ? "error" : "message", e->msg);
}

/* Shared by `run` and `ex`: both open files, apply something, and then either
 * print or save. */
static int drive(int argc, char **argv, bool ex_mode)
{
	ed_t *e = ed_new();
	opts_load(&e->o);

	char *keys = xstrdup("");
	bool write_back = false, status = false, quiet = false;
	const char *files[64];
	size_t nfiles = 0;
	const char *forced_lang = NULL;

	for (int i = 0; i < argc; i++) {
		const char *a = argv[i];
		if ((!strcmp(a, "--keys") || !strcmp(a, "-k")) && i + 1 < argc) {
			char *j = xasprintf("%s%s", keys, argv[++i]);
			free(keys);
			keys = j;
		} else if ((!strcmp(a, "-c") || !strcmp(a, "--ex")) && i + 1 < argc) {
			/* An ex command is just keys: ":" then the line then Enter. One
			 * path through the engine, so :s behaves identically whether it
			 * was typed in the window or passed on a command line. */
			char *esc = xstrdup(argv[++i]);
			char *j = xasprintf("%s:%s<CR>", keys, esc);
			free(esc);
			free(keys);
			keys = j;
		} else if (!strcmp(a, "-w") || !strcmp(a, "--write")) {
			write_back = true;
		} else if (!strcmp(a, "--status")) {
			status = true;
		} else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) {
			quiet = true;
		} else if (!strcmp(a, "--lang") && i + 1 < argc) {
			forced_lang = argv[++i];
		} else if (a[0] == '-' && a[1] && strcmp(a, "-")) {
			ed_free(e);
			free(keys);
			die(_("%s: unknown option '%s'"), ex_mode ? "ex" : "run", a);
		} else {
			if (nfiles < sizeof files / sizeof *files)
				files[nfiles++] = a;
		}
	}

	if (nfiles == 0) {
		ed_free(e);
		free(keys);
		die(_("%s: needs a file (use - for standard input)"),
		    ex_mode ? "ex" : "run");
	}

	bool from_stdin = false;
	for (size_t i = 0; i < nfiles; i++) {
		if (!strcmp(files[i], "-")) {
			from_stdin = true;
			load_stdin(e);
			continue;
		}
		char *err = NULL;
		if (ed_open(e, files[i], &err) < 0) {
			warn("%s", err ? err : "could not open");
			free(err);
			ed_free(e);
			free(keys);
			return 1;
		}
	}

	if (forced_lang) {
		int l = syn_lang_by_name(forced_lang);
		if (l < 0) {
			ed_free(e);
			free(keys);
			die(_("unknown language: %s"), forced_lang);
		}
		ed_buf(e)->lang = l;
	}

	/* The engine starts on the FIRST file given, not the last one opened —
	 * `run -k … a.c b.c` reads as "these two files, starting at the top". */
	e->cur = 0;
	e->cy = e->cx = 0;

	ed_keys(e, keys);
	free(keys);

	int rc = 0;

	if (write_back) {
		if (from_stdin) {
			warn(_("-w cannot write back to standard input"));
			rc = 1;
		}
		for (size_t i = 0; i < e->nbuf; i++) {
			if (!e->buf[i]->modified || !e->buf[i]->path)
				continue;
			char *err = NULL;
			if (!buf_save(e->buf[i], NULL, &err)) {
				warn("%s", err ? err : "write failed");
				free(err);
				rc = 1;
			}
		}
	}

	if (status) {
		print_state(e);
	} else if (!write_back && !quiet) {
		size_t len = 0;
		char *text = buf_text(ed_buf(e), &len);
		fwrite(text, 1, len, stdout);
		free(text);
	}

	/* A message the engine produced is a diagnostic, not output: it goes to
	 * stderr so that `run … > file` is the edited text and nothing else. */
	if (e->msg[0] && !status && !quiet)
		fprintf(stderr, "%s\n", e->msg);
	if (e->msg[0] && e->msg_err)
		rc = rc ? rc : 1;

	ed_free(e);
	return rc;
}

int cmd_run(int argc, char **argv)      { return drive(argc, argv, false); }
int cmd_ex_cli(int argc, char **argv)   { return drive(argc, argv, true); }

/* ── highlight ──────────────────────────────────────────────────────────── */

int cmd_highlight(int argc, char **argv)
{
	const char *path = NULL, *forced = NULL;
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--lang") && i + 1 < argc)
			forced = argv[++i];
		else if (argv[i][0] == '-' && argv[i][1])
			die(_("highlight: unknown option '%s'"), argv[i]);
		else
			path = argv[i];
	}
	if (!path)
		die(_("highlight: needs a file"));

	buf_t *b = buf_new();
	char *err = NULL;
	if (!buf_load(b, path, &err)) {
		warn("%s", err ? err : "could not open");
		free(err);
		buf_free(b);
		return 1;
	}
	if (forced) {
		int l = syn_lang_by_name(forced);
		if (l < 0) {
			buf_free(b);
			die(_("unknown language: %s"), forced);
		}
		b->lang = l;
	}

	if (g_out == OUT_REC)
		rec_row(4, "line", "start", "len", "token");
	else
		printf("%s%s%s — %s\n", C_ACCENT(), path, C_RESET(),
		       syn_lang_name(b->lang));

	span_t spans[512];
	syn_state st = 0;
	for (size_t y = 0; y < b->n; y++) {
		size_t n = syn_scan(b->lang, buf_line(b, y), buf_linelen(b, y),
		                    &st, spans, 512);
		for (size_t i = 0; i < n; i++) {
			char a[32], c[32], d[32];
			snprintf(a, sizeof a, "%zu", y + 1);
			snprintf(c, sizeof c, "%zu", spans[i].start);
			snprintf(d, sizeof d, "%zu", spans[i].len);
			if (g_out == OUT_REC) {
				rec_row(4, a, c, d, syn_tok_name(spans[i].tok));
			} else if (spans[i].tok != TK_TEXT) {
				char *text = xstrndup(buf_line(b, y) + spans[i].start,
				                      spans[i].len);
				printf("  %4s:%-4s %-9s %s\n", a, c,
				       syn_tok_name(spans[i].tok), text);
				free(text);
			}
		}
	}

	buf_free(b);
	return 0;
}

int cmd_langs(int argc, char **argv)
{
	for (int i = 0; i < argc; i++)
		die(_("langs: unknown option '%s'"), argv[i]);

	if (g_out == OUT_REC)
		rec_row(1, "language");
	for (size_t i = 0; i < syn_lang_count(); i++) {
		if (g_out == OUT_REC)
			rec_row(1, syn_lang_at(i));
		else
			printf("  %s\n", syn_lang_at(i));
	}
	return 0;
}
