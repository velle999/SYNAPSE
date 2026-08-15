/* key.c — what a keystroke becomes on the child's input.
 *
 * ── Why this is a file and not two hundred lines inside win.c ───────────────
 *
 * The same split mouse.c makes, and for the same reason: the seat cannot be
 * tested and the rules can. A keystroke needs a compositor, a focused surface
 * and a person pressing a key — input is never synthesised on a live session,
 * and a headless cage has no keyboard at all. So everything that DECIDES what
 * bytes a key produces lives here, takes a keysym and a modifier mask, returns
 * bytes, and `syntty key shift+tab` asserts it on a machine with no display.
 *
 * mouse.c's header has claimed since it was written that "that split is the
 * same one the keyboard has". It was not true when it was written. It is now.
 *
 * Until this file existed the encoder was unreachable from any test, and it
 * showed — three keys were silently wrong for as long as the window has
 * existed, all three of them the same defect:
 *
 *   ⚠ THE LEGACY ENCODER IGNORED MODIFIERS ENTIRELY.
 *
 *   · Shift+Tab sent NOTHING AT ALL. With Shift held, xkb resolves the Tab key
 *     to ISO_Left_Tab, which is not Tab, has no table entry, and produces no
 *     text — so the encoder fell through every branch and wrote zero bytes.
 *     Confirmed against a us keymap: sym 0xfe20, utf8 bytes 0. The key was
 *     dead, not wrong, which is why it looked like the terminal was not
 *     "reading" it.
 *   · Shift+Left was BYTE-IDENTICAL to Left (xkb resolves both to the Left
 *     keysym; the modifier is only in the state). Every program that extends a
 *     selection with Shift+Arrow saw a bare arrow and moved the cursor
 *     instead — there is no way for it to tell.
 *   · Alt+f and Alt+Backspace sent "f" and DEL. Alt is a PREFIX, not a level
 *     shift, so xkb hands back the unmodified text and something has to add
 *     the escape. Nothing did.
 *
 * ── The three shapes ───────────────────────────────────────────────────────
 *
 * LEGACY, which is what a program gets until it asks for anything else:
 *
 *     unmodified   ESC [ A          arrows, Home, End
 *                  ESC O P          F1..F4 — SS3, not CSI, which is what
 *                                   terminfo's kf1 says and what every curses
 *                                   program has compiled against
 *                  ESC [ 5 ~        the numbered block and F5..F12
 *     modified     ESC [ 1 ; m A    the modifier goes in as a SECOND parameter
 *                  ESC [ 5 ; m ~    with the key's own number first
 *
 * where m is 1 + shift + 2*alt + 4*ctrl + 8*super. The +1 is the convention,
 * not an off-by-one: it makes "no modifiers" a 1, so an absent parameter and a
 * present one agree.
 *
 * ⚠ AND SHIFT+TAB IS `ESC [ Z`, WHICH FITS NONE OF THAT. It is CBT, terminfo's
 * `kcbt`, and it predates the modifier convention by a decade. `CSI 1;2I` is
 * what the pattern would suggest and nothing reads it. This is the one key
 * where following the rule produces a key that does not work.
 *
 * THE KITTY PROTOCOL, when the program has pushed flags for it, which says
 * exactly what happened and can report releases and repeats:
 *
 *     CSI unicode-key ; modifiers : event ; text u
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"

#include <stdio.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

static size_t lit(char *out, size_t cap, const char *s)
{
	size_t n = strlen(s);
	if (n >= cap)
		return 0;
	memcpy(out, s, n);
	return n;
}

/* The keys that keep a legacy CSI shape even under the protocol: arrows, the
 * navigation block and the function keys. Returns the numeric parameter and
 * the final byte, or 0 for "not one of these". */
