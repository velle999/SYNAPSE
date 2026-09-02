/* buffer.c — the text, and the only way it is ever allowed to change.
 *
 * ── One primitive ──────────────────────────────────────────────────────────
 *
 * buf_splice(b, at, ndel, ins, nins) replaces ndel lines at `at` with nins new
 * ones. Deleting a word, joining two lines, pasting, indenting a block and
 * undo itself are all expressed as splices. Nothing else touches b->ln.
 *
 * That is what makes undo trustworthy rather than approximately right. Each
 * splice pushes its own inverse onto the journal, so an operation cannot be
 * added to this editor that undo does not already know how to reverse — the
 * usual way an undo stack acquires holes is somebody adding a command that
 * edits the array directly, and here there is no array to reach.
 *
 * The cost is that a one-character insert rewrites a whole line. For the files
 * a person edits by hand that is a memcpy of a few dozen bytes; a rope or a
 * piece table would buy nothing back and would put the correctness of undo
 * inside a data structure instead of inside a rule.
 *
 * ── Loading ────────────────────────────────────────────────────────────────
 *
 * A file that is not valid UTF-8, holds NUL bytes, uses CRLF, or does not end
 * with a newline still opens. An editor that refuses a malformed file is an
 * editor that cannot repair one. Each of those facts is REMEMBERED and put
 * back on save, so opening and writing a file without editing it is a no-op at
 * the byte level — including the missing final newline, which is the one every
 * editor silently "fixes" and which shows up as a spurious diff hunk.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-edit.h"
#include "i18n.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ── lines ──────────────────────────────────────────────────────────────── */

static void line_set(line_t *l, const char *s, size_t len)
{
	if (l->cap < len + 1) {
		l->cap = len + 1;
		l->s = xrealloc(l->s, l->cap);
	}
	if (len)
		memcpy(l->s, s, len);
	l->s[len] = '\0';
	l->len = len;
}

static void line_init(line_t *l, const char *s, size_t len)
{
	l->s = NULL;
	l->cap = 0;
	l->len = 0;
	line_set(l, s, len);
}

static void line_free(line_t *l)
{
	free(l->s);
	l->s = NULL;
	l->cap = l->len = 0;
}

/* ── buffer lifecycle ───────────────────────────────────────────────────── */

buf_t *buf_new(void)
{
	buf_t *b = xmalloc(sizeof *b);
	memset(b, 0, sizeof *b);
	b->cap = 16;
	b->ln = xmalloc(b->cap * sizeof *b->ln);
	/* A buffer always has at least one line. "Empty" means one empty line,
	 * exactly as an empty file that ends in a newline holds one empty line —
	 * and it means no motion has to special-case n == 0. */
	line_init(&b->ln[0], "", 0);
	b->n = 1;
	b->eol = EOL_LF;
	b->mode = 0644;
	b->lang = -1;
	return b;
}

static void undo_free_chain(undo_step *s)
{
	while (s) {
		undo_step *next = s->next;
		for (size_t i = 0; i < s->ndel; i++)
			line_free(&s->del[i]);
		free(s->del);
		free(s);
		s = next;
	}
}

void buf_free(buf_t *b)
{
	if (!b)
		return;
	for (size_t i = 0; i < b->n; i++)
		line_free(&b->ln[i]);
	free(b->ln);
	undo_free_chain(b->head);
	free(b->path);
	free(b);
}

const char *buf_line(const buf_t *b, size_t i)
{
	return i < b->n ? b->ln[i].s : "";
}

size_t buf_linelen(const buf_t *b, size_t i)
{
	return i < b->n ? b->ln[i].len : 0;
}

const char *buf_name(const buf_t *b)
{
	return (b && b->path && *b->path) ? b->path : "[No Name]";
}

/* ── the splice ─────────────────────────────────────────────────────────── */

/* Truncates the redo branch: once something new is typed after an undo, the
 * steps that were undone are unreachable and holding them would let a later
 * redo apply a change against a buffer it was never computed for. */
static void undo_drop_after(buf_t *b, undo_step *keep)
{
	undo_step *drop = keep ? keep->next : b->head;
	if (!drop)
		return;
	if (keep)
		keep->next = NULL;
	else
		b->head = NULL;
	undo_free_chain(drop);
}

