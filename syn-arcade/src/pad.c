/*
 * pad.c — game controllers: what is plugged in, what it is called, whether its
 * sticks are centred, and what its deadzones should be.
 *
 * ── Two sources, on purpose ─────────────────────────────────────────────────
 *
 * LISTING reads /sys/class/input, which is the kernel's own account of what is
 * attached: every field this needs — the name, the USB ids, the bus, and the
 * capability bitmasks that say whether a thing is a gamepad at all — is a file
 * there. That makes the whole discovery path drivable against a described
 * machine in a temp directory (SYN_ARCADE_SYSFS), which is the only way to
 * test a controller nobody has plugged in.
 *
 * TOUCHING a pad — reading its live events, rumbling it, changing a deadzone —
 * goes through ioctls on /dev/input/eventN, because there is no sysfs for any
 * of it. Those paths are never exercised by the suite.
 *
 * ── Why a gamepad is writable and a keyboard is not ─────────────────────────
 *
 * Rumble (EVIOCSFF) and deadzones (EVIOCSABS) need WRITE access to the event
 * node, which is root:input 0660. The user is not in `input` on a stock
 * SynapseOS install — syn-install puts only the `greeter` account there — so
 * that looks like it needs root, and it does not:
 *
 *     /usr/lib/udev/rules.d/70-uaccess.rules:61
 *     SUBSYSTEM=="input", ENV{ID_INPUT_JOYSTICK}=="?*", TAG+="uaccess"
 *
 * systemd tags JOYSTICKS for uaccess and grants the logged-in seat user an ACL
 * on them. Keyboards and mice get no such rule, which is the point — a game
 * controller is not a keylogger. So everything here works unprivileged for the
 * person at the machine, and needs no polkit action and no setuid helper.
 *
 * That also means "permission denied" has a specific meaning worth saying out
 * loud: not "you need root" but "this device was not recognised as a joystick
 * by udev, or you are not the active seat" — which is a real and different
 * problem. pad_open_rw() says so.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "arcade.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

/* ── where to look ───────────────────────────────────────────────────────── */

static const char *sysfs_root(void)
{
	const char *e = getenv("SYN_ARCADE_SYSFS");
	return (e && *e) ? e : "/sys/class/input";
}

static const char *dev_root(void)
{
	const char *e = getenv("SYN_ARCADE_DEV");
	return (e && *e) ? e : "/dev/input";
}

/* ── capability bitmasks ─────────────────────────────────────────────────── */

/*
 * sysfs prints these as space-separated 64-bit hex words, MOST SIGNIFICANT
 * FIRST — the LAST word holds bits 0..63. Reading them left to right, which is
 * the obvious way, puts every bit in the wrong place and quietly decides that
 * no device is a gamepad.
 */
#define MASK_MAX_WORDS 24

typedef struct {
	unsigned long long w[MASK_MAX_WORDS];
	int n;
} mask_t;

static void mask_load(mask_t *m, const char *path)
{
	m->n = 0;
	char *text = read_file(path);
	if (!text) return;

	char *save = NULL;
	for (char *tok = strtok_r(text, " \t\n", &save);
	     tok && m->n < MASK_MAX_WORDS;
	     tok = strtok_r(NULL, " \t\n", &save))
		m->w[m->n++] = strtoull(tok, NULL, 16);

	free(text);
}

static bool mask_test(const mask_t *m, int bit)
{
	if (bit < 0) return false;
	int from_right = bit / 64;
	int idx = m->n - 1 - from_right;
	if (idx < 0 || idx >= m->n) return false;
	return (m->w[idx] >> (bit % 64)) & 1ULL;
}

static int mask_count(const mask_t *m, int lo, int hi)
{
	int n = 0;
	for (int b = lo; b <= hi; b++)
		if (mask_test(m, b))
			n++;
	return n;
}

/* ── one pad ─────────────────────────────────────────────────────────────── */

typedef struct {
	char id[32];		/* "event20" — the event node's basename */
	char node[512];		/* "/dev/input/event20" */
	char name[256];
	char vendor[16], product[16], version[16], bus[16];
	bool is_pad;
	bool has_ff;		/* the device supports force feedback at all */
	bool has_rumble;	/* ...and specifically FF_RUMBLE */
	int  buttons;
	int  axes;
} pad_t;

/* A one-line file with its trailing newline removed; "" if absent. */
static void sysfs_str(char *out, size_t n, const char *dir, const char *rel)
{
	char path[1024];
	out[0] = '\0';
	if (snprintf(path, sizeof(path), "%s/%s", dir, rel) >= (int)sizeof(path))
		return;

	char *text = read_file(path);
	if (!text) return;
	strip_trailing_newline(text);
	snprintf(out, n, "%s", trim(text));
	free(text);
}

/*
 * Is this thing a game controller?
 *
 * The same two questions udev's input_id builtin asks before it sets
 * ID_INPUT_JOYSTICK: does it carry a gamepad or joystick BUTTON, and does it
 * have the two axes of a stick. Either half alone is not enough — a graphics
 * tablet has ABS_X/ABS_Y and no gamepad buttons, and some keyboards claim a
 * stray BTN_ in their key mask. Agreeing with udev matters beyond tidiness:
 * ID_INPUT_JOYSTICK is exactly what decides whether the uaccess ACL that makes
 * this device writable exists, so a device this called a pad but udev did not
 * would be one every write to fails on.
 */
static bool looks_like_pad(const mask_t *key, const mask_t *abs)
{
	bool button = mask_test(key, BTN_GAMEPAD) ||	/* BTN_SOUTH, 0x130 */
		      mask_test(key, BTN_JOYSTICK) ||	/* 0x120 */
		      mask_test(key, BTN_TRIGGER_HAPPY);
	bool stick = mask_test(abs, ABS_X) && mask_test(abs, ABS_Y);
	return button && stick;
}

static bool pad_read(pad_t *p, const char *id)
{
	char dir[1024];
	memset(p, 0, sizeof(*p));
	snprintf(p->id, sizeof(p->id), "%s", id);
	snprintf(p->node, sizeof(p->node), "%s/%s", dev_root(), id);

	if (snprintf(dir, sizeof(dir), "%s/%s/device", sysfs_root(), id)
	    >= (int)sizeof(dir))
		return false;

	sysfs_str(p->name, sizeof(p->name), dir, "name");
	sysfs_str(p->vendor, sizeof(p->vendor), dir, "id/vendor");
	sysfs_str(p->product, sizeof(p->product), dir, "id/product");
	sysfs_str(p->version, sizeof(p->version), dir, "id/version");
	sysfs_str(p->bus, sizeof(p->bus), dir, "id/bustype");

	/* Comfortably longer than `dir` plus the longest suffix below — gcc
	 * checks that arithmetic and warns if the result could be truncated. */
	char caps[sizeof(dir) + 32];
	mask_t key = {0}, abs = {0}, ff = {0};

	snprintf(caps, sizeof(caps), "%s/capabilities/key", dir);
	mask_load(&key, caps);
	snprintf(caps, sizeof(caps), "%s/capabilities/abs", dir);
	mask_load(&abs, caps);
	snprintf(caps, sizeof(caps), "%s/capabilities/ff", dir);
	mask_load(&ff, caps);

	p->is_pad = looks_like_pad(&key, &abs);
	p->buttons = mask_count(&key, BTN_MISC, KEY_MAX);
	p->axes = mask_count(&abs, 0, ABS_MAX);
	p->has_ff = mask_count(&ff, 0, FF_MAX) > 0;
	p->has_rumble = mask_test(&ff, FF_RUMBLE);

	if (!p->name[0])
		snprintf(p->name, sizeof(p->name), "unknown device");
	return true;
}

/* Numeric part of "eventN", or -1. Used only to order the list the way the
 * kernel numbered the devices rather than the way readdir happened to hand
 * them over — "event10" sorts before "event2" as a string. */
static int event_index(const char *id)
{
	if (strncmp(id, "event", 5) != 0) return -1;
	const char *d = id + 5;
	if (!*d) return -1;
	for (const char *p = d; *p; p++)
		if (*p < '0' || *p > '9')
			return -1;
	return (int)strtol(d, NULL, 10);
}

