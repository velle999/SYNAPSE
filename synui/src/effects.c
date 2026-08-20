/*
 * effects.c — GLES2 post-process pass for synui
 *
 * wlr_scene owns the render pass and exposes no shader hooks, so the
 * effects pipeline works around it: the scene is rendered into a private
 * offscreen swapchain (wlr_scene_output_build_state with a custom
 * swapchain), and that buffer is then drawn into the real output buffer
 * through a fullscreen-triangle GLES2 shader that applies CRT barrel
 * curvature, scanlines and chromatic aberration in one pass.
 *
 * Every failure path returns false and the caller falls back to the plain
 * wlr_scene_output_commit(), so a broken GL state can never black out the
 * session. On non-GLES2 renderers (pixman VMs) effects_init() refuses to
 * initialize and the fallback is permanent.
 *
 * EGL note: wlroots 0.19 doesn't export wlr_egl_make_current, so the pass
 * makes the renderer's context current with raw EGL and restores whatever
 * was current before. All GL work happens between those two points.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <wlr/render/egl.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include "fx_compat.h"
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "synui.h"
#include "effects.h"

struct syn_effects {
    EGLDisplay dpy;
    EGLContext ctx;
    /* [0] sampler2D, [1] samplerExternalOES (dmabuf-backed scene buffers) */
    GLuint prog[2];
    GLint  u_tex[2], u_scan[2], u_curv[2], u_aberr[2], u_size[2];
    GLint  u_time[2], u_glitch[2], u_mono[2], u_tint[2], u_swap[2], u_bloom[2];
    GLint  u_curve[2];

    /* Animation clocks (CLOCK_MONOTONIC seconds). While any of these is
     * live the frame pass keeps damaging + scheduling, so the animation
     * runs at output refresh. */
    double pulse_until;   /* L3: aberration ramp after a focus change */
    double close_until;   /* L2 (interim): brief glitch after a close  */

    int force_glitch;     /* SYNUI_EFFECTS_FORCE_GLITCH=1 (tests only) */

    /* scenefx's fx_renderer does not expose its EGL context (unlike wlroots'
     * gles2 renderer, whose wlr_egl we used to grab at init). So dpy/ctx and the
     * shader programs are captured LAZILY on the first frame, while a scenefx GL
     * op has the context current — see effects_output_commit. 0 until then. */
    int gl_ready;
};

#define FX_PULSE_SECS  0.25
#define FX_CLOSE_SECS  0.20

static double now_secs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static const char *vert_src =
    "attribute vec2 pos;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = pos * 0.5 + 0.5;\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "}\n";

/* %s is the sampler prelude; %s2 the sampler type. Curvature warps UVs,
 * out-of-bounds goes to black (the CRT bezel), aberration splits R/B
 * around the center, scanlines modulate physical output rows. */
