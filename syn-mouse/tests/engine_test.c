/* engine_test.c — every mode, on a fake clock, with no device anywhere.
 *
 * The engine emits through callbacks; here they append to a log, and each
 * case compares the log with what a person would expect their game to see.
 * After every case the log is checked for BALANCE: each code pressed as many
 * times as it was released, because a press left over is a key held down in
 * somebody's game.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synmouse.h"

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_pass, g_fail;

#define CHECK(cond, ...) do {                                         \
	if (cond) { g_pass++; }                                           \
	else { g_fail++; printf("  FAIL  %s:%d: ", __FILE__, __LINE__);   \
	       printf(__VA_ARGS__); putchar('\n'); }                      \
} while (0)

/* ── the fake output ─────────────────────────────────────────────────────── */

static char g_log[8192];
static int  g_down[1024];
static int  g_flips_on, g_flips_off;

static void t_emit(void *ctx, int dev, uint16_t code, int value)
{
	(void)ctx;
	char buf[48];
	snprintf(buf, sizeof buf, "%s%s%s@%d ", value ? "+" : "-",
	         key_name(code) ? key_name(code) : "?", "", dev);
	strncat(g_log, buf, sizeof g_log - strlen(g_log) - 1);
	g_down[code] += value ? 1 : -1;
}
static void t_flush(void *ctx, int dev) { (void)ctx; (void)dev; }
static void t_flipped(void *ctx, int in, const action_t *a, bool on)
{
	(void)ctx; (void)in; (void)a;
	if (on) g_flips_on++; else g_flips_off++;
}

static void reset_log(void) { g_log[0] = '\0'; }

static bool balanced(void)
{
	for (int i = 0; i < 1024; i++) if (g_down[i]) return false;
	return true;
}

/* Strip the "@dev" suffixes when a case does not care which mouse. */
static const char *keys_only(void)
{
	static char out[8192];
	char *o = out;
	for (const char *p = g_log; *p; p++) {
		if (*p == '@') { while (*p && *p != ' ') p++; if (!*p) break; }
		*o++ = *p;
	}
	*o = '\0';
	return out;
}

/* ── building configs ────────────────────────────────────────────────────── */

static config_t g_cfg;

static void cfg_from(const char *text)
{
	char path[] = "/tmp/synmouse-test-XXXXXX";
	int fd = mkstemp(path);
	if (fd < 0) { perror("mkstemp"); exit(2); }
	if (write(fd, text, strlen(text)) != (ssize_t)strlen(text)) exit(2);
	close(fd);
	config_free(&g_cfg);
	config_load(&g_cfg, path);
	unlink(path);
}

static engine_t E;

static void fresh(const char *text)
{
	cfg_from(text);
	engine_io_t io = { .emit = t_emit, .flush = t_flush, .flipped = t_flipped };
	engine_free(&E);   /* the previous test's slots; engine_init() zeroes */
	engine_init(&E, io);
	engine_load(&E, &g_cfg, 0);
	engine_activate(&E, 0, 0);
	reset_log();
	memset(g_down, 0, sizeof g_down);
	g_flips_on = g_flips_off = 0;
}

static void click(int in, int64_t t)
{
	engine_button(&E, 1, in, true, t);
	engine_button(&E, 1, in, false, t);
}

/* Run the clock from..to in 10 ms steps, the way the loop would. */
static void run_to(int64_t from, int64_t to)
{
	for (int64_t t = from; t <= to; t += 10) engine_tick(&E, t);
}

/* ── the cases ───────────────────────────────────────────────────────────── */

static void test_key(void)
{
	fresh("[G]\napp = game\nback = key shift+1\n");
	CHECK(engine_button(&E, 1, IN_BACK, true, 0), "a bound button is taken");
	CHECK(!strcmp(keys_only(), "+shift +1 "), "pressed in order: got '%s'", keys_only());
	reset_log();
	engine_button(&E, 1, IN_BACK, false, 100);
	CHECK(!strcmp(keys_only(), "-1 -shift "), "released in reverse: got '%s'", keys_only());
	CHECK(!engine_button(&E, 1, IN_FORWARD, true, 0), "an unbound button is not taken");
	CHECK(balanced(), "key: balanced");
}

