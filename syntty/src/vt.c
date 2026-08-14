/* vt.c — the escape-sequence parser.
 *
 * ── It is a state machine over bytes, and it survives any split ────────────
 *
 * st_vt_feed can be called with any slice of the stream. A read() can end in
 * the middle of `ESC[3` or halfway through a three-byte UTF-8 sequence, and the
 * state carries to the next call. This is not defensive programming: with 256
 * KB reads it happens constantly, and a parser that assumed a read ended on a
 * boundary would work perfectly for a year and then corrupt one screen in ten
 * thousand — on exactly the fast paths that make the terminal worth writing.
 *
 * ── The printable fast path is the whole throughput story ──────────────────
 *
 * The measured target is 22 MB/s, which is what both kitty and foot do, and
 * essentially all of a real stream is printable ASCII in runs. So GROUND state
 * does not step byte by byte through a switch: it finds the length of the
 * printable run ahead, then writes that many cells straight into the row with
 * no per-character dispatch, no width lookup and no wrap check inside the loop
 * — the run is pre-clipped to what fits on the line.
 *
 * That is the loop to vectorise later. It is written as a plain scan first
 * because a SIMD version of the wrong shape is slower than a clear one, and
 * because `syntty bench` has to be able to prove the difference.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"

#include <stdarg.h>

#include <string.h>

int st_char_width(uint32_t cp);

static void vt_reset_params(st_vt_t *vt)
{
	memset(vt->params, 0, sizeof vt->params);
	vt->nparams = 0;
	vt->param_seen = false;
	vt->intermediate = 0;
	vt->priv = 0;
}

void st_vt_init(st_vt_t *vt, st_grid_t *g)
{
	memset(vt, 0, sizeof *vt);
	vt->g = g;
	vt->state = VT_GROUND;
	memset(&vt->style, 0, sizeof vt->style);
	g->cur_style = st_style_intern(g, &vt->style);
}

/* A parameter that was never written is "absent", and absent is not zero:
 * `ESC[H` homes the cursor and `ESC[0;0H` also homes it, but `ESC[5A` and
 * `ESC[A` both mean one line where zero would mean none. Every caller states
 * its own default rather than sharing one. */
/* ── talking back to the child ──────────────────────────────────────────────
 *
 * A terminal answers questions: "which keyboard enhancements do you support?",
 * "what are you?", "where is the cursor?". Every answer is bytes written to the
 * pty, as though the person had typed them.
 *
 * ⚠ THE PARSER DOES NOT OWN A FILE DESCRIPTOR, deliberately. It is fed buffers
 * by whoever has one — `dump` from a file, `run` and the window from a pty —
 * and giving it an fd would mean the headless paths could no longer parse a
 * stream without one. So replies are QUEUED here and drained by the caller
 * after each feed. A caller that never drains loses only the replies, which is
 * the right failure for `dump`, whose input is a file that cannot be answered.
 *
 * The buffer is small and overflow is dropped rather than grown: replies are
 * tens of bytes, and a stream that generates more than this between drains is
 * a program spinning on queries, which growing a buffer would turn into
 * unbounded memory rather than a lost answer. */
static void vt_reply(st_vt_t *vt, const char *fmt, ...)
{
	char tmp[128];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	if (n <= 0)
		return;
	if ((size_t)n >= sizeof tmp)
		n = (int)sizeof tmp - 1;

	if (vt->reply_len + n > (int)sizeof vt->reply) {
		vt->reply_dropped++;
		return;
	}
	memcpy(vt->reply + vt->reply_len, tmp, (size_t)n);
	vt->reply_len += n;
}

/* The enhancements in force right now: the top of the stack. */
static unsigned vt_kbd_flags(const st_vt_t *vt)
{
	return vt->kkp_stack[vt->kkp_depth];
}

unsigned st_vt_kbd_flags(const st_vt_t *vt)
{
	return vt_kbd_flags(vt);
}

size_t st_vt_take_reply(st_vt_t *vt, char *out, size_t cap)
{
	size_t n = (size_t)vt->reply_len < cap ? (size_t)vt->reply_len : cap;
	memcpy(out, vt->reply, n);
	/* Emptied whole, not partially: a caller that could not take all of it has
	 * a buffer smaller than 128 bytes, and leaving a fragment of an escape
	 * sequence queued would send the child half an answer. */
	vt->reply_len = 0;
	return n;
}

