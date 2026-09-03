/* main.c — syn-vault's command line, and the window it can open.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "config.h"
#include "synvault.h"
#include "i18n.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef SYNVAULT_DATADIR
#define SYNVAULT_DATADIR "/usr/share/syn-vault"
#endif

static void usage(FILE *f)
{
	fprintf(f,
"syn-vault — a password-locked folder for your own files\n"
"\n"
"  syn-vault list                 the vaults, and which are open\n"
"  syn-vault create <name>        make one, and set its password\n"
"  syn-vault open <name>          unlock it at ~/Vaults/<name>\n"
"  syn-vault close <name>         lock it again\n"
"  syn-vault status <name>        just one\n"
"  syn-vault gui [name]           the window\n"
"\n"
"  --rec        one record per line, for a front end\n"
"  --version    print the version\n"
"\n"
"The files live encrypted in ~/.local/share/syn-vault/<name>.vault and are\n"
"readable only while the vault is open. gocryptfs does the encrypting; this\n"
"program does not invent any of its own.\n"
"\n"
"⚠ Nothing anywhere keeps a second copy of the password. A vault whose\n"
"password is lost is lost with it.\n");
}

/* ⛔ THE PASSWORD IS NEVER AN ARGUMENT, so `gui` cannot take one either. The
 * window asks, and pipes it to `syn-vault open` on stdin. */
int cmd_gui(const char *name)
{
	if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY"))
		die("%s", _("no display — syn-vault gui needs a graphical session"));

	if (access("/usr/bin/quickshell", X_OK) != 0 &&
	    access("/usr/local/bin/quickshell", X_OK) != 0)
		die("%s", _("quickshell is not installed — synpkg install quickshell"));

	/* The window's own Wayland identity, so the dock can find its .desktop and
	 * it does not inherit the app_id of whatever opened it. */
	setenv("QS_APP_ID", "syn-vault", 1);

	if (name && *name) setenv("SYNVAULT_OPEN", name, 1);
	else               unsetenv("SYNVAULT_OPEN");

	const char *qml = SYNVAULT_DATADIR "/syn-vault.qml";
	if (access(qml, R_OK) != 0 && access("data/syn-vault.qml", R_OK) == 0)
		qml = "data/syn-vault.qml";

	char *child[] = { (char *)"quickshell", (char *)"-p", (char *)qml, NULL };
	execvp(child[0], child);
	die("%s", _("could not start quickshell"));
	return 1;
}

int main(int argc, char **argv)
{
	syn_vault_i18n_init();

	const char *pos[4];
	int n = 0;

	for (int i = 1; i < argc; i++) {
		const char *v = argv[i];
		if (!strcmp(v, "--rec"))     { g_out = OUT_REC; continue; }
		if (!strcmp(v, "--help") || !strcmp(v, "-h")) { usage(stdout); return 0; }
		if (!strcmp(v, "--version")) { printf("syn-vault %s\n", SYNVAULT_VERSION); return 0; }
		if (v[0] == '-' && v[1] == '-') {
			warn(_("unknown option '%s'"), v);
			usage(stderr);
			return 2;
		}
		if (n < 4) pos[n++] = v;
	}

	if (n == 0) { usage(stdout); return 0; }

	const char *c = pos[0];

	if (!strcmp(c, "list"))                       return cmd_list();
	if (!strcmp(c, "gui"))                        return cmd_gui(n >= 2 ? pos[1] : NULL);
	if (!strcmp(c, "create") && n >= 2)           return cmd_create(pos[1]);
	if (!strcmp(c, "open")   && n >= 2)           return cmd_open(pos[1]);
	if (!strcmp(c, "close")  && n >= 2)           return cmd_close(pos[1]);
	if (!strcmp(c, "lock")   && n >= 2)           return cmd_close(pos[1]);
	if (!strcmp(c, "unlock") && n >= 2)           return cmd_open(pos[1]);
	if (!strcmp(c, "status") && n >= 2)           return cmd_status(pos[1]);

	/* A verb that needs a name and did not get one is not an unknown command,
	 * and saying so is the difference between a typo and a missing argument. */
	if (!strcmp(c, "create") || !strcmp(c, "open") || !strcmp(c, "close") ||
	    !strcmp(c, "lock") || !strcmp(c, "unlock") || !strcmp(c, "status")) {
		warn(_("%s needs the name of a vault"), c);
		return 2;
	}

	warn(_("unknown command '%s'"), c);
	usage(stderr);
	return 2;
}
