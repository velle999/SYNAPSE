/* syn-edit.h — the SynapseOS text editor.
 *
 * ── The one design decision ────────────────────────────────────────────────
 *
 * There is ONE editing engine and it is modal. The terminal front-end and the
 * graphical front-end are both renderers over it: they collect keys, hand them
 * to ed_key(), and draw whatever the engine says the buffer now looks like.
 * Neither of them contains a rule about what a key does.
 *
 * That is not tidiness for its own sake. An editor with two implementations of
 * "what does dw do" has two answers to it, and the one nobody is looking at is
 * always the wrong one. It also means the engine can be driven with no
 * terminal and no display at all — `syn-edit run --keys 'ggdG' file` — which is
 * how every one of the tests exercises it, and why the vim layer is tested at
 * all rather than being the part everybody hopes works.
 *
 * ── The one rule for mutation ──────────────────────────────────────────────
 *
 * Every change to a buffer goes through buf_splice(). Nothing writes b->ln[i]
 * directly. Undo is a journal of splices and their inverses, so an operation
 * that bypassed the primitive would be an operation that cannot be undone —
 * and an undo stack with a hole in it is worse than no undo, because it is
 * trusted.
 *
 * ── Text is BYTES ──────────────────────────────────────────────────────────
 *
 * A column is a byte offset, not a character. Files are not always UTF-8, and
 * an editor that refuses to open a file it cannot decode is an editor that
 * cannot fix the file. Display width is computed separately (utf8_*), so a
 * multi-byte character draws as one column while the cursor still addresses
 * the byte the buffer actually holds.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_EDIT_H
#define SYN_EDIT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#define SYNAPSE_DONATE_URL "https://buymeacoffee.com/velle999"

/* ── output ─────────────────────────────────────────────────────────────── */

typedef enum { OUT_HUMAN, OUT_REC } out_mode_t;

extern out_mode_t g_out;
extern bool g_color;
extern bool g_verbose;

