/* netscan.c — find the shares on this network, before anything is mounted.
 *
 * volumes.c lists network places that ALREADY EXIST as paths: gvfs has mounted
 * them, so they live under /run/user/<uid>/gvfs and the rest of this program
 * treats them like any other directory. That is the whole of "network" in a
 * file manager once you know the address.
 *
 * This file is the part before that. Nothing on SynapseOS answered "what is on
 * this network" — you had to already know the server's name and type an
 * smb:// URI, which is a thing people do not know and cannot guess. So:
 * discovery, from the two places servers actually announce themselves.
 *
 * ── The two sources, and why both ──────────────────────────────────────────
 *
 *   mDNS / DNS-SD (avahi-browse). What a NAS, a Mac, a printer and a modern
 *   Samba announce. It is the right answer and it is not the whole answer.
 *
 *   NetBIOS (nmblookup, from smbclient). What a WINDOWS machine on a home
 *   network still answers to, and many of them announce nothing over mDNS at
 *   all. A file manager that browsed only mDNS would report "no network
 *   shares" in a house full of Windows PCs sharing folders.
 *
 * Neither is a port scan and this file must never become one. Sweeping the
 * subnet for 445 is slow, indistinguishable from an attack to anything
 * watching, and finds machines that never offered anything — announcement is
 * consent, and consent is what makes a share a "place".
 *
 * ── Shares, not just servers ───────────────────────────────────────────────
 *
 * A host is not something you can open. `smbclient -L` asks the server what it
 * offers, as a guest, and each share becomes a row with a real smb:// URI.
 * Administrative shares (IPC$, print$, ADMIN$, C$) are dropped: they are not
 * places, and offering a row that answers "access denied" is worse than
 * offering nothing. A server that refuses guest enumeration still lists as a
 * host row — you can open it and be asked for credentials, which is what
 * gvfs's own dialogue is for.
 *
 * ── Mounting is gvfs's job ─────────────────────────────────────────────────
 *
 * `gio mount` and nothing else. gvfs owns the credential prompt, the keyring
 * and the FUSE mount, and once it is mounted the share is a PATH — so the
 * sidebar row a scan produced and the sidebar row volumes.c produces are the
 * same kind of thing one step apart.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"
#include "i18n.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* How many hosts a scan will interrogate for their share list. Each one is an
 * smbclient round trip with its own timeout, and a scan that takes a minute is
 * a scan nobody waits for — the sidebar is blocked on it. Hosts past this
 * still list; only their share enumeration is skipped. */
#define NETSCAN_HOST_MAX 24

/* Seconds. Deliberately short and deliberately not configurable: this runs
 * while somebody is looking at a spinner in a sidebar. A machine that has not
 * answered in three seconds is not a machine you were about to open a file on,
 * and it will be there on the next scan. */
#define NETSCAN_TIMEOUT "3"

/* ── The samba tools refuse to run without a config file ───────────────────
 *
 * Arch's smbclient package ships NO /etc/samba/smb.conf — that file belongs to
 * the samba SERVER package, and a machine that only browses shares has no
 * reason to install a server. Without it every one of these tools stops with
 *
 *     Can't load /etc/samba/smb.conf - run testparm to debug it
 *
 * before doing any work at all, which on a stock SynapseOS made the whole
 * NetBIOS half of discovery dead and silent. `-s /dev/null` runs them on their
 * compiled-in defaults, which is exactly right for a guest browse.
 *
 * ⚠ Only when there is no config to read. A machine that HAS one has a
 * workgroup, an interface list or credentials written in it by somebody who
 * meant it, and overriding that with /dev/null would quietly ignore their
 * settings. This is a fallback, not a policy.
 */
static const char *smb_conf_flag(void)
{
	static int checked = 0;
	static const char *flag = NULL;
	if (!checked) {
		checked = 1;
		flag = (access("/etc/samba/smb.conf", R_OK) == 0) ? NULL : "/dev/null";
	}
	return flag;
}

typedef struct {
	char *host;     /* what to put in the URI — a name if there is one */
	char *label;    /* what to show */
	char *service;  /* smb, sftp, nfs, afp */
} nethost_t;

typedef struct {
	nethost_t *v;
	size_t n, cap;
} hostlist_t;

