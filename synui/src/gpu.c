/*
 * gpu.c — GPU telemetry for the task manager
 *
 * Two back ends, picked at runtime:
 *
 *   NVML    dlopen'd from libnvidia-ml.so.1. The NVIDIA driver ships the
 *           library but the headers only come with the CUDA toolkit, so the
 *           handful of entry points we need are declared here and looked up
 *           by name. That keeps this a runtime dependency, not a build one:
 *           synui still builds and runs on a machine with no NVIDIA card, and
 *           the task manager just reports no GPU.
 *
 *   amdgpu  sysfs (gpu_busy_percent, mem_info_vram_*, hwmon) for the device,
 *           and DRM fdinfo for the per-process column — see
 *           drm_fdinfo_procs(). That walk was left out for a while on the
 *           grounds that it is "a lot of syscalls for one panel"; the count is
 *           real and the cost is not (7-10 ms for 344 processes, at 1 Hz, only
 *           while the panel is open), and the column read "–" on every AMD
 *           machine in the meantime.
 *
 * Sampling is synchronous and only happens while the panel is open (~1 Hz).
 * NVML queries are local ioctls on an already-open handle, so this costs
 * well under a millisecond and does not need the poller thread that
 * synapd_mon.c uses for its socket round-trips.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

#include "synui.h"

/* ── NVML, declared by hand (see the file comment) ───────── */

typedef void *nvml_device_t;

/* nvmlReturn_t. Only the two values we branch on. */
#define NVML_SUCCESS             0
#define NVML_TEMPERATURE_GPU     0

typedef struct { unsigned int gpu, memory; } nvml_utilization_t;

/* v1 ABI of nvmlMemory_t — what the unversioned nvmlDeviceGetMemoryInfo
 * returns. Its `used` is simply total - free, so it counts the memory the
 * driver reserves for itself (~400 MiB on a 3060) as in use, and the panel
 * then disagrees with nvidia-smi by that much. */
typedef struct { unsigned long long total, free, used; } nvml_memory_t;

/* v2 splits that reservation out, and its `used` is the figure nvidia-smi
 * prints — which is what someone will check this panel against. The caller
 * stamps `version` so NVML can tell the two ABIs apart; the constant is
 * NVML's own NVML_STRUCT_VERSION(Memory, 2) macro, expanded. Old drivers
 * lack the symbol, so v1 stays as the fallback. */
typedef struct {
    unsigned int       version;
    unsigned long long total, reserved, free, used;
} nvml_memory_v2_t;
#define NVML_MEMORY_V2 ((unsigned int)(sizeof(nvml_memory_v2_t) | (2u << 24)))

/* v2 ABI of nvmlProcessInfo_t — what the _v3 process entry points expect.
 * Getting this wrong writes past the caller's array, so the two extra
 * instance-id fields matter even though we never read them. */
typedef struct {
    unsigned int       pid;
    unsigned long long usedGpuMemory;
    unsigned int       gpuInstanceId;
    unsigned int       computeInstanceId;
} nvml_process_info_t;

/* NVML reports "no per-process figure available" as an all-ones value rather
 * than an error, and it would otherwise render as 16 exabytes of VRAM. */
#define NVML_VALUE_NOT_AVAILABLE ((unsigned long long)-1)

struct nvml_api {
    void *lib;
    unsigned (*init)(void);
    unsigned (*shutdown)(void);
    unsigned (*count)(unsigned *);
    unsigned (*handle)(unsigned, nvml_device_t *);
    unsigned (*name)(nvml_device_t, char *, unsigned);
    unsigned (*util)(nvml_device_t, nvml_utilization_t *);
    unsigned (*mem)(nvml_device_t, nvml_memory_t *);
    unsigned (*mem_v2)(nvml_device_t, nvml_memory_v2_t *);
    unsigned (*temp)(nvml_device_t, unsigned, unsigned *);
    unsigned (*power)(nvml_device_t, unsigned *);
    unsigned (*gfx_procs)(nvml_device_t, unsigned *, nvml_process_info_t *);
    unsigned (*comp_procs)(nvml_device_t, unsigned *, nvml_process_info_t *);
};

static struct nvml_api nvml;
static nvml_device_t   nvml_dev[SYN_GPU_MAX];

/* Prefer the versioned symbol, fall back to the plain one: the _v2/_v3 names
 * are what a current driver implements, but the unversioned ones stay exported
 * for old binaries, and a very old driver only has those. */
