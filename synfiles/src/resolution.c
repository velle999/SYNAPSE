/* resolution.c — the pixel dimensions of a media file, for the properties pane.
 *
 * "How big is this picture" is the one thing a properties dialog is asked that
 * stat() cannot answer, so it is the one place this program reads a file's
 * CONTENT rather than its metadata. Three rules keep that from turning into a
 * second mime system:
 *
 *   - Only `info` calls this, and `info` is one file that a person asked
 *     about. A listing never does. Reading 4000 files to draw 4000 rows is the
 *     bug mime.c exists to avoid ([[globs2, not libmagic]]) and adding it back
 *     under a different name would stall the same network share.
 *   - The format is decided by MAGIC BYTES, never by the extension. A phone
 *     photo saved as `.txt` still has its dimensions read, and — the half that
 *     actually matters — a text file called `.png` is never mis-parsed into a
 *     plausible-looking 1.2-billion-pixel answer.
 *   - Every parser reads a bounded number of bytes at explicit offsets with
 *     pread() and validates before it trusts. Nothing here seeks around a file
 *     under the control of whoever wrote it without a loop guard.
 *
 * Containers (MP4/MOV/AVIF/HEIF) share one bounded box walker: the layouts
 * differ but the framing is identical, and two copies of a box walk is two
 * places for an unchecked length to be wrong.
 *
 * Video that is not ISO-BMFF — Matroska, WebM, AVI — is DELEGATED to ffprobe,
 * for the same reason mounting is delegated to udisks2: the tool that owns the
 * format is already installed on any machine that plays the file, and a
 * hand-rolled EBML reader would be a new parser for every codec container that
 * ever ships. When ffprobe is absent the row is simply not shown; a properties
 * pane missing a line is a smaller failure than one showing a guess.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Nothing this file believes may exceed what a real image can be. The largest
 * dimension any of these formats can even encode is 2^32-1; a frame that says
 * so is corrupt or hostile, and printing it would put a nonsense number in a
 * dialog next to the true file size. */
#define DIM_MAX 1000000L

static bool plausible(long w, long h)
{
	return w > 0 && h > 0 && w <= DIM_MAX && h <= DIM_MAX;
}

/* Keep the biggest candidate. Several formats hold more than one size — an
 * ICO holds every icon in the file, a HEIF holds thumbnails beside the photo,
 * an MP4 holds one track per stream — and the largest is the one a person
 * means when they ask how big it is. */
static void keep_bigger(long cw, long ch, long *w, long *h)
{
	if (!plausible(cw, ch))
		return;
	if (cw * ch > *w * *h) {
		*w = cw;
		*h = ch;
	}
}

static uint32_t be32(const unsigned char *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
	       (uint32_t)p[2] << 8  | p[3];
}
static uint32_t le32(const unsigned char *p)
{
	return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 |
	       (uint32_t)p[1] << 8  | p[0];
}
static unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned le16(const unsigned char *p) { return (unsigned)p[1] << 8 | p[0]; }

/* pread that either fills the buffer exactly or fails. A short read at the
 * offset a header claims to live at means the file is truncated, which is not
 * a partial answer worth having. */
static bool at(int fd, void *buf, size_t n, off_t off)
{
	size_t got = 0;
	while (got < n) {
		ssize_t r = pread(fd, (char *)buf + got, n - got, off + (off_t)got);
		if (r <= 0)
			return false;
		got += (size_t)r;
	}
	return true;
}

/* ── PNG ─────────────────────────────────────────────────────────────────────
 * The IHDR chunk is mandatory and must come first, so its position is fixed:
 * 8 bytes signature, 4 length, 4 type, then width and height. */
static bool png_size(int fd, long *w, long *h)
{
	unsigned char b[8];
	if (!at(fd, b, 8, 16))
		return false;
	keep_bigger(be32(b), be32(b + 4), w, h);
	return plausible(*w, *h);
}

/* ── GIF ─────────────────────────────────────────────────────────────────── */
static bool gif_size(int fd, long *w, long *h)
{
	unsigned char b[4];
	if (!at(fd, b, 4, 6))
		return false;
	keep_bigger(le16(b), le16(b + 2), w, h);
	return plausible(*w, *h);
}

/* ── BMP ─────────────────────────────────────────────────────────────────────
 * Two header shapes are still in the wild. The 12-byte BITMAPCOREHEADER holds
 * 16-bit dimensions; everything since holds 32-bit signed ones, and a NEGATIVE
 * height is legal — it means the rows are stored top-down, not that the image
 * is -600 pixels tall. */
