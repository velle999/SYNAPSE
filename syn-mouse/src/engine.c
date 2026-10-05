/* engine.c — what a binding does.
 *
 * Button events and a clock in, key presses out, and nothing else: no device,
 * no file, no time of its own. That is what lets tests/engine_test.c drive
 * every mode through a fake clock and read back exactly what would have been
 * sent, without a mouse, a uinput device or a compositor anywhere near it.
 *
 * ⛔ EVERY PRESS IS RELEASED. The output devices are real keyboards and mice
 * to the compositor; a key this engine pressed and forgot is a key held down
 * in whatever window has focus until something else lets go of it. So every
 * path that ends a state — the button coming up, the profile leaving, a
 * reload, a stop, a mouse unplugged — goes through the same release.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synmouse.h"

#include <stdlib.h>
#include <string.h>

/* How long a tap holds its keys down. A game samples input once a frame, and
 * a press that came and went between two frames is a press it never saw:
 * 40 ms is over two frames at 60 fps. */
#define TAP_MS 40u

void engine_init(engine_t *e, engine_io_t io)
{
	memset(e, 0, sizeof *e);
	e->io = io;
	e->active = -1;
	e->tap_ms = TAP_MS;
}

void engine_free(engine_t *e)
{
	free(e->slots);
	e->slots = NULL;
	e->nprof = 0;
}

static slot_t *slot(engine_t *e, int prof, int in)
{
	return &e->slots[prof * IN_COUNT + in];
}

static const action_t *act(const engine_t *e, int prof, int in)
{
	return &e->cfg->p[prof].act[in];
}

/* ⚠ ONE FRAME PER KEY. A combo's keys go out in order with a report after
 * each, so `shift+1` reaches the game as shift-then-1 and not as two keys in
 * one frame whose order the receiving side is free to pick. */
static void press(engine_t *e, int dev, const combo_t *c)
{
	for (int i = 0; i < c->n; i++) {
		e->io.emit(e->io.ctx, dev, c->code[i], 1);
		e->io.flush(e->io.ctx, dev);
	}
}

static void release(engine_t *e, int dev, const combo_t *c)
{
	for (int i = c->n - 1; i >= 0; i--) {
		e->io.emit(e->io.ctx, dev, c->code[i], 0);
		e->io.flush(e->io.ctx, dev);
	}
}

static unsigned tap_len(const engine_t *e, const action_t *a)
{
	unsigned t = e->tap_ms;
	/* A tap shorter than its interval, always, so a release is never still
	 * pending when the next press is due. */
	if (act_has_interval(a->kind) && a->interval_ms / 2 < t) t = a->interval_ms / 2;
	return t ? t : 1;
}

static void tap_start(engine_t *e, slot_t *s, const action_t *a, int64_t now)
{
	press(e, s->dev, &a->combo);
	s->tap_down = true;
	s->up_ms = now + tap_len(e, a);
}

static void tap_end(engine_t *e, slot_t *s, const action_t *a)
{
	if (!s->tap_down) return;
	release(e, s->dev, &a->combo);
	s->tap_down = false;
	s->up_ms = 0;
}

/* Let go of everything this slot has down, and stop its timer. `on` is left
 * alone: whether a toggle is switched on is the caller's question. */
static void quiesce(engine_t *e, int prof, int in)
{
	slot_t *s = slot(e, prof, in);
	const action_t *a = act(e, prof, in);
	tap_end(e, s, a);
	if (s->held && a->kind == ACT_KEY) release(e, s->dev, &a->combo);
	if (s->latched) release(e, s->dev, &a->combo);
	s->held = false;
	s->latched = false;
	s->next_ms = 0;
}

static void all_off(engine_t *e, bool tell)
{
	for (int p = 0; p < e->nprof; p++) {
		for (int in = 0; in < IN_COUNT; in++) {
			slot_t *s = slot(e, p, in);
			if (p == e->active) quiesce(e, p, in);
			if (s->on) {
				s->on = false;
				if (tell && e->io.flipped) e->io.flipped(e->io.ctx, in, act(e, p, in), false);
			}
		}
	}
}

