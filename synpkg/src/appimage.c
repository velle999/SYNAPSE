/*
 * synpkg — AppImages.
 *
 * ⛔ THIS IS NOT A FIFTH BACKEND, AND THE DIFFERENCE IS THE WHOLE DESIGN.
 *
 * The repositories, the AUR, Flathub and BlackArch are all SEARCHABLE and all
 * UPGRADABLE: `synpkg search` reaches every one of them and `synpkg updates`
 * accounts for every one of them. An AppImage is neither, and pretending
 * otherwise would be the dishonest version of this feature:
 *
 *   · THERE IS NO INDEX. Flathub has an API and the AUR has one; AppImages
 *     have no authoritative catalogue — AppImageHub exists and is stale. So
 *     there is nothing here to search, and `search` deliberately does not
 *     learn about AppImages rather than returning a list that is somebody
 *     else's abandoned scrape.
 *   · THERE IS NO UPDATE PATH. Most AppImages embed no zsync information, so
 *     an installed one is invisible to `updates` for ever. `synpkg appimage
 *     list` says so on every row rather than letting a user assume the
 *     upgrade covers it.
 *   · THERE IS NO SIGNATURE. pacman verifies packages and Flatpak has ostree
 *     signing. An AppImage is a file off the internet, and this says that
 *     once, out loud, rather than implying a chain of trust it does not have.
 *
 * What it DOES do is the mechanical work nobody gets right by hand, which is
 * the actual reason it exists: place the file, make it executable, pull the
 * .desktop and the icons out of the image, rewrite Exec to an absolute path,
 * name the .desktop so the dock can pin it, and record what was placed so that
 * `remove` is a real uninstall rather than an invitation to go and find the
 * files again.
 *
 * ── Why the .desktop is named after StartupWMClass ──────────────────────────
 *
 * ⚠ A DOCK PIN KEYS ON THE app_id, and the app_id of an installed AppImage is
 * whatever the application sets — which for the Electron ones is their
 * StartupWMClass. If the entry is filed under any other basename the dock
 * cannot resolve a .desktop for the running window: it draws a generic icon,
 * offers no "New Window", and a pin does not survive a restart. So the entry
 * is named for the class the window will actually have, and the image's own
 * filename is not consulted.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synpkg.h"
#include "i18n.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Where the images live, and where the record of them lives.
 *
 * ⚠ ~/Applications IS THE CONVENTION, not a choice made here: it is what
 * appimaged and AppImageLauncher watch, so an image put here is also found by
 * whatever else the user may already run. The manifest sits beside synpkg's
 * other state rather than in that directory — a stray .json among the images
 * is a thing the desktop would offer to open. */
#define AI_DIR      "Applications"
#define AI_MANIFEST ".local/state/synpkg/appimages"

static char *home_join(const char *rel)
{
	const char *home = getenv("HOME");
	if (!home || !*home)
		die(_("HOME is not set, so there is nowhere to install an AppImage"));
	char *p = NULL;
	if (asprintf(&p, "%s/%s", home, rel) < 0)
		die(_("out of memory"));
	return p;
}

/* mkdir -p, because every path here is two or three levels below $HOME on a
 * machine that may never have had any of them. */
static void mkdir_p(const char *path)
{
	char *tmp = xstrdup(path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		mkdir(tmp, 0755);
		*p = '/';
	}
	mkdir(tmp, 0755);
	free(tmp);
}

/* ── the manifest ────────────────────────────────────────────────────────────
 *
 * One line per installed image:
 *
 *     <id>\t<version>\t<image path>\t<desktop path>
 *
 * ⛔ EVERY FILE THIS PLACED IS RECORDED, because `remove` has to be a real
 * uninstall. Working out afterwards which icons in hicolor came from an
 * AppImage means guessing, and a package manager that guesses at removal time
 * deletes somebody else's file eventually. The icons are derived from the id,
 * which is why the id is the first column and is what everything keys on.
 */
static char *manifest_path(void) { return home_join(AI_MANIFEST); }

/* ⚠ THE PATH FIELDS ARE PATH-SIZED, and that is not padding. They were 512
 * against a 1400-byte line buffer, which the compiler correctly called a
 * truncation: a long enough $HOME writes a cut-off path into the manifest, and
 * `remove` then unlinks a file that does not exist while reporting success.
 * Silent truncation on the path a REMOVER acts on is the worst place for it. */
