/*
 * news.c — news aggregator (Super+N)
 *
 * A compositor-drawn modal panel over a river of RSS/Atom feeds: Hacker News
 * and Lobsters for the hacker end, and the feeds that actually break this
 * machine at the Linux end — Arch news (a manual-intervention post is the
 * difference between `pacman -Syu` working and not), Arch security advisories,
 * kernel releases (every bump silently invalidates the synapse_kmod DKMS
 * build), LWN, Phoronix, GamingOnLinux.
 *
 * Fetching is a background thread, because a DNS lookup or a TLS handshake to
 * a slow host would otherwise stall the compositor's event loop — every
 * client's frame callbacks with it. The thread parks on a condvar until the
 * main thread asks for a refresh, fetches with libcurl, parses into fetched[]
 * under the lock, and writes one byte to a pipe whose read end sits in the
 * Wayland event loop. Only the main thread touches wlroots, as everywhere else
 * in synui. It is the synapd_mon idiom with a mutex instead of a pipe payload:
 * a snapshot here is ~360 items, far past PIPE_BUF, so it cannot ride the pipe
 * atomically — the pipe is only the wake-up.
 *
 * Shutdown is the lesson from the AI thread's 15s logout hang, applied up
 * front: the stop flag is also a libcurl progress callback, so a transfer
 * blocked on an unresponsive feed aborts within a poll interval instead of
 * holding logout hostage for the connect timeout.
 *
 * The panel never routes a URL through a shell. These strings come off the
 * network, and `spawn()` is /bin/sh -c: a title or link containing `;` would
 * otherwise be a remote code execution bug in a security-focused distro. URLs
 * are validated (http/https, no control characters, no whitespace) and handed
 * to xdg-open through fork + execlp as a single argv element.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>

#include "synui.h"

/* Feeds worth having on this machine. Order is the panel's Tab order. */
static const syn_news_source_t news_defaults[] = {
    { "HN",       "https://news.ycombinator.com/rss" },
    { "LOBSTERS", "https://lobste.rs/rss" },
    { "ARCH",     "https://archlinux.org/feeds/news/" },
    { "ARCHSEC",  "https://security.archlinux.org/advisory/feed.atom" },
    { "KERNEL",   "https://www.kernel.org/feeds/kdist.xml" },
    { "LWN",      "https://lwn.net/headlines/newrss" },
    { "PHORONIX", "https://www.phoronix.com/rss.php" },
    { "GOL",      "https://www.gamingonlinux.com/article_rss.php" },
};

#define NEWS_CONNECT_SEC   6
#define NEWS_XFER_SEC     15
#define NEWS_MAX_BODY     (4 * 1024 * 1024)
#define NEWS_FEED_SCAN   128   /* entries read from one feed before trimming */
#define NEWS_UA           "synui-news/0.1 (+https://github.com/velle999/SYNAPSE)"

/* Item order: newest-first, and grouped-by-feed-in-feed-order. parse_feed uses
 * both to pick the newest entries out of a feed without losing its ranking. */
static int by_time(const void *a, const void *b);
static int by_source(const void *a, const void *b);

/* ── Small helpers ───────────────────────────────────────── */

static uint64_t fnv1a(const char *s)
{
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

void news_age(time_t ts, char *buf, size_t n)
{
    if (ts <= 0) { snprintf(buf, n, "-"); return; }

    long d = (long)(time(NULL) - ts);
    if (d < 0)          d = 0;             /* clock skew, or a feed dated ahead */
    if (d < 60)         snprintf(buf, n, "now");
    else if (d < 3600)  snprintf(buf, n, "%ldm", d / 60);
    else if (d < 86400) snprintf(buf, n, "%ldh", d / 3600);
    else if (d < 86400L * 30) snprintf(buf, n, "%ldd", d / 86400);
    else                snprintf(buf, n, "%ldw", d / (86400L * 7));
}

void news_host(const char *url, char *buf, size_t n)
{
    buf[0] = '\0';
    const char *p = strstr(url, "://");
    if (!p) return;
    p += 3;
    if (!strncmp(p, "www.", 4)) p += 4;

    size_t i = 0;
    while (*p && *p != '/' && *p != ':' && i + 1 < n)
        buf[i++] = *p++;
    buf[i] = '\0';
}

/* Only ever hand an execlp() a URL we recognise. Everything here arrived over
 * the network from a third party. */
static int news_url_ok(const char *url)
{
    if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) return 0;
    if (strlen(url) >= NEWS_URL_LEN) return 0;
    for (const char *p = url; *p; p++)
        if ((unsigned char)*p <= 0x20 || (unsigned char)*p == 0x7f) return 0;
    return 1;
}

/* No shell: argv[1] is the URL, exactly as it is, and nothing tokenizes it. */
static int news_open_url(const char *url)
{
    if (!news_url_ok(url)) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setsid();
        synui_child_reset_signals();
        execlp("xdg-open", "xdg-open", url, (char *)NULL);
        _exit(127);
    }
    return 0;
}

static int news_copy_url(const char *url)
{
    if (!news_url_ok(url)) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setsid();
        synui_child_reset_signals();
        execlp("wl-copy", "wl-copy", "--", url, (char *)NULL);
        _exit(127);
    }
    return 0;
}

/* ── Feed parsing ────────────────────────────────────────── */
/* Hand-rolled rather than libxml2: feeds are a closed set of shapes, a strict
 * parser would reject the ones with stray entities in them (several here do),
 * and this keeps the compositor free of an XML dependency. */

static const char *find_ci(const char *p, const char *end, const char *needle)
{
    size_t nl = strlen(needle);
    if (nl == 0 || (size_t)(end - p) < nl) return NULL;
    for (const char *q = p; q + nl <= end; q++)
        if (strncasecmp(q, needle, nl) == 0) return q;
    return NULL;
}

