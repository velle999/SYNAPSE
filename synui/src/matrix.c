/*
 * matrix.c — native animated "kanji rain" wallpaper (GLES2)
 *
 * A port of the GPU shader from the KDE Plasma "Cyberpunk Rain" wallpaper
 * (github.com/velle999/kde-matrix-wallpaper) to synui's GLES2 renderer. The
 * effect is a single fully-procedural fragment shader: falling kanji from a
 * 16x16 texture atlas, plus CRT scanlines, vignette, periodic glitch, and a
 * phosphor glow. The Qt/QML/Plasma wrapper is dropped entirely.
 *
 * Rendering model (chosen to stay inside synui's existing scene graph):
 * each frame the shader is drawn into a per-output GPU buffer, which is then
 * handed to that output's `matrix_buf` scene node — a sibling of the static
 * wallpaper_buf under wallpaper_tree. Windows and panels composite on top
 * exactly as they do over the static wallpaper, and the CRT effects.c
 * post-process still layers over the whole scene. Only one of matrix_buf /
 * wallpaper_buf is ever populated (chosen by config.wallpaper_src).
 *
 * Like effects.c, every failure path bails cleanly: if the shader won't
 * compile, the atlas won't load, or the renderer isn't GLES2, matrix stays
 * uninitialized and the background falls back to the static wallpaper path
 * (solid bg_color under the matrix selection). A broken GL state can never
 * black out the session.
 *
 * The GL context here is the wlr renderer's own EGL context (same as
 * effects.c), so the atlas texture and program live alongside the renderer's
 * GL objects and no cross-context sharing is needed — only ordering, which a
 * glFinish() before commit guarantees.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cairo.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "synui.h"

struct syn_matrix {
    EGLDisplay dpy;
    EGLContext ctx;
    GLuint     prog;
    GLint      u_time, u_res, u_atlas;
    GLuint     atlas_tex;
    double     start;    /* CLOCK_MONOTONIC secs at init; t = now - start */
};

static double now_secs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ── EGL context juggling (mirrors effects.c) ─────────────── */

struct egl_saved {
    EGLDisplay dpy;
    EGLContext ctx;
    EGLSurface draw, read;
};

static bool mx_make_current(struct syn_matrix *m, struct egl_saved *save)
{
    save->dpy  = eglGetCurrentDisplay();
    save->ctx  = eglGetCurrentContext();
    save->draw = eglGetCurrentSurface(EGL_DRAW);
    save->read = eglGetCurrentSurface(EGL_READ);
    return eglMakeCurrent(m->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, m->ctx);
}

static void mx_restore(struct egl_saved *save)
{
    if (save->dpy != EGL_NO_DISPLAY)
        eglMakeCurrent(save->dpy, save->draw, save->read, save->ctx);
    else
        eglMakeCurrent(eglGetCurrentDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
}

/* ── Shaders ──────────────────────────────────────────────── */

/* Fullscreen triangle. v_uv is 0..1; v_uv.y = 0 lands at the buffer's memory
 * row 0, which wlr_scene displays at the top of the output (same convention
 * as the static cairo wallpaper). The shader treats uv.y = 0 as the top, so
 * the rain falls downward with no explicit flip. */
static const char *vert_src =
    "attribute vec2 pos;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = pos * 0.5 + 0.5;\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "}\n";

/* Ported from rain.frag (GLSL 440 -> GLES2): uniform block split into loose
 * uniforms, texture()->texture2D(), out fragColor->gl_FragColor, qt_Opacity
 * dropped. The visual algorithm is otherwise unchanged. */
