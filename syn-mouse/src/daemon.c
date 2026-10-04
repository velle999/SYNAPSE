/* daemon.c — `syn-mouse daemon`: the mice, the twins, and the loop.
 *
 * ── What it does ────────────────────────────────────────────────────────────
 *
 * While a profile with bindings is in force — normally: while the game it
 * names has focus — every mouse is GRABBED (EVIOCGRAB), so the compositor
 * stops hearing it directly, and a uinput TWIN with the same name, ids and
 * capabilities says everything the mouse said instead. Bound buttons are taken
 * out of that stream and handed to the engine; the keys the engine presses go
 * out through one more uinput device, a keyboard.
 *
 * When no profile is in force, the mice are let go and the compositor reads
 * them directly again. Outside the game nothing sits between the hand and the
 * cursor at all — not a forwarding loop with nothing to do, but nothing.
 *
 * ── Why focus, and not "always" ─────────────────────────────────────────────
 *
 * ⛔ SYNTHETIC KEYS GO TO WHATEVER HAS FOCUS. A toggle pressing `1` every five
 * seconds is exactly what the game wants and exactly what a browser must never
 * get; a system-wide input device cannot tell them apart, so this process has
 * to. The profile leaves the moment the game loses focus — the toggle pauses,
 * anything held is let go — and comes back when it returns.
 *
 * ── The traps, each of which has bitten a grab-and-forward tool here ───────
 *
 * ⛔ NEVER GRAB MID-CLICK. A grab taken between a press and its release sends
 * the release only to the grabber; the compositor keeps a button that never
 * comes up. Every grab and every ungrab waits until EVIOCGKEY says the mouse
 * has nothing down.
 *
 * ⛔ NEVER OPEN DEVICE NODES ON A TIMER. Closing an evdev node waits for an RCU
 * grace period, ~6 ms each; a periodic rescan of 25 nodes was a 150 ms freeze
 * in the aim every two seconds in xenia-kbm. Mice are found through sysfs —
 * which opens nothing — once at start and then only when inotify says a node
 * appeared or changed.
 *
 * ⚠ AN 8000 Hz MOUSE IS 8000 FRAMES A SECOND THROUGH HERE while grabbed. A
 * frame is read in one read() and written to the twin in one write(), at its
 * SYN_REPORT; nothing on that path allocates, logs or asks anybody anything.
 *
 * ⚠ A TWIN IS READY A MOMENT AFTER IT EXISTS. The compositor opens a new input
 * device asynchronously, and whatever is written to it before then goes
 * nowhere. Twins and the keyboard are made the first time a grab is wanted and
 * the grab waits TWIN_SETTLE_MS; after that they live as long as their mouse.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synmouse.h"
#include "i18n.h"
#include "config.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define MAX_MICE          8
#define FOCUS_POLL_MS     250
#define FOCUS_RETRY_MS    2000
#define FOCUS_TIMEOUT_MS  1000
#define UNGRAB_LINGER_MS  3000
#define TWIN_SETTLE_MS    250
#define GRAB_RETRY_MS     30

#define LBITS            (sizeof(unsigned long) * 8)
#define NLONGS(n)        (((n) + LBITS - 1) / LBITS)

static bool bit(const unsigned long *a, unsigned b)
{
	return (a[b / LBITS] >> (b % LBITS)) & 1;
}

typedef struct {
	int  id;                 /* stable for this process: the engine's `dev` */
	int  fd;
	char node[PATH_MAX];
	char name[128];
	struct input_id iid;
	unsigned long evbits[NLONGS(EV_CNT)];
	unsigned long keybits[NLONGS(KEY_CNT)];
	unsigned long relbits[NLONGS(REL_CNT)];
	unsigned long mscbits[NLONGS(MSC_CNT)];
	unsigned long propbits[NLONGS(INPUT_PROP_CNT)];

	int      twin;           /* uinput fd, or -1 */
	int64_t  twin_ready;
	bool     grabbed;
	int64_t  stale_us;       /* events stamped at or before this predate the grab */
	bool     dropping;       /* after SYN_DROPPED, until the next SYN_REPORT */
	bool     grab_failed;    /* said so once; quiet until it works */

	/* How each button's press went, so its release goes the same way: a
	 * press forwarded to the twin must be released there even if a profile
	 * came into force in between, or the twin holds it forever. */
	uint8_t  route[IN_FIRST_WHEEL];   /* 0 up, 1 forwarded, 2 the engine's */
	/* Who is holding each of BTN_LEFT..BTN_TASK down on the twin — the hand
	 * through forwarding, the engine through a binding, or both. */
	uint8_t  hold[8];

	struct input_event ob[128];
	int      on;

	/* The test seam: a FIFO of text lines instead of a device. */
	bool     fake;
	char     tbuf[512];
	size_t   tlen;
} mouse_t;

typedef struct {
	config_t cfg;
	engine_t eng;
	char    *cfg_path;
	char    *cfg_dir;
	struct stat cfg_st;      /* the file as last loaded, to skip a reload of the same bytes */

	mouse_t  mice[MAX_MICE];
	int      nmice;
	int      next_id;

	int      keys;           /* the uinput keyboard, or -1 */
	int64_t  keys_ready;
	uint8_t  khold[256];
	struct input_event kb[16];
	int      kn;
	char     uinput_err[32]; /* errno name of the last failure, "" if none */

	int      ino, wd_dev, wd_cfg;
	int      sigfd;
	int      ctl;
	char    *ctl_path;

	/* focus, asked of synui */
	char    *synui_path;
	int      ffd;
	char     fbuf[16384];
	size_t   flen;
	int64_t  fsent, fnext;
	bool     focus_known;
	bool     focus_none;
	char     app[256];
	char     title[512];

	int      profile;        /* in force, -1 none, -2 not yet decided */
	bool     rematch;        /* focus or config changed: decide the profile again */
	bool     app_profiles;   /* the config has a profile for an app: ask about focus */
	unsigned mice_gen;       /* bumped whenever a mouse is removed */
	int64_t  ungrab_at;
	bool     quitting;

	FILE    *fake_out;       /* SYNMOUSE_OUTPUT */
	const char *uinput_path;
} daemon_t;

/* ⚠ THE JOURNAL IS A RECORD: English, one line, no prefix — journald adds
 * the unit name. */
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static int64_t mono_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static const char *errname(int e)
{
	switch (e) {
	case EACCES: return "EACCES";
	case EPERM:  return "EPERM";
	case ENOENT: return "ENOENT";
	case EBUSY:  return "EBUSY";
	case ENODEV: return "ENODEV";
	default:     return "EIO";
	}
}

