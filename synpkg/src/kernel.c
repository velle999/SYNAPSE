/* kernel.c — noticing that the kernel you are RUNNING has been replaced.
 *
 * THE FAILURE THIS EXISTS TO CATCH
 *
 * Upgrading a kernel package on Arch DELETES /usr/lib/modules/<old release>/
 * and writes a new directory beside it. The running kernel keeps running — it
 * is in memory — but every module it has not already loaded is gone from the
 * disk. Plug in a USB device whose driver was not loaded, mount a filesystem
 * you have not mounted this boot, start a VM, connect a phone: modprobe looks
 * in a directory that no longer exists and fails. Nothing announces this. The
 * symptom is hardware that "stopped working", hours after an update that
 * reported success.
 *
 * Measured on the development machine while writing this, which is how the
 * shape of the check was decided: linux-cachyos 7.1.6-1 -> 7.1.8-1 upgraded at
 * 13:00, the machine last booted the evening before, `uname -r` reporting
 * 7.1.6-1-cachyos and /usr/lib/modules/7.1.6-1-cachyos already deleted. The
 * owner of the machine believed he was running 7.1.8, because that is what was
 * installed. Installed is not running.
 *
 * HOW A KERNEL IS RECOGNISED
 *
 * Not by name. "linux", "linux-lts", "linux-zen", "linux-cachyos",
 * "linux-cachyos-bore", "linux-hardened" and every -rt and vendor variant would
 * be a list to keep up to date forever, and it would be wrong the first time
 * someone installed one that is not on it.
 *
 * Instead: every packaged Arch kernel installs /usr/lib/modules/<release>/pkgbase
 * containing the name of the package that owns it. That file IS the definition —
 * mkinitcpio, the pacman hooks and dkms all key off it. Reading the directory
 * finds every kernel on the machine, including ones this program has never
 * heard of, and costs one readdir.
 *
 * WHY THE PROMPT IS NOT confirm()
 *
 * confirm() returns TRUE under --noconfirm, and --noconfirm is what the
 * graphical updater passes. Reusing it here would mean the Update button
 * silently rebooted the desktop out from under whoever pressed it. A reboot is
 * not a step in the transaction they approved, so the prompt below asks only on
 * a real terminal, defaults to NO, and otherwise just says what happened.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"
#include "i18n.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#define MODULES_DIR "/usr/lib/modules"

/* Overridable so the suite can point this at a synthetic tree with known
 * contents. Without it the only thing testable is whatever kernels happen to be
 * on the machine running the tests, which differs between velle's box, a
 * makepkg chroot and the ISO build — the definition of a test that gets
 * disabled. Same shape as SYNPKG_CURATED and SYNPKG_PACMAN_LOG. */
static const char *modules_dir(void)
{
	const char *d = getenv("SYNPKG_MODULES_DIR");
	return (d && *d) ? d : MODULES_DIR;
}

struct sp_kernel {
	char *release;   /* the directory name, e.g. "7.1.8-1-cachyos" */
	char *pkgbase;   /* what its pkgbase file says, e.g. "linux-cachyos" */
	char *version;   /* that package's installed version, or NULL if unowned */
};

static char *read_pkgbase(const char *dir)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s/pkgbase", modules_dir(), dir);

	FILE *f = fopen(path, "re");
	if (!f)
		return NULL;

	char line[128];
	char *got = fgets(line, sizeof line, f);
	fclose(f);
	if (!got)
		return NULL;

	strip_trailing_newline(line);
	if (!*line)
		return NULL;
	return xstrdup(line);
}

/* Every packaged kernel on this machine, with the version of the package that
 * owns it. Returns the count; *out is malloc'd (NULL when the count is 0). */
