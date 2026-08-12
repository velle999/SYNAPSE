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
 * MATCHING IS BY KERNEL RELEASE, NOT BY FILENAME. GRUB names the image
 * (`vmlinuz-linux-lts`), limine names it either way (see limine_line_boots —
 * its generated entries are Boot Loader Spec paths), but systemd-boot entries
 * written by kernel-install follow the Boot Loader Spec and name neither the package nor
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

#include <fnmatch.h>
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

/* Is this limine.conf line an entry that boots `pkg` (release `release`)?
 *
 * limine.conf carries TWO entry shapes at once on this OS and the first version
 * of this code knew only one of them. syn-install writes an entry by hand:
 *
 *     kernel_path: boot():/vmlinuz-linux
 *
 * while limine-mkinitcpio-hook — the generator the "Make bootable" button
 * installs — writes the Boot Loader Spec layout, which names neither "vmlinuz-"
 * nor the release:
 *
 *     comment: kernel-id=linux-cachyos
 *     path: boot():/<machine-id>/linux-cachyos/vmlinuz#<sha256>
 *
 * So the button worked, the entry was written, and the pane went on reporting
 * NO BOOT ENTRY — offering to fix a thing it had already fixed, for ever.
 *
 * It is NOT enough to look for the package name anywhere in the file. Where
 * limine-snapper-sync is installed, limine.conf also holds a snapshot list, and
 * each snapshot's comment is the pacman command line that made it:
 *
 *     comment: /usr/bin/synpkg --noconfirm install linux-cachyos ...
 *
 * That is a description of a transaction, not a boot entry — the snapshot below
 * it boots the OLD kernel. Only a line that actually names a kernel image
 * counts, plus the generator's own kernel-id declaration.
 */
static int limine_line_boots(const char *line, const char *pkg,
                             const char *release)
{
	const char *p = line;
	while (*p == ' ' || *p == '\t') p++;

	/* limine-entry-tool states the identity itself; take it at its word. */
	if (!strncmp(p, "comment:", 8)) {
		char id[160];
		snprintf(id, sizeof id, "kernel-id=%s", pkg);
		return token_match(p, id);
	}

	/* `path:` and `kernel_path:` name the image. `module_path:` is the
	 * initramfs — it sits in the same directory and would match the same
	 * segment, but an initramfs alone boots nothing, so it is not evidence. */
	const char *val;
	if (!strncmp(p, "kernel_path:", 12))   val = p + 12;
	else if (!strncmp(p, "path:", 5))      val = p + 5;
	else                                   return 0;

	char img[160];
	snprintf(img, sizeof img, "vmlinuz-%s", pkg);
	if (token_match(val, img)) return 1;

	/* BLS layout: the directory component before /vmlinuz is the kernel-id,
	 * which is the pkgbase. Some generators use the release there instead. */
	char seg[192];
	snprintf(seg, sizeof seg, "/%s/vmlinuz", pkg);
	if (strstr(val, seg)) return 1;
	if (release && *release) {
		snprintf(seg, sizeof seg, "/%s/vmlinuz", release);
		if (strstr(val, seg)) return 1;
	}
	return 0;
}

static int limine_has_entry(const char *conf, const char *pkg,
                            const char *release)
{
	FILE *f = fopen(conf, "re");
	if (!f) return 0;

	char line[4096];
	int hit = 0;
	while (!hit && fgets(line, sizeof line, f))
		hit = limine_line_boots(line, pkg, release);
	fclose(f);
	return hit;
}

/* ── The limine entry TREE ──────────────────────────────────────────────────
 *
 * limine.conf is a tree. Depth is the number of leading slashes, a `+` after
 * them means "branch, expanded", and everything until the next tree line is
 * that entry's body:
 *
 *     /+SynapseOS              depth 1, branch
 *       //linux-cachyos        depth 2, entry -> path "SynapseOS/linux-cachyos"
 *       path: boot():/...
 *     /EFI fallback            depth 1, entry -> path "EFI fallback"
 *
 * That path is how `default_entry` names an entry, and naming it by PATH is
 * the whole reason this walker exists: the alternative is the 1-based index,
 * and limine-mkinitcpio-hook reorders the generated entries whenever BOOT_ORDER
 * or the installed kernels change. An index would be a default that silently
 * comes to mean a different kernel.
 */
#define LIM_MAXDEPTH 8

/* Depth of a tree line, 0 if this line is not one. Sets *name to the entry
 * name (after the slashes and any `+`). */
static int lim_tree_line(const char *line, const char **name)
{
	const char *p = line;
	while (*p == ' ' || *p == '\t') p++;
	if (*p != '/') return 0;

	int depth = 0;
	while (*p == '/') { depth++; p++; }
	if (*p == '+') p++;
	*name = p;
	return depth;
}

/* Trailing newline and spaces off a name read from the file. limine writes
 * `comment: kernel-id=linux ` with a trailing space, and an entry name that
 * carries a stray \n composes a path that matches nothing. */