static void *nvml_sym(const char *versioned, const char *plain)
{
    void *fn = dlsym(nvml.lib, versioned);
    return fn ? fn : dlsym(nvml.lib, plain);
}

static bool nvml_open(syn_server_t *s)
{
    nvml.lib = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!nvml.lib) return false;   /* no NVIDIA driver — not an error */

    nvml.init       = nvml_sym("nvmlInit_v2", "nvmlInit");
    nvml.shutdown   = dlsym(nvml.lib, "nvmlShutdown");
    nvml.count      = nvml_sym("nvmlDeviceGetCount_v2", "nvmlDeviceGetCount");
    nvml.handle     = nvml_sym("nvmlDeviceGetHandleByIndex_v2",
                               "nvmlDeviceGetHandleByIndex");
    nvml.name       = dlsym(nvml.lib, "nvmlDeviceGetName");
    nvml.util       = dlsym(nvml.lib, "nvmlDeviceGetUtilizationRates");
    nvml.mem        = dlsym(nvml.lib, "nvmlDeviceGetMemoryInfo");
    nvml.mem_v2     = dlsym(nvml.lib, "nvmlDeviceGetMemoryInfo_v2");
    nvml.temp       = dlsym(nvml.lib, "nvmlDeviceGetTemperature");
    nvml.power      = dlsym(nvml.lib, "nvmlDeviceGetPowerUsage");
    /* Process lists are optional: without them the per-process VRAM column is
     * empty, which is survivable. Everything else is required. */
    nvml.gfx_procs  = nvml_sym("nvmlDeviceGetGraphicsRunningProcesses_v3",
                               "nvmlDeviceGetGraphicsRunningProcesses_v2");
    nvml.comp_procs = nvml_sym("nvmlDeviceGetComputeRunningProcesses_v3",
                               "nvmlDeviceGetComputeRunningProcesses_v2");

    if (!nvml.init || !nvml.count || !nvml.handle || !nvml.name ||
        !nvml.util || !nvml.mem) {
        wlr_log(WLR_INFO, "synui: gpu: libnvidia-ml is missing entry points");
        dlclose(nvml.lib);
        nvml.lib = NULL;
        return false;
    }

    if (nvml.init() != NVML_SUCCESS) {
        wlr_log(WLR_INFO, "synui: gpu: nvmlInit failed");
        dlclose(nvml.lib);
        nvml.lib = NULL;
        return false;
    }

    unsigned n = 0;
    if (nvml.count(&n) != NVML_SUCCESS || n == 0) {
        if (nvml.shutdown) nvml.shutdown();
        dlclose(nvml.lib);
        nvml.lib = NULL;
        return false;
    }
    if (n > SYN_GPU_MAX) n = SYN_GPU_MAX;

    for (unsigned i = 0; i < n; i++) {
        if (nvml.handle(i, &nvml_dev[i]) != NVML_SUCCESS) break;
        syn_gpu_t *g = &s->gpu[s->gpu_n];
        memset(g, 0, sizeof(*g));
        if (nvml.name(nvml_dev[i], g->name, sizeof(g->name)) != NVML_SUCCESS)
            snprintf(g->name, sizeof(g->name), "NVIDIA GPU %u", i);
        s->gpu_n++;
    }

    wlr_log(WLR_INFO, "synui: gpu: NVML up, %d device(s), first is '%s'",
            s->gpu_n, s->gpu_n ? s->gpu[0].name : "?");
    return s->gpu_n > 0;
}

/* ── amdgpu sysfs ────────────────────────────────────────── */

/* Whole-file read of a small sysfs attribute as an integer. -1 if absent —
 * every one of these is optional, and which exist varies by driver version. */
static long sysfs_long(const char *dir, const char *attr)
{
    char path[768];
    snprintf(path, sizeof(path), "%s/%s", dir, attr);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long v = -1;
    if (fscanf(f, "%ld", &v) != 1) v = -1;
    fclose(f);
    return v;
}

/* hwmon lives at <device>/hwmon/hwmonN/ with an unpredictable N. */
static bool amd_hwmon_dir(const char *devdir, char *out, size_t n)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/hwmon", devdir);
    DIR *d = opendir(path);
    if (!d) return false;

    bool found = false;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "hwmon", 5) != 0) continue;
        snprintf(out, n, "%s/hwmon/%s", devdir, e->d_name);
        found = true;
        break;
    }
    closedir(d);
    return found;
}

