/*
 * startup.c — what runs when you log in, and changing it.
 *
 * Three lists, because three different things start programs at login on this
 * desktop and only one of them is synui:
 *
 *   · synui's own `autostart =` lines in synuirc. The ONLY list a synui session
 *     runs. Switched off by rewriting the line as `#off: autostart = …`, which
 *     synui reads as a comment and this pane reads back as "off", so turning it
 *     on again does not mean typing it again.
 *   · systemd user units that are enabled — or could be — at login: Syncthing,
 *     KDE Connect's daemon, the remote desktop. Session plumbing (PipeWire, the
 *     portals, D-Bus) is left out; turning those off breaks the session rather
 *     than changing what starts in it.
 *   · XDG autostart entries (~/.config/autostart, /etc/xdg/autostart).
 *     ⛔ A SYNUI SESSION NEVER RUNS THESE. Nothing in synui implements the XDG
 *     autostart spec, so every package that relies on it — KDE Connect, OpenRGB,
 *     the limine snapshot notifier — installs a login item that silently does
 *     nothing. They are listed so that is visible, and switching one on copies
 *     its command into synui's list, which is the only way it will ever run.
 *     Under GNOME or KDE they DO run, so the section is not drawn there.
 *
 * ⚠ THE CAP IS synui's. synui runs at most SYN_AUTOSTART_MAX lines and cuts each
 * at 127 bytes; a line past either is dropped or truncated by synui. This pane
 * refuses to write one rather than write a line that will not do what it says.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"
#include "i18n.h"

#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* synui's SYN_AUTOSTART_MAX (synui/src/synui.h) and the length it keeps. */
#define STARTUP_MAX 32
#define CMD_MAX     127

static int refuse(const char *msg)
{
	fprintf(stderr, "syn-settings: %s\n", msg);
	return 2;
}

/* ── the token a command travels in ─────────────────────────────────────── */

/*
 * ⛔ AN ACTION CELL IS A SPACE-SEPARATED LIST, and a command has spaces in it.
 * `toggle:autostart/syn-arcade big guard` would reach the window as three
 * tokens. So the command rides percent-encoded — every byte outside a small
 * safe set — and is decoded on the way back into `set`.
 */
static void pct_encode(const char *in, char *out, size_t cap)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t o = 0;
	for (const unsigned char *p = (const unsigned char *)in; *p && o + 4 < cap; p++) {
		if (isalnum(*p) || strchr("._-/~", *p)) out[o++] = (char)*p;
		else {
			out[o++] = '%';
			out[o++] = hex[*p >> 4];
			out[o++] = hex[*p & 15];
		}
	}
	out[o] = '\0';
}

