/* syn-settings — the Kernel pane.
 *
 * Swapping kernels is the change most likely to leave a machine that does not
 * boot, and the thing that makes it survivable is having a SECOND kernel
 * installed before you need it. So the pane is built around that: every kernel
 * on offer is listed whether or not it is installed, with the one you are
 * running marked, and installing another is one action rather than a wiki page.
 *
 * "On offer" is the six Arch kernels plus the three CachyOS ones. The Cachy
 * rows need a repository this machine may not have; the row says so, and
 * installing one adds it. See do_pkg().
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
	/* The repository this kernel comes from, or NULL for "Arch ships it".
	 * Only the CachyOS ones are not in core/extra, and a row that cannot
	 * install until a repository is added has to say so BEFORE it is clicked
	 * — otherwise the failure is "not found in any repository", which reads
	 * as the pane offering something that does not exist. */
	const char *repo;
};

/* The stock set. Named rather than globbed over the repositories: a glob
 * would also catch linux-firmware, linux-api-headers and every -headers
 * package, and "install linux-api-headers to change your kernel" is exactly
 * the kind of helpfulness that breaks a system. */
static const struct kern kernels[] = {
	{ "linux",           "the stock Arch kernel; what SynapseOS ships",            NULL },
	{ "linux-lts",       "long-term support — the fallback worth having installed", NULL },
	{ "linux-zen",       "desktop-tuned; usually the best for latency and gaming",  NULL },
	{ "linux-hardened",  "security-hardened; slower, and it breaks some drivers",   NULL },
	{ "linux-rt",        "real-time preemption; for audio work",                    NULL },
	{ "linux-rt-lts",    "real-time on the LTS base",                               NULL },

	/* CachyOS. Not in any Arch repository, so installing one adds [cachyos]
	 * first — see do_pkg(). Only the PLAIN repo is ever added: upstream's own
	 * bootstrap would also swap in their pacman fork and put -march-optimised
	 * rebuilds of core and extra ahead of [core], which is a different
	 * decision entirely from trying a kernel. */
	{ "linux-cachyos",          "CachyOS BORE + sched-ext; tuned hard for desktop "
	                            "responsiveness", "cachyos" },
	{ "linux-cachyos-lts",      "the CachyOS patches on the LTS base — the Cachy "
	                            "kernel worth keeping as the fallback", "cachyos" },
	{ "linux-cachyos-hardened", "CachyOS with the hardened patch set; same driver "
	                            "caveats as linux-hardened", "cachyos" },
};

/* THE list. pkg.c used to keep a second copy as its install allowlist, which
 * is two places to add a kernel and one place to forget — a pane offering a
 * kernel the installer refuses, or worse the reverse. */
int syn_kernel_known(const char *pkg)
{
	for (size_t i = 0; i < sizeof kernels / sizeof kernels[0]; i++)
		if (!strcmp(pkg, kernels[i].pkg)) return 1;
	return 0;
}

/* The repository a kernel needs, or NULL when Arch ships it. Same reason as
 * syn_kernel_known(): do_pkg() must not carry its own copy of which kernels
 * are out-of-repo. */
const char *syn_kernel_repo(const char *pkg)
{
	for (size_t i = 0; i < sizeof kernels / sizeof kernels[0]; i++)
		if (!strcmp(pkg, kernels[i].pkg)) return kernels[i].repo;
	return NULL;
}

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

/* Is the CachyOS repository configured?
 *
 * ASKED OF synpkg, not parsed here. synpkg owns pacman on this OS and already
 * has to know the answer in order to add the repo; a second parse of
 * pacman.conf in this binary would be a second definition of "enabled" that
 * can drift from the one doing the work — the same two-copies mistake the
 * kernel list above exists to avoid.
 *
 * ⚠ The test is a PREFIX, not strstr(): the two answers are "enabled" and
 * "disabled", and "disabled" contains "enabled".
 */
