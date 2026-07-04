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
 * SynapseOS Project — GPLv2
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
#include <wlr/render/gles2.h>
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
    GLint  u_time[2], u_glitch[2];

    /* Animation clocks (CLOCK_MONOTONIC seconds). While any of these is
     * live the frame pass keeps damaging + scheduling, so the animation
     * runs at output refresh. */
    double pulse_until;   /* L3: aberration ramp after a focus change */
    double close_until;   /* L2 (interim): brief glitch after a close  */

    int force_glitch;     /* SYNUI_EFFECTS_FORCE_GLITCH=1 (tests only) */
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
 * around the center, scanlines darken alternate output rows. */
static const char *frag_fmt =
    "%s"
    "precision highp float;\n"
    "varying vec2 v_uv;\n"
    "uniform %s u_tex;\n"
    "uniform float u_scan;\n"
    "uniform float u_curv;\n"
    "uniform float u_aberr;\n"
    "uniform float u_time;\n"
    "uniform float u_glitch;\n"
    "uniform vec2 u_size;\n"
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
    "        /* 8px horizontal bands; a per-band hash reseeded ~24x/sec\n"
    "         * decides which bands displace and by how much. */\n"
    "        float band = floor(uv.y * u_size.y / 8.0);\n"
    "        float seed = band * 127.1 + floor(u_time * 24.0) * 311.7;\n"
    "        float h = fract(sin(seed) * 43758.5453);\n"
    "        float disp = (h - 0.5) * 0.10 * u_glitch * step(0.75, h);\n"
    "        uv.x = clamp(uv.x + disp, 0.0, 1.0);\n"
    "    }\n"
    "    vec2 d = (uv - 0.5) * u_aberr * 0.0035;\n"
    "    float r = texture2D(u_tex, uv - d).r;\n"
    "    vec4  g = texture2D(u_tex, uv);\n"
    "    float b = texture2D(u_tex, uv + d).b;\n"
    "    float scan = 1.0 - u_scan * 0.25 *\n"
    "                 mod(floor(uv.y * u_size.y), 2.0);\n"
    "    vec2 vg = uv * (1.0 - uv);\n"
    "    float vig = 1.0 - u_curv * 0.35 *\n"
    "                (1.0 - clamp(vg.x * vg.y * 18.0, 0.0, 1.0));\n"
    "    gl_FragColor = vec4(vec3(r, g.g, b) * scan * vig, g.a);\n"
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
    char frag[4096];
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

bool effects_init(syn_server_t *s)
{
    s->effects = NULL;

    if (!wlr_renderer_is_gles2(s->renderer)) {
        wlr_log(WLR_INFO, "effects: renderer is not GLES2 — post-process disabled");
        return false;
    }

    struct wlr_egl *egl = wlr_gles2_renderer_get_egl(s->renderer);
    struct syn_effects *fx = calloc(1, sizeof(*fx));
    if (!fx) return false;
    fx->dpy = wlr_egl_get_display(egl);
    fx->ctx = wlr_egl_get_context(egl);

    struct egl_saved saved;
    if (!fx_make_current(fx, &saved)) {
        wlr_log(WLR_ERROR, "effects: eglMakeCurrent failed");
        free(fx);
        return false;
    }

    fx->prog[0] = build_program("", "sampler2D");
    if (wlr_gles2_renderer_check_ext(s->renderer, "GL_OES_EGL_image_external"))
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
    }

    const char *force = getenv("SYNUI_EFFECTS_FORCE_GLITCH");
    fx->force_glitch = force && strcmp(force, "1") == 0;

    fx_restore(&saved);

    if (!fx->prog[0]) {
        free(fx);
        return false;
    }

    s->effects = fx;
    wlr_log(WLR_INFO, "effects: GLES2 post-process ready (external-oes: %s)",
            fx->prog[1] ? "yes" : "no");
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
    double time;
    bool animating;
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
           p->glitch > 0.0f;
}

/* ── Frame pass ───────────────────────────────────────────── */

bool effects_output_commit(syn_output_t *output)
{
    syn_server_t *s = output->server;
    struct syn_effects *fx = s->effects;
    struct wlr_output *wo = output->wlr_output;

    struct fx_params prm;
    if (!fx_compute(s, &prm)) return false;
    /* Rotated/flipped outputs would need the transform folded into the
     * shader; punt to the plain path there. */
    if (wo->transform != WL_OUTPUT_TRANSFORM_NORMAL) return false;

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

    /* Render the scene into the offscreen swapchain. */
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

    struct wlr_texture *tex = wlr_texture_from_buffer(s->renderer, scene_buf);
    if (!tex) goto fail_scene;

    struct wlr_buffer *dst = wlr_swapchain_acquire(wo->swapchain);
    if (!dst) goto fail_tex;

    struct egl_saved saved;
    if (!fx_make_current(fx, &saved)) goto fail_dst;

    GLuint fbo = wlr_gles2_renderer_get_buffer_fbo(s->renderer, dst);
    if (!fbo) {
        fx_restore(&saved);
        goto fail_dst;
    }

    struct wlr_gles2_texture_attribs ta;
    wlr_gles2_texture_get_attribs(tex, &ta);
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