static uint32_t kkp_functional(uint32_t sym, char *final)
{
	switch (sym) {
	case XKB_KEY_Up:        *final = 'A'; return 1;
	case XKB_KEY_Down:      *final = 'B'; return 1;
	case XKB_KEY_Right:     *final = 'C'; return 1;
	case XKB_KEY_Left:      *final = 'D'; return 1;
	case XKB_KEY_Home:      *final = 'H'; return 1;
	case XKB_KEY_End:       *final = 'F'; return 1;
	case XKB_KEY_Insert:    *final = '~'; return 2;
	case XKB_KEY_Delete:    *final = '~'; return 3;
	case XKB_KEY_Page_Up:   *final = '~'; return 5;
	case XKB_KEY_Page_Down: *final = '~'; return 6;
	case XKB_KEY_F1:        *final = 'P'; return 1;
	case XKB_KEY_F2:        *final = 'Q'; return 1;
	case XKB_KEY_F3:        *final = 'R'; return 1;
	case XKB_KEY_F4:        *final = 'S'; return 1;
	case XKB_KEY_F5:        *final = '~'; return 15;
	case XKB_KEY_F6:        *final = '~'; return 17;
	case XKB_KEY_F7:        *final = '~'; return 18;
	case XKB_KEY_F8:        *final = '~'; return 19;
	case XKB_KEY_F9:        *final = '~'; return 20;
	case XKB_KEY_F10:       *final = '~'; return 21;
	case XKB_KEY_F11:       *final = '~'; return 23;
	case XKB_KEY_F12:       *final = '~'; return 24;
	default:                return 0;
	}
}

/* F1..F4 take SS3 rather than CSI whenever they carry no modifier, always and
 * regardless of any mode. They are told apart from the arrows by their final
 * byte — both groups report parameter 1. */
static bool is_ss3_always(uint32_t num, char final)
{
	return num == 1 && final >= 'P' && final <= 'S';
}

/* The cursor keys, which are the ones DECCKM moves — the arrows plus Home and
 * End, and NOT the numbered block. `CSI 2~` is Insert in both modes; there is
 * no SS3 form of a `~` sequence to switch to. */
static bool is_cursor_key(uint32_t num, char final)
{
	if (num != 1)
		return false;
	switch (final) {
	case 'A': case 'B': case 'C': case 'D': case 'H': case 'F': return true;
	default: return false;
	}
}

/* ── DECCKM ─────────────────────────────────────────────────────────────────
 *
 * ⚠ THE ALTERNATE FORM IS USED ONLY WHEN NO MODIFIERS ARE PRESENT. That is not
 * an interpretation — it is what the terminfo entries describe and what kitty's
 * protocol specification states in as many words ("This form is used only in
 * cursor key mode and only when no modifiers are present"). Shift+Up stays
 * `CSI 1;2A` in both modes, because the modified form has nowhere to put the
 * parameters: SS3 takes none.
 *
 * Verified against `infocmp xterm-256color`, which carries both halves —
 * `smkx=\E[?1h\E=` turns it on, and the key capabilities recorded for that
 * state are `kcuu1=\EOA`, `kcub1=\EOD`, `khome=\EOH`, `kend=\EOF`.
 *
 * This is also why `khome` reads `\EOH` while an unmodified Home sends
 * `ESC [ H`: terminfo records the APPLICATION-mode value, because ncurses
 * calls smkx. Both are correct, in their own mode.
 */
static bool use_ss3(uint32_t num, char final, bool app_cursor)
{
	return is_ss3_always(num, final)
	       || (app_cursor && is_cursor_key(num, final));
}

/* The first codepoint of a UTF-8 run.
 *
 * The associated-text field is a CODEPOINT, not a byte — sending the first
 * byte of a multi-byte sequence would report 0xC3 for 'a' with an umlaut, and
 * the program would render whatever that is instead. xkb hands us UTF-8, so it
 * has to be decoded back. */