static void test_repeat(void)
{
	fresh("[G]\napp = game\nforward = repeat 2 every 0.5\n");
	engine_button(&E, 1, IN_FORWARD, true, 0);
	CHECK(!strcmp(keys_only(), "+2 "), "repeat presses at once: '%s'", keys_only());
	run_to(10, 690);
	CHECK(!strcmp(keys_only(), "+2 -2 +2 -2 "), "and again at 0.5 s: '%s'", keys_only());
	engine_button(&E, 1, IN_FORWARD, false, 700);
	reset_log();
	run_to(700, 3000);
	CHECK(!strcmp(keys_only(), ""), "nothing after the button comes up: '%s'", keys_only());
	CHECK(balanced(), "repeat: balanced");
}

static void test_repeat_short_hold(void)
{
	/* Released 5 ms after the press: the tap still runs its length, so the
	 * game gets a press it can see. */
	fresh("[G]\napp = game\nforward = repeat 2 every 1\n");
	engine_button(&E, 1, IN_FORWARD, true, 0);
	engine_button(&E, 1, IN_FORWARD, false, 5);
	engine_tick(&E, 10);
	CHECK(!strcmp(keys_only(), "+2 "), "still down at 10 ms: '%s'", keys_only());
	run_to(20, 2000);
	CHECK(!strcmp(keys_only(), "+2 -2 "), "up once, never again: '%s'", keys_only());
	CHECK(balanced(), "short hold: balanced");
}

static void test_toggle(void)
{
	fresh("[G]\napp = game\nback = toggle 1 every 1\n");
	click(IN_BACK, 0);
	CHECK(g_flips_on == 1, "a click switches it on");
	run_to(0, 2990);
	CHECK(!strcmp(keys_only(), "+1 -1 +1 -1 +1 -1 "), "pressed at 0, 1 and 2 s: '%s'", keys_only());
	click(IN_BACK, 3000);
	CHECK(g_flips_off == 1, "the next click switches it off");
	run_to(3000, 6000);
	CHECK(!strcmp(keys_only(), "+1 -1 +1 -1 +1 -1 "),
	      "the press that was due at 3 s never goes out: '%s'", keys_only());
	CHECK(balanced(), "toggle: balanced");
	CHECK(!engine_is_on(&E, 0, IN_BACK), "and status says off");
}

static void test_toggle_pauses(void)
{
	fresh("[G]\napp = game\nback = toggle 1 every 1\n");
	click(IN_BACK, 0);
	run_to(0, 500);
	engine_activate(&E, -1, 500);          /* alt-tab to a browser */
	reset_log();
	run_to(500, 5000);
	CHECK(!strcmp(keys_only(), ""), "nothing reaches the browser: '%s'", keys_only());
	CHECK(engine_is_on(&E, 0, IN_BACK), "but it is still switched on");
	engine_activate(&E, 0, 5000);          /* back to the game */
	run_to(5000, 6500);
	CHECK(!strcmp(keys_only(), "+1 -1 +1 -1 "), "it carries on at once: '%s'", keys_only());
	CHECK(g_flips_off == 0, "a pause is not a switch-off");
	engine_unload(&E, true);
	CHECK(balanced(), "toggle pause: balanced");
}

static void test_toggle_tap_cut_by_pause(void)
{
	/* Focus leaves 10 ms into a tap: the key must come up then, not stay
	 * down in the browser until something else releases it. */
	fresh("[G]\napp = game\nback = toggle 1 every 1\n");
	click(IN_BACK, 0);
	engine_tick(&E, 10);
	engine_activate(&E, -1, 10);
	CHECK(!strcmp(keys_only(), "+1 -1 "), "released on the way out: '%s'", keys_only());
	CHECK(balanced(), "cut tap: balanced");
}

