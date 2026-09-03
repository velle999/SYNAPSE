/*
 * hud.c — the MangoHud overlay: what it shows, where it sits, whether it is on.
 *
 * ── Why editing a config file is the right mechanism ────────────────────────
 *
 * MangoHud is an implicit Vulkan layer living INSIDE the game's process. There
 * is no socket and no signal. `mangohudctl` looks like the answer and is not:
 * it speaks over a SysV message queue keyed on "mangoapp", and only the
 * separate `mangoapp` process (the gamescope/SteamOS overlay) listens on it.
 * Verified against the shipped 0.8.4 build — libMangoHud.so imports no msgget,
 * msgrcv or ftok at all, so for the MangoHud layer this desktop loads,
 * mangohudctl is a no-op.
 *
 * ⚠ AND "EVERYWHERE" IS NO LONGER TRUE OF THAT LAYER. The session stopped
 * exporting MANGOHUD=1 for every process (it segfaulted three non-games on
 * AMD); the overlay now arrives with the launcher — `syn game …` /
 * synui-game-run — unless someone asks for the old behaviour with
 * `syn game hud on`. Nothing here depends on which: this file only ever
 * touches MANGOHUD_CONFIGFILE, which is the overlay's look and not whether it
 * loads.
 *
 * What libMangoHud DOES do is watch its own config file (src/notify.cpp):
 *
 *     inotify_add_watch(fd, config_file_path, IN_MODIFY | IN_DELETE_SELF)
 *
 * and on any event it reparses the file and replaces its whole parameter
 * struct. So writing the config file is a live control channel into every
 * running game at once — which is what makes a compositor keybind possible.
 * (/etc/MangoHud.conf's own comment says the opposite: that a toggle "cannot
 * be a compositor keybind". That was true of a signal or an IPC call; it is
 * not true of the file, and the file is what MangoHud watches.)
 *
 * ⚠ TWO consequences of that mechanism, both load-bearing:
 *
 *   1. The file must EXIST BEFORE THE GAME STARTS. inotify_add_watch on a
 *      missing path fails, start_notifier() returns false, and that process
 *      never watches anything for the rest of its life. `hud ensure` exists
 *      to guarantee it, and the session calls it.
 *
 *   2. Writes must be IN PLACE, not a rename. See write_file_inplace().
 *
 * ── Which file ──────────────────────────────────────────────────────────────
 *
 * MangoHud reads exactly ONE config file, and NOT the one you would guess.
 * config.cpp builds a candidate list, walks it in REVERSE, and returns on the
 * first that opens:
 *
 *     pushed:  [0] ~/.config/MangoHud/MangoHud.conf
 *              [1] /etc/MangoHud.conf
 *              [2] ~/.config/MangoHud/<program>.conf
 *              [3] <program_dir>/MangoHud.conf
 *              [4] ~/.config/MangoHud/wine-<exe>.conf
 *
 *     read:    [4] → [3] → [2] → [1] → [0], first hit wins, then `return`
 *
 * So /etc/MangoHud.conf OUTRANKS the user's file, and there is no merging.
 * SynapseOS ships /etc/MangoHud.conf (synui's PKGBUILD), which means that on a
 * stock install ~/.config/MangoHud/MangoHud.conf is never read at all.
 *
 * That is why this package sets MANGOHUD_CONFIGFILE in the session profile: it
 * collapses the whole list to one user-writable path, which is the only way a
 * per-user setting — or a keybind run as the user — can win. hud_seed() copies
 * whatever was previously in effect into that file the first time, so nothing
 * anybody tuned is lost in the move.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "arcade.h"
#include "i18n.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Clockwise from the top-left, so the cycle key walks the overlay around the
 * screen edge in the order a hand expects rather than in enum order. */
const char *const hud_positions[HUD_POS_COUNT] = {
	"top-left",	"top-center",	"top-right",	"middle-right",
	"bottom-right",	"bottom-center","bottom-left",	"middle-left",
};

