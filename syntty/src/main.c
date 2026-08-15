/* main.c — the headless front ends.
 *
 * There is no window here and that is the stage's whole point. What these
 * subcommands give is a terminal that can be TESTED and MEASURED with no seat,
 * no compositor and no human:
 *
 *   syntty dump FILE     feed a byte stream through the parser, print the screen
 *   syntty run -- CMD    run a command on a real pty, print the final screen
 *   syntty bench FILE    the throughput number, which is the claim being made
 *
 * `bench` exists in the first commit on purpose. The design this is built from
 * measured kitty and foot at 118.6 ms and 117.9 ms for 2.6 MB — a dead heat, so
 * there is no champion to copy and no lead to erode. A parser that cannot be
 * timed from the beginning is one that gets a benchmark added later, once the
 * number has stopped being embarrassing.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SYNTTY_VERSION "0.1.0"

static const char *usage_text =
"syntty " SYNTTY_VERSION " — the SynapseOS terminal\n"
"\n"
"  syntty dump [FILE]        feed a stream through the parser, print the screen\n"
"  syntty run [--] CMD...    run CMD on a pty, print the screen it left behind\n"
"  syntty bench [FILE]       parse throughput, in MB/s\n"
"  syntty font               the font that would be used, and what it cost\n"
"  syntty render [FILE]      parse a stream and paint it — see --out\n"
"  syntty damage-check [F]   prove damage tracking draws the same pixels\n"
"  syntty win [--] [CMD...]  a WINDOW, running CMD or your shell\n"
"  syntty [-e CMD...]        the same window — no subcommand means the window,\n"
"                            and -e is the convention other terminals take\n"
"  syntty mouse EVENT...     what a pointer event becomes on the child's input\n"
"  syntty paste TEXT         what pasted text becomes on the way to the child\n"
"  syntty config             the config file, where it is, and what it says\n"
"  syntty config --example   a commented config to start from\n"
"  syntty about              what this is and what it can do yet\n"
"\n"
"Options, before the subcommand:\n"
"  --cols=N --rows=N         the grid to parse into (default 80x24)\n"
"  --scrollback=N            scrollback lines to keep (default 1000)\n"
"  --styled                  dump the style index under each row\n"
"  --scrollback-too          dump the scrollback above the screen\n"
"  --stats                   what the parser could not handle, and the memory\n"
"  --runs=N                  bench: passes over the input (default 5)\n"
"  --font=NAME --font-size=N the font to rasterise (default monospace, 14)\n"
"  --out=FILE                render: write the painted screen as a PPM\n"
"  --no-cursor               render: leave the cursor cell unpainted\n"
"  --probe=COL,ROW           render: print that cell's background colour\n"
"  --no-deadline             win: paint on the frame callback, not the deadline\n"
"  --view=N                  scroll the view N lines back before dumping\n"
"  --jump=N                  jump back N prompt marks before dumping\n"
"  --select=C0,R0,C1,R1      print the text between two cells, as copied\n"
"  --click=C,R[,word|line]   select as a click would, and print what it copies\n"
"  --drag=C,R                ...dragged to there before the button came up\n"
"  --scroll-after=N          ...then N lines of output arrive underneath it\n"
"  --config=FILE             read that instead of the usual place\n"
"  --no-config               ignore the config file entirely\n"
"  --tabs=N                  win: open N tabs at startup, all running CMD\n"
"  --resize=COLSxROWS        resize the grid after the stream, before dumping\n"
"\n"
"mouse: EVENT is press:BUTTON@COL,ROW, release:..., move[:BUTTON]@COL,ROW or\n"
"wheel:up@COL,ROW; with --mode=1000|1002|1003, --sgr, --shift, --ctrl, --alt.\n"
"\n"
"Keys the window keeps — the program running in it never sees these:\n"
"  Ctrl+Shift+T  a new tab               Ctrl+Shift+W  close this one\n"
"  Ctrl+Shift+Left/Right      the tab before or after; Ctrl+Shift+1..9 by\n"
"                             number, and clicking one in the bar\n"
"  Ctrl+Shift+C  copy the selection      Ctrl+Shift+V  paste the clipboard\n"
"  Shift+Insert  paste the primary selection, as middle-click does\n"
"  Shift+PgUp/PgDn/Home/End   the scrollback\n"
"  Ctrl+Shift+Up/Down         jump to the previous or next prompt\n"
"  drag to select; double-click a word, triple-click a line; shift-drag\n"
"  selects even while a program is reading the mouse\n"
"\n"
"With no FILE, or with '-', the stream is read from standard input.\n";

typedef struct {
	/* ⚠ ZERO AND NULL MEAN "NOT GIVEN", not "the default". The config file is
	 * read after the flags are parsed and fills in only what the command line
	 * left alone, so every one of these needs a value that cannot be typed. */
	uint16_t cols, rows;
	long     scrollback;   /* -1 unset */
	bool     styled, with_scrollback, stats;
	int      runs;
	const char *font;
	double   font_size;
	const char *out;
	bool     no_cursor;
	const char *probe;
	bool     no_deadline;
	int      view;
	int      jump;      /* prompts to jump back before dumping */
	const char *select; /* "c0,r0,c1,r1" — print that selection as text */
	/* The pointer, without a pointer: where it was clicked, where it was
	 * dragged to, and how much output arrived afterwards. The third one is
	 * what proves a selection is anchored to its TEXT rather than to two
	 * screen rows — see apply_pointer(). */
	const char *click;
	const char *drag;
	int         scroll_after;

	int      tabs;           /* --tabs=N: open N at startup */
	const char *resize;      /* --resize=COLSxROWS, applied after the stream */
	const char *config;      /* --config=FILE, or NULL for the usual place */
	bool        no_config;   /* ignore the file entirely — what tests use */
	const st_config_t *cfg;  /* what was read, for whatever paints */

	/* ⚠ WHAT THE COMMAND LINE GAVE, KEPT SEPARATELY, because `font` above
	 * stops being able to answer the question the moment the file is merged
	 * into it. The window re-reads the config while it runs, and "a flag beats
	 * the file" has to keep being true on the second read as well as the
	 * first — otherwise `syntty --font=X` loses its font to the next theme
	 * switch. NULL and 0 still mean "not given". */
	const char *flag_font;
	double      flag_size;
} opts_t;

/* Read a whole stream into memory. A benchmark has to hold its input: timing a
 * parser with a read() in the loop measures the kernel, and every run would
 * differ by however warm the page cache happened to be. */