/* Content of the first <tag>…</tag> in [p,end). The name must end at the '>'
 * or at whitespace, so "link" cannot match "linkTitle". */
static const char *tag_text(const char *p, const char *end, const char *tag,
                            const char **cend)
{
    char open[24], close[24];
    snprintf(open,  sizeof(open),  "<%s",  tag);
    snprintf(close, sizeof(close), "</%s", tag);
    size_t olen = strlen(open);

    for (const char *o = p; (o = find_ci(o, end, open)); o += olen) {
        const char *after = o + olen;
        if (after >= end) return NULL;
        if (*after != '>' && *after != ' ' && *after != '\t' &&
            *after != '\r' && *after != '\n' && *after != '/')
            continue;   /* a longer tag name that merely starts the same way */

        const char *gt = memchr(after, '>', (size_t)(end - after));
        if (!gt) return NULL;
        if (gt > after && gt[-1] == '/') continue;   /* empty element */

        const char *c = find_ci(gt + 1, end, close);
        if (!c) return NULL;
        *cend = c;
        return gt + 1;
    }
    return NULL;
}

/* The length of a prefix of `len` bytes that does not end inside a UTF-8
 * sequence — i.e. where it is safe to cut.
 *
 * This matters more than it looks. cairo_show_text() puts the *whole cairo
 * context* into a permanent error state when handed invalid UTF-8, and every
 * later drawing call on it silently becomes a no-op — so a single headline cut
 * through a '—' takes out every row below it, the status line and the footer
 * with it. Backing off the continuation bytes is not enough: the lead byte
 * they belonged to has to go too, or what is left is a dangling lead, which is
 * just as invalid. */
size_t news_utf8_trim(const char *b, size_t len)
{
    if (len == 0) return 0;

    size_t s = len - 1;
    while (s > 0 && ((unsigned char)b[s] & 0xC0) == 0x80) s--;   /* to the lead */

    unsigned char c = (unsigned char)b[s];
    size_t need = (c < 0x80)            ? 1
                : ((c & 0xE0) == 0xC0)  ? 2
                : ((c & 0xF0) == 0xE0)  ? 3
                : ((c & 0xF8) == 0xF0)  ? 4
                                        : 1;   /* stray continuation: drop it */

    return (s + need <= len) ? len : s;   /* complete: keep. Cut short: drop. */
}

/* Well-formed UTF-8? Guards the one input that does not come from the parser:
 * news.cache, which is a file on disk and can be corrupt or hand-edited. */
static int utf8_ok(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n;
        if      (*p < 0x80)          n = 0;
        else if ((*p & 0xE0) == 0xC0) n = 1;
        else if ((*p & 0xF0) == 0xE0) n = 2;
        else if ((*p & 0xF8) == 0xF0) n = 3;
        else return 0;
        p++;
        for (int i = 0; i < n; i++, p++)
            if ((*p & 0xC0) != 0x80) return 0;
    }
    return 1;
}

