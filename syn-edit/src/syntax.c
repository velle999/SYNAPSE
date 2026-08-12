/* syntax.c — one tokenizer, parameterised by a language table.
 *
 * ── Why a table and not a grammar per language ─────────────────────────────
 *
 * Every language here is highlighted with the same five questions: where do
 * comments start, what quotes a string, what is a number, what is a word, and
 * which words are special. That is a table, and a table of twenty languages is
 * twenty rows somebody can add to without understanding the scanner. Twenty
 * hand-written scanners is twenty places for the C one's bug to be re-fixed.
 *
 * It is deliberately not a parser. Highlighting `foo(` as a call because an
 * identifier is followed by a bracket is wrong for a declaration and right
 * often enough to be worth it; being wrong about a colour costs a colour.
 *
 * ── The output contract ────────────────────────────────────────────────────
 *
 * syn_scan fills spans that cover the WHOLE line with no gaps and no overlap.
 * A renderer walks them start to end and never has to ask what the bytes
 * between two spans were — the version of this that emitted only "interesting"
 * spans made every front-end reimplement the gap-filling, and the TUI and the
 * GUI disagreed about tabs.
 *
 * ── The state carried between lines ────────────────────────────────────────
 *
 * A block comment, a Python docstring and a JS template literal all outlive
 * the line they open on, so a scan takes the previous line's ending state and
 * produces this line's. A front-end that scans only the visible lines must
 * therefore scan from a known state — the GUI rescans from the top of the
 * file, which for the size of file a person edits by hand costs nothing and
 * removes an entire class of "the colours are wrong until I scroll".
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-edit.h"

#include <ctype.h>
#include <string.h>

/* Carried state. Anything other than ST_NONE means the line opens inside
 * something that started earlier. */
enum {
	ST_NONE = 0,
	ST_BLOCK,        /* inside a block comment */
	ST_TRIPLE_D,     /* inside a """ string */
	ST_TRIPLE_S,     /* inside a ''' string */
	ST_BACKTICK      /* inside a `template` */
};

#define L_DQ       (1u << 0)   /* "…" is a string */
#define L_SQ       (1u << 1)   /* '…' is a string */
#define L_SQCHAR   (1u << 2)   /* '…' is a character literal */
#define L_BT       (1u << 3)   /* `…` is a template literal, may span lines */
#define L_TRIPLE   (1u << 4)   /* """ and ''' span lines */
#define L_PREPROC  (1u << 5)   /* a leading # is a preprocessor directive */
#define L_MARKDOWN (1u << 6)
#define L_DIFF     (1u << 7)
/* Backslash is not an escape inside a SINGLE-quoted string. This is the shell,
 * where '…' is literal to the closing quote but "…" escapes normally — so it
 * cannot be a blanket "this language has no escapes" flag, or `echo "a\"b"`
 * ends its string at the escaped quote and the rest of the line goes the
 * colour of a string. */
#define L_NOESC    (1u << 8)

typedef struct {
	const char *name;
	const char *exts;        /* space separated, with the dot */
	const char *files;       /* exact basenames */
	const char *shebang;     /* interpreter basenames */
	const char *keywords;
	const char *types;
	const char *constants;
	const char *lc1, *lc2;   /* line comment introducers */
	const char *bo, *bc;     /* block comment open/close */
	unsigned flags;
} lang_def;

/* Keyword lists are deliberately not exhaustive. A missing keyword renders as
 * plain text, which is invisible; a wrong one renders a variable as a keyword,
 * which is a lie about the code. When in doubt they are left out. */