/* ── output: the keyboard ────────────────────────────────────────────────── */

static int uinput_open(daemon_t *d)
{
	int fd = open(d->uinput_path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		const char *en = errname(errno);
		if (strcmp(d->uinput_err, en)) {
			say("cannot open %s: %s%s", d->uinput_path, strerror(errno),
			    errno == EACCES ? " — this account needs to be in the input group" : "");
			snprintf(d->uinput_err, sizeof d->uinput_err, "%s", en);
		}
		return -1;
	}
	d->uinput_err[0] = '\0';
	return fd;
}

static int uinput_finish(int fd, const char *name, const struct input_id *id)
{
	struct uinput_setup us;
	memset(&us, 0, sizeof us);
	us.id = *id;
	snprintf(us.name, sizeof us.name, "%s", name);
	if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
		say("cannot create the input device \"%s\": %s", name, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

static int keys_create(daemon_t *d, int64_t now)
{
	if (d->keys >= 0) return 0;
	if (d->fake_out) { d->keys = INT_MAX; d->keys_ready = now; return 0; }

	int fd = uinput_open(d);
	if (fd < 0) return -1;
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	for (int c = 1; c < 256; c++) ioctl(fd, UI_SET_KEYBIT, c);
	struct input_id id = { .bustype = BUS_VIRTUAL, .vendor = 0, .product = 0, .version = 1 };
	fd = uinput_finish(fd, "syn-mouse keys", &id);
	if (fd < 0) return -1;
	d->keys = fd;
	d->keys_ready = now + TWIN_SETTLE_MS;
	say("created the key output device");
	return 0;
}

static void keys_flush(daemon_t *d)
{
	if (!d->kn) return;
	if (d->fake_out) {
		for (int i = 0; i < d->kn; i++)
			if (d->kb[i].type == EV_KEY)
				fprintf(d->fake_out, "key %u %d\n", d->kb[i].code, d->kb[i].value);
	} else if (d->keys >= 0) {
		d->kb[d->kn].type = EV_SYN;
		d->kb[d->kn].code = SYN_REPORT;
		d->kb[d->kn].value = 0;
		d->kn++;
		ssize_t unused = write(d->keys, d->kb, sizeof d->kb[0] * (size_t)d->kn);
		(void)unused;
	}
	d->kn = 0;
}

static void keys_emit(daemon_t *d, uint16_t code, int value)
{
	if (code == 0 || code > 255 || d->keys < 0) return;
	/* Held by two bindings at once (two toggles both on shift): it goes down
	 * with the first and up with the last. */
	if (value) { if (d->khold[code]++) return; }
	else       { if (!d->khold[code] || --d->khold[code]) return; }
	if (d->kn >= (int)(sizeof d->kb / sizeof d->kb[0]) - 1) keys_flush(d);
	struct input_event *e = &d->kb[d->kn++];
	memset(e, 0, sizeof *e);
	e->type = EV_KEY;
	e->code = code;
	e->value = value;
}

/* ── output: the twins ───────────────────────────────────────────────────── */

static void twin_flush(daemon_t *d, mouse_t *m)
{
	if (!m->on) return;
	if (d->fake_out) {
		for (int i = 0; i < m->on; i++)
			if (m->ob[i].type != EV_SYN)
				fprintf(d->fake_out, "twin %d %u %u %d\n", m->id,
				        m->ob[i].type, m->ob[i].code, m->ob[i].value);
	} else if (m->twin >= 0) {
		ssize_t unused = write(m->twin, m->ob, sizeof m->ob[0] * (size_t)m->on);
		(void)unused;
	}
	m->on = 0;
}

static void twin_put(daemon_t *d, mouse_t *m, uint16_t type, uint16_t code, int32_t value)
{
	if (m->on >= (int)(sizeof m->ob / sizeof m->ob[0]) - 1) {
		/* A frame longer than the buffer: end it here rather than drop. */
		m->ob[m->on].type = EV_SYN; m->ob[m->on].code = SYN_REPORT; m->ob[m->on].value = 0;
		m->on++;
		twin_flush(d, m);
	}
	struct input_event *e = &m->ob[m->on++];
	memset(e, 0, sizeof *e);
	e->type = type;
	e->code = code;
	e->value = value;
}

static void twin_syn(daemon_t *d, mouse_t *m)
{
	twin_put(d, m, EV_SYN, SYN_REPORT, 0);
	twin_flush(d, m);
}

/* who: 1 = the hand (forwarding), 2 = the engine */
static void twin_button(daemon_t *d, mouse_t *m, uint16_t code, int value, int who)
{
	if (!code_is_button(code)) { twin_put(d, m, EV_KEY, code, value); return; }
	uint8_t *h = &m->hold[code - BTN_LEFT];
	bool was = *h != 0;
	if (value) *h |= (uint8_t)who; else *h &= (uint8_t)~who;
	bool is = *h != 0;
	if (was != is) twin_put(d, m, EV_KEY, code, is ? 1 : 0);
}

static int twin_create(daemon_t *d, mouse_t *m, int64_t now)
{
	if (m->twin >= 0) return 0;
	if (m->fake) { m->twin = INT_MAX; m->twin_ready = now; return 0; }

	int fd = uinput_open(d);
	if (fd < 0) return -1;

	/* The mouse's own capabilities, exactly — above all its wheel axes: a
	 * twin that claimed REL_WHEEL_HI_RES for a mouse that only sends
	 * REL_WHEEL would be a twin libinput ignores the wheel of, because a
	 * device with the high-resolution axis is read through that axis only. */
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	for (unsigned c = 0; c < KEY_CNT; c++)
		if (bit(m->keybits, c)) ioctl(fd, UI_SET_KEYBIT, c);
	/* …plus the five buttons a binding can send, whatever this mouse has. */
	for (unsigned c = BTN_LEFT; c <= BTN_EXTRA; c++) ioctl(fd, UI_SET_KEYBIT, c);
	if (bit(m->evbits, EV_REL)) {
		ioctl(fd, UI_SET_EVBIT, EV_REL);
		for (unsigned c = 0; c < REL_CNT; c++)
			if (bit(m->relbits, c)) ioctl(fd, UI_SET_RELBIT, c);
	}
	if (bit(m->evbits, EV_MSC)) {
		ioctl(fd, UI_SET_EVBIT, EV_MSC);
		for (unsigned c = 0; c < MSC_CNT; c++)
			if (bit(m->mscbits, c)) ioctl(fd, UI_SET_MSCBIT, c);
	}
	for (unsigned c = 0; c < INPUT_PROP_CNT; c++)
		if (bit(m->propbits, c)) ioctl(fd, UI_SET_PROPBIT, c);

	/* UINPUT_MAX_NAME_SIZE is the kernel's limit, prefix and NUL included. */
	char name[UINPUT_MAX_NAME_SIZE];
	snprintf(name, sizeof name, "syn-mouse: %.68s", m->name);
	fd = uinput_finish(fd, name, &m->iid);
	if (fd < 0) return -1;
	m->twin = fd;
	m->twin_ready = now + TWIN_SETTLE_MS;
	say("created the twin of %s (%s)", m->name, m->node);
	return 0;
}

/* ── the engine's way out ────────────────────────────────────────────────── */

static mouse_t *mouse_by_id(daemon_t *d, int id)
{
	for (int i = 0; i < d->nmice; i++)
		if (d->mice[i].id == id) return &d->mice[i];
	return NULL;
}

static void io_emit(void *ctx, int dev, uint16_t code, int value)
{
	daemon_t *d = ctx;
	if (!code_is_button(code)) { keys_emit(d, code, value); return; }
	mouse_t *m = mouse_by_id(d, dev);
	if (!m || m->twin < 0) {
		for (int i = 0; i < d->nmice && (!m || m->twin < 0); i++) m = &d->mice[i];
	}
	if (m && m->twin >= 0) twin_button(d, m, code, value, 2);
}

static void io_flush(void *ctx, int dev)
{
	daemon_t *d = ctx;
	keys_flush(d);
	mouse_t *m = mouse_by_id(d, dev);
	for (int i = 0; i < d->nmice; i++) {
		mouse_t *x = &d->mice[i];
		if (x->on && (x == m || x->twin >= 0)) twin_syn(d, x);
	}
}

static void notify(daemon_t *d, const char *msg)
{
	if (!d->cfg.notify || d->quitting) return;
	const char *appname = _("Mouse Buttons");
	char *argv[] = {
		(char *)"notify-send", (char *)"-a", (char *)appname,
		(char *)"-i", (char *)"syn-mouse", (char *)"-t", (char *)"2000",
		(char *)"-h", (char *)"string:x-canonical-private-synchronous:syn-mouse",
		(char *)msg, NULL
	};
	pid_t pid;
	/* SIGCHLD is ignored, so the child reaps itself; failure to start it is
	 * a missing optional tool, not a reason to stop pressing keys. */
	posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
}

static void io_flipped(void *ctx, int in, const action_t *a, bool on)
{
	daemon_t *d = ctx;
	char keys[128], secs[24], msg[256];
	combo_format(&a->combo, keys, sizeof keys);
	seconds_format(a->interval_ms, secs, sizeof secs);
	say("%s: %s %s%s%s -> %s", input_name(in), act_name(a->kind), keys,
	    a->kind == ACT_TOGGLE ? " every " : "", a->kind == ACT_TOGGLE ? secs : "",
	    on ? "on" : "off");

	if (a->kind == ACT_TOGGLE)
		snprintf(msg, sizeof msg, on ? _("Pressing %s every %s s") : _("Stopped pressing %s"),
		         keys, secs);
	else
		snprintf(msg, sizeof msg, on ? _("Holding %s down") : _("Let go of %s"), keys);
	notify(d, msg);
}

/* ── finding mice ────────────────────────────────────────────────────────── */

static bool read_line_file(const char *path, char *buf, size_t n)
{
	FILE *f = fopen(path, "re");
	if (!f) return false;
	bool ok = fgets(buf, (int)n, f) != NULL;
	fclose(f);
	if (ok) buf[strcspn(buf, "\n")] = '\0';
	return ok;
}

/* A sysfs capability file: hex words, most significant first, one long each. */
static void read_caps(const char *path, unsigned long *bits, size_t nlongs)
{
	memset(bits, 0, nlongs * sizeof *bits);
	char buf[1024];
	if (!read_line_file(path, buf, sizeof buf)) return;
	char *words[64];
	int n = 0;
	for (char *t = strtok(buf, " "); t && n < 64; t = strtok(NULL, " ")) words[n++] = t;
	for (int i = 0; i < n && (size_t)i < nlongs; i++)
		bits[i] = strtoul(words[n - 1 - i], NULL, 16);
}

bool mouse_probe(const char *ev, char *name, size_t nn, unsigned *inputs)
{
	char path[PATH_MAX], real[PATH_MAX];
	snprintf(path, sizeof path, "/sys/class/input/%s", ev);
	if (!realpath(path, real)) return false;
	/* ⛔ NEVER A VIRTUAL DEVICE: that is our own twins, and every other
	 * program's uinput device. Grabbing a twin would be a loop. */
	if (strstr(real, "/devices/virtual/")) return false;

	unsigned long rel[NLONGS(REL_CNT)], key[NLONGS(KEY_CNT)], abs[NLONGS(ABS_CNT)];
	snprintf(path, sizeof path, "/sys/class/input/%s/device/capabilities/rel", ev);
	read_caps(path, rel, NLONGS(REL_CNT));
	snprintf(path, sizeof path, "/sys/class/input/%s/device/capabilities/key", ev);
	read_caps(path, key, NLONGS(KEY_CNT));
	snprintf(path, sizeof path, "/sys/class/input/%s/device/capabilities/abs", ev);
	read_caps(path, abs, NLONGS(ABS_CNT));

	/* Moves relatively, has a left button, and is not a touchpad or a tablet
	 * (both of which report absolute X). */
	if (!bit(rel, REL_X) || !bit(rel, REL_Y) || !bit(key, BTN_LEFT) || bit(abs, ABS_X))
		return false;
	/* ⚠ NOT A MOUSE'S KEYBOARD HALF. Gaming mice expose a second interface
	 * under the same name for their onboard macros — a Razer Viper's
	 * advertises the whole keyboard, every mouse button and a tilt wheel the
	 * mouse does not have. It looks like a mouse to the test above and lists
	 * buttons nobody can press; letter keys are what give it away. */
	if (bit(key, KEY_A) && bit(key, KEY_Z))
		return false;

	if (inputs) {
		*inputs = 0;
		for (int in = 0; in < IN_FIRST_WHEEL; in++)
			if (bit(key, input_code(in))) *inputs |= 1u << in;
		if (bit(rel, REL_WHEEL))  *inputs |= 1u << IN_WHEEL_UP | 1u << IN_WHEEL_DOWN;
		if (bit(rel, REL_HWHEEL)) *inputs |= 1u << IN_WHEEL_LEFT | 1u << IN_WHEEL_RIGHT;
	}

	snprintf(path, sizeof path, "/sys/class/input/%s/device/name", ev);
	if (!read_line_file(path, name, nn)) snprintf(name, nn, "%s", ev);
	return true;
}

static bool wanted_by_filter(const daemon_t *d, const char *name)
{
	return !d->cfg.device[0] || strcasestr(name, d->cfg.device) != NULL;
}

static mouse_t *mouse_new(daemon_t *d)
{
	if (d->nmice >= MAX_MICE) return NULL;
	mouse_t *m = &d->mice[d->nmice++];
	memset(m, 0, sizeof *m);
	m->id = ++d->next_id;
	m->fd = -1;
	m->twin = -1;
	return m;
}

static void mouse_add(daemon_t *d, const char *ev)
{
	char node[PATH_MAX], name[128];
	snprintf(node, sizeof node, "/dev/input/%s", ev);
	for (int i = 0; i < d->nmice; i++)
		if (!strcmp(d->mice[i].node, node)) return;
	if (!mouse_probe(ev, name, sizeof name, NULL)) return;
	if (!wanted_by_filter(d, name)) return;

	int fd = open(node, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		/* EACCES right after the node appears is udev not having set its
		 * group yet; the IN_ATTRIB that follows brings this back. */
		if (errno != EACCES) say("cannot open %s (%s): %s", node, name, strerror(errno));
		return;
	}
	mouse_t *m = mouse_new(d);
	if (!m) { close(fd); return; }
	m->fd = fd;
	snprintf(m->node, sizeof m->node, "%s", node);
	snprintf(m->name, sizeof m->name, "%s", name);
	ioctl(fd, EVIOCGID, &m->iid);
	ioctl(fd, EVIOCGBIT(0, sizeof m->evbits), m->evbits);
	ioctl(fd, EVIOCGBIT(EV_KEY, sizeof m->keybits), m->keybits);
	ioctl(fd, EVIOCGBIT(EV_REL, sizeof m->relbits), m->relbits);
	ioctl(fd, EVIOCGBIT(EV_MSC, sizeof m->mscbits), m->mscbits);
	ioctl(fd, EVIOCGPROP(sizeof m->propbits), m->propbits);
	/* Stamps on the clock this process reads, so "before the grab" is a
	 * comparison and not a guess. */
	int clk = CLOCK_MONOTONIC;
	ioctl(fd, EVIOCSCLOCKID, &clk);
	say("found %s (%s)", name, node);
}

static void mouse_add_fake(daemon_t *d, const char *path)
{
	int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);  /* RDWR: no EOF between writers */
	if (fd < 0) { say("cannot open %s: %s", path, strerror(errno)); return; }
	mouse_t *m = mouse_new(d);
	if (!m) { close(fd); return; }
	m->fd = fd;
	m->fake = true;
	snprintf(m->node, sizeof m->node, "%s", path);
	snprintf(m->name, sizeof m->name, "test mouse");
	for (unsigned c = BTN_LEFT; c <= BTN_TASK; c++) m->keybits[c / LBITS] |= 1UL << (c % LBITS);
	m->evbits[0] = 1UL << EV_KEY | 1UL << EV_REL;
	m->relbits[0] = 1UL << REL_X | 1UL << REL_Y | 1UL << REL_WHEEL | 1UL << REL_HWHEEL;
}

static void mouse_remove(daemon_t *d, int i, int64_t now)
{
	mouse_t *m = &d->mice[i];
	say("lost %s (%s)", m->name, m->node);
	engine_device_gone(&d->eng, m->id, now);
	if (m->fd >= 0) close(m->fd);
	if (m->twin >= 0 && m->twin != INT_MAX) close(m->twin);   /* destroys it */
	memmove(&d->mice[i], &d->mice[i + 1], sizeof *m * (size_t)(d->nmice - i - 1));
	d->nmice--;
	d->mice_gen++;
}

static void scan_all(daemon_t *d)
{
	const char *fake = getenv("SYNMOUSE_INPUT");
	if (fake && *fake) {
		if (!d->nmice) mouse_add_fake(d, fake);
		return;
	}
	DIR *dir = opendir("/dev/input");
	if (!dir) return;
	struct dirent *de;
	while ((de = readdir(dir)))
		if (!strncmp(de->d_name, "event", 5)) mouse_add(d, de->d_name);
	closedir(dir);
}

/* ── grabbing ────────────────────────────────────────────────────────────── */

static bool buttons_down(mouse_t *m)
{
	if (m->fake) {
		for (int i = 0; i < IN_FIRST_WHEEL; i++) if (m->route[i]) return true;
		return false;
	}
	unsigned long k[NLONGS(KEY_CNT)];
	memset(k, 0, sizeof k);
	if (ioctl(m->fd, EVIOCGKEY(sizeof k), k) < 0) return true;
	for (unsigned c = BTN_MISC; c < BTN_JOYSTICK; c++)
		if (bit(k, c)) return true;
	return false;
}

static void try_grab(daemon_t *d, mouse_t *m, int64_t now)
{
	if (m->grabbed) return;
	if (keys_create(d, now) < 0 || twin_create(d, m, now) < 0) return;
	if (now < m->twin_ready || now < d->keys_ready) return;
	if (buttons_down(m)) return;

	if (!m->fake && ioctl(m->fd, EVIOCGRAB, 1) < 0) {
		if (!m->grab_failed)
			say("cannot take %s (%s): %s%s", m->name, m->node, strerror(errno),
			    errno == EBUSY ? " — another program has it" : "");
		m->grab_failed = true;
		return;
	}
	m->grab_failed = false;
	m->grabbed = true;
	m->stale_us = mono_us();
	m->dropping = false;
	memset(m->route, 0, sizeof m->route);
	if (d->fake_out) fprintf(d->fake_out, "grab %d 1\n", m->id);
}

static void mouse_read(daemon_t *d, mouse_t *m, int64_t now);

static void try_ungrab(daemon_t *d, mouse_t *m, int64_t now, bool force)
{
	if (!m->grabbed) return;
	if (!force && buttons_down(m)) return;

	/* What is already queued was meant for the twin: send it there first. */
	mouse_read(d, m, now);
	/* Anything still held on the twin goes up before the compositor starts
	 * reading the real mouse again. */
	for (int i = 0; i < 8; i++)
		if (m->hold[i]) { m->hold[i] = 1; twin_button(d, m, (uint16_t)(BTN_LEFT + i), 0, 1); }
	twin_syn(d, m);

	if (!m->fake) ioctl(m->fd, EVIOCGRAB, 0);
	m->grabbed = false;
	memset(m->route, 0, sizeof m->route);
	memset(m->hold, 0, sizeof m->hold);
	if (d->fake_out) fprintf(d->fake_out, "grab %d 0\n", m->id);
}

/* ── one event from a grabbed mouse ──────────────────────────────────────── */

static void wheel(daemon_t *d, mouse_t *m, const struct input_event *ev, int64_t now)
{
	bool hi = ev->code == REL_WHEEL_HI_RES || ev->code == REL_HWHEEL_HI_RES;
	bool vertical = ev->code == REL_WHEEL || ev->code == REL_WHEEL_HI_RES;
	int in = vertical ? (ev->value > 0 ? IN_WHEEL_UP : IN_WHEEL_DOWN)
	                  : (ev->value > 0 ? IN_WHEEL_RIGHT : IN_WHEEL_LEFT);

	if (!engine_bound(&d->eng, in)) {
		twin_put(d, m, ev->type, ev->code, ev->value);
		return;
	}
	/* ⚠ THE NOTCH IS COUNTED ONCE. A high-resolution mouse sends both axes
	 * for every notch; the engine hears the classic one and the hi-res copy
	 * in that direction is simply not forwarded. */
	if (hi) return;
	int n = ev->value < 0 ? -ev->value : ev->value;
	for (int i = 0; i < n && i < 16; i++) engine_wheel(&d->eng, m->id, in, now);
}

static void button(daemon_t *d, mouse_t *m, const struct input_event *ev, int64_t now)
{
	int in = input_from_code(ev->code);
	if (in < 0) { twin_put(d, m, ev->type, ev->code, ev->value); return; }

	if (ev->value == 1) {
		if (m->route[in]) return;                     /* already down */
		bool took = engine_button(&d->eng, m->id, in, true, now);
		m->route[in] = took ? 2 : 1;
		if (!took) twin_button(d, m, ev->code, 1, 1);
	} else if (ev->value == 0) {
		if (m->route[in] == 2) engine_button(&d->eng, m->id, in, false, now);
		else if (m->route[in] == 1) twin_button(d, m, ev->code, 0, 1);
		m->route[in] = 0;
	}
	/* value 2 is key repeat, which a mouse button does not do */
}

/* After SYN_DROPPED: the queue lost events, so ask the kernel what is down
 * now and settle every difference through the ordinary path. */
static void resync(daemon_t *d, mouse_t *m, int64_t now)
{
	if (m->fake) return;
	unsigned long k[NLONGS(KEY_CNT)];
	memset(k, 0, sizeof k);
	if (ioctl(m->fd, EVIOCGKEY(sizeof k), k) < 0) return;
	for (int in = 0; in < IN_FIRST_WHEEL; in++) {
		uint16_t code = input_code(in);
		bool down = bit(k, code);
		if (down == (m->route[in] != 0)) continue;
		struct input_event ev = { .type = EV_KEY, .code = code, .value = down };
		button(d, m, &ev, now);
	}
	twin_syn(d, m);
}

static void handle(daemon_t *d, mouse_t *m, const struct input_event *ev, int64_t now)
{
	if (m->dropping) {
		if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
			m->dropping = false;
			m->on = 0;
			resync(d, m, now);
		}
		return;
	}
	if (ev->type == EV_SYN) {
		if (ev->code == SYN_DROPPED) { m->dropping = true; m->on = 0; return; }
		if (ev->code == SYN_REPORT) twin_syn(d, m);
		return;
	}
	if (ev->type == EV_KEY) { button(d, m, ev, now); return; }
	if (ev->type == EV_REL && (ev->code == REL_WHEEL || ev->code == REL_HWHEEL ||
	                           ev->code == REL_WHEEL_HI_RES || ev->code == REL_HWHEEL_HI_RES)) {
		if (ev->value) wheel(d, m, ev, now);
		return;
	}
	twin_put(d, m, ev->type, ev->code, ev->value);
}

