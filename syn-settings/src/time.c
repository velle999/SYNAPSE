/* syn-settings — the Date & Time pane.
 *
 * Two clocks live here and they are NOT the same thing, which is the whole
 * reason this is its own pane rather than three more rows on Keyboard &
 * Region:
 *
 *   · the SYSTEM clock — what time it is, which zone, whether NTP has
 *     actually disciplined it. timedatectl owns every one of those answers
 *     and does its own polkit check, so this file only reads and forwards.
 *
 *   · the DESKTOP clock — how that time is WRITTEN. 12- or 24-hour, seconds
 *     or not, and which of several date orders. Nothing owns those but a
 *     file, ~/.config/synui/clock.state, so this is one of the two places in
 *     the app that writes configuration directly (the other is Default Apps).
 *
 * The date order is the part that needed doing. It was hardcoded to
 * %Y-%m-%d, which is unambiguous and is also not how most of the world
 * writes a date, and "08/12" means two different days depending on who is
 * reading it — which is why "Day first" and "Month first" are separate
 * choices here rather than one "international" checkbox that would still
 * leave half the people who tick it reading the wrong day.
 *
 * WHICH LAYOUTS EXIST IS NOT DECIDED HERE. synui-clock renders the bar, the
 * desktop widget and the tooltip, so it is asked — `synui-clock --layouts`.
 * A copy of the list in this package would be a list that drifts, and the
 * failure it produces is a settings app offering a layout that nothing draws.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <stdlib.h>
#include <string.h>

/* Every layout synui-clock knows, plus room to breathe. The list is seven
 * entries today; this is not a limit anybody will reach, it is a bound so a
 * malformed pipe cannot walk off an array. */
#define LAYOUTS_MAX 32
#define LAYOUT_ID_CAP 32
#define LAYOUT_LABEL_CAP 96

struct layout {
	char id[LAYOUT_ID_CAP];
	char label[LAYOUT_LABEL_CAP];   /* name and a worked example, together */
};

/* ── clock.state ────────────────────────────────────────────────────────────
 *
 * `key=value`, one per line, written by synui's own Date & Time panel and read
 * by synui-clock once a second. The format is deliberately dull; what matters
 * is that this app is now a SECOND writer of it, so every write here preserves
 * the keys it does not understand. A settings app that rewrites a file from
 * its own idea of the schema drops the world-clock zones the moment somebody
 * changes the hour format.
 */
static void clock_state_path(char *out, size_t cap)
{
	/* Short of PATH_CAP by more than the suffix, so the compiler can see the
	 * result fits — the same trick apps.c uses for mimeapps.list. */
	char base[PATH_CAP - 32];
	config_home(base, sizeof base);
	snprintf(out, cap, "%s/synui/clock.state", base);
}

/* The value of `key`, or `fallback` if the file or the key is missing.
 *
 * The fallbacks must match the ones in synui's clock.c and synui-clock, or the
 * pane describes a state the desktop is not in before anything has ever been
 * saved — which is the exact class of bug this app exists to surface. */
static void clock_get(const char *key, const char *fallback,
                      char *out, size_t cap)
{
	snprintf(out, cap, "%s", fallback);

	char path[PATH_CAP];
	clock_state_path(path, sizeof path);
	char *text = slurp(path);
	if (!text) return;

	size_t klen = strlen(key);
	for (const char *p = text; p && *p; ) {
		const char *eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol - p) : strlen(p);
		if (len > klen && !strncmp(p, key, klen) && p[klen] == '=') {
			size_t vlen = len - klen - 1;
			const char *v = p + klen + 1;
			while (vlen && (v[vlen - 1] == '\r' || v[vlen - 1] == ' ')) vlen--;
			if (vlen >= cap) vlen = cap - 1;
			memcpy(out, v, vlen);
			out[vlen] = '\0';
			break;
		}
		p = eol ? eol + 1 : NULL;
	}
	free(text);
}

/* Set one key, leaving every other line exactly as it was — including keys
 * this build has never heard of, comments, and ordering. Appended if absent. */