static const lang_def LANGS[] = {
{ "c", ".c .h", "", "",
  "if else for while do switch case default break continue return goto sizeof "
  "typedef struct union enum static extern inline const volatile register auto "
  "restrict _Atomic _Generic _Noreturn _Static_assert alignof",
  "void char short int long float double signed unsigned bool size_t ssize_t "
  "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t FILE",
  "NULL true false",
  "//", "", "/*", "*/", L_DQ | L_SQCHAR | L_PREPROC },

{ "cpp", ".cc .cpp .cxx .hpp .hh .hxx .ipp", "", "",
  "if else for while do switch case default break continue return goto sizeof "
  "class struct union enum namespace template typename using public private "
  "protected virtual override final friend operator new delete this throw try "
  "catch const constexpr consteval static extern inline mutable explicit "
  "noexcept nullptr static_cast dynamic_cast reinterpret_cast const_cast "
  "co_await co_return co_yield concept requires auto decltype",
  "void char short int long float double signed unsigned bool wchar_t size_t "
  "string vector map set pair unique_ptr shared_ptr",
  "NULL true false nullptr",
  "//", "", "/*", "*/", L_DQ | L_SQCHAR | L_PREPROC },

{ "python", ".py .pyw", "", "python python3 python2",
  "def class return if elif else for while break continue pass import from as "
  "with try except finally raise lambda yield global nonlocal assert del in is "
  "not and or async await match case",
  "int float str bytes bool list dict set tuple frozenset object type complex",
  "None True False self cls __name__ __main__",
  "#", "", "", "", L_DQ | L_SQ | L_TRIPLE },

{ "sh", ".sh .bash .zsh .bashrc .zshrc .profile", "PKGBUILD .bashrc .profile",
  "sh bash zsh dash ksh",
  "if then else elif fi for while until do done case esac function return "
  "break continue local export readonly declare typeset shift eval exec trap "
  "source set unset in select time coproc",
  "echo printf read cd test cat grep sed awk cut sort uniq head tail find xargs "
  "mkdir rm cp mv ln chmod chown tar curl git make",
  "true false", "#", "", "", "", L_DQ | L_SQ | L_NOESC },

{ "javascript", ".js .mjs .cjs .ts .tsx .jsx", "", "node",
  "function return if else for while do switch case default break continue "
  "var let const class extends new delete typeof instanceof in of this super "
  "import export from as async await yield try catch finally throw void "
  "static get set",
  "Object Array String Number Boolean Promise Map Set Symbol RegExp Date JSON "
  "Math console window document",
  "null undefined true false NaN Infinity",
  "//", "", "/*", "*/", L_DQ | L_SQ | L_BT },

{ "qml", ".qml", "", "",
  "import as property readonly signal function return if else for while do "
  "switch case break continue var let const new delete typeof instanceof this "
  "on component required default alias pragma",
  "int real double string bool url color date point size rect variant list "
  "Item Rectangle Text MouseArea Column Row Grid Flickable ListView GridView "
  "Repeater Loader Timer Component Connections",
  "null undefined true false parent",
  "//", "", "/*", "*/", L_DQ | L_SQ | L_BT },

{ "json", ".json .jsonc", "", "",
  "", "", "true false null", "//", "", "/*", "*/", L_DQ },

{ "rust", ".rs", "", "",
  "fn let mut const static struct enum impl trait for while loop if else match "
  "return break continue use mod pub crate super self as where type dyn move "
  "ref unsafe async await extern in box",
  "i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str "
  "String Vec Option Result Box Rc Arc HashMap",
  "true false None Some Ok Err Self",
  "//", "", "/*", "*/", L_DQ | L_SQCHAR },

{ "go", ".go", "", "",
  "func var const type struct interface map chan package import return if else "
  "for range switch case default break continue go defer select fallthrough",
  "int int8 int16 int32 int64 uint uint8 uint16 uint32 uint64 uintptr float32 "
  "float64 complex64 complex128 string bool byte rune error any",
  "true false nil iota", "//", "", "/*", "*/", L_DQ | L_SQCHAR | L_BT },

{ "meson", ".build .meson", "meson.build meson_options.txt", "",
  "if elif else endif foreach endforeach and or not in continue break "
  "project executable library shared_library static_library dependency "
  "declare_dependency configure_file install_data install_headers subdir test "
  "custom_target run_command find_program files include_directories",
  "", "true false", "#", "", "", "", L_DQ | L_SQ | L_TRIPLE },

{ "toml", ".toml", "Cargo.toml", "",
  "", "", "true false", "#", "", "", "", L_DQ | L_SQ },

{ "yaml", ".yaml .yml", "", "",
  "", "", "true false yes no null ~", "#", "", "", "", L_DQ | L_SQ },

{ "ini", ".ini .conf .cfg .desktop .service .socket .timer .mount .target",
  "mimeapps.list", "",
  "", "", "true false", "#", ";", "", "", L_DQ },

{ "css", ".css .scss .sass .less", "", "",
  "import media supports keyframes font-face charset include mixin extend use",
  "color background border margin padding width height display position font "
  "flex grid top left right bottom opacity transform transition z-index",
  "none auto inherit initial unset", "//", "", "/*", "*/", L_DQ | L_SQ },

{ "html", ".html .htm .xml .svg .ui .qrc", "", "",
  "", "", "", "", "", "<!--", "-->", L_DQ | L_SQ },

{ "makefile", ".mk .make", "Makefile GNUmakefile makefile", "",
  "ifeq ifneq ifdef ifndef else endif include define endef export unexport "
  "override vpath .PHONY .SUFFIXES .DEFAULT",
  "", "", "#", "", "", "", L_DQ | L_SQ },

{ "lua", ".lua", "", "lua",
  "function local return if then elseif else end for while do repeat until "
  "break goto in and or not",
  "string table math io os coroutine", "nil true false self",
  "--", "", "--[[", "]]", L_DQ | L_SQ },

{ "sql", ".sql", "", "",
  "select from where insert update delete create drop alter table index view "
  "join left right inner outer on group by order having limit offset union all "
  "as into values set primary key foreign references not null distinct",
  "int integer text varchar char blob real numeric boolean date timestamp",
  "true false null", "--", "#", "/*", "*/", L_DQ | L_SQ },

{ "java", ".java", "", "",
  "public private protected class interface extends implements new return if "
  "else for while do switch case default break continue import package static "
  "final abstract synchronized try catch finally throw throws this super enum "
  "instanceof record var",
  "void int long short byte char float double boolean String Object List Map "
  "Set Integer Long Double Boolean",
  "null true false", "//", "", "/*", "*/", L_DQ | L_SQCHAR },

{ "markdown", ".md .markdown", "README", "",
  "", "", "", "", "", "", "", L_MARKDOWN | L_BT },

{ "diff", ".diff .patch", "", "",
  "", "", "", "", "", "", "", L_DIFF },
};

