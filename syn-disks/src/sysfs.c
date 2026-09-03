/* sysfs.c — the kernel's own account of the storage tree.
 *
 * This is the primary source, not a fallback. /sys/class/block is complete,
 * always present, needs no daemon and no library, and it is the only source
 * that still answers in a rescue shell. lsblk is used later for the handful of
 * facts that live in the udev database instead, and its absence degrades the
 * output rather than breaking it.
 *
 * Everything in here works on a KERNEL NAME — "sda1", "nvme0n1", "dm-0" —
 * never on a /dev path. /dev is full of symlinks that all lead to the same
 * device under different names (/dev/mapper/cryptroot, /dev/dm-0,
 * /dev/disk/by-uuid/…), and comparing two spellings of one device as strings
 * is exactly how a safety check gets silently skipped. sd_kernel_name() is the
 * one door in, and it resolves by DEVICE NUMBER.
 *
 * SYN_DISKS_SYSFS overrides the root, so the test suite can describe a machine
 * with hardware this one does not have.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "syn-disks.h"
#include "i18n.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static const char *sysfs_root(void)
{
	const char *env = getenv("SYN_DISKS_SYSFS");
	return (env && *env) ? env : "/sys";
}

/* <sysfs>/class/block/<kname>[/<rel>] */
static char *blk_path(const char *kname, const char *rel)
{
	return rel ? xasprintf("%s/class/block/%s/%s", sysfs_root(), kname, rel)
	           : xasprintf("%s/class/block/%s", sysfs_root(), kname);
}

static const char *last_component(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

/* ── resolving a name ───────────────────────────────────────────────────── */

char *sd_kernel_name(const char *devpath)
{
	if (!devpath || !*devpath)
		return NULL;

	/* A bare name is taken as a kernel name if the kernel agrees one exists.
	 * That is a convenience on the command line — `syn-disks info sda1` —
	 * and it is what lets the test suite point at a fixture tree, where
	 * there are no device nodes to stat. */
	if (!strchr(devpath, '/')) {
		char *p = blk_path(devpath, NULL);
		bool ok = access(p, F_OK) == 0;
		free(p);
		return ok ? xstrdup(devpath) : NULL;
	}

	struct stat st;
	if (stat(devpath, &st) != 0 || !S_ISBLK(st.st_mode))
		return NULL;

	/* The device number is the identity. /dev/mapper/cryptroot and
	 * /dev/dm-0 are the same device and different strings; both land on
	 * /sys/dev/block/253:0, whose real path ends in "dm-0". */
	char *link = xasprintf("%s/dev/block/%u:%u", sysfs_root(),
	                       major(st.st_rdev), minor(st.st_rdev));
	char *real = realpath(link, NULL);
	free(link);
	if (!real)
		return NULL;

	char *name = xstrdup(last_component(real));
	free(real);
	return name;
}

char *sd_attr(const char *kname, const char *rel)
{
	char *p = blk_path(kname, rel);
	char *text = slurp(p);
	free(p);
	if (!text)
		return NULL;
	strip_trailing_newline(text);
	return trim(text);
}

unsigned long long sd_size_bytes(const char *kname)
{
	char *s = sd_attr(kname, "size");
	if (!s)
		return 0;
	unsigned long long sectors = strtoull(s, NULL, 10);
	free(s);
	/* 512 is not the device's block size and must not be replaced with it.
	 * The `size` attribute is documented in 512-byte units whatever the
	 * hardware reports, so scaling it by logical_block_size on a 4Kn drive
	 * would report eight times the real capacity. */
	return sectors * 512ULL;
}

bool sd_is_partition(const char *kname)
{
	char *p = blk_path(kname, "partition");
	bool yes = access(p, F_OK) == 0;
	free(p);
	return yes;
}

/* By CONTAINMENT rather than by resolving the sysfs symlink: /sys/class/block
 * holds every disk and every partition side by side, and a disk's directory
 * contains its own partitions as subdirectories. That needs no symlink to
 * resolve, so a fixture tree of plain directories describes a machine exactly
 * as the real one does. */
char *sd_parent_disk(const char *kname)
{
	if (!sd_is_partition(kname))
		return NULL;

	char *dir = xasprintf("%s/class/block", sysfs_root());
	DIR *d = opendir(dir);
	free(dir);
	if (!d)
		return NULL;

	char *found = NULL;
	struct dirent *e;
	while (!found && (e = readdir(d))) {
		if (e->d_name[0] == '.' || !strcmp(e->d_name, kname))
			continue;
		char *cand = xasprintf("%s/class/block/%s/%s", sysfs_root(),
		                       e->d_name, kname);
		struct stat st;
		if (stat(cand, &st) == 0 && S_ISDIR(st.st_mode))
			found = xstrdup(e->d_name);
		free(cand);
	}
	closedir(d);
	return found;
}

/* ── the walk down to physical hardware ─────────────────────────────────── */

static void list_add(char ***v, size_t *n, const char *s)
{
	for (size_t i = 0; i < *n; i++)
		if (!strcmp((*v)[i], s))
			return;               /* a disk reached twice is one disk */
	*v = xrealloc(*v, (*n + 1) * sizeof **v);
	(*v)[(*n)++] = xstrdup(s);
}

/* Names in a subdirectory of the device — slaves/ or holders/. */
static char **child_names(const char *kname, const char *rel, size_t *n)
{
	*n = 0;
	char *dir = blk_path(kname, rel);
	DIR *d = opendir(dir);
	free(dir);
	if (!d)
		return NULL;

	char **out = NULL;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		out = xrealloc(out, (*n + 1) * sizeof *out);
		out[(*n)++] = xstrdup(e->d_name);
	}
	closedir(d);
	return out;
}