static const char *frag_fmt =
    "%s"
    "precision highp float;\n"
    "#define SCAN_PITCH 3.0\n"
    "#define SCAN_DEPTH 0.55\n"
    /* Phosphor transfer curve, as (gamma, gain) in u_curve — driven by the
     * Phosphor lift slider, because where this curve should sit is a matter
     * of taste and not of fact, and it had been recompiled twice trying to
     * settle it by argument.
     *
     * gamma > 1 crushes the dim end: unlit phosphor is BLACK, and under
     * glass a window's backdrop is a blurred wallpaper at ~0.10 luminance,
     * not black, so a curve that LIFTS turns that haze into a screen-wide
     * tint. gain puts the driven end back up where a lit beam sits. Lower
     * gamma brings the unlit field back as a visible warm raster — the same
     * pixels either way, which is exactly why it is a knob. See
     * fx_phosphor_curve() for the two endpoints and where they came from.
     *
     * PH_HOT/PH_HOTMAX let a hard-driven dot outrun its own phosphor so the
     * core SATURATES with the tint surviving in the falloff. Toward full red
     * and green but NOT toward full blue — see the mix() below for why that
     * distinction is the difference between amber and cream. */
    "#define PH_HOT    0.55\n"
    "#define PH_HOTMAX 0.75\n"
    "#define BLOOM_THRESH 0.32\n"
    "varying vec2 v_uv;\n"
    "uniform %s u_tex;\n"
    "uniform float u_scan;\n"
    "uniform float u_curv;\n"
    "uniform float u_aberr;\n"
    "uniform float u_time;\n"
    "uniform float u_glitch;\n"
    "uniform float u_mono;\n"
    "uniform vec3 u_tint;\n"
    "uniform vec2 u_size;\n"
    /* 1.0 when the output is rotated 90/270 (portrait): the scanline and
     * glitch axes are keyed to the buffer, so they must swap to stay aligned
     * with what the viewer calls horizontal. */
    "uniform float u_swap;\n"
    "uniform float u_bloom;\n"
    "uniform vec2  u_curve;\n"   /* x = PH_GAMMA, y = PH_GAIN */
    "float phos(vec3 c) {\n"
    "    float lum = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "    return clamp(pow(lum, u_curve.x) * u_curve.y, 0.0, 1.0);\n"
    "}\n"
    "vec2 curve(vec2 uv) {\n"
    "    uv = uv * 2.0 - 1.0;\n"
    "    vec2 off = uv.yx * uv.yx * u_curv * 0.12;\n"
    "    uv += uv * off;\n"
    "    return uv * 0.5 + 0.5;\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = curve(v_uv);\n"
    "    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
    "        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    if (u_glitch > 0.0) {\n"
    "        /* 8px bands across the display's vertical, displaced along its\n"
    "         * horizontal; a per-band hash reseeded ~24x/sec decides which\n"
    "         * bands displace and by how much. On a portrait output both\n"
    "         * axes swap (u_swap) so the tearing still reads as horizontal. */\n"
    "        float along = mix(uv.y, uv.x, u_swap);\n"
    "        float dim   = mix(u_size.y, u_size.x, u_swap);\n"
    "        float band = floor(along * dim / 8.0);\n"
    "        float seed = band * 127.1 + floor(u_time * 24.0) * 311.7;\n"
    "        float h = fract(sin(seed) * 43758.5453);\n"
    "        float disp = (h - 0.5) * 0.10 * u_glitch * step(0.75, h);\n"
    "        if (u_swap > 0.5) uv.y = clamp(uv.y + disp, 0.0, 1.0);\n"
    "        else              uv.x = clamp(uv.x + disp, 0.0, 1.0);\n"
    "    }\n"
    "    vec2 d = (uv - 0.5) * u_aberr * 0.0035;\n"
    "    float r = texture2D(u_tex, uv - d).r;\n"
    "    vec4  g = texture2D(u_tex, uv);\n"
    "    float b = texture2D(u_tex, uv + d).b;\n"
    "    /* Scanlines run on gl_FragCoord, i.e. physical output rows: keyed\n"
    "     * off the curved uv instead they beat against the pixel grid and\n"
    "     * alias into a haze. SCAN_PITCH px per line (a 1px period is past\n"
    "     * what the eye resolves on a HiDPI panel - it just reads as dim),\n"
    "     * and the gain divisor holds mean brightness so the effect adds\n"
    "     * contrast rather than simply darkening the screen. */\n"
    "    float scanpos = mix(gl_FragCoord.y, gl_FragCoord.x, u_swap);\n"
    "    float beam = 0.5 + 0.5 * cos(scanpos * 6.2831853 / SCAN_PITCH);\n"
    "    float k    = u_scan * SCAN_DEPTH;\n"
    "    float scan = (1.0 - k * (1.0 - beam)) / (1.0 - 0.5 * k);\n"
    "    vec2 vg = uv * (1.0 - uv);\n"
    "    float vig = 1.0 - u_curv * 0.35 *\n"
    "                (1.0 - clamp(vg.x * vg.y * 18.0, 0.0, 1.0));\n"
    "    vec3 col = vec3(r, g.g, b) * scan * vig;\n"
    "    /* Monochrome phosphor: collapse to luminance and paint it in one tint.\n"
    "     * The curve is what makes it read as a lit tube rather than a colour\n"
    "     * wash — see PH_GAMMA. It matters most under glass: a window at\n"
    "     * foot_alpha 0 has a blurred wallpaper for a backdrop, and a curve\n"
    "     * that lifts turns that haze into a screen-wide tint. Beam\n"
    "     * saturation on top, or the brightest pixel the pass can emit is\n"
    "     * exactly u_tint and the text looks printed instead of lit. */\n"
    /*
     * ⚠ THE HOT CORE SATURATES TOWARD vec3(1.0, 1.0, u_tint.b), NOT TOWARD
     * WHITE, AND THE BLUE CHANNEL IS THE WHOLE POINT.
     *
     * This was `vec3(1.0)`. A phosphor driven hard does not start emitting
     * light it has no phosphor for — amber has essentially no blue in its
     * spectrum at any drive level, so a core that whitens has to get its blue
     * from somewhere the tube does not have. Mixing 75% toward white takes
     * amber's blue from 0.12 to 0.78, and the highlights come out CREAM.
     *
     * Measured, against a photograph of a real amber tube and a capture of this
     * filter (velle, 2026-08-19). Ratios rather than absolute colours, because
     * those survive exposure and backdrop:
     *
     *                       G/R     B/R
     *     real tube, hot    0.933   0.079     no blue, saturates to YELLOW
     *     ours, hot         0.934   0.737     cream        <- the fault
     *     predicted, white  0.925   0.780     confirms the cause
     *     predicted, this   0.925   0.120     #ffec1f vs the tube's #feed14
     *
     * The G behaviour was already right and is untouched: G/R climbing 0.70 ->
     * 0.925 is what makes a core read as driven rather than merely bright. Only
     * B was wrong, and it was wrong in the one direction a spectrum cannot go.
     *
     * Per-phosphor by construction, so the other two need no table: green keeps
     * its own low blue and saturates yellow-green (#d4ffcc -> #d4ff33), and
     * white, whose tint.b is already 0.92, barely moves (#fffdfa -> #fffdeb).
     *
     * ⚠ This is the HUE half only. "Too dark" is the Phosphor lift slider
     * (effect_lift, default 0.40) and is deliberately taste, not a constant.
     */
    "    if (u_mono > 0.0) {\n"
    "        float e = phos(col);\n"
    "        vec3 ph = mix(u_tint, vec3(1.0, 1.0, u_tint.b),\n"
    "                      smoothstep(PH_HOT, 1.0, e) * PH_HOTMAX) * e;\n"
    "        col = mix(col, ph, u_mono);\n"
    "    }\n"
    "    /* Phosphor bloom: a lit dot on a real tube spills a soft halo into its\n"
    "     * neighbours, so highlights glow instead of ending at a hard edge.\n"
    "     * Cheap single-pass take — sample three rings around the fragment,\n"
    "     * keep only what is bright (BLOOM_THRESH), and add it back in the\n"
    "     * phosphor tint. Gated on u_mono: no phosphor, nothing to bleed.\n"
    "     *\n"
    "     * The taps are thresholded on phos(), not on raw luminance, so the\n"
    "     * threshold means the same thing here as the tint does above. On raw\n"
    "     * luminance it does not: a merely bright wallpaper clears a raw\n"
    "     * threshold and blooms the whole screen, while text over glass sits\n"
    "     * under it and blooms nothing. The curve separates the two first. */\n"
    "    if (u_mono > 0.0 && u_bloom > 0.0) {\n"
    "        vec2 px = 1.0 / u_size;\n"
    "        float glow = 0.0;\n"
    "        for (int i = 0; i < 8; i++) {\n"
    "            float a = float(i) * 0.7853982;\n"
    "            vec2 dir = vec2(cos(a), sin(a));\n"
    "            float e1 = phos(texture2D(u_tex, uv + dir * px *  3.0).rgb);\n"
    "            float e2 = phos(texture2D(u_tex, uv + dir * px *  6.0).rgb);\n"
    "            float e3 = phos(texture2D(u_tex, uv + dir * px * 11.0).rgb);\n"
    "            glow += max(e1 - BLOOM_THRESH, 0.0)\n"
    "                  + 0.50 * max(e2 - BLOOM_THRESH, 0.0)\n"
    "                  + 0.25 * max(e3 - BLOOM_THRESH, 0.0);\n"
    "        }\n"
    "        glow *= 0.0714286;\n" /* /14 : ring weights sum to 8*(1+0.5+0.25) */
    "        col += u_tint * glow * u_bloom * 3.0 * u_mono;\n"
    "    }\n"
    "    gl_FragColor = vec4(col, g.a);\n"
    "}\n";

