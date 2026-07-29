/*
 * synapse_evchr.c — /dev/synapse-events, the event feed with per-reader cursors
 *
 * WHY THIS EXISTS
 *
 * The event ring used to be drained through /sys/kernel/synapse/syscall_log,
 * whose read advanced the ring's shared tail. That made it a single-consumer
 * queue published as a world-readable-to-root file, with nothing anywhere
 * enforcing the single consumer. Two consequences, one operational and one
 * security:
 *
 *   - `cat /sys/kernel/synapse/syscall_log` while testing a detection rule
 *     consumed the event being tested for. That happened twice during one
 *     audit before the cause was understood: a write to /etc/profile.d
 *     produced no synguard alert purely because the ring had been read.
 *
 *   - An attacker who already has root -- which is exactly the situation
 *     synguard exists to detect and report -- could read the file in a loop
 *     and consume events before the detector's ~100ms poll. No module unload,
 *     no kprobe disarm, so the probe self-integrity watchdog sees nothing.
 *     synguard's 20s canary does catch *sustained* draining, because the
 *     canary's own event is stolen too, but a short drain around one action
 *     fits inside that window.
 *
 * A sysfs attribute cannot fix this: kobj_attribute->show() receives only
 * (kobj, attr, buf) and has nowhere to keep per-open state. A character device
 * does, and it needs no allocation to do it -- read()'s *ppos IS per-open
 * state, maintained by the kernel. open() seeds it with the ring's current
 * tail and read() walks it forward, so each reader has an independent view and
 * no reader can affect another.
 *
 * The ring's tail is now advanced by ring_push() alone, when a new event
 * overwrites an old one. A reader that falls behind loses events rather than
 * holding them; that is reported through `dropped` rather than hidden.
 *
 * syscall_log remains, as a non-destructive peek at the newest events for a
 * human with a shell.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "synapse_kmod.h"
#include "synapse_probe.h"
#include "synapse_evchr.h"

/* One read() formats at most this much text. Bounded so a large read() from
 * userspace cannot ask the module for an arbitrary kernel allocation. */
#define SYN_EVCHR_BUF   8192

/*
 * Per-open state that does NOT fit in *ppos. The cursor itself lives in
 * *ppos; this only counts what the reader missed, which is per-open because
 * one reader falling behind says nothing about another.
 */
struct syn_evchr_reader {
    u64 dropped;
    u64 reported;
};

static int syn_evchr_open(struct inode *inode, struct file *file)
{
    struct syn_evchr_reader *r = kzalloc(sizeof(*r), GFP_KERNEL);

    if (!r)
        return -ENOMEM;

    file->private_data = r;

    /*
     * Start at the oldest event still in the ring rather than at the newest.
     * A restarted synguard then picks up the backlog instead of silently
     * beginning at "now" and missing whatever happened while it was down --
     * which is precisely the window an attacker would choose.
     */
    file->f_pos = (loff_t)synapse_probe_ring_tail();

    return 0;
}

static int syn_evchr_release(struct inode *inode, struct file *file)
{
    kfree(file->private_data);
    file->private_data = NULL;
    return 0;
}

static ssize_t syn_evchr_read(struct file *file, char __user *ubuf,
                              size_t len, loff_t *ppos)
{
    struct syn_evchr_reader *r = file->private_data;
    char *kbuf;
    ssize_t n;
    u32 cursor;

    if (!r)
        return -EBADF;
    if (!len)
        return 0;
    if (len > SYN_EVCHR_BUF)
        len = SYN_EVCHR_BUF;

    kbuf = kmalloc(len, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    cursor = (u32)*ppos;
    n = synapse_probe_read_from(kbuf, len, &cursor, &r->dropped);
    *ppos = (loff_t)cursor;

    /*
     * Loss is the one thing a detector must never learn about silently. Rate
     * limited to one line per newly dropped batch so a reader that is
     * permanently behind cannot itself flood the kernel log.
     */
    if (r->dropped != r->reported) {
        pr_warn_ratelimited(
            "synapse: event reader (%s pid %d) fell behind, %llu events lost\n",
            current->comm, task_pid_nr(current), r->dropped - r->reported);
        r->reported = r->dropped;
    }

    if (n > 0 && copy_to_user(ubuf, kbuf, (size_t)n)) {
        /* The cursor already moved. Rewinding it would re-deliver events on
         * the next read; leaving it means this batch is lost to a reader whose
         * own buffer just failed. Prefer losing them to duplicating them --
         * synguard's netwatch counts connections, and a replayed batch would
         * manufacture a worm alert out of a userspace fault. */
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);
    return n;
}

static const struct file_operations syn_evchr_fops = {
    .owner   = THIS_MODULE,
    .open    = syn_evchr_open,
    .read    = syn_evchr_read,
    .release = syn_evchr_release,
    .llseek  = default_llseek,
};

static struct miscdevice syn_evchr_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "synapse-events",
    /* 0440 root:root, matching the sysfs node this replaces. The event stream
     * names every path every process opens; it is a surveillance feed, not
     * something to widen for convenience. */
    .mode  = 0440,
    .fops  = &syn_evchr_fops,
};

int synapse_evchr_init(void)
{
    int ret = misc_register(&syn_evchr_dev);

    if (ret) {
        pr_err("synapse: cannot register /dev/synapse-events: %d\n", ret);
        return ret;
    }

    pr_info("synapse: /dev/synapse-events ready (per-reader cursors)\n");
    return 0;
}

void synapse_evchr_exit(void)
{
    misc_deregister(&syn_evchr_dev);
}
