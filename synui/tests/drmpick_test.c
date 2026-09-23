/*
 * drmpick_test.c — which DRM cards synui hands wlroots, from a fake sysfs.
 *
 * The case that matters is the first: the boot of 2026-09-22 where simpledrm
 * outlived nvidia_drm's handover and the whole desktop ran on llvmpipe. The
 * rest are the shapes where the guard must NOT act. A framebuffer that is the
 * only display, or one beside a card that cannot drive a screen, is the only
 * picture there is, and excluding it would leave the login screen black.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "synui.h"

static int failures;
static char root[] = "/tmp/drmpick_test.XXXXXX";
static char cls[256];

static void mkdirs(const char *path)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++)
        if (*p == '/') { *p = '\0'; mkdir(buf, 0755); *p = '/'; }
    mkdir(buf, 0755);
}

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

/* One card: its device bound to `driver`, `nconn` connectors beside it, and
 * the two boot flags wlroots reads. */
static void card(int minor, const char *driver, int nconn, int boot_display, int boot_vga)
{
    char p[512], t[512];
    snprintf(p, sizeof(p), "%s/card%d/device", cls, minor);
    mkdirs(p);
    snprintf(t, sizeof(t), "%s/bus/drivers/%s", root, driver);
    mkdirs(t);
    snprintf(p, sizeof(p), "%s/card%d/device/driver", cls, minor);
    symlink(t, p);
    snprintf(p, sizeof(p), "%s/card%d/boot_display", cls, minor);
    put(p, boot_display ? "1\n" : "0\n");
    snprintf(p, sizeof(p), "%s/card%d/device/boot_vga", cls, minor);
    put(p, boot_vga ? "1\n" : "0\n");
    for (int i = 0; i < nconn; i++) {
        snprintf(p, sizeof(p), "%s/card%d-DP-%d", cls, minor, i + 1);
        mkdirs(p);
    }
}

static void reset(void)
{
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s/class' '%s/bus'", root, root);
    if (system(cmd) != 0) { fprintf(stderr, "reset failed\n"); exit(2); }
    mkdirs(cls);
    /* Entries a card scan must skip. */
    char p[512];
    snprintf(p, sizeof(p), "%s/renderD128", cls); mkdirs(p);
    snprintf(p, sizeof(p), "%s/version", cls);    put(p, "drm 1.1.0\n");
}

static void expect(const char *what, const char *want)
{
    char *got = drm_pick_devices(cls, "/dev/dri");
    int ok = (!want && !got) || (want && got && strcmp(want, got) == 0);
    if (!ok) {
        failures++;
        fprintf(stderr, "FAIL %s: want %s, got %s\n", what, want ? want : "(nothing)",
                got ? got : "(nothing)");
    } else {
        printf("ok   %s\n", what);
    }
    free(got);
}

int main(void)
{
    if (!mkdtemp(root)) { perror("mkdtemp"); return 2; }
    snprintf(cls, sizeof(cls), "%s/class/drm", root);

    reset();
    card(0, "simple-framebuffer", 1, 1, 0);
    card(1, "nvidia", 4, 1, 1);
    expect("simpledrm left beside nvidia_drm (the 2026-09-22 boot) → nvidia only", "/dev/dri/card1");

    reset();
    card(1, "nvidia", 4, 1, 1);
    expect("framebuffer evicted, as it should be → untouched", NULL);

    reset();
    card(0, "simple-framebuffer", 1, 1, 0);
    expect("framebuffer is the only display (VM, no GPU driver) → untouched", NULL);

    reset();
    card(0, "simple-framebuffer", 1, 1, 0);
    card(1, "nvidia", 0, 0, 1);
    expect("nvidia_drm modeset=0: no connectors → keep the framebuffer", NULL);

    reset();
    card(0, "efi-framebuffer", 1, 0, 0);
    card(1, "i915", 3, 0, 0);
    card(2, "nvidia", 2, 0, 1);
    expect("hybrid laptop + leftover efidrm → both GPUs, boot_vga one first",
           "/dev/dri/card2:/dev/dri/card1");

    reset();
    card(0, "vesa-framebuffer", 1, 0, 0);
    card(2, "amdgpu", 1, 0, 0);
    card(1, "i915", 1, 0, 0);
    expect("vesadrm, no boot flag on either GPU → minor order", "/dev/dri/card1:/dev/dri/card2");

    reset();
    card(0, "of-display", 1, 0, 0);
    card(1, "amdgpu", 1, 1, 0);
    expect("ofdrm is a firmware framebuffer too", "/dev/dri/card1");

    reset();
    card(0, "amdgpu", 2, 1, 1);
    card(1, "nvidia", 1, 0, 0);
    expect("two real GPUs, no framebuffer → untouched (wlroots decides)", NULL);

    char gone[300];
    snprintf(gone, sizeof(gone), "%s/nowhere", root);
    char *g = drm_pick_devices(gone, "/dev/dri");
    if (g) { failures++; fprintf(stderr, "FAIL missing sysfs dir returned %s\n", g); free(g); }
    else printf("ok   missing /sys/class/drm → untouched\n");

    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    if (system(cmd) != 0) fprintf(stderr, "cleanup of %s failed\n", root);
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