static const char *frag_src =
    "precision highp float;\n"
    "varying vec2 v_uv;\n"
    "uniform float t;\n"
    "uniform vec2  res;\n"
    "uniform sampler2D atlas;\n"
    "float hash(float n) { return fract(sin(n) * 43758.5453123); }\n"
    "float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
    "float sampleKanji(vec2 cellUV, float charIndex) {\n"
    "    float idx = mod(floor(charIndex * 256.0), 256.0);\n"
    "    float atlasCol = mod(idx, 16.0);\n"
    "    float atlasRow = floor(idx / 16.0);\n"
    "    vec2 padding = vec2(0.05);\n"
    "    vec2 innerUV = mix(padding, vec2(1.0) - padding, cellUV);\n"
    "    vec2 atlasUV = (vec2(atlasCol, atlasRow) + innerUV) / 16.0;\n"
    "    return texture2D(atlas, atlasUV).r;\n"
    "}\n"
    "void main() {\n"
    "    vec2 uv = v_uv;\n"
    "    float cellW = 22.0 / res.x;\n"
    "    float cellH = 28.0 / res.y;\n"
    "    float cols = floor(1.0 / cellW);\n"
    "    float rows = floor(1.0 / cellH);\n"
    "    float col = floor(uv.x / cellW);\n"
    "    float row = floor(uv.y / cellH);\n"
    "    vec2 cellUV = fract(vec2(uv.x / cellW, uv.y / cellH));\n"
    "    float colSeed = hash(col * 13.37);\n"
    "    float speed = 0.4 + colSeed * 0.8;\n"
    "    float offset = colSeed * 100.0;\n"
    "    float brightness = 0.5 + 0.5 * hash(col * 7.13);\n"
    "    float trailLen = 8.0 + 18.0 * hash(col * 3.71);\n"
    "    float totalRows = rows + trailLen + 10.0;\n"
    "    float leadPos = mod(t * speed * 18.0 + offset, totalRows);\n"
    "    float dist = leadPos - row;\n"
    "    if (dist < 0.0) dist += totalRows;\n"
    "    float charSeed = hash2(vec2(col, row) + floor(leadPos));\n"
    "    float glyph = sampleKanji(cellUV, charSeed);\n"
    "    vec3 color = vec3(0.0);\n"
    "    float alpha = 0.0;\n"
    "    vec3 transPink = vec3(1.0, 0.3, 0.55);\n"
    "    vec3 transBlue = vec3(0.35, 0.75, 1.0);\n"
    "    bool isPink = mod(col, 2.0) < 0.5;\n"
    "    vec3 colColor = isPink ? transPink : transBlue;\n"
    "    if (dist < 1.0) {\n"
    "        color = mix(vec3(1.0), colColor, 0.4);\n"
    "        alpha = brightness;\n"
    "        float glowDist = length(cellUV - 0.5) * 2.0;\n"
    "        alpha += 0.3 * (1.0 - glowDist) * brightness;\n"
    "    } else if (dist < trailLen) {\n"
    "        float fade = 1.0 - (dist / trailLen);\n"
    "        fade = fade * fade;\n"
    "        color = colColor * (0.5 + 0.5 * fade);\n"
    "        alpha = fade * brightness * 0.9;\n"
    "    }\n"
    "    float charAlpha = glyph * alpha;\n"
    "    float ambient = 0.008 * hash2(vec2(col, row + floor(t * 0.5)));\n"
    "    vec3 bg = vec3(0.03, 0.02, 0.06);\n"
    "    float colMix = isPink ? 0.0 : 1.0;\n"
    "    vec3 ambientColor = mix(vec3(0.08, 0.02, 0.06), vec3(0.02, 0.06, 0.10), colMix);\n"
    "    vec3 finalColor = bg + color * charAlpha + ambientColor * ambient;\n"
    "    float scanline = 0.7 + 0.3 * smoothstep(0.0, 0.5, fract(uv.y * res.y * 0.5));\n"
    "    scanline *= 0.8 + 0.2 * sin(uv.y * res.y * 3.14159);\n"
    "    finalColor *= scanline;\n"
    "    vec2 vig = uv * (1.0 - uv);\n"
    "    float vigAmount = vig.x * vig.y * 12.0;\n"
    "    vigAmount = clamp(pow(vigAmount, 0.35), 0.0, 1.0);\n"
    "    finalColor *= vigAmount;\n"
    "    finalColor += vec3(0.015, 0.005, 0.02) * (1.0 - vigAmount);\n"
    "    float glitchCycle = floor(t * 0.8);\n"
    "    float glitchChance = hash(glitchCycle * 17.0);\n"
    "    float glitchIntensity = 0.0;\n"
    "    float glitchPhase = fract(t * 0.8);\n"
    "    if (glitchChance > 0.7) {\n"
    "        glitchIntensity = smoothstep(0.0, 0.02, glitchPhase) * smoothstep(0.2, 0.05, glitchPhase);\n"
    "    }\n"
    "    if (glitchIntensity > 0.0) {\n"
    "        float bandSeed = floor(uv.y * 30.0 + glitchCycle);\n"
    "        float bandChance = hash(bandSeed * 7.13);\n"
    "        if (bandChance > 0.6) {\n"
    "            float shift = (hash(bandSeed * 3.71) - 0.5) * 0.06 * glitchIntensity;\n"
    "            float shiftedCol = floor((uv.x + shift) / cellW);\n"
    "            float shiftedCharSeed = hash2(vec2(shiftedCol, row) + floor(leadPos));\n"
    "            float shiftedGlyph = sampleKanji(cellUV, shiftedCharSeed);\n"
    "            finalColor += color * shiftedGlyph * alpha * 0.5 * glitchIntensity;\n"
    "        }\n"
    "        float aberration = 0.004 * glitchIntensity;\n"
    "        vec2 rUV = uv + vec2(aberration, 0.0);\n"
    "        vec2 bUV = uv - vec2(aberration, 0.0);\n"
    "        float rCol = floor(rUV.x / cellW);\n"
    "        float bCol = floor(bUV.x / cellW);\n"
    "        float rSeed = hash2(vec2(rCol, row) + floor(leadPos));\n"
    "        float bSeed = hash2(vec2(bCol, row) + floor(leadPos));\n"
    "        float rGlyph = sampleKanji(fract(vec2(rUV.x / cellW, rUV.y / cellH)), rSeed);\n"
    "        float bGlyph = sampleKanji(fract(vec2(bUV.x / cellW, bUV.y / cellH)), bSeed);\n"
    "        finalColor.r += rGlyph * alpha * 0.15 * glitchIntensity;\n"
    "        finalColor.b += bGlyph * alpha * 0.1 * glitchIntensity;\n"
    "        float noiseBand = hash2(vec2(floor(uv.x * res.x), floor(uv.y * 20.0) + glitchCycle));\n"
    "        if (noiseBand > 0.92) {\n"
    "            finalColor += vec3(0.08, 0.04, 0.10) * glitchIntensity;\n"
    "        }\n"
    "        float sliceSeed = floor(uv.y * 60.0 + glitchCycle * 3.0);\n"
    "        if (hash(sliceSeed) > 0.97) {\n"
    "            finalColor += vec3(0.12, 0.08, 0.14) * glitchIntensity;\n"
    "        }\n"
    "    }\n"
    "    float fringe = charAlpha * 0.03;\n"
    "    finalColor.r += fringe * 0.5;\n"
    "    finalColor.b += fringe * 0.3;\n"
    "    float glow = charAlpha * charAlpha * 0.08;\n"
    "    float glowRadius = length(cellUV - 0.5);\n"
    "    float softGlow = glow * (1.0 - smoothstep(0.0, 0.7, glowRadius));\n"
    "    vec3 glowColor = mix(vec3(0.08, 0.18, 0.25), vec3(0.22, 0.1, 0.15), colMix);\n"
    "    finalColor += glowColor * softGlow;\n"
    "    float grain = (hash2(uv * res + t * 100.0) - 0.5) * 0.015;\n"
    "    finalColor += grain;\n"
    "    gl_FragColor = vec4(finalColor, 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(sh, sizeof(log) - 1, NULL, log);
        wlr_log(WLR_ERROR, "matrix: shader compile failed: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint build_program(void)
{
    GLuint vs = compile(GL_VERTEX_SHADER, vert_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag_src);
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
        char log[1024] = {0};
        glGetProgramInfoLog(prog, sizeof(log) - 1, NULL, log);
        wlr_log(WLR_ERROR, "matrix: program link failed: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/* Decode the bundled kanji atlas and upload it as a GL texture. The PNG is
 * 8-bit grayscale; cairo hands it back as 32bpp (R=G=B), and the shader only
 * samples .r, so a plain GL_RGBA upload is correct regardless of BGRA/RGBA
 * byte order. Returns 0 on any failure. */
static GLuint load_atlas(void)
{
    const char *path = SYNUI_DATADIR "/kanji_atlas.png";
    cairo_surface_t *surf = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        wlr_log(WLR_ERROR, "matrix: atlas decode failed for '%s': %s",
                path, cairo_status_to_string(cairo_surface_status(surf)));
        cairo_surface_destroy(surf);
        return 0;
    }
    cairo_surface_flush(surf);
    int w = cairo_image_surface_get_width(surf);
    int h = cairo_image_surface_get_height(surf);
    int stride = cairo_image_surface_get_stride(surf);
    unsigned char *data = cairo_image_surface_get_data(surf);
    if (w <= 0 || h <= 0 || !data || stride != w * 4) {
        wlr_log(WLR_ERROR, "matrix: atlas has unexpected geometry "
                "(%dx%d stride %d)", w, h, stride);
        cairo_surface_destroy(surf);
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    cairo_surface_destroy(surf);
    return tex;
}

/* ── Public API ──────────────────────────────────────────── */

void matrix_init(syn_server_t *s)
{
    s->matrix = NULL;

    if (!wlr_renderer_is_gles2(s->renderer)) {
        wlr_log(WLR_INFO, "matrix: renderer is not GLES2 — animated wallpaper disabled");
        return;
    }

    struct wlr_egl *egl = wlr_gles2_renderer_get_egl(s->renderer);
    struct syn_matrix *m = calloc(1, sizeof(*m));
    if (!m) return;
    m->dpy = wlr_egl_get_display(egl);
    m->ctx = wlr_egl_get_context(egl);
    m->start = now_secs();

    struct egl_saved saved;
    if (!mx_make_current(m, &saved)) {
        wlr_log(WLR_ERROR, "matrix: eglMakeCurrent failed");
        free(m);
        return;
    }

    m->prog = build_program();
    if (m->prog) {
        m->u_time  = glGetUniformLocation(m->prog, "t");
        m->u_res   = glGetUniformLocation(m->prog, "res");
        m->u_atlas = glGetUniformLocation(m->prog, "atlas");
        m->atlas_tex = load_atlas();
    }

    mx_restore(&saved);

    if (!m->prog || !m->atlas_tex) {
        /* Programs/textures die with the renderer's EGL context on teardown;
         * here we just drop the half-built state and stay disabled. */
        free(m);
        return;
    }

    s->matrix = m;
    wlr_log(WLR_INFO, "matrix: animated wallpaper ready");
}

void matrix_finish(syn_server_t *s)
{
    if (!s->matrix) return;
    /* GL objects are owned by the renderer's context and released with it. */
    free(s->matrix);
    s->matrix = NULL;
}

bool matrix_active(syn_server_t *s)
{
    return s->matrix != NULL && s->config.wallpaper_src == SYN_WP_SRC_MATRIX;
}

void matrix_output_destroy(syn_output_t *o)
{
    if (o->matrix_buf) {
        wlr_scene_node_destroy(&o->matrix_buf->node);
        o->matrix_buf = NULL;
    }
    if (o->matrix_swapchain) {
        wlr_swapchain_destroy(o->matrix_swapchain);
        o->matrix_swapchain = NULL;
    }
}

bool matrix_output_frame(syn_output_t *o)
{
    syn_server_t *s = o->server;
    struct syn_matrix *m = s->matrix;
    struct wlr_output *wo = o->wlr_output;

    if (!matrix_active(s)) {
        matrix_output_destroy(o);
        return false;
    }

    /* Rotated outputs need no special handling: we render offscreen into a
     * scene buffer sized to the *transformed* resolution (so a portrait
     * monitor gets a portrait buffer, and the rain falls down the long axis),
     * and the scene's output pass folds in wo->transform at composite time —
     * exactly as it does for the static cairo wallpaper. This is unlike
     * effects.c, which post-processes the output's own scanout buffer and
     * therefore does have to bail on a non-normal transform. */
    int pw, ph;
    wlr_output_transformed_resolution(wo, &pw, &ph);
    if (pw <= 0 || ph <= 0) return false;

    struct wlr_box box;
    wlr_output_layout_get_box(s->output_layout, wo, &box);
    if (box.width <= 0 || box.height <= 0) return false;

    /* A dedicated swapchain of GPU buffers we render the shader into. Format
     * matches the output's primary swapchain so the scene can texture it. */
    if (!wlr_output_configure_primary_swapchain(wo, NULL, &wo->swapchain))
        return false;
    if (o->matrix_swapchain &&
        (o->matrix_swapchain->width != pw || o->matrix_swapchain->height != ph)) {
        wlr_swapchain_destroy(o->matrix_swapchain);
        o->matrix_swapchain = NULL;
    }
    if (!o->matrix_swapchain) {
        o->matrix_swapchain = wlr_swapchain_create(s->allocator, pw, ph,
                                                   &wo->swapchain->format);
        if (!o->matrix_swapchain) return false;
    }

    struct wlr_buffer *dst = wlr_swapchain_acquire(o->matrix_swapchain);
    if (!dst) return false;

    struct egl_saved saved;
    if (!mx_make_current(m, &saved)) {
        wlr_buffer_unlock(dst);
        return false;
    }

    GLuint fbo = wlr_gles2_renderer_get_buffer_fbo(s->renderer, dst);
    if (!fbo) {
        mx_restore(&saved);
        wlr_buffer_unlock(dst);
        return false;
    }

    static const GLfloat tri[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, pw, ph);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(m->prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m->atlas_tex);
    glUniform1i(m->u_atlas, 0);
    /* Wrap the clock so float precision holds on long uptimes; the shader's
     * cycles are all sub-hour. */
    glUniform1f(m->u_time, (GLfloat)fmod(now_secs() - m->start, 3600.0));
    glUniform2f(m->u_res, (GLfloat)pw, (GLfloat)ph);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    /* Same GL context as the renderer, but force completion before the scene
     * textures this buffer during commit. */
    glFinish();

    mx_restore(&saved);

    /* Hand the freshly-rendered buffer to the scene node (a sibling of the
     * static wallpaper under wallpaper_tree). The node takes its own lock, so
     * release our acquire lock — the swapchain reclaims the buffer once the
     * node drops it on the next frame's set_buffer. */
    if (!o->matrix_buf) {
        o->matrix_buf = wlr_scene_buffer_create(s->wallpaper_tree, dst);
    } else {
        wlr_scene_buffer_set_buffer(o->matrix_buf, dst);
    }
    wlr_buffer_unlock(dst);
    if (!o->matrix_buf) return false;

    wlr_scene_buffer_set_dest_size(o->matrix_buf, box.width, box.height);
    wlr_scene_node_set_position(&o->matrix_buf->node, box.x, box.y);
    wlr_scene_node_lower_to_bottom(&o->matrix_buf->node);

    return true;   /* keep animating: caller schedules the next frame */
}
