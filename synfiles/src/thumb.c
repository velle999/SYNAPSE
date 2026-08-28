/* thumb.c — a preview frame for a video, in the shared thumbnail cache.
 *
 * synfiles has always SHOWN video thumbnails: previewFor() in the QML looks in
 * ~/.cache/thumbnails first, so anything Dolphin or a video player had already
 * generated appeared for free. What it could not do was MAKE one, and the code
 * said so — "a video never can: that needs a thumbnailer". On a desktop where
 * synfiles is the only file manager, nothing else ever ran one, so a folder of
 * films drew a row of identical film-strip glyphs for ever.
 *
 * ⛔ THE SHARED CACHE, WRITTEN TO THE SPEC, NOT A PRIVATE ONE. The freedesktop
 * layout exists so that the expensive half of this — decoding video — is done
 * once per file per machine rather than once per application. A private
 * ~/.cache/synfiles/thumbs would work and would be less code; it would also
 * decode every film a second time the first time anything else looked at the
 * folder. So the entries go where everyone else's do, in their format:
 *
 *   $XDG_CACHE_HOME/thumbnails/{normal,large}/<md5 of the URI>.png
 *
 * ⛔ AND THE `Thumb::MTime` tEXt CHUNK IS NOT DECORATION. It is how any
 * consumer — including this one — tells a current thumbnail from one of a file
 * that has since been re-encoded. Written without it, an entry is not merely
 * impolite: it is a picture with no way to know it has gone stale, and a
 * well-behaved reader is entitled to throw it away and do the work again.
 * ffmpeg cannot write it (-metadata does not reach the PNG muxer; measured),
 * so the chunks are spliced in here.
 *
 * ⚠ ffmpeg, NOT ffmpegthumbnailer. The purpose-built tool is better at this and
 * is the obvious dependency — and it is not installed on the machine this was
 * written on, so every path through it would have shipped untested. ffmpeg is
 * already here for syn-arcade and synstudio. If ffmpegthumbnailer is added to
 * the base set later, preferring it is a small change and a testable one.
 *
 * ⚠ NOTHING HERE RUNS PER LISTING. Decoding video is not something to do while
 * a folder is being drawn: the GUI asks for one thumbnail at a time, only for
 * rows on screen, only for videos with nothing in the cache — see synfiles.qml.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The two sizes the spec defines, and the longest edge each allows. */
#define THUMB_NORMAL 128
#define THUMB_LARGE  256

/* Where in the film to take the frame. The first frame is a title card, a
 * fade-in from black or a distributor's logo often enough that it is the wrong
 * default; a tenth of the way in is what ffmpegthumbnailer picks and it is a
 * good answer for the same reason. */
#define THUMB_SEEK_PCT 10

/* A film this program will not sit and decode. Seeking is a keyframe jump so
 * the cost barely tracks the file size, but a corrupt or pathological input can
 * make ffmpeg work for a long time, and this runs behind a user interface. */
#define THUMB_TIMEOUT_S 20

/* ── MD5 ────────────────────────────────────────────────────────────────────
 *
 * The cache key, and the spec names the algorithm, so there is no choice to
 * make here and nothing security-bearing about it: this hashes a filename to a
 * filename. RFC 1321, written out because synfiles links nothing but libc and
 * one dependency for one hash is a poor trade.
 */
typedef struct {
	uint32_t a, b, c, d;
	uint64_t len;
	unsigned char buf[64];
	size_t have;
} md5_t;

static const uint32_t MD5_K[64] = {
	0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
	0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
	0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
	0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
	0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
	0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
	0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
	0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
	0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
	0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
	0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};
