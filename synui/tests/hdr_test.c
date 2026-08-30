/*
 * hdr_test.c — the curve that puts an SDR desktop on an absolute scale
 *
 * WHAT THIS PINS, and why each of them is a bug that would ship silently:
 *
 *   THE WHITE LEVEL IS NOT COSMETIC. PQ is an absolute encoding: a code value
 *   is a number of cd/m², not a fraction of the panel's peak. An SDR desktop
 *   mapped into it with white left at 1.0 is a request for 10000 cd/m² of
 *   white background, and every monitor's answer to that is its own. The curve
 *   has to land full-scale sRGB white on BT.2408's 203 cd/m², and that is one
 *   number that can be checked against the standard rather than by eye.
 *
 *   NIGHT LIGHT HAS TO SURVIVE THE FOLD. There is one colour-transform slot per
 *   output (wlr_output_state.color_transform is a single pointer), so in HDR
 *   the warmth cannot be a second transform — it is folded into the PQ curve
 *   before that curve is built. Nothing in the compositor would notice if the
 *   fold were dropped: the screen would simply stop going warm at night on
 *   whichever monitor was in HDR, with no error anywhere.
 *
 *   AND THE FOLD HAPPENS IN THE ENCODED DOMAIN, before linearisation, which is
 *   where nightlight.c applies it when it owns the slot. Applying it after
 *   linearisation is a different and visibly duller warmth from the same
 *   setting — so the two paths are checked against each other rather than
 *   against a constant.
 *
 *   THE TRANSFORM IS CACHED BY EVERY INPUT. Both commit paths ask for it on
 *   every frame they take, and wlroots elides a redundant colour-transform
 *   commit by comparing the POINTER — so a rebuild per frame would re-upload
 *   the GAMMA_LUT blob to the kernel at the refresh rate. Three inputs move
 *   independently (LUT length, temperature, white level) and a cache that
 *   misses one of them returns a stale curve for a setting the user just moved.
 *
 * wlr_output_get_gamma_size() is stubbed for the reason nightlight_test.c
 * stubs it: it is the one input that decides whether a transform is accepted at
 * all, and the DRM backend is not reachable from a test.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/render/color.h>
#include <wlr/types/wlr_output.h>

#include "synui.h"

static int failures;

#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            failures++;                                         \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);\
            fprintf(stderr, __VA_ARGS__);                       \
            fprintf(stderr, "\n");                              \
        }                                                       \
    } while (0)

/* ── the stubbed world ─────────────────────────────────────────────────────
 *
 * The executable's definitions win over the shared library's. Only the two
 * wlroots calls that reach hardware are replaced; the colour-transform maths
 * is the real wlroots implementation, because a LUT this test built and then
 * evaluated with its own arithmetic would be checking nothing. */
static size_t stub_gamma_size = 1024;
size_t wlr_output_get_gamma_size(struct wlr_output *output)
{
    (void)output;
    return stub_gamma_size;
}

static int scheduled;
void wlr_output_schedule_frame(struct wlr_output *output)
{
    (void)output;
    scheduled++;
}

/* Referenced by hdr_set()'s 10-bit request, which nothing here calls: enabling
 * HDR needs a backend to test against and there is none. */
int dispcfg_set_deep_color(syn_server_t *s, syn_output_t *o, int enable)
{
    (void)s; (void)o; (void)enable;
    return 0;
}

/* ── the reference curve ───────────────────────────────────────────────────
 *
 * Written out again rather than shared with hdr.c on purpose: a test that
 * imports the implementation's arithmetic asserts only that the code equals
 * itself. These are the constants as SMPTE ST 2084 and IEC 61966-2-1 state
 * them, and if hdr.c's copy is edited into disagreement with the standard this
 * is what says so. */
