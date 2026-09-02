/* volumes.c — disks, removable media, and network places.
 *
 * Three sources, because they are genuinely three different things and a
 * single "list the drives" call does not exist:
 *
 *   - block devices come from `lsblk -P`, whose KEY="value" output is stable,
 *     shell-quoted and designed to be parsed. The JSON form would need a JSON
 *     parser in C to read four fields.
 *   - network shares come from gvfs, whose FUSE mounts appear as directories
 *     under /run/user/<uid>/gvfs. Once mounted, a share IS a path, which is
 *     what lets the rest of this program treat it like any other directory.
 *   - mounting is NOT reimplemented. udisks2 and gvfs already own it, they
 *     already have the polkit rules, and a file manager that opened block
 *     devices itself would need to run privileged.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synfiles.h"
#include "i18n.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

/* ── the mount table ────────────────────────────────────────────────────────
 *
 * lsblk's MOUNTPOINT is not the whole truth on a systemd machine. velle's
 * /mnt/drive8tb and /mnt/drive3tb are AUTOMOUNTS: /proc/self/mounts carries an
 * `autofs` entry for the path, and the real `/dev/sda1 ... ntfs3` entry only
 * appears once something touches it. lsblk reports MOUNTPOINT="" for both,
 * so a sidebar built on lsblk alone labels two working 8TB and 3TB drives
 * "not mounted" and offers to mount them — which is wrong twice over, because
 * they are already reachable and udisks would refuse.
 *
 * So the mount table is consulted as well, and an autofs path is treated as a
 * browsable location in its own right.
 */
typedef struct {
	char *device;
	char *point;
	char *fstype;
} mount_t;

static mount_t *g_mounts;
static size_t   g_nmounts;
static char    *g_mounts_backing;

/* /proc/self/mounts escapes spaces and tabs in paths as octal, because the
 * file is space-separated. Undoing that matters for a mount point like
 * "/mnt/My Disk", which is otherwise truncated at the space. */
static void unescape_mount(char *s)
{
	char *w = s;
	for (const char *r = s; *r; ) {
		if (r[0] == '\\' && r[1] >= '0' && r[1] <= '7'
		    && r[2] >= '0' && r[2] <= '7' && r[3] >= '0' && r[3] <= '7') {
			*w++ = (char)((r[1] - '0') * 64 + (r[2] - '0') * 8 + (r[3] - '0'));
			r += 4;
		} else {
			*w++ = *r++;
		}
	}
	*w = '\0';
}

static void load_mounts(void)
{
	if (g_mounts_backing)
		return;

	g_mounts_backing = slurp("/proc/self/mounts");
	if (!g_mounts_backing) {
		g_mounts_backing = xstrdup("");
		return;
	}

	size_t nlines = 0;
	char **lines = split(g_mounts_backing, '\n', &nlines);
	g_mounts = xmalloc((nlines ? nlines : 1) * sizeof *g_mounts);

	for (size_t i = 0; i < nlines; i++) {
		if (!*lines[i])
			continue;
		size_t nf = 0;
		char **f = split(lines[i], ' ', &nf);
		if (nf >= 3) {
			unescape_mount(f[0]);
			unescape_mount(f[1]);
			g_mounts[g_nmounts++] = (mount_t){ f[0], f[1], f[2] };
		}
		free(f);
	}
	free(lines);
}

/* Where this device is mounted, ignoring autofs rows — their "device" column
 * is the literal string "systemd-1" and never a device path. */
static const char *mount_point_of(const char *device)
{
	if (!device || !*device)
		return NULL;
	load_mounts();
	for (size_t i = 0; i < g_nmounts; i++)
		if (!strcmp(g_mounts[i].device, device))
			return g_mounts[i].point;
	return NULL;
}

/* Is a real filesystem mounted here right now? An autofs entry alone means
 * "browsable, will mount on access" — and asking statvfs about it would
 * TRIGGER that mount, spinning up a sleeping disk just to draw a meter. */
static bool really_mounted(const char *point)
{
	if (!point || !*point)
		return false;
	load_mounts();
	for (size_t i = 0; i < g_nmounts; i++)
		if (!strcmp(g_mounts[i].point, point) && strcmp(g_mounts[i].fstype, "autofs"))
			return true;
	return false;
}