static size_t utf8_put(char *dst, size_t cap, unsigned cp)
{
    if (cp < 0x80) {
        if (cap < 1) return 0;
        dst[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        if (cap < 2) return 0;
        dst[0] = (char)(0xC0 | (cp >> 6));
        dst[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        if (cap < 3) return 0;
        dst[0] = (char)(0xE0 | (cp >> 12));
        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cap < 4) return 0;
    dst[0] = (char)(0xF0 | (cp >> 18));
    dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    dst[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static const struct { const char *name; unsigned cp; } entities[] = {
    { "amp",   '&'    }, { "lt",    '<'    }, { "gt",    '>'    },
    { "quot",  '"'    }, { "apos",  '\''   }, { "nbsp",  ' '    },
    { "hellip", 0x2026 }, { "mdash", 0x2014 }, { "ndash", 0x2013 },
    { "rsquo", 0x2019 }, { "lsquo", 0x2018 },
    { "ldquo", 0x201C }, { "rdquo", 0x201D },
};

/* Copy XML text into a flat, single-line UTF-8 string: unwrap CDATA, drop
 * markup, decode entities, and collapse every run of whitespace to one space.
 * Control characters are dropped outright — the cache is a TSV, and a title
 * with a tab or a newline in it would corrupt the file it is written to. */
static void xml_text(char *dst, size_t cap, const char *p, const char *end)
{
    size_t o = 0;
    int in_tag = 0, pending_space = 0, wrote = 0;

    if (end - p > 9 && !strncmp(p, "<![CDATA[", 9)) {
        const char *c = find_ci(p, end, "]]>");
        p += 9;
        if (c) end = c;
    }

    while (p < end && o + 5 < cap) {
        unsigned char ch = (unsigned char)*p;

        if (ch == '<')      { in_tag = 1; p++; continue; }
        if (in_tag)         { if (ch == '>') in_tag = 0; p++; continue; }

        if (ch == '&') {
            const char *semi = memchr(p, ';', (size_t)(end - p) < 12
                                                  ? (size_t)(end - p) : 12);
            if (semi) {
                unsigned cp = 0;
                size_t len = (size_t)(semi - p - 1);
                char name[12];
                memcpy(name, p + 1, len);
                name[len] = '\0';

                if (name[0] == '#') {
                    cp = (name[1] == 'x' || name[1] == 'X')
                           ? (unsigned)strtoul(name + 2, NULL, 16)
                           : (unsigned)strtoul(name + 1, NULL, 10);
                } else {
                    for (size_t i = 0; i < sizeof(entities)/sizeof(entities[0]); i++)
                        if (!strcasecmp(name, entities[i].name)) {
                            cp = entities[i].cp;
                            break;
                        }
                }
                p = semi + 1;
                if (cp == 0 || cp > 0x10FFFF) continue;   /* unknown: drop it */

                if (cp == '\n' || cp == '\t' || cp == ' ') { pending_space = wrote; continue; }
                if (cp < 0x20) continue;
                if (pending_space) { dst[o++] = ' '; pending_space = 0; }
                o += utf8_put(dst + o, cap - o - 1, cp);
                wrote = 1;
                continue;
            }
        }

        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            pending_space = wrote;   /* never leading */
            p++;
            continue;
        }
        if (ch < 0x20 || ch == 0x7f) { p++; continue; }

        if (pending_space) { dst[o++] = ' '; pending_space = 0; }
        dst[o++] = (char)ch;
        wrote = 1;
        p++;
    }

    /* A title longer than the buffer is cut here, and the cut must not land
     * inside a multi-byte character (see news_utf8_trim). */
    o = news_utf8_trim(dst, o);
    dst[o] = '\0';   /* trailing whitespace never made it in */
}

/* Atom: the article is <link href="…"/>, with rel absent or "alternate".
 * rel="self" is the feed itself and rel="replies" its comment thread. */
static void atom_link(const char *p, const char *end, char *out, size_t n,
                      char *replies, size_t rn)
{
    out[0] = '\0';
    if (replies) replies[0] = '\0';

    for (const char *l = p; (l = find_ci(l, end, "<link")); l += 5) {
        const char *gt = memchr(l, '>', (size_t)(end - l));
        if (!gt) return;

        char rel[24] = "", href[NEWS_URL_LEN] = "";
        const char *a;

        if ((a = find_ci(l, gt, "rel=\"")))
            sscanf(a + 5, "%23[^\"]", rel);
        if ((a = find_ci(l, gt, "href=\"")))
            sscanf(a + 6, "%399[^\"]", href);
        if (!href[0]) continue;

        if (!strcasecmp(rel, "replies")) {
            if (replies) snprintf(replies, rn, "%s", href);
        } else if (!rel[0] || !strcasecmp(rel, "alternate")) {
            if (!out[0]) snprintf(out, n, "%s", href);
        }
    }
}

static const char *months[] = { "jan", "feb", "mar", "apr", "may", "jun",
                                "jul", "aug", "sep", "oct", "nov", "dec" };

/* RFC 822 ("Mon, 13 Jul 2026 18:22:16 +0000") and ISO 8601
 * ("2026-07-13T18:22:16.759771+00:00"). Hand-rolled, not strptime: %b matches
 * month names in the *current locale*, so a user running a non-English locale
 * would silently get an undated feed and a river sorted at random. */
static time_t parse_date(const char *s)
{
    while (*s == ' ') s++;

    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    int off_min = 0;
    const char *tz = NULL;

    /* ISO 8601 leads with the date: "2026-07-13T…". Sniffing for a 'T' and a
     * '-' anywhere in the string does not work — "Tue, 14 Jul 2026 00:57:11
     * -0400" has both and is RFC 822, so every Tuesday and Thursday behind a
     * negative UTC offset would come back undated and sink to the bottom of
     * the river. */
    int iso = isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]) &&
              isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3]) &&
              s[4] == '-';

    if (iso) {
        int y, mo, d, h = 0, mi = 0, sec = 0;
        if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 3)
            return 0;
        tm.tm_year = y - 1900;
        tm.tm_mon  = mo - 1;
        tm.tm_mday = d;
        tm.tm_hour = h;
        tm.tm_min  = mi;
        tm.tm_sec  = sec;

        /* Skip the date half before hunting the offset sign, or the '-' in
         * "2026-07-13" reads as a negative zone. */
        const char *t = strchr(s, 'T');
        for (const char *p = t; *p; p++)
            if (*p == '+' || *p == '-' || *p == 'Z' || *p == 'z') { tz = p; break; }
    } else {
        const char *p = strchr(s, ',');
        p = p ? p + 1 : s;
        while (*p == ' ') p++;

        int d, y, h = 0, mi = 0, sec = 0;
        char mon[8] = "";
        if (sscanf(p, "%d %7s %d %d:%d:%d", &d, mon, &y, &h, &mi, &sec) < 3)
            return 0;

        int m = -1;
        for (int i = 0; i < 12; i++)
            if (!strncasecmp(mon, months[i], 3)) { m = i; break; }
        if (m < 0) return 0;

        if (y < 100) y += (y < 70) ? 2000 : 1900;   /* RFC 822 two-digit years */
        tm.tm_year = y - 1900;
        tm.tm_mon  = m;
        tm.tm_mday = d;
        tm.tm_hour = h;
        tm.tm_min  = mi;
        tm.tm_sec  = sec;

        /* The zone is the last token: "+0000", "GMT", "PST"… */
        const char *sp = strrchr(p, ' ');
        if (sp) tz = sp + 1;
    }

    if (tz && (*tz == '+' || *tz == '-')) {
        int sign = (*tz == '-') ? -1 : 1;
        int th = 0, tm_ = 0;
        if (sscanf(tz + 1, "%2d:%2d", &th, &tm_) == 2 ||
            sscanf(tz + 1, "%2d%2d",  &th, &tm_) >= 1)
            off_min = sign * (th * 60 + tm_);
    }

    time_t t = timegm(&tm);
    if (t == (time_t)-1) return 0;
    return t - off_min * 60;
}