/* The test FIFO speaks "type code value" lines, one event each. */
static void fake_read(daemon_t *d, mouse_t *m, int64_t now)
{
	for (;;) {
		ssize_t n = read(m->fd, m->tbuf + m->tlen, sizeof m->tbuf - m->tlen - 1);
		if (n <= 0) break;
		m->tlen += (size_t)n;
		m->tbuf[m->tlen] = '\0';
		char *line = m->tbuf, *nl;
		while ((nl = strchr(line, '\n'))) {
			*nl = '\0';
			unsigned type, code; int value;
			if (sscanf(line, "%u %u %d", &type, &code, &value) == 3 && m->grabbed) {
				struct input_event ev = { .type = (uint16_t)type, .code = (uint16_t)code, .value = value };
				handle(d, m, &ev, now);
			}
			line = nl + 1;
		}
		m->tlen = strlen(line);
		memmove(m->tbuf, line, m->tlen + 1);
	}
}

static void mouse_read(daemon_t *d, mouse_t *m, int64_t now)
{
	if (m->fake) { fake_read(d, m, now); return; }
	struct input_event evs[256];
	for (;;) {
		ssize_t n = read(m->fd, evs, sizeof evs);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) break;
		size_t cnt = (size_t)n / sizeof evs[0];
		for (size_t i = 0; i < cnt; i++) {
			const struct input_event *ev = &evs[i];
			int64_t t = (int64_t)ev->input_event_sec * 1000000 + ev->input_event_usec;
			/* Queued before the grab: the compositor already had it. */
			if (t <= m->stale_us) continue;
			handle(d, m, ev, now);
		}
		if (cnt < sizeof evs / sizeof evs[0]) break;
	}
}

