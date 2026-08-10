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
		rec_row("device\t-\tunknown\t-\tnmcli reported nothing\t-");
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
		snprintf(c, sizeof c, "%s", conn && *conn ? conn : "no connection");
		tsv_clean(d); tsv_clean(t); tsv_clean(s); tsv_clean(c);

		rec_row("device\t%s\t%s\t%s\t%s\t-", d, t, s, c);
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

int pane_network(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	if (!have_cmd("nmcli")) {
		rec_row("device\t-\tunknown\t-\tNetworkManager is not installed\t-");
	} else {
		devices();
		radio("wifi");
		radio("wwan");
	}

	/* ── Firewall ─────────────────────────────────────────────────────── */
	/*
	 * `nft list ruleset` needs root, and a settings pane must not be a thing
	 * you have to sudo to read. So this asks a question an ordinary user CAN
	 * answer — is the nftables service up, does the kernel module exist — and
	 * says plainly that the ruleset itself was not read, rather than printing
	 * "empty" and letting that be mistaken for "no rules".
	 */
	if (have_cmd("systemctl")) {
		char out[128] = "";
		char *argv[] = { (char *)"systemctl", (char *)"is-active",
		                 (char *)"nftables.service", NULL };
		run_capture(argv, out, sizeof out);
		out[strcspn(out, "\n")] = '\0';
		tsv_clean(out);
		rec_row("firewall\tnftables.service\t%s\t-\t"
		        "synnet manages its own table whether or not this is active\t-",
		        out[0] ? out : "not installed");

		out[0] = '\0';
		char *a2[] = { (char *)"systemctl", (char *)"is-active",
		               (char *)"synnet.service", NULL };
		run_capture(a2, out, sizeof out);
		out[strcspn(out, "\n")] = '\0';
		tsv_clean(out);
		rec_row("firewall\tsynnet.service\t%s\t-\t"
		        "the egress policy and the default-deny input chain\t-",
		        out[0] ? out : "not installed");
	}

	rec_row("firewall\truleset\tnot read\t-\t"
	        "`nft list ruleset` needs root; run it yourself rather than trust "
	        "an empty table here\t-");

	return 0;
}