/* Every gamepad attached, kernel order. Caller frees. */
static pad_t *pads_scan(int *count)
{
	*count = 0;
	DIR *d = opendir(sysfs_root());
	if (!d) return NULL;

	pad_t *v = NULL;
	int n = 0, cap = 0;

	struct dirent *e;
	while ((e = readdir(d))) {
		if (event_index(e->d_name) < 0)
			continue;

		pad_t p;
		if (!pad_read(&p, e->d_name) || !p.is_pad)
			continue;

		if (n == cap) {
			cap = cap ? cap * 2 : 8;
			v = xrealloc(v, (size_t)cap * sizeof(*v));
		}
		v[n++] = p;
	}
	closedir(d);

	for (int i = 1; i < n; i++) {		/* insertion sort; n is tiny */
		pad_t k = v[i];
		int j = i - 1;
		while (j >= 0 && event_index(v[j].id) > event_index(k.id)) {
			v[j + 1] = v[j];
			j--;
		}
		v[j + 1] = k;
	}

	*count = n;
	return v;
}

/*
 * Resolve what the user typed to one pad.
 *
 * Accepts the event id ("event20"), a bare index into the list as printed
 * ("1"), or any unique case-insensitive fragment of the name ("dualsense").
 * An ambiguous fragment is an error that lists the candidates rather than
 * picking the first — rumbling the wrong controller is confusing, and setting
 * a deadzone on the wrong one is worse.
 */
static bool pad_find(const char *want, pad_t *out)
{
	int n;
	pad_t *v = pads_scan(&n);
	if (!v || n == 0) {
		free(v);
		fputs("syn-arcade: no game controllers found\n", stderr);
		return false;
	}

	int hit = -1, hits = 0;

	for (int i = 0; i < n; i++)
		if (strcmp(v[i].id, want) == 0) { hit = i; hits = 1; goto done; }

	if (event_index(want) < 0) {
		char *end = NULL;
		long idx = strtol(want, &end, 10);
		if (end && !*end && idx >= 1 && idx <= n) {
			hit = (int)(idx - 1);
			hits = 1;
			goto done;
		}
	}

	for (int i = 0; i < n; i++) {
		if (!strcasestr(v[i].name, want)) continue;
		hits++;
		if (hit < 0) hit = i;
	}

done:
	if (hits == 1) {
		*out = v[hit];
		free(v);
		return true;
	}

	if (hits == 0)
		fprintf(stderr, "syn-arcade: no controller matches '%s'\n", want);
	else {
		fprintf(stderr, "syn-arcade: '%s' matches %d controllers:\n",
			want, hits);
		for (int i = 0; i < n; i++)
			if (strcasestr(v[i].name, want))
				fprintf(stderr, "  %-10s %s\n", v[i].id, v[i].name);
	}
	free(v);
	return false;
}

/* ── opening the device ──────────────────────────────────────────────────── */

/*
 * Open for writing, and explain a refusal accurately.
 *
 * EACCES here does NOT mean "run it as root" — see the header comment. It
 * means udev did not tag this device ID_INPUT_JOYSTICK, or this session is not
 * the active seat, and telling somebody to sudo would be telling them to paper
 * over the actual fault (and to run a thing that pokes an input device as
 * root, which is worth not teaching).
 */
static int pad_open_rw(const pad_t *p)
{
	int fd = open(p->node, O_RDWR | O_CLOEXEC);
	if (fd >= 0)
		return fd;

	if (errno == EACCES) {
		fprintf(stderr,
		 "syn-arcade: no write access to %s\n"
		 "\n"
		 "Game controllers are normally writable by whoever is logged in at\n"
		 "the machine: udev tags joysticks for uaccess and systemd grants an\n"
		 "ACL. Two things stop that, and neither is fixed by sudo —\n"
		 "\n"
		 "  · udev did not recognise this as a joystick, so no ACL was added\n"
		 "    (check: udevadm info %s | grep ID_INPUT)\n"
		 "  · this session is not the active seat — an SSH login, or another\n"
		 "    user switched to\n", p->node, p->node);
	} else {
		fprintf(stderr, "syn-arcade: cannot open %s: %s\n",
			p->node, strerror(errno));
	}
	return -1;
}

static int pad_open_ro(const pad_t *p)
{
	int fd = open(p->node, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		fprintf(stderr, "syn-arcade: cannot open %s: %s\n",
			p->node, strerror(errno));
	return fd;
}

/* ── list and info ───────────────────────────────────────────────────────── */

static const char *bus_name(const char *hex)
{
	long b = strtol(hex, NULL, 16);
	switch (b) {
	case BUS_USB:		return "USB";
	case BUS_BLUETOOTH:	return "Bluetooth";
	case BUS_VIRTUAL:	return "virtual";
	case BUS_I8042:		return "PS/2";
	case BUS_HIL:		return "HIL";
	default:		return "other";
	}
}

/*
 * The controllers this suite can name on sight.
 *
 * Deliberately short. The point is not to be a device database — SDL already
 * ships one with thousands of entries and this would only ever be a worse copy
 * of it — but to turn the handful of pads whose kernel `name` is unhelpful
 * into something a person recognises. Anything not here shows its own name,
 * which for the vast majority of devices is already correct.
 */
static const char *friendly_kind(const char *vendor, const char *product)
{
	struct { const char *v, *p, *kind; } known[] = {
		{ "045e", NULL,   "Xbox controller" },
		{ "054c", "05c4", "DualShock 4" },
		{ "054c", "09cc", "DualShock 4 (v2)" },
		{ "054c", "0ce6", "DualSense" },
		{ "054c", "0df2", "DualSense Edge" },
		{ "057e", "2009", "Switch Pro Controller" },
		{ "28de", NULL,   "Steam controller" },
		{ "0079", NULL,   "generic USB gamepad" },
		{ "2dc8", NULL,   "8BitDo controller" },
		{ "0f0d", NULL,   "HORI controller" },
		{ "1532", NULL,   "Razer controller" },
	};

	for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
		if (strcasecmp(vendor, known[i].v) != 0)
			continue;
		if (known[i].p && strcasecmp(product, known[i].p) != 0)
			continue;
		return known[i].kind;
	}
	return NULL;
}

static int pads_list(bool rec)
{
	int n;
	pad_t *v = pads_scan(&n);

	if (n == 0) {
		free(v);
		if (rec)
			rec_row(6, "id", "name", "kind", "bus", "rumble", "node");
		else
			puts("no game controllers attached");
		/* An answer, not a failure. */
		return EX_EMPTY;
	}

	if (rec) {
		rec_row(6, "id", "name", "kind", "bus", "rumble", "node");
		for (int i = 0; i < n; i++) {
			const char *kind = friendly_kind(v[i].vendor, v[i].product);
			rec_row(6, v[i].id, v[i].name, kind ? kind : "controller",
				bus_name(v[i].bus),
				v[i].has_rumble ? "yes" : "no", v[i].node);
		}
	} else {
		for (int i = 0; i < n; i++) {
			const char *kind = friendly_kind(v[i].vendor, v[i].product);
			printf("%d. %-10s %s\n", i + 1, v[i].id, v[i].name);
			printf("      %s · %s · %d buttons · %d axes · %s\n",
			       kind ? kind : "controller", bus_name(v[i].bus),
			       v[i].buttons, v[i].axes,
			       v[i].has_rumble ? "rumble" : "no rumble");
		}
	}

	free(v);
	return EX_OK;
}

/* The axes worth naming, in the order a person thinks about them. */
static const struct { int code; const char *name; } axis_names[] = {
	{ ABS_X,	"left stick X" },
	{ ABS_Y,	"left stick Y" },
	{ ABS_RX,	"right stick X" },
	{ ABS_RY,	"right stick Y" },
	{ ABS_Z,	"left trigger" },
	{ ABS_RZ,	"right trigger" },
	{ ABS_HAT0X,	"d-pad X" },
	{ ABS_HAT0Y,	"d-pad Y" },
};
#define AXIS_COUNT ((int)(sizeof(axis_names) / sizeof(axis_names[0])))

