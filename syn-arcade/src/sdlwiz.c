/*
 * sdlwiz.c — `syn-arcade map learn`: press the buttons, get a mapping.
 *
 * ── Why this exists, against what sdlmap.c used to say ──────────────────────
 *
 * sdlmap.c argued that mappings are PASTED and not generated: producing one
 * means watching somebody press seventeen controls in a known order, that is a
 * wizard, antimicrox and the SDL project's own tool both do it well, and a
 * worse copy of a solved thing is not worth writing.
 *
 * Every clause of that is true and the conclusion was still wrong, because of
 * where it left the person holding the pad. The window listed mappings it could
 * not create and told them to go and find a program that could — so the one
 * tab in this application that exists for a broken controller was the one tab
 * that could not fix one. A settings panel that ends in "install something
 * else" has not been designed, it has been deferred.
 *
 * So the wizard is here. What is NOT reimplemented is the part sdlmap.c was
 * really warning about:
 *
 * ── ⚠ SDL COMPUTES THE GUID AND THE INDICES, NEVER THIS FILE ────────────────
 *
 * A mapping line names controls by SDL's own JOYSTICK indices — `b3`, `a1`,
 * `h0.2` — and is keyed on SDL's own GUID for the device. Neither is derivable
 * from evdev by looking: the GUID folds the bus, vendor, product and version
 * with a CRC of the name, and the button indices come out of the order SDL's
 * Linux backend walks the key bitmap in, which is not the order the bits are
 * in. Working either of them out here would be reimplementing SDL's internals
 * against SDL's internals, and getting it wrong FAILS SILENTLY: SDL declines
 * the mapping, the pad behaves exactly as it did before, and nothing anywhere
 * says why — which is the very failure the person is trying to escape.
 *
 * So this asks SDL. It opens the pad as an SDL_Joystick, reads SDL's events,
 * and writes down SDL's numbers. The wizard is ours; every fact in the line it
 * produces comes from the library that will read it back.
 *
 * ── dlopen, so libSDL3 is not a dependency of anything else here ────────────
 *
 * meson.build argues at length that libSDL has no business being linked into a
 * tool that must work over SSH, and that argument survives: this is the only
 * command in the binary that needs SDL, and it is the one command that cannot
 * be run without a controller in somebody's hands anyway.
 *
 * So SDL is opened at RUN TIME and the package lists it as an optdepend. The
 * headers are used at build time for the structures and the enums — an
 * SDL_Event laid out by hand would be a silent ABI trap the first time SDL
 * added a field — but nothing links against the library, and every other
 * command works on a machine that has never had SDL installed.
 *
 * ── A stream, because a wizard is a conversation ────────────────────────────
 *
 * `map learn` prints one record per thing that happens and reads one word per
 * line on stdin — `skip`, `back`, `cancel`. That is the same shape as `big
 * nav`, and for the same reason: the graphical wizard and the terminal one are
 * then the same program with a different reader, so testing one tests both.
 *
 * ⚠ EVERY ROW IS FLUSHED. stdout to a pipe is block-buffered, so a wizard that
 * did not flush would sit silent until it finished — which for a program whose
 * whole job is to say "press X now" is indistinguishable from a hang.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "arcade.h"
#include "i18n.h"

#include <dlfcn.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <SDL3/SDL.h>

/* ── the library, at run time ────────────────────────────────────────────── */

/*
 * ⚠ NAMED WITHOUT THE SDL_ PREFIX on purpose. The headers are included for the
 * types, so `SDL_Init` is already declared as a function; a member of the same
 * name would compile but read as though the real symbol were being called, and
 * the whole point here is that it is not.
 */
static struct {
	void *lib;

	bool            (*Init)(SDL_InitFlags);
	void            (*Quit)(void);
	bool            (*SetHint)(const char *, const char *);
	const char     *(*GetError)(void);
	void            (*free)(void *);

