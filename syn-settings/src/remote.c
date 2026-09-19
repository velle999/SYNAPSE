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

/* The same question of `syn-remote stream status --rec`.
 *
 * ⚠ A SECOND FUNCTION RATHER THAN A PARAMETER, because they are two records
 * with two lifetimes: one describes this desktop's VNC server and one describes
 * a streaming host that may not be installed. Sharing a buffer between them is
 * how a pane ends up drawing a row from whichever ran last. */
static void stream_field(const char *want, char *out, size_t cap)
{
	out[0] = '\0';
	if (!have_cmd("syn-remote"))
		return;

	char rec[4096] = "";
	char *a[] = { (char *)"syn-remote", (char *)"stream",
	              (char *)"status", (char *)"--rec", NULL };
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
	/* The streaming half. Same record, asked for by name like everything
	 * above — `syn-remote status --rec` grew these rows when it grew the
	 * server behind them. */
	char st_run[32], st_login[32], st_conn[32], st_port[32];
	char st_disp[64], st_serving[64], st_solo[32], sunshine[32];
	remote_field("running",     running, sizeof running);
	remote_field("atlogin",     atlogin, sizeof atlogin);
	remote_field("scope",       scope,   sizeof scope);
	remote_field("connections", conns,   sizeof conns);
	remote_field("auth",        auth,    sizeof auth);
	remote_field("port",        port,    sizeof port);
	remote_field("session",     session, sizeof session);
	remote_field("wayvnc",      wayvnc,  sizeof wayvnc);
	remote_field("streaming",          st_run,     sizeof st_run);
	remote_field("stream_connections", st_conn,    sizeof st_conn);
	remote_field("stream_port",        st_port,    sizeof st_port);
	/* ⚠ These two are on the stream's OWN record, not the main one: they are
	 * settings of a server rather than facts about this desktop, and putting
	 * them in `status --rec` would have made that record answer for two
	 * different things. */
	stream_field("atlogin", st_login,   sizeof st_login);
	stream_field("display", st_disp,    sizeof st_disp);
	stream_field("serving", st_serving, sizeof st_serving);
	stream_field("solo",    st_solo,    sizeof st_solo);
	/* ⚠ OFF THE STREAM RECORD. `sunshine` is a row of `stream status
	 * --rec` and not of the main one — asked for on the wrong record it
	 * comes back empty, which is neither "yes" nor "no" and drew the
	 * settings rows on a machine that cannot stream at all. */
	stream_field("sunshine", sunshine,  sizeof sunshine);

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

	/* ── Streaming ────────────────────────────────────────────────────── */
	/*
	 * ⛔ A SECOND SWITCH, AND THE ROW SAYS WHY IT IS NOT THE SAME ONE. This is
	 * an encoded video stream rather than rectangles of pixels, which is what
	 * makes a desktop usable at 1440p120 from another room — and it is on the
	 * network the moment it is on. sunshine binds every interface and announces
	 * itself over mDNS; there is no loopback-only streaming host, so the
	 * "Reachable from" row above does not apply to it and the detail says so
	 * here instead.
	 */
	rec_row("switch\t%s\t%s\t%s\t%s\ttoggle:remote-stream",
	        N_("Streaming (Moonlight)"),
	        !strcmp(st_login, "yes") ? "on" : "off",
	        !strcmp(st_run, "yes") ? N_("running") : N_("stopped"),
	        N_("Video rather than pixels \xc2\xb7 smooth enough for games and full-screen video, and reachable by any Moonlight client on this network"));

	if (!strcmp(sunshine, "no")) {
		rec_row("switch\t%s\tunavailable\t-\t%s\t-",
		        N_("Streaming server"),
		        N_("sunshine is not installed, so nothing can be streamed"));
	} else {
		/* ⛔ WHAT IT SHOWS IS THE SETTING PEOPLE COME HERE FOR. A display of
		 * its own is a screen this desk does not have, at whatever resolution
		 * the client asked for; the main screen is what is in the room. */
		/* ⚠ THE COMPARISON HAPPENS BEFORE THE CALL, not inside it. Every
		 * string literal in a rec_row argument is checked for unmarked prose,
		 * and a four-letter token like this one is indistinguishable from a
		 * drawn English word to that check — which is the right rule, kept by
		 * moving the token rather than by widening the rule. */
		const int shows_main = !strcmp(st_disp, "auto");
		rec_row("choice\t%s\t%s\t%s\t%s\tchoice:remote-stream-display",
		        N_("Stream shows"),
		        shows_main ? N_("the main screen")
		                   : N_("a display of its own"),
		        /* ⚠ The head that actually exists, beside the setting that
		         * asked for it. They differ exactly when synui could not make
		         * one, which is the case that otherwise looks like nothing
		         * happened. */
		        st_serving[0] ? st_serving : "-",
		        shows_main
		            ? N_("Whoever connects sees this desk as it is, at its resolution")
		            : N_("synui grows a screen with no monitor behind it, sized to whatever the connecting client asks for"));

		rec_row("switch\t%s\t%s\t-\t%s\ttoggle:remote-stream-solo",
		        N_("Blank this machine while streaming"),
		        !strcmp(st_solo, "on") ? "on" : "off",
		        N_("The screens in this room go dark while somebody is connected, and come back when they leave"));

		const char *shown_stream_port = st_port[0] ? st_port : "47989";
		rec_row("value\t%s\t%s\t-\t%s\t-",
		        N_("Stream port"), shown_stream_port,
		        N_("what a Moonlight client is pointed at \xc2\xb7 its settings page is one port above"));

		/* ⛔ PAIRING IS TYPED HERE, ON THE MACHINE BEING STREAMED. Moonlight
		 * shows a PIN on the CLIENT and waits for this end to enter it, and
		 * the only other place to do that was `syn-remote stream pair` at a
		 * prompt — or sunshine's own web page, which wants a password this
		 * window never shows. The value is empty so the editor opens blank.
		 * ⚠ Only while the server runs: the PIN goes to sunshine's local
		 * API, and with nothing listening there is nothing to take it.
		 *
		 * ⛔ AND WHILE IT DOES NOT, THE ROW STARTS IT rather than saying
		 * so. It said "turn streaming on first" and did nothing when
		 * clicked — beside a switch already reading On, because the switch
		 * is "at every login" and the server had stopped under it. There
		 * was no control anywhere on the page that started it again. Its
		 * action is the switch's own, `stream on`, which enables and starts
		 * alike and is harmless to repeat. Starting on Pair instead would
		 * not do: Moonlight asks the RUNNING server before it shows a PIN,
		 * so there is no PIN to type until this has happened. */
		if (!strcmp(st_run, "yes"))
			rec_row("add\t%s\t\t-\t%s\tset:remote-stream-pair",
			        N_("Pair a Moonlight client"),
			        N_("type the four digits Moonlight shows when it adds this machine \xc2\xb7 once per device"));
		else if (!strcmp(st_login, "yes"))
			rec_row("add\t%s\t\t-\t%s\ttoggle:remote-stream",
			        N_("Pair a Moonlight client"),
			        N_("the streaming server has stopped \xc2\xb7 Turn on starts it again, then Moonlight can ask for a PIN"));
		else
			rec_row("add\t%s\t\t-\t%s\ttoggle:remote-stream",
			        N_("Pair a Moonlight client"),
			        N_("streaming is off \xc2\xb7 Turn on starts it, then Moonlight can ask for a PIN"));

		if (st_conn[0] && strcmp(st_conn, "0") != 0)
			rec_row("value\t%s\t%s\t-\t%s\t-",
			        N_("Streaming now"), st_conn,
			        N_("somebody is streaming this machine"));
	}

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
		/* ⚠ Start, stop and restart, and only on a unit that EXISTS —
		 * the same rule as power.c's rows. Enabling is the switch's job;
		 * see do_unit(). */
		const int have_vnc = en[0] && strcmp(en, "not-found") != 0;
		rec_row("unit\t%s\t%s\t%s\t%s\t%s",
		        "syn-remote.service", shown_en, shown_act,
		        N_("the server \xc2\xb7 the switch above is what turns it on"),
		        have_vnc ? "userunit:syn-remote.service" : "-");

		/* The streaming unit gets its own row for the reason the comment above
		 * gives: a switch and the thing it switches are two facts, and there
		 * are two switches on this page now. */
		char sen[64] = "", sact[64] = "";
		char *se[] = { (char *)"systemctl", (char *)"--user",
		               (char *)"is-enabled", (char *)"syn-remote-stream.service", NULL };
		char *sa[] = { (char *)"systemctl", (char *)"--user",
		               (char *)"is-active",  (char *)"syn-remote-stream.service", NULL };
		run_capture_quiet(se, sen, sizeof sen);
		run_capture_quiet(sa, sact, sizeof sact);
		sen[strcspn(sen, "\n")] = '\0';   tsv_clean(sen);
		sact[strcspn(sact, "\n")] = '\0'; tsv_clean(sact);
		const char *shown_sen  = sen[0]  ? sen  : "not installed";
		const char *shown_sact = sact[0] ? sact : "-";
		const int have_stream = sen[0] && strcmp(sen, "not-found") != 0;
		rec_row("unit\t%s\t%s\t%s\t%s\t%s",
		        "syn-remote-stream.service", shown_sen, shown_sact,
		        N_("the streaming server \xc2\xb7 the second switch above turns it on"),
		        have_stream ? "userunit:syn-remote-stream.service" : "-");
	}

	return 0;
}
