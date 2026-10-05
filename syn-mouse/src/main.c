/* main.c — syn-mouse's command line, and the window it can open.
 *
 * Every change goes through here: the window runs these same commands, so a
 * binding made in either is the same line in the same file, checked by the
 * same code.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "config.h"
#include "synmouse.h"
#include "i18n.h"

#include <dirent.h>
#include <errno.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SYNMOUSE_DATADIR
#define SYNMOUSE_DATADIR "/usr/share/syn-mouse"
#endif

extern char **environ;

static void usage(FILE *f)
{
	fprintf(f,
"syn-mouse — mouse buttons that press keys, repeat them, or toggle them on\n"
"\n"
"  syn-mouse                       what is running, and what is on right now\n"
"  syn-mouse gui                   the window\n"
"\n"
"  syn-mouse add <profile> --app <app id>     a profile for one game\n"
"  syn-mouse add <profile>                    a profile for everywhere\n"
"  syn-mouse remove <profile>\n"
"  syn-mouse profiles              every profile and what it binds\n"
"\n"
"  syn-mouse bind <profile> <button> key <keys>\n"
"  syn-mouse bind <profile> <button> repeat <keys> every <seconds>\n"
"  syn-mouse bind <profile> <button> toggle <keys> every <seconds>\n"
"  syn-mouse bind <profile> <button> latch <keys>\n"
"  syn-mouse bind <profile> <button> off\n"
"  syn-mouse unbind <profile> <button>\n"
"  syn-mouse stop                  switch off everything toggled on\n"
"\n"
"  syn-mouse devices               the mice, and the buttons each one has\n"
"  syn-mouse apps                  open windows, to find an app id\n"
"  syn-mouse keys                  the key names a binding accepts\n"
"  syn-mouse buttons               the button names\n"
"  syn-mouse set notify on|off     a notice when a toggle switches\n"
"  syn-mouse set device <name>     only mice whose name contains this\n"
"  syn-mouse enable | disable      run at login, or not\n"
"  syn-mouse daemon                the service itself\n"
"\n"
"  key      held while the button is held\n"
"  repeat   pressed every N seconds while the button is held\n"
"  toggle   one click starts pressing it every N seconds, the next stops\n"
"  latch    one click holds it down, the next lets go\n"
"  off      the button does nothing\n"
"\n"
"  Buttons: left right middle back forward button6-8 wheelup wheeldown\n"
"  wheelleft wheelright. Keys: shift+1, ctrl+q, f5, space, mouse1-5.\n"
"\n"
"  A profile for an app is in force only while that app has focus. A toggle\n"
"  left on pauses when you switch away and carries on when you come back.\n"
"\n"
"  --rec        one record per line, for a front end\n"
"  --version    print the version\n");
}

/* ── the file ────────────────────────────────────────────────────────────── */

static char *g_path;

static void load(config_t *c)
{
	g_path = config_path();
	if (config_load(c, g_path) < 0)
		die(_("cannot read %s: %s"), g_path, strerror(errno));
}

/* Save, and tell the daemon to pick it up now rather than when inotify gets
 * round to it — so the line after `bind` in a script already has it. */
static bool save(config_t *c)
{
	if (config_save(c, g_path) < 0)
		die(_("cannot write %s: %s"), g_path, strerror(errno));
	return daemon_ask("reload", NULL) == 0;
}

static void not_running_hint(void)
{
	if (g_out == OUT_REC) return;
	warn("%s", _("saved, but the service is not running, so the mouse does not know yet — "
	             "`syn-mouse enable` starts it"));
}

static const char *name_refusal(const char *name)
{
	size_t n = strlen(name);
	if (!n) return _("a profile needs a name");
	if (n >= NAME_MAX_LEN) return _("that name is too long");
	if (strpbrk(name, "[]\n\r\t")) return _("a profile name cannot contain brackets, tabs or line breaks");
	if (name[0] == ' ' || name[n - 1] == ' ') return _("a profile name cannot start or end with a space");
	return NULL;
}

static profile_t *need_profile(config_t *c, const char *name)
{
	profile_t *p = config_find(c, name);
	if (!p) die(_("no profile called '%s' — `syn-mouse profiles` lists them"), name);
	return p;
}

/* ── commands that change it ─────────────────────────────────────────────── */