static bool amd_open(syn_server_t *s)
{
    DIR *d = opendir("/sys/class/drm");
    if (!d) return false;

    struct dirent *e;
    while ((e = readdir(d)) && s->gpu_n < SYN_GPU_MAX) {
        /* cardN only — skip the cardN-HDMI-A-1 connector entries. */
        if (strncmp(e->d_name, "card", 4) != 0) continue;
        if (strchr(e->d_name, '-')) continue;

        char dev[320];
        snprintf(dev, sizeof(dev), "/sys/class/drm/%s/device", e->d_name);

        /* gpu_busy_percent is the amdgpu-specific attribute we key on: if it
         * is there, this is a card we can actually report utilization for. */
        if (sysfs_long(dev, "gpu_busy_percent") < 0) continue;

        syn_gpu_t *g = &s->gpu[s->gpu_n];
        memset(g, 0, sizeof(*g));
        snprintf(g->name, sizeof(g->name), "AMD GPU (%.40s)", e->d_name);
        snprintf(g->sysfs, sizeof(g->sysfs), "%.240s", dev);
        s->gpu_n++;
    }
    closedir(d);

    if (s->gpu_n)
        wlr_log(WLR_INFO, "synui: gpu: amdgpu sysfs, %d device(s)", s->gpu_n);
    return s->gpu_n > 0;
}

static void amd_sample(syn_gpu_t *g)
{
    long busy = sysfs_long(g->sysfs, "gpu_busy_percent");
    long used = sysfs_long(g->sysfs, "mem_info_vram_used");
    long tot  = sysfs_long(g->sysfs, "mem_info_vram_total");

    g->util         = busy >= 0 ? (int)busy : -1;
    g->vram_used_kb = used > 0 ? (unsigned long)(used / 1024) : 0;
    g->vram_total_kb = tot > 0 ? (unsigned long)(tot / 1024) : 0;

    g->temp_c = -1;
    g->power_w = -1;

    char hw[640];
    if (!amd_hwmon_dir(g->sysfs, hw, sizeof(hw))) return;

    long t = sysfs_long(hw, "temp1_input");           /* millidegrees */
    if (t > 0) g->temp_c = (int)(t / 1000);

    long p = sysfs_long(hw, "power1_average");        /* microwatts */
    if (p < 0) p = sysfs_long(hw, "power1_input");
    if (p > 0) g->power_w = (int)(p / 1000000);
}

/* ── Public API ──────────────────────────────────────────── */

void gpu_init(syn_server_t *s)
{
    s->gpu_n = 0;
    s->gpu_proc_n = 0;

    if (nvml_open(s)) return;
    if (amd_open(s))  return;

    wlr_log(WLR_INFO, "synui: gpu: no supported GPU telemetry available");
}

void gpu_finish(syn_server_t *s)
{
    if (nvml.lib) {
        if (nvml.shutdown) nvml.shutdown();
        dlclose(nvml.lib);
        memset(&nvml, 0, sizeof(nvml));
    }
    s->gpu_n = 0;
    s->gpu_proc_n = 0;
}

/* Fold one NVML process list into the per-pid VRAM table. A pid running both
 * graphics and compute work on the same device appears in both lists with the
 * same figure, so take the max rather than summing and double-counting it. */
static void gpu_add_procs(syn_server_t *s, nvml_process_info_t *info, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        if (info[i].usedGpuMemory == NVML_VALUE_NOT_AVAILABLE) continue;
        unsigned long kb = (unsigned long)(info[i].usedGpuMemory / 1024);

        int found = 0;
        for (int j = 0; j < s->gpu_proc_n; j++) {
            if (s->gpu_proc[j].pid != (pid_t)info[i].pid) continue;
            if (kb > s->gpu_proc[j].vram_kb) s->gpu_proc[j].vram_kb = kb;
            found = 1;
            break;
        }
        if (found) continue;

        if (s->gpu_proc_n >= SYN_GPU_PROC_MAX) return;
        s->gpu_proc[s->gpu_proc_n].pid     = (pid_t)info[i].pid;
        s->gpu_proc[s->gpu_proc_n].vram_kb = kb;
        s->gpu_proc_n++;
    }
}

