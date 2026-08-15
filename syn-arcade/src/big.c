/*
 * big.c — big screen mode: the ten-foot face of this desktop.
 *
 * What this is FOR: a television is not a monitor with a bigger number. It is
 * four metres away, driven with a controller by somebody who is not sitting at
 * a desk, and every desktop convention — a 12px menu, a pointer, a window that
 * needs dragging — fails at that distance. Big screen mode is the couch face:
 * one full-screen surface, tiles the size of a hand, and a whole session
 * drivable from a gamepad with the keyboard left on the table.
 *
 * ── Why the pieces are where they are ───────────────────────────────────────
 *
 * The shell itself is quickshell rendering data/syn-arcade-big.qml, the same
 * arrangement as `syn-arcade gui`. This file is what it asks: what games are
 * installed, what else can be launched, and what to run when a tile is picked.
 * That split is not decoration — it is what makes the library scanner testable
 * against a fixture Steam tree, and it means `syn-arcade big games` answers the
 * same question over SSH that the tiles answer on the television.
 *
 * ── The Steam library is THREE questions, not one ───────────────────────────
 *
 * Nothing on the system will tell you what somebody owns and has installed, so
 * this reconstructs it the way Steam itself stores it:
 *
 *   1. WHERE the libraries are — steamapps/libraryfolders.vdf, which lists
 *      every drive Steam has been pointed at. A machine with games on a second
 *      SSD keeps NOTHING in the home directory for them; a scanner that reads
 *      only ~/.local/share/Steam finds the runtimes and none of the games. On
 *      this developer's own machine that is 16 entries in the home library and
 *      44 on the fast disk, so the wrong answer looks plausible and is missing
 *      three quarters of everything.
 *   2. WHAT is in each — steamapps/appmanifest_<appid>.acf, one per installed
 *      app, holding the name, the size and when it was last played.
 *   3. WHAT IT LOOKS LIKE — appcache/librarycache, which is a download cache,
 *      not an API: art may be absent for a game installed five minutes ago and
 *      never seen in the library view. Every art field here is therefore
 *      OPTIONAL and the shell draws a lettered tile when one is missing. It has
 *      had THREE on-disk layouts — flat <appid>_library_600x900.jpg, then a
 *      per-appid directory, then per-appid/content-hash subdirectories with
 *      different basenames again — and all three are still on this machine at
 *      once, because Steam does not rewrite what it has already cached. All
 *      three are checked; see art_find().
 *
 * ⚠ A "Steam library" is mostly NOT games. Proton builds, the Steam Linux
 * Runtimes and Steamworks Common Redistributables are ordinary installed apps
 * with ordinary manifests, and on a normal machine they outnumber the real
 * games in the home library. Listing them is not a cosmetic wart: the first
 * screen of a ten-foot launcher is the whole interface, and filling it with
 * four Proton builds is the difference between a games machine and a file
 * browser. They are dropped by name, and `--all` puts them back.
 *
 * ── Why not synui-game-run for Steam ────────────────────────────────────────
 *
 * The house launcher wraps a command in gamemoderun and mangohud. That is
 * right for a game binary and wrong for the Steam CLIENT, which is a bootstrap
 * that re-execs itself into a container runtime — wrapping it puts the overlay
 * around the store UI and leaves the actual game to Steam's own launch
 * options, which is where a person expects to control it. So `big steam` runs
 * steam, and per-game wrapping stays where Steam already keeps it.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "arcade.h"
#include "config.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * How long a path this file is willing to store as a STARTING POINT — a Steam
 * root or a library directory. Composed paths built from one get a buffer this
 * size plus room for the longest thing appended to it, which is what stops gcc
 * warning that a snprintf could truncate. 4096 everywhere looks safer and is
 * the opposite: two 4096-byte buffers concatenated cannot fit in a third, so
 * every join becomes a possible silent truncation.
 */
#define SYN_PATH 1024

/* ── where things are ────────────────────────────────────────────────────── */

/*
 * The Steam installation root.
 *
 * SYN_ARCADE_STEAM overrides it, which is what the suite drives: a library
 * scanner can only be tested against a described machine, and no build host has
 * a Steam install.
 *
 * The candidates are walked in the order Steam itself has used them. ~/.steam/
 * steam is a SYMLINK to the first on a normal install, so finding either is
 * finding the same tree — but on a machine that has moved between packaging
 * (native, flatpak) only one of them exists.
 */
static bool steam_root(char *buf, size_t n)
{
	const char *env = getenv("SYN_ARCADE_STEAM");
	if (env && *env)
		return snprintf(buf, n, "%s", env) < (int)n;

	static const char *const rel[] = {
		".local/share/Steam",
		".steam/steam",
		".steam/root",
		".var/app/com.valvesoftware.Steam/data/Steam",
		NULL
	};

	for (int i = 0; rel[i]; i++) {
		char p[SYN_PATH];
		if (!home_path(p, sizeof(p), rel[i]))
			continue;
		struct stat st;
		if (stat(p, &st) == 0 && S_ISDIR(st.st_mode))
			return snprintf(buf, n, "%s", p) < (int)n;
	}
	return false;
}

/*
 * The lock file, which is also the pid file.
 *
 * $XDG_RUNTIME_DIR is a tmpfs that logind destroys at logout, so a stale entry
 * cannot outlive the session that made it. The /tmp fallback exists because
 * this binary must work from a plain SSH login where that variable is unset,
 * and it is per-uid so two people on one machine do not share a lock.
 */
static bool big_lock_path(char *buf, size_t n)
{
	const char *run = getenv("XDG_RUNTIME_DIR");
	if (run && *run)
		return snprintf(buf, n, "%s/syn-arcade-big.pid", run) < (int)n;
	return snprintf(buf, n, "/tmp/syn-arcade-big-%u.pid",
			(unsigned)getuid()) < (int)n;
}

/*
 * Is the shell running, and as what pid?
 *
 * The LOCK is the truth, not the number in the file. `big start` takes an
 * exclusive flock and passes the open descriptor through exec into quickshell,
 * so the lock is held for exactly as long as that process lives and is released
 * by the kernel however it dies — no cleanup path to forget, no stale pid after
 * a SIGKILL or a crash. If we can take the lock ourselves, nothing is running,
 * whatever the file says.
 */
