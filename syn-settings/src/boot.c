/* syn-settings — which bootloader owns this machine, and what it can boot.
 *
 * SynapseOS installs one of THREE bootloaders (syn-install's SYN_BOOTLOADERS:
 * grub, systemd-boot, limine), and each answers "can I boot this kernel?"
 * somewhere different. The first version of this code checked two files with a
 * loop bounded at two and a comment promising a third case that was never
 * written, so on a systemd-boot install — where the ESP *is* /boot, so neither
 * limine.conf nor grub.cfg exists — every kernel was reported unbootable.
 *
 * THE ESP IS NOT ALWAYS /boot. syn-install's layout_esp_mount() puts it at
 * /boot for systemd-boot and limine (both keep kernels on the FAT32 partition)
 * and at /boot/efi for GRUB (which only needs its own binary there). Hardcoding
 * /boot/limine.conf happens to be right on this machine and is wrong by
 * construction on a GRUB install, so the paths are discovered.
 *
 * DETECTION IS BY CONFIG, NOT BY PACKAGE. This very machine has the `grub`
 * package installed with no grub.cfg anywhere — it boots limine. Asking pacman
 * which bootloaders are installed would confidently return the wrong answer.
 * A config file that exists is evidence; a package that exists is not.
 *
 * MATCHING IS BY KERNEL RELEASE, NOT BY FILENAME. limine and GRUB name the
 * image (`vmlinuz-linux-lts`), but systemd-boot entries written by
 * kernel-install follow the Boot Loader Spec and name neither the package nor
 * "vmlinuz" — they point at /<machine-id>/<release>/linux. So the release is
 * carried alongside, read from /usr/lib/modules/<release>/pkgbase, which is the
 * file that states which package owns a module tree. It is also what makes
 * "is this the running kernel?" an exact string compare against uname -r
 * instead of a numeric parse that called 7.1.6.arch1-1 and 7.1.6.arch2-1 the
 * same kernel.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int is_dir(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_file(const char *p)
{
	struct stat st;
	return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* Where an ESP could be mounted. syn-install uses the first two; /efi is the
 * systemd convention and costs nothing to look at. Order matters only in that
 * the first hit for a given loader wins, and a machine with two of these
 * mounted has bigger problems than this pane. */
static const char *esp_candidates[] = { "/boot", "/boot/efi", "/efi" };

/* A prefix to look under instead of /.
 *
 * This exists for the test suite and nothing else. The systemd-boot case is
 * the one that was broken, and it is the one this project's own machines
 * cannot exercise — every SynapseOS box here boots limine or GRUB, so the bug
 * survived precisely because "run it and look" could never have caught it.
 * With a prefix the suite builds a fixture ESP and asserts the answer.
 *
 * Safe to read from the environment because this binary is not setuid and
 * grants nothing: an attacker who can set it can already run anything as you.
 * It affects only which files are READ — every write still names its own
 * absolute path.
 */
static const char *boot_root(void)
{
	const char *r = getenv("SYN_SETTINGS_BOOT_ROOT");
	return (r && *r) ? r : "";
}

/* Find every bootloader with a config actually present.
 *
 * Deliberately plural. A machine can carry more than one — an install that
 * switched loaders leaves the old config behind, and a dual-boot ESP may hold
 * someone else's. Guessing which one the firmware will pick is not something
 * this pane can do honestly (BootCurrent on this machine points at the
 * removable fallback path \EFI\BOOT\BOOTX64.EFI, which names no loader at
 * all), so when there are several the caller is told so rather than sold a
 * coin flip.
 */
int syn_boot_detect(struct syn_boot *out, size_t max)
{
	size_t n = 0;
	const char *root = boot_root();

	for (size_t i = 0; i < sizeof esp_candidates / sizeof esp_candidates[0]; i++) {
		char espbuf[256];
		if (snprintf(espbuf, sizeof espbuf, "%s%s", root, esp_candidates[i])
		    >= (int)sizeof espbuf)
			continue;
		const char *esp = espbuf;
		if (!is_dir(esp)) continue;

		/* Composed straight into the destination and length-checked there, so
		 * a path that would not fit is dropped rather than silently truncated
		 * into a shorter path that might exist and name something else. */

		/* limine: a single limine.conf at the top of the ESP. */
		if (n < max &&
		    snprintf(out[n].conf, sizeof out[n].conf, "%s/limine.conf", esp)
		      < (int)sizeof out[n].conf &&
		    is_file(out[n].conf)) {
			out[n].kind = SYN_BL_LIMINE;
			snprintf(out[n].esp, sizeof out[n].esp, "%s", esp);
			n++;
		}

		/* systemd-boot: loader/entries holds one .conf per entry. The
		 * directory is the evidence — loader.conf can be absent on an ESP
		 * that another OS's installer touched. */
		if (n < max &&
		    snprintf(out[n].conf, sizeof out[n].conf, "%s/loader/entries", esp)
		      < (int)sizeof out[n].conf &&
		    is_dir(out[n].conf)) {
			out[n].kind = SYN_BL_SYSTEMD;
			snprintf(out[n].esp, sizeof out[n].esp, "%s", esp);
			n++;
		}
	}

	/* GRUB keeps grub.cfg under /boot/grub regardless of where the ESP is
	 * mounted — grub-install --efi-directory only decides where the EFI binary
	 * goes. So this is not part of the ESP loop. */
	if (n < max &&
	    snprintf(out[n].conf, sizeof out[n].conf, "%s/boot/grub/grub.cfg", root)
	      < (int)sizeof out[n].conf &&
	    is_file(out[n].conf)) {
		out[n].kind = SYN_BL_GRUB;
		snprintf(out[n].esp, sizeof out[n].esp, "%s/boot", root);
		n++;
	}

	return (int)n;
}

