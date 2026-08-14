/* config.c — the file, because `--font=` on every launch is not a terminal.
 *
 * ── Why the format is this small ───────────────────────────────────────────
 *
 * `key = value`, one per line, and a line starting with `#` is a comment. No
 * sections, no includes, no interpolation, no library. Everything a terminal is configured
 * with is a scalar: a font name, a size, a colour, a number of lines. Richer
 * formats buy nothing here and cost a dependency, a parser somebody has to
 * trust, and a set of failure modes ("why is my TOML string being parsed as a
 * date") that have nothing to do with terminals.
 *
 * ── The three rules that matter more than the format ───────────────────────
 *
 * ⚠ A BROKEN LINE NEVER STOPS THE TERMINAL STARTING. It is reported with its
 * line number, and the rest of the file is still read. A terminal that refuses
 * to open because of a typo in its config is a terminal that cannot be used to
 * fix the typo, and the person is left opening a different one — which is the
 * moment they wonder why they are running this at all.
 *
 * ⚠ AN UNKNOWN KEY IS REPORTED, NOT IGNORED. Silently dropping `font_famly` is
 * how a misspelling becomes half an hour of wondering why the font never
 * changed, and then a bug report about fonts.
 *
 * ⚠ FLAGS BEAT THE FILE, and `--no-config` ignores it. Both are what make the
 * command line still work once a config exists — and the second is what lets
 * the test suite assert on a terminal that behaves the same on a machine where
 * somebody has set a 30-point font.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void st_config_defaults(st_config_t *c)
{
	memset(c, 0, sizeof *c);
	c->font_size    = 0.0;
	c->scrollback   = -1;
	c->scroll_lines = 0;
	c->deadline     = -1;
	c->fg = c->bg = c->cursor = c->cursor_text = ST_CFG_UNSET;
	for (int i = 0; i < 16; i++)
		c->palette[i] = ST_CFG_UNSET;
}

void st_config_free(st_config_t *c)
{
	free(c->font);
	c->font = NULL;
}

const char *st_config_path(char *buf, size_t cap)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg)
		snprintf(buf, cap, "%s/syntty/syntty.conf", xdg);
	else {
		const char *home = getenv("HOME");
		snprintf(buf, cap, "%s/.config/syntty/syntty.conf",
		         home && *home ? home : ".");
	}
	return buf;
}

/* ── values ─────────────────────────────────────────────────────────────── */

static char *trim(char *s)
{
	while (*s && isspace((unsigned char)*s))
		s++;
	char *end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = '\0';
	return s;
}

/* A colour: `#rrggbb`, `0xrrggbb`, `rrggbb`, or one of the sixteen names that
 * everybody already uses for palette entries. Returns false for anything else,
 * which the caller reports rather than guessing at. */
static bool parse_color(const char *v, uint32_t *out)
{
	static const char *const names[16] = {
		"black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
		"bright_black", "bright_red", "bright_green", "bright_yellow",
		"bright_blue", "bright_magenta", "bright_cyan", "bright_white",
	};
	for (int i = 0; i < 16; i++)
		if (!strcmp(v, names[i])) {
			/* An INDEX, not an RGB value — resolved against whatever the
			 * palette is at the time, so `foreground = bright_white` follows a
			 * palette the same file may have changed two lines earlier. */
			*out = ST_COL_INDEXED | (uint32_t)i;
			return true;
		}

	if (*v == '#')
		v++;
	else if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X'))
		v += 2;

	if (strlen(v) != 6)
		return false;
	for (int i = 0; i < 6; i++)
		if (!isxdigit((unsigned char)v[i]))
			return false;

	*out = (uint32_t)strtoul(v, NULL, 16) & 0xFFFFFFu;
	return true;
}

static bool parse_bool(const char *v, int *out)
{
	if (!strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "on")
	    || !strcmp(v, "1")) { *out = 1; return true; }
	if (!strcmp(v, "false") || !strcmp(v, "no") || !strcmp(v, "off")
	    || !strcmp(v, "0")) { *out = 0; return true; }
	return false;
}

/* An integer that is really an integer: `rows = 24x` is a mistake, not 24. */
static bool parse_long(const char *v, long *out)
{
	char *end = NULL;
	long n = strtol(v, &end, 10);
	if (end == v || (end && *end))
		return false;
	*out = n;
	return true;
}