void engine_unload(engine_t *e, bool tell)
{
	if (e->cfg) all_off(e, tell);
	e->cfg = NULL;
	e->active = -1;
}

void engine_set_on(engine_t *e, int prof, int in)
{
	if (prof < 0 || prof >= e->nprof || in < 0 || in >= IN_COUNT) return;
	const action_t *a = act(e, prof, in);
	if (a->kind != ACT_TOGGLE && a->kind != ACT_LATCH) return;
	slot_t *s = slot(e, prof, in);
	s->on = true;
	s->dev = -1;
	/* In force already: start it now, as engine_activate() would have. */
	if (prof == e->active) {
		if (a->kind == ACT_TOGGLE) s->next_ms = 1;
		else { press(e, s->dev, &a->combo); s->latched = true; }
	}
}

void engine_load(engine_t *e, const config_t *cfg, int64_t now)
{
	(void)now;
	/* ⚠ THE OLD CONFIG IS NOT TOUCHED HERE — the caller has usually just
	 * overwritten it in place. engine_unload() is what lets go of the old
	 * one's keys, and has to run while that config is still the one the
	 * slots were built from. */
	free(e->slots);
	e->cfg = cfg;
	e->nprof = cfg->n;
	e->slots = calloc((size_t)(cfg->n ? cfg->n : 1) * IN_COUNT, sizeof *e->slots);
	if (!e->slots) die("out of memory");
	e->active = -1;
}

void engine_activate(engine_t *e, int prof, int64_t now)
{
	if (prof == e->active) return;
	if (prof >= e->nprof) prof = -1;

	if (e->active >= 0)
		for (int in = 0; in < IN_COUNT; in++) quiesce(e, e->active, in);

	e->active = prof;
	if (prof < 0) return;

	/* ⚠ A TOGGLE LEFT ON COMES BACK ON. Leaving the game for a browser pauses
	 * it — the keys must not go on landing in the browser — and returning
	 * picks it up where it was, because switching it off was never asked. */
	for (int in = 0; in < IN_COUNT; in++) {
		slot_t *s = slot(e, prof, in);
		const action_t *a = act(e, prof, in);
		if (!s->on) continue;
		if (a->kind == ACT_TOGGLE) s->next_ms = now;
		if (a->kind == ACT_LATCH) { press(e, s->dev, &a->combo); s->latched = true; }
	}
}

bool engine_bound(const engine_t *e, int in)
{
	return e->active >= 0 && in >= 0 && in < IN_COUNT &&
	       act(e, e->active, in)->kind != ACT_NONE;
}

static void flip(engine_t *e, slot_t *s, int in, const action_t *a, int dev, int64_t now)
{
	s->on = !s->on;
	if (s->on) {
		s->dev = dev;
		if (a->kind == ACT_TOGGLE) {
			tap_start(e, s, a, now);
			s->next_ms = now + a->interval_ms;
		} else {
			press(e, dev, &a->combo);
			s->latched = true;
		}
	} else {
		/* A tap already out finishes on its own schedule; only the next
		 * one is cancelled. */
		s->next_ms = 0;
		if (s->latched) { release(e, s->dev, &a->combo); s->latched = false; }
	}
	if (e->io.flipped) e->io.flipped(e->io.ctx, in, a, s->on);
}

bool engine_button(engine_t *e, int dev, int in, bool down, int64_t now)
{
	if (!engine_bound(e, in)) return false;
	slot_t *s = slot(e, e->active, in);
	const action_t *a = act(e, e->active, in);

	switch (a->kind) {
	case ACT_KEY:
		if (down && !s->held) {
			s->dev = dev;
			press(e, dev, &a->combo);
			s->held = true;
		} else if (!down && s->held) {
			release(e, s->dev, &a->combo);
			s->held = false;
		}
		break;
	case ACT_REPEAT:
		if (down && !s->held) {
			s->dev = dev;
			s->held = true;
			if (s->tap_down) tap_end(e, s, a);
			tap_start(e, s, a, now);
			s->next_ms = now + a->interval_ms;
		} else if (!down) {
			s->held = false;
			s->next_ms = 0;
		}
		break;
	case ACT_TOGGLE:
	case ACT_LATCH:
		if (down) flip(e, s, in, a, dev, now);
		break;
	default:
		break;
	}
	return true;
}

