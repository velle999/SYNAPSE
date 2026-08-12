/* syn-disks — the SynapseOS disk utility.
 *
 * Same shape as synfiles, synpkg and syn-settings: a C core that answers
 * questions, a line-oriented machine-readable output, and a front-end that is
 * only ever a renderer. Nothing in the QML knows what a block device is, and
 * nothing in C knows what a row looks like.
 *
 * ── Where the answers come from ────────────────────────────────────────────
 *
 * sysfs first, tools second, and never a library:
 *
 *   - the shape of the machine's storage is READ FROM /sys/class/block, which
 *     is the kernel's own account of it. No daemon, no D-Bus, no udev library,
 *     and it works in a rescue shell where nothing else is running.
 *   - filesystem types, labels and UUIDs come from `lsblk -P`, because they
 *     live in the udev database rather than in sysfs and lsblk is the tool
 *     that already reads it. Its KEY="value" form is designed to be parsed.
 *   - what is mounted comes from /proc/self/mounts and NEVER from lsblk. On
 *     this machine lsblk reports exactly one mount point for /dev/mapper/
 *     cryptroot — "/var/log" — because it is a btrfs with five subvolumes,
 *     and the one it happens to print is not the one that matters. A safety
 *     check built on that would have concluded the root filesystem was not
 *     mounted anywhere.
 *   - health comes from smartctl, which owns it. It is optional; without it
 *     there is no health row, never a guess.
 *   - mounting, unmounting and powering off are delegated to udisks2, which
 *     owns the polkit rules that let a desktop user do them without being
 *     root. A disk utility that called mount(2) itself would have to run
 *     privileged to do the same job worse.
 *
 * ── Output ─────────────────────────────────────────────────────────────────
 *
 * `--rec` emits tab-separated records whose FIRST record names the columns, so
 * a column added in C appears in the GUI with no QML change and the two can
 * never quietly disagree about what field three is.
 *
 *   EVERY FIELD IS PERCENT-ENCODED. Not just the paths.
 *
 * A filesystem label is arbitrary bytes, a mount point is a path that may hold
 * a tab, and a drive model is a vendor string that has already been observed
 * to arrive padded with control characters. Encoding some fields and not
 * others means every consumer has to know which is which, and the day that
 * list drifts is the day a tab in a label shifts every column of a row and the
 * GUI offers to format a different device than the one on screen.
 *
 * So there is one rule with no exceptions: encode on the way out, decode on
 * the way in, and the encoded form is the identity — the decoded form is for
 * display only and must never be handed back to this program.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_DISKS_H
#define SYN_DISKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

typedef enum { OUT_HUMAN, OUT_REC } out_mode_t;

extern out_mode_t g_out;
extern bool g_color;
extern bool g_verbose;

/* ── util.c ─────────────────────────────────────────────────────────────── */