/*
 * The same eight, as WORDS.
 *
 * ⛔ A SECOND TABLE RATHER THAN A MARKED FIRST ONE, and the reason is what
 * `hud choices hud-position` prints: an `id` column and a `label` column. The
 * id is what `hud position <id>` takes back and what MangoHud's own config
 * file stores; the label is what a window draws. They happened to be the same
 * eight strings, and marking that one array would have put the id in the
 * catalog too — so a German window would offer the user a position no command
 * accepts. See include/i18n.h.
 *
 * ⚠ N_(), not _(): these travel in a RECORD, so the row has to carry the
 * English word for the window to look up at the draw site.
 */
const char *const hud_position_labels[HUD_POS_COUNT] = {
	N_("top-left"),	    N_("top-center"),    N_("top-right"),   N_("middle-right"),
	N_("bottom-right"), N_("bottom-center"), N_("bottom-left"), N_("middle-left"),
};

/* The path this package makes authoritative. */
static bool hud_user_config(char *buf, size_t n)
{
	return config_path(buf, n, "MangoHud/MangoHud.conf");
}

/*
 * MangoHud's own precedence, reimplemented.
 *
 * Only the two GENERIC candidates are considered: the per-program and
 * per-wine-exe files are keyed on the name of the running game, which this
 * process is not, so they cannot be resolved here and are not ours to write.
 * They still outrank both of these inside a game that has one — `hud path`
 * says so rather than pretending otherwise.
 */
bool hud_effective_config(char *buf, size_t n)
{
	const char *env = getenv("MANGOHUD_CONFIGFILE");
	if (env && *env)
		return snprintf(buf, n, "%s", env) < (int)n;

	/* Reverse order: /etc before the user file, first existing wins. */
	if (file_exists("/etc/MangoHud.conf"))
		return snprintf(buf, n, "/etc/MangoHud.conf") < (int)n;

	return hud_user_config(buf, n);
}

/* ── the config as a list of lines ───────────────────────────────────────── */

typedef struct {
	char **line;
	int    count;
	int    cap;
} lines_t;

static void lines_push(lines_t *L, char *s)
{
	if (L->count == L->cap) {
		L->cap = L->cap ? L->cap * 2 : 32;
		L->line = xrealloc(L->line, (size_t)L->cap * sizeof(*L->line));
	}
	L->line[L->count++] = s;
}

static void lines_free(lines_t *L)
{
	for (int i = 0; i < L->count; i++)
		free(L->line[i]);
	free(L->line);
	L->line = NULL;
	L->count = L->cap = 0;
}

static void lines_split(lines_t *L, const char *text)
{
	const char *p = text;
	while (*p) {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);
		char *s = xmalloc(len + 1);
		memcpy(s, p, len);
		s[len] = '\0';
		lines_push(L, s);
		if (!nl) break;
		p = nl + 1;
	}
}

static char *lines_join(const lines_t *L)
{
	size_t total = 1;
	for (int i = 0; i < L->count; i++)
		total += strlen(L->line[i]) + 1;
	char *out = xmalloc(total);
	char *w = out;
	for (int i = 0; i < L->count; i++) {
		size_t n = strlen(L->line[i]);
		memcpy(w, L->line[i], n);
		w += n;
		*w++ = '\n';
	}
	*w = '\0';
	return out;
}

/*
 * Is this line the setting `key`?
 *
 * ⚠ MangoHud strips from the first '#' ANYWHERE in the line before parsing
 * (config.cpp: `line.erase(line.find("#"))`), so a commented-out setting is
 * genuinely inert and must not be matched — but an inline comment after a live
 * setting means the setting still counts. Mirror that exactly, or this reads a
 * state MangoHud does not have.
 *
 * A bare `key` with no '=' is a setting whose value is "1", also per config.cpp.
 *
 * The value goes into a CALLER-owned buffer. It is tempting to return a pointer
 * into a static instead, but the value is parsed before the key is compared, so
 * a static would be overwritten by every non-matching line that followed — and
 * two live results (`font_size` and `background_alpha`, say) would alias into
 * one. Nothing warns about either.
 */
