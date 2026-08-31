/* json_test.c — the parser, compiled and fed the bytes that break it.
 *
 * ⛔ A JSON READER CANNOT BE CHECKED BY READING IT. The bug this exists to
 * prevent is a key matching somebody else's VALUE: mpv answers `get_property
 * path` with a FILENAME, and a filename may contain `"data":` — at which point
 * a reader that searches the blob returns the wrong span, or, having found a
 * comma where a colon belonged, reports the field ABSENT while it is right
 * there. That exact shape cost synui's greeter five rounds and days of
 * apparently-broken hardware. Every case below is a real byte sequence.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "synplay.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ck(const char *what, bool ok)
{
	printf("%s  %s\n", ok ? "  ok  " : "  FAIL", what);
	if (!ok) fails++;
}

static void cks(const char *what, const char *want, const char *got)
{
	bool ok = !strcmp(want, got);
	printf("%s  %s\n", ok ? "  ok  " : "  FAIL", what);
	if (!ok) { printf("        want [%s]\n        got  [%s]\n", want, got); fails++; }
}

int main(void)
{
	char b[1024];
	double d;
	bool v;

	puts("json — what mpv actually sends");

	/* ⛔ the value that reads like a key */
	const char *decoy =
	 "{\"data\":\"S02E04 - \\\"data\\\": the one about JSON.mkv\","
	 "\"request_id\":7,\"error\":\"success\"}";
	sp_json_str(decoy, "data", b, sizeof b);
	cks("a filename containing \\\"data\\\": is read whole",
	    "S02E04 - \"data\": the one about JSON.mkv", b);
	sp_json_str(decoy, "error", b, sizeof b);
	cks("...and the field after it is still found", "success", b);
	ck("...and its request_id parses",
	   sp_json_num(decoy, "request_id", &d) && d == 7);

	/* a nested object carrying the same key */
	const char *nested =
	 "{\"event\":\"property-change\",\"inner\":{\"name\":\"decoy\"},"
	 "\"name\":\"time-pos\",\"data\":12.5}";
	sp_json_str(nested, "name", b, sizeof b);
	cks("a key inside a NESTED object does not match", "time-pos", b);
	ck("a number after a nested object parses",
	   sp_json_num(nested, "data", &d) && d == 12.5);

	/* ⛔ null is not zero */
	const char *nul = "{\"data\":null,\"error\":\"success\"}";
	ck("null is ABSENT, not 0 — a seek bar must not draw at the start "
	   "of a file nothing has opened",
	   !sp_json_num(nul, "data", &d));

	/* the playlist, which is where the array walking is used for real */
	const char *pl =
	 "{\"data\":[{\"filename\":\"/m/one.mkv\",\"current\":true,\"playing\":true},"
	 "{\"filename\":\"/m/two, [1080p].mkv\",\"title\":\"Two\"},"
	 "{\"filename\":\"/m/three.mkv\"}],\"error\":\"success\"}";
	size_t len = 0;
	const char *arr = sp_json_raw(pl, "data", &len);
	ck("the playlist array is found", arr != NULL);

	char item[1024];
	const char *e = sp_json_elem(arr, 0, &len);
	snprintf(item, sizeof item, "%.*s", (int)len, e);
	sp_json_str(item, "filename", b, sizeof b);
	cks("array element 0", "/m/one.mkv", b);
	ck("...and its `current` flag", sp_json_bool(item, "current", &v) && v);

	e = sp_json_elem(arr, 1, &len);
	snprintf(item, sizeof item, "%.*s", (int)len, e);
	sp_json_str(item, "filename", b, sizeof b);
	cks("array element 1, whose filename contains a comma and brackets",
	    "/m/two, [1080p].mkv", b);
	ck("an element with no `current` says so rather than guessing",
	   !sp_json_bool(item, "current", &v));

	ck("element 3 is absent", sp_json_elem(arr, 3, &len) == NULL);

	/* ⛔ THE ITERATOR IS WHAT sp_queue WALKS NOW, and it has to agree with
	 * sp_json_elem exactly — a playlist read one way and counted the other
	 * is the kind of disagreement that shows up as a row going missing. */
	const char *it;
	size_t ilen = 0;
	int walked = 0;
	bool agrees = true;
	sp_json_iter(arr, &it);
	for (const char *ie; (ie = sp_json_next(&it, &ilen)); walked++) {
		size_t elen = 0;
		const char *want = sp_json_elem(arr, walked, &elen);
		if (!want || elen != ilen || memcmp(want, ie, ilen)) agrees = false;
	}
	ck("the iterator walks every element", walked == 3);
	ck("...and gives byte for byte what sp_json_elem gives", agrees);
	ck("...and NULL past the end", sp_json_next(&it, &ilen) == NULL);

	/* ⚠ An empty array ends immediately rather than returning its own `]`. */
	sp_json_iter("[]", &it);
	ck("an empty array yields nothing", sp_json_next(&it, &ilen) == NULL);

	/* ⚠ Not an array at all: no cursor, no walk, no crash. */
	sp_json_iter("{\"a\":1}", &it);
	ck("an object is not walked as an array", sp_json_next(&it, &ilen) == NULL);

	/* escapes, both ways */
	const char *uni = "{\"data\":\"caf\\u00e9 \\u2014 live\\nset\"}";
	sp_json_str(uni, "data", b, sizeof b);
	cks("\\u and \\n escapes are undone", "café — live\nset", b);

	char q[1024], obj[1200], back[1024];
	const char *nasty = "Pulp Fiction (1994) [\"Director's Cut\"]\ttake\\2.mkv";
	sp_json_quote(nasty, q, sizeof q);
	snprintf(obj, sizeof obj, "{\"f\":%s}", q);
	sp_json_str(obj, "f", back, sizeof back);
	cks("⛔ a filename with a quote, a tab and a backslash survives the "
	    "round trip to mpv", nasty, back);

	ck("a field that is genuinely absent reports absent",
	   !sp_json_str(decoy, "nope", b, sizeof b));

	/* ── percent-encoding, which every path on the wire goes through ──── */
	char enc[1024], dec[1024];
	const char *tabbed = "/m/a\tb\nc%d é.mkv";
	sp_enc(tabbed, enc, sizeof enc);
	ck("an encoded path contains no tab or newline",
	   !strchr(enc, '\t') && !strchr(enc, '\n'));
	sp_dec(enc, dec, sizeof dec);
	cks("...and decodes back to itself", tabbed, dec);

	/* ── the title a person reads ─────────────────────────────────────── */
	sp_pretty_title("/m/Black Hawk Down (2001).mkv", b, sizeof b);
	cks("a title is the basename without its extension",
	    "Black Hawk Down (2001)", b);
	sp_pretty_title("/m/S01E02. The One Where They Argue.mkv", b, sizeof b);
	cks("⚠ only a SHORT trailing extension is cut, so a name with dots "
	    "in it survives", "S01E02. The One Where They Argue", b);
	sp_pretty_title("https://example.com/live.m3u8", b, sizeof b);
	cks("a URL is left as it is", "https://example.com/live.m3u8", b);

	/* ── a playlist name becomes a filename, so it is checked ─────────── */
	ck("⛔ a playlist called ../../.bashrc is refused",
	   !sp_playlist_path("../../.bashrc", b, sizeof b));
	ck("...and one with a slash in it", !sp_playlist_path("a/b", b, sizeof b));
	ck("...and a leading dot, which would hide it from its own listing",
	   !sp_playlist_path(".hidden", b, sizeof b));
	ck("an ordinary name is fine", sp_playlist_path("Road trip 2", b, sizeof b));

	ck("mkv is media", sp_is_media("a.mkv"));
	ck("FLAC in capitals is media", sp_is_media("a.FLAC"));
	ck("a text file is not", !sp_is_media("notes.txt"));
	ck("a file with no extension is not", !sp_is_media("README"));

	printf(fails ? "\n%d check(s) failed\n" : "\nall checks passed\n", fails);
	return fails ? 1 : 0;
}
