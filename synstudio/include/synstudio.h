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
enum { SS_ROW_UP, SS_ROW_DIR, SS_ROW_IMAGE, SS_ROW_VIDEO, SS_ROW_PROJECT };

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

/* What a clip IS. A media clip points at a file; a title and a solid are
 * generated, own no file, and exist because a cut needs a caption and a
 * background far more often than it needs a fourth colour wheel. They travel
 * the same track, take the same transform, the same fades and the same
 * transition, so nothing downstream has to special-case them beyond the one
 * place that decides what ffmpeg reads. */
enum { SS_CLIP_MEDIA, SS_CLIP_TITLE, SS_CLIP_SOLID };

/* Where a title sits in the frame. Nine positions, not free coordinates:
 * lower-third and centred cover almost every caption anyone types, and a
 * drag-anywhere title is the transform's job, not the title's. */
enum { SS_TEXT_TL, SS_TEXT_TC, SS_TEXT_TR,
       SS_TEXT_ML, SS_TEXT_MC, SS_TEXT_MR,
       SS_TEXT_BL, SS_TEXT_BC, SS_TEXT_BR };

/* Motion. `scale` 1.0 means "fitted to the frame"; position is a fraction of
 * the frame from centre, so 0,0 is centred at any project size.
 *
 * When `animate` is on, the second set of values is where the clip ENDS and
 * the pair is interpolated across its length — which is the whole of pan and
 * zoom, including the Ken Burns move that is the only way a still photograph
 * earns its place in a cut. One mechanism, because a separate "Ken Burns"
 * feature would be this with fewer options. */
typedef struct {
    float scale;                /* 0.05 .. 10, 1 = fit */
    float pos_x, pos_y;         /* -1 .. 1, fractions of the frame */
    float rotate;               /* degrees */
    int   animate;
    float scale2, pos_x2, pos_y2, rotate2;
} ss_xform;

/* A transition belongs to the INCOMING clip, and is expressed as an alpha
 * ramp over its first `trans_dur` seconds. That is not a simplification: the
 * compositor already overlays every clip onto what is beneath it, so a clip
 * whose alpha rises from 0 while the outgoing clip is still playing under it
 * IS a cross dissolve, with no second code path and no special case in the
 * export. Overlap the two clips by the transition length and it happens. */
enum { SS_TRANS_NONE, SS_TRANS_DISSOLVE,
       SS_TRANS_WIPE_L, SS_TRANS_WIPE_R, SS_TRANS_WIPE_U, SS_TRANS_WIPE_D };

typedef struct {
    int    kind;                /* SS_CLIP_* */
    char   path[1024];          /* media only */
    int    still;               /* a photograph, not a movie: needs -loop 1 */
    double src_in, src_out;     /* seconds into the source */
    double tl_in;               /* seconds on the timeline */
    double speed;               /* 1.0 = normal */
    float  gain_db;             /* audio */
    float  opacity;
    double fade_in, fade_out;   /* seconds */

    ss_xform xf;

    int    trans;               /* SS_TRANS_*, into this clip */
    double trans_dur;           /* seconds */

    /* title / solid */
    char   text[512];
    float  text_size;           /* fraction of frame height, 0 = 0.08 */
    float  text_r, text_g, text_b;
    int    text_pos;            /* SS_TEXT_* */
    float  col_r, col_g, col_b; /* SS_CLIP_SOLID, and a title's backdrop */
    float  col_a;               /* backdrop opacity behind a title */

    int    has_grade;
    ss_develop grade;
} ss_clip;

void ss_clip_reset(ss_clip *c);
void ss_xform_reset(ss_xform *x);
/* Length on the TIMELINE: the source span divided by the speed. */
double ss_clip_length(const ss_clip *c);

/* Named-field access to a clip, for the same reason ss_develop has it: the
 * CLI, the timeline file and the GUI inspector then read ONE table, and a new
 * clip property is one row rather than three lists that drift apart. This is
 * the video half of what ss_develop_describe does for the photo half, and the
 * inspector panel is built from it at startup exactly the same way. */