static bool line_is_key(const char *line, const char *key,
			char *val, size_t valn)
{
	char buf[1024];
	if (snprintf(buf, sizeof(buf), "%s", line) >= (int)sizeof(buf))
		return false;

	char *hash = strchr(buf, '#');
	if (hash) *hash = '\0';

	char *s = trim(buf);
	if (!*s) return false;

	char *eq = strchr(s, '=');
	const char *v = "1";
	if (eq) {
		*eq = '\0';
		v = trim(eq + 1);
	}
	if (strcmp(trim(s), key) != 0)
		return false;

	if (val && valn)
		snprintf(val, valn, "%s", v);
	return true;
}

/* Does the file set `key`? If so and `val` is given, its value lands there.
 * The LAST occurrence wins, because that is what MangoHud's
 * `options[param] = value` does. */
static bool cfg_get(const lines_t *L, const char *key, char *val, size_t valn)
{
	bool found = false;
	for (int i = 0; i < L->count; i++) {
		char v[1024];
		if (!line_is_key(L->line[i], key, v, sizeof(v)))
			continue;
		found = true;
		if (val && valn)
			snprintf(val, valn, "%s", v);
	}
	return found;
}

/*
 * Set `key` to `value`, preserving everything else byte for byte.
 *
 * The file may be one somebody spent an evening tuning, so this is a
 * key-preserving rewrite and not a regenerate: the line is edited where it
 * already is (keeping its position and any inline comment's absence), and only
 * appended when the key is genuinely missing. Commented-out lines are left
 * alone — they are the user's notes, not settings.
 */
static void cfg_set(lines_t *L, const char *key, const char *value)
{
	char neu[1024];
	snprintf(neu, sizeof(neu), "%s=%s", key, value);

	int last = -1;
	for (int i = 0; i < L->count; i++)
		if (line_is_key(L->line[i], key, NULL, 0))
			last = i;

	if (last >= 0) {
		free(L->line[last]);
		L->line[last] = xstrdup(neu);
		/* Any EARLIER occurrence is dead weight that MangoHud would
		 * override anyway; leaving it would make the file say two
		 * different things and the next reader believe the first. */
		for (int i = 0; i < last; i++) {
			if (!line_is_key(L->line[i], key, NULL, 0)) continue;
			free(L->line[i]);
			L->line[i] = xstrdup("");
		}
		return;
	}

	/* Trailing blank lines look tidier above the new setting than below it. */
	while (L->count > 0 && !*trim(L->line[L->count - 1]))
		free(L->line[--L->count]);
	lines_push(L, xstrdup(neu));
}

/* ── reading and writing the file ────────────────────────────────────────── */

/* The overlay this desktop starts from. Kept in step with the comments in
 * synui/config/MangoHud.conf — the values, not the prose. */
static const char *hud_default_config(void)
{
	return
"# MangoHud overlay for SynapseOS. Managed by syn-arcade, and safe to edit:\n"
"# every setting you change here is preserved. syn-arcade only ever rewrites\n"
"# the individual lines it is asked to change.\n"
"#\n"
"# `no_display` decides whether the overlay is drawn. syn-arcade toggles it\n"
"# live, inside running games, by rewriting this file — MangoHud watches it\n"
"# with inotify and reparses on every change.\n"
"no_display=1\n"
"legacy_layout=false\n"
"fps\n"
"frametime=1\n"
"frame_timing=1\n"
"gpu_stats\n"
"gpu_temp\n"
"gpu_load_change\n"
"cpu_stats\n"
"cpu_temp\n"
"cpu_load_change\n"
"ram\n"
"vram\n"
"position=top-left\n"
"font_size=20\n"
"background_alpha=0.4\n"
"# MangoHud's OWN key, handled inside the game process. Kept as a fallback for\n"
"# anywhere syn-arcade's compositor binds cannot reach. Deliberately NOT the\n"
"# same combo as those binds: two things toggling one setting on one keypress\n"
"# would cancel out.\n"
"toggle_hud=Shift_R+F12\n";
}