char **sd_holders(const char *kname, size_t *n)
{
	return child_names(kname, "holders", n);
}

char **sd_slaves(const char *kname, size_t *n)
{
	return child_names(kname, "slaves", n);
}

/* The partitions of a disk, in on-disk order.
 *
 * A disk's sysfs directory contains its partitions as subdirectories, each
 * with a `partition` file holding its number. Sorting by that number rather
 * than by name is what keeps sda10 after sda9 — a string sort puts it between
 * sda1 and sda2, and a partition table displayed in the wrong order is one
 * somebody will act on by position. */
typedef struct { char *name; long num; } part_t;

static int by_partnum(const void *a, const void *b)
{
	const part_t *x = a, *y = b;
	return x->num < y->num ? -1 : x->num > y->num ? 1 : 0;
}

char **sd_partitions(const char *kname, size_t *n)
{
	*n = 0;
	char *dir = blk_path(kname, NULL);
	DIR *d = opendir(dir);
	free(dir);
	if (!d)
		return NULL;

	part_t *tmp = NULL;
	size_t k = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		char *num = sd_attr(e->d_name, "partition");
		if (!num)
			continue;
		/* It must be a partition AND live inside this disk's directory —
		 * checking only the first would match every partition on the
		 * machine, since sd_attr resolves through /sys/class/block. */
		char *inside = blk_path(kname, e->d_name);
		bool here = access(inside, F_OK) == 0;
		free(inside);
		if (!here) {
			free(num);
			continue;
		}
		tmp = xrealloc(tmp, (k + 1) * sizeof *tmp);
		tmp[k].name = xstrdup(e->d_name);
		tmp[k].num = strtol(num, NULL, 10);
		k++;
		free(num);
	}
	closedir(d);

	if (k)
		qsort(tmp, k, sizeof *tmp, by_partnum);

	char **out = NULL;
	if (k) {
		out = xmalloc(k * sizeof *out);
		for (size_t i = 0; i < k; i++)
			out[i] = tmp[i].name;
	}
	free(tmp);
	*n = k;
	return out;
}

int sd_count_partitions(const char *kname)
{
	size_t n = 0;
	char **p = sd_partitions(kname, &n);
	sd_free_list(p, n);
	return (int)n;
}

static void base_walk(const char *kname, char ***out, size_t *n, int depth)
{
	/* Storage stacks are shallow — LVM on LUKS on MD on a partition is four
	 * — but a bound is cheap and a runaway recursion inside a safety check
	 * is worse than a wrong answer. */
	if (depth > 16)
		return;

	size_t ns = 0;
	char **slaves = child_names(kname, "slaves", &ns);
	if (ns > 0) {
		/* Device-mapper, LUKS, LVM, MD. A device can sit on several, which
		 * is why this returns a SET and not one name: a mirror spans two
		 * disks and both of them are "the disk the root filesystem is on". */
		for (size_t i = 0; i < ns; i++)
			base_walk(slaves[i], out, n, depth + 1);
		sd_free_list(slaves, ns);
		return;
	}
	sd_free_list(slaves, ns);

	char *parent = sd_parent_disk(kname);
	if (parent) {
		base_walk(parent, out, n, depth + 1);
		free(parent);
		return;
	}

	list_add(out, n, kname);
}

char **sd_base_disks(const char *kname, size_t *n)
{
	char **out = NULL;
	*n = 0;
	if (kname && *kname)
		base_walk(kname, &out, n, 0);
	return out;
}

void sd_free_list(char **v, size_t n)
{
	for (size_t i = 0; i < n; i++)
		free(v[i]);
	free(v);
}

/* ── enumerating ────────────────────────────────────────────────────────── */

