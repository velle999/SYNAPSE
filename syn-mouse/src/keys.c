/* keys.c — the names: which buttons can be bound, and what they can send.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synmouse.h"
#include "i18n.h"

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ── inputs ──────────────────────────────────────────────────────────────── */

static const struct {
	const char *name;
	const char *label;
	uint16_t    code;
} g_inputs[IN_KEY_FIRST] = {
	[IN_LEFT]        = { "left",       N_("Left button"),            BTN_LEFT },
	[IN_RIGHT]       = { "right",      N_("Right button"),           BTN_RIGHT },
	[IN_MIDDLE]      = { "middle",     N_("Middle button (wheel click)"), BTN_MIDDLE },
	[IN_BACK]        = { "back",       N_("Thumb button (back)"),    BTN_SIDE },
	[IN_FORWARD]     = { "forward",    N_("Thumb button (forward)"), BTN_EXTRA },
	[IN_BUTTON6]     = { "button6",    N_("Button 6"),               BTN_FORWARD },
	[IN_BUTTON7]     = { "button7",    N_("Button 7"),               BTN_BACK },
	[IN_BUTTON8]     = { "button8",    N_("Button 8"),               BTN_TASK },
	[IN_WHEEL_UP]    = { "wheelup",    N_("Wheel up"),               0 },
	[IN_WHEEL_DOWN]  = { "wheeldown",  N_("Wheel down"),             0 },
	[IN_WHEEL_LEFT]  = { "wheelleft",  N_("Wheel tilt left"),        0 },
	[IN_WHEEL_RIGHT] = { "wheelright", N_("Wheel tilt right"),       0 },
};

static int one_key(const char *tok, size_t len, uint16_t *out);

const char *input_name(int in)
{
	if (in >= 0 && in < IN_KEY_FIRST) return g_inputs[in].name;
	if (!input_is_key(in)) return "?";
	/* Built once per code and kept: callers hold on to the pointer. */
	static char names[256][24];
	uint16_t code = input_key(in);
	if (!names[code][0]) {
		const char *k = key_name(code);
		if (k) snprintf(names[code], sizeof names[code], "key:%s", k);
		else   snprintf(names[code], sizeof names[code], "key:code:%u", code);
	}
	return names[code];
}

const char *input_label(int in) { return in >= 0 && in < IN_KEY_FIRST ? g_inputs[in].label : NULL; }
uint16_t input_code(int in)     { return in >= 0 && in < IN_KEY_FIRST ? g_inputs[in].code : 0; }

int input_from_name(const char *s)
{
	for (int i = 0; i < IN_KEY_FIRST; i++)
		if (!strcasecmp(s, g_inputs[i].name)) return i;
	/* key:2, key:f13, key:code:30 — a key the mouse itself sends. Only the
	 * keyboard half's codes: the mouse's own buttons already have names. */
	if (!strncasecmp(s, "key:", 4)) {
		uint16_t code;
		if (one_key(s + 4, strlen(s + 4), &code) != 0) return -1;
		return input_from_key(code);
	}
	/* The spellings people already know from other tools. */
	if (!strcasecmp(s, "mouse4") || !strcasecmp(s, "side"))  return IN_BACK;
	if (!strcasecmp(s, "mouse5") || !strcasecmp(s, "extra")) return IN_FORWARD;
	if (!strcasecmp(s, "mouse1")) return IN_LEFT;
	if (!strcasecmp(s, "mouse2")) return IN_RIGHT;
	if (!strcasecmp(s, "mouse3")) return IN_MIDDLE;
	return -1;
}

int input_from_code(uint16_t btn)
{
	for (int i = 0; i < IN_FIRST_WHEEL; i++)
		if (g_inputs[i].code == btn) return i;
	return -1;
}

/* ── key names ───────────────────────────────────────────────────────────── */

/*
 * ⛔ A NAME IS A KEY'S POSITION, NOT ITS LETTER. This program sends evdev
 * codes, and the compositor turns them into characters with the desktop's
 * layout — so `q` is the key where Q sits on a US keyboard, which on AZERTY
 * types an A. Games mostly read positions too, which is why this is the right
 * unit, but the table cannot pretend to know what a code TYPES.
 *
 * The first name for a code is the one written back to the bindings file; the
 * rest are accepted spellings.
 */