static bool bmp_size(int fd, long *w, long *h)
{
	unsigned char sz[4];
	if (!at(fd, sz, 4, 14))
		return false;

	unsigned char b[8];
	if (le32(sz) == 12) {
		if (!at(fd, b, 4, 18))
			return false;
		keep_bigger(le16(b), le16(b + 2), w, h);
	} else {
		if (!at(fd, b, 8, 18))
			return false;
		long cw = (int32_t)le32(b), ch = (int32_t)le32(b + 4);
		keep_bigger(cw < 0 ? -cw : cw, ch < 0 ? -ch : ch, w, h);
	}
	return plausible(*w, *h);
}

/* ── JPEG ────────────────────────────────────────────────────────────────────
 * The size lives in a start-of-frame marker, which sits after however many
 * EXIF, colour-profile and comment segments the camera felt like writing — so
 * this is the one format that has to walk. Every step is a length taken from
 * the file, hence the iteration cap: a segment length of zero would otherwise
 * be an infinite loop on a corrupt file.
 *
 * These are the ENCODED dimensions. A photo with an EXIF orientation of 6
 * displays rotated, so a viewer shows 3000×4000 where this says 4000×3000;
 * reading the orientation would mean an EXIF parser, and the encoded size is
 * what every other tool that does not have one also reports. */
static bool jpeg_size(int fd, long *w, long *h)
{
	off_t off = 2;
	for (int guard = 0; guard < 2048; guard++) {
		unsigned char m[2];
		if (!at(fd, m, 2, off) || m[0] != 0xFF)
			return false;
		unsigned char id = m[1];
		off += 2;
		/* 0xFF is also the fill byte between markers. */
		while (id == 0xFF) {
			if (!at(fd, &id, 1, off))
				return false;
			off++;
		}
		/* Standalone markers carry no length field to skip over. */
		if (id == 0x01 || (id >= 0xD0 && id <= 0xD7))
			continue;
		/* Entropy-coded data starts here and the frame header is behind us. */
		if (id == 0xDA || id == 0xD9)
			return false;

		unsigned char len[2];
		if (!at(fd, len, 2, off))
			return false;
		unsigned seg = be16(len);
		if (seg < 2)
			return false;

		/* SOF0..SOF15, minus the three markers that share the range and are
		 * not frame headers: DHT (C4), JPG (C8) and DAC (CC). */
		if (id >= 0xC0 && id <= 0xCF && id != 0xC4 && id != 0xC8 && id != 0xCC) {
			unsigned char s[5];
			if (!at(fd, s, 5, off + 2))
				return false;
			keep_bigger(be16(s + 3), be16(s + 1), w, h);   /* height first */
			return plausible(*w, *h);
		}
		off += seg;
	}
	return false;
}

/* ── WebP ────────────────────────────────────────────────────────────────────
 * Three sub-formats behind one RIFF header. VP8X comes first when it is there
 * at all, and it is the only one that knows the canvas size of an animation. */