/* ── systemd mount units ────────────────────────────────────────────────────
 *
 * The last piece of the automount puzzle. /mnt/drive3tb is an autofs target
 * whose backing disk is not mounted yet, so nothing in the mount table or in
 * lsblk connects the two — and without the connection the sidebar shows the
 * same 3TB disk twice: once as "sdb1, not mounted, click to mount" and once as
 * "drive3tb". Worse, taking the first offer would mount it a SECOND time under
 * /run/media while the automount still owns /mnt/drive3tb.
 *
 * The unit file says it outright:
 *     What=/dev/disk/by-uuid/921017131016FE43
 *     Where=/mnt/drive3tb
 * so these are read as data. No systemctl, no D-Bus — if the directories are
 * absent, every disk simply keeps its own row, which is the old behaviour.
 *
 * Not fstab: velle's automounts are hand-written units and appear in no fstab
 * at all, which is exactly why fstab was checked first and abandoned.
 */
typedef struct {
	/* A device path, or a symlink under /dev/disk/by-uuid and friends.
	 * (Writing that glob inline closes this comment — the "*" and "/" of
	 * "by-*<slash>" are a terminator.) */
	char *what;
	char *where;
} unit_t;

static unit_t *g_units;
static size_t  g_nunits;

static void load_unit_dir(const char *dir)
{
	DIR *d = opendir(dir);
	if (!d)
		return;

	struct dirent *e;
	while ((e = readdir(d))) {
		const char *dot = strrchr(e->d_name, '.');
		if (!dot || strcmp(dot, ".mount"))
			continue;

		char *path = xasprintf("%s/%s", dir, e->d_name);
		char *text = slurp(path);
		free(path);
		if (!text)
			continue;

		char *what = NULL, *where = NULL;
		size_t nlines = 0;
		char **lines = split(text, '\n', &nlines);
		for (size_t i = 0; i < nlines; i++) {
			if (!strncmp(lines[i], "What=", 5) && !what)
				what = xstrdup(lines[i] + 5);
			else if (!strncmp(lines[i], "Where=", 6) && !where)
				where = xstrdup(lines[i] + 6);
		}
		free(lines);
		free(text);

		if (what && where) {
			g_units = xrealloc(g_units, (g_nunits + 1) * sizeof *g_units);
			g_units[g_nunits++] = (unit_t){ what, where };
		} else {
			free(what);
			free(where);
		}
	}
	closedir(d);
}

static void load_units(void)
{
	static bool done;
	if (done)
		return;
	done = true;

	const char *env = getenv("SYNFILES_UNIT_DIRS");
	if (env && *env) {
		char *copy = xstrdup(env);
		size_t n = 0;
		char **dirs = split(copy, ':', &n);
		for (size_t i = 0; i < n; i++)
			if (*dirs[i])
				load_unit_dir(dirs[i]);
		free(dirs);
		free(copy);
		return;
	}

	load_unit_dir("/etc/systemd/system");
	load_unit_dir("/run/systemd/generator");
}

/* Where a unit says this device belongs. `uuid` may be NULL. Both forms are
 * checked because a unit may name the device directly or through by-uuid. */
static const char *unit_where_for(const char *device, const char *uuid)
{
	load_units();
	for (size_t i = 0; i < g_nunits; i++) {
		const char *w = g_units[i].what;
		if (device && *device && !strcmp(w, device))
			return g_units[i].where;
		if (uuid && *uuid) {
			const char *slash = strrchr(w, '/');
			if (slash && !strcmp(slash + 1, uuid)
			    && strstr(w, "/dev/disk/by-uuid/") == w)
				return g_units[i].where;
		}
	}
	return NULL;
}

/* Is this path an autofs trigger right now? */
static bool is_autofs_target(const char *point)
{
	if (!point || !*point)
		return false;
	load_mounts();
	for (size_t i = 0; i < g_nmounts; i++)
		if (!strcmp(g_mounts[i].point, point) && !strcmp(g_mounts[i].fstype, "autofs"))
			return true;
	return false;
}

/* How full a mounted filesystem is, for the meter in the sidebar.
 *
 * statvfs rather than parsing `df`: it is the call df itself makes, and it
 * needs no subprocess per volume. f_bavail, not f_bfree — the difference is
 * the reserved blocks only root can use, and counting those as free is why a
 * disk can read "8% free" to a user who cannot write a single byte to it.
 *
 * Both are 0 when the volume is not mounted or cannot be queried, and callers
 * must treat that as "unknown" rather than "empty". */
