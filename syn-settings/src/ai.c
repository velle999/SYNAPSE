/* syn-settings — the AI pane.
 *
 * This pane exists because AI was reachable from exactly one place: synui's
 * control panel. Install SynapseOS with KDE or GNOME — both are offered by the
 * installer — and there was no way to see whether synapd was running, no way
 * to turn it off, and no way to find out which model it would load. The daemon
 * was still there holding GPU memory; the only thing missing was the switch.
 *
 * ⚠ IT DOES NOT REIMPLEMENT THE SWITCH. Turning the AI off correctly is four
 * bugs deep — a stop loses to socket activation, to the LAN bridge's socket, to
 * three separate `Wants=`, and to a reboot, so `off` has to MASK
 * synapd.socket + synapd.service and record the choice somewhere that is not a
 * tmpfs. All of that lives in synui-ai-backend(1) and is covered by synui's
 * tests/ai_backend_off.sh. This pane calls that helper. A second
 * implementation of "off must hold" is a second thing to get wrong, and the
 * first one took four attempts.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define AI_HELPER "synui-ai-backend"

/* The model synapd.service names on its command line. Fixed, not configurable:
 * syn-model(1) downloads to this path and repoints it. */
#define AI_MODEL "/var/lib/synapd/models/synapse.gguf"

/* What the helper says the backend is now: gpu, cpu, off, or auto.
 *
 * `status` needs no privilege — only the writes self-elevate — so this is safe
 * to call from a settings app running as the user. */
static void backend_now(char *out, size_t cap)
{
	char buf[64] = "";
	char *argv[] = { (char *)AI_HELPER, (char *)"status", NULL };

	if (!have_cmd(AI_HELPER)) {
		snprintf(out, cap, "unknown");
		return;
	}
	run_capture_quiet(argv, buf, sizeof buf);
	buf[strcspn(buf, "\n")] = '\0';
	tsv_clean(buf);
	snprintf(out, cap, "%s", buf[0] ? buf : "unknown");
}

/* enabled/disabled/masked/not installed, and active/inactive, for one unit.
 *
 * Lifted in shape from power.c's unit_state() for the same reason it exists
 * there: systemctl exits non-zero for "disabled", for "masked" and for "no such
 * unit" alike, so the STATUS is not the answer — the word it prints is, and an
 * absent unit prints nothing at all.
 */
static void unit_state(const char *unit, char *en, size_t en_cap,
                       char *act, size_t act_cap)
{
	char out[128] = "";
	char *is_en[]  = { (char *)"systemctl", (char *)"is-enabled", (char *)unit, NULL };
	char *is_act[] = { (char *)"systemctl", (char *)"is-active",  (char *)unit, NULL };

	run_capture_quiet(is_en, out, sizeof out);
	out[strcspn(out, "\n")] = '\0';
	tsv_clean(out);
	snprintf(en, en_cap, "%s", out[0] ? out : "not installed");

	out[0] = '\0';
	run_capture_quiet(is_act, out, sizeof out);
	out[strcspn(out, "\n")] = '\0';
	tsv_clean(out);
	snprintf(act, act_cap, "%s", out[0] ? out : "-");
}

struct ai_unit {
	const char *unit;
	const char *what;
};

/* Named one at a time, and the sockets are named too. A pane that listed only
 * synapd.service would show "inactive" on a machine where the next message
 * anybody sends starts it again — socket activation is the whole reason "off"
 * needed four attempts, so the thing that does the resurrecting is on screen.
 */
static const struct ai_unit ai_units[] = {
	{ "synapd.service",        "the AI daemon; holds GPU memory while loaded" },
	{ "synapd.socket",         "socket activation — this starts the daemon on the next request" },
	{ "synapd-bridge.socket",  "LAN bridge on :11435 — a client anywhere on the network reaches this" },
	{ "synapd-bridge.service", "proxies LAN requests to synapd" },
};