/* Load the effective config, or the shipped default if there is not one yet. */
static void hud_load(lines_t *L, char *path, size_t pathn)
{
	if (!hud_effective_config(path, pathn)) {
		fputs(_("syn-arcade: cannot resolve a MangoHud config path "
		        "(is HOME set?)\n"), stderr);
		exit(EX_FAIL);
	}

	char *text = read_file(path);
	if (!text)
		text = xstrdup(hud_default_config());
	lines_split(L, text);
	free(text);
}

/*
 * Persist the edited config.
 *
 * If the effective file is not ours to write — the /etc case described at the
 * top of this file — say so precisely rather than failing with EACCES. A
 * keybind that silently does nothing is the failure mode worth spending five
 * lines of error message on.
 */
static int hud_store(const lines_t *L, const char *path)
{
	if (!file_writable(path)) {
		fprintf(stderr,
		 _("syn-arcade: %s is not writable by you.\n"
		   "\n"
		   "MangoHud reads exactly one config file and /etc/MangoHud.conf\n"
		   "outranks the per-user one, so nothing you set as a user can win\n"
		   "while that file exists. Fix it once with:\n"
		   "\n"
		   "    syn-arcade hud adopt\n"
		   "\n"
		   "which copies the settings now in effect into your own config and\n"
		   "points MANGOHUD_CONFIGFILE at it for future logins.\n"), path);
		return EX_FAIL;
	}

	/* One-time backup of a file we did not write, so the in-place write
	 * cannot be the thing that loses somebody's tuned overlay. */
	char bak[4096];
	if (snprintf(bak, sizeof(bak), "%s.pre-syn-arcade", path) < (int)sizeof(bak) &&
	    !file_exists(bak)) {
		char *orig = read_file(path);
		if (orig) {
			write_file_inplace(bak, orig);
			free(orig);
		}
	}

	char *text = lines_join(L);
	int rc = write_file_inplace(path, text);
	free(text);

	if (rc < 0) {
		fprintf(stderr, _("syn-arcade: cannot write %s: %s\n"),
			path, strerror(-rc));
		return EX_FAIL;
	}
	return EX_OK;
}

/* ── state ───────────────────────────────────────────────────────────────── */

/* `no_display` is parsed by MangoHud as `strtol(value, NULL, 0) != 0`, and a
 * bare `no_display` line carries the value "1". So the overlay is VISIBLE when
 * the key is absent or numerically zero. */
static bool hud_hidden(const lines_t *L)
{
	char v[64];
	if (!cfg_get(L, "no_display", v, sizeof(v)))
		return false;
	return strtol(v, NULL, 0) != 0;
}

#define HUD_POS_MAX 64

static void hud_position(const lines_t *L, char *buf, size_t n)
{
	char v[HUD_POS_MAX];
	if (cfg_get(L, "position", v, sizeof(v)) && *v)
		snprintf(buf, n, "%s", v);
	else
		snprintf(buf, n, "top-left");
}

static int hud_position_index(const char *pos)
{
	for (int i = 0; i < HUD_POS_COUNT; i++)
		if (strcmp(hud_positions[i], pos) == 0)
			return i;
	return -1;
}

/* ── commands ────────────────────────────────────────────────────────────── */

