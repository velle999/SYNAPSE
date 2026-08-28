/* syn-settings — the speech pane.
 *
 * The desktop can talk and it can listen, and until this pane existed the only
 * ways to say so were a keybind, two state files edited by hand, and synui's
 * own control panel. That last one is the problem: install SynapseOS with KDE
 * or GNOME — both are offered by the installer — and synui's panel is not
 * there, so the two settings an accessibility user needs FIRST were reachable
 * only from a terminal. The person who wants a screen reader is the person who
 * cannot read the terminal to start one.
 *
 * ⚠ IT DOES NOT REIMPLEMENT ANY OF IT. syn-speak(1) owns the announcer and
 * vibe(1) owns the listener, the wake word and the voice stack; this pane reads
 * their state files to draw the rows and runs their commands to change them.
 * The same rule the AI pane follows for the same reason: a second
 * implementation of "the microphone is open" is a second thing that can be
 * wrong about it.
 *
 * ⛔ ONE ROW HERE OPENS A MICROPHONE, and every surface that shows it has to
 * agree. The wake row reads ~/.config/synui/assistant.state — the file the
 * ENGINE writes when its loop is really running — and not `systemctl
 * is-active`, because a unit can be active with the listener failing to open
 * the device. synui's bar indicator and its control panel row read that same
 * file. One fact, three surfaces.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A `key = value` or `key=value` line out of one of the state files, or "".
 *
 * ⚠ Both spellings, because the two files are written by different programs:
 * syn-speak writes `on=yes` (the shape every synui state file uses) and vibe
 * writes `wake = on`. Accepting one and not the other is a row that reads Off
 * while the microphone is open, which is the one wrong answer this pane must
 * never give. */
static void state_get(const char *file, const char *key, char *out, size_t cap)
{
	char cfg[384], path[512];
	out[0] = '\0';
	config_home(cfg, sizeof cfg);
	if (!cfg[0])
		return;
	snprintf(path, sizeof path, "%s/%s", cfg, file);

	FILE *f = fopen(path, "re");
	if (!f)
		return;

	char line[512];
	while (fgets(line, sizeof line, f)) {
		const char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		size_t n = strlen(key);
		if (strncmp(p, key, n) != 0)
			continue;
		p += n;
		while (*p == ' ' || *p == '\t') p++;
		if (*p != '=')
			continue;
		p++;
		while (*p == ' ' || *p == '\t') p++;
		snprintf(out, cap, "%s", p);
		out[strcspn(out, "\r\n")] = '\0';
		/* No break: the last assignment wins, which is what a file somebody
		 * has appended to twice actually means. */
	}
	fclose(f);
}

/* What vibe says this box can do — `vibe voice status`, which reports without
 * loading anything. Building piper to answer "is piper installed" would cost
 * the second this is trying to report on. */
static void voice_caps(char *speak, size_t sc, char *listen, size_t lc)
{
	snprintf(speak, sc, "unknown");
	snprintf(listen, lc, "unknown");
	if (!have_cmd("vibe"))
		return;

	char buf[512] = "";
	char *argv[] = { (char *)"vibe", (char *)"voice", (char *)"status", NULL };
	run_capture_quiet(argv, buf, sizeof buf);

	char *save = NULL;
	for (char *line = strtok_r(buf, "\n", &save); line;
	     line = strtok_r(NULL, "\n", &save)) {
		char *tab = strchr(line, '\t');
		if (!tab)
			continue;
		*tab = '\0';
		tsv_clean(tab + 1);
		if (!strcmp(line, "speak"))  snprintf(speak, sc, "%s", tab + 1);
		if (!strcmp(line, "listen")) snprintf(listen, lc, "%s", tab + 1);
	}
}

int pane_speech(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	char on[64] = "", wake[64] = "";
	state_get("synui/speak.state", "on", on, sizeof on);
	state_get("synui/assistant.state", "wake", wake, sizeof wake);

	/* ── The two switches ─────────────────────────────────────────────── */
	if (have_cmd("syn-speak")) {
		rec_row("switch\tScreen reader\t%s\t-\t"
		        "Speak the focused window as focus moves, and the highlighted "
		        "text on Super+U. Super+Shift+U toggles it\ttoggle:screen-reader",
		        !strcmp(on, "yes") ? "on" : "off");
	} else {
		rec_row("switch\tScreen reader\tunavailable\t-\t"
		        "needs syn-speak(1), shipped by the synui package\t-");
	}

	if (have_cmd("vibe")) {
		/* ⚠ "listening", not "on". "On" describes a setting; what is true is
		 * that a microphone is open right now, and this is the row where the
		 * difference matters. */
		rec_row("switch\tAnswer to its name\t%s\t-\t"
		        "Listen for \"Synapse\" and answer out loud. HOLDS THE "
		        "MICROPHONE OPEN until it is turned off; the bar shows a "
		        "microphone while it is on\ttoggle:wake-word",
		        !strcmp(wake, "on") ? "listening" : "off");
	} else {
		rec_row("switch\tAnswer to its name\tunavailable\t-\t"
		        "needs vibe(1) \xc2\xb7 synpkg install vibe\t-");
	}

	/* ── What this box can actually do ────────────────────────────────── */
	char speak_eng[64], listen_eng[64];
	voice_caps(speak_eng, sizeof speak_eng, listen_eng, sizeof listen_eng);
	rec_row("engine\tVoice\t%s\t-\t"
	        "piper is chibi's; espeak-ng is the fallback\t-", speak_eng);
	rec_row("engine\tDictation\t%s\t-\t"
	        "faster-whisper, from chibi. Super+Shift+V types what it hears\t-",
	        listen_eng);

	/* ── Tuning ───────────────────────────────────────────────────────── */
	char rate[32] = "", vol[32] = "", words[128] = "";
	state_get("synui/speak.state", "rate", rate, sizeof rate);
	state_get("synui/speak.state", "volume", vol, sizeof vol);
	state_get("synui/wake.state", "words", words, sizeof words);

	rec_row("value\tSpeech rate\t%s\t-\tWords per minute, 80 to 450\t"
	        "set:speech-rate", rate[0] ? rate : "175");
	rec_row("value\tSpeech volume\t%s\t-\t0 to 100\tset:speech-volume",
	        vol[0] ? vol : "100");
	rec_row("value\tWake words\t%s\t-\t"
	        "Comma separated. \"computer\" is kept because whisper transcribes "
	        "it every time \xc2\xb7 drop it if it wakes on the word in "
	        "conversation\tset:wake-words",
	        words[0] ? words : "synapse, computer");

	/* ── And the unit behind the listener ─────────────────────────────── */
	if (have_cmd("systemctl")) {
		char en[64] = "", act[64] = "";
		char *a1[] = { (char *)"systemctl", (char *)"--user",
		               (char *)"is-enabled", (char *)"vibe-wake.service", NULL };
		char *a2[] = { (char *)"systemctl", (char *)"--user",
		               (char *)"is-active", (char *)"vibe-wake.service", NULL };
		run_capture_quiet(a1, en, sizeof en);
		run_capture_quiet(a2, act, sizeof act);
		en[strcspn(en, "\n")] = '\0';
		act[strcspn(act, "\n")] = '\0';
		tsv_clean(en);
		tsv_clean(act);
		rec_row("unit\tvibe-wake.service\t%s\t%s\t"
		        "the listener. Shipped disabled \xc2\xb7 the switch above is "
		        "what turns it on\t-",
		        en[0] ? en : "not installed", act[0] ? act : "inactive");
	}

	return 0;
}
