/*
 * fit.c — gamescope wrappers: making a low-resolution game fill the screen.
 *
 * ── The problem this exists for ─────────────────────────────────────────────
 *
 * An old game renders at 640x480 or 1024x768 and has no idea what a 2560x1440
 * monitor is. Run it as it comes and you get a postage stamp in the middle of a
 * black screen. gamescope fixes that — it is a nested micro-compositor that
 * gives the game a display of exactly the size it wants and scales the result
 * up to the real one — and the line that does it is genuinely hard to remember:
 *
 *     gamescope -w 1024 -h 768 -W 2560 -H 1440 -f -F fsr -- wine Sims.exe
 *
 * ⚠ THE WHOLE TRAP IS THAT -w AND -W ARE DIFFERENT FLAGS. Lower case is the
 * size the GAME renders at; upper case is the size of the SCREEN it is stretched
 * onto. Swap them and the game renders at 2560x1440 (which is what you were
 * trying to avoid, and what most of these games cannot do at all) squeezed into
 * a 1024x768 window. Nothing warns; it just looks wrong in a way that is easy to
 * blame on the game.
 *
 * And then the line has to be kept somewhere. By hand that means writing a
 * .desktop file, getting Exec, Path, Icon and StartupWMClass right, and
 * remembering where it went the next time the resolution needs changing.
 *
 * So: `fit` keeps one small config file per wrapper, generates the command from
 * it, and writes the menu entry — and the desktop icon — that runs it.
 *
 * ── The .desktop points BACK at us, and that is the point ───────────────────
 *
 *     Exec=syn-arcade fit run <id>
 *
 * rather than the assembled gamescope line. It means editing a wrapper changes
 * what the menu entry does with no file in ~/.local/share/applications being
 * touched — which is the failure this suite has already been bitten by once:
 * editing a .desktop by hand and finding the menu still running the old command
 * because something else had cached it. The generated file carries the assembled
 * command as a COMMENT, so it is still readable by anybody who opens it, and
 * `fit command <id>` prints exactly what will run.
 *
 * ── Two refusals and one warning, all learned the hard way ──────────────────
 *
 *   · gamescope + Proton ABORTS INSTANTLY (SIGABRT, status 134). Lutris runs
 *     these games through umu/Proton, so wrapping a `lutris` command in
 *     gamescope produces a game that dies before it draws. Plain `wine` under
 *     gamescope is the combination that works. Warned about at create time,
 *     because the failure gives you nothing to go on.
 *   · A newline in any value would forge a second setting in the config file
 *     and a second line in the .desktop. Refused everywhere.
 *   · MangoHud under gamescope is `--mangoapp`, NOT `mangohud <command>`:
 *     gamescope composites the overlay itself, and the layer inside the game
 *     ends up scaled with the game — 20px text at 640x480 blown up to fill a
 *     wall.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "arcade.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>		/* strcasecmp, for sorting application names */
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define FIT_ID_MAX   64
#define FIT_VAL_MAX  1024
#define FIT_PATH_MAX 4096
#define FIT_ENV_MAX  12

/* How many wrappers and how many candidate applications are worth listing. Both
 * are bounds on somebody's own machine rather than on a protocol, so they are
 * generous and fixed — a list that reallocates to hold a thousand menu entries
 * is complexity bought for a case that does not happen. */
#define FIT_MAX      128
#define FIT_APPS_MAX 512

typedef struct {
	char id[FIT_ID_MAX];
	char name[FIT_VAL_MAX];
	char exec[FIT_VAL_MAX];
	char workdir[FIT_VAL_MAX];
	char env[FIT_ENV_MAX][FIT_VAL_MAX];
	int  envc;

	int  gw, gh;		/* what the game renders at   (-w/-h) */
	int  sw, sh;		/* what it is stretched onto  (-W/-H) */
	int  refresh;		/* -r; 0 = leave it to gamescope */

	char filter[16];	/* -F: fsr nis linear nearest pixel */
	char scaler[16];	/* -S: auto integer fit fill stretch */
	int  sharpness;		/* --sharpness 0-20; -1 = unset */

	bool fullscreen;
	bool force_win;		/* --force-windows-fullscreen */
	bool grab;		/* -g, the keyboard */
	bool overlay;		/* --mangoapp */
	bool gamemode;		/* gamemoderun, inside gamescope */

	char icon[FIT_VAL_MAX];
	char categories[FIT_VAL_MAX];

	bool menu;		/* an entry in the applications menu */
	bool desktop;		/* an icon on the desktop */
} fit_t;

/* The upscalers and scalers gamescope 3.16 accepts. Checked here rather than
 * left to gamescope, because a rejected flag makes it exit before the game
 * starts and the message scrolls past inside a launcher nobody is watching. */
static const char *const fit_filters[] = {
	"fsr", "nis", "linear", "nearest", "pixel", NULL
};
static const char *const fit_scalers[] = {
	"auto", "integer", "fit", "fill", "stretch", NULL
};

/* A bounded copy into one of the short fixed fields. snprintf("%s") would do
 * the same thing, and gcc rightly warns that a 1024-byte source cannot fit in
 * 16 — this says the truncation is the point. */
static void copy_short(char *dst, size_t n, const char *src)
{
	size_t l = strlen(src);
	if (l >= n)
		l = n - 1;
	memcpy(dst, src, l);
	dst[l] = '\0';
}

static bool in_list(const char *const *list, const char *s)
{
	for (int i = 0; list[i]; i++)
		if (strcmp(list[i], s) == 0)
			return true;
	return false;
}

/* ── where the pieces live ───────────────────────────────────────────────── */

static bool fit_conf_path(char *buf, size_t n, const char *id)
{
	char rel[FIT_ID_MAX + 32];
	snprintf(rel, sizeof(rel), "syn-arcade/fit/%s.conf", id);
	return config_path(buf, n, rel);
}

static bool fit_dir(char *buf, size_t n)
{
	return config_path(buf, n, "syn-arcade/fit");
}

/*
 * The menu entry.
 *
 * ~/.local/share/applications is the per-user half of XDG_DATA_DIRS, which is
 * what every menu on this desktop reads — synui's start menu builds itself from
 * Quickshell's DesktopEntries, which watches exactly that set. So a file written
 * here appears in the menu with nothing else to run and no cache to invalidate.
 *
 * ⚠ The basename is prefixed `syn-fit-` so that `fit remove` knows what it owns
 * and `fit apps` can leave our own wrappers out of the list of things to wrap.
 * The name shown in the menu is Name=, never the filename.
 */
static bool fit_menu_path(char *buf, size_t n, const char *id)
{
	char rel[FIT_ID_MAX + 64];
	snprintf(rel, sizeof(rel), "applications/syn-fit-%s.desktop", id);
	return data_path(buf, n, rel);
}

/*
 * The desktop icon.
 *
 * $HOME/Desktop, hard-coded, and deliberately NOT XDG_DESKTOP_DIR: synui draws
 * the desktop by scanning $HOME/Desktop (deskmenu.c), so that is the directory
 * an icon has to be in to appear. Honouring a user-dirs setting the compositor
 * does not read would put the file somewhere correct by the spec and invisible
 * on this desktop.
 */
static bool fit_icon_path(char *buf, size_t n, const char *id)
{
	char rel[FIT_ID_MAX + 64];
	snprintf(rel, sizeof(rel), "Desktop/syn-fit-%s.desktop", id);
	return home_path(buf, n, rel);
}

/* ── values ──────────────────────────────────────────────────────────────── */

/*
 * Every value ends up in three places that a newline would break differently:
 * the config file (where it would forge a second setting), the .desktop (a
 * second key), and the assembled shell line (a second command). One refusal
 * covers all three.
 */
static bool value_ok(const char *what, const char *v)
{
	if (strchr(v, '\n') || strchr(v, '\r')) {
		fprintf(stderr, "syn-arcade: %s cannot contain a newline\n", what);
		return false;
	}
	return true;
}

/* WxH, and nothing else. Returns false on anything this would otherwise pass to
 * gamescope as a size — "1024 x 768", "1024x", "-1x768". */
static bool parse_size(const char *s, int *w, int *h)
{
	if (!s || !*s)
		return false;

	char *end = NULL;
	long a = strtol(s, &end, 10);
	if (!end || (*end != 'x' && *end != 'X') || a < 1 || a > 16384)
		return false;

	char *end2 = NULL;
	long b = strtol(end + 1, &end2, 10);
	if (!end2 || *end2 || b < 1 || b > 16384)
		return false;

	*w = (int)a;
	*h = (int)b;
	return true;
}