static uint32_t decode_utf8_first(const char *s, int n)
{
	if (n <= 0)
		return 0;
	unsigned char c = (unsigned char)s[0];
	int need;
	uint32_t cp;
	if      (c < 0x80) return c;
	else if ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; }
	else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; }
	else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; }
	else return 0;
	if (n < need + 1)
		return 0;
	for (int i = 1; i <= need; i++) {
		unsigned char b = (unsigned char)s[i];
		if ((b & 0xC0) != 0x80)
			return 0;
		cp = cp << 6 | (b & 0x3F);
	}
	return cp;
}

/* The protocol's number for a key that is not one of the above. Escape, Enter,
 * Tab and Backspace have fixed codes; everything else is its own codepoint.
 *
 * ⚠ ISO_Left_Tab IS TAB. It is the keysym Shift+Tab resolves to, and the
 * protocol has no separate number for it — reporting it as its raw keysym
 * (0xfe20) would name a key no program has heard of. It is key 9 with shift
 * held, which is exactly what it is. */
static uint32_t kkp_code(uint32_t sym, const char *utf8, int n)
{
	switch (sym) {
	case XKB_KEY_Escape:       return 27;
	case XKB_KEY_Return:       return 13;
	case XKB_KEY_Tab:          return 9;
	case XKB_KEY_ISO_Left_Tab: return 9;
	case XKB_KEY_BackSpace:    return 127;
	default: break;
	}
	if (sym >= 0x20 && sym < 0x7f)
		return sym;
	/* A key whose keysym is not a character but which produced one — a dead
	 * key resolving, or a compose sequence finishing. */
	if (n > 0)
		return (unsigned char)utf8[0];
	return 0;
}

/* ── legacy ─────────────────────────────────────────────────────────────────
 *
 * Returns 0 for "this key sends nothing", which is a real answer: a modifier
 * pressed on its own has no bytes and never has had.
 */
static size_t legacy_encode(uint32_t sym, unsigned mods, const char *utf8,
                            int n, bool app_cursor, char *out, size_t cap)
{
	/* ⚠ FIRST, because it is the exception to everything below. */
	if (sym == XKB_KEY_ISO_Left_Tab)
		return lit(out, cap, "\033[Z");

	/* Ctrl+letter is a control code, and xkb does not fold it: it hands back
	 * the letter and the modifier separately. Shift is deliberately not
	 * consulted — Ctrl+C and Ctrl+Shift+C are both 0x03 once the terminal's
	 * own bindings have had their look. */
	if ((mods & ST_KEY_CTRL)
	    && ((sym >= 'a' && sym <= 'z') || (sym >= 'A' && sym <= 'Z'))) {
		if (cap < 1)
			return 0;
		out[0] = (char)((sym | 0x20) - 'a' + 1);
		return 1;
	}

	char     final = 0;
	uint32_t num   = kkp_functional(sym, &final);
	if (num) {
		/* ⚠ THE SHORT FORMS ARE NOT AN OPTIMISATION. `CSI A` is what every
		 * program has read for an unmodified Up since the seventies, and one
		 * handed `CSI 1;1A` will not recognise it. The parameters are spelled
		 * out only once there is something to say. */
		if (mods == 0) {
			if (final == '~')
				return (size_t)snprintf(out, cap, "\033[%u~", num);
			if (use_ss3(num, final, app_cursor))
				return (size_t)snprintf(out, cap, "\033O%c", final);
			return (size_t)snprintf(out, cap, "\033[%c", final);
		}
		return (size_t)snprintf(out, cap, "\033[%u;%u%c", num, mods + 1, final);
	}

	/* A named sequence WINS over the key's own text: several of these keys do
	 * produce text and it is the wrong text — Escape yields \033, which is
	 * right by accident, and Backspace yields \010, which is BS where every
	 * line editor on this machine wants DEL. */
	const char *seq = NULL;
	switch (sym) {
	/* ⚠ DEL and not BS: that is what `stty` reports and what readline
	 * expects, and getting it the other way round is why backspace sometimes
	 * prints ^H instead of erasing. Ctrl+Backspace is the other one — 0x08 is
	 * what readline and Claude Code read as delete-word. */
	case XKB_KEY_BackSpace: seq = (mods & ST_KEY_CTRL) ? "\010" : "\177"; break;
	case XKB_KEY_Escape:    seq = "\033"; break;
	default: break;
	}
	if (!seq && n <= 0)
		return 0;                  /* a modifier by itself, or a dead key */

	/* ⚠ ALT IS A PREFIX, NOT A LEVEL. xkb reports Alt+f as the text "f" with a
	 * modifier set, so without this the key is indistinguishable from f and
	 * every meta binding in readline, bash and Claude Code is dead. The escape
	 * goes in front of whatever the key would otherwise have sent, which is
	 * also how Alt+Backspace becomes delete-word. */
	size_t at = 0;
	if (mods & ST_KEY_ALT) {
		if (cap < 1)
			return 0;
		out[at++] = '\033';
	}
	if (seq) {
		size_t got = lit(out + at, cap - at, seq);
		return got ? at + got : 0;
	}
	if ((size_t)n >= cap - at)
		return 0;
	memcpy(out + at, utf8, (size_t)n);
	return at + (size_t)n;
}