static int hexval(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static bool pct_decode(const char *in, char *out, size_t cap)
{
	size_t o = 0;
	for (const char *p = in; *p; p++) {
		if (o + 1 >= cap) return false;
		if (*p == '%') {
			int h = hexval(p[1]), l = h < 0 ? -1 : hexval(p[2]);
			if (h < 0 || l < 0) return false;
			out[o++] = (char)(h * 16 + l);
			p += 2;
		} else out[o++] = *p;
	}
	out[o] = '\0';
	return true;
}

/* ── synuirc ────────────────────────────────────────────────────────────── */

static const char *system_synuirc(void)
{
	return env_or("SYN_SETTINGS_SYSTEM_SYNUIRC", "/etc/synui/synuirc");
}

/*
 * One line of synuirc, read the way synui reads it. Returns 1 for an
 * `autostart =` line, 0 for one switched off by this pane (`#off:`), -1 for
 * anything else; the command is cmd[0..cl).
 *
 * ⚠ synui's inline-comment rule too: a `#` with whitespace before it ends the
 * value (so `border_color = #ff296d` survives). A line synui would cut is shown
 * as synui will run it.
 */
static int autostart_line(const char *p, size_t len, const char **cmd, size_t *cl)
{
	while (len && (*p == ' ' || *p == '\t')) { p++; len--; }
	int on = 1;
	if (len >= 5 && !strncmp(p, "#off:", 5)) {
		on = 0; p += 5; len -= 5;
		while (len && (*p == ' ' || *p == '\t')) { p++; len--; }
	}
	if (len < 9 || strncmp(p, "autostart", 9)) return -1;
	p += 9; len -= 9;
	while (len && (*p == ' ' || *p == '\t')) { p++; len--; }
	if (!len || *p != '=') return -1;
	p++; len--;
	while (len && (*p == ' ' || *p == '\t')) { p++; len--; }

	size_t n = len;
	for (size_t i = 1; i < len; i++)
		if (p[i] == '#' && (p[i - 1] == ' ' || p[i - 1] == '\t')) { n = i; break; }
	while (n && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r')) n--;
	if (!n) return -1;
	*cmd = p; *cl = n;
	return on;
}

/* Every autostart command in `text`, in order; on[i] says whether it runs. */
struct rc_list { int n; char cmd[64][CMD_MAX + 1]; bool on[64]; };

static void rc_scan(const char *text, struct rc_list *out)
{
	out->n = 0;
	for (const char *p = text; p && *p && out->n < 64; ) {
		const char *eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol - p) : strlen(p);
		const char *c; size_t cl;
		int st = autostart_line(p, len, &c, &cl);
		if (st >= 0) {
			if (cl > CMD_MAX) cl = CMD_MAX;
			memcpy(out->cmd[out->n], c, cl);
			out->cmd[out->n][cl] = '\0';
			out->on[out->n] = st == 1;
			out->n++;
		}
		p = eol ? eol + 1 : NULL;
	}
}

/*
 * The text synui reads, and where it came from: the user's file, or — when
 * there is none — /etc/synui/synuirc, which synui falls back to. NULL when
 * neither exists, which is synui running its compiled-in defaults.
 */
static char *rc_text(char *path, size_t cap, bool *from_system)
{
	synuirc_path(path, cap);
	*from_system = false;
	char *t = slurp(path);
	if (t) return t;
	t = slurp(system_synuirc());
	if (t) *from_system = true;
	return t;
}

/* A command this pane will write. Each refusal is something synui would do
 * differently from what the line says. */
static int check_command(const char *cmd)
{
	if (!*cmd) return refuse("an empty command starts nothing");
	if (strlen(cmd) > CMD_MAX)
		return refuse("synui keeps 127 bytes of an autostart line; this "
		              "command would be cut short");
	for (const char *p = cmd; *p; p++) {
		if (*p == '\n' || *p == '\r' || *p == '\t')
			return refuse("a command is one line");
		if (*p == '#' && p > cmd && (p[-1] == ' ' || p[-1] == '\t'))
			return refuse("synui reads ' #' as the start of a comment, so the "
			              "command would be cut there");
	}
	if (isspace((unsigned char)cmd[0]) || isspace((unsigned char)cmd[strlen(cmd) - 1]))
		return refuse("leading or trailing spaces would not survive synui's reading");
	return 0;
}

/*
 * Rewrite synuirc line by line so every other key, comment and blank line
 * survives. op: 'a' on/add, 'o' off, 'r' remove.
 *
 * ⚠ A FILE THAT DOES NOT EXIST YET IS NOT AN EMPTY LIST. With no synuirc,
 * synui runs its built-in default (`autostart = syntty`) — and a file that
 * names ANY autostart replaces the defaults outright. So a first write starts
 * from what synui is doing now: /etc/synui/synuirc if it is there, otherwise
 * that one default line.
 */
static int rc_edit(const char *cmd, char op)
{
	char path[PATH_CAP];
	bool from_system;
	char *text = rc_text(path, sizeof path, &from_system);
	/* From /etc or not, the write goes to the user's own file; the system one
	 * is only where the list starts from. */
	(void)from_system;
	if (!text) {
		if (op != 'a') { return refuse("synui has no autostart list of yours yet"); }
		text = strdup("# synuirc — started by syn-settings (Startup). Every other\n"
		              "# setting keeps synui's built-in default until a line here\n"
		              "# names it.\n"
		              "autostart = syntty\n");
		if (!text) return 1;
	}

	size_t cap = strlen(text) + strlen(cmd) + 64;
	char *out = malloc(cap);
	if (!out) { free(text); return 1; }
	size_t n = 0;
	out[0] = '\0';

	int found = 0, changed = 0, enabled = 0;
	for (const char *p = text; *p; ) {
		const char *line = p;
		const char *eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol - p) : strlen(p);
		const char *c; size_t cl;
		int st = autostart_line(line, len, &c, &cl);
		bool mine = st >= 0 && cl == strlen(cmd) && !memcmp(c, cmd, cl);
		p = eol ? eol + 1 : p + len;

		if (mine && op == 'r') {             /* every copy of it goes */
			found = 1;
			changed = 1;
			continue;
		}
		if (mine && !found) {                /* the first copy is the switch */
			found = 1;
			int want = op == 'a';
			if (st != want) changed = 1;
			n += (size_t)snprintf(out + n, cap - n, "%sautostart = %s\n",
			                      want ? "" : "#off: ", cmd);
			if (want) enabled++;
			continue;
		}
		/* Anything else, a later duplicate included, is kept as written. */
		if (st == 1) enabled++;
		memcpy(out + n, line, len);
		n += len;
		out[n++] = '\n';
		out[n] = '\0';
	}
	free(text);

	if (!found && op != 'a') { free(out); return refuse("that command is not in synui's autostart list"); }
	if (!found) {
		n += (size_t)snprintf(out + n, cap - n, "autostart = %s\n", cmd);
		enabled++;
		changed = 1;
	}
	if (op == 'a' && enabled > STARTUP_MAX) {
		free(out);
		return refuse("synui starts at most 32 programs at login — turn one off first");
	}
	if (!changed) { free(out); return 0; }

	if (g_dry_run) {
		printf("would write %s:\n%s", path, out);
		free(out);
		return 0;
	}
	ensure_parent(path);
	backup_once(path);
	int rc = write_atomic(path, out);
	free(out);
	return rc;
}