	SDL_JoystickID *(*GetJoysticks)(int *);
	SDL_Joystick   *(*OpenJoystick)(SDL_JoystickID);
	void            (*CloseJoystick)(SDL_Joystick *);
	const char     *(*GetJoystickName)(SDL_Joystick *);
	SDL_GUID        (*GetJoystickGUID)(SDL_Joystick *);
	void            (*GUIDToString)(SDL_GUID, char *, int);
	int             (*GetNumJoystickAxes)(SDL_Joystick *);
	Sint16          (*GetJoystickAxis)(SDL_Joystick *, int);

	bool            (*PollEvent)(SDL_Event *);
} sdl;

#define SDL_SYM(name)                                                          \
	do {                                                                   \
		*(void **)(&sdl.name) = dlsym(sdl.lib, "SDL_" #name);          \
		if (!sdl.name) {                                               \
			fprintf(stderr, _("syn-arcade: %s has no SDL_%s\n"),   \
				SDL_SONAME, #name);                            \
			dlclose(sdl.lib);                                      \
			sdl.lib = NULL;                                        \
			return false;                                          \
		}                                                              \
	} while (0)

/*
 * ⚠ THE SONAME, not "libSDL3.so". The bare name is the DEVELOPMENT symlink and
 * it belongs to the headers package — a machine with SDL installed but not its
 * headers has the library this needs and not the name, so opening the plain
 * one would report SDL as missing on a box that is running SDL games.
 */
#define SDL_SONAME "libSDL3.so.0"

static bool sdl_load(void)
{
	if (sdl.lib)
		return true;

	sdl.lib = dlopen(SDL_SONAME, RTLD_LAZY | RTLD_LOCAL);
	if (!sdl.lib) {
		fprintf(stderr,
		 _("syn-arcade: SDL is not installed, and the mapping this writes\n"
		   "            is SDL's own format — its numbering has to come\n"
		   "            from the library that will read it back.\n"
		   "            synpkg install sdl3\n"));
		return false;
	}

	SDL_SYM(Init);
	SDL_SYM(Quit);
	SDL_SYM(SetHint);
	SDL_SYM(GetError);
	SDL_SYM(free);
	SDL_SYM(GetJoysticks);
	SDL_SYM(OpenJoystick);
	SDL_SYM(CloseJoystick);
	SDL_SYM(GetJoystickName);
	SDL_SYM(GetJoystickGUID);
	SDL_SYM(GUIDToString);
	SDL_SYM(GetNumJoystickAxes);
	SDL_SYM(GetJoystickAxis);
	SDL_SYM(PollEvent);
	return true;
}

/* ── the controls, in the order somebody is asked for them ───────────────── */

/*
 * ⚠ THE ORDER IS THE DESIGN. It walks the pad the way a hand does — the four
 * face buttons, then out to the shoulders, then the middle, then the sticks,
 * then the d-pad — so somebody can follow it without reading. An order that
 * jumped from A to the right stick and back would be answered wrongly by
 * people who are not looking at the screen, which on a sofa is most of them.
 *
 * The `axis` flag says what a press MEANS here, and it is not decoration: a
 * control that wants a whole axis records `a3`, one that wants a direction
 * records `+a3` or `-a3`, and a stick pushed the wrong way round records
 * `a3~`. Getting that distinction wrong produces a mapping SDL accepts and
 * that plays with the stick inverted.
 */
typedef enum { CTL_BUTTON, CTL_AXIS, CTL_TRIGGER } ctl_kind_t;

static const struct {
	const char *sdl;	/* the name SDL knows it by, exactly */
	const char *prompt;
	ctl_kind_t  kind;
} wiz_controls[] = {
	/* ⛔ THE FIRST COLUMN IS SDL'S OWN NAME AND IS NEVER MARKED — it is
	 * written into the mapping string SDL reads back, and a translated one
	 * is a mapping no game understands. The second is a sentence somebody
	 * reads, and it travels to the WINDOW as the record's `detail` field,
	 * so it is marked here: in the catalog, unchanged in the record, looked
	 * up at the draw site. */
	{ "a",             N_("the BOTTOM face button (A, or cross)"),       CTL_BUTTON },
	{ "b",             N_("the RIGHT face button (B, or circle)"),       CTL_BUTTON },
	{ "x",             N_("the LEFT face button (X, or square)"),        CTL_BUTTON },
	{ "y",             N_("the TOP face button (Y, or triangle)"),       CTL_BUTTON },
	{ "leftshoulder",  N_("the left shoulder button (LB, or L1)"),       CTL_BUTTON },
	{ "rightshoulder", N_("the right shoulder button (RB, or R1)"),      CTL_BUTTON },
	{ "lefttrigger",   N_("the left trigger, all the way in (LT, L2)"),  CTL_TRIGGER },
	{ "righttrigger",  N_("the right trigger, all the way in (RT, R2)"), CTL_TRIGGER },
	{ "back",          N_("Back, Select or View"),                       CTL_BUTTON },
	{ "start",         N_("Start or Menu"),                              CTL_BUTTON },
	{ "guide",         N_("the Guide or Home button, if it has one"),    CTL_BUTTON },
	{ "leftstick",     N_("the left stick pressed IN (L3)"),             CTL_BUTTON },
	{ "rightstick",    N_("the right stick pressed IN (R3)"),            CTL_BUTTON },
	{ "leftx",         N_("the left stick pushed RIGHT"),                CTL_AXIS },
	{ "lefty",         N_("the left stick pushed DOWN"),                 CTL_AXIS },
	{ "rightx",        N_("the right stick pushed RIGHT"),               CTL_AXIS },
	{ "righty",        N_("the right stick pushed DOWN"),                CTL_AXIS },
	{ "dpup",          N_("the d-pad UP"),                               CTL_BUTTON },
	{ "dpdown",        N_("the d-pad DOWN"),                             CTL_BUTTON },
	{ "dpleft",        N_("the d-pad LEFT"),                             CTL_BUTTON },
	{ "dpright",       N_("the d-pad RIGHT"),                            CTL_BUTTON },
};

#define WIZ_COUNT ((int)(sizeof(wiz_controls) / sizeof(wiz_controls[0])))

/*
 * How far a stick has to move to count as a deliberate push.
 *
 * ⚠ HALF THE RANGE, and generously so. A worn stick rests off centre and a
 * cheap one rattles a few thousand units on its own; a threshold tight enough
 * to feel responsive is a threshold that records the resting noise of the
 * first axis SDL happens to report and calls it an answer. There is no hurry
 * here — somebody is deliberately pushing a stick as far as it goes.
 */
#define WIZ_AXIS_MOVE 16000

/* Resting near the bottom of the range means a trigger that reads -32768 when
 * it is out, which is the common Linux shape and wants the WHOLE axis. One
 * resting near zero is a half axis and wants a sign. */
#define WIZ_AXIS_LOW  (-24000)

/* ── the stream ──────────────────────────────────────────────────────────── */

static bool wiz_rec;		/* records for the window, or words for a person */

static void wiz_row(const char *event, int index, const char *control,
		    const char *detail, const char *binding)
{
	char idx[16], tot[16];
	snprintf(idx, sizeof(idx), "%d", index);
	snprintf(tot, sizeof(tot), "%d", WIZ_COUNT);

	if (wiz_rec)
		rec_row(6, event, idx, tot, control ? control : "",
			detail ? detail : "", binding ? binding : "");
	/* ⚠ `detail` IS TRANSLATED ONLY IN THE "ask" BRANCH. It carries a marked
	 * prompt there and a CONTROLLER NAME off a USB descriptor in the "pad"
	 * one, and gettext on a device name would silently return a translation
	 * for any string that happened to match a msgid. `control` is SDL's own
	 * identifier throughout and is never translated at all. */
	else if (!strcmp(event, "ask"))
		printf(_("[%d/%d] %-14s press %s\n"), index + 1, WIZ_COUNT,
		       control, detail ? _(detail) : "");
	else if (!strcmp(event, "bound"))
		printf(_("        %-14s = %s\n"), control, binding);
	else if (!strcmp(event, "skipped"))
		printf(_("        %-14s — skipped\n"), control);
	else if (!strcmp(event, "taken"))
		printf(_("        that is already %s. Try another control.\n"),
		       binding);
	else if (detail && *detail)
		printf("%s\n", detail);

	/* ⚠ EVERY ROW. A pipe is block-buffered, and a wizard that says "press
	 * X" only once it has finished is a wizard nobody can follow. */
	fflush(stdout);
}

/* ── what the person typed while we were waiting ─────────────────────────── */

typedef enum { CMD_NONE, CMD_SKIP, CMD_BACK, CMD_CANCEL } wiz_cmd_t;

/*
 * ⚠ NON-BLOCKING, and polled in the same loop as the pad. A wizard that read
 * stdin between events would be a wizard that cannot be cancelled while it is
 * waiting for a button — which is exactly when somebody wants to cancel it,
 * because waiting for a button is what it does when the button does not work.
 */
static wiz_cmd_t wiz_stdin(void)
{
	/*
	 * ⚠ AN ENDED STDIN HAS TO BE REMEMBERED. poll() on a descriptor at end
	 * of file returns READY every single time — POLLIN with a read of zero,
	 * or POLLHUP — so a loop that merely ignored the empty read would come
	 * straight back and ask again, and `map learn </dev/null` would sit
	 * there spinning a core with a wizard on screen that looks like it is
	 * politely waiting. Same family as the FIFO opened read-only in big
	 * screen mode's listener.
	 *
	 * It is not an error: pressing buttons is still a complete way to use
	 * this. It only means the words — skip, back, cancel — are no longer
	 * coming, so stop listening for them.
	 */
	static bool ended = false;
	if (ended)
		return CMD_NONE;

	struct pollfd p = { .fd = STDIN_FILENO, .events = POLLIN };
	if (poll(&p, 1, 0) <= 0)
		return CMD_NONE;

	char buf[64];
	ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
	if (n == 0) {
		ended = true;
		return CMD_NONE;
	}
	if (n < 0)
		return CMD_NONE;
	buf[n] = '\0';

	char *s = trim(buf);
	/* A bare Enter at a terminal is the obvious way to say "this pad has
	 * no such button", and typing the word is the obvious way in a pipe.
	 * Both mean skip. */
	if (!*s || !strcasecmp(s, "skip") || !strcasecmp(s, "s"))
		return CMD_SKIP;
	if (!strcasecmp(s, "back") || !strcasecmp(s, "b"))
		return CMD_BACK;
	if (!strcasecmp(s, "cancel") || !strcasecmp(s, "quit") ||
	    !strcasecmp(s, "q"))
		return CMD_CANCEL;
	return CMD_NONE;
}

/* ── picking the pad ─────────────────────────────────────────────────────── */

static SDL_Joystick *wiz_open(const char *want, char *name, size_t namen,
			      char *guid, size_t guidn)
{
	int n = 0;
	SDL_JoystickID *ids = sdl.GetJoysticks(&n);
	if (!ids || n == 0) {
		if (ids) sdl.free(ids);
		fputs(_("syn-arcade: no controller. Plug one in — this reads the "
		        "pad you are\n            holding, so there is nothing to "
		        "learn without one.\n"), stderr);
		return NULL;
	}

	SDL_Joystick *chosen = NULL;
	for (int i = 0; i < n && !chosen; i++) {
		SDL_Joystick *j = sdl.OpenJoystick(ids[i]);
		if (!j)
			continue;
		const char *nm = sdl.GetJoystickName(j);
		if (!want || !*want ||
		    (nm && strcasestr(nm, want)) ||
		    (i == atoi(want) - 1 && want[0] >= '1' && want[0] <= '9')) {
			chosen = j;
			snprintf(name, namen, "%s", nm ? nm : "Controller");
			sdl.GUIDToString(sdl.GetJoystickGUID(j), guid,
					 (int)guidn);
			break;
		}
		sdl.CloseJoystick(j);
	}
	sdl.free(ids);

	if (!chosen)
		fprintf(stderr, _("syn-arcade: no controller matching '%s'\n"),
			want ? want : "");
	return chosen;
}

/* ── one control ─────────────────────────────────────────────────────────── */

static long long wiz_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*
 * The pad's axes as they were when the current question was asked, so both the
 * capture and the settle below are measuring against the same rest — a stick
 * that sits off centre must not read as pushed, and must not read as still
 * pushed once it is let go.
 */
#define WIZ_AXES_MAX 64
static Sint16 wiz_rest[WIZ_AXES_MAX];
static int    wiz_naxes;

/*
 * Wait for what was just pressed to be LET GO.
 *
 * ⚠ THIS IS THE BUG THE RIG FOUND, AND IT WAS INVISIBLE BY HAND. Draining the
 * queue before asking is not enough, because a release does not arrive with
 * the press — it arrives a moment later, while the next question is already on
 * screen. So the wizard recorded a trigger correctly, then took that trigger's
 * RETURN TO REST as the answer to the next control, and every control from
 * there on was one press behind: righttrigger came out as the left one's
 * release, the face buttons landed on the sticks, and the finished line was
 * well-formed and completely wrong.
 *
 * A person doing this by hand never sees it. They read "press RT", press it,
 * and the wizard has already moved on twice — which reads as the wizard being
 * fast, not as the wizard being wrong, and the mapping it produces is exactly
 * the kind of quietly-wrong that SDL will happily load.
 *
 * ⚠ IT GIVES UP after a few seconds rather than waiting forever. An axis that
 * never returns to rest is a stuck stick or a trigger with a dead spring, and
 * a wizard that hangs on one is worse than one that carries on.
 */
static void wiz_settle(SDL_Joystick *j, const char *bind)
{
	long long deadline = wiz_now_ms() + 3000;
	SDL_Event ev;

	while (wiz_now_ms() < deadline) {
		if (!sdl.PollEvent(&ev)) {
			/* Nothing pending. If the thing we recorded is an axis,
			 * ask its value directly — a stick let go before this
			 * loop started has already sent its last event. */
			if (bind[0] != 'b' && bind[0] != 'h') {
				bool moved = false;
				for (int a = 0; a < wiz_naxes && !moved; a++) {
					int d = sdl.GetJoystickAxis(j, a)
					      - wiz_rest[a];
					if (d > WIZ_AXIS_MOVE / 2 ||
					    d < -WIZ_AXIS_MOVE / 2)
						moved = true;
				}
				if (!moved)
					return;
			}
			struct timespec ts = { 0, 8 * 1000 * 1000 };
			nanosleep(&ts, NULL);
			continue;
		}

		if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_UP && bind[0] == 'b')
			return;
		if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION &&
		    ev.jhat.value == SDL_HAT_CENTERED && bind[0] == 'h')
			return;
		if (ev.type == SDL_EVENT_JOYSTICK_AXIS_MOTION &&
		    bind[0] != 'b' && bind[0] != 'h') {
			int a = ev.jaxis.axis;
			if (a >= 0 && a < wiz_naxes) {
				int d = ev.jaxis.value - wiz_rest[a];
				if (d < WIZ_AXIS_MOVE / 2 && d > -WIZ_AXIS_MOVE / 2)
					return;
			}
		}
	}
}