static int cmd_add(int argc, char **argv)
{
	const char *name = NULL, *app = NULL, *title = NULL;
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--app") && i + 1 < argc)   { app = argv[++i]; continue; }
		if (!strcmp(argv[i], "--title") && i + 1 < argc) { title = argv[++i]; continue; }
		if (!strncmp(argv[i], "--app=", 6))   { app = argv[i] + 6; continue; }
		if (!strncmp(argv[i], "--title=", 8)) { title = argv[i] + 8; continue; }
		if (!name) { name = argv[i]; continue; }
		die(_("unexpected '%s'"), argv[i]);
	}
	if (!name) die("%s", "usage: syn-mouse add <profile> [--app <app id>] [--title <glob>]");
	const char *why = name_refusal(name);
	if (why) die("%s", why);
	if ((app && strlen(app) >= MATCH_MAX_LEN) || (title && strlen(title) >= MATCH_MAX_LEN))
		die("%s", _("that app id or title is too long"));

	config_t c;
	load(&c);
	bool existed = config_find(&c, name) != NULL;
	profile_t *p = config_add(&c, name);
	profile_t before = *p;
	if (app)   snprintf(p->app, sizeof p->app, "%s", app);
	if (title) snprintf(p->title, sizeof p->title, "%s", title);

	why = binding_refusal(p, IN_LEFT, &p->act[IN_LEFT]);
	if (why) { *p = before; die("%s", why); }

	bool live = save(&c);
	if (g_out != OUT_REC) {
		if (profile_everywhere(p))
			printf(existed ? _("[%s] now applies everywhere\n") : _("added [%s], for everywhere\n"), name);
		else
			printf(existed ? _("[%s] now applies to %s\n") : _("added [%s], for %s\n"),
			       name, p->app[0] ? p->app : p->title);
	}
	if (!live) not_running_hint();
	config_free(&c);
	return 0;
}

static int cmd_remove(int argc, char **argv)
{
	if (argc != 1) die("%s", "usage: syn-mouse remove <profile>");
	config_t c;
	load(&c);
	need_profile(&c, argv[0]);
	config_remove(&c, argv[0]);
	bool live = save(&c);
	if (g_out != OUT_REC) printf(_("removed [%s]\n"), argv[0]);
	if (!live) not_running_hint();
	config_free(&c);
	return 0;
}

static int cmd_bind(int argc, char **argv)
{
	if (argc < 3) die("%s", "usage: syn-mouse bind <profile> <button> <mode> [keys] [every <seconds>]");
	int in = input_from_name(argv[1]);
	if (in < 0) die(_("'%s' is not a button — `syn-mouse buttons` lists them"), argv[1]);

	action_t a;
	const char *why = action_parse(argc - 2, argv + 2, &a);
	if (why) die("%s", why);

	config_t c;
	load(&c);
	profile_t *p = need_profile(&c, argv[0]);
	why = binding_refusal(p, in, &a);
	if (why) die("%s", why);
	p->act[in] = a;
	bool live = save(&c);

	if (g_out != OUT_REC) {
		char buf[200];
		action_format(&a, buf, sizeof buf);
		printf("[%s] %s = %s\n", p->name, input_name(in), buf);
	}
	if (!live) not_running_hint();
	config_free(&c);
	return 0;
}

static int cmd_unbind(int argc, char **argv)
{
	if (argc != 2) die("%s", "usage: syn-mouse unbind <profile> <button>");
	int in = input_from_name(argv[1]);
	if (in < 0) die(_("'%s' is not a button — `syn-mouse buttons` lists them"), argv[1]);
	config_t c;
	load(&c);
	profile_t *p = need_profile(&c, argv[0]);
	memset(&p->act[in], 0, sizeof p->act[in]);
	bool live = save(&c);
	if (g_out != OUT_REC) printf(_("[%s] %s does what it always did\n"), p->name, input_name(in));
	if (!live) not_running_hint();
	config_free(&c);
	return 0;
}