/* ── the config file ─────────────────────────────────────────────────────── */

static void fit_defaults(fit_t *f)
{
	memset(f, 0, sizeof(*f));
	f->sharpness  = -1;
	f->fullscreen = true;
	f->menu       = true;
	snprintf(f->filter, sizeof(f->filter), "fsr");
	snprintf(f->categories, sizeof(f->categories), "Game;");
}

static bool truthy(const char *v)
{
	return strcmp(v, "yes") == 0 || strcmp(v, "true") == 0 ||
	       strcmp(v, "on") == 0  || strcmp(v, "1") == 0;
}

static void fit_set_key(fit_t *f, const char *key, const char *val)
{
	if (strcmp(key, "name") == 0)		snprintf(f->name, sizeof(f->name), "%s", val);
	else if (strcmp(key, "exec") == 0)	snprintf(f->exec, sizeof(f->exec), "%s", val);
	else if (strcmp(key, "workdir") == 0)	snprintf(f->workdir, sizeof(f->workdir), "%s", val);
	else if (strcmp(key, "icon") == 0)	snprintf(f->icon, sizeof(f->icon), "%s", val);
	else if (strcmp(key, "categories") == 0)snprintf(f->categories, sizeof(f->categories), "%s", val);
	else if (strcmp(key, "filter") == 0)	snprintf(f->filter, sizeof(f->filter), "%s", val);
	else if (strcmp(key, "scaler") == 0)	snprintf(f->scaler, sizeof(f->scaler), "%s", val);
	else if (strcmp(key, "game") == 0)	parse_size(val, &f->gw, &f->gh);
	else if (strcmp(key, "screen") == 0)	parse_size(val, &f->sw, &f->sh);
	else if (strcmp(key, "refresh") == 0)	f->refresh = (int)strtol(val, NULL, 10);
	else if (strcmp(key, "sharpness") == 0)	f->sharpness = *val ? (int)strtol(val, NULL, 10) : -1;
	else if (strcmp(key, "fullscreen") == 0)f->fullscreen = truthy(val);
	else if (strcmp(key, "force_window") == 0) f->force_win = truthy(val);
	else if (strcmp(key, "grab") == 0)	f->grab = truthy(val);
	else if (strcmp(key, "overlay") == 0)	f->overlay = truthy(val);
	else if (strcmp(key, "gamemode") == 0)	f->gamemode = truthy(val);
	else if (strcmp(key, "menu") == 0)	f->menu = truthy(val);
	else if (strcmp(key, "desktop") == 0)	f->desktop = truthy(val);
	else if (strcmp(key, "env") == 0) {
		if (*val && f->envc < FIT_ENV_MAX)
			snprintf(f->env[f->envc++], FIT_VAL_MAX, "%s", val);
	}
	/* An unknown key is left alone rather than refused: a config written by a
	 * newer syn-arcade must not stop an older one from launching the game. */
}

static bool fit_load(const char *id, fit_t *f)
{
	fit_defaults(f);
	snprintf(f->id, sizeof(f->id), "%s", id);

	char path[FIT_PATH_MAX];
	if (!fit_conf_path(path, sizeof(path), id))
		return false;

	char *text = read_file(path);
	if (!text)
		return false;

	/* The filter default only applies to a wrapper this has never seen. A
	 * loaded one says what it wants, including "no filter at all". */
	f->filter[0] = '\0';

	char *save = NULL;
	for (char *ln = strtok_r(text, "\n", &save); ln;
	     ln = strtok_r(NULL, "\n", &save)) {
		char *t = trim(ln);
		if (!*t || *t == '#')
			continue;
		char *eq = strchr(t, '=');	/* the FIRST '=': env values hold one */
		if (!eq)
			continue;
		*eq = '\0';
		fit_set_key(f, trim(t), trim(eq + 1));
	}
	free(text);
	return true;
}

static int fit_store(const fit_t *f)
{
	char path[FIT_PATH_MAX];
	if (!fit_conf_path(path, sizeof(path), f->id))
		return EX_FAIL;

	char *out = NULL;
	size_t len = 0;
	FILE *m = open_memstream(&out, &len);
	if (!m) {
		fputs("syn-arcade: out of memory\n", stderr);
		return EX_FAIL;
	}

	fprintf(m,
"# A gamescope wrapper, managed by `syn-arcade fit`.\n"
"#\n"
"# game   = what the game renders at   (gamescope -w/-h)\n"
"# screen = what it is stretched onto  (gamescope -W/-H)\n"
"#\n"
"# Safe to edit by hand; run `syn-arcade fit apply %s` afterwards to rewrite\n"
"# the menu entry from it. `syn-arcade fit command %s` prints the line this\n"
"# produces.\n"
"name=%s\n"
"exec=%s\n", f->id, f->id, f->name, f->exec);

	if (f->workdir[0])	fprintf(m, "workdir=%s\n", f->workdir);
	for (int i = 0; i < f->envc; i++)
		fprintf(m, "env=%s\n", f->env[i]);
	if (f->gw && f->gh)	fprintf(m, "game=%dx%d\n", f->gw, f->gh);
	if (f->sw && f->sh)	fprintf(m, "screen=%dx%d\n", f->sw, f->sh);
	if (f->refresh > 0)	fprintf(m, "refresh=%d\n", f->refresh);
	if (f->filter[0])	fprintf(m, "filter=%s\n", f->filter);
	if (f->scaler[0])	fprintf(m, "scaler=%s\n", f->scaler);
	if (f->sharpness >= 0)	fprintf(m, "sharpness=%d\n", f->sharpness);

	fprintf(m, "fullscreen=%s\n", f->fullscreen ? "yes" : "no");
	fprintf(m, "force_window=%s\n", f->force_win ? "yes" : "no");
	fprintf(m, "grab=%s\n", f->grab ? "yes" : "no");
	fprintf(m, "overlay=%s\n", f->overlay ? "yes" : "no");
	fprintf(m, "gamemode=%s\n", f->gamemode ? "yes" : "no");

	if (f->icon[0])		fprintf(m, "icon=%s\n", f->icon);
	if (f->categories[0])	fprintf(m, "categories=%s\n", f->categories);
	fprintf(m, "menu=%s\n", f->menu ? "yes" : "no");
	fprintf(m, "desktop=%s\n", f->desktop ? "yes" : "no");

	fclose(m);

	int rc = write_file_inplace(path, out);
	free(out);
	if (rc < 0) {
		fprintf(stderr, "syn-arcade: cannot write %s: %s\n",
			path, strerror(-rc));
		return EX_FAIL;
	}
	return EX_OK;
}

/* ── the command ─────────────────────────────────────────────────────────── */

/*
 * A value going into the shell line, single-quoted.
 *
 * Only paths and environment assignments go through this. The game's own
 * command does NOT: it is a command line the person wrote — `env WINEPREFIX=…
 * wine game.exe`, `./game.x86_64 -windowed` — and quoting it would turn the
 * whole thing into the name of a program that does not exist. That is the one
 * place in this file where the config is trusted, and it is trusted for the
 * same reason a .desktop Exec is: writing it IS asking for it to be run.
 */
static char *shq(const char *s)
{
	size_t n = strlen(s);
	char *out = xmalloc(n * 4 + 3);
	char *w = out;
	*w++ = '\'';
	for (const char *p = s; *p; p++) {
		if (*p == '\'') {		/* '\'' — close, escape, reopen */
			memcpy(w, "'\\''", 4);
			w += 4;
		} else {
			*w++ = *p;
		}
	}
	*w++ = '\'';
	*w = '\0';
	return out;
}

typedef struct { char *s; size_t len, cap; } sb_t;

static void sb_add(sb_t *b, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	char tmp[FIT_VAL_MAX * 2];
	int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;

	size_t add = strlen(tmp);
	if (b->len + add + 1 > b->cap) {
		b->cap = (b->len + add + 1) * 2;
		b->s = xrealloc(b->s, b->cap);
	}
	memcpy(b->s + b->len, tmp, add + 1);
	b->len += add;
}

/*
 * The whole command line, exactly as `fit run` will hand it to /bin/sh.
 *
 * Assembled in the order the pieces nest, outside in:
 *
 *   cd <workdir> && exec env <vars> gamescope <flags> -- gamemoderun <game>
 *
 * ⚠ The environment goes OUTSIDE gamescope, not next to the game. It reaches
 * the game either way — gamescope passes its environment down — and outside is
 * where the lines that are known to work put it, so a command copied out of
 * `fit command` matches what somebody would have written by hand.
 */