static const unsigned char MD5_S[64] = {
	7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
	5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
	4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
	6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

static uint32_t rotl32(uint32_t x, unsigned n) { return (x << n) | (x >> (32 - n)); }

static void md5_block(md5_t *h, const unsigned char *p)
{
	uint32_t m[16];
	for (int i = 0; i < 16; i++)
		m[i] = (uint32_t)p[i * 4] | ((uint32_t)p[i * 4 + 1] << 8) |
		       ((uint32_t)p[i * 4 + 2] << 16) | ((uint32_t)p[i * 4 + 3] << 24);

	uint32_t a = h->a, b = h->b, c = h->c, d = h->d;
	for (int i = 0; i < 64; i++) {
		uint32_t f;
		int g;
		if (i < 16)      { f = (b & c) | (~b & d);        g = i; }
		else if (i < 32) { f = (d & b) | (~d & c);        g = (5 * i + 1) & 15; }
		else if (i < 48) { f = b ^ c ^ d;                 g = (3 * i + 5) & 15; }
		else             { f = c ^ (b | ~d);              g = (7 * i) & 15; }
		f += a + MD5_K[i] + m[g];
		a = d; d = c; c = b;
		b += rotl32(f, MD5_S[i]);
	}
	h->a += a; h->b += b; h->c += c; h->d += d;
}

static void md5_init(md5_t *h)
{
	h->a = 0x67452301; h->b = 0xefcdab89;
	h->c = 0x98badcfe; h->d = 0x10325476;
	h->len = 0; h->have = 0;
}

static void md5_push(md5_t *h, const void *data, size_t n)
{
	const unsigned char *p = data;
	h->len += n;
	while (n) {
		size_t take = 64 - h->have;
		if (take > n) take = n;
		memcpy(h->buf + h->have, p, take);
		h->have += take; p += take; n -= take;
		if (h->have == 64) { md5_block(h, h->buf); h->have = 0; }
	}
}

/* Into 32 lowercase hex characters plus a terminator — the filename form. */
static void md5_hex(md5_t *h, char out[33])
{
	uint64_t bits = h->len * 8;
	unsigned char pad = 0x80;
	md5_push(h, &pad, 1);
	unsigned char zero = 0;
	while (h->have != 56) md5_push(h, &zero, 1);

	unsigned char tail[8];
	for (int i = 0; i < 8; i++) tail[i] = (unsigned char)(bits >> (8 * i));
	/* Straight into the block: md5_push would count these as message bytes. */
	memcpy(h->buf + 56, tail, 8);
	md5_block(h, h->buf);

	const uint32_t w[4] = { h->a, h->b, h->c, h->d };
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			snprintf(out + (i * 4 + j) * 2, 3, "%02x",
			         (unsigned)((w[i] >> (8 * j)) & 0xff));
	out[32] = '\0';
}

static void md5_of(const char *s, char out[33])
{
	md5_t h;
	md5_init(&h);
	md5_push(&h, s, strlen(s));
	md5_hex(&h, out);
}

/* ── CRC32, for the PNG chunks written below ────────────────────────────── */

static uint32_t crc32_of(const unsigned char *p, size_t n, uint32_t crc)
{
	static uint32_t table[256];
	static bool built;
	if (!built) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		built = true;
	}
	crc ^= 0xffffffffu;
	while (n--) crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
	return crc ^ 0xffffffffu;
}

/* ── the cache ──────────────────────────────────────────────────────────── */

/* $XDG_CACHE_HOME, defaulting to ~/.cache. malloc'd. */
static char *cache_home(void)
{
	const char *env = getenv("XDG_CACHE_HOME");
	char *p = NULL;
	if (env && *env == '/') {
		p = strdup(env);
	} else if (asprintf(&p, "%s/.cache", home_dir()) < 0) {
		p = NULL;
	}
	return p;
}

/* The URI whose MD5 names the file, exactly as every other implementation
 * builds it: file:// followed by the percent-encoded absolute path.
 *
 * ⚠ THE SLASHES ARE ALREADY RIGHT. An absolute path opens with one, so
 * "file://" + "/home/…" is the "file:///home/…" the spec asks for; a third
 * slash added here would hash to something nothing else agrees with. This is
 * the same string the QML builds with "file://" + row.full, and the two MUST
 * agree or each writes entries the other cannot find. */
static char *thumb_uri(const char *abs_path)
{
	char *enc = pct_encode(abs_path, true);
	if (!enc) return NULL;
	char *uri = NULL;
	if (asprintf(&uri, "file://%s", enc) < 0) uri = NULL;
	free(enc);
	return uri;
}

char *thumb_cache_path(const char *abs_path, bool large)
{
	char *uri = thumb_uri(abs_path);
	if (!uri) return NULL;
	char hash[33];
	md5_of(uri, hash);
	free(uri);

	char *cache = cache_home();
	if (!cache) return NULL;
	char *out = NULL;
	if (asprintf(&out, "%s/thumbnails/%s/%s.png", cache,
	             large ? "large" : "normal", hash) < 0)
		out = NULL;
	free(cache);
	return out;
}

/* ── PNG: splice the spec's text chunks in after IHDR ───────────────────── */