static bool big_running(pid_t *out)
{
	char path[4096];
	if (!big_lock_path(path, sizeof(path)))
		return false;

	int fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return false;

	bool running = false;
	if (flock(fd, LOCK_EX | LOCK_NB) != 0 && errno == EWOULDBLOCK) {
		running = true;
		if (out) {
			char buf[32] = "";
			ssize_t got = pread(fd, buf, sizeof(buf) - 1, 0);
			if (got > 0) {
				buf[got] = '\0';
				*out = (pid_t)strtol(buf, NULL, 10);
			} else {
				*out = 0;
			}
		}
	} else {
		/* We took it — release it immediately. Holding it here would
		 * make every `big status` look like a running shell to the next
		 * caller for as long as this process lived. */
		flock(fd, LOCK_UN);
	}
	close(fd);
	return running;
}

/* ── the smallest VDF reader that answers these two questions ────────────── */

/*
 * Valve's key-value format is a nested, brace-delimited tree, and a full parser
 * for it would be several hundred lines. Everything needed here — a library
 * path, an app's name, when it was last played — is a `"key" "value"` pair on
 * its own line, and both files are machine-written with one pair per line.
 *
 * So this scans lines for two quoted tokens and asks nothing about structure.
 * The one thing it MUST get right is escaping: Valve escapes \\ and \" inside a
 * value, and a Windows-style library path is full of the first. Reading those
 * literally turns C:\\Games into C:\\Games, which then matches no directory.
 *
 * Returns false when the line holds no pair (a brace, a comment, blank).
 */
static bool vdf_pair(const char *line, char *key, size_t kn, char *val, size_t vn)
{
	const char *p = line;
	char *dst[2];
	size_t cap[2];
	dst[0] = key; cap[0] = kn;
	dst[1] = val; cap[1] = vn;

	for (int i = 0; i < 2; i++) {
		while (*p && *p != '"') {
			if (*p == '\n') return false;
			p++;
		}
		if (*p != '"') return false;
		p++;

		size_t w = 0;
		while (*p && *p != '"') {
			char c = *p;
			if (c == '\\' && p[1]) {	/* \\ and \" only */
				p++;
				c = *p;
			}
			if (w + 1 < cap[i])
				dst[i][w++] = c;
			p++;
		}
		if (*p != '"') return false;
		p++;
		dst[i][w] = '\0';
	}
	return true;
}

/* Walk `text` line by line, handing each to vdf_pair. Returns the value of the
 * first pair whose key matches (case-insensitively — .acf files spell keys
 * "LastPlayed" and libraryfolders.vdf spells them "path"). */
static bool vdf_lookup(const char *text, const char *want, char *val, size_t vn)
{
	const char *p = text;
	while (p && *p) {
		char k[128], v[4096];
		if (vdf_pair(p, k, sizeof(k), v, sizeof(v)) &&
		    strcasecmp(k, want) == 0) {
			snprintf(val, vn, "%s", v);
			return true;
		}
		p = strchr(p, '\n');
		if (p) p++;
	}
	return false;
}

/* ── the libraries ───────────────────────────────────────────────────────── */

#define LIB_MAX 32

typedef struct {
	char path[LIB_MAX][SYN_PATH];
	int  count;
} libs_t;

static void libs_add(libs_t *l, const char *path)
{
	if (l->count >= LIB_MAX || !path || !*path)
		return;
	for (int i = 0; i < l->count; i++)		/* the root is usually
							 * also listed in the
							 * vdf; do not scan it
							 * twice */
		if (strcmp(l->path[i], path) == 0)
			return;

	char apps[SYN_PATH + 32];
	if (snprintf(apps, sizeof(apps), "%s/steamapps", path) >= (int)sizeof(apps))
		return;
	struct stat st;
	if (stat(apps, &st) != 0 || !S_ISDIR(st.st_mode))
		return;			/* a library on an unplugged drive */

	snprintf(l->path[l->count++], sizeof(l->path[0]), "%s", path);
}

static void libs_find(const char *root, libs_t *l)
{
	l->count = 0;
	libs_add(l, root);

	char vdf[4096];
	snprintf(vdf, sizeof(vdf), "%s/steamapps/libraryfolders.vdf", root);
	char *text = read_file(vdf);
	if (!text)
		return;

	/* Every "path" in the file is a library. The keys around it — label,
	 * contentid, totalsize, and the whole "apps" block — are not needed:
	 * the manifests in the directory are the authority on what is actually
	 * installed there, and the vdf's app list goes stale. */
	const char *p = text;
	while (p && *p) {
		char k[128], v[4096];
		if (vdf_pair(p, k, sizeof(k), v, sizeof(v)) &&
		    strcasecmp(k, "path") == 0)
			libs_add(l, v);
		p = strchr(p, '\n');
		if (p) p++;
	}
	free(text);
}

/* ── the games ───────────────────────────────────────────────────────────── */

typedef struct {
	char      appid[24];
	char      name[256];
	char      art[4096];		/* portrait cover, "" if none cached */
	char      hero[4096];		/* wide background, "" if none */
	char      logo[4096];		/* transparent title logo, "" if none */
	long long last_played;
	long long size;
	char      library[SYN_PATH];
} game_t;

/*
 * The things a Steam library holds that nobody wants on the first screen of a
 * television.
 *
 * Matched as a PREFIX of the name, because these are families rather than
 * single entries — "Proton 9.0", "Proton Experimental" and "Proton Hotfix" are
 * three manifests, and there will be a fourth next month. Matching by appid
 * would need this list updated every time Valve ships a runtime; matching the
 * name needs it only when they rename the family, which has happened once in a
 * decade.
 */
static bool is_tool(const char *name)
{
	static const char *const pre[] = {
		"Proton",
		"Steam Linux Runtime",
		"Steamworks Common Redistributables",
		"Steam Controller Configs",
		"SteamVR",
		"Windows Media Player Shim",
		NULL
	};
	for (int i = 0; pre[i]; i++)
		if (strncasecmp(name, pre[i], strlen(pre[i])) == 0)
			return true;
	return false;
}

static bool try_art(char *out, size_t n, const char *fmt, ...)
{
	char path[4096];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(path, sizeof(path), fmt, ap);
	va_end(ap);

	if (!file_exists(path))
		return false;
	snprintf(out, n, "%s", path);
	return true;
}