/* ── focus ───────────────────────────────────────────────────────────────── */

static void focus_close(daemon_t *d)
{
	if (d->ffd >= 0) close(d->ffd);
	d->ffd = -1;
	d->flen = 0;
}

static void focus_lost(daemon_t *d, int64_t now);

static void focus_ask(daemon_t *d, int64_t now)
{
	if (d->ffd >= 0 || now < d->fnext) return;
	if (!d->app_profiles) {
		/* Nothing depends on focus: do not ask, and do not wake up to. */
		if (d->focus_known) d->rematch = true;
		d->focus_known = false;
		d->fnext = 0;
		return;
	}
	if (!d->synui_path) d->synui_path = synui_socket_path();
	if (!d->synui_path) { focus_lost(d, now); return; }
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	snprintf(a.sun_path, sizeof a.sun_path, "%s", d->synui_path);
	if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a) != 0 ||
	    write(fd, "activewindow\n", 13) != 13) {
		if (fd >= 0) close(fd);
		/* The compositor restarted or is not up yet: find it again. */
		free(d->synui_path);
		d->synui_path = NULL;
		focus_lost(d, now);
		return;
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	d->ffd = fd;
	d->flen = 0;
	d->fsent = now;
}

static void focus_read(daemon_t *d, int64_t now)
{
	for (;;) {
		ssize_t n = read(d->ffd, d->fbuf + d->flen, sizeof d->fbuf - d->flen - 1);
		if (n < 0 && errno == EAGAIN) return;          /* more to come */
		if (n > 0) { d->flen += (size_t)n; if (d->flen < sizeof d->fbuf - 1) continue; }
		break;                                          /* EOF, error or full */
	}
	d->fbuf[d->flen] = '\0';
	char app[256] = "", title[512] = "";
	bool has = json_string_member(d->fbuf, d->flen, "app_id", app, sizeof app);
	json_string_member(d->fbuf, d->flen, "title", title, sizeof title);
	bool known = d->flen > 0;
	if (known != d->focus_known || !has != d->focus_none ||
	    strcmp(app, d->app) || strcmp(title, d->title))
		d->rematch = true;
	d->focus_known = known;
	d->focus_none = !has;
	snprintf(d->app, sizeof d->app, "%s", app);
	snprintf(d->title, sizeof d->title, "%s", title);
	focus_close(d);
	d->fnext = now + FOCUS_POLL_MS;
}

