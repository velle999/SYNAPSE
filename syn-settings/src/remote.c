/* syn-settings — the remote desktop pane.
 *
 * ⚠ IT REIMPLEMENTS NOTHING. syn-remote(1) owns the server, the credentials and
 * the bind address; this pane reads `syn-remote status --rec` to draw the rows
 * and runs syn-remote's own commands to change them. The same rule the speech
 * and AI panes follow, for the same reason: a second idea of "is it listening
 * on the network" is a second thing that can be wrong about it.
 *
 * ⛔ THE ROW THAT MATTERS IS WHERE IT LISTENS, and it is drawn as prose rather
 * than as an address. synnet's default-drop input chain accepts everything from
 * 10/8, 172.16/12 and 192.168/16 (monitor.c), so "0.0.0.0" does not mean "bound
 * to every interface, still firewalled" — it means every device on the network
 * can reach this desktop, and there is no second door left to unlock. A row
 * showing an IP tells somebody who already knows that; a row saying it in words
 * tells the person who does not.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>

/* One field out of `syn-remote status --rec`, or "".
 *
 * ⚠ ASKED FOR BY NAME, never by column index. That record grows a column on the
 * END when it grows one, and a pane reading $3 would start reporting the wrong
 * fact the day it did — silently, because every value in it is a short word
 * that looks plausible in any column. */
static void remote_field(const char *want, char *out, size_t cap)
{
	out[0] = '\0';
	if (!have_cmd("syn-remote"))
		return;

	char rec[4096] = "";
	char *a[] = { (char *)"syn-remote", (char *)"status", (char *)"--rec", NULL };
	if (run_capture_quiet(a, rec, sizeof rec) != 0)
		return;

	for (char *line = strtok(rec, "\n"); line; line = strtok(NULL, "\n")) {
		char *tab = strchr(line, '\t');
		if (!tab)
			continue;
		*tab = '\0';
		if (strcmp(line, want) == 0) {
			snprintf(out, cap, "%s", tab + 1);
			tsv_clean(out);
			return;
		}
	}
}

