/* mouse.c — where the pointer is, in the words the child asked for.
 *
 * ── Why this is a file and not twenty lines inside win.c ───────────────────
 *
 * Because it is the only part of mouse support that can be TESTED. The window
 * needs a compositor, a seat and a person moving a real pointer — input is
 * never synthesised on a live session, and a headless cage has no pointer at
 * all. So the rules live here, in a function that takes numbers and returns
 * bytes, and `syntty mouse` asserts every one of them on a machine with no
 * display.
 *
 * That split is the same one the keyboard has: the encoder is testable, the
 * seat is not, and the seat is kept as thin as it can be so that what is
 * untested is only the plumbing.
 *
 * ── The two encodings ──────────────────────────────────────────────────────
 *
 * X10, from 1984, which everything still speaks:
 *
 *     ESC [ M  <32+button>  <32+col+1>  <32+row+1>
 *
 * Three BYTES for three numbers, each offset by 32 so none of them is a
 * control character. ⚠ That offset is also the flaw: a byte can carry at most
 * 255, so the last column it can name is 223. Two hundred and twenty-three
 * columns is not a theoretical limit — it is a 1920-pixel window at a normal
 * font size. Past it the encoding does not fail, it WRAPS, and the program
 * receives a confident click on a completely different cell.
 *
 * SGR (?1006), which is what fixed that:
 *
 *     ESC [ < button ; col+1 ; row+1 M      (press or motion)
 *     ESC [ < button ; col+1 ; row+1 m      (release — a lowercase m)
 *
 * Decimal numbers with no ceiling, and — the other thing X10 cannot do — a
 * release says WHICH button came up. In X10 every release is button 3,
 * "something was let go", so a program watching for a middle-click paste has
 * to remember what went down and hope it did not miss one.
 *
 * ⚠ WHICH ONE IS SENT IS THE PROGRAM'S CHOICE, NOT OURS. A terminal that
 * decides SGR is better and sends it to a program that never asked for ?1006
 * produces a screen full of `<0;40;12M` typed into it, because the program is
 * not parsing that shape and the bytes fall through to its input. The mode is
 * recorded on the grid by the parser and read here; nothing in this file
 * decides anything.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"

#include <stdio.h>

/* The last cell X10 can name: 32 + col + 1 must fit in a byte, so col+1 <= 223.
 * Named rather than written as 223 in three places, because the number is a
 * consequence of the offset rather than a choice anybody made. */
#define X10_MAX_COORD 223

size_t st_mouse_encode(uint16_t mode, bool sgr, int event, int button,
                       unsigned mods, int col, int row,
                       char *out, size_t cap, const char **why)
{
	if (why)
		*why = NULL;

	if (mode == 0) {
		if (why) *why = "no program asked for mouse reporting";
		return 0;
	}
	/* Negative coordinates happen: the pointer leaves the window while a
	 * button is held, which is an ordinary part of dragging. The event is
	 * dropped rather than clamped — a clamped drag reports a click on the edge
	 * cell that the person never made. */
	if (col < 0 || row < 0) {
		if (why) *why = "the pointer is outside the grid";
		return 0;
	}

	bool wheel = button >= ST_BTN_WHEEL_UP && button <= ST_BTN_WHEEL_RIGHT;

	switch (event) {
	case ST_MOUSE_MOTION:
		/* ⚠ THE WHOLE DIFFERENCE BETWEEN THE THREE MODES IS HERE. */
		if (mode == 1000) {
			if (why) *why = "?1000 reports buttons only, never motion";
			return 0;
		}
		if (mode == 1002 && button == ST_BTN_NONE) {
			if (why) *why = "?1002 reports motion only while a button is held";
			return 0;
		}
		break;
	case ST_MOUSE_RELEASE:
		/* A wheel notch is a press with no release. Sending one gives every
		 * program a phantom button-up for a button that was never down, and
		 * the ones that count presses and releases to track dragging get their
		 * state machine corrupted by a scroll. */
		if (wheel) {
			if (why) *why = "a wheel notch has no release";
			return 0;
		}
		break;
	default:
		break;
	}

	/* The button field: the button, plus 32 if this is motion, plus the
	 * modifiers. The wheel's 64 and 65 are the same field with bit 6 set,
	 * which is why they are not simply buttons 3 and 4. */
	unsigned cb = (unsigned)button;
	if (event == ST_MOUSE_MOTION)
		cb += 32;
	cb |= mods & (ST_MOUSE_SHIFT | ST_MOUSE_ALT | ST_MOUSE_CTRL);

	if (sgr) {
		int n = snprintf(out, cap, "\033[<%u;%d;%d%c", cb, col + 1, row + 1,
		                 event == ST_MOUSE_RELEASE ? 'm' : 'M');
		if (n < 0 || (size_t)n >= cap) {
			if (why) *why = "the encoded event did not fit the buffer";
			return 0;
		}
		return (size_t)n;
	}

	/* ⚠ X10 CANNOT SAY WHICH BUTTON WAS RELEASED, so it says 3 — "something
	 * came up". Reporting the actual button here would be a sequence that
	 * looks valid and means a press of button 3 to everything reading it. */
	if (event == ST_MOUSE_RELEASE)
		cb = (cb & ~7u) | ST_BTN_NONE;

	/* ⚠ REFUSED, NOT WRAPPED. Past column 223 the offset byte overflows and
	 * the program is told about a click somewhere else entirely — a bug that
	 * only appears on wide windows, only in programs still using the old
	 * encoding, and looks like the program's own hit testing is broken. A
	 * dropped event is a mouse that does nothing, which is at least honest,
	 * and `why` says so out loud. */
	if (col + 1 > X10_MAX_COORD || row + 1 > X10_MAX_COORD) {
		if (why)
			*why = "beyond column 223, which the 1984 encoding cannot name "
			       "(the program has not asked for ?1006)";
		return 0;
	}
	if (cap < 6) {
		if (why) *why = "the encoded event did not fit the buffer";
		return 0;
	}

	out[0] = '\033';
	out[1] = '[';
	out[2] = 'M';
	out[3] = (char)(32 + cb);
	out[4] = (char)(32 + col + 1);
	out[5] = (char)(32 + row + 1);
	return 6;
}