static void focus_lost(daemon_t *d, int64_t now)
{
	if (d->focus_known) d->rematch = true;
	d->focus_known = false;
	d->fnext = now + FOCUS_RETRY_MS;
}

/* ── deciding ────────────────────────────────────────────────────────────── */

static void decide(daemon_t *d, int64_t now)
{
	/* ⚠ MATCHED ONLY WHEN SOMETHING CHANGED. This runs once per pass of the
	 * loop, which is once per mouse frame while grabbed — 8000 times a second
	 * — and a glob per profile per frame would be spent on an answer that
	 * only moves when focus or the file does. */
	int prof = d->profile;
	if (d->rematch || prof == -2) {
		d->rematch = false;
		const char *app = d->focus_known && !d->focus_none ? d->app : NULL;
		prof = config_match(&d->cfg, app, app ? d->title : NULL);
		if (prof >= 0 && !profile_nbound(&d->cfg.p[prof])) prof = -1;
	}

	if (prof != d->profile) {
		const char *app = d->focus_known && !d->focus_none ? d->app : NULL;
		if (prof >= 0) say("in force: [%s]%s%s", d->cfg.p[prof].name,
		                   app ? " for " : "", app ? app : "");
		else if (d->profile >= 0) say("no profile in force");
		engine_activate(&d->eng, prof, now);
		d->profile = prof;
	}

	if (prof >= 0) {
		d->ungrab_at = 0;
		for (int i = 0; i < d->nmice; i++) try_grab(d, &d->mice[i], now);
		return;
	}
	/* ⚠ LET GO A LITTLE LATER THAN THE BINDINGS DO. The bindings stopped
	 * the instant focus left — the buttons are already plain buttons again,
	 * forwarded as they are — but an ungrab is itself an RCU wait, and focus
	 * flickering through a dialog or an alt-tab would cost one each way. */
	bool any = false;
	for (int i = 0; i < d->nmice; i++) any |= d->mice[i].grabbed;
	if (!any) { d->ungrab_at = 0; return; }
	if (!d->ungrab_at) d->ungrab_at = now + UNGRAB_LINGER_MS;
	if (now >= d->ungrab_at)
		for (int i = 0; i < d->nmice; i++) try_ungrab(d, &d->mice[i], now, false);
}

