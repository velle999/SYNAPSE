/*
 * vptr.c — the controller as a mouse.
 *
 * ── Why this file exists, given what pad.c says ─────────────────────────────
 *
 * pad.c is emphatic that it synthesises NOTHING: it reads gamepads and writes
 * words on a pipe, and exactly one program is listening. That is the right
 * answer for a MENU, and it is not an answer at all for a WEB BROWSER. A
 * browser does not read words on a pipe. It takes pointer motion, buttons and
 * scroll off the seat like every other application, and no amount of message
 * passing changes that — so the moment big screen mode hands the television to
 * somebody's browser, something has to move a real pointer.
 *
 * So this is the deliberate exception, and it is bounded on purpose:
 *
 *   · It is a SEPARATE PROCESS with a separate command, started by the shell
 *     only while a pointer-driven application is on screen, and killed the
 *     moment big screen mode comes back. Nothing runs it at login, and there
 *     is no daemon to forget about.
 *   · It goes through virtual-pointer-v1, which synui treats as a privileged
 *     global (see privileged_globals[] in synui_main.c) — so a sandboxed
 *     client cannot do this, and the compositor can revoke it in one place.
 *   · It moves a POINTER and never a key. Stick drift moves a cursor, which is
 *     visible and harmless; the equivalent through a virtual keyboard would be
 *     held arrow keys in every text field on the machine, which is why pad.c
 *     refuses to go that way and why nothing here does either.
 *
 * ⚠ It is still system-wide input while it runs. The cursor it drives is THE
 * cursor: whatever is under it gets the click, big screen mode included. That
 * is the whole reason `big mouse` is not left running when the shell is back on
 * screen — a stick nudged on the sofa would otherwise be clicking through a
 * desktop nobody is looking at.
 *
 * ── Why the pointer is not just wtype for mice ──────────────────────────────
 *
 * The on-screen keyboard next to this feature shells out to wtype, because
 * wtype exists, ships on every SynapseOS install and already speaks
 * virtual-keyboard-v1. There is no such tool for the pointer — synui did not
 * even advertise the protocol until this feature needed it — so this is a
 * Wayland client, and it is the one thing in syn-arcade that links anything
 * beyond libc.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "arcade.h"

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

#include <wayland-client.h>

#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

/* ── tuning ──────────────────────────────────────────────────────────────── */

#define TICK_MS        16	/* ~60 a second: one frame's worth of motion */
#define MOUSE_MAX_PXS  1250.0	/* pixels per second at full deflection */
#define SCROLL_MAX_PXS 900.0	/* the same for the right stick */
#define WHEEL_NOTCH    15.0	/* one d-pad step, in wl_pointer axis units */
#define WHEEL_DELAY_MS 380	/* held this long before the d-pad repeats */
#define WHEEL_RATE_MS  110
#define RESCAN_MS      2000

/*
 * The dead zone, as a fraction of the stick's half-range.
 *
 * ⚠ NOT the kernel's `flat` that `pads calibrate` writes. That is tuned so
 * aiming in a game stays precise, which is far too small for a pointer: a
 * thumb resting on a stick would walk the cursor across the screen and land it
 * on whatever was there. This is generous, and everything past it is scaled
 * from zero so the first pixel of movement is still slow.
 */
#define DEAD_FRAC      0.22

#define PADS_MAX       16

/* ── monotonic milliseconds ──────────────────────────────────────────────── */

static long long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ── one pad's live state ────────────────────────────────────────────────── */

typedef struct {
	int fd;

	/* Per-axis centre and half-range, from EVIOCGABS. A hardcoded range is
	 * wrong on nearly every pad: 0..255 on an old USB one, -32768..32767 on
	 * a DualSense, 0..65535 on some wheels. */
	struct { int centre, half; bool have; } ax[ABS_CNT];

	double lx, ly;		/* left stick, -1..1 after the dead zone */
	double rx, ry;		/* right stick, likewise */
	int    hat_x, hat_y;	/* the d-pad, as -1/0/1 */
	long long wheel_at;	/* when the held d-pad next scrolls, 0 = idle */
	bool   wheel_repeat;	/* past the first step, so use the fast rate */
} mpad_t;