static void test_latch(void)
{
	fresh("[G]\napp = game\nmiddle = latch shift\n");
	click(IN_MIDDLE, 0);
	CHECK(!strcmp(keys_only(), "+shift "), "a click holds it: '%s'", keys_only());
	engine_activate(&E, -1, 100);
	CHECK(!strcmp(keys_only(), "+shift -shift "), "let go when focus leaves: '%s'", keys_only());
	engine_activate(&E, 0, 200);
	CHECK(!strcmp(keys_only(), "+shift -shift +shift "), "held again on return: '%s'", keys_only());
	click(IN_MIDDLE, 300);
	CHECK(!strcmp(keys_only(), "+shift -shift +shift -shift "), "the next click lets go: '%s'", keys_only());
	CHECK(balanced(), "latch: balanced");
}

static void test_off(void)
{
	fresh("[G]\napp = game\nforward = off\n");
	CHECK(engine_button(&E, 1, IN_FORWARD, true, 0), "off is taken");
	engine_button(&E, 1, IN_FORWARD, false, 0);
	CHECK(!strcmp(keys_only(), ""), "and does nothing: '%s'", keys_only());
}

static void test_wheel(void)
{
	fresh("[G]\napp = game\nwheelup = key f\nwheeldown = toggle e every 2\n");
	CHECK(engine_wheel(&E, 1, IN_WHEEL_UP, 0), "a bound notch is taken");
	CHECK(!strcmp(keys_only(), "+f "), "a notch is a tap: '%s'", keys_only());
	run_to(10, 100);
	CHECK(!strcmp(keys_only(), "+f -f "), "that comes up: '%s'", keys_only());
	engine_wheel(&E, 1, IN_WHEEL_DOWN, 200);
	CHECK(g_flips_on == 1, "a notch flips a toggle");
	engine_wheel(&E, 1, IN_WHEEL_DOWN, 300);
	CHECK(g_flips_off == 1, "and the next flips it back");
	run_to(300, 3000);
	CHECK(balanced(), "wheel: balanced");
	CHECK(!engine_wheel(&E, 1, IN_WHEEL_LEFT, 0), "an unbound wheel direction is not taken");
}

static void test_stop(void)
{
	fresh("[G]\napp = game\nback = toggle 1 every 1\nforward = latch shift\n");
	click(IN_BACK, 0);
	click(IN_FORWARD, 0);
	engine_stop(&E, 10);
	CHECK(g_flips_off == 2, "stop switches both off");
	run_to(10, 3000);
	CHECK(balanced(), "stop: balanced");
}

static void test_unload(void)
{
	fresh("[G]\napp = game\nback = toggle 1 every 1\nforward = key ctrl\n");
	click(IN_BACK, 0);
	engine_button(&E, 1, IN_FORWARD, true, 5);
	engine_unload(&E, true);
	CHECK(balanced(), "a reload lets go of everything");
}

static void test_device_gone(void)
{
	fresh("[G]\napp = game\nback = toggle 1 every 1\nforward = toggle mouse1 every 1\n"
	      "middle = key ctrl\n");
	click(IN_BACK, 0);
	click(IN_FORWARD, 0);
	engine_button(&E, 1, IN_MIDDLE, true, 0);
	engine_device_gone(&E, 1, 10);
	CHECK(engine_is_on(&E, 0, IN_BACK), "a toggle on the keyboard survives the mouse leaving");
	CHECK(!engine_is_on(&E, 0, IN_FORWARD), "a toggle clicking that mouse's twin does not");
	CHECK(g_down[KEY_LEFTCTRL] == 0, "a key held by a button on that mouse is let go");
	engine_unload(&E, true);
	CHECK(balanced(), "device gone: balanced");
}

static void test_reactivate_other_profile(void)
{
	/* Button down under one profile, profile changes, button up: the
	 * release must not touch the new profile's binding. */
	fresh("[A]\napp = a\nback = key x\n\n[B]\napp = b\nback = key y\n");
	engine_button(&E, 1, IN_BACK, true, 0);
	engine_activate(&E, 1, 10);
	engine_button(&E, 1, IN_BACK, false, 20);
	CHECK(!strcmp(keys_only(), "+x -x "), "x up with A, y never touched: '%s'", keys_only());
	CHECK(balanced(), "profile switch mid-press: balanced");
}

/* ── the pieces underneath ───────────────────────────────────────────────── */