size_t st_key_encode(uint32_t sym, unsigned mods, const char *utf8, int n,
                     unsigned flags, bool app_cursor, bool pressed,
                     char *out, size_t cap)
{
	if (cap < 2)
		return 0;

	if (!flags) {
		/* ⚠ A RELEASE HAS NO LEGACY BYTES. Sent anyway it arrives at a shell
		 * as a burst of unrecognised escapes, which is how a terminal "types
		 * garbage by itself". */
		if (!pressed)
			return 0;
		return legacy_encode(sym, mods, utf8, n, app_cursor, out, cap);
	}

	unsigned m   = mods + 1;
	unsigned evt = pressed ? 1 : 3;

	char     final = 0;
	uint32_t num   = kkp_functional(sym, &final);
	if (!num) {
		num   = kkp_code(sym, utf8, n);
		final = 'u';
		if (!num)
			return 0;              /* a modifier by itself: nothing to report */
	}

	int len;
	if (m == 1 && evt == 1) {
		/* ⚠ DECCKM APPLIES HERE TOO. The protocol deliberately keeps the
		 * legacy shape for a functional key with no modifiers, and the mode is
		 * part of that shape — kitty's own specification says the alternate
		 * form is used "only in cursor key mode and only when no modifiers are
		 * present", which is exactly the branch this is. A program that pushed
		 * flags AND set smkx is asking for both. */
		if (final == 'u')      len = snprintf(out, cap, "\033[%uu", num);
		else if (final == '~') len = snprintf(out, cap, "\033[%u~", num);
		else if (use_ss3(num, final, app_cursor))
		                       len = snprintf(out, cap, "\033O%c", final);
		else                   len = snprintf(out, cap, "\033[%c", final);
	} else if (evt == 1) {
		len = snprintf(out, cap, "\033[%u;%u%c", num, m, final);
	} else {
		len = snprintf(out, cap, "\033[%u;%u:%u%c", num, m, evt, final);
	}

	/* The text the key produced, when it was asked for and there is any. It is
	 * a THIRD parameter, so the first two have to be spelled out even when they
	 * are defaults — `CSI 97;;97u` would be ambiguous about which field was
	 * omitted, and the protocol counts positions.
	 *
	 * Only for the `u` form: a functional key produces no text, and there is no
	 * position for it in the `~` and letter-terminated shapes. */
	if ((flags & KKP_ASSOCIATED_TEXT) && pressed && n > 0 && final == 'u') {
		uint32_t cp = decode_utf8_first(utf8, n);
		if (cp)
			len = snprintf(out, cap, "\033[%u;%u:%u;%uu", num, m, evt, cp);
	}

	if (len < 0 || (size_t)len >= cap)
		return 0;
	return (size_t)len;
}
