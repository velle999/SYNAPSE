/* util.c — paths, encoding, and the two ways this program gives up.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synplay.h"
#include "i18n.h"
#include "config.h"

#include <errno.h>
#include <locale.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

sp_out_t g_out = OUT_HUMAN;

/*
 * ⛔ LC_NUMERIC STAYS AT C, AND HERE IT IS NOT COSMETIC — IT IS THE WIRE.
 *
 * setlocale(LC_ALL, "") changes what printf's %f writes AND what atof() reads.
 * This program builds mpv's JSON with snprintf:
 *
 *     snprintf(args, sizeof args, "\"seek\",%.3f,\"relative\"", atof(s));
 *
 * A German locale turns that into `"seek",12,500,"relative"` — four arguments
 * where mpv expects three, and a seek that either fails or goes somewhere
 * nobody asked for. The same separator reaches `s\tpos\t%.3f` in the serve
 * stream, where the window does parseFloat() on it and gets 12, and the history
 * file, where a position saved in one locale is read back wrong in another.
 *
 * ⚠ AND atof() IS THE HALF THAT IS EASY TO MISS. Even with every printf fixed,
 * a locale-aware strtod stops at the '.' in "12.5" and answers 12.
 */
void syn_play_i18n_init(void)
{
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "C");

	const char *dir = getenv("SYN_PLAY_LOCALEDIR");
	bindtextdomain(SYN_PLAY_GETTEXT_DOMAIN,
	               dir && *dir ? dir : SYNPLAY_LOCALEDIR);
	bind_textdomain_codeset(SYN_PLAY_GETTEXT_DOMAIN, "UTF-8");
	textdomain(SYN_PLAY_GETTEXT_DOMAIN);
}

void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("syn-play: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(1);
}

void warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("syn-play: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

static const char *home_dir(void)
{
	const char *h = getenv("HOME");
	if (h && *h) return h;
	struct passwd *pw = getpwuid(getuid());
	return (pw && pw->pw_dir) ? pw->pw_dir : "/tmp";
}

/* mkdir -p. Every path here is built by this program, so the only failures
 * worth reporting are the ones that stop it working. */
int sp_mkdirs(const char *path)
{
	char tmp[PATH_MAX];
	size_t n = strlen(path);
	if (n == 0 || n >= sizeof tmp) return -1;
	memcpy(tmp, path, n + 1);
	if (tmp[n - 1] == '/') tmp[n - 1] = '\0';
	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return -1;
		*p = '/';
	}
	if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return -1;
	return 0;
}

/*
 * ⚠ SYNPLAY_HOME OVERRIDES EVERY PATH BELOW, and the test suite sets it.
 * A program that keeps a history of what somebody has watched must be testable
 * without writing into the history of whoever is running the tests — and the
 * build user's home is exactly that. Structural, rather than careful.
 */
const char *sp_data_dir(void)
{
	static char buf[PATH_MAX];
	if (!buf[0]) {
		const char *o = getenv("SYNPLAY_HOME");
		const char *x = getenv("XDG_DATA_HOME");
		if (o && *o)
			snprintf(buf, sizeof buf, "%s/share/syn-play", o);
		else if (x && *x)
			snprintf(buf, sizeof buf, "%s/syn-play", x);
		else
			snprintf(buf, sizeof buf, "%s/.local/share/syn-play", home_dir());
		sp_mkdirs(buf);
	}
	return buf;
}

const char *sp_config_dir(void)
{
	static char buf[PATH_MAX];
	if (!buf[0]) {
		const char *o = getenv("SYNPLAY_HOME");
		const char *x = getenv("XDG_CONFIG_HOME");
		if (o && *o)
			snprintf(buf, sizeof buf, "%s/config/syn-play", o);
		else if (x && *x)
			snprintf(buf, sizeof buf, "%s/syn-play", x);
		else
			snprintf(buf, sizeof buf, "%s/.config/syn-play", home_dir());
		sp_mkdirs(buf);
	}
	return buf;
}

const char *sp_playlist_dir(void)
{
	static char buf[PATH_MAX];
	if (!buf[0]) {
		snprintf(buf, sizeof buf, "%s/playlists", sp_data_dir());
		sp_mkdirs(buf);
	}
	return buf;
}

