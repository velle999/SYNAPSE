#ifndef GPULOAD_H
#define GPULOAD_H
/*
 * gpuload.h — how hard the card is being WORKED, as opposed to how full it is.
 *
 * ⛔ THE THIRD THING A MACHINE RUNS OUT OF, AND THE ONE THIS DAEMON WAS BLIND
 * TO. offload.c already reads free VRAM, and sysload.c reads RAM and CPU. None
 * of them says anything about GPU *compute*. A card pinned at 100% with two
 * gigabytes of VRAM still free reads, to every other input here, as a machine
 * with nothing whatever wrong — which is exactly what it looked like on
 * 2026-09-20, when the desktop stuttered for two and a half minutes and the
 * policy's own remedy was what was doing it.
 *
 * ⚠ WHAT THIS IS FOR, AND WHAT IT IS NOT FOR. Shedding a layer does not reduce
 * GPU *load*: an idle synapd runs no kernels at all, it merely holds VRAM. So a
 * busy GPU is NOT a reason to shed — there is nothing to relieve. It is a
 * reason not to START A RELOAD, because destroy+reload is the single most
 * GPU-expensive thing this daemon ever does, and doing it while another
 * application has the card is how a policy meant to relieve contention becomes
 * the contention. See pressure.c's restore branch.
 *
 * ⚠ AND IT IS OFTEN US. A generation drives the card as hard as any game, so
 * "the GPU is busy" during inference says only that synapd is working. The
 * caller passes `busy` to pressure.c for exactly this, the same guard CPU
 * pressure already has, and the GPU rule is not allowed to fire while it is set.
 *
 * ⛔ ZERO IS "NOT MEASURED", never "measured, and quiet" — the convention every
 * input in pressure.h follows. A card whose utilisation cannot be read must
 * leave the decision to the other inputs rather than read as an idle one.
 *
 * SOURCES, in the order tried:
 *   1. sysfs `gpu_busy_percent` — amdgpu and i915 expose it, NVIDIA does not.
 *   2. NVML, via dlopen. ⚠ dlopen and NOT a link-time dependency: synapd builds
 *      and runs on a machine with no NVIDIA driver at all, and a hard DT_NEEDED
 *      on libnvidia-ml would end that. Nothing in depends= changes for this.
 *
 * ⛔ THE SYS DIRECTORY IS A PARAMETER, and passing one means sysfs ONLY. That
 * is what makes the suite hermetic: a test on a machine that happens to have an
 * NVIDIA card must not silently start measuring the real one. Same reasoning as
 * sysload.h's proc_dir, one step stricter.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

typedef struct {
    /* 0-100. Meaningful only when `available` is 1. */
    unsigned busy_pct;
    int      available;
} syn_gpuload_t;

/*
 * Fill `out`. Never fails: anything unreadable leaves available = 0.
 *
 * sys_dir NULL  — the real machine: sysfs under /sys, then NVML.
 * sys_dir set   — that tree, sysfs only, NVML never consulted.
 *
 * ⚠ FIRST CARD WINS, the same approximation inference_vram() already makes
 * when it takes the first GGML_BACKEND_DEVICE_TYPE_GPU. On a two-card box this
 * can read the one the model is not on; correlating them needs a PCI id the
 * ggml device API does not hand out, so the two stay consistent rather than
 * one of them being differently wrong.
 */
void syn_gpuload_read(const char *sys_dir, syn_gpuload_t *out);

#endif
