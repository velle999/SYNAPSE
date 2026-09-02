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
#include "i18n.h"

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
	  N_("kernel hardening baseline") },
	{ "/etc/sysctl.d",                                "override",
	  N_("anything here WINS over the shipped baseline") },
	{ "/usr/lib/modprobe.d/90-synapse-blacklist.conf","shipped",
	  N_("refused kernel modules") },
	{ "/usr/lib/modprobe.d/synapse.conf",             "shipped",
	  N_("synapse_kmod parameters — the C defaults are NOT what loads") },
	{ "/etc/lynis/custom.prf",                        "override",
	  N_("audit decisions we have already made") },
	{ "/etc/synapd/synapd.conf",                      "override",
	  N_("AI daemon") },
	{ "/etc/xdg/synui/synuirc",                       "override",
	  N_("compositor rc — an upgrade never rewrites a user's copy") },
	{ "/usr/share/applications/mimeapps.list",        "shipped",
	  N_("the vendor default application list; this is what WINS") },
};

int pane_system(void)
{
	/* ⚠ An `action` column, so the two rows that are SETTINGS can be set.
	 * Every other row here is a fact about the machine and says "-": the GUI
	 * decides what is editable purely from this column, so a fact with a verb
	 * on it would draw a button that writes somewhere nothing reads. */
	rec_header("kind\tkey\tvalue\tdetail\taction");

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
	rec_row("about\tos\t%s\t%s\t-",
	        pretty, N_("/etc/os-release"));

	struct utsname u;
	if (uname(&u) == 0) {
		rec_row("about\t%s\t%s %s\t%s\t-",
		        N_("kernel"), u.sysname, u.release, N_("running kernel"));
		/* ⚠ SETTABLE, and worth setting: every SynapseOS install is called
		 * `synapse`, so two of them on one network means Avahi renames one
		 * `synapse-2.local` — with no say in which one, and no promise the
		 * suffix is the same tomorrow. The row says so, because "hostname:
		 * synapse" on its own gives nobody a reason to change it. */
		rec_row("about\t%s\t%s\t%s\tset:hostname",
		        N_("hostname"), u.nodename,
		        N_("the name this machine answers to on the network — every SynapseOS install ships as `synapse`"));
	}

	if (read_line_file("/proc/uptime", buf, sizeof buf)) {
		double secs = atof(buf);
		long h = (long)(secs / 3600), m = ((long)secs % 3600) / 60;
		rec_row("about\t%s\t%ldh %ldm\t%s\t-",
		        N_("uptime"), h, m, N_("since boot"));
	}

	/* ── The lights ───────────────────────────────────────────────────── */
	//
	// The desktop already decides one colour per wallpaper; syn-rgb(1) is what
	// puts it on the RAM, the board and the keyboard. The row is here rather
	// than on a pane of its own because it is one switch, and it says whether
	// OpenRGB is installed rather than vanishing when it is not: a control
	// that disappears is a feature nobody finds out about.
	{
		int have = have_cmd("syn-rgb");
		int rgb  = have_cmd("openrgb");
		char on[16] = "off";
		char path[512];
		const char *home = getenv("HOME");
		const char *xdg  = getenv("XDG_CONFIG_HOME");

		if (xdg && *xdg) snprintf(path, sizeof path, "%s/synui/rgb.state", xdg);
		else if (home)   snprintf(path, sizeof path, "%s/.config/synui/rgb.state", home);
		else             path[0] = '\0';

		if (path[0]) {
			FILE *rf = fopen(path, "re");
			if (rf) {
				char line[128];
				while (fgets(line, sizeof line, rf))
					if (!strncmp(line, "on=", 3)) {
						/* ⚠ The FILE is the answer, not this program's idea
						 * of it: `syn-rgb on` in a terminal has to move this
						 * row, and a cached copy here would disagree the
						 * moment anything else wrote the state. */
						snprintf(on, sizeof on, "%s",
						         strncmp(line + 3, "yes", 3) == 0 ? "on" : "off");
					}
				fclose(rf);
			}
		}

		if (!have)
			rec_row("lighting\t%s\tunavailable\t%s\t-",
			        N_("rgb"), N_("syn-rgb is not installed"));
		else if (!rgb)
			rec_row("lighting\t%s\t%s\t%s\ttoggle:rgb",
			        N_("rgb"), on,
			        N_("the wallpaper accent on RGB hardware — install `openrgb` to use it"));
		else
			rec_row("lighting\t%s\t%s\t%s\ttoggle:rgb",
			        N_("rgb"), on,
			        N_("the wallpaper accent on the RAM, the board and the keyboard"));
	}

	/* ── Where configuration lives ────────────────────────────────────── */
	for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
		const char *state = access(paths[i].path, F_OK) == 0 ? "present" : "absent";
		rec_row("config\t%s\t%s (%s)\t%s\t-",
		        paths[i].path, state, paths[i].layer, paths[i].what);
	}

	return 0;
}