static char *fit_command(const fit_t *f)
{
	sb_t b = {0};

	if (f->workdir[0]) {
		char *q = shq(f->workdir);
		sb_add(&b, "cd %s && ", q);
		free(q);
	}

	sb_add(&b, "exec ");

	if (f->envc > 0) {
		sb_add(&b, "env");
		for (int i = 0; i < f->envc; i++) {
			char *q = shq(f->env[i]);
			sb_add(&b, " %s", q);
			free(q);
		}
		sb_add(&b, " ");
	}

	sb_add(&b, "gamescope");

	/* Lower case is the GAME's size; upper case is the SCREEN's. */
	if (f->gw && f->gh)	sb_add(&b, " -w %d -h %d", f->gw, f->gh);
	if (f->sw && f->sh)	sb_add(&b, " -W %d -H %d", f->sw, f->sh);
	if (f->refresh > 0)	sb_add(&b, " -r %d", f->refresh);
	if (f->fullscreen)	sb_add(&b, " -f");
	if (f->grab)		sb_add(&b, " -g");
	if (f->scaler[0])	sb_add(&b, " -S %s", f->scaler);
	if (f->filter[0])	sb_add(&b, " -F %s", f->filter);
	if (f->sharpness >= 0)	sb_add(&b, " --sharpness %d", f->sharpness);
	if (f->force_win)	sb_add(&b, " --force-windows-fullscreen");
	if (f->overlay)		sb_add(&b, " --mangoapp");

	sb_add(&b, " -- ");
	if (f->gamemode)
		sb_add(&b, "gamemoderun ");
	sb_add(&b, "%s", f->exec);

	return b.s;
}

/* ── the .desktop files ──────────────────────────────────────────────────── */

/*
 * What the entry says under the name. The resolutions are the whole reason the
 * wrapper exists, so they are what it says — a Comment reading "launch the
 * game" would be a line of text nobody can act on.
 */
static void fit_comment(const fit_t *f, char *buf, size_t n)
{
	if (f->gw && f->sw)
		snprintf(buf, n, "%dx%d upscaled to %dx%d with gamescope%s%s",
			 f->gw, f->gh, f->sw, f->sh,
			 f->filter[0] ? " + " : "", f->filter);
	else if (f->sw)
		snprintf(buf, n, "Through gamescope at %dx%d", f->sw, f->sh);
	else
		snprintf(buf, n, "Through gamescope");
}

static char *fit_desktop_text(const fit_t *f)
{
	char *cmd = fit_command(f);
	char comment[512];
	fit_comment(f, comment, sizeof(comment));

	char *out = NULL;
	size_t len = 0;
	FILE *m = open_memstream(&out, &len);
	if (!m) {
		free(cmd);
		fputs("syn-arcade: out of memory\n", stderr);
		exit(EX_FAIL);
	}

	fprintf(m,
"[Desktop Entry]\n"
"# Written by syn-arcade. Do not edit this file: it is regenerated from\n"
"# ~/.config/syn-arcade/fit/%s.conf every time the wrapper changes, and\n"
"# anything added here is lost the next time. Change it with:\n"
"#\n"
"#     syn-arcade fit edit %s --game=WxH --screen=WxH …\n"
"#\n"
"# or in the Fit to screen tab of `syn-arcade gui`.\n"
"#\n"
"# What Exec below ends up running:\n"
"#\n"
"#     %s\n"
"#\n"
"# ⚠ Exec points back at syn-arcade rather than carrying that line, so that\n"
"# editing the wrapper changes what this entry does without this file — or any\n"
"# menu built from it — having to be rewritten.\n"
"Type=Application\n"
"Name=%s\n"
"Comment=%s\n"
"Exec=syn-arcade fit run %s\n",
		f->id, f->id, cmd, f->name, comment, f->id);

	if (f->workdir[0])
		fprintf(m, "Path=%s\n", f->workdir);
	if (f->icon[0])
		fprintf(m, "Icon=%s\n", f->icon);

	fprintf(m,
"Terminal=false\n"
"StartupNotify=true\n"
/* Every gamescope window has this app_id — it is gamescope's window, not the
 * game's — so this maps the window back to some entry rather than to this one.
 * It is still worth setting: without it the window resolves to nothing at all. */
"StartupWMClass=gamescope\n"
"Categories=%s\n", f->categories[0] ? f->categories : "Game;");

	fclose(m);
	free(cmd);
	return out;
}

/*
 * Ask synui to re-read ~/Desktop.
 *
 * ⚠ There is no inotify watch on that directory (synui's deskmenu.c says so in
 * as many words), so a file written into it does not appear until something
 * makes the compositor rescan. Without this the "Put an icon on the desktop"
 * checkbox is the exact shape of button this project keeps writing memos about:
 * it does everything correctly and puts nothing on screen.
 *
 * Best effort in both directions — no synui, no synctl, or a synui too old to
 * know the action all mean the icon appears at the next login instead, which is
 * what the caller says.
 */
static void deskicons_refresh(void)
{
	int rc = system("synctl dispatch deskicons_refresh >/dev/null 2>&1");
	(void)rc;
}

static int fit_apply(const fit_t *f, bool quiet)
{
	char menu[FIT_PATH_MAX], icon[FIT_PATH_MAX];
	if (!fit_menu_path(menu, sizeof(menu), f->id) ||
	    !fit_icon_path(icon, sizeof(icon), f->id)) {
		fputs("syn-arcade: cannot resolve where to write the shortcut "
		      "(is HOME set?)\n", stderr);
		return EX_FAIL;
	}

	char *text = fit_desktop_text(f);
	int rc = EX_OK;

	if (f->menu) {
		int w = write_file_inplace(menu, text);
		if (w < 0) {
			fprintf(stderr, "syn-arcade: cannot write %s: %s\n",
				menu, strerror(-w));
			rc = EX_FAIL;
		} else if (!quiet) {
			printf("menu     %s\n", menu);
		}
	} else if (file_exists(menu)) {
		unlink(menu);
	}

	if (f->desktop) {
		int w = write_file_inplace(icon, text);
		if (w < 0) {
			fprintf(stderr, "syn-arcade: cannot write %s: %s\n",
				icon, strerror(-w));
			rc = EX_FAIL;
		} else {
			/* A desktop icon is launched by a double click, and
			 * plenty of shells refuse to run one that is not
			 * executable. The menu copy is left 0644 — nothing
			 * execs that one directly. */
			chmod(icon, 0755);
			if (!quiet)
				printf("desktop  %s\n", icon);
			deskicons_refresh();
		}
	} else if (file_exists(icon)) {
		unlink(icon);
		deskicons_refresh();
	}

	free(text);
	return rc;
}

/* ── the list ────────────────────────────────────────────────────────────── */

