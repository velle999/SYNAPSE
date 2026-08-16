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

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
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
 * below needs it before it is defined. Likewise a media tile, which is a web
 * page rather than a program, and the cache that says which page. */
static int big_stop(void);
static int big_open(const char *url, bool wait_for_it);
static bool media_url(const char *id, char *out, size_t n);

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
static int spawn_detached_pid(char *const argv[], pid_t *out)
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
	 * Not waited for HERE. The child called setsid, so when this process
	 * exits — which for a keybind or a tile press is immediately — init
	 * adopts and reaps it. Waiting by default would turn "launch a game"
	 * into a command that blocks for as long as the game runs, which is
	 * what an earlier bug in this project's file manager did with xdg-open.
	 *
	 * The pid comes back for the one caller that DOES want to wait — `big
	 * run --wait`, which is how big screen mode learns that the browser it
	 * opened has been closed. That is a caller choosing to block, not this
	 * function deciding for everybody.
	 */
	if (out)
		*out = pid;
	return EX_OK;
}

static int spawn_detached(char *const argv[])
{
	return spawn_detached_pid(argv, NULL);
}

/* ── running something and reading what it said ──────────────────────────── */

/*
 * Run a command and return its stdout. NULL if it could not be run at all;
 * caller frees.
 *
 * ⚠ execvp and NOT popen, and that is not a style preference. The one caller
 * that matters passes a URL out of a config file the user edits, and popen
 * hands its argument to /bin/sh — a feed URL containing a backtick or a
 * semicolon would be a command. There is no shell here, so there is nothing to
 * quote and nothing to get wrong.
 *
 * The timeout is enforced in the CHILD, with alarm(), rather than by the
 * parent watching a clock. curl already has --max-time and this is the belt to
 * its braces: a DNS lookup that hangs before curl's own timer starts would
 * otherwise stall big screen mode's news shelf for as long as the resolver
 * feels like, and the alarm cannot be talked out of it.
 */
static char *run_capture(char *const argv[], int timeout_s)
{
	int fds[2];
	if (pipe(fds) != 0)
		return NULL;

	pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return NULL;
	}

	if (pid == 0) {
		close(fds[0]);
		dup2(fds[1], STDOUT_FILENO);
		if (fds[1] > STDERR_FILENO)
			close(fds[1]);

		int null = open("/dev/null", O_RDWR);
		if (null >= 0) {
			dup2(null, STDIN_FILENO);
			dup2(null, STDERR_FILENO);
			if (null > STDERR_FILENO)
				close(null);
		}

		if (timeout_s > 0)
			alarm((unsigned)timeout_s);
		execvp(argv[0], argv);
		_exit(127);
	}

	close(fds[1]);

	size_t cap = 65536, len = 0;
	char *buf = xmalloc(cap);
	ssize_t got;
	while ((got = read(fds[0], buf + len, cap - len - 1)) > 0) {
		len += (size_t)got;
		if (len + 1 >= cap) {
			/* A feed is a few tens of KB. The ceiling is not
			 * politeness, it is what stops a server that streams
			 * forever from being an out-of-memory kill. */
			if (cap >= 4u * 1024 * 1024)
				break;
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
	}
	buf[len] = '\0';
	close(fds[0]);

	int status = 0;
	waitpid(pid, &status, 0);

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		free(buf);
		return NULL;
	}
	return buf;
}

/* ── reading a JSON reply without a parser ───────────────────────────────── */

/*
 * One string out of a JSON object: "Address":"http://…". No parser, for the
 * same reason vdf_pair is not one — every shape read here is fixed and known,
 * and two of them are printed by synui's own ipc.c.
 *
 * ⚠ `end` BOUNDS THE OBJECT, and NULL means "the rest of the text". It exists
 * because `synctl clients` is an ARRAY: without a bound, a window with no
 * title silently borrows the next window's, and the Running shelf shows the
 * same name twice with no hint that anything went wrong. The single-object
 * callers (a Plex GDM reply, a Jellyfin broadcast) pass NULL and are unchanged.
 */
static bool json_str(const char *text, const char *end, const char *key,
		     char *out, size_t n)
{
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(text, pat);
	if (!p || (end && p >= end))
		return false;
	p += strlen(pat);
	while (*p == ' ' || *p == ':') p++;
	if (*p != '"')
		return false;
	p++;

	size_t w = 0;
	while (*p && *p != '"' && w + 1 < n) {
		if (*p == '\\' && p[1]) {
			p++;
			/* \uXXXX is a control character synui escaped on the
			 * way out — skip the whole escape rather than copying
			 * `u0007` into a window title as five literal
			 * characters. */
			if (*p == 'u' && strlen(p) >= 5) {
				p += 5;
				continue;
			}
		}
		out[w++] = *p++;
	}
	out[w] = '\0';
	return w > 0;
}

/* Whether a JSON boolean in this object is true. Bounded like json_str, and
 * for the same reason. */
static bool json_true(const char *text, const char *end, const char *key)
{
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\":true", key);
	const char *p = strstr(text, pat);
	return p && (!end || p < end);
}

/* ── what this machine can do ────────────────────────────────────────────── */

/*
 * The browser, as an argv.
 *
 * ⚠ NOT `--kiosk`, which is what this shipped with and which was wrong the
 * moment there was an on-screen keyboard. Kiosk mode removes the address bar,
 * the tabs and every control — on a desktop that is a deliberate lockdown, and
 * on a television with a gamepad it means the only pages reachable are the ones
 * something else opened. With a pointer and a keyboard on screen, an ordinary
 * window is a browser somebody can actually use, and Guide still takes the
 * whole thing away.
 */
static const char *first_installed(const char *const *cands, const char **cache)
{
	/* Cached because every one of these is a fork of /bin/sh, and the
	 * tables below ask twice — once to decide whether the tile exists and
	 * once for its command. Five extra shells to draw one tile is the kind
	 * of thing that makes a launcher feel slow for no visible reason. */
	if (*cache)
		return **cache ? *cache : NULL;
	for (int i = 0; cands[i]; i++) {
		if (have(cands[i])) {
			*cache = cands[i];
			return cands[i];
		}
	}
	*cache = "";
	return NULL;
}

static const char *browser_prog(void)
{
	static const char *const cands[] = {
		"firefox", "librewolf", "chromium", "brave", "google-chrome-stable",
		"epiphany", "falkon", NULL
	};
	static const char *cache;
	return first_installed(cands, &cache);
}

/*
 * The terminal.
 *
 * syntty first because it is this system's own terminal and the default
 * everywhere else in the desktop; the other two are what a machine that has
 * replaced it will have. Ordering matters more than usual here: a big screen
 * tile that opens a different terminal from Super+Return is the sort of small
 * inconsistency that makes a desktop feel assembled rather than designed.
 */
static const char *terminal_prog(void)
{
	static const char *const cands[] = {
		"syntty", "kitty", "foot", "alacritty", "xterm", NULL
	};
	static const char *cache;
	return first_installed(cands, &cache);
}

/*
 * A music player, in the order somebody would want one.
 *
 * Dedicated music applications first — they have a library, cover art and a
 * queue, which is what "Music" means on a television — then the general media
 * players, which will play an album and are better than an empty shelf. mpv is
 * deliberately absent: with no file to open it is a black window, which is a
 * tile that looks broken.
 */
static const char *music_prog(void)
{
	static const char *const cands[] = {
		"strawberry", "elisa", "amberol", "rhythmbox", "lollypop",
		"clementine", "audacious", "deadbeef", "quodlibet", "tauon",
		"plexamp", "spotify", "vlc", NULL
	};
	static const char *cache;
	return first_installed(cands, &cache);
}

/*
 * The other tiles: launchers, media, the browser and a terminal, then the four
 * things a television needs that are not applications at all.
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
 *
 * ── The five columns that are not the command ───────────────────────────────
 *
 *   shelf    which row of the television this belongs on. The shelf is a
 *            property of the TILE and not of the QML, so adding one here puts
 *            it in the right place with no change to the shell.
 *   kind     what pressing it DOES — an app to launch, or an action.
 *   pointer  whether launching it should turn the controller into a mouse.
 *            True for anything with a pointer-driven interface and false for
 *            everything else, because a stick that moves a cursor is exactly
 *            wrong in a launcher that has its own controller support (Steam
 *            Big Picture) and exactly right in a web browser.
 *   keys     whether the on-screen keyboard is worth offering. A terminal and
 *            a browser need one; RetroArch does not.
 *   full     whether this tile needs HELP filling the screen.
 *
 *            A window is what a desktop wants and a television does not: from
 *            the sofa, a titlebar and a strip of wallpaper around the edge are
 *            the whole difference between an appliance and somebody's computer
 *            left switched on. So a tile press asks the compositor to
 *            fullscreen what it opened — see fullscreen_when_ready below.
 *
 *            ⚠ FALSE is the interesting value, and it does not mean "leave it
 *            in a window". Steam Big Picture, RetroArch and Kodi already open
 *            full-screen, and plexhtpc is the HTPC build of Plex for exactly
 *            this reason. Asking on their behalf buys nothing and costs
 *            something: Steam maps a small startup splash BEFORE Big Picture
 *            itself, and helping would blow that splash up to fill a wall for
 *            a second on the way in. The rule is "does this need help", not
 *            "should this be full-screen".
 */