void  *xmalloc(size_t n);
void  *xrealloc(void *p, size_t n);
char  *xstrdup(const char *s);
char  *xstrndup(const char *s, size_t n);
char  *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void   die(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
void   warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

const char *C_RESET(void);
const char *C_BOLD(void);
const char *C_DIM(void);
const char *C_ACCENT(void);
const char *C_WARN(void);
const char *C_BAD(void);

/* Percent-encode `s` against the RFC 3986 unreserved set. `keep_slash` leaves
 * '/' literal so a path stays readable to somebody debugging by eye. */
char *pct_encode(const char *s, bool keep_slash);
/* Inverse. Invalid escapes pass through verbatim rather than being guessed at,
 * and a %00 is refused — a decoded NUL would truncate the string it belongs
 * to, and nothing this program handles can legitimately contain one. */
char *pct_decode(const char *s);

/* One record: fields joined by tab, terminated by newline. Every argument is
 * encoded here, so callers pass plain strings and cannot forget. */
void rec_row(int nfields, ...);

char **split(char *text, char sep, size_t *n);
void   strip_trailing_newline(char *s);
/* Trim ASCII whitespace from both ends, in place. sysfs pads model, vendor and
 * serial with trailing spaces out of the SCSI inquiry page — "ST8000VN004-2M21"
 * arrives with none but "WD Blue SN570 1TB" arrives with twenty-three. */
char  *trim(char *s);
char  *human_size(unsigned long long bytes);
bool   have_cmd(const char *name);
char  *slurp(const char *path);
char  *run_capture(char *const argv[], int *status, bool quiet_stderr);
/* Value of KEY="..." in one `lsblk -P` line, anchored on the delimiters so
 * that asking for NAME does not match the NAME inside PKNAME. malloc'd, or
 * NULL. */
char  *kv_val(const char *line, const char *key);

/* ── sysfs.c — the kernel's account of the storage tree ─────────────────────
 *
 * Everything here works on a KERNEL NAME ("sda1", "nvme0n1", "dm-0"), never on
 * a /dev path, because /dev is full of symlinks that all lead to the same
 * device under different names and comparing two of them as strings is how a
 * check gets skipped.
 */

/* Resolve any /dev path — including /dev/mapper/cryptroot and the by-uuid
 * symlinks — to its kernel name, via st_rdev and /sys/dev/block/MAJ:MIN.
 * Following the number rather than the name is what makes two spellings of one
 * device compare equal. malloc'd; NULL if it is not a block device. */
char *sd_kernel_name(const char *devpath);

/* Read one sysfs attribute of a block device, trimmed. malloc'd, or NULL. */
char *sd_attr(const char *kname, const char *rel);

/* Size in bytes. /sys/class/block/<x>/size is in 512-byte units ALWAYS —
 * it is not scaled by the device's logical block size, and a 4Kn drive read as
 * though it were would come out eight times too small. */
unsigned long long sd_size_bytes(const char *kname);

/* Is this a partition of something else? */
bool sd_is_partition(const char *kname);
/* The disk a partition belongs to, from the sysfs hierarchy. malloc'd, NULL if
 * `kname` is not a partition. */
char *sd_parent_disk(const char *kname);

/* The set of PHYSICAL DISKS a device ultimately sits on, as kernel names.
 *
 * This is the function the format guard is built on, so it walks all the way
 * down: `slaves/` handles device-mapper, LUKS, LVM and MD (a device can have
 * several), and the partition link handles the last hop to the disk. On this
 * machine "/" is /dev/mapper/cryptroot -> dm-0 -> nvme1n1p2 -> nvme1n1, and a
 * check that stopped at the first hop would have found nothing in common with
 * a request to format /dev/nvme1n1p2.
 *
 * Caller frees the array and every string in it. */
char **sd_base_disks(const char *kname, size_t *n);
void   sd_free_list(char **v, size_t n);

/* Every PHYSICAL disk on the machine, in kernel-name order. Excludes the
 * virtual block devices nobody manages from a disk utility (loop, ram, zram)
 * and anything built on top of something else — an unlocked LUKS volume or an
 * LVM volume is not a drive, and listing it beside the drive it lives on
 * claims the machine has hardware it does not. Those appear under `parts`
 * instead, nested below the partition holding them. */
char **sd_all_disks(size_t *n);

/* The partitions of a disk, ordered by partition NUMBER rather than by name —
 * a string sort files sda10 between sda1 and sda2, and a partition table shown
 * in the wrong order is one somebody will act on by position. */
char **sd_partitions(const char *kname, size_t *n);
int    sd_count_partitions(const char *kname);

/* What is built ON this device: the unlocked LUKS volume over a container, the
 * LVM volume over a physical volume. The inverse of slaves/. */
char **sd_holders(const char *kname, size_t *n);

/* usb, nvme, sata, scsi, virtio, mmc, dm, or "" when the device link says
 * nothing recognisable. Derived from the sysfs path, which names the bus it
 * hangs off. */
const char *sd_transport(const char *kname);

/* usb-stick / sd-card / optical / ssd / hdd / dm / unknown.
 *
 * REMOVABLE IS CHECKED BEFORE ROTATIONAL, and that ordering is the whole point
 * of this function: the SanDisk Cruzer Blade in this machine reports
 * queue/rotational = 1. Flash reporting itself as spinning rust is common on
 * USB bridges, so a straight reading of that flag labels a memory stick a hard
 * disk. */
const char *sd_kind(const char *kname);

/* ── mounts.c — /proc/self/mounts ──────────────────────────────────────────*/

/* Every mount point of a device, as an array of paths. A btrfs with subvolumes
 * has several and lsblk shows one of them, which is why nothing here asks
 * lsblk. Caller frees with sd_free_list. */
char **mt_points_of(const char *kname, size_t *n);
/* Is anything at all mounted from this device right now? */
bool mt_is_mounted(const char *kname);
/* The kernel name backing "/", or NULL. */
char *mt_root_device(void);
/* Filesystem type as the kernel reports it for a mount point, or NULL. */
const char *mt_fstype_at(const char *point);
/* Bytes used and total at a mount point. Both 0 means "unknown" and must never
 * be rendered as "empty".
 *
 * f_bavail, not f_bfree: the difference is the blocks reserved for root, and
 * counting those as free is how a full disk reads "8% free" to a user who
 * cannot write a single byte to it.
 *
 * Never called on an autofs trigger — asking would MOUNT the filesystem as a
 * side effect of drawing a meter, spinning up a sleeping disk to do it. */
void mt_usage(const char *point, unsigned long long *used, unsigned long long *total);
/* Is this path an autofs trigger rather than a real mount? */
bool mt_is_autofs(const char *point);

/* ── lsblk.c — the udev database, for what sysfs does not carry ────────────*/

/* FSTYPE, LABEL, UUID, PARTLABEL, PARTTYPENAME, PTTYPE and MODEL for one
 * kernel name. Returns NULL if lsblk is absent or says nothing — every caller
 * must degrade to "unknown" rather than failing, because the storage tree is
 * still fully described without it. malloc'd; free with lsblk_free. */
typedef struct {
	char *fstype;
	char *label;
	char *uuid;
	char *partlabel;
	char *parttype;
	char *pttype;
	char *model;
} lsblk_t;

const lsblk_t *lsblk_for(const char *kname);
void lsblk_done(void);

/* ── disks.c ────────────────────────────────────────────────────────────── */
int cmd_list(int argc, char **argv);
int cmd_parts(int argc, char **argv);
int cmd_info(int argc, char **argv);

/* ── smart.c ────────────────────────────────────────────────────────────────
 * smartctl needs to open the raw device, which a desktop user cannot do, so
 * the health pane is the one place this program asks for privilege — and only
 * when asked to, via --elevate. Reading health on window open would pop a
 * polkit prompt at somebody who just wanted to see how full a disk was. */
int cmd_smart(int argc, char **argv);

/* ── actions.c ──────────────────────────────────────────────────────────── */
int cmd_mount(int argc, char **argv);
int cmd_unmount(int argc, char **argv);
int cmd_eject(int argc, char **argv);
/* DESTRUCTIVE. Gated behind --yes, and refused outright — with no override —
 * for any device that is mounted or that shares a physical disk with "/".
 * See the file header in actions.c for why there is no --force. */
int cmd_format(int argc, char **argv);

/* ── about.c ────────────────────────────────────────────────────────────── */
int cmd_about(int argc, char **argv);

/* Where "Support" points, named identically in synfiles and synpkg so the
 * suite's windows cannot end up pointing at different places. */
#define SYNAPSE_DONATE_URL "https://buymeacoffee.com/velle999"

#endif /* SYN_DISKS_H */