size_t sp_kernel_snapshot(sp_kernel **out)
{
	*out = NULL;

	DIR *d = opendir(modules_dir());
	if (!d)
		return 0;

	/* One handle for the whole scan. A query handle, not a write one: this must
	 * never install the transaction callbacks, or a snapshot taken in the
	 * middle of an upgrade would print progress bars of its own. */
	alpm_handle_t *h = sp_alpm_init(false);
	alpm_db_t *local = h ? alpm_get_localdb(h) : NULL;

	sp_kernel *ks = NULL;
	size_t n = 0, cap = 0;
	struct dirent *e;

	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;

		char *base = read_pkgbase(e->d_name);
		if (!base)
			continue;   /* no pkgbase: not a packaged kernel (dkms leftovers,
			             * a hand-built kernel, an extramodules directory) */

		if (n == cap) {
			cap = cap ? cap * 2 : 4;
			ks = xrealloc(ks, cap * sizeof *ks);
		}
		ks[n].release = xstrdup(e->d_name);
		ks[n].pkgbase = base;
		ks[n].version = NULL;

		if (local) {
			alpm_pkg_t *p = alpm_db_get_pkg(local, base);
			const char *v = p ? alpm_pkg_get_version(p) : NULL;
			if (v)
				ks[n].version = xstrdup(v);
		}
		n++;
	}

	closedir(d);
	if (h)
		sp_alpm_free(h);

	*out = ks;
	return n;
}

void sp_kernel_free(sp_kernel *k, size_t n)
{
	if (!k)
		return;
	for (size_t i = 0; i < n; i++) {
		free(k[i].release);
		free(k[i].pkgbase);
		free(k[i].version);
	}
	free(k);
}

static const sp_kernel *find_pkgbase(const sp_kernel *k, size_t n, const char *base)
{
	for (size_t i = 0; i < n; i++)
		if (!strcmp(k[i].pkgbase, base))
			return &k[i];
	return NULL;
}

static bool has_release(const sp_kernel *k, size_t n, const char *release)
{
	for (size_t i = 0; i < n; i++)
		if (!strcmp(k[i].release, release))
			return true;
	return false;
}

/* ── status ─────────────────────────────────────────────────────────────────
 *
 * The same facts, reported rather than diffed, for `synpkg status`.
 *
 * This exists because the question it answers is one people get wrong with
 * confidence: asked what kernel he was running, the owner of the development
 * machine said 7.1.8 — the version `pacman -Q` reports — while `uname -r` said
 * 7.1.6, because the 7.1.8 upgrade had landed that afternoon and the machine had
 * last booted the night before. Installed and running are different facts and
 * nothing on a stock Arch shows them side by side.
 *
 * It is also how the reboot logic gets tested without upgrading a kernel.
 */
void sp_kernel_status(void)
{
	sp_kernel *k = NULL;
	size_t n = sp_kernel_snapshot(&k);

	struct utsname u;
	const char *running = uname(&u) == 0 ? u.release : NULL;
	bool running_installed = running && has_release(k, n, running);

	for (size_t i = 0; i < n; i++) {
		bool is_run = running && !strcmp(k[i].release, running);
		const char *ver = k[i].version ? k[i].version : "?";

		if (g_out == OUT_TSV) {
			tsv_row(4, "kernel", k[i].pkgbase, ver,
			        is_run ? "running" : "installed");
			continue;
		}
		printf("%s%-16s%s%-14s %s%s%s%s%s%s\n", C_DIM(), "Kernel", C_RESET(),
		       k[i].pkgbase, C_DIM(), ver, C_RESET(),
		       is_run ? C_OK() : "", is_run ? "  — running" : "", C_RESET());
	}

	if (running && !running_installed) {
		if (g_out == OUT_TSV) {
			tsv_row(4, "kernel", "", running, "reboot-pending");
		} else {
			printf("%s%-16s%s%-14s %s%s%s\n", C_DIM(), "Kernel", C_RESET(),
			       "(running)", C_WARN(), running, C_RESET());
			printf("%s%-16s%s%s\n", C_DIM(), "", C_RESET(),
			       "no longer installed — reboot to run the kernel you have");
		}
	}

	sp_kernel_free(k, n);
}