struct row {
	const char *id, *name, *exec, *icon, *kind, *shelf;
	bool pointer, keys, full;
};

static int apps_table(struct row *rows, int max)
{
	int n = 0;
	(void)max;	/* the table is a literal; the array is sized for it */

	/* ── play: the game launchers ── */
	/* Big Picture is full-screen the moment it finishes starting, and gets
	 * there through a splash window that must not be helped. */
	if (have("steam"))
		rows[n++] = (struct row){ "steam-bpm", "Steam Big Picture",
			"syn-arcade big steam", "steam", "app", "play",
			false, false, false };
	if (have("retroarch"))		/* --fullscreen, up in the exec */
		rows[n++] = (struct row){ "retroarch", "RetroArch",
			"retroarch --fullscreen", "retroarch", "app", "play",
			false, false, false };
	if (have("lutris"))
		rows[n++] = (struct row){ "lutris", "Lutris", "lutris",
			"lutris", "app", "play", true, false, true };
	if (have("heroic"))
		rows[n++] = (struct row){ "heroic", "Heroic", "heroic",
			"heroic", "app", "play", true, false, true };
	if (have("moonlight"))
		rows[n++] = (struct row){ "moonlight", "Moonlight", "moonlight",
			"moonlight", "app", "play", true, false, true };

	/* ── media ── */
	{
		const char *music = music_prog();
		if (music)
			rows[n++] = (struct row){ "music", "Music", music,
				"music", "app", "media", true, false, true };
	}
	if (have("kodi"))		/* opens full-screen by itself */
		rows[n++] = (struct row){ "kodi", "Kodi", "kodi", "kodi", "app",
			"media", false, false, false };
	if (have("plex-desktop"))
		rows[n++] = (struct row){ "plex", "Plex", "plex-desktop",
			"plex", "app", "media", true, false, true };
	else if (have("plexhtpc"))	/* the HTPC build: already full-screen */
		rows[n++] = (struct row){ "plex", "Plex", "plexhtpc",
			"plex", "app", "media", true, false, false };
	if (have("jellyfinmediaplayer"))
		rows[n++] = (struct row){ "jellyfin", "Jellyfin",
			"jellyfinmediaplayer", "jellyfin", "app", "media",
			true, false, true };
	else if (have("jellyfin-media-player"))
		rows[n++] = (struct row){ "jellyfin", "Jellyfin",
			"jellyfin-media-player", "jellyfin", "app", "media",
			true, false, true };

	/* ── apps: the two that need a pointer and a keyboard ── */
	if (browser_prog())
		rows[n++] = (struct row){ "web", "Web", browser_prog(),
			"firefox", "app", "apps", true, true, true };
	if (terminal_prog())
		rows[n++] = (struct row){ "terminal", "Terminal",
			terminal_prog(), "terminal", "app", "apps",
			true, true, true };
	if (have("syn-arcade"))
		rows[n++] = (struct row){ "arcade", "Controllers",
			"syn-arcade gui", "syn-arcade", "app", "apps",
			true, false, true };

	/* The way OUT is a tile, and it is not optional. A full-screen surface
	 * with exclusive keyboard focus that can only be dismissed by a key
	 * combination somebody has to already know is a trap, and on a gamepad
	 * there is no key combination at all. */
	rows[n++] = (struct row){ "desktop", "Desktop", "", "desktop", "action",
		"system", false, false, false };
	rows[n++] = (struct row){ "sleep", "Sleep", "systemctl suspend",
		"sleep", "action", "system", false, false, false };
	rows[n++] = (struct row){ "restart", "Restart", "systemctl reboot",
		"restart", "action", "system", false, false, false };
	rows[n++] = (struct row){ "poweroff", "Power off", "systemctl poweroff",
		"poweroff", "action", "system", false, false, false };

	return n;
}

#define APPS_MAX 32

static int big_apps(bool rec)
{
	struct row rows[APPS_MAX];
	int n = apps_table(rows, APPS_MAX);

	if (rec) {
		/* `full` is APPENDED and not slotted in among the others: the
		 * shell reads columns by name, but the test suite reads a few
		 * of them by number, and so does anybody at a prompt with a
		 * `cut -f`. A new column belongs on the end. */
		rec_row(9, "id", "name", "exec", "icon", "kind", "shelf",
			"pointer", "keys", "full");
		for (int i = 0; i < n; i++)
			rec_row(9, rows[i].id, rows[i].name, rows[i].exec,
				rows[i].icon, rows[i].kind, rows[i].shelf,
				rows[i].pointer ? "1" : "0",
				rows[i].keys ? "1" : "0",
				rows[i].full ? "1" : "0");
	} else {
		for (int i = 0; i < n; i++)
			printf("%-10s %-8s %-20s %s%s%s\n", rows[i].id,
			       rows[i].shelf, rows[i].name,
			       rows[i].exec[0] ? rows[i].exec : "(built in)",
			       rows[i].pointer ? "   [mouse]" : "",
			       rows[i].full ? "   [fullscreen]" : "");
	}
	return EX_OK;
}

/* ── filling the screen ──────────────────────────────────────────────────── */

/*
 * The compositor is the only thing that can make a launch fill a television,
 * and it offers exactly one verb for it: `synctl dispatch fullscreen_toggle`.
 *
 * ⚠ THAT VERB IS A TOGGLE, AND IT ACTS ON WHATEVER HAS THE FOCUS WHEN IT
 * ARRIVES. There is no "set fullscreen" to reach for — input.c has the one
 * action and it flips `!focused_view->fullscreen`. Fired blindly after a
 * launch it is wrong in both directions: too early and it fullscreens the
 * window the tile was covering, and on anything that got there by itself it
 * lands on a window that is ALREADY full and puts it back in a box. That
 * second case is not hypothetical — it is half the play shelf.
 *
 * So this looks before it leaps:
 *
 *   1. remember what had the focus BEFORE the launch, so a new window can be
 *      told from the old one. `synctl activewindow` prints {} when nothing is
 *      focused, which on a television is the ordinary case.
 *   2. poll until a DIFFERENT window has the focus. Something that never
 *      shows one — a single-instance app that handed its argument to the copy
 *      already running — times out, and nothing is toggled.
 *   3. let it SETTLE and read the state AGAIN, because an app that
 *      fullscreens itself does so a moment after mapping and the gap between
 *      is long enough to sample. Without this the toggle races the app, and
 *      when it wins the viewer loses.
 *   4. toggle only if it is still not fullscreen.
 *
 * Step 3 is what makes step 4 safe, and the `full` column is what keeps this
 * away from the tiles where even a correct answer is not worth the wait.
 */
struct focus {
	char app_id[128];
	long pid;
	bool fullscreen;
	bool any;		/* false when nothing at all is focused */
};

/*
 * What synui says has the focus. False means the question could not be ASKED
 * — no synctl on PATH, no compositor listening — which is different from `{}`,
 * "nothing is focused", and the difference decides whether to bother waiting.
 */
static bool focused_window(struct focus *f)
{
	char *argv[] = { (char *)"synctl", (char *)"activewindow", NULL };
	char *json = run_capture(argv, 2);
	if (!json)
		return false;

	memset(f, 0, sizeof(*f));
	f->pid = -1;

	/*
	 * Looked up by name with no JSON parser, the same way focused_output
	 * reads `synctl outputs`, and for the same reason: this file and ipc.c
	 * both know the shape.
	 *
	 * Safe against a hostile window TITLE, which is the one field here a
	 * stranger controls. ipc.c escapes the quotes inside a string, so a
	 * page that titles itself `","fullscreen":true` arrives as
	 * \",\"fullscreen\":true — and `"fullscreen":` cannot match across the
	 * backslash. The spellings searched for below exist nowhere but as
	 * real keys.
	 */
	const char *p = strstr(json, "\"app_id\":\"");
	if (p) {
		p += 10;
		const char *e = strchr(p, '"');
		if (e) {
			int len = (int)(e - p);
			if (len > (int)sizeof(f->app_id) - 1)
				len = (int)sizeof(f->app_id) - 1;
			snprintf(f->app_id, sizeof(f->app_id), "%.*s", len, p);
			f->any = true;
		}
	}
	if ((p = strstr(json, "\"pid\":")))
		f->pid = strtol(p + 6, NULL, 10);
	f->fullscreen = strstr(json, "\"fullscreen\":true") != NULL;

	free(json);
	return true;
}

/* pid AND app_id, because either alone is a false match waiting to happen: pids
 * are reused, and two windows of one application share an app_id. */
static bool same_window(const struct focus *a, const struct focus *b)
{
	return a->any && b->any && a->pid == b->pid &&
	       strcmp(a->app_id, b->app_id) == 0;
}

#define FULL_STEP_MS      250	/* between looks */
#define FULL_WAIT_MS    15000	/* before giving up on a window appearing */
#define FULL_SETTLE_MS    800	/* for an app to fullscreen itself first */

static void sleep_ms(int ms)
{
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
	while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
		;
}