static int id_cmp(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

/* Every wrapper's id, in a stable order — readdir's is whatever the filesystem
 * felt like, which would reshuffle the list on every refresh. */
static int fit_ids(char ids[][FIT_ID_MAX], int max)
{
	char dir[FIT_PATH_MAX];
	if (!fit_dir(dir, sizeof(dir)))
		return 0;

	DIR *d = opendir(dir);
	if (!d)
		return 0;

	int n = 0;
	struct dirent *de;
	while ((de = readdir(d)) && n < max) {
		size_t len = strlen(de->d_name);
		if (len <= 5 || strcmp(de->d_name + len - 5, ".conf") != 0)
			continue;
		if (len - 5 >= FIT_ID_MAX)
			continue;
		memcpy(ids[n], de->d_name, len - 5);
		ids[n][len - 5] = '\0';
		n++;
	}
	closedir(d);

	qsort(ids, (size_t)n, FIT_ID_MAX, id_cmp);
	return n;
}

static void fit_size_str(int w, int h, char *buf, size_t n)
{
	if (w && h)
		snprintf(buf, n, "%dx%d", w, h);
	else
		snprintf(buf, n, "—");
}

static int fit_list(bool rec)
{
	char ids[FIT_MAX][FIT_ID_MAX];
	int n = fit_ids(ids, FIT_MAX);

	if (rec)
		rec_row(8, "id", "name", "game", "screen", "filter",
			   "menu", "desktop", "command");

	if (n == 0) {
		if (!rec)
			puts("No gamescope wrappers yet.\n"
			     "\n"
			     "`syn-arcade fit new --name=\"The Sims\" "
			     "--exec=\"wine Sims.exe\" --game=1024x768`\n"
			     "makes one, or use the Fit to screen tab of "
			     "`syn-arcade gui`.");
		return EX_EMPTY;
	}

	for (int i = 0; i < n; i++) {
		fit_t f;
		if (!fit_load(ids[i], &f))
			continue;

		char g[32], s[32];
		fit_size_str(f.gw, f.gh, g, sizeof(g));
		fit_size_str(f.sw, f.sh, s, sizeof(s));
		char *cmd = fit_command(&f);

		if (rec)
			rec_row(8, f.id, f.name, g, s,
				f.filter[0] ? f.filter : "—",
				f.menu ? "yes" : "no",
				f.desktop ? "yes" : "no", cmd);
		else
			printf("%-24s %-11s → %-11s %-8s %s\n",
			       f.id, g, s, f.filter[0] ? f.filter : "-", f.name);

		free(cmd);
	}
	return EX_OK;
}

static int fit_print(const fit_t *fp, bool rec)
{
	fit_t f = *fp;
	char g[32], s[32], sharp[16], ref[16];
	fit_size_str(f.gw, f.gh, g, sizeof(g));
	fit_size_str(f.sw, f.sh, s, sizeof(s));
	snprintf(sharp, sizeof(sharp), "%d", f.sharpness);
	snprintf(ref, sizeof(ref), "%d", f.refresh);
	char *cmd = fit_command(&f);

	char menu[FIT_PATH_MAX] = "", icon[FIT_PATH_MAX] = "";
	fit_menu_path(menu, sizeof(menu), f.id);
	fit_icon_path(icon, sizeof(icon), f.id);

	if (rec) {
		/* Field names are the CONFIG's own keys, so the window can hand
		 * each one straight back to `fit edit --<key>=` without a
		 * translation table in the QML to fall out of step. */
		rec_row(2, "field", "value");
		rec_row(2, "id", f.id);
		rec_row(2, "name", f.name);
		rec_row(2, "exec", f.exec);
		rec_row(2, "workdir", f.workdir);
		rec_row(2, "game", (f.gw && f.gh) ? g : "");
		rec_row(2, "screen", (f.sw && f.sh) ? s : "");
		rec_row(2, "refresh", f.refresh > 0 ? ref : "");
		rec_row(2, "filter", f.filter);
		rec_row(2, "scaler", f.scaler);
		rec_row(2, "sharpness", f.sharpness >= 0 ? sharp : "");
		rec_row(2, "fullscreen", f.fullscreen ? "yes" : "no");
		rec_row(2, "force_window", f.force_win ? "yes" : "no");
		rec_row(2, "grab", f.grab ? "yes" : "no");
		rec_row(2, "overlay", f.overlay ? "yes" : "no");
		rec_row(2, "gamemode", f.gamemode ? "yes" : "no");
		rec_row(2, "icon", f.icon);
		rec_row(2, "categories", f.categories);
		rec_row(2, "menu", f.menu ? "yes" : "no");
		rec_row(2, "desktop", f.desktop ? "yes" : "no");
		rec_row(2, "command", cmd);
		for (int i = 0; i < f.envc; i++)
			rec_row(2, "env", f.env[i]);
	} else {
		printf("name      %s\n", f.name);
		printf("game      %s\n", g);
		printf("screen    %s\n", s);
		printf("filter    %s\n", f.filter[0] ? f.filter : "(none)");
		for (int i = 0; i < f.envc; i++)
			printf("env       %s\n", f.env[i]);
		if (f.workdir[0])
			printf("workdir   %s\n", f.workdir);
		/* An id means this has been created. `fit inspect` prints the
		 * same record for something that has not, and naming files
		 * that do not exist there would read as a wrapper that already
		 * existed. */
		if (f.id[0]) {
			printf("menu      %s\n",
			       f.menu ? menu : "(not in the menu)");
			printf("desktop   %s\n",
			       f.desktop ? icon : "(no desktop icon)");
		}
		printf("\n%s\n", cmd);
	}

	free(cmd);
	return EX_OK;
}

static int fit_show(const char *id, bool rec)
{
	fit_t f;
	if (!fit_load(id, &f)) {
		fprintf(stderr, "syn-arcade: no wrapper called '%s'\n", id);
		return EX_FAIL;
	}
	return fit_print(&f, rec);
}

/* ── the screens, for a sensible default ─────────────────────────────────── */

/*
 * What `--screen` should be, asked of the compositor.
 *
 * ⚠ This is the question the person making a wrapper gets wrong most often, and
 * it is the one the machine can answer for itself: the screen size is not a
 * preference, it is a fact about the monitor. `synctl outputs` prints one JSON
 * object per output with its logical size, so the default is the primary
 * output's — which is also the screen synui puts game windows on
 * (game_output defaults to primary), so the wrapper and the compositor agree
 * about which monitor is the gaming one.
 *
 * big.c reads the same command for a different field and keeps its own parser;
 * two small readers of one shape beat one reader with a mode flag, and neither
 * grows a JSON library for an output this file's own project prints.
 */
static int screens_scan(char names[][64], int *w, int *h, bool *primary, int max)
{
	FILE *p = popen("synctl outputs 2>/dev/null", "r");
	if (!p)
		return 0;

	char json[4096];
	size_t got = fread(json, 1, sizeof(json) - 1, p);
	pclose(p);
	if (got == 0)
		return 0;
	json[got] = '\0';

	int n = 0;
	for (char *obj = strtok(json, "{"); obj && n < max;
	     obj = strtok(NULL, "{")) {
		char *nm = strstr(obj, "\"name\":\"");
		char *sz = strstr(obj, "\"size\":[");
		if (!nm || !sz)
			continue;

		nm += 8;
		char *end = strchr(nm, '"');
		if (!end)
			continue;
		*end = '\0';

		int ww = 0, hh = 0;
		if (sscanf(sz + 8, "%d,%d", &ww, &hh) != 2 || ww < 1 || hh < 1)
			continue;

		snprintf(names[n], 64, "%s", nm);
		w[n] = ww;
		h[n] = hh;
		primary[n] = strstr(end + 1, "\"primary\":true") != NULL;
		n++;
	}
	return n;
}

static bool primary_size(int *w, int *h)
{
	char names[8][64];
	int ws[8], hs[8];
	bool pr[8];
	int n = screens_scan(names, ws, hs, pr, 8);
	for (int i = 0; i < n; i++) {
		if (!pr[i])
			continue;
		*w = ws[i];
		*h = hs[i];
		return true;
	}
	if (n > 0) {
		*w = ws[0];
		*h = hs[0];
		return true;
	}
	return false;
}

static int fit_screens(bool rec)
{
	char names[8][64];
	int ws[8], hs[8];
	bool pr[8];
	int n = screens_scan(names, ws, hs, pr, 8);

	if (rec)
		rec_row(4, "name", "size", "primary", "label");

	if (n == 0) {
		if (!rec)
			fputs("syn-arcade: cannot ask the compositor which "
			      "screens are attached (no synctl, or synui is not "
			      "running)\n", stderr);
		return EX_EMPTY;
	}

	for (int i = 0; i < n; i++) {
		char size[32], label[128];
		snprintf(size, sizeof(size), "%dx%d", ws[i], hs[i]);
		snprintf(label, sizeof(label), "%s — %s%s", names[i], size,
			 pr[i] ? " (primary)" : "");
		if (rec)
			rec_row(4, names[i], size, pr[i] ? "yes" : "no", label);
		else
			printf("%s\n", label);
	}
	return EX_OK;
}

/* ── the applications to wrap ────────────────────────────────────────────── */

typedef struct {
	char path[FIT_PATH_MAX];
	char name[FIT_VAL_MAX];
	char exec[FIT_VAL_MAX];
	char workdir[FIT_VAL_MAX];
	char icon[FIT_VAL_MAX];
	char categories[FIT_VAL_MAX];
	bool game;
} app_t;

/*
 * Read the first group of a .desktop file.
 *
 * Only [Desktop Entry] is read: the Actions groups below it carry their own
 * Name and Exec, and taking those would wrap "Open a new window" instead of the
 * program. Localised keys (Name[de]) are skipped for the same reason — the
 * bracket is what distinguishes them.
 */
static bool desktop_read(const char *path, app_t *a)
{
	char *text = read_file(path);
	if (!text)
		return false;

	memset(a, 0, sizeof(*a));
	snprintf(a->path, sizeof(a->path), "%s", path);

	bool in_entry = false, ok = false, hidden = false;
	char *save = NULL;
	for (char *ln = strtok_r(text, "\n", &save); ln;
	     ln = strtok_r(NULL, "\n", &save)) {
		char *t = trim(ln);
		if (*t == '#' || !*t)
			continue;
		if (*t == '[') {
			if (in_entry)
				break;			/* past our group */
			in_entry = strcmp(t, "[Desktop Entry]") == 0;
			continue;
		}
		if (!in_entry)
			continue;

		char *eq = strchr(t, '=');
		if (!eq)
			continue;
		*eq = '\0';
		char *key = trim(t), *val = trim(eq + 1);
		if (strchr(key, '['))
			continue;			/* Name[de] and friends */

		if (strcmp(key, "Type") == 0)
			ok = strcmp(val, "Application") == 0;
		else if (strcmp(key, "NoDisplay") == 0 || strcmp(key, "Hidden") == 0)
			hidden = hidden || truthy(val);
		else if (strcmp(key, "Name") == 0)
			snprintf(a->name, sizeof(a->name), "%s", val);
		else if (strcmp(key, "Exec") == 0)
			snprintf(a->exec, sizeof(a->exec), "%s", val);
		else if (strcmp(key, "Path") == 0)
			snprintf(a->workdir, sizeof(a->workdir), "%s", val);
		else if (strcmp(key, "Icon") == 0)
			snprintf(a->icon, sizeof(a->icon), "%s", val);
		else if (strcmp(key, "Categories") == 0)
			snprintf(a->categories, sizeof(a->categories), "%s", val);
	}
	free(text);

	if (!ok || hidden || !a->name[0] || !a->exec[0])
		return false;

	/* Our own wrappers are not candidates to wrap. Matched on what they RUN
	 * rather than on the filename, so a wrapper somebody renamed is still
	 * recognised. */
	if (strstr(a->exec, "syn-arcade fit run"))
		return false;

	a->game = strstr(a->categories, "Game") != NULL;
	return true;
}

/*
 * Strip the field codes out of an Exec.
 *
 * %f %F %u %U are file and URL placeholders the launcher is supposed to
 * substitute; %i %c %k are the icon, the name and the file path. Left in, they
 * arrive at the shell as literal `%U`, which wine and most native games treat
 * as a filename to open — a game that starts on an error dialog instead of the
 * main menu, from a wrapper that otherwise looks right.
 *
 * `%%` is a literal per cent and survives.
 */
static void exec_strip_codes(char *s)
{
	char *w = s;
	for (char *p = s; *p; p++) {
		if (*p != '%') {
			*w++ = *p;
			continue;
		}
		if (p[1] == '%') {
			*w++ = '%';
			p++;
			continue;
		}
		if (strchr("fFuUickdDnNvm", p[1]) && p[1]) {
			p++;
			continue;
		}
		*w++ = *p;
	}
	*w = '\0';

	/* The removals leave double spaces and a trailing one behind. */
	char *r = s;
	w = s;
	bool sp = false;
	for (; *r; r++) {
		if (*r == ' ') {
			sp = true;
			continue;
		}
		if (sp && w != s)
			*w++ = ' ';
		sp = false;
		*w++ = *r;
	}
	*w = '\0';
}

static int app_cmp(const void *a, const void *b)
{
	const app_t *x = a, *y = b;
	if (x->game != y->game)		/* games first — that is what this is for */
		return x->game ? -1 : 1;
	return strcasecmp(x->name, y->name);
}

static void apps_walk(const char *dir, app_t *apps, int *n, int max, int depth)
{
	if (*n >= max || depth > 4)
		return;

	DIR *d = opendir(dir);
	if (!d)
		return;

	struct dirent *de;
	while ((de = readdir(d)) && *n < max) {
		if (de->d_name[0] == '.')
			continue;

		char path[FIT_PATH_MAX];
		if (snprintf(path, sizeof(path), "%s/%s", dir, de->d_name) >=
		    (int)sizeof(path))
			continue;

		struct stat st;
		if (stat(path, &st) != 0)
			continue;

		/* ⚠ RECURSIVE, because Wine writes its shortcuts into
		 * applications/wine/Programs/<publisher>/… rather than flat.
		 * Those are exactly the entries somebody wants to wrap — a
		 * scanner that stops at the top level finds Steam games and
		 * none of the old Windows ones. */
		if (S_ISDIR(st.st_mode)) {
			apps_walk(path, apps, n, max, depth + 1);
			continue;
		}

		size_t len = strlen(de->d_name);
		if (len <= 8 || strcmp(de->d_name + len - 8, ".desktop") != 0)
			continue;

		app_t a;
		if (!desktop_read(path, &a))
			continue;
		exec_strip_codes(a.exec);

		/* One entry per Name: the same program is routinely installed
		 * in /usr/share and shadowed in ~/.local/share, and two rows
		 * that differ only in a path nobody sees is a list that looks
		 * broken. The user's copy is met first (see the search order)
		 * and wins. */
		bool dup = false;
		for (int i = 0; i < *n; i++)
			if (strcmp(apps[i].name, a.name) == 0) {
				dup = true;
				break;
			}
		if (dup)
			continue;

		apps[(*n)++] = a;
	}
	closedir(d);
}

static int fit_apps(bool rec, bool games_only)
{
	app_t *apps = xmalloc(sizeof(app_t) * FIT_APPS_MAX);
	int n = 0;

	/* The user's own entries first, so their copy is the one kept when a
	 * name appears twice. */
	char dir[FIT_PATH_MAX];
	if (data_path(dir, sizeof(dir), "applications"))
		apps_walk(dir, apps, &n, FIT_APPS_MAX, 0);
	apps_walk("/usr/local/share/applications", apps, &n, FIT_APPS_MAX, 0);
	apps_walk("/usr/share/applications", apps, &n, FIT_APPS_MAX, 0);

	qsort(apps, (size_t)n, sizeof(app_t), app_cmp);

	if (rec)
		rec_row(7, "id", "name", "exec", "workdir", "icon",
			   "categories", "kind");

	int shown = 0;
	for (int i = 0; i < n; i++) {
		if (games_only && !apps[i].game)
			continue;
		shown++;
		if (rec)
			rec_row(7, apps[i].path, apps[i].name, apps[i].exec,
				apps[i].workdir, apps[i].icon,
				apps[i].categories,
				apps[i].game ? "game" : "app");
		else
			printf("%-40s %s\n", apps[i].name, apps[i].exec);
	}

	free(apps);
	return shown ? EX_OK : EX_EMPTY;
}

/* ── ids ─────────────────────────────────────────────────────────────────── */

/*
 * A name turned into a filename: lower case, one dash for any run of anything
 * else. "The Sims (Fullscreen)" → "the-sims-fullscreen".
 *
 * ⚠ It has to be safe as a PATH COMPONENT, not merely tidy — the id is
 * interpolated into ~/.config/syn-arcade/fit/<id>.conf and into the .desktop
 * basename. Keeping only [a-z0-9-] means a name containing a slash or a `..`
 * cannot reach outside those directories.
 */
static void slugify(const char *name, char *buf, size_t n)
{
	size_t w = 0;
	bool dash = false;
	for (const char *p = name; *p && w + 1 < n && w < 48; p++) {
		unsigned char c = (unsigned char)*p;
		if (isalnum(c)) {
			buf[w++] = (char)tolower(c);
			dash = false;
		} else if (!dash && w > 0) {
			buf[w++] = '-';
			dash = true;
		}
	}
	while (w > 0 && buf[w - 1] == '-')
		w--;
	buf[w] = '\0';
	if (!w)
		snprintf(buf, n, "game");
}

static bool id_taken(const char *id)
{
	char path[FIT_PATH_MAX];
	return fit_conf_path(path, sizeof(path), id) && file_exists(path);
}

static void unique_id(const char *base, char *buf, size_t n)
{
	snprintf(buf, n, "%s", base);
	for (int i = 2; id_taken(buf) && i < 100; i++)
		snprintf(buf, n, "%s-%d", base, i);
}

/* ── new and edit ────────────────────────────────────────────────────────── */

/*
 * Apply the command-line flags to a wrapper.
 *
 * ONE function for `new` and `edit`, because they are the same question asked
 * of a blank record or a loaded one — and because the alternative, two flag
 * parsers, is how a field ends up settable at creation and not afterwards.
 *
 * A flag given as `--x=` with nothing after it CLEARS that field. That is the
 * only way to say "no filter", "no working directory", "no environment" from a
 * window whose text boxes can be emptied.
 */
static bool fit_flags(fit_t *f, int argc, char **argv)
{
	bool env_seen = false;

	for (int i = 0; i < argc; i++) {
		const char *arg = argv[i];
		if (strncmp(arg, "--", 2) != 0)
			continue;

		const char *eq = strchr(arg, '=');
		char key[64];
		size_t klen = eq ? (size_t)(eq - arg - 2) : strlen(arg) - 2;
		if (klen >= sizeof(key))
			continue;
		memcpy(key, arg + 2, klen);
		key[klen] = '\0';

		/* A bare `--grab` means yes; `--grab=no` means no. */
		const char *val = eq ? eq + 1 : "yes";

		if (!strcmp(key, "rec") || !strcmp(key, "detach") ||
		    !strcmp(key, "games") || !strcmp(key, "from") ||
		    !strcmp(key, "id"))
			continue;			/* handled by the caller */

		if (!value_ok(key, val))
			return false;

		if (!strcmp(key, "game") || !strcmp(key, "screen")) {
			int w = 0, h = 0;
			if (*val && strcmp(val, "none") != 0 &&
			    !parse_size(val, &w, &h)) {
				fprintf(stderr, "syn-arcade: --%s wants WxH, "
					"like --%s=1024x768 (or `none`)\n",
					key, key);
				return false;
			}
			if (!strcmp(key, "game")) { f->gw = w; f->gh = h; }
			else			  { f->sw = w; f->sh = h; }
			continue;
		}

		if (!strcmp(key, "filter")) {
			if (*val && !in_list(fit_filters, val)) {
				fprintf(stderr, "syn-arcade: unknown filter "
					"'%s'. One of: fsr nis linear nearest "
					"pixel\n", val);
				return false;
			}
			snprintf(f->filter, sizeof(f->filter), "%s", val);
			continue;
		}

		if (!strcmp(key, "scaler")) {
			if (*val && !in_list(fit_scalers, val)) {
				fprintf(stderr, "syn-arcade: unknown scaler "
					"'%s'. One of: auto integer fit fill "
					"stretch\n", val);
				return false;
			}
			snprintf(f->scaler, sizeof(f->scaler), "%s", val);
			continue;
		}

		if (!strcmp(key, "sharpness")) {
			if (!*val) {
				f->sharpness = -1;
				continue;
			}
			long s = strtol(val, NULL, 10);
			if (s < 0 || s > 20) {
				fputs("syn-arcade: --sharpness is 0 (sharpest) "
				      "to 20 (softest)\n", stderr);
				return false;
			}
			f->sharpness = (int)s;
			continue;
		}

		if (!strcmp(key, "env")) {
			/* The first --env of a run REPLACES the set; the rest
			 * add to it. Otherwise an edit could only ever grow the
			 * list, and there would be no way to take one out. */
			if (!env_seen) {
				f->envc = 0;
				env_seen = true;
			}
			if (!*val)
				continue;
			if (!strchr(val, '=')) {
				fprintf(stderr, "syn-arcade: --env wants "
					"NAME=VALUE, got '%s'\n", val);
				return false;
			}
			if (f->envc >= FIT_ENV_MAX) {
				fputs("syn-arcade: too many --env settings\n",
				      stderr);
				return false;
			}
			snprintf(f->env[f->envc++], FIT_VAL_MAX, "%s", val);
			continue;
		}

		if (!strcmp(key, "name") || !strcmp(key, "exec") ||
		    !strcmp(key, "workdir") || !strcmp(key, "icon") ||
		    !strcmp(key, "categories") || !strcmp(key, "refresh") ||
		    !strcmp(key, "fullscreen") || !strcmp(key, "grab") ||
		    !strcmp(key, "overlay") || !strcmp(key, "gamemode") ||
		    !strcmp(key, "menu") || !strcmp(key, "desktop") ||
		    !strcmp(key, "force_window") || !strcmp(key, "force-window")) {
			fit_set_key(f, !strcmp(key, "force-window")
					? "force_window" : key, val);
			continue;
		}

		fprintf(stderr, "syn-arcade: unknown option '%s'\n", arg);
		return false;
	}
	return true;
}

/*
 * A last look before it is written.
 *
 * ⚠ THE LUTRIS WARNING IS NOT A STYLE NOTE. gamescope around Proton aborts
 * instantly — SIGABRT, status 134, no window, nothing in any log — and Lutris
 * launches through umu/Proton by default. The game "just does not start", which
 * sends people looking at the game rather than at the wrapper. Warned rather
 * than refused: a Lutris entry configured to use plain wine is fine, and this
 * cannot tell which one it is looking at.
 */
static void fit_warn(const fit_t *f)
{
	if (strstr(f->exec, "lutris") || strstr(f->exec, "umu-run") ||
	    strstr(f->exec, "proton"))
		fprintf(stderr,
			"syn-arcade: ⚠ this launches through Lutris/Proton, and "
			"gamescope around Proton\n"
			"  aborts instantly (SIGABRT, status 134) with no window "
			"and nothing in any log.\n"
			"  If it does not start, point the command at plain "
			"`wine` with the prefix set\n"
			"  through --env=WINEPREFIX=…\n");

	/* The wrapper-around-a-wrapper case that --from now takes apart. It is
	 * still reachable by hand, through --exec, and nesting two gamescopes
	 * costs a whole extra composite of every frame. */
	if (strstr(f->exec, "gamescope"))
		fprintf(stderr,
			"syn-arcade: ⚠ the command already runs gamescope, so "
			"this wraps one inside\n"
			"  another. Give --exec the game's own command — the "
			"part after `--`.\n");

	if (f->gw && f->sw && f->gw == f->sw && f->gh == f->sh)
		fprintf(stderr,
			"syn-arcade: ⚠ the game size and the screen size are "
			"the same, so there is\n"
			"  nothing to upscale. Set --game to the resolution the "
			"game actually renders at.\n");

	if (system("command -v gamescope >/dev/null 2>&1") != 0)
		fputs("syn-arcade: ⚠ gamescope is not installed — "
		      "`synpkg install gamescope`\n", stderr);
}

static const char *opt_str(int argc, char **argv, const char *name)
{
	size_t len = strlen(name);
	for (int i = 0; i < argc; i++)
		if (strncmp(argv[i], name, len) == 0 && argv[i][len] == '=')
			return argv[i] + len + 1;
	return NULL;
}

static bool opt_has(int argc, char **argv, const char *name)
{
	for (int i = 0; i < argc; i++)
		if (strcmp(argv[i], name) == 0)
			return true;
	return false;
}

/* One whitespace-delimited token. Returns where to carry on, or NULL at the
 * end of the line. Quoting is deliberately not handled: this reads the FLAG
 * half of a gamescope command, where every token is a number or a word, and the
 * quoted half — the game's own command — is taken verbatim from after `--`. */
static const char *tok_next(const char *p, char *out, size_t n)
{
	while (*p == ' ' || *p == '\t')
		p++;
	if (!*p)
		return NULL;

	size_t w = 0;
	while (*p && *p != ' ' && *p != '\t') {
		if (w + 1 < n)
			out[w++] = *p;
		p++;
	}
	out[w] = '\0';
	return p;
}

/*
 * ADOPT a gamescope line that is already there.
 *
 * ⚠ Without this, pointing `--from` at a shortcut somebody had already made by
 * hand produced a wrapper around a wrapper: `gamescope … -- env … gamescope …
 * -- wine game.exe`, two nested micro-compositors, which is a real thing that
 * happens because the entries most worth wrapping are the ones already carrying
 * a hand-written gamescope line. This machine has two of them.
 *
 * So a source command that is already gamescope is taken APART instead: the
 * sizes, the filter and the environment come out into the wrapper's own fields,
 * where they can be edited, and the game's command — everything after `--`,
 * verbatim, quotes and all — becomes the thing being wrapped. Which is exactly
 * what somebody means by "make me one of these I can change".
 *
 * Returns false for anything that is not a gamescope command, leaving `f`
 * untouched.
 */
static bool exec_adopt_gamescope(fit_t *f, const char *line)
{
	char tmp_env[FIT_ENV_MAX][FIT_VAL_MAX];
	int  tmp_envc = 0;
	char tok[FIT_VAL_MAX];

	const char *p = line;

	/* A leading `env NAME=VALUE …`, which is how a wine prefix is nearly
	 * always spelled in these lines. */
	const char *q = tok_next(p, tok, sizeof(tok));
	if (q && strcmp(tok, "env") == 0) {
		p = q;
		while ((q = tok_next(p, tok, sizeof(tok)))) {
			if (!strchr(tok, '=') || tok[0] == '-')
				break;
			if (tmp_envc < FIT_ENV_MAX)
				snprintf(tmp_env[tmp_envc++], FIT_VAL_MAX, "%s", tok);
			p = q;
		}
	}

	q = tok_next(p, tok, sizeof(tok));
	if (!q || strcmp(tok, "gamescope") != 0)
		return false;
	p = q;

	/* Everything the line does not say is OFF, including fullscreen: this is
	 * reading a command, not merging with a default. */
	fit_t g = *f;
	g.envc = 0;
	g.gw = g.gh = g.sw = g.sh = g.refresh = 0;
	g.filter[0] = g.scaler[0] = '\0';
	g.sharpness = -1;
	g.fullscreen = g.grab = g.force_win = g.overlay = false;
	for (int i = 0; i < tmp_envc; i++)
		snprintf(g.env[g.envc++], FIT_VAL_MAX, "%s", tmp_env[i]);

	bool saw_sep = false;
	while ((q = tok_next(p, tok, sizeof(tok)))) {
		p = q;

		if (strcmp(tok, "--") == 0) {
			saw_sep = true;
			break;
		}

		/* --flag=value as well as --flag value: both are accepted by
		 * getopt_long, so both turn up in files people have written. */
		char *eq = strchr(tok, '=');
		char inline_val[FIT_VAL_MAX] = "";
		if (eq) {
			snprintf(inline_val, sizeof(inline_val), "%s", eq + 1);
			*eq = '\0';
		}

		const char *val = NULL;
		char next[FIT_VAL_MAX];
		bool wants_val =
			!strcmp(tok, "-w") || !strcmp(tok, "--nested-width") ||
			!strcmp(tok, "-h") || !strcmp(tok, "--nested-height") ||
			!strcmp(tok, "-W") || !strcmp(tok, "--output-width") ||
			!strcmp(tok, "-H") || !strcmp(tok, "--output-height") ||
			!strcmp(tok, "-r") || !strcmp(tok, "--nested-refresh") ||
			!strcmp(tok, "-F") || !strcmp(tok, "--filter") ||
			!strcmp(tok, "-S") || !strcmp(tok, "--scaler") ||
			!strcmp(tok, "--sharpness") ||
			!strcmp(tok, "--fsr-sharpness");

		if (wants_val) {
			if (eq) {
				val = inline_val;
			} else {
				const char *n2 = tok_next(p, next, sizeof(next));
				if (!n2)
					break;
				p = n2;
				val = next;
			}
		}

		if (!strcmp(tok, "-w") || !strcmp(tok, "--nested-width"))
			g.gw = (int)strtol(val, NULL, 10);
		else if (!strcmp(tok, "-h") || !strcmp(tok, "--nested-height"))
			g.gh = (int)strtol(val, NULL, 10);
		else if (!strcmp(tok, "-W") || !strcmp(tok, "--output-width"))
			g.sw = (int)strtol(val, NULL, 10);
		else if (!strcmp(tok, "-H") || !strcmp(tok, "--output-height"))
			g.sh = (int)strtol(val, NULL, 10);
		else if (!strcmp(tok, "-r") || !strcmp(tok, "--nested-refresh"))
			g.refresh = (int)strtol(val, NULL, 10);
		else if (!strcmp(tok, "-F") || !strcmp(tok, "--filter")) {
			if (in_list(fit_filters, val))
				copy_short(g.filter, sizeof(g.filter), val);
		} else if (!strcmp(tok, "-S") || !strcmp(tok, "--scaler")) {
			if (in_list(fit_scalers, val))
				copy_short(g.scaler, sizeof(g.scaler), val);
		} else if (!strcmp(tok, "--sharpness") ||
			   !strcmp(tok, "--fsr-sharpness")) {
			long s = strtol(val, NULL, 10);
			if (s >= 0 && s <= 20)
				g.sharpness = (int)s;
		} else if (!strcmp(tok, "-f") || !strcmp(tok, "--fullscreen"))
			g.fullscreen = true;
		else if (!strcmp(tok, "-g") || !strcmp(tok, "--grab"))
			g.grab = true;
		else if (!strcmp(tok, "--force-windows-fullscreen"))
			g.force_win = true;
		else if (!strcmp(tok, "--mangoapp"))
			g.overlay = true;
		/* Anything else — a flag this does not model — is dropped, and
		 * the wrapper says so by simply not carrying it. Keeping it
		 * would mean a field the window cannot show and the config file
		 * cannot express. */
	}

	/* No `--` means no game on the end of it: not a line this can take
	 * apart, whatever else it is. */
	if (!saw_sep)
		return false;

	while (*p == ' ' || *p == '\t')
		p++;
	if (!*p)
		return false;

	snprintf(g.exec, sizeof(g.exec), "%s", p);
	if (g.gamemode || strncmp(g.exec, "gamemoderun ", 12) == 0) {
		g.gamemode = true;
		if (strncmp(g.exec, "gamemoderun ", 12) == 0)
			memmove(g.exec, g.exec + 12, strlen(g.exec + 12) + 1);
	}

	*f = g;
	return true;
}

/* Does this name already say what the wrapper does? "Gangsters (Fullscreen)"
 * with another "(Fullscreen)" stapled on is what the naive version produced. */
static bool name_says_fullscreen(const char *name)
{
	return strcasestr(name, "fullscreen") || strcasestr(name, "widescreen") ||
	       strcasestr(name, "gamescope");
}

/*
 * Take the name, command, working directory and icon off an application that is
 * already installed.
 *
 * This is the difference between a tool somebody uses and one they read about:
 * the hard part of wrapping The Sims is not the gamescope flags, it is finding
 * out that its entry runs `wine 'C:\…\Sims.exe'` from a directory six levels
 * into a prefix. That is written down in the .desktop file, so ask it.
 */
static bool fit_from_desktop(fit_t *f, const char *path)
{
	app_t a;
	if (!desktop_read(path, &a)) {
		fprintf(stderr, "syn-arcade: %s is not an application entry "
			"this can read\n", path);
		return false;
	}
	exec_strip_codes(a.exec);

	if (name_says_fullscreen(a.name)) {
		snprintf(f->name, sizeof(f->name), "%s", a.name);
	} else {
		/* Truncated into a shorter buffer first: the suffix has to
		 * survive, and a name long enough to push it off the end would
		 * leave an entry indistinguishable from the one it wraps. */
		char base[FIT_VAL_MAX - 16];
		size_t bl = strlen(a.name);
		if (bl >= sizeof(base))
			bl = sizeof(base) - 1;
		memcpy(base, a.name, bl);
		base[bl] = '\0';
		snprintf(f->name, sizeof(f->name), "%s (Fullscreen)", base);
	}

	snprintf(f->workdir, sizeof(f->workdir), "%s", a.workdir);
	snprintf(f->icon, sizeof(f->icon), "%s", a.icon);
	if (a.categories[0])
		snprintf(f->categories, sizeof(f->categories), "%s", a.categories);

	/* Already a gamescope line? Take it apart rather than wrapping it. */
	if (!exec_adopt_gamescope(f, a.exec))
		snprintf(f->exec, sizeof(f->exec), "%s", a.exec);
	return true;
}

static int fit_new(int argc, char **argv)
{
	fit_t f;
	fit_defaults(&f);

	const char *from = opt_str(argc, argv, "--from");
	if (from && !fit_from_desktop(&f, from))
		return EX_FAIL;

	/* The screen is a fact about the monitor, not a preference, so it is
	 * filled in before the flags are read and overridden by --screen. */
	if (!f.sw)
		primary_size(&f.sw, &f.sh);

	if (!fit_flags(&f, argc, argv))
		return EX_USAGE;

	if (!f.exec[0]) {
		fputs("syn-arcade: a wrapper needs something to run — "
		      "--exec=\"wine Game.exe\" or --from=<a .desktop file>\n",
		      stderr);
		return EX_USAGE;
	}
	if (!f.name[0])
		snprintf(f.name, sizeof(f.name), "%s", f.exec);

	const char *want = opt_str(argc, argv, "--id");
	char base[FIT_ID_MAX];
	slugify(want && *want ? want : f.name, base, sizeof(base));
	unique_id(base, f.id, sizeof(f.id));

	int rc = fit_store(&f);
	if (rc != EX_OK)
		return rc;

	rc = fit_apply(&f, true);
	fit_warn(&f);

	/* The id first and on its own line: it is what every other verb takes,
	 * and a caller that wants it should not have to parse a sentence. */
	char *cmd = fit_command(&f);
	printf("%s\n", f.id);
	printf("%s — %s%s\n", f.name,
	       f.menu ? "in the Games menu" : "not in the menu",
	       f.desktop ? ", and on the desktop" : "");
	printf("%s\n", cmd);
	free(cmd);
	return rc;
}

static int fit_edit(const char *id, int argc, char **argv)
{
	fit_t f;
	if (!fit_load(id, &f)) {
		fprintf(stderr, "syn-arcade: no wrapper called '%s'\n", id);
		return EX_FAIL;
	}
	if (!fit_flags(&f, argc, argv))
		return EX_USAGE;
	if (!f.exec[0]) {
		fputs("syn-arcade: a wrapper needs something to run\n", stderr);
		return EX_USAGE;
	}

	int rc = fit_store(&f);
	if (rc != EX_OK)
		return rc;
	rc = fit_apply(&f, true);
	fit_warn(&f);

	char *cmd = fit_command(&f);
	printf("%s\n", cmd);
	free(cmd);
	return rc;
}

static int fit_remove(const char *id)
{
	fit_t f;
	if (!fit_load(id, &f)) {
		fprintf(stderr, "syn-arcade: no wrapper called '%s'\n", id);
		return EX_FAIL;
	}

	char conf[FIT_PATH_MAX], menu[FIT_PATH_MAX], icon[FIT_PATH_MAX];
	fit_conf_path(conf, sizeof(conf), id);
	fit_menu_path(menu, sizeof(menu), id);
	fit_icon_path(icon, sizeof(icon), id);

	bool had_icon = file_exists(icon);
	unlink(menu);
	unlink(icon);
	unlink(conf);
	if (had_icon)
		deskicons_refresh();

	printf("removed %s (%s)\n", id, f.name);
	return EX_OK;
}

/*
 * Run one.
 *
 * ⚠ Through /bin/sh, and that is what makes the assembled line the SAME thing
 * whether it is run from here, printed by `fit command` or pasted into a
 * terminal. A launcher that built an argv instead would be a second
 * interpretation of the config — and the one the user cannot see.
 *
 * --detach is for the graphical window: without it the game would be a child of
 * quickshell holding quickshell's pipes, and closing the window would take the
 * game with it. See spawn_detached().
 */
static int fit_run(const char *id, bool detach)
{
	fit_t f;
	if (!fit_load(id, &f)) {
		fprintf(stderr, "syn-arcade: no wrapper called '%s'\n", id);
		return EX_FAIL;
	}

	if (system("command -v gamescope >/dev/null 2>&1") != 0) {
		fputs("syn-arcade: gamescope is not installed — "
		      "`synpkg install gamescope`\n", stderr);
		return EX_FAIL;
	}

	char *cmd = fit_command(&f);

	if (detach) {
		char *argv[] = { (char *)"/bin/sh", (char *)"-c", cmd, NULL };
		int rc = spawn_detached(argv);
		free(cmd);
		return rc;
	}

	execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
	fprintf(stderr, "syn-arcade: cannot run /bin/sh: %s\n", strerror(errno));
	free(cmd);
	return EX_FAIL;
}

/* The choice lists the window draws its chips from, so the two cannot disagree
 * about what gamescope accepts. */
static int fit_choices(const char *key)
{
	const char *const *list = NULL;
	if (!strcmp(key, "filter"))	list = fit_filters;
	else if (!strcmp(key, "scaler"))list = fit_scalers;
	else {
		fprintf(stderr, "syn-arcade: no choices for '%s'\n", key);
		return EX_USAGE;
	}

	rec_row(2, "id", "label");
	for (int i = 0; list[i]; i++)
		rec_row(2, list[i], list[i]);
	return EX_OK;
}

/* ── dispatch ────────────────────────────────────────────────────────────── */

int cmd_fit(int argc, char **argv)
{
	if (argc < 1)
		return fit_list(false);

	const char *sub = argv[0];
	int rest_c = argc - 1;
	char **rest = argv + 1;

	if (!strcmp(sub, "--rec"))	return fit_list(true);
	if (!strcmp(sub, "list"))
		return fit_list(opt_has(rest_c, rest, "--rec"));

	if (!strcmp(sub, "screens"))
		return fit_screens(opt_has(rest_c, rest, "--rec"));

	if (!strcmp(sub, "apps"))
		return fit_apps(opt_has(rest_c, rest, "--rec"),
				opt_has(rest_c, rest, "--games"));

	if (!strcmp(sub, "new"))	return fit_new(rest_c, rest);

	/*
	 * What a wrapper made from this application would be, WITHOUT making
	 * one.
	 *
	 * The window's "new from an installed game" path runs this and fills its
	 * form from the answer, so that the person sees the command, the sizes
	 * and the folder BEFORE pressing Create — and so that the reading of a
	 * .desktop file, and the unpicking of a gamescope line that is already
	 * in it, happens in exactly one place. A window that parsed the entry
	 * itself would be a second implementation of both, and the way that
	 * fails is a form pre-filled with something the binary would never have
	 * produced.
	 */
	if (!strcmp(sub, "inspect")) {
		if (rest_c < 1) {
			fputs("syn-arcade: fit inspect needs a .desktop file\n",
			      stderr);
			return EX_USAGE;
		}
		fit_t f;
		fit_defaults(&f);
		if (!fit_from_desktop(&f, rest[0]))
			return EX_FAIL;
		if (!f.sw)
			primary_size(&f.sw, &f.sh);
		return fit_print(&f, opt_has(rest_c - 1, rest + 1, "--rec"));
	}

	if (!strcmp(sub, "choices")) {
		if (rest_c < 1) return EX_USAGE;
		return fit_choices(rest[0]);
	}

	/* Everything below names a wrapper. */
	if (rest_c < 1) {
		fprintf(stderr, "syn-arcade: fit %s needs the name of a "
			"wrapper (`syn-arcade fit` lists them)\n", sub);
		return EX_USAGE;
	}

	/*
	 * ⚠ The id is a PATH COMPONENT — of the config file, of the menu entry
	 * and of the desktop icon — and `fit remove` UNLINKS all three. Checked
	 * here rather than trusted because slugify() only guarantees the shape
	 * of ids this writes, and nothing stops one being typed.
	 */
	const char *id = rest[0];
	for (const char *p = id; *p; p++) {
		if (islower((unsigned char)*p) || isdigit((unsigned char)*p) ||
		    *p == '-')
			continue;
		fprintf(stderr, "syn-arcade: '%s' is not a wrapper name — they "
			"are lower case letters, digits and dashes "
			"(`syn-arcade fit` lists them)\n", id);
		return EX_USAGE;
	}
	if (!*id) {
		fputs("syn-arcade: fit needs the name of a wrapper\n", stderr);
		return EX_USAGE;
	}
	int flag_c = rest_c - 1;
	char **flags = rest + 1;

	if (!strcmp(sub, "show"))
		return fit_show(id, opt_has(flag_c, flags, "--rec"));
	if (!strcmp(sub, "edit"))	return fit_edit(id, flag_c, flags);
	if (!strcmp(sub, "remove") || !strcmp(sub, "delete"))
		return fit_remove(id);
	if (!strcmp(sub, "run"))
		return fit_run(id, opt_has(flag_c, flags, "--detach"));

	if (!strcmp(sub, "command")) {
		fit_t f;
		if (!fit_load(id, &f)) {
			fprintf(stderr, "syn-arcade: no wrapper called '%s'\n", id);
			return EX_FAIL;
		}
		char *cmd = fit_command(&f);
		printf("%s\n", cmd);
		free(cmd);
		return EX_OK;
	}

	if (!strcmp(sub, "apply")) {
		fit_t f;
		if (!fit_load(id, &f)) {
			fprintf(stderr, "syn-arcade: no wrapper called '%s'\n", id);
			return EX_FAIL;
		}
		return fit_apply(&f, false);
	}

	fprintf(stderr, "syn-arcade: unknown fit command '%s'\n", sub);
	return EX_USAGE;
}
