/* syn-settings — the System pane.
 *
 * Deliberately NOT a second `fetch`. fetch answers "what is this machine";
 * this answers "where does its configuration actually live", which is the
 * question that keeps costing time on this OS: a setting can be in
 * /usr/lib (shipped), /etc (an override that WINS over /usr/lib), or a
 * user's rc that an upgrade never touches, and the same name in two of those
 * places behaves very differently.
 *
 * Every path is printed with whether it exists and which layer it is, so
 * "systemd's /etc drop-in is shadowing the one we ship" is visible instead of
 * being rediscovered.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>

struct cfg_path {
	const char *path;
	const char *layer;
	const char *what;
};

static const struct cfg_path paths[] = {
	{ "/usr/lib/sysctl.d/90-synapse-hardening.conf", "shipped",
	  "kernel hardening baseline" },
	{ "/etc/sysctl.d",                                "override",
	  "anything here WINS over the shipped baseline" },
	{ "/usr/lib/modprobe.d/90-synapse-blacklist.conf","shipped",
	  "refused kernel modules" },
	{ "/usr/lib/modprobe.d/synapse.conf",             "shipped",
	  "synapse_kmod parameters — the C defaults are NOT what loads" },
	{ "/etc/lynis/custom.prf",                        "override",
	  "audit decisions we have already made" },
	{ "/etc/synapd/synapd.conf",                      "override",
	  "AI daemon" },
	{ "/etc/xdg/synui/synuirc",                       "override",
	  "compositor rc — an upgrade never rewrites a user's copy" },
	{ "/usr/share/applications/mimeapps.list",        "shipped",
	  "the vendor default application list; this is what WINS" },
};

int pane_system(void)
{
	rec_header("kind\tkey\tvalue\tdetail");

	/* ── Identity ─────────────────────────────────────────────────────── */
	/* Sized to the destination it feeds, not to the file's line length:
	 * a scratch buffer wider than `pretty` is a truncation warning that is
	 * harmless and still worth not having. */
	char buf[256];
	FILE *f = fopen("/etc/os-release", "re");
	char pretty[256] = "unknown";
	if (f) {
		while (fgets(buf, sizeof buf, f)) {
			if (strncmp(buf, "PRETTY_NAME=", 12) == 0) {
				char *v = buf + 12;
				buf[strcspn(buf, "\n")] = '\0';
				if (*v == '"') {
					v++;
					char *q = strrchr(v, '"');
					if (q) *q = '\0';
				}
				tsv_clean(v);
				snprintf(pretty, sizeof pretty, "%s", v);
				break;
			}
		}
		fclose(f);
	}
	rec_row("about\tos\t%s\t/etc/os-release", pretty);

	struct utsname u;
	if (uname(&u) == 0) {
		rec_row("about\tkernel\t%s %s\trunning kernel", u.sysname, u.release);
		rec_row("about\thostname\t%s\t/etc/hostname", u.nodename);
	}

	if (read_line_file("/proc/uptime", buf, sizeof buf)) {
		double secs = atof(buf);
		long h = (long)(secs / 3600), m = ((long)secs % 3600) / 60;
		rec_row("about\tuptime\t%ldh %ldm\tsince boot", h, m);
	}

	/* ── Where configuration lives ────────────────────────────────────── */
	for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
		const char *state = access(paths[i].path, F_OK) == 0 ? "present" : "absent";
		rec_row("config\t%s\t%s (%s)\t%s",
		        paths[i].path, state, paths[i].layer, paths[i].what);
	}

	return 0;
}
