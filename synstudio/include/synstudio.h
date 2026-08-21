/* synstudio — the SynapseOS media editor.
 *
 * One image engine, two front pages. The photo darkroom and the video colour
 * page apply the SAME ss_develop stack; video gets it as a baked 3D LUT (see
 * lut.c) so the colour maths exists once, in C, and never diverges between a
 * still and a clip.
 *
 * Everything here is scene-referred linear float RGBA, sRGB primaries. Files
 * arrive display-encoded and are linearised on the way in; they are re-encoded
 * on the way out. Ops that model light (exposure, white balance) are linear
 * multiplies; ops that model a LOOK (contrast, curve) run in a perceptual
 * encoding, because that is where their controls feel evenly spaced.
 */
#ifndef SYNSTUDIO_H
#define SYNSTUDIO_H

#include <stddef.h>
#include <stdio.h>

/* ---------------------------------------------------------------- image -- */

/* Interleaved RGBA. Alpha is STRAIGHT (not premultiplied): a develop stack
 * adjusts colour, and premultiplied colour would drag alpha into every tone
 * curve. Compositing premultiplies locally and puts it back. */
typedef struct {
    int    w, h;
    float *px;              /* w*h*4 */
} ss_image;

int   ss_image_init(ss_image *im, int w, int h);
void  ss_image_free(ss_image *im);
int   ss_image_copy(ss_image *dst, const ss_image *src);
/* Box-filtered downscale / bilinear upscale. Used to make previews. */
int   ss_image_scale(ss_image *dst, const ss_image *src, int w, int h);
/* Longest edge <= max_edge, aspect preserved. No-op if already smaller. */
int   ss_image_fit(ss_image *im, int max_edge);

/* ---------------------------------------------------------------- curve -- */

#define SS_CURVE_MAX_PTS 16
#define SS_CURVE_LUT     1024

/* A point curve over [0,1] in a display-referred encoding. Interpolated with
 * a monotone cubic (Fritsch-Carlson), so dragging one point can never make the
 * curve dip below a neighbour and invert a tone. */
typedef struct {
    int   n;
    float x[SS_CURVE_MAX_PTS];
    float y[SS_CURVE_MAX_PTS];
    float lut[SS_CURVE_LUT];    /* built by ss_curve_build */
    int   built;
    int   identity;             /* fast path: skip entirely */
} ss_curve;

void  ss_curve_reset(ss_curve *c);          /* two points, y = x */
int   ss_curve_add(ss_curve *c, float x, float y);
void  ss_curve_build(ss_curve *c);
float ss_curve_eval(const ss_curve *c, float x);

/* ------------------------------------------------------------- develop -- */

/* The eight HSL bands, in hue order starting at red. */
enum { SS_BAND_RED, SS_BAND_ORANGE, SS_BAND_YELLOW, SS_BAND_GREEN,
       SS_BAND_AQUA, SS_BAND_BLUE, SS_BAND_PURPLE, SS_BAND_MAGENTA,
       SS_BANDS };

typedef struct {
    int   on;
    float x, y, w, h;       /* fractions of the ORIGINAL frame, 0..1 */
    float angle;            /* straighten, degrees, applied before the cut */
} ss_crop;

/* Every field is 0 = no effect, so a zeroed struct is the null grade. That is
 * relied on by ss_develop_is_identity and by the .cube baker. */
typedef struct {
    /* white balance. temp_k 0 means "as shot" — no conversion at all. */
    float temp_k;           /* 2000..50000 */
    float tint;             /* -150..150, green .. magenta */

    /* tone */
    float exposure;         /* stops */
    float contrast;         /* -100..100 */
    float highlights;       /* -100..100 */
    float shadows;
    float whites;
    float blacks;

    /* presence */
    float texture;          /* -100..100, mid-frequency local contrast */
    float clarity;
    float dehaze;
    float vibrance;         /* -100..100, saturation weighted by how dull */
    float saturation;

    /* colour */
    float hsl_hue[SS_BANDS];    /* -100..100 */
    float hsl_sat[SS_BANDS];
    float hsl_lum[SS_BANDS];

    /* split toning / colour grading */
    float shadow_hue, shadow_sat;
    float hilite_hue, hilite_sat;
    float grade_balance;    /* -100..100 */

    /* curves */
    ss_curve curve_rgb, curve_r, curve_g, curve_b;

    /* detail (spatial) */
    float sharpen;          /* 0..150 */
    float sharpen_radius;   /* 0.5..3.0 px */
    float nr_luma;          /* 0..100 */
    float nr_chroma;        /* 0..100 */

    /* effects (spatial / positional) */
    float vignette;         /* -100..100 */
    float vignette_mid;     /* 0..100 */
    float vignette_feather; /* 0..100 */
    float grain;            /* 0..100 */
    float grain_size;       /* 0..100 */

    /* geometry */
    ss_crop crop;
    int   flip_h, flip_v;
    int   rotate90;         /* 0..3 quarter turns */
} ss_develop;

