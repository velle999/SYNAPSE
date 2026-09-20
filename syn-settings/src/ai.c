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
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define AI_HELPER "synui-ai-backend"
#define AI_BENCH  "synapd-bench"

/*
 * One `key=value` field out of a machine-readable line, matched as a WHOLE key.
 *
 * ⛔ NOT strstr(s, "decode_tps="). The line ends with model_file="…", which is
 * free text out of the GGUF's metadata — a model whose name contained a key
 * would be read as that key. synapd-bench's own parser was written twice for
 * this exact reason and its test pins it; this one only ever reads the numeric
 * fields, which come before the quoted one, and it still checks the whole key
 * rather than relying on that ordering holding.
 */
static double kv_num(const char *s, const char *key, double missing)
{
	size_t klen = strlen(key);
	for (const char *p = s; (p = strstr(p, key)); p += klen) {
		if (p != s && p[-1] != ' ') continue;
		if (p[klen] != '=')         continue;
		return strtod(p + klen + 1, NULL);
	}
	return missing;
}

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

/*
 * `syn-settings bench [HOST]` — measure, rather than read.
 *
 * ⛔ IT DOES NOT REIMPLEMENT THE MEASUREMENT, for the same reason the backend
 * switch above is handed to synui-ai-backend(1): the numbers have to come from
 * the daemon that did the work. synapd-bench asks it for the token counts and
 * the time each half took; a settings app timing a round trip from out here
 * would be counting the network and the queue as if they were the model.
 *
 * Streamed, not captured: it prints a line per run and takes seconds, and a
 * window that shows nothing until the end of a slow operation reads as a
 * window that has hung.
 */
int do_bench(int argc, char **argv)
{
	if (!have_cmd(AI_BENCH)) {
		fprintf(stderr, "syn-settings: %s is not installed "
		                "(it ships with synapd)\n", AI_BENCH);
		return 1;
	}

	/* An optional host, so a laptop can measure the desktop's daemon over
	 * the LAN bridge — which is the case where the interesting number is not
	 * the model's speed but what the network adds to it. */
	char *a[5];
	int n = 0;
	a[n++] = (char *)AI_BENCH;
	if (argc > 0 && argv[0] && argv[0][0]) {
		a[n++] = (char *)"--host";
		a[n++] = argv[0];
	}
	a[n] = NULL;
	return run_or_show_progress(a);
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

	/* ── How fast it answers ──────────────────────────────────────────── */
	/*
	 * ⚠ READ WHEN THIS PANE DRAWS, NEVER MEASURED. `synapd-bench --last` is
	 * a single status round trip that asks the daemon nothing: running a
	 * real benchmark to fill in a row would spend seconds of GPU and a few
	 * hundred generated tokens every time somebody opened this window. The
	 * button is what measures.
	 *
	 * ⛔ AND THE NUMBER IS WHOEVER ASKED LAST — a vibe turn, chibi, the
	 * command bar — not this pane's measurement. That is why the state
	 * column says "last answer": it is a fair picture of how the machine is
	 * behaving and a poor one for comparing two models, because nothing here
	 * chose the prompt. Pressing the button is what fixes the prompt.
	 */
	if (have_cmd(AI_BENCH)) {
		char out[512] = "";
		char *argv[] = { (char *)AI_BENCH, (char *)"--last", NULL };
		run_capture_quiet(argv, out, sizeof out);

		double tps  = kv_num(out, "decode_tps", -1);
		double toks = kv_num(out, "gen_tok", 0);

		if (tps >= 0 && toks > 0) {
			/* Two numbers, because they answer different questions: slow
			 * prefill is layers that are not on the GPU, slow decode with
			 * fast prefill is weights in the wrong kind of memory.
			 *
			 * ⚠ THE RATE IS FORMATTED FIRST, so the format string here holds
			 * no prose at all — the sentence a translator receives is whole
			 * and the number arrives in a %s. A phrase split across a format
			 * ships half of itself in English inside every other language. */
			char pre_s[32];
			snprintf(pre_s, sizeof pre_s, "%.0f tok/s", kv_num(out, "prefill_tps", 0));
			rec_row("bench\t%s\t%.1f tok/s\t%s\t%s %s\tbench:run",
			        N_("Speed"), tps, N_("last answer"),
			        N_("the answer, token by token \xc2\xb7 the prompt was read at"),
			        pre_s);
		} else {
			rec_row("bench\t%s\t%s\t-\t%s\tbench:run",
			        N_("Speed"), N_("not measured yet"),
			        N_("how fast this model answers on this machine \xc2\xb7 the daemon times the prompt and the answer separately"));
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
		rec_row("unit\t-\t%s\t-\t%s\t-",
		        N_("unknown"), N_("systemctl not available"));
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
			rec_row("model\t%s\t%s\tabsent\t%s\t-",
			        N_("model"), N_("none"),
			        N_("no model installed \xc2\xb7 syn-model download"));
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
		/*
		 * ⛔ ALL FOUR MARKED, OR NONE. Two of them were and two were not, and
		 * the two that were not are exactly the two this machine has no
		 * library for — so the drawn-label gate could not see them here and
		 * failed on velle's box, where libggml-vulkan.so exists. They are
		 * product names and every translator will leave them as they are;
		 * that is not the point. The point is that a label in this column is
		 * either in the catalog or it is not reachable, and "it happens to be
		 * a proper noun" is not a property a check can read.
		 */
		static const char *const accel[][2] = {
			{ "libggml-cuda.so",   N_("CUDA (NVIDIA)")  },
			{ "libggml-vulkan.so", N_("Vulkan")         },
			{ "libggml-hip.so",    N_("ROCm/HIP (AMD)") },
			{ "libggml-cpu.so",    N_("CPU")            },
		};
		/*
		 * ⚠ AND THE DIRECTORY IS A SEAM, for the same reason SYN_DISKS_SYSFS
		 * is one. Which of these rows exists is decided by what is installed
		 * on the machine asking, so a gate that reads the record reads THIS
		 * machine — twice now that has meant a check passing here and failing
		 * on a box with different hardware. tests/i18n_test.sh points this at
		 * four stub files and gets all four rows on any machine.
		 */
		const char *libdir = getenv("SYN_SETTINGS_LIBDIR");
		if (!libdir || !*libdir) libdir = "/usr/lib";
		int found = 0;
		for (size_t i = 0; i < sizeof accel / sizeof accel[0]; i++) {
			char path[512];
			snprintf(path, sizeof path, "%s/%s", libdir, accel[i][0]);
			struct stat st;
			if (stat(path, &st) != 0) continue;
			rec_row("accel\t%s\t%s\t-\t%s\t-",
			        accel[i][1], N_("available"), path);
			found = 1;
		}
		if (!found)
			rec_row("accel\t%s\t%s\t-\t%s\t-",
			        N_("acceleration"), N_("none"),
			        N_("no ggml backend library in /usr/lib \xc2\xb7 is a synapse-llama package installed?"));
	}

	return 0;
}