/* Parse one feed body, writing up to NEWS_PER_FEED items. Returns how many. */
static int parse_feed(const char *body, size_t len, int src,
                      syn_news_item_t *out, int max)
{
    const char *end = body + len;
    int atom = find_ci(body, end, "<entry") != NULL &&
               find_ci(body, end, "<feed")  != NULL;
    const char *open  = atom ? "<entry" : "<item";
    const char *close = atom ? "</entry" : "</item";

    syn_news_item_t *tmp = calloc(NEWS_FEED_SCAN, sizeof(syn_news_item_t));
    if (!tmp) return 0;

    int n = 0;
    const char *p = body;

    while (n < NEWS_FEED_SCAN) {
        const char *o = find_ci(p, end, open);
        if (!o) break;
        const char *c = find_ci(o, end, close);
        if (!c) break;

        syn_news_item_t *it = &tmp[n];
        memset(it, 0, sizeof(*it));
        it->src  = src;
        it->rank = n;

        const char *ts, *te;
        if ((ts = tag_text(o, c, "title", &te)))
            xml_text(it->title, sizeof(it->title), ts, te);

        if (atom) {
            atom_link(o, c, it->url, sizeof(it->url),
                      it->comments, sizeof(it->comments));
        } else {
            if ((ts = tag_text(o, c, "link", &te)))
                xml_text(it->url, sizeof(it->url), ts, te);
            if ((ts = tag_text(o, c, "comments", &te)))
                xml_text(it->comments, sizeof(it->comments), ts, te);
        }
        /* Some feeds carry the permalink only as a guid. */
        if (!it->url[0] && (ts = tag_text(o, c, "guid", &te)))
            xml_text(it->url, sizeof(it->url), ts, te);

        char date[64] = "";
        if ((ts = tag_text(o, c, "pubDate", &te)) ||
            (ts = tag_text(o, c, "published", &te)) ||
            (ts = tag_text(o, c, "updated", &te)) ||
            (ts = tag_text(o, c, "date", &te)))
            xml_text(date, sizeof(date), ts, te);
        it->ts = date[0] ? parse_date(date) : 0;

        /* An item with no title or no usable link is not something the panel
         * could show or open, so it never enters the river. */
        if (it->title[0] && news_url_ok(it->url)) {
            if (!news_url_ok(it->comments)) it->comments[0] = '\0';
            n++;
        }
        p = c + 1;
    }

    /* Trim to the newest NEWS_PER_FEED, then put document order back.
     *
     * "The first 30 entries" is not "the 30 that matter": a feed need not be
     * newest-first, and Arch's advisory feed is oldest-first — so once it
     * carries more than 30 advisories, keeping the head of it would show the
     * stale ones and silently drop every new one. Document order is restored
     * afterwards because `rank` has to keep meaning position-in-feed: HN's
     * front page is ranked, not chronological, and the by-source view is the
     * one place that ranking survives. */
    if (n > NEWS_PER_FEED) {
        qsort(tmp, (size_t)n, sizeof(syn_news_item_t), by_time);    /* newest */
        n = NEWS_PER_FEED;
        qsort(tmp, (size_t)n, sizeof(syn_news_item_t), by_source);  /* by rank */
    }
    if (n > max) n = max;

    memcpy(out, tmp, sizeof(syn_news_item_t) * (size_t)n);
    free(tmp);
    return n;
}

/* ── Fetching ────────────────────────────────────────────── */

typedef struct { char *buf; size_t len, cap; } dlbuf_t;

static size_t dl_write(char *ptr, size_t sz, size_t nm, void *ud)
{
    dlbuf_t *d = ud;
    size_t add = sz * nm;

    if (d->len + add + 1 > NEWS_MAX_BODY) return 0;   /* aborts the transfer */
    if (d->len + add + 1 > d->cap) {
        size_t cap = d->cap ? d->cap * 2 : 65536;
        while (cap < d->len + add + 1) cap *= 2;
        char *nb = realloc(d->buf, cap);
        if (!nb) return 0;
        d->buf = nb;
        d->cap = cap;
    }
    memcpy(d->buf + d->len, ptr, add);
    d->len += add;
    d->buf[d->len] = '\0';
    return add;
}

/* The stop flag, as libcurl sees it: a non-zero return aborts the transfer,
 * so logout does not wait out a connect timeout on a dead feed. */
static int dl_progress(void *ud, curl_off_t dt, curl_off_t dn,
                       curl_off_t ut, curl_off_t un)
{
    (void)dt; (void)dn; (void)ut; (void)un;
    syn_news_t *n = ud;
    return atomic_load(&n->stop) ? 1 : 0;
}

static int fetch_one(syn_news_t *n, CURL *curl, const syn_news_source_t *src,
                     int idx, syn_news_item_t *out, int max)
{
    dlbuf_t d = {0};

    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, src->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &d);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, NEWS_UA);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");   /* gzip if offered */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)NEWS_CONNECT_SEC);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)NEWS_XFER_SEC);
    /* Threads: libcurl's alarm()-based DNS timeout is not signal-safe here. */
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, dl_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, n);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    int got = 0;
    if (rc == CURLE_OK && code == 200 && d.len)
        got = parse_feed(d.buf, d.len, idx, out, max);
    else if (rc != CURLE_OK && !atomic_load(&n->stop))
        wlr_log(WLR_INFO, "synui: news: %s: %s", src->name,
                curl_easy_strerror(rc));

    free(d.buf);
    return got;
}

static int by_time(const void *a, const void *b)
{
    const syn_news_item_t *x = a, *y = b;
    if (x->ts != y->ts) return (y->ts > x->ts) ? 1 : -1;
    if (x->src != y->src) return x->src - y->src;
    return x->rank - y->rank;
}

static int by_source(const void *a, const void *b)
{
    const syn_news_item_t *x = a, *y = b;
    if (x->src != y->src) return x->src - y->src;
    return x->rank - y->rank;
}