/* ── util.c ─────────────────────────────────────────────────────────────── */

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void die(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
void warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

const char *C_RESET(void);
const char *C_BOLD(void);
const char *C_DIM(void);
const char *C_ACCENT(void);
const char *C_WARN(void);
const char *C_BAD(void);

char *pct_encode(const char *s, bool keep_slash);
char *pct_decode(const char *s);
/* Encoding happens inside rec_row, for every field, so that a column added
 * later cannot arrive unencoded. See syn-disks' util.c for the same rule and
 * the reason: a filename is arbitrary bytes and a tab in one silently shifts
 * every column of a record. */
void rec_row(int nfields, ...);

char *slurp(const char *path);
bool have_cmd(const char *name);
char *run_capture(char *const argv[], int *status, bool quiet_stderr);
char *expand_path(const char *p);   /* ~ and $HOME */
char *config_dir(void);
char *data_dir(void);
bool mkdir_p(const char *path);

/* Display width. Tabs expand to the next multiple of ts; a byte that is a
 * UTF-8 continuation adds nothing. */
size_t utf8_len(unsigned char c);          /* bytes in the sequence, >= 1 */
size_t disp_width(const char *s, size_t len, int ts);
/* terminal columns, NOT editor cells — see the note in util.c */
size_t term_cols(const char *s);
size_t disp_col(const char *s, size_t bytes, int ts);   /* width of a prefix */

/* ── buffer.c ───────────────────────────────────────────────────────────── */

typedef enum { EOL_LF, EOL_CRLF } eol_t;

typedef struct {
	char *s;
	size_t len;
	size_t cap;
} line_t;

typedef struct undo_step {
	size_t at;              /* first line replaced */
	size_t nins;            /* how many lines the splice put there */
	line_t *del;            /* what it took out */
	size_t ndel;
	size_t cy, cx;          /* cursor before the change */
	bool boundary;          /* first step of an undo group */
	struct undo_step *prev, *next;
} undo_step;

typedef struct {
	char *path;             /* NULL until saved; "" is never used */
	line_t *ln;
	size_t n;               /* always >= 1: an empty buffer is one empty line */
	size_t cap;

	bool modified;
	bool readonly;          /* file exists and is not writable, or :view */
	bool binary;            /* a NUL was found while loading */
	bool no_eol;            /* the file did not end with a newline */
	bool existed;
	eol_t eol;
	mode_t mode;            /* preserved across a save */

	/* Undo. Steps hang off a doubly-linked list; `cur` is the last applied
	 * one, so redo is simply cur->next. */
	undo_step *head, *cur;
	bool in_group;
	size_t saved_at;        /* undo sequence number at the last save */
	size_t seq;

	/* Marks a–z, then '' (26) and the two the visual selection leaves behind,
	 * '< (27) and '> (28). Line numbers, so 0 means unset and a mark on line
	 * one is 1 — a 0-based array here would make "unset" and "line 1" the
	 * same value. */
	size_t mark[29];
	size_t markx[29];

	int lang;               /* index into the syntax table, -1 = none */
} buf_t;

buf_t *buf_new(void);
void   buf_free(buf_t *b);
bool   buf_load(buf_t *b, const char *path, char **err);
bool   buf_save(buf_t *b, const char *path, char **err);
void   buf_splice(buf_t *b, size_t at, size_t ndel,
                  char *const *ins, size_t nins, size_t cy, size_t cx);
void   buf_group_begin(buf_t *b);
void   buf_group_end(buf_t *b);
bool   buf_undo(buf_t *b, size_t *cy, size_t *cx);
bool   buf_redo(buf_t *b, size_t *cy, size_t *cx);
const char *buf_line(const buf_t *b, size_t i);
size_t buf_linelen(const buf_t *b, size_t i);
char  *buf_text(const buf_t *b, size_t *len);   /* whole buffer, caller frees */
const char *buf_name(const buf_t *b);           /* path, or "[No Name]" */

/* ── syntax.c ───────────────────────────────────────────────────────────── */

typedef enum {
	TK_TEXT = 0,
	TK_KEYWORD,
	TK_TYPE,
	TK_CONSTANT,
	TK_STRING,
	TK_CHAR,
	TK_NUMBER,
	TK_COMMENT,
	TK_PREPROC,
	TK_FUNC,
	TK_OPERATOR,
	TK_HEADING,
	TK_ADDED,
	TK_REMOVED,
	TK_N
} tok_t;

typedef struct {
	size_t start, len;
	tok_t tok;
} span_t;

/* The carried state between lines: a block comment or a here-doc does not end
 * on the line it started. Zero is "nothing carried". */
typedef unsigned syn_state;

int          syn_lang_by_name(const char *name);
int          syn_lang_for(const char *path, const char *first_line);
const char  *syn_lang_name(int lang);
size_t       syn_lang_count(void);
const char  *syn_lang_at(size_t i);
const char  *syn_tok_name(tok_t t);
/* Fills spans covering the WHOLE line with no gaps, so a renderer can walk
 * them and never has to work out what the uncovered bytes were. Returns the
 * count and updates *st for the next line. */
size_t       syn_scan(int lang, const char *line, size_t len,
                      syn_state *st, span_t *out, size_t max);
const char  *syn_comment_prefix(int lang);   /* for gc / :set comment */

/* ── vim.c: the engine ──────────────────────────────────────────────────── */

/* edmode_t, not mode_t: <sys/types.h> already owns that name for a file's
 * permission bits, and buf_t stores one. A typedef that shadows a POSIX type
 * compiles in some translation units and not others. */
typedef enum {
	M_NORMAL, M_INSERT, M_REPLACE,
	M_VISUAL, M_VISUAL_LINE, M_VISUAL_BLOCK,
	M_CMDLINE
} edmode_t;

/* Keys above 0xff are the ones a terminal spells with an escape sequence and
 * a GUI has a name for. Everything else is the byte itself, so Ctrl-R is 18
 * and needs no table. */
enum {
	K_NONE  = -1,
	K_ESC   = 27,
	K_BS    = 127,
	K_UP    = 0x100, K_DOWN, K_LEFT, K_RIGHT,
	K_HOME, K_END, K_PGUP, K_PGDN, K_DEL, K_INS
};

#define NREG   64        /* a–z, 0–9, ", -, +, *, / and . */
#define MAXBUF 64

typedef struct {
	char *text;
	bool linewise;
} reg_t;

typedef struct {
	int  tabstop;
	int  shiftwidth;
	bool expandtab;
	bool autoindent;
	bool number;
	bool ignorecase;
	bool smartcase;
	bool wrap;
	bool hlsearch;
	bool showtabs;      /* GUI: draw the tab bar */
	bool tree;          /* GUI: draw the document sidebar */
	int  tree_width;    /* GUI: how wide it is, in points before scaling */
	int  text_scale;    /* GUI only, 75..175 */
} opts_t;

typedef struct ed {
	buf_t *buf[MAXBUF];
	size_t nbuf, cur;

	mode_t mode;
	size_t cy, cx;
	size_t want_col;        /* the column j/k tries to keep */

	/* pending command state */
	long   count;           /* 0 = none given */
	long   opcount;
	int    op;              /* pending operator, 0 = none */
	int    opg;             /* 'g' prefixed operator: u U ~ */
	int    prefix;          /* g z " ' ` m f F t T r Z @ q [ ] */
	int    reg;             /* register named for the pending command */
	char   pendkeys[16];    /* what has been typed toward a command */
	size_t npend;

	/* visual */
	size_t vy, vx;

	/* insert-mode bookkeeping */
	size_t ins_y, ins_x;
	long   ins_count;       /* 3i... repeats the inserted text */
	int    ins_cmd;         /* the key that entered insert, for repeat */

	reg_t  regs[NREG];

	char  *search;
	int    search_dir;
	int    last_ft, last_ft_cmd;   /* for ; and , */
	int    last_macro;             /* for @@ */

	/* the command line being typed (: / ?) */
	char   cmd[1024];
	size_t ncmd;
	int    cmdchar;

	/* messages the front-ends display */
	char   msg[1024];
	bool   msg_err;

	/* `.` — the keys of the last change, replayed verbatim */
	char   dot[512];
	size_t ndot;
	char   dotpend[512];
	size_t ndotpend;
	bool   dot_ins;         /* the command is still collecting inserted text */
	bool   replaying;       /* inside `.` or @x — do not re-record */

	/* What the front-end is showing. H, M, L, Ctrl-D and Ctrl-U are defined
	 * in terms of the SCREEN, so an engine with no notion of one either has
	 * to omit them or invent a viewport that disagrees with the window. The
	 * front-end sets these before handing over a key; the default is "the
	 * whole buffer", which is what the headless driver wants. */
	size_t view_top;
	size_t view_rows;

	/* q/@ macro recording */
	int    rec_reg;         /* 0 = not recording */
	char   rec[4096];
	size_t nrec;

	opts_t o;

	bool   quit;
	int    exit_code;

	/* Set by the engine when something outside the buffer must happen — the
	 * GUI and the TUI both poll it. */
	char  *want_open;       /* :e path */
} ed_t;

ed_t *ed_new(void);
void  ed_free(ed_t *e);
buf_t *ed_buf(ed_t *e);
int   ed_open(ed_t *e, const char *path, char **err);
void  ed_key(ed_t *e, int key);
void  ed_keys(ed_t *e, const char *s);      /* a whole sequence, see ed_keys() */
void  ed_message(ed_t *e, bool err, const char *fmt, ...)
      __attribute__((format(printf, 3, 4)));
const char *ed_mode_name(const ed_t *e);
/* The visual selection as a normalised span, or false when not in visual. */
bool  ed_selection(const ed_t *e, size_t *y0, size_t *x0, size_t *y1, size_t *x1);
bool  ed_ex(ed_t *e, const char *line);     /* one ex command, no leading ':' */

/* Key notation. "<Esc>x<C-r>" is what a test, a config file and the GUI all
 * spell a key sequence with; parsing it in one place is what stops those three
 * from disagreeing. Returns the key and advances *p. */
int   key_parse(const char **p);
const char *key_name(int key);

/* ── config.c ───────────────────────────────────────────────────────────── */

void opts_defaults(opts_t *o);
bool opts_set(opts_t *o, const char *key, const char *val, char **err);
bool opts_get(const opts_t *o, const char *key, char *out, size_t n);
void opts_load(opts_t *o);
bool opts_save(const opts_t *o, char **err);
size_t opts_count(void);
const char *opts_key_at(size_t i);

/* ── commands ───────────────────────────────────────────────────────────── */

int cmd_run(int argc, char **argv);
int cmd_ex_cli(int argc, char **argv);
int cmd_highlight(int argc, char **argv);
int cmd_langs(int argc, char **argv);
int cmd_config(int argc, char **argv);
int cmd_serve(int argc, char **argv);
int cmd_tui(int argc, char **argv);
int cmd_gui(int argc, char **argv);
int cmd_about(int argc, char **argv);

#endif /* SYN_EDIT_H */