static bool put_be32(FILE *f, uint32_t v)
{
	unsigned char b[4] = { (unsigned char)(v >> 24), (unsigned char)(v >> 16),
	                       (unsigned char)(v >> 8), (unsigned char)v };
	return fwrite(b, 1, 4, f) == 4;
}

/* One tEXt chunk: length, "tEXt", key\0value, CRC over type+data. */
static bool write_text_chunk(FILE *f, const char *key, const char *val)
{
	size_t kl = strlen(key), vl = strlen(val);
	size_t dl = kl + 1 + vl;
	unsigned char *data = malloc(4 + dl);
	if (!data) return false;
	memcpy(data, "tEXt", 4);
	memcpy(data + 4, key, kl);
	data[4 + kl] = '\0';
	memcpy(data + 4 + kl + 1, val, vl);

	bool ok = put_be32(f, (uint32_t)dl)
	       && fwrite(data, 1, 4 + dl, f) == 4 + dl
	       && put_be32(f, crc32_of(data, 4 + dl, 0));
	free(data);
	return ok;
}

/* Rewrite `png` with Thumb::URI and Thumb::MTime inserted directly after IHDR.
 *
 * ⚠ AFTER IHDR AND BEFORE EVERYTHING ELSE. tEXt may legally appear almost
 * anywhere, but IHDR must come first, and a reader that stops at the first
 * IDAT — which is the sensible way to read metadata — never sees a chunk put
 * at the end. */
static bool png_add_thumb_text(const char *png, const char *uri, long mtime)
{
	FILE *in = fopen(png, "rb");
	if (!in) return false;

	unsigned char sig[8];
	if (fread(sig, 1, 8, in) != 8 ||
	    memcmp(sig, "\x89PNG\r\n\x1a\n", 8) != 0) { fclose(in); return false; }

	/* IHDR: length, type, data, crc — a fixed 25 bytes, but read the length
	 * rather than assuming it. */
	unsigned char lenb[4];
	if (fread(lenb, 1, 4, in) != 4) { fclose(in); return false; }
	uint32_t ihdr_len = ((uint32_t)lenb[0] << 24) | ((uint32_t)lenb[1] << 16) |
	                    ((uint32_t)lenb[2] << 8) | lenb[3];
	unsigned char *ihdr = malloc(4 + ihdr_len + 4);
	if (!ihdr) { fclose(in); return false; }
	if (fread(ihdr, 1, 4 + ihdr_len + 4, in) != 4 + ihdr_len + 4) {
		free(ihdr); fclose(in); return false;
	}

	char *tmp = NULL;
	if (asprintf(&tmp, "%s.txt.tmp", png) < 0) {
		free(ihdr); fclose(in); return false;
	}
	FILE *out = fopen(tmp, "wb");
	if (!out) { free(tmp); free(ihdr); fclose(in); return false; }

	char mt[32];
	snprintf(mt, sizeof(mt), "%ld", mtime);

	bool ok = fwrite(sig, 1, 8, out) == 8
	       && fwrite(lenb, 1, 4, out) == 4
	       && fwrite(ihdr, 1, 4 + ihdr_len + 4, out) == 4 + ihdr_len + 4
	       && write_text_chunk(out, "Thumb::URI", uri)
	       && write_text_chunk(out, "Thumb::MTime", mt);

	if (ok) {
		char buf[65536];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
			if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
	}
	free(ihdr);
	fclose(in);
	if (fclose(out) != 0) ok = false;

	if (ok && rename(tmp, png) != 0) ok = false;
	if (!ok) unlink(tmp);
	free(tmp);
	return ok;
}

/* ── reading what is already there ──────────────────────────────────────── */

/* The Thumb::MTime a cached entry claims, or -1 when it has none. Only the
 * header is read: the value sits before the first IDAT by construction. */