static int cmd_set(int argc, char **argv)
{
	if (argc < 1) die("%s", "usage: syn-mouse set notify on|off, or set device <name>");
	config_t c;
	load(&c);
	if (!strcmp(argv[0], "notify") && argc == 2) {
		if (!strcasecmp(argv[1], "on")) c.notify = true;
		else if (!strcasecmp(argv[1], "off")) c.notify = false;
		else die("%s", _("notify is on or off"));
	} else if (!strcmp(argv[0], "device") && argc <= 2) {
		const char *v = argc == 2 ? argv[1] : "";
		if (strlen(v) >= MATCH_MAX_LEN) die("%s", _("that name is too long"));
		snprintf(c.device, sizeof c.device, "%s", v);
	} else {
		die("%s", "usage: syn-mouse set notify on|off, or set device <name>");
	}
	bool live = save(&c);
	if (!live) not_running_hint();
	config_free(&c);
	return 0;
}

/* ── commands that read ──────────────────────────────────────────────────── */

static int cmd_profiles(void)
{
	config_t c;
	load(&c);
	rec_header("kind\tname\tapp\ttitle\tdetail");
	rec_row("setting\tnotify\t%s", c.notify ? "on" : "off");
	{ char *d = pct_encode(c.device); rec_row("setting\tdevice\t%s", d); free(d); }

	if (!c.n && g_out != OUT_REC)
		printf("%s\n", _("No profiles yet. `syn-mouse add <name> --app <app id>` makes one."));

	for (int i = 0; i < c.n; i++) {
		const profile_t *p = &c.p[i];
		char *nm = pct_encode(p->name), *ap = pct_encode(p->app), *ti = pct_encode(p->title);
		rec_row("profile\t%s\t%s\t%s\t%d", nm, ap, ti, profile_everywhere(p) ? 1 : 0);
		if (g_out != OUT_REC) {
			if (i) putchar('\n');
			if (profile_everywhere(p)) printf(_("[%s] — everywhere\n"), p->name);
			else if (p->app[0] && p->title[0]) printf(_("[%s] — %s, titled %s\n"), p->name, p->app, p->title);
			else printf(_("[%s] — %s\n"), p->name, p->app[0] ? p->app : p->title);
		}
		int shown = 0;
		for (int in = 0; in < IN_COUNT; in++) {
			const action_t *a = &p->act[in];
			if (a->kind == ACT_NONE) continue;
			char keys[128], buf[200];
			combo_format(&a->combo, keys, sizeof keys);
			char *k = pct_encode(keys);
			rec_row("bind\t%s\t%s\t%s\t%s\t%u", nm, input_name(in), act_name(a->kind), k, a->interval_ms);
			free(k);
			if (g_out != OUT_REC) {
				action_format(a, buf, sizeof buf);
				printf("  %-11s %s\n", input_name(in), buf);
			}
			shown++;
		}
		if (!shown && g_out != OUT_REC) printf("  %s\n", _("(nothing bound)"));
		free(nm); free(ap); free(ti);
	}
	config_free(&c);
	return 0;
}

static int cmd_devices(void)
{
	rec_header("kind\tname\tnode\tbuttons\treadable");
	DIR *dir = opendir("/dev/input");
	int found = 0, unreadable = 0;
	if (dir) {
		struct dirent *de;
		char *names[64];
		int n = 0;
		while ((de = readdir(dir)) && n < 64)
			if (!strncmp(de->d_name, "event", 5)) names[n++] = xstrdup(de->d_name);
		closedir(dir);
		/* event10 after event9, so the list reads the way the kernel numbered it. */
		for (int i = 0; i < n; i++)
			for (int j = i + 1; j < n; j++)
				if (atoi(names[j] + 5) < atoi(names[i] + 5)) { char *t = names[i]; names[i] = names[j]; names[j] = t; }

		for (int i = 0; i < n; i++) {
			char name[128], node[64], btns[256] = "";
			unsigned inputs = 0;
			if (mouse_probe(names[i], name, sizeof name, &inputs)) {
				snprintf(node, sizeof node, "/dev/input/%s", names[i]);
				size_t off = 0;
				for (int in = 0; in < IN_KEY_FIRST; in++)
					if (inputs & (1u << in))
						off += (size_t)snprintf(btns + off, sizeof btns - off, "%s%s",
						                        off ? "," : "", input_name(in));
				bool rd = access(node, R_OK) == 0;
				unreadable += !rd;
				found++;
				char *nm = pct_encode(name);
				rec_row("mouse\t%s\t%s\t%s\t%d", nm, node, btns, rd ? 1 : 0);
				free(nm);
				if (g_out != OUT_REC) {
					printf("%s  (%s)%s\n", name, node, rd ? "" : _("  — cannot be read"));
					for (char *b = btns; *b; b++) if (*b == ',') *b = ' ';
					printf("  %s\n", btns);
				}
			}
			free(names[i]);
		}
	}
	if (!found && g_out != OUT_REC) printf("%s\n", _("No mouse found."));
	if (unreadable && g_out != OUT_REC)
		printf("\n%s\n", _("This account cannot read the mouse. Add it to the input group, then log out and back in:\n"
		                   "  sudo usermod -aG input $USER"));
	return 0;
}