/* Fill `dst` from the first candidate that exists. Written as a macro so the
 * candidates read as an ordered list of one-liners: the ORDER is the whole
 * content of art_find below, and a chain of ifs or of ||s buries it. */
#define ART(dst, ...) \
	do { if (!(dst)[0]) try_art((dst), sizeof(dst), __VA_ARGS__); } while (0)

/*
 * Find the three pictures Steam may have cached for an app.
 *
 * ⚠ The USER's own art is checked first, and that ordering is the whole point
 * of looking in two places. Anybody who has replaced a game's cover — with
 * SteamGridDB, or by dropping a file in — did so because they wanted to see
 * that one, and a launcher that shows the publisher's version instead has
 * quietly overridden a decision somebody made on purpose. Those live under
 * userdata/<steamid>/config/grid, one directory per account that has logged in
 * on this machine, named <appid>p.png for the portrait and <appid>_hero.png for
 * the background.
 *
 * Then the download cache, which has had two layouts: a per-appid directory
 * (current) and flat <appid>_library_600x900.jpg files (older Steam). Both are
 * checked because an install that predates the change keeps the old files and
 * never re-downloads them.
 */
static void art_find(const char *root, const char *appid, game_t *g)
{
	g->art[0] = g->hero[0] = g->logo[0] = '\0';

	/* ── the user's own overrides ── */
	char grids[SYN_PATH + 32];
	snprintf(grids, sizeof(grids), "%s/userdata", root);
	DIR *d = opendir(grids);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.')
				continue;
			char base[SYN_PATH + 320];
			snprintf(base, sizeof(base), "%s/%s/config/grid",
				 grids, e->d_name);

			ART(g->art,  "%s/%sp.png",      base, appid);
			ART(g->art,  "%s/%sp.jpg",      base, appid);
			ART(g->hero, "%s/%s_hero.png",  base, appid);
			ART(g->hero, "%s/%s_hero.jpg",  base, appid);
			ART(g->logo, "%s/%s_logo.png",  base, appid);
		}
		closedir(d);
	}

	/* ── Steam's download cache, current layout then legacy ── */
	char cache[SYN_PATH + 32];
	snprintf(cache, sizeof(cache), "%s/appcache/librarycache", root);

	ART(g->art, "%s/%s/library_600x900.jpg",    cache, appid);
	ART(g->art, "%s/%s/library_600x900_2x.jpg", cache, appid);
	ART(g->art, "%s/%s_library_600x900.jpg",    cache, appid);
	/* header.jpg is landscape where everything above it is portrait. It is
	 * last on purpose and the shell letterboxes it: a wrong-shaped picture
	 * still says which game this is, which is more than an empty tile
	 * does. */
	ART(g->art, "%s/%s/header.jpg",             cache, appid);
	ART(g->art, "%s/%s_header.jpg",             cache, appid);

	/*
	 * ⚠ The BLURRED hero is preferred, and that is a rendering decision
	 * made here because Steam already did the work. The shell puts this
	 * behind the tiles, where a sharp photograph competes with every label
	 * on top of it. Blurring it in the shell would cost a full-screen
	 * offscreen texture and a gaussian pass on a machine that may be
	 * driving a 4K television; Valve ships the same picture pre-blurred, so
	 * the good-looking option is also the free one.
	 */
	ART(g->hero, "%s/%s/library_hero_blur.jpg", cache, appid);
	ART(g->hero, "%s/%s/library_hero.jpg",      cache, appid);
	ART(g->hero, "%s/%s_library_hero.jpg",      cache, appid);

	ART(g->logo, "%s/%s/logo.png", cache, appid);
	ART(g->logo, "%s/%s_logo.png", cache, appid);

	/*
	 * ── The THIRD layout, and it is the current one ──
	 *
	 * Recent Steam clients store each picture in a CONTENT-HASH directory
	 * under the appid, with a different basename again:
	 *
	 *   librarycache/1091500/6399de67…/library_capsule.jpg   the portrait
	 *   librarycache/1091500/812a216d…/library_header.jpg    the landscape
	 *   librarycache/1091500/cf8cec80…/library_hero.jpg
	 *   librarycache/1091500/37680a27…/logo.png
	 *
	 * Nothing names those directories but the file inside them, so they have
	 * to be walked. Found by looking: on this machine the checks above
	 * covered 48 of 53 games and the five that came back blank were the five
	 * most recently updated — which is exactly the shape of a layout change
	 * being rolled out, and exactly the shape that a spot check of "a game"
	 * would have declared working.
	 */
	if (g->art[0] && g->hero[0] && g->logo[0])
		return;

	char appdir[SYN_PATH + 64];
	snprintf(appdir, sizeof(appdir), "%s/%s", cache, appid);

	DIR *ad = opendir(appdir);
	if (!ad)
		return;

	struct dirent *e;
	while ((e = readdir(ad))) {
		if (e->d_name[0] == '.')
			continue;

		char sub[SYN_PATH + 384];
		snprintf(sub, sizeof(sub), "%s/%s", appdir, e->d_name);

		struct stat st;
		if (stat(sub, &st) != 0 || !S_ISDIR(st.st_mode))
			continue;

		ART(g->art,  "%s/library_capsule.jpg",   sub);
		ART(g->art,  "%s/library_600x900.jpg",   sub);
		ART(g->hero, "%s/library_hero_blur.jpg", sub);
		ART(g->hero, "%s/library_hero.jpg",      sub);
		ART(g->logo, "%s/logo.png",              sub);
	}
	closedir(ad);

	/* The landscape header is a SECOND pass, after every directory has been
	 * given the chance to offer a portrait. Taking it inside the loop above
	 * would let a header found in the first directory beat a capsule sitting
	 * in the third, and readdir order is not something to bet a layout on. */
	if (!g->art[0]) {
		ad = opendir(appdir);
		if (!ad)
			return;
		while ((e = readdir(ad))) {
			if (e->d_name[0] == '.')
				continue;
			char sub[SYN_PATH + 384];
			snprintf(sub, sizeof(sub), "%s/%s", appdir, e->d_name);
			ART(g->art, "%s/library_header.jpg", sub);
			ART(g->art, "%s/header.jpg", sub);
		}
		closedir(ad);
	}
}