/*
 * Wait for a deliberate press and write down what SDL called it.
 *
 * ⚠ WHAT IS ALREADY HELD DOES NOT COUNT. A trigger resting at the bottom of
 * its range, a d-pad hat that has not centred, a button still down from the
 * previous question — each of them arrives as an event the instant SDL is
 * asked, and a loop that took the first event it saw would fill the next four
 * controls from one press and finish the wizard in under a second with a
 * mapping that binds everything to b0. So the resting state is sampled first
 * and a press only counts as an ANSWER once it has been released.
 */
static bool wiz_capture(SDL_Joystick *j, int idx, char *out, size_t outn,
			wiz_cmd_t *cmd)
{
	wiz_naxes = sdl.GetNumJoystickAxes(j);
	if (wiz_naxes > WIZ_AXES_MAX)
		wiz_naxes = WIZ_AXES_MAX;
	for (int a = 0; a < wiz_naxes; a++)
		wiz_rest[a] = sdl.GetJoystickAxis(j, a);

	/* Drain whatever is queued from the previous control before asking:
	 * the release of the last press is in there, and so is every frame of
	 * a stick on its way back to centre. */
	SDL_Event ev;
	while (sdl.PollEvent(&ev))
		;

	wiz_row("ask", idx, wiz_controls[idx].sdl, wiz_controls[idx].prompt, "");

	for (;;) {
		*cmd = wiz_stdin();
		if (*cmd != CMD_NONE)
			return false;

		if (!sdl.PollEvent(&ev)) {
			struct timespec ts = { 0, 8 * 1000 * 1000 };
			nanosleep(&ts, NULL);
			continue;
		}

		switch (ev.type) {
		case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
			snprintf(out, outn, "b%d", ev.jbutton.button);
			return true;

		case SDL_EVENT_JOYSTICK_HAT_MOTION:
			if (ev.jhat.value == SDL_HAT_CENTERED)
				break;
			/* ⚠ THE MASK, NOT THE INDEX. SDL writes a hat as
			 * h<hat>.<bitmask> — up is 1, right 2, down 4, left 8 —
			 * and a diagonal is two bits at once, which is not a
			 * direction anybody meant to press. */
			if (ev.jhat.value != SDL_HAT_UP &&
			    ev.jhat.value != SDL_HAT_RIGHT &&
			    ev.jhat.value != SDL_HAT_DOWN &&
			    ev.jhat.value != SDL_HAT_LEFT)
				break;
			snprintf(out, outn, "h%d.%d", ev.jhat.hat,
				 ev.jhat.value);
			return true;

		case SDL_EVENT_JOYSTICK_AXIS_MOTION: {
			int a = ev.jaxis.axis;
			if (a < 0 || a >= wiz_naxes)
				break;
			int base = wiz_rest[a];
			int moved = ev.jaxis.value - base;
			if (moved > -WIZ_AXIS_MOVE && moved < WIZ_AXIS_MOVE)
				break;

			switch (wiz_controls[idx].kind) {
			case CTL_AXIS:
				/* ⚠ A stick pushed RIGHT that reads NEGATIVE is
				 * wired the other way round, and SDL has a
				 * spelling for that. Recording a plain `a0`
				 * here produces a mapping it accepts and that
				 * plays with the stick inverted — which is the
				 * fault people are here to fix, reintroduced by
				 * the tool fixing it. */
				snprintf(out, outn, "a%d%s", a,
					 moved < 0 ? "~" : "");
				break;
			case CTL_TRIGGER:
				/* A trigger resting at the bottom of its range
				 * is a whole axis; one resting at centre is a
				 * half axis and needs its sign. */
				snprintf(out, outn, "%sa%d",
					 base <= WIZ_AXIS_LOW ? ""
					 : (moved < 0 ? "-" : "+"), a);
				break;
			case CTL_BUTTON:
				snprintf(out, outn, "%ca%d",
					 moved < 0 ? '-' : '+', a);
				break;
			}
			return true;
		}

		case SDL_EVENT_JOYSTICK_REMOVED:
			wiz_row("error", idx, "", "the controller was unplugged",
				"");
			*cmd = CMD_CANCEL;
			return false;

		default:
			break;
		}
	}
}