static void fullscreen_when_ready(const struct focus *before)
{
	struct focus now;

	for (int waited = 0; waited < FULL_WAIT_MS; waited += FULL_STEP_MS) {
		sleep_ms(FULL_STEP_MS);

		if (!focused_window(&now) || !now.any)
			continue;		/* nothing focused yet */
		if (same_window(&now, before))
			continue;		/* still the old window */

		sleep_ms(FULL_SETTLE_MS);

		if (!focused_window(&now) || !now.any)
			return;			/* it went away again */
		/* Or it went away and handed the focus BACK. Toggling now
		 * would fullscreen a window nobody asked about. */
		if (same_window(&now, before))
			return;
		if (now.fullscreen)
			return;			/* it got there on its own */

		char *argv[] = { (char *)"synctl", (char *)"dispatch",
				 (char *)"fullscreen_toggle", NULL };
		free(run_capture(argv, 2));
		return;
	}
}

/*
 * ⚠ THE WAITING ABOVE HAPPENS IN A CHILD, and that is not tidiness.
 *
 * The waitpid in spawn_wait is how big screen mode learns the application has
 * been closed — and the shell tells a real close from a single-instance
 * hand-off BY HOW LONG IT TOOK: under three seconds is a hand-off, see the
 * `lived < 3000` comment in syn-arcade-big.qml. Polling for a window on the
 * way to that wait would add up to fifteen seconds to every launch, turning
 * every hand-off into a "close" and throwing the television back over the
 * browser somebody just opened. That is precisely the bug the QML comment
 * exists to prevent, so this has to run BESIDE the wait and not before it.
 *
 * Double-forked so that nothing has to reap it: spawn_wait's own waitpid names
 * a single pid and may block for hours, which would leave a plain child a
 * zombie for all of them. The middle process exits at once and init adopts the
 * one doing the work.
 *
 * Its stdio goes to /dev/null for the reason spawn_detached documents at
 * length — this outlives its starter, and a launcher hands no descendant
 * anything the caller owns.
 */
static void fullscreen_after_launch(const struct focus *before)
{
	pid_t mid = fork();
	if (mid < 0)
		return;

	if (mid == 0) {
		if (fork() == 0) {
			setsid();
			int null = open("/dev/null", O_RDWR);
			if (null >= 0) {
				dup2(null, STDIN_FILENO);
				dup2(null, STDOUT_FILENO);
				dup2(null, STDERR_FILENO);
				if (null > STDERR_FILENO)
					close(null);
			}
			fullscreen_when_ready(before);
			_exit(0);
		}
		_exit(0);
	}

	int st = 0;
	while (waitpid(mid, &st, 0) < 0 && errno == EINTR)
		;
}

/*
 * Start something, fill the screen with it, and stay alive exactly as long as
 * it does.
 *
 * The two blocking callers — a tile press and a headline — did the spawn and
 * the wait identically, and the fullscreen step has to sit between them: any
 * earlier and there is no window to fullscreen, any later and the application
 * has already been closed.
 */
static int spawn_wait(char *const argv[], bool fill)
{
	/*
	 * Asked BEFORE the spawn, and its failure is the answer to whether the
	 * waiter is worth starting at all: `false` here means no synctl and no
	 * compositor to ask, so there would be fifteen seconds of forking to
	 * reach a question nobody can answer. `{}` — nothing focused — comes
	 * back TRUE and is a perfectly good starting point.
	 */
	struct focus before;
	memset(&before, 0, sizeof(before));
	if (fill && !focused_window(&before))
		fill = false;

	pid_t pid = 0;
	int rc = spawn_detached_pid(argv, &pid);
	if (rc != EX_OK || pid <= 0)
		return rc;

	if (fill)
		fullscreen_after_launch(&before);

	/* ⚠ EINTR is not "it finished". A signal arriving while this blocks
	 * would otherwise be read as the application closing, and big screen
	 * mode would come back over the top of something still in use. */
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
		;
	return EX_OK;
}

/* ── what is open ────────────────────────────────────────────────────────── */

/*
 * Big screen mode used to be able to run exactly ONE application, and it did
 * not say so.
 *
 * The shell had a single `Process`, and starting a second application while
 * the first was still running set `running = true` on a process that was
 * already running — which quickshell treats as a no-op, silently. Everything
 * else still happened: the interface recorded the new tile as the active one,
 * re-pointed the controller-as-mouse and the on-screen keyboard at it, and got
 * out of the way. So the television stepped aside to reveal the application
 * you were already looking at, and the only way out was a keyboard.
 *
 * Fixing the launch is half of it. The other half is that a machine which can
 * have three things open needs to be able to SAY which three, and to switch
 * and close them — from a gamepad, with no keyboard in reach.
 *
 * ── The compositor is the register, and nothing here is ────────────────────
 *
 * These verbs deliberately keep NO list of their own. `synctl clients` is what
 * synui has mapped, which is the only answer that stays true when an
 * application opens a second window, exits on its own, is closed from the
 * desktop, or was never started from here at all. A launcher that kept its own
 * tally would drift from the screen within a minute of anybody touching a
 * keyboard, and every "close" aimed at a stale row would land on nothing or,
 * worse, on something else.
 */
struct win {
	char app_id[128];
	char title[256];
	char name[128];		/* the tile's name, when one matches */
	char icon[64];
	bool focused, minimized;
};

/*
 * Which tile, if any, opened a window with this app-id — so the Running shelf
 * can say "Web" and show the browser's tile art rather than "org.mozilla.
 * firefox" in the same grey as everything else.
 *
 * ⚠ A HEURISTIC, and it has to be. Nothing anywhere promises that the program
 * a tile runs and the app-id its window reports are related: `plexhtpc` maps
 * `plexhtpc`, but Firefox has been `firefox`, `Navigator` and
 * `org.mozilla.firefox` depending on the build and the backend. So this tries
 * the spellings that are actually observed, in order, and falls back to the
 * window's own title — which is never wrong, only less pretty.
 */
static const struct row *tile_for(const struct row *rows, int n,
				  const char *app_id)
{
	if (!app_id || !*app_id)
		return NULL;

	/* org.mozilla.firefox → firefox. The last dot-component of a
	 * reverse-DNS app-id is the program often enough to be worth trying,
	 * and an app-id with no dots is its own last component. */
	const char *tail = strrchr(app_id, '.');
	tail = tail ? tail + 1 : app_id;

	/*
	 * ⚠ TWO PASSES, and the order is the whole correctness of this.
	 *
	 * What a tile CALLS ITSELF is checked before what it RUNS, because two
	 * tiles here run the same program: "Steam Big Picture" is
	 * `syn-arcade big steam` and "Controllers" is `syn-arcade gui`. With
	 * one pass in table order, every window this binary opens was labelled
	 * Steam Big Picture — the arcade GUI included, which is how this was
	 * caught the first time `big windows` was run against a real session.
	 */
	for (int i = 0; i < n; i++) {
		if (strcmp(rows[i].kind, "app") != 0)
			continue;
		if (strcasecmp(app_id, rows[i].id) == 0 ||
		    strcasecmp(tail, rows[i].id) == 0 ||
		    strcasecmp(app_id, rows[i].icon) == 0 ||
		    strcasecmp(tail, rows[i].icon) == 0)
			return &rows[i];
	}

	for (int i = 0; i < n; i++) {
		if (strcmp(rows[i].kind, "app") != 0)
			continue;

		/* The program, which is the first word of the exec and may be
		 * a path. */
		char prog[128];
		snprintf(prog, sizeof(prog), "%s", rows[i].exec);
		char *sp = strchr(prog, ' ');
		if (sp) *sp = '\0';
		const char *base = strrchr(prog, '/');
		base = base ? base + 1 : prog;

		/* Our own name is never evidence. A tile that runs
		 * `syn-arcade …` is a tile that launches something ELSE, and
		 * the window that appears belongs to whatever that was. */
		if (strcmp(base, "syn-arcade") == 0)
			continue;

		if (strcasecmp(app_id, base) == 0 ||
		    strcasecmp(tail, base) == 0)
			return &rows[i];
	}
	return NULL;
}

/*
 * Every mapped window synui has, newest workspace order first.
 *
 * Returns -1 when the question could not be ASKED — no synctl, no compositor —
 * which is different from "nothing is open", and the callers say so
 * differently.
 */