static void lim_trim(char *s)
{
	size_t n = strlen(s);
	while (n && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t'))
		s[--n] = '\0';
}

/* `/`, `\` and `#` are structural in an entry path and must be escaped when it
 * is written into default_entry. Kernel names never contain them; the branch
 * name is PRETTY_NAME out of os-release and could. */
static void lim_escape(const char *in, char *out, size_t cap)
{
	size_t o = 0;
	for (const char *p = in; *p && o + 2 < cap; p++) {
		if (*p == '/' || *p == '\\' || *p == '#') out[o++] = '\\';
		out[o++] = *p;
	}
	out[o < cap ? o : cap - 1] = '\0';
}

static void lim_unescape(const char *in, char *out, size_t cap)
{
	size_t o = 0;
	for (const char *p = in; *p && o + 1 < cap; p++) {
		if (*p == '\\' && (p[1] == '/' || p[1] == '\\' || p[1] == '#')) p++;
		out[o++] = *p;
	}
	out[o < cap ? o : cap - 1] = '\0';
}

/* The global `default_entry:` value, "" if unset. First occurrence wins:
 * limine's own precedence for a repeated global is not documented, and this
 * file only ever writes one. */
static void limine_default_value(const char *conf, char *out, size_t cap)
{
	if (cap) out[0] = '\0';
	FILE *f = fopen(conf, "re");
	if (!f) return;

	char line[4096];
	while (fgets(line, sizeof line, f)) {
		const char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (strncmp(p, "default_entry:", 14)) continue;
		p += 14;
		while (*p == ' ' || *p == '\t') p++;
		snprintf(out, cap, "%s", p);
		lim_trim(out);
		break;
	}
	fclose(f);
}

/* Walk the tree, calling nothing back — this fills in the two answers both
 * callers want, in ONE pass, because they need the same state:
 *
 *   want_pkg  : the path of the first entry that boots this kernel
 *   want_dflt : whether the entry `default_entry` names boots this kernel
 *
 * `target` is the unescaped default_entry path, or "" to select by index.
 * `target_idx` is that index; limine's default when unset is 1.
 */
struct lim_walk {
	char found_path[512];   /* path of the first entry booting pkg */
	char found_esc[768];    /* the same path, escaped for default_entry */
	int  found;
	int  default_boots_pkg; /* -1 until the default entry is identified */
};

static void limine_walk(const char *conf, const char *pkg, const char *release,
                        const char *target, int target_idx, struct lim_walk *w)
{
	memset(w, 0, sizeof *w);
	w->default_boots_pkg = -1;

	FILE *f = fopen(conf, "re");
	if (!f) return;

	char stack[LIM_MAXDEPTH][192];
	memset(stack, 0, sizeof stack);

	char cur_path[512] = "";
	char cur_esc[768] = "";
	int  cur_open = 0;      /* inside an entry at all */
	int  cur_leaf = 0;      /* has it shown a bootable directive? */
	int  cur_boots = 0;
	int  leaf_index = 0;

	char line[4096];
	/* Closing an entry is the only place a decision is made, so both the next
	 * tree line and EOF go through the same code. */
	#define LIM_CLOSE()                                                       \
		do {                                                                  \
			if (cur_open && cur_leaf) {                                       \
				if (cur_boots && !w->found) {                                 \
					snprintf(w->found_path, sizeof w->found_path, "%s",       \
					         cur_path);                                       \
					snprintf(w->found_esc, sizeof w->found_esc, "%s",         \
					         cur_esc);                                        \
					w->found = 1;                                             \
				}                                                             \
				int is_target = *target ? !strcmp(cur_path, target)           \
				                        : (leaf_index == target_idx);         \
				if (is_target && w->default_boots_pkg < 0)                    \
					w->default_boots_pkg = cur_boots;                         \
			}                                                                 \
		} while (0)

	while (fgets(line, sizeof line, f)) {
		const char *name;
		int depth = lim_tree_line(line, &name);

		if (depth > 0) {
			LIM_CLOSE();

			if (depth > LIM_MAXDEPTH) depth = LIM_MAXDEPTH;
			snprintf(stack[depth - 1], sizeof stack[0], "%s", name);
			lim_trim(stack[depth - 1]);

			/* Both forms, built together: the raw path is what a default_entry
			 * read out of the file is compared against, the escaped one is
			 * what a default_entry written into it must say. Escaping the
			 * joined path instead would escape its own separators. */
			cur_path[0] = '\0';
			cur_esc[0] = '\0';
			for (int d = 0; d < depth; d++) {
				char esc[384];
				lim_escape(stack[d], esc, sizeof esc);
				if (d) {
					strncat(cur_path, "/", sizeof cur_path - strlen(cur_path) - 1);
					strncat(cur_esc, "/", sizeof cur_esc - strlen(cur_esc) - 1);
				}
				strncat(cur_path, stack[d], sizeof cur_path - strlen(cur_path) - 1);
				strncat(cur_esc, esc, sizeof cur_esc - strlen(cur_esc) - 1);
			}
			cur_open = 1; cur_leaf = 0; cur_boots = 0;
			continue;
		}

		if (!cur_open) continue;

		const char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		/* A branch has no directives of its own; the first one an entry shows
		 * is what makes it a bootable leaf, and only leaves are counted by the
		 * 1-based index limine uses when default_entry is unset. */
		if (!cur_leaf &&
		    (!strncmp(p, "protocol:", 9) || !strncmp(p, "path:", 5) ||
		     !strncmp(p, "kernel_path:", 12))) {
			cur_leaf = 1;
			leaf_index++;
		}
		if (limine_line_boots(line, pkg, release)) cur_boots = 1;
	}
	LIM_CLOSE();
	#undef LIM_CLOSE

	fclose(f);
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
		/* Line-scoped: limine.conf mixes boot entries with a snapshot list
		 * whose comments quote pacman command lines verbatim. */
		return limine_has_entry(bl->conf, pkg, release);

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

/* ── Which kernel boots by DEFAULT ──────────────────────────────────────────
 *
 * "Bootable" and "booted by default" are different questions, and answering
 * only the first is what left this pane saying a kernel was ready while the
 * machine went on booting the other one. Every loader stores the answer
 * somewhere else, and — the trap — for two of the three the place it is READ
 * from is not the place the tool WRITES it:
 *
 *   limine        default_entry: in limine.conf. Written here, read here.
 *   systemd-boot  `bootctl set-default` writes the EFI variable
 *                 LoaderEntryDefault, which OVERRIDES loader.conf's `default`.
 *                 Reading only loader.conf would mean the pane never noticed
 *                 its own write — the exact bug fixed in pkgrel 17.
 *   grub          `grub-set-default` writes saved_entry into grubenv, which is
 *                 consulted only when /etc/default/grub says GRUB_DEFAULT=saved.
 */

/* The first `KEY=value` in a shell-ish config, unquoted. */
static int conf_value(const char *path, const char *key, char *out, size_t cap)
{
	if (cap) out[0] = '\0';
	FILE *f = fopen(path, "re");
	if (!f) return 0;

	size_t klen = strlen(key);
	char line[1024];
	int got = 0;
	while (!got && fgets(line, sizeof line, f)) {
		const char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (strncmp(p, key, klen) || p[klen] != '=') continue;
		p += klen + 1;
		if (*p == '"' || *p == '\'') p++;
		snprintf(out, cap, "%s", p);
		for (char *q = out; *q; q++)
			if (*q == '"' || *q == '\'' || *q == '\n' || *q == '\r') { *q = '\0'; break; }
		got = 1;
	}
	fclose(f);
	return got;
}

/* LoaderEntryDefault, as systemd-boot's bootctl writes it: 4 bytes of EFI
 * variable attributes, then UTF-16LE. Only the low byte of each unit is taken
 * — an entry id is a filename systemd itself keeps to ASCII. */
static int efi_loader_default(char *out, size_t cap)
{
	if (cap) out[0] = '\0';

	char path[512];
	snprintf(path, sizeof path,
	         "%s/sys/firmware/efi/efivars/"
	         "LoaderEntryDefault-4a67b082-0a4c-41cf-b6c7-440b29bb8c4f",
	         boot_root());

	FILE *f = fopen(path, "re");
	if (!f) return 0;

	unsigned char buf[512];
	size_t n = fread(buf, 1, sizeof buf, f);
	fclose(f);
	if (n < 6) return 0;

	size_t o = 0;
	for (size_t i = 4; i + 1 < n && o + 1 < cap; i += 2) {
		if (!buf[i] && !buf[i+1]) break;
		out[o++] = (char)buf[i];
	}
	out[o] = '\0';
	return o > 0;
}

/* Does the systemd-boot entry file `name` (an id, possibly a glob) boot pkg? */
static int sdb_entry_boots(const struct syn_boot *bl, const char *want,
                           const char *img, const char *release)
{
	DIR *d = opendir(bl->conf);
	if (!d) return 0;

	int hit = 0;
	struct dirent *de;
	while (!hit && (de = readdir(d))) {
		const char *dot = strrchr(de->d_name, '.');
		if (!dot || strcmp(dot, ".conf")) continue;

		/* loader.conf's `default` takes a glob and is conventionally written
		 * without the extension; the EFI variable carries the full filename. */
		char stem[256];
		snprintf(stem, sizeof stem, "%.*s", (int)(dot - de->d_name), de->d_name);
		if (fnmatch(want, de->d_name, 0) && fnmatch(want, stem, 0)) continue;

		char path[1024];
		if (snprintf(path, sizeof path, "%s/%s", bl->conf, de->d_name)
		    >= (int)sizeof path)
			continue;
		hit = file_names_kernel(path, img, release);
	}
	closedir(d);
	return hit;
}

/* grub.cfg is a shell script, and the only structure worth reading out of it
 * is the menu tree: `submenu '...' ... {` nests, `menuentry '...' ... {` is a
 * leaf, and `$menuentry_id_option 'id'` names it the way grub-set-default
 * wants — with a submenu's id joined by '>'.
 *
 * Not a shell parser. It tracks braces at the ends of header lines, which is
 * the shape grub-mkconfig writes and the only shape this needs to survive.
 */
static int grub_scan(const struct syn_boot *bl, const char *want,
                     int want_idx, const char *img, const char *release,
                     char *id_out, size_t id_cap)
{
	if (id_out && id_cap) id_out[0] = '\0';

	FILE *f = fopen(bl->conf, "re");
	if (!f) return -1;

	char sub_id[256] = "";
	char cur_id[256] = "";
	char cur_path[512] = "";
	int  in_entry = 0, in_sub = 0, index = 0, is_target = 0, answer = -1;

	char line[4096];
	while (fgets(line, sizeof line, f)) {
		const char *p = line;
		while (*p == ' ' || *p == '\t') p++;

		int is_menu = !strncmp(p, "menuentry ", 10);
		int is_submenu = !strncmp(p, "submenu ", 8);

		if (is_menu || is_submenu) {
			char id[256] = "";
			const char *opt = strstr(p, "$menuentry_id_option");
			if (opt) {
				const char *q = strchr(opt, '\'');
				if (q) {
					const char *e = strchr(++q, '\'');
					if (e && (size_t)(e - q) < sizeof id)
						snprintf(id, sizeof id, "%.*s", (int)(e - q), q);
				}
			}
			if (is_submenu) {
				snprintf(sub_id, sizeof sub_id, "%s", id);
				in_sub = 1;
				continue;
			}
			snprintf(cur_id, sizeof cur_id, "%s", id);
			if (in_sub && *sub_id)
				snprintf(cur_path, sizeof cur_path, "%s>%s", sub_id, cur_id);
			else
				snprintf(cur_path, sizeof cur_path, "%s", cur_id);

			index++;
			in_entry = 1;
			is_target = *want ? (!strcmp(cur_path, want) || !strcmp(cur_id, want))
			                  : (index == want_idx);
			continue;
		}

		if (in_entry && *p == '}') { in_entry = 0; continue; }
		if (!in_entry && *p == '}') { in_sub = 0; sub_id[0] = '\0'; continue; }

		if (!in_entry) continue;
		if (strncmp(p, "linux", 5) && strncmp(p, "linuxefi", 8)) continue;
		if (!token_match(p, img) &&
		    !(release && *release && token_match(p, release)))
			continue;

		/* Asked for the id: the FIRST entry that boots this kernel. That is
		 * grub-mkconfig's own top-level entry for it, which comes before the
		 * advanced submenu's copies — the one a person would pick. */
		if (id_out) {
			if (!*id_out && *cur_path)
				snprintf(id_out, id_cap, "%s", cur_path);
			continue;
		}
		if (is_target && answer < 0) answer = 1;
	}
	fclose(f);

	if (id_out) return *id_out != '\0';

	/* A target that was found and did not name this kernel is a NO, not an
	 * "I could not tell" — those are different answers to the pane. */
	return answer < 0 ? 0 : 1;
}

/* The systemd-boot entry id (the .conf filename) that boots this kernel —
 * what `bootctl set-default` takes. */
static int sdb_entry_id(const struct syn_boot *bl, const char *img,
                        const char *release, char *out, size_t cap)
{
	if (cap) out[0] = '\0';
	DIR *d = opendir(bl->conf);
	if (!d) return 0;

	struct dirent *de;
	while ((de = readdir(d))) {
		const char *dot = strrchr(de->d_name, '.');
		if (!dot || strcmp(dot, ".conf")) continue;

		char path[1024];
		if (snprintf(path, sizeof path, "%s/%s", bl->conf, de->d_name)
		    >= (int)sizeof path)
			continue;
		if (!file_names_kernel(path, img, release)) continue;

		snprintf(out, cap, "%s", de->d_name);
		break;
	}
	closedir(d);
	return *out != '\0';
}

/* Is `pkg` what this bootloader boots when nobody touches the menu?
 * 1 yes, 0 no, -1 cannot be told. */
int syn_boot_is_default(const struct syn_boot *bl, const char *pkg,
                        const char *release)
{
	char img[128];
	snprintf(img, sizeof img, "vmlinuz-%s", pkg);

	switch (bl->kind) {
	case SYN_BL_LIMINE: {
		char raw[512], target[512];
		limine_default_value(bl->conf, raw, sizeof raw);

		int idx = 1;
		target[0] = '\0';
		if (*raw) {
			char *end = NULL;
			long n = strtol(raw, &end, 10);
			if (end && *end == '\0' && n > 0) idx = (int)n;
			else lim_unescape(raw, target, sizeof target);
		}

		struct lim_walk w;
		limine_walk(bl->conf, pkg, release, target, idx, &w);
		return w.default_boots_pkg;
	}

	case SYN_BL_SYSTEMD: {
		char want[256];
		if (!efi_loader_default(want, sizeof want)) {
			char lconf[512];
			if (snprintf(lconf, sizeof lconf, "%s/loader/loader.conf", bl->esp)
			    >= (int)sizeof lconf)
				return -1;
			if (!conf_value(lconf, "default", want, sizeof want)) {
				/* loader.conf uses `key value`, not `key=value`. */
				FILE *f = fopen(lconf, "re");
				if (!f) return -1;
				char line[1024];
				want[0] = '\0';
				while (fgets(line, sizeof line, f)) {
					const char *p = line;
					while (*p == ' ' || *p == '\t') p++;
					if (strncmp(p, "default", 7) || (p[7] != ' ' && p[7] != '\t'))
						continue;
					p += 7;
					while (*p == ' ' || *p == '\t') p++;
					snprintf(want, sizeof want, "%s", p);
					for (char *q = want; *q; q++)
						if (*q == '\n' || *q == '\r') { *q = '\0'; break; }
					break;
				}
				fclose(f);
				if (!*want) return -1;
			}
		}
		return sdb_entry_boots(bl, want, img, release);
	}

	case SYN_BL_GRUB: {
		char defconf[512], val[256] = "", want[256] = "";
		snprintf(defconf, sizeof defconf, "%s/etc/default/grub", boot_root());
		conf_value(defconf, "GRUB_DEFAULT", val, sizeof val);

		int idx = 1;
		if (!strcmp(val, "saved")) {
			char env[512];
			snprintf(env, sizeof env, "%s/grubenv", bl->esp);
			if (!conf_value(env, "saved_entry", want, sizeof want)) {
				snprintf(env, sizeof env, "%s/grub/grubenv", bl->esp);
				conf_value(env, "saved_entry", want, sizeof want);
			}
			if (!*want) return -1;
		} else if (*val) {
			char *end = NULL;
			long n = strtol(val, &end, 10);
			/* GRUB_DEFAULT is 0-based where limine's index is 1-based. */
			if (end && *end == '\0') idx = (int)n + 1;
			else snprintf(want, sizeof want, "%s", val);
		}
		return grub_scan(bl, want, idx, img, release, NULL, 0);
	}

	default:
		return -1;
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
			/* ⚠ --noconfirm is LOAD-BEARING, exactly as in src/pkg.c, and
			 * this is where forgetting it was found: three polkit prompts on
			 * 2026-08-12, authenticated every time, nothing installed.
			 *
			 * synpkg's confirm() returns FALSE with no terminal to ask in, a
			 * declined transaction exits 0, so the button authenticated and
			 * then reported success having done nothing. The confirmation was
			 * already given — to the dialogue that showed this very command —
			 * and there is no second question worth asking into a pipe.
			 *
			 * Before the verb: synpkg stops parsing globals at the first
			 * non-option argument. */
			argv[n++] = (char *)"synpkg";
			argv[n++] = (char *)"--noconfirm";
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
	rc = run_or_show_progress(cmd);

	/* Say so on the way out, as src/pkg.c does. This returned bare, so a
	 * failure reached the GUI as an exit code and no words — and the pane it
	 * then reloads still reads "NO BOOT ENTRY", which is true but does not
	 * explain itself. */
	if (rc != 0)
		fprintf(stderr, "syn-settings: %s exited %d — the boot configuration "
		                "was NOT changed; authorisation may have been refused\n",
		        cmd[0], rc);
	return rc;
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

/* ── Making a kernel the DEFAULT ────────────────────────────────────────────
 *
 * "Bootable" was only half the job. A kernel can be installed, have an entry,
 * and still never boot — the machine goes on picking whatever the loader
 * decided, which on a limine install is entry 1 and on this box was the stock
 * kernel, for ever, no matter what the pane said about linux-cachyos.
 *
 * WHY LIMINE IS WRITTEN BY THIS BINARY AND THE OTHER TWO ARE NOT. grub and
 * systemd-boot each ship the tool for this — grub-set-default and bootctl —
 * and it is theirs to own. limine ships nothing: the default lives in
 * limine.conf as a `default_entry:` line, and there is no command anywhere
 * that sets it. So this one edit is ours.
 *
 * It is done by RE-EXECUTING THIS BINARY under pkexec (--as-root), not by
 * handing pkexec a path and a string to write. The privileged child re-detects
 * the bootloader and recomputes the entry path from scratch, so the only file
 * it can ever write is a limine.conf it found itself, and the only thing it
 * can write into it is a default_entry naming an entry that exists. A helper
 * that took "write THIS to THAT" would be a general-purpose root file writer
 * wearing a settings app's name.
 */

/* Replace or insert the global `default_entry:` line.
 *
 * Written to a sibling temp file and renamed, so an interrupted write cannot
 * leave a half-written limine.conf on the ESP — that file is the boot menu,
 * and truncating it is how a machine stops offering anything to boot.
 */
static int limine_write_default(const char *conf, const char *esc_path)
{
	FILE *in = fopen(conf, "re");
	if (!in) return boot_refuse("cannot read the limine configuration");

	char tmp[512];
	if (snprintf(tmp, sizeof tmp, "%s.syn-new", conf) >= (int)sizeof tmp) {
		fclose(in);
		return boot_refuse("that configuration path is too long to write beside");
	}

	FILE *out = fopen(tmp, "we");
	if (!out) {
		fclose(in);
		return boot_refuse("cannot write beside the limine configuration — "
		                   "this needs root, and is meant to be reached "
		                   "through pkexec");
	}

	/* FIRST LINE, always — a global may appear anywhere in limine.conf, so
	 * the position is ours to choose, and choosing it means the line lands in
	 * one predictable place instead of "wherever the old one happened to be,
	 * or the end of the file". Appending was the first version and it has two
	 * faults: a file whose last line has no newline gets the directive fused
	 * onto it, and the setting ends up 200 lines below the timeout it belongs
	 * beside, where nobody reading the file will find it.
	 *
	 * Every existing default_entry line is then dropped as the file is copied,
	 * so a config that somehow carried two ends up with exactly one. */
	int ok = fprintf(out, "default_entry: %s\n", esc_path) > 0;

	char line[4096];
	while (ok && fgets(line, sizeof line, in)) {
		const char *p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (!strncmp(p, "default_entry:", 14)) continue;
		ok = fputs(line, out) != EOF;
	}

	fclose(in);
	if (ok) ok = fflush(out) == 0 && fsync(fileno(out)) == 0;
	if (fclose(out) != 0) ok = 0;

	if (!ok) {
		unlink(tmp);
		return boot_refuse("writing the new limine configuration failed — "
		                   "nothing was changed");
	}
	if (rename(tmp, conf) != 0) {
		unlink(tmp);
		return boot_refuse("could not replace the limine configuration — "
		                   "nothing was changed");
	}
	return 0;
}

/* Put GRUB_DEFAULT=saved into /etc/default/grub.
 *
 * Returns 1 if it changed the file, 0 if it was already right, negative on
 * failure. Three shapes have to be handled and only the first is the easy one:
 *
 *   GRUB_DEFAULT=0        replaced in place, so its position and any comment
 *                         above it survive
 *   #GRUB_DEFAULT=0       a commented example is not a setting — the comment
 *                         is left exactly where it is and a real line added,
 *                         because editing somebody's commented-out example
 *                         into a live setting is a change they did not make
 *   (absent)              appended
 *
 * Everything else in the file is copied byte for byte. This is a file people
 * hand-edit — cmdline, timeouts, themes — and a settings app that reformats it
 * has taken something that was not offered.
 */
static int grub_write_default_saved(const char *path)
{
	FILE *in = fopen(path, "re");
	if (!in) return -1;

	char tmp[600];
	if (snprintf(tmp, sizeof tmp, "%s.syn-new", path) >= (int)sizeof tmp) {
		fclose(in);
		return -1;
	}

	/* Read it all first: the file is small, and holding it means the original
	 * is never open for writing — an interrupted run leaves the temp file and
	 * an untouched /etc/default/grub. */
	static char buf[64 * 1024];
	size_t len = fread(buf, 1, sizeof buf - 1, in);
	int truncated = !feof(in);
	fclose(in);
	if (truncated) return -1;
	buf[len] = '\0';

	FILE *out = fopen(tmp, "we");
	if (!out) return -1;

	int replaced = 0, ok = 1, changed = 0;
	char *save = NULL;
	/* strtok_r would eat empty lines; walk it by hand so blank lines and the
	 * absence of a trailing newline both survive. */
	char *p = buf;
	while (ok && *p) {
		char *nl = strchr(p, '\n');
		size_t n = nl ? (size_t)(nl - p) : strlen(p);

		const char *q = p;
		while (*q == ' ' || *q == '\t') q++;
		if (!replaced && !strncmp(q, "GRUB_DEFAULT=", 13)) {
			char val[256];
			snprintf(val, sizeof val, "%.*s", (int)(n - (q - p) - 13), q + 13);
			for (char *v = val; *v; v++)
				if (*v == '"' || *v == '\'') { memmove(v, v + 1, strlen(v)); v--; }
			if (strcmp(val, "saved")) changed = 1;
			ok = fputs("GRUB_DEFAULT=saved\n", out) != EOF;
			replaced = 1;
		} else {
			ok = fwrite(p, 1, n, out) == n && fputc('\n', out) != EOF;
		}

		if (!nl) break;
		p = nl + 1;
	}

	if (ok && !replaced) {
		ok = fputs("\n# Set by syn-settings: grub reads its saved default only\n"
		           "# when this is `saved`.\n"
		           "GRUB_DEFAULT=saved\n", out) != EOF;
		changed = 1;
	}

	if (ok) ok = fflush(out) == 0 && fsync(fileno(out)) == 0;
	if (fclose(out) != 0) ok = 0;
	if (!ok) { unlink(tmp); return -1; }

	if (!changed) { unlink(tmp); return 0; }

	/* Keep whatever mode it had; a fresh temp file is 0600 and this one is
	 * read by grub-mkconfig. */
	struct stat st;
	if (stat(path, &st) == 0) (void)chmod(tmp, st.st_mode & 07777);

	if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
	(void)save;
	return 1;
}

/* This binary's own path, for the pkexec re-exec.
 *
 * /proc/self/exe, not a compiled-in /usr/bin path: the test suite and every
 * development build run from the build directory, and a command that names an
 * installed binary while a different one composed it is a dialogue describing
 * something other than what would run. Falls back to the install path only if
 * the link cannot be read.
 */
static const char *self_path(void)
{
	static char buf[512];
	if (!buf[0]) {
		ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
		if (n > 0) buf[n] = '\0';
		else snprintf(buf, sizeof buf, "/usr/bin/syn-settings");
	}
	return buf;
}

/* Build the command that makes `pkg` the default under `bl`. */
static int default_command(const struct syn_boot *bl, const char *pkg,
                           const char *release, char *argv[8], char *scratch,
                           size_t scap, const char **why)
{
	int n = 0;
	char img[128];
	snprintf(img, sizeof img, "vmlinuz-%s", pkg);

	switch (bl->kind) {
	case SYN_BL_LIMINE: {
		struct lim_walk w;
		limine_walk(bl->conf, pkg, release, "", 1, &w);
		if (!w.found) {
			*why = NULL;
			return boot_refuse("that kernel has no entry in limine.conf to make "
			                   "default — make it bootable first");
		}
		*why = "limine picks by default_entry:, and an entry PATH survives the "
		       "reordering that an index does not";
		argv[n++] = (char *)"pkexec";
		argv[n++] = (char *)self_path();
		argv[n++] = (char *)"default";
		argv[n++] = (char *)pkg;
		argv[n++] = (char *)"--loader";
		argv[n++] = (char *)"limine";
		argv[n++] = (char *)"--as-root";
		snprintf(scratch, scap, "%s", w.found_esc);
		break;
	}

	case SYN_BL_SYSTEMD: {
		if (!sdb_entry_id(bl, img, release, scratch, scap)) {
			*why = NULL;
			return boot_refuse("no loader entry names that kernel — make it "
			                   "bootable first");
		}
		*why = "bootctl set-default writes LoaderEntryDefault, which is what "
		       "systemd-boot reads before loader.conf";
		argv[n++] = (char *)"pkexec";
		argv[n++] = (char *)"bootctl";
		argv[n++] = (char *)"set-default";
		argv[n++] = scratch;
		break;
	}

	case SYN_BL_GRUB: {
		/* THREE steps, in one privileged child, for the reason the ordering
		 * demands it: GRUB_DEFAULT is read by grub-mkconfig at GENERATION
		 * time, not by grub at boot. Setting saved_entry while grub.cfg still
		 * says `set default="0"` is a write that succeeds, reports success and
		 * changes nothing that boots. And the entry id has to be read from the
		 * grub.cfg that mkconfig has just written, not the one it replaced.
		 *
		 * A sequence that has to happen in order, with a value computed
		 * between two of its steps, is not something a single argv can carry —
		 * so the child does the whole thing and the dialogue lists the steps.
		 */
		if (!grub_scan(bl, "", 0, img, release, scratch, scap)) {
			*why = NULL;
			return boot_refuse("no menu entry in grub.cfg names that kernel — "
			                   "make it bootable first");
		}
		*why = "grub reads saved_entry only when GRUB_DEFAULT=saved, and only "
		       "grub-mkconfig puts that into grub.cfg";

		/* GRUB_SAVEDEFAULT means "whatever you boot becomes the default", so
		 * booting anything else once undoes this — quietly, later, long after
		 * the dialogue was dismissed. It is the user's setting and not ours to
		 * change; saying so beforehand is the whole of what this app can
		 * honestly do about it. */
		{
			char defconf[512], sd[64] = "";
			snprintf(defconf, sizeof defconf, "%s/etc/default/grub", boot_root());
			conf_value(defconf, "GRUB_SAVEDEFAULT", sd, sizeof sd);
			if (!strcmp(sd, "true") || !strcmp(sd, "yes"))
				*why = "grub reads saved_entry only when GRUB_DEFAULT=saved, and "
				       "only grub-mkconfig puts that into grub.cfg. NOTE: you "
				       "have GRUB_SAVEDEFAULT set, so booting a different entry "
				       "will replace this choice with that one";
		}
		argv[n++] = (char *)"pkexec";
		argv[n++] = (char *)self_path();
		argv[n++] = (char *)"default";
		argv[n++] = (char *)pkg;
		argv[n++] = (char *)"--loader";
		argv[n++] = (char *)"grub";
		argv[n++] = (char *)"--as-root";
		break;
	}

	default:
		*why = NULL;
		return boot_refuse("no bootloader configuration found");
	}

	argv[n] = NULL;
	return 0;
}

int do_default(int argc, char **argv)
{
	if (argc < 1)
		return boot_refuse("default needs a kernel name "
		                   "(default <kernel> [--loader <name>] [--confirm])");

	const char *pkg = argv[0];
	const char *want_loader = NULL;
	int confirmed = 0, as_root = 0;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--confirm")) { confirmed = 1; continue; }
		/* Set by the pkexec re-exec above, never by a person: it means "you
		 * are the privileged child, do the limine edit yourself". It implies
		 * --confirm because the parent already required it — asking the child
		 * to confirm a decision the user made two processes ago would be
		 * theatre, and the parent is where the boundary is. */
		if (!strcmp(argv[i], "--as-root")) { as_root = 1; confirmed = 1; continue; }
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
		return boot_refuse("no bootloader configuration found");

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

	/* The privileged half. Everything is recomputed here — the loader, the
	 * config, the entry path — so nothing the unprivileged parent said is
	 * trusted beyond the name of a kernel this app already knows. */
	if (as_root) {
		if (bl->kind == SYN_BL_LIMINE) {
			struct lim_walk w;
			limine_walk(bl->conf, pkg, release, "", 1, &w);
			if (!w.found)
				return boot_refuse("that kernel has no entry in limine.conf");
			if (g_dry_run) {
				printf("would write: default_entry: %s  ->  %s\n",
				       w.found_esc, bl->conf);
				return 0;
			}
			return limine_write_default(bl->conf, w.found_esc);
		}

		if (bl->kind == SYN_BL_GRUB) {
			char img[128];
			snprintf(img, sizeof img, "vmlinuz-%s", pkg);

			/* Checked BEFORE anything is written. Editing /etc/default/grub
			 * and then discovering there is no grub-mkconfig to act on it
			 * leaves a machine whose configured default is a promise nothing
			 * kept. */
			if (!have_cmd("grub-mkconfig") || !have_cmd("grub-set-default"))
				return boot_refuse("grub-mkconfig and grub-set-default are what "
				                   "perform this, and one of them is missing");

			char defconf[512];
			snprintf(defconf, sizeof defconf, "%s/etc/default/grub", boot_root());

			if (g_dry_run) {
				char val[256] = "";
				conf_value(defconf, "GRUB_DEFAULT", val, sizeof val);
				printf("would write: GRUB_DEFAULT=saved -> %s%s\n", defconf,
				       strcmp(val, "saved") ? "" : " (already set)");
			} else {
				int w = grub_write_default_saved(defconf);
				if (w < 0)
					return boot_refuse("could not set GRUB_DEFAULT=saved in "
					                   "/etc/default/grub — nothing was changed");
				if (w > 0)
					fprintf(stderr, "syn-settings: set GRUB_DEFAULT=saved in %s\n",
					        defconf);
			}

			/* Regenerate FIRST. GRUB_DEFAULT is read by grub-mkconfig, not by
			 * grub at boot: until this runs, grub.cfg still says
			 * `set default="0"` and saved_entry is a file nothing reads. */
			char *mk[6];
			int m = 0;
			mk[m++] = (char *)"grub-mkconfig";
			mk[m++] = (char *)"-o";
			mk[m++] = (char *)bl->conf;
			mk[m] = NULL;
			int rc = run_or_show_progress(mk);
			if (rc != 0)
				return boot_refuse("grub-mkconfig failed — the default was not "
				                   "changed");

			/* And read the id out of the file mkconfig has JUST written. Ids
			 * carry the kernel version, so a regeneration that picked up a new
			 * kernel changes them; the one computed before this ran could name
			 * an entry that no longer exists. */
			char id[512] = "";
			if (!grub_scan(bl, "", 0, img, release, id, sizeof id)) {
				if (g_dry_run) {
					printf("would run: grub-set-default <id of %s in the "
					       "regenerated grub.cfg>\n", pkg);
					return 0;
				}
				return boot_refuse("the regenerated grub.cfg has no entry for "
				                   "that kernel");
			}

			char *sd[4];
			int s = 0;
			sd[s++] = (char *)"grub-set-default";
			sd[s++] = id;
			sd[s] = NULL;
			return run_or_show_progress(sd);
		}

		return boot_refuse("--as-root is only for the loaders this app writes "
		                   "itself");
	}

	char *cmd[8];
	char scratch[768];
	const char *why = NULL;
	int rc = default_command(bl, pkg, release, cmd, scratch, sizeof scratch, &why);
	if (rc != 0) return rc;

	if (!confirmed && !g_dry_run) {
		fprintf(stderr,
		        "syn-settings: this changes which kernel boots and needs "
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
		printf("loader\t%s\n", syn_boot_name(bl->kind));
		printf("config\t%s\n", bl->conf);
		printf("why\t%s\n", why ? why : "-");
		fputs("command\t", stdout);
		for (int i = 0; cmd[i]; i++) printf("%s%s", i ? " " : "", cmd[i]);
		putchar('\n');

		/* THE STEPS, when the command is this binary re-running itself.
		 *
		 * "pkexec syn-settings default linux --as-root" is a true description
		 * of what runs and tells you nothing about what it DOES — and on grub
		 * what it does is edit /etc/default/grub, regenerate grub.cfg and set
		 * a saved entry, three privileged acts behind one opaque line. A
		 * confirmation dialogue that hides them is not a confirmation. Each is
		 * printed by the same code that will perform it, under --dry-run, so
		 * the list cannot drift from the work. */
		if (!strcmp(cmd[1], self_path())) {
			char *sub[10];
			int s = 0;
			sub[s++] = (char *)self_path();
			sub[s++] = (char *)"-n";
			for (int i = 2; cmd[i]; i++) sub[s++] = cmd[i];
			sub[s] = NULL;

			char out[4096];
			if (run_capture_quiet(sub, out, sizeof out) == 0) {
				int nstep = 0;
				for (char *l = strtok(out, "\n"); l; l = strtok(NULL, "\n")) {
					const char *t = strstr(l, "would run:");
					if (!t) t = strstr(l, "would write:");
					if (!t) continue;
					printf("step%d\t%s\n", ++nstep, t);
				}
			}
		}
		return 0;
	}

	rc = run_or_show_progress(cmd);
	if (rc != 0)
		fprintf(stderr, "syn-settings: %s exited %d — the default was NOT "
		                "changed; authorisation may have been refused\n",
		        cmd[0], rc);
	return rc;
}