static int clock_set(const char *key, const char *val)
{
	char path[PATH_CAP];
	clock_state_path(path, sizeof path);

	char *text = slurp(path);            /* NULL is fine: nothing saved yet */
	size_t cap = (text ? strlen(text) : 0) + 256;
	char *out = malloc(cap);
	if (!out) { free(text); return 1; }
	out[0] = '\0';

	size_t n = 0, klen = strlen(key);
	int wrote = 0;

	for (const char *p = text; p && *p; ) {
		const char *eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol - p) : strlen(p);

		if (len > klen && !strncmp(p, key, klen) && p[klen] == '=') {
			/* Replaced in place rather than dropped and appended: the file is
			 * short enough to read at a glance and keeping the order means a
			 * diff of it says what changed. */
			if (!wrote) {
				n += (size_t)snprintf(out + n, cap - n, "%s=%s\n", key, val);
				wrote = 1;
			}
		} else if (n + len + 2 < cap) {
			memcpy(out + n, p, len);
			n += len;
			out[n++] = '\n';
			out[n] = '\0';
		}

		p = eol ? eol + 1 : NULL;
	}

	if (!wrote)
		n += (size_t)snprintf(out + n, cap - n, "%s=%s\n", key, val);

	free(text);

	if (g_dry_run) {
		printf("would write %s:\n%s", path, out);
		free(out);
		return 0;
	}

	ensure_parent(path);
	backup_once(path);
	int rc = write_atomic(path, out);
	free(out);

	/* The GUI never gets here — it greys these rows outside synui, off the
	 * same helper. The CLI has no such gate and a power user typing this from
	 * a GNOME session would otherwise get silence and a clock that did not
	 * change. The write is still the right thing to do: it applies at the next
	 * synui login. It just has to SAY so. */
	if (rc == 0 && syn_synui_only())
		printf("written — %s is the session running, so this takes effect "
		       "at your next synui login\n", syn_session_desktop());
	return rc;
}

/* ── What synui-clock says ──────────────────────────────────────────────── */

/* `synui-clock --layouts` prints `id<TAB>label<TAB>example`. Returns how many
 * were read, or 0 — which is the honest answer on a machine where synui is not
 * installed, and is why every caller treats an empty list as "cannot choose"
 * rather than as "no layouts exist". */
static int clock_layouts(struct layout *out, int max)
{
	if (!have_cmd("synui-clock")) return 0;

	char buf[4096] = "";
	char *argv[] = { (char *)"synui-clock", (char *)"--layouts", NULL };
	if (run_capture_quiet(argv, buf, sizeof buf) != 0) return 0;

	int n = 0;
	for (char *p = buf; p && *p && n < max; ) {
		char *eol = strchr(p, '\n');
		if (eol) *eol = '\0';

		char *tab1 = strchr(p, '\t');
		if (tab1) {
			*tab1 = '\0';
			char *label = tab1 + 1;
			char *tab2 = strchr(label, '\t');
			const char *example = "";
			if (tab2) { *tab2 = '\0'; example = tab2 + 1; }

			snprintf(out[n].id, sizeof out[n].id, "%s", p);
			/* The example is glued to the label rather than kept as a third
			 * column, because it is not a separate fact — it IS the
			 * description. "Day first" and "Month first" are the same words to
			 * somebody who has to guess which order they mean; today's date
			 * written out cannot be misread. */
			if (*example)
				snprintf(out[n].label, sizeof out[n].label, "%s  (%s)",
				         label, example);
			else
				snprintf(out[n].label, sizeof out[n].label, "%s", label);
			tsv_clean(out[n].label);
			n++;
		}

		p = eol ? eol + 1 : NULL;
	}
	return n;
}

/* The label for a layout id, or the id itself. An id this build has never
 * heard of is SHOWN, not silently replaced: clock.state may have been written
 * by a newer synui, and a settings pane that quietly renames somebody's choice
 * to the first thing in its own list is worse than one that says a word it
 * does not recognise. */
static void layout_label(const char *id, char *out, size_t cap)
{
	struct layout ls[LAYOUTS_MAX];
	int n = clock_layouts(ls, LAYOUTS_MAX);
	for (int i = 0; i < n; i++) {
		if (!strcmp(ls[i].id, id)) {
			snprintf(out, cap, "%s", ls[i].label);
			return;
		}
	}
	if (n == 0) snprintf(out, cap, "%s", id);
	else        snprintf(out, cap, "%s (unknown to this synui-clock)", id);
}