static const size_t NLANG = sizeof LANGS / sizeof *LANGS;

/* ── language selection ─────────────────────────────────────────────────── */

/* Whole-word search in a space-separated list. Without the boundary check
 * ".h" matches inside ".hpp" and every C++ header opens as C. */
static bool in_list(const char *list, const char *word, bool ci)
{
	if (!list || !*list || !word || !*word)
		return false;
	size_t wl = strlen(word);
	for (const char *p = list; *p; ) {
		while (*p == ' ')
			p++;
		const char *q = p;
		while (*q && *q != ' ')
			q++;
		size_t n = (size_t)(q - p);
		if (n == wl && (ci ? strncasecmp(p, word, n) == 0
		                   : strncmp(p, word, n) == 0))
			return true;
		p = q;
	}
	return false;
}

int syn_lang_by_name(const char *name)
{
	if (!name)
		return -1;
	for (size_t i = 0; i < NLANG; i++)
		if (strcasecmp(LANGS[i].name, name) == 0)
			return (int)i;
	return -1;
}

const char *syn_lang_name(int lang)
{
	return (lang >= 0 && (size_t)lang < NLANG) ? LANGS[lang].name : "text";
}

size_t syn_lang_count(void) { return NLANG; }
const char *syn_lang_at(size_t i) { return i < NLANG ? LANGS[i].name : ""; }