static void usage_of(const char *mountpoint, unsigned long long *used,
                     unsigned long long *total)
{
	*used = 0;
	*total = 0;
	if (!mountpoint || !*mountpoint)
		return;

	struct statvfs vfs;
	if (statvfs(mountpoint, &vfs) != 0)
		return;

	unsigned long long unit = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
	*total = (unsigned long long)vfs.f_blocks * unit;
	*used = (unsigned long long)(vfs.f_blocks - vfs.f_bavail) * unit;
}

/* Value of KEY="..." in one lsblk -P line.
 *
 * The match must start at the line start or after a space AND be followed by
 * ="; without both, asking for NAME would match the NAME inside PKNAME, and
 * asking for PATH would match the one inside MOUNTPATH on an lsblk that has
 * it. Anchoring on the delimiters is cheaper than maintaining a list of keys
 * that happen to be substrings of other keys. */
static char *kv_val(const char *line, const char *key)
{
	size_t klen = strlen(key);
	for (const char *p = line; (p = strstr(p, key)); p += klen) {
		if (p != line && p[-1] != ' ')
			continue;
		if (p[klen] != '=' || p[klen + 1] != '"')
			continue;
		const char *v = p + klen + 2;
		const char *q = strchr(v, '"');
		return q ? xstrndup(v, (size_t)(q - v)) : NULL;
	}
	return NULL;
}

/* Pseudo and system mounts are not places anybody browses to. Hiding them is
 * not cosmetic: a sidebar with forty tmpfs entries in it is a sidebar nobody
 * reads, and the real drive is somewhere in the middle of them. */
static bool boring_mount(const char *mp)
{
	if (!mp || !*mp)
		return false;   /* unmounted is not boring — it is offerable */

	/* Checked BEFORE the skip list, because udisks2 mounts removable media at
	 * /run/media/<user>/<label> — which /run swallowed whole. The effect was
	 * not a missing row but a DISAPPEARING one: click a USB stick, synfiles
	 * mounts it, the volume list is re-read, and the drive is gone from the
	 * sidebar. Mounting a disk looked exactly like ejecting it. */
	static const char *keep[] = { "/run/media/", "/media/", "/mnt/" };
	for (size_t i = 0; i < sizeof keep / sizeof *keep; i++)
		if (!strncmp(mp, keep[i], strlen(keep[i])))
			return false;

	static const char *skip[] = {
		"/proc", "/sys", "/dev", "/run", "/tmp", "/boot", "/efi", "/var",
	};
	for (size_t i = 0; i < sizeof skip / sizeof *skip; i++) {
		size_t n = strlen(skip[i]);
		if (!strncmp(mp, skip[i], n) && (mp[n] == '\0' || mp[n] == '/'))
			return true;
	}
	return false;
}

/* Mount points already emitted by list_block, as "\npath\npath\n". */
static char *g_emitted;

static void mark_emitted(const char *mp)
{
	if (!mp || !*mp)
		return;
	char *grown = xasprintf("%s%s\n", g_emitted ? g_emitted : "\n", mp);
	free(g_emitted);
	g_emitted = grown;
}

static bool already_emitted(const char *mp)
{
	if (!g_emitted || !mp || !*mp)
		return false;
	char *wrapped = xasprintf("\n%s\n", mp);
	bool hit = strstr(g_emitted, wrapped) != NULL;
	free(wrapped);
	return hit;
}