/* ── EGL context juggling ─────────────────────────────────── */

struct egl_saved {
    EGLDisplay dpy;
    EGLContext ctx;
    EGLSurface draw, read;
};

static bool fx_make_current(struct syn_effects *fx, struct egl_saved *save)
{
    save->dpy  = eglGetCurrentDisplay();
    save->ctx  = eglGetCurrentContext();
    save->draw = eglGetCurrentSurface(EGL_DRAW);
    save->read = eglGetCurrentSurface(EGL_READ);
    return eglMakeCurrent(fx->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, fx->ctx);
}

static void fx_restore(struct egl_saved *save)
{
    if (save->dpy != EGL_NO_DISPLAY)
        eglMakeCurrent(save->dpy, save->draw, save->read, save->ctx);
    else
        eglMakeCurrent(eglGetCurrentDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
}

/* ── Shader setup ─────────────────────────────────────────── */

static GLuint compile(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(sh, sizeof(log) - 1, NULL, log);
        wlr_log(WLR_ERROR, "effects: shader compile failed: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint build_program(const char *prelude, const char *sampler)
{
    char frag[8192];
    int need = snprintf(frag, sizeof(frag), frag_fmt, prelude, sampler);
    if (need < 0 || (size_t)need >= sizeof(frag)) {
        /* snprintf truncates in silence, and a shader cut mid-statement fails
         * to compile with an error pointing at the wrong thing entirely. */
        wlr_log(WLR_ERROR, "effects: shader source needs %d bytes, buffer is %zu",
                need, sizeof(frag));
        return 0;
    }

    GLuint vs = compile(GL_VERTEX_SHADER, vert_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "pos");
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetProgramInfoLog(prog, sizeof(log) - 1, NULL, log);
        wlr_log(WLR_ERROR, "effects: program link failed: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/* Compile the CRT programs and resolve their uniform locations. MUST run with
 * the fx_renderer's EGL context already current (the caller captures it first).
 * Returns false if the base program failed to build. */
static bool effects_gl_setup(struct syn_effects *fx)
{
    fx->prog[0] = build_program("", "sampler2D");
    /* scenefx doesn't export a check_ext (its header declares one the .so does
     * not ship), but our GL context is current here, so ask GL itself whether
     * external-OES samplers (needed for dmabuf-backed scene buffers) exist. */
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    if (exts && strstr(exts, "GL_OES_EGL_image_external"))
        fx->prog[1] = build_program(
            "#extension GL_OES_EGL_image_external : require\n",
            "samplerExternalOES");

    for (int i = 0; i < 2; i++) {
        if (!fx->prog[i]) continue;
        fx->u_tex[i]    = glGetUniformLocation(fx->prog[i], "u_tex");
        fx->u_scan[i]   = glGetUniformLocation(fx->prog[i], "u_scan");
        fx->u_curv[i]   = glGetUniformLocation(fx->prog[i], "u_curv");
        fx->u_aberr[i]  = glGetUniformLocation(fx->prog[i], "u_aberr");
        fx->u_size[i]   = glGetUniformLocation(fx->prog[i], "u_size");
        fx->u_time[i]   = glGetUniformLocation(fx->prog[i], "u_time");
        fx->u_glitch[i] = glGetUniformLocation(fx->prog[i], "u_glitch");
        fx->u_mono[i]   = glGetUniformLocation(fx->prog[i], "u_mono");
        fx->u_tint[i]   = glGetUniformLocation(fx->prog[i], "u_tint");
        fx->u_swap[i]   = glGetUniformLocation(fx->prog[i], "u_swap");
        fx->u_bloom[i]  = glGetUniformLocation(fx->prog[i], "u_bloom");
        fx->u_curve[i]  = glGetUniformLocation(fx->prog[i], "u_curve");
    }

    if (!fx->prog[0]) {
        wlr_log(WLR_ERROR, "effects: base CRT program failed to compile");
        return false;
    }
    wlr_log(WLR_INFO, "effects: GLES2 post-process ready (external-oes: %s)",
            fx->prog[1] ? "yes" : "no");
    return true;
}

bool effects_init(syn_server_t *s)
{
    s->effects = NULL;

    /* No renderer-type gate and no shader compile here anymore: scenefx's
     * fx_renderer isn't a wlr_gles2_renderer (wlr_renderer_is_gles2 is false)
     * and hides its EGL context, so we cannot grab a context at init. Allocate
     * the state now and defer all GL work to the first frame, where a scenefx
     * op makes the context current for us to capture (see gl_ready). */
    struct syn_effects *fx = calloc(1, sizeof(*fx));
    if (!fx) return false;

    const char *force = getenv("SYNUI_EFFECTS_FORCE_GLITCH");
    fx->force_glitch = force && strcmp(force, "1") == 0;
    fx->gl_ready = 0;

    s->effects = fx;
    wlr_log(WLR_INFO, "effects: post-process armed (GL setup deferred to first frame)");
    return true;
}

void effects_finish(syn_server_t *s)
{
    if (!s->effects) return;
    /* Programs die with the renderer's EGL context; just drop the state. */
    free(s->effects);
    s->effects = NULL;
}

void effects_output_destroy(syn_output_t *output)
{
    if (output->fx_swapchain) {
        wlr_swapchain_destroy(output->fx_swapchain);
        output->fx_swapchain = NULL;
    }
}

/* ── Animation triggers ───────────────────────────────────── */

void effects_notify_focus(syn_server_t *s)
{
    if (s->effects && s->config.effects)
        s->effects->pulse_until = now_secs() + FX_PULSE_SECS;
}

void effects_notify_close(syn_server_t *s)
{
    if (s->effects && s->config.effects)
        s->effects->close_until = now_secs() + FX_CLOSE_SECS;
}

/* L4: any mapped window under a synguard ALERT/DENY verdict puts the whole
 * screen in glitch mode for as long as the verdict stands. */
static bool any_view_alerted(syn_server_t *s)
{
    for (int i = 0; i < WORKSPACE_MAX; i++) {
        syn_view_t *v;
        wl_list_for_each(v, &s->workspaces[i].windows, link) {
            if (v->mapped && (v->security == WIN_SECURE_ALERT ||
                              v->security == WIN_SECURE_DENIED))
                return true;
        }
    }
    return false;
}

/* Per-frame effect strengths. Returns false when the pass has nothing to
 * do this frame; *animating is set while a clock is live so the caller
 * keeps frames coming. */
struct fx_params {
    float scan, curv, aberr, glitch;
    float mono, tint[3], bloom;
    float curve[2];          /* phosphor transfer (gamma, gain) */
    double time;
    bool animating;
};

/* The phosphor tints (RGB), indexed by syn_phosphor_t. A white pixel comes out
 * exactly this colour; darker pixels ramp toward black through it. */
/*
 * Measured on the mid-lit falloff — pixels with 0.30 < R < 0.70, which is where
 * the tint actually lives, the hot core having whitened away:
 *
 *     amber g=0.70      G/R 0.716   ← the look this desktop is fitted to
 *     amber g=0.48      G/R 0.485   ← reads red-orange, not amber
 *
 * Because tint.r is 1.0, a screenshot's RED channel recovers the shader's own
 * `e` exactly, so candidate tints can be re-rendered offline from any live
 * capture and compared — no rebuild, no live compositor. That is how these
 * numbers were reached; see project_synui_phosphor_crt_curve. Rendered G/R
 * comes out within 0.005 of tint.g, so the table reads directly.
 *
 * ⚠ 0.48 was fitted to a photograph of a real tube and shipped for one pkgrel.
 * It is the datasheet-honest answer and it is the WRONG one on an LCD: against
 * a screenshot of this desktop it reads red. Textbook P3 (#FFB000, g = 0.69) is
 * where it belongs. Fit the tint to the SCREENSHOT, not to the tube.
 */
static const float phosphor_tint[SYN_PHOSPHOR_COUNT][3] = {
    [SYN_PHOSPHOR_OFF]   = { 1.00f, 1.00f, 1.00f },   /* unused (mono 0) */
    /* P1, warmed: 0.25/0.30 put more BLUE than red in it, which reads faintly
     * minty. Real P1 sits a touch to the yellow side of green. */
    [SYN_PHOSPHOR_GREEN] = { 0.32f, 1.00f, 0.20f },   /* P1 CRT green */
    [SYN_PHOSPHOR_AMBER] = { 1.00f, 0.70f, 0.12f },   /* P3 amber, #FFB000 */
    [SYN_PHOSPHOR_WHITE] = { 1.00f, 0.97f, 0.92f },   /* P4 warm white */
};

/*
 * The Phosphor lift slider, as the (gamma, gain) pair phos() runs on.
 *
 * Both endpoints are measured, not invented. Every window on this desktop has a
 * blurred wallpaper for a backdrop sitting at ~0.10 luminance (foot_alpha 0),
 * and what that 0.10 comes out as is the whole difference between the two looks
 * this filter has had:
 *
 *     lift 0.0   gamma 2.2  gain 2.4   backdrop -> 0.016   dead black
 *     lift 1.0   gamma 1.2  gain 1.6   backdrop -> 0.103   the field glows
 *
 * There is nothing in the signal that separates a lit terminal backdrop from an
 * empty desktop — they are the same luminance — so no curve gets a glowing
 * raster under text without also tinting the bare desktop. Which of those two
 * you want is taste, and taste is what a slider is for. Interpolating linearly
 * in both terms keeps the mid-lit end roughly put (lum 0.5 -> 0.52..0.62 across
 * the range) while the dim end travels, which is the axis the eye reads.
 *
 * BLOOM_THRESH is safe across the whole range: even at lift 1.0 the backdrop
 * reaches 0.103 against a 0.32 threshold, so lifting the field never blooms it.
 */
static void fx_phosphor_curve(float lift, float out[2])
{
    if (lift < 0.0f) lift = 0.0f;
    if (lift > 1.0f) lift = 1.0f;
    out[0] = 2.2f - 1.0f * lift;   /* gamma */
    out[1] = 2.4f - 0.8f * lift;   /* gain  */
}

/*
 * The Phosphor hue slider, applied to the preset tint before it reaches the
 * shader as u_tint.
 *
 * Every previous round of this argument moved a number in phosphor_tint[] and
 * asked velle whether it was right yet: 0.70 read mustard, 0.48 read red, 0.70
 * came back and still reads too yellow. Where amber sits between orange and
 * yellow is taste and an LCD's, not a datasheet's, so it becomes a knob — the
 * same conclusion, and for the same reason, as the lift slider next to it.
 *
 * Rotation in HSV, saturation and value untouched, so the row turns the COLOUR
 * and nothing else: a phosphor at half the hue range is still as saturated and
 * as bright as the preset it came from, and the tint keeps reading directly as
 * the colour it makes. That matters downstream — the hot core saturates toward
 * vec3(1.0, 1.0, u_tint.b), so a tint that quietly gained value or lost
 * saturation here would move the highlight colour too.
 *
 * Amber, as the row sweeps it (the table entry is hue 39.5 degrees):
 *
 *     0.00   -60   hue 340    #ff1f6b   red, going pink
 *     0.35   -18   hue  22    #ff6f1f   deep orange
 *     0.45    -6   hue  34    #ff9c1f   orange amber   <- the default
 *     0.50     0   hue  40    #ffb21f   the table, P3 #FFB000
 *     0.65   +18   hue  58    #fff61f   yellow
 *     1.00   +60   hue 100    #6bff1f   yellow-green
 *
 * Grey is left alone by construction: max == min means the hue is undefined and
 * there is nothing to rotate, which is also why white (s = 0.08) barely moves.
 */
static void fx_phosphor_hue(float tint[3], float hue)
{
    if (hue < 0.0f) hue = 0.0f;
    if (hue > 1.0f) hue = 1.0f;
    float deg = (hue - 0.5f) * 2.0f * SYN_PHOSPHOR_HUE_RANGE;
    if (deg == 0.0f) return;

    float r = tint[0], g = tint[1], b = tint[2];
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float c = mx - mn;
    if (c <= 0.0f || mx <= 0.0f) return;   /* grey: no hue to turn */

    float h;
    if      (mx == r) h = fmodf((g - b) / c, 6.0f);
    else if (mx == g) h = (b - r) / c + 2.0f;
    else              h = (r - g) / c + 4.0f;
    h = h * 60.0f + deg;
    h = fmodf(h, 360.0f);
    if (h < 0.0f) h += 360.0f;

    /* Back to RGB at the same chroma and value. */
    float hp = h / 60.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float rr, gg, bb;
    if      (hp < 1.0f) { rr = c;    gg = x;    bb = 0.0f; }
    else if (hp < 2.0f) { rr = x;    gg = c;    bb = 0.0f; }
    else if (hp < 3.0f) { rr = 0.0f; gg = c;    bb = x;    }
    else if (hp < 4.0f) { rr = 0.0f; gg = x;    bb = c;    }
    else if (hp < 5.0f) { rr = x;    gg = 0.0f; bb = c;    }
    else                { rr = c;    gg = 0.0f; bb = x;    }
    tint[0] = rr + mn;
    tint[1] = gg + mn;
    tint[2] = bb + mn;
}

static bool fx_compute(syn_server_t *s, struct fx_params *p)
{
    if (!s->effects || !s->config.effects) return false;
    struct syn_effects *fx = s->effects;
    double now = now_secs();

    p->scan  = s->config.effect_scanline;
    p->curv  = s->config.effect_curvature;
    p->aberr = s->config.effect_aberration;
    p->time  = now;
    p->glitch = 0.0f;
    p->animating = false;

    /* Monochrome phosphor tint. OFF (or amount 0) leaves colour untouched. */
    int ph = s->config.effect_phosphor;
    if (ph < 0 || ph >= SYN_PHOSPHOR_COUNT) ph = SYN_PHOSPHOR_OFF;
    if (ph == SYN_PHOSPHOR_OFF) {
        p->mono = 0.0f;
    } else {
        p->mono = s->config.effect_mono;
        if (p->mono < 0.0f) p->mono = 0.0f;
        if (p->mono > 1.0f) p->mono = 1.0f;
    }
    p->tint[0] = phosphor_tint[ph][0];
    p->tint[1] = phosphor_tint[ph][1];
    p->tint[2] = phosphor_tint[ph][2];
    fx_phosphor_hue(p->tint, s->config.effect_hue);

    /* Bloom is the phosphor glow; the shader gates it on mono, so it goes
     * quiet on its own when there is no tint. */
    p->bloom = s->config.effect_bloom;
    if (p->bloom < 0.0f) p->bloom = 0.0f;
    if (p->bloom > 1.0f) p->bloom = 1.0f;

    fx_phosphor_curve(s->config.effect_lift, p->curve);

    /* L3: aberration ramps up on focus change and decays back. */
    if (fx->pulse_until > now) {
        float k = (float)((fx->pulse_until - now) / FX_PULSE_SECS);
        p->aberr += 1.5f * k;
        p->animating = true;
    }

    /* L2 (interim) + L4: brief glitch on close; sustained under alert. */
    float glitch_gain = s->config.effect_glitch;
    if (glitch_gain > 0.0f) {
        if (fx->force_glitch || any_view_alerted(s)) {
            p->glitch = glitch_gain;
            p->animating = true;
        } else if (fx->close_until > now) {
            p->glitch = glitch_gain * (float)((fx->close_until - now) / FX_CLOSE_SECS);
            p->animating = true;
        }
    }

    return p->scan > 0.0f || p->curv > 0.0f || p->aberr > 0.0f ||
           p->glitch > 0.0f || p->mono > 0.0f;
}

/* ── Frame pass ───────────────────────────────────────────── */

bool effects_output_commit(syn_output_t *output)
{
    syn_server_t *s = output->server;
    struct syn_effects *fx = s->effects;
    struct wlr_output *wo = output->wlr_output;

    struct fx_params prm;
    if (!fx_compute(s, &prm)) return false;

    /* The scene is rendered into fx_swapchain with the output transform
     * already baked in, and this pass samples buffer→buffer 1:1, so the image
     * comes out correct on rotated outputs without any UV remap. Only the
     * procedural effects keyed to buffer axes (scanlines, glitch) need to know
     * the panel is turned: on a 90/270 rotation their axis swaps so they still
     * read as horizontal to the viewer. Flips leave the axis unchanged. */
    enum wl_output_transform t = wo->transform;
    float swap = (t == WL_OUTPUT_TRANSFORM_90 ||
                  t == WL_OUTPUT_TRANSFORM_270 ||
                  t == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
                  t == WL_OUTPUT_TRANSFORM_FLIPPED_270) ? 1.0f : 0.0f;

    if (!wlr_output_configure_primary_swapchain(wo, NULL, &wo->swapchain))
        return false;

    int w = wo->swapchain->width, h = wo->swapchain->height;
    if (output->fx_swapchain &&
        (output->fx_swapchain->width != w || output->fx_swapchain->height != h)) {
        wlr_swapchain_destroy(output->fx_swapchain);
        output->fx_swapchain = NULL;
    }
    if (!output->fx_swapchain) {
        output->fx_swapchain = wlr_swapchain_create(s->allocator, w, h,
                                                    &wo->swapchain->format);
        if (!output->fx_swapchain) return false;
    }

    /* Render the scene into the offscreen swapchain.
     *
     * The CRT shader below samples the WHOLE fx_swapchain buffer (barrel warp
     * moves every pixel), so partial damage is never enough — any region the
     * scene left unpainted shows whatever stale frame that rotating buffer last
     * held. That bites hardest on the FIRST frame of a transient effect (the
     * focus-change aberration pulse): the offscreen path is entered cold, the
     * buffer still holds an old composited frame — most often the wallpaper from
     * before a game went fullscreen — and with little incidental damage
     * build_state leaves most of it untouched, so the shader presents a full
     * frame of stale wallpaper: a one-frame flash on every click/window switch.
     * Force whole damage up front so build_state always fully repaints the
     * buffer the shader is about to consume. (The tail add_whole only primed
     * frames 2..N.) */
    wlr_damage_ring_add_whole(&output->scene_output->damage_ring);

    struct wlr_output_state st;
    wlr_output_state_init(&st);
    /* Night light is NOT passed in here, deliberately. The scene is free to
     * satisfy a colour transform by programming the CRTC LUT, which it would
     * record on the state we are about to throw away (only st.buffer survives
     * below) — the warmth would silently vanish whenever the post-process pass
     * is on. It goes on the final state instead, after our shader, which is
     * also where the old hardware gamma ramp sat. */
    struct wlr_scene_output_state_options opts = {
        .swapchain = output->fx_swapchain,
    };
    if (!wlr_scene_output_build_state(output->scene_output, &st, &opts)) {
        wlr_output_state_finish(&st);
        return false;
    }
    if (!(st.committed & WLR_OUTPUT_STATE_BUFFER)) {
        /* Nothing rendered this frame (no damage) — commit as built. */
        bool ok = wlr_output_commit_state(wo, &st);
        wlr_output_state_finish(&st);
        return ok;
    }

    /* Keep our own lock on the scene buffer, then drop the built state so
     * buffer ownership stays unambiguous. */
    struct wlr_buffer *scene_buf = wlr_buffer_lock(st.buffer);
    wlr_output_state_finish(&st);

    struct wlr_texture *tex = fx_texture_from_buffer(s->renderer, scene_buf);
    if (!tex) goto fail_scene;

    struct wlr_buffer *dst = wlr_swapchain_acquire(wo->swapchain);
    if (!dst) goto fail_tex;

    GLuint fbo = fx_renderer_get_buffer_fbo(s->renderer, dst);
    if (!fbo) goto fail_dst;

    /* scenefx exposes no getter for the fx_renderer's EGL context, and its
     * helpers (get_buffer_fbo) save/restore the context around themselves, so
     * nothing is left current to capture. A render pass, however, holds the
     * context current between begin and submit — so open a throwaway pass on
     * dst once, grab dpy/ctx and compile the CRT programs inside it, then
     * submit. dst gets overwritten by our own raw-GL pass immediately below, so
     * the empty pass is harmless. */
    if (!fx->gl_ready) {
        /* scenefx 0.5 unexported fx_renderer_begin_buffer_pass (its
         * replacement takes an internal fx_framebuffer). The plain wlroots
         * entry point reaches the same fx_renderer hook and makes the same
         * context current, which is all this pass is for. */
        struct wlr_buffer_pass_options bpo = {0};
        struct wlr_render_pass *cap =
            wlr_renderer_begin_buffer_pass(s->renderer, dst, &bpo);
        if (!cap) goto fail_dst;
        fx->dpy = eglGetCurrentDisplay();
        fx->ctx = eglGetCurrentContext();
        bool ok = fx->dpy != EGL_NO_DISPLAY && fx->ctx != EGL_NO_CONTEXT &&
                  effects_gl_setup(fx);
        wlr_render_pass_submit(cap);
        if (!ok) {
            wlr_log(WLR_ERROR, "effects: could not capture fx EGL context / "
                    "compile programs — post-process off");
            goto fail_dst;
        }
        fx->gl_ready = 1;
    }

    struct egl_saved saved;
    if (!fx_make_current(fx, &saved)) goto fail_dst;

    struct fx_texture_attribs ta;
    fx_texture_get_attribs(tex, &ta);
    int p = (ta.target == GL_TEXTURE_EXTERNAL_OES) ? 1 : 0;
    if (!fx->prog[p]) {
        fx_restore(&saved);
        goto fail_dst;
    }

    static const GLfloat tri[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(fx->prog[p]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(ta.target, ta.tex);
    glTexParameteri(ta.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(ta.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(ta.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(ta.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(fx->u_tex[p], 0);
    glUniform1f(fx->u_scan[p],   prm.scan);
    glUniform1f(fx->u_curv[p],   prm.curv);
    glUniform1f(fx->u_aberr[p],  prm.aberr);
    glUniform1f(fx->u_glitch[p], prm.glitch);
    glUniform1f(fx->u_mono[p],   prm.mono);
    glUniform3f(fx->u_tint[p],   prm.tint[0], prm.tint[1], prm.tint[2]);
    glUniform1f(fx->u_swap[p],   swap);
    glUniform1f(fx->u_bloom[p],  prm.bloom);
    glUniform2f(fx->u_curve[p],  prm.curve[0], prm.curve[1]);
    /* Wrapped so float precision stays fine on long uptimes. */
    glUniform1f(fx->u_time[p], (GLfloat)fmod(prm.time, 3600.0));
    glUniform2f(fx->u_size[p], (GLfloat)w, (GLfloat)h);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(0);
    glBindTexture(ta.target, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    /* The buffer is scanned out by another context/device; make sure the
     * pass is finished before commit. */
    glFinish();

    fx_restore(&saved);

    /* Commit the post-processed buffer with full damage (the warp moves
     * pixels, so partial damage from the scene doesn't hold). */
    struct wlr_output_state st2;
    wlr_output_state_init(&st2);
    wlr_output_state_set_buffer(&st2, dst);
    pixman_region32_t full;
    pixman_region32_init_rect(&full, 0, 0, w, h);
    wlr_output_state_set_damage(&st2, &full);
    pixman_region32_fini(&full);

    /* Night light, applied at scanout on top of the post-processed buffer —
     * the same place the pre-0.20 gamma ramp was applied, so a colour filter
     * and night light still compose in the order they always did.
     *
     * NULL — night light off — is committed too, and that is the whole point:
     * a state that leaves WLR_OUTPUT_STATE_COLOR_TRANSFORM unset leaves the
     * CRTC LUT exactly as the last warm frame programmed it. Skipping the call
     * on NULL gave a toggle that turned on and never off for as long as the
     * post-process pass was running. Committing NULL is committing identity.
     *
     * Tested on a copy first, as in scene_commit_nightlight(): a backend that
     * refuses the transform fails the WHOLE commit, which here would mean a
     * dropped frame every frame rather than a missing tint. */
    int temp = nightlight_effective_temp(s);
    struct wlr_color_transform *nl = nightlight_color_transform(s);
    bool warm_ok = true;
    struct wlr_output_state warm = {0};
    if (wlr_output_state_copy(&warm, &st2)) {
        wlr_output_state_set_color_transform(&warm, nl);
        warm_ok = wlr_output_test_state(wo, &warm);
        if (warm_ok)
            wlr_output_state_copy(&st2, &warm);
        wlr_output_state_finish(&warm);
    } else {
        warm_ok = false;
    }

    bool ok = wlr_output_commit_state(wo, &st2);
    wlr_output_state_finish(&st2);

    if (ok) {
        if (!warm_ok)
            wlr_log(WLR_ERROR, "synui: nightlight: %s will not take the %dK "
                    "transform — frame committed without it", wo->name, temp);
        /* Stamped for the same reason the scene path stamps it, and with the
         * same "asked once" rule for a refusal. Leaving it unstamped is not
         * merely untidy: output_frame() damages the whole output whenever this
         * disagrees with the effective temperature, so an effects session that
         * never stamped repainted everything, every frame, forever. */
        output->nightlight_temp = temp;
    }

    wlr_buffer_unlock(dst);
    wlr_texture_destroy(tex);
    wlr_buffer_unlock(scene_buf);

    /* Keep frames coming while an animation clock is live: the scene has
     * no damage of its own, so force a full re-render next frame. */
    if (ok && prm.animating) {
        wlr_damage_ring_add_whole(&output->scene_output->damage_ring);
        wlr_output_schedule_frame(wo);
    }
    return ok;

fail_dst:
    wlr_buffer_unlock(dst);
fail_tex:
    wlr_texture_destroy(tex);
fail_scene:
    wlr_buffer_unlock(scene_buf);
    return false;
}