const char *sp_history_path(void)
{
	static char buf[PATH_MAX];
	if (!buf[0]) snprintf(buf, sizeof buf, "%s/history.tsv", sp_data_dir());
	return buf;
}

/*
 * ⚠ THE SOCKET FOLLOWS SYNPLAY_HOME TOO. Two suites running at once, or a
 * suite running beside a real session, must not talk to each other's mpv — and
 * a test that quits "the" player would otherwise stop the music somebody was
 * listening to. Same family as the live-seat rules elsewhere in this repo.
 */
const char *sp_socket_path(void)
{
	static char buf[PATH_MAX];
	if (!buf[0]) {
		const char *o = getenv("SYNPLAY_SOCKET");
		if (o && *o) {
			snprintf(buf, sizeof buf, "%s", o);
			return buf;
		}
		const char *h = getenv("SYNPLAY_HOME");
		const char *r = getenv("XDG_RUNTIME_DIR");
		if (h && *h)      snprintf(buf, sizeof buf, "%s/syn-play.sock", h);
		else if (r && *r) snprintf(buf, sizeof buf, "%s/syn-play.sock", r);
		else              snprintf(buf, sizeof buf, "/tmp/syn-play-%u.sock",
		                           (unsigned)getuid());
	}
	return buf;
}

/* ── percent-encoding ───────────────────────────────────────────────────── */
/*
 * A path may contain a tab; a title may contain a newline. Both are record
 * separators here and on the wire to the window, so every field that came from
 * a filesystem or a media file goes through this. Kept deliberately narrow:
 * anything outside a small safe set is encoded, so there is no argument later
 * about which byte was allowed.
 */
static bool safe_byte(unsigned char c)
{
	if (c >= 'a' && c <= 'z') return true;
	if (c >= 'A' && c <= 'Z') return true;
	if (c >= '0' && c <= '9') return true;
	return strchr(" ._-+:/@,()[]{}!?'\"=<>*#$&;|~^", c) != NULL;
}

void sp_enc(const char *in, char *out, size_t cap)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t o = 0;
	if (cap == 0) return;
	for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
		if (safe_byte(*p)) {
			if (o + 1 >= cap) break;
			out[o++] = (char)*p;
		} else {
			if (o + 3 >= cap) break;
			out[o++] = '%';
			out[o++] = hex[*p >> 4];
			out[o++] = hex[*p & 15];
		}
	}
	out[o] = '\0';
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

void sp_dec(const char *in, char *out, size_t cap)
{
	size_t o = 0;
	if (cap == 0) return;
	for (const char *p = in; *p && o + 1 < cap; p++) {
		if (*p == '%' && hexval(p[1]) >= 0 && hexval(p[2]) >= 0) {
			out[o++] = (char)((hexval(p[1]) << 4) | hexval(p[2]));
			p += 2;
		} else {
			out[o++] = *p;
		}
	}
	out[o] = '\0';
}

bool sp_getline(FILE *f, char *buf, size_t cap)
{
	if (!fgets(buf, (int)cap, f)) return false;
	size_t n = strlen(buf);
	while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
	return true;
}

/*
 * The name a person would use for this thing. For a file that is the basename
 * without its extension; for a URL it is whatever mpv will end up calling it,
 * which we do not know yet — so the URL itself, which at least identifies it.
 */
void sp_pretty_title(const char *path, char *out, size_t cap)
{
	if (!path || !*path) { snprintf(out, cap, "%s", ""); return; }
	if (strstr(path, "://")) { snprintf(out, cap, "%s", path); return; }

	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	snprintf(out, cap, "%s", base);

	char *dot = strrchr(out, '.');
	/* ⚠ Only a SHORT trailing extension. A file called "S01E02. The One
	 * Where They Argue" loses everything after the first dot to a naive
	 * strrchr-and-truncate — and a dotfile with no extension loses its name
	 * entirely. */
	if (dot && dot != out && strlen(dot) <= 6 && dot[1] != '\0')
		*dot = '\0';
}