bool engine_wheel(engine_t *e, int dev, int in, int64_t now)
{
	if (!engine_bound(e, in)) return false;
	slot_t *s = slot(e, e->active, in);
	const action_t *a = act(e, e->active, in);

	switch (a->kind) {
	case ACT_KEY:
	case ACT_REPEAT:
		/* A notch has no length, so it is a tap: down, and up a moment later. */
		if (s->tap_down) tap_end(e, s, a);
		s->dev = dev;
		tap_start(e, s, a, now);
		break;
	case ACT_TOGGLE:
	case ACT_LATCH:
		flip(e, s, in, a, dev, now);
		break;
	default:
		break;
	}
	return true;
}

int64_t engine_tick(engine_t *e, int64_t now)
{
	int64_t next = -1;
	if (e->active < 0) return -1;

	for (int in = 0; in < IN_COUNT; in++) {
		slot_t *s = slot(e, e->active, in);
		/* Called on every frame of a grabbed mouse, over 268 inputs since keys
		 * joined them: a slot with no tap down and no press due has nothing
		 * here to do. */
		if (!s->tap_down && !s->next_ms) continue;
		const action_t *a = act(e, e->active, in);

		if (s->tap_down && now >= s->up_ms) tap_end(e, s, a);

		bool running = (a->kind == ACT_REPEAT && s->held) ||
		               (a->kind == ACT_TOGGLE && s->on);
		if (!running) s->next_ms = 0;
		if (s->next_ms && now >= s->next_ms) {
			tap_end(e, s, a);
			tap_start(e, s, a, now);
			/* ⚠ NO CATCH-UP BURST. A loop that stalled for three intervals
			 * fires once and carries on from now; three presses in one
			 * instant would be three presses nobody asked for. */
			s->next_ms += a->interval_ms;
			if (s->next_ms <= now) s->next_ms = now + a->interval_ms;
		}

		if (s->tap_down && (next < 0 || s->up_ms < next)) next = s->up_ms;
		if (s->next_ms && (next < 0 || s->next_ms < next)) next = s->next_ms;
	}
	return next;
}

void engine_stop(engine_t *e, int64_t now)
{
	(void)now;
	all_off(e, true);
}

void engine_device_gone(engine_t *e, int dev, int64_t now)
{
	(void)now;
	if (e->active < 0) return;
	for (int in = 0; in < IN_COUNT; in++) {
		slot_t *s = slot(e, e->active, in);
		const action_t *a = act(e, e->active, in);
		if (s->dev != dev) continue;
		/* A button held on that mouse will never send its release now. */
		if (s->held) {
			if (a->kind == ACT_KEY) release(e, s->dev, &a->combo);
			s->held = false;
			s->next_ms = 0;
		}
		/* ⚠ ONLY A COMBO WITH A MOUSE BUTTON IN IT STOPS. Its presses went to
		 * the twin of the mouse that is gone. A toggle pressing `1` goes to
		 * the keyboard device and has nothing to do with which mouse switched
		 * it on — a wireless mouse dropping out for a second must not cancel
		 * it. */
		if (combo_has_button(&a->combo) && (s->on || s->tap_down)) {
			tap_end(e, s, a);
			if (s->latched) { release(e, s->dev, &a->combo); s->latched = false; }
			s->next_ms = 0;
			if (s->on) {
				s->on = false;
				if (e->io.flipped) e->io.flipped(e->io.ctx, in, a, false);
			}
		}
	}
}

bool engine_is_on(const engine_t *e, int prof, int in)
{
	if (prof < 0 || prof >= e->nprof || in < 0 || in >= IN_COUNT) return false;
	return e->slots[prof * IN_COUNT + in].on;
}
