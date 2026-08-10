/* syn-settings — re-probe a DRM connector.
 *
 * Writing "detect" to a connector's sysfs `status` asks the kernel to
 * re-run detection on that head. It is the one recovery action this OS has
 * for a connector stuck `enabled` with no sink — the DP-3 case — and it is
 * currently reachable only as a root shell one-liner nobody remembers.
 *
 * WHAT THIS DELIBERATELY DOES NOT DO
 *
 * It does not acquire privilege. A connector's sysfs `status` is 0644
 * root:root, so an ordinary user cannot write it, and the honest response is
 * to say
 * so and print the exact command — not to ship a polkit action and a helper
 * that runs as root on request. A settings app is a poor place to add the
 * machine's most convenient privilege escalation, and this write is not
 * urgent enough to justify it. Whether to add a narrowly-scoped polkit action
 * for exactly this one operation is a posture decision, and it belongs to
 * whoever owns the OS, not to whoever was writing the pane.
 *
 * The argument is still validated as if it were privileged, because one day
 * this may run as root from a unit and "validated at the boundary" should not
 * depend on who happens to be calling.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Resolve a name the user would type — "DP-3" — to the sysfs directory that
 * actually holds it, "card1-DP-3". Matching against the real directory rather
 * than building a path from the argument is what makes traversal impossible:
 * a name that does not correspond to a connector present right now never
 * becomes a path at all. */
static int resolve_connector(const char *want, char *out, size_t cap)
{
	DIR *d = opendir("/sys/class/drm");
	if (!d) return 0;

	int found = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "card", 4) != 0) continue;
		const char *dash = strchr(e->d_name, '-');
		if (!dash) continue;

		/* Accept either spelling: "DP-3" or "card1-DP-3". */
		if (!strcmp(dash + 1, want) || !strcmp(e->d_name, want)) {
			if (snprintf(out, cap, "/sys/class/drm/%s", e->d_name) < (int)cap)
				found = 1;
			break;
		}
	}
	closedir(d);
	return found;
}

int do_probe(int argc, char **argv)
{
	if (argc < 1) {
		fprintf(stderr, "syn-settings: probe needs a connector, e.g. DP-3\n");
		return 2;
	}

	char dir[512];
	if (!resolve_connector(argv[0], dir, sizeof dir)) {
		fprintf(stderr, "syn-settings: no such connector '%s' — "
		                "`syn-settings --rec display` lists them\n", argv[0]);
		return 2;
	}

	char path[576];
	snprintf(path, sizeof path, "%s/status", dir);

	if (g_dry_run) {
		printf("would write: detect > %s\n", path);
		return 0;
	}

	FILE *f = fopen(path, "we");
	if (!f) {
		/* The common case, and it gets a useful answer rather than errno. */
		fprintf(stderr,
		        "syn-settings: cannot write %s (%s)\n"
		        "\n"
		        "That file is root-owned. syn-settings does not escalate, so run:\n"
		        "\n"
		        "    sudo sh -c 'echo detect > %s'\n"
		        "\n"
		        "Note that a re-probe has NOT recovered this panel in testing:\n"
		        "on 2026-08-10 six probes over six seconds failed, and what did\n"
		        "bring DP-3 back was a suspend/resume cycle.\n",
		        path, strerror(errno), path);
		return 1;
	}

	if (fputs("detect\n", f) == EOF) {
		fprintf(stderr, "syn-settings: write to %s failed\n", path);
		fclose(f);
		return 1;
	}
	if (fclose(f) != 0) {
		/* sysfs reports a rejected value at close, not at write. Checking
		 * only the write would report success for a store the kernel
		 * refused. */
		fprintf(stderr, "syn-settings: the kernel refused the probe on %s\n", dir);
		return 1;
	}

	/* Report the state AFTER, because "the probe ran" is not the question
	 * anyone is asking. */
	char st[64] = "unknown", spath[576];
	snprintf(spath, sizeof spath, "%s/status", dir);
	char buf[64];
	if (read_line_file(spath, buf, sizeof buf)) snprintf(st, sizeof st, "%s", buf);

	printf("probed %s — status is now %s\n", dir, st);
	return strcmp(st, "connected") == 0 ? 0 : 1;
}
