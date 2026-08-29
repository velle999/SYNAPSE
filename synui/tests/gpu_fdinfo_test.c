/*
 * gpu_fdinfo_test.c — reading a DRM client's VRAM out of /proc/<pid>/fdinfo.
 *
 * ⛔ THIS IS THE HALF THE BUILD MACHINE CANNOT OTHERWISE REACH. The task
 * manager's VRAM column was NVIDIA-only: gpu_sample() returned straight after
 * the AMD device sample, so every row read "–" on a machine whose header line
 * was reporting an AMD card quite happily. The fix reads the figure out of DRM
 * fdinfo — and this desktop has an NVIDIA card, whose driver publishes no
 * `drm-*` memory keys at all (verified: its fdinfo carries none). So the parser
 * would ship having never once seen the input it exists to read.
 *
 * Fixtures are that input. They are the real shapes, and the point of each:
 *
 *   1. amdgpu's ORIGINAL key, `drm-memory-vram`.
 *   2. the LATER generic scheme, `drm-resident-vram0` beside
 *      `drm-total-vram0` — resident is what is in VRAM now, total counts what
 *      has been evicted too, and reporting the larger of the two as "using"
 *      overstates every process on a card under pressure.
 *   3. a driver that publishes only `drm-total-vram0`, where that IS the answer.
 *   4. a client with no memory keys at all — Intel's, i915 — which must read 0
 *      rather than whatever the last line happened to contain.
 *   5. MiB units, because the number is meaningless without them and a MiB
 *      figure taken for KiB is a thousand-fold lie in the one column people
 *      open this panel to read.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

unsigned long syn_fdinfo_vram_kb(const char *path, long *client_id);

static int failures;

#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            failures++;                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                       \
            fprintf(stderr, "\n");                              \
        }                                                       \
    } while (0)

static const char *write_fixture(const char *body)
{
    static char path[] = "/tmp/synui-fdinfo-XXXXXX";
    static char kept[sizeof(path)];
    snprintf(path, sizeof(path), "/tmp/synui-fdinfo-XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); exit(1); }
    if (write(fd, body, strlen(body)) < 0) { perror("write"); exit(1); }
    close(fd);
    snprintf(kept, sizeof(kept), "%s", path);
    return kept;
}

static unsigned long read_fixture(const char *body, long *id)
{
    const char *p = write_fixture(body);
    unsigned long kb = syn_fdinfo_vram_kb(p, id);
    unlink(p);
    return kb;
}

int main(void)
{
    long id = 0;

    /* 1. amdgpu, as it has always spelled it. */
    unsigned long kb = read_fixture(
        "pos:\t0\n"
        "flags:\t02100002\n"
        "drm-driver:\tamdgpu\n"
        "drm-client-id:\t42\n"
        "drm-memory-vram:\t131072 KiB\n"
        "drm-memory-gtt:\t65536 KiB\n", &id);
    CHECK(kb == 131072, "amdgpu drm-memory-vram read %lu, wanted 131072", kb);
    CHECK(id == 42, "client id read %ld, wanted 42", id);

    /* 2. ⛔ RESIDENT WINS OVER TOTAL. Total counts allocations that have been
     *    evicted to system memory; reporting it as "this process is using"
     *    overstates every client on a card under pressure. */
    kb = read_fixture(
        "drm-driver:\tamdgpu\n"
        "drm-client-id:\t7\n"
        "drm-total-vram0:\t524288 KiB\n"
        "drm-resident-vram0:\t131072 KiB\n", &id);
    CHECK(kb == 131072, "resident should win over total; read %lu", kb);

    /* 3. …but total is the answer where it is the only one published. */
    kb = read_fixture(
        "drm-driver:\tamdgpu\n"
        "drm-client-id:\t8\n"
        "drm-total-vram0:\t4096 KiB\n", &id);
    CHECK(kb == 4096, "total-only should be used; read %lu", kb);

    /* 4. A DRM client with no memory keys is 0, not noise. */
    id = 99;
    kb = read_fixture(
        "pos:\t0\n"
        "drm-driver:\ti915\n"
        "drm-client-id:\t3\n"
        "drm-engine-render:\t123456 ns\n", &id);
    CHECK(kb == 0, "a client with no memory keys read %lu, wanted 0", kb);
    CHECK(id == 3, "client id should still be read; got %ld", id);

    /* 5. ⚠ UNITS. */
    kb = read_fixture(
        "drm-client-id:\t1\n"
        "drm-resident-vram0:\t512 MiB\n", &id);
    CHECK(kb == 512UL * 1024, "MiB not converted: read %lu, wanted %lu",
          kb, 512UL * 1024);

    /* 6. A file that is not there answers 0 and does not crash — the process
     *    exited between the readdir and the open, which happens constantly on
     *    a walk of /proc. */
    id = 5;
    kb = syn_fdinfo_vram_kb("/proc/nonexistent-by-design/fdinfo/9", &id);
    CHECK(kb == 0, "a missing fdinfo read %lu, wanted 0", kb);

    if (failures) {
        fprintf(stderr, "gpu_fdinfo_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("gpu_fdinfo_test: all checks passed\n");
    return 0;
}
