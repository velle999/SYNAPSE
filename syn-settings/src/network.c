/* syn-settings — the Network pane.
 *
 * NetworkManager owns the answer and nmcli prints it in a terse, stable,
 * colon-separated form (`-t`) that exists precisely so something else can
 * parse it. Using the pretty output instead — which is localised and reflows
 * with terminal width — is how a parser starts failing in another language.
 *
 * The firewall rows are here rather than in a security pane because this is
 * where somebody looks when the network is wrong, and "the firewall has no
 * ruleset" is a network answer before it is a security one.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* nmcli -t escapes a literal colon inside a field as "\:", so splitting on
 * every colon would cut a connection name called "Home:5G" in half. Walk it. */
static char *next_field(char **cursor)
{
	char *s = *cursor;
	if (!s) return NULL;

	char *out = s, *w = s;
	while (*s && *s != ':') {
		if (*s == '\\' && s[1]) s++;   /* take the escaped char literally */
		*w++ = *s++;
	}
	int more = (*s == ':');
	*w = '\0';
	*cursor = more ? s + 1 : NULL;
	return out;
}

static void devices(void)
{
	char out[16384] = "";
	char *argv[] = { (char *)"nmcli", (char *)"-t",
	                 (char *)"-f", (char *)"DEVICE,TYPE,STATE,CONNECTION",
	                 (char *)"device", NULL };
	if (run_capture(argv, out, sizeof out) != 0 || !out[0]) {
		rec_row("device\t-\tunknown\t-\t%s\t-",
		        N_("nmcli reported nothing"));
		return;
	}

	for (char *line = out, *eol; line && *line; line = eol) {
		eol = strchr(line, '\n');
		if (eol) *eol++ = '\0';
		if (!*line) continue;

		char *cur = line;
		const char *dev  = next_field(&cur);
		const char *type = next_field(&cur);
		const char *st   = next_field(&cur);
		const char *conn = next_field(&cur);

		if (!dev || !*dev) continue;
		/* Its own p2p pseudo-device is not a thing anyone configures, and it
		 * is permanently "disconnected" in a way that reads as a fault. */
		if (type && !strcmp(type, "wifi-p2p")) continue;

		char d[256], t[64], s[64], c[256];
		snprintf(d, sizeof d, "%s", dev);
		snprintf(t, sizeof t, "%s", type && *type ? type : "-");
		snprintf(s, sizeof s, "%s", st && *st ? st : "-");
		snprintf(c, sizeof c, "%s", conn && *conn ? conn : N_("no connection"));
		tsv_clean(d); tsv_clean(t); tsv_clean(s); tsv_clean(c);

		/* Loopback carries no action: see do_device. Everything else — the
		 * wired link included, which is the whole point — can be brought up
		 * and down. */
		/* Sized to hold "device:" plus the widest interface name this
		 * function can produce, so the compiler does not have to assume
		 * truncation. */
		char act[264] = "-";
		if (strcmp(d, "lo"))
			snprintf(act, sizeof act, "device:%s", d);

		rec_row("device\t%s\t%s\t%s\t%s\t%s", d, t, s, c, act);
	}
}

static void radio(const char *which)
{
	char out[128] = "";
	char *argv[] = { (char *)"nmcli", (char *)"radio", (char *)which, NULL };
	run_capture(argv, out, sizeof out);
	out[strcspn(out, "\n")] = '\0';
	tsv_clean(out);
	/* Only wifi is offered as a toggle. wwan is reported because it exists,
	 * and left alone because this machine has no modem to switch on and a
	 * button that always errors teaches people to distrust the others. */
	rec_row("radio\t%s\t%s\t-\tnmcli radio %s\t%s",
	        which, out[0] ? out : "unknown", which,
	        strcmp(which, "wifi") == 0 ? "toggle:wifi" : "-");
}

/* ── Firewall ────────────────────────────────────────────────────────────────
 *
 * These rows used to say "ruleset: not read", with an honest note that
 * `nft list ruleset` needs root and a settings pane must not be something you
 * sudo to read. The note was right and the answer was still useless: the box
 * HAS a firewall — synnet applies a default-drop input chain at every start —
 * and the pane could not say so, which is the same silence that had it believed
 * missing altogether.
 *
 * synnet publishes what it asserted to /run/synnet/firewall.state now,
 * world-readable, precisely so an unprivileged reader can answer. So this reads
 * that, and reports three separate things rather than one blurred one:
 *
 *   - the PREFERENCE (/etc/synnet/firewall), which is a decision;
 *   - the STATE (/run/…/firewall.state), which is what the daemon last managed;
 *   - the SERVICE, because none of the above happens if synnet is not running.
 *
 * ⚠ Still nothing here reads the kernel. "What synnet believes" is not "what
 * nftables holds", and a pane that presented the first as the second would be
 * making exactly the claim that this whole area got wrong once already.
 */