static int cmd_buttons(void)
{
	rec_header("kind\tname\tlabel");
	for (int in = 0; in < IN_KEY_FIRST; in++) {
		char *la = pct_encode(input_label(in));
		rec_row("button\t%s\t%s", input_name(in), la);
		free(la);
		if (g_out != OUT_REC) printf("  %-11s %s\n", input_name(in), _(input_label(in)));
	}
	/* key:<name> is not a row: there are 255 of them, and which ones this
	 * mouse sends is a question for the running daemon (`sent` in status). */
	if (g_out != OUT_REC)
		printf("\n%s\n", _("A button the mouse's own memory maps to a key sends that key instead, "
		                   "and is bound as key:<name> — key:2 for the key 2. "
		                   "`syn-mouse status` lists the keys it has seen the mouse send."));
	return 0;
}

static int cmd_keys(void)
{
	rec_header("kind\tname\tcode");
	int col = 0;
	for (size_t i = 0; i < g_nkeynames; i++) {
		rec_row("key\t%s\t%u", g_keynames[i].name, g_keynames[i].code);
		if (g_out == OUT_REC) continue;
		int w = printf("%s%s", col ? "  " : "", g_keynames[i].name);
		col += w;
		if (col > 66) { putchar('\n'); col = 0; }
	}
	if (g_out != OUT_REC) {
		if (col) putchar('\n');
		printf("\n%s\n", _("Join keys with + (shift+1). code:N sends key code N."));
	}
	return 0;
}

static int cmd_apps(void)
{
	rec_header("kind\tapp\ttitle");
	char *r = synui_ask("clients");
	if (!r) {
		if (g_out != OUT_REC) die("%s", _("cannot ask synui which windows are open — is this a synui session?"));
		return 1;
	}
	/* Each top-level object in the array is one window. */
	int depth = 0;
	const char *obj = NULL;
	bool instr = false;
	for (const char *p = r; *p; p++) {
		if (instr) {
			if (*p == '\\' && p[1]) p++;
			else if (*p == '"') instr = false;
			continue;
		}
		if (*p == '"') { instr = true; continue; }
		if (*p == '{' && depth++ == 0) obj = p;
		if (*p == '}' && --depth == 0 && obj) {
			char app[256] = "", title[512] = "";
			json_string_member(obj, (size_t)(p - obj + 1), "app_id", app, sizeof app);
			json_string_member(obj, (size_t)(p - obj + 1), "title", title, sizeof title);
			if (app[0]) {
				char *a = pct_encode(app), *t = pct_encode(title);
				rec_row("app\t%s\t%s", a, t);
				free(a); free(t);
				if (g_out != OUT_REC) printf("  %-28s %s\n", app, title);
			}
			obj = NULL;
		}
	}
	free(r);
	return 0;
}

/* ── status ──────────────────────────────────────────────────────────────── */

static char *pct_decode(const char *s)
{
	char *out = xstrdup(s), *o = out;
	for (const char *p = s; *p; p++) {
		if (*p == '%' && p[1] && p[2]) {
			char h[3] = { p[1], p[2], 0 };
			*o++ = (char)strtol(h, NULL, 16);
			p += 2;
		} else *o++ = *p;
	}
	*o = '\0';
	return out;
}

static int split_tabs(char *line, char **f, int max)
{
	int n = 0;
	f[n++] = line;
	for (char *p = line; *p && n < max; p++)
		if (*p == '\t') { *p = '\0'; f[n++] = p + 1; }
	return n;
}