static void *news_thread_fn(void *arg)
{
    syn_server_t *s = arg;
    syn_news_t   *n = &s->news;

    CURL *curl = curl_easy_init();
    if (!curl) {
        wlr_log(WLR_ERROR, "synui: news: curl init failed");
        return NULL;
    }

    /* Scratch for a fetch in progress: too big for the thread's stack, and it
     * has to live outside the lock — the fetch itself must never hold it. */
    syn_news_item_t *got = calloc(NEWS_ITEMS_MAX, sizeof(syn_news_item_t));
    if (!got) {
        curl_easy_cleanup(curl);
        wlr_log(WLR_ERROR, "synui: news: out of memory");
        return NULL;
    }

    while (!atomic_load(&n->stop)) {
        pthread_mutex_lock(&n->lock);
        while (!atomic_load(&n->stop) && !atomic_load(&n->want))
            pthread_cond_wait(&n->cv, &n->lock);
        pthread_mutex_unlock(&n->lock);
        if (atomic_load(&n->stop)) break;
        atomic_store(&n->want, 0);

        /* The lock is held only long enough to hand the finished snapshot
         * over — never across the network I/O. */
        int total = 0, failed = 0;

        for (int i = 0; i < n->n_sources && !atomic_load(&n->stop); i++) {
            int room = NEWS_ITEMS_MAX - total;
            if (room <= 0) break;
            int k = fetch_one(n, curl, &n->sources[i], i, got + total, room);
            if (k == 0) failed++;
            total += k;
        }
        if (atomic_load(&n->stop)) break;

        pthread_mutex_lock(&n->lock);
        memcpy(n->fetched, got, sizeof(syn_news_item_t) * (size_t)total);
        n->n_fetched    = total;
        n->fetch_failed = failed;
        pthread_mutex_unlock(&n->lock);

        char byte = 1;
        if (write(n->pipe[1], &byte, 1) < 0 && errno != EAGAIN)
            wlr_log(WLR_ERROR, "synui: news: pipe write failed");
    }

    free(got);
    curl_easy_cleanup(curl);
    return NULL;
}

/* ── Persistence ─────────────────────────────────────────── */
/* The cache is what makes the panel open instantly (and work at all offline);
 * the seen-set is what makes "new since I last looked" mean anything. */

static int news_cache_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "news.cache");
}

static int news_seen_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "news.seen");
}

static int src_index(syn_news_t *n, const char *name)
{
    for (int i = 0; i < n->n_sources; i++)
        if (!strcmp(n->sources[i].name, name)) return i;
    return -1;
}

static void news_cache_save(syn_news_t *n)
{
    char path[512];
    if (!news_cache_path(path, sizeof(path))) return;

    /* On a fresh install ~/.config/synui does not exist yet, and without this
     * the cache and the read-marks would never persist — silently, since the
     * panel refetches on open and looks fine either way. */
    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: news: cannot write %s: %s",
                path, strerror(errno));
        return;
    }
    for (int i = 0; i < n->n; i++) {
        syn_news_item_t *it = &n->items[i];
        fprintf(f, "%s\t%lld\t%d\t%s\t%s\t%s\n",
                n->sources[it->src].name, (long long)it->ts, it->rank,
                it->url, it->comments[0] ? it->comments : "-", it->title);
    }
    fclose(f);
}

static void news_cache_load(syn_news_t *n)
{
    char path[512];
    if (!news_cache_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[1200];
    n->n = 0;
    while (n->n < NEWS_ITEMS_MAX && fgets(line, sizeof(line), f)) {
        char *save = NULL;
        char *name = strtok_r(line, "\t", &save);
        char *ts   = strtok_r(NULL, "\t", &save);
        char *rank = strtok_r(NULL, "\t", &save);
        char *url  = strtok_r(NULL, "\t", &save);
        char *com  = strtok_r(NULL, "\t", &save);
        char *tit  = strtok_r(NULL, "\n", &save);
        if (!name || !ts || !rank || !url || !com || !tit) continue;

        /* A source dropped from the config takes its cached items with it.
         * A corrupt title is dropped rather than drawn: this file is the one
         * item source that did not come through the parser, and cairo will not
         * survive invalid UTF-8 (news_utf8_trim). It comes back on the next
         * fetch anyway. */
        int si = src_index(n, name);
        if (si < 0 || !news_url_ok(url) || !utf8_ok(tit)) continue;

        syn_news_item_t *it = &n->items[n->n];
        memset(it, 0, sizeof(*it));
        it->src  = si;
        it->ts   = (time_t)strtoll(ts, NULL, 10);
        it->rank = atoi(rank);
        snprintf(it->url,   sizeof(it->url),   "%s", url);
        snprintf(it->title, sizeof(it->title), "%s", tit);
        if (strcmp(com, "-") && news_url_ok(com))
            snprintf(it->comments, sizeof(it->comments), "%s", com);
        n->n++;
    }
    fclose(f);
}

static void news_seen_load(syn_news_t *n)
{
    char path[512];
    if (!news_seen_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[64];
    n->n_seen = 0;
    while (n->n_seen < NEWS_SEEN_MAX && fgets(line, sizeof(line), f)) {
        uint64_t h = strtoull(line, NULL, 16);
        if (h) n->seen[n->n_seen++] = h;
    }
    fclose(f);
}

static void news_seen_save(syn_news_t *n)
{
    char path[512];
    if (!news_seen_path(path, sizeof(path))) return;

    syn_config_ensure_dir();

    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "synui: news: cannot write %s: %s",
                path, strerror(errno));
        return;
    }
    for (int i = 0; i < n->n_seen; i++)
        fprintf(f, "%016llx\n", (unsigned long long)n->seen[i]);
    fclose(f);
}

static int is_seen(syn_news_t *n, const char *url)
{
    uint64_t h = fnv1a(url);
    for (int i = 0; i < n->n_seen; i++)
        if (n->seen[i] == h) return 1;
    return 0;
}

static void mark_seen(syn_news_t *n, syn_news_item_t *it)
{
    if (it->seen) return;
    it->seen = 1;

    uint64_t h = fnv1a(it->url);
    if (n->n_seen >= NEWS_SEEN_MAX) {
        /* Drop the oldest half rather than one at a time: the set is a plain
         * array and this keeps the eviction O(n) once per 2048 marks. */
        memmove(n->seen, n->seen + NEWS_SEEN_MAX / 2,
                sizeof(uint64_t) * (NEWS_SEEN_MAX / 2));
        n->n_seen = NEWS_SEEN_MAX / 2;
    }
    n->seen[n->n_seen++] = h;
}

/* ── The view (filter + search + sort) ───────────────────── */

