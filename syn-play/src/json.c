/* json.c — enough JSON to read what mpv says, and no guessing.
 *
 * ⛔ WHY THIS IS A SCANNER AND NOT strstr("\"key\"").
 *
 * mpv answers `get_property path` with a FILENAME, and a filename is bytes
 * somebody else chose. `{"data":"S02E04 - \"data\": the one about JSON.mkv"}`
 * is a legal reply, and so is a title containing `"event":`. A reader that
 * searches the blob for a key finds it inside that value and returns the wrong
 * thing; worse, having landed on a hit with a `,` where a `:` belonged, the
 * obvious implementation concludes the field is ABSENT when it is right there.
 *
 * That is not hypothetical. The same shape cost synui's greeter five rounds and
 * days of apparently-broken fingerprint hardware, because greetd's messages
 * carry `"type":"auth_message"` — a VALUE spelled exactly like the key being
 * looked for. So this walks the object: strings are skipped as strings with
 * their escapes honoured, nested objects and arrays are skipped whole, and only
 * a key at depth 1 can match.
 *
 * It is not a general JSON library and does not want to be. It reads one object
 * or one array at a time, which is what a line of mpv IPC is.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synplay.h"

#include <stdlib.h>
#include <string.h>

static const char *skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
	return p;
}

/* p points at the opening quote. Returns just past the closing one, or NULL on
 * a string that never ends — which is a truncated read, not a parse error to
 * paper over. */
static const char *skip_string(const char *p)
{
	if (*p != '"') return NULL;
	for (p++; *p; p++) {
		if (*p == '\\') {
			if (!p[1]) return NULL;
			p++;                     /* the escaped byte, whatever it is */
			continue;
		}
		if (*p == '"') return p + 1;
	}
	return NULL;
}

/* Returns just past one complete value of any type. */
static const char *skip_value(const char *p)
{
	p = skip_ws(p);
	if (*p == '"') return skip_string(p);
	if (*p == '{' || *p == '[') {
		char open = *p, close = (open == '{') ? '}' : ']';
		int depth = 0;
		for (; *p; p++) {
			if (*p == '"') {
				const char *q = skip_string(p);
				if (!q) return NULL;
				p = q - 1;           /* the loop's p++ lands past it */
				continue;
			}
			if (*p == open) depth++;
			else if (*p == close && --depth == 0) return p + 1;
		}
		return NULL;
	}
	/* A number, true, false or null: runs until a structural byte. */
	while (*p && !strchr(",}] \t\n\r", *p)) p++;
	return p;
}

/* The heart of it. Finds `key` at depth 1 of the object at `json` and returns a
 * pointer to its VALUE. */
static const char *field(const char *json, const char *key)
{
	const char *p = skip_ws(json);
	if (*p != '{') return NULL;
	p++;

	size_t klen = strlen(key);
	for (;;) {
		p = skip_ws(p);
		if (*p == '}' || !*p) return NULL;
		if (*p == ',') { p++; continue; }
		if (*p != '"') return NULL;      /* not an object we understand */

		const char *kstart = p + 1;
		const char *kend = skip_string(p);
		if (!kend) return NULL;
		size_t have = (size_t)(kend - 1 - kstart);

		p = skip_ws(kend);
		if (*p != ':') return NULL;
		p++;

		/* ⚠ THE MATCH IS ON A KEY WE PARSED AS A KEY. Reaching here at all
		 * means this string was followed by a colon at depth 1 — which is
		 * the whole point of walking rather than searching. */
		if (have == klen && !memcmp(kstart, key, klen))
			return skip_ws(p);

		p = skip_value(p);
		if (!p) return NULL;
	}
}

/* Copy a JSON string value, undoing its escapes. ⚠ A value carrying `\"` ends
 * early for anything that does not do this, and truncates mid-word. */