static int param(const st_vt_t *vt, int i, int def)
{
	if (i >= vt->nparams)
		return def;
	return vt->params[i] == 0 ? def : vt->params[i];
}

static int param_raw(const st_vt_t *vt, int i)
{
	return i < vt->nparams ? vt->params[i] : 0;
}

/* ── SGR ────────────────────────────────────────────────────────────────── */

static void sgr(st_vt_t *vt)
{
	st_grid_t *g = vt->g;
	int n = vt->nparams ? vt->nparams : 1;

	for (int i = 0; i < n; i++) {
		int p = param_raw(vt, i);
		switch (p) {
		case 0:  memset(&vt->style, 0, sizeof vt->style); break;
		case 1:  vt->style.attrs |= ST_BOLD; break;
		case 2:  vt->style.attrs |= ST_DIM; break;
		case 3:  vt->style.attrs |= ST_ITALIC; break;
		case 4:  vt->style.attrs |= ST_UNDERLINE; break;
		case 5:
		case 6:  vt->style.attrs |= ST_BLINK; break;
		case 7:  vt->style.attrs |= ST_REVERSE; break;
		case 8:  vt->style.attrs |= ST_HIDDEN; break;
		case 9:  vt->style.attrs |= ST_STRIKE; break;
		case 21:
		case 22: vt->style.attrs &= (uint16_t)~(ST_BOLD | ST_DIM); break;
		case 23: vt->style.attrs &= (uint16_t)~ST_ITALIC; break;
		case 24: vt->style.attrs &= (uint16_t)~ST_UNDERLINE; break;
		case 25: vt->style.attrs &= (uint16_t)~ST_BLINK; break;
		case 27: vt->style.attrs &= (uint16_t)~ST_REVERSE; break;
		case 28: vt->style.attrs &= (uint16_t)~ST_HIDDEN; break;
		case 29: vt->style.attrs &= (uint16_t)~ST_STRIKE; break;
		case 39: vt->style.fg = ST_COL_DEFAULT; break;
		case 49: vt->style.bg = ST_COL_DEFAULT; break;

		/* 38/48 take their arguments from the SAME parameter list, so the
		 * loop has to consume them here — a switch that ignored them would
		 * then read `5` and `196` as "blink" and "bright white". */
		case 38:
		case 48: {
			uint32_t *slot = (p == 38) ? &vt->style.fg : &vt->style.bg;
			int kind = param_raw(vt, i + 1);
			if (kind == 5 && i + 2 < n) {
				*slot = ST_COL_INDEXED | (uint32_t)(param_raw(vt, i + 2) & 0xFF);
				i += 2;
			} else if (kind == 2 && i + 4 < n) {
				uint32_t r = (uint32_t)(param_raw(vt, i + 2) & 0xFF);
				uint32_t gg = (uint32_t)(param_raw(vt, i + 3) & 0xFF);
				uint32_t b = (uint32_t)(param_raw(vt, i + 4) & 0xFF);
				*slot = ST_COL_RGB | (r << 16) | (gg << 8) | b;
				i += 4;
			} else {
				i = n;   /* malformed: stop rather than misread the rest */
			}
			break;
		}
		default:
			if (p >= 30 && p <= 37)
				vt->style.fg = ST_COL_INDEXED | (uint32_t)(p - 30);
			else if (p >= 40 && p <= 47)
				vt->style.bg = ST_COL_INDEXED | (uint32_t)(p - 40);
			else if (p >= 90 && p <= 97)
				vt->style.fg = ST_COL_INDEXED | (uint32_t)(p - 90 + 8);
			else if (p >= 100 && p <= 107)
				vt->style.bg = ST_COL_INDEXED | (uint32_t)(p - 100 + 8);
			break;
		}
	}
	g->cur_style = st_style_intern(g, &vt->style);
}

/* ── mode setting ───────────────────────────────────────────────────────── */