/* ── XDG autostart entries ──────────────────────────────────────────────── */

/* Exec= with its field codes taken out: an autostart has no files or URLs to
 * hand over, and %U left in would reach sh as a literal argument. */
static void exec_clean(const char *in, char *out, size_t cap)
{
	size_t o = 0;
	for (const char *p = in; *p && o + 1 < cap; p++) {
		if (*p == '%' && p[1]) {
			if (p[1] == '%') { out[o++] = '%'; p++; continue; }
			if (strchr("fFuUdDnNickvm", p[1])) { p++; continue; }
		}
		out[o++] = *p;
	}
	out[o] = '\0';
	/* collapse the doubled spaces a removed code leaves, and trim */
	char *w = out, *r = out;
	bool sp = true;
	for (; *r; r++) {
		if (*r == ' ' || *r == '\t') { if (!sp) *w++ = ' '; sp = true; }
		else { *w++ = *r; sp = false; }
	}
	if (w > out && w[-1] == ' ') w--;
	*w = '\0';
}

/* Does `list` (a ;-separated desktop list) name this session? */
static bool names_us(const char *list)
{
	const char *ours[8] = { "synui", "SynapseOS" };
	int n = 2;
	char cur[256];
	snprintf(cur, sizeof cur, "%s", env_or("XDG_CURRENT_DESKTOP", ""));
	for (char *s = NULL, *t = strtok_r(cur, ":", &s); t && n < 8; t = strtok_r(NULL, ":", &s))
		ours[n++] = t;

	char dup[512];
	snprintf(dup, sizeof dup, "%s", list);
	for (char *s = NULL, *t = strtok_r(dup, ";", &s); t; t = strtok_r(NULL, ";", &s))
		for (int i = 0; i < n; i++)
			if (!strcmp(t, ours[i])) return true;
	return false;
}

static bool is_true(const char *v) { return !strcmp(v, "true") || !strcmp(v, "1"); }

static bool plumbing(const char *u);

struct xdg_entry { char id[256], name[256], exec[512], file[PATH_CAP]; bool user; };

/*
 * Every autostart entry a spec-following session would start: the user's
 * directory first, so an entry there with the same file name REPLACES the
 * system one (Hidden=true in it is how the spec switches a system entry off).
 */