static bool unescape(const char *v, char *out, size_t cap)
{
	if (*v != '"' || cap == 0) return false;
	size_t o = 0;
	for (const char *p = v + 1; *p; p++) {
		if (*p == '"') { out[o] = '\0'; return true; }
		if (*p == '\\') {
			p++;
			if (!*p) break;
			char c;
			switch (*p) {
			case 'n': c = '\n'; break;
			case 't': c = '\t'; break;
			case 'r': c = '\r'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'u': {
				/* Only the BMP, encoded as UTF-8. A surrogate pair is
				 * written as its two halves rather than dropped: mpv
				 * sends UTF-8 in practice and this path is for
				 * completeness, not for correctness of astral titles. */
				if (!p[1] || !p[2] || !p[3] || !p[4]) { out[o] = '\0'; return true; }
				char hex[5] = { p[1], p[2], p[3], p[4], 0 };
				unsigned cp = (unsigned)strtoul(hex, NULL, 16);
				p += 4;
				if (cp < 0x80) {
					if (o + 1 >= cap) break;
					out[o++] = (char)cp;
				} else if (cp < 0x800) {
					if (o + 2 >= cap) break;
					out[o++] = (char)(0xC0 | (cp >> 6));
					out[o++] = (char)(0x80 | (cp & 0x3F));
				} else {
					if (o + 3 >= cap) break;
					out[o++] = (char)(0xE0 | (cp >> 12));
					out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
					out[o++] = (char)(0x80 | (cp & 0x3F));
				}
				continue;
			}
			default: c = *p; break;      /* \\ \/ \" and anything else */
			}
			if (o + 1 >= cap) break;
			out[o++] = c;
			continue;
		}
		if (o + 1 >= cap) break;
		out[o++] = *p;
	}
	out[o] = '\0';
	return true;
}

bool sp_json_str(const char *json, const char *key, char *out, size_t cap)
{
	const char *v = field(json, key);
	if (!v || *v != '"') return false;
	return unescape(v, out, cap);
}

bool sp_json_num(const char *json, const char *key, double *out)
{
	const char *v = field(json, key);
	if (!v) return false;
	/* ⚠ `null` is mpv's answer for a property it has no value for yet —
	 * time-pos before a file is loaded, duration on a stream. That is not a
	 * zero, and returning one would draw a seek bar at the start of a file
	 * nothing has opened. */
	if (*v != '-' && *v != '+' && *v != '.' && (*v < '0' || *v > '9')) return false;
	char *end = NULL;
	double d = strtod(v, &end);
	if (end == v) return false;
	*out = d;
	return true;
}

bool sp_json_bool(const char *json, const char *key, bool *out)
{
	const char *v = field(json, key);
	if (!v) return false;
	if (!strncmp(v, "true", 4))  { *out = true;  return true; }
	if (!strncmp(v, "false", 5)) { *out = false; return true; }
	return false;
}

const char *sp_json_raw(const char *json, const char *key, size_t *len)
{
	const char *v = field(json, key);
	if (!v) return NULL;
	const char *end = skip_value(v);
	if (!end) return NULL;
	if (len) *len = (size_t)(end - v);
	return v;
}

const char *sp_json_elem(const char *array, int idx, size_t *len)
{
	const char *p = skip_ws(array);
	if (*p != '[') return NULL;
	p++;
	for (int i = 0;; i++) {
		p = skip_ws(p);
		if (*p == ']' || !*p) return NULL;
		if (*p == ',') { p++; i--; continue; }
		const char *end = skip_value(p);
		if (!end) return NULL;
		if (i == idx) {
			if (len) *len = (size_t)(end - p);
			return p;
		}
		p = end;
	}
}

/* ⚠ THE OTHER DIRECTION, and the one people forget. A filename is bytes
 * somebody else chose: `Pulp Fiction (1994) [\"Director\'s Cut\"].mkv` is a
 * legal name, and handed to mpv unescaped it ends the JSON string three
 * characters in. Control bytes are escaped too — a newline in a filename would
 * end the whole IPC line. */
void sp_json_quote(const char *in, char *out, size_t cap)
{
	size_t o = 0;
	if (cap < 3) { if (cap) out[0] = '\0'; return; }
	out[o++] = '"';
	for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
		char esc[8];
		int n;
		switch (*p) {
		case '"':  memcpy(esc, "\\\"", 2); n = 2; break;
		case '\\': memcpy(esc, "\\\\", 2); n = 2; break;
		case '\n': memcpy(esc, "\\n", 2);  n = 2; break;
		case '\r': memcpy(esc, "\\r", 2);  n = 2; break;
		case '\t': memcpy(esc, "\\t", 2);  n = 2; break;
		default:
			if (*p < 0x20) { n = snprintf(esc, sizeof esc, "\\u%04x", *p); }
			else           { esc[0] = (char)*p; n = 1; }
			break;
		}
		if (o + (size_t)n + 2 > cap) break;
		memcpy(out + o, esc, (size_t)n);
		o += (size_t)n;
	}
	out[o++] = '"';
	out[o] = '\0';
}
