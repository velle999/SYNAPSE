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
			warn("%s was removed", before[i].pkgbase);
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
		warn("the modules for the kernel you are RUNNING (%s) were just deleted.\n"
		     "    Anything that has not already loaded its driver — a USB device, a\n"
		     "    filesystem, a VM module — will fail to load one until you reboot.",
		     running);
	} else if (changed && running && !has_release(after, n_after, running)) {
		/* Already gone before this run — an earlier upgrade nobody rebooted
		 * for. Worth one line, since another kernel just moved on top of it. */
		warn("you are still running %s, which is no longer installed", running);
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
			warn("systemctl is not available — reboot by hand");
		} else {
			info("rebooting");
			char *argv[] = { (char *)"systemctl", (char *)"reboot", NULL };
			if (run(argv, false) != 0)
				warn("could not reboot — run `systemctl reboot` yourself");
		}
	} else {
		info("reboot when convenient:  systemctl reboot");
	}

	sp_kernel_free(after, n_after);
}