static int xdg_collect(struct xdg_entry *out, int max)
{
	char dirs[8][PATH_CAP];
	int nd = 0;
	char base[PATH_CAP];
	config_home(base, sizeof base);
	snprintf(dirs[nd++], PATH_CAP, "%.480s/autostart", base);

	char sys[1024];
	snprintf(sys, sizeof sys, "%s", env_or("XDG_CONFIG_DIRS", "/etc/xdg"));
	for (char *s = NULL, *t = strtok_r(sys, ":", &s); t && nd < 8; t = strtok_r(NULL, ":", &s))
		snprintf(dirs[nd++], PATH_CAP, "%.480s/autostart", t);

	int n = 0;
	char seen[128][256];
	int nseen = 0;
	for (int d = 0; d < nd; d++) {
		DIR *dh = opendir(dirs[d]);
		if (!dh) continue;
		struct dirent *de;
		while ((de = readdir(dh)) && n < max) {
			size_t l = strlen(de->d_name);
			if (l < 9 || strcmp(de->d_name + l - 8, ".desktop")) continue;

			bool dup = false;
			for (int i = 0; i < nseen; i++) if (!strcmp(seen[i], de->d_name)) dup = true;
			if (dup) continue;
			if (nseen < 128) snprintf(seen[nseen++], 256, "%s", de->d_name);

			struct xdg_entry *e = &out[n];
			snprintf(e->file, sizeof e->file, "%.400s/%.100s", dirs[d], de->d_name);
			char *t = slurp(e->file);
			if (!t) continue;

			char v[512];
			bool skip = false;
			if (ini_get(t, "Desktop Entry", "Type", v, sizeof v) && strcmp(v, "Application")) skip = true;
			if (ini_get(t, "Desktop Entry", "Hidden", v, sizeof v) && is_true(v)) skip = true;
			if (ini_get(t, "Desktop Entry", "OnlyShowIn", v, sizeof v) && !names_us(v)) skip = true;
			if (ini_get(t, "Desktop Entry", "NotShowIn", v, sizeof v) && names_us(v)) skip = true;
			if (ini_get(t, "Desktop Entry", "TryExec", v, sizeof v) && !have_cmd(v)) skip = true;
			if (!ini_get(t, "Desktop Entry", "Exec", v, sizeof v)) skip = true;
			/* Plumbing here too: xdg-user-dirs ships BOTH an XDG entry and a
			 * user unit, and the unit runs it — "never runs" would be false. */
			if (plumbing(de->d_name)) skip = true;
			if (!skip) {
				exec_clean(v, e->exec, sizeof e->exec);
				/* The program itself has to exist — an entry left behind by a
				 * package that is gone is not a login item. */
				char prog[256];
				size_t pl = strcspn(e->exec, " ");
				if (pl >= sizeof prog) pl = 0;       /* no program is that long */
				memcpy(prog, e->exec, pl);
				prog[pl] = '\0';
				if (!*prog || !have_cmd(prog)) skip = true;
			}
			if (!skip) {
				if (!ini_get(t, "Desktop Entry", "Name", e->name, sizeof e->name))
					snprintf(e->name, sizeof e->name, "%s", de->d_name);
				snprintf(e->id, sizeof e->id, "%s", de->d_name);
				e->user = d == 0;
				n++;
			}
			free(t);
		}
		closedir(dh);
	}
	return n;
}

/* ── user units ─────────────────────────────────────────────────────────── */

/*
 * Session plumbing: the session needs these, and a switch for them would be a
 * way to break audio or the portals rather than a choice about what starts.
 */
static bool plumbing(const char *u)
{
	static const char *pre[] = {
		"pipewire", "wireplumber", "filter-chain", "dbus", "p11-kit",
		"systemd-", "xdg-desktop-portal", "xdg-document-portal",
		"xdg-permission-store", "xdg-user-dirs", "at-spi", "gvfs", "dconf",
		"gnome-keyring", "gpg-agent", "dirmngr", "keyboxd", "grub-boot-",
		"obex", "pulseaudio", NULL };
	for (int i = 0; pre[i]; i++)
		if (!strncmp(u, pre[i], strlen(pre[i]))) return true;
	return false;
}

static bool unit_name_ok(const char *u)
{
	size_t l = strlen(u);
	if (l < 6 || l > 200) return false;
	for (const char *p = u; *p; p++)
		if (!isalnum((unsigned char)*p) && !strchr("._-:\\", *p)) return false;
	return (l > 8 && !strcmp(u + l - 8, ".service")) ||
	       (l > 6 && !strcmp(u + l - 6, ".timer")) ||
	       (l > 5 && !strcmp(u + l - 5, ".path"));
}

/* ── the pane ───────────────────────────────────────────────────────────── */