static void set_mode(st_vt_t *vt, bool on)
{
	st_grid_t *g = vt->g;
	for (int i = 0; i < (vt->nparams ? vt->nparams : 1); i++) {
		int p = param_raw(vt, i);
		if (vt->priv == '?') {
			switch (p) {
			case 6:  g->origin = on; st_move_to(g, 0, 0); break;
			case 7:  g->autowrap = on; break;
			case 25: break;   /* cursor visibility: a renderer's business */
			default: vt->unhandled_csi++; break;
			}
		} else {
			vt->unhandled_csi++;
		}
	}
}

/* ── CSI dispatch ───────────────────────────────────────────────────────── */

static void csi_dispatch(st_vt_t *vt, uint8_t final)
{
	st_grid_t *g = vt->g;

	/* A PRIVATE prefix makes it a different sequence, not the same one with a
	 * decoration. `ESC[>4;2m` is XTMODKEYS and has nothing to do with SGR —
	 * falling through to the colour handler applied "4" as underline and "2"
	 * as dim to everything the program printed afterwards. Only the mode
	 * setters have a private form this stage understands. */
	if (vt->priv && final != 'h' && final != 'l' && final != 'u') {
		vt->unhandled_csi++;
		return;
	}

	/* ── the kitty keyboard protocol's control sequences ────────────────────
	 *
	 * Four sequences, told apart ONLY by their private prefix — `CSI > 1 u`
	 * and `CSI < 1 u` and `CSI = 1 u` are three different operations that
	 * differ by one byte. This is the same trap as `ESC[>4;2m` above, which is
	 * why they are handled here, before the switch, rather than under case 'u'
	 * where the prefix would have to be re-tested.
	 *
	 * The protocol is PROGRESSIVE: a program declares which enhancements it
	 * wants, and a terminal that only understands some of them still says so
	 * honestly rather than claiming all of them. Programs push their flags on
	 * entry and pop on exit, so a program that crashes does not leave the
	 * terminal in a mode the shell cannot use — which is exactly why the stack
	 * exists and why popping past the bottom must be harmless. */
	if (final == 'u' && vt->priv) {
		switch (vt->priv) {
		case '?':
			/* Query. The answer is what we ACTUALLY implement, never what was
			 * asked for: a terminal that echoes the request back claims
			 * support for enhancements it does not have, and the program then
			 * sends key encodings it will never be able to read. */
			vt_reply(vt, "\033[?%uu", (unsigned)(vt_kbd_flags(vt) & KKP_SUPPORTED));
			break;
		case '>': {
			unsigned f = (unsigned)param(vt, 0, 0) & KKP_SUPPORTED;
			if (vt->kkp_depth < VT_KKP_DEPTH - 1)
				vt->kkp_stack[++vt->kkp_depth] = (uint8_t)f;
			else
				vt->kkp_overflow++;   /* counted, not silently dropped */
			break;
		}
		case '<': {
			int n = param(vt, 0, 1);
			/* Popping an empty stack is a no-op, not an error. It is what a
			 * program does when it pops on exit having failed to push on
			 * entry, and the base state is the correct place to land. */
			while (n-- > 0 && vt->kkp_depth > 0)
				vt->kkp_depth--;
			break;
		}
		case '=': {
			unsigned f = (unsigned)param(vt, 0, 0) & KKP_SUPPORTED;
			int mode = param(vt, 1, 1);
			uint8_t *cur = &vt->kkp_stack[vt->kkp_depth];
			if      (mode == 1) *cur = (uint8_t)f;
			else if (mode == 2) *cur |= (uint8_t)f;
			else if (mode == 3) *cur &= (uint8_t)~f;
			break;
		}
		default:
			vt->unhandled_csi++;
			break;
		}
		return;
	}

	switch (final) {
	case '@': st_insert_chars(g, param(vt, 0, 1)); break;
	case 'A': st_move_by(g, 0, -param(vt, 0, 1)); break;
	case 'B': st_move_by(g, 0,  param(vt, 0, 1)); break;
	case 'C': st_move_by(g,  param(vt, 0, 1), 0); break;
	case 'D': st_move_by(g, -param(vt, 0, 1), 0); break;
	case 'E': st_move_to(g, 0, g->cy + param(vt, 0, 1)); break;
	case 'F': st_move_to(g, 0, g->cy - param(vt, 0, 1)); break;
	case 'G':
	case '`': st_move_to(g, param(vt, 0, 1) - 1, g->cy); break;
	case 'H':
	case 'f': st_move_to(g, param(vt, 1, 1) - 1, param(vt, 0, 1) - 1); break;
	case 'I': st_tab(g, param(vt, 0, 1)); break;
	case 'J': st_erase_display(g, param_raw(vt, 0)); break;
	case 'K': st_erase_line(g, param_raw(vt, 0)); break;
	case 'L': st_insert_lines(g, param(vt, 0, 1)); break;
	case 'M': st_delete_lines(g, param(vt, 0, 1)); break;
	case 'P': st_delete_chars(g, param(vt, 0, 1)); break;
	case 'S': st_scroll_up(g, param(vt, 0, 1)); break;
	case 'T': st_scroll_down(g, param(vt, 0, 1)); break;
	case 'X': st_erase_chars(g, param(vt, 0, 1)); break;
	case 'a': st_move_by(g, param(vt, 0, 1), 0); break;
	case 'd': st_move_to(g, g->cx, param(vt, 0, 1) - 1); break;
	case 'e': st_move_by(g, 0, param(vt, 0, 1)); break;
	case 'h': set_mode(vt, true); break;
	case 'l': set_mode(vt, false); break;
	case 'm': sgr(vt); break;
	case 'r':
		st_set_region(g, param(vt, 0, 1) - 1, param(vt, 1, g->rows) - 1);
		break;
	case 's':
		vt->saved_cx = g->cx; vt->saved_cy = g->cy;
		vt->saved_style = vt->style;
		break;
	case 'u':
		vt->style = vt->saved_style;
		g->cur_style = st_style_intern(g, &vt->style);
		st_move_to(g, vt->saved_cx, vt->saved_cy);
		break;
	default:
		/* COUNTED, not silently dropped. A stage with no renderer cannot
		 * tell "drew nothing" from "was never asked", so the parser keeps
		 * the tally and `syntty dump --stats` prints it. */
		vt->unhandled_csi++;
		break;
	}
}