/* Are these two announcements the same machine?
 *
 * mDNS gives "nas.local" and NetBIOS gives "NAS" — one box, two names, and a
 * plain string compare lists it twice, which reads as two servers you have to
 * try in turn to find out they are the same one. The first LABEL is what both
 * protocols agree on, so that is what is compared, case-insensitively (NetBIOS
 * shouts, mDNS does not).
 *
 * An ADDRESS is compared whole: 192.168.40.10 and 192.168.40.99 share their
 * first label and are not the same machine at all. That is the case a
 * short-name compare gets catastrophically wrong, so it is checked first.
 */
static bool same_machine(const char *a, const char *b)
{
	bool a_ip = strspn(a, "0123456789.") == strlen(a);
	bool b_ip = strspn(b, "0123456789.") == strlen(b);
	if (a_ip || b_ip)
		return strcmp(a, b) == 0;

	size_t la = strcspn(a, "."), lb = strcspn(b, ".");
	return la == lb && strncasecmp(a, b, la) == 0;
}

static void hosts_add(hostlist_t *l, const char *host, const char *label,
                      const char *service)
{
	if (!host || !*host)
		return;

	/* The same machine is routinely announced twice — once by mDNS as
	 * "nas.local" and once by NetBIOS as "NAS" — and twice over if it offers
	 * two protocols. Deduped on machine+service, because smb and sftp on one
	 * box are genuinely two places.
	 *
	 * FIRST ONE WINS, and mDNS is scanned first on purpose: "nas.local"
	 * resolves through the same responder that announced it, while a bare
	 * NetBIOS name needs WINS or a broadcast lookup that gvfs may not do. The
	 * name that is kept is the one a mount will still resolve tomorrow. */
	for (size_t i = 0; i < l->n; i++)
		if (same_machine(l->v[i].host, host) && !strcmp(l->v[i].service, service))
			return;

	if (l->n == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 8;
		l->v = xrealloc(l->v, l->cap * sizeof(*l->v));
	}
	l->v[l->n].host    = xstrdup(host);
	l->v[l->n].label   = xstrdup(label && *label ? label : host);
	l->v[l->n].service = xstrdup(service);
	l->n++;
}

static void hosts_free(hostlist_t *l)
{
	for (size_t i = 0; i < l->n; i++) {
		free(l->v[i].host);
		free(l->v[i].label);
		free(l->v[i].service);
	}
	free(l->v);
	l->v = NULL;
	l->n = l->cap = 0;
}

/* ── mDNS ──────────────────────────────────────────────────────────────────
 *
 * `avahi-browse -ptrk <type>` is the parsable form: -p semicolon-separated, -r
 * resolved (so a record carries an address), -k no service-type database
 * lookup, and -t TERMINATES rather than sitting there printing changes for
 * ever. Without -t this call never returns and the sidebar spins until the
 * user kills the app.
 *
 * ⚠ AND THE EXIT STATUS IS CHECKED, which is not ceremony: the first version
 * of this passed `-pterk` — one letter that is not an option — and avahi-browse
 * answered "invalid option -- 'e'" on a stderr that was being discarded, exited
 * 1, and printed nothing. The scan reported "nothing announced itself on this
 * network", which is a perfectly plausible answer and was completely wrong. A
 * discovery tool whose failure mode is an empty list has to say when the probe
 * itself failed.
 *
 *   =;wlan0;IPv4;NAS;_smb._tcp;local;nas.local;192.168.1.5;445;"txt=…"
 *   0  1     2    3   4        5     6          7           8   9
 *
 * The HOSTNAME (field 6) is used and not the address: a name survives DHCP
 * handing the box a different lease tomorrow, which matters because a mounted
 * share ends up remembered by URI. The address is the fallback when a record
 * arrives without one.
 */