static int pads_info(const char *want, bool rec)
{
	pad_t p;
	if (!pad_find(want, &p))
		return EX_FAIL;

	int fd = pad_open_ro(&p);

	if (rec) {
		rec_row(3, "field", "value", "action");
		rec_row(3, "name", p.name, "detail");
		rec_row(3, "id", p.id, "detail");
		rec_row(3, "node", p.node, "detail");
		rec_row(3, "bus", bus_name(p.bus), "detail");
		char ids[64];
		snprintf(ids, sizeof(ids), "%s:%s", p.vendor, p.product);
		rec_row(3, "usb id", ids, "detail");
		char num[32];
		snprintf(num, sizeof(num), "%d", p.buttons);
		rec_row(3, "buttons", num, "detail");
		snprintf(num, sizeof(num), "%d", p.axes);
		rec_row(3, "axes", num, "detail");
		rec_row(3, "rumble", p.has_rumble ? "yes" : "no",
			p.has_rumble ? "action:rumble" : "detail");
	} else {
		const char *kind = friendly_kind(p.vendor, p.product);
		printf("%s\n", p.name);
		if (kind) printf("  kind      %s\n", kind);
		printf("  id        %s\n", p.id);
		printf("  node      %s\n", p.node);
		printf("  bus       %s\n", bus_name(p.bus));
		printf("  usb id    %s:%s\n", p.vendor, p.product);
		printf("  buttons   %d\n", p.buttons);
		printf("  axes      %d\n", p.axes);
		printf("  rumble    %s\n", p.has_rumble ? "yes" : "no");
	}

	if (fd < 0)
		return EX_OK;	/* everything above came from sysfs */

	if (!rec)
		puts("\n  axis            value      range        deadzone");

	for (int i = 0; i < AXIS_COUNT; i++) {
		struct input_absinfo ai;
		if (ioctl(fd, EVIOCGABS(axis_names[i].code), &ai) < 0)
			continue;

		if (rec) {
			char val[32], range[64], flat[32];
			snprintf(val, sizeof(val), "%d", ai.value);
			snprintf(range, sizeof(range), "%d … %d",
				 ai.minimum, ai.maximum);
			snprintf(flat, sizeof(flat), "%d", ai.flat);
			rec_row(4, axis_names[i].name, val, range, flat);
		} else {
			printf("  %-14s %6d   %6d … %-6d  %d\n",
			       axis_names[i].name, ai.value,
			       ai.minimum, ai.maximum, ai.flat);
		}
	}

	close(fd);
	return EX_OK;
}

/* ── live test ───────────────────────────────────────────────────────────── */

/* Monotonic milliseconds. CLOCK_MONOTONIC and not the wall clock, so a test
 * running across an NTP step or a DST change still stops when it said it
 * would. */
static long long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static const char *button_name(int code)
{
	switch (code) {
	case BTN_SOUTH:		return "A / cross";
	case BTN_EAST:		return "B / circle";
	case BTN_NORTH:		return "Y / triangle";
	case BTN_WEST:		return "X / square";
	case BTN_TL:		return "left bumper";
	case BTN_TR:		return "right bumper";
	case BTN_TL2:		return "left trigger";
	case BTN_TR2:		return "right trigger";
	case BTN_SELECT:	return "select / share";
	case BTN_START:		return "start / options";
	case BTN_MODE:		return "guide / home";
	case BTN_THUMBL:	return "left stick click";
	case BTN_THUMBR:	return "right stick click";
	case BTN_DPAD_UP:	return "d-pad up";
	case BTN_DPAD_DOWN:	return "d-pad down";
	case BTN_DPAD_LEFT:	return "d-pad left";
	case BTN_DPAD_RIGHT:	return "d-pad right";
	default:		return NULL;
	}
}

static const char *axis_label(int code)
{
	for (int i = 0; i < AXIS_COUNT; i++)
		if (axis_names[i].code == code)
			return axis_names[i].name;
	return NULL;
}

/*
 * Watch a controller's events until the user stops or the timeout expires.
 *
 * Every line is a record, in plain mode too. The GUI reads this stream live to
 * light up a button as it is pressed, so there is no separate "machine" mode to
 * fall out of step with what a person sees in a terminal.
 *
 * ⚠ stdout is FLUSHED per event. It is a pipe when the GUI is reading it, and
 * a pipe is block-buffered — a button test that shows nothing until 4KB of
 * events have piled up reads as a dead controller, which is the exact thing
 * this command exists to rule out.
 */