/*
 * Sort: most recently played first, then alphabetically.
 *
 * Not alphabetically alone, which is what a file manager would do. The tile
 * somebody wants is nearly always the one they played yesterday, and on a
 * gamepad every position down the list is a physical press — an A-to-Z ordering
 * makes "carry on with the game I am playing" cost eleven of them.
 *
 * Games never launched share last_played 0 and fall back to the name, so the
 * tail of the list is stable and browsable rather than in manifest order.
 */
static int game_cmp(const void *a, const void *b)
{
	const game_t *x = a, *y = b;
	if (x->last_played != y->last_played)
		return x->last_played > y->last_played ? -1 : 1;
	return strcasecmp(x->name, y->name);
}

/* Every installed app across every library. Caller frees. */
static game_t *games_scan(const char *root, bool all, int *count)
{
	*count = 0;

	libs_t libs;
	libs_find(root, &libs);

	int cap = 64, n = 0;
	game_t *out = xmalloc((size_t)cap * sizeof(*out));

	for (int i = 0; i < libs.count; i++) {
		char apps[SYN_PATH + 32];
		snprintf(apps, sizeof(apps), "%s/steamapps", libs.path[i]);

		DIR *d = opendir(apps);
		if (!d)
			continue;

		struct dirent *e;
		while ((e = readdir(d))) {
			if (strncmp(e->d_name, "appmanifest_", 12) != 0)
				continue;

			char mf[SYN_PATH + 320];
			snprintf(mf, sizeof(mf), "%s/%s", apps, e->d_name);
			char *text = read_file(mf);
			if (!text)
				continue;

			game_t g;
			memset(&g, 0, sizeof(g));

			char v[4096];
			if (!vdf_lookup(text, "appid", g.appid, sizeof(g.appid)) ||
			    !vdf_lookup(text, "name", g.name, sizeof(g.name))) {
				free(text);
				continue;
			}

			/*
			 * ⚠ StateFlags bit 2 (value 4) is "fully installed".
			 * Without this check a game being DOWNLOADED — or one
			 * whose files were deleted while the manifest stayed —
			 * appears as a tile that launches into a Steam download
			 * dialog. The manifest existing is not the same as the
			 * game being there.
			 */
			if (vdf_lookup(text, "StateFlags", v, sizeof(v))) {
				long flags = strtol(v, NULL, 10);
				if (!(flags & 4)) {
					free(text);
					continue;
				}
			}

			if (!all && is_tool(g.name)) {
				free(text);
				continue;
			}

			if (vdf_lookup(text, "LastPlayed", v, sizeof(v)))
				g.last_played = strtoll(v, NULL, 10);
			if (vdf_lookup(text, "SizeOnDisk", v, sizeof(v)))
				g.size = strtoll(v, NULL, 10);
			snprintf(g.library, sizeof(g.library), "%s", libs.path[i]);

			art_find(root, g.appid, &g);
			free(text);

			if (n == cap) {
				cap *= 2;
				out = xrealloc(out, (size_t)cap * sizeof(*out));
			}
			out[n++] = g;
		}
		closedir(d);
	}

	qsort(out, (size_t)n, sizeof(*out), game_cmp);
	*count = n;
	return out;
}

static void human_size(char *buf, size_t n, long long bytes)
{
	if (bytes <= 0) { snprintf(buf, n, "-"); return; }
	const char *u[] = { "B", "KB", "MB", "GB", "TB" };
	double v = (double)bytes;
	int i = 0;
	while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
	snprintf(buf, n, "%.*f %s", v < 10.0 && i > 0 ? 1 : 0, v, u[i]);
}

static int big_games(bool rec, bool all)
{
	char root[SYN_PATH];
	if (!steam_root(root, sizeof(root))) {
		if (rec) {
			rec_row(8, "appid", "name", "art", "hero", "logo",
				"lastplayed", "size", "library");
			return EX_EMPTY;
		}
		fputs("syn-arcade: no Steam installation found\n", stderr);
		return EX_EMPTY;
	}

	int n = 0;
	game_t *g = games_scan(root, all, &n);

	if (rec) {
		rec_row(8, "appid", "name", "art", "hero", "logo",
			"lastplayed", "size", "library");
		for (int i = 0; i < n; i++) {
			char lp[32], sz[32];
			snprintf(lp, sizeof(lp), "%lld", g[i].last_played);
			snprintf(sz, sizeof(sz), "%lld", g[i].size);
			rec_row(8, g[i].appid, g[i].name, g[i].art, g[i].hero,
				g[i].logo, lp, sz, g[i].library);
		}
	} else if (n == 0) {
		printf("no games installed under %s\n", root);
	} else {
		for (int i = 0; i < n; i++) {
			char sz[32];
			human_size(sz, sizeof(sz), g[i].size);
			printf("%-10s %-48.48s %8s%s\n", g[i].appid, g[i].name,
			       sz, g[i].art[0] ? "" : "   (no art)");
		}
		printf("\n%d game%s in %s\n", n, n == 1 ? "" : "s", root);
	}

	free(g);
	return n ? EX_OK : EX_EMPTY;
}

/* ── launching, and the pipes that must not come with it ─────────────────── */

/* Closing big screen mode is what the "Desktop" tile does, so the tile runner
 * below needs it before it is defined. */
static int big_stop(void);

static bool have(const char *prog)
{
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", prog);
	return system(cmd) == 0;
}

/*
 * Start a command and let go of it completely.
 *
 * ⚠ THE POINT OF THIS FUNCTION IS THE THREE DESCRIPTORS, not the fork. A
 * launcher that only forks and setsid()s still hands the child ITS OWN stdin,
 * stdout and stderr — and when the launcher is started by quickshell, those are
 * pipes quickshell closes the instant the direct child exits, which for a
 * launcher is immediately. The game then writes its first line of startup
 * logging into a pipe with no reader, takes SIGPIPE, and dies before it maps a
 * window. Every visible sign says it worked: right arguments, exit 0, nothing
 * on any log. And it works perfectly when run from a terminal, because there
 * stderr is a tty nobody closes.
 *
 * That exact bug shipped in synfiles' launcher and took a day to find. The rule
 * it produced is the one implemented here: A LAUNCHER HANDS ITS CHILD NOTHING
 * THE CALLER OWNS.
 *
 * Which is also why this is done in C rather than left to the QML: the shell
 * would be spawning through the very pipes at issue, and the fix has to live
 * where the child is created.
 */