static int list_block(void)
{
	if (!have_cmd("lsblk"))
		return 0;

	char *argv[] = { (char *)"lsblk", (char *)"-P", (char *)"-o",
	                 (char *)"NAME,PATH,LABEL,SIZE,FSTYPE,MOUNTPOINT,RM,TYPE,HOTPLUG,UUID,PKNAME",
	                 NULL };
	int st = 0;
	char *out = run_capture(argv, &st, true);
	if (st != 0) {
		free(out);
		return 0;
	}

	int n = 0;
	size_t nlines = 0;
	char **lines = split(out, '\n', &nlines);

	/* Which disks have something else sitting on them.
	 *
	 * A whole disk can carry a filesystem signature of its OWN and still be a
	 * container. A hybrid ISO written to a stick is the case that matters
	 * here: /dev/sdd reports FSTYPE="iso9660" while the filesystems anyone
	 * actually mounts are sdd1 and sdd2. Emitting the parent as well would put
	 * a third, permanently unmounted "SYNAPSEOS_202608" in the sidebar for one
	 * stick — the same duplicate the automount handling above exists to avoid.
	 *
	 * So the question is not "does this disk have a filesystem" but "does
	 * anything claim it as a parent", which PKNAME answers directly. */
	char *parents = xstrdup("\n");
	for (size_t i = 0; i < nlines; i++) {
		if (!*lines[i])
			continue;
		char *pk = kv_val(lines[i], "PKNAME");
		if (pk && *pk) {
			char *grown = xasprintf("%s%s\n", parents, pk);
			free(parents);
			parents = grown;
		}
		free(pk);
	}

	for (size_t i = 0; i < nlines; i++) {
		if (!*lines[i])
			continue;

		char *type = kv_val(lines[i], "TYPE");
		char *fstype = kv_val(lines[i], "FSTYPE");
		char *mp = kv_val(lines[i], "MOUNTPOINT");
		char *name = kv_val(lines[i], "NAME");

		/* A whole disk that nothing partitions, but which does have a
		 * filesystem, IS somewhere to go: `mkfs.vfat /dev/sdX` with no
		 * partition table is how most cameras and plenty of USB sticks come
		 * formatted, and udisks2 mounts them at /run/media like any other
		 * volume. Excluding TYPE="disk" outright hid every one of them from
		 * the sidebar no matter what was mounted. */
		bool container = false;
		if (type && !strcmp(type, "disk") && name && *name) {
			char *w = xasprintf("\n%s\n", name);
			container = strstr(parents, w) != NULL;
			free(w);
		}

		/* Swap is a filesystem by FSTYPE and a place by nothing else: there
		 * is no directory to open, and lsblk gives its MOUNTPOINT as the
		 * literal "[SWAP]", which is not a path. zram0 is the one that shows
		 * up here — TYPE="disk", nothing partitions it — but a swap PARTITION
		 * would have satisfied the old test too, so this is checked on fstype
		 * rather than on the shape of the device carrying it. */
		bool swap = fstype && !strcmp(fstype, "swap");

		/* A whole disk with no filesystem is a container for the partitions
		 * that follow it, not somewhere to go. */
		bool usable = type && fstype && *fstype && !swap
		              && (!strcmp(type, "part") || !strcmp(type, "rom")
		                  || !strcmp(type, "crypt") || !strcmp(type, "lvm")
		                  || (!strcmp(type, "disk") && !container));

		char *path = kv_val(lines[i], "PATH");

		/* lsblk said nothing; the mount table may still know. This is what
		 * stops an automounted drive being labelled "not mounted". */
		bool via_unit = false;
		if (usable && (!mp || !*mp)) {
			const char *tbl = mount_point_of(path);
			if (tbl) {
				free(mp);
				mp = xstrdup(tbl);
			} else {
				/* Not mounted anywhere — but a systemd unit may still
				 * claim it for an autofs target, in which case the disk
				 * IS reachable there and must not also be offered as a
				 * separate unmounted device. */
				char *uuid = kv_val(lines[i], "UUID");
				const char *where = unit_where_for(path, uuid);
				if (where && is_autofs_target(where)) {
					free(mp);
					mp = xstrdup(where);
					via_unit = true;
				}
				free(uuid);
			}
		}

		if (usable && !boring_mount(mp)) {
			char *label = kv_val(lines[i], "LABEL");
			char *size = kv_val(lines[i], "SIZE");
			char *rm = kv_val(lines[i], "RM");
			char *hot = kv_val(lines[i], "HOTPLUG");

			bool removable = (rm && !strcmp(rm, "1")) || (hot && !strcmp(hot, "1"));
			bool optical = type && !strcmp(type, "rom");
			const char *kind = optical ? "optical" : removable ? "removable" : "disk";
			const char *icon = optical ? "media-optical"
			                 : removable ? "drive-removable-media"
			                             : "drive-harddisk";

			/* A filesystem label if it has one; otherwise the mount point's
			 * own name, which is nearly always what the user called the
			 * drive ("drive8tb"). "sda1" is the last resort, not the first:
			 * it identifies a device, not a place, and it changes when disks
			 * are plugged in a different order. */
			const char *title = (label && *label) ? label
			                  : (mp && *mp) ? sf_basename(mp)
			                  : (name && *name) ? name : "disk";
			char *emp = pct_encode(mp ? mp : "", true);

			/* Only a REAL mount. statvfs on an autofs trigger would mount
			 * the disk as a side effect of drawing its meter. */
			unsigned long long used = 0, total = 0;
			if (really_mounted(mp))
				usage_of(mp, &used, &total);
			char *su = xasprintf("%llu", used);
			char *st = xasprintf("%llu", total);

			/* fstype reports how the mount is MANAGED, not just what the
			 * filesystem is, because that is the fact the front-end needs:
			 * offering "Unmount" on an automount is offering to fight
			 * systemd, which remounts it on the next access. */
			const char *shown_fs = via_unit ? "autofs" : fstype;

			if (g_out == OUT_REC) {
				rec_row(10, emp, kind, title, icon, size ? size : "",
				        shown_fs, path ? path : "",
				        (mp && *mp) ? "1" : "0", su, st);
			} else {
				printf("%s%-22s%s %s%-10s%s %s%s%s", C_ACCENT(), title,
				       C_RESET(), C_DIM(), size ? size : "", C_RESET(),
				       C_DIM(), (mp && *mp) ? mp : "(not mounted)", C_RESET());
				if (total) {
					char *hu = human_size((off_t)used);
					char *ht = human_size((off_t)total);
					printf("  %s%s of %s (%llu%%)%s", C_DIM(), hu, ht,
					       used * 100 / total, C_RESET());
					free(hu);
					free(ht);
				}
				putchar('\n');
			}
			n++;

			mark_emitted(mp);

			free(su); free(st);
			free(emp); free(label); free(size);
			free(rm); free(hot);
		}

		free(path);
		free(type); free(fstype); free(mp); free(name);
	}

	free(parents);
	free(lines);
	free(out);
	return n;
}

