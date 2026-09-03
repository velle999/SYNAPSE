/* syn-settings — the Power & Sleep pane.
 *
 * This pane exists because of a specific failure. On 2026-07-30 a reinstall
 * left the NVIDIA sleep units present but DISABLED, and the symptom was a
 * black screen after resume with nothing in any log saying a unit had not run
 * (project_synapse_nvidia_sleep_units_reinstall_regression). A unit that is
 * installed and disabled looks exactly like a unit that is working, right up
 * until you suspend.
 *
 * So the pane lists them by name with their real enablement state, rather than
 * offering a "power" abstraction that hides which of them is missing.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"
#include "i18n.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct unit_row {
	const char *unit;
	const char *what;
};

/* Named individually and on purpose. A glob over nvidia-*.service would look
 * tidier and would not have caught the 07-30 regression, because the units
 * were all THERE — the question is enabled-vs-disabled, one at a time. */
static const struct unit_row units[] = {
	{ "nvidia-suspend.service",     N_("runs NVIDIA's suspend hook") },
	{ "nvidia-resume.service",      N_("runs NVIDIA's resume hook") },
	{ "nvidia-hibernate.service",   N_("runs NVIDIA's hibernate hook") },
	{ "nvidia-persistenced.service",N_("keeps the driver loaded with no client") },
	{ "synapse-drm-watch.service",  N_("records connector state changes") },
	{ "synapd.service",             N_("the AI daemon; holds GPU memory") },
	{ "synguard.service",           N_("the intrusion monitor") },
	{ "synnet.service",             N_("network policy and the firewall set") },
};

/* Is this unit absent from the machine?
 *
 * ⛔ TWO SPELLINGS, AND ONE OF THEM IS THE ONLY ONE THAT HAPPENS. systemd
 * prints "not-found" for `is-enabled` on a unit it does not have; the empty
 * output this file's "not installed" sentinel was written for comes from an
 * older systemd, or from systemctl failing outright. So the check that was
 * meant to hide the button for a missing unit never fired: every absent unit
 * was offered Enable/Start, which does nothing and reports success at having
 * done it — a dead button in the app whose whole job is showing true state.
 */
static int unit_absent(const char *en)
{
	return !strcmp(en, "not installed") || !strcmp(en, "not-found") ||
	       !strcmp(en, "not-found\n");
}

static void unit_state(const char *unit, char *en, size_t en_cap,
                       char *act, size_t act_cap)
{
	char out[128] = "";
	char *is_en[] = { (char *)"systemctl", (char *)"is-enabled", (char *)unit, NULL };
	char *is_act[] = { (char *)"systemctl", (char *)"is-active", (char *)unit, NULL };

	/* systemctl exits non-zero for "disabled" and for "no such unit" alike,
	 * so the STATUS is not the answer — the word it prints is. An absent unit
	 * prints nothing at all, which is how they are told apart. */
	run_capture(is_en, out, sizeof out);
	out[strcspn(out, "\n")] = '\0';
	tsv_clean(out);
	snprintf(en, en_cap, "%s", out[0] ? out : "not installed");

	out[0] = '\0';
	run_capture(is_act, out, sizeof out);
	out[strcspn(out, "\n")] = '\0';
	tsv_clean(out);
	snprintf(act, act_cap, "%s", out[0] ? out : "-");
}

