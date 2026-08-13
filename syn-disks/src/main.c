/* main.c — flags and dispatch.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-disks.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(FILE *f)
{
	fputs(
"syn-disks " SYNDISKS_VERSION " — the SynapseOS disk utility\n"
"\n"
"Usage: syn-disks [options] <command> [arguments]\n"
"\n"
"Looking\n"
"  list                    every drive in this machine\n"
"  parts <disk>            the partitions on one, and anything unlocked\n"
"                          or assembled on top of them\n"
"  info <device>           everything known about one disk or partition\n"
"  smart <disk>            drive health, from smartmontools\n"
"       --elevate          ask for authorisation; SMART needs root\n"
"  table <disk>            the partition table, the free space between the\n"
"                          partitions, and what is protecting each of them\n"
"\n"
"Changing things\n"
"  mount <partition>       mount it through udisks2\n"
"  unmount <partition>     unmount it again\n"
"  eject <disk>            flush, unmount everything, power the drive down\n"
"  format <device> --fs=TYPE [--label=NAME] --yes\n"
"                          ERASES the device and makes a new filesystem\n"
"       --fs=TYPE          ext4, btrfs, xfs, vfat, exfat or ntfs\n"
"       -n, --dry-run      print the exact command instead of running it\n"
"\n"
"Partitioning — all four take --yes and -n, exactly as format does\n"
"  mkpart <disk> [--size=SIZE] [--fs=TYPE] [--label=NAME] --yes\n"
"                          a new partition in the largest free space; all of\n"
"                          it when --size is left out\n"
"  rmpart <partition> --yes\n"
"                          DESTROYS one partition and everything on it\n"
"  resize <partition> --size=SIZE --yes\n"
"                          GROWS it into the free space that follows. It will\n"
"                          not shrink one: the filesystem inside would still\n"
"                          believe it owns the blocks past the new end\n"
"  mktable <disk> --type=gpt|dos --yes\n"
"                          DESTROYS every partition on the drive at once\n"
"\n"
"SIZE is 20G, 512MiB, 1.5TB or a plain number of bytes. IEC suffixes (KiB,\n"
"MiB, GiB) are powers of 1024 and the two-letter SI ones (KB, MB, GB) are\n"
"powers of 1000. Partitions are aligned to a megabyte.\n"
"\n"
"Front-ends\n"
"  gui [device]            the graphical window\n"
"  gui --format <device>   the window, opened on the format dialogue for that\n"
"                          device (it still asks before it erases anything)\n"
"  about                   version, licence, and what works on this machine\n"
"\n"
"Options\n"
"  --rec                   machine-readable records (what the GUI parses)\n"
"  -v, --verbose           more detail on stderr\n"
"  --no-color              plain output even on a terminal\n"
"  -V, --version           print the version\n"
"  -h, --help              this text\n"
"\n"
"format refuses, with no override, to touch anything that is mounted or that\n"
"shares a physical disk with \"/\". The check walks the whole stack, so an\n"
"encrypted container holding the running system is refused even though the\n"
"partition itself reports nothing mounted.\n"
"\n"
"Partitioning is narrower on purpose — refusing the whole drive would mean the\n"
"feature could never do anything on a machine with one disk. It refuses the\n"
"PARTITIONS that matter and allows the free space around them: anything \"/\"\n"
"rests on, anything mounted, anything holding live swap, anything with a\n"
"volume unlocked on top of it, and anything /etc/fstab expects at the next\n"
"boot. `table` prints that reason per row, so a front-end never has to work it\n"
"out for itself. There is no --force for any of it.\n"
"\n"
"Every field of --rec output is PERCENT-ENCODED, including the ones that look\n"
"like plain words: a filesystem label is arbitrary bytes and a mount point is\n"
"a path. Decode for display only — the encoded form is what to hand back.\n"
"\n"
"Exit status: 0 success, 1 failure or refusal, 2 needed --yes, 3 unavailable,\n"
"100 \"nothing to list\".\n", f);
}

/* The GUI is quickshell rendering data/syn-disks.qml in a separate process,
 * for the same reason synfiles and synpkg do it that way: the binary stays
 * usable over SSH, and the window consumes exactly the records any other
 * consumer would. */