/* Automount targets that no block row claimed.
 *
 * /mnt/drive3tb is a real, browsable 3TB disk that lsblk knows nothing about
 * until something touches it: the only evidence it exists is an `autofs` row
 * in the mount table. Listing those makes the drive reachable from the sidebar
 * without first mounting it — which is the entire point of an automount, and
 * the reason this does not statvfs them.
 *
 * Anything list_block already emitted is skipped, so a triggered automount
 * appears once with its real usage rather than twice. */
static int list_autofs(void)
{
	load_mounts();

	int n = 0;
	for (size_t i = 0; i < g_nmounts; i++) {
		if (strcmp(g_mounts[i].fstype, "autofs"))
			continue;
		const char *mp = g_mounts[i].point;
		if (boring_mount(mp) || already_emitted(mp))
			continue;

		const char *title = sf_basename(mp);
		char *enc = pct_encode(mp, true);

		if (g_out == OUT_REC)
			rec_row(10, enc, "disk", title, "drive-harddisk", "", "autofs",
			        "", "1", "0", "0");
		else
			printf("%s%-22s%s %s%-10s%s %s%s%s\n", C_ACCENT(), title, C_RESET(),
			       C_DIM(), "", C_RESET(), C_DIM(), mp, C_RESET());
		n++;
		mark_emitted(mp);
		free(enc);
	}
	return n;
}

/* gvfs presents every mounted share as a directory under
 * /run/user/<uid>/gvfs, named for the protocol and its parameters —
 * "smb-share:server=nas,share=media". Ugly, but it is a real path, so a
 * network share needs no special case anywhere else in this program. */
