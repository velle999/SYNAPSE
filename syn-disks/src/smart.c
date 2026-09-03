/* smart.c — drive health, from smartctl.
 *
 * Not reimplemented, and not guessed at. SMART is a per-vendor mess of
 * attribute IDs whose raw values mean different things on different firmware;
 * smartmontools carries a database of those quirks that is decades old, and a
 * disk utility that parsed the raw registers itself would be confidently wrong
 * about exactly the drives that are failing.
 *
 * ── Why this is the one thing that asks for privilege ───────────────────────
 *
 * SMART lives behind an ioctl on the raw device, and a desktop user cannot
 * open /dev/sda. So `smart` is the only command here that can need root, and
 * it asks through pkexec only when given --elevate.
 *
 * That flag exists so the GUI does not pop an authentication dialogue at
 * somebody who opened a window to see how full a disk was. Health is read when
 * it is asked for, by a button that says so.
 *
 * ── Text, not JSON ─────────────────────────────────────────────────────────
 *
 * smartctl has --json, and using it would mean carrying a JSON parser in a
 * program whose entire dependency list is libc. The plain output is stable,
 * line-oriented and already what every script in the world parses.
 *
 * Absence is reported as absence. With smartmontools not installed there is no
 * health row — never a cheerful "OK" inferred from nothing, because "this
 * program could not tell" and "this drive is fine" must not look the same.
 *
 * SYN_DISKS_SMARTCTL points at a fake for the test suite, which is how the
 * parsing below is exercised against SATA, NVMe and failing-drive output on a
 * machine that has none of them.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-disks.h"
#include "i18n.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char *smart_tool(void)
{
	const char *env = getenv("SYN_DISKS_SMARTCTL");
	return (env && *env) ? env : "smartctl";
}

/*
 * One `field <TAB> value` row, or one aligned line for a person.
 *
 * ⛔ TRANSLATED AT THE DRAW SITE, NEVER IN THE ROW. data/syn-disks.qml DRAWS
 * both of these and MATCHES on both — `field === "retry"` decides whether a row
 * is a row at all, and `value === "FAILING"` is what turns the text red. So the
 * record carries the English word and the window looks it up when it paints;
 * the call sites mark theirs N_() so a translator sees them.
 */
static void row(const char *key, const char *val)
{
	if (!val || !*val)
		return;
	if (g_out == OUT_REC)
		rec_row(2, key, val);
	else
		printf("  %s%-22s%s %s\n", C_DIM(), _(key), C_RESET(), _(val));
}

/* Text after "<prefix>:" on the line that starts with it, trimmed. NULL if the
 * line is not there — which is the normal case for half of these on any given
 * drive, since SATA and NVMe report different things. */
static char *field_after(char **lines, size_t n, const char *prefix)
{
	size_t plen = strlen(prefix);
	for (size_t i = 0; i < n; i++) {
		if (strncmp(lines[i], prefix, plen))
			continue;
		const char *colon = strchr(lines[i] + plen, ':');
		if (!colon)
			continue;
		char *v = xstrdup(colon + 1);
		return trim(v);
	}
	return NULL;
}

/* The RAW_VALUE column of one SATA attribute row.
 *
 * The table has ten columns, aligned with runs of spaces:
 *
 *   ID# ATTRIBUTE_NAME FLAG VALUE WORST THRESH TYPE UPDATED WHEN_FAILED RAW_VALUE
 *     5 Reallocated_Sector_Ct 0x0033 001 001 010 Pre-fail Always FAILING_NOW 2144
 *   194 Temperature_Celsius   0x0022 030 045 000 Old_age  Always -           30 (Min/Max 25/45)
 *
 * ⚠ THE RAW VALUE IS COLUMN TEN, not the last field.
 *
 * "Last non-empty field" is the obvious reading and it is wrong twice over.
 * WHEN_FAILED is "-" on a healthy drive and "FAILING_NOW" on a dying one, so
 * counting from the RIGHT shifts by a column exactly when the answer matters;
 * and several attributes append parenthesised detail, so the last field of the
 * temperature row above is "25/45)" — which truncates to 25, a plausible
 * temperature that is not the drive's. It reported the low-water mark as the
 * current reading and nothing looked wrong.
 *
 * So: index the tenth non-empty field, and take its leading integer.
 *
 * The name is matched against column TWO rather than anywhere in the line, or
 * an attribute whose name contains another's would answer for it. */