/* ── the kernel STAGED FOR BOOT ─────────────────────────────────────────────
 *
 * sp_kernel_status() answers "installed vs running". This answers the third
 * question, and it is the one that actually strands a machine: what will the
 * bootloader LOAD next time, and does that kernel still have modules on disk?
 *
 * They are not the same file. limine-entry-tool does not boot
 * /boot/vmlinuz-<pkgbase> — it boots a private copy at
 * /boot/<machine-id>/<pkgbase>/vmlinuz, pinned in limine.conf by a BLAKE2B hash
 * OF THAT COPY. Refresh one and not the other and nothing at any layer reports
 * a problem: the hash still matches its own stale file, so the bootloader loads
 * it faithfully, while the upgrade that wrote the new kernel deleted the old
 * one's /usr/lib/modules tree. What boots has no root driver, no crypto and no
 * console, and dies in the initramfs.
 *
 * That is not hypothetical. On 2026-08-13 limine stayed pinned to
 * 7.1.6-1-cachyos after linux-cachyos moved to 7.1.8-1, because synpkg was not
 * registering /etc/pacman.d/hooks/ and so ran Arch's mkinitcpio hook in place
 * of limine's override. The hook bug is fixed; this is the check that would
 * have caught it BEFORE the reboot instead of after.
 *
 * The release is read out of the image itself, never from the filename or the
 * config comment — those are precisely what went stale while looking right.
 */

/* Images are located from each kernel's own pkgbase, never by scanning a
 * directory. A scan would sweep up /boot/<machine-id>/limine_history/, whose
 * kernels are SUPPOSED to be old: they pair with btrfs snapshot entries that
 * carry their own matching /usr/lib/modules inside the snapshot. Reporting
 * those would be a false alarm on every machine with snapshots enabled. */
static const char *BOOT_ROOTS[] = { "/boot", "/efi", "/boot/efi" };

static char *machine_id(void)
{
	const char *env = getenv("SYNPKG_MACHINE_ID");
	if (env && *env)
		return xstrdup(env);

	FILE *f = fopen("/etc/machine-id", "re");
	if (!f)
		return NULL;
	char line[64];
	char *got = fgets(line, sizeof line, f);
	fclose(f);
	if (!got)
		return NULL;
	strip_trailing_newline(line);
	return *line ? xstrdup(line) : NULL;
}

/* The release string baked into an x86 bzImage.
 *
 * The setup header carries "HdrS" at 0x202 and, at 0x20e, a u16 holding the
 * offset of the version string measured from 0x200. This is the same field
 * file(1) reports; checked against all three kernel images on the development
 * machine (two cachyos, one arch) before being relied on.
 *
 * NULL for anything that is not a bzImage — a UKI, an arm64 Image, a truncated
 * file — and NULL means "no opinion", never "stale". Guessing here would put a
 * scary warning on machines that boot perfectly well. */
static char *image_release(const char *path)
{
	FILE *f = fopen(path, "re");
	if (!f)
		return NULL;

	char magic[4];
	unsigned char off[2];
	if (fseek(f, 0x202, SEEK_SET) != 0 || fread(magic, 1, 4, f) != 4 ||
	    memcmp(magic, "HdrS", 4) != 0 ||
	    fseek(f, 0x20e, SEEK_SET) != 0 || fread(off, 1, 2, f) != 2) {
		fclose(f);
		return NULL;
	}

	/* Zero means the field is unused (pre-2.4 images), not offset 0x200. */
	long at = (long)(off[0] | (off[1] << 8)) + 0x200;
	char buf[128];
	size_t got = 0;
	if (at > 0x200 && fseek(f, at, SEEK_SET) == 0)
		got = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	if (got == 0)
		return NULL;

	/* "7.1.8-1-cachyos (linux-cachyos@cachyos) #1 SMP ..." — the release, then
	 * the builder and toolchain. The field is NUL-terminated in the file; cut at
	 * that or at the first space, whichever comes first. */
	buf[got] = '\0';
	buf[strcspn(buf, " \t\r\n")] = '\0';
	return *buf ? xstrdup(buf) : NULL;
}