const char *syn_boot_name(enum syn_bl kind)
{
	switch (kind) {
	case SYN_BL_LIMINE:  return "limine";
	case SYN_BL_SYSTEMD: return "systemd-boot";
	case SYN_BL_GRUB:    return "grub";
	default:             return "unknown";
	}
}

/* Does `hay` contain `needle` as a whole token?
 *
 * "vmlinuz-linux" is a prefix of "vmlinuz-linux-lts", so a bare strstr reports
 * the stock kernel bootable on the strength of an LTS entry — the pane would
 * say "bootable" about the one kernel that is not. The character after the
 * match has to end the name.
 */
static int token_match(const char *hay, const char *needle)
{
	size_t nlen = strlen(needle);
	for (const char *p = strstr(hay, needle); p; p = strstr(p + 1, needle)) {
		char after = p[nlen];
		if (after == '\0' || after == '\n' || after == '\r' || after == ' ' ||
		    after == '\t' || after == '"'  || after == '\'' || after == '/')
			return 1;
	}
	return 0;
}

/* Scan one file for either the image name or the kernel release. */
static int file_names_kernel(const char *path, const char *img, const char *release)
{
	FILE *f = fopen(path, "re");
	if (!f) return 0;

	char line[4096];
	int hit = 0;
	while (!hit && fgets(line, sizeof line, f)) {
		if (img && *img && token_match(line, img)) hit = 1;
		else if (release && *release && token_match(line, release)) hit = 1;
	}
	fclose(f);
	return hit;
}

/* Can this bootloader boot the kernel from package `pkg` (release `release`)?
 *
 * `release` may be empty when the package is not installed, in which case only
 * the image name is looked for — an entry can legitimately outlive the package
 * it was written for, and saying so is more useful than pretending the entry
 * is not there.
 */
int syn_boot_has_entry(const struct syn_boot *bl, const char *pkg,
                       const char *release)
{
	char img[128];
	snprintf(img, sizeof img, "vmlinuz-%s", pkg);

	switch (bl->kind) {
	case SYN_BL_LIMINE:
	case SYN_BL_GRUB:
		return file_names_kernel(bl->conf, img, NULL);

	case SYN_BL_SYSTEMD: {
		/* One .conf per entry. Both layouts are live on SynapseOS: the
		 * installer writes `linux /vmlinuz-linux` by hand, while
		 * kernel-install writes `linux /<machine-id>/<release>/linux` — which
		 * contains neither "vmlinuz" nor the package name. Hence the release. */
		DIR *d = opendir(bl->conf);
		if (!d) return 0;

		int hit = 0;
		struct dirent *de;
		while (!hit && (de = readdir(d))) {
			const char *dot = strrchr(de->d_name, '.');
			if (!dot || strcmp(dot, ".conf")) continue;

			char path[1024];
			if (snprintf(path, sizeof path, "%s/%s", bl->conf, de->d_name)
			    >= (int)sizeof path)
				continue;
			hit = file_names_kernel(path, img, release);
		}
		closedir(d);
		return hit;
	}

	default:
		return 0;
	}
}

/* ── Making a kernel bootable ───────────────────────────────────────────────
 *
 * THE ESCALATION. Everything else this app writes is handed to a systemd tool
 * that does its own polkit check, so the binary is not setuid and grants
 * nothing. There is no such tool for boot entries: grub-mkconfig and
 * kernel-install both simply need root. src/probe.c declined to escalate for
 * the connector re-probe and said the posture call belonged to whoever owns
 * the OS rather than to whoever was writing the pane; velle made that call for
 * this operation.
 *
 * So: pkexec, exactly as synpkg does it (src/trans.c). No polkit policy of our
 * own ships — without one, pkexec demands admin authentication, which is the
 * property worth keeping. A shipped .policy granting these commands to an
 * active session is how a settings app becomes the most convenient privilege
 * escalation on the machine.
 *
 * NOTHING RUNS WITHOUT --confirm. The GUI shows a dialogue naming the
 * bootloader, the config file and the command, and only then passes the flag.
 * Enforcing it here rather than only in QML is deliberate: the C binary is the
 * real boundary, and a confirmation that lives in the GUI is a confirmation
 * anything else can skip.
 */

