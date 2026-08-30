/*
 * hdr.c — drive a monitor in HDR10, on a compositor that cannot render HDR.
 *
 * ── The route, and why it is this one ────────────────────────────────────────
 *
 * The obvious way to output HDR is to composite in it: render into fp16, blend
 * in linear light, encode PQ on the way out. synui cannot. It composites
 * through scenefx's fx_renderer, which declares
 *
 *     renderer->wlr_renderer.features.output_color_transform = false;
 *
 * and ignores the field — so a colour transform handed to the SCENE is dropped
 * in silence. nightlight.c found that the hard way and documents it.
 *
 * What is left is the path nightlight took instead: the OUTPUT STATE, where the
 * DRM backend programs the transform into CRTC hardware AFTER blending, and an
 * image description tells the connector what the pixels leaving it mean. synui
 * keeps compositing 8-bit sRGB with every effect intact, and the display is
 * driven in HDR10 anyway.
 *
 * ⛔ A CRTC IS A GAMMA LUT — ONE CURVE PER CHANNEL, AND NOTHING ELSE. That is
 * the whole shape of what this file can do, and it was measured, not assumed:
 * `synctl hdr` on the desk reported tf_curve=no, matrix=no, pipeline=no and
 * pq_lut=yes. An inverse-EOTF object, a 3x3 matrix and a pipeline of them are
 * not expressible in that hardware, so the DRM backend falls back to the
 * renderer, the renderer is fx_renderer, and the commit fails validation. A
 * transfer function is per-channel, so sRGB→PQ bakes into a 1D LUT and goes in.
 *
 * ⚠ SO THE PRIMARIES ARE NOT CONVERTED HERE, AND MUST NOT BE CLAIMED TO BE. A
 * matrix mixes channels; no per-channel curve does. Sending sRGB-primaried
 * pixels while telling the display they are BT.2020 is what makes a desktop
 * come out over-saturated in HDR, and it is the one thing about this that a
 * user would notice and could not explain. The answer is to ask the connector
 * for sRGB primaries FIRST (hdr_probe below) and only fall back to BT.2020: an
 * HDR10 signal is entered by the PQ transfer function, not by the colour
 * volume, so a PQ image description with sRGB primaries is a perfectly ordinary
 * thing to send and leaves the gamut alone. Whichever is accepted, the
 * mastering display primaries say sRGB, because that is the truth about what
 * produced these pixels.
 *
 * ⛔ THERE IS ONE COLOUR-TRANSFORM SLOT PER OUTPUT, AND NIGHT LIGHT IS ALREADY
 * IN IT. wlr_output_state.color_transform is a single pointer. HDR cannot set
 * "its own" transform beside night light's — the second call replaces the
 * first, silently, and whichever ran last wins. So the warmth is not a separate
 * transform here: it is folded into the SDR→PQ curve before that curve is
 * built (hdr_build below), and every commit path asks THIS file for the one
 * answer rather than asking nightlight.c for half of it.
 *
 * ⚠ AND AN OUTPUT THAT WAS PUT INTO HDR HAS TO BE TAKEN BACK OUT EXPLICITLY.
 * An output state that does not set the image description leaves the connector
 * carrying whatever the last commit gave it — the same trap as committing a
 * NULL colour transform, which night light had to learn: turning the mode off
 * and leaving the field alone would leave the display in PQ receiving
 * sRGB-encoded pixels, which is a very dark, very wrong screen with no error
 * anywhere. hdr_touched marks an output that owes the display an SDR image
 * description, and the mode is not offered at all on a connector that will not
 * accept one (hdr_probe) — a mode you cannot leave is worse than one you never
 * had.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/render/color.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "synui.h"

/* ── The curve ───────────────────────────────────────────── */

/* SMPTE ST 2084 inverse EOTF: linear [0,1], where 1.0 is 10000 cd/m², to a PQ
 * code value. The constants are the standard's, spelled as ratios so they can
 * be checked against it rather than trusted as decimals. */