enum { SS_CT_FLOAT, SS_CT_INT, SS_CT_ENUM, SS_CT_TEXT };
typedef struct {
    const char *key, *group, *label;
    float lo, hi;
    int   type;
    const char *choices;    /* "a|b|c" for SS_CT_ENUM, else NULL */
} ss_clip_info;
int  ss_clip_describe(int i, ss_clip_info *out);    /* 0 past the end */
int  ss_clip_set(ss_clip *c, const char *key, const char *val);
int  ss_clip_get(const ss_clip *c, const char *key, char *out, size_t n);

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

/* ---- editing ----
 *
 * An edit is a rearrangement of intent, never of media. All four of these
 * move numbers around in the document; none of them reads or writes a frame,
 * which is why a cut costs nothing and why undo is a file copy.
 *
 * Clip indices are positions in the track's array and are NOT stable across
 * a remove or a split — both return the affected index and the caller is
 * expected to re-read the track rather than hold an index over an edit. */

/* Slide a clip along the timeline. Negative lands are clamped to 0. */
int ss_timeline_move(ss_timeline *t, int track, int clip, double tl_in);
/* Drag an edge. `which` <0 for the head, >0 for the tail. Trimming the head
 * moves the source in point AND the timeline position together, which is what
 * makes the frame under the cursor stay put — a head trim that only moved one
 * of them would slide the whole clip's content sideways. */
int ss_timeline_trim(ss_timeline *t, int track, int clip, int which, double delta);
/* Cut at a timeline time. Returns the index of the new second half, or -1 if
 * the time does not fall strictly inside the clip. Both halves keep the grade
 * and the transform; the second half loses the incoming transition, because a
 * transition into the middle of a shot is never what a razor meant. */
int ss_timeline_split(ss_timeline *t, int track, int clip, double at);
int ss_timeline_remove(ss_timeline *t, int track, int clip);
/* Close the gap a removed clip left: every later clip on the track slides
 * back by `len`. This is the ripple that separates a delete from a lift. */
void ss_timeline_ripple(ss_timeline *t, int track, double from, double len);

/* Which clip is under a timeline time on a track, or -1. Later clips win, so
 * the answer matches what the compositor draws when two overlap. */
int ss_timeline_at(const ss_timeline *t, int track, double time);

/* ---- rendering ----
 *
 * Emit the ffmpeg argv for an export. lutdir holds one .cube per graded clip.
 * Returns argc; argv entries are strdup'd and owned by the caller. */
/* `preview` asks for a PLAYABLE render rather than a deliverable one: smaller,
 * encoded as fast as x264 can, and quality traded away on purpose. It is what
 * the window plays when you press play — the same graph, so what you watch is
 * what you will ship, only rougher. A deliverable export passes 0. */
int    ss_timeline_ffmpeg(const ss_timeline *t, const char *out,
                          const char *lutdir, int preview, char ***argv);

/* One composited frame, for the program monitor.
 *
 * NOT the export graph with a seek on the front: that decodes the timeline
 * from zero and a scrub at minute nine would take minute nine's worth of
 * work. This builds a graph containing ONLY the clips that are actually on
 * screen at `time`, each seeked directly to its own source position, so the
 * cost is the clips under the playhead and nothing else — which is what makes
 * dragging the playhead feel like dragging a playhead.
 *
 * max_edge 0 = the project size. Writes to `out` (.png). */
int    ss_timeline_frame(const ss_timeline *t, double time, const char *out,
                         const char *lutdir, int max_edge, char ***argv);

/* The side files a graph refers to — one .cube per graded clip, one text file
 * per title — written into `dir` under the names both builders above expect.
 * Returns how many were written, or -1. `unbake` removes exactly those. */
int    ss_timeline_bake(const ss_timeline *t, const char *dir);
void   ss_timeline_unbake(const ss_timeline *t, const char *dir);

/* Name to enum for the three things a clip line can say. -1 = not a name. */
int    ss_trans_value(const char *s);
int    ss_clip_kind_value(const char *s);
int    ss_textpos_value(const char *s);

/* ----------------------------------------------------------------- util -- */

float ss_srgb_to_linear(float v);
float ss_linear_to_srgb(float v);
float ss_clampf(float v, float lo, float hi);
float ss_luma(float r, float g, float b);

#endif /* SYNSTUDIO_H */