/* The two synnet files, overridable so the suite can drive every state.
 *
 * ⚠ /run and /etc are not writable by a test, and the states worth testing here
 * are the ones nobody can produce on demand: a failed apply, a firewall that
 * has been rebuilt nine times, a daemon that has not published yet. The same
 * seam synnet itself carries, and for the same reason. */
static const char *fw_state_file(void)
{
	const char *e = getenv("SYNNET_FW_STATE_FILE");
	return (e && *e) ? e : "/run/synnet/firewall.state";
}
static const char *fw_pref_file(void)
{
	const char *e = getenv("SYNNET_FW_PREF_FILE");
	return (e && *e) ? e : "/etc/synnet/firewall";
}
static const char *fw_ifaces_file(void)
{
	const char *e = getenv("SYNNET_FW_IFACES_FILE");
	return (e && *e) ? e : "/etc/synnet/trusted-ifaces";
}

/* One key out of a `key=value` file, or "" — used for both synnet files. */
static void kv_get(const char *path, const char *key, char *out, size_t outlen)
{
	out[0] = '\0';
	FILE *f = fopen(path, "r");
	if (!f) return;

	char line[256];
	size_t klen = strlen(key);
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (!strncmp(line, key, klen) && line[klen] == '=') {
			snprintf(out, outlen, "%s", line + klen + 1);
			break;
		}
	}
	fclose(f);
}

int synnet_firewall_on(void)
{
	FILE *f = fopen(fw_pref_file(), "r");
	if (!f) return 1;                 /* absent means on — see synnet.h */
	char buf[32] = "";
	if (!fgets(buf, sizeof buf, f)) { fclose(f); return 1; }
	fclose(f);
	buf[strcspn(buf, "\r\n")] = '\0';
	return strcmp(buf, "off") != 0;
}

