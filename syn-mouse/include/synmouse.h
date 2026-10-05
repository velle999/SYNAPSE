/*
 * synmouse.h — mouse buttons that do what you bind them to, per game.
 *
 * Three layers, kept apart so the middle one can be tested with no device:
 *
 *   config.c   the bindings file: profiles, what each button does
 *   engine.c   what a binding DOES — presses, repeats, toggles — given button
 *              events and a clock, emitting key events through callbacks
 *   daemon.c   the devices: grab the mouse while a profile is in force, feed
 *              the engine, forward everything it does not consume
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNMOUSE_H
#define SYNMOUSE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ── the inputs a binding can be put on ──────────────────────────────────── */

/* ⚠ "back" AND "forward" ARE THE THUMB BUTTONS, which the kernel calls
 * BTN_SIDE and BTN_EXTRA. Its own BTN_BACK and BTN_FORWARD are two further
 * buttons a few mice have, and they are button7 and button6 here. Naming the
 * thumb buttons after the kernel would have made the two everybody has the
 * two nobody recognises. */
/*
 * ⚠ AND A KEY THE MOUSE ITSELF SENDS IS AN INPUT TOO: `key:2`. A gaming
 * mouse's onboard memory — what the vendor's app writes — can make a button
 * send a keyboard key from the mouse's own keyboard interface instead of a
 * button from its pointer one. That button is never BTN_SIDE again until the
 * vendor's app says so, and on Linux there is no vendor's app; the key it sends
 * is the only handle there is. IN_KEY_FIRST + code, for codes 1 to 255.
 */
enum {
	IN_LEFT, IN_RIGHT, IN_MIDDLE, IN_BACK, IN_FORWARD,
	IN_BUTTON6, IN_BUTTON7, IN_BUTTON8,
	IN_WHEEL_UP, IN_WHEEL_DOWN, IN_WHEEL_LEFT, IN_WHEEL_RIGHT,
	IN_KEY_FIRST,
	IN_COUNT = IN_KEY_FIRST + 256
};
#define IN_FIRST_WHEEL IN_WHEEL_UP

const char *input_name(int in);              /* "back", "key:2" */
const char *input_label(int in);             /* N_("Thumb button (back)"); NULL for a key */
int  input_from_name(const char *s);         /* -1 if unknown */
int  input_from_code(uint16_t btn);          /* BTN_* -> IN_*, or -1 */
uint16_t input_code(int in);                 /* IN_* -> BTN_*, 0 for a wheel or a key */
static inline bool input_is_wheel(int in) { return in >= IN_FIRST_WHEEL && in < IN_KEY_FIRST; }
static inline bool input_is_key(int in)   { return in > IN_KEY_FIRST && in < IN_COUNT; }
/* KEY_* sent by the mouse -> its input, or -1 outside 1..255. */
static inline int input_from_key(unsigned code)
{ return code >= 1 && code <= 255 ? IN_KEY_FIRST + (int)code : -1; }
static inline uint16_t input_key(int in)
{ return input_is_key(in) ? (uint16_t)(in - IN_KEY_FIRST) : 0; }

/* ── what comes out: keys and mouse buttons ─────────────────────────────── */

#define COMBO_MAX 6
typedef struct {
	int      n;
	uint16_t code[COMBO_MAX];   /* pressed in this order, released in reverse */
} combo_t;

/* Parse "shift+1", "ctrl+mouse1", "code:30". 0 on success; on failure -1 and
 * *bad points at the token that was not a key. */
int  combo_parse(const char *s, combo_t *c, const char **bad, size_t *badlen);
/* The canonical spelling, into buf. */
void combo_format(const combo_t *c, char *buf, size_t n);
bool combo_has_button(const combo_t *c);

/* The key-name table, for `syn-mouse keys`. */
typedef struct { uint16_t code; const char *name; } keyname_t;
extern const keyname_t g_keynames[];
extern const size_t g_nkeynames;
const char *key_name(uint16_t code);         /* canonical, or NULL */
static inline bool code_is_button(uint16_t c) { return c >= 0x110 && c <= 0x117; }