static uint8_t *slurp(const char *path, size_t *out_len)
{
	FILE *f = (!path || !strcmp(path, "-")) ? stdin : fopen(path, "rb");
	if (!f)
		die("%s: cannot read", path ? path : "-");

	size_t cap = 1 << 16, len = 0;
	uint8_t *buf = xmalloc(cap);
	for (;;) {
		if (len == cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
		size_t n = fread(buf + len, 1, cap - len, f);
		if (n == 0)
			break;
		len += n;
	}
	if (f != stdin)
		fclose(f);
	*out_len = len;
	return buf;
}

/* ── selecting, with no pointer to select with ──────────────────────────────
 *
 * `--click=COL,ROW[,word|line]` and `--drag=COL,ROW` drive exactly the calls
 * win.c makes when somebody presses, drags and releases the left button. The
 * seat is what cannot be tested here; the selection is the part that can, and
 * this is how the suite reaches it.
 *
 * `--scroll-after=N` then feeds N lines of output from the bottom of the
 * screen, which is the case an anchored selection exists for: the text the
 * person highlighted moves up the window, and what they copy must still be
 * that text rather than whatever has since scrolled into those rows. */
/* ⚠ THE ONLY WAY A RESIZE CAN BE TESTED WITHOUT A COMPOSITOR — and until this
 * existed, nothing tested one at all. A window is resized by the compositor,
 * so every rule about what happens to the text and to the cursor lived on a
 * path the suite could not reach; a grow that pushed the content away from the
 * cursor survived three releases because of it. */
static void apply_resize(const opts_t *o, st_grid_t *g)
{
	if (!o->resize)
		return;
	int c = 0, r = 0;
	if (sscanf(o->resize, "%dx%d", &c, &r) != 2 || c < 1 || r < 1)
		die("--resize wants COLSxROWS");
	st_grid_resize(g, (uint16_t)c, (uint16_t)r);
}

static void apply_pointer(const opts_t *o, st_vt_t *vt, st_grid_t *g)
{
	if (!o->click)
		return;

	int c = 0, r = 0;
	char how[16] = "char";
	if (sscanf(o->click, "%d,%d,%15s", &c, &r, how) < 2)
		die("--click wants COL,ROW[,word|line]");
	int mode = !strcmp(how, "word") ? ST_SEL_WORD
	         : !strcmp(how, "line") ? ST_SEL_LINE : ST_SEL_CHAR;

	st_sel_start(g, c, r, mode);
	if (o->drag) {
		int dc = 0, dr = 0;
		if (sscanf(o->drag, "%d,%d", &dc, &dr) != 2)
			die("--drag wants COL,ROW");
		st_sel_extend(g, dc, dr);
	}

	/* From the LAST row, so each newline scrolls exactly one line rather than
	 * walking the cursor down first — the count has to mean something. */
	if (o->scroll_after > 0) {
		st_vt_feed(vt, (const uint8_t *)"\033[999;1H", 8);
		for (int i = 0; i < o->scroll_after; i++)
			st_vt_feed(vt, (const uint8_t *)"\n", 1);
	}
}

static void print_stats(const st_vt_t *vt, const st_grid_t *g)
{
	fprintf(stderr, "cursor        %u,%u\n", g->cx, g->cy);
	fprintf(stderr, "styles        %u interned\n", g->nstyles);
	fprintf(stderr, "scrollback    %u rows\n", g->count);
	fprintf(stderr, "memory        %zu bytes (%.1f KB)\n",
	        st_grid_bytes(g), st_grid_bytes(g) / 1024.0);
	fprintf(stderr, "unhandled     %llu CSI, %llu ESC\n",
	        (unsigned long long)vt->unhandled_csi,
	        (unsigned long long)vt->unhandled_esc);
	fprintf(stderr, "osc           %llu seen\n",
	        (unsigned long long)vt->osc_seen);
	if (vt->title[0])
		fprintf(stderr, "title         %s\n", vt->title);

	/* ── what the shell told us (OSC 133) ───────────────────────────────────
	 *
	 * Printed so the marks can be asserted without a window: which rows are
	 * prompts, which are output, and what each command did. */
	if (vt->ncmds) {
		fprintf(stderr, "commands      %u recorded\n", vt->ncmds);
		uint32_t first = vt->ncmds > ST_MAX_CMDS ? vt->ncmds - ST_MAX_CMDS : 0;
		for (uint32_t i = first; i < vt->ncmds; i++) {
			const st_cmd_t *c = &vt->cmds[i % ST_MAX_CMDS];
			fprintf(stderr, "  cmd %u       row %u, %.2f ms, status ",
			        i, c->row, (double)c->duration_ns / 1e6);
			/* ⚠ UNKNOWN IS NOT ZERO. A shell that emits a bare `D` has said
			 * it finished and nothing else; printing that as 0 is how a
			 * failing command comes back looking successful. */
			if (c->status < 0) fprintf(stderr, "unknown\n");
			else               fprintf(stderr, "%d\n", c->status);
		}
	}
	{
		int prompts = 0, outputs = 0;
		for (int y = 0; y < g->rows; y++) {
			if (g->screen[y].mark == ST_MARK_PROMPT) prompts++;
			if (g->screen[y].mark == ST_MARK_OUTPUT) outputs++;
		}
		if (prompts || outputs)
			fprintf(stderr, "marks         %d prompt, %d output (on screen)\n",
			        prompts, outputs);
	}

	fprintf(stderr, "kbd flags     %u%s\n", st_vt_kbd_flags(vt),
	        st_vt_kbd_flags(vt) ? "" : " (legacy encodings)");

	/* ⚠ THE NUMBER, NOT "on". The mode is stored as 1000, 1002 or 1003, and
	 * for a whole release the field it lived in was a uint8_t — so it held
	 * 232, 234 and 235 instead. All three are non-zero and all three are
	 * distinct, which is everything a test asking "was the mode understood?"
	 * checks. Printing the value is what makes that visible from outside. */
	if (g->mouse_mode)
		fprintf(stderr, "mouse         %u, %s coordinates\n", g->mouse_mode,
		        g->mouse_sgr ? "SGR" : "1984");

	/* ── OSC 52, the clipboard the child asked for ──────────────────────────
	 *
	 * Printed as a LENGTH and never as the text. A stats line is written to a
	 * terminal, and echoing whatever a program just copied — which is
	 * regularly a password — onto somebody's screen is not a diagnostic. */
	if (vt->clip_sets)
		fprintf(stderr, "clipboard     %llu set, %zu bytes waiting for the %s\n",
		        (unsigned long long)vt->clip_sets, vt->clip_len,
		        vt->clip_target == ST_CLIP_PRIMARY ? "primary" : "clipboard");
	/* ⚠ ALWAYS SAID OUT LOUD. A program asking to READ the clipboard is
	 * asking the terminal to type its contents back at it, and the refusal is
	 * the security decision this program makes on the person's behalf. */
	if (vt->clip_reads_refused)
		fprintf(stderr, "clipboard     %llu READ request(s) refused — a program "
		        "cannot be allowed to read what was copied\n",
		        (unsigned long long)vt->clip_reads_refused);
	if (vt->osc_dropped)
		fprintf(stderr, "osc           %llu dropped for running past %d bytes\n",
		        (unsigned long long)vt->osc_dropped, VT_OSC_MAX);

	/* What the parser owes the child. Printed ESCAPED, because the whole
	 * point of a reply is that it is a control sequence — printing it raw
	 * would have the enclosing terminal act on it rather than show it, which
	 * is how a test asserting on this output would appear to pass while the
	 * terminal running the test quietly changed mode. */
	char rep[128];
	size_t rn = st_vt_take_reply((st_vt_t *)vt, rep, sizeof rep);
	if (rn) {
		fprintf(stderr, "reply         ");
		for (size_t i = 0; i < rn; i++) {
			unsigned char c = (unsigned char)rep[i];
			if (c == 0x1b)                 fprintf(stderr, "ESC");
			else if (c >= 0x20 && c < 0x7f) fputc(c, stderr);
			else                            fprintf(stderr, "\\x%02x", c);
		}
		fputc('\n', stderr);
	}
}

static int cmd_dump(const opts_t *o, const char *path)
{
	size_t len = 0;
	uint8_t *buf = slurp(path, &len);

	st_grid_t g;
	st_vt_t vt;
	st_grid_init(&g, o->cols, o->rows, (uint32_t)o->scrollback);
	st_vt_init(&vt, &g);

	/* Fed in ONE call here, and in many small ones by the test suite, because
	 * the two must produce identical screens — see the split-feed assertions.
	 * A parser is only stream-safe if something proves it. */
	st_vt_feed(&vt, buf, len);
	apply_resize(o, &g);
	if (o->view)
		st_grid_view_scroll(&g, o->view);
	for (int j = 0; j < o->jump; j++) {
		long off = st_grid_find_prompt(&g, +1);
		if (off < 0)
			break;
		st_grid_view_scroll(&g, (int)(off - (long)g.view));
	}

	apply_pointer(o, &vt, &g);
	if (o->click) {
		/* What a release would put on the clipboard. NULL means there is no
		 * selection at all, which is not the same as one that copies nothing
		 * — a distinction the output has to keep or a test cannot tell a
		 * cleared selection from an empty line. */
		char *sel = st_sel_text(&g);
		puts(sel ? sel : "(no selection)");
		free(sel);
		st_vt_free(&vt);
		st_grid_free(&g);
		free(buf);
		return 0;
	}

	/* The selection is printed INSTEAD of the screen: what it is for is
	 * asserting exactly what would land on the clipboard, and a screen dump
	 * around it would have to be stripped off again by every caller. */
	if (o->select) {
		int c0 = 0, r0 = 0, c1 = 0, r1 = 0;
		if (sscanf(o->select, "%d,%d,%d,%d", &c0, &r0, &c1, &r1) != 4)
			die("--select wants C0,R0,C1,R1");
		char *sel = st_grid_selection_text(&g, c0, r0, c1, r1);
		fputs(sel, stdout);
		fputc('\n', stdout);
		free(sel);
		st_vt_free(&vt);
		st_grid_free(&g);
		free(buf);
		return 0;
	}

	if (o->with_scrollback)
		st_dump_scrollback(&g, stdout);
	if (o->styled)
		st_dump_styled(&g, stdout);
	else
		st_dump_text(&g, stdout);
	if (o->stats)
		print_stats(&vt, &g);

	st_vt_free(&vt);
	st_grid_free(&g);
	free(buf);
	return 0;
}

/* Feed the same stream in awkward slices. Not a debugging aid — it is the
 * assertion that the state machine survives a read() landing mid-escape, which
 * is a thing that happens constantly at 256 KB and never at 80 bytes. */
static int cmd_dump_split(const opts_t *o, const char *path, size_t chunk)
{
	size_t len = 0;
	uint8_t *buf = slurp(path, &len);

	st_grid_t g;
	st_vt_t vt;
	st_grid_init(&g, o->cols, o->rows, (uint32_t)o->scrollback);
	st_vt_init(&vt, &g);

	for (size_t off = 0; off < len; off += chunk) {
		size_t n = len - off < chunk ? len - off : chunk;
		st_vt_feed(&vt, buf + off, n);
	}

	if (o->with_scrollback)
		st_dump_scrollback(&g, stdout);
	if (o->styled)
		st_dump_styled(&g, stdout);
	else
		st_dump_text(&g, stdout);
	if (o->stats)
		print_stats(&vt, &g);

	st_vt_free(&vt);
	st_grid_free(&g);
	free(buf);
	return 0;
}

static int cmd_run(const opts_t *o, int argc, char **argv)
{
	if (argc < 1)
		die("run: need a command");

	st_grid_t g;
	st_vt_t vt;
	st_grid_init(&g, o->cols, o->rows, (uint32_t)o->scrollback);
	st_vt_init(&vt, &g);

	st_pty_t p;
	if (!st_pty_spawn(&p, argv, o->cols, o->rows))
		die("run: cannot allocate a pty");

	int rc = st_pty_pump(&p, &vt);

	if (o->with_scrollback)
		st_dump_scrollback(&g, stdout);
	if (o->styled)
		st_dump_styled(&g, stdout);
	else
		st_dump_text(&g, stdout);
	if (o->stats)
		print_stats(&vt, &g);

	st_vt_free(&vt);
	st_grid_free(&g);
	return rc;
}

static int cmd_bench(const opts_t *o, const char *path)
{
	size_t len = 0;
	uint8_t *buf = slurp(path, &len);
	if (len == 0)
		die("bench: nothing to parse");

	int runs = o->runs > 0 ? o->runs : 5;
	uint64_t best = UINT64_MAX, total = 0;

	for (int r = 0; r < runs; r++) {
		st_grid_t g;
		st_vt_t vt;
		st_grid_init(&g, o->cols, o->rows, (uint32_t)o->scrollback);
		st_vt_init(&vt, &g);

		/* The grid is built OUTSIDE the timed section and torn down outside
		 * it, because what is being measured is the parser and the writes it
		 * makes — not malloc. Both cost real time in a terminal, but they are
		 * different numbers and averaging them hides which one moved. */
		uint64_t t0 = now_ns();
		st_vt_feed(&vt, buf, len);
		uint64_t dt = now_ns() - t0;

		if (dt < best)
			best = dt;
		total += dt;
		st_vt_free(&vt);
		st_grid_free(&g);
	}
	free(buf);

	double mb = (double)len / (1024.0 * 1024.0);
	double best_ms = (double)best / 1e6;
	double mean_ms = (double)total / (double)runs / 1e6;

	/* BEST, not mean, is the headline. A parser's fastest run is the one with
	 * the least noise from everything else on the machine; the mean measures
	 * the machine as much as the code. Both are printed so neither can be
	 * quoted without the other. */
	printf("bytes    %zu (%.2f MB)\n", len, mb);
	printf("runs     %d\n", runs);
	printf("best     %.2f ms   %.1f MB/s\n", best_ms, mb / (best_ms / 1000.0));
	printf("mean     %.2f ms   %.1f MB/s\n", mean_ms, mb / (mean_ms / 1000.0));
	return 0;
}

/* Where the startup milliseconds went, and whether the cache did anything.
 *
 * This is stage 2's equivalent of `bench`: the design claims a terminal can
 * start in under 15 ms by not scanning font directories, and a claim about
 * startup that nothing prints is a claim nobody can check. Run it twice — the
 * first run populates the cache and says `fontconfig`, the second reads it and
 * says `cache`. If the second one still says fontconfig, the cache is broken
 * and every start is paying for it. */
static int cmd_font(const opts_t *o)
{
	char *err = NULL;
	uint64_t t0 = now_ns();
	st_font_t *f = st_font_open(o->font, o->font_size, &err);
	double total = (double)(now_ns() - t0) / 1e6;
	if (!f)
		die("font: %s", err ? err : "could not open");

	const st_font_stats_t *s = st_font_get_stats(f);

	printf("family       %s\n", o->font ? o->font : "monospace");
	printf("file         %s\n", s->path);
	printf("size         %.1f px\n", o->font_size);
	printf("cell         %dx%d px, baseline %d\n",
	       st_font_cell_w(f), st_font_cell_h(f), st_font_baseline(f));
	printf("atlas        %u ASCII glyphs, %zu bytes\n",
	       s->ascii_glyphs, s->atlas_bytes);
	printf("lookup       %.2f ms via %s\n", s->lookup_ms,
	       s->used_fontconfig ? "fontconfig" : "cache");
	printf("open         %.2f ms total\n", total);

	/* Proof the rasteriser produced something, not just that it returned.
	 * A glyph box of all zeroes is exactly what a broken FreeType path gives
	 * back, and it looks identical to a working one from the outside. */
	unsigned long ink = 0;
	for (uint32_t cp = 33; cp < 127; cp++) {
		const st_glyph_t *g = st_font_glyph(f, cp, 0);
		for (int i = 0; i < g->w * g->h; i++)
			if (g->bits[i])
				ink++;
	}
	printf("ink          %lu covered pixels across ASCII 33..126\n", ink);
	if (ink == 0)
		die("font: every glyph rasterised blank — the face loaded but drew nothing");

	st_font_close(f);
	free(err);
	return 0;
}

/* The colours from the file, onto the renderer. The rule that makes this
 * order-sensitive — the palette before anything that can name one of its
 * entries — lives in config.c now, because the window re-reads the file while
 * it is running and a second copy of that ordering would drift. */
static void apply_colors(const opts_t *o, st_render_t *r)
{
	st_config_apply_colors(o->cfg, r);
}

/* Parse a stream and paint it, with no compositor anywhere.
 *
 * The renderer's `dump`. Stage 1 could assert on text because its output was
 * text; the moment pixels are involved, "it returned without crashing" passes
 * on an all-black window. This writes a PPM the suite can measure and a person
 * can open, which keeps stage 2's output checkable in the same place stage 1's
 * was — on a machine with no seat and no display. */
static int cmd_render(const opts_t *o, const char *path)
{
	size_t len = 0;
	uint8_t *buf = slurp(path, &len);

	st_grid_t g;
	st_vt_t vt;
	st_grid_init(&g, o->cols, o->rows, (uint32_t)o->scrollback);
	st_vt_init(&vt, &g);
	st_vt_feed(&vt, buf, len);
	free(buf);
	if (o->view)
		st_grid_view_scroll(&g, o->view);
	/* So the highlight can be proved in PIXELS: --click here and --probe on a
	 * cell inside the selection says what colour it actually came out. */
	apply_pointer(o, &vt, &g);

	char *err = NULL;
	st_font_t *f = st_font_open(o->font, o->font_size, &err);
	if (!f)
		die("render: %s", err ? err : "no font");

	st_render_t *r = st_render_new(f);
	apply_colors(o, r);
	st_render_cursor(r, !o->no_cursor);
	st_render_set_gfx(r, vt.gfx);

	int w = st_render_width(r, &g), h = st_render_height(r, &g);
	uint32_t *px = xcalloc((size_t)w * h, sizeof *px);

	int runs = o->runs > 0 ? o->runs : 1;
	uint64_t best = UINT64_MAX;
	size_t drawn = 0;
	for (int i = 0; i < runs; i++) {
		uint64_t t0 = now_ns();
		drawn = st_render_grid(r, &g, px, w, w, h);
		uint64_t dt = now_ns() - t0;
		if (dt < best) best = dt;
	}

	if (o->out) {
		FILE *fp = fopen(o->out, "wb");
		if (!fp)
			die("render: cannot write %s", o->out);
		st_render_write_ppm(px, w, w, h, fp);
		fclose(fp);
	}

	/* WHAT IS ACTUALLY IN THE BUFFER, so the suite can assert on pixels
	 * without an image library and without a screen.
	 *
	 * `ink` is the share of pixels that are not the default background — a
	 * renderer that paints a correctly-sized rectangle of nothing scores zero
	 * here and passes every other check in this function. `colours` catches
	 * the opposite failure, where everything is drawn in one colour because
	 * the style lookup silently resolved to the default. */
	uint32_t bg0 = px[0];
	size_t   ink = 0;
	uint32_t seen[64];
	int      nseen = 0;
	for (size_t i = 0; i < (size_t)w * h; i++) {
		if (px[i] != bg0)
			ink++;
		bool have = false;
		for (int k = 0; k < nseen; k++)
			if (seen[k] == px[i]) { have = true; break; }
		if (!have && nseen < 64)
			seen[nseen++] = px[i];
	}

	fprintf(stderr, "size     %dx%d px (%ux%u cells of %dx%d)\n",
	        w, h, g.cols, g.rows, st_font_cell_w(f), st_font_cell_h(f));
	fprintf(stderr, "cells    %zu drawn\n", drawn);
	fprintf(stderr, "paint    %.2f ms  (best of %d, whole screen)\n",
	        (double)best / 1e6, runs);
	fprintf(stderr, "buffer   %zu bytes\n", (size_t)w * h * 4);
	fprintf(stderr, "ink      %.2f%% of pixels differ from the background\n",
	        100.0 * (double)ink / ((double)w * h));
	fprintf(stderr, "colours  %d distinct%s\n", nseen, nseen >= 64 ? "+" : "");

	/* The colour of one cell, named by grid coordinate. A test that wants to
	 * know whether `ESC[7m` really swapped the colours has to look at a
	 * specific cell; anything aggregate would pass on a screen that reversed
	 * the wrong one. The CORNER pixel is sampled because it is background for
	 * every glyph in the font — the ink never reaches it. */
	if (o->probe) {
		int pc = 0, pr = 0;
		if (sscanf(o->probe, "%d,%d", &pc, &pr) == 2
		    && pc >= 0 && pr >= 0
		    && pc < g.cols && pr < g.rows) {
			int x = pc * st_font_cell_w(f), y = pr * st_font_cell_h(f);
			printf("probe %d,%d %06X\n", pc, pr, px[(size_t)y * w + x] & 0xFFFFFF);
		} else {
			die("render: --probe wants col,row inside the grid");
		}
	}

	free(px);
	st_render_free(r);
	st_font_close(f);
	st_vt_free(&vt);
	st_grid_free(&g);
	free(err);
	return 0;
}

/* The terminal, with a window. Stage 2's actual deliverable.
 *
 * Everything it needs was already built and tested headlessly: the grid, the
 * parser, the pty, the font and the renderer. This assembles them, hands them
 * to win.c and prints what the start cost — which is the number the whole
 * project is aimed at. kitty spends 230 ms before it can run `true` and foot
 * spends 25 ms; `first frame` is the comparable figure here. */
static int cmd_win(const opts_t *o, int argc, char **argv)
{
	char *shell_argv[2];
	if (argc < 1) {
		/* $SHELL, and /bin/sh when there is none. Exec'd DIRECTLY with no
		 * shell in the middle — see the header of pty.c. */
		const char *sh = getenv("SHELL");
		shell_argv[0] = (char *)(sh && *sh ? sh : "/bin/sh");
		shell_argv[1] = NULL;
		argv = shell_argv;
	}

	char *err = NULL;
	st_font_t *f = st_font_open(o->font, o->font_size, &err);
	if (!f)
		die("%s", err ? err : "no font");

	st_render_t *r = st_render_new(f);
	apply_colors(o, r);

	/* ⚠ THE SESSIONS BELONG TO THE WINDOW NOW, not to this function. With tabs
	 * there is not one grid and one pty but a set of them that comes and goes
	 * while the window runs, and the thing that owns their lifetime has to be
	 * the thing that outlives them. What this hands over is a RECIPE: what a
	 * tab runs, and the shape it starts in. */
	st_tab_spec_t spec = {
		.argv       = argv,
		.cols       = o->cols,
		.rows       = o->rows,
		.scrollback = (uint32_t)o->scrollback,
		.tabs       = o->tabs,
	};

	/* What the window needs to re-read the file while it runs — see
	 * st_win_conf_t. `--no-config` turns the watch off rather than leaving it
	 * watching a file it was told to ignore: the test suite passes that flag
	 * precisely so a developer's own settings cannot reach an assertion, and a
	 * reload would let them in through the back door. */
	st_win_conf_t conf = {
		.cfg       = o->cfg,
		.watch     = !o->no_config,
		.flag_font = o->flag_font,
		.flag_size = o->flag_size,
		.font      = o->font,
		.size      = o->font_size,
	};

	st_win_stats_t ws = {0};
	int rc = st_win_run(&f, r, &spec, "syntty", !o->no_deadline, &conf, &ws);

	if (o->stats) {
		fprintf(stderr, "first frame   %.2f ms\n", ws.first_frame_ms);
		fprintf(stderr, "frames        %llu drawn, %llu skipped\n",
		        (unsigned long long)ws.frames,
		        (unsigned long long)ws.skipped);
		fprintf(stderr, "grid          %ux%u, %zu bytes\n",
		        ws.cols, ws.rows, ws.grid_bytes);
		if (ws.tabs_opened > 1)
			fprintf(stderr, "tabs          %llu opened\n",
			        (unsigned long long)ws.tabs_opened);
		if (ws.rows_possible)
			fprintf(stderr, "rows painted  %llu of %llu (%.1f%% — the rest were "
			        "already right)\n",
			        (unsigned long long)ws.rows_painted,
			        (unsigned long long)ws.rows_possible,
			        100.0 * (double)ws.rows_painted / (double)ws.rows_possible);

		/* ⚠ THE DROPPED COUNT IS PRINTED WHENEVER IT IS NOT ZERO. A program
		 * using the 1984 encoding cannot be told about a click past column
		 * 223, so on a wide window its mouse works on the left half and does
		 * nothing on the right — which looks like the program's bug and is
		 * ours to report. */
		if (ws.mouse_sent || ws.mouse_dropped)
			fprintf(stderr, "mouse         %llu events reported%s",
			        (unsigned long long)ws.mouse_sent,
			        ws.mouse_dropped ? "" : "\n");
		if (ws.mouse_dropped)
			fprintf(stderr, ", %llu dropped past column 223 "
			        "(the program never asked for ?1006)\n",
			        (unsigned long long)ws.mouse_dropped);

		/* ⚠ "not measured" is printed rather than zeros. A compositor with no
		 * wp_presentation, or one timestamping on a clock that is not ours,
		 * gives no latency at all — and "0.00 ms" is a spectacular claim to
		 * make by accident. */
		if (!ws.have_presentation) {
			fprintf(stderr, "latency       not measured "
			        "(no wp_presentation on this compositor, or a clock "
			        "that is not CLOCK_MONOTONIC)\n");
		} else {
			if (ws.discarded)
				fprintf(stderr, "discarded     %llu frames superseded "
				        "before being shown\n",
				        (unsigned long long)ws.discarded);
			if (ws.commit_n)
				fprintf(stderr, "commit->photon %.2f ms avg  "
				        "(%.2f min, %.2f max, n=%llu)\n",
				        ws.commit_avg, ws.commit_min, ws.commit_max,
				        (unsigned long long)ws.commit_n);
			else
				fprintf(stderr, "commit->photon no frame reached the screen\n");

			if (ws.input_n)
				fprintf(stderr, "input->photon  %.2f ms avg  "
				        "(%.2f min, %.2f max, n=%llu) "
				        "— includes the child's echo\n",
				        ws.input_avg, ws.input_min, ws.input_max,
				        (unsigned long long)ws.input_n);
			else
				fprintf(stderr, "input->photon  nothing was typed\n");
		}

		/* The deadline line is printed whether the mode is on or off, because
		 * the two runs of an A/B comparison have to be told apart afterwards
		 * and a flag that leaves no trace in the output is one somebody will
		 * forget they passed. */
		if (!ws.deadline_on) {
			fprintf(stderr, "deadline      off — painting on the frame "
			        "callback\n");
		} else if (ws.refresh_ms > 0) {
			fprintf(stderr, "deadline      on, %.2f ms refresh, aiming "
			        "%.2f ms early (%llu on time, %llu already past)\n",
			        ws.refresh_ms, ws.margin_ms,
			        (unsigned long long)ws.deadline_used,
			        (unsigned long long)ws.deadline_late);
		} else {
			fprintf(stderr, "deadline      on, but the compositor reported no "
			        "constant refresh rate — painting immediately\n");
		}
	}

	st_render_free(r);
	st_font_close(f);
	free(err);
	return rc;
}

/* ── does damage tracking draw the same screen? ─────────────────────────────
 *
 * THE test for stage 2's last unfinished piece, and the reason it stayed
 * unfinished: a damage rectangle that is subtly too small leaves stale pixels,
 * which looks like memory corruption and is invisible to every test that only
 * checks the final screen — because the final screen, drawn fully, is right.
 *
 * So this draws the same stream twice into two buffers:
 *
 *   FULL         fed in one go, painted entirely, once.
 *   INCREMENTAL  fed in chunks, and after each chunk only the rows the grid
 *                reported as dirty are repainted.
 *
 * Then it compares the two buffers BYTE FOR BYTE. Any row the grid failed to
 * mark shows up as a difference, and the failure names the pixel. A missed
 * mutation site in grid.c cannot survive this.
 *
 * ⚠ The chunk size matters and small is harsher: a mutation whose damage is
 * only noticed because a later chunk happened to redraw the same row passes at
 * 64 KB and fails at 7 bytes. */
static int cmd_damage_check(const opts_t *o, const char *path, size_t chunk)
{
	size_t len = 0;
	uint8_t *buf = slurp(path, &len);
	if (len == 0)
		die("damage-check: nothing to parse");
	if (chunk == 0)
		chunk = 64;

	char *err = NULL;
	st_font_t *f = st_font_open(o->font, o->font_size, &err);
	if (!f)
		die("damage-check: %s", err ? err : "no font");
	st_render_t *r = st_render_new(f);
	st_render_cursor(r, !o->no_cursor);

	int cw = st_font_cell_w(f), ch = st_font_cell_h(f);
	int w = cw * o->cols, h = ch * o->rows;
	size_t npx = (size_t)w * h;

	uint32_t *full = xcalloc(npx, sizeof *full);
	uint32_t *incr = xcalloc(npx, sizeof *incr);
	uint8_t  *rows = xcalloc(o->rows, 1);

	/* INCREMENTAL. The first paint is a full one, exactly as the window does
	 * on its first configure — there is nothing on screen to preserve. */
	st_grid_t gi;
	st_vt_t vti;
	st_grid_init(&gi, o->cols, o->rows, (uint32_t)o->scrollback);
	st_vt_init(&vti, &gi);
	st_render_grid(r, &gi, incr, w, w, h);
	st_grid_clear_dirty(&gi);

	uint16_t last_cx = gi.cx, last_cy = gi.cy;
	size_t   repaints = 0, chunks = 0;
	uint64_t incr_ns = 0, full_ns = 0;

	for (size_t off = 0; off < len; off += chunk) {
		size_t n = len - off < chunk ? len - off : chunk;
		st_vt_feed(&vti, buf + off, n);
		chunks++;

		memset(rows, 0, o->rows);
		for (int y = 0; y < o->rows; y++)
			if (st_grid_row_dirty(&gi, y))
				rows[y] = 1;

		/* THE CURSOR IS DAMAGE TOO, and the grid does not know it: moving the
		 * cursor changes no cell, but it changes two rows on screen — the one
		 * it left and the one it arrived at. Left out, a cursor smears a trail
		 * of itself across the screen, which is the most visible possible
		 * version of this bug and the easiest to forget. */
		if (last_cy < o->rows) rows[last_cy] = 1;
		if (gi.cy   < o->rows) rows[gi.cy]   = 1;
		last_cx = gi.cx; last_cy = gi.cy;
		(void)last_cx;

		for (int y = 0; y < o->rows; y++)
			repaints += rows[y] ? 1 : 0;
		uint64_t t0 = now_ns();
		st_render_rows(r, &gi, rows, incr, w, w, h);
		incr_ns += now_ns() - t0;
		st_grid_clear_dirty(&gi);
	}

	/* FULL, from a clean grid fed in one go. */
	st_grid_t gf;
	st_vt_t vtf;
	st_grid_init(&gf, o->cols, o->rows, (uint32_t)o->scrollback);
	st_vt_init(&vtf, &gf);
	st_vt_feed(&vtf, buf, len);
	uint64_t tf = now_ns();
	st_render_grid(r, &gf, full, w, w, h);
	full_ns = now_ns() - tf;

	/* Compare. The first difference is reported as a CELL, because "pixel
	 * 418,233" is not something a person can act on and "row 11, column 52"
	 * points straight at the escape sequence that did it. */
	int bad_x = -1, bad_y = -1;
	for (int y = 0; y < h && bad_y < 0; y++)
		for (int x = 0; x < w; x++)
			if (full[(size_t)y * w + x] != incr[(size_t)y * w + x]) {
				bad_x = x; bad_y = y;
				break;
			}

	size_t possible = (size_t)o->rows * chunks;
	printf("chunks     %zu of %zu bytes\n", chunks, chunk);
	printf("repaints   %zu rows of a possible %zu (%.1f%%)\n",
	       repaints, possible,
	       possible ? 100.0 * (double)repaints / (double)possible : 0.0);
	/* What one frame costs each way. The incremental total is divided by the
	 * number of frames it took, so the two numbers are comparable: one full
	 * repaint against the AVERAGE damaged repaint. */
	printf("per frame  %.3f ms damaged, %.3f ms full  (%.1fx)\n",
	       chunks ? (double)incr_ns / (double)chunks / 1e6 : 0.0,
	       (double)full_ns / 1e6,
	       (chunks && incr_ns) ?
	         ((double)full_ns * (double)chunks) / (double)incr_ns : 0.0);

	int rc = 0;
	if (bad_y >= 0) {
		printf("MISMATCH   pixel %d,%d = cell %d,%d "
		       "(full %06X, incremental %06X)\n",
		       bad_x, bad_y, bad_x / cw, bad_y / ch,
		       full[(size_t)bad_y * w + bad_x] & 0xFFFFFF,
		       incr[(size_t)bad_y * w + bad_x] & 0xFFFFFF);
		printf("           a row changed and was not marked dirty — "
		       "see the damage note in include/syntty.h\n");
		rc = 1;
	} else {
		printf("identical  damage tracking drew the same %d x %d buffer\n", w, h);
	}

	free(rows); free(full); free(incr); free(buf); free(err);
	st_vt_free(&vti); st_vt_free(&vtf);
	st_grid_free(&gi); st_grid_free(&gf);
	st_render_free(r); st_font_close(f);
	return rc;
}

/* ── the pointer, with no pointer ───────────────────────────────────────────
 *
 * Mouse support is one testable half and one untestable half. The untestable
 * half is the seat: a compositor, a person, a real pointer, and input is never
 * synthesised on a live session — so it is kept as thin as it can be in win.c.
 * The testable half is every RULE about what should be sent, and this is where
 * the suite gets at it:
 *
 *     syntty mouse --mode=1002 --sgr press:left@10,5 move@11,5 release:left@11,5
 *
 * Each event prints as the bytes it would put on the child's input, ESCAPED —
 * printing them raw would have the terminal running the test act on them,
 * which is how an assertion "passes" while the enclosing terminal quietly
 * enters mouse mode. An event a mode does not report prints WHY, because
 * "nothing was sent" and "nothing should have been sent" are the same output
 * and very different facts. */
static int mouse_button(const char *s, int *b)
{
	if (!strcmp(s, "left"))        *b = ST_BTN_LEFT;
	else if (!strcmp(s, "middle")) *b = ST_BTN_MIDDLE;
	else if (!strcmp(s, "right"))  *b = ST_BTN_RIGHT;
	else if (!strcmp(s, "none"))   *b = ST_BTN_NONE;
	else if (!strcmp(s, "up"))     *b = ST_BTN_WHEEL_UP;
	else if (!strcmp(s, "down"))   *b = ST_BTN_WHEEL_DOWN;
	else if (s[0] >= '0' && s[0] <= '9') *b = atoi(s);
	else return 0;
	return 1;
}

static int cmd_mouse(int argc, char **argv)
{
	uint16_t mode = 1000;
	bool     sgr  = false;
	unsigned mods = 0;
	int      n_events = 0;

	for (int i = 0; i < argc; i++) {
		const char *a = argv[i];

		if (!strncmp(a, "--mode=", 7))    { mode = (uint16_t)atoi(a + 7); continue; }
		if (!strcmp(a, "--sgr"))          { sgr = true;  continue; }
		if (!strcmp(a, "--shift"))        { mods |= ST_MOUSE_SHIFT; continue; }
		if (!strcmp(a, "--ctrl"))         { mods |= ST_MOUSE_CTRL;  continue; }
		if (!strcmp(a, "--alt"))          { mods |= ST_MOUSE_ALT;   continue; }
		if (a[0] == '-')
			die("mouse: unknown option '%s'", a);

		/* EVENT[:BUTTON]@COL,ROW */
		char verb[16] = {0}, btn[16] = "left";
		int  col = 0, row = 0;
		const char *at = strchr(a, '@');
		if (!at || sscanf(at + 1, "%d,%d", &col, &row) != 2)
			die("mouse: '%s' is not EVENT[:BUTTON]@COL,ROW", a);

		size_t head = (size_t)(at - a);
		const char *colon = memchr(a, ':', head);
		size_t vlen = colon ? (size_t)(colon - a) : head;
		if (vlen >= sizeof verb)
			die("mouse: '%s' is not an event", a);
		memcpy(verb, a, vlen);
		if (colon) {
			size_t blen = head - vlen - 1;
			if (blen >= sizeof btn)
				die("mouse: '%s' is not a button", a);
			memcpy(btn, colon + 1, blen);
			btn[blen] = '\0';
		}

		int event, button;
		if (!strcmp(verb, "press"))        event = ST_MOUSE_PRESS;
		else if (!strcmp(verb, "release")) event = ST_MOUSE_RELEASE;
		else if (!strcmp(verb, "move"))    event = ST_MOUSE_MOTION;
		else if (!strcmp(verb, "wheel"))   event = ST_MOUSE_PRESS;
		else die("mouse: '%s' is not press, release, move or wheel", verb);

		if (!strcmp(verb, "move") && !colon)
			strcpy(btn, "none");
		if (!mouse_button(btn, &button))
			die("mouse: '%s' is not a button", btn);

		char out[32];
		const char *why = NULL;
		size_t len = st_mouse_encode(mode, sgr, event, button, mods, col, row,
		                             out, sizeof out, &why);
		printf("%-24s ", a);
		if (!len) {
			printf("(nothing — %s)\n", why ? why : "no reason given");
		} else {
			for (size_t k = 0; k < len; k++) {
				unsigned char c = (unsigned char)out[k];
				if (c == 0x1b)                  fputs("ESC", stdout);
				else if (c >= 0x20 && c < 0x7f) fputc(c, stdout);
				else                            printf("\\x%02x", c);
			}
			printf("   (%zu bytes)\n", len);
		}
		n_events++;
	}

	if (!n_events) {
		fprintf(stderr,
		    "mouse: give it events — press:left@10,5 move@11,5 "
		    "release:left@11,5\n"
		    "       options: --mode=1000|1002|1003 --sgr --shift --ctrl --alt\n");
		return 2;
	}
	return 0;
}

/* ── what a paste becomes, with no clipboard ────────────────────────────────
 *
 * The other half that can be tested. Getting text out of another program is
 * Wayland plumbing and a person pressing keys; what happens to the bytes
 * afterwards is a pure function, and it is the half where a mistake runs
 * somebody's command for them.
 *
 *     syntty paste --bracketed "$(printf 'ls\nrm -rf /')"
 *
 * Printed ESCAPED, for the same reason every other control sequence in this
 * program is: printing it raw would have the terminal running the test act on
 * it. */
static int cmd_paste(int argc, char **argv)
{
	bool bracketed = false;
	const char *text = NULL;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--bracketed")) { bracketed = true; continue; }
		if (argv[i][0] == '-' && argv[i][1])
			die("paste: unknown option '%s'", argv[i]);
		if (text)
			die("paste: one text at a time");
		text = argv[i];
	}
	if (!text) {
		fprintf(stderr, "paste: give it text — syntty paste [--bracketed] TEXT\n");
		return 2;
	}

	size_t n = 0;
	char *out = st_paste_encode(text, strlen(text), bracketed, &n);
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)out[i];
		if (c == 0x1b)                  fputs("ESC", stdout);
		else if (c == '\r')             fputs("CR", stdout);
		else if (c == '\t')             fputs("TAB", stdout);
		else if (c >= 0x20 && c < 0x7f) fputc(c, stdout);
		else                            printf("\\x%02x", c);
	}
	printf("\n");
	free(out);
	return 0;
}