static void scan_mdns(hostlist_t *l, const char *type, const char *service)
{
	char *const argv[] = {
		(char *)"avahi-browse", (char *)"-ptrk", (char *)type, NULL
	};
	int st = 0;
	char *out = run_capture(argv, &st, true);

	if (st != 0) {
		/* Not fatal — the other source may still find something — but never
		 * silent. warn() goes to stderr, so a --rec caller's records stay
		 * clean while a person running this in a terminal is told. */
		warn(_("netscan: avahi-browse failed for %s (exit %d) — mDNS discovery "
		     "is not working"), type, st);
		free(out);
		return;
	}

	size_t nlines = 0;
	char **lines = split(out, '\n', &nlines);

	for (size_t i = 0; i < nlines; i++) {
		if (lines[i][0] != '=')
			continue;   /* '+' is "seen", '=' is "resolved with an address" */

		size_t nf = 0;
		char **f = split(lines[i], ';', &nf);
		if (nf >= 8) {
			/* avahi escapes spaces in the service name as "\032". Left as-is
			 * would put a literal backslash-zero-three-two in the sidebar. */
			char *name = f[3];
			for (char *p = name; (p = strstr(p, "\\032")); ) {
				*p = ' ';
				memmove(p + 1, p + 4, strlen(p + 4) + 1);
			}

			const char *host = (f[6] && *f[6]) ? f[6] : f[7];
			/* Trailing dot on an mDNS hostname — "nas.local." — is correct DNS
			 * and wrong in a URI. */
			char *h = xstrdup(host ? host : "");
			size_t hl = strlen(h);
			if (hl && h[hl - 1] == '.')
				h[hl - 1] = '\0';

			hosts_add(l, h, name, service);
			free(h);
		}
		free(f);
	}

	free(lines);
	free(out);
}

/* ── NetBIOS ───────────────────────────────────────────────────────────────
 *
 * `nmblookup '*'` broadcasts and every SMB machine on the segment answers with
 * its address; `nmblookup -A <addr>` then asks that machine its NAME, because
 * an address is not something to show a person and not something to remember a
 * mount by.
 *
 * The name table reply marks a machine's own name with <00>, and marks GROUP
 * names — the workgroup — with a literal "<GROUP>" on the same line.
 *
 *      Looking up status of 192.168.40.99
 *              KENZIE-PC       <00> -         B <ACTIVE>
 *              WORKGROUP       <00> - <GROUP> B <ACTIVE>
 *              __MSBROWSE__    <01> - <GROUP> B <ACTIVE>
 *
 * ⚠ THERE IS NO "UNIQUE" IN THAT OUTPUT. The first version of this asked for
 * one — the word appears in descriptions of NetBIOS name types, not in what
 * nmblookup prints (`strings /usr/bin/nmblookup` has <GROUP> and <ACTIVE> and
 * no UNIQUE) — so every name table was rejected and the whole Windows half of
 * discovery found nothing, on a network where it should have found something.
 * The absence of <GROUP> is what "this is a machine" looks like here.
 * __MSBROWSE__ is the browser election and is not a host you can open.
 */
static void scan_netbios(hostlist_t *l)
{
	const char *conf = smb_conf_flag();
	char *bargv[6] = { (char *)"nmblookup", (char *)"*", NULL, NULL, NULL, NULL };
	if (conf) { bargv[2] = (char *)"-s"; bargv[3] = (char *)conf; }
	int st = 0;
	char *out = run_capture(bargv, &st, true);

	size_t nlines = 0;
	char **lines = split(out, '\n', &nlines);
	int probed = 0;

	for (size_t i = 0; i < nlines && probed < NETSCAN_HOST_MAX; i++) {
		/* "192.168.1.5 *<00>" — the address is the first field. */
		char *sp = strchr(lines[i], ' ');
		if (!sp)
			continue;
		*sp = '\0';
		const char *addr = lines[i];
		if (!*addr || !strchr(addr, '.'))
			continue;

		char *aargv[7] = {
			(char *)"nmblookup", (char *)"-A", (char *)addr, NULL, NULL, NULL, NULL
		};
		if (conf) { aargv[3] = (char *)"-s"; aargv[4] = (char *)conf; }
		int ast = 0;
		char *names = run_capture(aargv, &ast, true);
		probed++;

		size_t nn = 0;
		char **nlines2 = split(names, '\n', &nn);
		for (size_t j = 0; j < nn; j++) {
			if (!strstr(nlines2[j], "<00>"))
				continue;
			if (strstr(nlines2[j], "<GROUP>"))
				continue;   /* the workgroup, not a machine */
			if (strstr(nlines2[j], "__MSBROWSE__"))
				continue;

			/* "\tNAS             <00> -         B <ACTIVE>" */
			char *p = nlines2[j];
			while (*p == ' ' || *p == '\t')
				p++;
			char *e = p;
			while (*e && *e != ' ' && *e != '\t')
				e++;
			char *name = xstrndup(p, (size_t)(e - p));
			if (*name)
				hosts_add(l, name, name, "smb");
			free(name);
			break;
		}
		free(nlines2);
		free(names);
	}

	free(lines);
	free(out);
}