static int spawn_detached(char *const argv[])
{
	/*
	 * With stdio on /dev/null a failed exec — a missing binary, a typo in a
	 * PATH — is completely silent. This dup exists ONLY on that path: it is
	 * CLOEXEC, so a child that actually starts never inherits it, which is
	 * the whole rule above still being obeyed.
	 */
	int errfd = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 10);

	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "syn-arcade: fork: %s\n", strerror(errno));
		if (errfd >= 0) close(errfd);
		return EX_FAIL;
	}

	if (pid == 0) {
		/* A new session, so the game does not die with the shell that
		 * started it and is not in its terminal's foreground group. */
		setsid();

		int null = open("/dev/null", O_RDWR);
		if (null >= 0) {
			dup2(null, STDIN_FILENO);
			dup2(null, STDOUT_FILENO);
			dup2(null, STDERR_FILENO);
			if (null > STDERR_FILENO)
				close(null);
		}

		execvp(argv[0], argv);

		if (errfd >= 0) {
			char msg[512];
			int len = snprintf(msg, sizeof(msg),
					   "syn-arcade: cannot run %s: %s\n",
					   argv[0], strerror(errno));
			ssize_t w = write(errfd, msg, (size_t)len);
			(void)w;
		}
		_exit(127);
	}

	if (errfd >= 0)
		close(errfd);

	/*
	 * Not waited for. The child called setsid, so when this process exits —
	 * which for a keybind or a tile press is immediately — init adopts and
	 * reaps it. Waiting would turn "launch a game" into a command that
	 * blocks for as long as the game runs, which is what an earlier bug in
	 * this project's file manager did with xdg-open.
	 */
	return EX_OK;
}

/*
 * The other tiles: launchers and media on this machine, then the four things a
 * television needs that are not applications at all.
 *
 * DETECTED, never listed unconditionally. A tile for something that is not
 * installed is a tile that does nothing when pressed, and on a gamepad four
 * metres away there is no status bar to explain why — the honest interface is
 * one where every tile on it works.
 *
 * The system actions go through `systemctl` and `synctl` rather than
 * reimplementing anything, for the same reason the start menu does: there must
 * be one definition of "suspend this machine", or the couch and the desk drift
 * apart.
 */
struct row { const char *id, *name, *exec, *icon, *kind; };

static int apps_table(struct row *rows, int max)
{
	int n = 0;
	(void)max;	/* the table is a literal; the array is sized for it */

	if (have("steam"))
		rows[n++] = (struct row){ "steam-bpm", "Steam Big Picture",
			"syn-arcade big steam", "steam", "app" };
	if (have("retroarch"))
		rows[n++] = (struct row){ "retroarch", "RetroArch",
			"retroarch --fullscreen", "retroarch", "app" };
	if (have("lutris"))
		rows[n++] = (struct row){ "lutris", "Lutris", "lutris",
			"lutris", "app" };
	if (have("heroic"))
		rows[n++] = (struct row){ "heroic", "Heroic", "heroic",
			"heroic", "app" };
	if (have("kodi"))
		rows[n++] = (struct row){ "kodi", "Kodi", "kodi", "kodi", "app" };
	if (have("plex-desktop"))
		rows[n++] = (struct row){ "plex", "Plex", "plex-desktop",
			"plex", "app" };
	else if (have("plexhtpc"))
		rows[n++] = (struct row){ "plex", "Plex", "plexhtpc",
			"plex", "app" };
	if (have("moonlight"))
		rows[n++] = (struct row){ "moonlight", "Moonlight", "moonlight",
			"moonlight", "app" };
	if (have("firefox"))
		rows[n++] = (struct row){ "web", "Web", "firefox --kiosk",
			"firefox", "app" };
	if (have("syn-arcade"))
		rows[n++] = (struct row){ "arcade", "Controllers",
			"syn-arcade gui", "syn-arcade", "app" };

	/* The way OUT is a tile, and it is not optional. A full-screen surface
	 * with exclusive keyboard focus that can only be dismissed by a key
	 * combination somebody has to already know is a trap, and on a gamepad
	 * there is no key combination at all. */
	rows[n++] = (struct row){ "desktop", "Desktop", "", "desktop", "action" };
	rows[n++] = (struct row){ "sleep", "Sleep", "systemctl suspend",
		"sleep", "action" };
	rows[n++] = (struct row){ "restart", "Restart", "systemctl reboot",
		"restart", "action" };
	rows[n++] = (struct row){ "poweroff", "Power off", "systemctl poweroff",
		"poweroff", "action" };

	return n;
}

#define APPS_MAX 24

static int big_apps(bool rec)
{
	struct row rows[APPS_MAX];
	int n = apps_table(rows, APPS_MAX);

	if (rec) {
		rec_row(5, "id", "name", "exec", "icon", "kind");
		for (int i = 0; i < n; i++)
			rec_row(5, rows[i].id, rows[i].name, rows[i].exec,
				rows[i].icon, rows[i].kind);
	} else {
		for (int i = 0; i < n; i++)
			printf("%-12s %-20s %s\n", rows[i].id, rows[i].name,
			       rows[i].exec[0] ? rows[i].exec : "(built in)");
	}
	return EX_OK;
}

/*
 * Run one of those tiles, by id.
 *
 * The shell presses THIS rather than the exec string it was handed, so that
 * what a tile does is decided in one place. A front-end that took the command
 * and ran it itself would be a second launcher — with its own idea of quoting,
 * its own inherited descriptors, and its own answer to what "Desktop" means.
 */
static int big_run(const char *id)
{
	if (!id || !*id) {
		fputs("syn-arcade: big run needs a tile id "
		      "(`syn-arcade big apps` lists them)\n", stderr);
		return EX_USAGE;
	}

	struct row rows[APPS_MAX];
	int n = apps_table(rows, APPS_MAX);

	for (int i = 0; i < n; i++) {
		if (strcmp(rows[i].id, id) != 0)
			continue;

		/* "Desktop" is the way out, and it is not a command: closing
		 * big screen mode IS going back to the desktop, which was
		 * always there underneath. */
		if (!rows[i].exec[0])
			return big_stop();

		/* Split on spaces. Every command in the table above is a
		 * literal written here, none has an argument containing a
		 * space, and nothing user-supplied reaches this — which is why
		 * a word split is honest rather than a quoting bug waiting to
		 * happen. A shell would be, since it would then be running a
		 * string through /bin/sh on behalf of a front-end. */
		char buf[512];
		snprintf(buf, sizeof(buf), "%s", rows[i].exec);

		char *argv[16];
		int argc = 0;
		char *save = NULL;
		for (char *t = strtok_r(buf, " ", &save);
		     t && argc < 15; t = strtok_r(NULL, " ", &save))
			argv[argc++] = t;
		argv[argc] = NULL;

		if (!argc)
			return EX_FAIL;
		return spawn_detached(argv);
	}

	fprintf(stderr, "syn-arcade: no tile called '%s'\n", id);
	return EX_USAGE;
}