/* Does this exact release belong to this pkgbase? Membership rather than
 * equality, because mid-upgrade a pkgbase can briefly own two module trees and
 * comparing against only one of them would invent a fault. */
static bool pkgbase_has_release(const sp_kernel *k, size_t n,
                                const char *base, const char *release)
{
	for (size_t i = 0; i < n; i++)
		if (!strcmp(k[i].pkgbase, base) && !strcmp(k[i].release, release))
			return true;
	return false;
}

void sp_boot_status(void)
{
	sp_kernel *k = NULL;
	size_t n = sp_kernel_snapshot(&k);
	if (n == 0) {
		sp_kernel_free(k, n);
		return;
	}

	char *mid = machine_id();

	const char *roots[sizeof BOOT_ROOTS / sizeof BOOT_ROOTS[0]];
	size_t nroots = 0;
	const char *override = getenv("SYNPKG_BOOT_DIR");
	if (override && *override) {
		roots[nroots++] = override;
	} else {
		for (size_t i = 0; i < sizeof BOOT_ROOTS / sizeof BOOT_ROOTS[0]; i++)
			roots[nroots++] = BOOT_ROOTS[i];
	}

	for (size_t i = 0; i < n; i++) {
		/* One pass per pkgbase, not per module tree, or a kernel that owns two
		 * releases would be reported twice with identical rows. */
		bool done = false;
		for (size_t j = 0; j < i && !done; j++)
			done = !strcmp(k[j].pkgbase, k[i].pkgbase);
		if (done)
			continue;

		char *shown = NULL;   /* last release printed, to collapse duplicates */

		for (size_t r = 0; r < nroots; r++) {
			char *paths[2];
			size_t np = 0;
			paths[np++] = xasprintf("%s/vmlinuz-%s", roots[r], k[i].pkgbase);
			if (mid)
				paths[np++] = xasprintf("%s/%s/%s/vmlinuz", roots[r], mid,
				                        k[i].pkgbase);

			for (size_t p = 0; p < np; p++) {
				char *rel = image_release(paths[p]);
				if (!rel) {
					free(paths[p]);
					continue;
				}

				/* Orphaned is the fatal one and outranks stale: an image whose
				 * modules are gone cannot boot, whereas an image merely behind
				 * the installed package still has a tree to load from. */
				bool orphan = !has_release(k, n, rel);
				bool stale  = !pkgbase_has_release(k, n, k[i].pkgbase, rel);
				const char *state = orphan ? "orphaned" : stale ? "stale" : "ok";

				if (g_out == OUT_TSV) {
					tsv_row(4, "boot", paths[p], rel, state);
				} else if (orphan) {
					printf("%s%-16s%s%-14s %s%s%s\n", C_DIM(), "Boot image",
					       C_RESET(), k[i].pkgbase, C_WARN(), rel, C_RESET());
					printf("%s%-16s%s%s\n", C_DIM(), "", C_RESET(), paths[p]);
					printf("%s%-16s%s%s\n", C_DIM(), "", C_RESET(),
					       "its modules are NOT installed — this entry cannot "
					       "boot. Run: sudo limine-mkinitcpio");
				} else if (stale) {
					printf("%s%-16s%s%-14s %s%s%s\n", C_DIM(), "Boot image",
					       C_RESET(), k[i].pkgbase, C_WARN(), rel, C_RESET());
					printf("%s%-16s%s%s%s\n", C_DIM(), "", C_RESET(), paths[p],
					       "  — behind the installed kernel");
				} else if (!shown || strcmp(shown, rel) != 0) {
					printf("%s%-16s%s%-14s %s%s%s\n", C_DIM(), "Boot image",
					       C_RESET(), k[i].pkgbase, C_DIM(), rel, C_RESET());
				}

				free(shown);
				shown = rel;
				free(paths[p]);
			}
		}
		free(shown);
	}

	free(mid);
	sp_kernel_free(k, n);
}