/* ── bindings ────────────────────────────────────────────────────────────── */

typedef enum {
	ACT_NONE = 0,   /* not bound: the button does what it always did */
	ACT_KEY,        /* held while the button is held */
	ACT_REPEAT,     /* pressed every N seconds while the button is held */
	ACT_TOGGLE,     /* one click starts pressing it every N seconds, the next stops */
	ACT_LATCH,      /* one click holds it down, the next lets go */
	ACT_OFF,        /* the button does nothing at all */
	ACT_COUNT
} act_kind_t;

const char *act_name(act_kind_t k);          /* "toggle" */
act_kind_t  act_from_name(const char *s);    /* ACT_NONE if unknown */
static inline bool act_has_interval(act_kind_t k)
{ return k == ACT_REPEAT || k == ACT_TOGGLE; }
static inline bool act_has_keys(act_kind_t k)
{ return k == ACT_KEY || k == ACT_REPEAT || k == ACT_TOGGLE || k == ACT_LATCH; }

#define INTERVAL_MIN_MS   50u
#define INTERVAL_MAX_MS   3600000u
#define INTERVAL_DEF_MS   1000u

typedef struct {
	act_kind_t kind;
	combo_t    combo;
	unsigned   interval_ms;
} action_t;

#define NAME_MAX_LEN  64
#define MATCH_MAX_LEN 128

typedef struct {
	char     name[NAME_MAX_LEN];
	char     app[MATCH_MAX_LEN];     /* glob on the focused window's app_id */
	char     title[MATCH_MAX_LEN];   /* glob on its title */
	action_t act[IN_COUNT];
} profile_t;

static inline bool profile_everywhere(const profile_t *p)
{ return !p->app[0] && !p->title[0]; }
int profile_nbound(const profile_t *p);

typedef struct {
	profile_t *p;
	int        n;
	bool       notify;               /* a toast when a toggle or latch flips */
	char       device[MATCH_MAX_LEN];/* only mice whose name contains this */
} config_t;

void config_init(config_t *c);
void config_free(config_t *c);
/* Read a file. A missing file is an empty config, not an error. Problems in
 * it are reported through warn() and the line is skipped; -1 only when the
 * file exists and cannot be read. */
int  config_load(config_t *c, const char *path);
int  config_save(const config_t *c, const char *path);
profile_t *config_find(config_t *c, const char *name);
profile_t *config_add(config_t *c, const char *name);
void config_remove(config_t *c, const char *name);
/* The profile in force for a focused window: the first whose app/title globs
 * match, else the first that applies everywhere, else -1. app may be NULL when
 * nothing is known about focus. */
int  config_match(const config_t *c, const char *app, const char *title);
bool config_has_app_profiles(const config_t *c);

/* Validates one binding against the profile it is going into; returns a
 * translated reason, or NULL when it is fine. */
const char *binding_refusal(const profile_t *p, int in, const action_t *a);
/* "toggle 1 every 5" */
void action_format(const action_t *a, char *buf, size_t n);
/* Parse the words after the button name. NULL on success, else a reason. */
const char *action_parse(int argc, char **argv, action_t *a);
/* Seconds as written: "5", "0.5" — never "5.000000" and never "0,5". */
void seconds_format(unsigned ms, char *buf, size_t n);
int  seconds_parse(const char *s, unsigned *ms);

/* ── the engine ──────────────────────────────────────────────────────────── */

typedef struct {
	void *ctx;
	/* Press (1) or release (0) a key or button. `dev` is the mouse whose twin
	 * a mouse-button output goes to; key outputs ignore it. Every press the
	 * engine sends is later released. */
	void (*emit)(void *ctx, int dev, uint16_t code, int value);
	void (*flush)(void *ctx, int dev);
	/* A toggle or latch changed state. */
	void (*flipped)(void *ctx, int in, const action_t *a, bool on);
} engine_io_t;