static int cmd_gui(int argc, char **argv)
{
	/* --format: open straight into the format dialogue for the device named.
	 * The file manager's "Format…" is the caller — a menu entry that opened a
	 * drive list and left the user to find the same device again would be a
	 * link to the app, not the action it says it is.
	 *
	 * It only ASKS. The dialogue still runs the dry run, still shows what
	 * would be erased, and still refuses through guard.c; there is no path
	 * here that formats anything without the confirmation the window asks
	 * for. */
	bool want_format = false;
	if (argc > 0 && !strcmp(argv[0], "--format")) {
		want_format = true;
		argc--;
		argv++;
	}
	if (want_format && (argc == 0 || !*argv[0]))
		die("gui --format: need a device to format");

	/* The device to open on travels in the environment; quickshell takes no
	 * arguments of its own. Resolved to a kernel name first, so that the
	 * window opens on the right drive whether it was given /dev/sda1,
	 * /dev/mapper/cryptroot or a by-uuid symlink — and so that a name that
	 * is not a block device is caught here rather than drawing an empty
	 * window.
	 *
	 * Argument checking comes BEFORE the display and quickshell checks: a
	 * mistyped device is a mistyped device on a headless box too, and putting
	 * the environment first meant the only thing testable about this command
	 * depended on the test host having a session. */
	char *k = NULL;
	if (argc > 0 && *argv[0]) {
		k = sd_kernel_name(argv[0]);
		if (!k)
			die("%s: not a block device", argv[0]);
	}

	if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY"))
		die("no display — syn-disks gui needs a graphical session");
	if (!have_cmd("quickshell"))
		die("quickshell is not installed — synpkg install quickshell");

	if (k) {
		/* The window is a view of a DRIVE with something highlighted inside
		 * it, so the argument has to be split into those two things.
		 *
		 * sd_base_disks rather than sd_parent_disk, because the argument is
		 * not always a partition. /dev/mapper/cryptroot resolves to dm-0,
		 * which is a volume and deliberately not in the drive list at all —
		 * naming it as the drive opened the window on a device the sidebar
		 * does not contain, and the GUI fell back to the first disk in the
		 * machine. Walking to the physical disk underneath gets both halves
		 * right for a partition, for an unlocked volume, and for a whole
		 * drive, which resolves to itself. */
		size_t nb = 0;
		char **base = sd_base_disks(k, &nb);
		setenv("SYN_DISKS_SELECT", k, 1);
		setenv("SYN_DISKS_DISK", nb > 0 ? base[0] : k, 1);
		sd_free_list(base, nb);
		free(k);

		/* Set only once the name has resolved, so a bad device leaves a
		 * window that says so rather than one asking to format nothing. */
		if (want_format)
			setenv("SYN_DISKS_FORMAT", "1", 1);
	}


	/* The window's Wayland app_id. Without it quickshell names every one of
	 * its windows "org.quickshell", which is both the generic icon in the
	 * dock and the reason the dock cannot resolve a .desktop for the window
	 * and so offers no "New Window" either.
	 *
	 * OVERWRITTEN, not merely set. This is one process deciding what ITS OWN
	 * window is called, and no caller has ever had a reason to name it
	 * something else. An INHERITED value is the real and common accident:
	 * every one of these apps is a quickshell app that hands its whole
	 * environment to what it spawns, and synfiles' "Open in Disks" is exactly
	 * that path — it gave this window synfiles' identity and no dock entry of
	 * its own. */
	setenv("QS_APP_ID", "syn-disks", 1);

	const char *qml = SYNDISKS_DATADIR "/syn-disks.qml";
	if (access(qml, R_OK) != 0 && access("data/syn-disks.qml", R_OK) == 0)
		qml = "data/syn-disks.qml";

	char *child[] = { (char *)"quickshell", (char *)"-p", (char *)qml, NULL };
	execvp(child[0], child);
	die("could not start quickshell");
}

int main(int argc, char **argv)
{
	g_color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR");

	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--rec")) {
			g_out = OUT_REC;
			g_color = false;
		} else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
			g_verbose = true;
		} else if (!strcmp(a, "--no-color") || !strcmp(a, "--no-colour")) {
			g_color = false;
		} else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			printf("syn-disks " SYNDISKS_VERSION "\n");
			return 0;
		} else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout);
			return 0;
		} else {
			break;
		}
	}

	if (i >= argc) {
		usage(stderr);
		return 2;
	}

	const char *cmd = argv[i++];
	int rest_argc = argc - i;
	char **rest = argv + i;

	if (!strcmp(cmd, "list"))    return cmd_list(rest_argc, rest);
	if (!strcmp(cmd, "parts"))   return cmd_parts(rest_argc, rest);
	if (!strcmp(cmd, "info"))    return cmd_info(rest_argc, rest);
	if (!strcmp(cmd, "smart"))   return cmd_smart(rest_argc, rest);
	if (!strcmp(cmd, "mount"))   return cmd_mount(rest_argc, rest);
	if (!strcmp(cmd, "unmount")) return cmd_unmount(rest_argc, rest);
	if (!strcmp(cmd, "eject"))   return cmd_eject(rest_argc, rest);
	if (!strcmp(cmd, "format"))  return cmd_format(rest_argc, rest);
	if (!strcmp(cmd, "table"))   return cmd_table(rest_argc, rest);
	if (!strcmp(cmd, "mkpart"))  return cmd_mkpart(rest_argc, rest);
	if (!strcmp(cmd, "rmpart"))  return cmd_rmpart(rest_argc, rest);
	if (!strcmp(cmd, "resize"))  return cmd_resize(rest_argc, rest);
	if (!strcmp(cmd, "mktable")) return cmd_mktable(rest_argc, rest);
	if (!strcmp(cmd, "about"))   return cmd_about(rest_argc, rest);
	if (!strcmp(cmd, "gui"))     return cmd_gui(rest_argc, rest);

	fprintf(stderr, "syn-disks: unknown command '%s'\n\n", cmd);
	usage(stderr);
	return 2;
}