static char *attr_raw(char **lines, size_t n, const char *name)
{
	for (size_t i = 0; i < n; i++) {
		if (!strstr(lines[i], name))
			continue;

		char *copy = xstrdup(lines[i]);
		size_t nf = 0;
		char **f = split(copy, ' ', &nf);

		/* split() keeps the empty strings produced by runs of spaces, so
		 * the columns are the non-empty fields in order. */
		char *cols[12];
		size_t nc = 0;
		for (size_t j = 0; j < nf && nc < 12; j++)
			if (*f[j])
				cols[nc++] = f[j];

		char *val = NULL;
		if (nc >= 10 && !strcmp(cols[1], name))
			val = xstrdup(cols[9]);

		free(f);
		free(copy);

		if (val) {
			for (char *p = val; *p; p++)
				if (!isdigit((unsigned char)*p)) {
					*p = '\0';
					break;
				}
			if (*val)
				return val;
			free(val);
		}
	}
	return NULL;
}

int cmd_smart(int argc, char **argv)
{
	const char *target = NULL;
	bool elevate = false;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--elevate"))  elevate = true;
		else if (argv[i][0] == '-')         die(_("smart: unknown option '%s'"), argv[i]);
		else if (!target)                   target = argv[i];
		else die(_("smart: one device at a time"));
	}

	if (!target)
		die(_("smart: need a disk (see: syn-disks list)"));

	char *k = sd_kernel_name(target);
	if (!k)
		die(_("%s: not a block device"), target);
	/* SMART is a property of the DRIVE. Asked about a partition, answer about
	 * the disk it is on rather than failing — that is what was meant. */
	if (sd_is_partition(k)) {
		char *parent = sd_parent_disk(k);
		if (parent) {
			free(k);
			k = parent;
		}
	}
	char *dev = xasprintf("/dev/%s", k);

	const char *tool = smart_tool();
	if (!have_cmd(tool)) {
		if (g_out == OUT_REC) {
			rec_row(2, "field", "value");
			rec_row(2, N_("health"), N_("unavailable"));
			rec_row(2, N_("reason"), N_("smartmontools is not installed"));
		} else {
			fprintf(stderr, _("syn-disks: %s is not installed — "
			        "install smartmontools to read drive health\n"), tool);
		}
		free(dev);
		free(k);
		return 3;
	}

	char *cmd[8];
	int n = 0;
	if (elevate && !getenv("SYN_DISKS_NO_PKEXEC")) {
		const char *pk = getenv("SYN_DISKS_PKEXEC");
		cmd[n++] = (char *)((pk && *pk) ? pk : "pkexec");
	}
	cmd[n++] = (char *)tool;
	cmd[n++] = (char *)"-H";   /* overall assessment */
	cmd[n++] = (char *)"-A";   /* the attribute table */
	cmd[n++] = (char *)"-i";   /* identity, for the rotation rate */
	cmd[n++] = dev;
	cmd[n] = NULL;

	int st = 0;
	char *out = run_capture(cmd, &st, false);

	/* smartctl's exit status is a BITMASK, not a success/failure code: bit 0
	 * means the command line was wrong, bit 1 means the device could not be
	 * opened, and bits 3-7 are findings about the drive. A failing disk
	 * therefore exits non-zero with a perfectly good report attached, and
	 * treating that as an error would hide the one result worth seeing. */
	bool could_not_open = (st & 0x02) != 0;

	size_t nlines = 0;
	char **lines = split(out, '\n', &nlines);

	if (could_not_open || (st & 0x01)) {
		bool denied = strstr(out, "Permission denied") != NULL
		           || strstr(out, "requires root") != NULL;
		if (g_out == OUT_REC) {
			rec_row(2, "field", "value");
			rec_row(2, N_("health"), N_("unavailable"));
			/* ⚠ lines[0] is smartctl's OWN text and is not ours to mark. */
			rec_row(2, N_("reason"), denied && !elevate
			        ? N_("reading health needs authorisation")
			        : (*out ? lines[0]
			                : N_("smartctl could not open the device")));
			/* The GUI turns this into the button it should offer next,
			 * rather than deciding for itself what a refusal meant. */
			rec_row(2, "retry", denied && !elevate ? "elevate" : "");
		} else {
			fprintf(stderr, "%s%s%s\n", C_BAD(),
			        *out ? lines[0] : "smartctl could not open the device",
			        C_RESET());
			if (denied && !elevate)
				fprintf(stderr, _("  try: syn-disks smart --elevate %s\n"), dev);
		}
		free(lines);
		free(out);
		free(dev);
		free(k);
		return 3;
	}

	if (g_out == OUT_REC)
		rec_row(2, "field", "value");
	else
		printf("%s%s%s\n", C_ACCENT(), dev, C_RESET());

	/* SATA says "SMART overall-health self-assessment test result: PASSED".
	 * NVMe has no such line and reports "Critical Warning: 0x00" instead, so
	 * both are checked and the verdict is normalised to one word the GUI can
	 * colour without knowing which kind of drive it is looking at. */
	char *overall = field_after(lines, nlines,
	                            "SMART overall-health self-assessment test result");
	char *crit = field_after(lines, nlines, "Critical Warning");

	const char *verdict = N_("unknown");
	if (overall)
		/* ⚠ THE WINDOW TURNS THE ROW RED ON `value === "FAILING"`, so this
		 * word is matched as well as drawn — N_() and not _(). */
		verdict = strstr(overall, "PASS") ? N_("healthy") : N_("FAILING");
	else if (crit)
		verdict = (!strcmp(crit, "0x00") || !strcmp(crit, "0")) ? "healthy" : "FAILING";

	row(N_("health"), verdict);

	/* SATA calls it "Device Model", NVMe calls it "Model Number". */
	char *model = field_after(lines, nlines, "Device Model");
	if (!model)
		model = field_after(lines, nlines, "Model Number");
	row(N_("model"), model);
	free(model);

	char *temp = field_after(lines, nlines, "Temperature");
	if (!temp)
		temp = attr_raw(lines, nlines, "Temperature_Celsius");
	if (temp && *temp) {
		char *t = strchr(temp, ' ') ? xstrndup(temp, strcspn(temp, " ")) : xstrdup(temp);
		char *shown = xasprintf("%s °C", t);
		row(N_("temperature"), shown);
		free(shown);
		free(t);
	}
	free(temp);

	char *hours = field_after(lines, nlines, "Power On Hours");
	if (!hours)
		hours = attr_raw(lines, nlines, "Power_On_Hours");
	if (hours && *hours) {
		/* Hours are how the drive counts; years are how anybody decides
		 * whether to trust it. Both, because the raw number alone means
		 * nothing to most people looking at this window. */
		double years = strtod(hours, NULL) / 24.0 / 365.25;
		char *shown = years >= 0.5
		            ? xasprintf(_("%s (about %.1f years)"), hours, years)
		            : xstrdup(hours);
		row(N_("powered on (hours)"), shown);
		free(shown);
	}
	free(hours);

	char *cycles = field_after(lines, nlines, "Power Cycles");
	if (!cycles)
		cycles = attr_raw(lines, nlines, "Power_Cycle_Count");
	row(N_("power cycles"), cycles);
	free(cycles);

	char *wear = field_after(lines, nlines, "Percentage Used");
	row(N_("wear used"), wear);
	free(wear);

	char *written = field_after(lines, nlines, "Data Units Written");
	row(N_("data written"), written);
	free(written);

	/* The three counters that actually predict a failing SATA drive. A
	 * non-zero reallocated or pending count is the thing to notice, so they
	 * are reported even when zero — a row that only appears once it is bad
	 * cannot be checked for being good. */
	static const struct { const char *attr; const char *label; } COUNTERS[] = {
		{ "Reallocated_Sector_Ct", N_("reallocated sectors") },
		{ "Current_Pending_Sector", N_("pending sectors") },
		{ "Offline_Uncorrectable", N_("uncorrectable sectors") },
	};
	for (size_t i = 0; i < sizeof COUNTERS / sizeof *COUNTERS; i++) {
		char *v = attr_raw(lines, nlines, COUNTERS[i].attr);
		if (v)
			row(COUNTERS[i].label, v);
		free(v);
	}

	char *rot = field_after(lines, nlines, "Rotation Rate");
	row(N_("rotation rate"), rot);
	free(rot);

	free(crit);
	free(overall);
	free(lines);
	free(out);
	free(dev);
	free(k);
	return strcmp(verdict, "FAILING") ? 0 : 1;
}