static int matches(syn_news_t *n, syn_news_item_t *it)
{
    if (n->filter >= 0 && it->src != n->filter) return 0;
    if (!n->query[0]) return 1;
    return strcasestr(it->title, n->query) != NULL ||
           strcasestr(n->sources[it->src].name, n->query) != NULL;
}

static void news_rebuild_view(syn_news_t *n)
{
    /* Remember what the cursor was on, so a filter change moves the selection
     * with the item rather than leaving it on whatever row happens to be at
     * that index afterwards. */
    const char *sel_url = NULL;
    if (n->selected >= 0 && n->selected < n->n_view)
        sel_url = n->items[n->view[n->selected]].url;

    qsort(n->items, (size_t)n->n, sizeof(syn_news_item_t),
          n->sort == NEWS_SORT_TIME ? by_time : by_source);

    n->n_view = 0;
    for (int i = 0; i < n->n; i++)
        if (matches(n, &n->items[i]))
            n->view[n->n_view++] = i;

    int keep = 0;
    if (sel_url)
        for (int r = 0; r < n->n_view; r++)
            if (!strcmp(n->items[n->view[r]].url, sel_url)) { keep = r; break; }

    n->selected = keep;
    if (n->selected >= n->n_view) n->selected = n->n_view ? n->n_view - 1 : 0;

    if (n->scroll > n->selected)              n->scroll = n->selected;
    if (n->selected >= n->scroll + NEWS_ROWS) n->scroll = n->selected - NEWS_ROWS + 1;
    if (n->scroll < 0) n->scroll = 0;
}

static syn_news_item_t *news_sel(syn_news_t *n)
{
    if (n->selected < 0 || n->selected >= n->n_view) return NULL;
    return &n->items[n->view[n->selected]];
}

static void news_move(syn_news_t *n, int delta)
{
    if (!n->n_view) return;

    n->selected += delta;
    if (n->selected < 0)           n->selected = 0;
    if (n->selected >= n->n_view)  n->selected = n->n_view - 1;

    if (n->selected < n->scroll)              n->scroll = n->selected;
    if (n->selected >= n->scroll + NEWS_ROWS) n->scroll = n->selected - NEWS_ROWS + 1;
    if (n->scroll < 0) n->scroll = 0;
}

/* ── Refresh plumbing ────────────────────────────────────── */

static void news_request_fetch(syn_server_t *s)
{
    syn_news_t *n = &s->news;
    if (!n->running || n->fetching) return;

    n->fetching = 1;
    pthread_mutex_lock(&n->lock);
    atomic_store(&n->want, 1);
    pthread_cond_signal(&n->cv);
    pthread_mutex_unlock(&n->lock);
}

/* A finished fetch: fold the new items in, keeping the read-marks. */
static int news_readable(int fd, uint32_t mask, void *data)
{
    (void)mask;
    syn_server_t *s = data;
    syn_news_t   *n = &s->news;

    char drain[64];
    while (read(fd, drain, sizeof(drain)) > 0)
        ;   /* the pipe is only a wake-up; the snapshot came via the lock */

    pthread_mutex_lock(&n->lock);
    int total = n->n_fetched;
    if (total > 0) {
        memcpy(n->items, n->fetched, sizeof(syn_news_item_t) * (size_t)total);
        n->n = total;
    }
    n->failed = n->fetch_failed;
    pthread_mutex_unlock(&n->lock);

    n->fetching = 0;

    if (total > 0) {
        for (int i = 0; i < n->n; i++)
            n->items[i].seen = is_seen(n, n->items[i].url);
        n->updated = time(NULL);
        news_rebuild_view(n);
        news_cache_save(n);

        int unread = 0;
        for (int i = 0; i < n->n; i++) if (!n->items[i].seen) unread++;
        snprintf(n->status, sizeof(n->status), "%d items, %d new%s",
                 n->n, unread,
                 n->failed ? "  (some feeds did not answer)" : "");
    } else {
        snprintf(n->status, sizeof(n->status), "no feeds answered");
    }

    if (n->visible) synui_render_news(s);
    return 0;
}

/* Auto-refresh while the panel is up. Closed, synui makes no network traffic:
 * a news panel nobody is looking at has no business talking to eight hosts. */
static int news_tick(void *data)
{
    syn_server_t *s = data;
    syn_news_t   *n = &s->news;
    if (!n->visible) return 0;

    if (time(NULL) - n->updated >= s->config.news_refresh_min * 60) {
        news_request_fetch(s);
        synui_render_news(s);
    }
    wl_event_source_timer_update(n->timer, 60 * 1000);
    return 0;
}

/* ── Lifecycle ───────────────────────────────────────────── */