const char *syn_comment_prefix(int lang)
{
	if (lang < 0 || (size_t)lang >= NLANG)
		return "";
	if (LANGS[lang].lc1 && *LANGS[lang].lc1)
		return LANGS[lang].lc1;
	return "";
}

int syn_lang_for(const char *path, const char *first_line)
{
	if (!path)
		path = "";
	const char *slash = strrchr(path, '/');
	const char *base = slash ? slash + 1 : path;

	/* Exact basename first. A file called "Makefile" has no extension and a
	 * file called "meson.build" has one that means nothing on its own — ".build"
	 * belongs to half a dozen build systems. */
	for (size_t i = 0; i < NLANG; i++)
		if (in_list(LANGS[i].files, base, false))
			return (int)i;

	/* Longest extension wins, so ".tar.gz"-shaped names and ".d.ts" pick the
	 * more specific rule rather than whichever row came first. */
	int best = -1;
	size_t bestlen = 0;
	for (const char *dot = strchr(base, '.'); dot; dot = strchr(dot + 1, '.')) {
		for (size_t i = 0; i < NLANG; i++) {
			if (in_list(LANGS[i].exts, dot, true) && strlen(dot) > bestlen) {
				best = (int)i;
				bestlen = strlen(dot);
			}
		}
	}
	if (best >= 0)
		return best;

	/* Then the shebang: a shell script called "build" has neither. */
	if (first_line && first_line[0] == '#' && first_line[1] == '!') {
		const char *p = first_line + 2;
		while (*p == ' ')
			p++;
		/* "/usr/bin/env python3" names the interpreter in the second word. */
		char word[64];
		for (;;) {
			const char *q = p;
			while (*q && *q != ' ' && *q != '\t')
				q++;
			size_t n = (size_t)(q - p);
			if (n == 0 || n >= sizeof word)
				break;
			memcpy(word, p, n);
			word[n] = '\0';
			char *sl = strrchr(word, '/');
			const char *nm = sl ? sl + 1 : word;
			if (strcmp(nm, "env") != 0) {
				for (size_t i = 0; i < NLANG; i++)
					if (in_list(LANGS[i].shebang, nm, false))
						return (int)i;
				break;
			}
			p = q;
			while (*p == ' ' || *p == '\t')
				p++;
			if (!*p)
				break;
		}
	}
	return -1;
}

const char *syn_tok_name(tok_t t)
{
	static const char *N[TK_N] = {
		"text", "keyword", "type", "constant", "string", "char", "number",
		"comment", "preproc", "func", "operator", "heading", "added", "removed"
	};
	return (t >= 0 && t < TK_N) ? N[t] : "text";
}

/* ── the scanner ────────────────────────────────────────────────────────── */

typedef struct {
	span_t *out;
	size_t max;
	size_t n;
	size_t covered;      /* bytes accounted for so far */
} emitter;

/* Adjacent spans of the same kind are merged. A line of C is otherwise ~40
 * one-byte spans of TK_TEXT, and every one of them costs the GUI a rich-text
 * fragment. */
static void emit(emitter *e, size_t start, size_t len, tok_t t)
{
	if (len == 0)
		return;
	if (e->n && e->out[e->n - 1].tok == t
	         && e->out[e->n - 1].start + e->out[e->n - 1].len == start) {
		e->out[e->n - 1].len += len;
		e->covered = start + len;
		return;
	}
	if (e->n < e->max) {
		e->out[e->n].start = start;
		e->out[e->n].len = len;
		e->out[e->n].tok = t;
		e->n++;
	}
	e->covered = start + len;
}

static bool word_char(unsigned char c)
{
	return isalnum(c) || c == '_';
}