static int windows_collect(struct win *out, int max)
{
	char *argv[] = { (char *)"synctl", (char *)"clients", NULL };
	char *json = run_capture(argv, 3);
	if (!json)
		return -1;

	struct row rows[APPS_MAX];
	int nrows = apps_table(rows, APPS_MAX);

	/*
	 * ⚠ Objects are found by their FIRST KEY, `{"app_id":"`, and not by
	 * splitting on braces. A window title is the one string here written by
	 * a stranger — a web page picks it — and a title containing `},{` would
	 * cut a brace-split parser in half. It cannot forge this: synui escapes
	 * every quote inside a string, so the real quotes in `{"app_id":"`
	 * appear nowhere but at the start of a real object.
	 */
	static const char *const head = "{\"app_id\":\"";
	int n = 0;

	for (const char *p = strstr(json, head); p && n < max; ) {
		const char *end = strstr(p + strlen(head), head);

		struct win *w = &out[n];
		memset(w, 0, sizeof(*w));
		json_str(p, end, "app_id", w->app_id, sizeof(w->app_id));
		json_str(p, end, "title", w->title, sizeof(w->title));
		w->focused   = json_true(p, end, "focused");
		w->minimized = json_true(p, end, "minimized");

		const struct row *t = tile_for(rows, nrows, w->app_id);
		if (t) {
			snprintf(w->name, sizeof(w->name), "%s", t->name);
			snprintf(w->icon, sizeof(w->icon), "%s", t->icon);
		} else {
			/* Clipped ON PURPOSE, and said so with a precision so
			 * that gcc knows it too: a window title is as long as
			 * a web page felt like making it, and what this feeds
			 * is one tile on a shelf. */
			snprintf(w->name, sizeof(w->name), "%.*s",
				 (int)sizeof(w->name) - 1,
				 w->title[0] ? w->title : w->app_id);
			snprintf(w->icon, sizeof(w->icon), "%.*s",
				 (int)sizeof(w->icon) - 1, w->app_id);
		}

		if (w->name[0] || w->app_id[0])
			n++;
		p = end;
	}

	free(json);
	return n;
}

#define WINS_MAX 32

static int big_windows(bool rec)
{
	struct win wins[WINS_MAX];
	int n = windows_collect(wins, WINS_MAX);

	if (n < 0) {
		fputs("syn-arcade: no synui to ask — `big windows` needs a "
		      "running compositor and synctl on PATH\n", stderr);
		return EX_FAIL;
	}

	if (rec) {
		rec_row(5, "app_id", "name", "icon", "title", "focused");
		for (int i = 0; i < n; i++)
			rec_row(5, wins[i].app_id, wins[i].name, wins[i].icon,
				wins[i].title, wins[i].focused ? "1" : "0");
		return EX_OK;
	}

	if (!n) {
		puts("nothing is open");
		return EX_OK;
	}
	for (int i = 0; i < n; i++)
		printf("%-24s %-20s %s%s\n", wins[i].app_id, wins[i].name,
		       wins[i].title,
		       wins[i].focused ? "   [focused]" : "");
	return EX_OK;
}

/*
 * Switch to a window, or close one, by app-id.
 *
 * Both are one `synctl dispatch` — see the comment on focus_app in synui's
 * input.c for why the compositor grew a by-app-id verb rather than this
 * vendoring a protocol to ask the same question.
 *
 * ⚠ NOT a shell. The app-id is a string a window chose for itself, and
 * `system()` would hand a semicolon in one to /bin/sh.
 */
static int big_window_act(const char *app_id, bool close_it)
{
	if (!app_id || !*app_id) {
		fprintf(stderr, "syn-arcade: big %s needs an app-id "
				"(`syn-arcade big windows` lists them)\n",
			close_it ? "close" : "focus");
		return EX_USAGE;
	}

	char *argv[] = { (char *)"synctl", (char *)"dispatch",
			 (char *)(close_it ? "close_app" : "focus_app"),
			 (char *)app_id, NULL };
	char *reply = run_capture(argv, 3);
	if (!reply) {
		fputs("syn-arcade: synui did not answer — is it running?\n",
		      stderr);
		return EX_FAIL;
	}
	free(reply);
	return EX_OK;
}

/*
 * Run one of those tiles, by id.
 *
 * The shell presses THIS rather than the exec string it was handed, so that
 * what a tile does is decided in one place. A front-end that took the command
 * and ran it itself would be a second launcher — with its own idea of quoting,
 * its own inherited descriptors, and its own answer to what "Desktop" means.
 *
 * ── `wait` is what makes big screen mode come BACK ──────────────────────────
 *
 * Without it this returns the moment the child is forked, and the shell has no
 * way to know when somebody has finished with the browser — which is why big
 * screen mode used to quit on the way out and stay quit. With it, this process
 * stays alive for exactly as long as the application does, so the shell can
 * simply watch its own Process exit and put the television back.
 *
 * ⚠ Waiting does NOT mean the child keeps our descriptors. It is still forked
 * with setsid and /dev/null on all three, exactly as spawn_detached does and
 * for exactly the same reason — a game that inherits quickshell's pipes dies
 * on its first line of logging. The only difference is that this process hangs
 * around to reap it. If we are killed first the application is unaffected: it
 * has its own session and init adopts it.
 */
static int big_run(const char *id, bool wait_for_it)
{
	if (!id || !*id) {
		fputs("syn-arcade: big run needs a tile id "
		      "(`syn-arcade big apps` lists them)\n", stderr);
		return EX_USAGE;
	}

	/* A media server found on the network is a tile with no program behind
	 * it — the answer is a web page. Its URL comes back out of the same
	 * cache `big media` wrote, so the shell never hands us one. */
	if (strncmp(id, "plex-", 5) == 0 || strncmp(id, "jellyfin-", 9) == 0) {
		char url[512];
		if (media_url(id, url, sizeof(url)))
			return big_open(url, wait_for_it);
		fprintf(stderr, "syn-arcade: no media server called '%s' — "
				"`syn-arcade big media --refresh` looks again\n", id);
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

		/* Without --wait this is not a tile press from the television:
		 * it is somebody at a prompt, or one of the system actions,
		 * which open no window to fill. Filling the screen belongs to
		 * the shell's launch path and not to the verb. */
		if (!wait_for_it)
			return spawn_detached(argv);

		return spawn_wait(argv, rows[i].full);
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

/*
 * Open a URL in the browser.
 *
 * Every URL that reaches this comes from somewhere else: a headline in an RSS
 * feed, or a media server that answered a broadcast. So it is CHECKED rather
 * than trusted — http or https, no whitespace, no control characters, nothing
 * that could be read as an option by the browser it is handed to. A URL
 * beginning with a dash is the whole attack: `firefox --something` is not a
 * page, and `--` is not enough on its own because not every browser honours it.
 */
static bool url_ok(const char *url)
{
	if (!url || !*url)
		return false;
	if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
		return false;
	if (strlen(url) > 2000)
		return false;
	for (const char *p = url; *p; p++)
		if ((unsigned char)*p <= 0x20 || (unsigned char)*p == 0x7f)
			return false;
	return true;
}

static int big_open(const char *url, bool wait_for_it)
{
	if (!url_ok(url)) {
		fprintf(stderr, "syn-arcade: '%s' is not an http(s) URL\n",
			url ? url : "");
		return EX_USAGE;
	}

	const char *browser = browser_prog();
	if (!browser) {
		fputs("syn-arcade: no web browser installed\n", stderr);
		return EX_FAIL;
	}

	char *argv[] = { (char *)browser, (char *)url, NULL };
	if (!wait_for_it)
		return spawn_detached(argv);

	/* --wait, for the same reason `big run --wait` has it: this is how the
	 * shell finds out that the page it opened has been closed, and puts
	 * the television back.
	 *
	 * ⚠ A browser that is ALREADY running exits immediately — the second
	 * process hands the URL to the first over its own socket and is done —
	 * so the shell must not treat this returning as "they finished
	 * reading". It does not: it stays out of the way until Guide is
	 * pressed, and only an app that was actually started brings it back.
	 *
	 * A headline is a browser window, and a browser window is the case the
	 * `full` column exists for — nothing about a web page fills a
	 * television by itself. So this one is always true. */
	return spawn_wait(argv, true);
}

/* ── where the caches live ───────────────────────────────────────────────── */

/*
 * $XDG_CACHE_HOME/syn-arcade/<rel>, or ~/.cache/syn-arcade/<rel>.
 *
 * The cache and not the config directory, and the distinction is load-bearing
 * for the test suite as much as for tidiness: everything under here is
 * DERIVED — headlines off the internet, servers that answered a broadcast —
 * and deleting the lot costs a refresh and nothing else.
 */
static bool cache_path(char *buf, size_t n, const char *rel)
{
	const char *xdg = getenv("XDG_CACHE_HOME");
	if (xdg && *xdg)
		return snprintf(buf, n, "%s/syn-arcade/%s", xdg, rel) < (int)n;

	char sub[256];
	snprintf(sub, sizeof(sub), ".cache/syn-arcade/%s", rel);
	return home_path(buf, n, sub);
}

/* How old the file is in seconds, or -1 if it is not there. */
static long file_age(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return -1;
	time_t now = time(NULL);
	if (now < st.st_mtime)
		return 0;		/* clock stepped backwards */
	return (long)(now - st.st_mtime);
}

/*
 * Print a cache file, and say whether there was one.
 *
 * The cache IS the record text, so this is a copy — no parse, no re-encode,
 * and no chance of the cached form and the printed form disagreeing.
 */
static bool cache_print(const char *path)
{
	char *text = read_file(path);
	if (!text)
		return false;
	fputs(text, stdout);
	free(text);
	return true;
}

/* Whether anything at all is allowed to touch the network.
 *
 * The suite sets this. Two of the commands below talk to the internet and to
 * the local network, and a test run that did either would be a test whose
 * result depended on the machine it ran on and on whether the building had
 * working DNS that morning. */
static bool net_allowed(void)
{
	const char *e = getenv("SYN_ARCADE_NO_NET");
	return !(e && *e && strcmp(e, "0") != 0);
}

/* ── media servers on the network ────────────────────────────────────────── */

/*
 * Plex and Jellyfin both answer a UDP broadcast, and that is the only reliable
 * way to find them.
 *
 * There is no shared standard here and no daemon on this machine that knows —
 * mDNS would be the tidy answer and neither product uses it for this. So each
 * is asked in its own dialect, on one socket each, and both are listened to at
 * once:
 *
 *   Plex     GDM. "M-SEARCH * HTTP/1.0" to 239.0.0.250:32414, and the reply is
 *            an HTTP-shaped block with `Name:` and `Port:` headers. The
 *            ADDRESS is not in the reply at all — it is where the packet came
 *            from, which is why recvfrom's source address is what builds the
 *            URL.
 *   Jellyfin the literal string "who is JellyfinServer?" to port 7359, and the
 *            reply is JSON with an "Address" that is already a full URL.
 *
 * ⚠ Broadcast, not just multicast, for both. A router or a bridge that does
 * not forward multicast is common enough on home networks — and on a machine
 * with several interfaces (a VM bridge, a VPN) the multicast leaves through
 * whichever one the kernel picked. Sending both costs two datagrams.
 *
 * Servers on THIS machine answer neither reliably (a server does not always
 * broadcast to itself), so localhost is probed directly afterwards.
 */
typedef struct {
	char id[32];
	char name[128];
	char url[256];
	char source[16];	/* "plex" or "jellyfin" */
} server_t;

static void servers_add(server_t *out, int *n, int max, const char *source,
			const char *name, const char *url)
{
	if (*n >= max || !url || !*url)
		return;
	for (int i = 0; i < *n; i++)
		if (strcmp(out[i].url, url) == 0)
			return;			/* answered twice */

	server_t *s = &out[(*n)++];
	memset(s, 0, sizeof(*s));
	snprintf(s->id, sizeof(s->id), "%s-%d", source, *n);
	snprintf(s->name, sizeof(s->name), "%s",
		 (name && *name) ? name : source);
	snprintf(s->url, sizeof(s->url), "%s", url);
	snprintf(s->source, sizeof(s->source), "%s", source);
}

/* One header out of a Plex GDM reply: `Name: Living Room`. */
static bool gdm_field(const char *text, const char *key, char *out, size_t n)
{
	size_t klen = strlen(key);
	for (const char *p = text; p && *p; ) {
		if (strncasecmp(p, key, klen) == 0 && p[klen] == ':') {
			p += klen + 1;
			while (*p == ' ' || *p == '\t') p++;
			size_t w = 0;
			while (*p && *p != '\r' && *p != '\n' && w + 1 < n)
				out[w++] = *p++;
			out[w] = '\0';
			return w > 0;
		}
		p = strchr(p, '\n');
		if (p) p++;
	}
	return false;
}

/* Is something listening on this port of this host? A connect() with a short
 * timeout, which is the only question that matters — a Plex server on this
 * machine is one that has port 32400 open. */
static bool port_open(const char *host, int port, int ms)
{
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return false;

	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
		close(fd);
		return false;
	}

	bool ok = false;
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
		ok = true;
	} else if (errno == EINPROGRESS) {
		struct pollfd p = { .fd = fd, .events = POLLOUT };
		if (poll(&p, 1, ms) > 0) {
			int err = 0;
			socklen_t len = sizeof(err);
			if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0)
				ok = err == 0;
		}
	}
	close(fd);
	return ok;
}

