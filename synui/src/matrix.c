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
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include "fx_compat.h"
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "contrast.h"
#include "synui.h"

struct syn_matrix {
    EGLDisplay dpy;
    EGLContext ctx;
    GLuint     prog;
    GLint      u_time, u_res, u_atlas;
    GLuint     atlas_tex;
    double     start;    /* CLOCK_MONOTONIC secs at init; t = now - start */
    int        gl_ready; /* fx_renderer hides its EGL context; capture dpy/ctx +
                          * build program/atlas lazily on the first frame. */
    /* Sticky: the deferred setup above failed and will fail again. Without it
     * every frame retried the same shader compile and logged the same three
     * errors at the refresh rate, and — worse — matrix_usable() had no way to
     * tell a caller that the rain is never going to appear. The SCREENSAVER
     * needs that answer: it falls back to drawing the clock, because a saver
     * showing a black screen because the GPU said no reads as a broken
     * machine. Not cleared on its own; nothing that failed here recovers
     * without a restart. */
    int        gl_failed;

    /* Scratch for the strip readback below, grown to the widest output and
     * kept: the alternative is a malloc of a few hundred KB inside a frame
     * callback. Shared across outputs because they are measured one at a
     * time, in this thread, and the bytes are consumed before the next. */
    unsigned char *lum_buf;
    size_t         lum_buf_n;
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

/* Compile the rain shader and upload the glyph atlas. MUST run with the
 * fx_renderer's EGL context current (the caller captures it first). Returns
 * false if the program or atlas failed. */
static bool matrix_gl_setup(struct syn_matrix *m)
{
    m->prog = build_program();
    if (m->prog) {
        m->u_time  = glGetUniformLocation(m->prog, "t");
        m->u_res   = glGetUniformLocation(m->prog, "res");
        m->u_atlas = glGetUniformLocation(m->prog, "atlas");
        m->atlas_tex = load_atlas();
    }
    if (!m->prog || !m->atlas_tex) {
        wlr_log(WLR_ERROR, "matrix: shader/atlas setup failed");
        return false;
    }
    wlr_log(WLR_INFO, "matrix: animated wallpaper ready");
    return true;
}

/* ── What the bar is drawn on ────────────────────────────── */
/*
 * A bar with no background of its own takes its ink from the wallpaper, which
 * means something has to MEASURE the wallpaper — and wallpaper.c can only
 * measure the cairo surface it paints. The rain is not one of those: it is a
 * GPU buffer this file renders and hands straight to the scene, so that painter
 * had nothing to sample and published "unmeasured".
 *
 * The bar reads "unmeasured" as "no ink is safe here" and puts its whole opaque
 * background back — correctly, for the case that answer was written for (a
 * wallpaper-engine client painting over us, which really is unknowable). It is
 * the wrong answer here, and it is the visible bug: picking the matrix
 * wallpaper turned the glass bar off, and picking a photograph turned it back
 * on, with the theme never changing.
 *
 * MEASURED, not declared dark, even though the shader's base is #080510 and
 * every term is added on top of it. What the mean over the strip actually comes
 * to depends on how much of it the glyphs cover, and a shader is the kind of
 * file somebody tunes. A number read off the frame survives that; a constant is
 * right until the commit that moves it, and wrong silently after.
 */

/* How often the strip is re-read. The value moves by a hair as glyphs pass
 * through it and the decision it feeds — black ink or white — sits two orders
 * of magnitude from either end of that range, so this is about tracking a
 * resize or a shader edit, not the animation. Once a frame would be a ~350 KB
 * readback at the refresh rate to re-answer a question that has not moved. */
#define MX_LUM_PERIOD 2.0

static void mx_measure_strip(syn_output_t *o, int pw, int ph)
{
    syn_server_t *s = o->server;
    struct syn_matrix *m = s->matrix;

    double now = now_secs();
    if (now - o->matrix_lum_at < MX_LUM_PERIOD) return;
    bool first = o->matrix_lum_at == 0.0;
    o->matrix_lum_at = now;

    struct wlr_box box;
    wlr_output_layout_get_box(s->output_layout, o->wlr_output, &box);
    if (box.height <= 0) return;

    /* The logical strip in this output's physical rows — the buffer is rendered
     * at the physical size, so on a 2x monitor the bar's 34 logical rows are
     * 68 of these. Same conversion the cairo painter does, off the same shared
     * constant, because the two are measuring the same bar. */
    int rows = (int)lround(SYN_BAR_STRIP_LOGICAL * (double)ph / box.height);
    if (rows < 1)  rows = 1;
    if (rows > ph) rows = ph;

    size_t need = (size_t)pw * (size_t)rows * 4;
    if (need > m->lum_buf_n) {
        unsigned char *nb = realloc(m->lum_buf, need);
        if (!nb) return;   /* keep the last answer rather than publishing junk */
        m->lum_buf   = nb;
        m->lum_buf_n = need;
    }

    /* WHICH END. glReadPixels' y is in the same GL window coordinates the
     * vertex shader writes to: y = 0 is where gl_Position.y = -1 lands, which
     * is v_uv.y = 0, which the shader and wlr_scene both treat as the top of
     * the output (see the vertex shader's comment). So reading from 0 reads the
     * rows a top bar covers whatever the buffer's memory layout turns out to
     * be, and a bar moved to the bottom reads from the far end — exactly as it
     * does in wallpaper.c, and for the same reason: measuring the end the bar
     * is NOT on is a failure that shows up on one theme and in no screenshot. */
    int y0 = s->config.bar_edge == SYN_BAR_EDGE_BOTTOM ? ph - rows : 0;

    /* No glPixelStorei: GL_PACK_ALIGNMENT defaults to 4 and every row here is
     * pw whole RGBA pixels, so the row length in bytes is a multiple of 4 and
     * no padding is ever inserted. Worth saying because the obvious defensive
     * `glPixelStorei(GL_PACK_ALIGNMENT, 1)` would be a write to state we do not
     * own — this runs on the fx_renderer's own EGL context, captured from it,
     * and nothing here restores it afterwards.
     *
     * RGBA/UNSIGNED_BYTE is the one format/type pair GLES2 guarantees on every
     * implementation. */
    glReadPixels(0, y0, pw, rows, GL_RGBA, GL_UNSIGNED_BYTE, m->lum_buf);

    double sum = 0.0;
    for (size_t i = 0; i + 3 < need; i += 4)
        sum += 0.2126 * syn_srgb_lut(m->lum_buf[i]) +
               0.7152 * syn_srgb_lut(m->lum_buf[i + 1]) +
               0.0722 * syn_srgb_lut(m->lum_buf[i + 2]);
    double lum = sum / ((double)pw * rows);

    /* Once per run of the rain on this output, and not every two seconds: the
     * value is published on CHANGE and the ink it resolves to never moves, so
     * without this the only evidence the readback happened at all would be an
     * ink that the seed in wallpaper.c already produces. That is also what the
     * test hangs off — an assertion that cannot fail is not one, and this line
     * is the difference between "the rain measured 0.002" and "nobody measured
     * anything and the seed happened to agree". Reset in
     * matrix_output_destroy, so switching back to the rain says so again. */
    if (first) wlr_log(WLR_INFO, "matrix: backdrop under the bar is %.4f on %s",
                       lum, o->wlr_output->name);

    wallpaper_backdrop_measured(o, lum);
}

void matrix_init(syn_server_t *s)
{
    s->matrix = NULL;

    /* scenefx's fx_renderer isn't a wlr_gles2_renderer and hides its EGL
     * context, so — like effects.c — we can't touch GL at init. Allocate now,
     * defer the shader + atlas to the first frame where a scenefx op makes the
     * context current for us to capture (see gl_ready in the render path). */
    struct syn_matrix *m = calloc(1, sizeof(*m));
    if (!m) return;
    m->start = now_secs();
    m->gl_ready = 0;

    s->matrix = m;
    wlr_log(WLR_INFO, "matrix: animated wallpaper armed (GL setup deferred)");
}

void matrix_finish(syn_server_t *s)
{
    if (!s->matrix) return;
    /* GL objects are owned by the renderer's context and released with it. */
    free(s->matrix->lum_buf);
    free(s->matrix);
    s->matrix = NULL;
}

bool matrix_active(syn_server_t *s)
{
    if (!s->matrix) return false;
    if (s->config.wallpaper_src == SYN_WP_SRC_MATRIX) return true;

    /* A per-monitor override can select the rain even when the global
     * wallpaper is an image, so "is the matrix on" is no longer a single
     * config field. */
    for (int i = 0; i < s->config.wallpaper_out_n; i++)
        if (s->config.wallpaper_out[i].src == SYN_WP_SRC_MATRIX)
            return true;
    return false;
}

/* Can the rain actually be drawn? NOT the same question as `s->matrix != NULL`:
 * matrix_init only ARMS the backend, and the shader and atlas are built on the
 * first frame, so a missing atlas or a shader that will not compile is not
 * known until then. Callers choosing a mode ahead of time (saver.c) must ask
 * this, or they select a mode that renders nothing. */
bool matrix_usable(syn_server_t *s)
{
    return s->matrix && !s->matrix->gl_failed;
}

bool matrix_output_active(syn_output_t *o)
{
    syn_server_t *s = o->server;
    if (!s->matrix) return false;

    /* The screensaver drives EVERY output when it is the matrix mode, whatever
     * each one's wallpaper says — a saver that covered only the monitor whose
     * wallpaper happened to be the rain would be a strange thing to ship. */
    if (saver_wants_matrix(s)) return true;

    syn_wallpaper_src_t src;
    wallpaper_effective(&s->config, o->wlr_output->name, &src, NULL, NULL);
    return src == SYN_WP_SRC_MATRIX;
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
    /* The rain is no longer this output's background, so the next frame that
     * draws it is a fresh run — and its first measurement is worth a line
     * again. Also the throttle: a wallpaper switched away and back must
     * re-measure at once rather than serve up to two seconds of the picture it
     * had before. */
    o->matrix_lum_at = 0.0;
}

bool matrix_output_frame(syn_output_t *o)
{
    syn_server_t *s = o->server;
    struct syn_matrix *m = s->matrix;
    struct wlr_output *wo = o->wlr_output;

    /* Per output, not per server: with the rain picked for one monitor only,
     * the others must keep painting their own wallpaper and must not be kept
     * awake re-rendering a shader they do not show. */
    if (!matrix_output_active(o)) {
        matrix_output_destroy(o);
        return false;
    }

    /* Already known to be impossible — do not retry the compile once a frame. */
    if (m->gl_failed) return false;

    /* Which of the rain's two jobs this frame is. Answered up here because the
     * strip measurement below the render needs it too, and asking twice would
     * be two chances for the two to disagree within one frame. */
    bool as_saver = saver_wants_matrix(s);

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

    GLuint fbo = fx_renderer_get_buffer_fbo(s->renderer, dst);
    if (!fbo) {
        wlr_buffer_unlock(dst);
        return false;
    }

    /* scenefx exposes no getter for the fx_renderer's EGL context and its
     * helpers restore the context after themselves, so capture it inside a
     * throwaway render pass (the context is current between begin and submit)
     * and build the shader + atlas there. dst is overwritten by our own raw-GL
     * pass just below, so the empty pass is harmless. */
    if (!m->gl_ready) {
        /* scenefx 0.5 unexported fx_renderer_begin_buffer_pass; the plain
         * wlroots entry point reaches the same fx_renderer hook. */
        struct wlr_buffer_pass_options bpo = {0};
        struct wlr_render_pass *cap =
            wlr_renderer_begin_buffer_pass(s->renderer, dst, &bpo);
        if (!cap) {
            wlr_buffer_unlock(dst);
            return false;
        }
        m->dpy = eglGetCurrentDisplay();
        m->ctx = eglGetCurrentContext();
        bool ok = m->dpy != EGL_NO_DISPLAY && m->ctx != EGL_NO_CONTEXT &&
                  matrix_gl_setup(m);
        wlr_render_pass_submit(cap);
        if (!ok) {
            wlr_log(WLR_ERROR, "matrix: could not capture fx EGL context / "
                    "build shader — animated wallpaper off");
            m->gl_failed = 1;
            wlr_buffer_unlock(dst);
            return false;
        }
        m->gl_ready = 1;
    }

    struct egl_saved saved;
    if (!mx_make_current(m, &saved)) {
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

    /* Still bound and still current, which is the only place the strip can be
     * read back from. Not while the rain is the SCREENSAVER: it covers the bar
     * then, so what it measures is not what the bar is drawn on — and with
     * as_saver false the only way this output got here is its own wallpaper
     * being the rain (see matrix_output_active). */
    if (!as_saver) mx_measure_strip(o, pw, ph);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    /* Same GL context as the renderer, but force completion before the scene
     * textures this buffer during commit. */
    glFinish();

    mx_restore(&saved);

    /* Where the buffer hangs is the ONLY thing that differs between the rain's
     * two jobs. As the wallpaper it is a sibling of the static wallpaper under
     * wallpaper_tree, lowered under every window. As the screensaver it belongs
     * to the saver's own tree, raised over all of them. Same shader, same
     * swapchain, same buffer — so this is the one place that has to know which
     * job it is doing.
     *
     * The node is REPARENTED rather than rebuilt when the job changes: the
     * saver coming up must not cost a swapchain teardown, and a node left under
     * wallpaper_tree would draw the rain underneath the windows the saver is
     * supposed to be covering. */
    struct wlr_scene_tree *parent = as_saver ? s->saver.tree : s->wallpaper_tree;

    /* The node takes its own lock, so release our acquire lock — the swapchain
     * reclaims the buffer once the node drops it on the next set_buffer. */
    if (!o->matrix_buf) {
        o->matrix_buf = wlr_scene_buffer_create(parent, dst);
    } else {
        wlr_scene_buffer_set_buffer(o->matrix_buf, dst);
        if (o->matrix_buf->node.parent != parent)
            wlr_scene_node_reparent(&o->matrix_buf->node, parent);
    }
    wlr_buffer_unlock(dst);
    if (!o->matrix_buf) return false;

    wlr_scene_buffer_set_dest_size(o->matrix_buf, box.width, box.height);
    wlr_scene_node_set_position(&o->matrix_buf->node, box.x, box.y);
    if (as_saver) wlr_scene_node_raise_to_top(&o->matrix_buf->node);
    else          wlr_scene_node_lower_to_bottom(&o->matrix_buf->node);

    return true;   /* keep animating: caller schedules the next frame */
}