static double pq_encode(double linear)
{
    static const double m1 = 2610.0 / 16384.0;
    static const double m2 = 2523.0 / 4096.0 * 128.0;
    static const double c1 = 3424.0 / 4096.0;
    static const double c2 = 2413.0 / 4096.0 * 32.0;
    static const double c3 = 2392.0 / 4096.0 * 32.0;

    if (linear < 0.0) linear = 0.0;
    if (linear > 1.0) linear = 1.0;
    double y = pow(linear, m1);
    return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

/* sRGB EOTF: encoded [0,1] to linear [0,1]. */
static double srgb_to_linear(double v)
{
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

int hdr_white_clamp(int nits)
{
    if (nits <= 0) return HDR_SDR_WHITE_DEFAULT;
    if (nits < HDR_SDR_WHITE_MIN) return HDR_SDR_WHITE_MIN;
    if (nits > HDR_SDR_WHITE_MAX) return HDR_SDR_WHITE_MAX;
    return nits;
}

/*
 * The one transform an HDR output's state may carry: night light folded into
 * the SDR→PQ curve.
 *
 * ⚠ THE WHITE LEVEL IS THE WHOLE LOOK, and it is not a matter of taste that can
 * be left at "1.0". PQ is an ABSOLUTE scale — a code value means a number of
 * cd/m², not a fraction of the panel's maximum — so an SDR desktop mapped
 * straight into it with white at 1.0 would ask for 10000 cd/m² of white
 * background. 203 is BT.2408's reference SDR white and what every other
 * compositor maps a desktop to; the control exists because a display's own
 * idea of comfortable differs, and because SDR-on-HDR reads washed out or
 * blinding within about a stop of wrong.
 *
 * ⚠ NIGHT LIGHT IS APPLIED IN THE ENCODED DOMAIN, BEFORE LINEARISATION, which
 * is exactly where nightlight.c applies it when it owns the slot: it scales the
 * ramp rather than curving it, and that is what makes it read as a colour shift
 * instead of a contrast change. Folding it in after linearisation would be a
 * different — and visibly duller — warmth from the same setting.
 */
static struct wlr_color_transform *hdr_lut_build(size_t dim, const double k[3],
                                                 int white)
{
    if (dim < 2) return NULL;

    uint16_t *lut = calloc(3 * dim, sizeof *lut);
    if (!lut) return NULL;
    uint16_t *r = lut, *g = lut + dim, *b = lut + 2 * dim;
    uint16_t *ch[3] = { r, g, b };

    for (size_t i = 0; i < dim; i++) {
        double enc = (double)i / (double)(dim - 1);
        for (int c = 0; c < 3; c++) {
            double lin = srgb_to_linear(enc * k[c]) * ((double)white / 10000.0);
            double v = pq_encode(lin) * (double)UINT16_MAX;
            ch[c][i] = (uint16_t)(v < 0.0 ? 0.0 : (v > 65535.0 ? 65535.0 : v));
        }
    }

    struct wlr_color_transform *tf = wlr_color_transform_init_lut_3x1d(dim, r, g, b);
    free(lut);
    return tf;
}

/*
 * Cached, for the reason nightlight.c's cache exists and one more.
 *
 * Both commit paths ask for the transform on EVERY frame they take, and wlroots
 * elides a redundant colour-transform commit by comparing the POINTER
 * (output_compare_state_fields: output->color_transform == state->color_transform).
 * A freshly built transform each frame would therefore re-upload the GAMMA_LUT
 * blob to the kernel at the refresh rate while looking identical. And this LUT
 * is not cheap to build: three pow() calls per entry per channel, so ~12000 of
 * them at NVIDIA's 1024 entries.
 *
 * Keyed by every input, because all three move independently: the LUT length is
 * the hardware's (see nightlight.c — a hard-coded 1024 is why night light did
 * nothing on the laptop), the white level is a user control, and the
 * temperature changes on a timer.
 */
#define HDR_CACHE 4

static struct {
    size_t dim;
    int    temp;
    int    white;
    struct wlr_color_transform *tf;
} g_hdr[HDR_CACHE];

static void hdr_cache_flush(void)
{
    for (int i = 0; i < HDR_CACHE; i++) {
        if (g_hdr[i].tf) wlr_color_transform_unref(g_hdr[i].tf);
        g_hdr[i].tf = NULL;
        g_hdr[i].dim = 0;
    }
}

/* Borrowed, not owned — see the header declaration. NULL if the LUT could not
 * be built, which the caller must treat as "no HDR this frame", never as
 * identity: identity here is an SDR ramp on a display that has been told it is
 * receiving PQ. */
static struct wlr_color_transform *hdr_build(syn_server_t *s, size_t dim, int white)
{
    int temp = nightlight_effective_temp(s);

    int slot = -1;
    for (int i = 0; i < HDR_CACHE; i++) {
        if (g_hdr[i].tf && g_hdr[i].dim == dim &&
            g_hdr[i].temp == temp && g_hdr[i].white == white)
            return g_hdr[i].tf;
        if (!g_hdr[i].tf && slot < 0) slot = i;
    }
    if (slot < 0) {   /* full: evict slot 0, as nightlight.c does */
        wlr_color_transform_unref(g_hdr[0].tf);
        g_hdr[0].tf = NULL;
        slot = 0;
    }

    double k[3] = { 1.0, 1.0, 1.0 };
    nightlight_channel_scale(s, k);

    struct wlr_color_transform *tf = hdr_lut_build(dim, k, white);
    if (!tf) {
        wlr_log(WLR_ERROR, "synui: hdr: could not build the SDR->PQ curve "
                "(%d cd/m2 white, %dK, %zu entries)", white, temp, dim);
        return NULL;
    }

    g_hdr[slot].dim   = dim;
    g_hdr[slot].temp  = temp;
    g_hdr[slot].white = white;
    g_hdr[slot].tf    = tf;
    wlr_log(WLR_DEBUG, "synui: hdr: built SDR->PQ at %d cd/m2 white, %dK, "
            "%zu entries", white, temp, dim);
    return tf;
}

/* ── What the pixels mean ────────────────────────────────── */

/*
 * The image description: what the connector is told about the signal it is
 * being handed. This is what actually puts the display into HDR — the PQ
 * transfer function is the switch; the curve above only makes the picture
 * correct once it has been thrown.
 *
 * ⚠ mastering_display_primaries SAYS sRGB WHATEVER `primaries` SAYS, and that
 * is not a contradiction: `primaries` describes the container this signal is
 * carried in, and the mastering display describes the screen the content was
 * made on. Every pixel synui composites was made on an sRGB desktop. A display
 * that reads the mastering metadata therefore has what it needs to avoid
 * stretching a Rec.709 gamut across BT.2020 in the case where BT.2020 is the
 * only container this connector would accept.
 *
 * ⚠ The luminances are the SDR desktop's, not the panel's. hdr_max_nits from
 * the EDID is what the MONITOR can do; claiming it as the mastering peak would
 * be telling the display that the taskbar was graded at 351 cd/m² when nothing
 * here emits above the SDR white level. Content light levels are the same
 * number for the same reason: an SDR desktop's brightest pixel IS white.
 */
static void hdr_desc_build(uint32_t primaries, int white,
                           struct wlr_output_image_description *out)
{
    memset(out, 0, sizeof *out);
    out->primaries = (enum wlr_color_named_primaries)primaries;
    out->transfer_function = WLR_COLOR_TRANSFER_FUNCTION_ST2084_PQ;
    wlr_color_primaries_from_named(&out->mastering_display_primaries,
                                   WLR_COLOR_NAMED_PRIMARIES_SRGB);
    out->mastering_luminance.min = 0.0;
    out->mastering_luminance.max = white;
    out->max_cll  = white;
    out->max_fall = white;
}

/* Plain sRGB — the description an output that has been in HDR must be given to
 * come back out. Not an absence of a description: see the file header. */
static void sdr_desc_build(struct wlr_output_image_description *out)
{
    memset(out, 0, sizeof *out);
    out->primaries = WLR_COLOR_NAMED_PRIMARIES_SRGB;
    out->transfer_function = WLR_COLOR_TRANSFER_FUNCTION_SRGB;
}

/* ── Asking the hardware ─────────────────────────────────── */

/* One test commit. ⛔ NOTHING HERE COMMITS: wlr_output_test_state() is
 * validated by the atomic backend against the kernel with TEST_ONLY, so the
 * screen never changes. Finding out by committing would mean putting a display
 * into HDR to learn whether it can be — which on half-supporting hardware is a
 * black screen on somebody's only monitor. */
static bool test_desc(struct wlr_output *wo,
                      const struct wlr_output_image_description *desc)
{
    struct wlr_output_state st;
    wlr_output_state_init(&st);
    bool ok = wlr_output_state_set_image_description(&st, desc) &&
              wlr_output_test_state(wo, &st);
    wlr_output_state_finish(&st);
    return ok;
}

static bool test_transform(struct wlr_output *wo, struct wlr_color_transform *tf)
{
    if (!tf) return false;
    struct wlr_output_state st;
    wlr_output_state_init(&st);
    wlr_output_state_set_color_transform(&st, tf);
    bool ok = wlr_output_test_state(wo, &st);
    wlr_output_state_finish(&st);
    return ok;
}

void hdr_probe(syn_server_t *s, syn_output_t *o)
{
    if (!o || !o->wlr_output) return;
    struct wlr_output *wo = o->wlr_output;

    o->hdr_capable    = 0;
    o->hdr_primaries  = 0;
    o->hdr_lut_ok     = 0;
    o->hdr_sdr_ok     = 0;

    /*
     * sRGB primaries first, BT.2020 only as a fallback — see the file header.
     * The order is the gamut policy, and it is the only place it is decided.
     */
    static const uint32_t candidates[] = {
        WLR_COLOR_NAMED_PRIMARIES_SRGB,
        WLR_COLOR_NAMED_PRIMARIES_BT2020,
    };
    struct wlr_output_image_description desc;
    for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
        hdr_desc_build(candidates[i], hdr_white_clamp(o->hdr_white), &desc);
        if (test_desc(wo, &desc)) { o->hdr_primaries = candidates[i]; break; }
    }
    if (!o->hdr_primaries) return;

    /* The curve, at this connector's own gamma size. */
    o->hdr_lut_ok = test_transform(wo,
        hdr_build(s, nightlight_lut_dim(wo), hdr_white_clamp(o->hdr_white)));
    if (!o->hdr_lut_ok) return;

    /* And the way back out, which is a capability like any other. */
    sdr_desc_build(&desc);
    o->hdr_sdr_ok = test_desc(wo, &desc) ? 1 : 0;
    if (!o->hdr_sdr_ok) {
        wlr_log(WLR_INFO, "synui: hdr: %s takes an HDR10 signal but will not "
                "take a plain sRGB one back — not offering a mode that cannot "
                "be switched off", wo->name);
        return;
    }

    o->hdr_capable = 1;
    wlr_log(WLR_INFO, "synui: hdr: %s can be driven in HDR10 (%s primaries)",
            wo->name,
            o->hdr_primaries == WLR_COLOR_NAMED_PRIMARIES_SRGB ? "sRGB"
                                                               : "BT.2020");
}

/* ── The state the commit paths carry ────────────────────── */

/*
 * The single transform this output's state may carry.
 *
 * ⛔ SINGLE is the operative word — see the file header. Night light off and HDR
 * off is NULL, and committing NULL is how a screen gets its colour back: an
 * output state that leaves the field unset leaves the CRTC LUT holding whatever
 * the last frame put there. In HDR the answer is never NULL, because identity
 * there is an SDR ramp on a display that has been told it is receiving PQ —
 * which is a black screen with a picture faintly in it.
 *
 * Borrowed, not owned: the caller must not unref it, and it lives only until
 * the temperature, the LUT length or the white level moves.
 */
struct wlr_color_transform *hdr_color_transform(syn_server_t *s, syn_output_t *o)
{
    if (!o->hdr_on) return nightlight_color_transform(s, o->wlr_output);
    return hdr_build(s, nightlight_lut_dim(o->wlr_output),
                     hdr_white_clamp(o->hdr_white));
}

syn_color_state_t hdr_color_state(syn_server_t *s, syn_output_t *o)
{
    syn_color_state_t cs = {
        .temp  = nightlight_effective_temp(s),
        .dim   = nightlight_lut_dim(o->wlr_output),
        .hdr   = o->hdr_on ? 1 : 0,
        .white = hdr_white_clamp(o->hdr_white),
    };
    return cs;
}

bool hdr_color_state_eq(const syn_color_state_t *a, const syn_color_state_t *b)
{
    return a->temp == b->temp && a->dim == b->dim &&
           a->hdr == b->hdr && a->white == b->white;
}

/*
 * Put this output's whole colour pipeline onto `st` and commit it.
 *
 * ⚠ THE COLOUR BITS GO ON A COPY THAT IS EITHER COMMITTED OR THROWN AWAY.
 * A backend that refuses a colour transform fails the WHOLE commit, and on the
 * effects path that would be a dropped frame every frame rather than a missing
 * tint. So the copy is tested first; if the hardware says no, the plain state
 * goes out and the frame is not lost.
 *
 * Both commit paths call this and nothing else — synui_main.c's plain scene
 * commit and effects.c's post-process commit. There is one colour-transform
 * slot per output and one place that fills it, which is the point.
 */
bool hdr_commit(syn_server_t *s, syn_output_t *o, struct wlr_output_state *st)
{
    struct wlr_output *wo = o->wlr_output;
    syn_color_state_t want = hdr_color_state(s, o);

    /* Which description this output owes the display, if any. An output that
     * has never been in HDR is left alone entirely — DP-2 here refuses an HDR10
     * description and there is no reason to ask it for anything. */
    struct wlr_output_image_description desc;
    bool have_desc = false, leaving = false;
    if (o->hdr_on) {
        hdr_desc_build(o->hdr_primaries, want.white, &desc);
        have_desc = true;
    } else if (o->hdr_touched) {
        sdr_desc_build(&desc);
        have_desc = true;
        leaving = true;
    }

    struct wlr_color_transform *tf = hdr_color_transform(s, o);
    bool hdr_lost = o->hdr_on && !tf;

    /*
     * ⚠ A STATE IS ONLY FINISHED IF IT WAS STARTED. wlr_output_state_finish()
     * frees a damage region and a buffer reference; handing it a zeroed struct
     * that wlr_output_state_copy() never filled is not a no-op. So the copy's
     * success is the only thing that decides whether it is committed OR freed.
     */
    bool colour_ok = false, committed_try = false, ok = false;
    struct wlr_output_state try = {0};
    if (!hdr_lost && wlr_output_state_copy(&try, st)) {
        wlr_output_state_set_color_transform(&try, tf);
        if (!have_desc || wlr_output_state_set_image_description(&try, &desc))
            colour_ok = wlr_output_test_state(wo, &try);
        if (colour_ok) {
            ok = wlr_output_commit_state(wo, &try);
            committed_try = true;
        }
        wlr_output_state_finish(&try);
    }
    if (!committed_try) ok = wlr_output_commit_state(wo, st);

    if (!ok) return false;

    if (colour_ok) {
        o->committed_color = want;
        /* The display has been taken back to sRGB: the debt is paid, and the
         * next commit on this output can stop setting the field. */
        if (leaving) o->hdr_touched = 0;
        if (o->hdr_on) o->hdr_touched = 1;
        return true;
    }

    /*
     * Refused. Stamped anyway, and deliberately: a backend that says no once
     * says no to the same request every frame, and retrying it per frame would
     * mean testing — and logging — at the refresh rate. The next change of
     * temperature, LUT length, white level or HDR state asks again.
     */
    o->committed_color = want;

    if (o->hdr_on) {
        /*
         * ⚠ HDR IS DROPPED RATHER THAN LEFT HALF-APPLIED. If the pipeline was
         * refused while the mode was on, the display may still be in PQ from an
         * earlier frame with no curve in front of it — a very dark screen with
         * a picture faintly in it, and no error anywhere.
         *
         * ⛔ AND THE STAMP HAS TO SAY THE DISPLAY IS STILL IN HDR, because it
         * is. Stamping the state that was REFUSED would make output_frame()'s
         * guard compare equal on the very next frame, skip the commit, and
         * leave the display in PQ for good with the mode reading "off" — the
         * way out would never be sent.
         */
        wlr_log(WLR_ERROR, "synui: hdr: %s refused the HDR10 pipeline "
                "(%d cd/m2 white, %zu LUT entries) — switching HDR off so the "
                "display is not left in PQ with no curve", wo->name,
                want.white, want.dim);
        o->hdr_on = 0;
        o->committed_color.hdr = 1;
        wlr_output_schedule_frame(wo);
    } else if (leaving) {
        /* hdr_probe() refuses to offer the mode on a connector that will not
         * take an sRGB description back, so reaching here means the answer
         * changed under us. Say so: the screen is wrong and nothing else in
         * the system can tell why. */
        wlr_log(WLR_ERROR, "synui: hdr: %s will not take a plain sRGB image "
                "description — the display is stuck in PQ", wo->name);
        /* hdr_touched is deliberately left set, so the next real state change
         * tries again — and committed_color is deliberately left stamped, so
         * this does NOT become a full repaint every frame forever. A wrong
         * screen is bad; a wrong screen that also pins a GPU is worse. */
    } else if (want.temp) {
        wlr_log(WLR_ERROR, "synui: nightlight: %s will not take the %dK "
                "transform at %zu LUT entries — frame committed without it",
                wo->name, want.temp, want.dim);
    }
    return true;
}

/* ── The control ─────────────────────────────────────────── */

int hdr_set(syn_server_t *s, syn_output_t *o, int enable)
{
    if (!o || !o->wlr_output) return 0;
    struct wlr_output *wo = o->wlr_output;

    if (!enable) {
        if (!o->hdr_on) return 1;
        o->hdr_on = 0;
        /* hdr_touched stays set: the display is still in PQ until a frame goes
         * out carrying the sRGB description. */
        if (o->hdr_forced_deep) {
            dispcfg_set_deep_color(s, o, 0);
            o->hdr_forced_deep = 0;
        }
        wlr_output_schedule_frame(wo);
        wlr_log(WLR_INFO, "synui: hdr: %s back to SDR", wo->name);
        return 1;
    }

    if (!o->hdr_capable) {
        wlr_log(WLR_INFO, "synui: hdr: %s cannot be driven in HDR10", wo->name);
        return 0;
    }

    /*
     * ⚠ 10-BIT FIRST, AND THIS IS NOT A NICETY. PQ spends most of its code
     * range on the bottom two stops, which is exactly what makes it good for
     * shadows and exactly what makes 8 bits of it visibly banded — a desktop
     * gradient in 8-bit PQ shows steps a 10-bit one does not. The scanout
     * format is a separate commit from everything below (it is a modeset), it
     * is allowed to fail on a link without the bandwidth, and HDR is still
     * worth having when it does — so this is best-effort and remembered, so
     * that switching HDR off puts back a setting HDR switched on.
     */
    if (!o->deep_color && dispcfg_set_deep_color(s, o, 1))
        o->hdr_forced_deep = 1;

    /* The capability probe ran at output creation, and the white level has
     * moved since then in every case but the first. Ask again for exactly what
     * a frame is about to carry. */
    int white = hdr_white_clamp(o->hdr_white);
    size_t dim = nightlight_lut_dim(wo);
    struct wlr_output_image_description desc;
    hdr_desc_build(o->hdr_primaries, white, &desc);

    struct wlr_output_state st;
    wlr_output_state_init(&st);
    struct wlr_color_transform *tf = hdr_build(s, dim, white);
    bool ok = tf != NULL;
    if (ok) {
        wlr_output_state_set_color_transform(&st, tf);
        ok = wlr_output_state_set_image_description(&st, &desc) &&
             wlr_output_test_state(wo, &st);
    }
    wlr_output_state_finish(&st);

    if (!ok) {
        wlr_log(WLR_ERROR, "synui: hdr: %s refused HDR10 at %d cd/m2 white",
                wo->name, white);
        if (o->hdr_forced_deep) {
            dispcfg_set_deep_color(s, o, 0);
            o->hdr_forced_deep = 0;
        }
        return 0;
    }

    o->hdr_on = 1;
    o->hdr_white = white;
    /* Nothing is committed here. The frame below carries it — and it has to be
     * asked for, because an HDR change has no damage of its own: the pixels are
     * identical, only what the connector is told they mean has moved. */
    wlr_output_schedule_frame(wo);
    wlr_log(WLR_INFO, "synui: hdr: %s in HDR10 (%s primaries, %d cd/m2 SDR "
            "white, %s scanout)", wo->name,
            o->hdr_primaries == WLR_COLOR_NAMED_PRIMARIES_SRGB ? "sRGB"
                                                               : "BT.2020",
            white, o->deep_color ? "10-bit" : "8-bit");
    return 1;
}

int hdr_toggle(syn_server_t *s, syn_output_t *o)
{
    return hdr_set(s, o, o && o->hdr_on ? 0 : 1);
}

int hdr_set_white(syn_server_t *s, syn_output_t *o, int nits)
{
    if (!o || !o->wlr_output) return 0;
    int white = hdr_white_clamp(nits);
    if (white == hdr_white_clamp(o->hdr_white)) { o->hdr_white = white; return 1; }

    if (!o->hdr_on) {          /* nothing to test against: it takes effect on */
        o->hdr_white = white;  /* the next enable, which tests it then        */
        return 1;
    }

    int was = o->hdr_white;
    o->hdr_white = white;
    /* The cache is keyed by the white level, so this builds a new curve; the
     * commit path picks it up on the frame the state change asks for. A level
     * the hardware refuses is caught there, which switches HDR off rather than
     * leaving a wrong picture — so test it here instead, and keep the old one. */
    struct wlr_color_transform *tf =
        hdr_build(s, nightlight_lut_dim(o->wlr_output), white);
    struct wlr_output_image_description desc;
    hdr_desc_build(o->hdr_primaries, white, &desc);

    struct wlr_output_state st;
    wlr_output_state_init(&st);
    bool ok = tf != NULL;
    if (ok) {
        wlr_output_state_set_color_transform(&st, tf);
        ok = wlr_output_state_set_image_description(&st, &desc) &&
             wlr_output_test_state(o->wlr_output, &st);
    }
    wlr_output_state_finish(&st);

    if (!ok) { o->hdr_white = was; return 0; }
    wlr_output_schedule_frame(o->wlr_output);
    return 1;
}

/*
 * A new connector: ask what it can do, then give it back what outputs.conf
 * asked for.
 *
 * ⚠ AFTER the EDID and deep-colour probes and after output_persist_apply(),
 * which is why this is its own call rather than part of either. The saved flag
 * is read before the capability is known — outputs.conf is applied while the
 * output is still being built — so `hdr_want` is a request parked by the
 * persist layer and honoured here, once there is an answer to honour it with.
 */
void hdr_output_added(syn_server_t *s, syn_output_t *o)
{
    hdr_probe(s, o);
    if (!o->hdr_want) return;
    o->hdr_want = 0;
    if (!hdr_set(s, o, 1))
        wlr_log(WLR_INFO, "synui: hdr: %s was saved in HDR10 and will not take "
                "it now — left in SDR", o->wlr_output->name);
}

/* A connector is going away, or the session is ending. Nothing to commit — the
 * CRTC is being torn down — but the cache is process-wide and the transforms in
 * it are the only thing here that owns memory. */
void hdr_shutdown(void)
{
    hdr_cache_flush();
}
