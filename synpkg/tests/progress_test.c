/* progress_test.c — drive progress.c the way alpm does, and print what lands.
 *
 * ⚠ A DRIVER, NOT AN ASSERTING TEST. The assertions are in progress_test.sh,
 * which runs this under a pty — because the ONE branch that decides whether
 * anything is drawn at all is isatty(stderr), and a test that ran this on a
 * pipe would prove the bar silent and call it a pass. That is the whole reason
 * ILoveCandy looked broken in the first place: nobody was looking at a terminal
 * when the drawing was decided.
 *
 * Usage: progress_test <percent> [<percent>...]
 * Each argument is one redraw of the same key, in order, so a sequence walks
 * the mouth along the bar exactly as a download does.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"

#include <stdlib.h>

int main(int argc, char **argv)
{
	/* HUMAN and colour OFF. The escapes are pacman's and are not what is
	 * being asserted — a test that grepped for a yellow C would be asserting
	 * the terminal's vocabulary rather than the bar's shape, and would pass on
	 * a bar made entirely of escape sequences. */
	g_out   = OUT_HUMAN;
	g_color = false;

	for (int i = 1; i < argc; i++)
		progress_draw("test-pkg", "test-pkg-1.0-1", atoi(argv[i]));
	progress_end();
	return 0;
}
