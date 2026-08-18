/* syn-settings — display modes.
 *
 * wlr-randr does the work. synui implements wlr-output-management, and
 * output_persist.c saves whatever a client sets to ~/.config/synui/outputs.conf
 * and restores it on the next hotplug or boot — so setting a mode through
 * wlr-randr is both applied live AND remembered, with no file for this app to
 * write and no second implementation to disagree with the compositor.
 *
 * Writing outputs.conf directly was the alternative and it is worse in the way
 * that matters: it is applied on new_output, so nothing would happen until the
 * monitor was unplugged or the machine rebooted. A resolution setting that
 * takes effect at some unspecified later time is one nobody trusts.
 *
 * No privilege is involved. This is a Wayland client talking to the compositor
 * on the user's own session.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <stdlib.h>
#include <string.h>

/* Accept only what wlr-randr's own mode syntax allows: WIDTHxHEIGHT with an
 * optional @REFRESH. Checked because this string reaches a command line, and
 * because a typo should be refused here with a readable message rather than
 * by wlr-randr with a usage dump. */
static int sane_mode(const char *m)
{
	if (!m || !*m) return 0;

	int digits = 0, x = 0, at = 0;
	for (const char *p = m; *p; p++) {
		if (*p >= '0' && *p <= '9') { digits++; continue; }
		if (*p == 'x' && !at) { if (x++) return 0; continue; }
		if (*p == '@') { if (at++ || !x) return 0; continue; }
		if (*p == '.' && at) continue;   /* 143.998 Hz */
		return 0;
	}
	return x == 1 && digits >= 4;
}

/* An output name as the kernel and wlr-randr spell them: DP-3, HDMI-A-1,
 * eDP-1. Checked for SHAPE here so validation stays independent of whether a
 * compositor is running; whether the output actually exists is checked later,
 * against wlr-randr, and only when something is really going to be set. */
