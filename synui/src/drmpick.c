/*
 * drmpick.c — keep a firmware framebuffer from becoming the render GPU.
 *
 * The firmware's boot framebuffer (simpledrm, efidrm, vesadrm, ofdrm) is a KMS
 * card of its own until the real driver evicts it. When eviction does not
 * happen (nvidia_drm with fbdev=0 before synapse_kmod 32 is the case that bit),
 * wlroots finds "2 GPUs" and can make the framebuffer its primary, which it did
 * on a 3060 whose own card reads boot_display=1. fx_renderer on the framebuffer
 * got only software GL and refused it. synui's fallback then forced software
 * rendering, which is the path that corrupts layer surfaces
 * (project_synui_vm_forced_llvmpipe).
 * The whole desktop was drawn by llvmpipe and copied to the real GPU, the
 * framebuffer's own connector became a phantom fourth monitor, and the
 * compositor burned every core.
 *
 * So before wlr_backend_autocreate() the DRM class is read from sysfs, and if
 * a firmware framebuffer sits beside a real KMS card, WLR_DRM_DEVICES is set to
 * the real cards only, boot display first. In every other case nothing is
 * touched: no firmware framebuffer, a framebuffer that is the ONLY display (a
 * VM with no GPU driver, NVIDIA with modeset=0), or a WLR_DRM_DEVICES the user
 * set themselves.
 *
 * Pure sysfs, no device is opened, so tests/drmpick_test.c drives it with a
 * fake /sys/class/drm.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "synui.h"

#define DRMPICK_MAX 16

/* The platform drivers of the kernel's firmware-framebuffer DRM drivers
 * (drivers/gpu/drm/sysfb/). They bind the boot framebuffer, not a GPU. */
static const char *const firmware_drivers[] = {
    "simple-framebuffer", "efi-framebuffer", "vesa-framebuffer", "of-display", NULL,
};

struct card {
    int  minor;
    int  boot;          /* boot_display / boot_vga — what wlroots ranks first */
};

static int read_flag(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int c = fgetc(f);
    fclose(f);
    return c == '1';
}

static int is_firmware(const char *drm_class, const char *name)
{
    char link[PATH_MAX], real[PATH_MAX];
    snprintf(link, sizeof(link), "%s/%s/device/driver", drm_class, name);
    if (!realpath(link, real)) return 0;
    const char *drv = strrchr(real, '/');
    drv = drv ? drv + 1 : real;
    for (int i = 0; firmware_drivers[i]; i++)
        if (strcmp(drv, firmware_drivers[i]) == 0) return 1;
    return 0;
}

/* A KMS driver publishes its connectors beside the card (card1-DP-3). A card
 * with none cannot drive a screen (nvidia_drm with modeset=0), so dropping the
 * framebuffer in its favour would leave nothing to display on. */
static int has_connectors(const char *name, DIR *d)
{
    size_t n = strlen(name);
    rewinddir(d);
    for (struct dirent *e; (e = readdir(d));)
        if (strncmp(e->d_name, name, n) == 0 && e->d_name[n] == '-') return 1;
    return 0;
}

static int by_boot_then_minor(const void *a, const void *b)
{
    const struct card *x = a, *y = b;
    if (x->boot != y->boot) return y->boot - x->boot;
    return x->minor - y->minor;
}

char *drm_pick_devices(const char *drm_class, const char *dev_dir)
{
    DIR *d = opendir(drm_class);
    if (!d) return NULL;

    /* Collect names first: has_connectors() rewinds the same stream. */
    char names[DRMPICK_MAX][32];
    int nnames = 0;
    for (struct dirent *e; (e = readdir(d)) && nnames < DRMPICK_MAX;) {
        int minor, used = 0;
        size_t len = strlen(e->d_name);
        if (len < sizeof(names[0]) &&
            sscanf(e->d_name, "card%d%n", &minor, &used) == 1 && e->d_name[used] == '\0')
            memcpy(names[nnames++], e->d_name, len + 1);
    }

    struct card real[DRMPICK_MAX];
    int nreal = 0, nfirmware = 0;
    for (int i = 0; i < nnames; i++) {
        if (is_firmware(drm_class, names[i])) {
            nfirmware++;
            continue;
        }
        if (!has_connectors(names[i], d)) continue;
        char path[PATH_MAX];
        struct card c = {0};
        sscanf(names[i], "card%d", &c.minor);
        snprintf(path, sizeof(path), "%s/%s/boot_display", drm_class, names[i]);
        c.boot = read_flag(path);
        snprintf(path, sizeof(path), "%s/%s/device/boot_vga", drm_class, names[i]);
        c.boot |= read_flag(path);
        real[nreal++] = c;
    }
    closedir(d);

    if (nfirmware == 0 || nreal == 0) return NULL;

    qsort(real, nreal, sizeof(real[0]), by_boot_then_minor);
    size_t cap = (size_t)nreal * (strlen(dev_dir) + 16), len = 0;
    char *out = malloc(cap);
    if (!out) return NULL;
    out[0] = '\0';
    for (int i = 0; i < nreal; i++)
        len += snprintf(out + len, cap - len, "%s%s/card%d", i ? ":" : "", dev_dir, real[i].minor);
    return out;
}