static void oops(st_config_t *c, int line, const char *what, const char *detail)
{
	c->errors++;
	if (c->first_error[0])
		return;
	snprintf(c->first_error, sizeof c->first_error, "line %d: %s%s%s",
	         line, what, detail ? ": " : "", detail ? detail : "");
}

/* ── one line ───────────────────────────────────────────────────────────── */

static void set_key(st_config_t *c, char *key, char *val, int line)
{
	long n = 0;
	uint32_t col = 0;
	int b = 0;

	if (!strcmp(key, "font")) {
		free(c->font);
		c->font = xstrdup(val);
		return;
	}
	if (!strcmp(key, "font_size")) {
		char *end = NULL;
		double d = strtod(val, &end);
		if (end == val || (end && *end) || d <= 0)
			oops(c, line, "font_size wants a positive number", val);
		else
			c->font_size = d;
		return;
	}
	if (!strcmp(key, "columns") || !strcmp(key, "cols")) {
		if (!parse_long(val, &n) || n < 1 || n > 4096)
			oops(c, line, "columns wants 1..4096", val);
		else
			c->cols = (int)n;
		return;
	}
	if (!strcmp(key, "rows")) {
		if (!parse_long(val, &n) || n < 1 || n > 4096)
			oops(c, line, "rows wants 1..4096", val);
		else
			c->rows = (int)n;
		return;
	}
	if (!strcmp(key, "scrollback")) {
		if (!parse_long(val, &n) || n < 0 || n > 10000000)
			oops(c, line, "scrollback wants 0..10000000 lines", val);
		else
			c->scrollback = n;
		return;
	}
	if (!strcmp(key, "scroll_lines")) {
		if (!parse_long(val, &n) || n < 1 || n > 100)
			oops(c, line, "scroll_lines wants 1..100", val);
		else
			c->scroll_lines = (int)n;
		return;
	}
	if (!strcmp(key, "deadline")) {
		if (!parse_bool(val, &b))
			oops(c, line, "deadline wants true or false", val);
		else
			c->deadline = b;
		return;
	}

	if (!strcmp(key, "foreground") || !strcmp(key, "background")
	    || !strcmp(key, "cursor") || !strcmp(key, "cursor_text")) {
		if (!parse_color(val, &col)) {
			oops(c, line, "not a colour (#rrggbb, or a palette name)", val);
			return;
		}
		if      (!strcmp(key, "foreground"))  c->fg = col;
		else if (!strcmp(key, "background"))  c->bg = col;
		else if (!strcmp(key, "cursor"))      c->cursor = col;
		else                                  c->cursor_text = col;
		return;
	}

	/* color0 .. color15 — the sixteen a program names with SGR 30..37 and
	 * 90..97, and the ones every theme in the world is expressed in. */
	if (!strncmp(key, "color", 5)) {
		char *end = NULL;
		long idx = strtol(key + 5, &end, 10);
		if (end && !*end && idx >= 0 && idx < 16) {
			if (!parse_color(val, &col))
				oops(c, line, "not a colour (#rrggbb, or a palette name)", val);
			else if (col & 0xFF000000u)
				oops(c, line, "a palette entry cannot name another one", val);
			else
				c->palette[idx] = col;
			return;
		}
	}

	/* ⚠ NOT SILENTLY IGNORED. See the header — a misspelled key that does
	 * nothing and says nothing is the worst outcome available here. */
	oops(c, line, "unknown setting", key);
}