/* ── the config, and where it came from ─────────────────────────────────────
 *
 * `syntty config` prints the file it would read, whether it found one, every
 * setting AS RESOLVED, and anything it could not understand. All four matter:
 * "it did not work" about a config file is nearly always one of "you wrote it
 * somewhere else", "you spelled the key differently" or "the flag you are
 * also passing wins".
 *
 * `syntty config --example` writes a commented file to stdout. ⚠ THIS IS HOW
 * ANYBODY LEARNS THE FILE EXISTS — the compositor this ships beside had a
 * config nothing installed and nothing documented, and it may as well not have
 * had one. */
static int cmd_config(const opts_t *o, int argc, char **argv)
{
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--example")) {
			st_config_example(stdout);
			return 0;
		}
		die("config: unknown option '%s'", argv[i]);
	}

	st_config_t c;
	st_config_defaults(&c);
	st_config_load(&c, o->config);

	printf("path         %s\n", c.path);
	printf("status       %s\n", c.found ? "read" : "no file (not an error)");

	/* ⚠ EVERY FILE, NOT JUST THE ONE ABOVE. With `include` the answer to "why
	 * is my background not what I wrote" is usually "something else set it
	 * afterwards", and that something else is a file the person is not looking
	 * at — the desktop regenerates one on every theme switch. Naming them here
	 * is the difference between reading the answer and hunting for it.
	 * The main file is already printed as `path`, so this starts at the
	 * second and stays silent for the ordinary single-file config. */
	for (int i = 1; i < c.nfiles; i++)
		printf("included     %s\n", c.files[i]);

	printf("font         %s\n", c.font ? c.font : "monospace (default)");
	if (c.font_size > 0) printf("font_size    %.1f\n", c.font_size);
	else                 printf("font_size    14.0 (default)\n");
	if (c.cols > 0)      printf("columns      %d\n", c.cols);
	else                 printf("columns      80 (default)\n");
	if (c.rows > 0)      printf("rows         %d\n", c.rows);
	else                 printf("rows         24 (default)\n");
	if (c.scrollback >= 0) printf("scrollback   %ld\n", c.scrollback);
	else                   printf("scrollback   1000 (default)\n");
	if (c.scroll_lines > 0) printf("scroll_lines %d\n", c.scroll_lines);
	else                    printf("scroll_lines 3 (default)\n");
	if (c.deadline >= 0)  printf("deadline     %s\n", c.deadline ? "on" : "off");
	else                  printf("deadline     on (default)\n");

	/* Colours print as they were WRITTEN — a palette name stays a palette name
	 * here, because the renderer is what resolves it and this command has no
	 * renderer. */
	static const char *const cnames[4] =
		{ "foreground", "background", "cursor", "cursor_text" };
	const uint32_t cvals[4] = { c.fg, c.bg, c.cursor, c.cursor_text };
	for (int i = 0; i < 4; i++) {
		if (cvals[i] == ST_CFG_UNSET)
			continue;
		if ((cvals[i] & 0xFF000000u) == ST_COL_INDEXED)
			printf("%-12s palette %u\n", cnames[i], cvals[i] & 0xFF);
		else
			printf("%-12s #%06X\n", cnames[i], cvals[i] & 0xFFFFFFu);
	}
	for (int i = 0; i < 16; i++)
		if (c.palette[i] != ST_CFG_UNSET)
			printf("color%-7d #%06X\n", i, c.palette[i]);

	/* ⚠ COUNTED AND NAMED. A config that half-worked and said nothing is the
	 * failure this whole subcommand exists to prevent. */
	if (c.errors)
		printf("errors       %d, first at %s\n", c.errors, c.first_error);
	else if (c.found)
		printf("errors       none\n");

	st_config_free(&c);
	return c.errors ? 1 : 0;
}