static void rows_autostart(struct rc_list *rc)
{
	char path[PATH_CAP];
	bool from_system;
	char *text = rc_text(path, sizeof path, &from_system);
	rc->n = 0;

	if (!text) {
		/* synui's compiled-in default, which a synuirc of any kind replaces */
		rec_row("autostart\tsyntty\t%s\tok\t%s\t-", N_("on"),
		        N_("synui's built-in default — there is no synuirc yet; adding a program here keeps it"));
	} else {
		rc_scan(text, rc);
		free(text);
		for (int i = 0; i < rc->n; i++) {
			char shown[CMD_MAX + 1], enc[CMD_MAX * 3 + 1];
			snprintf(shown, sizeof shown, "%s", rc->cmd[i]);
			tsv_clean(shown);
			pct_encode(rc->cmd[i], enc, sizeof enc);
			rec_row("autostart\t%s\t%s\t%s\t%s\ttoggle:autostart/%s drop:autostart/%s",
			        shown, rc->on[i] ? N_("on") : N_("off"), rc->on[i] ? "ok" : "-",
			        rc->on[i] ? N_("synui starts this when you log in")
			                  : N_("switched off — kept so it can be switched back on"),
			        enc, enc);
		}
	}

	int on = 0;
	for (int i = 0; i < rc->n; i++) on += rc->on[i];
	if (on >= STARTUP_MAX)
		rec_row("add\t%s\t\twarn\t%s\t-", N_("Add a program"),
		        N_("synui starts at most 32 programs at login — switch one off to add another"));
	else
		rec_row("add\t%s\t\t-\t%s\tset:autostart-add", N_("Add a program"),
		        N_("a command for synui to run at login, as you would type it in a terminal — it starts from your next login"));
}

static void rows_services(void)
{
	/* LC_ALL=C: the state words are what this parses. */
	char *list = malloc(65536);
	if (!list) return;
	char *argv[] = { (char *)"env", (char *)"LC_ALL=C", (char *)"systemctl", (char *)"--user",
	                 (char *)"list-unit-files", (char *)"--type=service,timer,path",
	                 (char *)"--no-legend", (char *)"--no-pager", NULL };
	if (run_capture_quiet(argv, list, 65536) != 0 || !*list) {
		rec_row("service\t%s\t%s\t-\t%s\t-", N_("background services"), N_("unavailable"),
		        N_("there is no user service manager in this session to ask"));
		free(list);
		return;
	}

	char units[96][208];
	bool en[96];
	int n = 0;
	for (char *s = NULL, *l = strtok_r(list, "\n", &s); l && n < 96; l = strtok_r(NULL, "\n", &s)) {
		char u[208], st[32];
		if (sscanf(l, "%207s %31s", u, st) != 2) continue;
		if (strcmp(st, "enabled") && strcmp(st, "disabled")) continue;
		if (strchr(u, '@') || plumbing(u) || !unit_name_ok(u)) continue;
		snprintf(units[n], sizeof units[n], "%s", u);
		en[n] = !strcmp(st, "enabled");
		n++;
	}
	free(list);
	if (!n) return;

	/* Every description in ONE call: a fork per unit is thirty forks every
	 * time the pane opens. */
	char *show[104];
	int a = 0;
	show[a++] = (char *)"env"; show[a++] = (char *)"LC_ALL=C";
	show[a++] = (char *)"systemctl"; show[a++] = (char *)"--user";
	show[a++] = (char *)"show"; show[a++] = (char *)"-p"; show[a++] = (char *)"Id";
	show[a++] = (char *)"-p"; show[a++] = (char *)"Description"; show[a++] = (char *)"--";
	for (int i = 0; i < n && a < 103; i++) show[a++] = units[i];
	show[a] = NULL;
	char *desc = malloc(65536);
	if (desc) run_capture_quiet(show, desc, 65536);

	for (int i = 0; i < n; i++) {
		char d[256] = "";
		if (desc) {
			char want[224];
			snprintf(want, sizeof want, "Id=%.207s\n", units[i]);
			const char *at = strstr(desc, want);
			/* Id and Description sit in one blank-line-separated block */
			if (at) {
				const char *blk = at;
				while (blk > desc && !(blk[-1] == '\n' && blk - 1 > desc && blk[-2] == '\n')) blk--;
				const char *end = strstr(at, "\n\n");
				const char *dp = strstr(blk, "Description=");
				if (dp && (!end || dp < end)) {
					dp += 12;
					size_t dl = strcspn(dp, "\n");
					if (dl >= sizeof d) dl = sizeof d - 1;
					memcpy(d, dp, dl);
					d[dl] = '\0';
				}
			}
		}
		tsv_clean(d);
		rec_row("service\t%s\t%s\t%s\t%s\ttoggle:user-unit/%s",
		        units[i], en[i] ? N_("on") : N_("off"), en[i] ? "ok" : "-",
		        *d ? d : "-", units[i]);
	}
	free(desc);
}