/* Consumes a string opened at `i` with delimiter `q`. Returns the index just
 * past the closing quote, or len when the string runs to the end of the line.
 * An unterminated string does NOT carry to the next line for single-quoted
 * forms — in every language here that is a typo, and highlighting the rest of
 * the file as a string because of one is how a stray apostrophe in a comment
 * turns the screen a single colour. */
static size_t scan_string(const char *s, size_t len, size_t i, char q, bool esc)
{
	i++;
	while (i < len) {
		if (esc && s[i] == '\\' && i + 1 < len) {
			i += 2;
			continue;
		}
		if (s[i] == q)
			return i + 1;
		i++;
	}
	return len;
}

size_t syn_scan(int lang, const char *line, size_t len,
                syn_state *st, span_t *out, size_t max)
{
	emitter e = { out, max, 0, 0 };
	syn_state state = st ? *st : ST_NONE;

	if (lang < 0 || (size_t)lang >= NLANG) {
		emit(&e, 0, len, TK_TEXT);
		if (st) *st = ST_NONE;
		return e.n;
	}
	const lang_def *L = &LANGS[lang];
	/* Double quotes and backticks always escape; only the single-quoted form
	 * varies, and only for the shell. */
	const bool esc = true;
	const bool sq_esc = !(L->flags & L_NOESC);

	/* ── whole-line rules ────────────────────────────────────────────────
	 * diff and markdown are decided by the first character, not by tokens.
	 * Running the generic scanner over them highlights "int" inside prose. */
	if (L->flags & L_DIFF) {
		tok_t t = TK_TEXT;
		if (len >= 3 && (!strncmp(line, "+++", 3) || !strncmp(line, "---", 3)))
			t = TK_HEADING;
		else if (len >= 2 && !strncmp(line, "@@", 2))
			t = TK_PREPROC;
		else if (len && line[0] == '+')
			t = TK_ADDED;
		else if (len && line[0] == '-')
			t = TK_REMOVED;
		else if (len >= 4 && !strncmp(line, "diff", 4))
			t = TK_KEYWORD;
		emit(&e, 0, len, t);
		if (st) *st = ST_NONE;
		return e.n;
	}

	if (L->flags & L_MARKDOWN) {
		size_t i = 0;
		while (i < len && (line[i] == ' ' || line[i] == '\t'))
			i++;
		if (state == ST_BACKTICK) {
			/* Inside a fenced block: everything is code until the fence. */
			emit(&e, 0, len, TK_STRING);
			if (i + 3 <= len && !strncmp(line + i, "```", 3))
				state = ST_NONE;
			if (st) *st = state;
			return e.n;
		}
		if (i + 3 <= len && !strncmp(line + i, "```", 3)) {
			emit(&e, 0, len, TK_STRING);
			if (st) *st = ST_BACKTICK;
			return e.n;
		}
		if (i < len && line[i] == '#') {
			emit(&e, 0, len, TK_HEADING);
			if (st) *st = ST_NONE;
			return e.n;
		}
		if (i < len && line[i] == '>') {
			emit(&e, 0, len, TK_COMMENT);
			if (st) *st = ST_NONE;
			return e.n;
		}
		/* Bullets and inline `code`. */
		emit(&e, 0, i, TK_TEXT);
		size_t j = i;
		if (j < len && (line[j] == '-' || line[j] == '*' || line[j] == '+')
		            && j + 1 < len && line[j + 1] == ' ') {
			emit(&e, j, 1, TK_KEYWORD);
			j += 1;
		}
		while (j < len) {
			if (line[j] == '`') {
				size_t k = j + 1;
				while (k < len && line[k] != '`')
					k++;
				emit(&e, j, (k < len ? k + 1 : len) - j, TK_STRING);
				j = k < len ? k + 1 : len;
			} else {
				emit(&e, j, 1, TK_TEXT);
				j++;
			}
		}
		if (st) *st = ST_NONE;
		return e.n;
	}

	size_t i = 0;

	/* ── resume whatever the previous line left open ─────────────────── */
	if (state == ST_BLOCK && L->bc && *L->bc) {
		size_t bl = strlen(L->bc);
		while (i < len) {
			if (i + bl <= len && !strncmp(line + i, L->bc, bl)) {
				i += bl;
				state = ST_NONE;
				break;
			}
			i++;
		}
		emit(&e, 0, i, TK_COMMENT);
	} else if (state == ST_TRIPLE_D || state == ST_TRIPLE_S) {
		const char *q = (state == ST_TRIPLE_D) ? "\"\"\"" : "'''";
		while (i < len) {
			if (i + 3 <= len && !strncmp(line + i, q, 3)) {
				i += 3;
				state = ST_NONE;
				break;
			}
			i++;
		}
		emit(&e, 0, i, TK_STRING);
	} else if (state == ST_BACKTICK) {
		while (i < len) {
			if (esc && line[i] == '\\' && i + 1 < len) {
				i += 2;
				continue;
			}
			if (line[i] == '`') {
				i++;
				state = ST_NONE;
				break;
			}
			i++;
		}
		emit(&e, 0, i, TK_STRING);
	} else {
		state = ST_NONE;
	}

	/* ── the preprocessor line ───────────────────────────────────────────
	 * Only when # is the first non-blank byte. In C a # anywhere else is a
	 * stringify operator inside a macro, not a directive. */
	if (state == ST_NONE && (L->flags & L_PREPROC) && i == 0) {
		size_t j = 0;
		while (j < len && (line[j] == ' ' || line[j] == '\t'))
			j++;
		if (j < len && line[j] == '#') {
			size_t k = j + 1;
			while (k < len && isalpha((unsigned char)line[k]))
				k++;
			emit(&e, 0, j, TK_TEXT);
			emit(&e, j, k - j, TK_PREPROC);
			i = k;
			/* Scanning continues rather than claiming the whole line: the
			 * rest of a directive still holds strings and comments, and
			 * an include's header name and a trailing comment both matter. */
		}
	}

	size_t lc1 = L->lc1 ? strlen(L->lc1) : 0;
	size_t lc2 = L->lc2 ? strlen(L->lc2) : 0;
	size_t bo  = L->bo  ? strlen(L->bo)  : 0;
	size_t bc  = L->bc  ? strlen(L->bc)  : 0;

	while (i < len) {
		unsigned char c = (unsigned char)line[i];

		/* line comment */
		if (lc1 && i + lc1 <= len && !strncmp(line + i, L->lc1, lc1)) {
			emit(&e, i, len - i, TK_COMMENT);
			i = len;
			break;
		}
		if (lc2 && i + lc2 <= len && !strncmp(line + i, L->lc2, lc2)) {
			emit(&e, i, len - i, TK_COMMENT);
			i = len;
			break;
		}

		/* block comment */
		if (bo && i + bo <= len && !strncmp(line + i, L->bo, bo)) {
			size_t j = i + bo;
			bool closed = false;
			while (j < len) {
				if (bc && j + bc <= len && !strncmp(line + j, L->bc, bc)) {
					j += bc;
					closed = true;
					break;
				}
				j++;
			}
			emit(&e, i, j - i, TK_COMMENT);
			i = j;
			if (!closed)
				state = ST_BLOCK;
			continue;
		}

		/* triple-quoted strings, which must be tested before the single ones */
		if ((L->flags & L_TRIPLE) && i + 3 <= len
		    && (!strncmp(line + i, "\"\"\"", 3) || !strncmp(line + i, "'''", 3))) {
			const char *q = line[i] == '"' ? "\"\"\"" : "'''";
			size_t j = i + 3;
			bool closed = false;
			while (j < len) {
				if (j + 3 <= len && !strncmp(line + j, q, 3)) {
					j += 3;
					closed = true;
					break;
				}
				j++;
			}
			emit(&e, i, j - i, TK_STRING);
			i = j;
			if (!closed)
				state = (q[0] == '"') ? ST_TRIPLE_D : ST_TRIPLE_S;
			continue;
		}

		if ((L->flags & L_DQ) && c == '"') {
			size_t j = scan_string(line, len, i, '"', esc);
			emit(&e, i, j - i, TK_STRING);
			i = j;
			continue;
		}
		if ((L->flags & L_SQ) && c == '\'') {
			size_t j = scan_string(line, len, i, '\'', sq_esc);
			emit(&e, i, j - i, TK_STRING);
			i = j;
			continue;
		}
		if ((L->flags & L_SQCHAR) && c == '\'') {
			size_t j = scan_string(line, len, i, '\'', true);
			emit(&e, i, j - i, TK_CHAR);
			i = j;
			continue;
		}
		if ((L->flags & L_BT) && c == '`') {
			size_t j = i + 1;
			bool closed = false;
			while (j < len) {
				if (esc && line[j] == '\\' && j + 1 < len) {
					j += 2;
					continue;
				}
				if (line[j] == '`') {
					j++;
					closed = true;
					break;
				}
				j++;
			}
			emit(&e, i, j - i, TK_STRING);
			i = j;
			if (!closed)
				state = ST_BACKTICK;
			continue;
		}

		/* numbers — only when they start a word, or 3 in "utf8" lights up */
		if (isdigit(c) && (i == 0 || !word_char((unsigned char)line[i - 1]))) {
			size_t j = i;
			if (c == '0' && j + 1 < len
			    && (line[j + 1] == 'x' || line[j + 1] == 'X'
			     || line[j + 1] == 'b' || line[j + 1] == 'B'))
				j += 2;
			while (j < len && (isalnum((unsigned char)line[j])
			                || line[j] == '.' || line[j] == '_'))
				j++;
			emit(&e, i, j - i, TK_NUMBER);
			i = j;
			continue;
		}

		/* words */
		if (isalpha(c) || c == '_' || c == '$' || c == '.' || c == '@') {
			size_t j = i;
			/* A leading . or @ is part of the word for Makefile targets
			 * (.PHONY) and decorators/attributes (@property). It is not
			 * swallowed mid-word, so a.b stays two words. */
			if (line[j] == '.' || line[j] == '@' || line[j] == '$')
				j++;
			while (j < len && word_char((unsigned char)line[j]))
				j++;
			if (j == i) {
				emit(&e, i, 1, TK_OPERATOR);
				i++;
				continue;
			}
			char word[128];
			size_t n = j - i;
			tok_t t = TK_TEXT;
			if (n < sizeof word) {
				memcpy(word, line + i, n);
				word[n] = '\0';
				if (in_list(L->keywords, word, false))
					t = TK_KEYWORD;
				else if (in_list(L->types, word, false))
					t = TK_TYPE;
				else if (in_list(L->constants, word, false))
					t = TK_CONSTANT;
				else {
					/* An identifier that a bracket follows reads as a call.
					 * Whitespace between them is allowed — `foo ()` is still a
					 * call — but a newline is not, or every line ending in a
					 * name would light up. */
					size_t k = j;
					while (k < len && (line[k] == ' ' || line[k] == '\t'))
						k++;
					if (k < len && line[k] == '(')
						t = TK_FUNC;
				}
			}
			emit(&e, i, n, t);
			i = j;
			continue;
		}

		if (isspace(c)) {
			size_t j = i;
			while (j < len && isspace((unsigned char)line[j]))
				j++;
			emit(&e, i, j - i, TK_TEXT);
			i = j;
			continue;
		}

		if (strchr("+-*/%=<>!&|^~?:;,.(){}[]", (char)c)) {
			emit(&e, i, 1, TK_OPERATOR);
			i++;
			continue;
		}

		emit(&e, i, 1, TK_TEXT);
		i++;
	}

	/* The contract: nothing uncovered. */
	if (e.covered < len)
		emit(&e, e.covered, len - e.covered, TK_TEXT);

	if (st)
		*st = state;
	return e.n;
}