void buf_splice(buf_t *b, size_t at, size_t ndel,
                char *const *ins, size_t nins, size_t cy, size_t cx)
{
	if (at > b->n)
		at = b->n;
	if (at + ndel > b->n)
		ndel = b->n - at;

	/* Record what is about to be lost, before it is. */
	undo_step *st = xmalloc(sizeof *st);
	memset(st, 0, sizeof *st);
	st->at = at;
	st->nins = nins;
	st->ndel = ndel;
	st->cy = cy;
	st->cx = cx;
	st->boundary = !b->in_group;
	if (ndel) {
		st->del = xmalloc(ndel * sizeof *st->del);
		for (size_t i = 0; i < ndel; i++)
			line_init(&st->del[i], b->ln[at + i].s, b->ln[at + i].len);
	}

	undo_drop_after(b, b->cur);
	st->prev = b->cur;
	st->next = NULL;
	if (b->cur)
		b->cur->next = st;
	else
		b->head = st;
	b->cur = st;
	/* Once a group is open every further splice joins it, so an operator that
	 * touches six lines undoes as one thing. buf_group_begin sets in_group;
	 * the FIRST splice of the group is the boundary. */
	b->in_group = true;

	/* Apply. */
	for (size_t i = 0; i < ndel; i++)
		line_free(&b->ln[at + i]);

	if (nins != ndel) {
		size_t need = b->n - ndel + nins;
		if (need > b->cap) {
			while (b->cap < need)
				b->cap = b->cap ? b->cap * 2 : 16;
			b->ln = xrealloc(b->ln, b->cap * sizeof *b->ln);
		}
		memmove(&b->ln[at + nins], &b->ln[at + ndel],
		        (b->n - at - ndel) * sizeof *b->ln);
		b->n = need;
	}
	for (size_t i = 0; i < nins; i++)
		line_init(&b->ln[at + i], ins[i], strlen(ins[i]));

	if (b->n == 0) {
		/* Deleting every line leaves one empty one, which is what vim does and
		 * what every motion in this program assumes. */
		if (b->cap < 1) {
			b->cap = 16;
			b->ln = xrealloc(b->ln, b->cap * sizeof *b->ln);
		}
		line_init(&b->ln[0], "", 0);
		b->n = 1;
	}

	b->seq++;
	b->modified = (b->seq != b->saved_at);
}

void buf_group_begin(buf_t *b)
{
	b->in_group = false;   /* the next splice starts a new group */
}

void buf_group_end(buf_t *b)
{
	b->in_group = false;
}

/* Reverse one step: put back what it deleted, take out what it inserted. */
static void step_invert(buf_t *b, undo_step *s)
{
	size_t at = s->at, nins = s->nins, ndel = s->ndel;

	/* Capture the current text of the inserted range — that becomes what a
	 * redo will re-insert, so the step is its own inverse and the two
	 * directions cannot drift apart. */
	line_t *now = NULL;
	if (nins) {
		now = xmalloc(nins * sizeof *now);
		for (size_t i = 0; i < nins; i++) {
			size_t j = at + i;
			line_init(&now[i], j < b->n ? b->ln[j].s : "",
			                   j < b->n ? b->ln[j].len : 0);
		}
	}

	for (size_t i = 0; i < nins && at + i < b->n; i++)
		line_free(&b->ln[at + i]);

	size_t have = nins;
	if (at + have > b->n)
		have = b->n > at ? b->n - at : 0;

	size_t need = b->n - have + ndel;
	if (need > b->cap) {
		while (b->cap < need)
			b->cap = b->cap ? b->cap * 2 : 16;
		b->ln = xrealloc(b->ln, b->cap * sizeof *b->ln);
	}
	memmove(&b->ln[at + ndel], &b->ln[at + have],
	        (b->n - at - have) * sizeof *b->ln);
	b->n = need;

	for (size_t i = 0; i < ndel; i++) {
		line_init(&b->ln[at + i], s->del[i].s, s->del[i].len);
		line_free(&s->del[i]);
	}
	free(s->del);

	/* The step now describes the opposite change. */
	s->del = now;
	s->ndel = nins;
	s->nins = ndel;

	if (b->n == 0) {
		if (b->cap < 1) {
			b->cap = 16;
			b->ln = xrealloc(b->ln, b->cap * sizeof *b->ln);
		}
		line_init(&b->ln[0], "", 0);
		b->n = 1;
	}
}

