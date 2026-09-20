/*
 * gpuload.c — see gpuload.h.
 *
 * The sysfs half is dependency-free and reads out of a directory the caller
 * names, so gpuload_test drives it off fixture files. The NVML half cannot be
 * unit tested without a card and is deliberately the second choice, reached
 * only when no sysfs counter exists and only when the caller asked for the real
 * machine.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/* _GNU_SOURCE comes from meson.build's add_project_arguments; defining it here
 * as well only earns a -Wmacro-redefined. fopen's "e" mode needs it. */
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "gpuload.h"

/* ── sysfs: amdgpu / i915 ─────────────────────────────────────────────── */

/* Read one whole-percent integer out of a file. 1 on success. */
static int read_pct(const char *path, unsigned *out)
{
    FILE *f = fopen(path, "re");
    if (!f) return 0;
    unsigned v = 0;
    int got = fscanf(f, "%u", &v);
    fclose(f);
    if (got != 1) return 0;
    /* A driver that reports past 100 is reporting nonsense; clamp rather than
     * propagate it, because every consumer compares against a percentage. */
    *out = v > 100 ? 100 : v;
    return 1;
}

static int sysfs_busy(const char *sys_dir, unsigned *out)
{
    char drm[512];
    snprintf(drm, sizeof(drm), "%s/class/drm", sys_dir);

    DIR *d = opendir(drm);
    if (!d) return 0;

    /* ⚠ SORTED ORDER IS NOT PROMISED by readdir, and "the first card" has to
     * mean the same thing twice running or the policy would follow whichever
     * card the directory happened to hand back first. So keep the lowest name
     * seen rather than the first. */
    char best[512];
    best[0] = '\0';

    const struct dirent *e;
    while ((e = readdir(d))) {
        /* cardN only: card0-DP-1 and friends are connectors, not devices. */
        if (strncmp(e->d_name, "card", 4) != 0) continue;
        const char *n = e->d_name + 4;
        if (!*n) continue;
        int digits = 1;
        for (const char *p = n; *p; p++)
            if (*p < '0' || *p > '9') { digits = 0; break; }
        if (!digits) continue;

        if (best[0] && strcmp(e->d_name, best) >= 0) continue;
        snprintf(best, sizeof(best), "%s", e->d_name);
    }
    closedir(d);

    if (!best[0]) return 0;

    /* drm[] and best[] are 512 each and the tail is 24 more, so 1024 is not
     * provably enough and -Wformat-truncation is right to say so. */
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s/device/gpu_busy_percent", drm, best);
    return read_pct(path, out);
}

/* ── NVML, via dlopen ─────────────────────────────────────────────────── */

/*
 * ⚠ THE TYPES ARE DECLARED HERE rather than pulled from nvml.h, which is part
 * of the CUDA toolkit and is not a build dependency of this daemon. Only three
 * entry points are needed and their ABI has been stable since NVML 2. A handle
 * is an opaque pointer; the utilisation struct is two unsigned ints, gpu first.
 */
typedef void *nvml_dev_t;
typedef struct { unsigned gpu; unsigned memory; } nvml_util_t;

static struct {
    int         tried;      /* we have attempted to load; do not attempt again */
    void       *lib;
    nvml_dev_t  dev;
    int (*get_util)(nvml_dev_t, nvml_util_t *);
} nv;

/* Load and initialise NVML once. 0 = unavailable, and it stays unavailable:
 * retrying a dlopen every twenty seconds for the life of a daemon on a machine
 * with no NVIDIA driver is pure waste. */
static int nvml_ready(void)
{
    if (nv.tried) return nv.lib != NULL;
    nv.tried = 1;

    nv.lib = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!nv.lib) return 0;

    int (*init)(void)                       = dlsym(nv.lib, "nvmlInit_v2");
    int (*by_index)(unsigned, nvml_dev_t *) = dlsym(nv.lib, "nvmlDeviceGetHandleByIndex_v2");
    nv.get_util = dlsym(nv.lib, "nvmlDeviceGetUtilizationRates");

    /* NVML_SUCCESS is 0. Anything else here — no driver, a driver newer than
     * the library, a container without /dev/nvidiactl — means no readings, and
     * the caller's other inputs carry the decision. */
    if (!init || !by_index || !nv.get_util || init() != 0 ||
        by_index(0, &nv.dev) != 0) {
        dlclose(nv.lib);
        nv.lib      = NULL;
        nv.get_util = NULL;
        return 0;
    }
    return 1;
}

static int nvml_busy(unsigned *out)
{
    if (!nvml_ready()) return 0;
    nvml_util_t u = { 0, 0 };
    if (nv.get_util(nv.dev, &u) != 0) return 0;
    *out = u.gpu > 100 ? 100 : u.gpu;
    return 1;
}

/* ── The answer ───────────────────────────────────────────────────────── */

void syn_gpuload_read(const char *sys_dir, syn_gpuload_t *out)
{
    out->busy_pct  = 0;
    out->available = 0;

    unsigned pct = 0;

    if (sysfs_busy(sys_dir ? sys_dir : "/sys", &pct)) {
        out->busy_pct  = pct;
        out->available = 1;
        return;
    }

    /* A caller that named a tree is testing, and must never reach the card. */
    if (sys_dir) return;

    if (nvml_busy(&pct)) {
        out->busy_pct  = pct;
        out->available = 1;
    }
}