/* The d-pad direction changed: arm (or disarm) the wheel repeat. Called from
 * four places — the hat axes and the four buttons some pads use instead — so
 * that "held" always means the same thing however the pad reports it. */
static void hat_armed(mpad_t *p, long long now)
{
	if (p->hat_x || p->hat_y) {
		p->wheel_at = now;	/* the first notch is immediate */
		p->wheel_repeat = false;
	} else {
		p->wheel_at = 0;
		p->wheel_repeat = false;
	}
}

static void axis_learn(mpad_t *p, int code)
{
	struct input_absinfo ai;
	if (code < 0 || code >= ABS_CNT)
		return;
	if (ioctl(p->fd, EVIOCGABS(code), &ai) != 0)
		return;
	p->ax[code].centre = (ai.maximum + ai.minimum) / 2;
	p->ax[code].half   = (ai.maximum - ai.minimum) / 2;
	if (p->ax[code].half < 1)
		p->ax[code].half = 1;
	p->ax[code].have = true;
}

/*
 * A raw axis value as -1..1, with the dead zone taken out and the remainder
 * rescaled so it still starts at zero.
 *
 * The square is the difference between a pointer somebody can aim and one that
 * either crawls or bolts: at a third of a stick's travel it gives a ninth of
 * the speed, which is where fine positioning happens, and full deflection is
 * still full speed.
 */
static double axis_norm(const mpad_t *p, int code, int value)
{
	if (code < 0 || code >= ABS_CNT || !p->ax[code].have)
		return 0.0;

	double v = (double)(value - p->ax[code].centre) / (double)p->ax[code].half;
	if (v > 1.0) v = 1.0;
	if (v < -1.0) v = -1.0;

	double mag = v < 0 ? -v : v;
	if (mag <= DEAD_FRAC)
		return 0.0;

	mag = (mag - DEAD_FRAC) / (1.0 - DEAD_FRAC);
	mag *= mag;
	return v < 0 ? -mag : mag;
}

/* ── the Wayland half ────────────────────────────────────────────────────── */

typedef struct {
	struct wl_display  *display;
	struct wl_registry *registry;
	struct wl_seat     *seat;
	struct zwlr_virtual_pointer_manager_v1 *mgr;
	uint32_t mgr_version;

	/* Every output, so the one big screen mode is on can be named. Kept
	 * as a small table because wl_output only learns its NAME in a later
	 * event than the bind — there is nothing to match against yet at the
	 * moment the global is announced. */
	struct { struct wl_output *out; char name[64]; } outs[16];
	int n_outs;

	struct zwlr_virtual_pointer_v1 *ptr;
} wl_t;

static void output_geometry(void *d, struct wl_output *o, int32_t x, int32_t y,
			    int32_t pw, int32_t ph, int32_t sub, const char *make,
			    const char *model, int32_t tr)
{
	(void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sub;
	(void)make; (void)model; (void)tr;
}
static void output_mode(void *d, struct wl_output *o, uint32_t f, int32_t w,
			int32_t h, int32_t r)
{
	(void)d; (void)o; (void)f; (void)w; (void)h; (void)r;
}
static void output_done(void *d, struct wl_output *o) { (void)d; (void)o; }
static void output_scale(void *d, struct wl_output *o, int32_t s)
{
	(void)d; (void)o; (void)s;
}
static void output_description(void *d, struct wl_output *o, const char *desc)
{
	(void)d; (void)o; (void)desc;
}

static void output_name(void *data, struct wl_output *o, const char *name)
{
	wl_t *w = data;
	for (int i = 0; i < w->n_outs; i++)
		if (w->outs[i].out == o)
			snprintf(w->outs[i].name, sizeof(w->outs[i].name), "%s",
				 name ? name : "");
}

static const struct wl_output_listener output_listener = {
	.geometry    = output_geometry,
	.mode        = output_mode,
	.done        = output_done,
	.scale       = output_scale,
	.name        = output_name,
	.description = output_description,
};

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
			    const char *iface, uint32_t version)
{
	wl_t *w = data;

	if (strcmp(iface, wl_seat_interface.name) == 0 && !w->seat) {
		w->seat = wl_registry_bind(reg, name, &wl_seat_interface, 1);
		return;
	}

	if (strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
		/* Version 2 is what has create_virtual_pointer_with_output.
		 * Binding at 1 against a v2 compositor is not an error, it just
		 * means the pointer cannot be pinned to the television. */
		uint32_t v = version < 2 ? version : 2;
		w->mgr = wl_registry_bind(reg, name,
					  &zwlr_virtual_pointer_manager_v1_interface,
					  v);
		w->mgr_version = v;
		return;
	}

	/* wl_output learned to say its own name in version 4. Below that there
	 * is no way to tell one from another without xdg-output, and the only
	 * thing lost is pinning the cursor to one screen. */
	if (strcmp(iface, wl_output_interface.name) == 0 && version >= 4 &&
	    w->n_outs < (int)(sizeof(w->outs) / sizeof(w->outs[0]))) {
		struct wl_output *o = wl_registry_bind(reg, name,
						       &wl_output_interface, 4);
		w->outs[w->n_outs].out = o;
		w->outs[w->n_outs].name[0] = '\0';
		wl_output_add_listener(o, &output_listener, w);
		w->n_outs++;
	}
}