static void test_combo(void)
{
	combo_t c;
	const char *bad; size_t bl;
	char buf[128];
	CHECK(combo_parse("shift+1", &c, &bad, &bl) == 0 && c.n == 2 &&
	      c.code[0] == KEY_LEFTSHIFT && c.code[1] == KEY_1, "shift+1");
	CHECK(combo_parse("Ctrl+Q", &c, &bad, &bl) == 0 && c.code[0] == KEY_LEFTCTRL &&
	      c.code[1] == KEY_Q, "case does not matter");
	combo_format(&c, buf, sizeof buf);
	CHECK(!strcmp(buf, "ctrl+q"), "written back canonically: '%s'", buf);
	CHECK(combo_parse("code:30", &c, &bad, &bl) == 0 && c.code[0] == KEY_A, "code:30 is a");
	combo_format(&c, buf, sizeof buf);
	CHECK(!strcmp(buf, "a"), "and is written as its name: '%s'", buf);
	CHECK(combo_parse("code:300", &c, &bad, &bl) != 0, "code:300 cannot be sent");
	CHECK(combo_parse("shift+nope", &c, &bad, &bl) != 0 && bl == 4 && !strncmp(bad, "nope", 4),
	      "the bad token is named");
	CHECK(combo_parse("shift+shift", &c, &bad, &bl) == 0 && c.n == 1, "a key named twice is one key");
	CHECK(combo_parse("", &c, &bad, &bl) != 0, "empty is not a combo");
	CHECK(combo_parse("mouse4", &c, &bad, &bl) == 0 && c.code[0] == BTN_SIDE &&
	      combo_has_button(&c), "mouse4 is the back thumb button");
	CHECK(combo_parse("a+b+c+d+e+f+g", &c, &bad, &bl) != 0, "seven keys is too many");
}

static void test_seconds(void)
{
	unsigned ms;
	char buf[24];
	CHECK(seconds_parse("5", &ms) == 0 && ms == 5000, "5");
	CHECK(seconds_parse("0.5", &ms) == 0 && ms == 500, "0.5");
	CHECK(seconds_parse("0,5", &ms) == 0 && ms == 500, "0,5 as typed in a decimal-comma locale");
	CHECK(seconds_parse("250ms", &ms) == 0 && ms == 250, "250ms");
	CHECK(seconds_parse("1.25s", &ms) == 0 && ms == 1250, "1.25s");
	CHECK(seconds_parse(".5", &ms) == 0 && ms == 500, ".5");
	CHECK(seconds_parse("abc", &ms) != 0, "abc");
	CHECK(seconds_parse("5x", &ms) != 0, "5x");
	CHECK(seconds_parse("1.5ms", &ms) != 0, "a fraction of a millisecond");
	seconds_format(500, buf, sizeof buf);  CHECK(!strcmp(buf, "0.5"), "500 -> '%s'", buf);
	seconds_format(5000, buf, sizeof buf); CHECK(!strcmp(buf, "5"), "5000 -> '%s'", buf);
	seconds_format(1250, buf, sizeof buf); CHECK(!strcmp(buf, "1.25"), "1250 -> '%s'", buf);
	seconds_format(50, buf, sizeof buf);   CHECK(!strcmp(buf, "0.05"), "50 -> '%s'", buf);
}

/* A button the mouse's own memory turned into a key: bound as key:<name>, and
 * from there on exactly like a button. */