bool st_config_load(st_config_t *c, const char *path)
{
	char buf[512];
	if (path)
		snprintf(c->path, sizeof c->path, "%s", path);
	else
		st_config_path(c->path, sizeof c->path);
	(void)buf;

	FILE *f = fopen(c->path, "r");
	if (!f) {
		/* ⚠ NOT AN ERROR. Most people never write one, and a terminal that
		 * complained about a missing config on every start would be noise on
		 * every start. */
		c->found = false;
		return false;
	}
	c->found = true;

	char line[1024];
	int lineno = 0;
	while (fgets(line, sizeof line, f)) {
		lineno++;

		/* ⚠ A COMMENT IS A WHOLE LINE, AND IT HAS TO BE. The comment character
		 * and the colour prefix are the SAME CHARACTER: strip `#` to the end
		 * of the line and `background = #123456` becomes `background =` with
		 * no value — every colour in the file silently stops working, and the
		 * error it produces ("no value") points at nothing that looks wrong.
		 * This was written the obvious way first and the first test config
		 * caught it.
		 *
		 * So: a line whose first non-blank character is `#` is a comment, and
		 * there are no trailing comments. That rule has no exceptions to
		 * remember, which is worth more here than the convenience. */
		char *s = trim(line);
		if (*s == '#')
			continue;
		if (!*s)
			continue;

		char *eq = strchr(s, '=');
		if (!eq) {
			oops(c, lineno, "no '=' — settings are `key = value`", s);
			continue;
		}
		*eq = '\0';
		char *key = trim(s);
		char *val = trim(eq + 1);
		if (!*key) {
			oops(c, lineno, "a setting with no name", NULL);
			continue;
		}
		if (!*val) {
			oops(c, lineno, "no value", key);
			continue;
		}
		set_key(c, key, val, lineno);
	}
	fclose(f);
	return true;
}

/* ⚠ THIS IS HOW ANYBODY FINDS OUT THE FILE EXISTS. The compositor this ships
 * beside spent months with a configuration file that was documented nowhere and
 * installed nowhere, and the answer is not a wiki page — it is a subcommand
 * that writes a working example:
 *
 *     syntty config --example > ~/.config/syntty/syntty.conf
 */
void st_config_example(FILE *out)
{
	fputs(
"# syntty.conf — `key = value`, one per line.\n"
"#\n"
"# A line beginning with # is a comment. There are no TRAILING comments, on\n"
"# purpose: # is also how a colour is written, and stripping it to the end of\n"
"# the line would silently empty every colour setting in this file.\n"
"#\n"
"# Written to $XDG_CONFIG_HOME/syntty/syntty.conf, or\n"
"# ~/.config/syntty/syntty.conf when that is unset. Command-line flags win\n"
"# over anything here, and `--no-config` ignores the file entirely.\n"
"#\n"
"# Every line below is commented out and shows the built-in default.\n"
"\n"
"# The font. Any fontconfig family name; the file it resolves to is cached,\n"
"# so this costs 0.03 ms at startup rather than 7.4 ms.\n"
"# font = monospace\n"
"# font_size = 14\n"
"\n"
"# The size a window asks for when the compositor lets it choose. A tiling\n"
"# compositor will not, and the grid follows the window rather than fighting\n"
"# it.\n"
"# columns = 80\n"
"# rows = 24\n"
"\n"
"# Lines of history kept. They are allocated as output scrolls off, not up\n"
"# front: 10,000 lines of 200 columns is about 350 KB, and costs nothing\n"
"# until it is used.\n"
"# scrollback = 1000\n"
"\n"
"# Lines the wheel moves per notch.\n"
"# scroll_lines = 3\n"
"\n"
"# Paint as late as the next refresh allows, so a keystroke arriving after\n"
"# the frame callback still makes it into THAT frame. Turn it off to compare.\n"
"# deadline = true\n"
"\n"
"# Colours: #rrggbb, 0xrrggbb, rrggbb, or one of the sixteen palette names\n"
"# (black, red, ..., bright_white). `foreground = bright_white` follows the\n"
"# palette; `foreground = #f0f4fa` does not.\n"
"# foreground = #C8CDD6\n"
"# background = #1B1F26\n"
"\n"
"# The cursor block, and the text under it. Unset means the cell's own\n"
"# colours are inverted, which contrasts with whatever is underneath.\n"
"# cursor = #C8CDD6\n"
"# cursor_text = #1B1F26\n"
"\n"
"# The sixteen. color0..color7 are what SGR 30..37 name; color8..color15 are\n"
"# the bright half, SGR 90..97.\n"
"# color0  = #1B1F26\n"
"# color1  = #CC5555\n"
"# color2  = #5FA85F\n"
"# color3  = #C7A03C\n"
"# color4  = #4C86C4\n"
"# color5  = #9A76DB\n"
"# color6  = #4FA8A8\n"
"# color7  = #C8CDD6\n"
"# color8  = #4A5260\n"
"# color9  = #E87B7B\n"
"# color10 = #86C986\n"
"# color11 = #E3C25F\n"
"# color12 = #74A6E0\n"
"# color13 = #A391E8\n"
"# color14 = #74C9C9\n"
"# color15 = #F0F4FA\n",
	out);
}
