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
#include <stdlib.h>

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

/* The parser owns no memory except the image store, which arrives only if a
 * program sends a graphics sequence. Callers that build a vt on the stack — all
 * of them — still have to release that. */
void st_vt_free(st_vt_t *vt)
{
	st_gfx_free(vt->gfx);
	vt->gfx = NULL;
	/* Whatever the child last asked to copy and nobody took. A terminal that
	 * shows one OSC 52 and exits must not leak it — the same rule the image
	 * store above is here for. */
	free(vt->clip);
	vt->clip = NULL;
	vt->clip_len = 0;
	vt->clip_target = ST_CLIP_NONE;
}

unsigned st_vt_kbd_flags(const st_vt_t *vt)
{
	return vt_kbd_flags(vt);
}

/* The graphics protocol answers in its own transport — an APC sequence, not a
 * CSI — because that is what the program is listening for. */
void vt_gfx_reply(st_vt_t *vt, uint32_t id, const char *msg)
{
	if (id)
		vt_reply(vt, "\033_Gi=%u;%s\033\\", id, msg);
	else
		vt_reply(vt, "\033_G;%s\033\\", msg);
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

/* ── OSC 52: the child asks for the clipboard ───────────────────────────────
 *
 *     ESC ] 52 ; <selection> ; <base64>  BEL
 *
 * This is how vim's `"+y`, tmux's copy mode and anything running over ssh put
 * text on the clipboard of the machine the person is actually sitting at —
 * there is no other channel from the far end of a pty to a local seat.
 *
 * ⚠ THE READ FORM IS REFUSED. `52;c;?` asks the terminal to send the clipboard
 * back as though typed, and the terminal has no idea who is asking: a program
 * on the far end of an ssh session, a script, anything sharing the pty. The
 * clipboard holds whatever was last copied, which is regularly a password or a
 * token. It is counted rather than ignored, so `--stats` can say that
 * something asked. */
static void osc52(st_vt_t *vt, const char *arg, int len)
{
	uint8_t target = ST_CLIP_CLIPBOARD;
	int i = 0;
	for (; i < len && arg[i] != ';'; i++) {
		/* The selection field is a set of letters. `c` is the clipboard; `p`
		 * and `s` are the primary — the one middle-click pastes. An empty
		 * field means the clipboard, which is what the spec says. */
		if (arg[i] == 'p' || arg[i] == 's')
			target = ST_CLIP_PRIMARY;
	}
	if (i >= len)
		return;                    /* no payload field at all */

	const char *b64 = arg + i + 1;
	int b64_len = len - i - 1;

	if (b64_len == 1 && b64[0] == '?') {
		vt->clip_reads_refused++;
		return;                    /* see the block above — never answered */
	}

	free(vt->clip);
	vt->clip = NULL;
	vt->clip_len = 0;
	vt->clip_target = target;
	vt->clip_sets++;

	if (b64_len <= 0)
		return;                    /* an empty payload clears the selection */

	/* 3 bytes out per 4 in, and the OSC buffer already bounds the input — the
	 * child cannot make this arbitrarily large by sending more. */
	size_t cap = (size_t)b64_len / 4 * 3 + 4;
	uint8_t *buf = xmalloc(cap);
	size_t n = st_b64_decode(b64, (size_t)b64_len, buf, cap);
	vt->clip = (char *)buf;
	vt->clip_len = n;
}

char *st_vt_take_clipboard(st_vt_t *vt, size_t *len, uint8_t *target)
{
	if (!vt->clip_target)
		return NULL;
	char *out = vt->clip;
	if (len)    *len = vt->clip_len;
	if (target) *target = vt->clip_target;
	vt->clip = NULL;
	vt->clip_len = 0;
	vt->clip_target = ST_CLIP_NONE;
	/* ⚠ An empty clipboard is still an answer — "the child cleared it" — so a
	 * NULL buffer with a target is handed back as an empty string rather than
	 * as "nothing happened". */
	return out ? out : xstrdup("");
}

/* ── one OSC handler, called from both terminators ──────────────────────────
 *
 * An OSC string ends with BEL or with ESC \, and the two paths used to carry
 * their own copy of what to do with the result. That is the shape every drift
 * bug in this codebase has had — a rule that exists in two places is a rule
 * that will shortly exist in one and a half. */
static void osc_dispatch(st_vt_t *vt)
{
	/* ⚠ DROPPED WHOLE, NOT TRUNCATED. This used to dispatch the first
	 * VT_OSC_MAX bytes of an overlong string as though that were the message.
	 * For a window title it is cosmetic; for OSC 52 it means half a base64
	 * payload decoding cleanly into DIFFERENT text and landing on the
	 * clipboard, which is the kind of wrong nobody would ever suspect. */
	if (vt->osc_over) {
		vt->osc_over = false;
		vt->osc_len = 0;
		vt->osc_dropped++;
		return;
	}

	vt->osc[vt->osc_len] = 0;
	vt->osc_seen++;

	if (vt->osc_len >= 4 && !memcmp(vt->osc, "52;", 3)) {
		osc52(vt, vt->osc + 3, vt->osc_len - 3);
		return;
	}

	/* Titles: the one OSC stage 1 kept, because a test can assert it. */
	if (vt->osc_len > 2 && (vt->osc[0] == '0' || vt->osc[0] == '2')
	    && vt->osc[1] == ';') {
		snprintf(vt->title, sizeof vt->title, "%s", vt->osc + 2);
		return;
	}

	/* ── OSC 133: where the prompt is, and what the command did ─────────────
	 *
	 * The de-facto semantic-prompt standard (FinalTerm, then iTerm2, then
	 * everyone). Four marks:
	 *
	 *   A  a prompt starts here
	 *   B  the prompt ends and what the person types begins
	 *   C  the command was submitted; output starts here
	 *   D  the command finished, optionally `D;<exit status>`
	 *
	 * ⚠ WE PARSE THE STANDARD, not something of our own, even though we ship
	 * the shell at the other end. A terminal that only understands its own
	 * shell's private marks gets none of this from bash, zsh, fish or a remote
	 * ssh session — all of which already emit 133 — and a shell that emits
	 * something private is useless in every other terminal. Owning both ends
	 * is worth using to make the marks RELIABLE, not to make them ours. */
	if (vt->osc_len >= 5 && !memcmp(vt->osc, "133;", 4)) {
		char kind = vt->osc[4];
		switch (kind) {
		case 'A':
			vt->g->screen[vt->g->cy].mark = ST_MARK_PROMPT;
			vt->g->screen[vt->g->cy].dirty = true;
			break;
		case 'B':
			/* The command line begins. Nothing to mark on the grid — the row
			 * is already the prompt's — but it is where a jump-to-prompt
			 * wants the cursor to land. */
			vt->cmd_col = vt->g->cx;
			break;
		case 'C':
			vt->g->screen[vt->g->cy].mark = ST_MARK_OUTPUT;
			vt->g->screen[vt->g->cy].dirty = true;
			vt->cmd_start_ns = now_ns();
			vt->cmd_running = true;
			break;
		case 'D': {
			/* `D` or `D;<status>`. A missing status is not zero — it is
			 * unknown, and reporting unknown as success is how a failing
			 * command comes back green. */
			int status = -1;
			if (vt->osc_len > 6 && vt->osc[5] == ';')
				status = (int)strtol(vt->osc + 6, NULL, 10);

			if (vt->cmd_running) {
				st_cmd_t *c = &vt->cmds[vt->ncmds % ST_MAX_CMDS];
				c->duration_ns = now_ns() - vt->cmd_start_ns;
				c->status = status;
				c->row = vt->g->cy;
				vt->ncmds++;
				vt->cmd_running = false;
			}
			break;
		}
		default:
			break;
		}
	}
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
			case 25: g->cursor_visible = on; break;

			/* ── mouse reporting ────────────────────────────────────────────
			 *
			 * Three levels, and a program picks one: buttons only, buttons
			 * plus motion while held, or every motion. They are recorded
			 * rather than merged because a program that asked for 1000 and is
			 * sent 1003's motion events gets a flood it never wanted. */
			case 1000: g->mouse_mode = on ? 1000 : 0; break;
			case 1002: g->mouse_mode = on ? 1002 : 0; break;
			case 1003: g->mouse_mode = on ? 1003 : 0; break;
			/* SGR coordinates. The old encoding put the column in ONE BYTE
			 * offset by 32, so it breaks silently past column 223 — which is
			 * an ordinary width on a wide monitor. Programs ask for 1006 to
			 * get numbers instead. */
			case 1006: g->mouse_sgr = on; break;

			/* ⚠ BRACKETED PASTE IS A SAFETY FEATURE, not a convenience. With
			 * it off, pasting three lines into a shell RUNS the first two
			 * immediately — there is no chance to look at them. With it on,
			 * the paste arrives wrapped in markers the shell recognises as
			 * text rather than as typing. */
			case 2004: g->bracketed_paste = on; break;

			/* ── the alternate screen ───────────────────────────────────────
			 *
			 * 1049 is the modern one: switch, and save/restore the cursor with
			 * it. 47 and 1047 are the older forms that switch only. All three
			 * are still emitted in the wild by different programs. */
			case 1049:
			case 1047:
			case 47:
				st_grid_alt_screen(g, on, p == 1049);
				break;

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
 * the run fits.
 *
 * ⚠ IT STILL HAS TO MARK THE ROW DIRTY. This is the path essentially all
 * ordinary output takes, and for three releases it was the one path that wrote
 * cells without saying so — st_grid_putc marks, every erase marks, every
 * scroll marks a whole range, and the fast path silently did not. Rows written
 * by it were therefore never repainted, and the only reason a terminal was
 * usable at all is that something ELSE happened to dirty most of them: the
 * cursor forces the row it left and the row it arrived at, and the cursor
 * lands on almost every row when output arrives a few bytes at a time.
 *
 * It arrives a few bytes at a time in the TESTS. A pty read is 256 KB, so a
 * command that prints a block of text lands whole, the cursor touches only the
 * first row and the last, and every row between them is written and never
 * drawn — until a keystroke, a scroll or a drag-select dirties it and the text
 * appears all at once, as if it had been hiding. That is what `syntty about`
 * under a real compositor looked like: three visible lines out of nineteen.
 *
 * ⚠ AND IT IS WHY `damage-check --split=3` PASSED. Small chunks are harsher
 * for a missed MARK — a later chunk covering the same row hides one — but they
 * are the wrong tool for a missed row entirely, because they hand the cursor
 * to every row in turn and it does the marking the grid failed to do. The
 * suite now checks a single whole-stream feed as well, which is what a pty
 * actually delivers. */
static void put_run(st_grid_t *g, const uint8_t *p, size_t n)
{
	st_row_t *row = &g->screen[g->cy];
	row->dirty = true;
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
		/* ⚠ THE STATES THAT SWALLOW THEIR OWN CONTROL BYTES must be listed
		 * here, and APC is one of them. Without it the C0 handler steals the
		 * ESC that TERMINATES the sequence: the payload accumulates, the
		 * terminator never arrives, and the collected bytes are silently
		 * dropped while the parser wanders off into VT_ESC. The symptom is a
		 * graphics protocol that answers nothing at all. */
		if (b < 0x20 && vt->state != VT_OSC && vt->state != VT_DCS_IGNORE
		    && vt->state != VT_APC && vt->state != VT_APC_ESC) {
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
			else if (b == ']') {
				vt->osc_len = 0;
				vt->osc_over = false;
				vt->state = VT_OSC;
			}
			else if (b == '_') {
				/* APC. Everything else in this family is still swallowed —
				 * DCS, SOS and PM have no meaning here — but APC is where the
				 * graphics protocol lives, so it is COLLECTED. */
				vt->apc_len = 0;
				vt->apc_over = false;
				vt->state = VT_APC;
			} else if (b == 'P' || b == 'X' || b == '^') {
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
				osc_dispatch(vt);
				vt->state = VT_GROUND;
			} else if (b == 0x1B) {
				vt->state = VT_OSC_ESC;
			} else if (vt->osc_len < VT_OSC_MAX - 1) {
				vt->osc[vt->osc_len++] = (char)b;
			} else {
				/* Past the buffer. The rest of the string is still consumed —
				 * the terminator has to be found or the parser would treat the
				 * remainder as text — but the whole sequence is discarded when
				 * it arrives. */
				vt->osc_over = true;
			}
			break;

		case VT_OSC_ESC:
			osc_dispatch(vt);
			/* ESC \ ends it; ESC anything-else means the string was
			 * abandoned and that byte starts a new sequence. */
			vt->state = VT_GROUND;
			if (b != '\\')
				i--;
			break;

		case VT_APC:
			if (b == 0x1B) {
				vt->state = VT_APC_ESC;
				break;
			}
			/* Overlong is DROPPED, not truncated-and-parsed: half a control
			 * string parsed as if whole is how a truncated image id becomes a
			 * different image id. The sequence is still consumed to its
			 * terminator so the rest of the stream stays in step. */
			if (vt->apc_len < VT_APC_MAX)
				vt->apc[vt->apc_len++] = (char)b;
			else
				vt->apc_over = true;
			break;

		case VT_APC_ESC:
			if (b == '\\') {
				if (vt->apc_over) {
					vt->apc_dropped++;
				} else {
					vt->apc[vt->apc_len] = '\0';
					st_gfx_apc(vt, vt->apc, (size_t)vt->apc_len);
				}
				vt->state = VT_GROUND;
			} else {
				/* An ESC that was not the terminator is part of the payload —
				 * put it back and keep collecting. */
				if (vt->apc_len < VT_APC_MAX)
					vt->apc[vt->apc_len++] = 0x1B;
				vt->state = VT_APC;
				i--;
			}
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