/* ── config ──────────────────────────────────────────────────────────────── */

static bool same_action(const action_t *a, const action_t *b)
{
	if (a->kind != b->kind || a->interval_ms != b->interval_ms || a->combo.n != b->combo.n)
		return false;
	for (int i = 0; i < a->combo.n; i++)
		if (a->combo.code[i] != b->combo.code[i]) return false;
	return true;
}

static void reload(daemon_t *d, int64_t now, bool force)
{
	/* ⚠ ONE SAVE IS TWO SIGNALS: `syn-mouse bind` asks for a reload, and the
	 * rename it saved with is an inotify event a moment later. The second
	 * finds the same file it already has and does nothing. */
	struct stat st;
	memset(&st, 0, sizeof st);
	stat(d->cfg_path, &st);
	if (!force && st.st_ino == d->cfg_st.st_ino && st.st_size == d->cfg_st.st_size &&
	    st.st_mtim.tv_sec == d->cfg_st.st_mtim.tv_sec &&
	    st.st_mtim.tv_nsec == d->cfg_st.st_mtim.tv_nsec)
		return;

	config_t nc;
	if (config_load(&nc, d->cfg_path) < 0) {
		say("cannot read %s: %s — keeping the bindings already loaded",
		    d->cfg_path, strerror(errno));
		return;
	}
	d->cfg_st = st;

	/* ⛔ AN EDIT DOES NOT SWITCH OFF WHAT IT DID NOT TOUCH. Changing what the
	 * wheel does, mid-game, from the window, must leave the toggle on the
	 * thumb button running: everything on whose profile and binding are
	 * unchanged comes back on after the reload. */
	struct { char prof[NAME_MAX_LEN]; int in; action_t a; } carry[64];
	int ncarry = 0;
	for (int p = 0; p < d->cfg.n; p++)
		for (int in = 0; in < IN_COUNT && ncarry < 64; in++)
			if (engine_is_on(&d->eng, p, in)) {
				snprintf(carry[ncarry].prof, sizeof carry[ncarry].prof, "%s", d->cfg.p[p].name);
				carry[ncarry].in = in;
				carry[ncarry].a = d->cfg.p[p].act[in];
				ncarry++;
			}
	engine_unload(&d->eng, false);
	char old_device[MATCH_MAX_LEN];
	snprintf(old_device, sizeof old_device, "%s", d->cfg.device);
	config_free(&d->cfg);
	d->cfg = nc;
	engine_load(&d->eng, &d->cfg, now);
	for (int i = 0; i < ncarry; i++) {
		int idx = -1;
		for (int p = 0; p < d->cfg.n; p++)
			if (!strcmp(d->cfg.p[p].name, carry[i].prof)) idx = p;
		if (idx >= 0 && same_action(&d->cfg.p[idx].act[carry[i].in], &carry[i].a))
			engine_set_on(&d->eng, idx, carry[i].in);
		else
			io_flipped(d, carry[i].in, &carry[i].a, false);
	}
	d->profile = -2;
	d->rematch = true;
	d->app_profiles = config_has_app_profiles(&d->cfg);
	d->fnext = now;

	int nb = 0;
	for (int i = 0; i < d->cfg.n; i++) nb += profile_nbound(&d->cfg.p[i]);
	say("loaded %s: %d profile%s, %d binding%s", d->cfg_path,
	    d->cfg.n, d->cfg.n == 1 ? "" : "s", nb, nb == 1 ? "" : "s");

	if (strcmp(old_device, d->cfg.device)) {
		for (int i = d->nmice - 1; i >= 0; i--) {
			mouse_t *m = &d->mice[i];
			if (m->fake || wanted_by_filter(d, m->name)) continue;
			try_ungrab(d, m, now, true);
			mouse_remove(d, i, now);
		}
		scan_all(d);
	}
}