void news_init(syn_server_t *s)
{
    syn_news_t *n = &s->news;

    memset(n, 0, sizeof(*n));
    n->filter   = -1;
    n->sort     = NEWS_SORT_TIME;
    n->pipe[0]  = n->pipe[1] = -1;
    atomic_store(&n->stop, 0);
    atomic_store(&n->want, 0);

    if (s->config.news_sources_n > 0) {
        n->n_sources = s->config.news_sources_n;
        memcpy(n->sources, s->config.news_sources,
               sizeof(syn_news_source_t) * (size_t)n->n_sources);
    } else {
        n->n_sources = (int)(sizeof(news_defaults) / sizeof(news_defaults[0]));
        memcpy(n->sources, news_defaults, sizeof(news_defaults));
    }

    news_seen_load(n);
    news_cache_load(n);
    for (int i = 0; i < n->n; i++)
        n->items[i].seen = is_seen(n, n->items[i].url);
    news_rebuild_view(n);

    /* The cache's mtime is when it was written, which is when the last fetch
     * landed — so a session that starts and opens the panel inside the refresh
     * window shows what it had without going near the network. */
    char path[512];
    struct stat st;
    if (news_cache_path(path, sizeof(path)) && stat(path, &st) == 0)
        n->updated = st.st_mtime;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (pipe2(n->pipe, O_CLOEXEC) < 0) {
        wlr_log(WLR_ERROR, "synui: news: pipe() failed");
        n->pipe[0] = n->pipe[1] = -1;
        return;
    }
    fcntl(n->pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(n->pipe[1], F_SETFL, O_NONBLOCK);

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    n->src   = wl_event_loop_add_fd(loop, n->pipe[0], WL_EVENT_READABLE,
                                    news_readable, s);
    n->timer = wl_event_loop_add_timer(loop, news_tick, s);

    pthread_mutex_init(&n->lock, NULL);
    pthread_cond_init(&n->cv, NULL);

    if (pthread_create(&n->thread, NULL, news_thread_fn, s) != 0) {
        wlr_log(WLR_ERROR, "synui: news: thread failed");
        if (n->src)   { wl_event_source_remove(n->src);   n->src = NULL; }
        if (n->timer) { wl_event_source_remove(n->timer); n->timer = NULL; }
        close(n->pipe[0]); close(n->pipe[1]);
        n->pipe[0] = n->pipe[1] = -1;
        pthread_mutex_destroy(&n->lock);
        pthread_cond_destroy(&n->cv);
        return;
    }
    n->running = 1;
}

void news_finish(syn_server_t *s)
{
    syn_news_t *n = &s->news;

    if (n->running) {
        atomic_store(&n->stop, 1);
        pthread_mutex_lock(&n->lock);
        pthread_cond_signal(&n->cv);      /* out of the idle wait… */
        pthread_mutex_unlock(&n->lock);
        pthread_join(n->thread, NULL);    /* …and dl_progress aborts a transfer */
        n->running = 0;
        pthread_mutex_destroy(&n->lock);
        pthread_cond_destroy(&n->cv);
    }
    if (n->src)   { wl_event_source_remove(n->src);   n->src = NULL; }
    if (n->timer) { wl_event_source_remove(n->timer); n->timer = NULL; }
    if (n->pipe[0] >= 0) { close(n->pipe[0]); n->pipe[0] = -1; }
    if (n->pipe[1] >= 0) { close(n->pipe[1]); n->pipe[1] = -1; }

    news_seen_save(n);
    curl_global_cleanup();
}

/* ── Panel ───────────────────────────────────────────────── */

void news_show(syn_server_t *s)
{
    syn_news_t *n = &s->news;

    n->visible   = 1;
    n->searching = 0;
    n->query[0]  = '\0';
    n->selected  = 0;
    n->scroll    = 0;

    news_rebuild_view(n);

    int stale = (time(NULL) - n->updated) >= s->config.news_refresh_min * 60;
    if (n->n == 0 || stale) {
        news_request_fetch(s);
        snprintf(n->status, sizeof(n->status),
                 n->n ? "refreshing…" : "fetching…");
    } else {
        int unread = 0;
        for (int i = 0; i < n->n; i++) if (!n->items[i].seen) unread++;
        snprintf(n->status, sizeof(n->status), "%d items, %d new", n->n, unread);
    }

    wl_event_source_timer_update(n->timer, 60 * 1000);
    synui_render_news(s);
}

void news_hide(syn_server_t *s)
{
    syn_news_t *n = &s->news;

    n->visible   = 0;
    n->searching = 0;
    wl_event_source_timer_update(n->timer, 0);
    news_seen_save(n);
    synui_render_news(s);
    ctlpanel_child_closed(s, "news");
}

void news_toggle(syn_server_t *s)
{
    if (s->news.visible) news_hide(s);
    else                 news_show(s);
}

/* Open the article (or its discussion). `close_panel` is what separates Enter
 * — read this, I'm done here — from `o`, which opens in the background and
 * leaves the list up so a run of stories can be opened in tabs. */
static void news_open(syn_server_t *s, int comments, int close_panel)
{
    syn_news_t *n = &s->news;
    syn_news_item_t *it = news_sel(n);
    if (!it) return;

    const char *url = comments && it->comments[0] ? it->comments : it->url;
    if (comments && !it->comments[0]) {
        snprintf(n->status, sizeof(n->status), "no discussion link for this one");
        synui_render_news(s);
        return;
    }

    if (news_open_url(url) < 0) {
        snprintf(n->status, sizeof(n->status), "could not open that link");
        synui_render_news(s);
        return;
    }

    mark_seen(n, it);
    if (close_panel) {
        news_hide(s);
        return;
    }
    snprintf(n->status, sizeof(n->status), "opened%s", comments ? " discussion" : "");
    news_move(n, 1);
    synui_render_news(s);
}

/* Search mode is a plain typing buffer. It filters as you type, so there is
 * nothing to submit: Enter just puts the keys back to normal use. */
static int news_search_key(syn_server_t *s, xkb_keysym_t sym)
{
    syn_news_t *n = &s->news;
    size_t len = strlen(n->query);

    switch (sym) {
    case XKB_KEY_Escape:
        n->searching = 0;
        n->query[0]  = '\0';
        news_rebuild_view(n);
        break;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        n->searching = 0;   /* keep the filter, hand the keys back */
        break;
    case XKB_KEY_BackSpace:
        if (len) n->query[len - 1] = '\0';
        news_rebuild_view(n);
        break;
    case XKB_KEY_Down:
        news_move(n, 1);
        break;
    case XKB_KEY_Up:
        news_move(n, -1);
        break;
    default:
        if (sym >= 0x20 && sym < 0x7f && len < NEWS_QUERY_MAX - 1) {
            n->query[len]     = (char)sym;
            n->query[len + 1] = '\0';
            news_rebuild_view(n);
        }
        break;
    }
    synui_render_news(s);
    return 1;
}

/* ── Pointer ─────────────────────────────────────────────────
 *
 * See the panel pointer contract in synui.h. A list of headlines is the one
 * thing on this desktop that already behaves like a web page, so it behaves
 * like one: hovering highlights, one click selects, and a double click opens
 * the story — the same 400ms window the pickers use.
 *
 * Not single-click-to-open. Launching a browser is the most expensive thing any
 * of these panels can do, and doing it on a stray click while scrolling a feed
 * would be the worst possible time for it. */

int news_motion(syn_server_t *s, double lx, double ly)
{
    syn_news_t *n = &s->news;
    if (!n->visible) return 0;
    if (n->searching) return 1;      /* the search box owns the panel */

    int i = hit_index_at(&n->hit, lx, ly);
    if (i < 0 || i >= n->n_view || i == n->selected) return 1;
    n->selected = i;
    synui_render_news(s);
    return 1;
}

int news_click(syn_server_t *s, double lx, double ly, uint32_t button,
               uint32_t time_msec)
{
    syn_news_t *n = &s->news;
    if (!n->visible) return 0;

    if (!hit_in_panel(&n->hit, lx, ly)) {
        news_hide(s);
        return 1;
    }

    if (button != BTN_LEFT || n->searching) return 1;

    int i = hit_index_at(&n->hit, lx, ly);
    if (i < 0 || i >= n->n_view) return 1;   /* chrome */

    bool dbl = (n->last_click_row == i) &&
               (time_msec - n->last_click_ms < 400);
    n->last_click_row = dbl ? -1 : i;
    n->last_click_ms  = time_msec;

    n->selected = i;
    if (dbl) news_open(s, 0, 1);             /* what Enter does */
    else     synui_render_news(s);
    return 1;
}

int news_scroll(syn_server_t *s, double lx, double ly, double delta)
{
    (void)lx; (void)ly;
    syn_news_t *n = &s->news;
    if (!n->visible) return 0;
    if (delta == 0) return 1;

    news_move(n, delta > 0 ? 3 : -3);
    synui_render_news(s);
    return 1;
}

int news_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    syn_news_t *n = &s->news;
    if (!n->visible) return 0;

    /* Same modal contract as the task manager: Super/Ctrl/Alt combos still
     * reach the global binds, bare Shift is ours (it types capitals in the
     * search box, and '/' needs it on most layouts). */
    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    /* Shift+a reaches us as 'A' from a real keyboard, but as 'a' with the shift
     * bit from a *virtual* one — a virtual-keyboard client (wtype; the path
     * waybar's start menu drives synui through) ships a keymap with a single
     * level per keycode, so there is no shifted level to resolve to. Normalize
     * both to the shifted form, or every Shift-keyed action here silently works
     * only when a human is at the keys. */
    if (mods & WLR_MODIFIER_SHIFT) {
        if (sym >= XKB_KEY_a && sym <= XKB_KEY_z)
            sym = XKB_KEY_A + (sym - XKB_KEY_a);
        else if (sym == XKB_KEY_Tab)
            sym = XKB_KEY_ISO_Left_Tab;
    }

    if (n->searching) return news_search_key(s, sym);

    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        news_hide(s);
        return 1;

    case XKB_KEY_j:
    case XKB_KEY_Down:
        news_move(n, 1);
        break;
    case XKB_KEY_k:
    case XKB_KEY_Up:
        news_move(n, -1);
        break;
    case XKB_KEY_Page_Down:
        news_move(n, NEWS_ROWS);
        break;
    case XKB_KEY_Page_Up:
        news_move(n, -NEWS_ROWS);
        break;
    case XKB_KEY_Home:
    case XKB_KEY_g:
        news_move(n, -n->n_view);
        break;
    case XKB_KEY_End:
    case XKB_KEY_G:
        news_move(n, n->n_view);
        break;

    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        news_open(s, 0, 1);
        return 1;
    case XKB_KEY_o:
        news_open(s, 0, 0);
        return 1;
    case XKB_KEY_c:
        news_open(s, 1, 1);
        return 1;

    case XKB_KEY_y: {
        syn_news_item_t *it = news_sel(n);
        if (it && news_copy_url(it->url) == 0)
            snprintf(n->status, sizeof(n->status), "copied %s", it->url);
        break;
    }

    case XKB_KEY_Tab:
        /* Cycle: all sources -> each source in turn -> back to all. */
        n->filter = (n->filter + 1 >= n->n_sources) ? -1 : n->filter + 1;
        news_rebuild_view(n);
        break;
    case XKB_KEY_ISO_Left_Tab:   /* Shift+Tab */
        n->filter = (n->filter < 0) ? n->n_sources - 1 : n->filter - 1;
        news_rebuild_view(n);
        break;
    case XKB_KEY_a:
        n->filter = -1;
        news_rebuild_view(n);
        break;

    case XKB_KEY_s:
        n->sort = (n->sort == NEWS_SORT_TIME) ? NEWS_SORT_SOURCE : NEWS_SORT_TIME;
        snprintf(n->status, sizeof(n->status), "sorted by %s",
                 n->sort == NEWS_SORT_TIME ? "time" : "source");
        news_rebuild_view(n);
        break;

    case XKB_KEY_slash:
        n->searching = 1;
        n->query[0]  = '\0';
        news_rebuild_view(n);
        break;

    case XKB_KEY_m: {
        syn_news_item_t *it = news_sel(n);
        if (it) {
            if (it->seen) it->seen = 0;   /* the mark stays in news.seen; this
                                           * is a per-session "keep it lit" */
            else          mark_seen(n, it);
        }
        break;
    }
    case XKB_KEY_A:
        for (int i = 0; i < n->n; i++) mark_seen(n, &n->items[i]);
        snprintf(n->status, sizeof(n->status), "all marked read");
        break;

    case XKB_KEY_r:
        news_request_fetch(s);
        snprintf(n->status, sizeof(n->status), "refreshing…");
        break;

    default:
        return 1;   /* modal: swallow anything else while we are up */
    }

    synui_render_news(s);
    return 1;
}