static void esc_dispatch(st_vt_t *vt, uint8_t b)
{
	st_grid_t *g = vt->g;
	switch (b) {
	case '7':
		vt->saved_cx = g->cx; vt->saved_cy = g->cy;
		vt->saved_style = vt->style;
		break;
	case '8':
		vt->style = vt->saved_style;
		g->cur_style = st_style_intern(g, &vt->style);
		st_move_to(g, vt->saved_cx, vt->saved_cy);
		break;
	case 'D': st_newline(g); break;
	case 'E': st_carriage_return(g); st_newline(g); break;
	case 'M':
		if (g->cy == g->top)
			st_scroll_down(g, 1);
		else if (g->cy > 0)
			g->cy--;
		break;
	case 'c':
		st_erase_display(g, 2);
		st_move_to(g, 0, 0);
		memset(&vt->style, 0, sizeof vt->style);
		g->cur_style = st_style_intern(g, &vt->style);
		g->autowrap = true;
		st_set_region(g, 0, g->rows - 1);
		break;
	default:
		vt->unhandled_esc++;
		break;
	}
}

/* ── control characters ─────────────────────────────────────────────────── */

static void exec_c0(st_vt_t *vt, uint8_t b)
{
	switch (b) {
	case 0x07: break;                          /* BEL */
	case 0x08: st_backspace(vt->g); break;
	case 0x09: st_tab(vt->g, 1); break;
	case 0x0A:
	case 0x0B:
	case 0x0C: st_newline(vt->g); break;
	case 0x0D: st_carriage_return(vt->g); break;
	default: break;
	}
}

/* ── the printable fast path ────────────────────────────────────────────── */

/* How many bytes from p are plain printable ASCII. This is the loop that
 * decides the throughput number, and it is deliberately the simplest thing
 * that can be vectorised without changing any of its callers. */
static size_t ascii_run(const uint8_t *p, size_t n)
{
	size_t i = 0;
	while (i < n && p[i] >= 0x20 && p[i] < 0x7F)
		i++;
	return i;
}