/* ── the control socket ──────────────────────────────────────────────────── */

static void status(daemon_t *d, FILE *f)
{
	char *a, *b;
	fprintf(f, "daemon\t%d\t%s\n", (int)getpid(), SYNMOUSE_VERSION);
	a = pct_encode(d->app); b = pct_encode(d->title);
	fprintf(f, "focus\t%d\t%s\t%s\n",
	        d->focus_known ? (d->focus_none ? 2 : 1) : 0, a, b);
	free(a); free(b);
	a = pct_encode(d->profile >= 0 ? d->cfg.p[d->profile].name : "");
	fprintf(f, "profile\t%s\n", a);
	free(a);
	for (int i = 0; i < d->nmice; i++) {
		a = pct_encode(d->mice[i].name); b = pct_encode(d->mice[i].node);
		fprintf(f, "mouse\t%s\t%s\t%d\n", a, b, d->mice[i].grabbed ? 1 : 0);
		free(a); free(b);
	}
	if (d->uinput_err[0]) fprintf(f, "problem\tuinput\t%s\n", d->uinput_err);
	if (!d->nmice) fprintf(f, "problem\tnomouse\t\n");
	for (int p = 0; p < d->cfg.n; p++) {
		for (int in = 0; in < IN_COUNT; in++) {
			if (!engine_is_on(&d->eng, p, in)) continue;
			const action_t *ac = &d->cfg.p[p].act[in];
			char keys[128];
			combo_format(&ac->combo, keys, sizeof keys);
			a = pct_encode(d->cfg.p[p].name); b = pct_encode(keys);
			fprintf(f, "active\t%s\t%s\t%s\t%s\t%u\t%d\n", a, input_name(in),
			        act_name(ac->kind), b, ac->interval_ms, p == d->profile ? 1 : 0);
			free(a); free(b);
		}
	}
}

static void control(daemon_t *d, int64_t now)
{
	int c = accept4(d->ctl, NULL, NULL, SOCK_CLOEXEC);
	if (c < 0) return;
	/* ⚠ BOUNDED: a client gets 50 ms to say what it wants. The mice are
	 * forwarded by this same loop, and a client that connects and says
	 * nothing must not freeze the cursor of the game being played. */
	struct pollfd pf = { .fd = c, .events = POLLIN };
	char line[256] = "";
	if (poll(&pf, 1, 50) == 1) {
		ssize_t n = read(c, line, sizeof line - 1);
		line[n > 0 ? n : 0] = '\0';
		line[strcspn(line, "\r\n")] = '\0';
	}

	char *out = NULL;
	size_t outlen = 0;
	FILE *f = open_memstream(&out, &outlen);
	if (!f) { close(c); return; }

	if (!strcmp(line, "status")) {
		status(d, f);
	} else if (!strcmp(line, "stop")) {
		engine_stop(&d->eng, now);
		fputs("ok\n", f);
	} else if (!strcmp(line, "reload")) {
		reload(d, now, false);
		fputs("ok\n", f);
	} else if (!strcmp(line, "ping")) {
		fputs("ok\n", f);
	} else {
		fputs("error\tunknown command\n", f);
	}
	fclose(f);
	ssize_t unused = write(c, out, outlen);
	(void)unused;
	free(out);
	close(c);
}

static int control_listen(daemon_t *d)
{
	d->ctl_path = control_socket_path();
	if (!d->ctl_path) { say("no XDG_RUNTIME_DIR — nowhere to put the control socket"); return -1; }

	if (daemon_ask("ping", NULL) == 0) {
		say("already running");
		return 1;
	}
	unlink(d->ctl_path);
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	if (strlen(d->ctl_path) >= sizeof a.sun_path) {
		say("the control socket path is too long for a unix socket: %s", d->ctl_path);
		return -1;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0) return -1;
	snprintf(a.sun_path, sizeof a.sun_path, "%s", d->ctl_path);
	mode_t old = umask(0177);
	int r = bind(fd, (struct sockaddr *)&a, sizeof a);
	umask(old);
	if (r != 0 || listen(fd, 8) != 0) {
		say("cannot listen on %s: %s", d->ctl_path, strerror(errno));
		close(fd);
		return -1;
	}
	d->ctl = fd;
	return 0;
}

/* ── inotify ─────────────────────────────────────────────────────────────── */