static int boot_refuse(const char *msg)
{
	fprintf(stderr, "syn-settings: %s\n", msg);
	return 2;
}

/* Build the command that makes `pkg` bootable under `bl`.
 *
 * Each is the mechanism that bootloader's own ecosystem uses. None of them
 * hand-writes a boot config: this app has no business composing an entry, and
 * every one of these generators is maintained by people who do.
 */
static int boot_command(const struct syn_boot *bl, const char *pkg,
                        const char *release, char *argv[8], char *scratch,
                        size_t scap, const char **why)
{
	int n = 0;

	switch (bl->kind) {
	case SYN_BL_GRUB:
		/* grub-mkconfig regenerates from scratch and its 10_linux finds every
		 * installed kernel by itself, so this needs no per-kernel argument. It
		 * is also exactly what syn-install runs at install time, which makes
		 * this the one action here with a proven precedent on this OS. */
		*why = "grub-mkconfig regenerates grub.cfg; its 10_linux script finds "
		       "every installed kernel";
		argv[n++] = (char *)"pkexec";
		argv[n++] = (char *)"grub-mkconfig";
		argv[n++] = (char *)"-o";
		snprintf(scratch, scap, "%s", bl->conf);
		argv[n++] = scratch;
		break;

	case SYN_BL_SYSTEMD:
		/* kernel-install writes a Boot Loader Spec entry using systemd's
		 * 90-loaderentry.install and mkinitcpio's 50-mkinitcpio.install. It
		 * needs the release and the image, and the image is where the module
		 * tree keeps it. */
		if (!release || !*release) {
			*why = NULL;
			return boot_refuse("that kernel has no module tree, so there is no "
			                   "release to install an entry for — install the "
			                   "kernel first");
		}
		*why = "kernel-install writes a Boot Loader Spec entry for this release";
		snprintf(scratch, scap, "/usr/lib/modules/%s/vmlinuz", release);
		if (access(scratch, R_OK) != 0) {
			*why = NULL;
			return boot_refuse("the kernel image is missing from that module "
			                   "tree — reinstall the kernel package");
		}
		argv[n++] = (char *)"pkexec";
		argv[n++] = (char *)"kernel-install";
		argv[n++] = (char *)"add";
		argv[n++] = (char *)release;
		argv[n++] = scratch;
		break;

	case SYN_BL_LIMINE:
		/* limine ships no entry generator; limine-mkinitcpio-hook is it, from
		 * the same upstream as the limine-snapper-sync this OS already vendors
		 * and already names as an optdepend. If it is present, run it; if not,
		 * installing it IS the fix, and it brings a pacman hook so no future
		 * kernel needs this button again. */
		if (have_cmd("limine-update")) {
			*why = "limine-update regenerates the kernel entries in limine.conf";
			argv[n++] = (char *)"pkexec";
			argv[n++] = (char *)"limine-update";
		} else {
			/* Say that it may BUILD. On a machine whose repositories do
			 * not carry it — any limine install predating the package —
			 * synpkg falls back to the AUR, and this one is a GraalVM
			 * native-image build: several minutes. It used to run with its
			 * output discarded, which made that indistinguishable from a
			 * hung settings app; run_or_show_progress() now forwards what it
			 * says, so the wait is visibly a wait. The warning stays — the
			 * duration is real, and only the silence was fixed. */
			*why = "limine has no entry generator of its own; installing "
			       "limine-mkinitcpio-hook adds one, plus a pacman hook so "
			       "future kernels are handled automatically. If your "
			       "repositories do not carry it, synpkg builds it from the "
			       "AUR — that can take SEVERAL MINUTES; progress is shown "
			       "while it runs";
			if (!have_cmd("synpkg")) {
				*why = NULL;
				return boot_refuse("synpkg is not installed; it is what "
				                   "performs the install");
			}
			argv[n++] = (char *)"synpkg";
			argv[n++] = (char *)"--verbose";
			argv[n++] = (char *)"install";
			argv[n++] = (char *)"limine-mkinitcpio-hook";
		}
		break;

	default:
		*why = NULL;
		return boot_refuse("no bootloader configuration found");
	}

	argv[n] = NULL;
	(void)pkg;
	return 0;
}