void ss_develop_reset(ss_develop *d);
int  ss_develop_is_identity(const ss_develop *d);

/* Named-field access, so the CLI, the sidecar parser and the GUI all use one
 * table and adding a control means adding one row in develop.c. */
int         ss_develop_set(ss_develop *d, const char *key, const char *val);
int         ss_develop_get(const ss_develop *d, const char *key, char *out, size_t n);
const char *ss_develop_key(int i);          /* NULL past the end */

enum { SS_T_FLOAT, SS_T_INT, SS_T_CURVE };
typedef struct {
    const char *key, *group, *label;
    float lo, hi;               /* what the engine will ACCEPT */
    float ui_lo, ui_hi;         /* what a slider should OFFER */
    int   type;
} ss_develop_info;
int ss_develop_describe(int i, ss_develop_info *out);
int         ss_develop_write(const ss_develop *d, FILE *fp);   /* key<TAB>value */
int         ss_develop_read(ss_develop *d, FILE *fp);

/* ----------------------------------------------------------- pipeline -- */

/* The pointwise half of the stack: white balance, exposure, the tone region
 * controls, contrast, curves, HSL, grading, vibrance/saturation. Depends on
 * the pixel VALUE and nothing else, which is exactly what makes it bakeable
 * into a 3D LUT for video. */
void ss_apply_pointwise(ss_image *im, const ss_develop *d);
/* One pixel through the same maths. The baker and the pixel probe use it, so
 * a LUT can never drift from the image path. */
void ss_pixel_pointwise(const ss_develop *d, float in[3], float out[3]);

/* The spatial half: texture/clarity/dehaze, sharpening, noise reduction,
 * grain, vignette. Position- or neighbourhood-dependent, never LUT-able. */
void ss_apply_spatial(ss_image *im, const ss_develop *d);

/* Geometry: rotate90/flip/straighten/crop. Changes the image dimensions. */
int  ss_apply_geometry(ss_image *im, const ss_develop *d);

/* Whole stack, in the fixed order above. */
int  ss_render(ss_image *im, const ss_develop *d);

/* ---------------------------------------------------------------- masks -- */

enum { SS_MASK_LINEAR, SS_MASK_RADIAL };

typedef struct {
    int   type;
    int   invert;
    /* linear: the gradient runs from (x0,y0) to (x1,y1), fractions of frame */
    /* radial: centre (x0,y0), radii (x1,y1), feather 0..1 */
    float x0, y0, x1, y1;
    float feather;
    ss_develop dev;         /* what this mask DOES */
} ss_mask;

void ss_mask_reset(ss_mask *m, int type);
/* Coverage 0..1 for a pixel, frame-relative coords. */
float ss_mask_at(const ss_mask *m, float fx, float fy);
/* Apply one mask: renders the masked develop and blends by coverage. */
int  ss_apply_mask(ss_image *im, const ss_mask *m);

/* -------------------------------------------------------------- the edit -- */

#define SS_MAX_MASKS 16

/* Everything that has been decided about one photograph: the global develop
 * stack plus its local adjustments. This is what a sidecar holds and what the
 * darkroom edits. The source file is never written to. */
typedef struct {
    ss_develop dev;
    int        nmasks;
    ss_mask    mask[SS_MAX_MASKS];
} ss_edit;

void ss_edit_reset(ss_edit *e);
int  ss_edit_apply(ss_image *im, const ss_edit *e);
int  ss_edit_write(const ss_edit *e, FILE *fp);
int  ss_edit_read(ss_edit *e, FILE *fp);
int  ss_edit_load(ss_edit *e, const char *path);    /* missing file = reset, ok */
int  ss_edit_save(const ss_edit *e, const char *path);
/* "<image path>.synstudio" — the sidecar sits beside the original. */
void ss_sidecar_path(const char *img, char *out, size_t n);

/* 256 bins per channel plus luma, over the DISPLAY-encoded range, which is
 * the only histogram that tells you whether the exported file will clip. */
typedef struct { unsigned r[256], g[256], b[256], l[256]; unsigned clip_lo, clip_hi; }
    ss_histogram;
void ss_histogram_of(const ss_image *im, ss_histogram *h);

/* ------------------------------------------------------------------ lut -- */