/* Ask, on a terminal, defaulting to NO. See the header for why this is not
 * confirm(). */
static bool ask_reboot(void)
{
	if (g_noconfirm || g_out == OUT_TSV || !isatty(STDIN_FILENO))
		return false;

	fputs("Reboot now? [y/N] ", stderr);
	fflush(stderr);

	char line[16];
	if (!fgets(line, sizeof line, stdin))
		return false;
	return line[0] == 'y' || line[0] == 'Y';
}

/* Diff the snapshot taken before the upgrade against the machine now, and say
 * what it means. Silent when no kernel moved — which is most upgrades, and
 * covers a transaction that failed or was declined without needing to know
 * that it was. */
void sp_kernel_reboot_check(const sp_kernel *before, size_t n_before)
{
	sp_kernel *after = NULL;
	size_t n_after = sp_kernel_snapshot(&after);

	/* Nothing to compare against: the snapshot could not be taken (no
	 * /usr/lib/modules at all, in a container). Not a fault, and not something
	 * to report — this machine has no kernel of its own to reboot into. */
	if (n_before == 0 && n_after == 0) {
		sp_kernel_free(after, n_after);
		return;
	}

	struct utsname u;
	const char *running = uname(&u) == 0 ? u.release : NULL;

	bool changed = false, ran_gone = false;
	size_t reported = 0;

	for (size_t i = 0; i < n_after; i++) {
		const sp_kernel *was = find_pkgbase(before, n_before, after[i].pkgbase);
		if (!was) {
			info("%s %s is newly installed — select it in the boot menu to use it",
			     after[i].pkgbase, after[i].version ? after[i].version : "");
			reported++;
			continue;
		}
		/* A NULL version on either side means the package could not be looked
		 * up; comparing that against a real version would invent a change. */
		if (!was->version || !after[i].version)
			continue;
		if (strcmp(was->version, after[i].version) != 0) {
			changed = true;
			info("%s was updated: %s -> %s", after[i].pkgbase,
			     was->version, after[i].version);
			reported++;
		}
	}

	for (size_t i = 0; i < n_before; i++) {
		if (!find_pkgbase(after, n_after, before[i].pkgbase)) {
			changed = true;
			warn(_("%s was removed"), before[i].pkgbase);
			reported++;
		}
	}

	/* The sharp end: the modules for the kernel this process is running on were
	 * there before the upgrade and are not there now. */
	if (running && has_release(before, n_before, running) &&
	    !has_release(after, n_after, running))
		ran_gone = true;

	if (!changed && !reported) {
		sp_kernel_free(after, n_after);
		return;
	}

	if (ran_gone) {
		warn(_("the modules for the kernel you are RUNNING (%s) were just deleted.\n"
		     "    Anything that has not already loaded its driver — a USB device, a\n"
		     "    filesystem, a VM module — will fail to load one until you reboot."),
		     running);
	} else if (changed && running && !has_release(after, n_after, running)) {
		/* Already gone before this run — an earlier upgrade nobody rebooted
		 * for. Worth one line, since another kernel just moved on top of it. */
		warn(_("you are still running %s, which is no longer installed"), running);
	}

	if (!changed) {
		sp_kernel_free(after, n_after);
		return;
	}

	if (g_out != OUT_TSV) {
		printf("\n%sThe kernel was updated. The one you are running (%s) stays "
		       "in memory until%s\n", C_WARN(), running ? running : "?", C_RESET());
		printf("%syou reboot.%s\n\n", C_WARN(), C_RESET());
	}

	if (ask_reboot()) {
		if (!have_cmd("systemctl")) {
			warn(_("systemctl is not available — reboot by hand"));
		} else {
			info("rebooting");
			char *argv[] = { (char *)"systemctl", (char *)"reboot", NULL };
			if (run(argv, false) != 0)
				warn(_("could not reboot — run `systemctl reboot` yourself"));
		}
	} else {
		info("reboot when convenient:  systemctl reboot");
	}

	sp_kernel_free(after, n_after);
}