int pane_ai(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	/* ── The switch ───────────────────────────────────────────────────── */
	if (have_cmd(AI_HELPER)) {
		char now[32];
		backend_now(now, sizeof now);
		rec_row("backend\tAI backend\t%s\t-\t"
		        "gpu offloads every layer \xc2\xb7 cpu runs on the CPU \xc2\xb7 "
		        "off masks the daemon so it cannot be started again\t"
		        "choice:ai-backend", now);
	} else {
		/* Read-only rather than a button that cannot work. The helper ships
		 * with synui; a machine that installed KDE or GNOME without the synui
		 * component has the daemon and not the switch, and saying so is more
		 * use than an Apply that fails. */
		rec_row("backend\tAI backend\tunavailable\t-\t"
		        "needs synui-ai-backend(1), shipped by the synui package\t-");
	}

	/* ── What is actually running ─────────────────────────────────────── */
	if (have_cmd("systemctl")) {
		for (size_t i = 0; i < sizeof ai_units / sizeof ai_units[0]; i++) {
			char en[64], act[64], action[128];
			unit_state(ai_units[i].unit, en, sizeof en, act, sizeof act);
			snprintf(action, sizeof action, "unit:%s", ai_units[i].unit);
			rec_row("unit\t%s\t%s\t%s\t%s\t%s",
			        ai_units[i].unit, en, act, ai_units[i].what,
			        strcmp(en, "not installed") ? action : "-");
		}
	} else {
		rec_row("unit\t-\tunknown\t-\tsystemctl not available\t-");
	}

	/* ── The model ────────────────────────────────────────────────────── */
	/*
	 * Reported by SIZE as well as presence. The ISO ships no gguf at all (the
	 * installer downloads one), and a zero-byte or part-downloaded file is
	 * indistinguishable from a good one by existence alone — synapd then
	 * starts, fails to load, and the desktop looks like the AI is simply
	 * ignoring it.
	 */
	{
		struct stat st;
		if (stat(AI_MODEL, &st) == 0 && st.st_size > 0) {
			double gib = (double)st.st_size / (1024.0 * 1024.0 * 1024.0);
			rec_row("model\tmodel\t%.1f GiB\tpresent\t" AI_MODEL "\t-", gib);
		} else if (stat(AI_MODEL, &st) == 0) {
			rec_row("model\tmodel\t0 bytes\tEMPTY\t"
			        "a part-downloaded model; syn-model download\t-");
		} else {
			rec_row("model\tmodel\tnone\tabsent\t"
			        "no model installed \xc2\xb7 syn-model download\t-");
		}
	}

	/* Which llama build is on the machine — the same question as "will this be
	 * fast".
	 *
	 * ⚠ ENUMERATED FROM WHAT IS ON DISK, not from a list of paths this file
	 * expects. The first draft looked for /usr/lib/synapse-llama/{cuda,vulkan,
	 * cpu} and reported "no synapse-llama build found" on a machine running
	 * synapse-llama-cuda — the packages install straight into /usr/bin and
	 * /usr/lib and there is no per-variant directory at all. Same trap power.c
	 * documents for the sleep hooks: a settings app whose whole job is showing
	 * true state cannot answer from guesses, and a hardcoded path is a guess
	 * that rots without a word.
	 *
	 * The ggml backend libraries ARE the discriminator — one per accelerator,
	 * named for it, and the variant packages differ by exactly which ones they
	 * ship beside the always-present CPU one. */
	{
		static const char *const accel[][2] = {
			{ "/usr/lib/libggml-cuda.so",   "CUDA (NVIDIA)" },
			{ "/usr/lib/libggml-vulkan.so", "Vulkan" },
			{ "/usr/lib/libggml-hip.so",    "ROCm/HIP (AMD)" },
			{ "/usr/lib/libggml-cpu.so",    "CPU" },
		};
		int found = 0;
		for (size_t i = 0; i < sizeof accel / sizeof accel[0]; i++) {
			struct stat st;
			if (stat(accel[i][0], &st) != 0) continue;
			rec_row("accel\t%s\tavailable\t-\t%s\t-", accel[i][1], accel[i][0]);
			found = 1;
		}
		if (!found)
			rec_row("accel\tacceleration\tnone\t-\t"
			        "no ggml backend library in /usr/lib \xc2\xb7 "
			        "is a synapse-llama package installed?\t-");
	}

	return 0;
}