static void watch_read(daemon_t *d, int64_t now)
{
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	bool cfg = false;
	for (;;) {
		ssize_t n = read(d->ino, buf, sizeof buf);
		if (n <= 0) break;
		for (char *p = buf; p < buf + n; ) {
			struct inotify_event *ie = (struct inotify_event *)p;
			p += sizeof *ie + ie->len;
			if (!ie->len) continue;
			if (ie->wd == d->wd_cfg && !strcmp(ie->name, "bindings.conf")) cfg = true;
			if (ie->wd == d->wd_dev && !strncmp(ie->name, "event", 5) &&
			    (ie->mask & (IN_CREATE | IN_ATTRIB)))
				mouse_add(d, ie->name);
		}
	}
	if (cfg) reload(d, now, false);
}

/* ── the loop ────────────────────────────────────────────────────────────── */

static void timeout_min(int64_t *t, int64_t when)
{
	if (when > 0 && (*t < 0 || when < *t)) *t = when;
}

int daemon_run(void)
{
	static daemon_t D;
	daemon_t *d = &D;
	memset(d, 0, sizeof *d);
	d->keys = d->ctl = d->ffd = d->ino = d->sigfd = -1;
	d->profile = -2;
	d->uinput_path = getenv("SYNMOUSE_UINPUT");
	if (!d->uinput_path || !*d->uinput_path) d->uinput_path = "/dev/uinput";

	const char *fo = getenv("SYNMOUSE_OUTPUT");
	if (fo && *fo) {
		d->fake_out = fopen(fo, "ae");
		if (!d->fake_out) die("cannot open %s: %s", fo, strerror(errno));
		setvbuf(d->fake_out, NULL, _IOLBF, 0);
	}

	engine_io_t io = { .ctx = d, .emit = io_emit, .flush = io_flush, .flipped = io_flipped };
	engine_init(&d->eng, io);
	config_init(&d->cfg);
	engine_load(&d->eng, &d->cfg, now_ms());

	int r = control_listen(d);
	if (r != 0) return r > 0 ? 0 : 1;

	sigset_t ss;
	sigemptyset(&ss);
	sigaddset(&ss, SIGTERM);
	sigaddset(&ss, SIGINT);
	sigaddset(&ss, SIGHUP);
	sigprocmask(SIG_BLOCK, &ss, NULL);
	d->sigfd = signalfd(-1, &ss, SFD_CLOEXEC | SFD_NONBLOCK);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);

	d->cfg_dir = config_dir();
	d->cfg_path = config_path();
	mkdir(d->cfg_dir, 0755);
	d->ino = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
	d->wd_cfg = inotify_add_watch(d->ino, d->cfg_dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE);
	d->wd_dev = getenv("SYNMOUSE_INPUT") ? -1
	          : inotify_add_watch(d->ino, "/dev/input", IN_CREATE | IN_ATTRIB);

	say("syn-mouse %s", SYNMOUSE_VERSION);
	reload(d, now_ms(), true);
	scan_all(d);
	if (!d->nmice) say("no mouse found yet");

	while (!d->quitting) {
		int64_t now = now_ms();

		focus_ask(d, now);
		if (d->ffd >= 0 && now - d->fsent > FOCUS_TIMEOUT_MS) {
			focus_close(d);
			focus_lost(d, now);
		}
		decide(d, now);
		int64_t next = engine_tick(&d->eng, now);

		timeout_min(&next, d->fnext);
		timeout_min(&next, d->ungrab_at);
		if (d->ffd >= 0) timeout_min(&next, d->fsent + FOCUS_TIMEOUT_MS);
		if (d->profile >= 0)
			for (int i = 0; i < d->nmice; i++)
				if (!d->mice[i].grabbed) timeout_min(&next, now + GRAB_RETRY_MS);

		struct pollfd pf[8 + MAX_MICE];
		int np = 0, at_sig, at_ino, at_ctl, at_focus = -1, at_mouse[MAX_MICE];
		pf[at_sig = np++] = (struct pollfd){ .fd = d->sigfd, .events = POLLIN };
		pf[at_ino = np++] = (struct pollfd){ .fd = d->ino, .events = POLLIN };
		pf[at_ctl = np++] = (struct pollfd){ .fd = d->ctl, .events = POLLIN };
		if (d->ffd >= 0) pf[at_focus = np++] = (struct pollfd){ .fd = d->ffd, .events = POLLIN };
		/* ⚠ A MOUSE NOT GRABBED IS NOT READ. The compositor is reading it,
		 * and waking 8000 times a second to throw its frames away would be
		 * the whole cost of this program spent on doing nothing. */
		int nm = d->nmice;
		for (int i = 0; i < nm; i++) {
			at_mouse[i] = -1;
			if (d->mice[i].grabbed || d->mice[i].fake)
				pf[at_mouse[i] = np++] = (struct pollfd){ .fd = d->mice[i].fd, .events = POLLIN };
		}

		int wait = next < 0 ? -1 : (int)(next > now ? next - now : 0);
		int n = poll(pf, (nfds_t)np, wait);
		if (n < 0) { if (errno == EINTR) continue; say("poll: %s", strerror(errno)); break; }
		now = now_ms();

		unsigned gen = d->mice_gen;
		if (pf[at_sig].revents & POLLIN) {
			struct signalfd_siginfo si;
			while (read(d->sigfd, &si, sizeof si) == sizeof si) {
				if (si.ssi_signo == SIGHUP) reload(d, now, true);
				else d->quitting = true;
			}
		}
		if (pf[at_ino].revents & POLLIN) watch_read(d, now);
		if (pf[at_ctl].revents & POLLIN) control(d, now);
		if (at_focus >= 0 && pf[at_focus].revents) focus_read(d, now);

		/* A reload above may have dropped a mouse, and then the indices the
		 * poll set was built from no longer line up: next pass. */
		for (int i = nm - 1; i >= 0 && gen == d->mice_gen; i--) {
			if (at_mouse[i] < 0 || i >= d->nmice) continue;
			short re = pf[at_mouse[i]].revents;
			if (re & POLLIN) mouse_read(d, &d->mice[i], now);
			if (re & (POLLERR | POLLHUP | POLLNVAL)) mouse_remove(d, i, now);
		}
	}

	say("stopping");
	engine_unload(&d->eng, false);
	keys_flush(d);
	for (int i = 0; i < d->nmice; i++) try_ungrab(d, &d->mice[i], now_ms(), true);
	for (int i = d->nmice - 1; i >= 0; i--) mouse_remove(d, i, now_ms());
	if (d->keys >= 0 && d->keys != INT_MAX) close(d->keys);
	if (d->ctl >= 0) { close(d->ctl); unlink(d->ctl_path); }
	return 0;
}
