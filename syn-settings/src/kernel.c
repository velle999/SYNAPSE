/* syn-settings — the Kernel pane.
 *
 * Swapping kernels is the change most likely to leave a machine that does not
 * boot, and the thing that makes it survivable is having a SECOND kernel
 * installed before you need it. So the pane is built around that: every kernel
 * Arch ships is listed whether or not it is installed, with the one you are
 * running marked, and installing another is one action rather than a wiki page.
 *
 * WHAT THIS DOES NOT DO
 *
 * It does not choose the boot default. That belongs to the bootloader — GRUB
 * or limine here — and writing a boot entry from a settings pane is how a
 * machine stops booting. The pane says which kernels EXIST and which one is
 * running; the picker at boot is where you choose between them.
 *
 * Installing and removing is handed to synpkg, which already owns pacman and
 * its polkit prompt on this OS. Shelling to pacman directly would mean a
 * second implementation of "install a package" that could disagree with the
 * one the package manager uses.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

struct kern {
	const char *pkg;
	const char *what;
};

/* The stock set. Named rather than globbed over the repositories: a glob
 * would also catch linux-firmware, linux-api-headers and every -headers
 * package, and "install linux-api-headers to change your kernel" is exactly
 * the kind of helpfulness that breaks a system. */
static const struct kern kernels[] = {
	{ "linux",           "the stock Arch kernel; what SynapseOS ships" },
	{ "linux-lts",       "long-term support — the fallback worth having installed" },
	{ "linux-zen",       "desktop-tuned; usually the best for latency and gaming" },
	{ "linux-hardened",  "security-hardened; slower, and it breaks some drivers" },
	{ "linux-rt",        "real-time preemption; for audio work" },
	{ "linux-rt-lts",    "real-time on the LTS base" },
};

/* Installed version of a package, or NULL. `pacman -Q <pkg>` prints
 * "name version" and exits non-zero when it is not installed. */
static int installed_version(const char *pkg, char *out, size_t cap)
{
	char buf[256] = "";
	char *argv[] = { (char *)"pacman", (char *)"-Q", (char *)pkg, NULL };
	/* Quiet: "not installed" is the answer this asks for half the time. */
	if (run_capture_quiet(argv, buf, sizeof buf) != 0 || !buf[0]) return 0;

	char *sp = strchr(buf, ' ');
	if (!sp) return 0;
	sp++;
	sp[strcspn(sp, "\n")] = '\0';
	tsv_clean(sp);
	snprintf(out, cap, "%s", sp);
	return 1;
}

int pane_kernel(void)
{
	rec_header("kernel\tinstalled\tstate\tdetail\taction");

	if (!have_cmd("pacman")) {
		rec_row("-\tunknown\t-\tpacman not available\t-");
		return 1;
	}

	struct utsname u;
	const char *running = uname(&u) == 0 ? u.release : "";

	for (size_t i = 0; i < sizeof kernels / sizeof kernels[0]; i++) {
		char ver[128] = "";
		int have = installed_version(kernels[i].pkg, ver, sizeof ver);

		/* Is THIS the kernel currently booted? pacman's version is
		 * "7.1.6.arch1-1" and uname reports "7.1.6-arch1-1" — same build,
		 * different punctuation — so a string compare says no every time.
		 * Comparing the leading numeric release is what actually answers it.
		 *
		 * A kernel package can also be installed and NOT running because the
		 * machine has not rebooted since, which is a state worth naming: it is
		 * the difference between "this is what you are on" and "this is what
		 * you would be on after a reboot". */
		int is_running = 0;
		if (have && running[0]) {
			/* utsname.release is 65 bytes, so b must be at least that. */
			char a[128], b[128];
			snprintf(a, sizeof a, "%s", ver);
			snprintf(b, sizeof b, "%s", running);
			for (char *p = a; *p; p++) if (*p == '.' || *p == '-') *p = ' ';
			for (char *p = b; *p; p++) if (*p == '.' || *p == '-') *p = ' ';
			int a1, a2, a3, b1, b2, b3;
			if (sscanf(a, "%d %d %d", &a1, &a2, &a3) == 3 &&
			    sscanf(b, "%d %d %d", &b1, &b2, &b3) == 3)
				is_running = (a1 == b1 && a2 == b2 && a3 == b3);
		}

		char hdr[64];
		snprintf(hdr, sizeof hdr, "%s-headers", kernels[i].pkg);
		char hver[128] = "";
		int have_hdr = installed_version(hdr, hver, sizeof hver);

		const char *state = !have      ? "not installed"
		                  : is_running ? "running"
		                               : "installed";

		char detail[640];
		/* Headers matter here and nowhere else on this pane: without them
		 * DKMS cannot build synapse_kmod or the NVIDIA module against this
		 * kernel, so it boots to a system with no GPU driver. Reported per
		 * kernel because it is per kernel. */
		snprintf(detail, sizeof detail, "%s%s", kernels[i].what,
		         have && !have_hdr
		           ? "  ⚠ headers MISSING — DKMS cannot build for it"
		           : "");

		char action[128];
		snprintf(action, sizeof action, "pkg:%s", kernels[i].pkg);

		rec_row("%s\t%s\t%s\t%s\t%s",
		        kernels[i].pkg, have ? ver : "-", state, detail,
		        is_running ? "-" : action);
	}

	/* Removing the kernel you booted from is the one action here that can
	 * leave a machine unbootable, so the running one carries no action at all
	 * (above) and the reason is said out loud rather than left to be inferred
	 * from a greyed-out button. */
	rec_row("-\t-\t-\tThe running kernel has no actions: removing what you "
	        "booted from is how a machine stops booting. Reboot into another "
	        "one first.\t-");

	if (have_cmd("bootctl") || have_cmd("grub-mkconfig") || have_cmd("limine")) {
		rec_row("-\t-\t-\tWhich kernel boots by DEFAULT is the bootloader's "
		        "choice, not this pane's — pick it at the boot menu.\t-");
	}

	return 0;
}