struct ai_entry {
	char id[512];
	char version[64];
	char image[4096];
	char desktop[4096];
};

/* One manifest field, or false if it does not fit exactly. See the caller. */
static bool field_copy(char *dst, size_t n, const char *src)
{
	size_t len = strlen(src);
	if (len >= n)
		return false;
	memcpy(dst, src, len + 1);
	return true;
}

static int manifest_read(struct ai_entry **out)
{
	*out = NULL;
	char *path = manifest_path();
	FILE *f = fopen(path, "re");
	free(path);
	if (!f)
		return 0;

	int n = 0, cap = 8;
	struct ai_entry *v = xmalloc(cap * sizeof *v);
	char line[9000];
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (!line[0])
			continue;
		if (n == cap) {
			cap *= 2;
			v = xrealloc(v, cap * sizeof *v);
		}
		struct ai_entry *e = &v[n];
		memset(e, 0, sizeof *e);
		/* Tab-separated and read by field, so a path with a space in it
		 * survives — which is most of ~/Applications on a machine whose
		 * user has a space in their name.
		 *
		 * ⛔ A FIELD TOO LONG FOR ITS SLOT DROPS THE WHOLE LINE rather than
		 * being cut down to fit. snprintf() here would truncate silently,
		 * and a truncated path is one `remove` unlinks nothing at while
		 * reporting success — the worst place in this file for a quiet
		 * wrong answer. A manifest line that cannot be read exactly is a
		 * manifest line this refuses to act on. */
		char *p = line, *tab;
		int col = 0;
		bool sane = true;
		while (p && col < 4) {
			tab = strchr(p, '\t');
			if (tab) *tab = '\0';
			switch (col) {
			case 0: sane = field_copy(e->id, sizeof e->id, p); break;
			case 1: sane = field_copy(e->version, sizeof e->version, p); break;
			case 2: sane = field_copy(e->image, sizeof e->image, p); break;
			case 3: sane = field_copy(e->desktop, sizeof e->desktop, p); break;
			}
			if (!sane)
				break;
			p = tab ? tab + 1 : NULL;
			col++;
		}
		if (!sane) {
			warn(_("ignoring an unreadable line in the AppImage manifest "
			       "(a path longer than this can hold)"));
			continue;
		}
		if (e->id[0])
			n++;
	}
	fclose(f);
	*out = v;
	return n;
}

static void manifest_write(struct ai_entry *v, int n)
{
	char *path = manifest_path();
	char *dir = xstrdup(path);
	char *slash = strrchr(dir, '/');
	if (slash) { *slash = '\0'; mkdir_p(dir); }
	free(dir);

	FILE *f = fopen(path, "we");
	if (!f)
		die(_("cannot write %s: %s"), path, strerror(errno));
	for (int i = 0; i < n; i++)
		fprintf(f, "%s\t%s\t%s\t%s\n", v[i].id, v[i].version,
			v[i].image, v[i].desktop);
	fclose(f);
	free(path);
}

/* ── reading the image ───────────────────────────────────────────────────────
 *
 * ⚠ `--appimage-extract` RUNS THE IMAGE'S RUNTIME, not the application inside
 * it. That is the documented way to get at the contents and it is what every
 * other AppImage tool does; the alternative is finding the squashfs offset by
 * hand and hoping the runtime never changes. It still means executing a file
 * the user downloaded, which is why `install` says whose file it is before it
 * does anything — see the note in cmd_appimage.
 *
 * Extraction lands in ./squashfs-root RELATIVE TO THE WORKING DIRECTORY, with
 * no way to redirect it, so this changes into a scratch directory first and
 * changes back. Without that, running `synpkg appimage install` from $HOME
 * leaves a squashfs-root full of an unpacked application in the user's home.
 */
static bool extract_image(const char *image, const char *scratch)
{
	char cwd[4096];
	if (!getcwd(cwd, sizeof cwd))
		return false;
	if (chdir(scratch) != 0)
		return false;

	char *const argv[] = { (char *)image, (char *)"--appimage-extract", NULL };
	int rc = run(argv, true);

	if (chdir(cwd) != 0)
		die(_("could not return to %s"), cwd);
	return rc == 0;
}