/* ── launching ───────────────────────────────────────────────────────────── */

/*
 * Steam Big Picture.
 *
 * `-gamepadui` is the flag for the controller interface Valve ships on the Deck
 * and calls Big Picture on the desktop. Passing it to a Steam that is ALREADY
 * RUNNING works — the second process hands the argument to the first over
 * Steam's own single-instance socket and exits — which is why this does not
 * check for a running client first, and why it returns immediately in that
 * case rather than blocking until Big Picture closes.
 *
 * ⚠ gamescope is opt-in and NOT the default, though on a television it is
 * usually what you want. It is a nested compositor: everything inside it is
 * isolated from this one, which fixes scaling on a panel whose resolution is
 * not the desktop's and breaks anything that expected to talk to the outer
 * session. Defaulting to it would change what "launch Steam" means for
 * somebody who only ever plugs in a second monitor.
 */
static int big_steam(const char *gamescope)
{
	if (!have("steam")) {
		fputs("syn-arcade: Steam is not installed — "
		      "synpkg install steam\n", stderr);
		return EX_FAIL;
	}

	if (gamescope && *gamescope && !have("gamescope")) {
		fputs("syn-arcade: gamescope is not installed — running Big "
		      "Picture without it\n", stderr);
		gamescope = NULL;
	}

	char *argv[16];
	int argc = 0;
	char w[32] = "", h[32] = "", r[32] = "";

	if (gamescope && *gamescope) {
		argv[argc++] = (char *)"gamescope";
		argv[argc++] = (char *)"-f";		/* start fullscreen */
		/* -e is gamescope's Steam integration: it is what lets the
		 * client drive the nested compositor's resolution and HDR per
		 * game, instead of every game being stuck with whatever it was
		 * started at. */
		argv[argc++] = (char *)"-e";

		if (strcmp(gamescope, "default") != 0) {
			/* WxH or WxH@R — the same spelling synui-game-run
			 * takes, because two gaming tools on one system
			 * disagreeing about how a resolution is written is a
			 * papercut nobody remembers the answer to. */
			char res[64] = "";
			const char *at = strchr(gamescope, '@');
			if (at) {
				snprintf(res, sizeof(res), "%.*s",
					 (int)(at - gamescope), gamescope);
				snprintf(r, sizeof(r), "%s", at + 1);
			} else {
				snprintf(res, sizeof(res), "%s", gamescope);
			}
			const char *x = strchr(res, 'x');
			if (x) {
				snprintf(w, sizeof(w), "%.*s", (int)(x - res), res);
				snprintf(h, sizeof(h), "%s", x + 1);
				argv[argc++] = (char *)"-W";
				argv[argc++] = w;
				argv[argc++] = (char *)"-H";
				argv[argc++] = h;
			}
			if (r[0]) {
				argv[argc++] = (char *)"-r";
				argv[argc++] = r;
			}
		}
		argv[argc++] = (char *)"--";
	}

	argv[argc++] = (char *)"steam";
	argv[argc++] = (char *)"-gamepadui";
	argv[argc] = NULL;

	return spawn_detached(argv);
}

/*
 * One game, by appid.
 *
 * steam://rungameid is the URL Steam's own shortcuts use, and it is the only
 * launch path that gets a game its Steam launch options, its compatibility
 * tool and its cloud saves. Running the game's binary directly — which the
 * manifest has enough information to do — would skip all three and produce a
 * game that runs but cannot see your saves.
 */
static int big_launch(const char *appid)
{
	if (!appid || !*appid) {
		fputs("syn-arcade: big launch needs an appid\n", stderr);
		return EX_USAGE;
	}
	for (const char *p = appid; *p; p++) {
		if (!isdigit((unsigned char)*p)) {
			fprintf(stderr, "syn-arcade: '%s' is not an appid — "
					"`syn-arcade big games` lists them\n",
				appid);
			return EX_USAGE;
		}
	}
	if (!have("steam")) {
		fputs("syn-arcade: Steam is not installed\n", stderr);
		return EX_FAIL;
	}

	char url[64];
	snprintf(url, sizeof(url), "steam://rungameid/%s", appid);

	char *argv[] = { (char *)"steam", url, NULL };
	return spawn_detached(argv);
}

/* ── the shell ───────────────────────────────────────────────────────────── */

/*
 * Which screen big screen mode should open on, when nobody said.
 *
 * "Wherever I am" is the only answer a person means, and a layer-shell client
 * cannot work it out: there is no Wayland protocol that tells one where the
 * pointer or the keyboard focus is. synui knows, because it is the compositor,
 * and `synctl outputs` already prints it — the same reason synui has to pass an
 * output name into the start menu rather than letting it choose.
 *
 * Resolved HERE and not in the QML so that the shell is handed a name, and a
 * person running `syn-arcade big start` from a terminal on the second monitor
 * gets big screen mode on the second monitor.
 *
 * Failure is not an error: no synui, no synctl, or output printed in some shape
 * this does not recognise all mean "no preference", and the shell falls back to
 * the first screen. A launcher that refuses to open because it could not decide
 * which monitor to be on would be a worse answer than being on the wrong one.
 */
static bool focused_output(char *buf, size_t n)
{
	FILE *p = popen("synctl outputs 2>/dev/null", "r");
	if (!p)
		return false;

	char json[4096];
	size_t got = fread(json, 1, sizeof(json) - 1, p);
	pclose(p);
	if (got == 0)
		return false;
	json[got] = '\0';

	/* One object per output, and "name" comes before "focused" in each. So
	 * walk the objects and answer with the name of the one that claims the
	 * focus — no JSON parser for a shape this file also controls. */
	for (char *obj = strtok(json, "{"); obj; obj = strtok(NULL, "{")) {
		if (!strstr(obj, "\"focused\":true"))
			continue;
		char *nm = strstr(obj, "\"name\":\"");
		if (!nm)
			continue;
		nm += 8;
		char *end = strchr(nm, '"');
		if (!end)
			continue;
		*end = '\0';
		return snprintf(buf, n, "%s", nm) < (int)n;
	}
	return false;
}