static int hud_show_state(bool rec)
{
	lines_t L = {0};
	char path[4096];
	hud_load(&L, path, sizeof(path));

	char pos[HUD_POS_MAX];
	hud_position(&L, pos, sizeof(pos));
	bool hidden = hud_hidden(&L);

	char fsz[64], alpha[64];
	if (!cfg_get(&L, "font_size", fsz, sizeof(fsz)) || !*fsz)
		snprintf(fsz, sizeof(fsz), "20");
	if (!cfg_get(&L, "background_alpha", alpha, sizeof(alpha)) || !*alpha)
		snprintf(alpha, sizeof(alpha), "0.4");

	if (rec) {
		rec_row(3, "field", "value", "action");
		/* ⛔ THE `action` COLUMN IS AN INSTRUCTION TO THE WINDOW —
		 * `toggle:hud`, `set:font_size` — and is never a word. Nor is
		 * `pos`, which is the id from hud_positions[]: the window draws
		 * it through the label column of `hud choices`. */
		rec_row(3, N_("state"), hidden ? N_("hidden") : N_("visible"),
			"toggle:hud");
		rec_row(3, N_("position"), pos, "choice:hud-position");
		rec_row(3, N_("font_size"), fsz, "set:font_size");
		rec_row(3, N_("background_alpha"), alpha, "set:background_alpha");
		rec_row(3, N_("config"), path,
			file_writable(path) ? "detail" : "detail readonly");
	} else {
		printf(_("state     %s\n"), hidden ? _("hidden") : _("visible"));
		printf(_("position  %s\n"), _(pos));
		printf(_("config    %s%s\n"), path,
		       file_writable(path) ? "" : _("   (NOT WRITABLE BY YOU)"));
	}

	lines_free(&L);
	return EX_OK;
}

/*
 * What changed, said where somebody will actually see it.
 *
 * ⚠ "VISIBLE" HAS NEVER MEANT "ON SCREEN NOW", and reporting it as though it
 * did is what made a working switch look broken. MangoHud is a Vulkan and
 * OpenGL layer: it draws inside a program it has been injected into and it
 * cannot draw on the desktop at all. So `hud show` on a machine with no game
 * running correctly writes the setting, correctly reports success, and
 * correctly puts nothing anywhere — which is indistinguishable, from the
 * outside, from a command that did nothing.
 *
 * The state is real, and it is the state the NEXT game starts in. A game
 * already running picks it up through MangoHud's inotify watch on this file,
 * which is why the toggle is worth having at all rather than being a thing you
 * set before launching.
 *
 * ⚠ SAID ON EVERY CHANGE, position included. "Move it" with nothing running
 * moves nothing visible for exactly the same reason, and a message that
 * explained the silence only half the time would teach people to stop reading
 * it.
 */
static void hud_said(const char *what)
{
	/* ⚠ `what` is a hud_positions[] id or "on"/"off"; it goes through the
	 * catalog because the label table above put the eight positions in it,
	 * and falls back unchanged for anything else. */
	printf(_("hud %s — MangoHud draws inside a game, not on the desktop\n"),
	       _(what));
}

static int hud_set_hidden(bool hide)
{
	lines_t L = {0};
	char path[4096];
	hud_load(&L, path, sizeof(path));
	cfg_set(&L, "no_display", hide ? "1" : "0");
	int rc = hud_store(&L, path);
	if (rc == EX_OK)
		hud_said(hide ? "hidden" : "visible");
	lines_free(&L);
	return rc;
}

static int hud_toggle(void)
{
	lines_t L = {0};
	char path[4096];
	hud_load(&L, path, sizeof(path));
	bool hide = !hud_hidden(&L);
	cfg_set(&L, "no_display", hide ? "1" : "0");
	int rc = hud_store(&L, path);
	if (rc == EX_OK)
		hud_said(hide ? "hidden" : "visible");
	lines_free(&L);
	return rc;
}

/*
 * Move the overlay one step around the screen.
 *
 * Cycling also UNHIDES. Pressing the position key on a hidden overlay and
 * seeing nothing happen reads as a broken key, and there is no other reason to
 * press it — you move the hud because you want to look at it.
 */
static int hud_cycle(int step)
{
	lines_t L = {0};
	char path[4096];
	hud_load(&L, path, sizeof(path));

	char cur[HUD_POS_MAX];
	hud_position(&L, cur, sizeof(cur));
	int i = hud_position_index(cur);
	/* An unrecognised position (a MangoHud version with more of them, or a
	 * typo) starts the walk at the beginning rather than being preserved
	 * into an index that means nothing. */
	int next = (i < 0) ? 0
	                   : ((i + step) % HUD_POS_COUNT + HUD_POS_COUNT)
	                     % HUD_POS_COUNT;

	cfg_set(&L, "position", hud_positions[next]);
	cfg_set(&L, "no_display", "0");

	int rc = hud_store(&L, path);
	if (rc == EX_OK)
		hud_said(hud_positions[next]);
	lines_free(&L);
	return rc;
}