const keyname_t g_keynames[] = {
	{ KEY_A, "a" }, { KEY_B, "b" }, { KEY_C, "c" }, { KEY_D, "d" },
	{ KEY_E, "e" }, { KEY_F, "f" }, { KEY_G, "g" }, { KEY_H, "h" },
	{ KEY_I, "i" }, { KEY_J, "j" }, { KEY_K, "k" }, { KEY_L, "l" },
	{ KEY_M, "m" }, { KEY_N, "n" }, { KEY_O, "o" }, { KEY_P, "p" },
	{ KEY_Q, "q" }, { KEY_R, "r" }, { KEY_S, "s" }, { KEY_T, "t" },
	{ KEY_U, "u" }, { KEY_V, "v" }, { KEY_W, "w" }, { KEY_X, "x" },
	{ KEY_Y, "y" }, { KEY_Z, "z" },
	{ KEY_1, "1" }, { KEY_2, "2" }, { KEY_3, "3" }, { KEY_4, "4" },
	{ KEY_5, "5" }, { KEY_6, "6" }, { KEY_7, "7" }, { KEY_8, "8" },
	{ KEY_9, "9" }, { KEY_0, "0" },
	{ KEY_F1, "f1" },   { KEY_F2, "f2" },   { KEY_F3, "f3" },   { KEY_F4, "f4" },
	{ KEY_F5, "f5" },   { KEY_F6, "f6" },   { KEY_F7, "f7" },   { KEY_F8, "f8" },
	{ KEY_F9, "f9" },   { KEY_F10, "f10" }, { KEY_F11, "f11" }, { KEY_F12, "f12" },
	{ KEY_F13, "f13" }, { KEY_F14, "f14" }, { KEY_F15, "f15" }, { KEY_F16, "f16" },
	{ KEY_F17, "f17" }, { KEY_F18, "f18" }, { KEY_F19, "f19" }, { KEY_F20, "f20" },
	{ KEY_F21, "f21" }, { KEY_F22, "f22" }, { KEY_F23, "f23" }, { KEY_F24, "f24" },
	{ KEY_ESC, "esc" }, { KEY_ESC, "escape" },
	{ KEY_TAB, "tab" },
	{ KEY_SPACE, "space" },
	{ KEY_ENTER, "enter" }, { KEY_ENTER, "return" },
	{ KEY_BACKSPACE, "backspace" },
	{ KEY_CAPSLOCK, "capslock" },
	{ KEY_LEFTSHIFT, "shift" },  { KEY_LEFTSHIFT, "leftshift" }, { KEY_LEFTSHIFT, "lshift" },
	{ KEY_RIGHTSHIFT, "rightshift" }, { KEY_RIGHTSHIFT, "rshift" },
	{ KEY_LEFTCTRL, "ctrl" },    { KEY_LEFTCTRL, "leftctrl" }, { KEY_LEFTCTRL, "lctrl" },
	{ KEY_LEFTCTRL, "control" },
	{ KEY_RIGHTCTRL, "rightctrl" }, { KEY_RIGHTCTRL, "rctrl" },
	{ KEY_LEFTALT, "alt" },      { KEY_LEFTALT, "leftalt" }, { KEY_LEFTALT, "lalt" },
	{ KEY_RIGHTALT, "altgr" },   { KEY_RIGHTALT, "rightalt" }, { KEY_RIGHTALT, "ralt" },
	{ KEY_LEFTMETA, "super" },   { KEY_LEFTMETA, "leftmeta" }, { KEY_LEFTMETA, "meta" },
	{ KEY_LEFTMETA, "win" },
	{ KEY_RIGHTMETA, "rightsuper" }, { KEY_RIGHTMETA, "rightmeta" },
	{ KEY_COMPOSE, "menu" },
	{ KEY_UP, "up" }, { KEY_DOWN, "down" }, { KEY_LEFT, "left" }, { KEY_RIGHT, "right" },
	{ KEY_HOME, "home" }, { KEY_END, "end" },
	{ KEY_PAGEUP, "pageup" }, { KEY_PAGEDOWN, "pagedown" },
	{ KEY_INSERT, "insert" }, { KEY_DELETE, "delete" },
	{ KEY_SYSRQ, "print" }, { KEY_SYSRQ, "printscreen" },
	{ KEY_SCROLLLOCK, "scrolllock" }, { KEY_PAUSE, "pause" },
	{ KEY_NUMLOCK, "numlock" },
	{ KEY_MINUS, "minus" },          { KEY_EQUAL, "equal" },
	{ KEY_LEFTBRACE, "leftbrace" },  { KEY_RIGHTBRACE, "rightbrace" },
	{ KEY_SEMICOLON, "semicolon" },  { KEY_APOSTROPHE, "apostrophe" },
	{ KEY_GRAVE, "grave" },          { KEY_BACKSLASH, "backslash" },
	{ KEY_COMMA, "comma" },          { KEY_DOT, "dot" }, { KEY_DOT, "period" },
	{ KEY_SLASH, "slash" },          { KEY_102ND, "102nd" },
	{ KEY_KP0, "kp0" }, { KEY_KP1, "kp1" }, { KEY_KP2, "kp2" }, { KEY_KP3, "kp3" },
	{ KEY_KP4, "kp4" }, { KEY_KP5, "kp5" }, { KEY_KP6, "kp6" }, { KEY_KP7, "kp7" },
	{ KEY_KP8, "kp8" }, { KEY_KP9, "kp9" },
	{ KEY_KPPLUS, "kpplus" },         { KEY_KPMINUS, "kpminus" },
	{ KEY_KPASTERISK, "kpasterisk" }, { KEY_KPSLASH, "kpslash" },
	{ KEY_KPDOT, "kpdot" },           { KEY_KPENTER, "kpenter" },
	{ KEY_MUTE, "mute" }, { KEY_VOLUMEDOWN, "volumedown" }, { KEY_VOLUMEUP, "volumeup" },
	{ KEY_PLAYPAUSE, "playpause" }, { KEY_NEXTSONG, "nextsong" },
	{ KEY_PREVIOUSSONG, "previoussong" }, { KEY_STOPCD, "stop" },
	/* The mouse's own buttons, as things to SEND. */
	{ BTN_LEFT, "mouse1" }, { BTN_RIGHT, "mouse2" }, { BTN_MIDDLE, "mouse3" },
	{ BTN_SIDE, "mouse4" }, { BTN_EXTRA, "mouse5" },
};
const size_t g_nkeynames = sizeof g_keynames / sizeof g_keynames[0];