#define DISCOVER_MS 900

static int media_discover(server_t *out, int max)
{
	int n = 0;

	if (!net_allowed())
		return 0;

	int plex = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	int jelly = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

	int yes = 1;
	if (plex >= 0)
		setsockopt(plex, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
	if (jelly >= 0)
		setsockopt(jelly, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

	struct sockaddr_in to;
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;

	static const char gdm[] = "M-SEARCH * HTTP/1.0\r\n\r\n";
	if (plex >= 0) {
		to.sin_port = htons(32414);
		inet_pton(AF_INET, "239.0.0.250", &to.sin_addr);
		sendto(plex, gdm, sizeof(gdm) - 1, 0,
		       (struct sockaddr *)&to, sizeof(to));
		inet_pton(AF_INET, "255.255.255.255", &to.sin_addr);
		sendto(plex, gdm, sizeof(gdm) - 1, 0,
		       (struct sockaddr *)&to, sizeof(to));
	}

	static const char jf[] = "who is JellyfinServer?";
	if (jelly >= 0) {
		to.sin_port = htons(7359);
		inet_pton(AF_INET, "255.255.255.255", &to.sin_addr);
		sendto(jelly, jf, sizeof(jf) - 1, 0,
		       (struct sockaddr *)&to, sizeof(to));
		inet_pton(AF_INET, "239.255.255.250", &to.sin_addr);
		sendto(jelly, jf, sizeof(jf) - 1, 0,
		       (struct sockaddr *)&to, sizeof(to));
	}

	/* One window for both, because they are answered in parallel and a
	 * television is waiting. */
	struct timespec t0;
	clock_gettime(CLOCK_MONOTONIC, &t0);

	for (;;) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		long elapsed = (long)((now.tv_sec - t0.tv_sec) * 1000 +
				      (now.tv_nsec - t0.tv_nsec) / 1000000);
		int left = DISCOVER_MS - (int)elapsed;
		if (left <= 0)
			break;

		struct pollfd pfd[2];
		int np = 0;
		int pi = -1, ji = -1;
		if (plex >= 0) { pi = np; pfd[np].fd = plex; pfd[np].events = POLLIN; pfd[np].revents = 0; np++; }
		if (jelly >= 0) { ji = np; pfd[np].fd = jelly; pfd[np].events = POLLIN; pfd[np].revents = 0; np++; }
		if (!np)
			break;

		int r = poll(pfd, (nfds_t)np, left);
		if (r <= 0)
			break;

		char buf[2048];
		struct sockaddr_in from;
		socklen_t flen;

		if (pi >= 0 && (pfd[pi].revents & POLLIN)) {
			flen = sizeof(from);
			ssize_t got = recvfrom(plex, buf, sizeof(buf) - 1, 0,
					       (struct sockaddr *)&from, &flen);
			if (got > 0) {
				buf[got] = '\0';
				char name[128] = "Plex", port[16] = "32400";
				gdm_field(buf, "Name", name, sizeof(name));
				gdm_field(buf, "Port", port, sizeof(port));

				char ip[INET_ADDRSTRLEN] = "";
				inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
				if (ip[0]) {
					char url[256];
					snprintf(url, sizeof(url), "http://%s:%s/web",
						 ip, port);
					servers_add(out, &n, max, "plex", name, url);
				}
			}
		}

		if (ji >= 0 && (pfd[ji].revents & POLLIN)) {
			flen = sizeof(from);
			ssize_t got = recvfrom(jelly, buf, sizeof(buf) - 1, 0,
					       (struct sockaddr *)&from, &flen);
			if (got > 0) {
				buf[got] = '\0';
				char name[128] = "Jellyfin", addr[256] = "";
				json_str(buf, NULL, "Name", name, sizeof(name));
				if (json_str(buf, NULL, "Address", addr, sizeof(addr)) &&
				    url_ok(addr))
					servers_add(out, &n, max, "jellyfin",
						    name, addr);
			}
		}
	}

	if (plex >= 0) close(plex);
	if (jelly >= 0) close(jelly);

	/* …and this machine, which does not answer its own broadcast. */
	if (port_open("127.0.0.1", 32400, 200))
		servers_add(out, &n, max, "plex", "Plex (this machine)",
			    "http://127.0.0.1:32400/web");
	if (port_open("127.0.0.1", 8096, 200))
		servers_add(out, &n, max, "jellyfin", "Jellyfin (this machine)",
			    "http://127.0.0.1:8096");

	return n;
}

#define MEDIA_MAX 12
#define MEDIA_TTL 600		/* ten minutes: a server does not move often */

/*
 * The media servers, from the cache or from the network.
 *
 * Cached because this is on the path of drawing a screen. Nine hundred
 * milliseconds is nothing when somebody asks for it and everything when it
 * happens every time the television comes on; the shell asks for the cached
 * answer immediately and refreshes behind it.
 */
static int big_media(bool rec, bool refresh)
{
	char path[4096];
	if (!cache_path(path, sizeof(path), "media.tsv"))
		return EX_FAIL;

	long age = file_age(path);
	if (!refresh && age >= 0 && age < MEDIA_TTL) {
		if (rec) {
			if (cache_print(path))
				return EX_OK;
		} else {
			char *text = read_file(path);
			if (text) {
				/* The cache is records; print them as prose. */
				char *save = NULL;
				int row = 0;
				for (char *ln = strtok_r(text, "\n", &save); ln;
				     ln = strtok_r(NULL, "\n", &save)) {
					if (row++ == 0)
						continue;	/* the header */
					char *tab = strchr(ln, '\t');
					if (!tab) continue;
					*tab = '\0';
					char *name = pct_decode(tab + 1);
					char *tab2 = strchr(name, '\t');
					if (tab2) *tab2 = '\0';
					printf("%-12s %s\n", ln, name);
					free(name);
				}
				free(text);
				return EX_OK;
			}
		}
	}

	server_t found[MEDIA_MAX];
	int n = media_discover(found, MEDIA_MAX);

	/* Built into a string first, so the same text is both printed and
	 * cached and the two cannot drift. */
	char *buf = NULL;
	size_t len = 0;
	FILE *mem = open_memstream(&buf, &len);
	if (mem) {
		rec_frow(mem, 5, "id", "name", "url", "source", "kind");
		for (int i = 0; i < n; i++)
			rec_frow(mem, 5, found[i].id, found[i].name,
				 found[i].url, found[i].source, "server");
		fclose(mem);
	}

	if (buf) {
		/* A failed write is not a failed command: the answer is right
		 * here, it just will not be remembered. */
		mkdir_parents(path);
		write_file_inplace(path, buf);
	}

	if (rec) {
		if (buf)
			fputs(buf, stdout);
		else
			rec_row(5, "id", "name", "url", "source", "kind");
	} else if (n == 0) {
		puts("no Plex or Jellyfin server answered on this network");
	} else {
		for (int i = 0; i < n; i++)
			printf("%-12s %-28s %s\n", found[i].id, found[i].name,
			       found[i].url);
	}

	free(buf);
	return n ? EX_OK : EX_EMPTY;
}

/* The URL behind a media tile id, out of the cache. Used by `big run`, so
 * that pressing a tile goes through the same one place every other tile does
 * rather than the shell running a URL it was handed. */
static bool media_url(const char *id, char *out, size_t n)
{
	char path[4096];
	if (!cache_path(path, sizeof(path), "media.tsv"))
		return false;

	char *text = read_file(path);
	if (!text)
		return false;

	bool hit = false;
	char *save = NULL;
	int row = 0;
	for (char *ln = strtok_r(text, "\n", &save); ln && !hit;
	     ln = strtok_r(NULL, "\n", &save)) {
		if (row++ == 0)
			continue;
		char *f1 = strchr(ln, '\t');
		if (!f1) continue;
		*f1++ = '\0';
		if (strcmp(ln, id) != 0)
			continue;
		char *f2 = strchr(f1, '\t');
		if (!f2) continue;
		f2++;
		char *f3 = strchr(f2, '\t');
		if (f3) *f3 = '\0';
		char *url = pct_decode(f2);
		if (url_ok(url)) {
			snprintf(out, n, "%s", url);
			hit = true;
		}
		free(url);
	}

	free(text);
	return hit;
}

/* ── the news shelf ──────────────────────────────────────────────────────── */

/*
 * Headlines, on the shelf below the machine's own switches.
 *
 * ── Why RSS, and why curl ───────────────────────────────────────────────────
 *
 * RSS because it is the one thing every news source still emits that is not an
 * API key, a rate limit and a terms-of-service — and because the same feeds
 * are already what chibi reads on this system, so a machine's idea of "the
 * news" is one thing rather than two. curl because this binary does not link
 * an HTTP client and should not: TLS, redirects, proxies and a CA store are a
 * library's worth of decisions, all of which curl has already made and every
 * one of which would have to be maintained here.
 *
 * ⚠ Nothing here BLOCKS the television. The shell asks for the cache, which is
 * a file read, and the fetch happens on a refresh behind it. A launcher that
 * waits for the internet before it draws is a launcher that does not open when
 * the internet is down.
 */
#define NEWS_TTL   1200		/* twenty minutes */
#define NEWS_MAX   24
#define NEWS_PER_FEED 8

static const char *const default_feeds[] = {
	"https://news.google.com/rss/search?q=video+games&hl=en-US&gl=US&ceid=US:en",
	"https://news.google.com/rss?hl=en-US&gl=US&ceid=US:en",
	NULL
};

/* One `<tag>…</tag>` out of `item`, with CDATA unwrapped. Returns false if the
 * tag is absent, which for a feed in the wild is normal rather than an error. */
static bool xml_tag(const char *item, const char *tag, char *out, size_t n)
{
	char open[32], close[32];
	snprintf(open, sizeof(open), "<%s>", tag);
	snprintf(close, sizeof(close), "</%s>", tag);

	const char *a = strstr(item, open);
	if (!a)
		return false;
	a += strlen(open);
	const char *b = strstr(a, close);
	if (!b)
		return false;

	/* <![CDATA[ … ]]>, which is how half of these ship a title that has an
	 * ampersand in it. */
	if (strncmp(a, "<![CDATA[", 9) == 0) {
		a += 9;
		const char *end = strstr(a, "]]>");
		if (end && end < b)
			b = end;
	}

	size_t len = (size_t)(b - a);
	if (len >= n)
		len = n - 1;
	memcpy(out, a, len);
	out[len] = '\0';
	return true;
}

/*
 * XML entities, in place.
 *
 * Only the five that are defined in XML itself plus the numeric forms — a feed
 * that uses an HTML entity is a feed that is already wrong, and a table of 253
 * of them would be a table to maintain. `&amp;` is the one that actually
 * matters: it is in nearly every Google News link.
 */
static void xml_unescape(char *s)
{
	char *w = s;
	for (char *r = s; *r; ) {
		if (*r != '&') { *w++ = *r++; continue; }

		if (strncmp(r, "&amp;", 5) == 0)       { *w++ = '&';  r += 5; continue; }
		if (strncmp(r, "&lt;", 4) == 0)        { *w++ = '<';  r += 4; continue; }
		if (strncmp(r, "&gt;", 4) == 0)        { *w++ = '>';  r += 4; continue; }
		if (strncmp(r, "&quot;", 6) == 0)      { *w++ = '"';  r += 6; continue; }
		if (strncmp(r, "&apos;", 6) == 0)      { *w++ = '\''; r += 6; continue; }

		if (r[1] == '#') {
			int base = (r[2] == 'x' || r[2] == 'X') ? 16 : 10;
			char *end = NULL;
			long v = strtol(r + (base == 16 ? 3 : 2), &end, base);
			if (end && *end == ';' && v > 0 && v < 0x110000) {
				/* UTF-8 out, because everything downstream —
				 * the record encoding, the QML — is UTF-8. */
				if (v < 0x80) {
					*w++ = (char)v;
				} else if (v < 0x800) {
					*w++ = (char)(0xC0 | (v >> 6));
					*w++ = (char)(0x80 | (v & 0x3F));
				} else if (v < 0x10000) {
					*w++ = (char)(0xE0 | (v >> 12));
					*w++ = (char)(0x80 | ((v >> 6) & 0x3F));
					*w++ = (char)(0x80 | (v & 0x3F));
				} else {
					*w++ = (char)(0xF0 | (v >> 18));
					*w++ = (char)(0x80 | ((v >> 12) & 0x3F));
					*w++ = (char)(0x80 | ((v >> 6) & 0x3F));
					*w++ = (char)(0x80 | (v & 0x3F));
				}
				r = end + 1;
				continue;
			}
		}

		*w++ = *r++;
	}
	*w = '\0';
}

/* The feeds to read: one URL per line in the config, or the two defaults. */
static int news_feeds(char feeds[][512], int max)
{
	int n = 0;
	char path[4096];

	if (config_path(path, sizeof(path), "syn-arcade/news.conf")) {
		char *text = read_file(path);
		if (text) {
			char *save = NULL;
			for (char *ln = strtok_r(text, "\n", &save);
			     ln && n < max; ln = strtok_r(NULL, "\n", &save)) {
				char *t = trim(ln);
				if (!*t || *t == '#')
					continue;
				if (!url_ok(t)) {
					fprintf(stderr, "syn-arcade: %s: "
						"ignoring '%s' — not an http(s) "
						"URL\n", path, t);
					continue;
				}
				snprintf(feeds[n++], 512, "%s", t);
			}
			free(text);
			if (n)
				return n;
		}
	}

	for (int i = 0; default_feeds[i] && n < max; i++)
		snprintf(feeds[n++], 512, "%s", default_feeds[i]);
	return n;
}

typedef struct {
	char title[300];
	char source[96];
	char link[1024];
} news_t;

/*
 * Google News titles arrive as "Headline - Publisher", which is a fact about
 * that feed and not about RSS. Splitting it gives a shelf where the publisher
 * is a small line under a large headline instead of thirty tiles that all end
 * in a dash and a name.
 *
 * Only for that host, because in any other feed a trailing " - something" is
 * part of the headline and cutting it would be editing somebody's copy.
 */
static void news_split_source(const char *feed, news_t *it)
{
	if (!strstr(feed, "news.google.com"))
		return;

	char *dash = NULL;
	for (char *p = it->title; *p; p++)
		if (p[0] == ' ' && p[1] == '-' && p[2] == ' ')
			dash = p;
	if (!dash)
		return;

	const char *pub = dash + 3;
	if (!*pub || strlen(pub) > 48)
		return;			/* that dash was part of the headline */

	snprintf(it->source, sizeof(it->source), "%s", pub);
	*dash = '\0';
}

static int news_fetch(news_t *out, int max)
{
	char feeds[8][512];
	int nf = news_feeds(feeds, 8);
	int n = 0;

	for (int f = 0; f < nf && n < max; f++) {
		char *argv[] = {
			(char *)"curl", (char *)"-fsSL",
			(char *)"--max-time", (char *)"8",
			(char *)"-A", (char *)"syn-arcade/1.0",
			feeds[f], NULL
		};
		char *body = run_capture(argv, 12);
		if (!body)
			continue;

		const char *p = body;
		int taken = 0;
		while (taken < NEWS_PER_FEED && n < max) {
			const char *a = strstr(p, "<item");
			if (!a) break;
			a = strchr(a, '>');
			if (!a) break;
			a++;
			const char *b = strstr(a, "</item>");
			if (!b) break;

			size_t len = (size_t)(b - a);
			char *item = xmalloc(len + 1);
			memcpy(item, a, len);
			item[len] = '\0';

			news_t it;
			memset(&it, 0, sizeof(it));
			if (xml_tag(item, "title", it.title, sizeof(it.title)) &&
			    xml_tag(item, "link", it.link, sizeof(it.link))) {
				xml_unescape(it.title);
				xml_unescape(it.link);
				news_split_source(feeds[f], &it);
				if (url_ok(it.link) && it.title[0]) {
					out[n++] = it;
					taken++;
				}
			}

			free(item);
			p = b + 7;
		}
		free(body);
	}
	return n;
}

static int big_news(bool rec, bool refresh)
{
	char path[4096];
	if (!cache_path(path, sizeof(path), "news.tsv"))
		return EX_FAIL;

	long age = file_age(path);
	if (!refresh && age >= 0 && age < NEWS_TTL && rec) {
		if (cache_print(path))
			return EX_OK;
	}

	if (!net_allowed() && !rec) {
		fputs("syn-arcade: the network is turned off for this run "
		      "(SYN_ARCADE_NO_NET)\n", stderr);
		return EX_EMPTY;
	}

	news_t items[NEWS_MAX];
	int n = net_allowed() ? news_fetch(items, NEWS_MAX) : 0;

	/*
	 * ⚠ A fetch that came back with NOTHING must not overwrite the cache.
	 *
	 * The common reason for nothing is that the machine is not on the
	 * internet yet — a television switched on before the Wi-Fi came up —
	 * and replacing yesterday's headlines with an empty file turns a shelf
	 * that is merely stale into a shelf that is gone, until the next
	 * refresh happens to succeed. Old news is better than no news.
	 */
	if (n == 0) {
		if (rec && cache_print(path))
			return EX_OK;
		if (rec) {
			rec_row(5, "id", "title", "source", "link", "feed");
			return EX_EMPTY;
		}
		fputs("syn-arcade: no headlines — is this machine online?\n",
		      stderr);
		return EX_EMPTY;
	}

	char *buf = NULL;
	size_t len = 0;
	FILE *mem = open_memstream(&buf, &len);
	if (mem) {
		rec_frow(mem, 5, "id", "title", "source", "link", "feed");
		for (int i = 0; i < n; i++) {
			char id[32];
			snprintf(id, sizeof(id), "news-%d", i);
			rec_frow(mem, 5, id, items[i].title, items[i].source,
				 items[i].link, "news");
		}
		fclose(mem);
	}

	if (buf) {
		mkdir_parents(path);
		write_file_inplace(path, buf);
	}

	if (rec) {
		if (buf)
			fputs(buf, stdout);
	} else {
		for (int i = 0; i < n; i++)
			printf("%-56.56s  %s\n", items[i].title,
			       items[i].source[0] ? items[i].source : "");
		printf("\n%d headline%s\n", n, n == 1 ? "" : "s");
	}

	free(buf);
	return EX_OK;
}

/* ── typing, for the on-screen keyboard ──────────────────────────────────── */

/*
 * `syn-arcade big keys` — read what to type on stdin, one instruction a line,
 * and type it into whatever is focused.
 *
 * ── Why wtype and not another Wayland client ────────────────────────────────
 *
 * The pointer half of this feature (vptr.c) had to be written, because nothing
 * on the system could move a pointer. The keyboard half did not: wtype speaks
 * virtual-keyboard-v1, ships on every SynapseOS install and on the ISO, and
 * the desktop's own start menu already drives compositor keybinds through it.
 * A second implementation of "press a key" would be a second set of keymap
 * bugs for no gain.
 *
 * ── Why it is a stream and not one process per key ──────────────────────────
 *
 * The shell would otherwise have to start a process per keystroke from QML,
 * where the natural spelling — set `command`, set `running` — silently drops a
 * press that arrives while the last one is still going. Here the shell writes
 * a line, and this decides what that means. It also puts the argument checking
 * in C: a keysym name is matched against a strict set before it is passed on,
 * so nothing typed on a television becomes an option to another program.
 *
 * The instructions, one per line:
 *
 *   t <percent-encoded text>   type it literally
 *   k <keysym>                 press one key by XKB name — Return, BackSpace
 *   c <keysym>                 the same with ctrl held — c l is Ctrl+L
 */
static bool keysym_ok(const char *s)
{
	if (!s || !*s || strlen(s) > 32)
		return false;
	for (const char *p = s; *p; p++)
		if (!isalnum((unsigned char)*p) && *p != '_')
			return false;
	return true;
}

static int keys_stream(void)
{
	if (!have("wtype")) {
		fputs("syn-arcade: wtype is not installed — the on-screen "
		      "keyboard needs it to type\n", stderr);
		return EX_FAIL;
	}

	char line[4096];
	while (fgets(line, sizeof(line), stdin)) {
		strip_trailing_newline(line);
		if (!line[0])
			continue;

		char verb = line[0];
		char *arg = line[1] == ' ' ? line + 2 : NULL;
		if (!arg || !*arg)
			continue;

		char *argv[16];
		int argc = 0;
		char *text = NULL;

		if (verb == 't') {
			text = pct_decode(arg);
			argv[argc++] = (char *)"wtype";
			/* ⚠ The separator is not optional: somebody typing a
			 * hyphen into a search box would otherwise be handing
			 * wtype an option. */
			argv[argc++] = (char *)"--";
			argv[argc++] = text;
		} else if (verb == 'k' || verb == 'c') {
			if (!keysym_ok(arg))
				continue;
			argv[argc++] = (char *)"wtype";
			if (verb == 'c') {
				argv[argc++] = (char *)"-M";
				argv[argc++] = (char *)"ctrl";
			}
			argv[argc++] = (char *)"-k";
			argv[argc++] = arg;
			if (verb == 'c') {
				argv[argc++] = (char *)"-m";
				argv[argc++] = (char *)"ctrl";
			}
		} else {
			continue;
		}
		argv[argc] = NULL;

		/*
		 * Waited for, unlike everything else this file starts. Two
		 * wtype processes racing would deliver keys out of order — and
		 * on a keyboard, order is the entire content. It is a
		 * millisecond either way at the speed a thumb moves.
		 */
		pid_t pid = fork();
		if (pid == 0) {
			int null = open("/dev/null", O_RDWR);
			if (null >= 0) {
				dup2(null, STDIN_FILENO);
				dup2(null, STDOUT_FILENO);
				if (null > STDERR_FILENO)
					close(null);
			}
			execvp(argv[0], argv);
			_exit(127);
		}
		if (pid > 0) {
			int st = 0;
			waitpid(pid, &st, 0);
		}
		free(text);
	}
	return EX_OK;
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

	/*
	 * ⚠⚠ AND NOW EVERY GRANDCHILD INHERITS THE LOCK TOO — which is the same
	 * accident as the QS_APP_ID one below, one layer down. Clearing CLOEXEC
	 * hands the descriptor to quickshell, as intended; what was not intended
	 * is that quickshell hands it on to everything IT spawns — `big nav`,
	 * `big keys`, `big mouse`, `pads hold` — because an inherited fd is
	 * inherited all the way down unless somebody stops it.
	 *
	 * The lock is then held by the LONGEST-LIVED of that family, not by the
	 * shell. Observed on a live box: the shell was gone, an orphaned
	 * `big nav` still held fd 5 on this file 51 minutes later, and every
	 * `big start` answered "big screen mode is already running" while there
	 * was visibly no big screen anywhere. `big stop` could not help either —
	 * it kills the pid in the file, and that pid had already exited.
	 *
	 * ⚠ IT SURVIVES A LOGOUT, so this is not self-clearing: logind ships
	 * `KillUserProcesses=no`, so the orphan is still there at the next
	 * login and the feature is dead until somebody finds the process by
	 * hand. The name is exported rather than a fixed number because the fd
	 * is whatever open() returned, and the reader must never guess.
	 */
	char fdbuf[16];
	snprintf(fdbuf, sizeof(fdbuf), "%d", fd);
	setenv("SYN_BIG_LOCK_FD", fdbuf, 1);

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

/* ── talking to a big screen mode that is already running ────────────────── */

/*
 * A named pipe, and why there is now an IPC channel where there deliberately
 * was not one.
 *
 * The first version of this file argued that `toggle` should be stop-or-start,
 * because keeping the shell resident to hide it would cost a QML engine and a
 * library scan for the whole session and needed a channel between the
 * compositor's keybind and a process that might not exist.
 *
 * That argument is now wrong, and it is worth saying why rather than quietly
 * reversing it. Big screen mode has to STEP ASIDE for the browser, the
 * terminal and the controller window and then come BACK — that is the whole
 * point of a console interface, and quitting outright is exactly what made it
 * feel broken ("I opened the controller mapping and it just closed"). Coming
 * back means being alive while away, so the process is resident regardless;
 * once it is, a key that stops it rather than showing it is the wrong key.
 *
 * A FIFO rather than a socket because the whole protocol is one word, and
 * because the failure case has to be silent and immediate: a writer opening
 * O_WRONLY | O_NONBLOCK with nobody reading gets ENXIO instantly, which is
 * precisely the question being asked — "is there a shell listening?" — and
 * needs no timeout and no cleanup. $XDG_RUNTIME_DIR is a tmpfs logind wipes at
 * logout, so a stale one cannot outlive the session that made it.
 */
static bool big_ctl_path(char *buf, size_t n)
{
	const char *run = getenv("XDG_RUNTIME_DIR");
	if (run && *run)
		return snprintf(buf, n, "%s/syn-arcade-big.ctl", run) < (int)n;
	return snprintf(buf, n, "/tmp/syn-arcade-big-%u.ctl",
			(unsigned)getuid()) < (int)n;
}

/* Send one word to a running shell. False means nobody is listening — which is
 * not an error, it is how an older shell (or none) is detected. */
static bool big_ctl_send(const char *word)
{
	char path[4096];
	if (!big_ctl_path(path, sizeof(path)))
		return false;

	int fd = open(path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return false;		/* ENXIO: no reader. ENOENT: no fifo. */

	char line[64];
	int len = snprintf(line, sizeof(line), "%s\n", word);
	ssize_t w = write(fd, line, (size_t)len);
	close(fd);
	return w == len;
}

/*
 * The other end: print what arrives, one word a line, until the shell goes.
 *
 * ⚠ Opened O_RDWR, which looks wrong for a reader and is the only thing that
 * makes this work. A FIFO opened read-only reports end-of-file the moment the
 * last writer closes — and every writer here is a one-shot `big show` that
 * closes immediately — so the poll would come back readable forever with
 * nothing to read, spinning a core for the rest of the session. Holding a
 * write end ourselves means there is always a writer and EOF never arrives.
 */
static int big_listen(void)
{
	char path[4096];
	if (!big_ctl_path(path, sizeof(path)))
		return EX_FAIL;

	if (mkfifo(path, 0600) != 0 && errno != EEXIST) {
		fprintf(stderr, "syn-arcade: %s: %s\n", path, strerror(errno));
		return EX_FAIL;
	}

	int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "syn-arcade: %s: %s\n", path, strerror(errno));
		return EX_FAIL;
	}

	char buf[256];
	size_t used = 0;

	for (;;) {
		struct pollfd pfd[2];
		pfd[0].fd = fd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		/* The shell's pipe. When quickshell goes, so does this. */
		pfd[1].fd = STDOUT_FILENO;
		pfd[1].events = 0;
		pfd[1].revents = 0;

		int r = poll(pfd, 2, -1);
		if (r < 0 && errno != EINTR)
			break;
		if (pfd[1].revents & (POLLERR | POLLHUP | POLLNVAL))
			break;
		if (!(pfd[0].revents & POLLIN))
			continue;

		ssize_t got = read(fd, buf + used, sizeof(buf) - used - 1);
		if (got <= 0)
			continue;
		used += (size_t)got;
		buf[used] = '\0';

		char *start = buf, *nl;
		while ((nl = strchr(start, '\n'))) {
			*nl = '\0';
			if (*start) {
				puts(start);
				fflush(stdout);
			}
			start = nl + 1;
		}

		/* Keep any partial line, drop anything absurd. */
		used = strlen(start);
		if (used >= sizeof(buf) - 1)
			used = 0;
		else
			memmove(buf, start, used + 1);

		if (ferror(stdout))
			break;
	}

	close(fd);
	return EX_OK;
}

/*
 * Show, hide, or the key's own behaviour.
 *
 * `toggle` is what Super+F10 runs, and it now has three answers rather than
 * two:
 *
 *   nothing running          start it
 *   running and listening    tell it to show or hide itself
 *   running, no listener     stop it — an older shell, or one that has not got
 *                            as far as opening the channel yet. Falling back
 *                            to the old behaviour is better than a key that
 *                            silently does nothing.
 */
static int big_toggle(const char *output)
{
	if (!big_running(NULL))
		return big_start(output, false);
	if (big_ctl_send("toggle"))
		return EX_OK;
	return big_stop();
}

static int big_show_hide(const char *word, const char *output)
{
	if (!big_running(NULL)) {
		/* "Show it" with nothing running means start it. Anything else
		 * has nothing to act on. */
		if (strcmp(word, "show") == 0)
			return big_start(output, false);
		fputs("syn-arcade: big screen mode is not running\n", stderr);
		return EX_FAIL;
	}

	if (big_ctl_send(word))
		return EX_OK;

	fputs("syn-arcade: big screen mode is running but is not listening — "
	      "it predates this command\n", stderr);
	return EX_FAIL;
}

/* ── the guide button, from the desktop ──────────────────────────────────── */

/*
 * The reverse of the Guide tile.
 *
 * Guide inside big screen mode takes you to the desktop; this is what makes
 * Guide on the desktop bring big screen mode back, which is the half a console
 * has and a desktop does not. It is a session-long process because there is
 * nothing else to hang it on: the button is on a USB device, not on the
 * compositor, so somebody has to be holding the pads open and reading them
 * when none of our windows exist.
 *
 * ⚠ It starts big screen mode DETACHED. This process must not become
 * quickshell — it has to still be here for the next press.
 */
static bool guard_running(void)
{
	return big_running(NULL);
}

static void guard_press(void)
{
	big_start(NULL, true);
}

static int big_guard(void)
{
	if (!getenv("WAYLAND_DISPLAY")) {
		fputs("syn-arcade: no Wayland session — `big guard` opens big "
		      "screen mode and needs one\n", stderr);
		return EX_FAIL;
	}
	return pads_guide_watch(guard_running, guard_press);
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
	bool guide = binds_guard_get();

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
		rec_row(3, "guide button", guide ? "on" : "off",
			guide ? "action:guide-off" : "action:guide-on");
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
	printf("  guide button   %s\n", guide ? "on — opens this from the desktop"
	     : "off (`syn-arcade big guide on`)");
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
	if (!strcmp(sub, "show") || !strcmp(sub, "hide"))
		return big_show_hide(sub, opt_value(rest_c, rest, "--output"));
	if (!strcmp(sub, "listen"))
		return big_listen();
	if (!strcmp(sub, "guard"))
		return big_guard();

	if (!strcmp(sub, "games"))
		return big_games(rec, opt_present(rest_c, rest, "--all"));
	if (!strcmp(sub, "apps"))
		return big_apps(rec);
	if (!strcmp(sub, "run"))
		return big_run(first_operand(rest_c, rest),
			       opt_present(rest_c, rest, "--wait"));
	if (!strcmp(sub, "open"))
		return big_open(first_operand(rest_c, rest),
				opt_present(rest_c, rest, "--wait"));

	/* What is open, and the two things worth doing to one of them. The
	 * shell's Running shelf is these three verbs and nothing else. */
	if (!strcmp(sub, "windows"))
		return big_windows(rec);
	if (!strcmp(sub, "focus"))
		return big_window_act(first_operand(rest_c, rest), false);
	if (!strcmp(sub, "close"))
		return big_window_act(first_operand(rest_c, rest), true);

	if (!strcmp(sub, "news"))
		return big_news(rec, opt_present(rest_c, rest, "--refresh"));
	if (!strcmp(sub, "media"))
		return big_media(rec, opt_present(rest_c, rest, "--refresh"));

	/* The on-screen keyboard's typist, and the controller-as-mouse. Both
	 * are streams the shell owns for as long as it needs them; neither is
	 * something to run by hand, and both stop when their pipe goes. */
	if (!strcmp(sub, "keys"))
		return keys_stream();
	if (!strcmp(sub, "mouse")) {
		const char *out = opt_value(rest_c, rest, "--output");
		if (!out)
			out = getenv("SYN_BIG_OUTPUT");
		return pads_mouse_stream(out);
	}

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

	/* `big guide on|off` — the same setting `binds guide` writes, named
	 * here too because it is a big screen mode feature and this is where
	 * somebody will look for it. */
	if (!strcmp(sub, "guide")) {
		const char *arg = first_operand(rest_c, rest);
		if (!arg || !strcmp(arg, "status")) {
			bool on = binds_guard_get();
			if (rec) {
				rec_row(3, "field", "value", "action");
				rec_row(3, "guide button", on ? "on" : "off",
					on ? "action:guide-off" : "action:guide-on");
			} else {
				puts(on ? "on" : "off");
			}
			return EX_OK;
		}
		if (!strcmp(arg, "on") || !strcmp(arg, "off"))
			return binds_guard_set(strcmp(arg, "on") == 0);

		fprintf(stderr, "syn-arcade: big guide takes on, off or status "
				"(not '%s')\n", arg);
		return EX_USAGE;
	}

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