/* ── Per-process VRAM on everything that is not NVIDIA ──────────────────────
 *
 * ⛔ THIS COLUMN WAS NVIDIA-ONLY, AND NOTHING SAID SO. gpu_sample() returned
 * straight after amd_sample() on the non-NVML path, so gpu_proc_n stayed 0 and
 * every row of the task manager's VRAM column read "–" — on a machine whose
 * header line was cheerfully reporting "AMD GPU (card1)  0.3 / 0.5G". Reported
 * from a ThinkPad: "vram isn't populating anywhere in task man".
 *
 * The header here used to say per-process VRAM "would mean walking every fd's
 * drm fdinfo, which is a lot of syscalls for one panel". The count is real —
 * measured on a busy desktop: 344 processes, 2240 file descriptors. The COST is
 * not: the whole walk takes 7-10 ms, at 1 Hz, and only while the panel is open.
 * That is under 1% of one core to fill a column that was previously blank.
 *
 * ⚠ DEDUPED BY drm-client-id, NOT BY fd. A process routinely holds the same DRM
 * client on several descriptors — dup'd, or one per EGL surface — and each one
 * reports the SAME allocation. Adding them up gives a browser six times its
 * real VRAM, which is worse than the blank column this replaces.
 *
 * ⚠ THREE SPELLINGS, BECAUSE THE KERNEL CHANGED ITS MIND. amdgpu's original
 * key is `drm-memory-vram`; the later generic scheme is `drm-resident-vram0`
 * (what is in VRAM now) alongside `drm-total-vram0` (what is allocated,
 * including what has been evicted). Resident is the honest answer to "what is
 * this process using", and total is the fallback where a driver publishes only
 * that. Which of the three exists depends on the kernel AND the driver, so all
 * are read and the resident figure wins.
 */
unsigned long syn_fdinfo_vram_kb(const char *path, long *client_id)
{
    FILE *f = fopen(path, "re");
    if (!f) return 0;

    unsigned long resident = 0, total = 0;
    char line[256];
    *client_id = -1;

    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "drm-client-id:", 14)) {
            *client_id = strtol(line + 14, NULL, 10);
            continue;
        }
        /* Every memory key is "<name>:\t<number> <unit>". The unit is KiB on
         * every driver that publishes these, but read it rather than assume:
         * a MiB figure taken for KiB is a thousand-fold lie in a column people
         * use to find what is eating the card. */
        const char *v = NULL;
        int is_resident = 0;
        if (!strncmp(line, "drm-resident-vram", 17)) { v = strchr(line, ':'); is_resident = 1; }
        else if (!strncmp(line, "drm-memory-vram", 15)) { v = strchr(line, ':'); is_resident = 1; }
        else if (!strncmp(line, "drm-total-vram", 14))   { v = strchr(line, ':'); }
        if (!v) continue;

        char unit[8] = "";
        unsigned long n = 0;
        if (sscanf(v + 1, "%lu %7s", &n, unit) < 1) continue;
        if (!strncasecmp(unit, "MiB", 3)) n *= 1024;
        else if (!strncasecmp(unit, "GiB", 3)) n *= 1024 * 1024;
        else if (!strncasecmp(unit, "B", 1) && unit[1] == '\0') n /= 1024;

        if (is_resident) { if (n > resident) resident = n; }
        else             { if (n > total)    total    = n; }
    }
    fclose(f);
    return resident ? resident : total;
}

/* One process: every DRM client it holds, counted once each. */
static unsigned long proc_vram_kb(const char *pid)
{
    char dir[64];
    snprintf(dir, sizeof(dir), "/proc/%s/fd", pid);
    DIR *d = opendir(dir);
    if (!d) return 0;                       /* another user's, or already gone */

    /* Small and fixed: a process with more than this many DISTINCT drm clients
     * is not a thing, and a bound here is what stops a hostile /proc from
     * turning one row of a panel into an unbounded loop. */
    long seen[32];
    int nseen = 0;
    unsigned long kb = 0;

    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;

        char link[320], tgt[256];
        snprintf(link, sizeof(link), "/proc/%s/fd/%s", pid, e->d_name);
        ssize_t n = readlink(link, tgt, sizeof(tgt) - 1);
        if (n <= 0) continue;
        tgt[n] = '\0';
        if (!strstr(tgt, "/dev/dri/")) continue;

        char fdi[320];
        snprintf(fdi, sizeof(fdi), "/proc/%s/fdinfo/%s", pid, e->d_name);
        long id = -1;
        unsigned long got = syn_fdinfo_vram_kb(fdi, &id);
        if (!got) continue;

        int dup = 0;
        for (int i = 0; i < nseen; i++)
            if (seen[i] == id && id >= 0) { dup = 1; break; }
        if (dup) continue;
        if (nseen < (int)(sizeof(seen) / sizeof(*seen))) seen[nseen++] = id;

        kb += got;
    }
    closedir(d);
    return kb;
}

