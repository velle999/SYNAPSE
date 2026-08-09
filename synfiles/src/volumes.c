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

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static int list_block(void)
{
	if (!have_cmd("lsblk"))
		return 0;

	char *argv[] = { (char *)"lsblk", (char *)"-P", (char *)"-o",
	                 (char *)"NAME,PATH,LABEL,SIZE,FSTYPE,MOUNTPOINT,RM,TYPE,HOTPLUG",
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

	for (size_t i = 0; i < nlines; i++) {
		if (!*lines[i])
			continue;

		char *type = kv_val(lines[i], "TYPE");
		char *fstype = kv_val(lines[i], "FSTYPE");
		char *mp = kv_val(lines[i], "MOUNTPOINT");

		/* A whole disk with no filesystem is a container for the partitions
		 * that follow it, not somewhere to go. */
		bool usable = type && (!strcmp(type, "part") || !strcmp(type, "rom")
		                       || !strcmp(type, "crypt") || !strcmp(type, "lvm"))
		              && fstype && *fstype;

		if (usable && !boring_mount(mp)) {
			char *path = kv_val(lines[i], "PATH");
			char *label = kv_val(lines[i], "LABEL");
			char *size = kv_val(lines[i], "SIZE");
			char *rm = kv_val(lines[i], "RM");
			char *hot = kv_val(lines[i], "HOTPLUG");
			char *name = kv_val(lines[i], "NAME");

			bool removable = (rm && !strcmp(rm, "1")) || (hot && !strcmp(hot, "1"));
			bool optical = type && !strcmp(type, "rom");
			const char *kind = optical ? "optical" : removable ? "removable" : "disk";
			const char *icon = optical ? "media-optical"
			                 : removable ? "drive-removable-media"
			                             : "drive-harddisk";

			const char *title = (label && *label) ? label
			                  : (name && *name) ? name : "disk";
			char *emp = pct_encode(mp ? mp : "", true);

			if (g_out == OUT_REC) {
				rec_row(8, emp, kind, title, icon, size ? size : "",
				        fstype, path ? path : "",
				        (mp && *mp) ? "1" : "0");
			} else {
				printf("%s%-22s%s %s%-10s%s %s%s%s\n", C_ACCENT(), title,
				       C_RESET(), C_DIM(), size ? size : "", C_RESET(),
				       C_DIM(), (mp && *mp) ? mp : "(not mounted)", C_RESET());
			}
			n++;

			free(emp); free(path); free(label); free(size);
			free(rm); free(hot); free(name);
		}

		free(type); free(fstype); free(mp);
	}

	free(lines);
	free(out);
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
			rec_row(8, enc, "network", title ? title : e->d_name,
			        "folder-network", "", "gvfs", "", "1");
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

int cmd_volumes(int argc, char **argv)
{
	bool net_only = false, blk_only = false;
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--network"))     net_only = true;
		else if (!strcmp(argv[i], "--block"))  blk_only = true;
		else die("volumes: unknown option '%s'", argv[i]);
	}

	if (g_out == OUT_REC)
		rec_row(8, "path", "kind", "title", "icon", "size", "fstype",
		        "device", "mounted");

	int n = 0;
	if (!net_only)
		n += list_block();
	if (!blk_only)
		n += list_network();

	if (g_out == OUT_HUMAN && n == 0)
		printf("%sno volumes%s\n", C_DIM(), C_RESET());

	return n ? 0 : 100;
}