/* The first .desktop at the root of an extracted image.
 *
 * ⚠ MATCHED ON THE SUFFIX AND NOT ON A GLOB. An Electron image's main binary
 * is commonly named `ai.opencode.desktop` — no extension — and it sits beside
 * `ai.opencode.desktop.desktop`, which is the entry. A `*.desktop` glob picks
 * up the ELF, and the installer then writes a 150MB binary into
 * ~/.local/share/applications. Measured on the image that prompted this. */
static bool find_desktop(const char *root, char *out, size_t n)
{
	DIR *d = opendir(root);
	if (!d)
		return false;
	bool found = false;
	struct dirent *e;
	while (!found && (e = readdir(d))) {
		size_t len = strlen(e->d_name);
		if (len <= 8 || strcmp(e->d_name + len - 8, ".desktop") != 0)
			continue;
		snprintf(out, n, "%s/%s", root, e->d_name);
		struct stat st;
		/* A regular file, so the ELF above cannot win even if it were
		 * somehow named with the suffix. */
		if (stat(out, &st) == 0 && S_ISREG(st.st_mode) && st.st_size < 64 * 1024)
			found = true;
	}
	closedir(d);
	return found;
}

/* One `Key=` out of a .desktop, unquoted, into buf. */
static bool desktop_key(const char *path, const char *key, char *buf, size_t n)
{
	buf[0] = '\0';
	FILE *f = fopen(path, "re");
	if (!f)
		return false;
	size_t klen = strlen(key);
	char line[1024];
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (strncmp(line, key, klen) != 0 || line[klen] != '=')
			continue;
		snprintf(buf, n, "%s", line + klen + 1);
		break;
	}
	fclose(f);
	return buf[0] != '\0';
}

/* Copy one file. */
static bool copy_file(const char *from, const char *to, mode_t mode)
{
	FILE *in = fopen(from, "re");
	if (!in)
		return false;
	char *dir = xstrdup(to);
	char *slash = strrchr(dir, '/');
	if (slash) { *slash = '\0'; mkdir_p(dir); }
	free(dir);

	FILE *out = fopen(to, "we");
	if (!out) { fclose(in); return false; }

	char buf[65536];
	size_t got;
	bool ok = true;
	while ((got = fread(buf, 1, sizeof buf, in)) > 0)
		if (fwrite(buf, 1, got, out) != got) { ok = false; break; }
	fclose(in);

	/* ⚠ fchmod ON THE DESCRIPTOR, not chmod on the name. The file is already
	 * open here, so naming it a second time is a path resolved twice — which
	 * is the check-then-use tools/check-toctou.sh refuses, and it matters more
	 * than usual for this one: the mode being set is 0755, and the name being
	 * re-resolved is under a directory the caller chose. */
	if (ok && fchmod(fileno(out), mode) != 0)
		ok = false;
	if (fclose(out) != 0)
		ok = false;
	return ok;
}

/* The unpacked tree, gone. Named rather than inlined because every exit from
 * ai_install() has to do it and the first version only did it on success. */
static void ai_scratch_free(const char *scratch)
{
	char *rm[] = { (char *)"rm", (char *)"-rf", (char *)scratch, NULL };
	run(rm, true);
}

/* ── install ─────────────────────────────────────────────────────────────── */

