/* syn-settings — the Display pane.
 *
 * Two sources, because they answer different questions and disagree in a way
 * that is itself the finding:
 *
 *   synctl outputs   what the COMPOSITOR is driving — logical position, size,
 *                    scale. If a head is missing here it is not on your desk.
 *   /sys/class/drm   what the KERNEL sees — connector status, whether a CRTC
 *                    is still assigned, and the EDID the sink handed over.
 *
 * A connector that reads `enabled` with `status=disconnected` is a CRTC still
 * assigned to a head with no sink answering, which is the exact signature of
 * the DP-3 failure this OS has been chasing. Showing both columns side by side
 * is the whole point of the pane: "synui lists two monitors, the kernel lists
 * three, one of them has a zero-byte EDID" is a diagnosis you can read.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* EDID size measured by CONTENT.
 *
 * `stat` on a sysfs binary attribute reports its ALLOCATED size, which is 0
 * for every DRM `edid` whether the sink handed one over or not. That mistake
 * lived in synapse-drm-watch and synapse-gpu-sleep until 2026-08-10 and made
 * "0-byte EDID" look like evidence when it was a constant. Read it. */
static long edid_bytes(const char *dir)
{
	char path[512];
	if (snprintf(path, sizeof path, "%s/edid", dir) >= (int)sizeof path)
		return -1;

	FILE *f = fopen(path, "rbe");
	if (!f) return -1;

	long n = 0;
	char buf[256];
	size_t got;
	while ((got = fread(buf, 1, sizeof buf, f)) > 0) n += (long)got;
	fclose(f);
	return n;
}

static void sysfs_field(const char *dir, const char *name, char *out, size_t cap)
{
	char path[512];
	snprintf(out, cap, "unknown");
	if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path)
		return;
	/* Sized to the destination, not larger. A scratch buffer wider than
	 * `cap` is a truncation warning that is technically harmless and still
	 * worth not having: the compiler cannot know these values are short, and
	 * a warning everyone has learned to scroll past is a warning that will
	 * hide the next real one. */
	char buf[64];
	if (read_line_file(path, buf, sizeof buf)) {
		tsv_clean(buf);
		snprintf(out, cap, "%s", buf);
	}
}

/* A deliberately small scanner over `synctl outputs`, whose shape is fixed:
 * a flat array of objects with "name", "at":[x,y], "size":[w,h], "scale".
 * Pulling in a JSON library to read four scalars out of our own compositor's
 * own output would be the larger mistake. If synctl ever nests anything here,
 * this returns 0 and the pane falls back to "-" rather than lying. */
static int synctl_lookup(const char *json, const char *name,
                         char *at, size_t at_cap, char *size, size_t size_cap,
                         char *scale, size_t scale_cap)
{
	snprintf(at, at_cap, "-");
	snprintf(size, size_cap, "-");
	snprintf(scale, scale_cap, "-");
	if (!json || !*json) return 0;

	char needle[128];
	if (snprintf(needle, sizeof needle, "\"name\":\"%s\"", name) >= (int)sizeof needle)
		return 0;

	const char *p = strstr(json, needle);
	if (!p) return 0;

	/* Bounded to this object, so a missing key cannot read the next head's. */
	const char *end = strchr(p, '}');
	if (!end) end = p + strlen(p);

	int x, y, w, h;
	double sc;
	const char *q;

	if ((q = strstr(p, "\"at\":[")) && q < end && sscanf(q + 6, "%d,%d", &x, &y) == 2)
		snprintf(at, at_cap, "%d,%d", x, y);
	if ((q = strstr(p, "\"size\":[")) && q < end && sscanf(q + 8, "%d,%d", &w, &h) == 2)
		snprintf(size, size_cap, "%dx%d", w, h);
	if ((q = strstr(p, "\"scale\":")) && q < end && sscanf(q + 8, "%lf", &sc) == 1)
		snprintf(scale, scale_cap, "%.2f", sc);
	return 1;
}