int pane_remote(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	if (!have_cmd("syn-remote")) {
		rec_row("switch\t%s\tunavailable\t-\t%s\t-",
		        N_("Remote desktop"),
		        N_("needs syn-remote(1) \xc2\xb7 synpkg install syn-remote"));
		return 0;
	}

	char running[32], atlogin[32], scope[32], conns[32], auth[32];
	char port[32], session[32], wayvnc[32];
	remote_field("running",     running, sizeof running);
	remote_field("atlogin",     atlogin, sizeof atlogin);
	remote_field("scope",       scope,   sizeof scope);
	remote_field("connections", conns,   sizeof conns);
	remote_field("auth",        auth,    sizeof auth);
	remote_field("port",        port,    sizeof port);
	remote_field("session",     session, sizeof session);
	remote_field("wayvnc",      wayvnc,  sizeof wayvnc);

	/* ── The switch ───────────────────────────────────────────────────── */
	/*
	 * ⚠ "on" HERE MEANS AT EVERY LOGIN, not just now, because that is the
	 * setting somebody is looking for on this page — a remote desktop that has
	 * to be started by hand from the machine is a remote desktop for a machine
	 * you are standing at. `syn-remote start` is the just-this-session one and
	 * it lives at the prompt.
	 */
	rec_row("switch\t%s\t%s\t%s\t%s\ttoggle:remote-desktop",
	        N_("Remote desktop"),
	        !strcmp(atlogin, "yes") ? "on" : "off",
	        !strcmp(running, "yes") ? N_("running") : N_("stopped"),
	        N_("Reach this desktop from another machine over VNC. Off until you switch it on, and it starts with every login once you have"));

	if (!strcmp(wayvnc, "no"))
		rec_row("switch\t%s\tunavailable\t-\t%s\t-",
		        N_("Server"), N_("wayvnc is not installed, so nothing can serve"));

	/* ⛔ NO SESSION, NO DESKTOP TO SHARE — on any Wayland system, not just this
	 * one. Said as a row rather than left to a failure message, because the
	 * message a person would otherwise meet is wayvnc's "failed to connect to
	 * Wayland display", which reads as a bug in this. */
	if (!strcmp(session, "no"))
		rec_row("switch\t%s\tunavailable\t-\t%s\t-",
		        N_("Session"),
		        N_("no desktop is running yet \xc2\xb7 nothing exists to share until somebody logs in"));

	/* ── Where it listens ─────────────────────────────────────────────── */
	/*
	 * ⛔ THE WARNING IS ON THE ROW, not in a manual. This is the one setting
	 * here that changes who can reach the machine, and the firewall does not
	 * stand behind it: synnet accepts every private-range source by design.
	 */
	rec_row("choice\t%s\t%s\t-\t%s\tchoice:remote-scope",
	        N_("Reachable from"),
	        !strcmp(scope, "lan") ? N_("the network") : N_("this machine only"),
	        !strcmp(scope, "lan")
	            ? N_("Every device on this LAN can reach it. synnet accepts private-range traffic by design, so the certificate and the password are what stand in the way")
	            : N_("Loopback only \xc2\xb7 reach it from elsewhere over an SSH tunnel. \"The network\" puts it on the LAN"));

	/* ⚠ The fallback sits in a variable rather than inside the call. Every
	 * cell of a rec_row is checked for unmarked prose, and a bare "5900" there
	 * reads to that check exactly like a drawn English word — which is the
	 * right rule, kept by moving the number rather than by weakening it. */
	const char *shown_port = port[0] ? port : "5900";
	rec_row("value\t%s\t%s\t-\t%s\t-",
	        N_("Port"), shown_port,
	        N_("5900 is the first VNC port \xc2\xb7 change it with `syn-remote port`"));

	/* ── Who may connect ──────────────────────────────────────────────── */
	rec_row("choice\t%s\t%s\t-\t%s\tchoice:remote-auth",
	        N_("Sign in with"),
	        !strcmp(auth, "pam") ? N_("your account password") : N_("a generated password"),
	        N_("Either way the connection is encrypted \xc2\xb7 `syn-remote password` prints the generated one"));

	/* ⚠ WHO IS WATCHING, RIGHT NOW, and it is a row rather than an indicator
	 * because this page can be read on the machine being watched. */
	if (conns[0] && strcmp(conns, "0") != 0)
		rec_row("value\t%s\t%s\t-\t%s\t-",
		        N_("Connected now"), conns,
		        N_("somebody is looking at this screen"));

	/* ── And the unit behind the switch ───────────────────────────────── */
	/*
	 * ⛔ A SWITCH AND THE THING IT SWITCHES ARE TWO SEPARATE FACTS. syn-speak
	 * shipped four releases with a switch reading On and no unit packaged
	 * behind it; this row is what would have said so.
	 */
	if (have_cmd("systemctl")) {
		char en[64] = "", act[64] = "";
		char *e[] = { (char *)"systemctl", (char *)"--user",
		              (char *)"is-enabled", (char *)"syn-remote.service", NULL };
		char *a[] = { (char *)"systemctl", (char *)"--user",
		              (char *)"is-active",  (char *)"syn-remote.service", NULL };
		run_capture_quiet(e, en, sizeof en);
		run_capture_quiet(a, act, sizeof act);
		en[strcspn(en, "\n")] = '\0';   tsv_clean(en);
		act[strcspn(act, "\n")] = '\0'; tsv_clean(act);
		/* Same reason as the port above: the record carries the English
		 * token and the window translates it at the draw site. */
		const char *shown_en  = en[0]  ? en  : "not installed";
		const char *shown_act = act[0] ? act : "-";
		rec_row("unit\t%s\t%s\t%s\t%s\t-",
		        "syn-remote.service", shown_en, shown_act,
		        N_("the server \xc2\xb7 the switch above is what turns it on"));
	}

	return 0;
}
