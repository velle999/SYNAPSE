/* syn-settings — install and remove, by way of synpkg.
 *
 * synpkg already owns pacman on this OS, including its polkit prompt and its
 * "this is what will change, continue?" step. Calling it is the only way this
 * pane can install a kernel without growing a second implementation of
 * "install a package" that could disagree with the package manager about what
 * is installed, what it depends on, or whether the operation was authorised.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <string.h>

/* Only the kernels this app offers. A settings pane must not become a general
 * "install anything by name" endpoint: that is synpkg's job, it has a search
 * and a description for every result, and a package name arriving from a GUI
 * row is not a considered choice about software. */
static const char *allowed[] = {
	"linux", "linux-lts", "linux-zen", "linux-hardened",
	"linux-rt", "linux-rt-lts",
};

int do_pkg(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "syn-settings: pkg needs install|remove and a name\n");
		return 2;
	}
	const char *act = argv[0], *name = argv[1];

	if (strcmp(act, "install") && strcmp(act, "remove")) {
		fprintf(stderr, "syn-settings: pkg action must be install or remove\n");
		return 2;
	}

	int ok = 0;
	for (size_t i = 0; i < sizeof allowed / sizeof allowed[0]; i++)
		if (!strcmp(name, allowed[i])) { ok = 1; break; }
	if (!ok) {
		fprintf(stderr, "syn-settings: '%s' is not one of the kernels this "
		                "pane manages — use synpkg for anything else\n", name);
		return 2;
	}

	if (!have_cmd("synpkg")) {
		fprintf(stderr, "syn-settings: synpkg is not installed; it is what "
		                "performs the install\n");
		return 1;
	}

	/* Headers travel with the kernel, always. A kernel installed without them
	 * boots to a machine where DKMS cannot build synapse_kmod or the NVIDIA
	 * module — which is to say, no GPU driver — and the failure appears one
	 * reboot later with nothing connecting it to this click. */
	char headers[128];
	snprintf(headers, sizeof headers, "%s-headers", name);

	char *a[] = { (char *)"synpkg", (char *)act, (char *)name,
	              (char *)headers, NULL };

	if (g_dry_run) {
		fputs("would run:", stdout);
		for (int i = 0; a[i]; i++) printf(" %s", a[i]);
		putchar('\n');
		return 0;
	}

	int rc = run_quiet(a);
	if (rc != 0)
		fprintf(stderr, "syn-settings: synpkg exited %d — "
		                "authorisation refused, or the package was not found\n", rc);
	return rc;
}