int do_boot(int argc, char **argv)
{
	if (argc < 1)
		return boot_refuse("boot needs a kernel name "
		                   "(boot <kernel> [--loader <name>] [--confirm])");

	const char *pkg = argv[0];
	const char *want_loader = NULL;
	int confirmed = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--confirm")) { confirmed = 1; continue; }
		if (!strcmp(argv[i], "--loader")) {
			if (++i >= argc) return boot_refuse("--loader needs a name");
			want_loader = argv[i];
			continue;
		}
		return boot_refuse("unknown option — try --loader <name> or --confirm");
	}

	if (!syn_kernel_known(pkg))
		return boot_refuse("that is not one of the kernels this pane manages");

	struct syn_boot found[6];
	int n = syn_boot_detect(found, sizeof found / sizeof found[0]);

	if (n == 0)
		return boot_refuse("no bootloader configuration found — looked for "
		                   "limine.conf and loader/entries on /boot, /boot/efi "
		                   "and /efi, and /boot/grub/grub.cfg");

	/* Which one. With several present, picking for the user means picking
	 * which config file to rewrite, and being wrong there is the failure this
	 * whole pane exists to avoid. */
	const struct syn_boot *bl = NULL;
	if (want_loader) {
		for (int i = 0; i < n; i++)
			if (!strcmp(syn_boot_name(found[i].kind), want_loader))
				bl = &found[i];
		if (!bl)
			return boot_refuse("no configuration present for that bootloader");
	} else if (n == 1) {
		bl = &found[0];
	} else {
		fprintf(stderr, "syn-settings: more than one bootloader is configured "
		                "here; name one with --loader:\n");
		for (int i = 0; i < n; i++)
			fprintf(stderr, "    %-13s %s\n",
			        syn_boot_name(found[i].kind), found[i].conf);
		return 2;
	}

	char release[128] = "";
	syn_kernel_release(pkg, release, sizeof release);

	char *cmd[8];
	char scratch[512];
	const char *why = NULL;
	int rc = boot_command(bl, pkg, release, cmd, scratch, sizeof scratch, &why);
	if (rc != 0) return rc;

	/* --dry-run is how the GUI populates its confirmation dialogue: it asks
	 * what would happen, shows exactly that, and only then re-runs with
	 * --confirm. One code path decides, so the dialogue cannot describe
	 * something other than what runs. */
	if (!confirmed && !g_dry_run) {
		fprintf(stderr,
		        "syn-settings: this changes boot configuration and needs "
		        "--confirm.\n"
		        "  bootloader : %s\n"
		        "  config     : %s\n"
		        "  because    : %s\n"
		        "  would run  :",
		        syn_boot_name(bl->kind), bl->conf, why ? why : "-");
		for (int i = 0; cmd[i]; i++) fprintf(stderr, " %s", cmd[i]);
		fputc('\n', stderr);
		return 2;
	}

	if (g_dry_run) {
		/* TSV, so the GUI parses it the way it parses every other read. */
		printf("loader\t%s\n", syn_boot_name(bl->kind));
		printf("config\t%s\n", bl->conf);
		printf("why\t%s\n", why ? why : "-");
		fputs("command\t", stdout);
		for (int i = 0; cmd[i]; i++) printf("%s%s", i ? " " : "", cmd[i]);
		putchar('\n');
		return 0;
	}

	/* Streamed. Every branch of this is slow enough to be doubted:
	 * grub-mkconfig probes every disk, kernel-install rebuilds an initramfs,
	 * and the limine branch may compile a package from source. */
	return run_or_show_progress(cmd);
}

/* The kernel release a package currently owns, e.g. "7.1.7-arch1-1".
 *
 * /usr/lib/modules/<release>/pkgbase holds the name of the package that owns
 * that module tree — it is what mkinitcpio's and limine's pacman hooks trigger
 * on, and it is the only place the package->release mapping is stated as fact
 * rather than inferred. pacman's version string cannot be used: it reads
 * "7.1.7.arch1-1" where uname reports "7.1.7-arch1-1", same build, different
 * punctuation.
 */
int syn_kernel_release(const char *pkg, char *out, size_t cap)
{
	if (cap) out[0] = '\0';

	DIR *d = opendir("/usr/lib/modules");
	if (!d) return 0;

	int found = 0;
	struct dirent *de;
	while (!found && (de = readdir(d))) {
		if (de->d_name[0] == '.') continue;

		char path[1024];
		if (snprintf(path, sizeof path, "/usr/lib/modules/%s/pkgbase",
		             de->d_name) >= (int)sizeof path)
			continue;

		char base[128];
		if (!read_line_file(path, base, sizeof base)) continue;
		if (strcmp(base, pkg)) continue;

		snprintf(out, cap, "%s", de->d_name);
		found = 1;
	}
	closedir(d);
	return found;
}
