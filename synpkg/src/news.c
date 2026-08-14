/* news.c — the Arch Linux news gate.
 *
 * WHY AN UPGRADE ASKS FIRST
 *
 * Arch is a rolling distribution that occasionally requires a MANUAL step
 * before an upgrade will succeed — a keyring refreshed by hand, a config file
 * moved, a package replaced with `pacman -Syu --ignore`. When that happens the
 * only warning is a post on archlinux.org/news, and the failure mode for
 * missing it is not a friendly error: it is a machine that boots to a shell, or
 * a pacman that refuses every later transaction.
 *
 * `pacman -Syu` has never told anyone this. Every Arch user is expected to have
 * read the news by some other means, which in practice means most have not.
 * SynapseOS ships a graphical Update button, so the number of people upgrading
 * without ever seeing a news feed is effectively everyone.
 *
 * So: fetch the feed, show what has been published since this machine's last
 * full system upgrade, and ask. Nothing more clever than that.
 *
 * WHAT COUNTS AS "SINCE"
 *
 * The timestamp of the most recent `starting full system upgrade` line in
 * /var/log/pacman.log — a machine-wide fact, in the one place that already
 * records it. The alternative, a read-marker file, would live in a $HOME, and
 * this program runs as three different users over its life (you, pkexec's root,
 * sudo's root); a marker one of them wrote is a marker the others cannot see.
 * The log has no such problem and needs no maintenance: upgrading is what makes
 * news read.
 *
 * A FAILED CHECK MUST NOT BLOCK AN UPGRADE
 *
 * No network, DNS down, archlinux.org having a bad day — none of those are
 * reasons to refuse to install security updates. The check says so and gets out
 * of the way. It is an advisory, and an advisory that can wedge the updater is
 * worse than no advisory.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define NEWS_URL   "https://archlinux.org/feeds/news/"
#define PACMAN_LOG "/var/log/pacman.log"

/* The feed carries far more than anyone wants printed at a prompt. Ten is more
 * than a year of Arch news in practice. */
#define NEWS_MAX   10

/* How long a fetched feed is reused before going back to the network. See
 * news_fetch(): archlinux.org rate-limits, and this document changes a handful
 * of times a year. */
#define NEWS_TTL   (30 * 60)

/* Bodies are shown to say WHAT the manual step is; the whole post is not the
 * point, and a wall of text at a confirmation prompt is a wall of text people
 * scroll past. Past this, the link is the rest. */
#define BODY_MAX   1400

typedef struct {
	char  *title;
	char  *link;
	char  *body;    /* tags stripped, entities decoded, whitespace collapsed */
	time_t when;    /* 0 when the feed's date could not be parsed */
} news_item;

static void news_item_free(news_item *n)
{
	free(n->title);
	free(n->link);
	free(n->body);
}

/* ── XML, by hand ───────────────────────────────────────────────────────────
 *
 * A real parser would be a dependency, and this is one machine-generated feed
 * with a fixed shape. What matters is that nothing here trusts a length: every
 * scan is bounded by the end of the item it was handed, so a truncated download
 * (curl killed by --max-time mid-transfer) runs off the end of nothing.
 */