static double ref_pq(double linear)
{
    const double m1 = 2610.0 / 16384.0;
    const double m2 = 2523.0 / 4096.0 * 128.0;
    const double c1 = 3424.0 / 4096.0;
    const double c2 = 2413.0 / 4096.0 * 32.0;
    const double c3 = 2392.0 / 4096.0 * 32.0;
    double y = pow(linear, m1);
    return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

static double ref_srgb_to_linear(double v)
{
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

/* Two distinct non-NULL addresses. Nothing dereferences them — the gamma stub
 * is the only thing that ever looks at an output. */
static struct wlr_output panel, external;

static float eval1(struct wlr_color_transform *tf, float v, int channel)
{
    float out[3], in[3] = { v, v, v };
    wlr_color_transform_eval(tf, out, in);
    return out[channel];
}

int main(void)
{
    syn_server_t s = {0};
    wl_list_init(&s.outputs);
    s.config.night_light = false;
    s.config.night_light_temp = 3400;

    syn_output_t o = {0};
    o.wlr_output = &panel;

    /* ── the level, and its bounds ───────────────────────────────────── */
    /*
     * 0 means "never set" — a fresh output, or an outputs.conf written before
     * the field existed — and must read as the default rather than as a
     * request for no light at all.
     */
    CHECK(hdr_white_clamp(0) == HDR_SDR_WHITE_DEFAULT,
          "an unset white level did not read as the %d cd/m2 default",
          HDR_SDR_WHITE_DEFAULT);
    CHECK(hdr_white_clamp(HDR_SDR_WHITE_DEFAULT) == HDR_SDR_WHITE_DEFAULT,
          "the default did not survive clamping");
    CHECK(hdr_white_clamp(5) == HDR_SDR_WHITE_MIN, "below the floor was not clamped");
    CHECK(hdr_white_clamp(99999) == HDR_SDR_WHITE_MAX, "above the ceiling was not clamped");
    CHECK(hdr_white_clamp(-40) == HDR_SDR_WHITE_DEFAULT,
          "a negative level was not treated as unset");

    /* ── off is night light's answer, and nothing else ───────────────── */
    /*
     * There is one slot. With HDR off, whatever is in it has to be exactly what
     * it was before HDR existed, or turning the mode off would not put the
     * screen back.
     */
    CHECK(hdr_color_transform(&s, &o) == NULL,
          "HDR off with night light off produced a transform; identity is NULL");
    s.config.night_light = true;
    CHECK(hdr_color_transform(&s, &o) == nightlight_color_transform(&s, &panel),
          "HDR off did not hand back night light's own transform");
    s.config.night_light = false;

    /* ── white lands where 203 cd/m2 lands on PQ ─────────────────────── */
    o.hdr_on = 1;
    o.hdr_white = HDR_SDR_WHITE_DEFAULT;
    struct wlr_color_transform *hdr = hdr_color_transform(&s, &o);
    CHECK(hdr != NULL, "HDR on produced no transform");

    if (hdr) {
        double want = ref_pq((double)HDR_SDR_WHITE_DEFAULT / 10000.0);
        float got = eval1(hdr, 1.0f, 0);
        CHECK(fabs((double)got - want) < 0.01,
              "full-scale white encoded to %f, not %f — %d cd/m2 on PQ",
              (double)got, want, HDR_SDR_WHITE_DEFAULT);

        /* Black is black. PQ's floor is 0 and a curve that lifts it is a grey
         * desktop on an OLED, which is the most visible way this can be wrong. */
        CHECK(eval1(hdr, 0.0f, 0) < 0.001f,
              "black encoded to %f", (double)eval1(hdr, 0.0f, 0));

        /* Mid-grey, checked against the standard rather than against a shape:
         * sRGB 0.5 linearises to ~0.214, which is ~43 cd/m2 at 203 white. */
        double mid = ref_pq(ref_srgb_to_linear(0.5) * HDR_SDR_WHITE_DEFAULT / 10000.0);
        CHECK(fabs((double)eval1(hdr, 0.5f, 0) - mid) < 0.01,
              "mid-grey encoded to %f, not %f",
              (double)eval1(hdr, 0.5f, 0), mid);

        /* Monotone. A LUT that is not is a posterised desktop. */
        float prev = -1.0f;
        bool mono = true;
        for (int i = 0; i <= 64; i++) {
            float v = eval1(hdr, (float)i / 64.0f, 1);
            if (v < prev - 1e-4f) mono = false;
            prev = v;
        }
        CHECK(mono, "the SDR->PQ curve is not monotonically increasing");
    }

    /* ── the white level is the control it claims to be ──────────────── */
    o.hdr_white = 400;
    struct wlr_color_transform *bright = hdr_color_transform(&s, &o);
    CHECK(bright != NULL && bright != hdr,
          "a different white level reused the cached curve");
    if (bright && hdr)
        CHECK(eval1(bright, 1.0f, 0) > eval1(hdr, 1.0f, 0),
              "400 cd/m2 white was not brighter than %d (%f vs %f)",
              HDR_SDR_WHITE_DEFAULT, (double)eval1(bright, 1.0f, 0),
              (double)eval1(hdr, 1.0f, 0));
    o.hdr_white = HDR_SDR_WHITE_DEFAULT;

    /* ── night light survives the fold ───────────────────────────────── */
    /*
     * Asserted against night light's OWN answer at the same temperature, not
     * against a constant: the point is that the two paths warm the screen by
     * the same amount, so that a monitor in HDR and one beside it in SDR do not
     * go two different colours at sunset.
     */
    s.config.night_light = true;
    s.config.night_light_temp = 3400;
    struct wlr_color_transform *warm = hdr_color_transform(&s, &o);
    CHECK(warm != NULL, "HDR with night light on produced no transform");

    if (warm) {
        float r = eval1(warm, 1.0f, 0), b = eval1(warm, 1.0f, 2);
        CHECK(b < r, "3400K did not pull blue below red in HDR (r=%f b=%f)",
              (double)r, (double)b);

        /*
         * The fold is in the ENCODED domain: blue's full-scale output is the PQ
         * encoding of sRGB-linearised (1.0 x the blue multiplier), not of
         * (linear 1.0) x the multiplier. The two differ by the sRGB gamma and
         * are far more than a rounding apart — which is exactly why a fold put
         * in the wrong place looks plausible and is wrong.
         */
        double k[3];
        nightlight_channel_scale(&s, k);
        double encoded_fold =
            ref_pq(ref_srgb_to_linear(k[2]) * HDR_SDR_WHITE_DEFAULT / 10000.0);
        double linear_fold =
            ref_pq(k[2] * HDR_SDR_WHITE_DEFAULT / 10000.0);
        CHECK(fabs((double)b - encoded_fold) < 0.01,
              "blue encoded to %f; the encoded-domain fold is %f and the "
              "linear-domain one %f", (double)b, encoded_fold, linear_fold);
        CHECK(fabs(encoded_fold - linear_fold) > 0.01,
              "the two folds are indistinguishable here — this check proves "
              "nothing at %dK, pick a warmer one", s.config.night_light_temp);

        /* Red is untouched at 3400K, so HDR white must still be where it was.
         * A fold that dimmed everything would be a brightness change wearing
         * night light's name. */
        CHECK(fabs((double)r - ref_pq((double)HDR_SDR_WHITE_DEFAULT / 10000.0)) < 0.01,
              "night light moved red away from SDR white (%f)", (double)r);
    }
    s.config.night_light = false;

    /* ── cached on every input that can move ─────────────────────────── */
    struct wlr_color_transform *a = hdr_color_transform(&s, &o);
    CHECK(a == hdr_color_transform(&s, &o),
          "the same output, temperature and white level rebuilt the curve");

    /* A second LUT size lives beside the first: a hybrid laptop drives its
     * panel and its external screen from CRTCs that need not agree, and one
     * cached curve would be refused by whichever it was not built for. */
    syn_output_t o2 = {0};
    o2.wlr_output = &external;
    o2.hdr_on = 1;
    o2.hdr_white = HDR_SDR_WHITE_DEFAULT;
    stub_gamma_size = 4096;
    struct wlr_color_transform *big = hdr_color_transform(&s, &o2);
    CHECK(big != NULL && big != a, "a second LUT size did not get its own curve");
    stub_gamma_size = 1024;
    CHECK(hdr_color_transform(&s, &o) == a,
          "asking for the second size evicted the first");

    /* ── the commit guard sees every field ───────────────────────────── */
    /*
     * output_frame() damages a still screen only when this comparison says
     * something moved, and a colour change has no damage of its own. A field
     * left out of it is a setting that does nothing until something else
     * happens to repaint.
     */
    syn_color_state_t base = hdr_color_state(&s, &o);
    CHECK(hdr_color_state_eq(&base, &base), "a state did not equal itself");

    struct { const char *what; syn_color_state_t st; } moved[] = {
        { "night light",  base }, { "LUT length", base },
        { "HDR",          base }, { "white level", base },
    };
    moved[0].st.temp  = base.temp + 100;
    moved[1].st.dim   = base.dim * 2;
    moved[2].st.hdr   = !base.hdr;
    moved[3].st.white = base.white + HDR_SDR_WHITE_STEP;
    for (size_t i = 0; i < sizeof moved / sizeof moved[0]; i++)
        CHECK(!hdr_color_state_eq(&base, &moved[i].st),
              "a change of %s compared equal — the screen would not repaint",
              moved[i].what);

    /* And it reads the live state, not a copy taken at startup. */
    o.hdr_on = 0;
    CHECK(hdr_color_state(&s, &o).hdr == 0, "HDR off still read as on");
    o.hdr_on = 1;
    o.hdr_white = 0;
    CHECK(hdr_color_state(&s, &o).white == HDR_SDR_WHITE_DEFAULT,
          "an unset white level did not read as the default in the guard");

    hdr_shutdown();

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("hdr: all checks passed\n");
    return 0;
}