static void drm_fdinfo_procs(syn_server_t *s)
{
    DIR *proc = opendir("/proc");
    if (!proc) return;

    struct dirent *e;
    while ((e = readdir(proc))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        if (s->gpu_proc_n >= SYN_GPU_PROC_MAX) break;

        unsigned long kb = proc_vram_kb(e->d_name);
        if (!kb) continue;

        s->gpu_proc[s->gpu_proc_n].pid     = (pid_t)strtol(e->d_name, NULL, 10);
        s->gpu_proc[s->gpu_proc_n].vram_kb = kb;
        s->gpu_proc_n++;
    }
    closedir(proc);
}

void gpu_sample(syn_server_t *s)
{
    s->gpu_proc_n = 0;

    if (!nvml.lib) {
        for (int i = 0; i < s->gpu_n; i++) amd_sample(&s->gpu[i]);
        /* ⚠ AND THE PER-PROCESS COLUMN, which used to stop at the return above
         * — see drm_fdinfo_procs(). NVIDIA is excluded because its driver
         * publishes no drm-memory keys at all: NVML is where that answer lives,
         * and the branch below is the one that asks. */
        drm_fdinfo_procs(s);
        return;
    }

    for (int i = 0; i < s->gpu_n; i++) {
        syn_gpu_t *g = &s->gpu[i];

        nvml_utilization_t u;
        g->util = (nvml.util(nvml_dev[i], &u) == NVML_SUCCESS) ? (int)u.gpu : -1;

        g->vram_used_kb = g->vram_total_kb = 0;

        nvml_memory_v2_t m2 = { .version = NVML_MEMORY_V2 };
        nvml_memory_t m;
        if (nvml.mem_v2 && nvml.mem_v2(nvml_dev[i], &m2) == NVML_SUCCESS) {
            g->vram_used_kb  = (unsigned long)(m2.used  / 1024);
            g->vram_total_kb = (unsigned long)(m2.total / 1024);
        } else if (nvml.mem(nvml_dev[i], &m) == NVML_SUCCESS) {
            g->vram_used_kb  = (unsigned long)(m.used  / 1024);
            g->vram_total_kb = (unsigned long)(m.total / 1024);
        }

        unsigned t = 0;
        g->temp_c = (nvml.temp &&
                     nvml.temp(nvml_dev[i], NVML_TEMPERATURE_GPU, &t) == NVML_SUCCESS)
                        ? (int)t : -1;

        unsigned mw = 0;
        g->power_w = (nvml.power &&
                      nvml.power(nvml_dev[i], &mw) == NVML_SUCCESS)
                         ? (int)((mw + 500) / 1000) : -1;

        /* Both calls take the array size in *count and overwrite it with how
         * many they wrote. They fail with INSUFFICIENT_SIZE if the array is
         * too small; a machine with more than 64 GPU clients would just lose
         * the tail of the table, which the panel can live with. */
        nvml_process_info_t info[64];
        unsigned n;

        if (nvml.gfx_procs) {
            n = sizeof(info) / sizeof(info[0]);
            if (nvml.gfx_procs(nvml_dev[i], &n, info) == NVML_SUCCESS)
                gpu_add_procs(s, info, n);
        }
        if (nvml.comp_procs) {
            n = sizeof(info) / sizeof(info[0]);
            if (nvml.comp_procs(nvml_dev[i], &n, info) == NVML_SUCCESS)
                gpu_add_procs(s, info, n);
        }
    }
}

unsigned long gpu_proc_vram_kb(syn_server_t *s, pid_t pid)
{
    for (int i = 0; i < s->gpu_proc_n; i++)
        if (s->gpu_proc[i].pid == pid) return s->gpu_proc[i].vram_kb;
    return 0;
}