bool buf_undo(buf_t *b, size_t *cy, size_t *cx)
{
	if (!b->cur)
		return false;

	/* Unwind the whole group — every step back to and including the one
	 * marked as its boundary. A six-line indent went in as six splices and
	 * has to come out as one press of u. */
	for (;;) {
		undo_step *s = b->cur;
		bool boundary = s->boundary;
		step_invert(b, s);
		*cy = s->cy;
		*cx = s->cx;
		b->cur = s->prev;
		b->seq--;
		if (boundary)
			break;
		if (!b->cur)
			break;
	}

	if (*cy >= b->n)
		*cy = b->n - 1;
	b->modified = (b->seq != b->saved_at);
	b->in_group = false;
	return true;
}

bool buf_redo(buf_t *b, size_t *cy, size_t *cx)
{
	undo_step *s = b->cur ? b->cur->next : b->head;
	if (!s)
		return false;

	for (;;) {
		step_invert(b, s);
		*cy = s->cy;
		*cx = s->cx;
		b->cur = s;
		b->seq++;
		/* Redo stops when the NEXT step starts a new group. */
		if (!s->next || s->next->boundary)
			break;
		s = s->next;
	}

	if (*cy >= b->n)
		*cy = b->n - 1;
	b->modified = (b->seq != b->saved_at);
	b->in_group = false;
	return true;
}

/* ── loading ────────────────────────────────────────────────────────────── */

char *buf_text(const buf_t *b, size_t *len)
{
	size_t total = 0;
	size_t sep = (b->eol == EOL_CRLF) ? 2 : 1;
	for (size_t i = 0; i < b->n; i++)
		total += b->ln[i].len + sep;
	if (b->no_eol && total >= sep)
		total -= sep;

	char *out = xmalloc(total + 1);
	size_t w = 0;
	for (size_t i = 0; i < b->n; i++) {
		memcpy(out + w, b->ln[i].s, b->ln[i].len);
		w += b->ln[i].len;
		bool last = (i + 1 == b->n);
		if (!last || !b->no_eol) {
			if (b->eol == EOL_CRLF)
				out[w++] = '\r';
			out[w++] = '\n';
		}
	}
	out[w] = '\0';
	if (len)
		*len = w;
	return out;
}

bool buf_load(buf_t *b, const char *path, char **err)
{
	char *full = expand_path(path);
	free(b->path);
	b->path = full;

	int fd = open(full, O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT) {
			/* Not an error. Opening a name that does not exist yet is how a
			 * new file is made, and the buffer is already one empty line. */
			b->existed = false;
			b->readonly = false;
			b->lang = syn_lang_for(full, "");
			return true;
		}
		if (err)
			*err = xasprintf("%s: %s", path, strerror(errno));
		return false;
	}

	struct stat sb;
	if (fstat(fd, &sb) == 0) {
		if (S_ISDIR(sb.st_mode)) {
			close(fd);
			if (err)
				*err = xasprintf("%s: is a directory", path);
			return false;
		}
		b->mode = sb.st_mode & 07777;
	}
	b->existed = true;
	b->readonly = access(full, W_OK) != 0;

	size_t cap = 65536, len = 0;
	char *data = xmalloc(cap);
	for (;;) {
		if (len + 1 >= cap) {
			cap *= 2;
			data = xrealloc(data, cap);
		}
		ssize_t n = read(fd, data + len, cap - len - 1);
		if (n < 0) {
			close(fd);
			free(data);
			if (err)
				*err = xasprintf("%s: %s", path, strerror(errno));
			return false;
		}
		if (n == 0)
			break;
		len += (size_t)n;
	}
	close(fd);

	for (size_t i = 0; i < len; i++) {
		if (data[i] == '\0') {
			b->binary = true;
			break;
		}
	}

	/* CRLF is decided by the FIRST line ending seen, not by a majority: a file
	 * with mixed endings is a file somebody is trying to fix, and rewriting
	 * every line to match the majority buries the thing they opened it to
	 * find. Only the ending this buffer writes is chosen here; the \r on any
	 * other line stays in the text, visible. */
	b->eol = EOL_LF;
	for (size_t i = 0; i < len; i++) {
		if (data[i] == '\n') {
			b->eol = (i > 0 && data[i - 1] == '\r') ? EOL_CRLF : EOL_LF;
			break;
		}
	}

	/* Replace the buffer wholesale, without journalling: loading is not an
	 * edit and must not be undoable back to an empty buffer. */
	for (size_t i = 0; i < b->n; i++)
		line_free(&b->ln[i]);
	b->n = 0;

	size_t start = 0;
	for (size_t i = 0; i <= len; i++) {
		if (i == len || data[i] == '\n') {
			if (i == len && start == len && len > 0)
				break;              /* trailing newline: no phantom last line */
			size_t end = i;
			if (b->eol == EOL_CRLF && end > start && data[end - 1] == '\r')
				end--;
			if (b->n == b->cap) {
				b->cap = b->cap ? b->cap * 2 : 16;
				b->ln = xrealloc(b->ln, b->cap * sizeof *b->ln);
			}
			line_init(&b->ln[b->n++], data + start, end - start);
			start = i + 1;
		}
	}
	b->no_eol = (len > 0 && data[len - 1] != '\n');
	if (b->n == 0) {
		line_init(&b->ln[0], "", 0);
		b->n = 1;
		b->no_eol = false;
	}

	b->lang = syn_lang_for(full, b->ln[0].s);

	free(data);
	b->modified = false;
	b->seq = 0;
	b->saved_at = 0;
	undo_free_chain(b->head);
	b->head = b->cur = NULL;
	return true;
}