/* ── the wizard ──────────────────────────────────────────────────────────── */

int map_learn(int argc, char **argv)
{
	const char *want = NULL;
	wiz_rec = false;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--rec"))
			wiz_rec = true;
		else if (!strcmp(argv[i], "--pad") && i + 1 < argc)
			want = argv[++i];
		else if (argv[i][0] != '-')
			want = argv[i];
	}

	if (!sdl_load())
		return EX_FAIL;

	/* ⚠ JOYSTICK ONLY, and no video. There is no window here and there is
	 * not going to be one — this runs over SSH as readily as it runs under
	 * the window — so nothing may be initialised that needs a display.
	 * Without the hint SDL delivers events only to a focused window, and
	 * a program with no window never has focus. */
	sdl.SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if (!sdl.Init(SDL_INIT_JOYSTICK)) {
		fprintf(stderr, _("syn-arcade: SDL would not start: %s\n"),
			sdl.GetError());
		return EX_FAIL;
	}

	char name[256] = "", guid[64] = "";
	SDL_Joystick *j = wiz_open(want, name, sizeof(name), guid, sizeof(guid));
	if (!j) {
		sdl.Quit();
		return EX_FAIL;
	}

	if (wiz_rec)
		rec_row(6, "event", "index", "total", "control", "detail",
			"binding");
	wiz_row("pad", 0, guid, name, "");
	if (!wiz_rec)
		/* ⚠ `back` and `cancel` are what somebody TYPES; they stay inside
		 * the marked sentence so it reads naturally, and a translator
		 * leaves the two words as they stand. */
		puts(_("\nPress each control as it is named. Enter skips one this "
		       "pad does not\nhave, `back` re-does the last, `cancel` "
		       "stops.\n"));

	char bind[WIZ_COUNT][32];
	for (int i = 0; i < WIZ_COUNT; i++)
		bind[i][0] = '\0';

	for (int i = 0; i < WIZ_COUNT; ) {
		char got[32] = "";
		wiz_cmd_t cmd = CMD_NONE;

		if (!wiz_capture(j, i, got, sizeof(got), &cmd)) {
			if (cmd == CMD_CANCEL) {
				wiz_row("cancelled", i, "", "nothing was written",
					"");
				sdl.CloseJoystick(j);
				sdl.Quit();
				return EX_FAIL;
			}
			if (cmd == CMD_BACK) {
				if (i > 0) bind[--i][0] = '\0';
				continue;
			}
			bind[i][0] = '\0';
			wiz_row("skipped", i, wiz_controls[i].sdl,
				wiz_controls[i].prompt, "");
			i++;
			continue;
		}

		/*
		 * ⚠ TWO CONTROLS ON ONE BUTTON IS A MIS-PRESS, ALWAYS. SDL
		 * accepts the line, and the pad then has a B that is also an A
		 * — which reads in a game as a button that does two things and
		 * is very hard to trace back to this moment. Say which control
		 * already has it and ask again, rather than recording it and
		 * being right about what was pressed.
		 */
		int clash = -1;
		for (int k = 0; k < i; k++)
			if (bind[k][0] && !strcmp(bind[k], got)) { clash = k; break; }
		if (clash >= 0) {
			wiz_row("taken", i, wiz_controls[i].sdl,
				wiz_controls[clash].sdl, got);
			continue;
		}

		snprintf(bind[i], sizeof(bind[i]), "%s", got);
		wiz_row("bound", i, wiz_controls[i].sdl, wiz_controls[i].prompt,
			bind[i]);
		/* ⚠ BEFORE THE NEXT QUESTION, never after it. The release is
		 * the answer the next control would otherwise be given. */
		wiz_settle(j, bind[i]);
		i++;
	}

	sdl.CloseJoystick(j);
	sdl.Quit();

	/*
	 * ⚠ platform:Linux, and it is not garnish. SDL only applies a mapping
	 * whose platform matches, and a line without the field is loaded and
	 * silently never used — the single most common way a mapping copied
	 * from anywhere fails. mapping_ok() refuses one without it; this is the
	 * side that has to put it there.
	 */
	char line[2048];
	int len = snprintf(line, sizeof(line), "%s,%s", guid, name);
	int bound = 0;
	for (int i = 0; i < WIZ_COUNT && len > 0 && len < (int)sizeof(line); i++) {
		if (!bind[i][0])
			continue;
		len += snprintf(line + len, sizeof(line) - (size_t)len, ",%s:%s",
				wiz_controls[i].sdl, bind[i]);
		bound++;
	}
	if (len > 0 && len < (int)sizeof(line))
		len += snprintf(line + len, sizeof(line) - (size_t)len,
				",platform:Linux,");

	if (bound == 0) {
		wiz_row("cancelled", WIZ_COUNT, "",
			"every control was skipped — nothing was written", "");
		return EX_FAIL;
	}
	if (len <= 0 || len >= (int)sizeof(line)) {
		wiz_row("error", WIZ_COUNT, "",
			"that came out longer than a mapping line may be", "");
		return EX_FAIL;
	}

	wiz_row("done", WIZ_COUNT, guid, name, line);

	/* ⚠ Written through the SAME map_add() the paste path uses, so the
	 * refusals, the replace-don't-append rule and the file's header are one
	 * implementation. A wizard that wrote the file itself would be a second
	 * writer to get out of step with the first. */
	return map_add_line(line);
}