static long png_thumb_mtime(const char *png)
{
	FILE *f = fopen(png, "rb");
	if (!f) return -1;
	unsigned char sig[8];
	long found = -1;
	if (fread(sig, 1, 8, f) == 8 && !memcmp(sig, "\x89PNG\r\n\x1a\n", 8)) {
		for (;;) {
			unsigned char h[8];
			if (fread(h, 1, 8, f) != 8) break;
			uint32_t len = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) |
			               ((uint32_t)h[2] << 8) | h[3];
			if (!memcmp(h + 4, "IDAT", 4) || !memcmp(h + 4, "IEND", 4)) break;
			if (!memcmp(h + 4, "tEXt", 4) && len < 4096) {
				char *d = malloc(len + 1);
				if (!d) break;
				if (fread(d, 1, len, f) != len) { free(d); break; }
				d[len] = '\0';
				if (!strcmp(d, "Thumb::MTime")) found = atol(d + 13);
				free(d);
				if (fseek(f, 4, SEEK_CUR) != 0) break;   /* the CRC */
				if (found >= 0) break;
				continue;
			}
			if (fseek(f, (long)len + 4, SEEK_CUR) != 0) break;
		}
	}
	fclose(f);
	return found;
}

/* ── making one ─────────────────────────────────────────────────────────── */

/* The film's length in seconds, or -1. Used only to choose where to seek, so a
 * stream that will not report one is not an error — it just gets frame zero. */
static double video_duration(const char *path)
{
	if (!have_cmd("ffprobe")) return -1;
	char *const argv[] = {
		(char *)"ffprobe", (char *)"-v", (char *)"error",
		(char *)"-show_entries", (char *)"format=duration",
		(char *)"-of", (char *)"default=nw=1:nk=1",
		(char *)path, NULL
	};
	int st = 0;
	char *out = run_capture(argv, &st, true);
	if (!out) return -1;
	double d = (st == 0) ? atof(out) : -1;
	free(out);
	return (d > 0) ? d : -1;
}

static bool ensure_dir(const char *path)
{
	/* mkdir -p, one component at a time. The cache root may not exist at all
	 * on a machine where nothing has ever made a thumbnail. */
	char *copy = strdup(path);
	if (!copy) return false;
	for (char *p = copy + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		if (mkdir(copy, 0700) != 0 && errno != EEXIST) { free(copy); return false; }
		*p = '/';
	}
	bool ok = (mkdir(copy, 0700) == 0 || errno == EEXIST);
	free(copy);
	return ok;
}

/* Generate a thumbnail for one video. Returns the cache path (malloc'd) or
 * NULL, and never regenerates an entry that is already current unless forced.
 *
 * ⚠ THE STALENESS TEST IS THE SPEC'S, not the file's own timestamp. A cache
 * entry records the mtime of the video it was made from; comparing that is the
 * only way to notice a film that was re-encoded in place, which leaves the
 * thumbnail newer than the source and a timestamp comparison none the wiser. */