/* ── saving ─────────────────────────────────────────────────────────────── */

/* Written to a temporary beside the target, fsync'd, then renamed over it.
 *
 * The fsync is the part that matters and the part usually left out: rename()
 * is atomic with respect to other processes, but on a crash between the write
 * and the flush the directory entry can point at a file whose contents never
 * reached the disk — which is how "I saved it" becomes a zero-length file.
 *
 * The temporary is in the SAME directory because rename() cannot cross a
 * filesystem, and /tmp is very often a different one. */
bool buf_save(buf_t *b, const char *path, char **err)
{
	const char *target = path && *path ? path : b->path;
	if (!target || !*target) {
		if (err)
			*err = xstrdup("no file name");
		return false;
	}
	char *full = expand_path(target);

	char *slash = strrchr(full, '/');
	char *dir = slash ? xstrndup(full, (size_t)(slash - full)) : xstrdup(".");
	if (!*dir) {
		free(dir);
		dir = xstrdup("/");
	}
	char *tmp = xasprintf("%s/.syn-edit.XXXXXX", dir);

	int fd = mkstemp(tmp);
	if (fd < 0) {
		if (err)
			*err = xasprintf("%s: %s", target, strerror(errno));
		free(tmp); free(dir); free(full);
		return false;
	}

	/* Permissions of the file being replaced, not mkstemp's 0600 — saving a
	 * script must not silently remove its executable bit. */
	struct stat sb;
	mode_t want = (stat(full, &sb) == 0) ? (sb.st_mode & 07777) : b->mode;
	if (fchmod(fd, want) != 0 && g_verbose)
		warn(_("could not set mode on %s"), tmp);

	size_t len = 0;
	char *text = buf_text(b, &len);

	bool ok = true;
	for (size_t off = 0; off < len; ) {
		ssize_t w = write(fd, text + off, len - off);
		if (w <= 0) {
			ok = false;
			break;
		}
		off += (size_t)w;
	}
	if (ok && fsync(fd) != 0)
		ok = false;
	if (close(fd) != 0)
		ok = false;
	free(text);

	if (ok && rename(tmp, full) != 0)
		ok = false;

	if (!ok) {
		if (err)
			*err = xasprintf("%s: %s", target, strerror(errno));
		unlink(tmp);
		free(tmp); free(dir); free(full);
		return false;
	}

	/* And the directory, so the new name itself survives a crash. Best
	 * effort: some filesystems refuse O_RDONLY fsync on a directory and
	 * failing the save over it would be worse than the risk. */
	int dfd = open(dir, O_RDONLY | O_DIRECTORY);
	if (dfd >= 0) {
		if (fsync(dfd) != 0 && g_verbose)
			warn(_("could not flush directory %s"), dir);
		close(dfd);
	}

	if (!b->path || strcmp(b->path, full) != 0) {
		free(b->path);
		b->path = xstrdup(full);
		b->lang = syn_lang_for(b->path, buf_line(b, 0));
	}
	b->existed = true;
	b->readonly = false;
	b->mode = want;
	b->saved_at = b->seq;
	b->modified = false;

	free(tmp); free(dir); free(full);
	return true;
}