static int ai_install(const char *src)
{
	if (access(src, R_OK) != 0)
		die(_("cannot read %s: %s"), src, strerror(errno));

	/* ⚠ THE FILE IS EXECUTED TO BE READ, so say so before doing it. There is
	 * no signature on an AppImage and no repository behind it: the whole
	 * trust decision is "the user chose this file", and an installer that
	 * never mentions that it is about to run it is hiding the only part a
	 * person can act on. */
	if (g_out == OUT_HUMAN)
		printf(_("Reading %s\n  (an AppImage is unpacked by running it; it is not "
			 "signed and no repository stands behind it)\n"), src);

	char scratch[] = "/tmp/synpkg-appimage-XXXXXX";
	if (!mkdtemp(scratch))
		die(_("cannot create a scratch directory: %s"), strerror(errno));

	/* An absolute path: the extractor runs with the scratch directory as its
	 * working directory, so a relative one would no longer resolve. */
	char abs[4096];
	if (!realpath(src, abs))
		die(_("cannot resolve %s: %s"), src, strerror(errno));

	/*
	 * ⛔ THE COPY IS MADE EXECUTABLE, NEVER THE CALLER'S FILE.
	 *
	 * An AppImage has to be executable to be unpacked — its own runtime does
	 * the unpacking. The first version chmod'd the argument, which is a
	 * package manager modifying a file it was only asked to read: harmless on
	 * a fresh download in ~/Downloads, and quite different when the argument
	 * is wrong. `synpkg appimage install /etc/hostname` tried to make a system
	 * file executable, and the only reason it did not is that the caller does
	 * not own it.
	 *
	 * ⚠ It also outlived the refusal. A file rejected for not being an
	 * AppImage should be left exactly as it was found, and the +x bit was not.
	 */
	char work[4200];
	snprintf(work, sizeof work, "%s/image.AppImage", scratch);
	if (!copy_file(abs, work, 0755)) {
		ai_scratch_free(scratch);
		die(_("cannot stage %s: %s"), src, strerror(errno));
	}

	/* ⛔ EVERY EXIT FROM HERE DOWN CLEANS THE SCRATCH DIRECTORY. The first
	 * version cleaned it only on success, so the two paths that refuse — a
	 * file that is not an AppImage, and one carrying no .desktop — each left
	 * an unpacked copy of whatever it was in /tmp. For a 152MB Electron image
	 * that is 400MB of extracted tree per failed attempt, and the attempts
	 * that fail are exactly the ones a person repeats.
	 *
	 * ⚠ die() STILL LEAVES ONE. It is noreturn by design and unwinding it
	 * here would mean an error path with two ways out; the cases that reach
	 * it are out-of-memory and an unwritable $HOME, where a directory in
	 * /tmp is not the problem worth solving. */
	if (!extract_image(work, scratch)) {
		fprintf(stderr, _("synpkg: %s is not an AppImage, or its runtime "
				  "refused to unpack it\n"), src);
		ai_scratch_free(scratch);
		return 1;
	}

	char root[4200], desktop[4400];
	snprintf(root, sizeof root, "%s/squashfs-root", scratch);
	if (!find_desktop(root, desktop, sizeof desktop)) {
		fprintf(stderr, _("synpkg: %s carries no .desktop entry, so there is "
				  "nothing to put in a menu\n"), src);
		ai_scratch_free(scratch);
		return 1;
	}

	char name[256], icon[256], wmclass[256], exec[1024], version[64];
	char categories[256], mimetype[512], comment[512];
	desktop_key(desktop, "Name", name, sizeof name);
	desktop_key(desktop, "Icon", icon, sizeof icon);
	desktop_key(desktop, "StartupWMClass", wmclass, sizeof wmclass);
	desktop_key(desktop, "Exec", exec, sizeof exec);
	desktop_key(desktop, "Categories", categories, sizeof categories);
	desktop_key(desktop, "MimeType", mimetype, sizeof mimetype);
	desktop_key(desktop, "Comment", comment, sizeof comment);
	if (!desktop_key(desktop, "X-AppImage-Version", version, sizeof version))
		snprintf(version, sizeof version, "%s", "-");
	if (!name[0])
		die(_("%s has a .desktop with no Name"), src);

	/* ⛔ THE ID IS StartupWMClass WHERE THERE IS ONE — see the header. It is
	 * what the window will call itself, so it is what the dock will look for,
	 * and filing the entry under anything else costs the icon and the pin. */
	char id[512];
	snprintf(id, sizeof id, "%s", wmclass[0] ? wmclass : (icon[0] ? icon : name));
	for (char *p = id; *p; p++)
		if (*p == '/' || *p == ' ')
			*p = '-';

	char *appdir = home_join(AI_DIR);
	mkdir_p(appdir);
	char image[1024];
	snprintf(image, sizeof image, "%s/%s.AppImage", appdir, id);
	free(appdir);

	if (!copy_file(abs, image, 0755))
		die(_("cannot write %s: %s"), image, strerror(errno));

	/* The icons, at whatever sizes the image happens to carry. */
	int icons = 0;
	if (icon[0]) {
		static const char *const sizes[] = {
			"16x16", "22x22", "24x24", "32x32", "48x48", "64x64",
			"128x128", "256x256", "512x512", NULL
		};
		for (int i = 0; sizes[i]; i++) {
			char from[4800], to[1024];
			snprintf(from, sizeof from,
				 "%s/usr/share/icons/hicolor/%s/apps/%s.png",
				 root, sizes[i], icon);
			if (access(from, R_OK) != 0)
				continue;
			char *idir = home_join(".local/share/icons/hicolor");
			snprintf(to, sizeof to, "%s/%s/apps/%s.png", idir, sizes[i], id);
			free(idir);
			if (copy_file(from, to, 0644))
				icons++;
		}
		/* The one at the root, for an image that ships no hicolor tree.
		 * Filed at 256x256 because there is nothing else to go on and a
		 * wrong-but-present size beats no icon at all. */
		if (icons == 0) {
			char from[4800], to[1024];
			snprintf(from, sizeof from, "%s/%s.png", root, icon);
			if (access(from, R_OK) == 0) {
				char *idir = home_join(".local/share/icons/hicolor");
				snprintf(to, sizeof to, "%s/256x256/apps/%s.png", idir, id);
				free(idir);
				if (copy_file(from, to, 0644))
					icons++;
			}
		}
	}

	/* ── the entry ───────────────────────────────────────────────────────
	 *
	 * ⛔ Exec IS REWRITTEN, NEVER COPIED. Inside the image it reads
	 * `AppRun …`, which resolves to nothing at all outside it — an entry
	 * copied verbatim is a menu row that silently does nothing. The flags
	 * after AppRun are kept: `--no-sandbox` is there because an AppImage
	 * cannot ship a setuid chrome-sandbox, and an Electron app started
	 * without it does not start.
	 */
	const char *flags = "";
	if (strncmp(exec, "AppRun", 6) == 0) {
		flags = exec + 6;
		while (*flags == ' ')
			flags++;
	}

	char *appsdir = home_join(".local/share/applications");
	mkdir_p(appsdir);
	char dpath[1024];
	snprintf(dpath, sizeof dpath, "%s/%s.desktop", appsdir, id);
	free(appsdir);

	FILE *out = fopen(dpath, "we");
	if (!out)
		die(_("cannot write %s: %s"), dpath, strerror(errno));
	fprintf(out, "[Desktop Entry]\nType=Application\nName=%s\n", name);
	if (comment[0])
		fprintf(out, "Comment=%s\n", comment);
	fprintf(out, "Exec=%s%s%s\n", image, *flags ? " " : "", flags);
	fprintf(out, "Terminal=false\n");
	if (icons)
		fprintf(out, "Icon=%s\n", id);
	if (wmclass[0])
		fprintf(out, "StartupWMClass=%s\n", wmclass);
	if (categories[0])
		fprintf(out, "Categories=%s\n", categories);
	if (mimetype[0])
		fprintf(out, "MimeType=%s\n", mimetype);
	/* ⚠ MARKED AS OURS. `synpkg appimage list` reads the manifest, but a
	 * person reading ~/.local/share/applications by hand deserves to know
	 * which entries a tool wrote and which they wrote themselves. */
	fprintf(out, "X-AppImage-Version=%s\nX-SynpkgAppImage=true\n", version);
	fclose(out);

	/* Record it before the caches, so a failure in a cache refresh does not
	 * leave an installed image with no way to remove it. */
	struct ai_entry *v = NULL;
	int n = manifest_read(&v);
	int at = -1;
	for (int i = 0; i < n; i++)
		if (!strcmp(v[i].id, id))
			at = i;
	if (at < 0) {
		v = xrealloc(v, (n + 1) * sizeof *v);
		at = n++;
		memset(&v[at], 0, sizeof v[at]);
	}
	snprintf(v[at].id, sizeof v[at].id, "%s", id);
	snprintf(v[at].version, sizeof v[at].version, "%s", version);
	snprintf(v[at].image, sizeof v[at].image, "%s", image);
	snprintf(v[at].desktop, sizeof v[at].desktop, "%s", dpath);
	manifest_write(v, n);
	free(v);

	ai_scratch_free(scratch);

	char *ud[] = { (char *)"update-desktop-database",
		       home_join(".local/share/applications"), NULL };
	run(ud, true);
	free(ud[1]);

	if (g_out == OUT_TSV) {
		tsv_row(4, id, version, image, dpath);
	} else {
		printf(_("%s %s installed\n"), name, version);
		printf(_("  %s\n  %s\n"), image, dpath);
		if (!icons)
			printf(_("  (no icon in the image — the menu entry will use a "
				 "generic one)\n"));
		/* ⛔ SAID AT INSTALL TIME, not left to be discovered. */
		printf(_("  ⚠ synpkg cannot update this. AppImages carry no update\n"
			 "    information, so `synpkg updates` will never mention it —\n"
			 "    replace it by installing a newer file over the top.\n"));
	}
	return 0;
}

