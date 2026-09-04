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
#include <strings.h>

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
		rec_row("device\t-\t%s\t-\t%s\t-",
		        N_("unknown"), N_("nmcli reported nothing"));
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

/* ── Addresses ───────────────────────────────────────────────────────────────
 *
 * The device rows above say WHICH interfaces exist and whether they are up.
 * They never said what any of them IS — and the two questions people actually
 * arrive at this pane with are "what address is this machine on" and "what is
 * this card's MAC", neither of which the pane could answer. The second one got
 * sharper when `syn-remote wakeable` shipped: a magic packet is addressed to a
 * hardware address, so waking this machine from another one means reading a
 * MAC off it first, and the only place that existed was a terminal.
 *
 * ⚠ ONE nmcli CALL FOR EVERY DEVICE, parsed by FIELD NAME rather than by
 * position. `device show` prints `FIELD:value` blocks separated by a blank
 * line, and which fields appear depends on the device — a disconnected wifi
 * card has a GENERAL.HWADDR and no IP4.ADDRESS at all, so anything counting
 * lines reads the next device's address as this one's.
 *
 * ⚠ AND THE VALUE IS EVERYTHING AFTER THE FIRST COLON, not up to the next one.
 * `GENERAL.HWADDR:BC:FC:E7:E8:FD:E3` is one field whose value is full of
 * colons, and an IPv6 address is worse. This is the one place in the file that
 * does NOT want next_field(): `device show` is a two-column form, not the
 * n-column one the escaping in next_field() exists for.
 */
struct netdev {
	char dev[64];
	char type[32];
	char mac[32];
	char ip4[192];
	char ip6[192];
	char gw[64];
	char dns[160];
};

/* Append one more address to a comma-joined cell, silently dropping any that
 * would not fit — a machine with more IPv6 addresses than the cell holds is
 * still better served by the first few than by a truncated last one. */
static void addr_join(char *dst, size_t cap, const char *v)
{
	if (!v || !*v) return;
	size_t l = strlen(dst);
	size_t need = strlen(v) + (l ? 2 : 0);
	if (l + need + 1 > cap) return;
	snprintf(dst + l, cap - l, "%s%s", l ? ", " : "", v);
}

/* True for a field name with or without nmcli's `[n]` index suffix:
 * IP4.ADDRESS is written IP4.ADDRESS[1] when there is one of them and
 * IP4.ADDRESS[1], [2], … when there are several. */
static int field_is(const char *name, const char *want)
{
	size_t l = strlen(want);
	return !strncmp(name, want, l) && (name[l] == '\0' || name[l] == '[');
}