/* ── What a host offers ────────────────────────────────────────────────────
 *
 * Guest enumeration (-N). A server that wants credentials refuses this, which
 * is not an error here: the host row remains and gvfs will ask when it is
 * opened.
 *
 * smbclient prints a table:
 *
 *      Sharename       Type      Comment
 *      ---------       ----      -------
 *      media           Disk      Films
 *      IPC$            IPC       IPC Service
 */
static bool share_is_admin(const char *name)
{
	size_t n = strlen(name);
	if (n && name[n - 1] == '$')
		return true;   /* IPC$, ADMIN$, C$, print$ — every administrative share */
	return !strcasecmp(name, "IPC") || !strcasecmp(name, "print");
}

static int list_shares(const char *host, const char *label)
{
	char *target = xasprintf("//%s", host);
	const char *conf = smb_conf_flag();
	char *argv[11] = {
		(char *)"smbclient", (char *)"-N", (char *)"-g", (char *)"-L", target,
		(char *)"-t", (char *)NETSCAN_TIMEOUT, NULL, NULL, NULL, NULL
	};
	if (conf) { argv[7] = (char *)"-s"; argv[8] = (char *)conf; }
	int st = 0;
	char *out = run_capture(argv, &st, true);

	size_t nlines = 0;
	char **lines = split(out, '\n', &nlines);
	int n = 0;

	for (size_t i = 0; i < nlines; i++) {
		/* -g is the machine-readable form: "Disk|media|Films". Parsing the
		 * human table would mean depending on its column widths, which differ
		 * between samba versions. */
		if (strncmp(lines[i], "Disk|", 5) != 0)
			continue;

		char *name = lines[i] + 5;
		char *bar = strchr(name, '|');
		if (bar)
			*bar = '\0';
		if (!*name || share_is_admin(name))
			continue;

		char *uri = xasprintf("smb://%s/%s", host, name);
		char *enc = pct_encode(uri, true);
		char *title = xasprintf("%s on %s", name, label);
		char *local = netscan_mounted_path(uri);

		if (g_out == OUT_REC)
			rec_row(8, enc, "share", title, "folder-network", host, "smb",
			        local ? "1" : "0", local ? local : "");
		else
			printf("%s%-28s%s %s%s%s\n", C_ACCENT(), title, C_RESET(),
			       C_DIM(), uri, C_RESET());

		free(local);
		free(title);
		free(enc);
		free(uri);
		n++;
	}

	free(lines);
	free(out);
	free(target);
	return n;
}

/* Is this URI already mounted? gvfs names its FUSE directories from the URI's
 * parts — "smb-share:server=nas,share=media" — so the question is answerable
 * without asking gvfs anything, which matters because this runs once per row.
 *
 * Returned as the LOCAL PATH, not a boolean: a row that is already mounted
 * should navigate rather than mount again, and the path is what navigating
 * needs. Caller frees. */
char *netscan_mounted_path(const char *uri)
{
	if (strncmp(uri, "smb://", 6) != 0)
		return NULL;

	char *rest = xstrdup(uri + 6);
	char *slash = strchr(rest, '/');
	if (!slash) {
		free(rest);
		return NULL;
	}
	*slash = '\0';
	const char *server = rest, *share = slash + 1;

	char *want = xasprintf("smb-share:server=%s,share=%s", server, share);
	char *root = xasprintf("/run/user/%lu/gvfs", (unsigned long)getuid());
	char *found = NULL;

	DIR *d = opendir(root);
	if (d) {
		struct dirent *e;
		while ((e = readdir(d))) {
			/* strcasecmp: gvfs lower-cases the server name it was given, and
			 * NetBIOS hands us "NAS" in capitals. A case-sensitive compare
			 * here means an already-mounted share offers to mount itself. */
			if (!strcasecmp(e->d_name, want)) {
				found = xasprintf("%s/%s", root, e->d_name);
				break;
			}
		}
		closedir(d);
	}

	free(root);
	free(want);
	free(rest);
	return found;
}