/* Virtual block devices nobody manages from a disk utility. loop devices come
 * and go with every ISO somebody opens, ram/zram are memory, and listing forty
 * of them is how the drive you were looking for ends up in the middle of a
 * list nobody reads. */
static bool uninteresting(const char *name)
{
	static const char *pre[] = { "loop", "ram", "zram", "md" };
	for (size_t i = 0; i < sizeof pre / sizeof *pre; i++) {
		size_t n = strlen(pre[i]);
		if (!strncmp(name, pre[i], n) && name[n] >= '0' && name[n] <= '9')
			return true;
	}
	return false;
}

static int by_name(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

char **sd_all_disks(size_t *n)
{
	*n = 0;
	char *dir = xasprintf("%s/class/block", sysfs_root());
	DIR *d = opendir(dir);
	free(dir);
	if (!d)
		return NULL;

	char **out = NULL;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' || uninteresting(e->d_name))
			continue;
		if (sd_is_partition(e->d_name))
			continue;

		/* A device with slaves is built ON something — cryptroot, an LVM
		 * volume, a RAID array. It is a volume, not a drive, and it belongs
		 * in the partition list of the disk underneath it rather than
		 * beside that disk as though it were separate hardware. */
		size_t ns = 0;
		char **slaves = child_names(e->d_name, "slaves", &ns);
		sd_free_list(slaves, ns);
		if (ns > 0)
			continue;

		out = xrealloc(out, (*n + 1) * sizeof *out);
		out[(*n)++] = xstrdup(e->d_name);
	}
	closedir(d);

	if (out)
		qsort(out, *n, sizeof *out, by_name);
	return out;
}

/* ── describing one ─────────────────────────────────────────────────────── */

const char *sd_transport(const char *kname)
{
	/* The bus is written into the sysfs path the device hangs off:
	 *   /devices/pci…/usb1/1-4/…/block/sdd        -> usb
	 *   /devices/pci…/nvme/nvme0/nvme0n1          -> nvme
	 *   /devices/pci…/ata1/host0/…/block/sda      -> sata
	 * Reading it from there costs one readlink and needs no tool. */
	char *p = blk_path(kname, NULL);
	char *real = realpath(p, NULL);
	free(p);
	if (!real)
		return "";

	const char *out = "";
	if (strstr(real, "/usb"))            out = "usb";
	else if (strstr(real, "/nvme/"))     out = "nvme";
	else if (strstr(real, "/ata"))       out = "sata";
	else if (strstr(real, "/mmc"))       out = "mmc";
	else if (strstr(real, "/virtio"))    out = "virtio";
	else if (strstr(real, "/virtual/"))  out = "virtual";
	else if (strstr(real, "/host"))      out = "scsi";
	free(real);
	return out;
}

const char *sd_kind(const char *kname)
{
	/* Ask the DRIVE, not the partition.
	 *
	 * `removable` and `queue/rotational` exist only on a whole disk — a
	 * partition's sysfs directory has neither. Read there they come back
	 * NULL, "is it spinning" answers no by default, and every partition of
	 * the 8TB spinning disk in this machine confidently reported itself as
	 * an SSD. Hardware facts belong to hardware. */
	char *owner = sd_is_partition(kname) ? sd_parent_disk(kname) : NULL;
	const char *hw = owner ? owner : kname;

	char *removable = sd_attr(hw, "removable");
	char *rot = sd_attr(hw, "queue/rotational");
	const char *tran = sd_transport(hw);

	bool rm = removable && !strcmp(removable, "1");
	bool spinning = rot && !strcmp(rot, "1");
	free(removable);
	free(rot);

	const char *out;

	/* An optical drive is removable AND its media is, and it is the one
	 * device here whose "size" means the disc rather than the hardware. */
	if (!strncmp(hw, "sr", 2))
		out = "optical";
	/* REMOVABLE BEFORE ROTATIONAL, and this order is the whole point.
	 *
	 * The SanDisk Cruzer Blade in this machine reports
	 * queue/rotational = 1. USB mass-storage bridges routinely do; the flag
	 * describes what the bridge chose to claim, not what the flash is. Read
	 * rotational first and a memory stick is confidently labelled a hard
	 * disk, complete with a spinning-platter icon. */
	else if (rm || !strcmp(tran, "usb"))
		out = !strcmp(tran, "mmc") ? "sd-card" : "usb-stick";
	else if (!strcmp(tran, "mmc"))
		out = "sd-card";
	else if (!strcmp(tran, "virtual"))
		out = "dm";
	else if (!strcmp(tran, "nvme"))
		out = "ssd";
	else if (spinning)
		out = "hdd";
	else
		out = "ssd";

	/* Freed last: `hw` aliases it until the final comparison above, and
	 * every value of `out` is a string literal rather than a borrow. */
	free(owner);
	return out;
}