static void put_utf8(char **d, unsigned cp)
{
	char *p = *d;
	if (cp < 0x80) {
		*p++ = (char)cp;
	} else if (cp < 0x800) {
		*p++ = (char)(0xC0 | (cp >> 6));
		*p++ = (char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		*p++ = (char)(0xE0 | (cp >> 12));
		*p++ = (char)(0x80 | ((cp >> 6) & 0x3F));
		*p++ = (char)(0x80 | (cp & 0x3F));
	} else {
		*p++ = (char)(0xF0 | (cp >> 18));
		*p++ = (char)(0x80 | ((cp >> 12) & 0x3F));
		*p++ = (char)(0x80 | ((cp >> 6) & 0x3F));
		*p++ = (char)(0x80 | (cp & 0x3F));
	}
	*d = p;
}

/* Decode XML entities in place-ish, into a fresh buffer.
 *
 * The feed's descriptions are HTML escaped INTO XML, so a paragraph arrives as
 * `&lt;p&gt;`. That is why strip_html() below decodes AFTER stripping tags and
 * not before: decoding first would turn the escaped text into markup that the
 * stripper would then eat, and a news post that mentions `<hostname>` would
 * lose the word. */
static char *unescape(const char *s, size_t len)
{
	/* Every replacement is shorter than its entity except a 4-byte UTF-8
	 * sequence from a numeric reference, and the shortest of those (`&#x1F;`)
	 * is 6 characters. len + 1 is always enough. */
	char *out = xmalloc(len + 1);
	char *d = out;

	for (size_t i = 0; i < len; ) {
		if (s[i] != '&') {
			*d++ = s[i++];
			continue;
		}

		/* Bounded: a bare '&' near the end must not send strchr past len. */
		size_t j = i + 1;
		while (j < len && j - i < 12 && s[j] != ';' && s[j] != '&')
			j++;
		if (j >= len || s[j] != ';') {
			*d++ = s[i++];
			continue;
		}

		size_t n = j - i - 1;
		const char *e = s + i + 1;
		if (n == 3 && !strncmp(e, "amp", 3))        *d++ = '&';
		else if (n == 2 && !strncmp(e, "lt", 2))    *d++ = '<';
		else if (n == 2 && !strncmp(e, "gt", 2))    *d++ = '>';
		else if (n == 4 && !strncmp(e, "quot", 4))  *d++ = '"';
		else if (n == 4 && !strncmp(e, "apos", 4))  *d++ = '\'';
		else if (n == 4 && !strncmp(e, "nbsp", 4))  *d++ = ' ';
		else if (n > 1 && e[0] == '#') {
			unsigned cp = 0;
			if (e[1] == 'x' || e[1] == 'X')
				cp = (unsigned)strtoul(e + 2, NULL, 16);
			else
				cp = (unsigned)strtoul(e + 1, NULL, 10);
			if (cp == 0 || cp > 0x10FFFF)
				cp = '?';
			put_utf8(&d, cp);
		} else {
			/* Something we do not know. Keep it verbatim rather than dropping
			 * it: an unrecognised entity in a news title is still readable, and
			 * a silently deleted one is not. */
			memcpy(d, s + i, j - i + 1);
			d += j - i + 1;
		}
		i = j + 1;
	}
	*d = '\0';
	return out;
}

/* The text of <tag>…</tag> inside [start,end), raw (still escaped), or NULL. */
static char *tag_raw(const char *start, const char *end, const char *tag)
{
	char open[32], close[32];
	snprintf(open, sizeof open, "<%s>", tag);
	snprintf(close, sizeof close, "</%s>", tag);

	const char *o = start;
	size_t olen = strlen(open);
	for (; o + olen <= end; o++)
		if (!strncmp(o, open, olen))
			break;
	if (o + olen > end)
		return NULL;
	o += olen;

	const char *c = o;
	size_t clen = strlen(close);
	for (; c + clen <= end; c++)
		if (!strncmp(c, close, clen))
			break;
	if (c + clen > end)
		return NULL;

	/* <![CDATA[ … ]]> around the payload, which the feed uses for some fields
	 * and not others. */
	if ((size_t)(c - o) > 12 && !strncmp(o, "<![CDATA[", 9)) {
		o += 9;
		if (c - o >= 3 && !strncmp(c - 3, "]]>", 3))
			c -= 3;
	}

	char *raw = xmalloc((size_t)(c - o) + 1);
	memcpy(raw, o, (size_t)(c - o));
	raw[c - o] = '\0';
	return raw;
}

static char *tag_text(const char *start, const char *end, const char *tag)
{
	char *raw = tag_raw(start, end, tag);
	if (!raw)
		return NULL;
	char *out = unescape(raw, strlen(raw));
	free(raw);
	return out;
}

/* Markup out, whitespace collapsed to single spaces. Block-level closers become
 * a space too, or the last word of a paragraph would run into the first word of
 * the next. */
static char *strip_html(const char *html)
{
	size_t len = strlen(html);
	char *flat = xmalloc(len + 1);
	char *d = flat;
	bool in_tag = false, sp = true;   /* sp: suppress a leading space */

	for (size_t i = 0; i < len; i++) {
		char c = html[i];
		if (c == '<') { in_tag = true; continue; }
		if (c == '>') {
			in_tag = false;
			if (!sp) { *d++ = ' '; sp = true; }
			continue;
		}
		if (in_tag)
			continue;
		if (isspace((unsigned char)c)) {
			if (!sp) { *d++ = ' '; sp = true; }
			continue;
		}
		*d++ = c;
		sp = false;
	}
	while (d > flat && d[-1] == ' ')
		d--;
	*d = '\0';

	char *out = unescape(flat, strlen(flat));
	free(flat);
	return out;
}

/* ── time ───────────────────────────────────────────────────────────────────
 *
 * RFC 822: "Mon, 11 Aug 2026 14:05:00 +0000".
 *
 * Hand-parsed rather than strptime()'d, because %a and %b are LOCALE
 * DEPENDENT. This program does not call setlocale() today, so strptime would
 * work — right up until something else in the suite does, at which point a
 * machine set to fr_FR silently stops recognising every date in the feed and
 * every news item becomes "undated". A three-letter month table cannot develop
 * that problem.
 */
static time_t rfc822_time(const char *s)
{
	static const char *MON[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	if (!s)
		return 0;

	/* Skip the optional day name and any leading space. */
	while (*s && !isdigit((unsigned char)*s))
		s++;

	int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
	char mon[4] = { 0 };
	if (sscanf(s, "%d %3s %d %d:%d:%d", &day, mon, &year, &hh, &mm, &ss) < 5)
		return 0;

	int m = -1;
	for (int i = 0; i < 12; i++)
		if (!strncasecmp(mon, MON[i], 3))
			m = i;
	if (m < 0 || day < 1 || day > 31 || year < 1970)
		return 0;

	struct tm tm = { 0 };
	tm.tm_mday = day;
	tm.tm_mon  = m;
	tm.tm_year = year - 1900;
	tm.tm_hour = hh;
	tm.tm_min  = mm;
	tm.tm_sec  = ss;

	time_t t = timegm(&tm);
	if (t == (time_t)-1)
		return 0;

	/* Trailing numeric zone, if any. The Arch feed is always +0000, but a feed
	 * read through a proxy or a mirror need not be, and an unhandled offset is
	 * a silent half-day of error either side of a boundary. */
	const char *z = strrchr(s, '+');
	const char *neg = strrchr(s, '-');
	int sign = 1;
	if (neg && (!z || neg > z)) { z = neg; sign = -1; }
	if (z && isdigit((unsigned char)z[1]) && isdigit((unsigned char)z[2]) &&
	    isdigit((unsigned char)z[3]) && isdigit((unsigned char)z[4])) {
		int off = (z[1] - '0') * 10 + (z[2] - '0');
		int omin = (z[3] - '0') * 10 + (z[4] - '0');
		t -= sign * (off * 3600 + omin * 60);
	}
	return t;
}

/* The most recent `starting full system upgrade` in pacman.log, or 0.
 *
 * 0 means "cannot tell", NOT "never" — the caller must treat the two
 * differently, because "never upgraded" would make every item unread and turn
 * the gate into a wall of a year's news on a machine whose log was simply
 * rotated away. */
static time_t last_sysupgrade(void)
{
	/* Overridable so the test suite can hand it a log with known timestamps.
	 * Without this the only way to test the cutoff is to upgrade the machine
	 * running the tests, which is not a test. Same shape as SYNPKG_CURATED. */
	const char *path = getenv("SYNPKG_PACMAN_LOG");
	if (!path || !*path)
		path = PACMAN_LOG;

	FILE *f = fopen(path, "re");
	if (!f)
		return 0;

	time_t best = 0;
	char line[512];

	/* THE WHOLE FILE, from the top.
	 *
	 * This read only the last 256 KiB at first — the marker is normally near
	 * the end, and pacman.log grows without bound. It was wrong on the very
	 * machine it was written on: the log was 519 KiB, the last full system
	 * upgrade was six days back, and every line since was hooks, scriptlets and
	 * `pacman -U` from a week of package builds. The marker sat outside the
	 * window, the cutoff came back "unknown", and the gate fell through to
	 * showing a fixed handful of items on every run.
	 *
	 * Any fixed window has that failure, and it is silent — the fallback looks
	 * like working software. A busy machine is exactly the one that scrolls the
	 * marker out of reach, and it is also the one that upgrades most often.
	 *
	 * So: read it all. A megabyte of fgets is unmeasurable next to the upgrade
	 * this is about to gate, and it cannot be wrong. */
	while (fgets(line, sizeof line, f)) {
		/* TWO frontends write this, and the substring is what they share:
		 *
		 *   [PACMAN] starting full system upgrade     — the pacman COMMAND
		 *   [SYNPKG] completed full system upgrade    — trans.c, see below
		 *
		 * `starting full system upgrade` is logged by pacman.c, NOT by libalpm.
		 * A program that drives libalpm directly — which is what synpkg is —
		 * upgrades the whole system and leaves nothing behind but the
		 * individual `upgraded <pkg>` lines. Matching only pacman's wording
		 * pinned this machine's cutoff at 2026-08-07, the last time anyone had
		 * typed `pacman -Syu` by hand, while synpkg had upgraded it since; the
		 * same news would then have been shown at every upgrade, for ever,
		 * which is how a warning becomes something people press through.
		 *
		 * Not `transaction started`, which libalpm DOES log: that fires for
		 * every install and every removal, so installing one package would mark
		 * the news read. Wrong in the dangerous direction. */
		if (!strstr(line, "full system upgrade"))
			continue;

		struct tm tm = { 0 };
		int off_h = 0, off_m = 0;
		char sign = '+';

		/* Current pacman: [2026-08-11T14:05:00-0500]
		 * Older pacman:   [2026-08-11 14:05] */
		int n = sscanf(line, "[%d-%d-%dT%d:%d:%d%c%2d%2d]",
		               &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
		               &tm.tm_hour, &tm.tm_min, &tm.tm_sec,
		               &sign, &off_h, &off_m);
		if (n < 6) {
			n = sscanf(line, "[%d-%d-%d %d:%d]", &tm.tm_year, &tm.tm_mon,
			           &tm.tm_mday, &tm.tm_hour, &tm.tm_min);
			if (n < 5)
				continue;
			/* No zone in the old format: it was local time. */
			tm.tm_year -= 1900; tm.tm_mon -= 1; tm.tm_isdst = -1;
			time_t t = mktime(&tm);
			if (t != (time_t)-1 && t > best)
				best = t;
			continue;
		}

		tm.tm_year -= 1900;
		tm.tm_mon  -= 1;
		time_t t = timegm(&tm);
		if (t == (time_t)-1)
			continue;
		if (n >= 9) {
			int secs = off_h * 3600 + off_m * 60;
			t -= (sign == '-') ? -secs : secs;
		}
		if (t > best)
			best = t;
	}

	fclose(f);
	return best;
}

/* ── the feed ───────────────────────────────────────────────────────────── */

/* Where the fetched feed is kept between runs. Beside the AUR checkouts, in the
 * cache of whoever is running — which is the right scope: the gate runs
 * unprivileged, so a machine-wide /var/cache path would need root to write and
 * the one process that must never need root is the one asking the question. */
static char *cache_path(void)
{
	const char *xdg = getenv("XDG_CACHE_HOME");
	if (xdg && *xdg)
		return xasprintf("%s/synpkg/news.xml", xdg);
	const char *home = getenv("HOME");
	if (!home || !*home)
		return NULL;
	return xasprintf("%s/.cache/synpkg/news.xml", home);
}

static char *read_file(const char *path)
{
	FILE *f = fopen(path, "re");
	if (!f)
		return NULL;

	size_t cap = 64 * 1024, len = 0;
	char *buf = xmalloc(cap);
	size_t n;
	while ((n = fread(buf + len, 1, cap - len - 1, f)) > 0) {
		len += n;
		if (len + 1 >= cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
	}
	fclose(f);
	buf[len] = '\0';

	if (len == 0) {
		free(buf);
		return NULL;
	}
	return buf;
}

/* Temp-and-rename so a run killed mid-write cannot leave a half-feed behind:
 * that would parse to nothing and quietly disable the gate for the whole TTL,
 * which is the failure mode this is all trying to avoid. */
static void write_cache(const char *path, const char *data)
{
	char *dir = xstrdup(path);
	char *slash = strrchr(dir, '/');
	if (slash) {
		*slash = '\0';
		/* One level is enough in practice (~/.cache exists); if it does not,
		 * the open below fails and the cache is simply not used. */
		mkdir(dir, 0755);
	}
	free(dir);

	char *tmp = xasprintf("%s.new", path);
	FILE *f = fopen(tmp, "we");
	if (f) {
		size_t len = strlen(data);
		bool whole = fwrite(data, 1, len, f) == len;
		if (fclose(f) == 0 && whole)
			rename(tmp, path);
		else
			unlink(tmp);
	}
	free(tmp);
}

/* Seconds since a file was last written, or -1 if it is not there. */
static long file_age(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0)
		return -1;
	time_t now = time(NULL);
	long age = (long)(now - st.st_mtime);
	return age < 0 ? 0 : age;
}

static char *news_fetch(void)
{
	/* Test hook: a fixture on disk instead of the network. The suite must not
	 * depend on archlinux.org being reachable — a test that needs the internet
	 * is a test that gets disabled the first time a build runs in a chroot. */
	const char *fixture = getenv("SYNPKG_NEWS_FILE");
	if (fixture && *fixture)
		return read_file(fixture);

	char *cache = cache_path();
	long age = cache ? file_age(cache) : -1;

	/* A FRESH CACHE IS USED WITHOUT ASKING.
	 *
	 * archlinux.org rate-limits, and it answers with 429 rather than anything
	 * that reads like a rate limit. Hit while testing this file: six requests
	 * in a few seconds was enough. An upgrade retried after a failure, a
	 * `news` invocation, and the updater window opening would all be separate
	 * requests for a document that changes a handful of times a year.
	 *
	 * Half an hour is far shorter than the news cycle it is tracking and far
	 * longer than any burst of upgrade attempts. */
	if (cache && age >= 0 && age < NEWS_TTL) {
		char *hit = read_file(cache);
		if (hit) {
			free(cache);
			return hit;
		}
	}

	char *out = NULL;
	if (have_cmd("curl")) {
		/* --max-time is not optional. This runs on the path to an upgrade the
		 * user is waiting on, and a captive portal or a black-holed route
		 * answers a TCP connect and then says nothing at all — without a
		 * deadline the updater hangs before it has done anything, with no
		 * output to explain why. --proto =https so a redirect cannot walk the
		 * request down to cleartext. */
		char *argv[] = { (char *)"curl", (char *)"-fsS", (char *)"--proto",
		                 (char *)"=https", (char *)"--tlsv1.2",
		                 (char *)"--max-time", (char *)"15",
		                 (char *)"--location", (char *)NEWS_URL, NULL };
		int st = 0;
		out = run_capture(argv, &st, false);
		if (st != 0) {
			free(out);
			out = NULL;
		}
	}

	if (out) {
		if (cache)
			write_cache(cache, out);
		free(cache);
		return out;
	}

	/* Offline, or throttled. A STALE copy beats no copy: the news it holds was
	 * true when it was fetched, and the alternative is an upgrade with no
	 * check at all. Its age is stated so nobody reads a week-old feed as
	 * today's. */
	if (cache && age >= 0) {
		char *stale = read_file(cache);
		free(cache);
		if (stale) {
			warn("could not reach %s — showing a cached copy from %ld hour(s) ago",
			     NEWS_URL, age / 3600);
			return stale;
		}
		return NULL;
	}
	free(cache);
	return NULL;
}

/* Newest first, which is the order the feed is already in. */
static size_t news_parse(const char *xml, news_item *out, size_t max)
{
	size_t n = 0;
	const char *cur = xml;

	while (n < max) {
		const char *p = strstr(cur, "<item>");
		if (!p)
			break;
		const char *e = strstr(p, "</item>");
		if (!e)
			break;

		news_item it = { 0 };
		it.title = tag_text(p, e, "title");
		it.link  = tag_text(p, e, "link");

		char *date = tag_text(p, e, "pubDate");
		it.when = rfc822_time(date);
		free(date);

		char *desc = tag_text(p, e, "description");
		if (desc) {
			it.body = strip_html(desc);
			free(desc);
		}

		/* An item with no title is a parse failure, not a news post. Dropping
		 * it is right: printing "(null)" at an upgrade prompt would look like
		 * the news itself had gone wrong. */
		if (it.title && *it.title)
			out[n++] = it;
		else
			news_item_free(&it);

		cur = e + 7;
	}
	return n;
}

/* ── rendering ──────────────────────────────────────────────────────────── */

static int term_width(void)
{
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 40)
		return ws.ws_col > 100 ? 100 : ws.ws_col;
	return 80;
}

/* Wrap on spaces at `width`, every line prefixed with `indent`.
 *
 * Counts BYTES, not columns, so a body with accented characters wraps a little
 * early. That is the right way to be wrong here: the alternative is a
 * multibyte-width dependency for a paragraph of advisory text. */
static void wrap_print(const char *text, int width, const char *indent)
{
	int avail = width - (int)strlen(indent);
	if (avail < 20)
		avail = 20;

	const char *p = text;
	while (*p) {
		while (*p == ' ')
			p++;
		if (!*p)
			break;

		int len = (int)strlen(p);
		int take = len < avail ? len : avail;
		if (take == avail) {
			int brk = take;
			while (brk > 0 && p[brk] != ' ')
				brk--;
			if (brk > 0)
				take = brk;   /* a token longer than a line is hard-split */
		}
		printf("%s%.*s\n", indent, take, p);
		p += take;
	}
}

static void news_show(const news_item *it, bool body)
{
	char when[32] = "undated";
	if (it->when) {
		struct tm tm;
		if (localtime_r(&it->when, &tm))
			strftime(when, sizeof when, "%Y-%m-%d", &tm);
	}

	if (g_out == OUT_TSV) {
		tsv_row(4, when, it->title, it->link ? it->link : "",
		        it->body ? it->body : "");
		return;
	}

	printf("\n  %s%s%s\n", C_BOLD(), it->title, C_RESET());
	printf("  %s%s%s\n", C_DIM(), when, C_RESET());
	if (it->link && *it->link)
		printf("  %s%s%s\n", C_DIM(), it->link, C_RESET());

	if (body && it->body && *it->body) {
		int w = term_width();
		if (strlen(it->body) > BODY_MAX) {
			char *cut = xstrdup(it->body);
			cut[BODY_MAX] = '\0';
			char *sp = strrchr(cut, ' ');
			if (sp)
				*sp = '\0';
			wrap_print(cut, w, "    ");
			printf("    %s…  (the rest is at the link above)%s\n",
			       C_DIM(), C_RESET());
			free(cut);
		} else {
			wrap_print(it->body, w, "    ");
		}
	}
}

/* ── the gate ───────────────────────────────────────────────────────────── */

bool sp_news_gate(void)
{
	if (!sp_setting_bool("upgrade_news"))
		return true;

	char *xml = news_fetch();
	if (!xml) {
		/* Advisory, so it gets out of the way. See the file header: refusing to
		 * install security updates because a news feed was unreachable would be
		 * a worse failure than the one being guarded against. */
		warn("could not read the Arch news feed — continuing without the check "
		     "(https://archlinux.org/news/)");
		return true;
	}

	news_item items[NEWS_MAX];
	size_t n = news_parse(xml, items, NEWS_MAX);
	free(xml);

	if (n == 0) {
		warn("the Arch news feed did not parse — continuing without the check");
		return true;
	}

	time_t cutoff = last_sysupgrade();

	/* Cannot tell when this machine last upgraded. Show a SMALL fixed window
	 * rather than everything: unbounded output at a confirmation prompt trains
	 * people to hit Enter, which is the exact habit this is trying not to
	 * create. */
	size_t unread = 0;
	bool dated = cutoff != 0;
	if (dated) {
		for (size_t i = 0; i < n; i++)
			if (items[i].when > cutoff)
				unread++;
	} else {
		unread = n < 3 ? n : 3;
	}

	bool proceed = true;
	if (unread == 0) {
		if (g_verbose)
			info("no Arch news since your last upgrade");
	} else {
		if (dated) {
			char when[32] = "?";
			struct tm tm;
			if (localtime_r(&cutoff, &tm))
				strftime(when, sizeof when, "%Y-%m-%d", &tm);
			info("%zu Arch news item(s) published since your last upgrade (%s)",
			     unread, when);
		} else {
			info("%zu recent Arch news item(s) — this machine's last upgrade "
			     "could not be read from " PACMAN_LOG, unread);
		}

		size_t shown = 0;
		for (size_t i = 0; i < n && shown < unread; i++) {
			if (dated && items[i].when <= cutoff)
				continue;
			news_show(&items[i], true);
			shown++;
		}

		if (g_out != OUT_TSV) {
			printf("\n%sAn Arch upgrade occasionally needs a manual step, and "
			       "the news above is the only%s\n", C_WARN(), C_RESET());
			printf("%splace it is announced. Read it before continuing.%s\n\n",
			       C_WARN(), C_RESET());
		}

		proceed = confirm("Continue with the upgrade?");
	}

	for (size_t i = 0; i < n; i++)
		news_item_free(&items[i]);
	return proceed;
}

int cmd_news(int argc, char **argv)
{
	bool all = false;
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--all"))
			all = true;
		else
			die("news: unknown argument '%s'", argv[i]);
	}

	char *xml = news_fetch();
	if (!xml) {
		warn("could not read %s", NEWS_URL);
		return 1;
	}

	news_item items[NEWS_MAX];
	size_t n = news_parse(xml, items, NEWS_MAX);
	free(xml);

	if (n == 0) {
		warn("the Arch news feed did not parse");
		return 1;
	}

	time_t cutoff = all ? 0 : last_sysupgrade();
	size_t shown = 0;
	for (size_t i = 0; i < n; i++) {
		if (cutoff && items[i].when <= cutoff)
			continue;
		news_show(&items[i], true);
		shown++;
	}

	if (!shown && g_out == OUT_HUMAN)
		printf("%sno Arch news since your last upgrade%s "
		       "%s(synpkg news --all for the last %d)%s\n",
		       C_OK(), C_RESET(), C_DIM(), NEWS_MAX, C_RESET());

	for (size_t i = 0; i < n; i++)
		news_item_free(&items[i]);

	/* 100 is this program's "nothing to do" for the list commands, so a poller
	 * — a bar widget, a cron job — can branch on it without parsing. */
	return shown ? 0 : 100;
}