/* Exactly what the bar is showing this second, asked of the thing that draws
 * it. Rendering a preview here from our own strftime call would be a second
 * implementation of the formatting, and a preview that can disagree with the
 * bar is worse than no preview. */
static int clock_preview(char *out, size_t cap)
{
	out[0] = '\0';
	if (!have_cmd("synui-clock")) return 0;

	char buf[512] = "";
	char *argv[] = { (char *)"synui-clock", (char *)"--preview", NULL };
	if (run_capture_quiet(argv, buf, sizeof buf) != 0) return 0;

	buf[strcspn(buf, "\n")] = '\0';
	if (!buf[0]) return 0;
	/* A synui-clock older than --preview does not know the flag, IGNORES it,
	 * and prints its normal waybar JSON with a successful exit — so "did the
	 * command work?" cannot tell the two apart. Only the shape can. Without
	 * this the pane showed a row of raw JSON, which is what an older
	 * synui-clock on the box actually produced while testing this. */
	if (buf[0] == '{') return 0;
	snprintf(out, cap, "%s", buf);
	tsv_clean(out);
	return 1;
}

/* ── The pane ───────────────────────────────────────────────────────────── */

static void row_or_unknown(const char *key, const char *text,
                           const char *field, const char *detail,
                           const char *action)
{
	char val[256];
	if (!scrape_field(text, field, val, sizeof val) || !val[0])
		snprintf(val, sizeof val, "unknown");
	rec_row("%s\t%s\t%s\t%s", key, val, detail, action);
}

/* The action for one of the three clock knobs. Not installed is "-": there is
 * nothing to offer and nothing to explain. Installed but another desktop is
 * running is `unavailable:<desktop>` — an action that EXISTS and cannot be
 * taken here, which is what greys the row rather than hiding that it is a
 * setting at all. */
static const char *clock_action(int have_clock, const char *elsewhere,
                                const char *verb)
{
	if (!have_clock) return "-";
	return elsewhere ? elsewhere : verb;
}

int pane_time(void)
{
	rec_header("key\tvalue\tdetail\taction");

	/* ── The system clock ─────────────────────────────────────────────── */
	if (have_cmd("timedatectl")) {
		char out[4096] = "";
		char *argv[] = { (char *)"timedatectl", (char *)"status", NULL };
		run_capture(argv, out, sizeof out);

		row_or_unknown("timezone", out, "Time zone", "/etc/localtime",
		               "set:timezone");
		row_or_unknown("clock-local", out, "Local time",
		               "as the system reads it", "-");
		row_or_unknown("clock-utc", out, "Universal time", "UTC", "-");
		row_or_unknown("rtc", out, "RTC time", "the hardware clock", "-");
		/* Two different questions that read almost the same: whether the NTP
		 * CLIENT is running, and whether the clock has actually been
		 * disciplined by it. A machine can have the first without the second
		 * for a long time, and only the second means the clock is right.
		 * Lynis TIME-3104 asks about the first. */
		row_or_unknown("ntp-enabled", out, "NTP service",
		               "is a time client running", "toggle:ntp");
		row_or_unknown("ntp-synced", out, "System clock synchronized",
		               "has it actually disciplined the clock", "-");
	} else {
		rec_row("timezone\tunknown\ttimedatectl not installed\t-");
		rec_row("ntp-enabled\tunknown\ttimedatectl not installed\t-");
	}

	/* ── How the desktop writes it ────────────────────────────────────── */
	//
	// Two different reasons these three might not be settable, and they are
	// worth telling apart. No synui-clock at all means synui is not installed:
	// the setting has no reader anywhere. Another desktop running means it is
	// installed and simply is not the thing drawing the clock in front of you
	// — the write would land in clock.state, report success, and change
	// nothing on screen. Only the second one names a desktop.
	int have_clock = have_cmd("synui-clock");
	const char *elsewhere = syn_synui_only();

	char why[160];
	if (elsewhere)
		snprintf(why, sizeof why,
		         "synui's clock — %s draws its own and never reads this",
		         syn_session_desktop());

	char fmt[16];
	clock_get("format", "12", fmt, sizeof fmt);
	rec_row("time-format\t%s\t%s\t%s",
	        !strcmp(fmt, "24") ? "24-hour" : "12-hour",
	        elsewhere ? why : "the bar, the lock screen and the desktop clock",
	        clock_action(have_clock, elsewhere, "choice:time-format"));

	char secs[16];
	clock_get("seconds", "0", secs, sizeof secs);
	rec_row("time-seconds\t%s\t%s\t%s",
	        atoi(secs) ? "on" : "off",
	        elsewhere ? why : "seconds in the bar clock",
	        clock_action(have_clock, elsewhere, "toggle:time-seconds"));

	char date[64], label[LAYOUT_LABEL_CAP + 48];
	clock_get("date", "iso", date, sizeof date);
	layout_label(date, label, sizeof label);
	rec_row("date-format\t%s\t%s\t%s", label,
	        elsewhere ? why : "the order the date is written in",
	        clock_action(have_clock, elsewhere, "choice:date-format"));

	/* Not a setting — the result of the three above, drawn by the thing that
	 * draws the bar. A pane that offers "Day first" and never shows what that
	 * looks like leaves you flipping settings and squinting at the corner of
	 * the screen. */
	char prev[256];
	if (clock_preview(prev, sizeof prev))
		rec_row("desktop-clock\t%s\t%s\t-", prev,
		        elsewhere ? "what synui's bar WOULD show — it is not running"
		                  : "what the bar is showing right now");
	else
		rec_row("desktop-clock\tunknown\t%s\t-",
		        have_clock ? "synui-clock returned nothing"
		                   : "synui-clock not installed — not a synui session");

	return 0;
}