/* ── list ────────────────────────────────────────────────────────────────── */

static int ai_list(void)
{
	struct ai_entry *v = NULL;
	int n = manifest_read(&v);

	if (g_out == OUT_TSV) {
		tsv_row(5, "id", "version", "image", "desktop", "present");
		for (int i = 0; i < n; i++)
			tsv_row(5, v[i].id, v[i].version, v[i].image, v[i].desktop,
				access(v[i].image, F_OK) == 0 ? "yes" : "no");
		free(v);
		return 0;
	}

	if (n == 0) {
		printf(_("No AppImages installed by synpkg.\n"
			 "  synpkg appimage install <file>\n"));
		free(v);
		return 0;
	}
	for (int i = 0; i < n; i++) {
		/* ⚠ A MISSING FILE IS SAID, not hidden. Somebody who deleted the
		 * image by hand has a menu entry that does nothing, and this is
		 * the only place that can tell them why. */
		bool here = access(v[i].image, F_OK) == 0;
		printf("  %-28s %-12s %s%s\n", v[i].id, v[i].version, v[i].image,
		       here ? "" : _("   (file is gone)"));
	}
	printf(_("\n  ⚠ None of these can be updated by synpkg — an AppImage carries\n"
		 "    no update information. `synpkg updates` does not cover them.\n"));
	free(v);
	return 0;
}

