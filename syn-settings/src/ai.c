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
#include "i18n.h"

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
	{ "synapd.service",        N_("the AI daemon; holds GPU memory while loaded") },
	{ "synapd.socket",         N_("socket activation — this starts the daemon on the next request") },
	{ "synapd-bridge.socket",  "LAN bridge on :11435 — a client anywhere on the network reaches this" },
	{ "synapd-bridge.service", N_("proxies LAN requests to synapd") },
	{ "synapd-http-proxy.socket",
	  "127.0.0.1:8080 for llama.cpp-shaped frontends — off unless enabled" },
	{ "synapd-http-proxy.service", N_("proxies that port to synapd's HTTP socket") },
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

int pane_ai(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	/* ── The switch ───────────────────────────────────────────────────── */
	if (have_cmd(AI_HELPER)) {
		char now[32];
		backend_now(now, sizeof now);
		rec_row("backend\t%s\t%s\t-\t%s\tchoice:ai-backend",
		        N_("AI backend"), now,
		        N_("gpu offloads every layer \xc2\xb7 cpu runs on the CPU \xc2\xb7 off masks the daemon so it cannot be started again"));
	} else {
		/* Read-only rather than a button that cannot work. The helper ships
		 * with synui; a machine that installed KDE or GNOME without the synui
		 * component has the daemon and not the switch, and saying so is more
		 * use than an Apply that fails. */
		rec_row("backend\t%s\tunavailable\t-\t%s\t-",
		        N_("AI backend"),
		        N_("needs synui-ai-backend(1), shipped by the synui package"));
	}

	/* ── The llama.cpp-compatible port ────────────────────────────────── */
	/*
	 * ⛔ A SWITCH, NOT TWO UNIT BUTTONS. The socket below is in ai_units[] as
	 * well, because seeing its real state matters — but enable and start are
	 * separate there, and a port that is enabled and not listening (or
	 * listening and not enabled) is neither of the two answers anybody wanted.
	 *
	 * ⚠ AND IT SAYS WHAT TURNING IT ON MEANS. synapd's HTTP API is on a unix
	 * socket precisely because a port is reachable by every local process, a
	 * page in a browser included, and this model answers questions about this
	 * machine. Somebody deciding is owed that sentence, not just a toggle.
	 */
	if (have_cmd("systemctl")) {
		char en[64], act[64];
		unit_state("synapd-http-proxy.socket", en, sizeof en, act, sizeof act);

		if (unit_absent(en)) {
			rec_row("llama-api\t%s\tunavailable\t-\t%s\t-",
			        N_("llama.cpp API port"),
			        N_("needs a synapd that ships synapd-http-proxy.socket"));
		} else {
			const char *on = !strcmp(en, "enabled") ? "on" : "off";
			rec_row("llama-api\t%s\t%s\t%s\t%s\ttoggle:llama-api",
			        N_("llama.cpp API port"), on, act,
			        N_("127.0.0.1:8080 for frontends written against llama-server or the OpenAI API, over the model synapd already holds \xc2\xb7 no authentication, so every process on this machine can reach it \xc2\xb7 loopback only, never the network"));
		}
	}

	/* ── What is actually running ─────────────────────────────────────── */
	if (have_cmd("systemctl")) {
		for (size_t i = 0; i < sizeof ai_units / sizeof ai_units[0]; i++) {
			char en[64], act[64], action[128];
			unit_state(ai_units[i].unit, en, sizeof en, act, sizeof act);
			snprintf(action, sizeof action, "unit:%s", ai_units[i].unit);
			rec_row("unit\t%s\t%s\t%s\t%s\t%s",
			        ai_units[i].unit, en, act, ai_units[i].what,
			        !unit_absent(en) ? action : "-");
		}
	} else {
		rec_row("unit\t-\tunknown\t-\t%s\t-",
		        N_("systemctl not available"));
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
			rec_row("model\t%s\t0 bytes\tEMPTY\t%s\t-",
			        N_("model"), N_("a part-downloaded model; syn-model download"));
		} else {
			rec_row("model\t%s\tnone\tabsent\t%s\t-",
			        N_("model"), N_("no model installed \xc2\xb7 syn-model download"));
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
			{ "/usr/lib/libggml-cuda.so",   N_("CUDA (NVIDIA)") },
			{ "/usr/lib/libggml-vulkan.so", "Vulkan" },
			{ "/usr/lib/libggml-hip.so",    "ROCm/HIP (AMD)" },
			{ "/usr/lib/libggml-cpu.so",    N_("CPU") },
		};
		int found = 0;
		for (size_t i = 0; i < sizeof accel / sizeof accel[0]; i++) {
			struct stat st;
			if (stat(accel[i][0], &st) != 0) continue;
			rec_row("accel\t%s\tavailable\t-\t%s\t-", accel[i][1], accel[i][0]);
			found = 1;
		}
		if (!found)
			rec_row("accel\t%s\tnone\t-\t%s\t-",
			        N_("acceleration"),
			        N_("no ggml backend library in /usr/lib \xc2\xb7 is a synapse-llama package installed?"));
	}

	return 0;
}