static void registry_global_remove(void *data, struct wl_registry *reg,
				   uint32_t name)
{
	(void)data; (void)reg; (void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global        = registry_global,
	.global_remove = registry_global_remove,
};

/* ── the events this sends ───────────────────────────────────────────────── */

static uint32_t stamp(void)
{
	return (uint32_t)now_ms();
}

static void ptr_motion(wl_t *w, double dx, double dy)
{
	zwlr_virtual_pointer_v1_motion(w->ptr, stamp(),
				       wl_fixed_from_double(dx),
				       wl_fixed_from_double(dy));
	zwlr_virtual_pointer_v1_frame(w->ptr);
}

static void ptr_button(wl_t *w, uint32_t button, bool down)
{
	zwlr_virtual_pointer_v1_button(w->ptr, stamp(), button,
				       down ? WL_POINTER_BUTTON_STATE_PRESSED
					    : WL_POINTER_BUTTON_STATE_RELEASED);
	zwlr_virtual_pointer_v1_frame(w->ptr);
}

/*
 * Scroll.
 *
 * `source` matters to the applications on the other end, which is why it is a
 * parameter rather than a constant: a stick pushed and held is a CONTINUOUS
 * gesture and a browser should scroll it smoothly and kinetically, while a
 * d-pad press is a WHEEL notch and should move a fixed step. Sending both as
 * the same source makes one of the two feel broken, and which one depends on
 * the application.
 */
static void ptr_axis(wl_t *w, uint32_t axis, double value, uint32_t source)
{
	zwlr_virtual_pointer_v1_axis_source(w->ptr, source);
	zwlr_virtual_pointer_v1_axis(w->ptr, stamp(), axis,
				     wl_fixed_from_double(value));
	zwlr_virtual_pointer_v1_frame(w->ptr);
}

/* The end of a continuous scroll. Without it a client that is doing kinetic
 * scrolling never learns the finger left, and keeps going. */
static void ptr_axis_stop(wl_t *w, uint32_t axis)
{
	zwlr_virtual_pointer_v1_axis_source(w->ptr,
					    WL_POINTER_AXIS_SOURCE_FINGER);
	zwlr_virtual_pointer_v1_axis_stop(w->ptr, stamp(), axis);
	zwlr_virtual_pointer_v1_frame(w->ptr);
}

/*
 * The buttons.
 *
 * Chosen for a browser on a television, which is what this is for, and printed
 * on screen by the shell so nobody has to guess:
 *
 *   A, right trigger      left click — the one everything needs
 *   X, left trigger       right click, for a context menu
 *   right stick click     middle click (open in a new tab)
 *   B, left shoulder      Back, as the side button on a mouse. Firefox and
 *                         Chromium both navigate back on BTN_SIDE.
 *   right shoulder        Forward, likewise BTN_EXTRA.
 *
 * ⚠ Y (BTN_NORTH), Start and Guide are deliberately NOT here. They belong to
 * the shell — the on-screen keyboard and the way back to big screen mode —
 * which is still reading the same pad through `big nav` while this runs. A
 * button cannot be in both places: it would type and click at once.
 */
static uint32_t mouse_button(int code)
{
	switch (code) {
	case BTN_SOUTH:  return BTN_LEFT;
	case BTN_TR2:    return BTN_LEFT;
	case BTN_WEST:   return BTN_RIGHT;
	case BTN_TL2:    return BTN_RIGHT;
	case BTN_THUMBR: return BTN_MIDDLE;
	case BTN_EAST:   return BTN_SIDE;	/* Back */
	case BTN_TL:     return BTN_SIDE;
	case BTN_TR:     return BTN_EXTRA;	/* Forward */
	default:         return 0;
	}
}

/* ── shutdown ────────────────────────────────────────────────────────────── */

/*
 * ⚠ Buttons held at the moment this is asked to stop must be RELEASED, and
 * that is the reason this process handles signals at all.
 *
 * The shell kills `big mouse` when big screen mode comes back — routinely,
 * every time, and quite possibly while somebody still has the trigger down.
 * Destroying the virtual pointer with a button down leaves the seat believing
 * that button is still held: the next window to be focused starts life inside
 * a drag it cannot end, which looks like the desktop having locked up.
 */
static volatile sig_atomic_t stop_now;

static void on_signal(int sig)
{
	(void)sig;
	stop_now = 1;
}

/* ── the command ─────────────────────────────────────────────────────────── */

int pads_mouse_stream(const char *want_output)
{
	/* If whoever started this goes without killing us — a crash, a SIGKILL —
	 * this must not be left driving the cursor. */
	prctl(PR_SET_PDEATHSIG, SIGTERM);

	if (!getenv("WAYLAND_DISPLAY")) {
		fputs("syn-arcade: no Wayland session — `big mouse` moves the "
		      "compositor's pointer and needs one\n", stderr);
		return EX_FAIL;
	}

	wl_t w;
	memset(&w, 0, sizeof(w));

	w.display = wl_display_connect(NULL);
	if (!w.display) {
		fputs("syn-arcade: cannot connect to the Wayland display\n", stderr);
		return EX_FAIL;
	}

	w.registry = wl_display_get_registry(w.display);
	wl_registry_add_listener(w.registry, &registry_listener, &w);

	/* Two round trips, not one: the first delivers the globals, the second
	 * the wl_output.name events that arrive after each output is bound. */
	wl_display_roundtrip(w.display);
	wl_display_roundtrip(w.display);

	if (!w.mgr) {
		fputs("syn-arcade: this compositor does not offer "
		      "zwlr_virtual_pointer_manager_v1, so a controller cannot "
		      "move the pointer.\nOn SynapseOS that means synui is older "
		      "than the feature — update it.\n", stderr);
		wl_display_disconnect(w.display);
		return EX_FAIL;
	}

	/* Which screen. Pinning the pointer to the television is what stops the
	 * cursor wandering onto the desk monitor, where somebody four metres
	 * away cannot see it and cannot get it back. */
	struct wl_output *pin = NULL;
	if (want_output && *want_output) {
		for (int i = 0; i < w.n_outs; i++)
			if (strcmp(w.outs[i].name, want_output) == 0)
				pin = w.outs[i].out;
	}

	if (pin && w.mgr_version >= 2)
		w.ptr = zwlr_virtual_pointer_manager_v1_create_virtual_pointer_with_output(
				w.mgr, w.seat, pin);
	else
		w.ptr = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(
				w.mgr, w.seat);

	if (!w.ptr) {
		fputs("syn-arcade: could not create a virtual pointer\n", stderr);
		wl_display_disconnect(w.display);
		return EX_FAIL;
	}
	wl_display_flush(w.display);

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	/* No SA_RESTART: poll() must come back so the loop can notice. */
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	/* ── the pads ── */
	int fds[PADS_MAX];
	int count = pads_open_all(fds, PADS_MAX);
	mpad_t pads[PADS_MAX];

	for (int i = 0; i < count; i++) {
		memset(&pads[i], 0, sizeof(pads[i]));
		pads[i].fd = fds[i];
		axis_learn(&pads[i], ABS_X);
		axis_learn(&pads[i], ABS_Y);
		axis_learn(&pads[i], ABS_RX);
		axis_learn(&pads[i], ABS_RY);
	}

	/* Sub-pixel motion has to accumulate. A stick nudged gently produces a
	 * fraction of a pixel per frame, and rounding each frame to a whole one
	 * would either round it to nothing — a stick that does not work when
	 * pushed slightly — or to a whole pixel, which is a jump. */
	double carry_x = 0, carry_y = 0;
	bool scrolling_v = false, scrolling_h = false;

	/*
	 * How many things are holding each pointer button down — a COUNT, not a
	 * flag, and it is not over-engineering.
	 *
	 * Two pad buttons map to the same pointer button on purpose (A and the
	 * right trigger are both a left click, because both are what somebody
	 * reaches for). With a flag, pressing A, then the trigger, then
	 * releasing A sends a release while the trigger is still down: the
	 * click ends in the middle of a drag, and it is the sort of thing that
	 * only shows up when somebody rests a finger on a trigger. Two pads on
	 * one machine do the same thing.
	 */
	static const uint32_t buttons[] = {
		BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA
	};
	int down[sizeof(buttons) / sizeof(buttons[0])];
	memset(down, 0, sizeof(down));

	long long next_tick = now_ms() + TICK_MS;
	long long rescan_at = now_ms() + RESCAN_MS;

	while (!stop_now) {
		struct pollfd pfd[PADS_MAX + 2];
		int n = 0;

		for (int i = 0; i < count; i++) {
			pfd[n].fd = pads[i].fd;
			pfd[n].events = POLLIN;
			pfd[n].revents = 0;
			n++;
		}

		int wl_idx = n;
		pfd[n].fd = wl_display_get_fd(w.display);
		pfd[n].events = POLLIN;
		pfd[n].revents = 0;
		n++;

		/* stdin is the shell's handle on this process: quickshell closes
		 * the pipe when the Process is stopped or the window goes, and
		 * that is the ordinary way this exits. */
		int in_idx = n;
		pfd[n].fd = STDIN_FILENO;
		pfd[n].events = 0;
		pfd[n].revents = 0;
		n++;

		/*
		 * The tick only matters while something is DEFLECTED. With
		 * every stick centred there is nothing to emit, and waking
		 * sixty times a second to work that out is a wakeup per frame
		 * for the whole time somebody is reading a web page. Idle, this
		 * waits five times as long — and loses nothing, because the
		 * first movement arrives as POLLIN on the device rather than on
		 * the timeout.
		 */
		bool moving = scrolling_v || scrolling_h;	/* a scroll still
								 * owes its stop */
		for (int i = 0; i < count && !moving; i++)
			moving = pads[i].lx != 0 || pads[i].ly != 0 ||
				 pads[i].rx != 0 || pads[i].ry != 0 ||
				 pads[i].wheel_at != 0;

		long long now = now_ms();
		int timeout = (int)(next_tick - now);
		if (timeout < 0) timeout = 0;
		if (timeout > TICK_MS) timeout = TICK_MS;
		if (!moving && timeout < TICK_MS * 5) timeout = TICK_MS * 5;

		/* ⚠ Flush before blocking, or every event composed above sits in
		 * the client's outgoing buffer until something else happens to
		 * flush it — which for a pointer means motion that arrives in
		 * bursts, or not at all. */
		wl_display_flush(w.display);

		int r = poll(pfd, (nfds_t)n, timeout);
		if (r < 0 && errno != EINTR)
			break;
		if (stop_now)
			break;
		if (pfd[in_idx].revents & (POLLERR | POLLHUP | POLLNVAL))
			break;

		if (pfd[wl_idx].revents & POLLIN) {
			if (wl_display_dispatch(w.display) < 0)
				break;		/* the compositor went away */
		}

		now = now_ms();

		/* ── read the pads ── */
		bool relist = false;
		for (int i = 0; i < count; i++) {
			if (pfd[i].revents & (POLLERR | POLLHUP))
				relist = true;
			if (!(pfd[i].revents & POLLIN))
				continue;

			struct input_event ev[64];
			ssize_t got;
			while ((got = read(pads[i].fd, ev, sizeof(ev))) > 0) {
				int k = (int)(got / (ssize_t)sizeof(ev[0]));
				for (int j = 0; j < k; j++) {
					mpad_t *p = &pads[i];
					int code = ev[j].code, val = ev[j].value;

					if (ev[j].type == EV_ABS) {
						switch (code) {
						case ABS_X:
							p->lx = axis_norm(p, ABS_X, val);
							break;
						case ABS_Y:
							p->ly = axis_norm(p, ABS_Y, val);
							break;
						case ABS_RX:
							p->rx = axis_norm(p, ABS_RX, val);
							break;
						case ABS_RY:
							p->ry = axis_norm(p, ABS_RY, val);
							break;
						case ABS_HAT0X:
							p->hat_x = val > 0 ? 1 : val < 0 ? -1 : 0;
							hat_armed(p, now);
							break;
						case ABS_HAT0Y:
							p->hat_y = val > 0 ? 1 : val < 0 ? -1 : 0;
							hat_armed(p, now);
							break;
						default:
							break;
						}
						continue;
					}

					if (ev[j].type != EV_KEY || val == 2)
						continue;

					/* The d-pad as four buttons, which is
					 * how several pads report it. Same
					 * handling as the hat above. */
					switch (code) {
					case BTN_DPAD_UP:
					case BTN_DPAD_DOWN:
						p->hat_y = code == BTN_DPAD_UP
							? (val ? -1 : 0)
							: (val ? 1 : 0);
						hat_armed(p, now);
						continue;
					case BTN_DPAD_LEFT:
					case BTN_DPAD_RIGHT:
						p->hat_x = code == BTN_DPAD_LEFT
							? (val ? -1 : 0)
							: (val ? 1 : 0);
						hat_armed(p, now);
						continue;
					default:
						break;
					}

					uint32_t b = mouse_button(code);
					if (!b)
						continue;

					int bi = -1;
					for (int q = 0; q < (int)(sizeof(buttons) /
								 sizeof(buttons[0])); q++)
						if (buttons[q] == b)
							bi = q;
					if (bi < 0)
						continue;

					if (val == 1) {
						if (down[bi]++ == 0)
							ptr_button(&w, b, true);
					} else if (down[bi] > 0 && --down[bi] == 0) {
						ptr_button(&w, b, false);
					}
				}
			}
		}

		/*
		 * ── hotplug ──
		 *
		 * ⚠ Before the tick check below, not after it. A pad that has
		 * been unplugged reports POLLHUP on every poll and never any
		 * data, so a loop that only reopened on a tick would spin at
		 * full speed until the next rescan — and the rescan is two
		 * seconds away. Read first, rescan after, act on it here.
		 */
		if (relist || now >= rescan_at) {
			if (relist || pads_attached() != count) {
				for (int i = 0; i < count; i++)
					close(pads[i].fd);
				count = pads_open_all(fds, PADS_MAX);
				for (int i = 0; i < count; i++) {
					memset(&pads[i], 0, sizeof(pads[i]));
					pads[i].fd = fds[i];
					axis_learn(&pads[i], ABS_X);
					axis_learn(&pads[i], ABS_Y);
					axis_learn(&pads[i], ABS_RX);
					axis_learn(&pads[i], ABS_RY);
				}
				/* A pad that vanished mid-press cannot send the
				 * release. Let go of everything rather than
				 * leaving the seat in a drag. */
				for (int q = 0; q < (int)(sizeof(buttons) /
							  sizeof(buttons[0])); q++) {
					if (down[q] > 0)
						ptr_button(&w, buttons[q], false);
					down[q] = 0;
				}
			}
			rescan_at = now + RESCAN_MS;
		}

		/* ── motion, scroll and wheel repeat, once per tick ── */
		if (now < next_tick)
			continue;
		next_tick = now + TICK_MS;

		double mx = 0, my = 0, sx = 0, sy = 0;
		for (int i = 0; i < count; i++) {
			/* Whichever pad is pushed furthest wins, rather than
			 * the sum: two controllers plugged in should not move
			 * the cursor at twice the speed. */
			if ((pads[i].lx < 0 ? -pads[i].lx : pads[i].lx) >
			    (mx < 0 ? -mx : mx)) mx = pads[i].lx;
			if ((pads[i].ly < 0 ? -pads[i].ly : pads[i].ly) >
			    (my < 0 ? -my : my)) my = pads[i].ly;
			if ((pads[i].rx < 0 ? -pads[i].rx : pads[i].rx) >
			    (sx < 0 ? -sx : sx)) sx = pads[i].rx;
			if ((pads[i].ry < 0 ? -pads[i].ry : pads[i].ry) >
			    (sy < 0 ? -sy : sy)) sy = pads[i].ry;
		}

		if (mx != 0 || my != 0) {
			carry_x += mx * MOUSE_MAX_PXS * TICK_MS / 1000.0;
			carry_y += my * MOUSE_MAX_PXS * TICK_MS / 1000.0;

			/* Whole pixels out, remainder kept. */
			double dx = (double)(long)carry_x;
			double dy = (double)(long)carry_y;
			carry_x -= dx;
			carry_y -= dy;
			if (dx != 0 || dy != 0)
				ptr_motion(&w, dx, dy);
		} else {
			carry_x = carry_y = 0;
		}

		if (sy != 0) {
			ptr_axis(&w, WL_POINTER_AXIS_VERTICAL_SCROLL,
				 sy * SCROLL_MAX_PXS * TICK_MS / 1000.0,
				 WL_POINTER_AXIS_SOURCE_FINGER);
			scrolling_v = true;
		} else if (scrolling_v) {
			ptr_axis_stop(&w, WL_POINTER_AXIS_VERTICAL_SCROLL);
			scrolling_v = false;
		}

		if (sx != 0) {
			ptr_axis(&w, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
				 sx * SCROLL_MAX_PXS * TICK_MS / 1000.0,
				 WL_POINTER_AXIS_SOURCE_FINGER);
			scrolling_h = true;
		} else if (scrolling_h) {
			ptr_axis_stop(&w, WL_POINTER_AXIS_HORIZONTAL_SCROLL);
			scrolling_h = false;
		}

		for (int i = 0; i < count; i++) {
			mpad_t *p = &pads[i];
			if ((!p->hat_x && !p->hat_y) || !p->wheel_at)
				continue;
			if (now < p->wheel_at)
				continue;
			if (p->hat_y)
				ptr_axis(&w, WL_POINTER_AXIS_VERTICAL_SCROLL,
					 p->hat_y * WHEEL_NOTCH,
					 WL_POINTER_AXIS_SOURCE_WHEEL);
			if (p->hat_x)
				ptr_axis(&w, WL_POINTER_AXIS_HORIZONTAL_SCROLL,
					 p->hat_x * WHEEL_NOTCH,
					 WL_POINTER_AXIS_SOURCE_WHEEL);
			/* One step, then a pause, then a steady stream — the
			 * same shape as a held key, and for the same reason:
			 * without the pause a tap scrolls half a page. */
			p->wheel_at = now + (p->wheel_repeat ? WHEEL_RATE_MS
							     : WHEEL_DELAY_MS);
			p->wheel_repeat = true;
		}

	}

	/* Let go of everything, in this order. See the comment above stop_now:
	 * a button left down here is a drag the next window cannot end. */
	for (int q = 0; q < (int)(sizeof(buttons) / sizeof(buttons[0])); q++)
		if (down[q] > 0)
			ptr_button(&w, buttons[q], false);
	if (scrolling_v)
		ptr_axis_stop(&w, WL_POINTER_AXIS_VERTICAL_SCROLL);
	if (scrolling_h)
		ptr_axis_stop(&w, WL_POINTER_AXIS_HORIZONTAL_SCROLL);

	wl_display_flush(w.display);
	wl_display_roundtrip(w.display);	/* …and make sure it arrived */

	for (int i = 0; i < count; i++)
		close(pads[i].fd);

	zwlr_virtual_pointer_v1_destroy(w.ptr);
	zwlr_virtual_pointer_manager_v1_destroy(w.mgr);
	if (w.seat)
		wl_seat_destroy(w.seat);
	for (int i = 0; i < w.n_outs; i++)
		wl_output_destroy(w.outs[i].out);
	wl_registry_destroy(w.registry);
	wl_display_disconnect(w.display);
	return EX_OK;
}