static const char *big_qml(void)
{
	static const char *installed = SYNARCADE_DATADIR "/syn-arcade-big.qml";
	if (access(installed, R_OK) == 0)
		return installed;
	if (access("data/syn-arcade-big.qml", R_OK) == 0)
		return "data/syn-arcade-big.qml";	/* the source tree */
	return installed;
}

/*
 * Open big screen mode.
 *
 * `detach` matters for exactly one caller and is wrong for the others, so it is
 * a flag rather than the default:
 *
 *   synui autostart, the keybind, a terminal   exec in place. The caller
 *       already spawned us to be this; replacing ourselves with quickshell
 *       means one process instead of two and Ctrl-C works from a shell.
 *   the arcade window's button                 must detach. That window is
 *       quickshell, and it runs this through a Process — so without a fork the
 *       big screen shell would BE that Process: the window would sit there
 *       waiting for it to exit, and closing the window would close the pipes
 *       out from under it. Same failure as any other launcher; see
 *       spawn_detached().
 */
static int big_start(const char *output, bool detach)
{
	if (!getenv("WAYLAND_DISPLAY")) {
		fputs("syn-arcade: no Wayland session — big screen mode is a "
		      "layer-shell surface and needs one\n", stderr);
		return EX_FAIL;
	}
	if (!have("quickshell")) {
		fputs("syn-arcade: quickshell is not installed — "
		      "synpkg install quickshell\n", stderr);
		return EX_FAIL;
	}

	/* Checked here, BEFORE any fork, so the person who asked sees the
	 * answer. In the detached case everything after the fork writes to
	 * /dev/null, including the lock's own refusal. */
	if (big_running(NULL)) {
		fputs("syn-arcade: big screen mode is already running\n", stderr);
		return EX_FAIL;
	}

	if (detach) {
		pid_t pid = fork();
		if (pid < 0) {
			fprintf(stderr, "syn-arcade: fork: %s\n", strerror(errno));
			return EX_FAIL;
		}
		if (pid > 0)
			return EX_OK;		/* the caller is free */

		setsid();
		int null = open("/dev/null", O_RDWR);
		if (null >= 0) {
			dup2(null, STDIN_FILENO);
			dup2(null, STDOUT_FILENO);
			dup2(null, STDERR_FILENO);
			if (null > STDERR_FILENO)
				close(null);
		}
		/* …and on through the ordinary path, which now runs in a child
		 * that owns nothing of the caller's. */
	}

	char path[4096];
	if (!big_lock_path(path, sizeof(path)))
		return EX_FAIL;

	int fd = open(path, O_RDWR | O_CREAT, 0600);
	if (fd < 0) {
		fprintf(stderr, "syn-arcade: %s: %s\n", path, strerror(errno));
		return EX_FAIL;
	}

	/*
	 * ⚠ The lock, and why it is taken here rather than checked here.
	 *
	 * A keybind is a thing people press twice. Without this, the second
	 * press starts a SECOND full-screen surface with exclusive keyboard
	 * focus on top of the first, and the only visible symptom is that
	 * closing big screen mode leaves big screen mode on screen — with no
	 * way to tell, since the two are pixel-identical.
	 *
	 * flock and not a pid check because it is held by the KERNEL for as
	 * long as the process lives. It survives exec into quickshell (the fd
	 * is deliberately not CLOEXEC), and it is released by any death,
	 * including a SIGKILL that runs no cleanup.
	 */
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		close(fd);
		fputs("syn-arcade: big screen mode is already running\n", stderr);
		return EX_FAIL;
	}

	int flags = fcntl(fd, F_GETFD);
	if (flags >= 0)
		fcntl(fd, F_SETFD, flags & ~FD_CLOEXEC);

	if (ftruncate(fd, 0) == 0) {
		char buf[32];
		int len = snprintf(buf, sizeof(buf), "%d\n", (int)getpid());
		ssize_t w = write(fd, buf, (size_t)len);
		(void)w;	/* a pid file that failed to write is a worse
				 * status message, not a reason not to start */
	}

	/* Which screen. The QML needs a name it can match against
	 * Quickshell.screens; with nothing to go on it takes the first, so ask
	 * the compositor which one has focus before falling back to that. */
	char out[128] = "";
	if (output && *output)
		snprintf(out, sizeof(out), "%s", output);
	else
		focused_output(out, sizeof(out));
	setenv("SYN_BIG_OUTPUT", out, 1);

	/* OVERWRITTEN, not merely set — the inherited-QS_APP_ID accident.
	 * Every app in this suite hands its whole environment to what it
	 * spawns, so a big screen started from `syn-arcade gui` would wear the
	 * arcade window's identity. */
	setenv("QS_APP_ID", "syn-arcade-big", 1);
	setenv("SYNARCADE_BIN", "syn-arcade", 0);

	execlp("quickshell", "quickshell", "-p", big_qml(), (char *)NULL);

	fputs("syn-arcade: could not start quickshell\n", stderr);
	return EX_FAIL;
}

static int big_stop(void)
{
	pid_t pid = 0;
	if (!big_running(&pid)) {
		fputs("syn-arcade: big screen mode is not running\n", stderr);
		return EX_FAIL;
	}
	if (pid <= 0) {
		fputs("syn-arcade: big screen mode is running but its pid file "
		      "is empty\n", stderr);
		return EX_FAIL;
	}

	/*
	 * ⚠ Check WHAT this pid is before signalling it.
	 *
	 * The lock says something is running; the number says what to kill, and
	 * those are two different claims. A pid file written before a crash,
	 * with the number since reused by the kernel, turns `big stop` into a
	 * command that kills an unrelated process — and pids are reused fastest
	 * on exactly the busy machine where this is hardest to notice.
	 */
	char comm[4096];
	snprintf(comm, sizeof(comm), "/proc/%d/comm", (int)pid);
	char *who = read_file(comm);
	if (who) {
		strip_trailing_newline(who);
		bool ours = strstr(who, "quickshell") || strstr(who, "syn-arcade");
		free(who);
		if (!ours) {
			fprintf(stderr, "syn-arcade: pid %d is not big screen "
					"mode — refusing to signal it\n",
				(int)pid);
			return EX_FAIL;
		}
	}

	if (kill(pid, SIGTERM) != 0) {
		fprintf(stderr, "syn-arcade: could not stop pid %d: %s\n",
			(int)pid, strerror(errno));
		return EX_FAIL;
	}
	return EX_OK;
}