static int hud_set_position(const char *pos)
{
	if (hud_position_index(pos) < 0) {
		/* ⚠ THE LIST IS THE IDs, UNTRANSLATED — it is what to type next. */
		fprintf(stderr, _("syn-arcade: unknown position '%s'. One of:"), pos);
		for (int i = 0; i < HUD_POS_COUNT; i++)
			fprintf(stderr, " %s", hud_positions[i]);
		fputc('\n', stderr);
		return EX_USAGE;
	}

	lines_t L = {0};
	char path[4096];
	hud_load(&L, path, sizeof(path));
	cfg_set(&L, "position", pos);
	int rc = hud_store(&L, path);
	if (rc == EX_OK)
		hud_said(pos);
	lines_free(&L);
	return rc;
}

static int hud_set_key(const char *key, const char *value)
{
	/* '#' would be swallowed as a comment by MangoHud's parser and silently
	 * truncate the value — and a newline would forge an extra setting. */
	if (strchr(value, '#') || strchr(value, '\n')) {
		fprintf(stderr, _("syn-arcade: '#' and newlines cannot appear in a "
		                  "MangoHud value\n"));
		return EX_USAGE;
	}

	lines_t L = {0};
	char path[4096];
	hud_load(&L, path, sizeof(path));
	cfg_set(&L, key, value);
	int rc = hud_store(&L, path);
	if (rc == EX_OK)
		printf("%s=%s\n", key, value);
	lines_free(&L);
	return rc;
}

/* The choice list the generic GUI asks for. */
static int hud_choices(const char *key)
{
	if (strcmp(key, "hud-position") != 0) {
		fprintf(stderr, _("syn-arcade: no choices for '%s'\n"), key);
		return EX_USAGE;
	}

	lines_t L = {0};
	char path[4096];
	hud_load(&L, path, sizeof(path));
	char cur[HUD_POS_MAX];
	hud_position(&L, cur, sizeof(cur));

	rec_row(3, "id", "label", "current");
	for (int i = 0; i < HUD_POS_COUNT; i++)
		rec_row(3, hud_positions[i], hud_position_labels[i],
			strcmp(hud_positions[i], cur) == 0 ? "current" : "-");

	lines_free(&L);
	return EX_OK;
}

/*
 * Make sure a config file exists before any game starts.
 *
 * This is not a nicety. MangoHud's inotify watch is added once, at layer init;
 * if the file is missing at that moment the watch fails and that process never
 * sees a config change again, so every keybind is dead for the whole session.
 */
static int hud_ensure(void)
{
	char path[4096];
	if (!hud_effective_config(path, sizeof(path)))
		return EX_FAIL;

	if (file_exists(path))
		return EX_OK;

	if (!file_writable(path)) {
		fprintf(stderr, _("syn-arcade: %s is missing and not creatable\n"), path);
		return EX_FAIL;
	}

	int rc = write_file_inplace(path, hud_default_config());
	if (rc < 0) {
		fprintf(stderr, _("syn-arcade: cannot create %s: %s\n"),
			path, strerror(-rc));
		return EX_FAIL;
	}
	printf(_("created %s\n"), path);
	return EX_OK;
}

/*
 * Take ownership of the overlay config.
 *
 * Copies whatever is CURRENTLY in effect — very often /etc/MangoHud.conf,
 * which outranks the user's file and is not writable by them — into
 * ~/.config/MangoHud/MangoHud.conf, so that the settings do not change at the
 * moment the precedence does. The session profile installed by this package
 * then points MANGOHUD_CONFIGFILE at that file, which collapses MangoHud's
 * whole candidate list to it.
 */