/* The connector's preferred mode: the first line of sysfs `modes`, which the
 * kernel lists best-first. Not necessarily what is being SCANNED OUT — that
 * belongs to the compositor and comes from synctl — so it is labelled
 * "preferred" rather than "mode". */
static void preferred_mode(const char *dir, char *out, size_t cap)
{
	char path[512];
	snprintf(out, cap, "-");
	if (snprintf(path, sizeof path, "%s/modes", dir) >= (int)sizeof path)
		return;
	char buf[64];
	if (read_line_file(path, buf, sizeof buf) && buf[0]) {
		tsv_clean(buf);
		snprintf(out, cap, "%s", buf);
	}
}

static int name_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

int pane_display(void)
{
	char json[8192] = "";
	char *synctl_argv[] = { (char *)"synctl", (char *)"outputs", NULL };
	run_capture(synctl_argv, json, sizeof json);

	DIR *d = opendir("/sys/class/drm");
	if (!d) {
		rec_header("connector\tstatus\tcrtc\tpreferred\tedid\tcompositor\tposition\tscale\taction");
		rec_row("-\tno /sys/class/drm\t-\t-\t-\t-\t-\t-\t-");
		return 1;
	}

	/* Collected and sorted rather than printed as readdir hands them over:
	 * directory order is arbitrary, and a settings pane whose rows move
	 * between refreshes is one nobody can read a change out of. */
	char *names[64];
	size_t n = 0;
	struct dirent *e;
	while ((e = readdir(d)) && n < sizeof names / sizeof names[0]) {
		/* card<N>-<CONNECTOR>. The bare "cardN" and "renderD*" nodes are
		 * devices, not heads, and have no status to report. */
		if (strncmp(e->d_name, "card", 4) != 0) continue;
		if (!strchr(e->d_name, '-')) continue;
		names[n] = strdup(e->d_name);
		if (names[n]) n++;
	}
	closedir(d);
	qsort(names, n, sizeof names[0], name_cmp);

	rec_header("connector\tstatus\tcrtc\tpreferred\tedid\tcompositor\tposition\tscale\taction");

	for (size_t i = 0; i < n; i++) {
		char dir[512];
		snprintf(dir, sizeof dir, "/sys/class/drm/%s", names[i]);

		char status[64], enabled[64], mode[64];
		sysfs_field(dir, "status", status, sizeof status);
		sysfs_field(dir, "enabled", enabled, sizeof enabled);
		preferred_mode(dir, mode, sizeof mode);
		long edid = edid_bytes(dir);

		/* synctl names a head "DP-3"; sysfs calls it "card1-DP-3". */
		const char *shortname = strchr(names[i], '-');
		shortname = shortname ? shortname + 1 : names[i];

		char at[64], size[64], scale[32];
		int driven = synctl_lookup(json, shortname, at, sizeof at,
		                           size, sizeof size, scale, sizeof scale);

		char edidbuf[32];
		if (edid < 0) snprintf(edidbuf, sizeof edidbuf, "-");
		else          snprintf(edidbuf, sizeof edidbuf, "%ld", edid);

		/* Offer a re-probe only for the signature that has one: a CRTC
		 * still assigned to a head with no sink answering. A port with
		 * nothing plugged in reads enabled=disabled and must not sprout
		 * a button — probing it would be asking the driver to look for
		 * a monitor nobody claimed was there. */
		char action[128] = "-";
		if (!strcmp(status, "disconnected") && !strcmp(enabled, "enabled"))
			snprintf(action, sizeof action, "probe:%s", shortname);
		else if (driven && !strcmp(status, "connected"))
			/* A mode can only be set on a head the compositor is actually
			 * driving. Offering it for a connector the kernel sees but synui
			 * is not scanning out to would be a picker whose every choice is
			 * refused. */
			snprintf(action, sizeof action, "mode:%s", shortname);

		rec_row("%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s",
		        shortname, status, enabled, mode, edidbuf,
		        driven ? size : "not driven", at, scale, action);
		free(names[i]);
	}
	return 0;
}