int cmd_netscan(int argc, char **argv)
{
	bool hosts_only = false;
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--hosts"))
			hosts_only = true;
		else
			die(_("netscan: unknown option '%s'"), argv[i]);
	}

	/* Neither tool is a hard dependency: a machine with no avahi still finds
	 * Windows shares over NetBIOS, and a machine with no samba client still
	 * finds a NAS over mDNS. Saying which one is missing is the difference
	 * between "there is nothing on your network" and "I cannot see half of
	 * it", and only one of those is actionable. */
	bool have_avahi = have_cmd("avahi-browse");
	bool have_smb   = have_cmd("smbclient");

	if (!have_avahi && !have_smb)
		die(_("netscan: install avahi (mDNS) or smbclient (Windows shares) to "
		    "discover network places"));

	hostlist_t hosts = { 0 };

	if (have_avahi) {
		scan_mdns(&hosts, "_smb._tcp", "smb");
		scan_mdns(&hosts, "_sftp-ssh._tcp", "sftp");
		scan_mdns(&hosts, "_nfs._tcp", "nfs");
		scan_mdns(&hosts, "_afpovertcp._tcp", "afp");
	}
	if (have_smb && have_cmd("nmblookup"))
		scan_netbios(&hosts);

	if (g_out == OUT_REC)
		rec_row(8, "uri", "kind", "title", "icon", "host", "service",
		        "mounted", "path");

	int n = 0;
	for (size_t i = 0; i < hosts.n && i < NETSCAN_HOST_MAX; i++) {
		const nethost_t *h = &hosts.v[i];

		/* The host itself, always — a server whose shares cannot be listed
		 * without credentials is still somewhere to go, and gvfs asks. */
		char *uri = xasprintf("%s://%s", h->service, h->host);
		char *enc = pct_encode(uri, true);

		if (g_out == OUT_REC)
			rec_row(8, enc, "host", h->label, "network-server", h->host,
			        h->service, "0", "");
		else
			printf("%s%-28s%s %s%s%s\n", C_ACCENT(), h->label, C_RESET(),
			       C_DIM(), uri, C_RESET());
		n++;

		free(enc);
		free(uri);

		if (!hosts_only && have_smb && !strcmp(h->service, "smb"))
			n += list_shares(h->host, h->label);
	}

	hosts_free(&hosts);

	if (g_out == OUT_HUMAN && n == 0)
		printf("%s%s%s\n", C_DIM(),
		       _("nothing announced itself on this network"), C_RESET());

	/* 100, not 1: the same "nothing to show" status the other listers use, so
	 * a caller can tell an empty network from a failed scan. */
	return n ? 0 : 100;
}

/* ── Mounting ──────────────────────────────────────────────────────────────
 *
 * `gio mount`, and nothing else. It is gvfs's credential prompt, gvfs's
 * keyring entry and gvfs's FUSE mount; reimplementing any of that would mean
 * this program asking for a password, which is the last thing a file manager
 * should teach anybody to type into.
 *
 * ⚠ Prints the LOCAL PATH on success. Without it the caller has mounted
 * something and has no idea where it went — gvfs's directory name is derived
 * from the URI, not returned by the command.
 */
int cmd_netmount(int argc, char **argv)
{
	if (argc < 1)
		die(_("netmount: need a URI (see: synfiles netscan)"));

	const char *uri = argv[0];
	/* The URI reaches gio as an argv element and never a shell, but it also
	 * becomes a path we hand back to a GUI, so refuse the shapes that are not
	 * URIs at all rather than letting one through to be interpreted later. */
	if (!strstr(uri, "://"))
		die(_("netmount: '%s' is not a URI"), uri);

	if (!have_cmd("gio"))
		die(_("netmount: gio is not installed — install gvfs to mount network shares"));

	char *already = netscan_mounted_path(uri);
	if (already) {
		if (g_out == OUT_REC) {
			char *enc = pct_encode(already, true);
			rec_row(3, "status", "uri", "path");
			rec_row(3, "mounted", uri, enc);
			free(enc);
		} else {
			printf("%s\n", already);
		}
		free(already);
		return 0;
	}

	char *const gargv[] = {
		(char *)"gio", (char *)"mount", (char *)uri, NULL
	};
	int st = 0;
	char *out = run_capture(gargv, &st, false);
	free(out);

	if (st != 0)
		die(_("netmount: could not mount %s"), uri);

	char *path = netscan_mounted_path(uri);
	if (g_out == OUT_REC) {
		char *enc = path ? pct_encode(path, true) : xstrdup("");
		rec_row(3, "status", "uri", "path");
		rec_row(3, path ? "mounted" : "unknown", uri, enc);
		free(enc);
	} else if (path) {
		printf("%s\n", path);
	}

	free(path);
	return 0;
}