/* ── remove ──────────────────────────────────────────────────────────────── */

static int ai_remove(const char *id)
{
	struct ai_entry *v = NULL;
	int n = manifest_read(&v);
	int at = -1;
	for (int i = 0; i < n; i++)
		if (!strcmp(v[i].id, id))
			at = i;

	if (at < 0) {
		fprintf(stderr, _("synpkg: no AppImage called '%s' "
				  "(synpkg appimage list)\n"), id);
		free(v);
		return 1;
	}

	/* ⛔ ONLY WHAT THE MANIFEST RECORDS. The icons are derived from the id
	 * because that is how they were written; nothing here walks hicolor
	 * looking for things that might have come from an AppImage, because a
	 * remover that guesses deletes somebody else's file eventually. */
	unlink(v[at].image);
	unlink(v[at].desktop);
	static const char *const sizes[] = {
		"16x16", "22x22", "24x24", "32x32", "48x48", "64x64",
		"128x128", "256x256", "512x512", NULL
	};
	char *idir = home_join(".local/share/icons/hicolor");
	for (int i = 0; sizes[i]; i++) {
		char p[1024];
		snprintf(p, sizeof p, "%s/%s/apps/%s.png", idir, sizes[i], id);
		unlink(p);
	}
	free(idir);

	memmove(&v[at], &v[at + 1], (size_t)(n - at - 1) * sizeof *v);
	manifest_write(v, n - 1);
	free(v);

	char *ud[] = { (char *)"update-desktop-database",
		       home_join(".local/share/applications"), NULL };
	run(ud, true);
	free(ud[1]);

	if (g_out == OUT_HUMAN)
		printf(_("%s removed\n"), id);
	return 0;
}

int cmd_appimage(int argc, char **argv)
{
	const char *sub = argc > 0 ? argv[0] : "list";

	if (!strcmp(sub, "list"))
		return ai_list();
	if (!strcmp(sub, "install")) {
		if (argc < 2)
			die(_("synpkg appimage install needs a file"));
		return ai_install(argv[1]);
	}
	if (!strcmp(sub, "remove")) {
		if (argc < 2)
			die(_("synpkg appimage remove needs a name "
			      "(synpkg appimage list)"));
		return ai_remove(argv[1]);
	}

	fprintf(stderr, _("synpkg: appimage takes install, list or remove "
			  "(not '%s')\n"), sub);
	return 2;
}