char *thumb_make(const char *path, bool large, bool force, const char **why)
{
	if (why) *why = NULL;

	char *abs = sf_resolve(path);
	if (!abs) { if (why) *why = "no such file"; return NULL; }

	struct stat st;
	if (stat(abs, &st) != 0 || !S_ISREG(st.st_mode)) {
		if (why) *why = "not a regular file";
		free(abs);
		return NULL;
	}

	/* ⚠ THE SECOND ARGUMENT IS is_dir, NOT "sniff the contents". Passing true
	 * here called every file a directory and this refused every video with
	 * "not a video". */
	const char *mime = mime_for(abs, false);
	if (!mime || strncmp(mime, "video/", 6) != 0) {
		if (why) *why = "not a video";
		free(abs);
		return NULL;
	}

	char *out = thumb_cache_path(abs, large);
	if (!out) { if (why) *why = "cannot build the cache path"; free(abs); return NULL; }

	if (!force && png_thumb_mtime(out) == (long)st.st_mtime) {
		free(abs);
		return out;                     /* already current — nothing to do */
	}

	if (!have_cmd("ffmpeg")) {
		if (why) *why = "ffmpeg is not installed";
		free(abs); free(out);
		return NULL;
	}

	/* The directory, and a temp file beside the target: the spec asks for an
	 * atomic replace so a reader never sees a half-written PNG, and a rename
	 * only counts as one within a filesystem. */
	char *dir = strdup(out);
	char *slash = dir ? strrchr(dir, '/') : NULL;
	if (!dir || !slash) { if (why) *why = "out of memory"; free(abs); free(out); free(dir); return NULL; }
	*slash = '\0';
	if (!ensure_dir(dir)) {
		if (why) *why = "cannot create the cache directory";
		free(abs); free(out); free(dir);
		return NULL;
	}
	free(dir);

	char *tmp = NULL;
	if (asprintf(&tmp, "%s.%d.tmp", out, (int)getpid()) < 0) {
		if (why) *why = "out of memory";
		free(abs); free(out);
		return NULL;
	}

	double dur = video_duration(abs);
	char ss[32];
	/* Clamped inside the film: seeking past the end produces no frame at all,
	 * and a two-second clip has no tenth-of-the-way-in worth having. */
	double at = (dur > 0) ? dur * THUMB_SEEK_PCT / 100.0 : 0;
	if (dur > 0 && at > dur - 0.1) at = 0;
	snprintf(ss, sizeof(ss), "%.2f", at);

	char scale[64];
	int edge = large ? THUMB_LARGE : THUMB_NORMAL;
	snprintf(scale, sizeof(scale),
	         "scale=%d:%d:force_original_aspect_ratio=decrease", edge, edge);

	char to[32];
	snprintf(to, sizeof(to), "%d", THUMB_TIMEOUT_S);

	/* -ss BEFORE -i is the keyframe seek: it costs about the same on a
	 * two-hour film as on a two-minute one, which is what makes this usable
	 * from a file manager at all. */
	char *const argv[] = {
		(char *)"timeout", (char *)to,
		(char *)"ffmpeg", (char *)"-v", (char *)"error", (char *)"-y",
		(char *)"-ss", ss, (char *)"-i", abs,
		(char *)"-frames:v", (char *)"1",
		(char *)"-vf", scale,
		/* ⚠ THE FORMAT IS NAMED, NOT INFERRED. ffmpeg picks its muxer from the
		 * output's extension, and the temp file this writes ends in ".tmp" —
		 * "Unable to choose an output format", every time, for a command that
		 * is correct in every other respect. */
		(char *)"-f", (char *)"image2", (char *)"-c:v", (char *)"png",
		tmp, NULL
	};
	int status = 0;
	char *cap = run_capture(argv, &status, true);
	free(cap);

	/* Did ffmpeg actually write a frame? An exit status of 0 is not enough on
	 * its own — a stream with no video track leaves an empty file behind.
	 *
	 * `tmp` is a name this process invented — the target plus its own pid —
	 * inside a 0700 directory it created, and the only use after the check is
	 * unlink() of that same name. Nothing opens or trusts it. */
	struct stat ts;
	int wrote = stat(tmp, &ts);   /* toctou-ok: only unlink()ed after this */
	if (status != 0 || wrote != 0 || ts.st_size == 0) {
		if (why) *why = "ffmpeg could not read a frame";
		unlink(tmp);
		free(tmp); free(abs); free(out);
		return NULL;
	}

	char *uri = thumb_uri(abs);
	if (!uri || !png_add_thumb_text(tmp, uri, (long)st.st_mtime)) {
		if (why) *why = "cannot write the thumbnail's metadata";
		unlink(tmp);
		free(uri); free(tmp); free(abs); free(out);
		return NULL;
	}
	free(uri);

	/* The spec asks for 0600: a thumbnail can reveal the contents of a file
	 * whose own permissions are stricter than the cache directory's. */
	if (chmod(tmp, 0600) != 0 || rename(tmp, out) != 0) {
		if (why) *why = "cannot install the thumbnail";
		unlink(tmp);
		free(tmp); free(abs); free(out);
		return NULL;
	}

	free(tmp);
	free(abs);
	return out;
}

/* ── the command ────────────────────────────────────────────────────────── */

int cmd_thumb(int argc, char **argv)
{
	bool large = true, force = false;
	const char *paths[256];
	int n = 0;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--force")) { force = true; continue; }
		if (!strcmp(argv[i], "--size=normal")) { large = false; continue; }
		if (!strcmp(argv[i], "--size=large"))  { large = true;  continue; }
		if (!strncmp(argv[i], "--", 2)) {
			fprintf(stderr, "synfiles: thumb: unknown option '%s'\n", argv[i]);
			return 2;
		}
		if (n < (int)(sizeof(paths) / sizeof(*paths))) paths[n++] = argv[i];
	}

	if (!n) {
		fprintf(stderr, "usage: synfiles thumb [--size=normal|large] [--force] <video>...\n");
		return 2;
	}

	int failures = 0;
	for (int i = 0; i < n; i++) {
		const char *why = NULL;
		char *made = thumb_make(paths[i], large, force, &why);
		if (made) {
			printf("%s\n", made);
			free(made);
		} else {
			fprintf(stderr, "synfiles: thumb: %s: %s\n",
			        paths[i], why ? why : "failed");
			failures++;
		}
	}
	return failures ? 1 : 0;
}