static void addresses(void)
{
	char out[32768] = "";
	char *argv[] = { (char *)"nmcli", (char *)"-t", (char *)"-f",
	                 (char *)"GENERAL.DEVICE,GENERAL.TYPE,GENERAL.HWADDR,"
	                         "IP4.ADDRESS,IP4.GATEWAY,IP4.DNS,IP6.ADDRESS",
	                 (char *)"device", (char *)"show", NULL };
	if (run_capture(argv, out, sizeof out) != 0 || !out[0]) return;

	struct netdev devs[24];
	int n = 0;
	memset(devs, 0, sizeof devs);

	for (char *line = out, *eol; line && *line; line = eol) {
		eol = strchr(line, '\n');
		if (eol) *eol++ = '\0';
		char *colon = strchr(line, ':');
		if (!colon) continue;          /* the blank line between devices */
		*colon = '\0';
		const char *name = line, *val = colon + 1;

		/* GENERAL.DEVICE opens a device. Everything after it belongs to that
		 * one until the next, so a field arriving before any DEVICE line —
		 * which nothing produces, but which would otherwise write off the
		 * front of the array — is dropped. */
		if (field_is(name, "GENERAL.DEVICE")) {
			if (n == (int)(sizeof devs / sizeof devs[0])) break;
			snprintf(devs[n].dev, sizeof devs[n].dev, "%s", val);
			n++;
			continue;
		}
		if (!n || !*val) continue;
		struct netdev *d = &devs[n - 1];

		if      (field_is(name, "GENERAL.TYPE"))   snprintf(d->type, sizeof d->type, "%s", val);
		else if (field_is(name, "GENERAL.HWADDR")) snprintf(d->mac, sizeof d->mac, "%s", val);
		else if (field_is(name, "IP4.GATEWAY"))    snprintf(d->gw, sizeof d->gw, "%s", val);
		else if (field_is(name, "IP4.ADDRESS"))    addr_join(d->ip4, sizeof d->ip4, val);
		else if (field_is(name, "IP4.DNS"))        addr_join(d->dns, sizeof d->dns, val);
		/* ⛔ NOT THE LINK-LOCAL ONE. Every interface has an fe80:: address at
		 * all times, it is the same length as a real one, and it cannot be
		 * used to reach anything without naming the interface alongside it —
		 * so listing it triples the width of the cell to say nothing. An
		 * interface whose only IPv6 address is link-local has no IPv6
		 * address worth reporting, and the row says so by leaving it out. */
		else if (field_is(name, "IP6.ADDRESS")) {
			if (strncasecmp(val, "fe80:", 5))
				addr_join(d->ip6, sizeof d->ip6, val);
		}
	}

	for (int i = 0; i < n; i++) {
		struct netdev *d = &devs[i];
		tsv_clean(d->dev); tsv_clean(d->type); tsv_clean(d->mac);
		tsv_clean(d->ip4); tsv_clean(d->ip6);
		tsv_clean(d->gw);  tsv_clean(d->dns);
	}

	/* Loopback carries no address anybody configures and its MAC is all
	 * zeroes; the p2p pseudo-device is skipped for the same reason the device
	 * list skips it. An interface with neither a hardware address nor an
	 * address of its own is a row with nothing in it. */
	int shown[24];
	int m = 0;
	for (int i = 0; i < n; i++) {
		struct netdev *d = &devs[i];
		if (!strcmp(d->type, "loopback") || !strcmp(d->type, "wifi-p2p")) continue;
		if (!d->mac[0] && !d->ip4[0] && !d->ip6[0]) continue;
		shown[m++] = i;
	}

	/* ⛔ THE HARDWARE ADDRESSES TOGETHER, THEN THE IP ONES. Grouped by kind
	 * rather than interleaved per interface, because the kind column is what
	 * the window draws as the left edge of the block — interleaving them makes
	 * two alternating one-row blocks out of what a person reads as two lists. */
	for (int j = 0; j < m; j++) {
		struct netdev *d = &devs[shown[j]];
		const char *detail;
		if (!strcmp(d->type, "ethernet"))
			detail = N_("The wired card's hardware address. A magic packet is addressed to this one — `syn-remote wakeable on` arms this card, and then another machine on this network can wake this one while it sleeps.");
		else if (!strcmp(d->type, "wifi"))
			detail = N_("This card's hardware address. ⚠ Wi-Fi cannot be woken by a magic packet: the card does not stay associated through a suspend, so only a wired card can be.");
		else if (!strcmp(d->type, "bridge"))
			detail = N_("A bridge this machine runs for containers or virtual machines rather than a physical card. A guest on it sees this address as its gateway.");
		else
			detail = N_("The hardware address of this interface.");

		rec_row("mac\t%s\t%s\t-\t%s\t-",
		        d->dev, d->mac[0] ? d->mac : N_("none"), detail);
	}

	for (int j = 0; j < m; j++) {
		struct netdev *d = &devs[shown[j]];
		if (!d->ip4[0] && !d->ip6[0]) continue;
		char val[384] = "";
		addr_join(val, sizeof val, d->ip4);
		addr_join(val, sizeof val, d->ip6);
		rec_row("ip\t%s\t%s\t-\t%s\t-", d->dev, val,
		        N_("The addresses this interface holds right now, IPv4 first. A lease can change at any time, so an address is not a permanent way to find this machine again."));
	}

	/* ⚠ ONE OF EACH, NOT ONE PER INTERFACE. Every device carries an
	 * IP4.GATEWAY field and it is empty on all but the one holding the default
	 * route — a bridge has an address and no way off the machine — so a row per
	 * interface would be a column of blanks around the single answer. */
	for (int j = 0; j < m; j++) {
		if (!devs[shown[j]].gw[0]) continue;
		rec_row("ip\t%s\t%s\t-\t%s\t-", N_("gateway"), devs[shown[j]].gw,
		        N_("Where anything that is not on this network is sent. A machine with an address and no gateway reaches the local network and nothing past it."));
		break;
	}
	for (int j = 0; j < m; j++) {
		if (!devs[shown[j]].dns[0]) continue;
		rec_row("ip\t%s\t%s\t-\t%s\t-", N_("nameservers"), devs[shown[j]].dns,
		        N_("The servers that turn a name into an address. A network that is up and still cannot open a site is usually this."));
		break;
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
	        which, out[0] ? out : N_("unknown"), which,
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

		/* ⛔ THE SENTENCE IS AN ARGUMENT, NOT PART OF THE FORMAT. Written into
		 * the format string it is drawn on screen and reaches no template at
		 * all — xgettext extracts the format as one msgid, and the cell the
		 * window looks up is that sentence with a second one appended, which
		 * matches nothing. */
		rec_row("firewall\t%s\t%s\t%s\t%s %s\t-",
		        N_("container links"), n ? list : N_("none"), stale ? N_("warn") : "-",
		        N_("Bridges this machine serves DHCP and DNS on for a container "
		           "or VM (Waydroid, libvirt, Docker). A guest asks for its "
		           "address from 0.0.0.0, which the default-drop policy would "
		           "otherwise eat — the guest then has no network and nothing "
		           "says firewall."),
		        stale
		          ? N_("⚠ synnet last applied a different number of these; "
		               "`sudo synnet --firewall` loads the current list.")
		          : N_("Add one with `sudo synnet --trust-if <iface>`."));
	}

	/* Only when it has actually happened. A zero here would be a row about
	 * nothing, and the pane is long enough already. */
	if (reasserts[0] && strcmp(reasserts, "0"))
		rec_row("firewall\t%s\t%s\t%s\t%s\t-",
		        N_("rebuilt"), reasserts, N_("warn"),
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
		        "synnet.service", out[0] ? out : N_("not installed"),
		        !strcmp(out, N_("active")) ? "-" : N_("warn"),
		        N_("The daemon that applies all of this. The rules outlive it, so a stopped synnet leaves the last ruleset in place and stops maintaining it."));

		out[0] = '\0';
		char *argv[] = { (char *)"systemctl", (char *)"is-active",
		                 (char *)"nftables.service", NULL };
		run_capture(argv, out, sizeof out);
		out[strcspn(out, "\n")] = '\0';
		tsv_clean(out);
		rec_row("firewall\t%s\t%s\t-\t%s\t-",
		        "nftables.service", out[0] ? out : N_("not installed"),
		        N_("Arch's own firewall service, which SynapseOS does not use — synnet manages its table whether or not this is active."));
	}
}

int pane_network(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	if (!have_cmd("nmcli")) {
		rec_row("device\t-\t%s\t-\t%s\t-",
		        N_("unknown"), N_("NetworkManager is not installed"));
	} else {
		devices();
		addresses();
		radio("wifi");
		radio(N_("wwan"));
	}

	firewall_rows();
	return 0;
}