typedef struct {
	bool    held;        /* KEY / REPEAT: the button is down now */
	bool    on;          /* TOGGLE / LATCH: switched on */
	bool    latched;     /* LATCH: its keys are down right now */
	bool    tap_down;    /* a tap's press is out; its release is due at up_ms */
	int64_t next_ms;     /* the next repeat press, or 0 */
	int64_t up_ms;
	int     dev;
} slot_t;

typedef struct {
	engine_io_t     io;
	const config_t *cfg;
	slot_t         *slots;      /* [cfg->n][IN_COUNT] */
	int             nprof;
	int             active;     /* profile in force, or -1 */
	unsigned        tap_ms;
} engine_t;

void    engine_init(engine_t *e, engine_io_t io);
void    engine_free(engine_t *e);
/* Before the config the engine was loaded with changes: everything that is
 * down goes up, every toggle goes off (and, with `tell`, says so). Then
 * engine_load() the new one. */
void    engine_unload(engine_t *e, bool tell);
/* Switch a toggle or latch back on after a reload, without a press of the
 * button: it starts when its profile is in force. */
void    engine_set_on(engine_t *e, int prof, int in);
void    engine_load(engine_t *e, const config_t *cfg, int64_t now);
/* Change the profile in force (-1 = none). The old one's toggles PAUSE and
 * come back when it is in force again; anything held is let go. */
void    engine_activate(engine_t *e, int prof, int64_t now);
bool    engine_bound(const engine_t *e, int in);
/* A button went down or up. True when the engine took it. */
bool    engine_button(engine_t *e, int dev, int in, bool down, int64_t now);
/* One notch of a wheel. True when the engine took it. */
bool    engine_wheel(engine_t *e, int dev, int in, int64_t now);
/* Do whatever is due. Returns the next deadline, or -1 for none. */
int64_t engine_tick(engine_t *e, int64_t now);
/* Every toggle and latch off, in every profile. */
void    engine_stop(engine_t *e, int64_t now);
/* A mouse went away: anything aimed at its twin stops. */
void    engine_device_gone(engine_t *e, int dev, int64_t now);
/* For status: is this input's toggle/latch on, and is it running now. */
bool    engine_is_on(const engine_t *e, int prof, int in);

/* ── the daemon, and talking to it ───────────────────────────────────────── */

int  daemon_run(void);
/* Is /dev/input/<ev> a real mouse? From sysfs alone — nothing is opened. Its
 * name, and a bit per IN_* it can produce. */
bool mouse_probe(const char *ev, char *name, size_t nn, unsigned *inputs);
/* Send one command line to the running daemon; its reply goes to `out`.
 * -1 when there is no daemon to talk to. */
int  daemon_ask(const char *cmd, FILE *out);

/* ── paths ───────────────────────────────────────────────────────────────── */

char *config_dir(void);        /* $SYNMOUSE_HOME or ~/.config/syn-mouse */
char *config_path(void);       /* …/bindings.conf */
char *control_socket_path(void);
char *synui_socket_path(void); /* NULL when there is no compositor to ask */

/* ── synui ───────────────────────────────────────────────────────────────── */

/* Pull one string member out of a flat JSON object: "app_id":"…". The value
 * is unescaped into out. False when the key is absent. */
bool json_string_member(const char *obj, size_t len, const char *key,
                        char *out, size_t n);
/* Ask synui a question on a blocking socket; the reply is malloc'd. */
char *synui_ask(const char *cmd);

/* ── output ──────────────────────────────────────────────────────────────── */

typedef enum { OUT_HUMAN, OUT_REC } out_mode_t;
extern out_mode_t g_out;

void  warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void  die(const char *fmt, ...)  __attribute__((format(printf, 1, 2), noreturn));
void  rec_header(const char *fields);
void  rec_row(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *pct_encode(const char *s);
char *xstrdup(const char *s);
char *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int64_t now_ms(void);

#endif /* SYNMOUSE_H */
