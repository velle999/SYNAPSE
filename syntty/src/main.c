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
"  syntty win [--] [CMD...]  a WINDOW, running CMD or your shell\n"
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
"\n"
"With no FILE, or with '-', the stream is read from standard input.\n";

typedef struct {
	uint16_t cols, rows;
	uint32_t scrollback;
	bool     styled, with_scrollback, stats;
	int      runs;
	const char *font;
	double   font_size;
	const char *out;
	bool     no_cursor;
	const char *probe;
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
}

static int cmd_dump(const opts_t *o, const char *path)
{
	size_t len = 0;
	uint8_t *buf = slurp(path, &len);

	st_grid_t g;
	st_vt_t vt;
	st_grid_init(&g, o->cols, o->rows, o->scrollback);
	st_vt_init(&vt, &g);

	/* Fed in ONE call here, and in many small ones by the test suite, because
	 * the two must produce identical screens — see the split-feed assertions.
	 * A parser is only stream-safe if something proves it. */
	st_vt_feed(&vt, buf, len);

	if (o->with_scrollback)
		st_dump_scrollback(&g, stdout);
	if (o->styled)
		st_dump_styled(&g, stdout);
	else
		st_dump_text(&g, stdout);
	if (o->stats)
		print_stats(&vt, &g);

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
	st_grid_init(&g, o->cols, o->rows, o->scrollback);
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
	st_grid_init(&g, o->cols, o->rows, o->scrollback);
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
		st_grid_init(&g, o->cols, o->rows, o->scrollback);
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
	st_grid_init(&g, o->cols, o->rows, o->scrollback);
	st_vt_init(&vt, &g);
	st_vt_feed(&vt, buf, len);
	free(buf);

	char *err = NULL;
	st_font_t *f = st_font_open(o->font, o->font_size, &err);
	if (!f)
		die("render: %s", err ? err : "no font");

	st_render_t *r = st_render_new(f);
	st_render_cursor(r, !o->no_cursor);

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

	st_grid_t g;
	st_vt_t vt;
	st_grid_init(&g, o->cols, o->rows, o->scrollback);
	st_vt_init(&vt, &g);

	st_render_t *r = st_render_new(f);

	st_pty_t p;
	if (!st_pty_spawn(&p, argv, o->cols, o->rows))
		die("cannot allocate a pty");
	/* The window's loop polls two descriptors and drains this one to EAGAIN;
	 * a blocking read here would mean not answering the compositor. */
	st_pty_set_nonblocking(&p);

	st_win_stats_t ws = {0};
	int rc = st_win_run(&g, &vt, &p, f, r, vt.title[0] ? vt.title : "syntty", &ws);

	if (o->stats) {
		fprintf(stderr, "first frame   %.2f ms\n", ws.first_frame_ms);
		fprintf(stderr, "frames        %llu drawn, %llu skipped\n",
		        (unsigned long long)ws.frames,
		        (unsigned long long)ws.skipped);
		fprintf(stderr, "grid          %zu bytes\n", st_grid_bytes(&g));
	}

	st_render_free(r);
	st_font_close(f);
	st_grid_free(&g);
	free(err);
	return rc;
}

static int cmd_about(void)
{
	printf("syntty %s — the SynapseOS terminal\n\n", SYNTTY_VERSION);
	printf("  stage        2 of 5: pty, parser, grid, glyphs and a window.\n");
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
		.cols = 80, .rows = 24, .scrollback = 1000,
		.styled = false, .with_scrollback = false, .stats = false, .runs = 5,
		.font = NULL, .font_size = 14.0, .out = NULL, .no_cursor = false,
		.probe = NULL
	};
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

		if (cmd && (!strcmp(cmd, "run") || !strcmp(cmd, "win"))) {
			child_at = (!strcmp(a, "--")) ? i + 1 : i;
			break;
		}
		if (!strcmp(a, "--")) {
			if (i + 1 < argc && !cmd) cmd = argv[++i];
			continue;
		}
		if (!strncmp(a, "--cols=", 7))             o.cols = (uint16_t)atoi(a + 7);
		else if (!strncmp(a, "--rows=", 7))        o.rows = (uint16_t)atoi(a + 7);
		else if (!strncmp(a, "--scrollback=", 13)) o.scrollback = (uint32_t)atoi(a + 13);
		else if (!strncmp(a, "--runs=", 7))        o.runs = atoi(a + 7);
		else if (!strncmp(a, "--split=", 8))       split = (size_t)atoi(a + 8);
		else if (!strncmp(a, "--font=", 7))        o.font = a + 7;
		else if (!strncmp(a, "--font-size=", 12))  o.font_size = atof(a + 12);
		else if (!strncmp(a, "--out=", 6))         o.out = a + 6;
		else if (!strcmp(a, "--no-cursor"))        o.no_cursor = true;
		else if (!strncmp(a, "--probe=", 8))       o.probe = a + 8;
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
	if (o.cols < 1)  o.cols = 80;
	if (o.rows < 1)  o.rows = 24;

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
	if (!strcmp(cmd, "win"))
		return child_at ? cmd_win(&o, argc - child_at, argv + child_at)
		                : cmd_win(&o, 0, NULL);
	if (!strcmp(cmd, "about"))
		return cmd_about();

	die("unknown command '%s' (try --help)", cmd);
}
