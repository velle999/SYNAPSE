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

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_texture.h>
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
};

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
        fx->u_tex[i]   = glGetUniformLocation(fx->prog[i], "u_tex");
        fx->u_scan[i]  = glGetUniformLocation(fx->prog[i], "u_scan");
        fx->u_curv[i]  = glGetUniformLocation(fx->prog[i], "u_curv");
        fx->u_aberr[i] = glGetUniformLocation(fx->prog[i], "u_aberr");
        fx->u_size[i]  = glGetUniformLocation(fx->prog[i], "u_size");
    }

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

/* ── Frame pass ───────────────────────────────────────────── */

static bool effects_wanted(syn_server_t *s)
{
    if (!s->effects || !s->config.effects) return false;
    return s->config.effect_scanline   > 0.0f ||
           s->config.effect_curvature  > 0.0f ||
           s->config.effect_aberration > 0.0f;
}

bool effects_output_commit(syn_output_t *output)
{
    syn_server_t *s = output->server;
    struct syn_effects *fx = s->effects;
    struct wlr_output *wo = output->wlr_output;

    if (!effects_wanted(s)) return false;
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
    glUniform1f(fx->u_scan[p],  s->config.effect_scanline);
    glUniform1f(fx->u_curv[p],  s->config.effect_curvature);
    glUniform1f(fx->u_aberr[p], s->config.effect_aberration);
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
    return ok;

fail_dst:
    wlr_buffer_unlock(dst);
fail_tex:
    wlr_texture_destroy(tex);
fail_scene:
    wlr_buffer_unlock(scene_buf);
    return false;
}