static int big_toggle(const char *output)
{
	/*
	 * Toggle is stop-or-start, not show-or-hide, and that is a deliberate
	 * trade. Keeping the shell resident and hiding it would save the ~300ms
	 * quickshell takes to come up — and would keep a full-screen surface,
	 * its QML engine and its library scan alive for the entire session for
	 * the sake of it, on a desktop where the same key has to work when
	 * nothing has started it yet. Starting it is also the only behaviour
	 * that needs no IPC channel between the compositor's keybind and a
	 * process that may not exist.
	 */
	if (big_running(NULL))
		return big_stop();
	return big_start(output, false);
}

static int big_status(bool rec)
{
	pid_t pid = 0;
	bool running = big_running(&pid);

	char root[SYN_PATH] = "";
	bool steam_found = steam_root(root, sizeof(root));

	int games = 0;
	if (steam_found) {
		game_t *g = games_scan(root, false, &games);
		free(g);
	}

	bool autostart = binds_autostart_get();

	char pidbuf[32] = "-";
	if (running && pid > 0)
		snprintf(pidbuf, sizeof(pidbuf), "%d", (int)pid);
	char gamebuf[32];
	snprintf(gamebuf, sizeof(gamebuf), "%d", games);

	if (rec) {
		rec_row(3, "field", "value", "action");
		rec_row(3, "running", running ? "yes" : "no",
			running ? "action:stop" : "action:start");
		rec_row(3, "pid", pidbuf, "detail");
		rec_row(3, "at login", autostart ? "on" : "off",
			autostart ? "action:autostart-off" : "action:autostart-on");
		rec_row(3, "Steam", steam_found ? root : "NOT FOUND", "detail");
		rec_row(3, "games", gamebuf, "detail");
		rec_row(3, "gamescope", have("gamescope") ? "installed"
			: "NOT INSTALLED", "detail");
		rec_row(3, "quickshell", have("quickshell") ? "installed"
			: "NOT INSTALLED", "detail");
		return EX_OK;
	}

	printf("big screen mode  %s%s%s\n",
	       running ? "running" : "not running",
	       running && pid > 0 ? "  pid " : "",
	       running && pid > 0 ? pidbuf : "");
	printf("  at login       %s\n", autostart ? "on" : "off");
	printf("  Steam          %s\n", steam_found ? root
	     : "NOT FOUND — the library tiles will be empty");
	printf("  games          %d\n", games);
	printf("  gamescope      %s\n", have("gamescope") ? "installed"
	     : "not installed (--gamescope does nothing)");
	printf("  quickshell     %s\n", have("quickshell") ? "installed"
	     : "NOT INSTALLED — `big start` needs it");
	return EX_OK;
}

/* ── dispatch ────────────────────────────────────────────────────────────── */

static const char *opt_value(int argc, char **argv, const char *name)
{
	size_t len = strlen(name);
	for (int i = 0; i < argc; i++)
		if (strncmp(argv[i], name, len) == 0 && argv[i][len] == '=')
			return argv[i] + len + 1;
	return NULL;
}

static bool opt_present(int argc, char **argv, const char *name)
{
	size_t len = strlen(name);
	for (int i = 0; i < argc; i++)
		if (strcmp(argv[i], name) == 0 ||
		    (strncmp(argv[i], name, len) == 0 && argv[i][len] == '='))
			return true;
	return false;
}

static const char *first_operand(int argc, char **argv)
{
	for (int i = 0; i < argc; i++)
		if (argv[i][0] != '-')
			return argv[i];
	return NULL;
}

int cmd_big(int argc, char **argv)
{
	bool rec = opt_present(argc, argv, "--rec");
	const char *sub = argc > 0 && argv[0][0] != '-' ? argv[0] : NULL;

	if (!sub || !strcmp(sub, "status"))
		return big_status(rec);

	int rest_c = argc - 1;
	char **rest = argv + 1;

	if (!strcmp(sub, "start"))
		return big_start(opt_value(rest_c, rest, "--output"),
				 opt_present(rest_c, rest, "--detach"));
	if (!strcmp(sub, "stop"))
		return big_stop();
	if (!strcmp(sub, "toggle"))
		return big_toggle(opt_value(rest_c, rest, "--output"));

	if (!strcmp(sub, "games"))
		return big_games(rec, opt_present(rest_c, rest, "--all"));
	if (!strcmp(sub, "apps"))
		return big_apps(rec);
	if (!strcmp(sub, "run"))
		return big_run(first_operand(rest_c, rest));

	if (!strcmp(sub, "steam")) {
		const char *gs = NULL;
		if (opt_present(rest_c, rest, "--gamescope")) {
			gs = opt_value(rest_c, rest, "--gamescope");
			if (!gs) gs = "default";
		}
		return big_steam(gs);
	}
	if (!strcmp(sub, "launch"))
		return big_launch(first_operand(rest_c, rest));

	/* The gamepad event stream the shell reads. It lives in pad.c with the
	 * rest of the evdev code — this is a name for it, not a second
	 * implementation. */
	if (!strcmp(sub, "nav"))
		return pads_nav_stream();

	if (!strcmp(sub, "autostart")) {
		const char *arg = first_operand(rest_c, rest);
		if (!arg || !strcmp(arg, "status")) {
			bool on = binds_autostart_get();
			if (rec) {
				rec_row(3, "field", "value", "action");
				rec_row(3, "at login", on ? "on" : "off",
					on ? "action:autostart-off"
					   : "action:autostart-on");
			} else {
				puts(on ? "on" : "off");
			}
			return EX_OK;
		}
		if (!strcmp(arg, "on") || !strcmp(arg, "off"))
			return binds_autostart_set(strcmp(arg, "on") == 0);

		fprintf(stderr, "syn-arcade: big autostart takes on, off or "
				"status (not '%s')\n", arg);
		return EX_USAGE;
	}

	fprintf(stderr, "syn-arcade: unknown big command '%s'\n", sub);
	return EX_USAGE;
}