/* ── Choices, for the GUI ───────────────────────────────────────────────── */

/* `syn-settings choices <key>` — the options a `choice:` row can take, as
 * `id<TAB>label<TAB>current`. Same shape and same reason as `apps <role>`: the
 * options belong to the row you picked, not to every row, and the GUI knows
 * only the verb.
 *
 * Fetched rather than carried in the table because the list is not static —
 * the date layouts come from synui-clock, which is a different package on a
 * different release cadence. */
int do_choices(int argc, char **argv)
{
	if (argc < 1) {
		fprintf(stderr, "syn-settings: choices needs a key\n");
		return 2;
	}
	const char *key = argv[0];

	if (!strcmp(key, "time-format")) {
		char cur[16];
		clock_get("format", "12", cur, sizeof cur);
		int f24 = !strcmp(cur, "24");
		/* The examples are rendered here rather than asked of synui-clock
		 * because 12- and 24-hour are not a list that can drift; there are two
		 * of them and there always will be. The date layouts are the opposite,
		 * which is why those are asked for. */
		rec_row("12\t12-hour  (2:35 PM)\t%s", f24 ? "-" : "current");
		rec_row("24\t24-hour  (14:35)\t%s",   f24 ? "current" : "-");
		return 0;
	}

	/* Lives in assistant.c, which owns the list and asks vibe which is
	 * current — one place that knows the backends, not two. */
	if (!strcmp(key, "assistant-backend"))
		return assistant_choices();

	if (!strcmp(key, "date-format")) {
		char cur[64];
		clock_get("date", "iso", cur, sizeof cur);

		struct layout ls[LAYOUTS_MAX];
		int n = clock_layouts(ls, LAYOUTS_MAX);
		if (n == 0) {
			fprintf(stderr, "syn-settings: synui-clock did not list any date "
			                "layouts — is synui installed?\n");
			return 1;
		}
		for (int i = 0; i < n; i++)
			rec_row("%s\t%s\t%s", ls[i].id, ls[i].label,
			        !strcmp(ls[i].id, cur) ? "current" : "-");
		return 0;
	}

	/* The firewall, on or off.
	 *
	 * ⚠ THE LABELS SAY WHAT HAPPENS, not what the switch is called. This is the
	 * one setting in this app that makes the machine less safe than it shipped,
	 * and "Off" on its own does not tell somebody that their laptop will start
	 * answering strangers on café Wi-Fi. Every other row here can be undone by
	 * flipping it back; this one can be undone after something has already
	 * connected.
	 *
	 * The current value comes from the same file synnet reads, with the same
	 * rule — absent means ON. A settings pane that defaulted the display to
	 * "off" on a box with no preference file would be reporting an unfiltered
	 * machine that is in fact filtered. */
	if (!strcmp(key, "firewall")) {
		int on = synnet_firewall_on();
		rec_row("on	On — refuse unsolicited connections from outside "
		        "the local network	%s", on ? "current" : "-");
		rec_row("off	Off — answer anything that reaches this machine, "
		        "on any network	%s", on ? "-" : "current");
		return 0;
	}

	/* The AI backend's three settable values. Asked of the helper for the
	 * CURRENT one rather than read off a file, because the answer has to come
	 * from the MASK first: a hand-placed `systemctl mask synapd.service` means
	 * the backend is off no matter what any state file says, and a row that
	 * disagreed with systemd would cycle the user through a daemon that
	 * cannot start.
	 *
	 * `auto` is not offered. It is a state the machine can be IN — nobody has
	 * chosen yet — and not one you can ask for; there is no verb for it in
	 * synui-ai-backend, and a button that quietly did nothing is worse than
	 * three that work. */
	if (!strcmp(key, "ai-backend")) {
		char cur[32] = "";
		if (have_cmd("synui-ai-backend")) {
			char *a[] = { (char *)"synui-ai-backend", (char *)"status", NULL };
			run_capture_quiet(a, cur, sizeof cur);
			cur[strcspn(cur, "\n")] = '\0';
			tsv_clean(cur);
		}
		rec_row("gpu\tGPU  (offload every layer)\t%s",
		        !strcmp(cur, "gpu") ? "current" : "-");
		rec_row("cpu\tCPU  (no GPU offload)\t%s",
		        !strcmp(cur, "cpu") ? "current" : "-");
		rec_row("off\tOff  (mask the daemon)\t%s",
		        !strcmp(cur, "off") ? "current" : "-");
		return 0;
	}

	fprintf(stderr, "syn-settings: nothing to choose for '%s'\n", key);
	return 2;
}