static void firewall_rows(void)
{
	int want = synnet_firewall_on();

	char st[64] = "", reasserts[32] = "";
	kv_get(fw_state_file(), "state", st, sizeof st);
	kv_get(fw_state_file(), "reasserts", reasserts, sizeof reasserts);

	const char *value, *state, *detail;
	if (!want) {
		value  = "off";
		state  = "off";
		detail = "Nothing inbound is filtered. This machine answers anything "
		         "that reaches it, on any network it joins.";
	} else if (!st[0]) {
		value  = "unknown";
		state  = "unknown";
		detail = "synnet has not published a state this boot — it may not have "
		         "started yet, or may predate the version that reports one.";
	} else if (!strcmp(st, "active")) {
		value  = "on";
		state  = "active";
		detail = N_("Default-drop inbound. Loopback, replies to connections this "
		         "machine made, ICMP, and anything from the local network are "
		         "let through; unsolicited traffic from a public address is not.");
	} else if (!strcmp(st, "off")) {
		/* The daemon publishes this after an explicit switch-off, so the
		 * preference and the state agree and there is nothing to reconcile. */
		value  = "off";
		state  = "off";
		detail = "Switched off. Nothing inbound is filtered.";
	} else {
		value  = "failed";
		state  = "failed";
		detail = "synnet could not load the ruleset — this machine is NOT "
		         "filtered. `journalctl -u synnet` has what nft said.";
	}

	rec_row("firewall\t%s\t%s\t%s\t%s\tchoice:firewall",
	        N_("input filtering"), value, state, detail);

	/* ── Container / VM links ────────────────────────────────────────────
	 *
	 * ⚠ THE ROW EXISTS BECAUSE THE FAILURE IS SILENT AND LOOKS LIKE ANYTHING
	 * BUT A FIREWALL. A Waydroid or libvirt guest gets its address by DHCP, and
	 * that first request is sent from 0.0.0.0 — before the guest has anything
	 * the LAN-trust rule can recognise, so the drop policy eats it. The symptom
	 * is "the container has no internet", which sends people to the container's
	 * own settings, to DNS, to the bridge — anywhere but here. Naming the bridge
	 * in /etc/synnet/trusted-ifaces fixes it, and this row is where somebody
	 * looking at a broken container network can see whether that has been done.
	 *
	 * Read from the FILE rather than the published count, so a link that has
	 * been added but not applied shows up as the name it is rather than as an
	 * absence — and so the pane answers on a machine where synnet has never
	 * started. The count from the state file is only used to flag the mismatch.
	 */
	{
		char list[256] = "";
		unsigned n = 0;
		FILE *f = fopen(fw_ifaces_file(), "r");
		if (f) {
			char line[256];
			while (fgets(line, sizeof line, f)) {
				line[strcspn(line, "\r\n")] = '\0';
				char *h = strchr(line, '#');
				if (h) *h = '\0';
				char *p = line;
				while (*p == ' ' || *p == '\t') p++;
				size_t l = strlen(p);
				while (l && (p[l-1] == ' ' || p[l-1] == '\t')) p[--l] = '\0';
				if (!*p) continue;
				n++;
				if (strlen(list) + strlen(p) + 3 < sizeof list)
					snprintf(list + strlen(list), sizeof list - strlen(list),
					         "%s%s", list[0] ? ", " : "", p);
			}
			fclose(f);
		}
		tsv_clean(list);

		char applied[32] = "";
		kv_get(fw_state_file(), "links", applied, sizeof applied);

		/* ⚠ Only a mismatch on a LIVE firewall is news. An off or unpublished
		 * firewall not matching the list is the definition of those states, not
		 * a fault to send somebody chasing. */
		int stale = want && !strcmp(st, "active") && applied[0] &&
		            (unsigned)atoi(applied) != n;

		rec_row("firewall\t%s\t%s\t%s\t"
		        "Bridges this machine serves DHCP and DNS on for a container or VM (Waydroid, libvirt, Docker). A guest asks for its address from 0.0.0.0, which the default-drop policy would otherwise eat — the guest then has no network and nothing says firewall. %s\t"
		        "-",
		        N_("container links"), n ? list : "none", stale ? "warn" : "-",
		        stale
		          ? "⚠ synnet last applied a different number of these; "
		            "`sudo synnet --firewall` loads the current list."
		          : "Add one with `sudo synnet --trust-if <iface>`.");
	}

	/* Only when it has actually happened. A zero here would be a row about
	 * nothing, and the pane is long enough already. */
	if (reasserts[0] && strcmp(reasserts, "0"))
		rec_row("firewall\t%s\t%s\twarn\t%s\t-",
		        N_("rebuilt"), reasserts,
		        N_("The firewall has gone missing and been rebuilt this many times since synnet started. Something on this machine is flushing nftables."));

	if (have_cmd("systemctl")) {
		char out[128] = "";
		char *a2[] = { (char *)"systemctl", (char *)"is-active",
		               (char *)"synnet.service", NULL };
		run_capture(a2, out, sizeof out);
		out[strcspn(out, "\n")] = '\0';
		tsv_clean(out);
		/* ⚠ The service row carries the unit action, and it matters more than
		 * it looks: none of the above happens if synnet is not running, and
		 * "the firewall says active but the daemon is dead" is a real state —
		 * the chain outlives the process that loaded it. */
		rec_row("firewall\t%s\t%s\t%s\t%s\tunit:synnet.service",
		        "synnet.service", out[0] ? out : "not installed",
		        !strcmp(out, "active") ? "-" : "warn",
		        N_("The daemon that applies all of this. The rules outlive it, so a stopped synnet leaves the last ruleset in place and stops maintaining it."));

		out[0] = '\0';
		char *argv[] = { (char *)"systemctl", (char *)"is-active",
		                 (char *)"nftables.service", NULL };
		run_capture(argv, out, sizeof out);
		out[strcspn(out, "\n")] = '\0';
		tsv_clean(out);
		rec_row("firewall\t%s\t%s\t-\t%s\t-",
		        "nftables.service", out[0] ? out : "not installed",
		        N_("Arch's own firewall service, which SynapseOS does not use — synnet manages its table whether or not this is active."));
	}
}

int pane_network(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	if (!have_cmd("nmcli")) {
		rec_row("device\t-\tunknown\t-\t%s\t-",
		        N_("NetworkManager is not installed"));
	} else {
		devices();
		radio("wifi");
		radio(N_("wwan"));
	}

	firewall_rows();
	return 0;
}
