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

	/* Only the kernels this app offers. A settings pane must not become a
	 * general "install anything by name" endpoint: that is synpkg's job, it has
	 * a search and a description for every result, and a package name arriving
	 * from a GUI row is not a considered choice about software.
	 *
	 * The list lives in kernel.c, which is where the pane's own list is — two
	 * copies is two places to add a kernel and one place to forget. */
	if (!syn_kernel_known(name)) {
		fprintf(stderr, "syn-settings: '%s' is not one of the kernels this "
		                "pane manages — use synpkg for anything else\n", name);
		return 2;
	}

	if (!have_cmd("synpkg")) {
		fprintf(stderr, "syn-settings: synpkg is not installed; it is what "
		                "performs the install\n");
		return 1;
	}

	/* A kernel from a repository this machine may not have yet.
	 *
	 * Done HERE, before the install, because pacman cannot be told to find a
	 * package in a repository that is not configured — the install would fail
	 * with "not found in any repository" AFTER the user had authenticated,
	 * which is indistinguishable from the pane offering software that does not
	 * exist.
	 *
	 * Only on install. Removing a kernel must never add a repository, and a
	 * `remove` that quietly configured one would be a genuinely surprising
	 * thing for a button labelled Remove to do.
	 *
	 * synpkg decides whether the work is needed — enable-repo is idempotent
	 * and returns early when [cachyos] is already there — so this does not
	 * keep its own answer to "is it enabled", which could disagree. */
	if (!strcmp(act, "install") && syn_kernel_repo(name)) {
		char *r[] = { (char *)"synpkg", (char *)"cachyos",
		              (char *)"enable-repo", NULL };
		if (g_dry_run) {
			fputs("would run: synpkg cachyos enable-repo\n", stdout);
		} else if (run_quiet(r) != 0) {
			fprintf(stderr, "syn-settings: could not enable the CachyOS "
			                "repository — %s cannot be installed without it\n",
			        name);
			return 1;
		}
	}

	/* Headers travel with the kernel, always. A kernel installed without them
	 * boots to a machine where DKMS cannot build synapse_kmod or the NVIDIA
	 * module — which is to say, no GPU driver — and the failure appears one
	 * reboot later with nothing connecting it to this click. */
	char headers[128];
	snprintf(headers, sizeof headers, "%s-headers", name);

	/* ⚠ --noconfirm is REQUIRED here, and it is not a shortcut.
	 *
	 * synpkg's confirm() returns FALSE when stdin is not a terminal — the safe
	 * default, since a GUI front-end has no terminal to answer "Proceed?" in.
	 * This process has no terminal, so without this flag the sequence was:
	 * polkit authenticated, the escalated child asked a question into a stdin
	 * that could never answer, took the refusal, and exited 0. A declined
	 * transaction is not an error, so nothing was printed and nothing was
	 * installed — the button authenticated and then did nothing at all.
	 *
	 * What is dropped is only the unanswerable prompt. The authorisation
	 * itself is untouched: polkit still challenges, and the target is still
	 * restricted to syn_kernel_known() above.
	 *
	 * It must come BEFORE the verb — synpkg stops parsing globals at the first
	 * non-option argument. */
	char *a[] = { (char *)"synpkg", (char *)"--noconfirm", (char *)act,
	              (char *)name, (char *)headers, NULL };

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