static int hud_adopt(void)
{
	char eff[4096], user[4096];
	if (!hud_effective_config(eff, sizeof(eff)) ||
	    !hud_user_config(user, sizeof(user)))
		return EX_FAIL;

	if (strcmp(eff, user) == 0 && file_exists(user)) {
		printf(_("already yours: %s\n"), user);
		return EX_OK;
	}

	char *src = read_file(eff);
	if (!src)
		src = xstrdup(hud_default_config());

	if (file_exists(user)) {
		printf(_("kept your existing %s (nothing copied over it)\n"), user);
		free(src);
	} else {
		int rc = write_file_inplace(user, src);
		free(src);
		if (rc < 0) {
			fprintf(stderr, _("syn-arcade: cannot write %s: %s\n"),
				user, strerror(-rc));
			return EX_FAIL;
		}
		printf(_("copied %s → %s\n"), eff, user);
	}

	const char *env = getenv("MANGOHUD_CONFIGFILE");
	if (!env || strcmp(env, user) != 0)
		printf(_("\nLog out and back in (or `export MANGOHUD_CONFIGFILE=%s`)\n"
		         "for this to take effect — /etc/MangoHud.conf outranks it until\n"
		         "that variable is set.\n"), user);
	return EX_OK;
}

static int hud_path(void)
{
	char eff[4096], user[4096];
	hud_effective_config(eff, sizeof(eff));
	hud_user_config(user, sizeof(user));

	const char *env = getenv("MANGOHUD_CONFIGFILE");

	printf(_("effective   %s\n"), eff);
	printf(_("writable    %s\n"), file_writable(eff) ? _("yes") : _("NO"));
	printf(_("yours       %s%s\n"), user,
	       file_exists(user) ? "" : _("   (does not exist)"));
	printf(_("pinned by   %s\n"),
	       (env && *env) ? "MANGOHUD_CONFIGFILE" : _("(nothing)"));

	if (!(env && *env) && file_exists("/etc/MangoHud.conf") &&
	    strcmp(eff, user) != 0) {
		printf(_("\n⚠ /etc/MangoHud.conf outranks your own config, so nothing you\n"
		         "  set as a user reaches MangoHud. `syn-arcade hud adopt` fixes it.\n"));
	}
	printf(_("\nNote: a game with its own ~/.config/MangoHud/<program>.conf or a\n"
	         "wine-<exe>.conf outranks all of the above, for that game only.\n"));
	return EX_OK;
}

/* ── dispatch ────────────────────────────────────────────────────────────── */

int cmd_hud(int argc, char **argv)
{
	if (argc < 1)
		return hud_show_state(false);

	const char *sub = argv[0];

	if (strcmp(sub, "--rec") == 0)	return hud_show_state(true);
	if (strcmp(sub, "state") == 0)	return hud_show_state(false);
	if (strcmp(sub, "toggle") == 0)	return hud_toggle();
	if (strcmp(sub, "show") == 0)	return hud_set_hidden(false);
	if (strcmp(sub, "hide") == 0)	return hud_set_hidden(true);
	if (strcmp(sub, "cycle") == 0)	return hud_cycle(+1);
	if (strcmp(sub, "cycle-back") == 0) return hud_cycle(-1);
	if (strcmp(sub, "ensure") == 0)	return hud_ensure();
	if (strcmp(sub, "adopt") == 0)	return hud_adopt();
	if (strcmp(sub, "path") == 0)	return hud_path();

	if (strcmp(sub, "position") == 0) {
		if (argc < 2) {
			lines_t L = {0};
			char path[4096], pos[HUD_POS_MAX];
			hud_load(&L, path, sizeof(path));
			hud_position(&L, pos, sizeof(pos));
			printf("%s\n", pos);
			lines_free(&L);
			return EX_OK;
		}
		return hud_set_position(argv[1]);
	}

	if (strcmp(sub, "choices") == 0) {
		if (argc < 2) return EX_USAGE;
		return hud_choices(argv[1]);
	}

	if (strcmp(sub, "set") == 0) {
		if (argc < 3) {
			fputs(_("syn-arcade: hud set <key> <value>\n"), stderr);
			return EX_USAGE;
		}
		return hud_set_key(argv[1], argv[2]);
	}

	fprintf(stderr, _("syn-arcade: unknown hud command '%s'\n"), sub);
	return EX_USAGE;
}