static int pads_test(const char *want, int seconds)
{
	pad_t p;
	if (!pad_find(want, &p))
		return EX_FAIL;

	int fd = pad_open_ro(&p);
	if (fd < 0)
		return EX_FAIL;

	rec_row(4, "kind", "code", "label", "value");

	long long deadline = now_ms() + (long long)seconds * 1000;

	for (;;) {
		long long left = deadline - now_ms();
		if (left <= 0) break;

		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		int r = poll(&pfd, 1, (int)(left > 1000 ? 1000 : left));
		if (r < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (r == 0) continue;

		/* A disconnected pad reports POLLERR/POLLHUP forever and would
		 * otherwise spin here until the deadline. */
		if (pfd.revents & (POLLERR | POLLHUP)) {
			rec_row(4, "gone", "-", "controller disconnected", "-");
			fflush(stdout);
			break;
		}

		struct input_event ev[64];
		ssize_t got = read(fd, ev, sizeof(ev));
		if (got < (ssize_t)sizeof(ev[0])) {
			if (got < 0 && errno == EINTR) continue;
			if (got < 0) break;
			continue;
		}

		int count = (int)(got / (ssize_t)sizeof(ev[0]));
		for (int i = 0; i < count; i++) {
			char code[32], val[32];
			snprintf(val, sizeof(val), "%d", ev[i].value);

			if (ev[i].type == EV_KEY) {
				const char *nm = button_name(ev[i].code);
				snprintf(code, sizeof(code), "%d", ev[i].code);
				rec_row(4, "button", code,
					nm ? nm : "unnamed button", val);
			} else if (ev[i].type == EV_ABS) {
				const char *nm = axis_label(ev[i].code);
				snprintf(code, sizeof(code), "%d", ev[i].code);
				rec_row(4, "axis", code,
					nm ? nm : "unnamed axis", val);
			}
			/* EV_SYN and EV_MSC are framing, not input. */
		}
		fflush(stdout);
	}

	close(fd);
	return EX_OK;
}

/* ── rumble ──────────────────────────────────────────────────────────────── */

/*
 * Upload one rumble effect, play it, wait, then remove it.
 *
 * ⚠ The effect MUST be removed (EVIOCRMFF) and the wait must actually happen.
 * The kernel keeps uploaded effects for as long as the fd is open and every
 * device has a small fixed number of slots — a command that uploaded and
 * exited would leak one per run, and after a dozen tries EVIOCSFF starts
 * failing with ENOSPC on a controller that is working perfectly. Exiting early
 * also cuts the motor off mid-effect, since closing the fd stops playback,
 * which looks like a pad that cannot rumble.
 */
static int pads_rumble(const char *want, int strong_pct, int weak_pct, int ms)
{
	pad_t p;
	if (!pad_find(want, &p))
		return EX_FAIL;

	if (!p.has_rumble) {
		fprintf(stderr, "syn-arcade: %s has no rumble motors\n", p.name);
		return EX_FAIL;
	}

	if (strong_pct < 0) strong_pct = 0;
	if (strong_pct > 100) strong_pct = 100;
	if (weak_pct < 0) weak_pct = 0;
	if (weak_pct > 100) weak_pct = 100;
	if (ms < 1) ms = 1;
	if (ms > 10000) ms = 10000;

	int fd = pad_open_rw(&p);
	if (fd < 0)
		return EX_FAIL;

	struct ff_effect e;
	memset(&e, 0, sizeof(e));
	e.type = FF_RUMBLE;
	e.id = -1;			/* -1 asks the kernel to allocate a slot */
	e.replay.length = (unsigned)ms;
	e.u.rumble.strong_magnitude = (unsigned)(0xffff * strong_pct / 100);
	e.u.rumble.weak_magnitude   = (unsigned)(0xffff * weak_pct / 100);

	if (ioctl(fd, EVIOCSFF, &e) < 0) {
		fprintf(stderr, "syn-arcade: cannot upload rumble effect: %s\n",
			strerror(errno));
		close(fd);
		return EX_FAIL;
	}

	struct input_event play;
	memset(&play, 0, sizeof(play));
	play.type = EV_FF;
	play.code = (unsigned short)e.id;
	play.value = 1;

	if (write(fd, &play, sizeof(play)) != (ssize_t)sizeof(play)) {
		fprintf(stderr, "syn-arcade: cannot start rumble: %s\n",
			strerror(errno));
		ioctl(fd, EVIOCRMFF, e.id);
		close(fd);
		return EX_FAIL;
	}

	printf("rumbling %s — strong %d%%, weak %d%%, %dms\n",
	       p.name, strong_pct, weak_pct, ms);
	fflush(stdout);

	struct timespec ts = {
		.tv_sec = ms / 1000,
		.tv_nsec = (long)(ms % 1000) * 1000000L,
	};
	while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
		;

	ioctl(fd, EVIOCRMFF, e.id);
	close(fd);
	return EX_OK;
}

/* ── deadzones ───────────────────────────────────────────────────────────── */

/*
 * Measure how far a stick wanders while nobody is touching it, and set each
 * axis's deadzone wide enough to cover it.
 *
 * The kernel's per-axis `flat` IS the deadzone: EV_ABS values within `flat` of
 * the centre are reported as centred, and SDL, evdev-based emulators and most
 * native Linux games read it out of the absinfo rather than inventing their
 * own. So this fixes drift once for everything that uses the device, instead of
 * per game.
 *
 * ⚠ Sampling has to be event-driven, not a series of EVIOCGABS reads. The
 * absinfo `value` only changes when the driver posts an event, so polling it in
 * a loop mostly re-reads one number and concludes a badly drifting stick is
 * perfect. Read the event stream for the whole window instead and keep the
 * furthest excursion seen.
 *
 * ⚠ And it must NOT run while a stick is held. A pad measured with the stick
 * pushed over gets a deadzone covering half its range, which is a controller
 * that ignores small movements forever. Anything past a quarter of the range is
 * refused as "you were holding it" rather than believed.
 */
#define CAL_AXES 4

static int pads_calibrate(const char *want, int seconds, int explicit_pct,
			  bool reset)
{
	pad_t p;
	if (!pad_find(want, &p))
		return EX_FAIL;

	int fd = pad_open_rw(&p);
	if (fd < 0)
		return EX_FAIL;

	const int axes[CAL_AXES] = { ABS_X, ABS_Y, ABS_RX, ABS_RY };
	struct input_absinfo ai[CAL_AXES];
	bool have[CAL_AXES] = { false, false, false, false };
	int centre[CAL_AXES], worst[CAL_AXES] = { 0, 0, 0, 0 };

	for (int i = 0; i < CAL_AXES; i++) {
		if (ioctl(fd, EVIOCGABS(axes[i]), &ai[i]) < 0)
			continue;
		have[i] = true;
		centre[i] = (ai[i].minimum + ai[i].maximum) / 2;
	}

	int usable = 0;
	for (int i = 0; i < CAL_AXES; i++)
		usable += have[i] ? 1 : 0;
	if (usable == 0) {
		fprintf(stderr, "syn-arcade: %s reports no stick axes\n", p.name);
		close(fd);
		return EX_FAIL;
	}

	/* ── reset: hand every axis back to what the driver shipped ──────── */
	if (reset) {
		int failed = 0;
		for (int i = 0; i < CAL_AXES; i++) {
			if (!have[i]) continue;
			ai[i].flat = 0;
			if (ioctl(fd, EVIOCSABS(axes[i]), &ai[i]) < 0)
				failed++;
		}
		close(fd);
		if (failed) {
			fprintf(stderr, "syn-arcade: could not reset %d axes\n",
				failed);
			return EX_FAIL;
		}
		printf("deadzones cleared on %s\n", p.name);
		puts("Replug the controller to get the driver's own defaults back.");
		return EX_OK;
	}

	/* ── an explicit percentage skips the measuring ──────────────────── */
	if (explicit_pct >= 0) {
		if (explicit_pct > 50) {
			fprintf(stderr, "syn-arcade: a deadzone over 50%% would "
					"ignore half the stick\n");
			close(fd);
			return EX_USAGE;
		}
		int failed = 0;
		for (int i = 0; i < CAL_AXES; i++) {
			if (!have[i]) continue;
			int range = ai[i].maximum - ai[i].minimum;
			ai[i].flat = range * explicit_pct / 100 / 2;
			if (ioctl(fd, EVIOCSABS(axes[i]), &ai[i]) < 0)
				failed++;
		}
		close(fd);
		if (failed) {
			fprintf(stderr, "syn-arcade: could not set %d axes\n", failed);
			return EX_FAIL;
		}
		printf("deadzone set to %d%% on %s\n", explicit_pct, p.name);
		return EX_OK;
	}

	/* ── measure ─────────────────────────────────────────────────────── */
	printf("Let go of both sticks. Measuring %s for %d seconds…\n",
	       p.name, seconds);
	fflush(stdout);

	long long deadline = now_ms() + (long long)seconds * 1000;
	for (;;) {
		long long left = deadline - now_ms();
		if (left <= 0) break;

		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		int r = poll(&pfd, 1, (int)(left > 200 ? 200 : left));
		if (r < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (r == 0) continue;
		if (pfd.revents & (POLLERR | POLLHUP)) {
			fputs("syn-arcade: controller disconnected\n", stderr);
			close(fd);
			return EX_FAIL;
		}

		struct input_event ev[64];
		ssize_t got = read(fd, ev, sizeof(ev));
		if (got < (ssize_t)sizeof(ev[0]))
			continue;

		int count = (int)(got / (ssize_t)sizeof(ev[0]));
		for (int k = 0; k < count; k++) {
			if (ev[k].type != EV_ABS) continue;
			for (int i = 0; i < CAL_AXES; i++) {
				if (!have[i] || axes[i] != (int)ev[k].code)
					continue;
				int dev = ev[k].value - centre[i];
				if (dev < 0) dev = -dev;
				if (dev > worst[i]) worst[i] = dev;
			}
		}
	}

	/* ── refuse a measurement taken with a stick held ────────────────── */
	for (int i = 0; i < CAL_AXES; i++) {
		if (!have[i]) continue;
		int range = ai[i].maximum - ai[i].minimum;
		if (range > 0 && worst[i] * 4 > range) {
			fprintf(stderr,
			 "syn-arcade: %s moved %d of %d — that is a stick being\n"
			 "held, not drift. Nothing changed; let go and run it again.\n",
			 axis_label(axes[i]) ? axis_label(axes[i]) : "an axis",
			 worst[i], range);
			close(fd);
			return EX_FAIL;
		}
	}

	/* Half again over the worst excursion seen, because `seconds` of a stick
	 * at rest is a sample and the next wander will be slightly wider. */
	int failed = 0;
	for (int i = 0; i < CAL_AXES; i++) {
		if (!have[i]) continue;
		int range = ai[i].maximum - ai[i].minimum;
		int flat = worst[i] * 3 / 2;

		/* A stick that never moved still gets a floor: a pad reporting
		 * dead-on centre for eight seconds will still jitter by one or
		 * two units later, and a deadzone of 0 passes that through as
		 * real input. */
		int floor = range / 200;			/* 0.5% */
		if (flat < floor) flat = floor;

		int ceiling = range / 8;			/* 12.5% */
		if (flat > ceiling) flat = ceiling;

		ai[i].flat = flat;
		if (ioctl(fd, EVIOCSABS(axes[i]), &ai[i]) < 0) {
			failed++;
			continue;
		}

		int pct_x10 = range > 0 ? flat * 1000 / range : 0;
		printf("  %-14s drift %4d → deadzone %4d  (%d.%d%%)\n",
		       axis_label(axes[i]) ? axis_label(axes[i]) : "axis",
		       worst[i], flat, pct_x10 / 10, pct_x10 % 10);
	}

	close(fd);

	if (failed) {
		fprintf(stderr, "syn-arcade: could not set %d axes\n", failed);
		return EX_FAIL;
	}

	/*
	 * Say the awkward part out loud. `flat` lives in the kernel's copy of
	 * the device, which is destroyed when the device is unplugged — there
	 * is no sysfs attribute and no udev property that carries it back.
	 * `pads apply` re-runs this from the saved state, and the session calls
	 * it at login; a pad plugged in mid-session needs it again.
	 */
	puts("\nSaved. Deadzones live in the kernel's copy of the device, so they");
	puts("are lost when it is unplugged — `syn-arcade pads apply` puts them");
	puts("back, and your session runs that at login.");
	return EX_OK;
}

/* ── persistence ─────────────────────────────────────────────────────────── */

/* One line per pad: vendor:product<TAB>percent. Keyed on the USB ids rather
 * than on "eventN", which is whatever number the kernel had free at plug-in
 * time and means nothing across a reboot. */
static bool pads_state_path(char *buf, size_t n)
{
	return config_path(buf, n, "syn-arcade/deadzones.state");
}

static int pads_save(const char *want, int pct)
{
	pad_t p;
	if (!pad_find(want, &p))
		return EX_FAIL;

	char path[4096];
	if (!pads_state_path(path, sizeof(path)))
		return EX_FAIL;

	char key[64];
	snprintf(key, sizeof(key), "%s:%s", p.vendor, p.product);

	char *old = read_file(path);
	size_t cap = (old ? strlen(old) : 0) + 128;
	char *neu = xmalloc(cap);
	neu[0] = '\0';

	/* Rewrite without this pad's line, then append the new one — so the
	 * file never accumulates two answers for one controller. */
	if (old) {
		char *save = NULL;
		for (char *ln = strtok_r(old, "\n", &save); ln;
		     ln = strtok_r(NULL, "\n", &save)) {
			if (!*trim(ln)) continue;
			if (strncmp(ln, key, strlen(key)) == 0 &&
			    ln[strlen(key)] == '\t')
				continue;
			strcat(neu, ln);
			strcat(neu, "\n");
		}
		free(old);
	}

	char line[128];
	snprintf(line, sizeof(line), "%s\t%d\n", key, pct);
	strcat(neu, line);

	int rc = write_file_inplace(path, neu);
	free(neu);

	if (rc < 0) {
		fprintf(stderr, "syn-arcade: cannot write %s: %s\n",
			path, strerror(-rc));
		return EX_FAIL;
	}
	printf("saved %d%% for %s (%s)\n", pct, p.name, key);
	return EX_OK;
}

/*
 * Re-apply saved deadzones to everything currently attached.
 *
 * Runs at login, and is safe to run at any time: a pad with nothing saved for
 * it is left exactly as the driver set it up. Silent unless something is
 * actually applied, because it runs from the session and a login that prints
 * about controllers nobody owns is noise.
 */
static int pads_apply(void)
{
	char path[4096];
	if (!pads_state_path(path, sizeof(path)))
		return EX_FAIL;

	char *text = read_file(path);
	if (!text)
		return EX_OK;		/* nothing saved is not a failure */

	int n;
	pad_t *v = pads_scan(&n);
	int applied = 0;

	char *save = NULL;
	for (char *ln = strtok_r(text, "\n", &save); ln;
	     ln = strtok_r(NULL, "\n", &save)) {
		char *tab = strchr(ln, '\t');
		if (!tab) continue;
		*tab = '\0';
		int pct = (int)strtol(tab + 1, NULL, 10);
		if (pct < 0 || pct > 50) continue;

		for (int i = 0; i < n; i++) {
			char key[64];
			snprintf(key, sizeof(key), "%s:%s",
				 v[i].vendor, v[i].product);
			if (strcmp(key, ln) != 0)
				continue;

			int fd = open(v[i].node, O_RDWR | O_CLOEXEC);
			if (fd < 0) continue;

			const int axes[CAL_AXES] = { ABS_X, ABS_Y, ABS_RX, ABS_RY };
			for (int a = 0; a < CAL_AXES; a++) {
				struct input_absinfo ai;
				if (ioctl(fd, EVIOCGABS(axes[a]), &ai) < 0)
					continue;
				ai.flat = (ai.maximum - ai.minimum) * pct / 100 / 2;
				if (ioctl(fd, EVIOCSABS(axes[a]), &ai) == 0)
					applied++;
			}
			close(fd);
		}
	}

	free(text);
	free(v);

	if (applied)
		printf("applied saved deadzones to %d axes\n", applied);
	return EX_OK;
}

/* ── dispatch ────────────────────────────────────────────────────────────── */

static int opt_int(int argc, char **argv, const char *name, int fallback)
{
	size_t n = strlen(name);
	for (int i = 0; i < argc; i++) {
		if (strncmp(argv[i], name, n) != 0 || argv[i][n] != '=')
			continue;
		return (int)strtol(argv[i] + n + 1, NULL, 10);
	}
	return fallback;
}

static bool opt_flag(int argc, char **argv, const char *name)
{
	for (int i = 0; i < argc; i++)
		if (strcmp(argv[i], name) == 0)
			return true;
	return false;
}

/* The first argument that is not an option — the controller the user named. */
static const char *first_operand(int argc, char **argv)
{
	for (int i = 0; i < argc; i++)
		if (argv[i][0] != '-')
			return argv[i];
	return NULL;
}

/* Defined at the bottom, with the other evdev polling loop it borrows from. */
static int pads_hold_stream(void);

int cmd_pads(int argc, char **argv)
{
	if (argc < 1)
		return pads_list(false);

	const char *sub = argv[0];
	int rest_c = argc - 1;
	char **rest = argv + 1;

	if (strcmp(sub, "--rec") == 0)	return pads_list(true);
	if (strcmp(sub, "list") == 0)
		return pads_list(opt_flag(rest_c, rest, "--rec"));
	if (strcmp(sub, "apply") == 0)	return pads_apply();
	if (strcmp(sub, "hold") == 0)	return pads_hold_stream();

	const char *who = first_operand(rest_c, rest);

	if (strcmp(sub, "info") == 0) {
		if (!who) { fputs("syn-arcade: pads info <controller>\n", stderr);
			    return EX_USAGE; }
		return pads_info(who, opt_flag(rest_c, rest, "--rec"));
	}

	if (strcmp(sub, "test") == 0) {
		if (!who) { fputs("syn-arcade: pads test <controller>\n", stderr);
			    return EX_USAGE; }
		return pads_test(who, opt_int(rest_c, rest, "--seconds", 30));
	}

	if (strcmp(sub, "rumble") == 0) {
		if (!who) { fputs("syn-arcade: pads rumble <controller>\n", stderr);
			    return EX_USAGE; }
		return pads_rumble(who,
				   opt_int(rest_c, rest, "--strong", 80),
				   opt_int(rest_c, rest, "--weak", 40),
				   opt_int(rest_c, rest, "--ms", 600));
	}

	if (strcmp(sub, "calibrate") == 0) {
		if (!who) { fputs("syn-arcade: pads calibrate <controller>\n", stderr);
			    return EX_USAGE; }
		return pads_calibrate(who,
				      opt_int(rest_c, rest, "--seconds", 5),
				      opt_int(rest_c, rest, "--deadzone", -1),
				      opt_flag(rest_c, rest, "--reset"));
	}

	if (strcmp(sub, "save") == 0) {
		int pct = opt_int(rest_c, rest, "--deadzone", -1);
		if (!who || pct < 0) {
			fputs("syn-arcade: pads save <controller> --deadzone=N\n",
			      stderr);
			return EX_USAGE;
		}
		return pads_save(who, pct);
	}

	fprintf(stderr, "syn-arcade: unknown pads command '%s'\n", sub);
	return EX_USAGE;
}

/* ── the navigation stream ───────────────────────────────────────────────── */

/*
 * `syn-arcade big nav` — every controller on the machine, reduced to the eight
 * words a menu needs, one per line on stdout.
 *
 * ── Why this exists at all, and why it is NOT synthetic input ───────────────
 *
 * A ten-foot interface has to be drivable from a gamepad, and Qt does not read
 * one: QtGamepad was removed in Qt 6, and quickshell has no evdev binding. The
 * obvious fix — a daemon that turns stick movement into arrow KEY events
 * through uinput — is the one thing this project will not do. Synthetic input
 * goes to the compositor, which delivers it to whatever is focused, so a stray
 * event lands in somebody's browser rather than in this menu; that has happened
 * here before and it is not recoverable. It is also wrong even when it works:
 * a virtual keyboard is a system-wide device, so every game, terminal and text
 * field on the machine sees stick drift as held arrow keys.
 *
 * So nothing is synthesised. This process reads the event nodes it is already
 * allowed to read and writes WORDS to a pipe, and exactly one program is
 * listening at the other end. Nothing outside the shell can see a keystroke,
 * because there is no keystroke.
 *
 * ── Repeat is implemented here, not in the shell ────────────────────────────
 *
 * A direction that is HELD has to repeat, or crossing a library of sixty games
 * means sixty separate pushes of the stick. Doing that in QML would need the
 * shell to track which direction is down, which is the same state machine one
 * layer further from the device — and it cannot see the difference between
 * "held" and "the pad stopped reporting", which is what an unplugged
 * controller looks like. Here, a POLLHUP ends the hold; there, it would repeat
 * forever.
 *
 * ── The three sources of a direction, all merged ────────────────────────────
 *
 * A d-pad arrives as a hat (ABS_HAT0X/Y) on most pads and as four BUTTONS
 * (BTN_DPAD_*) on some — the Switch Pro controller and several 8BitDo modes —
 * and the left stick is a third. All three are folded into one direction per
 * pad, so a person can use whichever their thumb is on, and no pad reports a
 * direction twice.
 */

#define NAV_MAX 16
#define NAV_DELAY_MS  380	/* held this long before it starts repeating */
#define NAV_REPEAT_MS 110	/* then one step this often */
#define NAV_RESCAN_MS 2000	/* how often a hotplugged pad is noticed */

typedef struct {
	int  fd;
	char id[32];
	int  cx, cy;		/* stick centre and the distance that counts */
	int  span_x, span_y;
	int  stick_x, stick_y;	/* the three sources, each -1/0/1 */
	int  hat_x, hat_y;
	int  btn_x, btn_y;
	int  dir_x, dir_y;	/* what they merge to, and what was last sent */
	long long repeat_at;
} navpad_t;

/*
 * Where "pushed" starts, per axis, per device.
 *
 * NOT a fixed number: axis ranges are whatever the device reports — 0..255 on
 * an old USB pad, -32768..32767 on a DualSense, 0..65535 on some wheels — so a
 * hardcoded threshold is either unreachable on one and permanently tripped on
 * another. EVIOCGABS gives the real range, and half of it from centre is far
 * enough that resting-hand jitter never trips and a deliberate push always
 * does.
 *
 * ⚠ Deliberately NOT the device's `flat` (the deadzone `pads calibrate` sets).
 * That is tuned for a game — small, so aiming stays precise — and a menu
 * threshold that small turns a thumb resting on the stick into a list
 * scrolling on its own.
 */
static void nav_axis_range(int fd, int code, int *centre, int *span)
{
	struct input_absinfo ai;
	*centre = 0;
	*span = 1;
	if (ioctl(fd, EVIOCGABS(code), &ai) != 0)
		return;
	*centre = (ai.maximum + ai.minimum) / 2;
	*span = (ai.maximum - ai.minimum) / 4;	/* half of half-range */
	if (*span < 1)
		*span = 1;
}

static int nav_axis_dir(int value, int centre, int span)
{
	if (value > centre + span) return 1;
	if (value < centre - span) return -1;
	return 0;
}

/* Open every attached pad. Returns how many are open. */
static int nav_open(navpad_t *pads, int max)
{
	int count = 0;
	int n = 0;
	pad_t *found = pads_scan(&n);

	for (int i = 0; i < n && count < max; i++) {
		if (!found[i].is_pad)
			continue;
		/* Quiet on failure, unlike every other command here: a pad that
		 * cannot be opened is one this stream ignores, and a menu is
		 * not the place to explain uaccess. `syn-arcade pads` is. */
		int fd = open(found[i].node, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
		if (fd < 0)
			continue;

		navpad_t *p = &pads[count++];
		memset(p, 0, sizeof(*p));
		p->fd = fd;
		snprintf(p->id, sizeof(p->id), "%s", found[i].id);
		nav_axis_range(fd, ABS_X, &p->cx, &p->span_x);
		nav_axis_range(fd, ABS_Y, &p->cy, &p->span_y);
	}
	free(found);
	return count;
}

static void nav_close(navpad_t *pads, int count)
{
	for (int i = 0; i < count; i++)
		if (pads[i].fd >= 0)
			close(pads[i].fd);
}

/* ⚠ FLUSHED on every line. stdout is a pipe here — the shell is the only
 * consumer — and a pipe is block buffered, so without this a press does
 * nothing until 4KB of them have piled up. That is not a slow menu, it is a
 * dead one. */
static void nav_say(const char *word)
{
	puts(word);
	fflush(stdout);
}

static void nav_say_dir(int dx, int dy)
{
	if (dy < 0) nav_say("up");
	else if (dy > 0) nav_say("down");
	else if (dx < 0) nav_say("left");
	else if (dx > 0) nav_say("right");
}

/*
 * The button map.
 *
 * Only the buttons a menu has a meaning for. Everything else — triggers, stick
 * clicks, the four paddles on the back of a pad that claims to have them — is
 * dropped here rather than passed on as a number, because a stream carrying
 * events the reader has no use for is one where a new button silently becomes a
 * navigation command.
 *
 * ⚠ A is BTN_SOUTH and B is BTN_EAST *by position*, not by the letter printed
 * on the pad. Nintendo-layout controllers have those letters the other way
 * round and the kernel still reports the bottom button as BTN_SOUTH, which is
 * the one under the thumb — matching the physical position is what makes the
 * same tile confirm on every controller.
 */
static const char *nav_button(int code)
{
	switch (code) {
	case BTN_SOUTH:		return "accept";
	case BTN_EAST:		return "back";
	case BTN_NORTH:		return "info";
	case BTN_WEST:		return "search";
	case BTN_START:		return "menu";
	case BTN_SELECT:	return "select";
	case BTN_MODE:		return "guide";
	case BTN_TL:		return "page-left";
	case BTN_TR:		return "page-right";
	default:		return NULL;
	}
}

/*
 * Fold this pad's three direction sources into one and say so if it changed.
 *
 * The stick is consulted LAST so that a d-pad press wins while both are held —
 * the d-pad is the deliberate one, and a thumb resting on a stick should not
 * override it.
 */
static void nav_settle(navpad_t *p, long long now)
{
	int dx = p->hat_x ? p->hat_x : p->btn_x ? p->btn_x : p->stick_x;
	int dy = p->hat_y ? p->hat_y : p->btn_y ? p->btn_y : p->stick_y;

	/* One axis at a time: a stick pushed diagonally would otherwise emit
	 * two moves per step and the selection would jump a row AND a column.
	 * Vertical wins, because a shelf-and-rows layout is browsed down more
	 * than it is browsed across. */
	if (dy) dx = 0;

	if (dx == p->dir_x && dy == p->dir_y)
		return;

	p->dir_x = dx;
	p->dir_y = dy;

	if (dx || dy) {
		nav_say_dir(dx, dy);
		p->repeat_at = now + NAV_DELAY_MS;
	} else {
		p->repeat_at = 0;	/* let go: stop repeating */
	}
}

int pads_nav_stream(void)
{
	navpad_t pads[NAV_MAX];
	int count = nav_open(pads, NAV_MAX);

	/*
	 * Not an error, and nothing is printed about it. A television session
	 * starts before anybody picks up a controller, and this stream is the
	 * shell's only chance to hear the one plugged in a minute later — so it
	 * waits, rescanning, rather than exiting to report an empty machine.
	 * The shell says "no controller" on its own if it wants to; that is a
	 * question `syn-arcade pads` already answers.
	 */
	long long rescan_at = now_ms() + NAV_RESCAN_MS;

	for (;;) {
		struct pollfd pfd[NAV_MAX];
		for (int i = 0; i < count; i++) {
			pfd[i].fd = pads[i].fd;
			pfd[i].events = POLLIN;
			pfd[i].revents = 0;
		}

		/* The timeout is whichever comes first: the next repeat step or
		 * the next hotplug scan. Polling on a fixed tick instead would
		 * either make repeat lumpy or spin the CPU when nothing is
		 * held. */
		long long now = now_ms();
		long long wake = rescan_at;
		for (int i = 0; i < count; i++)
			if (pads[i].repeat_at && pads[i].repeat_at < wake)
				wake = pads[i].repeat_at;
		int timeout = (int)(wake - now);
		if (timeout < 0) timeout = 0;
		if (timeout > NAV_RESCAN_MS) timeout = NAV_RESCAN_MS;

		int r = poll(pfd, (nfds_t)count, timeout);
		if (r < 0 && errno != EINTR)
			break;

		now = now_ms();

		/* ── events ── */
		for (int i = 0; i < count && r > 0; i++) {
			if (!(pfd[i].revents & POLLIN))
				continue;

			struct input_event ev[64];
			ssize_t got = read(pads[i].fd, ev, sizeof(ev));
			if (got < (ssize_t)sizeof(ev[0]))
				continue;

			navpad_t *p = &pads[i];
			int n = (int)(got / (ssize_t)sizeof(ev[0]));

			for (int k = 0; k < n; k++) {
				int code = ev[k].code, val = ev[k].value;

				if (ev[k].type == EV_ABS) {
					switch (code) {
					case ABS_X:
						p->stick_x = nav_axis_dir(val, p->cx, p->span_x);
						break;
					case ABS_Y:
						p->stick_y = nav_axis_dir(val, p->cy, p->span_y);
						break;
					case ABS_HAT0X:
						p->hat_x = val > 0 ? 1 : val < 0 ? -1 : 0;
						break;
					case ABS_HAT0Y:
						p->hat_y = val > 0 ? 1 : val < 0 ? -1 : 0;
						break;
					default:
						break;
					}
				} else if (ev[k].type == EV_SYN &&
					   code == SYN_REPORT) {
					/* END OF FRAME — and the reason this
					 * is handled rather than skipped as
					 * "framing, not input".
					 *
					 * One read() can return several
					 * frames. Deciding the direction once
					 * per READ means a press and its
					 * release, both landing in the same
					 * batch, cancel each other out and
					 * emit NOTHING — a d-pad tap quick
					 * enough to arrive in one go simply
					 * does not move the selection, and it
					 * happens more the busier the machine
					 * is, which is the worst possible
					 * shape for a bug. SYN_REPORT is
					 * exactly the kernel saying "that is
					 * one complete state now", so it is
					 * where the state is read. */
					nav_settle(p, now);
				} else if (ev[k].type == EV_KEY) {
					/* value 2 is the kernel's own key
					 * autorepeat, which pads do not send
					 * and which would double every press
					 * from anything that did. */
					int down = val == 1;
					switch (code) {
					case BTN_DPAD_UP:    p->btn_y = down ? -1 : 0; break;
					case BTN_DPAD_DOWN:  p->btn_y = down ?  1 : 0; break;
					case BTN_DPAD_LEFT:  p->btn_x = down ? -1 : 0; break;
					case BTN_DPAD_RIGHT: p->btn_x = down ?  1 : 0; break;
					default: {
						const char *w = nav_button(code);
						if (w && val == 1)
							nav_say(w);
						break;
					}
					}
				}
			}

			/* One last settle for a device that sent no SYN in
			 * this batch. Cheap, and idempotent — nothing is
			 * emitted unless the direction actually changed. */
			nav_settle(p, now);
		}

		/*
		 * ── hotplug, in both directions ──
		 *
		 * ⚠ AFTER the events above, never before, and this is the one
		 * ordering in this function that is not arbitrary. A hangup
		 * does NOT arrive on its own: the kernel sets POLLHUP in the
		 * SAME revents as the POLLIN carrying the last events the
		 * device produced. Rescanning first would close and reopen
		 * every descriptor with those events still unread, so the last
		 * thing somebody did before a pad dropped out would be thrown
		 * away — and on a device that produces a hangup per batch, it
		 * would be every event, forever, with the stream looking
		 * perfectly healthy.
		 */
		bool relist = false;
		for (int i = 0; i < count; i++)
			if (pfd[i].revents & (POLLERR | POLLHUP))
				relist = true;	/* unplugged mid-session */

		if (relist || now >= rescan_at) {
			int n = 0;
			pad_t *found = pads_scan(&n);
			int live = 0;
			for (int i = 0; i < n; i++)
				if (found[i].is_pad)
					live++;
			free(found);

			if (relist || live != count) {
				/* ⚠ Reopen the WHOLE set rather than diffing.
				 * Event node numbers are recycled: a pad
				 * unplugged and plugged back in is usually a
				 * different eventN, and sometimes the same one
				 * pointing at different hardware. Matching by
				 * id across a replug is how a stream ends up
				 * reading a keyboard. */
				nav_close(pads, count);
				count = nav_open(pads, NAV_MAX);
			}
			rescan_at = now + NAV_RESCAN_MS;
		}

		/* ── repeat ── */
		for (int i = 0; i < count; i++) {
			navpad_t *p = &pads[i];
			if (!p->repeat_at || now < p->repeat_at)
				continue;
			if (!p->dir_x && !p->dir_y) {
				p->repeat_at = 0;
				continue;
			}
			nav_say_dir(p->dir_x, p->dir_y);
			p->repeat_at = now + NAV_REPEAT_MS;
		}

		/*
		 * The shell exiting closes the pipe, and this is what makes
		 * that the end of this process too — one leaked stream per big
		 * screen session, each holding every event node open, is the
		 * alternative.
		 *
		 * ⚠ Not redundant with SIGPIPE. The default disposition would
		 * kill us on the first write to a closed pipe, but a signal
		 * disposition is INHERITED: start big screen mode from a shell
		 * or a service that ignores SIGPIPE and every descendant does
		 * too, and then the write simply fails with EPIPE and sets the
		 * error flag instead. Checking it covers both worlds.
		 */
		if (feof(stdout) || ferror(stdout))
			break;
	}

	nav_close(pads, count);
	return EX_OK;
}

/*
 * `pads hold` — keep every attached controller AWAKE for as long as a UI is up.
 *
 * ⚠ This exists because of a fault that looks nothing like its cause, and the
 * reasoning is worth keeping because the obvious reading blames the wrong
 * program.
 *
 * xpad submits its USB interrupt URB when the input device is OPENED, and only
 * then. With nothing holding the node open the endpoint is never polled, a
 * 2.4GHz dongle sees no traffic, and the pad's own idle timer switches it off —
 * the dongle re-enumerating to its "no pad" product id seconds later. Steam
 * Input holds every pad open for as long as it runs, which is the entire reason
 * a controller stays alive under Steam and dies under anything that does not.
 *
 * `pads list` opens each node, reads what it needs and exits. So the window
 * built on it sat and watched the controller it was describing switch itself
 * off, and looked exactly like the thing that had killed it. Measured on an
 * 8BitDo Ultimate: 4-5s with nothing holding it, 43s under Steam, and no
 * disconnect at all in five minutes with a single blocking read on the node.
 *
 * It reads and DISCARDS. Holding the descriptor is what keeps the URB
 * submitted, but a client that never reads has the kernel fill its buffer and
 * raise SYN_DROPPED, and draining is also what makes POLLHUP arrive so a replug
 * is noticed. It GRABS nothing — no EVIOCGRAB — so a game, Steam and this can
 * all hold the same pad at once, which they routinely will.
 */
static int pads_hold_stream(void)
{
	/*
	 * ⚠ TWO independent ways to notice the UI has gone, because this
	 * process prints nothing and so cannot find out the way pads_nav_stream
	 * does (write a word, then check ferror).
	 *
	 *   1. POLLERR on stdout. quickshell hands a Process a pipe; when the
	 *      window closes and the read end goes, the write end reports
	 *      POLLERR — without us ever writing to it.
	 *   2. PR_SET_PDEATHSIG, for when stdout is NOT a pipe — a terminal, or
	 *      /dev/null — and so never errors at all.
	 *
	 * Neither alone covers both, and getting it wrong leaves a stray
	 * process holding every event node open for the rest of the session.
	 * On a pad that sleeps, that failure is invisible: it looks like the
	 * bug being fixed here, fixed.
	 *
	 * ⚠ Do NOT add a `getppid() == 1 -> exit` guard for the race where the
	 * parent dies before the prctl above. It looks like it closes a hole and
	 * it opens a worse one: under systemd, or from a disowned shell, the
	 * parent IS init, and the command would exit instantly and hold nothing
	 * while reporting success — the silent no-op this file keeps warning
	 * about. The race it guards against is already covered, because in the
	 * one case that matters the parent is quickshell and stdout is its pipe.
	 */
	prctl(PR_SET_PDEATHSIG, SIGTERM);

	navpad_t pads[NAV_MAX];
	int count = nav_open(pads, NAV_MAX);
	long long rescan_at = now_ms() + NAV_RESCAN_MS;

	for (;;) {
		struct pollfd pfd[NAV_MAX + 1];
		int n = 0;

		for (int i = 0; i < count; i++) {
			pfd[n].fd = pads[i].fd;
			pfd[n].events = POLLIN;
			pfd[n].revents = 0;
			n++;
		}

		/* events = 0: the error bits are reported whether asked for or
		 * not, and POLLIN on stdout would be meaningless. */
		int out = n;
		pfd[n].fd = STDOUT_FILENO;
		pfd[n].events = 0;
		pfd[n].revents = 0;
		n++;

		int r = poll(pfd, (nfds_t)n, NAV_RESCAN_MS);
		if (r < 0 && errno != EINTR)
			break;
		if (pfd[out].revents & (POLLERR | POLLHUP | POLLNVAL))
			break;

		/*
		 * ⚠ Drain BEFORE rescanning, the same ordering pads_nav_stream
		 * depends on and for the same reason: a hangup arrives in the
		 * SAME revents as the last data the device produced. The events
		 * are thrown away here, but reading them is still what lets the
		 * POLLHUP in that revents mean "replug" rather than being lost
		 * with the descriptor that carried it.
		 */
		bool relist = false;
		for (int i = 0; i < count; i++) {
			if (pfd[i].revents & POLLIN) {
				struct input_event ev[64];
				/* Non-blocking (nav_open sets O_NONBLOCK), so
				 * this ends on EAGAIN. */
				while (read(pads[i].fd, ev, sizeof(ev)) > 0)
					;
			}
			if (pfd[i].revents & (POLLERR | POLLHUP))
				relist = true;
		}

		long long now = now_ms();
		if (relist || now >= rescan_at) {
			int found_n = 0;
			pad_t *found = pads_scan(&found_n);
			int live = 0;
			for (int i = 0; i < found_n; i++)
				if (found[i].is_pad)
					live++;
			free(found);

			/* Reopen the whole set rather than diffing — event node
			 * numbers are recycled across a replug. Same reasoning
			 * as the nav loop above. */
			if (relist || live != count) {
				nav_close(pads, count);
				count = nav_open(pads, NAV_MAX);
			}
			rescan_at = now + NAV_RESCAN_MS;
		}
	}

	nav_close(pads, count);
	return EX_OK;
}

/* ── the two doors out of this file ──────────────────────────────────────── */

/*
 * Open every attached controller for reading, and say how many.
 *
 * Exported so vptr.c can drive a POINTER from the same devices this file
 * already knows how to find, without a second copy of "what counts as a
 * gamepad" — which is the part that is easy to get wrong (see the capability
 * bitmask comment at the top: sysfs prints those words most-significant first,
 * and reading them the obvious way finds no gamepads at all).
 *
 * Read-only, non-blocking, close-on-exec, and NOT grabbed: a game, Steam, the
 * navigation stream and the mouse can all hold the same pad at once, and every
 * one of them routinely does.
 */
int pads_open_all(int *fds, int max)
{
	int count = 0, n = 0;
	pad_t *found = pads_scan(&n);

	for (int i = 0; i < n && count < max; i++) {
		if (!found[i].is_pad)
			continue;
		int fd = open(found[i].node, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
		if (fd < 0)
			continue;	/* quiet: `syn-arcade pads` explains uaccess */
		fds[count++] = fd;
	}
	free(found);
	return count;
}

/* How many gamepads are attached right now. The cheap half of the above, for
 * the hotplug check: a count that changed is the signal to reopen the set. */
int pads_attached(void)
{
	int n = 0, live = 0;
	pad_t *found = pads_scan(&n);
	for (int i = 0; i < n; i++)
		if (found[i].is_pad)
			live++;
	free(found);
	return live;
}

/*
 * `syn-arcade big guard` — watch every controller for the GUIDE button and
 * nothing else.
 *
 * This is the other half of the guide button, and it is a separate process
 * from the navigation stream on purpose. `big nav` only exists while big
 * screen mode is on screen; the guide button has to work when it is NOT, from
 * the desktop, with nothing of ours running — which means something has to be
 * holding the pads open and listening all session. That something is this.
 *
 * ⚠ It deliberately does nothing at all while big screen mode is running. Its
 * own `big nav` is reading the same button from the same device, and both
 * acting on one press is a race with two wrong outcomes: the shell hides
 * itself and this immediately puts it back, or two of them start. `running`
 * is the caller's test (the flock in big.c), asked at the moment of the press
 * rather than remembered, because big screen mode can come and go a dozen
 * times while this process lives.
 *
 * It also holds every pad OPEN for the whole session, which is `pads hold`'s
 * job as a side effect — see the comment above pads_hold_stream() for why a
 * wireless pad falls asleep otherwise. That is not a coincidence worth
 * removing: a guide button on a controller that has switched itself off is a
 * guide button that does nothing.
 */
int pads_guide_watch(bool (*running)(void), void (*on_guide)(void))
{
	/* Same two-way death detection as `pads hold`: a pipe that goes away,
	 * or a parent that does. Without it this outlives the session that
	 * started it and holds every event node open. */
	prctl(PR_SET_PDEATHSIG, SIGTERM);

	navpad_t pads[NAV_MAX];
	int count = nav_open(pads, NAV_MAX);
	long long rescan_at = now_ms() + NAV_RESCAN_MS;

	for (;;) {
		struct pollfd pfd[NAV_MAX + 1];
		int n = 0;

		for (int i = 0; i < count; i++) {
			pfd[n].fd = pads[i].fd;
			pfd[n].events = POLLIN;
			pfd[n].revents = 0;
			n++;
		}

		int out = n;
		pfd[n].fd = STDOUT_FILENO;
		pfd[n].events = 0;
		pfd[n].revents = 0;
		n++;

		int r = poll(pfd, (nfds_t)n, NAV_RESCAN_MS);
		if (r < 0 && errno != EINTR)
			break;
		if (pfd[out].revents & (POLLERR | POLLHUP | POLLNVAL))
			break;

		/* ⚠ Read first, rescan after — the hangup is in the same revents
		 * as the last events the device produced. */
		bool relist = false;
		for (int i = 0; i < count; i++) {
			if (pfd[i].revents & POLLIN) {
				struct input_event ev[64];
				ssize_t got;
				while ((got = read(pads[i].fd, ev, sizeof(ev))) > 0) {
					int k = (int)(got / (ssize_t)sizeof(ev[0]));
					for (int j = 0; j < k; j++) {
						if (ev[j].type != EV_KEY ||
						    ev[j].code != BTN_MODE ||
						    ev[j].value != 1)
							continue;
						if (running && running())
							continue;
						if (on_guide)
							on_guide();
					}
				}
			}
			if (pfd[i].revents & (POLLERR | POLLHUP))
				relist = true;
		}

		long long now = now_ms();
		if (relist || now >= rescan_at) {
			if (relist || pads_attached() != count) {
				nav_close(pads, count);
				count = nav_open(pads, NAV_MAX);
			}
			rescan_at = now + NAV_RESCAN_MS;
		}
	}

	nav_close(pads, count);
	return EX_OK;
}