int pane_power(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	/* ── Sleep-critical units ─────────────────────────────────────────── */
	if (have_cmd("systemctl")) {
		for (size_t i = 0; i < sizeof units / sizeof units[0]; i++) {
			char en[64], act[64], unit_action[128];
			unit_state(units[i].unit, en, sizeof en, act, sizeof act);
			snprintf(unit_action, sizeof unit_action, "unit:%s", units[i].unit);
			/* Only a unit that EXISTS can be acted on. Offering
			 * Enable on something systemd has never heard of is a
			 * button whose only outcome is an error. */
			rec_row("unit\t%s\t%s\t%s\t%s\t%s",
			        units[i].unit, en, act, units[i].what,
			        !unit_absent(en) ? unit_action : "-");
		}
	} else {
		rec_row("unit\t-\t%s\t-\t%s\t-",
		        N_("unknown"), N_("systemctl not available"));
	}

	/* ── Sleep hooks on disk ──────────────────────────────────────────── */
	/*
	 * ENUMERATED, not a hardcoded list, and that is not a style preference.
	 * The first draft of this pane named the hooks it expected — and reported
	 * "synapd-sleep: absent" on a machine where the hook was present, running,
	 * and had logged during the suspend twenty minutes earlier. Its real name
	 * is synapd-sleep-hook. A settings app whose whole justification is
	 * showing true state cannot afford to answer from a list of guesses, and
	 * a hardcoded name is a guess that rots silently.
	 *
	 * A system-sleep hook has no enablement to read: it either exists and is
	 * executable, or it does nothing and says nothing. That silence is why
	 * these are worth listing at all. systemd scans both directories and a
	 * file in /etc shadows the same name under /usr/lib, so both are shown
	 * with their layer rather than merged.
	 */
	static const char *hookdirs[] = {
		"/etc/systemd/system-sleep",       /* override — wins on a name clash */
		"/usr/lib/systemd/system-sleep",   /* shipped */
	};
	for (size_t d = 0; d < sizeof hookdirs / sizeof hookdirs[0]; d++) {
		DIR *dir = opendir(hookdirs[d]);
		if (!dir) continue;

		struct dirent *e;
		while ((e = readdir(dir))) {
			if (e->d_name[0] == '.') continue;

			char full[512];
			if (snprintf(full, sizeof full, "%s/%s", hookdirs[d], e->d_name)
			    >= (int)sizeof full)
				continue;

			struct stat st;
			if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;

			/* Not executable is the interesting state: systemd skips it
			 * without a word, so the hook is installed and inert. */
			const char *state = (st.st_mode & S_IXUSR) ? "executable"
			                                           : "NOT executable";
			rec_row("hook\t%s\t%s\t%s\t%s\t-", e->d_name, state,
			        d == 0 ? "override" : N_("shipped"), hookdirs[d]);
		}
		closedir(dir);
	}

	/* ── What the last suspend actually did ───────────────────────────── */
	/* The single most useful number on this pane and the hardest to get by
	 * hand: how long the machine was away. Every DP-3 failure so far has been
	 * on a multi-hour suspend and never on a short one, and until this line
	 * existed that correlation had to be rebuilt from two journal greps every
	 * time. */
	if (have_cmd("journalctl")) {
		char out[65536] = "";
		char *argv[] = { (char *)"journalctl", (char *)"-b", (char *)"-o",
		                 (char *)"short-iso", (char *)"-g",
		                 (char *)"PM: suspend (entry|exit)", (char *)"--no-pager",
		                 NULL };
		run_capture(argv, out, sizeof out);

		/* Last entry and last exit, by scanning to the end. */
		char last_entry[64] = "", last_exit[64] = "";
		for (char *p = out; p && *p; ) {
			char *eol = strchr(p, '\n');
			if (eol) *eol = '\0';
			if (strstr(p, "suspend entry")) {
				snprintf(last_entry, sizeof last_entry, "%.25s", p);
			} else if (strstr(p, "suspend exit")) {
				snprintf(last_exit, sizeof last_exit, "%.25s", p);
			}
			p = eol ? eol + 1 : NULL;
		}
		rec_row("sleep\t%s\t%s\t-\t%s\t-",
		        N_("last-suspend"), last_entry[0] ? last_entry : N_("none this boot"),
		        N_("when this boot last went to sleep"));
		rec_row("sleep\t%s\t%s\t-\t%s\t-",
		        N_("last-resume"), last_exit[0] ? last_exit : N_("none this boot"),
		        N_("when it came back"));
	}

	return 0;
}