/* ── The writes ─────────────────────────────────────────────────────────── */

static int refuse(const char *msg)
{
	fprintf(stderr, "syn-settings: %s\n", msg);
	return 2;
}

/* The three desktop-clock keys. Routed here from do_set() so `set` stays the
 * one verb for every write in the app.
 *
 * Nothing needs privilege: this is the user's own file in their own config
 * directory, which is also why it does not go anywhere near polkit. */
int do_set_clock(const char *key, const char *val)
{
	if (!strcmp(key, "time-format")) {
		/* "12-hour" is what the pane DISPLAYS, so accept it as well as "12" —
		 * somebody reading the value off the row and typing it back is not
		 * making a mistake. */
		if (!strcmp(val, "12") || !strcmp(val, "12-hour"))
			return clock_set("format", "12");
		if (!strcmp(val, "24") || !strcmp(val, "24-hour"))
			return clock_set("format", "24");
		return refuse("time-format takes 12 or 24");
	}

	if (!strcmp(key, "time-seconds")) {
		if (!strcmp(val, "on"))  return clock_set("seconds", "1");
		if (!strcmp(val, "off")) return clock_set("seconds", "0");
		return refuse("time-seconds takes on or off");
	}

	if (!strcmp(key, "date-format")) {
		/* Checked against what synui-clock actually renders, not against a
		 * character class. An id that passes the character class and that
		 * nothing draws would leave the bar falling back to the default with
		 * no error anywhere — a setting that reports success and does
		 * nothing. */
		struct layout ls[LAYOUTS_MAX];
		int n = clock_layouts(ls, LAYOUTS_MAX);
		if (n == 0)
			return refuse("synui-clock is not installed, so there is no date "
			              "layout to set");
		for (int i = 0; i < n; i++)
			if (!strcmp(ls[i].id, val)) return clock_set("date", val);

		fprintf(stderr, "syn-settings: no date layout called '%s'. Try: ", val);
		for (int i = 0; i < n; i++)
			fprintf(stderr, "%s%s", i ? ", " : "", ls[i].id);
		fprintf(stderr, "\n");
		return 2;
	}

	return refuse("unknown clock key");
}
