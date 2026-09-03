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
#include "i18n.h"

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
	{ "linux",           N_("the stock Arch kernel; what SynapseOS ships"),            NULL },
	{ "linux-lts",       N_("long-term support — the fallback worth having installed"), NULL },
	{ "linux-zen",       N_("desktop-tuned; usually the best for latency and gaming"),  NULL },
	{ "linux-hardened",  N_("security-hardened; slower, and it breaks some drivers"),   NULL },
	{ "linux-rt",        N_("real-time preemption; for audio work"),                    NULL },
	{ "linux-rt-lts",    N_("real-time on the LTS base"),                               NULL },

	/* CachyOS. Not in any Arch repository, so installing one adds [cachyos]
	 * first — see do_pkg(). Only the PLAIN repo is ever added: upstream's own
	 * bootstrap would also swap in their pacman fork and put -march-optimised
	 * rebuilds of core and extra ahead of [core], which is a different
	 * decision entirely from trying a kernel. */
	{ "linux-cachyos",          N_("CachyOS BORE + sched-ext; tuned hard for desktop "
	                            "responsiveness"), "cachyos" },
	{ "linux-cachyos-lts",      N_("the CachyOS patches on the LTS base — the Cachy "
	                            "kernel worth keeping as the fallback"), "cachyos" },
	{ "linux-cachyos-hardened", N_("CachyOS with the hardened patch set; same driver "
	                            "caveats as linux-hardened"), "cachyos" },
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
int syn_cachyos_enabled(void)
{
	/* Overridable, on the same terms as SYN_SETTINGS_RUNNING_RELEASE and
	 * SYN_SETTINGS_BOOT_ROOT: the suite has to check BOTH branches of the
	 * repo gate, and a machine is only ever in one of them. Without this the
	 * "installing a Cachy kernel enables the repo first" assertion silently
	 * stops meaning anything the moment the repo is enabled — which is the
	 * state every machine ends up in, so the check would rot exactly where it
	 * is needed. Reads nothing and writes nothing. */
	const char *env = getenv("SYN_SETTINGS_CACHYOS_ENABLED");
	if (env && (*env == '0' || *env == '1')) return *env == '1';

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
		rec_row("-\tunknown\t-\t%s\t-",
		        N_("pacman not available"));
		return 1;
	}

	/* The kernel release this machine booted — overridable, on exactly the
	 * terms and for exactly the reason as SYN_SETTINGS_BOOT_ROOT in boot.c.
	 *
	 * Every state on this pane pivots on is_running, and "running" takes
	 * precedence over the bootable check below. So on a machine booted into
	 * plain `linux` — which is every SynapseOS box here — the one kernel a
	 * fixture bootloader can name is the one whose bootable answer cannot be
	 * observed. The prefix-trap test could only pass on a machine booted into
	 * something else, which is not a test passing, it is a coincidence. It
	 * duly failed here and took the package build down with it.
	 *
	 * Safe for the same reason: this binary is not setuid and this grants
	 * nothing. It changes which row is LABELLED running, and nothing writes
	 * based on it. */
	struct utsname u;
	const char *env_rel = getenv("SYN_SETTINGS_RUNNING_RELEASE");
	const char *running = (env_rel && *env_rel) ? env_rel
	                    : (uname(&u) == 0 ? u.release : "");

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
		 * normal case — this is just that one's answer.
		 *
		 * TRI-STATE, and the third value is the one that matters: -1 is "the
		 * config is there and this process may not read it", which is what
		 * every GRUB install looks like — grub-mkconfig leaves grub.cfg 0600
		 * root:root. Folded into 0, it made the pane state, as a fact, that
		 * the kernel the machine was RUNNING had no boot entry. One definite
		 * yes from any loader wins; short of that, one blind answer means the
		 * row cannot say no either. */
		int bootable = 0;
		for (int b = 0; have && bootable != 1 && b < nloaders; b++) {
			int r = syn_boot_has_entry(&loaders[b], kernels[i].pkg, release);
			if (r == 1) bootable = 1;
			else if (r < 0) bootable = -1;
		}

		/* And which one the machine will actually PICK. "Bootable" was still
		 * only half the answer: a kernel can be installed, have an entry, and
		 * never boot, because the loader goes on choosing whatever it chose
		 * before. -1 means the loader could not be asked, which is not the
		 * same as "no" and must not be drawn as one. */
		int is_default = 0;
		for (int b = 0; have && !is_default && b < nloaders; b++)
			is_default = syn_boot_is_default(&loaders[b], kernels[i].pkg,
			                                 release) == 1;

		/* "installed" alone was the lie. A kernel on disk that the
		 * bootloader has never heard of is not a kernel you can switch to. */
		const char *state = !have         ? "not installed"
		                  : is_running    ? (is_default ? "running, default" : "running")
		                  : bootable < 0  ? "installed, ENTRY UNKNOWN"
		                  : !bootable     ? "installed, NO BOOT ENTRY"
		                  : is_default    ? "installed, boots by default"
		                                  : "installed, bootable";

		/* A kernel from a repository this machine does not have. Said on the
		 * row rather than discovered at the click: "not found in any
		 * repository" after a password prompt reads as the pane offering
		 * software that does not exist. */
		int needs_repo = 0;
		if (kernels[i].repo && !have) {
			if (!cachy_checked) { cachy_on = syn_cachyos_enabled(); cachy_checked = 1; }
			needs_repo = !cachy_on;
		}

		char detail[768];
		/* Headers matter here and nowhere else on this pane: without them
		 * DKMS cannot build synapse_kmod or the NVIDIA module against this
		 * kernel, so it boots to a system with no GPU driver. Reported per
		 * kernel because it is per kernel. */
		snprintf(detail, sizeof detail, "%s%s%s%s", kernels[i].what,
		         needs_repo
		           ? N_("  ⓘ not in any Arch repository — installing adds the "
		                "CachyOS repo first (core and extra keep priority)")
		           : "",
		         have && !have_hdr
		           ? N_("  ⚠ headers MISSING — DKMS cannot build for it")
		           : "",
		         have && !is_running && bootable == 0
		           ? N_("  ⚠ no bootloader entry yet — use “Make bootable” before "
		                "you reboot expecting a choice")
		           : have && bootable < 0
		           ? N_("  ⚠ the bootloader config is root-only, so this pane "
		                "cannot say whether an entry exists — “Make bootable” "
		                "regenerates it and makes it readable")
		           : "");

		/* WHAT YOU CAN DO TO THIS KERNEL — a list, and it is the whole list.
		 *
		 * This was one token, `pkg:<name>`, for every row that was not asking
		 * to be made bootable, and the GUI answered it by drawing BOTH an
		 * Install and a Remove button. So a kernel that was not installed
		 * offered to remove itself, and one that was installed offered to
		 * install itself again: the buttons described the verb, never the
		 * machine. Reported 2026-08-12 as "the install and remove button
		 * disregard actual state".
		 *
		 * Splitting `pkg:` into `install:` and `remove:` is what makes the
		 * state legible, and a SPACE-SEPARATED LIST is what lets a row offer
		 * the two things that are genuinely both true at once — an installed
		 * kernel with no boot entry can be made bootable AND removed, and
		 * under one token it could only ever be the first.
		 *
		 * The C decides, never the QML. A button drawn from a verb the row did
		 * not offer is a button that has no idea what it is looking at.
		 */
		char action[192];
		int n = 0;
		action[0] = '\0';

		if (!have) {
			n += snprintf(action + n, sizeof action - n, "install:%s",
			              kernels[i].pkg);
		} else {
			/* Offered when the answer is no AND when it cannot be had: on a
			 * root-only grub.cfg this action is the thing that makes the
			 * config readable, so withholding it until the pane can read the
			 * config is a lock whose key is inside it. */
			if (bootable != 1 && nloaders > 0)
				n += snprintf(action + n, sizeof action - n, "boot:%s",
				              kernels[i].pkg);
			/* Offered for the RUNNING kernel too. Booting it once is not the
			 * same as it being what boots — this box ran linux-cachyos with
			 * limine still set to pick the stock kernel, which is exactly the
			 * gap this action closes. */
			if (bootable != 0 && !is_default && nloaders > 0)
				n += snprintf(action + n, sizeof action - n, "%sdefault:%s",
				              n ? " " : "", kernels[i].pkg);
			/* REMOVING WHAT YOU BOOTED is how a machine stops booting, so the
			 * running kernel never offers it. That rule is about removal
			 * alone: making the kernel you are running bootable is not only
			 * safe, it is the one case where the entry is provably missing
			 * while the kernel provably works. */
			if (!is_running)
				n += snprintf(action + n, sizeof action - n, "%sremove:%s",
				              n ? " " : "", kernels[i].pkg);
		}

		rec_row("%s\t%s\t%s\t%s\t%s",
		        kernels[i].pkg, have ? ver : "-", state, detail,
		        n ? action : "-");
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
	/*
	 * ⛔ THE RELEASE MOVED TO THE END OF THE SENTENCE, ON PURPOSE. The window
	 * looks up the WHOLE CELL, so a sentence with a kernel release in the
	 * middle of it can never be a msgid — every machine composes a different
	 * string. With the variable trailing, the marked sentence is a PREFIX of
	 * the cell, which is the shape the rest of this pane already uses and the
	 * one tests/i18n_test.sh's drawn-label check accepts.
	 *
	 * ⚠ AND THE VALUE IS AN ARGUMENT NOW. `⚠ running, unowned` sat inside the
	 * format string, where it is drawn on screen and reaches no template.
	 */
	if (running[0] && !running_is_owned)
		rec_row("-\t%s\t%s\t%s (%s)\t-",
		        running, N_("⚠ running, unowned"),
		        N_("No installed kernel package owns the release you booted — "
		           "so none of the rows above is the kernel you are on. Usually "
		           "this just means a kernel upgrade has not been rebooted into "
		           "yet; until you do, modules for the running kernel are gone "
		           "from /usr/lib/modules."),
		        running);

	/* Removing the kernel you booted from is the one action here that can
	 * leave a machine unbootable, so the running one carries no action at all
	 * (above) and the reason is said out loud rather than left to be inferred
	 * from a greyed-out button. */
	rec_row("-\t-\t-\t%s\t-",
	        N_("The running kernel has no actions: removing what you booted from is how a machine stops booting. Reboot into another one first."));

	/* What this machine boots with, named. The gap this pane used to describe
	 * — install a kernel, get no boot entry — is real and is what "Make
	 * bootable" now closes, so the standing text says which mechanism will run
	 * rather than telling the user they are on their own. */
	if (nloaders == 0) {
		rec_row("-\t-\t-\t%s\t-",
		        N_("No bootloader configuration was found, so whether a kernel is bootable cannot be answered here. Looked for limine.conf and loader/entries under /boot, /boot/efi and /efi, and /boot/grub/grub.cfg."));
	} else {
		for (int b = 0; b < nloaders; b++) {
			/* ⚠ THE FULL STOP IS INSIDE EACH MSGID. The format used to
			 * supply it (`… %s.`), which made the drawn cell one
			 * character longer than any msgid and matched nothing —
			 * whatever a cell ends up being has to BE a msgid, punctuation
			 * and all.  */
			const char *how =
				loaders[b].kind == SYN_BL_GRUB
				  ? N_("“Make bootable” runs grub-mkconfig, which finds every "
				       "installed kernel by itself.")
				: loaders[b].kind == SYN_BL_SYSTEMD
				  ? N_("“Make bootable” runs kernel-install, which writes a "
				       "Boot Loader Spec entry.")
				: have_cmd("limine-update")
				  ? N_("“Make bootable” runs limine-update, which regenerates "
				       "the kernel entries.")
				  : N_("limine has no entry generator installed — “Make "
				       "bootable” installs limine-mkinitcpio-hook, which adds "
				       "one plus a pacman hook so future kernels are handled "
				       "automatically.");

			/* ⛔ THE DATA GOES IN COLUMNS, NOT INTO THE SENTENCE. This
			 * read `Bootloader: %s (%s). %s.` — a cell built at runtime
			 * out of the loader's name, its config path and one of four
			 * explanations. The window translates a drawn cell by looking
			 * the WHOLE cell up, so a cell with a path in it can never
			 * match a msgid and stayed English in all thirteen languages
			 * however well `how` was marked.
			 *
			 * The row already had three columns spent on "-", and they
			 * are what the sentence was carrying: the loader in `kernel`,
			 * its config in `installed`, and `bootloader` in `state` to
			 * say what kind of row this is. `detail` is now nothing but
			 * the explanation, which IS a msgid. `--rec kernel | column
			 * -t` reads better for it, and every cell is either data or
			 * translatable.  */
			rec_row("%s\t%s\t%s\t%s\t-",
			        syn_boot_name(loaders[b].kind), loaders[b].conf,
			        N_("bootloader"), how);
		}

		if (nloaders > 1)
			rec_row("-\t-\t-\t%s\t-",
			        N_("⚠ More than one bootloader is configured here. Which one the firmware actually starts cannot be read reliably, so a kernel counts as bootable if ANY of them can boot it."));

		/* This row used to say the default was the bootloader's business and
		 * you should pick it at the menu. It was true and it was not enough:
		 * a kernel that is installed and bootable and never chosen is a
		 * kernel you are not running, and "reboot and catch the menu" is not
		 * a setting. Each loader now gets named with the mechanism that will
		 * actually run, the same way "Make bootable" does. */
		for (int b = 0; b < nloaders; b++) {
			/* ⛔ THE FULL STOP IS INSIDE THE MSGID, NOT IN THE FORMAT.
			 * The window translates the CELL, whole, by looking it up in
			 * the catalog — so `rec_row("…%s.\t-", how)` drew a sentence
			 * one character longer than any msgid and matched nothing,
			 * leaving these English however well translated. Whatever a
			 * cell ends up being has to BE a msgid, punctuation included. */
			const char *how =
				loaders[b].kind == SYN_BL_GRUB
				  ? N_("“Make default” sets GRUB_DEFAULT=saved in "
				       "/etc/default/grub, regenerates grub.cfg, then runs "
				       "grub-set-default — in that order, because GRUB_DEFAULT "
				       "is read when the config is generated, not at boot. The "
				       "confirmation lists all three.")
				: loaders[b].kind == SYN_BL_SYSTEMD
				  ? N_("“Make default” runs bootctl set-default.")
				  : N_("“Make default” writes default_entry: into limine.conf, "
				       "naming the entry by path so a reordering cannot quietly "
				       "change what it means.");

			rec_row("-\t-\t-\t%s\t-", how);
		}
	}

	return 0;
}