static void rows_xdg(const struct rc_list *rc)
{
	/* Under GNOME or KDE these entries do run — the claim this section makes
	 * would be false there. */
	if (syn_synui_only()) return;

	struct xdg_entry *e = calloc(64, sizeof *e);
	if (!e) return;
	int n = xdg_collect(e, 64);
	for (int i = 0; i < n; i++) {
		/* Already copied into synui's list — it is a row up there. */
		bool adopted = false;
		for (int j = 0; j < rc->n; j++)
			if (!strcmp(rc->cmd[j], e[i].exec)) adopted = true;
		if (adopted) continue;

		char name[256], enc[800];
		snprintf(name, sizeof name, "%s", e[i].name);
		tsv_clean(name);
		pct_encode(e[i].id, enc, sizeof enc);
		/* warn for the user's own entries: somebody asked for those. */
		static const char *why =
			N_("an autostart entry synui never runs — switch it on to copy its command into synui's list");
		if (e[i].user)
			rec_row("xdg\t%s\t%s\twarn\t%s\ttoggle:xdg/%s", name, N_("off"), why, enc);
		else
			rec_row("xdg\t%s\t%s\t-\t%s\ttoggle:xdg/%s", name, N_("off"), why, enc);
	}
	free(e);
}

int pane_startup(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");
	struct rc_list *rc = calloc(1, sizeof *rc);
	if (!rc) return 1;
	rows_autostart(rc);
	rows_services();
	rows_xdg(rc);
	free(rc);
	return 0;
}

/* ── the writes ─────────────────────────────────────────────────────────── */

static int set_user_unit(const char *unit, const char *val)
{
	if (!unit_name_ok(unit) || strchr(unit, '@'))
		return refuse("that does not look like a user service");
	if (plumbing(unit))
		return refuse("that unit is part of the session itself; this pane does not switch it");
	bool on = !strcmp(val, "on");
	/* --now: a switch that said Off with the thing still running, or On with
	 * nothing started until the next login, would read as broken. */
	char *a[] = { (char *)"systemctl", (char *)"--user", on ? (char *)"enable" : (char *)"disable",
	              (char *)"--now", (char *)unit, NULL };
	if (g_dry_run) {
		printf("would run: systemctl --user %s --now %s\n", on ? "enable" : "disable", unit);
		return 0;
	}
	return run_or_show(a);
}

static int set_xdg(const char *id, const char *val)
{
	if (strcmp(val, "on"))
		return refuse("an autostart entry is switched off by switching off its line in synui's list");
	struct xdg_entry *e = calloc(64, sizeof *e);
	if (!e) return 1;
	int n = xdg_collect(e, 64), rc = -1;
	for (int i = 0; i < n; i++) {
		if (strcmp(e[i].id, id)) continue;
		rc = check_command(e[i].exec);
		if (!rc) rc = rc_edit(e[i].exec, 'a');
		break;
	}
	free(e);
	if (rc < 0) return refuse("no autostart entry by that name");
	return rc;
}

int startup_set(const char *key, const char *val)
{
	char arg[1024];

	if (!strcmp(key, "autostart-add")) {
		int rc = check_command(val);
		return rc ? rc : rc_edit(val, 'a');
	}
	if (!strncmp(key, "autostart/", 10)) {
		if (!pct_decode(key + 10, arg, sizeof arg)) return refuse("that entry name is malformed");
		if (!strcmp(val, "on"))     { int rc = check_command(arg); return rc ? rc : rc_edit(arg, 'a'); }
		if (!strcmp(val, "off"))    return rc_edit(arg, 'o');
		if (!strcmp(val, "remove")) return rc_edit(arg, 'r');
		return refuse("autostart takes on, off or remove");
	}
	if (!strncmp(key, "user-unit/", 10)) {
		if (strcmp(val, "on") && strcmp(val, "off")) return refuse("a service takes on or off");
		return set_user_unit(key + 10, val);
	}
	if (!strncmp(key, "xdg/", 4)) {
		if (!pct_decode(key + 4, arg, sizeof arg) || strchr(arg, '/'))
			return refuse("that entry name is malformed");
		return set_xdg(arg, val);
	}
	return -1;   /* not ours */
}