static bool webp_size(int fd, long *w, long *h)
{
	unsigned char fourcc[4], p[10];
	if (!at(fd, fourcc, 4, 12))
		return false;

	if (!memcmp(fourcc, "VP8X", 4)) {
		if (!at(fd, p, 10, 20))
			return false;
		long cw = ((long)p[4] | (long)p[5] << 8 | (long)p[6] << 16) + 1;
		long ch = ((long)p[7] | (long)p[8] << 8 | (long)p[9] << 16) + 1;
		keep_bigger(cw, ch, w, h);
	} else if (!memcmp(fourcc, "VP8L", 4)) {
		if (!at(fd, p, 5, 20) || p[0] != 0x2F)
			return false;
		uint32_t bits = le32(p + 1);
		keep_bigger((bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1, w, h);
	} else if (!memcmp(fourcc, "VP8 ", 4)) {
		if (!at(fd, p, 10, 20))
			return false;
		/* Only a keyframe carries the size, and it is stamped with this sync
		 * code — without the check, an inter-frame's payload would be read as
		 * dimensions. */
		if (p[3] != 0x9D || p[4] != 0x01 || p[5] != 0x2A)
			return false;
		keep_bigger(le16(p + 6) & 0x3FFF, le16(p + 8) & 0x3FFF, w, h);
	}
	return plausible(*w, *h);
}

/* ── TIFF ────────────────────────────────────────────────────────────────────
 * Both byte orders, first directory only. Camera raws (CR2, NEF, DNG) are
 * TIFFs too, so this answers for those as far as their first IFD goes. */
static bool tiff_size(int fd, long *w, long *h)
{
	unsigned char hdr[8];
	if (!at(fd, hdr, 8, 0))
		return false;
	bool big = hdr[0] == 'M';
	uint32_t ifd = big ? be32(hdr + 4) : le32(hdr + 4);
	if (ifd < 8)
		return false;

	unsigned char cnt[2];
	if (!at(fd, cnt, 2, (off_t)ifd))
		return false;
	unsigned n = big ? be16(cnt) : le16(cnt);
	if (n > 512)
		n = 512;

	long cw = 0, ch = 0;
	for (unsigned i = 0; i < n; i++) {
		unsigned char e[12];
		if (!at(fd, e, 12, (off_t)ifd + 2 + (off_t)i * 12))
			return false;
		unsigned tag  = big ? be16(e)     : le16(e);
		unsigned type = big ? be16(e + 2) : le16(e + 2);
		if (tag != 0x0100 && tag != 0x0101)
			continue;
		/* The value is inline in the last four bytes; a SHORT sits in the
		 * first two of them, which on a big-endian file is not where a LONG
		 * would be. */
		long v;
		if (type == 3)
			v = big ? (long)be16(e + 8) : (long)le16(e + 8);
		else if (type == 4)
			v = big ? (long)be32(e + 8) : (long)le32(e + 8);
		else
			continue;
		if (tag == 0x0100) cw = v;
		else               ch = v;
	}
	keep_bigger(cw, ch, w, h);
	return plausible(*w, *h);
}

/* ── ICO / CUR ───────────────────────────────────────────────────────────────
 * One byte per dimension per image, where 0 means 256 — the format cannot say
 * "256" any other way. */
static bool ico_size(int fd, long *w, long *h)
{
	unsigned char b[2];
	if (!at(fd, b, 2, 4))
		return false;
	unsigned n = le16(b);
	if (n > 256)
		n = 256;
	for (unsigned i = 0; i < n; i++) {
		unsigned char e[2];
		if (!at(fd, e, 2, 6 + (off_t)i * 16))
			break;
		keep_bigger(e[0] ? e[0] : 256, e[1] ? e[1] : 256, w, h);
	}
	return plausible(*w, *h);
}

/* ── ISO base media: MP4, MOV, AVIF, HEIF ────────────────────────────────────
 * One walker for all four. The dimensions live in a different box per family —
 * `tkhd` for a video track, `ispe` for a still image — but both are reached by
 * descending the same nested-box structure, and both are bounded by the size
 * fields of the boxes containing them.
 *
 * Depth is capped and every step must make forward progress: a box declaring
 * its own size as zero is the loop that a walker written without the check
 * spins in forever. */
static void bmff_walk(int fd, off_t start, off_t end, int depth, long *w, long *h)
{
	if (depth > 6)
		return;
	while (start + 8 <= end) {
		unsigned char hdr[8];
		if (!at(fd, hdr, 8, start))
			return;

		uint64_t size = be32(hdr);
		off_t body = start + 8;
		if (size == 1) {                    /* 64-bit size follows the type */
			unsigned char ext[8];
			if (!at(fd, ext, 8, body))
				return;
			size = (uint64_t)be32(ext) << 32 | be32(ext + 4);
			body += 8;
		} else if (size == 0) {             /* "to end of file" */
			size = (uint64_t)(end - start);
		}
		if (size > (uint64_t)(end - start) || (off_t)size < body - start)
			return;
		off_t box_end = start + (off_t)size;

		const char *t = (const char *)hdr + 4;
		if (!memcmp(t, "meta", 4))
			/* meta is a FullBox: four bytes of version and flags sit between
			 * the header and the first child. Descending without skipping
			 * them reads the child's size four bytes early. */
			bmff_walk(fd, body + 4, box_end, depth + 1, w, h);
		else if (!memcmp(t, "moov", 4) || !memcmp(t, "trak", 4) ||
		         !memcmp(t, "mdia", 4) || !memcmp(t, "iprp", 4) ||
		         !memcmp(t, "ipco", 4))
			bmff_walk(fd, body, box_end, depth + 1, w, h);
		else if (!memcmp(t, "ispe", 4)) {
			unsigned char b[12];
			if (at(fd, b, 12, body))
				keep_bigger(be32(b + 4), be32(b + 8), w, h);
		} else if (!memcmp(t, "tkhd", 4)) {
			unsigned char ver;
			if (at(fd, &ver, 1, body)) {
				/* Fixed layout after the version/flags word, differing only
				 * in whether the times and duration are 32- or 64-bit:
				 *   4 version+flags
				 *  20 or 32  created, modified, track_ID, reserved, duration
				 *   8 reserved
				 *   8 layer, alternate_group, volume, reserved
				 *  36 the display matrix
				 * and then width and height. Counting this wrong reads the
				 * height as the width and four bytes of the NEXT box as the
				 * height — which is exactly what it did, and the only reason
				 * it was caught is that the answer failed plausible() rather
				 * than coming out slightly off. */
				off_t at_wh = body + (ver == 1 ? 88 : 76);
				unsigned char b[8];
				if (at(fd, b, 8, at_wh))
					/* 16.16 fixed point: a track is measured in points, not
					 * pixels, and the fraction is always zero in practice. */
					keep_bigger(be32(b) >> 16, be32(b + 4) >> 16, w, h);
			}
		}

		if (box_end <= start)
			return;
		start = box_end;
	}
}

static bool bmff_size(int fd, long *w, long *h)
{
	struct stat st;
	if (fstat(fd, &st) != 0)
		return false;
	bmff_walk(fd, 0, st.st_size, 0, w, h);
	return plausible(*w, *h);
}

/* ── ffprobe, for the containers this file deliberately does not parse ─────── */
static bool ffprobe_size(const char *path, long *w, long *h)
{
	if (!have_cmd("ffprobe"))
		return false;

	char *const argv[] = {
		(char *)"ffprobe", (char *)"-v", (char *)"error",
		(char *)"-select_streams", (char *)"v:0",
		(char *)"-show_entries", (char *)"stream=width,height",
		(char *)"-of", (char *)"csv=p=0",
		/* Everything after this is data, and ffprobe takes no more options —
		 * a file named like a flag cannot become one. */
		(char *)"--", (char *)path, NULL
	};
	int status = -1;
	char *out = run_capture(argv, &status, true);
	bool ok = false;
	if (status == 0) {
		char *comma = strchr(out, ',');
		if (comma) {
			*comma = '\0';
			long cw = strtol(out, NULL, 10);
			long ch = strtol(comma + 1, NULL, 10);
			keep_bigger(cw, ch, w, h);
			ok = plausible(*w, *h);
		}
	}
	free(out);
	return ok;
}

bool resolution_for(const char *path, const char *mime, long *w, long *h)
{
	*w = 0;
	*h = 0;

	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0)
		return false;

	/* O_NONBLOCK above and this check are one pair: opening a fifo blocks
	 * forever and reading a character device is not a thing a properties
	 * dialog should do to somebody's tape drive. */
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		return false;
	}

	/* A SHORT read here is not a failure. The signatures are 2 to 12 bytes and
	 * a header-only file is a legitimate thing to be asked about — demanding
	 * all 16 up front made a 10-byte GIF, which carries its size in the first
	 * ten, report nothing at all. Each test below checks it has the bytes it
	 * is about to compare. */
	unsigned char m[16] = {0};
	ssize_t n = pread(fd, m, sizeof m, 0);
	bool got = false;
	if (n >= 8 && !memcmp(m, "\x89PNG\r\n\x1a\n", 8))
		got = png_size(fd, w, h);
	else if (n >= 6 && (!memcmp(m, "GIF87a", 6) || !memcmp(m, "GIF89a", 6)))
		got = gif_size(fd, w, h);
	else if (n >= 2 && m[0] == 0xFF && m[1] == 0xD8)
		got = jpeg_size(fd, w, h);
	else if (n >= 2 && m[0] == 'B' && m[1] == 'M')
		got = bmp_size(fd, w, h);
	else if (n >= 12 && !memcmp(m, "RIFF", 4) && !memcmp(m + 8, "WEBP", 4))
		got = webp_size(fd, w, h);
	else if (n >= 4 && (!memcmp(m, "II\x2a\x00", 4) || !memcmp(m, "MM\x00\x2a", 4)))
		got = tiff_size(fd, w, h);
	else if (n >= 4 && (!memcmp(m, "\x00\x00\x01\x00", 4) ||
	                    !memcmp(m, "\x00\x00\x02\x00", 4)))
		got = ico_size(fd, w, h);
	else if (n >= 8 && !memcmp(m + 4, "ftyp", 4))
		got = bmff_size(fd, w, h);
	close(fd);

	/* Only video gets the subprocess. Running ffprobe over every unrecognised
	 * file would fork on opening the properties of a text document. */
	if (!got && mime && !strncmp(mime, "video/", 6))
		got = ffprobe_size(path, w, h);

	return got;
}