/* Bake the pointwise half of a develop stack into an Iridas/Adobe .cube 3D
 * LUT. This is the bridge to video: ffmpeg's lut3d filter then applies the
 * exact same grade to every frame of a clip. */
int ss_lut_write(const ss_develop *d, int size, FILE *fp, const char *title);

/* ------------------------------------------------------------------- io -- */

/* Decode is delegated to ffmpeg (and to libraw's dcraw_emu for camera raw),
 * NEVER linked. A SONAME bump in ffmpeg has taken out a shipped SynapseOS
 * component before; a subprocess boundary is immune to it and costs one pipe.
 */
typedef struct {
    int  w, h;
    int  ok;
    char fmt[64];
    char codec[64];
    double duration;        /* seconds, 0 for stills */
    double fps;
    int  is_video;
} ss_probe;

int ss_probe_file(const char *path, ss_probe *p);
/* max_edge 0 = full resolution. */
int ss_load(const char *path, ss_image *im, int max_edge);
/* Frame at a timestamp, for the video pages. */
int ss_load_frame(const char *path, double t, ss_image *im, int max_edge);
/* quality 1..100 for jpeg; ignored for png/tiff. bits 8 or 16. */
int ss_save(const char *path, const ss_image *im, int quality, int bits);

/* ------------------------------------------------------------- browse -- */

/* The Open button needs a list of places to go. This is deliberately NOT
 * delegated to synfiles: synfiles has no picker mode (`--pick` was assumed and
 * does not exist — the button was dead), and giving a file manager one purely
 * so another program can borrow it puts a modal chooser in a browser that
 * never wanted one. The engine already knows which extensions it can decode,
 * which is the only question a picker for THIS program has to answer.
 *
 * Rows are what the window draws: parent first, then directories, then files
 * this engine can actually open. Anything else is not listed, because a row
 * that fails when clicked is worse than an absent one. */
enum { SS_ROW_UP, SS_ROW_DIR, SS_ROW_IMAGE, SS_ROW_VIDEO };

typedef struct {
    int  type;
    char name[256];
    char path[1024];
} ss_row;

/* Lists `dir` into a grown array the caller frees. `dir` may be relative or
 * start with ~; *out is always absolute. Returns the row count, or -1. */
int ss_browse(const char *dir, ss_row **rows, char abs_out[1024]);

/* ------------------------------------------------------------- timeline -- */

#define SS_MAX_TRACKS 16

enum { SS_TRACK_VIDEO, SS_TRACK_AUDIO };

typedef struct {
    char   path[1024];
    double src_in, src_out;     /* seconds into the source */
    double tl_in;               /* seconds on the timeline */
    double speed;               /* 1.0 = normal */
    float  gain_db;             /* audio */
    float  opacity;
    double fade_in, fade_out;   /* seconds */
    int    has_grade;
    ss_develop grade;
} ss_clip;

/* Clips are a GROWN array, not a fixed one. An ss_clip carries a whole
 * ss_develop, which carries four curves, which carry a 1024-entry table each
 * — about 18KB per clip. A fixed 512 of those per track across 16 tracks is a
 * 147MB structure, and declaring one on the stack is an instant segfault with
 * no diagnostic. It was exactly that, first run. */
typedef struct {
    int    type;
    int    muted, hidden;
    char   name[64];
    int    nclips, cap;
    ss_clip *clip;
} ss_track;

typedef struct {
    char   name[256];
    int    w, h;
    double fps;
    int    ntracks;
    ss_track track[SS_MAX_TRACKS];
} ss_timeline;

void   ss_timeline_reset(ss_timeline *t, int w, int h, double fps);
void   ss_timeline_free(ss_timeline *t);
int    ss_timeline_add_track(ss_timeline *t, int type, const char *name);
int    ss_timeline_add_clip(ss_timeline *t, int track, const ss_clip *c);
double ss_timeline_duration(const ss_timeline *t);
int    ss_timeline_write(const ss_timeline *t, FILE *fp);
int    ss_timeline_read(ss_timeline *t, FILE *fp);
/* Emit the ffmpeg argv for an export. lutdir holds one .cube per graded clip.
 * Returns argc; argv entries are strdup'd and owned by the caller. */
int    ss_timeline_ffmpeg(const ss_timeline *t, const char *out,
                          const char *lutdir, char ***argv);

/* ----------------------------------------------------------------- util -- */

float ss_srgb_to_linear(float v);
float ss_linear_to_srgb(float v);
float ss_clampf(float v, float lo, float hi);
float ss_luma(float r, float g, float b);

#endif /* SYNSTUDIO_H */