static int cmd_about(void)
{
	printf("syntty %s — the SynapseOS terminal\n\n", SYNTTY_VERSION);
	printf("  built        pty, parser, grid, glyphs, a window, deadline\n");
	printf("               rendering, damage tracking, the keyboard and\n");
	printf("               graphics protocols, OSC 133 marks, the alternate\n");
	printf("               screen, the pointer, the clipboard, a config file\n");
	printf("               (see `syntty config --example`) and tabs.\n");
	printf("  not yet      splits, remote control, OSC 8 links, notifications\n");
	printf("  cell         %zu bytes\n", sizeof(st_cell_t));
	printf("  style        %zu bytes, interned\n", sizeof(st_style_t));
	printf("  renderer     CPU, into wl_shm — nothing here links GL, and that\n");
	printf("               is the decision worth 188 MB of kitty's 264\n");
	printf("\n");
	printf("The baseline this is measured against, taken on the machine it was\n");
	printf("written on with hyperfine inside a headless cage:\n\n");
	printf("               startup     fresh RSS   2.6 MB parsed\n");
	printf("  kitty        230.3 ms      264 MB      118.6 ms\n");
	printf("  foot          24.9 ms       21 MB      117.9 ms\n");
	return 0;
}

int main(int argc, char **argv)
{
	opts_t o = {
		.cols = 0, .rows = 0, .scrollback = -1,
		.styled = false, .with_scrollback = false, .stats = false, .runs = 5,
		.font = NULL, .font_size = 0.0, .out = NULL, .no_cursor = false,
		.probe = NULL, .no_deadline = false,
		.view = 0, .jump = 0, .select = NULL,
		.click = NULL, .drag = NULL, .scroll_after = 0,
		.config = NULL, .no_config = false
	};
	st_config_t cfg;
	st_config_defaults(&cfg);
	size_t split = 0;

	/* Options are accepted on either side of the subcommand, because that is
	 * what people type. The first cut only read them BEFORE it, so
	 * `syntty dump file --stats` silently ignored --stats and printed nothing
	 * — and two tests "passed" against that empty output by grepping for
	 * something they never found. An option that is quietly dropped is worse
	 * than one that is rejected.
	 *
	 * `run` is the exception and stops the scan: everything after it belongs
	 * to the child, and a terminal that eats its command's arguments because
	 * they start with a dash is not a terminal. */
	const char *cmd = NULL;
	const char *file = NULL;
	int child_at = 0;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (cmd && (!strcmp(cmd, "run") || !strcmp(cmd, "win")
		            || !strcmp(cmd, "mouse") || !strcmp(cmd, "paste")
		            || !strcmp(cmd, "config"))) {
			/* `--` and `-e` both mean "the rest is the child". Accepting
			 * -e here as well as below is not redundancy: `syntty win -e
			 * htop` is what somebody writes who knows both conventions,
			 * and without this the child would be argv "-e htop" and the
			 * exec would fail on a program named -e. */
			child_at = (!strcmp(a, "--") || !strcmp(a, "-e")) ? i + 1 : i;
			break;
		}

		/* ── `-e CMD…`, the convention everything else already speaks ────
		 *
		 * xterm, foot, kitty, alacritty, konsole and gnome-terminal all
		 * take it; KDE's KTerminalLauncherJob, xdg-terminal-exec, `xdg-open`
		 * on a Terminal=true .desktop, and every script anybody has ever
		 * written that runs a command in a terminal all emit it. A terminal
		 * that dies on `-e` with "unknown option" cannot be the system's
		 * default terminal, whatever the config says — which is what this
		 * one did until it became the default.
		 *
		 * It implies the window, so `syntty -e htop` needs no subcommand,
		 * and everything after it belongs to the child untouched. */
		if (!strcmp(a, "-e") || !strcmp(a, "--command")) {
			if (!cmd) cmd = "win";
			child_at = i + 1;
			break;
		}
		if (!strcmp(a, "--")) {
			if (i + 1 < argc && !cmd) cmd = argv[++i];
			continue;
		}
		if (!strncmp(a, "--cols=", 7))             o.cols = (uint16_t)atoi(a + 7);
		else if (!strncmp(a, "--rows=", 7))        o.rows = (uint16_t)atoi(a + 7);
		else if (!strncmp(a, "--scrollback=", 13)) o.scrollback = atol(a + 13);
		else if (!strncmp(a, "--runs=", 7))        o.runs = atoi(a + 7);
		else if (!strncmp(a, "--split=", 8))       split = (size_t)atoi(a + 8);
		else if (!strncmp(a, "--font=", 7))        o.font = a + 7;
		else if (!strncmp(a, "--font-size=", 12))  o.font_size = atof(a + 12);
		else if (!strncmp(a, "--out=", 6))         o.out = a + 6;
		else if (!strcmp(a, "--no-cursor"))        o.no_cursor = true;
		else if (!strncmp(a, "--probe=", 8))       o.probe = a + 8;
		else if (!strcmp(a, "--no-deadline"))      o.no_deadline = true;
		else if (!strncmp(a, "--view=", 7))        o.view = atoi(a + 7);
		else if (!strncmp(a, "--jump=", 7))        o.jump = atoi(a + 7);
		else if (!strncmp(a, "--select=", 9))      o.select = a + 9;
		else if (!strncmp(a, "--tabs=", 7))        o.tabs = atoi(a + 7);
		else if (!strncmp(a, "--resize=", 9))      o.resize = a + 9;
		else if (!strncmp(a, "--config=", 9))      o.config = a + 9;
		else if (!strcmp(a, "--no-config"))        o.no_config = true;
		else if (!strncmp(a, "--click=", 8))       o.click = a + 8;
		else if (!strncmp(a, "--drag=", 7))        o.drag = a + 7;
		else if (!strncmp(a, "--scroll-after=", 15)) o.scroll_after = atoi(a + 15);
		else if (!strcmp(a, "--styled"))           o.styled = true;
		else if (!strcmp(a, "--scrollback-too"))   o.with_scrollback = true;
		else if (!strcmp(a, "--stats"))            o.stats = true;
		else if (!strcmp(a, "--version"))          { printf("syntty %s\n", SYNTTY_VERSION); return 0; }
		else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { fputs(usage_text, stdout); return 0; }
		else if (a[0] == '-' && a[1])              die("unknown option '%s'", a);
		else if (!cmd)                             cmd = a;
		else if (!file)                            file = a;
		else die("%s: one input at a time (got '%s' as well)", cmd, a);
	}

	/* No subcommand at all means the window, because that is what a terminal
	 * is for. Every other entry point here exists to test and measure the
	 * pieces it is built from. */
	if (!cmd)
		cmd = "win";

	/* ── the file, under the flags ──────────────────────────────────────────
	 *
	 * Read AFTER the command line and used only where the command line said
	 * nothing, so a flag always wins. `--no-config` skips it, which is what the
	 * test suite passes: a developer's own font or colours must not change what
	 * an assertion sees.
	 *
	 * ⚠ ERRORS ARE PRINTED AND THE TERMINAL STILL STARTS. See config.c: a
	 * terminal that refuses to open over a typo cannot be used to fix it. */
	o.cfg = &cfg;
	/* Recorded BEFORE the merge below overwrites them — see opts_t. After it,
	 * `o.font` cannot tell a flag from a config line. */
	o.flag_font = o.font;
	o.flag_size = o.font_size;
	if (!o.no_config && strcmp(cmd, "config") != 0) {
		st_config_load(&cfg, o.config);
		if (cfg.errors)
			fprintf(stderr, "syntty: %s: %d problem%s, first at %s\n",
			        cfg.path, cfg.errors, cfg.errors == 1 ? "" : "s",
			        cfg.first_error);
		if (!o.font && cfg.font)        o.font = cfg.font;
		if (o.font_size <= 0)           o.font_size = cfg.font_size;
		if (!o.cols && cfg.cols > 0)    o.cols = (uint16_t)cfg.cols;
		if (!o.rows && cfg.rows > 0)    o.rows = (uint16_t)cfg.rows;
		if (o.scrollback < 0 && cfg.scrollback >= 0)
			o.scrollback = cfg.scrollback;
		if (!o.no_deadline && cfg.deadline == 0)
			o.no_deadline = true;
	}

	if (o.cols < 1)         o.cols = 80;
	if (o.rows < 1)         o.rows = 24;
	if (o.scrollback < 0)   o.scrollback = 1000;
	if (o.font_size <= 0)   o.font_size = 14.0;

	if (!strcmp(cmd, "dump"))
		return split ? cmd_dump_split(&o, file, split) : cmd_dump(&o, file);
	if (!strcmp(cmd, "bench"))
		return cmd_bench(&o, file);
	if (!strcmp(cmd, "run"))
		return child_at ? cmd_run(&o, argc - child_at, argv + child_at)
		                : (die("run: need a command"), 1);
	if (!strcmp(cmd, "font"))
		return cmd_font(&o);
	if (!strcmp(cmd, "render"))
		return cmd_render(&o, file);
	if (!strcmp(cmd, "damage-check"))
		return cmd_damage_check(&o, file, split);
	if (!strcmp(cmd, "win"))
		return child_at ? cmd_win(&o, argc - child_at, argv + child_at)
		                : cmd_win(&o, 0, NULL);
	if (!strcmp(cmd, "mouse"))
		return child_at ? cmd_mouse(argc - child_at, argv + child_at)
		                : cmd_mouse(0, NULL);
	if (!strcmp(cmd, "paste"))
		return child_at ? cmd_paste(argc - child_at, argv + child_at)
		                : cmd_paste(0, NULL);
	if (!strcmp(cmd, "config"))
		return child_at ? cmd_config(&o, argc - child_at, argv + child_at)
		                : cmd_config(&o, 0, NULL);
	if (!strcmp(cmd, "about"))
		return cmd_about();

	/* Name the -e form in the message. `kitty synsh` and `foot synsh` both run
	 * the program, so a bare word here is what somebody who knows those types,
	 * and "unknown command" alone reads as "syntty cannot run it" rather than
	 * "say -e first". Not accepted silently instead: a bare word that is really
	 * a mistyped subcommand would then start a shell called `dumpp` and look
	 * like the terminal ignoring the argument. */
	die("unknown command '%s' (try --help; to RUN it: syntty -e %s)", cmd, cmd);
}