static int sane_output(const char *o)
{
	if (!o || !*o || *o == '-') return 0;
	for (const char *p = o; *p; p++) {
		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		    (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')
			continue;
		return 0;
	}
	return 1;
}

/* wlr-randr's listing indents modes under their output:
 *
 *   DP-3 "Goldstar Company Ltd 27GL850 ..."
 *     Modes:
 *       2560x1440 px, 143.998000 Hz (preferred, current)
 *
 * so a mode line is recognised by its indent and its "px," rather than by
 * counting lines from the header, which would break the moment wlr-randr adds
 * a field.
 */
int do_modes(int argc, char **argv)
{
	if (argc < 1) {
		fprintf(stderr, "syn-settings: modes needs a connector, e.g. DP-3\n");
		return 2;
	}
	if (!have_cmd("wlr-randr")) {
		fprintf(stderr, "syn-settings: wlr-randr is not installed — "
		                "it is what applies a mode and makes synui remember it\n");
		return 1;
	}

	/* wlr-output-management is a wlroots protocol. synui implements it; KWin
	 * and mutter do not, so under KDE or GNOME wlr-randr connects to a real
	 * Wayland session and still lists nothing. Asking "is this a Wayland
	 * session?" is the wrong question there, and it is one this can answer
	 * itself — so it names the desktop that is in the way instead. */
	char out[65536] = "";
	char *a[] = { (char *)"wlr-randr", NULL };
	if (run_capture(a, out, sizeof out) != 0 || !out[0]) {
		const char *d = syn_session_desktop();
		if (*d && strcmp(d, "synui"))
			fprintf(stderr, "syn-settings: %s does not implement "
			                "wlr-output-management — modes can only be set "
			                "from a synui session\n", d);
		else
			fprintf(stderr, "syn-settings: wlr-randr reported nothing — "
			                "is this a Wayland session?\n");
		return 1;
	}

	int inside = 0, printed = 0;
	for (char *line = out, *eol; line && *line; line = eol) {
		eol = strchr(line, '\n');
		if (eol) *eol++ = '\0';

		/* A new output header is unindented. */
		if (line[0] != ' ' && line[0] != '\t') {
			char name[128] = "";
			sscanf(line, "%127s", name);
			inside = (strcmp(name, argv[0]) == 0);
			continue;
		}
		if (!inside) continue;
		if (!strstr(line, "px,")) continue;

		char mode[64] = "";
		double hz = 0;
		/* "      2560x1440 px, 143.998000 Hz (preferred, current)" */
		if (sscanf(line, " %63s px, %lf Hz", mode, &hz) < 1) continue;
		if (!sane_mode(mode)) continue;

		/* Refresh included, because two entries of the same size at 60 and 144
		 * are the whole reason somebody opened this list. Rounded: wlr-randr
		 * accepts 143.998 and nobody wants to read it. */
		if (hz > 0) printf("%s@%.0f%s\n", mode, hz,
		                   strstr(line, "current") ? "\tcurrent" : "");
		else        printf("%s%s\n", mode,
		                   strstr(line, "current") ? "\tcurrent" : "");
		printed++;
	}

	if (!printed) {
		fprintf(stderr, "syn-settings: no modes listed for '%s'\n", argv[0]);
		return 1;
	}
	return 0;
}

int do_mode(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "syn-settings: mode needs a connector and a mode, "
		                "e.g. DP-3 2560x1440@144\n");
		return 2;
	}
	const char *conn = argv[0], *mode = argv[1];

	if (!sane_mode(mode)) {
		fprintf(stderr, "syn-settings: '%s' is not a mode — "
		                "expected WIDTHxHEIGHT or WIDTHxHEIGHT@REFRESH\n", mode);
		return 2;
	}
	if (!sane_output(conn)) {
		fprintf(stderr, "syn-settings: '%s' is not an output name\n", conn);
		return 2;
	}

	/* --dry-run stops HERE, before anything looks at the session.
	 *
	 * It used to go on to query wlr-randr and check the output existed, which
	 * made a dry run depend on there being a Wayland session — so the package
	 * check() failed the moment wlr-randr became a hard dependency: present in
	 * the build environment, no compositor to talk to, every mode "refused".
	 * `--dry-run unit enable foo.service` does not verify the unit exists
	 * either; this now matches. Argument validation above is environment-free
	 * on purpose, which is exactly what a test can assert. */
	if (g_dry_run) {
		printf("would run: wlr-randr --output %s --mode %s\n", conn, mode);
		return 0;
	}

	/* A connector name is checked the same way probe checks it: against what
	 * wlr-randr actually reports, so a name that is not a real output never
	 * becomes an argument. */
	if (!have_cmd("wlr-randr")) {
		fprintf(stderr, "syn-settings: wlr-randr is not installed\n");
		return 1;
	}

	char list[65536] = "";
	char *la[] = { (char *)"wlr-randr", NULL };
	run_capture(la, list, sizeof list);

	int found = 0;
	for (char *line = list, *eol; line && *line; line = eol) {
		eol = strchr(line, '\n');
		if (eol) *eol++ = '\0';
		if (line[0] == ' ' || line[0] == '\t') continue;
		char name[128] = "";
		sscanf(line, "%127s", name);
		if (!strcmp(name, conn)) { found = 1; break; }
	}
	if (!found) {
		fprintf(stderr, "syn-settings: no such output '%s' — "
		                "`syn-settings --rec display` lists them\n", conn);
		return 2;
	}

	char *a[] = { (char *)"wlr-randr", (char *)"--output", (char *)conn,
	              (char *)"--mode", (char *)mode, NULL };

	int rc = run_quiet(a);
	if (rc != 0) {
		fprintf(stderr, "syn-settings: wlr-randr refused that mode "
		                "(the sink may not accept it)\n");
		return 1;
	}
	printf("%s set to %s — synui has saved it to outputs.conf\n", conn, mode);
	return 0;
}