static int cmd_status(void)
{
	char *buf = NULL;
	size_t len = 0;
	FILE *m = open_memstream(&buf, &len);
	if (!m) die("out of memory");
	int r = daemon_ask("status", m);
	fclose(m);

	if (g_out == OUT_REC) {
		rec_header("kind\ta\tb\tc\td\te\tf");
		if (r != 0) rec_row("daemon\t0\t");
		else fputs(buf, stdout);
		free(buf);
		return 0;
	}
	if (r != 0) {
		printf("%s\n", _("syn-mouse is not running. `syn-mouse enable` starts it now and at every login."));
		free(buf);
		return 0;
	}

	char *save_ptr = NULL;
	int nactive = 0;
	for (char *line = strtok_r(buf, "\n", &save_ptr); line; line = strtok_r(NULL, "\n", &save_ptr)) {
		char *f[8];
		int n = split_tabs(line, f, 8);
		if (!strcmp(f[0], "daemon") && n >= 2) {
			printf(_("syn-mouse is running (pid %s)\n"), f[1]);
		} else if (!strcmp(f[0], "focus") && n >= 4) {
			char *app = pct_decode(f[2]), *title = pct_decode(f[3]);
			if (!strcmp(f[1], "1")) printf(_("  focus      %s (%s)\n"), title, app);
			else if (!strcmp(f[1], "2")) printf("%s\n", _("  focus      no window"));
			free(app); free(title);
		} else if (!strcmp(f[0], "profile") && n >= 2) {
			char *p = pct_decode(f[1]);
			if (*p) printf(_("  in force   [%s]\n"), p);
			else printf("%s\n", _("  in force   nothing — the mouse is as it always is"));
			free(p);
		} else if (!strcmp(f[0], "mouse") && n >= 4) {
			char *nm = pct_decode(f[1]);
			printf(!strcmp(f[3], "1") ? _("  mouse      %s — taken over\n") : _("  mouse      %s\n"), nm);
			free(nm);
		} else if (!strcmp(f[0], "sent") && n >= 2 && !strncmp(f[1], "key:", 4)) {
			printf(_("  sends      %s — a button sends this key; bind it as %s\n"), f[1] + 4, f[1]);
		} else if (!strcmp(f[0], "problem") && n >= 2) {
			if (!strcmp(f[1], "uinput"))
				printf("%s\n", _("  problem    cannot create input devices — this account needs to be in the input group"));
			else if (!strcmp(f[1], "nomouse"))
				printf("%s\n", _("  problem    no mouse it can read"));
		} else if (!strcmp(f[0], "active") && n >= 7) {
			char *prof = pct_decode(f[1]), *keys = pct_decode(f[4]);
			char secs[24];
			seconds_format((unsigned)atoi(f[5]), secs, sizeof secs);
			bool running = !strcmp(f[6], "1");
			/* ⛔ A WHOLE LINE PER CASE: "(paused)" appended to a translated
			 * sentence is a fragment no language can place. */
			if (!strcmp(f[3], "toggle"))
				printf(running ? _("  on         %s in [%s]: pressing %s every %s s\n")
				               : _("  paused     %s in [%s]: pressing %s every %s s, once [%s] is in force again\n"),
				       f[2], prof, keys, secs, prof);
			else
				printf(running ? _("  on         %s in [%s]: holding %s down\n")
				               : _("  paused     %s in [%s]: holding %s down, once [%s] is in force again\n"),
				       f[2], prof, keys, prof);
			nactive++;
			free(prof); free(keys);
		}
	}
	if (nactive) printf("%s\n", _("  `syn-mouse stop` switches them all off."));
	free(buf);
	return 0;
}

static int cmd_stop(void)
{
	if (daemon_ask("stop", NULL) != 0) {
		if (g_out != OUT_REC) printf("%s\n", _("syn-mouse is not running, so nothing is on."));
		return 0;
	}
	if (g_out != OUT_REC) printf("%s\n", _("Everything toggled on is off."));
	return 0;
}

/* ── the service ─────────────────────────────────────────────────────────── */