/* Write a clipped run straight into the current row: no wrap test, no width
 * lookup, no function call per character. The caller has already guaranteed
 * the run fits. */
static void put_run(st_grid_t *g, const uint8_t *p, size_t n)
{
	st_row_t *row = &g->screen[g->cy];
	if (g->cx + n > row->hi)
		row->hi = (uint16_t)(g->cx + n);
	st_cell_t *cells = row->cells + g->cx;
	uint16_t style = g->cur_style;
	for (size_t i = 0; i < n; i++) {
		cells[i].cp = p[i];
		cells[i].style = style;
		cells[i].width = 1;
		cells[i].flags = 0;
	}
	g->cx = (uint16_t)(g->cx + n);
}

/* ── the machine ────────────────────────────────────────────────────────── */

void st_vt_feed(st_vt_t *vt, const uint8_t *buf, size_t len)
{
	st_grid_t *g = vt->g;
	size_t i = 0;

	while (i < len) {
		uint8_t b = buf[i];

		if (vt->state == VT_GROUND && vt->utf_need == 0 &&
		    b >= 0x20 && b < 0x7F) {
			/* Take the whole printable run, clipped to what is left on the
			 * line. Everything about this branch exists to avoid touching
			 * the state machine 2.6 million times per benchmark. */
			size_t run = ascii_run(buf + i, len - i);
			while (run > 0) {
				if (g->wrap_next && g->autowrap) {
					g->screen[g->cy].wrapped = true;
					st_carriage_return(g);
					st_newline(g);
				}
				size_t room = (size_t)(g->cols - g->cx);
				if (room == 0) {   /* autowrap off: overwrite the last cell */
					st_put(g, buf[i], 1);
					i++; run--;
					continue;
				}
				size_t take = run < room ? run : room;
				put_run(g, buf + i, take);
				i += take;
				run -= take;
				if (g->cx >= g->cols) {
					g->cx = (uint16_t)(g->cols - 1);
					/* The cursor parks on the last column and the wrap
					 * happens on the NEXT character, not now: a line that
					 * exactly fills the width must not leave a blank row
					 * behind it. */
					g->wrap_next = true;
				}
			}
			continue;
		}

		i++;

		/* C0 controls are executed from any state except the string ones —
		 * a BEL inside an OSC terminates it, and a CR inside a CSI really
		 * does move the cursor on every terminal worth matching. */
		if (b < 0x20 && vt->state != VT_OSC && vt->state != VT_DCS_IGNORE) {
			if (b == 0x1B) {
				vt->state = VT_ESC;
				vt_reset_params(vt);
				vt->utf_need = 0;
				continue;
			}
			exec_c0(vt, b);
			continue;
		}

		switch (vt->state) {
		case VT_GROUND: {
			/* UTF-8. Anything malformed becomes U+FFFD and resynchronises on
			 * the next lead byte — a decoder that swallowed the following
			 * bytes would turn one bad byte into a lost word. */
			if (vt->utf_need > 0) {
				if ((b & 0xC0) != 0x80) {
					st_put(g, 0xFFFD, 1);
					vt->utf_need = 0;
					i--;             /* reconsider this byte as a lead */
					continue;
				}
				vt->utf_cp = (vt->utf_cp << 6) | (uint32_t)(b & 0x3F);
				if (++vt->utf_seen == vt->utf_need) {
					uint32_t cp = vt->utf_cp;
					vt->utf_need = 0;
					st_put(g, cp, st_char_width(cp));
				}
				continue;
			}
			if (b < 0x80) {
				if (b == 0x7F)
					continue;        /* DEL is not a character */
				st_put(g, b, 1);
			} else if ((b & 0xE0) == 0xC0) {
				vt->utf_cp = (uint32_t)(b & 0x1F); vt->utf_need = 1; vt->utf_seen = 0;
			} else if ((b & 0xF0) == 0xE0) {
				vt->utf_cp = (uint32_t)(b & 0x0F); vt->utf_need = 2; vt->utf_seen = 0;
			} else if ((b & 0xF8) == 0xF0) {
				vt->utf_cp = (uint32_t)(b & 0x07); vt->utf_need = 3; vt->utf_seen = 0;
			} else {
				st_put(g, 0xFFFD, 1);
			}
			break;
		}

		case VT_ESC:
			if (b == '[') { vt_reset_params(vt); vt->state = VT_CSI_ENTRY; }
			else if (b == ']') { vt->osc_len = 0; vt->state = VT_OSC; }
			else if (b == 'P' || b == 'X' || b == '^' || b == '_') {
				vt->state = VT_DCS_IGNORE;
			} else if (b == '(' || b == ')' || b == '*' || b == '+') {
				/* Character-set designation. Consumed and ignored: the byte
				 * after it must not be printed, which is the actual bug if
				 * this case is missing. */
				vt->state = VT_ESC;
				vt->intermediate = (char)b;
				continue;
			} else if (vt->intermediate) {
				vt->intermediate = 0;
				vt->state = VT_GROUND;
			} else {
				esc_dispatch(vt, b);
				vt->state = VT_GROUND;
			}
			break;

		case VT_CSI_ENTRY:
		case VT_CSI_PARAM:
			if (b >= '0' && b <= '9') {
				if (vt->nparams == 0) vt->nparams = 1;
				if (vt->nparams <= VT_MAX_PARAMS) {
					int *p = &vt->params[vt->nparams - 1];
					if (*p < 100000000)
						*p = *p * 10 + (b - '0');
				}
				vt->param_seen = true;
				vt->state = VT_CSI_PARAM;
			} else if (b == ';' || b == ':') {
				if (vt->nparams == 0) vt->nparams = 1;
				if (vt->nparams < VT_MAX_PARAMS)
					vt->params[vt->nparams++] = 0;
				vt->state = VT_CSI_PARAM;
			} else if (b >= '<' && b <= '?') {
				vt->priv = (char)b;
				vt->state = VT_CSI_PARAM;
			} else if (b >= 0x20 && b <= 0x2F) {
				vt->intermediate = (char)b;
				vt->state = VT_CSI_INTERMEDIATE;
			} else if (b >= 0x40 && b <= 0x7E) {
				csi_dispatch(vt, b);
				vt->state = VT_GROUND;
			} else {
				vt->state = VT_CSI_IGNORE;
			}
			break;

		case VT_CSI_INTERMEDIATE:
			if (b >= 0x40 && b <= 0x7E) {
				csi_dispatch(vt, b);
				vt->state = VT_GROUND;
			} else if (b < 0x20 || b > 0x2F) {
				vt->state = VT_CSI_IGNORE;
			}
			break;

		case VT_CSI_IGNORE:
			if (b >= 0x40 && b <= 0x7E)
				vt->state = VT_GROUND;
			break;

		case VT_OSC:
			if (b == 0x07) {
				vt->osc[vt->osc_len] = 0;
				vt->osc_seen++;
				/* Titles are the one OSC this stage keeps, because a test
				 * can assert it and because it costs one strcpy. */
				if (vt->osc_len > 2 &&
				    (vt->osc[0] == '0' || vt->osc[0] == '2') &&
				    vt->osc[1] == ';') {
					snprintf(vt->title, sizeof vt->title, "%s", vt->osc + 2);
				}
				vt->state = VT_GROUND;
			} else if (b == 0x1B) {
				vt->state = VT_OSC_ESC;
			} else if (vt->osc_len < VT_OSC_MAX - 1) {
				vt->osc[vt->osc_len++] = (char)b;
			}
			break;

		case VT_OSC_ESC:
			vt->osc[vt->osc_len] = 0;
			vt->osc_seen++;
			if (vt->osc_len > 2 &&
			    (vt->osc[0] == '0' || vt->osc[0] == '2') && vt->osc[1] == ';')
				snprintf(vt->title, sizeof vt->title, "%s", vt->osc + 2);
			/* ESC \ ends it; ESC anything-else means the string was
			 * abandoned and that byte starts a new sequence. */
			vt->state = VT_GROUND;
			if (b != '\\')
				i--;
			break;

		case VT_DCS_IGNORE:
			if (b == 0x1B)
				vt->state = VT_DCS_ESC;
			break;

		case VT_DCS_ESC:
			vt->state = VT_GROUND;
			if (b != '\\')
				i--;
			break;
		}
	}
}