static int list_network(void)
{
	char *root = xasprintf("/run/user/%lu/gvfs", (unsigned long)getuid());
	DIR *d = opendir(root);
	if (!d) {
		free(root);
		return 0;
	}

	int n = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;

		char *full = xasprintf("%s/%s", root, e->d_name);
		char *enc = pct_encode(full, true);

		/* "smb-share:server=nas,share=media" reads as "media on nas" once the
		 * parameters are pulled apart. Falling back to the raw directory name
		 * is fine — it is ugly but it is never wrong. */
		char *title = NULL;
		const char *server = strstr(e->d_name, "server=");
		const char *share = strstr(e->d_name, "share=");
		if (server && share) {
			const char *se = strchr(server, ',');
			char *sv = xstrndup(server + 7, se ? (size_t)(se - server - 7)
			                                   : strlen(server + 7));
			const char *he = strchr(share, ',');
			char *sh = xstrndup(share + 6, he ? (size_t)(he - share - 6)
			                                  : strlen(share + 6));
			title = xasprintf("%s on %s", sh, sv);
			free(sv);
			free(sh);
		}

		if (g_out == OUT_REC)
			rec_row(10, enc, "network", title ? title : e->d_name,
			        "folder-network", "", "gvfs", "", "1", "0", "0");
		else
			printf("%s%-22s%s %s%s%s\n", C_ACCENT(), title ? title : e->d_name,
			       C_RESET(), C_DIM(), full, C_RESET());
		n++;

		free(title);
		free(enc);
		free(full);
	}

	closedir(d);
	free(root);
	return n;
}

/* ── mounting ───────────────────────────────────────────────────────────────
 *
 * Delegated to udisksctl, never reimplemented. udisks2 owns the polkit rules
 * that let a desktop user mount a disk without being root, and a file manager
 * that called mount(2) itself would have to run privileged to do the same job
 * worse. The only work here is turning its output into a record.
 */
static int volume_mount(const char *device, bool unmount)
{
	if (!have_cmd("udisksctl"))
		die(_("udisksctl is not installed — install udisks2 to mount from here"));

	/* A device path, not a name: "sdc1" would be ambiguous, and passing a
	 * caller-supplied string straight through to a mount helper is exactly
	 * where a surprising argument does damage. */
	if (strncmp(device, "/dev/", 5))
		die(_("%s: expected a device path like /dev/sdc1"), device);

	char *argv[] = { (char *)"udisksctl", (char *)(unmount ? "unmount" : "mount"),
	                 (char *)"-b", (char *)device, NULL };
	int st = 0;
	char *out = run_capture(argv, &st, false);
	strip_trailing_newline(out);

	if (st != 0) {
		if (g_out == OUT_REC) {
			char *e = pct_encode(device, true);
			rec_row(3, e, "failed", out);
			free(e);
		}
		free(out);
		return 1;
	}

	/* "Mounted /dev/sdc1 at /run/media/velle/label" — the mount point is what
	 * the caller wants next, so it is dug out rather than making the GUI
	 * re-run `volumes` to discover where the disk landed. */
	const char *at = strstr(out, " at ");
	const char *mp = at ? at + 4 : "";

	if (g_out == OUT_REC) {
		char *e = pct_encode(device, true);
		char *m = pct_encode(mp, true);
		rec_row(3, e, unmount ? "unmounted" : "mounted", m);
		free(e);
		free(m);
	} else {
		printf("%s\n", out);
	}

	free(out);
	return 0;
}

int cmd_mount(int argc, char **argv)
{
	if (argc < 1)
		die(_("mount: need a device path (see: synfiles volumes)"));
	if (g_out == OUT_REC)
		rec_row(3, "device", "status", "path");
	return volume_mount(argv[0], false);
}

int cmd_unmount(int argc, char **argv)
{
	if (argc < 1)
		die(_("unmount: need a device path (see: synfiles volumes)"));
	if (g_out == OUT_REC)
		rec_row(3, "device", "status", "path");
	return volume_mount(argv[0], true);
}

int cmd_volumes(int argc, char **argv)
{
	bool net_only = false, blk_only = false;
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--network"))     net_only = true;
		else if (!strcmp(argv[i], "--block"))  blk_only = true;
		else die(_("volumes: unknown option '%s'"), argv[i]);
	}

	if (g_out == OUT_REC)
		rec_row(10, "path", "kind", "title", "icon", "size", "fstype",
		        "device", "mounted", "used", "total");

	int n = 0;
	if (!net_only) {
		n += list_block();
		n += list_autofs();
	}
	if (!blk_only)
		n += list_network();

	if (g_out == OUT_HUMAN && n == 0)
		printf("%s%s%s\n", C_DIM(),
		       _("no volumes"), C_RESET());

	return n ? 0 : 100;
}
