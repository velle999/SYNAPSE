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
    "     * pow() lifts the midtones so the tint glows like a phosphor screen\n"
    "     * instead of reading as a flat colourize; u_mono blends it in. */\n"
    "    if (u_mono > 0.0) {\n"
    "        float lum = dot(col, vec3(0.299, 0.587, 0.114));\n"
    "        vec3 ph = u_tint * pow(lum, 0.85);\n"
    "        col = mix(col, ph, u_mono);\n"
    "    }\n"
    "    /* Phosphor bloom: a lit dot on a real tube spills a soft halo into its\n"
    "     * neighbours, so highlights glow instead of ending at a hard edge.\n"
    "     * Cheap single-pass take — sample two rings around the fragment, keep\n"
    "     * only what is bright (BLOOM_THRESH), and add it back in the phosphor\n"
    "     * tint. Gated on u_mono: no phosphor, nothing to bleed. */\n"
    "    if (u_mono > 0.0 && u_bloom > 0.0) {\n"
    "        vec2 px = 1.0 / u_size;\n"
    "        float glow = 0.0;\n"
    "        for (int i = 0; i < 8; i++) {\n"
    "            float a = float(i) * 0.7853982;\n"
    "            vec2 dir = vec2(cos(a), sin(a));\n"
    "            float l1 = dot(texture2D(u_tex, uv + dir * px * 3.0).rgb,\n"
    "                           vec3(0.299, 0.587, 0.114));\n"
    "            float l2 = dot(texture2D(u_tex, uv + dir * px * 6.0).rgb,\n"
    "                           vec3(0.299, 0.587, 0.114));\n"
    "            glow += max(l1 - 0.55, 0.0) + 0.5 * max(l2 - 0.55, 0.0);\n"
    "        }\n"
    "        glow *= 0.08333;\n"   /* /12 : the ring weights sum to 8*(1+0.5) */
    "        col += u_tint * glow * u_bloom * 2.2 * u_mono;\n"
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
    snprintf(frag, sizeof(frag), frag_fmt, prelude, sampler);

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
    double time;
    bool animating;
};

/* The phosphor tints (RGB), indexed by syn_phosphor_t. A white pixel comes out
 * exactly this colour; darker pixels ramp toward black through it. */
static const float phosphor_tint[SYN_PHOSPHOR_COUNT][3] = {
    [SYN_PHOSPHOR_OFF]   = { 1.00f, 1.00f, 1.00f },   /* unused (mono 0) */
    [SYN_PHOSPHOR_GREEN] = { 0.25f, 1.00f, 0.30f },   /* P1 CRT green */
    [SYN_PHOSPHOR_AMBER] = { 1.00f, 0.70f, 0.12f },   /* P3 amber */
    [SYN_PHOSPHOR_WHITE] = { 1.00f, 0.97f, 0.92f },   /* P4 warm white */
};

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

    /* Bloom is the phosphor glow; the shader gates it on mono, so it goes
     * quiet on its own when there is no tint. */
    p->bloom = s->config.effect_bloom;
    if (p->bloom < 0.0f) p->bloom = 0.0f;
    if (p->bloom > 1.0f) p->bloom = 1.0f;

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
        struct wlr_buffer_pass_options bpo = {0};
        struct fx_buffer_pass_options fxo = { .base = &bpo };
        struct fx_gles_render_pass *cap =
            fx_renderer_begin_buffer_pass(s->renderer, dst, wo, &fxo);
        if (!cap) goto fail_dst;
        fx->dpy = eglGetCurrentDisplay();
        fx->ctx = eglGetCurrentContext();
        bool ok = fx->dpy != EGL_NO_DISPLAY && fx->ctx != EGL_NO_CONTEXT &&
                  effects_gl_setup(fx);
        wlr_render_pass_submit((struct wlr_render_pass *)cap);
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

    bool ok = wlr_output_commit_state(wo, &st2);
    wlr_output_state_finish(&st2);

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