static int run(char *const argv[])
{
	pid_t pid;
	if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) != 0) return -1;
	int st;
	while (waitpid(pid, &st, 0) < 0) if (errno != EINTR) return -1;
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/*
 * ⚠ ON FOR EVERY ACCOUNT BY DEFAULT, so OFF IS A MASK. The package links the
 * unit into default.target.wants under /usr/lib — the only way a package can
 * enable a user unit — and `systemctl --user disable` cannot remove a link it
 * did not make. A mask in the user's own directory wins over it and survives
 * upgrades; `enable` lifts the mask and starts it.
 */
static int cmd_enable(bool on)
{
	char *unmask[]  = { "systemctl", "--user", "unmask", "syn-mouse.service", NULL };
	char *enable[]  = { "systemctl", "--user", "enable", "--now", "syn-mouse.service", NULL };
	char *disable[] = { "systemctl", "--user", "disable", "--now", "syn-mouse.service", NULL };
	char *mask[]    = { "systemctl", "--user", "mask", "syn-mouse.service", NULL };
	int r;
	if (on) {
		run(unmask);
		r = run(enable);
	} else {
		run(disable);
		r = run(mask);
	}
	if (r != 0) die("%s", _("systemctl did not do it — see the message above"));
	if (g_out != OUT_REC)
		printf("%s\n", on ? _("syn-mouse is on, now and at every login.")
		                  : _("syn-mouse is off, and stays off at login."));
	return 0;
}

static int cmd_gui(void)
{
	if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY"))
		die("%s", _("no display — syn-mouse gui needs a graphical session"));
	if (access("/usr/bin/quickshell", X_OK) != 0 &&
	    access("/usr/local/bin/quickshell", X_OK) != 0)
		die("%s", _("quickshell is not installed — synpkg install quickshell"));

	/* The window's own Wayland identity, so the dock resolves its .desktop and
	 * it does not inherit the app_id of whatever launched it. */
	setenv("QS_APP_ID", "syn-mouse", 1);

	const char *qml = SYNMOUSE_DATADIR "/syn-mouse.qml";
	if (access(qml, R_OK) != 0 && access("data/syn-mouse.qml", R_OK) == 0)
		qml = "data/syn-mouse.qml";

	char *child[] = { (char *)"quickshell", (char *)"-p", (char *)qml, NULL };
	execvp(child[0], child);
	die("%s", _("could not start quickshell"));
}

int main(int argc, char **argv)
{
	syn_mouse_i18n_init();

	char *pos[64];
	int n = 0;
	for (int i = 1; i < argc; i++) {
		char *v = argv[i];
		if (!strcmp(v, "--rec"))  { g_out = OUT_REC; continue; }
		if (!strcmp(v, "--help") || !strcmp(v, "-h")) { usage(stdout); return 0; }
		if (!strcmp(v, "--version")) { printf("syn-mouse %s\n", SYNMOUSE_VERSION); return 0; }
		if (n < 64) pos[n++] = v;
	}

	if (n == 0) {
		int r = cmd_status();
		if (g_out != OUT_REC) printf("\n%s\n", _("`syn-mouse gui` opens the window; `syn-mouse --help` lists the commands."));
		return r;
	}

	const char *c = pos[0];
	if (!strcmp(c, "status"))   return cmd_status();
	if (!strcmp(c, "profiles") || !strcmp(c, "list")) return cmd_profiles();
	if (!strcmp(c, "add"))      return cmd_add(n - 1, pos + 1);
	if (!strcmp(c, "remove"))   return cmd_remove(n - 1, pos + 1);
	if (!strcmp(c, "bind"))     return cmd_bind(n - 1, pos + 1);
	if (!strcmp(c, "unbind"))   return cmd_unbind(n - 1, pos + 1);
	if (!strcmp(c, "set"))      return cmd_set(n - 1, pos + 1);
	if (!strcmp(c, "stop"))     return cmd_stop();
	if (!strcmp(c, "devices"))  return cmd_devices();
	if (!strcmp(c, "buttons"))  return cmd_buttons();
	if (!strcmp(c, "keys"))     return cmd_keys();
	if (!strcmp(c, "apps"))     return cmd_apps();
	if (!strcmp(c, "enable"))   return cmd_enable(true);
	if (!strcmp(c, "disable"))  return cmd_enable(false);
	if (!strcmp(c, "daemon"))   return daemon_run();
	if (!strcmp(c, "gui"))      return cmd_gui();

	warn(_("unknown command '%s'"), c);
	usage(stderr);
	return 2;
}