static void test_key_inputs(void)
{
	int two = input_from_name("key:2");
	CHECK(two == input_from_key(KEY_2) && input_is_key(two), "key:2 is the key 2: %d", two);
	CHECK(input_from_name("KEY:2") == two, "the prefix is case-blind");
	CHECK(input_from_name("key:code:30") == input_from_key(KEY_A), "key:code:30 is the key a");
	CHECK(input_from_name("key:mouse1") < 0, "key:mouse1 is a button, not a key the mouse sends");
	CHECK(input_from_name("key:") < 0, "key: with nothing after it");
	CHECK(input_from_name("key:nosuch") < 0, "key:nosuch");
	CHECK(input_from_name("key:shift+2") < 0, "one key, not a combination");
	CHECK(input_from_name("key:code:300") < 0, "a code past 255");
	CHECK(!strcmp(input_name(two), "key:2"), "written back as key:2: '%s'", input_name(two));
	CHECK(!strcmp(input_name(input_from_key(KEY_F13)), "key:f13"), "key:f13");
	CHECK(!strcmp(input_name(input_from_key(0xf8)), "key:code:248"), "a key with no name: '%s'",
	      input_name(input_from_key(0xf8)));
	CHECK(input_label(two) == NULL && input_code(two) == 0 && !input_is_wheel(two),
	      "a key input has no fixed label, button code or wheel");
	CHECK(input_from_key(0) < 0 && input_from_key(256) < 0, "only codes 1..255");

	fresh("[G]\napp = game\nkey:2 = toggle e every 1\nkey:3 = key mouse4\n");
	CHECK(g_cfg.p[0].act[two].kind == ACT_TOGGLE, "key:2 = toggle reads into the profile");
	CHECK(engine_bound(&E, two), "and is bound");
	CHECK(!engine_bound(&E, input_from_key(KEY_4)), "key:4 is not");
	click(two, 0);
	run_to(0, 1990);
	CHECK(!strcmp(keys_only(), "+e -e +e -e "), "the key 2 toggles e at 0 and 1 s: '%s'", keys_only());
	click(two, 2000);
	reset_log();
	CHECK(engine_button(&E, 2, input_from_key(KEY_3), true, 2100), "key:3 is taken");
	engine_button(&E, 2, input_from_key(KEY_3), false, 2200);
	CHECK(!strcmp(g_log, "+mouse4@2 -mouse4@2 "), "and sends the back button, from that device: '%s'", g_log);
	CHECK(balanced(), "key inputs: balanced");
}

static void test_config(void)
{
	cfg_from("notify = off\n"
	         "device = viper\n"
	         "\n"
	         "[Diablo IV]\n"
	         "left = key 1\n"                     /* above app=: still an app profile */
	         "app = steam_app_2344520\n"
	         "back = toggle 1 every 5\n"
	         "forward = repeat 2 every 0.5\n"
	         "bogus = key 1\n"
	         "middle = key nosuchkey\n"
	         "key:2 = key e\n"
	         "\n"
	         "[Everywhere]\n"
	         "left = key 2\n"                     /* refused: no app */
	         "right = latch shift\n");
	CHECK(!g_cfg.notify, "notify = off");
	CHECK(!strcmp(g_cfg.device, "viper"), "device");
	CHECK(g_cfg.n == 2, "two profiles, got %d", g_cfg.n);
	profile_t *d = config_find(&g_cfg, "Diablo IV");
	CHECK(d && d->act[IN_LEFT].kind == ACT_KEY, "left before app= is kept in an app profile");
	CHECK(d && d->act[IN_BACK].kind == ACT_TOGGLE && d->act[IN_BACK].interval_ms == 5000, "toggle every 5");
	CHECK(d && d->act[IN_MIDDLE].kind == ACT_NONE, "a bad key name is skipped, not guessed");
	CHECK(d && d->act[input_from_key(KEY_2)].kind == ACT_KEY, "a key the mouse sends, bound");
	profile_t *e = config_find(&g_cfg, "Everywhere");
	CHECK(e && e->act[IN_LEFT].kind == ACT_NONE, "left is refused in a profile for everywhere");
	CHECK(e && e->act[IN_RIGHT].kind == ACT_LATCH, "right is fine there");

	CHECK(config_match(&g_cfg, "steam_app_2344520", "Diablo IV") == 0, "the game's own profile");
	CHECK(config_match(&g_cfg, "STEAM_APP_2344520", "") == 0, "app ids match without case");
	CHECK(config_match(&g_cfg, "vivaldi-stable", "x") == 1, "anything else: everywhere");
	CHECK(config_match(&g_cfg, NULL, NULL) == 1, "unknown focus: everywhere");

	/* Round trip: what is saved reads back the same. */
	char path[] = "/tmp/synmouse-rt-XXXXXX";
	int fd = mkstemp(path); close(fd);
	CHECK(config_save(&g_cfg, path) == 0, "saved");
	config_t back;
	config_load(&back, path);
	unlink(path);
	CHECK(back.n == g_cfg.n && back.notify == g_cfg.notify &&
	      !strcmp(back.device, g_cfg.device), "settings survive a round trip");
	bool same = true;
	for (int i = 0; i < back.n && i < g_cfg.n; i++)
		same &= !memcmp(back.p[i].act, g_cfg.p[i].act, sizeof back.p[i].act) &&
		        !strcmp(back.p[i].app, g_cfg.p[i].app) && !strcmp(back.p[i].name, g_cfg.p[i].name);
	CHECK(same, "every binding survives a round trip");
	config_free(&back);

	cfg_from("[T]\ntitle = *Diablo*\nback = key 1\n");
	CHECK(config_match(&g_cfg, "whatever", "Diablo IV") == 0, "a title glob");
	CHECK(config_match(&g_cfg, "whatever", "Steam") == -1, "no match, no everywhere: none");
	CHECK(config_match(&g_cfg, NULL, NULL) == -1, "unknown focus never matches a title");
}