static int cachyos_enabled(void)
{
	if (!have_cmd("synpkg")) return 0;

	char buf[128] = "";
	char *argv[] = { (char *)"synpkg", (char *)"--tsv",
	                 (char *)"cachyos", (char *)"status", NULL };
	if (run_capture_quiet(argv, buf, sizeof buf) != 0) return 0;
	return strncmp(buf, "enabled", 7) == 0;
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

	/* Which bootloader(s) this machine actually has configured. Detected once:
	 * it cannot change between rows, and reading three files per kernel to
	 * learn the same thing six times is wasteful in a pane that redraws. */
	struct syn_boot loaders[6];
	int nloaders = syn_boot_detect(loaders, sizeof loaders / sizeof loaders[0]);

	/* Did any installed package turn out to own the release we booted? */
	int running_is_owned = 0;

	/* Once, not per row: it cannot change between rows, and it costs a
	 * subprocess. Lazily — a machine with no out-of-repo kernel listed never
	 * pays for the question at all. */
	int cachy_checked = 0, cachy_on = 0;

	for (size_t i = 0; i < sizeof kernels / sizeof kernels[0]; i++) {
		char ver[128] = "";
		int have = installed_version(kernels[i].pkg, ver, sizeof ver);

		/* The kernel release this package owns, from its module tree's
		 * pkgbase — "7.1.7-arch1-1". This is the form uname reports and the
		 * form systemd-boot's generated entries are named after, so it answers
		 * both "is this running?" and "is there an entry?" exactly.
		 *
		 * It replaces comparing pacman's version numerically. pacman says
		 * "7.1.7.arch1-1" where uname says "7.1.7-arch1-1", so a plain compare
		 * never matched and the old code parsed three integers out of each —
		 * which made 7.1.6.arch1-1 and 7.1.6.arch2-1 compare EQUAL, reporting a
		 * different build as the one you booted.
		 *
		 * A kernel can be installed and NOT running because the machine has
		 * not rebooted since — this box is in exactly that state — and that is
		 * the difference between what you are on and what you would be on
		 * after a reboot. */
		char release[128] = "";
		int have_rel = have && syn_kernel_release(kernels[i].pkg, release,
		                                         sizeof release);
		int is_running = have_rel && running[0] && !strcmp(release, running);
		if (is_running) running_is_owned = 1;

		char hdr[64];
		snprintf(hdr, sizeof hdr, "%s-headers", kernels[i].pkg);
		char hver[128] = "";
		int have_hdr = installed_version(hdr, hver, sizeof hver);

		/* Bootable under ANY configured loader. On a machine with one — the
		 * normal case — this is just that one's answer. */
		int bootable = 0;
		for (int b = 0; have && !bootable && b < nloaders; b++)
			bootable = syn_boot_has_entry(&loaders[b], kernels[i].pkg, release);

		/* "installed" alone was the lie. A kernel on disk that the
		 * bootloader has never heard of is not a kernel you can switch to. */
		const char *state = !have      ? "not installed"
		                  : is_running ? "running"
		                  : bootable   ? "installed, bootable"
		                               : "installed, NO BOOT ENTRY";

		/* A kernel from a repository this machine does not have. Said on the
		 * row rather than discovered at the click: "not found in any
		 * repository" after a password prompt reads as the pane offering
		 * software that does not exist. */
		int needs_repo = 0;
		if (kernels[i].repo && !have) {
			if (!cachy_checked) { cachy_on = cachyos_enabled(); cachy_checked = 1; }
			needs_repo = !cachy_on;
		}

		char detail[768];
		/* Headers matter here and nowhere else on this pane: without them
		 * DKMS cannot build synapse_kmod or the NVIDIA module against this
		 * kernel, so it boots to a system with no GPU driver. Reported per
		 * kernel because it is per kernel. */
		snprintf(detail, sizeof detail, "%s%s%s%s", kernels[i].what,
		         needs_repo
		           ? "  ⓘ not in any Arch repository — installing adds the "
		             "CachyOS repo first (core and extra keep priority)"
		           : "",
		         have && !have_hdr
		           ? "  ⚠ headers MISSING — DKMS cannot build for it"
		           : "",
		         have && !is_running && !bootable
		           ? "  ⚠ no bootloader entry yet — use “Make bootable” before "
		             "you reboot expecting a choice"
		           : "");

		/* An installed kernel with no entry needs making bootable; anything
		 * else offers install/remove. The running one offers nothing at all —
		 * see below. */
		char action[128];
		if (have && !is_running && !bootable && nloaders > 0)
			snprintf(action, sizeof action, "boot:%s", kernels[i].pkg);
		else
			snprintf(action, sizeof action, "pkg:%s", kernels[i].pkg);

		rec_row("%s\t%s\t%s\t%s\t%s",
		        kernels[i].pkg, have ? ver : "-", state, detail,
		        is_running ? "-" : action);
	}

	/* You are running a kernel no installed package owns.
	 *
	 * The ordinary cause is benign and extremely common: a kernel upgrade
	 * landed and the machine has not rebooted, so /usr/lib/modules holds the
	 * NEW release while uname still reports the old one. It is worth saying
	 * out loud anyway, because it is also the state in which `modprobe` cannot
	 * find modules for the running kernel — the module tree it wants was
	 * replaced — and that surfaces later as hardware that stops working with
	 * nothing pointing back at the upgrade.
	 *
	 * It is equally the signature of a hand-built or removed kernel, which is
	 * the case where "none of the rows above is what you booted" genuinely
	 * changes what the rest of this pane means. */
	if (running[0] && !running_is_owned)
		rec_row("-\t%s\t⚠ running, unowned\tYou booted %s, and no installed "
		        "kernel package owns that release — so none of the rows above "
		        "is the kernel you are on. Usually this just means a kernel "
		        "upgrade has not been rebooted into yet; until you do, modules "
		        "for the running kernel are gone from /usr/lib/modules.\t-",
		        running, running);

	/* Removing the kernel you booted from is the one action here that can
	 * leave a machine unbootable, so the running one carries no action at all
	 * (above) and the reason is said out loud rather than left to be inferred
	 * from a greyed-out button. */
	rec_row("-\t-\t-\tThe running kernel has no actions: removing what you "
	        "booted from is how a machine stops booting. Reboot into another "
	        "one first.\t-");

	/* What this machine boots with, named. The gap this pane used to describe
	 * — install a kernel, get no boot entry — is real and is what "Make
	 * bootable" now closes, so the standing text says which mechanism will run
	 * rather than telling the user they are on their own. */
	if (nloaders == 0) {
		rec_row("-\t-\t-\tNo bootloader configuration was found, so whether a "
		        "kernel is bootable cannot be answered here. Looked for "
		        "limine.conf and loader/entries under /boot, /boot/efi and "
		        "/efi, and /boot/grub/grub.cfg.\t-");
	} else {
		for (int b = 0; b < nloaders; b++) {
			const char *how =
				loaders[b].kind == SYN_BL_GRUB
				  ? "“Make bootable” runs grub-mkconfig, which finds every "
				    "installed kernel by itself"
				: loaders[b].kind == SYN_BL_SYSTEMD
				  ? "“Make bootable” runs kernel-install, which writes a Boot "
				    "Loader Spec entry"
				: have_cmd("limine-update")
				  ? "“Make bootable” runs limine-update, which regenerates the "
				    "kernel entries"
				  : "limine has no entry generator installed — “Make bootable” "
				    "installs limine-mkinitcpio-hook, which adds one plus a "
				    "pacman hook so future kernels are handled automatically";

			rec_row("-\t-\t-\tBootloader: %s (%s). %s.\t-",
			        syn_boot_name(loaders[b].kind), loaders[b].conf, how);
		}

		if (nloaders > 1)
			rec_row("-\t-\t-\t⚠ More than one bootloader is configured here. "
			        "Which one the firmware actually starts cannot be read "
			        "reliably, so a kernel counts as bootable if ANY of them "
			        "can boot it.\t-");

		rec_row("-\t-\t-\tWhich kernel boots by DEFAULT is still the "
		        "bootloader's choice, not this pane's — pick it at the boot "
		        "menu.\t-");
	}

	return 0;
}
