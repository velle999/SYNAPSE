/* paste.c — turning a clipboard into something safe to hand a shell.
 *
 * ── Why this is a file of its own ──────────────────────────────────────────
 *
 * Same reason mouse.c is: the part that can be tested is separated from the
 * part that cannot. Getting the text OUT of another program is Wayland
 * plumbing — a data offer, a pipe, an fd in the poll loop, and a person who
 * pressed something. What happens to the bytes afterwards is a pure function,
 * and it is the half where the mistakes are dangerous rather than merely
 * annoying.
 *
 * ── The threat, stated plainly ─────────────────────────────────────────────
 *
 * A terminal that writes the clipboard to the pty unchanged has turned a copy
 * into an execution channel. Copy a command off a web page and the page can
 * append a newline to it: the shell runs it before the person has read it.
 * That is not hypothetical — it is a well-known trick with a decade of
 * examples, and BRACKETED PASTE (?2004) exists specifically to stop it, by
 * wrapping the text in markers that tell the shell "this was pasted, do not
 * treat the newlines as Enter".
 *
 * But bracketed paste is only as good as the wrapper. A clipboard containing
 * the END MARKER closes the bracket early, and everything after it is back to
 * being typing. So the payload is stripped of control bytes first, which makes
 * the marker unforgeable, and that stripping is not conditional on the mode:
 * when a program has NOT turned bracketed paste on — an old shell, a plain
 * `cat` — it is the only protection there is.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syntty.h"

#include <stdlib.h>
#include <string.h>

#define PASTE_START "\033[200~"
#define PASTE_END   "\033[201~"

char *st_paste_encode(const char *text, size_t len, bool bracketed,
                      size_t *out_len)
{
	/* Every input byte can survive at most as itself, plus the two markers. */
	size_t cap = len + sizeof PASTE_START + sizeof PASTE_END + 1;
	char  *out = xmalloc(cap);
	size_t n = 0;

	if (bracketed) {
		memcpy(out + n, PASTE_START, strlen(PASTE_START));
		n += strlen(PASTE_START);
	}

	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)text[i];

		/* ⚠ \r\n IS ONE LINE ENDING, NOT TWO. Text copied from anything that
		 * has been near Windows carries both bytes; sending both submits the
		 * line and then submits an empty one, so a two-line paste runs three
		 * commands. */
		if (c == '\r' && i + 1 < len && text[i + 1] == '\n')
			continue;

		if (c == '\n' || c == '\r') {
			/* ⚠ CR, NOT LF. What the Enter key sends is \r, and a line editor
			 * reading \n does not see a finished line: the paste appears to do
			 * nothing, or the last line is left dangling in the prompt. */
			out[n++] = '\r';
			continue;
		}
		if (c == '\t') {
			out[n++] = '\t';
			continue;
		}
		/* Everything else below 0x20, and DEL, is dropped. ESC most of all:
		 * with it gone the end marker below cannot be forged, and no pasted
		 * byte can be read as the start of a control sequence. */
		if (c < 0x20 || c == 0x7f)
			continue;

		out[n++] = (char)c;
	}

	if (bracketed) {
		memcpy(out + n, PASTE_END, strlen(PASTE_END));
		n += strlen(PASTE_END);
	}

	out[n] = '\0';
	if (out_len)
		*out_len = n;
	return out;
}