static void test_refusals(void)
{
	profile_t p;
	memset(&p, 0, sizeof p);
	action_t a;
	char *v1[] = { "toggle", "1", "every", "0.01" };
	CHECK(action_parse(4, v1, &a) == NULL, "parses");
	CHECK(binding_refusal(&p, IN_BACK, &a) != NULL, "every 0.01 s is refused");
	char *v2[] = { "key" };
	CHECK(action_parse(1, v2, &a) != NULL, "key with no key");
	char *v3[] = { "toggle", "1" };
	CHECK(action_parse(2, v3, &a) == NULL && a.interval_ms == INTERVAL_DEF_MS, "every is optional");
	char *v4[] = { "toggle", "1", "every" };
	CHECK(action_parse(3, v4, &a) != NULL, "every with no number");
	char *v5[] = { "off", "x" };
	CHECK(action_parse(2, v5, &a) != NULL, "off takes nothing");
	char *v6[] = { "key", "a", "every", "5" };
	CHECK(action_parse(4, v6, &a) != NULL, "key has no interval");
	char *v7[] = { "key", "1" };
	action_parse(2, v7, &a);
	CHECK(binding_refusal(&p, IN_LEFT, &a) != NULL, "left everywhere refused");
	snprintf(p.app, sizeof p.app, "game");
	CHECK(binding_refusal(&p, IN_LEFT, &a) == NULL, "left in an app profile allowed");
}

static void test_json(void)
{
	char out[128];
	const char *j = "{\"app_id\":\"steam_app_1\",\"title\":\"Di\\\"ablo \\u00e9\",\"at\":[1,2],"
	                "\"nested\":{\"app_id\":\"inner\"},\"pid\":5}";
	CHECK(json_string_member(j, strlen(j), "app_id", out, sizeof out) && !strcmp(out, "steam_app_1"),
	      "app_id: '%s'", out);
	CHECK(json_string_member(j, strlen(j), "title", out, sizeof out) && !strcmp(out, "Di\"ablo \xc3\xa9"),
	      "escapes undone: '%s'", out);
	CHECK(!json_string_member(j, strlen(j), "pid", out, sizeof out), "a number is not a string");
	CHECK(!json_string_member("{}", 2, "app_id", out, sizeof out), "{} has no app_id");
	const char *k = "{\"title\":\"\\\"app_id\\\":\\\"fake\\\"\"}";
	CHECK(!json_string_member(k, strlen(k), "app_id", out, sizeof out),
	      "a member name inside a string value is not a member");
}

int main(void)
{
	printf("engine\n");
	config_init(&g_cfg);
	test_key();
	test_repeat();
	test_repeat_short_hold();
	test_toggle();
	test_toggle_pauses();
	test_toggle_tap_cut_by_pause();
	test_latch();
	test_off();
	test_wheel();
	test_stop();
	test_unload();
	test_device_gone();
	test_reactivate_other_profile();
	test_combo();
	test_seconds();
	test_key_inputs();
	test_config();
	test_refusals();
	test_json();
	engine_free(&E);
	config_free(&g_cfg);
	printf("\n  %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