const char *key_name(uint16_t code)
{
	for (size_t i = 0; i < g_nkeynames; i++)
		if (g_keynames[i].code == code) return g_keynames[i].name;
	return NULL;
}

/* ⚠ code:N IS LIMITED TO WHAT THE OUTPUT DEVICES CAN SEND: the keyboard half
 * advertises every code from 1 to 255, and the mouse half the five buttons
 * every mouse twin is given. Accepting a code outside both would be a binding
 * the kernel silently drops at the first press. */
static bool code_sendable(long v)
{
	return (v >= 1 && v <= 255) || (v >= BTN_LEFT && v <= BTN_EXTRA);
}

static int one_key(const char *tok, size_t len, uint16_t *out)
{
	char buf[32];
	if (len == 0 || len >= sizeof buf) return -1;
	memcpy(buf, tok, len);
	buf[len] = '\0';

	if (!strncasecmp(buf, "code:", 5)) {
		char *end = NULL;
		long v = strtol(buf + 5, &end, 10);
		if (!end || *end || end == buf + 5 || !code_sendable(v)) return -1;
		*out = (uint16_t)v;
		return 0;
	}
	for (size_t i = 0; i < g_nkeynames; i++)
		if (!strcasecmp(buf, g_keynames[i].name)) { *out = g_keynames[i].code; return 0; }
	return -1;
}

int combo_parse(const char *s, combo_t *c, const char **bad, size_t *badlen)
{
	memset(c, 0, sizeof *c);
	const char *p = s;
	if (bad) *bad = s;
	if (badlen) *badlen = strlen(s);
	if (!*p) return -1;

	for (;;) {
		const char *q = strchr(p, '+');
		size_t len = q ? (size_t)(q - p) : strlen(p);
		uint16_t code;
		if (c->n >= COMBO_MAX || one_key(p, len, &code) != 0) {
			if (bad) *bad = p;
			if (badlen) *badlen = len;
			return -1;
		}
		/* A key named twice is pressed once. */
		bool dup = false;
		for (int i = 0; i < c->n; i++) dup |= c->code[i] == code;
		if (!dup) c->code[c->n++] = code;
		if (!q) break;
		p = q + 1;
	}
	return 0;
}

void combo_format(const combo_t *c, char *buf, size_t n)
{
	size_t off = 0;
	buf[0] = '\0';
	for (int i = 0; i < c->n && off < n; i++) {
		const char *nm = key_name(c->code[i]);
		char tmp[16];
		if (!nm) { snprintf(tmp, sizeof tmp, "code:%u", c->code[i]); nm = tmp; }
		int w = snprintf(buf + off, n - off, "%s%s", i ? "+" : "", nm);
		if (w < 0) break;
		off += (size_t)w;
	}
}

bool combo_has_button(const combo_t *c)
{
	for (int i = 0; i < c->n; i++)
		if (code_is_button(c->code[i])) return true;
	return false;
}

/* ── modes ───────────────────────────────────────────────────────────────── */

static const char *const g_acts[ACT_COUNT] = {
	[ACT_NONE]   = "default",
	[ACT_KEY]    = "key",
	[ACT_REPEAT] = "repeat",
	[ACT_TOGGLE] = "toggle",
	[ACT_LATCH]  = "latch",
	[ACT_OFF]    = "off",
};

const char *act_name(act_kind_t k) { return k < ACT_COUNT ? g_acts[k] : "?"; }

act_kind_t act_from_name(const char *s)
{
	for (int i = 1; i < ACT_COUNT; i++)
		if (!strcasecmp(s, g_acts[i])) return (act_kind_t)i;
	return ACT_NONE;
}
