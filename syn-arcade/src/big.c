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
#include <ifaddrs.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>		/* strcasecmp, for the audio extensions */
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

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

/* Declared in arcade.h: fit.c launches games through this too. */
int spawn_detached(char *const argv[])
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
	/* ⚠ `[` IS SKIPPED, and it is for MPRIS. Half of what a media player
	 * publishes about a track is typed `as` — `xesam:artist` is "a list
	 * with one entry" in the specification, and every player really does
	 * send a one-element array. Skipping the bracket reads the first
	 * string of a list with the same call that reads a plain one; nothing
	 * else here passes a key whose value is an array. */
	while (*p == ' ' || *p == ':' || *p == '[') p++;
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

/*
 * One number out of a JSON object, or `def` when the key is not there.
 *
 * ⚠ ABSENT IS NOT ZERO EVERYWHERE, which is why the default is the caller's to
 * choose. cliamp omits `"total"` entirely when its queue is empty rather than
 * printing 0 — measured — so the one caller here asks for 0 and gets the same
 * answer either way, but nothing about that is general.
 */
static long json_int(const char *text, const char *end, const char *key,
		     long def)
{
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = strstr(text, pat);
	if (!p || (end && p >= end))
		return def;
	p += strlen(pat);
	while (*p == ' ' || *p == ':') p++;
	if (*p != '-' && (*p < '0' || *p > '9'))
		return def;
	return strtol(p, NULL, 10);
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

/*
 * One key out of ~/.config/syn-arcade/big.conf. `key = value`, `#` comments.
 *
 * Deliberately not a library: the file has a handful of keys, all of them
 * "which one of these do you want", and the whole reason it exists is the case
 * a command-line flag cannot reach — `big autostart` takes no arguments, so a
 * preference that can only be spelled as a flag is no preference at all.
 *
 * ⚠ THE LAST ASSIGNMENT WINS, which matches synui's own config and is what
 * somebody who edited a line twice rather than deleting the first one expects.
 */
static bool big_conf_get(const char *key, char *buf, size_t n)
{
	buf[0] = '\0';

	char path[SYN_PATH];
	if (!config_path(path, sizeof(path), "syn-arcade/big.conf"))
		return false;
	char *text = read_file(path);
	if (!text)
		return false;

	char *save = NULL;
	for (char *ln = strtok_r(text, "\n", &save); ln;
	     ln = strtok_r(NULL, "\n", &save)) {
		char *t = trim(ln);
		if (!*t || *t == '#')
			continue;
		char *eq = strchr(t, '=');
		if (!eq)
			continue;
		*eq = '\0';
		char *k = trim(t);
		char *v = trim(eq + 1);
		if (strcmp(k, key) == 0 && *v)
			snprintf(buf, n, "%s", v);
	}

	free(text);
	return buf[0] != '\0';
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
 * Greenlight — Xbox cloud gaming and console remote play — as an argv, or NULL.
 *
 * ⚠ THE ONLY TILE HERE THAT MAY NOT BE ON PATH, and that is the whole reason
 * this is a function rather than one more have() beside Moonlight. Greenlight
 * ships as a Flatpak (io.github.unknownskl.greenlight, on Flathub), and a
 * Flatpak is invisible to have(), which is `command -v` and nothing else. As a
 * plain have("greenlight") the tile would simply never have appeared — no
 * warning, no empty tile, nothing on the shelf at all — on precisely the
 * install this package recommends. There is an AUR build that would have put
 * the name on PATH and made this unnecessary; it is a five-vote package, and a
 * television four metres away is the wrong place to discover one went stale.
 *
 * PATH first all the same: a native binary is somebody's deliberate choice, it
 * costs one fork to find, and `flatpak info` is a far heavier question than
 * `command -v` to ask on the way to drawing a shelf. Cached for the reason
 * first_installed() is — apps_table() is walked twice per invocation, once to
 * decide the tile exists and once for its command.
 */
static const char *greenlight_prog(void)
{
	static const char *cache;
	if (cache)
		return *cache ? cache : NULL;

	if (have("greenlight")) {
		cache = "greenlight";
		return cache;
	}

	/* ⚠ `flatpak info <id>`, NOT `flatpak list | grep`. list prints the
	 * whole installation formatted for a person to read, and a grep over
	 * it matches a remote name, a branch or another app's description as
	 * readily as the app itself. info takes the ID and answers with its
	 * exit status, which is the question actually being asked. */
	if (system("flatpak info io.github.unknownskl.greenlight "
		   ">/dev/null 2>&1") == 0) {
		cache = "flatpak run io.github.unknownskl.greenlight";
		return cache;
	}

	cache = "";
	return NULL;
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
 * A music player: `music = <program>` in big.conf, or the first one installed.
 *
 * ⚠ THE CONFIG KEY EXISTS BECAUSE THE LIST CANNOT BE RIGHT FOR EVERYBODY.
 * Ordering a dozen players by what most people would want is a guess, and it
 * silently overrules somebody who installed two. Naming one ends the argument.
 *
 * cliamp is first among the detected ones, and it is not favouritism: it is the
 * only player here that big screen mode can DRIVE — it runs headless and takes
 * transport commands over a socket, so Music on a television becomes a tile
 * that starts the music and a row in the menu that controls it, rather than a
 * window somebody then has to get out of with a gamepad. See music_headless().
 *
 * Dedicated music applications next — they have a library, cover art and a
 * queue, which is what "Music" means on a television — then the general media
 * players, which will play an album and are better than an empty shelf. mpv is
 * deliberately absent: with no file to open it is a black window, which is a
 * tile that looks broken.
 */
static const char *music_prog(void)
{
	static char chosen[128];
	static bool decided;

	if (decided)
		return chosen[0] ? chosen : NULL;
	decided = true;

	char want[128];
	if (big_conf_get("music", want, sizeof(want))) {
		if (have(want)) {
			snprintf(chosen, sizeof(chosen), "%s", want);
			return chosen;
		}
		/* ⚠ SAID OUT LOUD, and it falls back anyway. A named player
		 * that is not installed is a typo or an uninstall, and a tile
		 * that quietly opens a DIFFERENT program from the one in the
		 * config file is the kind of thing nobody thinks to check —
		 * they check the config, see the right name, and look
		 * elsewhere. stderr, so `--rec` output is untouched. */
		fprintf(stderr, "syn-arcade: big.conf says music = %s, which is "
				"not installed — using what is\n", want);
	}

	static const char *const cands[] = {
		"cliamp",
		"strawberry", "elisa", "amberol", "rhythmbox", "lollypop",
		"clementine", "audacious", "deadbeef", "quodlibet", "tauon",
		"plexamp", "spotify", "vlc", NULL
	};
	static const char *cache;
	const char *found = first_installed(cands, &cache);
	if (found)
		snprintf(chosen, sizeof(chosen), "%s", found);
	return chosen[0] ? chosen : NULL;
}

/*
 * projectM: the visualizer, as a program rather than as ten rectangles.
 *
 * The Start menu already draws cliamp's own bands behind the Now Playing row,
 * and that is a meter — it says the music is playing. This is the other thing
 * a visualizer is for: a television with nothing to show while an album plays.
 * projectM is the Milkdrop engine on Linux and ships four thousand presets, so
 * "Visualizer" is a tile that fills the screen and needs no configuring.
 *
 * ⚠ THE PULSEAUDIO BUILD FIRST, and not as a matter of taste. projectMSDL
 * opens an SDL capture device by name from its own enumeration, and the first
 * one on that list is usually a microphone: a visualizer that dances to the
 * room and sits still through the music.
 *
 * ⚠ AND IT DOES NOT READ PULSE_SOURCE — a claim that used to be in this
 * comment, and it was wrong, and being wrong cost a working desktop's audio.
 * projectM-pulseaudio calls pa_context_get_source_info_list(), picks a device
 * itself, remembers the choice in ~/.config/projectM/qprojectM-pulseaudio.conf
 * as `pulseAudioDeviceName=`, and connects to THAT by name. libpulse only
 * substitutes PULSE_SOURCE for a stream that connects with a NULL device, so
 * the variable was set on every launch and read by nothing. See
 * big_visualizer() for what it did.
 *
 * ⚠ NOT a dependency of this package. It is a 30MB Qt5 program with a preset
 * library behind it, and a machine with no interest in it should not carry
 * one; the tile appears when it is installed, like every other tile here.
 */
static const char *visualizer_prog(void)
{
	static const char *const cands[] = {
		"projectM-pulseaudio", "projectMSDL", NULL
	};
	static const char *cache;
	return first_installed(cands, &cache);
}

/*
 * Can the chosen player be driven WITHOUT a window?
 *
 * Exactly one can, so this is a name check and says so rather than pretending
 * to be a capability test. The property being asked about is real, though, and
 * it is what makes the Music tile a different KIND of thing: cliamp runs
 * headless (`--daemon`, its own word for it) and takes play/pause/next over a
 * unix socket, so nothing has to be mapped, focused, or escaped from.
 *
 * ⚠ Compared on the BASENAME. `music = /home/velle/.local/bin/cliamp` is a
 * perfectly reasonable thing to write in a config file, and it is the same
 * program.
 */
static bool music_headless(void)
{
	const char *m = music_prog();
	if (!m)
		return false;
	const char *base = strrchr(m, '/');
	return strcmp(base ? base + 1 : m, "cliamp") == 0;
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
 *   transient  whether this tile's application should be ENDED when big screen
 *            mode comes back, rather than left running behind it.
 *
 *            ⚠ FALSE FOR ALMOST EVERYTHING, and that is the point: a game, a
 *            browser or a film is exactly what Guide is meant to step away
 *            from and come back to. This is for the one kind of tile that has
 *            nothing to be away from — the visualizer draws the music that is
 *            playing anyway, so hidden behind the interface it is a window
 *            with no viewer.
 *
 *            It is not merely wasteful. A surface fully covered by an opaque
 *            one is occlusion-culled by the compositor and gets NO FRAME
 *            CALLBACKS, and projectM does not idle without them: measured on a
 *            headless synui, it free-runs at 100% of a core while covered, and
 *            comes back frozen often enough that the way out was to resize the
 *            window twice with a mouse. Reported from the sofa as "open the
 *            visualizer, hit Guide, go back and it is frozen".
 */
struct row {
	const char *id, *name, *exec, *icon, *kind, *shelf;
	bool pointer, keys, full, transient;
};

/*
 * The drawn glyph for an icon name, as a path the shell can open.
 *
 * ⚠ RESOLVED HERE, not in the QML, for the same reason cover art is: the shell
 * is a renderer. It is handed a path that exists or an empty string, and it
 * never has to know where this package installed itself, that there is a
 * source tree, or that the drawings are SVG at all. Adding a tile in
 * apps_table() and dropping <icon>.svg into data/icons is then the whole
 * change — there is no second list anywhere to fall out of step with this one.
 *
 * ⚠ ABSOLUTE, and the relative form is not good enough. The shell turns this
 * into a file:// URL, and quickshell's working directory is wherever the
 * person who started it happened to be — which for the desktop launcher is
 * `/`. A path that resolves perfectly from the source tree in a terminal
 * silently draws nothing on a television.
 *
 * ⚠ ONE STATIC BUFFER, so exactly one of these may be live at a time. That is
 * true of every caller below — one per record row — and it is why this returns
 * a const char * rather than filling something the caller owns.
 */
static const char *icon_file(const char *name)
{
	static char path[SYN_PATH];

	if (!name || !name[0])
		return "";

	snprintf(path, sizeof(path), "%s/icons/%s.svg", SYNARCADE_DATADIR, name);
	if (access(path, R_OK) == 0)
		return path;

	/* the source tree, made absolute against the working directory rather
	 * than through realpath(): realpath wants a PATH_MAX buffer and this
	 * one is a quarter of that. */
	char rel[SYN_PATH], cwd[SYN_PATH];
	snprintf(rel, sizeof(rel), "data/icons/%s.svg", name);
	if (access(rel, R_OK) == 0 && getcwd(cwd, sizeof(cwd))) {
		/* ⚠ The RETURN VALUE, checked. Two buffers this size cannot
		 * both fit in one of them, and a silent truncation here is a
		 * path that names some other file — so a joined path that did
		 * not fit is no path at all. */
		int len = snprintf(path, sizeof(path), "%s/%s", cwd, rel);
		if (len > 0 && (size_t)len < sizeof(path))
			return path;
	}
	return "";
}

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
			false, false, false, false };
	if (have("retroarch"))		/* --fullscreen, up in the exec */
		rows[n++] = (struct row){ "retroarch", "RetroArch",
			"retroarch --fullscreen", "retroarch", "app", "play",
			false, false, false, false };
	if (have("lutris"))
		rows[n++] = (struct row){ "lutris", "Lutris", "lutris",
			"lutris", "app", "play", true, false, true, false };
	if (have("heroic"))
		rows[n++] = (struct row){ "heroic", "Heroic", "heroic",
			"heroic", "app", "play", true, false, true, false };
	if (have("moonlight"))
		rows[n++] = (struct row){ "moonlight", "Moonlight", "moonlight",
			"moonlight", "app", "play", true, false, true, false };
	/* ⚠ THE EXEC MAY BE THREE WORDS — `flatpak run <id>` — which big_run's
	 * space split already handles and which nothing else in this table
	 * needed. And the id stays `greenlight` rather than the Flatpak's:
	 * tile_for() matches a window's app-id against the tile id and against
	 * the LAST dot-component of that app-id, so a window reporting
	 * io.github.unknownskl.greenlight finds this row on the Running shelf
	 * with no second spelling anywhere to fall out of step. Matched in the
	 * FIRST pass, too — the second pass looks at the first word of the
	 * exec, which for a Flatpak is `flatpak` and is evidence of nothing. */
	if (greenlight_prog())
		rows[n++] = (struct row){ "greenlight", "Greenlight",
			greenlight_prog(), "greenlight", "app", "play",
			true, false, true, false };

	/* ── media ── */
	{
		const char *music = music_prog();
		/* ⚠ AN ACTION, NOT AN APP, when the player is headless — and
		 * that is the whole difference in how it behaves. An app tile
		 * launches something, steps the television aside, and waits for
		 * it to exit. cliamp opens no window, so stepping aside would
		 * reveal the desktop and waiting would wait for ever. As an
		 * action it starts the music and the interface stays where it
		 * is, which is what a music button on a television should do. */
		if (music && music_headless())
			rows[n++] = (struct row){ "music", "Music",
				"syn-arcade big music play", "music", "action",
				"media", false, false, false, false };
		else if (music)
			rows[n++] = (struct row){ "music", "Music", music,
				"music", "app", "media", true, false, true, false };
	}
	if (have("kodi"))		/* opens full-screen by itself */
		rows[n++] = (struct row){ "kodi", "Kodi", "kodi", "kodi", "app",
			"media", false, false, false, false };
	if (have("plex-desktop"))
		rows[n++] = (struct row){ "plex", "Plex", "plex-desktop",
			"plex", "app", "media", true, false, true, false };
	else if (have("plexhtpc"))	/* the HTPC build: already full-screen */
		rows[n++] = (struct row){ "plex", "Plex", "plexhtpc",
			"plex", "app", "media", true, false, false, false };
	if (have("jellyfinmediaplayer"))
		rows[n++] = (struct row){ "jellyfin", "Jellyfin",
			"jellyfinmediaplayer", "jellyfin", "app", "media",
			true, false, true, false };
	else if (have("jellyfin-media-player"))
		rows[n++] = (struct row){ "jellyfin", "Jellyfin",
			"jellyfin-media-player", "jellyfin", "app", "media",
			true, false, true, false };

	/* ── apps: the two that need a pointer and a keyboard ── */
	if (browser_prog())
		rows[n++] = (struct row){ "web", "Web", browser_prog(),
			"firefox", "app", "apps", true, true, true, false };
	if (terminal_prog())
		rows[n++] = (struct row){ "terminal", "Terminal",
			terminal_prog(), "terminal", "app", "apps",
			true, true, true, false };
	if (have("syn-arcade"))
		rows[n++] = (struct row){ "arcade", "Controllers",
			"syn-arcade gui", "syn-arcade", "app", "apps",
			true, false, true, false };

	/* ── the Start menu's own row ── */
	/* ⚠ `shelf = system` means BEHIND START, not on a shelf: since
	 * 0.1.0-16 the QML draws the system rows in the Start menu and nowhere
	 * else. The visualizer belongs there rather than among the media tiles
	 * because it is not something to browse to — it is what you turn on
	 * while something else is already playing. It needs help filling the
	 * screen: projectM opens a 512x512 window by default, which on a
	 * television is a stamp in the middle of a wall. */
	if (visualizer_prog())
		rows[n++] = (struct row){ "visualizer", "Visualizer",
			"syn-arcade big visualizer", "visualizer", "app",
			"system", false, false, true, true };

	/* The way OUT is a tile, and it is not optional. A full-screen surface
	 * with exclusive keyboard focus that can only be dismissed by a key
	 * combination somebody has to already know is a trap, and on a gamepad
	 * there is no key combination at all.
	 *
	 * ⚠ TWO WAYS OUT, AND THEY DIFFER IN WHAT IS LEFT BEHIND. Both reveal
	 * the desktop and from four metres they look identical, so the split is
	 * worth stating: Desktop gets out of the way and STAYS LOADED, which is
	 * what makes Guide come straight back; Quit ends the process.
	 *
	 * Desktop used to be the one that quit, which left no way to leave big
	 * screen mode running-but-away on purpose and no way to close it on
	 * purpose either — Super+F10 only ever hides it, so the usual way out
	 * left it resident for the rest of the session with nothing in the dock
	 * to close (a layer-shell surface is not a window and never appears in
	 * one). Reported from the sofa as "it runs in the background but is not
	 * a program I can close". */
	rows[n++] = (struct row){ "desktop", "Desktop", "", "desktop", "action",
		"system", false, false, false, false };
	rows[n++] = (struct row){ "quit", "Quit", "", "quit", "action",
		"system", false, false, false, false };
	rows[n++] = (struct row){ "sleep", "Sleep", "systemctl suspend",
		"sleep", "action", "system", false, false, false, false };
	rows[n++] = (struct row){ "restart", "Restart", "systemctl reboot",
		"restart", "action", "system", false, false, false, false };
	rows[n++] = (struct row){ "poweroff", "Power off", "systemctl poweroff",
		"poweroff", "action", "system", false, false, false, false };

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
		 * `cut -f`. A new column belongs on the end. `iconfile` came
		 * later and went on the end after it, for the same reason.
		 *
		 * ⚠ `icon` STAYS, and iconfile is a second column rather than
		 * a replacement. The name is the identity — it is what the
		 * plain-text output prints, what a drawing is filed under, and
		 * what anything else reading this table would match on. The
		 * path is a fact about this installation that is empty on a
		 * machine where the drawing is missing; overwriting the name
		 * with it would lose the identity in exactly the case where
		 * somebody needs to know which glyph failed to ship. */
		rec_row(11, "id", "name", "exec", "icon", "kind", "shelf",
			"pointer", "keys", "full", "iconfile", "transient");
		for (int i = 0; i < n; i++)
			rec_row(11, rows[i].id, rows[i].name, rows[i].exec,
				rows[i].icon, rows[i].kind, rows[i].shelf,
				rows[i].pointer ? "1" : "0",
				rows[i].keys ? "1" : "0",
				rows[i].full ? "1" : "0",
				icon_file(rows[i].icon),
				rows[i].transient ? "1" : "0");
	} else {
		for (int i = 0; i < n; i++)
			printf("%-10s %-8s %-20s %s%s%s%s\n", rows[i].id,
			       rows[i].shelf, rows[i].name,
			       rows[i].exec[0] ? rows[i].exec : "(built in)",
			       rows[i].pointer ? "   [mouse]" : "",
			       rows[i].full ? "   [fullscreen]" : "",
			       rows[i].transient ? "   [ends on return]" : "");
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
 * ── ending what this started, from the outside ──────────────────────────────
 *
 * The pid of the application this process is waiting on, so that a signal
 * arriving HERE can be passed on to IT.
 *
 * ⚠ KILLING THIS PROCESS DOES NOT KILL THE APPLICATION, and that is the whole
 * reason this exists. spawn_detached_pid gives the child its own session, so
 * `syn-arcade big run <id> --wait` is the only thing a SIGTERM from the shell
 * reaches; the program it started is orphaned and carries on drawing behind
 * the television. The shell has no other handle on it — a layer-shell surface
 * cannot close a window, and there is no pid anywhere in QML.
 *
 * So the waiter forwards. The kill is to `-pid`, the whole process GROUP,
 * because setsid() made the child a group leader and anything it started in
 * turn (a wrapper script's real program) belongs to that group and would
 * otherwise be left behind — which is the same "it is still running" this is
 * here to fix, one level down.
 *
 * ⚠ Async-signal-safe, and only just: kill(), alarm() and a sig_atomic_t
 * assignment are on the list, and nothing else happens in the handler. The reap
 * continues afterwards through the EINTR loop below, so this still returns when
 * the application really has gone rather than the moment it was asked to.
 *
 * ── ⚠ AND ASKING IS NOT ENOUGH, WHICH IS WHY THERE IS A SECOND HALF ─────────
 *
 * SIGTERM's default action ends a process. A program that CATCHES it does not
 * have to, and a program that catches it and then waits for its event loop
 * cannot, if that loop is the thing that is stuck.
 *
 * That is exactly the visualizer. projectM-pulseaudio imports `signal` and
 * pa_signal_new, so SIGTERM is delivered into pulse's mainloop rather than to
 * the kernel's default action — measured on this machine:
 * `SigCgt` has bit 15 set, and a SIGTERM to a HEALTHY offscreen projectM left
 * it still running five seconds later. Covered by the interface it gets no
 * frame callbacks, so its loop is not turning at all and there is nothing to
 * deliver the quit TO. The journal caught the whole shape of it: Guide at
 * 16:08:16, the interface back, and projectM only printing TERMINATED at
 * 16:08:24 — eight seconds later, and only after the television stepped aside
 * again and handed it frames back. From the sofa that is the bug 0.1.0-26 was
 * supposed to have fixed: come back, and the frozen visualizer is still there.
 *
 * So the polite signal is asked FIRST and enforced SECOND. `alarm()` is armed
 * as the TERM goes out, and SIGALRM sends the group a SIGKILL, which no
 * program catches and no stuck event loop can sit on.
 *
 * ⚠ ONLY FOR THE TILES THAT ASKED FOR IT. `insist` comes from the SAME
 * `transient` column that decides what gets signalled at all — it is not a new
 * list, and it is not everything. Somebody pressing Ctrl+C at a prompt on
 * `big run steam-bpm --wait` means "stop waiting", and answering that by
 * SIGKILLing Steam two seconds later would lose whatever it had not written
 * yet. A transient tile has nothing to lose by construction: it is ended and
 * launched afresh every time.
 */
#define INSIST_AFTER 2		/* seconds of grace before the SIGKILL */

static volatile sig_atomic_t waited_pid;
static volatile sig_atomic_t insist_on_it;

static void pass_on_the_signal(int sig)
{
	if (waited_pid <= 0)
		return;
	kill(-(pid_t)waited_pid, sig);
	if (insist_on_it)
		alarm(INSIST_AFTER);
}

static void insist(int sig)
{
	(void)sig;
	if (waited_pid > 0)
		kill(-(pid_t)waited_pid, SIGKILL);
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
static int spawn_wait(char *const argv[], bool fill, bool hard)
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

	/* Armed only once there is something to pass a signal on TO. Both
	 * signals, because a terminal sends INT and the shell sends TERM, and
	 * an interface that can only end what it started from one of them is
	 * an interface that cannot end it from the sofa.
	 *
	 * ⚠ sigaction with no SA_RESTART, deliberately: the waitpid below has
	 * to be interrupted for the handler to run at all, and the EINTR loop
	 * is already written for it. */
	waited_pid = pid;
	insist_on_it = hard;
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = pass_on_the_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	/* ⚠ AND SIGALRM HAS TO BE HANDLED, not merely armed. Its default action
	 * ends the process — so without this the enforcement would kill the
	 * WAITER two seconds after the polite signal and leave the application
	 * it was supposed to be enforcing against running, which is the bug
	 * with an extra step. Installed unconditionally: it costs nothing when
	 * nothing arms the alarm, and it means the arming half in the handler
	 * above never has to ask whether it is safe to arm. */
	sa.sa_handler = insist;
	sigaction(SIGALRM, &sa, NULL);

	if (fill)
		fullscreen_after_launch(&before);

	/* ⚠ EINTR is not "it finished". A signal arriving while this blocks
	 * would otherwise be read as the application closing, and big screen
	 * mode would come back over the top of something still in use. */
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
		;

	/* Disarmed in the order that cannot misfire: the alarm first, so a
	 * pending one cannot land on a pid this no longer owns, and only then
	 * the pid it would have used. */
	alarm(0);
	insist_on_it = 0;
	waited_pid = 0;
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

		/* The `transient` column answers BOTH halves: what gets
		 * signalled on the way back, and what may be insisted upon if
		 * asking is ignored. There is no second list. */
		return spawn_wait(argv, rows[i].full, rows[i].transient);
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
	return spawn_wait(argv, true, false);
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

/* The host out of "http://192.168.40.153:32400/web". */
static bool url_host(const char *url, char *out, size_t n)
{
	if (!url || !out || n == 0)
		return false;

	const char *p = strstr(url, "://");
	p = p ? p + 3 : url;

	size_t w = 0;
	while (*p && *p != ':' && *p != '/' && w + 1 < n)
		out[w++] = *p++;
	out[w] = '\0';
	return w > 0;
}

/*
 * Is this address one of THIS machine's own?
 *
 * ⚠ THE SERVER THAT ANSWERS ITS OWN BROADCAST IS WHY THIS EXISTS. The localhost
 * probe below is there because a server does not RELIABLY answer itself — but
 * plenty of them do, and then the same server has described itself twice under
 * two different addresses: once as 192.168.40.153 in the GDM reply and once as
 * 127.0.0.1 from the probe. servers_add() deduplicates on the URL, and those
 * are two different strings, so the Media shelf showed Plex twice — once under
 * the server's own name and once as "Plex (this machine)".
 *
 * From four metres away that does not read as a bug. It reads as two servers,
 * and pressing the wrong one is indistinguishable from pressing the right one,
 * which is why it survived a release: everything WORKED, there was just one
 * tile too many.
 *
 * It covers a second duplicate that has nothing to do with loopback: a machine
 * with Wi-Fi and Ethernet on one subnet gets a reply per interface, each naming
 * the address it arrived on.
 */
static bool addr_is_local(const char *ip)
{
	if (!ip || !*ip)
		return false;
	if (strncmp(ip, "127.", 4) == 0)
		return true;

	struct ifaddrs *list = NULL;
	if (getifaddrs(&list) != 0)
		return false;		/* cannot tell: keep both, do not lose one */

	bool local = false;
	for (struct ifaddrs *p = list; p && !local; p = p->ifa_next) {
		if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
			continue;

		const struct sockaddr_in *sin =
			(const struct sockaddr_in *)(const void *)p->ifa_addr;
		char mine[INET_ADDRSTRLEN] = "";
		if (inet_ntop(AF_INET, &sin->sin_addr, mine, sizeof(mine)) &&
		    strcmp(mine, ip) == 0)
			local = true;
	}

	freeifaddrs(list);
	return local;
}

/* Has a server of this kind already been found ON THIS MACHINE, under whatever
 * address it chose to announce itself with? */
static bool have_local_server(const server_t *out, int n, const char *source)
{
	for (int i = 0; i < n; i++) {
		if (strcmp(out[i].source, source) != 0)
			continue;

		char host[128];
		if (url_host(out[i].url, host, sizeof(host)) &&
		    addr_is_local(host))
			return true;
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

	/* …and this machine, which does not RELIABLY answer its own broadcast.
	 *
	 * ⚠ Skipped when it already has. See addr_is_local: a server that both
	 * replies and is listening on loopback is one server, and adding it
	 * again under a second address put Plex on the shelf twice. The reply
	 * is preferred over the probe because the reply carries the server's
	 * OWN name — "synapse" rather than "Plex (this machine)". */
	if (!have_local_server(out, n, "plex") &&
	    port_open("127.0.0.1", 32400, 200))
		servers_add(out, &n, max, "plex", "Plex (this machine)",
			    "http://127.0.0.1:32400/web");
	if (!have_local_server(out, n, "jellyfin") &&
	    port_open("127.0.0.1", 8096, 200))
		servers_add(out, &n, max, "jellyfin", "Jellyfin (this machine)",
			    "http://127.0.0.1:8096");

	return n;
}

#define MEDIA_MAX 12
#define MEDIA_TTL 600		/* ten minutes: a server does not move often */

/*
 * Whether a cached media.tsv was written by a build that knew about the
 * `pointer` column.
 *
 * ⚠ THIS IS NOT VERSIONING THE CACHE, and it is deliberately not a schema
 * number. The rule for this file is that columns go on the END and are read by
 * NAME, precisely so an older cache stays readable — and it does. The problem
 * is narrower: a cache written before `pointer` existed is READABLE but WRONG
 * in a way nobody can see, because the missing column reads as `undefined` and
 * a server tile then launches with no mouse. Age cannot tell the two apart, so
 * the header is asked directly.
 *
 * Without this the fix would arrive and appear not to work for up to
 * MEDIA_TTL — the update lands, the television comes on, and the FIRST press
 * of Plex is still served from the file the old build wrote. Ten minutes of
 * looking exactly like the bug that was just fixed is worth these few lines.
 *
 * A cache that cannot be read at all says false and is simply re-discovered.
 */
static bool cache_has_pointer(const char *path)
{
	char *text = read_file(path);
	if (!text)
		return false;

	/* The header only: a URL or a server's name could contain anything,
	 * and this question is about the columns. */
	char *nl = strchr(text, '\n');
	if (nl)
		*nl = '\0';
	bool ok = strstr(text, "\tpointer") != NULL;
	free(text);
	return ok;
}

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
	if (!refresh && age >= 0 && age < MEDIA_TTL && cache_has_pointer(path)) {
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
		/* ⚠ `iconfile` on the END, which is also what keeps a cache
		 * written by an older build readable: the shell reads columns
		 * by NAME, so a five-column media.tsv from yesterday simply
		 * has no glyph until the next refresh rewrites it, rather than
		 * shifting every field one to the left.
		 *
		 * The glyph comes off `source` — plex, jellyfin — which is the
		 * same name the installed application's tile files its drawing
		 * under, so a Plex server on the network and Plex on this
		 * machine look like the same thing on the shelf. They are.
		 *
		 * ⚠ `pointer` AND `keys`, ALWAYS 1, and their absence was a bug.
		 * These are the same two columns apps_table() gives every tile,
		 * and the shell gates the controller-as-mouse and the on-screen
		 * keyboard on them BY NAME — `activeApp.pointer === "1"`. A
		 * record without the column reads `undefined`, which is not
		 * "1", so a server tile launched with no mouse and no keyboard
		 * while every app tile beside it on the same shelf had both.
		 * Reported from the sofa as "the controller mouse isn't working
		 * when I launch Plex like the rest of the apps".
		 *
		 * They are not a judgement call here the way they are in
		 * apps_table(). A server tile has no program behind it: it is a
		 * URL, and pressing it opens somebody's web interface in a
		 * browser (big_run → big_open). That is the case the pointer
		 * was WRITTEN for — a browser takes pointer events and cannot
		 * be handed words on a pipe — and the case the keyboard is for,
		 * since a media server's web interface wants a login and a
		 * search box. The news shelf, which opens a browser by the very
		 * same route, has always passed "1"/"1"; this shelf was simply
		 * never given the columns to pass.
		 *
		 * ⚠ ON THE END, per the rule above, so a six-column media.tsv
		 * cached by an older build stays readable — it just has no
		 * mouse until MEDIA_TTL expires and the next refresh rewrites
		 * it. Ten minutes, not a reinstall. */
		rec_frow(mem, 8, "id", "name", "url", "source", "kind",
			 "iconfile", "pointer", "keys");
		for (int i = 0; i < n; i++)
			rec_frow(mem, 8, found[i].id, found[i].name,
				 found[i].url, found[i].source, "server",
				 icon_file(found[i].source), "1", "1");
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
			rec_row(8, "id", "name", "url", "source", "kind",
				"iconfile", "pointer", "keys");
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

/* ── music, driven rather than launched ──────────────────────────────────── */

/* Declared rather than moved: the XML scanners belong with the news reader
 * below, which is what they were written for. Plex answers XML too, and one
 * scanner read by two callers is better than the same loop written twice. */
static bool xml_attr(const char *tag, const char *name, char *out, size_t n);
static void xml_unescape(char *s);

/*
 * WHERE the music comes from, which is a different question from WHICH PLAYER
 * plays it.
 *
 * `music = <program>` (above) picks the program. This picks what that program
 * is pointed AT, and until now nothing did: cliamp starts on its `radio`
 * provider with three internet stations preloaded, so the Music tile on a
 * television full of somebody's own albums played lo-fi radio and there was no
 * way from the sofa to say otherwise.
 *
 * ⚠ THE SOURCE IS A START-UP FLAG, NOT A TRANSPORT VERB. cliamp takes
 * `--provider` when it starts and has no IPC verb to change it afterwards, so
 * switching source means stopping the player and starting it again. That is
 * why source_apply() below is allowed to do something as rude as killing the
 * music: there is no gentler mechanism, and a picker that silently did nothing
 * until the next reboot would be worse.
 *
 * ── queueable, and why three of the five are not the same kind of thing ────
 *
 * `queueable` is the honest half of this table. cliamp's own interface can
 * browse all of these; big screen mode can only FILL A QUEUE for the ones
 * whose contents are reachable from outside that interface:
 *
 *   radio    cliamp preloads its stations itself — nothing to do
 *   plex     an HTTP API this file can read (see big_music_plex)
 *   local    files on a disk
 *
 * YouTube Music and Spotify are searched and browsed inside the TUI and
 * nowhere else, so choosing one of them cannot end in music the way the other
 * three do. Rather than pretend, they open cliamp itself on the television —
 * see big_music_browse(). A tile that sets a setting and plays nothing is the
 * exact failure this file keeps warning about.
 */
struct source {
	const char *id;		/* what goes in big.conf                     */
	const char *name;	/* what the television says                  */
	const char *provider;	/* cliamp's --provider, NULL for none        */
	bool queueable;		/* can big screen mode fill a queue for it?  */
};

/* Ordered for somebody on a sofa: their own library first, then the services
 * they might subscribe to, then the two that always work. */
static const struct source SOURCES[] = {
	{ "plex",    "Plex",          "plex",    true  },
	/* ⚠ QUEUEABLE SINCE 0.1.0-29, and it took a station list to become so.
	 * cliamp cannot be asked to search YouTube Music from outside its TUI,
	 * but yt-dlp can enumerate any URL and `cliamp queue` takes what comes
	 * out — so the contents ARE reachable from here, which is the only
	 * thing this column has ever meant. See yt_stations(). */
	{ "ytmusic", "YouTube Music", "ytmusic", true  },
	{ "spotify", "Spotify",       "spotify", false },
	{ "local",   "Local files",   NULL,      true  },
	{ "radio",   "Radio",         "radio",   true  },
};
#define SOURCES_N ((int)(sizeof(SOURCES) / sizeof(SOURCES[0])))

static const struct source *source_by_id(const char *id)
{
	if (!id || !*id)
		return NULL;
	for (int i = 0; i < SOURCES_N; i++)
		if (strcmp(SOURCES[i].id, id) == 0)
			return &SOURCES[i];
	return NULL;
}

/*
 * The chosen source: `music_source = <id>` in big.conf, or radio.
 *
 * ⚠ RADIO IS THE DEFAULT BECAUSE IT IS WHAT ALREADY HAPPENS. cliamp with no
 * --provider starts on radio with a queue in it, so an unset key has to mean
 * radio or the first release of this picker would silently change what the
 * Music tile does on every machine that never opened it.
 */
static const struct source *music_source(void)
{
	char want[64];
	if (big_conf_get("music_source", want, sizeof(want))) {
		const struct source *s = source_by_id(want);
		if (s)
			return s;
		/* Said out loud and falls back, for the same reason `music =`
		 * does: a config file that names a source nobody implements is
		 * a typo, and silence sends somebody to look at the player. */
		fprintf(stderr, "syn-arcade: big.conf says music_source = %s, "
				"which is not one of plex, ytmusic, spotify, "
				"local or radio — using radio\n", want);
	}
	return source_by_id("radio");
}

/*
 * Write one key back into big.conf.
 *
 * ⚠ EVERY EXISTING LINE FOR THE KEY GOES, and the new one is appended. Not
 * "replace the first": big_conf_get takes the LAST assignment it sees, so a
 * file that already says the key twice — which is what a hand-edited config
 * looks like after somebody changed their mind — would be rewritten at the top
 * and still read from the bottom. The setting would appear not to take, and
 * the file would look right.
 *
 * Comments and every other key are preserved, because this file is a person's
 * config and not this program's storage.
 */
static int big_conf_set(const char *key, const char *val)
{
	char path[SYN_PATH];
	if (!config_path(path, sizeof(path), "syn-arcade/big.conf"))
		return EX_FAIL;

	char *text = read_file(path);		/* may not exist yet */
	size_t cap = (text ? strlen(text) : 0) + strlen(key) + strlen(val) + 16;
	char *out = xmalloc(cap);
	size_t w = 0;
	out[0] = '\0';

	if (text) {
		char *save = NULL;
		for (char *ln = strtok_r(text, "\n", &save); ln;
		     ln = strtok_r(NULL, "\n", &save)) {
			/* ⚠ The line is matched on a COPY. trim() and the split
			 * on '=' both write into what they are given, and the
			 * line that survives has to reach the new file exactly
			 * as the person wrote it — spacing, comment and all. */
			char probe[1024];
			snprintf(probe, sizeof(probe), "%s", ln);
			char *t = trim(probe);
			char *eq = strchr(t, '=');
			if (*t && *t != '#' && eq) {
				*eq = '\0';
				if (strcmp(trim(t), key) == 0)
					continue;	/* dropped */
			}
			w += (size_t)snprintf(out + w, cap - w, "%s\n", ln);
		}
		free(text);
	}

	snprintf(out + w, cap - w, "%s = %s\n", key, val);

	int rc = mkdir_parents(path);
	if (rc == 0)
		rc = write_file_inplace(path, out);
	free(out);
	return rc == 0 ? EX_OK : EX_FAIL;
}

/*
 * Transport for a headless player, which on this system means cliamp.
 *
 * ⚠ THIS IS NOT A SECOND MUSIC PLAYER. Every verb here is one cliamp
 * subcommand, run and waited for. The point is that the television never has to
 * show a terminal: cliamp is a TUI, and a TUI four metres away with a gamepad in
 * somebody's hands is a window they cannot read and cannot leave. Its own
 * `--daemon` flag exists for exactly this ("serving IPC for scripts/Waybar"),
 * so big screen mode becomes its face.
 *
 * The socket is the whole state model. `~/.config/cliamp/cliamp.sock` present
 * and answering means there is something to control; absent means there is not,
 * and the menu simply has no music row rather than a row whose buttons do
 * nothing.
 */
static bool music_socket_live(void)
{
	/* ⚠ ASKED, not stat()ed. A socket FILE outlives the process that bound
	 * it — there is a stale one on this machine from a cliamp that exited
	 * in August — so its presence proves nothing. `status` answers "not
	 * running" for a stale socket, which is the question actually being
	 * asked. */
	char *out = run_capture((char *const[]){ "cliamp", "status", "--json",
						 NULL }, 2);
	if (!out)
		return false;
	bool live = strstr(out, "\"ok\":true") || strstr(out, "\"ok\": true");
	free(out);
	return live;
}

/*
 * Start the player and wait for it to answer.
 *
 * ⚠ ON A PTY, AND NOT WITH `--daemon`, WHICH IS THE OBVIOUS THING AND IS WRONG.
 * cliamp has a headless mode and it does exactly what it says — but its
 * visualizer is part of the TUI's own draw loop, so in `--daemon` the bands
 * never get computed: `cliamp vis` answers "visualizer not available in
 * headless mode" and `visstream` yields nothing but `{"ok":false,"error":"bands
 * timeout"}` for ever. A visualizer that is silently always flat is worse than
 * no visualizer, because it looks like a rendering bug in this code.
 *
 * So the TUI is run with a terminal and no WINDOW: `script` hands it a pty,
 * cliamp draws into it, and the drawing goes to /dev/null. Everything else is
 * identical — the same socket, the same transport verbs — and the bands are
 * real. `script` is util-linux, which is on every Arch system by construction.
 *
 * ⚠ WAITED FOR, and the wait is the reason this is not two lines. cliamp
 * returns before its socket is bound, so a `play` sent straight afterwards
 * lands on nothing and the tile press is silently lost — the one failure this
 * whole design is meant to avoid, and the one that would look exactly like a
 * broken tile.
 *
 * ⚠ AND ON THE CHOSEN SOURCE, which only has an effect HERE. `--provider` is a
 * start-up flag; a player already running keeps whatever it was started with,
 * which is why this returns early and why changing source has to go through
 * music_restart() rather than through here.
 */
/*
 * ── the marker: did WE start this player? ──────────────────────────────────
 *
 * Quitting big screen mode has to answer a question that only this file knows
 * the answer to.
 *
 * cliamp here is HEADLESS — `script -qfc cliamp …`, a TUI on a pty with no
 * terminal attached to it. It has no window, it is not a toplevel so nothing
 * in the dock or the switcher can reach it, and synui's bar has no MPRIS
 * controls. So a player this interface started and then walked away from is
 * music with NO WAY TO STOP IT short of opening a terminal and typing
 * `cliamp stop`. Reported exactly that way: Quit, and the music is still
 * going.
 *
 * ⚠ AND "STOP IT ALWAYS" IS THE WRONG FIX, which is why this exists rather
 * than a kill on the way out. A cliamp somebody started themselves, in a
 * terminal they are looking at, is not headless and not ours — big screen mode
 * will happily drive it over the same socket while it is up, and ending it on
 * the way out would be this launcher reaching over somebody's music.
 *
 * So the marker records the one thing that distinguishes them: whether the
 * player on that socket was started BY THIS PACKAGE. Written where the lock
 * lives, in a tmpfs logind wipes at logout, so it cannot outlive the session
 * that made it and be believed by the next one.
 */
static bool music_ours_path(char *buf, size_t n)
{
	const char *run = getenv("XDG_RUNTIME_DIR");
	if (run && *run)
		return snprintf(buf, n, "%s/syn-arcade-music.ours", run) < (int)n;
	return snprintf(buf, n, "/tmp/syn-arcade-music-%u.ours",
			(unsigned)getuid()) < (int)n;
}

static void music_mark_ours(bool ours)
{
	char path[SYN_PATH];
	if (!music_ours_path(path, sizeof(path)))
		return;
	if (!ours) {
		unlink(path);
		return;
	}
	FILE *f = fopen(path, "w");
	if (f)
		fclose(f);
}

static bool music_is_ours(void)
{
	char path[SYN_PATH];
	return music_ours_path(path, sizeof(path)) && access(path, F_OK) == 0;
}

static bool music_ensure_running(void)
{
	if (music_socket_live())
		return true;

	/* ⚠ ONE STRING, and `script -c` hands it to /bin/sh. Nothing
	 * user-supplied may ever reach it: the provider comes from SOURCES[]
	 * above, which is a table of literals in this file, and music_source()
	 * refuses anything that is not in it. A config file that could put
	 * arbitrary text here would be a config file that runs commands. */
	const struct source *src = music_source();
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "cliamp%s%s",
		 src && src->provider ? " --provider " : "",
		 src && src->provider ? src->provider : "");

	/* ⚠ -f, so the pty is flushed rather than buffered: `script` without it
	 * can hold a TUI's output long enough that the program blocks on a
	 * write nobody is draining. */
	char *const argv[] = { "script", "-qfc", cmd, "/dev/null", NULL };
	if (spawn_detached(argv) != EX_OK)
		return false;

	for (int i = 0; i < 30; i++) {		/* up to ~3s */
		struct timespec ts = { 0, 100 * 1000 * 1000 };
		nanosleep(&ts, NULL);
		if (music_socket_live()) {
			/* ⚠ MARKED ONLY WHERE ONE WAS ACTUALLY STARTED. The
			 * early return at the top of this function — "already
			 * running" — deliberately does not come through here,
			 * because a player that was already up is somebody
			 * else's. */
			music_mark_ours(true);
			return true;
		}
	}
	return false;
}

/*
 * Stop the player, and be sure it is the player being stopped.
 *
 * cliamp writes its pid beside its socket, which is the only handle there is:
 * it has transport verbs but no "quit", and the source cannot be changed
 * without a restart.
 *
 * ⚠ THE PID FILE IS NOT PROOF. It outlives the process that wrote it — this
 * machine had a stale socket from a cliamp that exited weeks earlier — and a
 * pid on a busy machine is reused. Sending SIGTERM to a number out of a stale
 * file is how a music picker kills somebody's compile. So the socket must be
 * answering first (something IS bound to it), and /proc/<pid>/comm must say
 * cliamp before anything is signalled.
 */
static bool music_stop_player(void)
{
	if (!music_socket_live())
		return true;			/* nothing to stop */

	char path[SYN_PATH];
	if (!config_path(path, sizeof(path), "cliamp/cliamp.sock.pid"))
		return false;
	char *text = read_file(path);
	if (!text)
		return false;
	long pid = strtol(trim(text), NULL, 10);
	free(text);
	if (pid <= 1)
		return false;

	char comm[64];
	snprintf(comm, sizeof(comm), "/proc/%ld/comm", pid);
	char *who = read_file(comm);
	if (!who)
		return false;
	bool is_cliamp = strcmp(trim(who), "cliamp") == 0;
	free(who);
	if (!is_cliamp)
		return false;

	if (kill((pid_t)pid, SIGTERM) != 0)
		return false;

	for (int i = 0; i < 30; i++) {		/* up to ~3s */
		struct timespec ts = { 0, 100 * 1000 * 1000 };
		nanosleep(&ts, NULL);
		if (!music_socket_live()) {
			/* Whatever we started is gone, so the claim goes with
			 * it — including on the restart paths, where the next
			 * ensure_running() makes it again. */
			music_mark_ours(false);
			return true;
		}
	}
	return false;
}

/*
 * Let go of the music on the way out.
 *
 * ⚠ ONLY WHAT THIS PACKAGE STARTED, which is the whole point — see the marker
 * above. A player somebody has in a terminal is left exactly where it is.
 *
 * ⚠ AND IT IS NOT AN ERROR TO HAVE NOTHING TO DO. This runs on every Quit,
 * including the overwhelmingly common one where no music was ever started, so
 * "nothing to release" is a success and says nothing.
 */
static int big_music_release(void)
{
	if (!music_is_ours())
		return EX_OK;

	/* ⚠ THE CLAIM IS DROPPED WHEN THERE IS NOTHING LEFT TO CLAIM. A player
	 * that has already gone — somebody quit it from its own interface, or
	 * it crashed — leaves a marker that would otherwise outlive it for the
	 * rest of the session and make the next Quit report a failure it could
	 * do nothing about. */
	if (!music_socket_live()) {
		music_mark_ours(false);
		return EX_OK;
	}

	if (!music_stop_player()) {
		fputs("syn-arcade: the music player did not stop\n", stderr);
		return EX_FAIL;
	}
	puts("stopped the music this session started");
	return EX_OK;
}

/*
 * Start again on the source that is configured now.
 *
 * ⚠ IT REALLY DOES STOP THE MUSIC, and that is the point rather than a side
 * effect. Two things need it: changing source (a start-up flag), and playing a
 * Plex album — cliamp's `queue` APPENDS, so without a restart the second album
 * somebody picks plays after the first one finishes, which from a sofa is a
 * button that did nothing.
 */
static bool music_restart(void)
{
	music_stop_player();
	return music_ensure_running();
}

/* One transport word, straight through. */
static int music_cmd(const char *verb)
{
	char *const argv[] = { "cliamp", (char *)verb, NULL };
	char *out = run_capture(argv, 3);
	free(out);
	return EX_OK;
}

/*
 * What is playing, and what it is CALLED.
 *
 * ⚠ CLIAMP DOES NOT NAME A QUEUED TRACK. `cliamp queue <thing>` takes a path
 * and reports that path back as the title — no tags are read — so everything
 * this file queues would arrive on the television as
 *
 *   /mnt/drive8tb/music/2Pac - Discography [FLAC…]/…/02. Trapped.flac
 *
 * or, for a track streamed from a Plex server that is not this machine, as an
 * HTTP URL WITH THE PLEX TOKEN IN IT — somebody's credential, four metres
 * wide, in every screenshot of the Start menu. Measured, not assumed: that is
 * what `status --json` answers after a queue.
 *
 * So whatever queues also writes down what it queued, in the same record
 * encoding as every other cache here, and this reads it back. The map is keyed
 * on the path cliamp reports.
 */
static bool music_titles_path(char *buf, size_t n)
{
	return cache_path(buf, n, "music-titles.rec");
}

/*
 * How a queued track is NAMED in the titles cache — the one home for that
 * rule, because a writer and a reader that disagree about it produce a lookup
 * that silently misses and a television showing a URL.
 *
 * ⚠ THE QUERY IS DROPPED, AND FOR ONE REASON: a track streamed from a Plex
 * server carries `?X-Plex-Token=…`, somebody's credential, and nothing here
 * writes one into a cache file. Which means nothing here may look one up with
 * it either — hence both sides going through this.
 *
 * ⚠ EXCEPT ON YOUTUBE, WHERE THE QUERY IS THE IDENTITY. `?v=<id>` is not
 * decoration on a YouTube URL, it is which song it is, and dropping it keyed
 * every track on the site to `https://www.youtube.com/watch` — one bucket, and
 * a fallback that named the song "watch". Measured exactly that way on the
 * first station that played. So a YouTube URL is reduced to its video id and
 * nothing else: no `list=`, no `t=`, no tracking parameters, and still no
 * credential — YouTube does not put one there.
 */
static void music_key(const char *raw, char *out, size_t n)
{
	if (!out || !n)
		return;
	if (!raw || !*raw) {
		out[0] = '\0';
		return;
	}

	if (strstr(raw, "youtube.com/") || strstr(raw, "youtu.be/")) {
		/* `?v=` or `&v=`, because a watch URL reached through a
		 * playlist has the list first often enough. The short form
		 * youtu.be/<id> carries the id as its PATH instead. */
		const char *v = strstr(raw, "?v=");
		if (!v)
			v = strstr(raw, "&v=");
		if (v)
			v += 3;
		else if ((v = strstr(raw, "youtu.be/")) != NULL)
			v += 9;

		if (v) {
			char id[64];
			size_t i = 0;
			while (v[i] && v[i] != '&' && v[i] != '?' &&
			       v[i] != '/' && i < sizeof(id) - 1) {
				id[i] = v[i];
				i++;
			}
			id[i] = '\0';
			if (id[0]) {
				snprintf(out, n,
					 "https://www.youtube.com/watch?v=%s",
					 id);
				return;
			}
		}
	}

	snprintf(out, n, "%s", raw);
	char *q = strchr(out, '?');
	if (q)
		*q = '\0';
}

/* Remember `path` is called `title`, appended to whatever is already known.
 * Called once per track by the queueing paths below. */
static void music_title_remember(FILE *f, const char *path, const char *title)
{
	if (f && path && *path && title && *title)
		rec_frow(f, 2, path, title);
}

static bool music_title_lookup(const char *path, char *out, size_t n)
{
	if (!path || !*path)
		return false;

	char cache[SYN_PATH];
	if (!music_titles_path(cache, sizeof(cache)))
		return false;
	char *text = read_file(cache);
	if (!text)
		return false;

	bool found = false;
	char *save = NULL;
	for (char *ln = strtok_r(text, "\n", &save); ln && !found;
	     ln = strtok_r(NULL, "\n", &save)) {
		char *tab = strchr(ln, '\t');
		if (!tab)
			continue;
		*tab = '\0';
		char *k = pct_decode(ln);
		char *v = pct_decode(tab + 1);
		if (k && v && strcmp(k, path) == 0) {
			snprintf(out, n, "%s", v);
			found = true;
		}
		free(k);
		free(v);
	}
	free(text);
	return found;
}

/*
 * The fallback, for a track nothing here queued.
 *
 * ⚠ THE QUERY GOES, ALWAYS. It is where a Plex token lives, and this string is
 * printed, recorded and drawn. Cutting at '?' is not tidying.
 */
static void music_title_fallback(const char *path, char *out, size_t n)
{
	char work[512];
	snprintf(work, sizeof(work), "%s", path);

	char *q = strchr(work, '?');
	if (q)
		*q = '\0';

	const char *base = strrchr(work, '/');
	base = base ? base + 1 : work;
	snprintf(out, n, "%s", *base ? base : work);
}

/*
 * ── the PICTURE for a track, which nobody publishes ────────────────────────
 *
 * ⚠ CLIAMP PUBLISHES NO `mpris:artUrl` AT ALL — not an empty one, not a broken
 * one, the key is ABSENT. Measured on this machine mid-playlist, the whole of
 * what a playing YouTube track offers:
 *
 *     xesam:title "watch"   xesam:url https://www.youtube.com/watch?v=SsKT0s5J8ko
 *     mpris:length          mpris:trackid
 *
 * Four keys. So the widget's cover tile had nothing to load and sat on its
 * placeholder for every song — while the SAME video played through Firefox
 * filled it in, because Firefox publishes the thumbnail. That difference is
 * what made a missing field look like a broken widget, and it is the
 * discriminating test: compare the two players' metadata, not the QML.
 *
 * ⚠ AND YT-DLP CANNOT SUPPLY IT — THE SAME TRAP `%(url)s` SPRINGS, ONE FIELD
 * OVER. Under `--flat-playlist`, `%(thumbnail)s` prints the literal string
 * `NA`, exactly as `%(url)s` does; measured on a mix, three entries, all `NA`.
 * Asking for it properly means dropping `--flat-playlist`, which is one HTTP
 * round trip per track and the minutes-long station start yt_enumerate() exists
 * to avoid. So there is no thumbnail to write into the titles cache at queue
 * time, and adding a column for one would have cached `NA` sixty times.
 *
 * It does not need caching. A YouTube thumbnail is a pure function of the video
 * id — and the key IS the video id, already reduced by music_key(). So the
 * picture is derived from it here: no network, no second cache to go stale, and
 * no third place that knows how a YouTube URL is spelled.
 *
 * ⚠ `mqdefault`, AND THE CHOICE IS NOT COSMETIC. Measured, same video:
 *
 *     maxresdefault  1280x720   404s on older videos — measured on jNQXAC9IVRw
 *     hqdefault       480x360   always there, 4:3 — BLACK BARS baked in
 *     mqdefault       320x180   always there, 16:9, no bars
 *
 * The tile is 64px square and CROPS what it draws, so hqdefault's bars would be
 * cropped into the picture rather than off it. And a maxres that 404s is
 * precisely the failure MusicPlayer.qml's own header warns about: an Image that
 * sits at Error for ever, showing nothing, which is the bug being fixed here
 * wearing a different hat. mqdefault is the one that is always present and
 * always the right shape — 320x180 crops to 180 square, for a tile that decodes
 * at 128.
 *
 * ⚠ THE ID IS VALIDATED, NOT JUST COPIED. This string is printed as a record and
 * handed to another program to fetch, and the key it came from was built out of
 * somebody else's URL. A YouTube id is [A-Za-z0-9_-]; anything else means this
 * is not the shape assumed here, and the honest answer is no picture rather
 * than a malformed URL for a shell to chase.
 *
 * ⚠ ONLY YOUTUBE ANSWERS, and the two silences are deliberate. A Plex stream's
 * art needs the server and the token that nothing here may write down
 * ([[music_key]]'s whole reason), and a local file's art is in its tags, which
 * nothing on this path reads. Both get "" and keep the placeholder that has
 * always been drawn — an empty column is a real answer, not a failure.
 */
static void music_art(const char *keyed, char *out, size_t n)
{
	if (!out || !n)
		return;
	out[0] = '\0';
	if (!keyed || !*keyed)
		return;

	/* ⚠ music_key()'s CANONICAL SPELLING, not a URL as it arrived. Every
	 * YouTube key is reduced to exactly this shape before it is written or
	 * looked up, so matching it whole is what keeps this from being a
	 * second URL parser that can disagree with the first. */
	static const char pfx[] = "https://www.youtube.com/watch?v=";
	if (strncmp(keyed, pfx, sizeof(pfx) - 1) != 0)
		return;

	const char *id = keyed + sizeof(pfx) - 1;
	if (!*id)
		return;
	for (const char *c = id; *c; c++)
		if (!isalnum((unsigned char)*c) && *c != '_' && *c != '-')
			return;

	snprintf(out, n, "https://i.ytimg.com/vi/%s/mqdefault.jpg", id);
}

/*
 * ── what was playing last, so the Music tile has something to play ─────────
 *
 * ⚠ THE QUEUE DOES NOT SURVIVE THE PLAYER, AND SINCE 0.1.0-33 THE PLAYER DOES
 * NOT SURVIVE QUIT. Those two facts are harmless apart and together they made
 * the Music tile do nothing at all.
 *
 * `--provider` is a start-up flag, and what it preloads is the whole of the
 * queue a fresh player comes up with: measured on this machine, `radio` brings
 * eleven stations and `ytmusic` brings NOTHING. Every other queue here is one
 * this file filled, track by track, over cliamp's socket — and nothing on the
 * other end writes it down. So a player that has been stopped and started
 * again is an empty one, and `toggle` on an empty queue is silence.
 *
 * Until 0.1.0-33 that was hidden by the player outliving big screen mode: Quit
 * left it running with its queue, so the next press was a genuine resume. Now
 * that Quit lets go of the music — which it must, or the music cannot be
 * stopped at all — pressing Music started a bare player on a source with no
 * preloaded queue and played nothing. Reported exactly that way.
 *
 * So whatever fills a queue writes down what filled it, and the Music tile
 * puts it back. ⚠ THE SOURCE IS PART OF THE RECORD, not decoration: replaying
 * a YouTube station goes through yt_play(), which SETS `music_source`, so
 * replaying one after somebody deliberately moved to Plex would quietly undo
 * the choice they just made. A record from another source is not this source's
 * to resume.
 *
 * ⚠ AND IT IS A REFERENCE, NEVER A TRACK URL. What goes in is a Plex rating
 * key, a station URL or a music directory — the thing that was asked for. The
 * URLs cliamp is actually handed carry the Plex token, and nothing here writes
 * one into a cache file; see music_key() for the other half of that rule.
 */
static bool music_last_path(char *buf, size_t n)
{
	return cache_path(buf, n, "music-last.rec");
}

static void music_last_remember(const char *source, const char *what)
{
	if (!source || !*source || !what || !*what)
		return;

	char path[SYN_PATH];
	if (!music_last_path(path, sizeof(path)) || mkdir_parents(path) != 0)
		return;

	/* ⚠ TRUNCATED, not appended. There is one last thing, and a file that
	 * grew a line per press would be read by whichever line came first. */
	FILE *f = fopen(path, "w");
	if (!f)
		return;
	rec_frow(f, 2, source, what);
	fclose(f);
}

static bool music_last_read(char *source, size_t sn, char *what, size_t wn)
{
	if (source && sn) source[0] = '\0';
	if (what && wn) what[0] = '\0';

	char path[SYN_PATH];
	if (!music_last_path(path, sizeof(path)))
		return false;
	char *text = read_file(path);
	if (!text)
		return false;

	bool ok = false;
	char *nl = strchr(text, '\n');
	if (nl)
		*nl = '\0';
	char *tab = strchr(text, '\t');
	if (tab) {
		*tab = '\0';
		char *s = pct_decode(text);
		char *w = pct_decode(tab + 1);
		if (s && w && *s && *w) {
			snprintf(source, sn, "%s", s);
			snprintf(what, wn, "%s", w);
			ok = true;
		}
		free(s);
		free(w);
	}
	free(text);
	return ok;
}

/*
 * `state` is empty when there is nothing to control, and that is the signal the
 * menu keys off — an absent row rather than a dead one.
 */
/*
 * One read of the player's state, for everything that needs one.
 *
 * ⚠ `total` IS HOW MANY TRACKS ARE QUEUED, and it is asked here rather than by
 * a second status call because this function is the one door onto that answer
 * — two readers of the same JSON are two readers that can disagree about it.
 * cliamp omits the key entirely on an empty queue, so 0 means "nothing to
 * play", which is a different thing from `stopped` and is the difference
 * between resuming and having something to resume.
 */
static void music_read(char *state, size_t sn, char *title, size_t tn,
		       char *path, size_t pn, long *total)
{
	if (state && sn) state[0] = '\0';
	if (title && tn) title[0] = '\0';
	if (path && pn) path[0] = '\0';
	if (total) *total = 0;

	char *json = run_capture((char *const[]){ "cliamp", "status", "--json",
						  NULL }, 2);
	if (json && (strstr(json, "\"ok\":true") || strstr(json, "\"ok\": true"))) {
		if (state) json_str(json, NULL, "state", state, sn);
		if (total) *total = json_int(json, NULL, "total", 0);
		/* ⚠ Scoped to the track object. `title` is a common enough key
		 * that searching the whole document would one day find
		 * somebody else's. */
		const char *tr = strstr(json, "\"track\"");

		char raw[512] = "";
		json_str(tr ? tr : json, NULL, "path", raw, sizeof(raw));

		/* ⚠ THE KEY IS music_key's ANSWER AND NOTHING ELSE, because
		 * both sides of the lookup have to agree on it. It used to be
		 * spelled out here — strip everything from the `?` — and that
		 * is the rule that keyed every YouTube track to
		 * `https://www.youtube.com/watch`. */
		char keyed[512];
		music_key(raw, keyed, sizeof(keyed));

		if (title) {
			json_str(tr ? tr : json, NULL, "title", title, tn);

			/*
			 * ⚠ WHAT WE WROTE DOWN WINS, AND IT HAS TO BE ASKED
			 * FIRST.
			 *
			 * This used to consult the cache only when cliamp's
			 * title was character-for-character the path — its way
			 * of saying it has no name for a queued track. That
			 * test held for Plex and for local files and is FALSE
			 * for YouTube: cliamp names such a track from the last
			 * segment of its URL, so every song off a station
			 * arrived on the television called `watch`. Measured
			 * on the first station that played.
			 *
			 * The cache only ever holds tracks THIS FILE queued,
			 * with the names their source gave them, so a hit is
			 * always the better answer. A miss leaves whatever
			 * cliamp said — a radio station, or a local file it
			 * knows — exactly as before.
			 */
			if (!music_title_lookup(keyed, title, tn) &&
			    raw[0] && strcmp(title, raw) == 0)
				music_title_fallback(keyed, title, tn);
		}
		/* ⚠ The path is recorded too, and it is the one field that can
		 * carry a token. Stripped here rather than at each reader:
		 * this function is the only door it comes through. */
		if (path)
			snprintf(path, pn, "%s", keyed);
	}
	free(json);
}

static int big_music_status(bool rec)
{
	char state[32], title[256], path[512], art[512];
	music_read(state, sizeof(state), title, sizeof(title),
		   path, sizeof(path), NULL);

	/* ⚠ DERIVED FROM `path`, WHICH music_read() HAS ALREADY KEYED — never
	 * from cliamp's raw answer. That is what keeps a Plex token out of this
	 * column as well as out of the last one. */
	music_art(path, art, sizeof(art));

	if (rec) {
		/* ⚠ A COLUMN IS ADDED, NEVER REORDERED. Both readers —
		 * synui's MusicLibrary and this program's own television —
		 * parse these rows by HEADER NAME into an object, so a new
		 * field at the end is invisible to a shell that predates it
		 * and present to one that wants it. */
		rec_row(4, "state", "title", "path", "art");
		rec_row(4, state, title, path, art);
		return EX_OK;
	}

	if (!state[0]) {
		puts("no music player is running");
		return EX_EMPTY;
	}
	printf("%-8s %s\n", state, title[0] ? title : "(nothing)");
	return EX_OK;
}

/*
 * ── a track that will not play, and the silence it makes ───────────────────
 *
 * ⚠ MEASURED, NOT GUESSED, and it is the whole reason this exists: a queued
 * YouTube URL that cannot be resolved leaves cliamp `stopped` FOR EVER. It
 * does not skip it, it does not stop the queue, it does not say anything on
 * any stream this program can read — it simply never starts. Watched here for
 * 24 seconds against a real player: state `stopped`, no movement, no error.
 *
 * ⚠ AND SUCH A TRACK LOOKS PERFECTLY FINE ON THE WAY IN. Enumeration is
 * `--flat-playlist`, which asks YouTube for the playlist's own listing rather
 * than resolving each entry — that is what makes it fast enough to press a
 * button for. The listing gives a real title, a real duration and a real view
 * count for a video that answers "Video unavailable" the moment anybody tries
 * to play it, cookies or no cookies. Region locks and rights withdrawals both
 * look like this, and `%(availability)s` is `NA` in a flat listing, so there
 * is nothing to filter on. Somebody's own playlist, made over years, will have
 * a few. This one had its dead track FIRST.
 *
 * Reported from the sofa as "it doesn't want to play unless I skip and then
 * play, and it's inconsistent" — which is exactly right, and is this: skipping
 * moves off the track that will not play, and whether the next one works
 * decides whether it was worth doing.
 *
 * So the player is asked to start, and then asked whether it really did.
 *
 * ⚠ THE WAIT IS THE POINT, AND THIS FILE HAS THE SCARS TO PROVE IT. A "make
 * sure it started" check was written once before and made a reliable station
 * start about half the time, because the state LAGS the command — the queue
 * finishes at t+4s and cliamp does not report `playing` until t+6s while it
 * resolves the first track through yt-dlp. That check ran two seconds early
 * every time, always saw `stopped`, and always sent a second `toggle`, which
 * from `playing` is PAUSE and cancelled the start it was insuring.
 *
 * The lesson was "not playing yet is not did not take", and the answer is to
 * WAIT for the settle rather than to sample once. Fifteen seconds is more than
 * twice the longest start measured here, and nothing acts until it is up.
 */
/*
 * ⚠ THE FIRST WAIT IS SHORT NOW, AND THAT IS SAFE BECAUSE OF WHAT FOLLOWS IT.
 * It was fifteen seconds when the only answer to a stalled start was to SKIP
 * the track — a wait that long was the price of not throwing away a song that
 * was merely slow. The answer now is to ask the same track again, which costs
 * nothing if it was already starting, so the wait only has to be longer than a
 * healthy start (measured 2–4s on this machine, 8s of headroom here).
 */
#define MUSIC_SETTLE_MS   5000		/* > the longest measured healthy start */
#define MUSIC_NUDGE_MS    5000		/* and how long a re-ask is given     */
#define MUSIC_NUDGE_MAX       3		/* re-asks before the track is blamed */
#define MUSIC_RETRY_MS   12000		/* the same patience, one track along */
#define MUSIC_SKIP_MAX       3		/* a whole dead playlist is not ours */

/*
 * ⚠ THE WAITS ARE OVERRIDABLE FOR THE SUITE, AND FOR NOTHING ELSE — the same
 * arrangement SYN_ARCADE_NO_NET and SYN_ARCADE_SYSFS already have in this
 * file. Fifteen seconds is the right answer on a television and the wrong one
 * inside a test run that meson kills at two minutes: without this, the
 * assertions that drive a stalled queue take a minute apiece and turn a
 * passing suite into a failed BUILD.
 */
static int music_wait_ms(int def)
{
	const char *s = getenv("SYN_ARCADE_MUSIC_WAIT_MS");
	if (s && *s) {
		long v = strtol(s, NULL, 10);
		if (v > 0 && v < 120000)
			return (int)v;
	}
	return def;
}

static bool music_settled(int ms)
{
	ms = music_wait_ms(ms);
	for (int waited = 0; waited < ms; waited += 1000) {
		struct timespec ts = { 1, 0 };
		nanosleep(&ts, NULL);

		char state[32];
		music_read(state, sizeof(state), NULL, 0, NULL, 0, NULL);

		if (!strcmp(state, "playing"))
			return true;

		/* ⚠ PAUSED IS A DECISION SOMEBODY MADE while this was
		 * watching, and an empty state is a player that has gone. Both
		 * mean there is nothing here to rescue, and carrying on would
		 * be this program pressing play over the top of a person. */
		if (!strcmp(state, "paused") || !state[0])
			return true;
	}
	return false;
}

/*
 * ⚠ ONE OF THESE AT A TIME, AND THE LOCK IS NOT TIDINESS.
 *
 * Waiting fifteen seconds is a long time on a sofa, and the natural response
 * to a button that has not answered is to press it again. Two of these running
 * at once are two processes sending `next` and `toggle` at a queue on their
 * own timers — and `toggle` from `playing` is PAUSE, so the second one would
 * stop the music the first one had just got going. That is the same shape of
 * fault as the "insurance toggle" this file already paid for once.
 *
 * The lock is the kernel's, held for exactly as long as the process lives and
 * released however it dies, so a rescue killed halfway leaves nothing behind
 * to block the next one. Failing to take it is not an error: it means one is
 * already in flight, which is precisely what the second press wanted.
 */
static bool music_insist_lock(int *fd)
{
	char path[SYN_PATH];
	const char *run = getenv("XDG_RUNTIME_DIR");
	if (run && *run)
		snprintf(path, sizeof(path), "%s/syn-arcade-music.start", run);
	else
		snprintf(path, sizeof(path), "/tmp/syn-arcade-music-%u.start",
			 (unsigned)getuid());

	*fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (*fd < 0)
		return true;		/* no lock to be had: carry on alone */
	if (flock(*fd, LOCK_EX | LOCK_NB) != 0) {
		close(*fd);
		*fd = -1;
		return false;
	}
	return true;
}

/*
 * ── the first toggle is LOST, and the track is not the problem ─────────────
 *
 * ⚠ MEASURED, AFTER THREE RELEASES OF GUESSING AT IT. A player that has just
 * come up and been given its first track answers `toggle` by doing nothing,
 * often enough to be the normal case on this machine — and then sits at
 * `stopped` for as long as it is left. Sent a SECOND toggle, on the SAME
 * track, it starts within four seconds. Watched directly against the real
 * player: stuck at 12s, one plain `toggle`, playing at 16s.
 *
 * That is the whole fault, and every release before this one mistook it for a
 * dead track: 0.1.0-35's rescue answered a stalled start with `next`, so it
 * SKIPPED A PERFECTLY GOOD SONG — velle's entry 1 reports `public`, plays on
 * its own in two seconds, and was skipped anyway — and then waited another
 * twelve seconds to do it again. Thirty-four seconds from press to sound, and
 * two songs missing from the front of the queue.
 *
 * So the recovery is now ASK AGAIN before ASK ELSEWHERE: re-toggle the same
 * track, up to MUSIC_NUDGE_MAX times, and only once that has failed is a skip
 * even considered — by which point the track really is the suspect.
 *
 * ⚠ AND A NUDGE THAT LANDS LATE IS UNDONE. `toggle` from `playing` is PAUSE,
 * and the settle above samples once a second, so there is a narrow window
 * where a track starts just as the nudge goes out. Left alone that is a button
 * that stops the music it was sent to start — the exact fault 0.1.0-29 shipped
 * — so a `paused` player found immediately after a nudge is toggled back.
 */
static bool music_nudge(void)
{
	char state[32];
	music_read(state, sizeof(state), NULL, 0, NULL, 0, NULL);
	if (!strcmp(state, "playing"))
		return true;
	if (!state[0])
		return true;		/* the player has gone; nothing to do */

	music_cmd("toggle");

	if (!music_settled(MUSIC_NUDGE_MS))
		return false;

	/* ⚠ Did that PAUSE it rather than start it? music_settled treats
	 * `paused` as "leave this alone", which is right for a person pressing
	 * pause and wrong for a pause this function just caused. */
	music_read(state, sizeof(state), NULL, 0, NULL, 0, NULL);
	if (!strcmp(state, "paused")) {
		music_cmd("toggle");
		return music_settled(MUSIC_NUDGE_MS);
	}
	return true;
}

static void music_start_insist(void)
{
	int lock = -1;
	if (!music_insist_lock(&lock))
		return;

	if (!music_settled(MUSIC_SETTLE_MS)) {
		/* ⚠ THE SAME TRACK, FIRST. See music_nudge(): a stalled start
		 * is a lost toggle far more often than it is a dead song, and
		 * skipping is not reversible from a sofa. */
		bool going = false;
		for (int i = 0; i < MUSIC_NUDGE_MAX && !going; i++)
			going = music_nudge();
		if (going) {
			if (lock >= 0)
				close(lock);
			return;
		}

		int skipped = 0;
		for (int i = 0; i < MUSIC_SKIP_MAX && !skipped; i++) {
			/* ⚠ `next` MOVES THE TRACK BUT DOES NOT START IT —
			 * measured: after a skip the player sits at the new
			 * index, still `stopped`, and `play` (which is resume)
			 * does nothing from there. `toggle` is what begins
			 * playback from a standing start, exactly as it is
			 * everywhere else in this file. */
			music_cmd("next");

			/* ⚠ A BEAT BETWEEN THEM. `next` and `toggle` are two
			 * commands over one socket and the index moves on
			 * cliamp's own timing; sent back to back, the toggle
			 * can land on the track being left rather than the one
			 * being moved to. */
			struct timespec beat = { 1, 0 };
			nanosleep(&beat, NULL);

			music_cmd("toggle");
			if (music_settled(MUSIC_RETRY_MS))
				skipped = i + 1;
		}

		if (skipped)
			fprintf(stderr, "syn-arcade: skipped %d track%s that "
					"would not play\n", skipped,
				skipped == 1 ? "" : "s");
		else
			fputs("syn-arcade: nothing at the front of the queue "
			      "would play — the tracks may be unavailable in "
			      "this country\n", stderr);
	}

	if (lock >= 0)
		close(lock);
}

/* ── the Plex library, reached without a Plex client ─────────────────────── */

/*
 * Plex is where this machine's music actually is, and until now big screen
 * mode could only offer the SERVER — a tile that opens Plex's web interface in
 * a browser, which on a television is a pointer, a login and somebody else's
 * idea of a remote control.
 *
 * The library itself is an ordinary HTTP API, and cliamp is already configured
 * for it: `cliamp setup` writes the server and a token into
 * ~/.config/cliamp/config.toml, which is the one place either program has to
 * look. Nothing here asks for a password, stores one, or has a second idea of
 * where the server is.
 *
 * ⚠ THE TOKEN GOES IN A HEADER, not in the URL. Plex accepts either, and both
 * are equally visible in `ps` — but a URL is logged by the server, kept in its
 * own diagnostics, and would end up in this file's caches. A header is not.
 * The one place a token has to be in a URL is a stream handed to cliamp, which
 * has no way to send a header, and that is exactly the case avoided below when
 * the file is readable directly.
 */
static bool plex_conf(char *url, size_t un, char *token, size_t tn)
{
	url[0] = '\0';
	token[0] = '\0';

	char path[SYN_PATH];
	if (!config_path(path, sizeof(path), "cliamp/config.toml"))
		return false;
	char *text = read_file(path);
	if (!text)
		return false;

	bool in_plex = false;
	char *save = NULL;
	for (char *ln = strtok_r(text, "\n", &save); ln;
	     ln = strtok_r(NULL, "\n", &save)) {
		char *t = trim(ln);
		if (*t == '[') {
			in_plex = strncmp(t, "[plex]", 6) == 0;
			continue;
		}
		if (!in_plex || !*t || *t == '#')
			continue;

		char *eq = strchr(t, '=');
		if (!eq)
			continue;
		*eq = '\0';
		char *k = trim(t);
		char *v = trim(eq + 1);

		/* TOML quotes; nothing here needs escape handling, because a
		 * URL and a token contain neither quotes nor backslashes. */
		size_t vl = strlen(v);
		if (vl >= 2 && v[0] == '"' && v[vl - 1] == '"') {
			v[vl - 1] = '\0';
			v++;
		}

		if (strcmp(k, "url") == 0)
			snprintf(url, un, "%s", v);
		else if (strcmp(k, "token") == 0)
			snprintf(token, tn, "%s", v);
	}
	free(text);

	/* A trailing slash on the configured URL would make every path below
	 * double-slashed. Plex tolerates it; nothing else has to. */
	size_t ul = strlen(url);
	while (ul && url[ul - 1] == '/')
		url[--ul] = '\0';

	return url[0] && token[0];
}

/* One GET against the configured server. NULL for every kind of "no". */
static char *plex_get(const char *path)
{
	if (!net_allowed())
		return NULL;

	char base[256], token[192];
	if (!plex_conf(base, sizeof(base), token, sizeof(token)))
		return NULL;

	char url[SYN_PATH], hdr[256];
	snprintf(url, sizeof(url), "%s%s", base, path);
	snprintf(hdr, sizeof(hdr), "X-Plex-Token: %s", token);

	/* Accept the default XML rather than asking for JSON: this file
	 * already has an XML scanner for the news reader, and Plex's XML puts
	 * everything on attributes of one element per row. */
	char *const argv[] = { "curl", "-sS", "--max-time", "8",
			       "-H", hdr, url, NULL };
	return run_capture(argv, 12);
}

/*
 * Which library section holds the music.
 *
 * ⚠ ASKED, never assumed to be section 3 (which it is on this machine). A
 * hard-coded section number is a launcher that shows somebody's films as
 * albums on the one server whose sections were made in a different order.
 */
static bool plex_music_section(char *out, size_t n)
{
	char *xml = plex_get("/library/sections");
	if (!xml)
		return false;

	bool found = false;
	for (const char *p = strstr(xml, "<Directory ");
	     p && !found; p = strstr(p + 1, "<Directory ")) {
		char type[32];
		if (xml_attr(p, "type", type, sizeof(type)) &&
		    strcmp(type, "artist") == 0)
			found = xml_attr(p, "key", out, n);
	}
	free(xml);
	return found;
}

/*
 * Whether the server's own file paths mean anything on THIS machine.
 *
 * A Part carries `file=` — where the track sits on the server's disk — and on
 * this system that is a directory the user can read, because the server IS
 * this machine. Handing cliamp the file rather than an HTTP stream is worth a
 * check: it plays without the round trip, and no URL means no token anywhere.
 *
 * ⚠ The check is on the SERVER, not on the file. A remote Plex whose library
 * lives at /mnt/music would otherwise have its paths tested against this
 * machine's /mnt/music — a different disk with the same name, which is the one
 * way this could quietly play the wrong music.
 */
static bool plex_is_local(void)
{
	char base[256], token[192], host[128];
	if (!plex_conf(base, sizeof(base), token, sizeof(token)))
		return false;
	if (!url_host(base, host, sizeof(host)))
		return false;
	return strcmp(host, "localhost") == 0 || addr_is_local(host);
}

/* The albums, one record per row, for the picker to draw. */
static int plex_albums(bool rec)
{
	char sect[32];
	if (!plex_music_section(sect, sizeof(sect))) {
		fputs("syn-arcade: no Plex music library — check the [plex] "
		      "section of ~/.config/cliamp/config.toml, or run "
		      "`cliamp setup`\n", stderr);
		return EX_FAIL;
	}

	char path[128];
	snprintf(path, sizeof(path), "/library/sections/%s/all?type=9", sect);
	char *xml = plex_get(path);
	if (!xml) {
		fputs("syn-arcade: the Plex server did not answer\n", stderr);
		return EX_FAIL;
	}

	if (rec)
		rec_row(4, "id", "name", "artist", "year");

	int n = 0;
	for (const char *p = strstr(xml, "<Directory ");
	     p; p = strstr(p + 1, "<Directory ")) {
		char key[32], title[256], artist[256], year[16];
		if (!xml_attr(p, "ratingKey", key, sizeof(key)) ||
		    !xml_attr(p, "title", title, sizeof(title)))
			continue;
		if (!xml_attr(p, "parentTitle", artist, sizeof(artist)))
			artist[0] = '\0';
		if (!xml_attr(p, "year", year, sizeof(year)))
			year[0] = '\0';

		if (rec)
			rec_row(4, key, title, artist, year);
		else
			printf("%-8s %-30s %s%s%s\n", key, artist, title,
			       year[0] ? "  " : "", year);
		n++;
	}
	free(xml);

	if (!n && !rec)
		puts("no albums in the Plex music library");
	return n ? EX_OK : EX_EMPTY;
}

/*
 * Play one album.
 *
 * ⚠ THE PLAYER IS RESTARTED FIRST, and that is not heavy-handedness. cliamp's
 * `queue` APPENDS — there is no verb that clears a queue — so picking a second
 * album without a restart puts it behind the first one and the television goes
 * on playing what it was already playing. From four metres that is a button
 * that did nothing. A restart makes the queue exactly the album that was
 * chosen, every time.
 */
static int plex_play_album(const char *key)
{
	char path[128];
	snprintf(path, sizeof(path), "/library/metadata/%s/children", key);
	char *xml = plex_get(path);
	if (!xml) {
		fputs("syn-arcade: the Plex server did not answer\n", stderr);
		return EX_FAIL;
	}

	char base[256], token[192];
	plex_conf(base, sizeof(base), token, sizeof(token));
	bool local = plex_is_local();

	if (!music_restart()) {
		free(xml);
		fputs("syn-arcade: cliamp did not come up\n", stderr);
		return EX_FAIL;
	}

	/* Truncated, not appended: this map describes the queue, and the queue
	 * was just replaced. */
	char cache[SYN_PATH];
	FILE *titles = NULL;
	if (music_titles_path(cache, sizeof(cache)) && mkdir_parents(cache) == 0)
		titles = fopen(cache, "w");

	int queued = 0;
	for (const char *p = strstr(xml, "<Track ");
	     p; p = strstr(p + 1, "<Track ")) {
		char title[256], artist[256];
		if (!xml_attr(p, "title", title, sizeof(title)))
			continue;
		if (!xml_attr(p, "grandparentTitle", artist, sizeof(artist)))
			artist[0] = '\0';

		/* ⚠ BOUNDED BY THE NEXT TRACK. A Part is nested inside the
		 * Track it belongs to, and an unbounded search would give
		 * every track after the last one with no media the file of a
		 * track further down — thirteen rows pointing at eleven
		 * songs, in the right order, with two wrong. */
		const char *next = strstr(p + 1, "<Track ");
		const char *part = strstr(p, "<Part ");
		if (!part || (next && part > next))
			continue;

		char file[SYN_PATH], pkey[256];
		bool have_file = xml_attr(part, "file", file, sizeof(file));
		bool have_key = xml_attr(part, "key", pkey, sizeof(pkey));

		char what[SYN_PATH];
		if (local && have_file && access(file, R_OK) == 0) {
			snprintf(what, sizeof(what), "%s", file);
		} else if (have_key) {
			snprintf(what, sizeof(what), "%s%s?X-Plex-Token=%s",
				 base, pkey, token);
		} else {
			continue;
		}

		char *out = run_capture((char *const[]){ "cliamp", "queue",
							 what, NULL }, 5);
		free(out);

		/* What the television will call it. cliamp reports a queued
		 * track's path as its title; this is the only place that knows
		 * the real one. ⚠ Keyed through music_key(), which is where
		 * the rule about the token in the query lives. */
		char keyed[SYN_PATH];
		music_key(what, keyed, sizeof(keyed));

		char shown[SYN_PATH];
		if (artist[0])
			snprintf(shown, sizeof(shown), "%s — %s", artist, title);
		else
			snprintf(shown, sizeof(shown), "%s", title);
		music_title_remember(titles, keyed, shown);
		queued++;
	}
	free(xml);
	if (titles)
		fclose(titles);

	if (!queued) {
		fputs("syn-arcade: that album has no playable tracks\n", stderr);
		return EX_EMPTY;
	}

	/* ⚠ THE RATING KEY, NOT THE TRACK URLS. Those carry the Plex token —
	 * see music_last_remember() — and the key is what would be asked for
	 * again anyway. Written where the queue really filled, so an album
	 * that turned out to have nothing playable is not what the tile
	 * resumes. */
	music_last_remember("plex", key);

	/* `toggle`, not `play` — see big_music: a player that has just started
	 * is `stopped`, and resume does nothing from there. */
	music_cmd("toggle");
	printf("queued %d track%s\n", queued, queued == 1 ? "" : "s");
	return EX_OK;
}

/* ── local files ─────────────────────────────────────────────────────────── */

/*
 * The music directory, which on a lot of machines is not there at all.
 *
 * XDG_MUSIC_DIR out of user-dirs.dirs first, because that is where a desktop
 * records the answer, and ~/Music after it. ⚠ user-dirs.dirs writes the value
 * as `XDG_MUSIC_DIR="$HOME/Music"` — a shell expression, and the $HOME in it
 * is literal text in that file.
 */
static bool music_dir(char *out, size_t n)
{
	out[0] = '\0';

	char conf[SYN_PATH];
	if (config_path(conf, sizeof(conf), "user-dirs.dirs")) {
		char *text = read_file(conf);
		if (text) {
			const char *p = strstr(text, "XDG_MUSIC_DIR=");
			if (p) {
				p += strlen("XDG_MUSIC_DIR=");
				if (*p == '"')
					p++;
				char val[SYN_PATH];
				size_t w = 0;
				while (*p && *p != '"' && *p != '\n' &&
				       w + 1 < sizeof(val))
					val[w++] = *p++;
				val[w] = '\0';

				if (strncmp(val, "$HOME/", 6) == 0)
					home_path(out, n, val + 6);
				else if (val[0] == '/')
					snprintf(out, n, "%s", val);
			}
			free(text);
		}
	}

	if (!out[0])
		home_path(out, n, "Music");

	struct stat st;
	return out[0] && stat(out, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool is_audio(const char *name)
{
	static const char *const ext[] = { ".flac", ".mp3", ".ogg", ".opus",
					   ".m4a", ".wav", ".aac", ".wma",
					   NULL };
	const char *dot = strrchr(name, '.');
	if (!dot)
		return false;
	for (int i = 0; ext[i]; i++)
		if (strcasecmp(dot, ext[i]) == 0)
			return true;
	return false;
}

#define LOCAL_MAX 300

/* Every audio file under `dir`, depth first, capped. */
static void local_scan(const char *dir, char (*out)[SYN_PATH], int *n,
		       int depth)
{
	if (*n >= LOCAL_MAX || depth > 6)
		return;

	DIR *d = opendir(dir);
	if (!d)
		return;

	struct dirent *e;
	while ((e = readdir(d)) && *n < LOCAL_MAX) {
		if (e->d_name[0] == '.')
			continue;

		char path[SYN_PATH];
		if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >=
		    (int)sizeof(path))
			continue;

		struct stat st;
		if (stat(path, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
			local_scan(path, out, n, depth + 1);
		else if (S_ISREG(st.st_mode) && is_audio(e->d_name))
			snprintf(out[(*n)++], SYN_PATH, "%s", path);
	}
	closedir(d);
}

static int path_cmp(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

/*
 * Fill the queue from the music directory.
 *
 * ⚠ SORTED. readdir answers in whatever order the filesystem feels like, and
 * an album queued in hash order is an album nobody recognises. Sorting by path
 * puts a disc in track order, because that is what the numbers in the file
 * names are for.
 */
static int local_queue(void)
{
	char dir[SYN_PATH];
	if (!music_dir(dir, sizeof(dir))) {
		fprintf(stderr, "syn-arcade: no music directory — there is "
				"nothing at %s\n", dir);
		return EX_EMPTY;
	}

	static char found[LOCAL_MAX][SYN_PATH];
	int n = 0;
	local_scan(dir, found, &n, 0);
	if (!n) {
		fprintf(stderr, "syn-arcade: no music files under %s\n", dir);
		return EX_EMPTY;
	}
	qsort(found, (size_t)n, SYN_PATH, path_cmp);

	if (!music_restart()) {
		fputs("syn-arcade: cliamp did not come up\n", stderr);
		return EX_FAIL;
	}

	char cache[SYN_PATH];
	FILE *titles = NULL;
	if (music_titles_path(cache, sizeof(cache)) && mkdir_parents(cache) == 0)
		titles = fopen(cache, "w");

	for (int i = 0; i < n; i++) {
		char *out = run_capture((char *const[]){ "cliamp", "queue",
							 found[i], NULL }, 5);
		free(out);

		/* The file name without its extension is a better title than
		 * the path, and it is all there is: nothing here reads tags,
		 * and neither does cliamp's queue. */
		const char *base = strrchr(found[i], '/');
		char shown[SYN_PATH];
		snprintf(shown, sizeof(shown), "%s", base ? base + 1 : found[i]);
		char *dot = strrchr(shown, '.');
		if (dot)
			*dot = '\0';
		music_title_remember(titles, found[i], shown);
	}
	if (titles)
		fclose(titles);

	/* The directory, which is the whole of what was asked for here — this
	 * source has no smaller unit than "the music on this machine". */
	music_last_remember("local", dir);

	music_cmd("toggle");
	printf("queued %d track%s from %s\n", n, n == 1 ? "" : "s", dir);
	return EX_OK;
}

/* ── YouTube Music, without an account ───────────────────────────────────── */

/*
 * Stations, so YouTube Music PLAYS THROUGH THE TELEVISION rather than opening a
 * music player somebody then has to drive with a d-pad.
 *
 * ── ⚠ AND IT NEEDS NO CREDENTIALS, WHICH IS THE WHOLE REASON THIS EXISTS ────
 *
 * The row used to dead-end at cliamp's own interface because browsing YouTube
 * Music goes through the Data API, and cliamp v1.63.2 has no credentials to
 * reach it with — see ytmusic_credentialed(). That is true of BROWSING and of
 * nothing else. Measured on this machine, with no client ID anywhere:
 *
 *     yt-dlp --flat-playlist --print "%(title)s" --print "%(url)s" \
 *            "ytsearch3:daft punk one more time"
 *     → three titles and three watch URLs, exit 0
 *
 *     cliamp queue "https://www.youtube.com/watch?v=…"
 *     → exit 0, and the player's `total` went 11 → 12
 *
 * So the two halves a television needs — find something, and play it — are both
 * reachable without an account. resolve.go sends every YouTube URL through
 * yt-dlp and the native client, neither of which ever sees an OAuth token. The
 * account buys SEARCH INSIDE CLIAMP'S TUI, and this file does not need it.
 *
 * ── what a station IS ───────────────────────────────────────────────────────
 *
 * One line of ~/.config/syn-arcade/ytmusic.list: a URL, a tab, and what to call
 * it. A playlist, an album, a mix or a single track — anything yt-dlp can
 * enumerate, which is the same question as "can cliamp play it".
 *
 * ⚠ EXPANDED HERE RATHER THAN HANDED OVER WHOLE, and the reason is the title.
 * cliamp reports a queued track's title as its PATH (see music_read), so a
 * playlist queued as one URL plays perfectly and the television says
 * `https://music.youtube.com/playlist?list=…` for twenty minutes. Enumerating
 * it means every track has a name to remember, exactly as the Plex and local
 * paths already do.
 */
#define YT_MAX	   60		/* tracks queued from one station           */
#define YT_FIND	   12		/* results a search comes back with         */
#define YT_TIMEOUT 25		/* seconds — a search crosses the internet  */
#define YT_VERIFY      8	/* head tracks ASKED about before queueing  */
#define YT_VERIFY_SECS 20	/* one of those questions, at the outside   */

/* Declared rather than moved: it belongs beside the source picker below, with
 * the rest of what reads cliamp's own config — but the stations list has to
 * ask it whether the OAuth row is still worth offering. */
static bool ytmusic_credentialed(void);
static int term_run_and_hold(const char *command);

/*
 * Is there anybody to ask?
 *
 * ⚠ THE TWO VERBS BELOW READ FROM STDIN, AND THE SHELL HAS NONE TO GIVE. A
 * tile is launched as a plain process by the television — no terminal, no tty
 * — so `fgets` there returns immediately on EOF and an interactive prompt
 * becomes a command that flashes and exits having done nothing. That is the
 * exact shape of a dead button.
 *
 * So the rule is the honest one: if there is no terminal to be read from, GET
 * one, and run the same command inside it. `keys: "1"` on the shell's side
 * points the on-screen keyboard at that terminal, which is how everything on
 * this system that needs typing from a sofa already works.
 *
 * ⚠ It cannot loop: the command inside the terminal HAS a tty, so it takes the
 * interactive branch.
 */
static bool can_be_asked(void)
{
	return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

static bool yt_stations_path(char *buf, size_t n)
{
	return config_path(buf, n, "syn-arcade/ytmusic.list");
}

/*
 * ── signing in, and why it is a BROWSER rather than a Google Cloud project ──
 *
 * "Log in so I can use my own playlists" has two possible answers on this
 * system and they are wildly different amounts of work:
 *
 *   OAuth client   what cliamp's own wizard asks for. Unlocks SEARCH AND
 *                  BROWSE inside cliamp's TUI. Requires somebody to create a
 *                  Google Cloud project, enable the YouTube Data API and copy
 *                  a client id and secret out of a web console — and then
 *                  leaves them driving a terminal interface from a sofa.
 *   browser cookies  what yt-dlp takes. Requires being signed in to YouTube in
 *                  a browser, which most people already are. With them yt-dlp
 *                  enumerates PRIVATE playlists and Liked Music, which means
 *                  this package can list somebody's own playlists as stations
 *                  and play them with a d-pad.
 *
 * Both are offered — the OAuth route is still one row away, and `cliamp setup`
 * is still the thing that does it — but THIS is the one that puts a person's
 * playlists on their television, so it is the one the row leads with.
 *
 * ⚠ NOTHING IS COPIED OUT OF THE BROWSER BY THIS PACKAGE. The name of a
 * browser is written to big.conf and handed to yt-dlp, which reads the cookie
 * store itself, in its own process, for the length of one command. No cookie,
 * token or password is read, logged or cached here — the titles cache is the
 * only thing this writes, and it holds names and URLs.
 *
 * ⚠ AND THE STORE IS LOCKED WHILE THE BROWSER RUNS on the Chromium family.
 * yt-dlp copies it rather than failing, but a browser that has never been
 * signed in is a silent nothing: measured here, Vivaldi with no YouTube
 * session yielded SEVEN cookies, decrypted perfectly, and a 401. Which is why
 * `yt login` VERIFIES rather than just writing the setting down.
 */
static bool yt_cookie_browser(char *buf, size_t n)
{
	return big_conf_get("yt_cookies", buf, n) && buf[0];
}

/* The browsers yt-dlp can read, in the order this machine is likely to have
 * meant. ⚠ Not a guess at what is installed — `yt login` checks that. */
static const char *const YT_BROWSERS[] = {
	"firefox", "vivaldi", "chromium", "chrome", "brave", "edge", "opera",
	"safari", "whale", NULL
};

static bool yt_browser_known(const char *name)
{
	for (int i = 0; YT_BROWSERS[i]; i++)
		if (strcmp(YT_BROWSERS[i], name) == 0)
			return true;
	return false;
}

/*
 * Ask yt-dlp to enumerate something, into two parallel arrays.
 *
 * `spec` is either a URL or `ytsearchN:<words>` — one code path, because
 * "expand this playlist" and "find me some songs" are the same question to
 * yt-dlp and should not be two parsers here.
 *
 * ⚠ TWO `--print` FLAGS RATHER THAN ONE WITH A SEPARATOR IN IT. A title is
 * somebody else's text and may contain any delimiter that could be chosen —
 * tabs and pipes included. Two flags print two LINES per result, so the split
 * is the newline yt-dlp puts there itself and no title can forge it.
 *
 * ⚠ `--flat-playlist`, so a playlist is listed rather than each entry being
 * fetched. Without it this is one HTTP round trip per track and a station takes
 * minutes to start.
 *
 * ⚠ `%(webpage_url)s`, NOT `%(url)s`, and the difference is silent. `url` is
 * only filled in for entries of a FLAT PLAYLIST; asked of a single video it
 * prints the literal string `NA`. Measured: `yt add <a watch URL>` resolved its
 * title perfectly and stored `NA` beside it, which is a station that lists
 * correctly and plays nothing. `webpage_url` is right for both shapes.
 *
 * ⚠ AND `--playlist-end`, because the cap has to be at the SOURCE. A YouTube
 * mix (`list=RD<videoid>` — the endless station a track seeds, and the closest
 * thing here to radio) enumerates 661 entries; taking the first sixty AFTER
 * fetching them all is sixty tracks' worth of music behind ten times the wait.
 */
static int yt_enumerate(const char *spec, char titles[][256],
			char urls[][SYN_PATH], int max)
{
	/* ⚠ The THIRD thing in this file that touches the internet, and so the
	 * third that has to ask. A suite that reached YouTube would pass or
	 * fail on whether the building had DNS that morning — and would be
	 * asserting on somebody else's search results. */
	if (!net_allowed())
		return 0;

	char end[16];
	snprintf(end, sizeof(end), "%d", max);

	/* ⚠ THE COOKIES GO ON EVERY ENUMERATION, not only on the one that
	 * lists somebody's library. A private playlist is private at PLAY time
	 * too: without them a station that lists perfectly resolves to nothing
	 * when it is pressed, which is the worst shape this bug could take. */
	char browser[64];
	bool signed_in = yt_cookie_browser(browser, sizeof(browser));

	char *out = signed_in
		? run_capture((char *const[]){
			(char *)"yt-dlp", (char *)"--flat-playlist",
			(char *)"--no-warnings", (char *)"--ignore-errors",
			(char *)"--cookies-from-browser", browser,
			(char *)"--playlist-end", end,
			(char *)"--print", (char *)"%(title)s",
			(char *)"--print", (char *)"%(webpage_url)s",
			(char *)spec, NULL }, YT_TIMEOUT)
		: run_capture((char *const[]){
			(char *)"yt-dlp", (char *)"--flat-playlist",
			(char *)"--no-warnings", (char *)"--ignore-errors",
			(char *)"--playlist-end", end,
			(char *)"--print", (char *)"%(title)s",
			(char *)"--print", (char *)"%(webpage_url)s",
			(char *)spec, NULL }, YT_TIMEOUT);
	if (!out)
		return 0;

	int n = 0;
	bool want_url = false;
	char *save = NULL;
	for (char *ln = strtok_r(out, "\n", &save); ln && n < max;
	     ln = strtok_r(NULL, "\n", &save)) {
		char *t = trim(ln);
		if (!*t)
			continue;

		if (!want_url) {
			snprintf(titles[n], 256, "%s", t);
			want_url = true;
			continue;
		}

		/* ⚠ A URL IS THE ONLY THING THAT COUNTS AS THE SECOND LINE.
		 * yt-dlp writes its own notes to stdout on occasion, and a
		 * pair knocked out of step turns every row after it into a
		 * title pointing at somebody else's song. */
		if (strncmp(t, "http", 4) != 0) {
			snprintf(titles[n], 256, "%s", t);
			continue;
		}
		snprintf(urls[n], SYN_PATH, "%s", t);
		want_url = false;
		n++;
	}
	free(out);
	return n;
}

/*
 * The stations, as rows.
 *
 * ⚠ A MISSING FILE IS NOT AN ERROR, it is a machine nobody has added a station
 * to yet — which is every machine on the day it is installed. It says how to
 * add one rather than answering an empty list, because an empty panel on a
 * television is indistinguishable from a broken button.
 */
/*
 * ── will this one actually PLAY? ───────────────────────────────────────────
 *
 * ⚠ THE QUESTION THE ENUMERATION CANNOT ANSWER, and the reason the Music tile
 * looked broken for three releases.
 *
 * `--flat-playlist` is what makes reading a station fast enough to be a button
 * — one request for the whole list instead of one per track — and its entries
 * carry a real title, a real duration and a real view count for videos that
 * answer "Video unavailable" the moment anything tries to play them.
 * `%(availability)s` is `null` for EVERY entry in that listing, available or
 * not, so there is nothing in it to filter on. Measured on velle's own
 * playlist: entry 0 is dead, entries 1 and 2 are fine, and all three look
 * identical on the way in.
 *
 * Asked about ONE video, though, yt-dlp answers immediately and definitively:
 *
 *     yt-dlp --simulate --print "%(id)s" <dead>   → rc 1 in 2s
 *     yt-dlp --simulate --print "%(id)s" <good>   → rc 0 in 1s
 *
 * and run_capture() already turns a non-zero exit into NULL. So this costs
 * about a second and removes the whole problem at its source, rather than
 * discovering it fifteen seconds later with the television silent.
 *
 * ⚠ WITH THE SESSION, for the same reason every other enumeration carries it:
 * a members-only or otherwise gated track is playable for the signed-in person
 * and not for anybody else, and asking without the cookies would drop tracks
 * that would have played perfectly.
 */
static bool yt_playable(const char *url)
{
	char browser[64];
	bool signed_in = yt_cookie_browser(browser, sizeof(browser));

	char *out = signed_in
		? run_capture((char *const[]){
			(char *)"yt-dlp", (char *)"--simulate",
			(char *)"--no-warnings",
			(char *)"--cookies-from-browser", browser,
			(char *)"--print", (char *)"%(id)s",
			(char *)url, NULL }, YT_VERIFY_SECS)
		: run_capture((char *const[]){
			(char *)"yt-dlp", (char *)"--simulate",
			(char *)"--no-warnings",
			(char *)"--print", (char *)"%(id)s",
			(char *)url, NULL }, YT_VERIFY_SECS);

	bool ok = out && *trim(out);
	free(out);
	return ok;
}

static int yt_stations(bool rec)
{
	char path[SYN_PATH];
	char *text = NULL;
	if (yt_stations_path(path, sizeof(path)))
		text = read_file(path);

	if (rec)
		rec_row(4, "id", "name", "note", "kind");

	/*
	 * ── the rows that are ERRANDS rather than stations ─────────────────
	 *
	 * ⚠ THE SHELL DOES NOT DECIDE WHICH OF THESE EXIST, for the same
	 * reason it does not decide what a source's action is: whether this
	 * machine has a YouTube session, and whether cliamp has an OAuth
	 * client, are facts about the machine. A copy of that reasoning in QML
	 * is a copy that stops being true — and it is how the sign-in route
	 * disappeared from the television in 0.1.0-29, when the C answer
	 * changed and nothing in the shell noticed.
	 *
	 * `kind` is what the shell dispatches on, so a row added here needs no
	 * change there unless it needs a NEW kind of handling.
	 */
	char browser[64];
	bool signed_in = yt_cookie_browser(browser, sizeof(browser));

	if (rec) {
		/* ⚠ THE NOTE HAS TO BE TRUE ON BOTH SURFACES. This list is
		 * the television's AND the desktop widget's — same rows, same
		 * `kind`, one C answer — and it used to say "type a search on
		 * the television" to somebody looking at a card on their
		 * wallpaper. What actually happens is the same on both: a
		 * terminal comes up to type into, which on the television is
		 * the one the on-screen keyboard points at. See yt_find(). */
		rec_row(4, "find", "Search…",
			"opens a terminal to type in", "action");

		if (signed_in)
			rec_row(4, "mine", "Your playlists", browser, "action");
		else
			rec_row(4, "login", "Sign in…",
				"use your own playlists and Liked Music",
				"action");

		/* ⚠ THE OAUTH ROUTE, WHICH IS A DIFFERENT THING AND STILL
		 * REAL. Signing in above is browser cookies and gets somebody
		 * their playlists HERE; this is cliamp's own search inside its
		 * own interface, and it needs a Google Cloud client. Offered
		 * last and only when it is missing, because it is the long way
		 * round and most people will never want it. */
		if (!ytmusic_credentialed())
			rec_row(4, "setup", "Search inside cliamp…",
				"needs a Google OAuth client", "action");
	}

	int n = 0;
	if (text) {
		char *save = NULL;
		for (char *ln = strtok_r(text, "\n", &save); ln;
		     ln = strtok_r(NULL, "\n", &save)) {
			char *t = trim(ln);
			if (!*t || *t == '#')
				continue;

			/* URL first, name after a tab. The URL cannot contain
			 * one and a name may contain anything else, which is
			 * why the split is this way round. */
			char *tab = strchr(t, '\t');
			char *name = NULL;
			if (tab) {
				*tab = '\0';
				name = trim(tab + 1);
			}

			char id[16];
			snprintf(id, sizeof(id), "%d", n + 1);
			if (rec)
				rec_row(4, id, name && *name ? name : t, t,
					"station");
			else
				printf("%-4s %-40s %s\n", id,
				       name && *name ? name : t, t);
			n++;
		}
		free(text);
	}

	if (!n && !rec)
		printf("no YouTube Music stations yet — add one with\n"
		       "    syn-arcade big music yt add <url>\n"
		       "where <url> is a playlist, album, mix or track.\n");
	return n ? EX_OK : EX_EMPTY;
}

/* The URL of station `id`, which is its 1-based position in the file. */
static bool yt_station_url(const char *id, char *out, size_t n)
{
	int want = id ? atoi(id) : 0;
	if (want < 1)
		return false;

	char path[SYN_PATH];
	if (!yt_stations_path(path, sizeof(path)))
		return false;
	char *text = read_file(path);
	if (!text)
		return false;

	bool found = false;
	int i = 0;
	char *save = NULL;
	for (char *ln = strtok_r(text, "\n", &save); ln && !found;
	     ln = strtok_r(NULL, "\n", &save)) {
		char *t = trim(ln);
		if (!*t || *t == '#')
			continue;
		char *tab = strchr(t, '\t');
		if (tab)
			*tab = '\0';
		if (++i == want) {
			snprintf(out, n, "%s", trim(t));
			found = true;
		}
	}
	free(text);
	return found;
}

/*
 * Play something: a station id, or a URL directly.
 *
 * ⚠ THE PLAYER IS RESTARTED FIRST, for the reason plex_play_album gives in
 * full: cliamp's `queue` APPENDS and there is no verb that clears a queue, so
 * without this a second station goes behind the first and the television goes
 * on playing what it was already playing. From four metres that is a button
 * that did nothing.
 */
static int yt_play(const char *what)
{
	char url[SYN_PATH];
	if (!what || !*what) {
		fputs("syn-arcade: nothing to play\n", stderr);
		return EX_USAGE;
	}
	if (strncmp(what, "http", 4) == 0)
		snprintf(url, sizeof(url), "%s", what);
	else if (!yt_station_url(what, url, sizeof(url))) {
		fprintf(stderr, "syn-arcade: no station %s — `big music yt` "
				"lists them\n", what);
		return EX_USAGE;
	}

	if (!have("yt-dlp")) {
		fputs("syn-arcade: yt-dlp is not installed, so nothing on "
		      "YouTube can be resolved — synpkg install yt-dlp\n",
		      stderr);
		return EX_FAIL;
	}

	static char titles[YT_MAX][256];
	static char urls[YT_MAX][SYN_PATH];
	int n = yt_enumerate(url, titles, urls, YT_MAX);
	if (!n) {
		fputs("syn-arcade: nothing playable came back from that URL\n",
		      stderr);
		return EX_EMPTY;
	}

	/*
	 * ⚠ THE FRONT OF THE QUEUE IS ASKED ABOUT BEFORE IT IS PLAYED, and
	 * this is the fix the last two releases were circling.
	 *
	 * A dead track at position 0 leaves cliamp `stopped` for ever — no
	 * skip, no error, nothing on any stream — so the television answered a
	 * press with silence. 0.1.0-35 rescued that AFTERWARDS, waiting out a
	 * fifteen-second settle before skipping, and measured on velle's own
	 * playlist that came to THIRTY-FOUR SECONDS and two skipped tracks
	 * before a note was heard. From four metres, thirty-four seconds of
	 * nothing is a button that does not work — which is exactly how it was
	 * reported, twice.
	 *
	 * ⚠ AND ONE OF THOSE TWO SKIPS WAS A GOOD TRACK. Asked on its own,
	 * entry 1 of that playlist reports `public` and plays in two seconds;
	 * the rescue skipped it anyway. Waiting for a symptom and then guessing
	 * is worse than asking, and this is the asking.
	 *
	 * ⚠ BOUNDED, IN BOTH DIRECTIONS. At most YT_VERIFY questions, because a
	 * station whose first eight tracks are all dead is not something to
	 * spend a minute proving — the queue is filled from wherever this got
	 * to and music_start_insist() is still behind it. And it never runs
	 * past the END of the list: `head < n` is what stops an all-dead
	 * playlist queueing nothing at all.
	 */
	int head = 0;
	while (head < n && head < YT_VERIFY && !yt_playable(urls[head]))
		head++;

	if (head >= n) {
		fputs("syn-arcade: nothing in that list will play — the "
		      "tracks may be unavailable in this country\n", stderr);
		return EX_EMPTY;
	}
	if (head)
		fprintf(stderr, "syn-arcade: %d track%s at the front of that "
				"list will not play — starting past %s\n",
			head, head == 1 ? "" : "s",
			head == 1 ? "it" : "them");

	/*
	 * ⚠ THE SOURCE IS SET FIRST, AND IT IS NOT BOOKKEEPING — IT IS WHAT
	 * MAKES THE QUEUE EMPTY.
	 *
	 * `--provider` is a start-up flag, so the restart below comes up on
	 * whatever `music_source` says. Measured on this machine, on a fresh
	 * player: `--provider radio` arrives with ELEVEN stations already
	 * queued and `--provider ytmusic` with nothing at all. So a station
	 * played while the setting still said radio queued sixty YouTube
	 * tracks at positions 12..71 and `toggle` played Lofi Stream — the
	 * press worked perfectly and the television played internet radio.
	 *
	 * Writing it is also the honest thing: playing a YouTube Music station
	 * IS the machine's music coming from YouTube Music, and the picker
	 * would say so the moment it was next opened either way.
	 */
	const struct source *yt = source_by_id("ytmusic");
	if (yt && music_source() != yt)
		big_conf_set("music_source", yt->id);

	if (!music_restart()) {
		fputs("syn-arcade: cliamp did not come up\n", stderr);
		return EX_FAIL;
	}

	/* Truncated, not appended: this map describes the queue, and the queue
	 * was just replaced. Same contract as the Plex path. */
	char cache[SYN_PATH];
	FILE *tf = NULL;
	if (music_titles_path(cache, sizeof(cache)) && mkdir_parents(cache) == 0)
		tf = fopen(cache, "w");

	/*
	 * ⚠ THE FIRST TRACK IS QUEUED AND STARTED BEFORE THE REST ARE QUEUED,
	 * and on a television that is the difference between a button that
	 * works and a button that appears not to.
	 *
	 * A station is up to YT_MAX tracks and each one is its own `cliamp
	 * queue` — a process apiece, because there is no verb that takes a
	 * list. Queueing the lot first means a quarter of a minute of silence
	 * after the press, which from four metres is indistinguishable from
	 * nothing having happened; the natural response is to press it again,
	 * and now two stations are loading. Started first, the music is playing
	 * while the rest of the queue fills in behind it.
	 *
	 * ⚠ And its title is remembered BEFORE the toggle, so the first thing
	 * Now Playing says is the track rather than the URL.
	 */
	/* ⚠ FROM `head`, NOT FROM ZERO. The tracks before it were asked and
	 * said no; queueing them anyway would put the dead one back at
	 * position 0 and undo the whole check above. */
	int queued = 0;
	for (int i = head; i < n; i++) {
		char *out = run_capture((char *const[]){
			(char *)"cliamp", (char *)"queue", urls[i], NULL }, 15);
		free(out);

		char keyed[SYN_PATH];
		music_key(urls[i], keyed, sizeof(keyed));
		music_title_remember(tf, keyed, titles[i]);
		queued++;

		/* `toggle`, not `play` — a player that has just started is
		 * `stopped`, and resume does nothing from there. */
		if (i == head) {
			if (tf)
				fflush(tf);
			music_cmd("toggle");
		}
	}
	if (tf)
		fclose(tf);

	/*
	 * ⚠ AND ASKED AGAIN IF IT DID NOT TAKE. The early `toggle` above is
	 * sent one queue-command after the player came up, and once in three
	 * runs here it landed while cliamp was still resolving the first track:
	 * the toggle did nothing, the remaining fifty-nine tracks queued
	 * perfectly, and the machine sat at `stopped` for as long as it was
	 * left. A station that starts most of the time is the worst kind of
	 * button, because the answer to it is to press it again — which
	 * reloads the whole queue.
	 *
	 * By here the queue is full and several seconds have passed, so this
	 * is the same question with the race gone. ⚠ Conditional, and it has
	 * to be: `toggle` from `playing` is PAUSE, so sending it unconditially
	 * would stop the music it just started.
	 */
	/*
	 * ⚠ AND NOTHING ASKS AGAIN HERE, WHICH IS DELIBERATE AND WAS LEARNED
	 * THE EXPENSIVE WAY.
	 *
	 * The obvious belt-and-braces — read the state once the queue is full,
	 * and toggle again if it is not `playing` — was written, and it turned
	 * a reliable station into one that started about half the time.
	 *
	 * `toggle` from `playing` is PAUSE, and the state LAGS the command:
	 * measured second by second, the queue finishes at t+4s and cliamp
	 * does not report `playing` until t+6s, because it is still resolving
	 * the first track through yt-dlp. So the check ran two seconds early
	 * EVERY time, always saw `stopped`, and always sent a second toggle —
	 * which cancelled the start it was meant to insure. Whether the music
	 * played came down to which of the two toggles cliamp processed first.
	 *
	 * Measured both ways round, four runs each: with the second toggle,
	 * two of four never played; without it, four of four played by t+6s.
	 *
	 * "Not playing yet" is not "did not take". A checker for this would
	 * have to WAIT for the state to settle, and there is nothing for it to
	 * fix once it has.
	 */

	/* ⚠ THE STATION, NOT THE SIXTY URLS IT ENUMERATED TO. A `list=RD…` mix
	 * answers differently every time it is asked — that is what a mix is —
	 * so remembering the tracks would resume a station that no longer
	 * exists, while remembering the station resumes the station. */
	music_last_remember("ytmusic", url);

	printf("queued %d track%s\n", queued, queued == 1 ? "" : "s");

	/* ⚠ AND ONLY NOW IS IT ASKED WHETHER IT REALLY STARTED. This is the
	 * check the note above forbids taking early, and by here the queue is
	 * full and several seconds have passed — see music_start_insist(),
	 * which waits for the settle rather than sampling once. A station
	 * whose first track is region-locked is otherwise a press that queues
	 * fifty-four tracks perfectly and plays silence. */
	music_start_insist();
	return EX_OK;
}

/*
 * Find something, as rows the television can draw.
 *
 * ⚠ NOT A ROW THAT PLAYS BY ITSELF. The results are `id = url`, so choosing one
 * is `big music yt <url>` and goes through exactly the path a station does.
 */
static int yt_search(const char *query, bool rec)
{
	if (!query || !*query) {
		fputs("syn-arcade: big music yt search takes something to "
		      "search for\n", stderr);
		return EX_USAGE;
	}
	if (!have("yt-dlp")) {
		fputs("syn-arcade: yt-dlp is not installed — "
		      "synpkg install yt-dlp\n", stderr);
		return EX_FAIL;
	}

	/* ⚠ NOT A SHELL STRING. The query is somebody's typing and goes into
	 * ONE argv element, so a quote or a semicolon in a song title is a
	 * character in a search rather than something yt-dlp's caller has to
	 * escape. */
	char spec[512];
	snprintf(spec, sizeof(spec), "ytsearch%d:%s", YT_FIND, query);

	static char titles[YT_FIND][256];
	static char urls[YT_FIND][SYN_PATH];
	int n = yt_enumerate(spec, titles, urls, YT_FIND);

	if (rec)
		rec_row(3, "id", "name", "note");
	for (int i = 0; i < n; i++) {
		if (rec)
			rec_row(3, urls[i], titles[i], "");
		else
			printf("%-60s %s\n", titles[i], urls[i]);
	}

	if (!n && !rec)
		puts("nothing came back for that");
	return n ? EX_OK : EX_EMPTY;
}

/*
 * Somebody's own playlists, which is the whole point of signing in.
 *
 * ⚠ ONE URL, AND IT IS THE ACCOUNT'S FEED RATHER THAN A LIBRARY PAGE.
 * music.youtube.com's library is rendered for a browser; `/feed/playlists` is
 * what yt-dlp's youtube:tab extractor is built for and is the same list.
 *
 * ⚠ AND A SIGNED-OUT MACHINE GETS A SENTENCE, NOT AN EMPTY LIST. Without
 * cookies this URL answers 401 and yt_enumerate returns nothing, which drawn
 * on a television is "you have no playlists" — a lie, and one somebody would
 * reasonably act on by making some.
 */
static int yt_mine(bool rec)
{
	char browser[64];
	if (!yt_cookie_browser(browser, sizeof(browser))) {
		if (!rec)
			fputs("syn-arcade: not signed in, so there are no "
			      "playlists to read — `big music yt login`\n",
			      stderr);
		return EX_FAIL;
	}
	if (!have("yt-dlp")) {
		fputs("syn-arcade: yt-dlp is not installed — "
		      "synpkg install yt-dlp\n", stderr);
		return EX_FAIL;
	}

	static char titles[YT_FIND][256];
	static char urls[YT_FIND][SYN_PATH];
	int n = yt_enumerate("https://www.youtube.com/feed/playlists",
			     titles, urls, YT_FIND);

	if (rec)
		rec_row(3, "id", "name", "note");
	for (int i = 0; i < n; i++) {
		if (rec)
			rec_row(3, urls[i], titles[i], "");
		else
			printf("%-50s %s\n", titles[i], urls[i]);
	}

	if (!n && !rec)
		printf("nothing came back from %s's YouTube session.\n"
		       "Sign in to YouTube in that browser and try again — "
		       "`big music yt login` checks it.\n", browser);
	return n ? EX_OK : EX_EMPTY;
}

/*
 * Sign in: remember which browser to take a YouTube session from.
 *
 * ⚠ VERIFIED, NOT MERELY WRITTEN DOWN, and that is the whole value of this
 * verb. Every failure here is silent by nature — a browser that was never
 * signed in decrypts its cookies perfectly and answers 401, and yt-dlp says so
 * on a stderr that a television never shows. Measured on this machine: Vivaldi
 * with no YouTube session gave seven cookies, `v10` every one of them, and
 * nothing at all from the playlists feed.
 *
 * So the setting is written, the account is asked what it has, and what comes
 * back is REPORTED. A person who is told "signed in — 14 playlists" knows it
 * worked; a person who is told "that browser has no YouTube session" knows
 * exactly what to go and do.
 */
static int yt_login(const char *browser)
{
	if (!browser || !*browser) {
		if (!can_be_asked())
			return term_run_and_hold("syn-arcade big music yt login");

		/*
		 * ⚠ NUMBERED, AND INSTALLED FIRST, BECAUSE THE ANSWER IS BEING
		 * TYPED WITH A D-PAD.
		 *
		 * This asked for a browser by NAME and printed an unnumbered
		 * list beside it. Reported immediately, and it was the obvious
		 * thing to do: somebody read a list, typed `1`, and got
		 * "yt-dlp cannot read cookies from '1'". The search picker two
		 * rows away takes a number, every other list on this system
		 * takes a number, and spelling out `vivaldi` on an on-screen
		 * keyboard is exactly the errand the terminal trick exists to
		 * keep short.
		 *
		 * ⚠ The order is what is NUMBERED, so what is installed comes
		 * first: on a machine with one browser the answer is `1`
		 * rather than however far down the fixed list it happened to
		 * sit. A name is still accepted, because from a shell prompt
		 * that is the obvious thing to type.
		 */
		const char *shown[16];
		bool here[16];
		int n = 0;
		for (int pass = 0; pass < 2 && n < 16; pass++)
			for (int i = 0; YT_BROWSERS[i] && n < 16; i++) {
				bool installed = have(YT_BROWSERS[i]);
				if (installed != (pass == 0))
					continue;
				shown[n] = YT_BROWSERS[i];
				here[n] = installed;
				n++;
			}

		puts("Which browser are you signed in to YouTube with?");
		puts("");
		for (int i = 0; i < n; i++)
			printf("  %2d  %s%s\n", i + 1, shown[i],
			       here[i] ? "   · installed here" : "");
		puts("");
		fputs("Number or name (Enter to cancel): ", stdout);
		fflush(stdout);

		char line[64];
		if (!fgets(line, sizeof(line), stdin))
			return EX_FAIL;
		char *t = trim(line);
		if (!*t) {
			puts("nothing changed.");
			return EX_OK;
		}

		/* ⚠ ALL digits, not "starts with one": `chrome` must not be
		 * read as a number, and neither must `2fast`. */
		bool numeric = true;
		for (const char *p = t; *p && numeric; p++)
			numeric = *p >= '0' && *p <= '9';

		if (numeric) {
			int pick = atoi(t);
			if (pick < 1 || pick > n) {
				fprintf(stderr, "syn-arcade: there is no %d on "
						"that list\n", pick);
				return EX_USAGE;
			}
			return yt_login(shown[pick - 1]);
		}
		return yt_login(t);
	}

	if (!yt_browser_known(browser)) {
		fprintf(stderr, "syn-arcade: yt-dlp cannot read cookies from "
				"'%s'\n", browser);
		return EX_USAGE;
	}
	if (!have("yt-dlp")) {
		fputs("syn-arcade: yt-dlp is not installed — "
		      "synpkg install yt-dlp\n", stderr);
		return EX_FAIL;
	}

	/* ⚠ WRITTEN BEFORE THE CHECK, because the check reads it: yt_enumerate
	 * takes the browser from big.conf rather than from an argument, so
	 * that there is one answer to "which session is this package using"
	 * and every path gets the same one. */
	if (big_conf_set("yt_cookies", browser) != EX_OK) {
		fputs("syn-arcade: could not write big.conf\n", stderr);
		return EX_FAIL;
	}

	printf("checking what %s's YouTube session can see…\n", browser);
	fflush(stdout);

	static char titles[YT_FIND][256];
	static char urls[YT_FIND][SYN_PATH];
	int n = yt_enumerate("https://www.youtube.com/feed/playlists",
			     titles, urls, YT_FIND);
	if (!n) {
		/* ⚠ THE SETTING IS TAKEN BACK OUT. Leaving it would put
		 * `--cookies-from-browser` on every enumeration from now on —
		 * slower, and it would keep answering "no playlists" as though
		 * the account were empty. */
		big_conf_set("yt_cookies", "");
		fprintf(stderr,
			"\nsyn-arcade: %s has no YouTube session — nothing came "
			"back.\n"
			"\n"
			"Open %s, sign in at music.youtube.com, and run this "
			"again.\n"
			"You are still signed out, and the stations you "
			"already have still play.\n", browser, browser);
		return EX_FAIL;
	}

	printf("\nsigned in with %s — %d playlist%s:\n\n",
	       browser, n, n == 1 ? "" : "s");
	for (int i = 0; i < n; i++)
		printf("    %s\n", titles[i]);
	printf("\nThey are on the television under Music Source ▸ YouTube "
	       "Music ▸ Your playlists.\n");
	return EX_OK;
}

/*
 * Search and play, from a terminal ON the television.
 *
 * ⚠ THIS IS HOW TYPING HAPPENS HERE, and it is not a workaround — it is the
 * same mechanism the install and sign-in rows already use. The on-screen
 * keyboard types through wtype into whatever holds keyboard focus, and the
 * shell's own surface deliberately does not: a menu that grabbed the keyboard
 * to draw a keyboard would type into itself. So the shell steps aside, a
 * terminal comes up with `keys: "1"`, and the keyboard types into THAT.
 *
 * ⚠ AND IT PLAYS THE CHOICE ITSELF rather than handing an id back. The shell
 * is already out of the way by the time somebody has typed a query; coming
 * back to a menu to press A again would be two interfaces for one errand.
 */
static int yt_find(void)
{
	if (!can_be_asked())
		return term_run_and_hold("syn-arcade big music yt find");

	if (!have("yt-dlp")) {
		fputs("syn-arcade: yt-dlp is not installed — "
		      "synpkg install yt-dlp\n", stderr);
		return EX_FAIL;
	}

	fputs("Search YouTube Music: ", stdout);
	fflush(stdout);

	char query[256];
	if (!fgets(query, sizeof(query), stdin))
		return EX_FAIL;
	char *q = trim(query);
	if (!*q) {
		puts("nothing to search for.");
		return EX_OK;
	}

	char spec[512];
	snprintf(spec, sizeof(spec), "ytsearch%d:%s", YT_FIND, q);

	static char titles[YT_FIND][256];
	static char urls[YT_FIND][SYN_PATH];
	printf("\nsearching…\n\n");
	fflush(stdout);
	int n = yt_enumerate(spec, titles, urls, YT_FIND);
	if (!n) {
		puts("nothing came back for that.");
		return EX_EMPTY;
	}

	for (int i = 0; i < n; i++)
		printf("  %2d  %s\n", i + 1, titles[i]);

	fputs("\nPlay which? (number, `s<number>` to save it as a station, "
	      "Enter to cancel): ", stdout);
	fflush(stdout);

	char line[32];
	if (!fgets(line, sizeof(line), stdin))
		return EX_OK;
	char *t = trim(line);
	if (!*t)
		return EX_OK;

	bool save = (*t == 's' || *t == 'S');
	if (save)
		t++;

	int pick = atoi(t);
	if (pick < 1 || pick > n) {
		fprintf(stderr, "syn-arcade: there is no %d on that list\n",
			pick);
		return EX_USAGE;
	}

	if (save) {
		/* ⚠ SAVED AND THEN PLAYED, in that order: a station somebody
		 * asked to keep is kept even if the playing half then fails
		 * for a reason that has nothing to do with the list. */
		char path[SYN_PATH];
		if (yt_stations_path(path, sizeof(path)) &&
		    mkdir_parents(path) == 0) {
			FILE *f = fopen(path, "a");
			if (f) {
				fprintf(f, "%s\t%s\n", urls[pick - 1],
					titles[pick - 1]);
				fclose(f);
				printf("kept as a station: %s\n",
				       titles[pick - 1]);
			}
		}
	}

	printf("\nplaying %s…\n", titles[pick - 1]);
	return yt_play(urls[pick - 1]);
}

/*
 * Keep one.
 *
 * ⚠ THE NAME IS RESOLVED RATHER THAN ASKED FOR. A station whose name has to be
 * typed is a station nobody adds, and yt-dlp already knows what the thing is
 * called — one `--flat-playlist` enumeration, first title, done.
 */
static int yt_add(const char *url)
{
	if (!url || strncmp(url, "http", 4) != 0) {
		fputs("syn-arcade: big music yt add takes a URL\n", stderr);
		return EX_USAGE;
	}

	char name[320] = "";	/* title + the " — mix" suffix */
	if (have("yt-dlp")) {
		static char titles[1][256];
		static char urls[1][SYN_PATH];
		if (yt_enumerate(url, titles, urls, 1) == 1) {
			/* ⚠ A MIX IS NAMED AFTER ITS SEED, so a track and the
			 * endless station it seeds resolve to the SAME title —
			 * two rows on the television with identical names and
			 * very different behaviour. `list=RD…` is YouTube's own
			 * marker for the generated station, so the row can say
			 * which one it is. */
			if (strstr(url, "list=RD"))
				snprintf(name, sizeof(name), "%s — mix",
					 titles[0]);
			else
				snprintf(name, sizeof(name), "%s", titles[0]);
		}
	}

	char path[SYN_PATH];
	if (!yt_stations_path(path, sizeof(path)) || mkdir_parents(path) != 0) {
		fputs("syn-arcade: could not open the station list\n", stderr);
		return EX_FAIL;
	}

	FILE *f = fopen(path, "a");
	if (!f) {
		fprintf(stderr, "syn-arcade: %s: %s\n", path, strerror(errno));
		return EX_FAIL;
	}
	fprintf(f, "%s\t%s\n", url, name[0] ? name : url);
	fclose(f);

	printf("added %s\n", name[0] ? name : url);
	return EX_OK;
}

/* ── the source picker ───────────────────────────────────────────────────── */

/*
 * ── the two streaming services, and why their rows used to dead-end ─────────
 *
 * Reported from the sofa: choosing YouTube Music or Spotify opened cliamp, and
 * cliamp appeared not to support either of them. Both halves were true, and
 * neither was cliamp's fault:
 *
 *   YouTube Music  works with no account at all — cliamp ships fallback
 *                  credentials — but every track is fetched with **yt-dlp**,
 *                  which is an OPTDEPEND of the cliamp package and is not
 *                  installed here. cliamp's own package says what happens
 *                  without it: "those sources return nothing". So the row
 *                  opened a music player onto an empty library.
 *   Spotify        needs a `[spotify]` section in cliamp's config.toml and a
 *                  sign-in through a browser, and a Premium account to play
 *                  anything at all. With no section there is nothing to open.
 *
 * ⚠ NEITHER IS SOMETHING A TILE CAN FIX BY TRYING HARDER, and that is why they
 * get their own actions rather than a better error message. What is missing is
 * a package on one and an account on the other; the row's job is to say which
 * and to start the thing that fixes it.
 *
 * Checked rather than assumed on both counts: `yt-dlp` on PATH, and the
 * section `cliamp setup` writes when somebody finishes signing in.
 */
static bool cliamp_conf_section(const char *section)
{
	char path[SYN_PATH];
	if (!config_path(path, sizeof(path), "cliamp/config.toml"))
		return false;
	char *text = read_file(path);
	if (!text)
		return false;

	bool found = false;
	char *save = NULL;
	for (char *ln = strtok_r(text, "\n", &save); ln && !found;
	     ln = strtok_r(NULL, "\n", &save))
		found = strcmp(trim(ln), section) == 0;
	free(text);
	return found;
}

/*
 * Whether YouTube Music can actually BROWSE anything on this machine.
 *
 * ── ⚠ AND `enabled = true` IS NOT THE ANSWER, WHICH IS THE WHOLE POINT ──────
 *
 * cliamp's own setup wizard offers three modes for YouTube Music and calls the
 * first one "Use built-in credentials (recommended)". It writes
 * `[ytmusic]\nenabled = true` and nothing else, on the documented promise that
 * the player carries a pool of shared Google OAuth desktop credentials to fall
 * back on.
 *
 * ⚠ THAT POOL IS EMPTY. In v1.63.2 — the pinned version this OS ships —
 * external/ytmusic/fallback.go declares `var fallbackCredentials []oauthCreds`
 * with no entries, so FallbackCredentials() returns two empty strings and
 * ResolveCredentials() has nothing to resolve to. Measured against the
 * installed binary, with a config directory of its own so nothing live was
 * touched:
 *
 *     cliamp --provider ytmusic
 *     → YouTube: no credentials available (configure client_id/client_secret
 *       in config.toml)
 *
 * and the three YouTube entries are never added to the provider list at all.
 * So the television sent somebody to cliamp, cliamp opened with no YouTube
 * Music in it, and the one line explaining why went to a stderr nobody on a
 * sofa is reading. Reported exactly that way: "cliamp never got setup and I
 * don't see how".
 *
 * ⚠ THE OAUTH CLIENT IS FOR BROWSING, NOT FOR PLAYING. Worth stating because
 * it is the difference between "needs an account" and "needs a Google Cloud
 * project": resolve.go sends every YouTube URL through yt-dlp and the native
 * client, neither of which sees these credentials. A URL plays with yt-dlp
 * alone. It is SEARCH and BROWSE that go through the Data API, which is what
 * `browse` on this row means and why this check gates that row and no other.
 *
 * ⚠ So this asks for the two keys rather than for the section. A section with
 * `enabled = true` and nothing else is a machine that has been through the
 * wizard and still cannot play anything, which is the state this exists to
 * stop calling ready. If upstream ever fills that pool, THIS is the check to
 * revisit — the section alone would then be enough.
 *
 * `[yt]`, `[youtube]` and `[ytmusic]` are one section to cliamp's parser
 * (config.go normalises all three), so they are one section here.
 */
static bool ytmusic_credentialed(void)
{
	char path[SYN_PATH];
	if (!config_path(path, sizeof(path), "cliamp/config.toml"))
		return false;
	char *text = read_file(path);
	if (!text)
		return false;

	bool inside = false, id = false, secret = false;
	char *save = NULL;
	for (char *ln = strtok_r(text, "\n", &save); ln;
	     ln = strtok_r(NULL, "\n", &save)) {
		char *t = trim(ln);
		if (*t == '[') {
			inside = strcmp(t, "[ytmusic]") == 0 ||
				 strcmp(t, "[youtube]") == 0 ||
				 strcmp(t, "[yt]") == 0;
			continue;
		}
		if (!inside)
			continue;

		char *eq = strchr(t, '=');
		if (!eq)
			continue;
		*eq = '\0';
		char *key = trim(t);
		char *val = trim(eq + 1);

		/* Quoted in everything the wizard writes, and the quotes are
		 * what "empty" has to be measured inside: `client_id = ""` is
		 * a key that is present and useless. */
		size_t n = strlen(val);
		if (n >= 2 && val[0] == '"' && val[n - 1] == '"') {
			val[n - 1] = '\0';
			val++;
		}
		if (!*val)
			continue;

		if (strcmp(key, "client_id") == 0)
			id = true;
		else if (strcmp(key, "client_secret") == 0)
			secret = true;
	}
	free(text);
	return id && secret;
}

/*
 * What a source DOES when it is chosen, which is the column the television
 * acts on:
 *
 *   play     it is playing now — nothing else to do
 *   albums   pick something from the library first (Plex)
 *   browse   only cliamp's own interface can reach this one
 *   install  something has to be installed before it can play anything
 *   setup    somebody has to sign in before it can play anything
 *
 * ⚠ THE COLUMN EXISTS SO THE SHELL DOES NOT HAVE TO KNOW THE LIST. Which
 * sources can be queued from outside cliamp is a fact about cliamp, and a copy
 * of it in QML is a copy that stops being true. The two new actions are the
 * same rule one step further on: whether YouTube Music needs a package is a
 * fact about this machine, and the shell only has to know how to launch what
 * the column names.
 */
static const char *source_action(const struct source *s)
{
	/* ⚠ YouTube Music ANSWERS BEFORE THE QUEUEABLE SPLIT, because the one
	 * thing it needs is not a queue and not an account: it is yt-dlp, which
	 * is what resolves a URL into something cliamp can play. With it the
	 * row opens this package's own station list; without it there is
	 * nothing to open and the row offers to install it. The OAuth client
	 * gates cliamp's own SEARCH and nothing here — see
	 * ytmusic_credentialed(), and the note below that says so. */
	if (strcmp(s->id, "ytmusic") == 0)
		return have("yt-dlp") ? "yt" : "install";

	if (!s->queueable) {
		if (strcmp(s->id, "spotify") == 0 &&
		    !cliamp_conf_section("[spotify]"))
			return "setup";
		return "browse";
	}
	if (strcmp(s->id, "plex") == 0)
		return "albums";
	return "play";
}

static int big_music_sources(bool rec)
{
	const struct source *cur = music_source();
	char dir[SYN_PATH];
	bool plex_ready = false;
	{
		char u[256], t[192];
		plex_ready = plex_conf(u, sizeof(u), t, sizeof(t));
	}

	if (rec)
		rec_row(5, "id", "name", "current", "action", "note");

	for (int i = 0; i < SOURCES_N; i++) {
		const struct source *s = &SOURCES[i];
		const char *note = "";
		const char *act = source_action(s);

		/* The notes are facts about THIS machine rather than about the
		 * source, and they are here for one reason: a row that answers
		 * a button press with an error is a row nobody can debug from
		 * a sofa. Both of these are silent failures otherwise — the
		 * music stops, nothing starts, and the menu looks fine.
		 *
		 * ⚠ `local` needs one as much as Plex does. Plenty of machines
		 * have no music directory at all (this one does not: everything
		 * is on the Plex server), and choosing it there would stop
		 * whatever was playing to queue nothing.
		 *
		 * ⚠ AND A NOTE THAT NAMES WHAT IS MISSING IS THE WHOLE POINT of
		 * the two new actions. "opens cliamp" was true and useless on a
		 * machine with no yt-dlp: it described the button rather than
		 * the outcome, which was an empty library. */
		if (strcmp(s->id, "plex") == 0 && !plex_ready)
			note = "not set up — run `cliamp setup`";
		else if (strcmp(s->id, "local") == 0 && !music_dir(dir, sizeof(dir)))
			note = "no music folder on this machine";
		else if (strcmp(act, "install") == 0)
			note = "needs yt-dlp — press to install it";

		/* ⚠ THE NOTE IS PER SOURCE, NOT PER ACTION. `setup` is only
		 * Spotify's now, but it was briefly both, and a note keyed on
		 * the action alone told somebody setting up YouTube Music that
		 * they needed Spotify Premium. Keyed on the source it cannot
		 * say that again whatever else grows a `setup`. */
		else if (strcmp(act, "setup") == 0)
			note = strcmp(s->id, "spotify") == 0
				? "press to sign in — needs Spotify Premium"
				: "press to sign in";

		/* ⚠ THE ONE THING THIS ROW STILL CANNOT DO, said on the row
		 * rather than discovered by pressing it. Stations play here
		 * with no account at all; searching INSIDE cliamp goes through
		 * the Data API, and cliamp v1.63.2 has no credentials to reach
		 * it with. Somebody who wants that has somewhere to go, and
		 * somebody who does not is not being sent to a wizard for a
		 * row that already works. */
		/* ⚠ THE NOTE ANSWERS "CAN I GET AT MY OWN MUSIC", which is the
		 * question somebody actually has, and signing in is what
		 * changes the answer. The OAuth client is a second, narrower
		 * thing — cliamp's own search — and saying so on a row that
		 * already works only reads as another thing gone wrong. */
		else if (strcmp(act, "yt") == 0) {
			char br[64];
			if (yt_cookie_browser(br, sizeof(br)))
				note = "your playlists and your stations";
			else
				note = "stations play here — sign in for your "
				       "own playlists";
		}
		else if (!s->queueable)
			note = "opens cliamp";

		if (rec)
			rec_row(5, s->id, s->name,
				s == cur ? "1" : "0", act, note);
		else
			printf("%-8s %-14s %s%s%s\n", s->id, s->name,
			       s == cur ? "· current" : "",
			       note[0] ? "   " : "", note);
	}
	return EX_OK;
}

/*
 * Choose a source.
 *
 * Written down FIRST and started SECOND: if the player refuses to come up, the
 * setting is still what somebody asked for, and the next press of the Music
 * tile uses it. The reverse order would lose the choice to a transient.
 *
 * ⚠ CHOOSING A SOURCE IS A SETTING, NOT A TRANSPORT COMMAND, and this used to
 * treat it as both. It restarted the player unconditionally — which STARTS one
 * that was not running, because music_restart() ends in music_ensure_running()
 * — and then sent `toggle` for radio, which from `stopped` does not toggle
 * anything, it begins playing. So picking a source from a settings list on a
 * silent machine started the music: measured, changing Plex → Radio left
 * `script -qfc cliamp --provider radio` running and audible, having asked for
 * nothing but a preference.
 *
 * So the player's state is read BEFORE the setting is written, and it is the
 * thing that decides what happens next:
 *
 *   · not running → write it down and stop. `--provider` is read at start-up
 *                   and music_ensure_running() reads music_source(), so the
 *                   next press of the Music tile comes up on this source by
 *                   itself. Nothing needs to start now for that to be true.
 *   · running     → the change has to REACH it, and a start-up flag can only
 *                   be changed by starting again. Restart.
 *   · playing     → and only then does anything play, because the music was
 *                   already playing and stopping it is not what "change
 *                   source" means either.
 *
 * ⚠ `toggle` and not `play`: after a restart the state is `stopped`, and
 * `play` is RESUME, which does nothing from there.
 */
static int big_music_source_set(const char *id)
{
	const struct source *s = source_by_id(id);
	if (!s) {
		fprintf(stderr, "syn-arcade: no music source called '%s' — "
				"`syn-arcade big music source` lists them\n", id);
		return EX_USAGE;
	}

	/* ⚠ ASKED FIRST. Everything below changes it, so there is exactly one
	 * moment when the answer is still about what the player was doing when
	 * somebody opened the list. An empty state is "there is nothing to
	 * control" — see music_read(). */
	char was[32];
	music_read(was, sizeof(was), NULL, 0, NULL, 0, NULL);
	const bool was_running = was[0] != '\0';
	const bool was_playing = strcmp(was, "playing") == 0;

	if (big_conf_set("music_source", s->id) != EX_OK) {
		fputs("syn-arcade: could not write "
		      "~/.config/syn-arcade/big.conf\n", stderr);
		return EX_FAIL;
	}

	if (!was_running) {
		printf("music source: %s\n", s->name);
		return EX_OK;
	}

	/* local_queue() restarts on its own way through, so it must not be
	 * given a second one — and it is the only source whose queue this
	 * program fills. Plex needs an album picked and the two services need
	 * cliamp's own interface; both are the caller's next move. */
	if (strcmp(s->id, "local") == 0) {
		int rc = local_queue();
		if (rc != EX_OK)
			return rc;
	} else if (!music_restart()) {
		fputs("syn-arcade: cliamp did not come up\n", stderr);
		return EX_FAIL;
	}

	/* Only where the new source arrives with something in it. Plex and the
	 * two services come up empty by design, and `toggle` on an empty queue
	 * is a button that appears to do nothing. */
	if (was_playing && (strcmp(s->id, "radio") == 0 ||
			    strcmp(s->id, "local") == 0))
		music_cmd("toggle");

	printf("music source: %s\n", s->name);
	return EX_OK;
}

/*
 * Open cliamp itself, on the television.
 *
 * ⚠ THE HONEST ANSWER FOR TWO OF THE FIVE SOURCES. YouTube Music and Spotify
 * are searched inside cliamp's own interface and are reachable no other way —
 * there is no CLI verb that lists them and nothing to queue from outside. The
 * alternative was a menu entry that sets a provider and then plays silence,
 * which is the failure this whole file is written against.
 *
 * ⚠ THE HEADLESS PLAYER IS STOPPED FIRST. There can be exactly one cliamp on
 * the socket; a second one started in a terminal while the pty instance is up
 * finds the socket taken, and what appears on the television is a music player
 * that will not accept a keypress.
 *
 * It is an ordinary application launch after that: the interface steps aside,
 * the on-screen keyboard is available, and Guide comes back — with the music
 * still playing, because the terminal instance IS the player now.
 */
static int big_music_browse(void)
{
	const char *term = terminal_prog();
	if (!term) {
		fputs("syn-arcade: no terminal is installed to open cliamp "
		      "in\n", stderr);
		return EX_FAIL;
	}

	music_stop_player();

	const struct source *src = music_source();
	char *argv[8];
	int argc = 0;
	argv[argc++] = (char *)term;

	/* ⚠ `-e` for some and not for others, and it is not a preference:
	 * syntty, alacritty and xterm take the command after -e; kitty and
	 * foot take it as their own trailing arguments and treat -e as an
	 * error or a legacy alias. Getting this wrong opens an empty terminal
	 * on the television. */
	if (strcmp(term, "kitty") != 0 && strcmp(term, "foot") != 0)
		argv[argc++] = (char *)"-e";

	argv[argc++] = (char *)"cliamp";
	if (src && src->provider) {
		argv[argc++] = (char *)"--provider";
		argv[argc++] = (char *)src->provider;
	}
	argv[argc] = NULL;

	/* ⚠ FULL SCREEN ONLY WHERE THERE IS A SCREEN TO FILL. This is reached
	 * from two places now — a tile on the television, and the desktop music
	 * widget's source picker — and `fill` is the television's rule: an
	 * application launched from four metres away should take the whole
	 * display. Applied unconditionally it means pressing a button on a
	 * 268px card on the wallpaper throws a fullscreen terminal over
	 * whatever somebody was doing, which is not what that card promised.
	 * big_running() is the shell's presence rather than a guess at it: the
	 * lock is held for exactly as long as that process lives. */
	return spawn_wait(argv, big_running(NULL), false);
}

/*
 * ── the two rows that have something to fix first ───────────────────────────
 *
 * A terminal on the television running one command, and then WAITING with what
 * it said still on the screen.
 *
 * ⚠ THE PAUSE AT THE END IS THE FEATURE. Both of these end in something worth
 * reading — a package manager's summary, or a wizard saying which account it
 * just signed in as — and a terminal that closes the instant the command
 * returns takes the answer with it. Worse than useless from four metres: the
 * screen flashes, the interface comes back, and there is no way to tell
 * success from "yt-dlp: not found".
 *
 * ⚠ A SHELL, AND EVERY WORD OF IT IS A LITERAL IN THIS FILE. Nothing here is
 * built from a config file, a package name typed by anybody, or a source id —
 * the two commands are chosen from a fixed list below and the shell only ever
 * sees text that is in this source. That is the whole reason run_capture
 * refuses /bin/sh (see its header); the rule is "no user input reaches a
 * shell", not "no shell".
 */
static int term_run_and_hold(const char *command)
{
	const char *term = terminal_prog();
	if (!term) {
		fputs("syn-arcade: no terminal is installed to run this in\n",
		      stderr);
		return EX_FAIL;
	}

	char script[512];
	snprintf(script, sizeof(script),
		 "%s; printf '\\n── press Enter to close ──'; read _",
		 command);

	char *argv[8];
	int argc = 0;
	argv[argc++] = (char *)term;
	if (strcmp(term, "kitty") != 0 && strcmp(term, "foot") != 0)
		argv[argc++] = (char *)"-e";
	argv[argc++] = (char *)"sh";
	argv[argc++] = (char *)"-c";
	argv[argc++] = script;
	argv[argc] = NULL;

	/* Fullscreen for the television and windowed for the desktop, for the
	 * reason big_music_browse() gives at length. */
	return spawn_wait(argv, big_running(NULL), false);
}

/*
 * Install what a source needs before it can play anything.
 *
 * ⚠ `--noconfirm` BEFORE THE VERB, and both halves matter. synpkg stops
 * parsing global options at the first non-option argument, so
 * `install --noconfirm` is a flag it never sees — and without it a front-end
 * that cannot answer a prompt authenticates through polkit and then declines
 * itself, installing nothing and reporting success. That has bitten this
 * project twice already; see the note in synpkg's own trans.c.
 *
 * A television with a gamepad is the front-end that cannot answer, even with
 * the on-screen keyboard up. The password prompt is unavoidable — installing a
 * package is root's business — but a y/n question that somebody has to spell
 * out with a d-pad is not.
 */
static int big_music_install(const struct source *s)
{
	if (!s || strcmp(s->id, "ytmusic") != 0) {
		fputs("syn-arcade: nothing to install for that source\n",
		      stderr);
		return EX_USAGE;
	}
	if (have("yt-dlp")) {
		puts("yt-dlp is already installed");
		return EX_OK;
	}
	return term_run_and_hold("synpkg --noconfirm install yt-dlp");
}

/*
 * Sign in to a source, through cliamp's own wizard.
 *
 * ⚠ NOT A WIZARD OF OUR OWN, and that is deliberate. What Spotify needs is an
 * OAuth round trip through a browser and a credential cache in cliamp's config
 * directory; a second implementation of that here would be a second thing to
 * keep in step with a player that changes its providers between releases. The
 * wizard already exists, it validates the connection before it writes, and it
 * is one command.
 *
 * ⚠ THE HEADLESS PLAYER IS STOPPED FIRST, for the reason big_music_browse
 * gives: one socket, one cliamp.
 */
static int big_music_setup(void)
{
	music_stop_player();
	return term_run_and_hold("cliamp setup");
}

/*
 * Put back what was playing last — the other half of music_last_remember().
 *
 * ⚠ IT LIVES DOWN HERE FOR ONE REASON: it calls the three paths that fill a
 * queue, and all three are defined above it. A forward declaration apiece
 * would be three more places to keep in step with a signature, for a function
 * with exactly one caller.
 *
 * ⚠ AND "NOTHING REMEMBERED" IS NOT AN EXIT CODE. It cannot be: yt_play()
 * already answers EX_EMPTY for "nothing playable came back from that URL",
 * which is a real failure worth printing, and a caller that could not tell the
 * two apart would fall through to starting a bare player and calling it fine.
 * So the outcome goes out by pointer and the answer here is whether there was
 * one at all.
 */
static bool music_play_last(int *rc)
{
	char src[32], what[SYN_PATH];
	if (!music_last_read(src, sizeof(src), what, sizeof(what)))
		return false;

	/* ⚠ ONLY FOR THE SOURCE THAT IS CHOSEN NOW. Replaying a YouTube
	 * station goes through yt_play(), which WRITES `music_source` — so
	 * resuming one after somebody moved the picker to Plex would silently
	 * undo the choice they had just made. */
	const struct source *cur = music_source();
	if (!cur || strcmp(cur->id, src) != 0)
		return false;

	if (!strcmp(src, "plex")) {
		*rc = plex_play_album(what);
		return true;
	}
	if (!strcmp(src, "ytmusic")) {
		*rc = yt_play(what);
		return true;
	}
	if (!strcmp(src, "local")) {
		*rc = local_queue();
		return true;
	}

	/* radio and spotify fill no queue of ours, so neither can have written
	 * one of these. A record naming one is a file from a later version, or
	 * a hand-edited one; either way there is nothing here to replay. */
	return false;
}

/* Declared rather than moved: they belong with the dispatch below, which is
 * the only other thing that parses an argument list. */
static const char *first_operand(int argc, char **argv);
static const char *second_operand(int argc, char **argv);
static const char *third_operand(int argc, char **argv);

/* ⚠ The verb is the first OPERAND, not argv[0], and `rec` is the caller's.
 * `--rec` is a global flag everywhere else in this command — `big music --rec
 * status` has to mean what `big music status --rec` means, or the shell's
 * habit of putting it first quietly turns a status query into a usage error. */
static int big_music(int argc, char **argv, bool rec)
{
	const char *verb = first_operand(argc, argv);
	if (!verb)
		verb = "status";

	/* ⚠ THE VERB IS CHECKED BEFORE THE PLAYER IS. A typo is a typo on every
	 * machine, and answering it with "your music player cannot be driven"
	 * sends somebody to edit a config file over a missing letter. */
	static const char *const verbs[] = { "status", "play", "pause",
					     "toggle", "next", "prev", "stop",
					     "vis", "source", "plex", "yt",
					     "browse", "install", "setup",
					     "release",
					     NULL };
	bool known = false;
	for (int i = 0; verbs[i] && !known; i++)
		known = strcmp(verb, verbs[i]) == 0;
	if (!known) {
		fprintf(stderr, "syn-arcade: big music takes status, play, "
				"pause, toggle, next, prev, stop, source, plex, "
				"yt, browse, install, setup or release "
				"(got '%s')\n", verb);
		return EX_USAGE;
	}

	if (!music_headless()) {
		fprintf(stderr, "syn-arcade: the music player is %s, which big "
				"screen mode cannot drive — set `music = cliamp` "
				"in ~/.config/syn-arcade/big.conf\n",
			music_prog() ? music_prog() : "not installed");
		return EX_FAIL;
	}

	if (!strcmp(verb, "status"))
		return big_music_status(rec);

	/* The way out. Answered here, above everything that needs a player to
	 * be running, because its whole job is that there should not be one. */
	if (!strcmp(verb, "release"))
		return big_music_release();

	/*
	 * Where the music comes from.
	 *
	 * ⚠ THE LIST IS A READ AND THE SET IS A RESTART, and both are this one
	 * verb because they are one thing from the sofa: the picker asks what
	 * the sources are, draws them, and hands back the one that was chosen.
	 */
	if (!strcmp(verb, "source")) {
		const char *id = second_operand(argc, argv);
		return id ? big_music_source_set(id) : big_music_sources(rec);
	}

	/* The Plex library: the albums, and playing one. */
	if (!strcmp(verb, "plex")) {
		const char *key = second_operand(argc, argv);
		return key ? plex_play_album(key) : plex_albums(rec);
	}

	/*
	 * YouTube Music. Same shape as `plex` — a list, and playing one off it
	 * — with two more for keeping the list, because unlike a Plex server
	 * there is nothing on the other end that already knows what somebody
	 * wants to listen to.
	 *
	 * ⚠ `search` and `add` BEFORE the fall-through, and the fall-through
	 * takes a station id OR a URL. A search result's id IS its URL, so the
	 * shell plays one with exactly the command it plays a station with and
	 * there is no second path to keep in step.
	 */
	if (!strcmp(verb, "yt")) {
		const char *a = second_operand(argc, argv);
		if (!a)
			return yt_stations(rec);
		if (!strcmp(a, "search"))
			return yt_search(third_operand(argc, argv), rec);
		if (!strcmp(a, "add"))
			return yt_add(third_operand(argc, argv));

		/* Somebody's own library, and the two that TYPE. `login` and
		 * `find` read from stdin, which on the television means a
		 * terminal with the on-screen keyboard pointed at it — see
		 * yt_find() for why that is the mechanism rather than a text
		 * field in the shell. */
		if (!strcmp(a, "mine"))
			return yt_mine(rec);
		if (!strcmp(a, "login"))
			return yt_login(third_operand(argc, argv));
		if (!strcmp(a, "find"))
			return yt_find();

		return yt_play(a);
	}

	/* cliamp's own interface, for the sources nothing else can reach. */
	if (!strcmp(verb, "browse"))
		return big_music_browse();

	/* The two rows that have something to fix before they can play: a
	 * package on one, an account on the other. Both open a terminal on the
	 * television and both are named by the `action` column of the source
	 * picker, so the shell launches what the row said and nothing here is
	 * a second copy of which source needs what. */
	if (!strcmp(verb, "install"))
		return big_music_install(source_by_id(second_operand(argc, argv)));
	if (!strcmp(verb, "setup"))
		return big_music_setup();

	/* ⚠ Only `play` starts anything. toggle/next/prev on a player that is
	 * not running would otherwise START one and then skip a track in it,
	 * which is a surprising amount to happen because somebody pressed
	 * pause. */
	if (!strcmp(verb, "play")) {
		/*
		 * ⚠ AN EMPTY QUEUE IS NOT SOMETHING TO RESUME, IT IS SOMETHING
		 * TO FILL — and that is what the Music tile got wrong.
		 *
		 * A player that has just started has whatever `--provider`
		 * preloaded and nothing else: eleven stations on radio,
		 * NOTHING on ytmusic, plex or local, because those queues are
		 * ones this file fills a track at a time and nothing on the
		 * other end writes one down. Until 0.1.0-33 that never showed,
		 * because Quit left the player running and its queue with it,
		 * so the next press really was a resume. Now that Quit lets go
		 * of the music, the tile came up on a bare player and played
		 * silence. Reported from the sofa exactly that way.
		 *
		 * So: something queued → resume it, which is the old
		 * behaviour and the common one. Nothing queued → put back what
		 * was playing last, which is what somebody pressing Music
		 * meant either way.
		 *
		 * ⚠ AND ONLY OUR OWN PLAYER IS RESTARTED. Replaying goes
		 * through music_restart() — `--provider` is a start-up flag,
		 * so there is no other way to change a source — and a cliamp
		 * somebody has open in a terminal is not this launcher's to
		 * restart. Same marker, and the same argument, as `release`.
		 */
		bool live = music_socket_live();
		long queued = 0;
		if (live)
			music_read(NULL, 0, NULL, 0, NULL, 0, &queued);

		if (!live || (queued == 0 && music_is_ours())) {
			int rc;
			if (music_play_last(&rc))
				return rc;
		}

		if (!music_ensure_running()) {
			fputs("syn-arcade: cliamp did not come up\n", stderr);
			return EX_FAIL;
		}

		/* ⚠ `play` IS RESUME, AND RESUME DOES NOTHING FROM `stopped` —
		 * which is exactly the state a player that has just started is
		 * in. Sending it there starts the music player and plays
		 * nothing, which is the worst kind of working: the tile
		 * responds, the daemon is up, the menu grows a Now Playing row,
		 * and there is silence. `toggle` is what begins playback from a
		 * standing start, and it is also the right verb from `paused`. */
		char state[32];
		long have = 0;
		music_read(state, sizeof(state), NULL, 0, NULL, 0, &have);
		if (!strcmp(state, "playing"))
			return music_cmd("play");

		music_cmd("toggle");

		/* ⚠ AND THEN IT IS ASKED WHETHER IT STARTED. A queue whose
		 * current track cannot be resolved leaves cliamp `stopped` for
		 * ever with nothing said anywhere — which is a Music tile that
		 * responds to every press and never makes a sound. Reported
		 * exactly that way; see music_start_insist().
		 *
		 * ⚠ ONLY WITH SOMETHING QUEUED. An empty queue is not a track
		 * that will not play, it is no track at all, and skipping
		 * through it would be a minute of the program pressing `next`
		 * against nothing. */
		if (have > 0)
			music_start_insist();
		return EX_OK;
	}

	/*
	 * The visualizer's bands, one NDJSON frame per line, straight from
	 * cliamp to whoever is reading this.
	 *
	 * ⚠ EXEC, not a copy loop, and that is the whole implementation. This
	 * process has nothing left to do but move lines, and the reader one
	 * layer up has to see cliamp's OWN end-of-stream when the player goes —
	 * a relay in the middle turns "the music stopped" into "the relay is
	 * still here with nothing to say", which draws a visualizer frozen on
	 * its last frame rather than one that goes away.
	 *
	 * ⚠ It answers `{"ok":false,"error":"bands timeout"}` for ever if the
	 * player was started headless. See music_ensure_running for why it is
	 * not, and never make that `--daemon` again.
	 */
	if (!strcmp(verb, "vis")) {
		if (!music_socket_live()) {
			fputs("syn-arcade: no music player is running\n", stderr);
			return EX_FAIL;
		}
		/* 20 rather than cliamp's default 30: this drives ten
		 * rectangles on a television, and the frames a launcher cannot
		 * draw are frames it pays a subprocess to hand it anyway. */
		execlp("cliamp", "cliamp", "visstream", "--fps", "20",
		       (char *)NULL);
		fputs("syn-arcade: could not start the visualizer stream\n",
		      stderr);
		return EX_FAIL;
	}

	if (!strcmp(verb, "toggle") || !strcmp(verb, "pause") ||
	    !strcmp(verb, "next") || !strcmp(verb, "prev") ||
	    !strcmp(verb, "stop")) {
		if (!music_socket_live()) {
			fputs("syn-arcade: no music player is running\n", stderr);
			return EX_FAIL;
		}

		/* ⚠ `toggle` IS TWO VERBS WEARING ONE NAME, and only one of
		 * them is a start. From `playing` it is pause and there is
		 * nothing to insist on; from `stopped` it is play, and it is
		 * the Now Playing row's A button — the other way somebody on a
		 * sofa asks a stalled queue to start. Asked BEFORE the command,
		 * because afterwards the answer is the one being tested for. */
		char state[32];
		long have = 0;
		if (!strcmp(verb, "toggle"))
			music_read(state, sizeof(state), NULL, 0, NULL, 0,
				   &have);
		else
			state[0] = '\0';

		int rc = music_cmd(verb);
		if (!strcmp(verb, "toggle") && !strcmp(state, "stopped") &&
		    have > 0)
			music_start_insist();
		return rc;
	}

	return EX_OK;		/* unreachable: the verb list above is closed */
}

/* ── transport, for whatever is playing ──────────────────────────────────── */

/*
 * The media buttons along the bottom of the television, and the keys on a
 * keyboard or a remote that mean the same thing.
 *
 * ⚠ THIS IS NOT `big music`, AND THE DIFFERENCE IS WHOSE MUSIC IT IS.
 * `big music` drives cliamp — one player, over its own socket, with a source
 * picker and a queue this program fills. That is a good deal of machinery for
 * one program, and it is worth nothing the moment somebody is listening to
 * Spotify, watching a film in mpv, or playing a video in a browser tab. A
 * play/pause button on a television has to work on whatever is making the
 * noise, or it is a button that works on Tuesdays.
 *
 * So this speaks MPRIS2 — the freedesktop interface every media player on
 * Linux implements, cliamp included (`org.mpris.MediaPlayer2.cliamp`, and its
 * own documentation is where that was checked rather than guessed).
 *
 * ⚠ THROUGH `busctl` RATHER THAN A D-BUS LIBRARY, and that is the same
 * decision the news shelf made with curl. libsystemd would be a link-time
 * dependency on every machine, for six method calls that a program shipped
 * with systemd already makes — and this binary's one hard dependency beyond
 * libc is a Wayland protocol nothing else implements. busctl is on every
 * SynapseOS machine because systemd is; nothing here needs a connection held
 * open, and a failed call is simply "nothing is playing".
 *
 * ⚠ AND CLIAMP IS STILL DRIVEN OVER ITS SOCKET when cliamp is what is
 * playing. Not for want of MPRIS: cliamp's MPRIS reports the FILE PATH as the
 * track title (measured — `xesam:title` came back as
 * /mnt/.../04. In a Lonely Place (Tricky Mix).mp3), and for a Plex stream that
 * path is a URL with the server token in it. music_read() already knows how to
 * turn that back into a name, and `play` from `stopped` is already known to be
 * the wrong verb there. Rediscovering both through a second interface is how a
 * television ends up with two answers about the same player.
 */

#define MPRIS_PREFIX	"org.mpris.MediaPlayer2."
#define PLAYERS_MAX	16

struct playing {
	char bus[128];		/* the whole D-Bus name                      */
	char who[64];		/* the part after the prefix: cliamp, spotify */
	char app[64];		/* Identity — what the player calls itself   */
	char state[32];		/* playing | paused | stopped                */
	char title[256];
	char artist[192];
	bool can_next, can_prev, can_play, can_pause;
};

/*
 * One value out of a D-Bus variant, as `busctl --json=short` prints it:
 *
 *     "PlaybackStatus":{"type":"s","data":"Paused"}
 *
 * The key names the variant and the value is one level in, which is the only
 * difference from reading an ordinary JSON object — hence json_str at the
 * position of the key rather than at the top of the document.
 */
static bool var_str(const char *json, const char *key, char *out, size_t n)
{
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = json ? strstr(json, pat) : NULL;
	return p && json_str(p, NULL, "data", out, n);
}

static bool var_true(const char *json, const char *key)
{
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	const char *p = json ? strstr(json, pat) : NULL;
	if (!p)
		return false;
	const char *d = strstr(p, "\"data\":");
	return d && strncmp(d + 7, "true", 4) == 0;
}

/*
 * Every media player with a name on the session bus.
 *
 * ⚠ THE NAME IS THE FIRST FIELD OF `busctl list`, and the list holds unique
 * names (`:1.67`) and every other service on the bus besides. Matching the
 * prefix is the whole filter, and it is what the specification says to do.
 */
static int mpris_players(char names[][128], int max)
{
	char *out = run_capture((char *const[]){ "busctl", "--user", "list",
						 "--no-pager", NULL }, 3);
	if (!out)
		return 0;

	int n = 0;
	char *save = NULL;
	for (char *ln = strtok_r(out, "\n", &save); ln && n < max;
	     ln = strtok_r(NULL, "\n", &save)) {
		if (strncmp(ln, MPRIS_PREFIX, strlen(MPRIS_PREFIX)) != 0)
			continue;
		size_t k = strcspn(ln, " \t");
		if (k >= sizeof(names[0]))
			continue;
		memcpy(names[n], ln, k);
		names[n][k] = '\0';
		n++;
	}
	free(out);
	return n;
}

/* Everything the Player interface knows, in one call. Caller frees. */
static char *mpris_props(const char *bus)
{
	return run_capture((char *const[]){
		"busctl", "--user", "--json=short", "call", (char *)bus,
		"/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties",
		"GetAll", "s", "org.mpris.MediaPlayer2.Player", NULL }, 3);
}

/* playing → 2, paused → 1, anything else → 0. What the pick below sorts on. */
static int state_rank(const char *state)
{
	if (!strcmp(state, "playing"))
		return 2;
	if (!strcmp(state, "paused"))
		return 1;
	return 0;
}

/*
 * A title that is really a path, made into something to draw on a television.
 *
 * ⚠ THE SAME RULE AS music_read, FOR THE SAME REASON, and it has to be applied
 * here too because MPRIS is a second door onto the same fact. cliamp publishes
 * the path as the title, and for a Plex stream that path carries the account
 * token in its query. Nothing that leaves this function has a query on it.
 */
static void title_from_path(char *title, size_t n)
{
	if (!strchr(title, '/'))
		return;			/* a real title; leave it alone */

	char keyed[512];
	snprintf(keyed, sizeof(keyed), "%s", title);
	char *q = strchr(keyed, '?');
	if (q)
		*q = '\0';

	if (!music_title_lookup(keyed, title, n))
		music_title_fallback(keyed, title, n);
}

/*
 * WHICH player the buttons act on, when there is more than one.
 *
 * Whatever is playing wins; failing that, whatever is paused, which is the
 * player somebody most likely wants to start again. cliamp breaks a tie
 * because it is the one this interface starts itself — with the Music tile
 * paused and a browser tab paused as well, the button belongs to the music.
 *
 * ⚠ A player that is STOPPED is still a player, and it is returned. The shell
 * decides what to draw; a status command that answered "nothing" for a player
 * sitting at the start of a track would be lying about the machine.
 */
static bool transport_pick(struct playing *p)
{
	char names[PLAYERS_MAX][128];
	int n = mpris_players(names, PLAYERS_MAX);
	if (n <= 0)
		return false;

	memset(p, 0, sizeof(*p));
	int best = -1;

	for (int i = 0; i < n; i++) {
		char *props = mpris_props(names[i]);
		if (!props)
			continue;	/* it went away between the two calls */

		struct playing c;
		memset(&c, 0, sizeof(c));
		snprintf(c.bus, sizeof(c.bus), "%s", names[i]);
		snprintf(c.who, sizeof(c.who), "%s",
			 names[i] + strlen(MPRIS_PREFIX));

		char raw[32] = "";
		var_str(props, "PlaybackStatus", raw, sizeof(raw));
		snprintf(c.state, sizeof(c.state), "%s",
			 !strcmp(raw, "Playing") ? "playing" :
			 !strcmp(raw, "Paused")  ? "paused"  : "stopped");

		c.can_next  = var_true(props, "CanGoNext");
		c.can_prev  = var_true(props, "CanGoPrevious");
		c.can_play  = var_true(props, "CanPlay");
		c.can_pause = var_true(props, "CanPause");

		/* ⚠ SCOPED TO THE METADATA. `xesam:title` is unambiguous, but
		 * the rest of the reply is another program's and the whole
		 * point of a bound is not having to be sure. */
		const char *meta = strstr(props, "\"Metadata\"");
		if (meta) {
			var_str(meta, "xesam:title", c.title, sizeof(c.title));
			var_str(meta, "xesam:artist", c.artist,
				sizeof(c.artist));
			if (!c.title[0])
				var_str(meta, "xesam:url", c.title,
					sizeof(c.title));
			title_from_path(c.title, sizeof(c.title));
		}
		free(props);

		int rank = state_rank(c.state) * 2 +
			   (strcmp(c.who, "cliamp") == 0 ? 1 : 0);
		if (rank > best) {
			best = rank;
			*p = c;
		}
	}

	if (!p->bus[0])
		return false;

	/* What the player calls itself, for the one place a television has to
	 * name it. Its own interface, not the Player one, so it is a second
	 * call — and a cheap one, made once for the chosen player rather than
	 * for every name on the bus. */
	char *id = run_capture((char *const[]){
		"busctl", "--user", "get-property", p->bus,
		"/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2",
		"Identity", NULL }, 3);
	if (id) {
		/* `s "Cliamp"` — the type, then the string. */
		const char *q = strchr(id, '"');
		if (q) {
			snprintf(p->app, sizeof(p->app), "%s", q + 1);
			char *close = strrchr(p->app, '"');
			if (close)
				*close = '\0';
		}
		free(id);
	}
	if (!p->app[0])
		snprintf(p->app, sizeof(p->app), "%s", p->who);

	/* ⚠ CLIAMP ANSWERS FOR ITSELF. See the header of this section: its
	 * MPRIS title is the file path, and music_read knows the name. */
	if (!strcmp(p->who, "cliamp")) {
		char state[32], title[256];
		music_read(state, sizeof(state), title, sizeof(title), NULL, 0,
			   NULL);
		if (state[0])
			snprintf(p->state, sizeof(p->state), "%s", state);
		if (title[0])
			snprintf(p->title, sizeof(p->title), "%s", title);
	}
	return true;
}

static int big_transport_status(bool rec)
{
	struct playing p;
	bool any = transport_pick(&p);

	if (rec) {
		rec_row(9, "player", "app", "state", "title", "artist",
			"cannext", "canprev", "canplay", "canpause");
		if (any)
			rec_row(9, p.who, p.app, p.state, p.title, p.artist,
				p.can_next  ? "1" : "0",
				p.can_prev  ? "1" : "0",
				p.can_play  ? "1" : "0",
				p.can_pause ? "1" : "0");
		return any ? EX_OK : EX_EMPTY;
	}

	if (!any) {
		puts("nothing is playing");
		return EX_EMPTY;
	}
	printf("%-10s %s\n", "player", p.app);
	printf("%-10s %s\n", "state", p.state);
	if (p.title[0])
		printf("%-10s %s\n", "title", p.title);
	if (p.artist[0])
		printf("%-10s %s\n", "artist", p.artist);
	return EX_OK;
}

/*
 * One button press, on whichever player answered.
 *
 * ⚠ `prev` IS NOT ALWAYS THE PREVIOUS TRACK, and that is the player's rule
 * rather than this one's: MPRIS says a player more than a few seconds into a
 * track may restart it instead, and cliamp documents exactly that at three
 * seconds. It is what every physical skip-back button on earth does, so it is
 * left alone — but it is the reason a second press is sometimes needed, and
 * that is worth knowing before somebody reports it as a bug.
 */
static int big_transport_cmd(const char *verb)
{
	static const struct { const char *verb, *method; } MAP[] = {
		{ "play",   "Play"      },
		{ "pause",  "Pause"     },
		{ "toggle", "PlayPause" },
		{ "next",   "Next"      },
		{ "prev",   "Previous"  },
		{ "stop",   "Stop"      },
	};
	const char *method = NULL;
	for (size_t i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++)
		if (!strcmp(verb, MAP[i].verb))
			method = MAP[i].method;

	/* ⚠ THE VERB BEFORE THE PLAYER, the rule big_music already follows: a
	 * typo is a typo on every machine, and answering it with "nothing is
	 * playing" sends somebody to look at their music player. */
	if (!method) {
		fprintf(stderr, "syn-arcade: big transport takes status, play, "
				"pause, toggle, next, prev or stop (got "
				"'%s')\n", verb);
		return EX_USAGE;
	}

	struct playing p;
	if (!transport_pick(&p)) {
		fputs("syn-arcade: nothing is playing\n", stderr);
		return EX_EMPTY;
	}

	/* cliamp over its own socket — including the rule that `play` from
	 * `stopped` does nothing at all, which is why the tile sends `toggle`
	 * there. See big_music. */
	if (!strcmp(p.who, "cliamp")) {
		if (!strcmp(verb, "play") && strcmp(p.state, "playing") != 0)
			return music_cmd("toggle");
		return music_cmd(verb);
	}

	/* Said rather than sent, for the two buttons a player can refuse. A
	 * radio stream has no previous track; pretending the press worked is
	 * how a button teaches somebody it is broken. */
	if ((!strcmp(verb, "next") && !p.can_next) ||
	    (!strcmp(verb, "prev") && !p.can_prev)) {
		fprintf(stderr, "syn-arcade: %s cannot skip %s\n", p.app,
			!strcmp(verb, "next") ? "forward" : "back");
		return EX_FAIL;
	}

	char *out = run_capture((char *const[]){
		"busctl", "--user", "call", p.bus, "/org/mpris/MediaPlayer2",
		"org.mpris.MediaPlayer2.Player", (char *)method, NULL }, 3);
	if (!out) {
		fprintf(stderr, "syn-arcade: %s did not answer %s\n", p.app,
			method);
		return EX_FAIL;
	}
	free(out);
	return EX_OK;
}

static int big_transport(int argc, char **argv, bool rec)
{
	const char *verb = first_operand(argc, argv);
	if (!verb || !strcmp(verb, "status"))
		return big_transport_status(rec);
	return big_transport_cmd(verb);
}

/* ── the visualizer, as a program ────────────────────────────────────────── */

/*
 * Every capture source on the machine, as `pactl list sources short` prints
 * them: one per line, tab separated, the NAME in field two.
 *
 * Asked rather than composed, because the whole failure below was a device name
 * that was assembled by hand and did not exist.
 */
static bool source_exists(const char *want)
{
	char *out = run_capture((char *const[]){ "pactl", "list", "sources",
						 "short", NULL }, 3);
	if (!out)
		return false;

	bool found = false;
	char *save = NULL;
	for (char *ln = strtok_r(out, "\n", &save); ln && !found;
	     ln = strtok_r(NULL, "\n", &save)) {
		char *tab = strchr(ln, '\t');
		if (!tab)
			continue;
		char *name = tab + 1;
		char *end = strchr(name, '\t');
		if (end)
			*end = '\0';
		found = strcmp(name, want) == 0;
	}
	free(out);
	return found;
}

/* The first monitor source there is, for when the default sink's own monitor
 * cannot be found. A monitor — any monitor — is the right KIND of device; a
 * microphone never is. */
static bool first_monitor(char *buf, size_t n)
{
	char *out = run_capture((char *const[]){ "pactl", "list", "sources",
						 "short", NULL }, 3);
	if (!out)
		return false;

	bool found = false;
	char *save = NULL;
	for (char *ln = strtok_r(out, "\n", &save); ln && !found;
	     ln = strtok_r(NULL, "\n", &save)) {
		char *tab = strchr(ln, '\t');
		if (!tab)
			continue;
		char *name = tab + 1;
		char *end = strchr(name, '\t');
		if (end)
			*end = '\0';

		size_t len = strlen(name);
		if (len > 8 && strcmp(name + len - 8, ".monitor") == 0) {
			snprintf(buf, n, "%s", name);
			found = true;
		}
	}
	free(out);
	return found;
}

/*
 * Which source the visualizer should listen to: the MONITOR of the sink the
 * music is going to — a recording of the output.
 *
 * ⚠ The name is VERIFIED against the source list rather than composed and
 * hoped for. `<default-sink>.monitor` is the right shape and is not always a
 * real device: with synui's equaliser in the chain the default sink is a
 * virtual node (`effect_input.synui_eq`), whose monitor is not named that way
 * at all. A name that does not resolve is worse than no name — see below for
 * what a program does when the device it was told about is not there.
 */
static bool monitor_source(char *buf, size_t n)
{
	char *sink = run_capture((char *const[]){ "pactl", "get-default-sink",
						  NULL }, 3);
	if (sink) {
		strip_trailing_newline(sink);
		char want[320];
		snprintf(want, sizeof(want), "%s.monitor", trim(sink));
		free(sink);
		if (*want && source_exists(want)) {
			snprintf(buf, n, "%s", want);
			return true;
		}
	}
	return first_monitor(buf, n);
}

/*
 * Tell projectM which device to open, in the file projectM actually reads.
 *
 * ⚠ THIS IS THE FIX FOR THE BUG THAT KILLED A DESKTOP'S AUDIO, and it is worth
 * the whole comment because every part of it looked right.
 *
 * projectM-pulseaudio does NOT honour PULSE_SOURCE. It enumerates sources with
 * pa_context_get_source_info_list(), connects to one BY NAME, and remembers the
 * choice in ~/.config/projectM/qprojectM-pulseaudio.conf. The saved value on
 * this developer's machine was:
 *
 *     pulseAudioDeviceName=bluez_input.F4:B6:2D:DA:0E:BD
 *
 * — the MICROPHONE of a Bluetooth headset. And a Bluetooth device cannot do
 * high-fidelity playback and microphone input at the same time: opening that
 * mic makes WirePlumber switch the card from A2DP to the HSP/HFP headset
 * profile, which
 *
 *   · drops the OUTPUT to 16kHz mono — everything suddenly quiet and muffled,
 *     with the volume control making no difference, because the level was
 *     never the problem;
 *   · destroys and rebuilds the sink, which breaks every stream playing
 *     through it — so the music stopped and the player froze.
 *
 * Pressing Visualizer therefore killed the music it was meant to draw, and
 * left the whole desktop's audio broken until somebody knew to run
 * `pactl set-card-profile <card> a2dp-sink`. Nothing in it looked like the
 * visualizer's fault.
 *
 * So the device is WRITTEN, not suggested — and it is written only when it is
 * a monitor.
 *
 * ⚠ AND `tryFirstAvailablePlaybackMonitor` IS SET TO FALSE, WHICH IS THE
 * OPPOSITE OF WHAT IT SOUNDS LIKE. It reads as the safety net — "if all else
 * fails, find a playback monitor" — and it is not: while it is true projectM
 * runs its own scan INSTEAD of opening the device named above, and that scan
 * is what picks a microphone. Measured, both ways round, on this machine:
 *
 *     pulseAudioDeviceName=<the monitor>  tryFirst=false  → the monitor  ✓
 *     pulseAudioDeviceName=<the monitor>  tryFirst=true   → the analog MIC
 *
 * The name is re-resolved on every launch anyway, so nothing is lost by
 * turning the scan off: a monitor that has gone away since last time is
 * replaced before projectM ever sees the file.
 *
 * Every other key in the file is preserved: it is projectM's own config, not
 * ours, and it holds a window position and a preset playlist.
 */
static int visualizer_pin_device(const char *monitor)
{
	char path[SYN_PATH];
	if (!config_path(path, sizeof(path), "projectM/qprojectM-pulseaudio.conf"))
		return EX_FAIL;

	char *old = read_file(path);

	char *out = NULL;
	size_t len = 0;
	FILE *m = open_memstream(&out, &len);
	if (!m) {
		free(old);
		return EX_FAIL;
	}

	fputs("[General]\n", m);
	fprintf(m, "pulseAudioDeviceName=%s\n", monitor);
	fputs("tryFirstAvailablePlaybackMonitor=false\n", m);

	/* Anything else that was in there, minus the two keys just written and
	 * the group header they live under. */
	if (old) {
		char *save = NULL;
		for (char *ln = strtok_r(old, "\n", &save); ln;
		     ln = strtok_r(NULL, "\n", &save)) {
			char *t = trim(ln);
			if (!*t)
				continue;
			if (strcmp(t, "[General]") == 0)
				continue;
			if (strncmp(t, "pulseAudioDeviceName=", 21) == 0)
				continue;
			if (strncmp(t, "tryFirstAvailablePlaybackMonitor=", 33) == 0)
				continue;
			fprintf(m, "%s\n", t);
		}
		free(old);
	}
	fclose(m);

	int rc = write_file_inplace(path, out);
	free(out);
	return rc < 0 ? EX_FAIL : EX_OK;
}

/*
 * projectM, pointed at whatever this machine is playing.
 *
 * ⚠ THE DEFAULT CAPTURE DEVICE IS A MICROPHONE, which is the whole reason this
 * is a command rather than the program's name in the tile table. What is wanted
 * is the MONITOR of the sink the music is going to.
 *
 * ⚠ And it must be a monitor or NOTHING. Refusing to start is the right answer
 * to "no monitor could be found": the alternative is a program that opens a
 * microphone, and on a Bluetooth headset that does not merely listen to the
 * wrong thing — it forces the card into headset mode and takes the machine's
 * audio down with it. See visualizer_pin_device().
 *
 * ⚠ The device is resolved at LAUNCH, not written once at install: this
 * machine's default output is a Bluetooth headset one hour and an HDMI
 * television the next, and a visualizer wired to the wrong one is silent with
 * no explanation.
 *
 * PULSE_SOURCE and SDL_AUDIO_INCLUDE_MONITORS are still set — they cost
 * nothing, and they are what the SDL build and any other future one read.
 * They are no longer relied upon.
 */
static int big_visualizer(void)
{
	const char *prog = visualizer_prog();
	if (!prog) {
		fputs("syn-arcade: projectM is not installed — "
		      "synpkg install projectm-pulseaudio\n", stderr);
		return EX_FAIL;
	}

	char monitor[320] = "";
	if (!monitor_source(monitor, sizeof(monitor))) {
		fputs("syn-arcade: no audio monitor to listen to, so the "
		      "visualizer is not being started.\n"
		      "\n"
		      "It would open a microphone instead — which on a "
		      "Bluetooth headset switches the\n"
		      "card into headset mode and takes the machine's audio "
		      "down with it. Check that\n"
		      "something is playing and that `pactl list sources "
		      "short` shows a .monitor.\n", stderr);
		return EX_FAIL;
	}

	if (visualizer_pin_device(monitor) != EX_OK)
		fprintf(stderr, "syn-arcade: could not write projectM's own "
			"config — it may open a microphone instead of %s\n",
			monitor);

	setenv("PULSE_SOURCE", monitor, 1);
	setenv("SDL_AUDIO_INCLUDE_MONITORS", "1", 1);

	execlp(prog, prog, (char *)NULL);
	fprintf(stderr, "syn-arcade: could not start %s\n", prog);
	return EX_FAIL;
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
 * One ATTRIBUTE of one element: `<Track title="…" …>`.
 *
 * The news reader wants the text between two tags; Plex puts everything on
 * attributes and one element per row, so it wants this instead. Same file,
 * same reasoning: every shape read here is fixed and known, and an XML library
 * would be a dependency for two loops.
 *
 * ⚠ THE LEADING SPACE IN THE PATTERN IS LOAD-BEARING. Plex's album rows carry
 * both `title=` and `parentTitle=`, and a search for `title="` finds the
 * artist's name on every row — an album list where every album is called after
 * the band. A value cannot contain a raw quote (Plex escapes it as &#34;), so
 * ` name="` cannot appear inside another attribute's text.
 *
 * ⚠ BOUNDED BY THE ELEMENT'S OWN '>'. Without it, an element that happens not
 * to carry the attribute silently borrows the next element's — which is the
 * same failure json_str's `end` argument exists to stop.
 */
static bool xml_attr(const char *tag, const char *name, char *out, size_t n)
{
	const char *end = strchr(tag, '>');

	char pat[64];
	snprintf(pat, sizeof(pat), " %s=\"", name);

	const char *p = strstr(tag, pat);
	if (!p || (end && p > end))
		return false;
	p += strlen(pat);

	const char *q = strchr(p, '"');
	if (!q)
		return false;

	size_t len = (size_t)(q - p);
	if (len >= n)
		len = n - 1;
	memcpy(out, p, len);
	out[len] = '\0';
	xml_unescape(out);
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
 * Which screen big screen mode should open on.
 *
 * A layer-shell client cannot work this out for itself: there is no Wayland
 * protocol that tells one where the pointer or the keyboard focus is, and none
 * that says which monitor a person calls their main one. synui knows both,
 * because it is the compositor, and `synctl outputs` prints them — the same
 * reason synui has to pass an output name into the start menu rather than
 * letting it choose.
 *
 * Resolved HERE and not in the QML so that the shell is handed a name, and so
 * that `big status` can say where the next start will land.
 *
 * ⚠ THE DEFAULT IS `primary`, AND `focused` IS WHY.
 *
 * This used to be "wherever the pointer is", unconditionally. That reads well —
 * it is what a person running `big start` from a terminal on the second monitor
 * means — but it has no answer at all for the case big screen mode is most used
 * in: `big autostart on`, where the shell opens AT LOGIN. Nobody has pointed at
 * anything yet, so the cursor is wherever the compositor parked it, and the ten-
 * foot interface opens on whatever monitor that happened to be — a portrait side
 * panel as readily as the television. Nothing about that is diagnosable from the
 * screen it lands on.
 *
 * synui had already answered the same question for game windows: game_output
 * defaults to `primary` for exactly this reason (see game.c and the comment on
 * server_primary_output — "no primary is what makes SDL games open on an
 * arbitrary monitor"). Two gaming tools on one desktop disagreeing about which
 * screen is the gaming screen is the papercut nobody remembers the answer to,
 * so this takes synui's answer and its spelling.
 *
 * `primary` is not the raw flag: ipc.c reports the EFFECTIVE primary, which
 * falls back to the largest enabled output, so it is always somebody. That
 * matters here — a fresh install where nobody has pressed the display panel's
 * p key still resolves, and it resolves to the biggest screen.
 */
static bool output_flagged(const char *flag, char *buf, size_t n)
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

	char want[32];
	snprintf(want, sizeof(want), "\"%s\":true", flag);

	/* One object per output, and "name" comes before both flags in each. So
	 * walk the objects and answer with the name of the one that claims the
	 * flag — no JSON parser for a shape this file also controls. */
	for (char *obj = strtok(json, "{"); obj; obj = strtok(NULL, "{")) {
		if (!strstr(obj, want))
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

/* Is that connector actually here? Asked before honouring a name out of the
 * config, because a monitor named in a file six months ago may be unplugged
 * today, and handing the shell a name that matches no screen is worse than
 * having no preference: Quickshell.screens has nothing to match, and the
 * interface opens on the first screen with no sign of why. */
static bool output_exists(const char *name)
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

	char want[160];
	snprintf(want, sizeof(want), "\"name\":\"%s\"", name);
	return strstr(json, want) != NULL;
}

/*
 * The configured preference: `output = primary | focused | <connector>` in
 * ~/.config/syn-arcade/big.conf.
 *
 * A whole file for one key looks like a lot, and it is deliberate: this is the
 * setting somebody reaches for when the television is not where the interface
 * opened, and a setting that can only be spelled as a flag on a command line is
 * no use to `big autostart`, which is the case that needs it most.
 */
static void big_output_pref(char *buf, size_t n)
{
	snprintf(buf, n, "primary");

	char path[SYN_PATH];
	if (!config_path(path, sizeof(path), "syn-arcade/big.conf"))
		return;
	char *text = read_file(path);
	if (!text)
		return;

	char *save = NULL;
	for (char *ln = strtok_r(text, "\n", &save); ln;
	     ln = strtok_r(NULL, "\n", &save)) {
		char *t = trim(ln);
		if (!*t || *t == '#')
			continue;
		char *eq = strchr(t, '=');
		if (!eq)
			continue;
		*eq = '\0';
		char *key = trim(t);
		char *val = trim(eq + 1);
		if (strcmp(key, "output") == 0 && *val)
			snprintf(buf, n, "%s", val);
	}
	free(text);
}

/*
 * The whole policy, in one place: --output, then the config, then the default.
 *
 * Never fails. No synui, no synctl, an unplugged monitor named in the config, or
 * output printed in some shape this does not recognise all mean "no preference",
 * and the shell falls back to its first screen. A launcher that refused to open
 * because it could not decide which monitor to be on would be a worse answer
 * than being on the wrong one.
 */
static void resolve_output(const char *cli, char *buf, size_t n)
{
	buf[0] = '\0';

	char pref[128];
	if (cli && *cli)
		snprintf(pref, sizeof(pref), "%s", cli);
	else
		big_output_pref(pref, sizeof(pref));

	if (strcmp(pref, "focused") == 0) {
		output_flagged("focused", buf, n);
		return;
	}
	if (strcmp(pref, "primary") == 0) {
		output_flagged("primary", buf, n);
		return;
	}

	/* A connector name. Honoured only if it is plugged in — otherwise say
	 * so and fall back, rather than opening somewhere unexplained. */
	if (output_exists(pref)) {
		snprintf(buf, n, "%s", pref);
		return;
	}
	fprintf(stderr, "syn-arcade: no output called '%s' — using the primary "
			"screen (`synctl outputs` lists them)\n", pref);
	output_flagged("primary", buf, n);
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
/* ── Can this compositor show a big screen at all? ───────────────────────────
 *
 * Big screen mode is TWO quickshell PanelWindows, which are wlr-layer-shell
 * surfaces. That protocol is a wlroots one: synui offers it, KWin offers it,
 * and mutter never has. Under GNOME the shell starts, fails to map anything,
 * and exits — no window, no error the user ever sees, an entry in the app grid
 * that does nothing when clicked. Confirmed on a GNOME session, 2026-08-18.
 *
 * Asked of the REGISTRY rather than of XDG_CURRENT_DESKTOP. The desktop name
 * is a policy answer to a technical question: it is wrong about a wlroots
 * compositor nobody has heard of, wrong about the two names synui itself logs
 * in under, and would have to be edited every time either list changes. The
 * registry is the compositor answering for itself.
 *
 * Compared against the interface NAME as a string, so this needs no generated
 * protocol binding and adds nothing to the build — nothing here binds the
 * global, it only asks whether it is advertised.
 *
 * Returns 1 yes, 0 no, -1 cannot be told (no display to ask). -1 is NOT a
 * refusal: a machine that cannot answer must not be blocked from trying, which
 * is the same posture the synctl callers above take.
 */
struct ls_probe { int found; };

static void ls_global(void *data, struct wl_registry *reg, uint32_t name,
		      const char *iface, uint32_t version)
{
	(void)reg; (void)name; (void)version;
	if (strcmp(iface, "zwlr_layer_shell_v1") == 0)
		((struct ls_probe *)data)->found = 1;
}

static void ls_global_remove(void *data, struct wl_registry *reg, uint32_t name)
{
	(void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener ls_listener = {
	.global        = ls_global,
	.global_remove = ls_global_remove,
};

static int have_layer_shell(void)
{
	struct wl_display *d = wl_display_connect(NULL);
	if (!d)
		return -1;

	struct ls_probe p = { 0 };
	struct wl_registry *reg = wl_display_get_registry(d);
	wl_registry_add_listener(reg, &ls_listener, &p);
	wl_display_roundtrip(d);		/* one trip: the globals arrive together */

	wl_registry_destroy(reg);
	wl_display_disconnect(d);
	return p.found;
}

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

	/* Before quickshell, so the refusal comes from the thing that knows why
	 * rather than as silence from a shell that started and mapped nothing. */
	if (have_layer_shell() == 0) {
		const char *de = getenv("XDG_CURRENT_DESKTOP");
		fprintf(stderr,
			"syn-arcade: %s does not offer zwlr_layer_shell_v1, and "
			"big screen mode is built from layer-shell surfaces — "
			"there is nothing it can map here.\n"
			"            It needs synui, or another wlroots-based "
			"compositor.\n",
			(de && *de) ? de : "this compositor");
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
	 * the compositor before falling back to that. See resolve_output. */
	char out[128] = "";
	resolve_output(output, out, sizeof(out));
	setenv("SYN_BIG_OUTPUT", out, 1);

	/* The dendrite mark for the header. Resolved HERE for the same reason
	 * every tile glyph is: the shell is a renderer handed a path that
	 * exists or an empty string, and it never has to know where this
	 * package installed itself or that there is a source tree. An empty
	 * value draws no emblem rather than a broken-image box. */
	setenv("SYN_BIG_LOGO", icon_file("synapse"), 1);

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

/*
 * Which screen big screen mode opens on: read, or set.
 *
 * The setting has existed since 0.1.0-11 and could only be spelled by editing
 * ~/.config/syn-arcade/big.conf by hand — which is no use at all to the case it
 * exists for, somebody sitting in front of a television that the interface just
 * opened on the wrong monitor. A verb makes it reachable from the window.
 *
 * ⚠ The connector is CHECKED against what is plugged in. A name that matches no
 * screen is not an error the compositor will report — the shell simply falls
 * back to its first screen, which is indistinguishable from the setting being
 * ignored.
 */
static int big_output(const char *want, bool rec)
{
	if (!want) {
		char pref[128], screen[128] = "";
		big_output_pref(pref, sizeof(pref));
		resolve_output(NULL, screen, sizeof(screen));

		if (rec) {
			rec_row(3, "id", "label", "current");
			rec_row(3, "primary", "Main screen",
				strcmp(pref, "primary") == 0 ? "current" : "-");
			rec_row(3, "focused", "Wherever the pointer is",
				strcmp(pref, "focused") == 0 ? "current" : "-");

			/* Every connector by name, so the window can offer a
			 * specific monitor without knowing what one is called. */
			char *out = run_capture((char *const[]){ "synctl",
					"outputs", NULL }, 2);
			if (out) {
				for (char *obj = strtok(out, "{"); obj;
				     obj = strtok(NULL, "{")) {
					char *nm = strstr(obj, "\"name\":\"");
					if (!nm)
						continue;
					nm += 8;
					char *end = strchr(nm, '"');
					if (!end)
						continue;
					*end = '\0';
					rec_row(3, nm, nm,
						strcmp(pref, nm) == 0 ? "current" : "-");
				}
				free(out);
			}
			return EX_OK;
		}

		printf("%s  (%s)\n", screen[0] ? screen : "first screen", pref);
		return EX_OK;
	}

	if (strcmp(want, "primary") != 0 && strcmp(want, "focused") != 0 &&
	    !output_exists(want)) {
		fprintf(stderr, "syn-arcade: no screen called '%s'. Use "
			"`primary`, `focused`, or a connector name from "
			"`synctl outputs`\n", want);
		return EX_USAGE;
	}

	if (big_conf_set("output", want) != EX_OK) {
		fputs("syn-arcade: could not write big.conf\n", stderr);
		return EX_FAIL;
	}
	printf("big screen mode opens on %s\n", want);
	return EX_OK;
}

/*
 * Which music player: read, set, or list the ones installed.
 *
 * ⚠ cliamp is not merely first in the list, and the window has to be able to
 * say so: it is the only player big screen mode can DRIVE rather than launch.
 * With it, Music is a tile that starts the music and a row in the menu that
 * controls it; with anything else it is a tile that opens a window somebody
 * then has to get out of with a gamepad. A picker that presented fourteen
 * equal choices would be hiding the one fact that matters.
 */
static int big_player(const char *want, bool rec)
{
	static const char *const cands[] = {
		"cliamp",
		"strawberry", "elisa", "amberol", "rhythmbox", "lollypop",
		"clementine", "audacious", "deadbeef", "quodlibet", "tauon",
		"plexamp", "spotify", "vlc", NULL
	};

	if (!want) {
		const char *now = music_prog();
		char named[128] = "";
		bool pinned = big_conf_get("music", named, sizeof(named));

		if (rec) {
			rec_row(4, "id", "label", "current", "note");
			for (int i = 0; cands[i]; i++) {
				if (!have(cands[i]))
					continue;
				rec_row(4, cands[i], cands[i],
					now && strcmp(now, cands[i]) == 0
						? "current" : "-",
					strcmp(cands[i], "cliamp") == 0
						? "played without a window"
						: "opens its own window");
			}
			/* A player named in the config that is not installed is
			 * a row too — otherwise the window shows the fallback
			 * as though it were the choice, and the config file
			 * says something nobody can see. */
			if (pinned && *named && !have(named))
				rec_row(4, named, named, "-", "NOT INSTALLED");
			return EX_OK;
		}

		printf("%s%s\n", now ? now : "(none installed)",
		       pinned ? "   (named in big.conf)" : "   (first installed)");
		return EX_OK;
	}

	if (!have(want)) {
		fprintf(stderr, "syn-arcade: %s is not installed\n", want);
		return EX_FAIL;
	}

	if (big_conf_set("music", want) != EX_OK) {
		fputs("syn-arcade: could not write big.conf\n", stderr);
		return EX_FAIL;
	}

	/* ⚠ Said on every change, because the difference is the whole reason
	 * somebody would come here and it is invisible until they are back on
	 * the sofa with a pad in their hand. */
	if (strcmp(want, "cliamp") == 0)
		printf("music player: cliamp — played without a window, with "
		       "transport in the Start menu\n");
	else
		printf("music player: %s — the Music tile opens its window; "
		       "only cliamp can be driven from the sofa\n", want);
	return EX_OK;
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

	/* Where the NEXT start will land, and the rule that decided it. Worth a
	 * line because the failure this answers — the interface opening on the
	 * wrong monitor — leaves no evidence anywhere else, and the screen it
	 * opened on is the one thing that cannot explain itself. */
	char pref[128], screen[128] = "";
	big_output_pref(pref, sizeof(pref));
	resolve_output(NULL, screen, sizeof(screen));
	char screenbuf[300];
	snprintf(screenbuf, sizeof(screenbuf), "%s (%s)",
		 screen[0] ? screen : "first screen", pref);

	if (rec) {
		rec_row(3, "field", "value", "action");
		rec_row(3, "running", running ? "yes" : "no",
			running ? "action:stop" : "action:start");
		rec_row(3, "pid", pidbuf, "detail");
		rec_row(3, "at login", autostart ? "on" : "off",
			autostart ? "action:autostart-off" : "action:autostart-on");
		rec_row(3, "screen", screenbuf, "detail");
		/* The PREFERENCE as written, beside the screen it resolved to.
		 * The window needs the rule to show which chip is chosen; the
		 * resolved name is what a person reads. */
		rec_row(3, "screen rule", pref, "choice:big-output");
		rec_row(3, "guide button", guide ? "on" : "off",
			guide ? "action:guide-off" : "action:guide-on");
		rec_row(3, "music player", music_prog() ? music_prog()
			: "NONE INSTALLED", "choice:big-player");
		rec_row(3, "cliamp", have("cliamp") ? "installed"
			: "NOT INSTALLED", "detail");
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
	printf("  screen         %s\n", screenbuf);
	printf("  guide button   %s\n", guide ? "on — opens this from the desktop"
	     : "off (`syn-arcade big guide on`)");
	printf("  music player   %s%s\n", music_prog() ? music_prog() : "none installed",
	       have("cliamp") ? "" : "   (cliamp not installed — the Music tile "
				     "opens a window instead of playing)");
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

/* The one after it — `big music source plex`, `big music plex 15305`. Skips
 * flags for the same reason first_operand does: `--rec` may be anywhere. */
static const char *second_operand(int argc, char **argv)
{
	bool seen = false;
	for (int i = 0; i < argc; i++) {
		if (argv[i][0] == '-')
			continue;
		if (seen)
			return argv[i];
		seen = true;
	}
	return NULL;
}

/* And the one after THAT — `big music yt search <words>`, `big music yt add
 * <url>`. ⚠ It is one operand, not the rest of the line: a search is passed as
 * a single argv element by everything that calls it, which is what keeps a
 * quote or a semicolon in a song title a character rather than an escaping
 * problem for the caller. */
static const char *third_operand(int argc, char **argv)
{
	int seen = 0;
	for (int i = 0; i < argc; i++) {
		if (argv[i][0] == '-')
			continue;
		if (seen == 2)
			return argv[i];
		seen++;
	}
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

	/* Transport for a headless player. Unlike every other tile, Music is
	 * something the television DRIVES rather than opens — see big_music. */
	if (!strcmp(sub, "music"))
		return big_music(rest_c, rest, rec);

	/* The media buttons, which act on whatever is playing rather than on
	 * cliamp alone — see the header of that section for why the two are
	 * different commands and not one. */
	if (!strcmp(sub, "transport"))
		return big_transport(rest_c, rest, rec);

	/* projectM, told which audio to listen to. A tile in the Start menu
	 * runs this through `big run visualizer --wait`, which is what fills
	 * the screen and what brings the television back when it closes. */
	if (!strcmp(sub, "output"))
		return big_output(first_operand(rest_c, rest), rec);
	if (!strcmp(sub, "player"))
		return big_player(first_operand(rest_c, rest), rec);
	if (!strcmp(sub, "visualizer"))
		return big_visualizer();

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
