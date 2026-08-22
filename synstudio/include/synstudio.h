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

/* How long a LUT reference may be: a catalogue name, or a path to a .cube. */
#define SS_LUT_REF 192

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

    /* An imported look: a .cube 3D LUT by name or by path, and how much of
     * it to take. Held as a REFERENCE and not as a table — a 33-node cube is
     * 431KB and this struct is copied into every keyframe and every mask.
     * Empty means no look, which is why a zeroed struct is still the null
     * grade. See look.c. */
    char  lut[SS_LUT_REF];
    float lut_amount;       /* 0..100 */

    /* geometry */
    ss_crop crop;
    int   flip_h, flip_v;
    int   rotate90;         /* 0..3 quarter turns */
} ss_develop;

void ss_develop_reset(ss_develop *d);
int  ss_develop_is_identity(const ss_develop *d);

/* A develop stack part way between two others, m in 0..1.
 *
 * Numbers interpolate. Curves interpolate through their built tables rather
 * than their control points, because two curves rarely have the same number
 * of points and lerping a 3-point curve towards a 7-point one has no
 * meaning. The handful of switches — crop on, the flips, the quarter turns —
 * take the NEARER end: there is no half a flip. */
void ss_develop_lerp(const ss_develop *a, const ss_develop *b, float m,
                     ss_develop *out);

/* Named-field access, so the CLI, the sidecar parser and the GUI all use one
 * table and adding a control means adding one row in develop.c. */
int         ss_develop_set(ss_develop *d, const char *key, const char *val);
int         ss_develop_get(const ss_develop *d, const char *key, char *out, size_t n);
const char *ss_develop_key(int i);          /* NULL past the end */

enum { SS_T_FLOAT, SS_T_INT, SS_T_CURVE, SS_T_STR };
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

/* ---- scopes ----
 *
 * A waveform, an RGB parade and a vectorscope, rendered into an image of
 * their own. Computed here rather than by an ffmpeg filter for the reason the
 * histogram is: a scope is read to decide whether a shot is legal and whether
 * two shots match, and an answer produced by a different renderer than the
 * picture is an answer about something else.
 *
 * Measured in the DISPLAY encoding — a waveform in linear light puts middle
 * grey at 18% and nobody reads one there. */
enum { SS_SCOPE_WAVEFORM, SS_SCOPE_PARADE, SS_SCOPE_VECTOR };
int         ss_scope_value(const char *s);      /* -1 if it is not one */
const char *ss_scope_name(int v);
int         ss_scope_render(const ss_image *in, int kind, int w, int h,
                            ss_image *out);

/* ---- shot match ----
 *
 * Make one shot look like another, by FITTING the controls rather than
 * solving them. Every control has a transfer function of its own — exposure
 * is stops, contrast is a curve around a pivot, temperature is a chromatic
 * adaptation — and solving any of them in closed form means writing a second
 * model of what colour.c does, which drifts the first time colour.c is
 * improved. So each one is set, rendered through the REAL engine, measured
 * and bisected: correct by construction, and it never needs to know what
 * `contrast` means.
 *
 * Matches brightness, contrast and white balance. Not a three-way grade —
 * there are no per-channel lift/gamma/gain controls here, and inventing them
 * to have something to solve for would be the tail wagging the dog.
 *
 * `d` is read as the starting stack and written with the fitted one. */
typedef struct {
    double want_luma, got_luma;
    double want_spread, got_spread;
    double want_rb, got_rb;     /* warm against cool */
    double want_gm, got_gm;     /* green against magenta */
} ss_match_report;

int ss_shot_match(const ss_image *ref, const ss_image *tgt, ss_develop *d,
                  ss_match_report *rep);

/* ------------------------------------------------------------------ lut -- */

/* Bake the pointwise half of a develop stack into an Iridas/Adobe .cube 3D
 * LUT. This is the bridge to video: ffmpeg's lut3d filter then applies the
 * exact same grade to every frame of a clip. */
int ss_lut_write(const ss_develop *d, int size, FILE *fp, const char *title);

/* ----------------------------------------------------------- looks in -- */

/* The other direction: somebody ELSE's .cube, read and applied.
 *
 * It goes on at the END of the pointwise chain, in the display encoding,
 * because that is the domain a .cube is defined in and the one every tool
 * that writes one assumes. Which means the LUT BRIDGE COMPOSES: baking a
 * graded clip walks the same ss_pixel_pointwise, so an imported look comes
 * out inside the baked cube and the export needs no second lut3d and no new
 * code path at all. A still and a frame still agree by construction.
 *
 * A LUT that this machine has not got is KEPT in the document and renders as
 * nothing, the way a missing effect is — dropping the reference would delete
 * somebody's grade from their own project the first time a colleague opened
 * it. */
#define SS_LUT_MAX_3D  129      /* nodes per axis */
#define SS_LUT_MAX_1D 16384

typedef struct {
    int    dims;                /* 1 or 3 */
    int    size;                /* nodes per axis */
    float  dmin[3], dmax[3];    /* DOMAIN_MIN / DOMAIN_MAX */
    char   title[64];
    float *tab;                 /* dims==3 ? size^3*3 : size*3, R fastest */
} ss_lut3d;

int  ss_lut_read(const char *path, ss_lut3d *out, char *err, size_t errn);
void ss_lut_free(ss_lut3d *l);
/* Display-encoded in, display-encoded out. Input outside the domain is
 * clamped to it — a LUT has nothing to say about where it does not reach. */
void ss_lut_eval(const ss_lut3d *l, const float in[3], float out[3]);

/* The catalogue, found the way effects are: what is installed, then the
 * user's own, then anything in SYNSTUDIO_LUTS. A name wins over an earlier
 * one of the same name. */
typedef struct {
    char name[64];
    char path[1024];
    int  dims, size;
} ss_lut_entry;

int                 ss_lut_count(void);
const ss_lut_entry *ss_lut_at(int i);
/* A reference is a catalogue NAME, or a path if it has a '/' in it (~ is
 * expanded). NULL when nothing answers to it. */
const ss_lut_entry *ss_lut_lookup(const char *ref);
/* Where a reference POINTS, without opening it. Kept apart from the lookup
 * above because the lookup answers NULL for a file that exists and is broken,
 * which turns "row 4096 of 4913, the download stopped" into "no LUT of that
 * name" — the one message that sends somebody looking in the wrong place. */
int                 ss_lut_resolve(const char *ref, char *out, size_t n);
/* Loaded once and held, because the pixel loop asks per pixel. NULL for a
 * reference nothing answers to, and the first miss says so on stderr — once
 * per reference, not once per pixel. */
const ss_lut3d     *ss_lut_cached(const char *ref);

/* ------------------------------------------------------------- looks -- */

/* A look is a develop stack in a file: the same tab-separated text the
 * sidecar holds, named `.synlook`, carrying only the fields it means to
 * change. Applying one SETS those fields and leaves the rest alone, so a look
 * lands on top of the exposure and white balance a photograph already needed
 * rather than throwing them away.
 *
 * Geometry is never in a look. A crop belongs to one photograph; a look is
 * meant to travel. */
typedef struct {
    char name[64], label[64], about[160], path[1024];
} ss_look;

int             ss_look_count(void);
const ss_look  *ss_look_at(int i);
const ss_look  *ss_look_find(const char *name);
/* Apply the look's fields onto `d`. Returns the number of lines it could not
 * understand (a look written by a newer synstudio still applies, minus what
 * this build has never heard of), or -1 if the file cannot be read. */
int             ss_look_apply(const ss_look *lk, ss_develop *d);
/* Write every field of `d` that is not at its default and not geometry.
 * `path` gets the file it wrote. */
int             ss_look_save(const char *name, const char *label,
                             const ss_develop *d, char *path, size_t n);
/* Where a look of the user's own goes. */
int             ss_look_dir(char *out, size_t n);

/* ---- the render queue ----
 *
 * A file of commands, not a daemon. One job per line: the arguments of a
 * `timeline export`, tab separated. Running the queue is running those
 * commands, so there is no second code path that renders things and no option
 * that only the queue understands — a job somebody typed and a job the window
 * queued are the same object. It survives the program closing because it is a
 * file, the same reason undo is a directory of documents. */
#define SS_QUEUE_LINE 4096
#define SS_QUEUE_MAX  256
int ss_queue_path(char *out, size_t n);
int ss_queue_add(const char *const *argv, int argc);   /* -2: a tab in an arg */
int ss_queue_read(char (*job)[SS_QUEUE_LINE], int max);
int ss_queue_clear(void);

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
/* How long anything ffprobe can open runs, in seconds, or 0.
 *
 * ss_probe_file is about the PICTURE — it fails outright without a video
 * stream, because for a darkroom a file with no image in it is not a file. A
 * music bed on an audio track is exactly that file, and asking the picture
 * probe how long it is answered "no idea", which made a whole album arrive on
 * the timeline as a five second clip. */
double ss_media_duration(const char *path);
/* Whether there is a sound track in there at all. Asked once per clip before
 * an export, because referencing [N:a] for an input that has no audio stream
 * fails the WHOLE graph rather than being quietly ignored. */
int    ss_media_has_audio(const char *path);
/* How many channels that sound track has, or 0. A pan has to know: panning a
 * MONO source means naming c0 twice, and routing it through a stereo upmix
 * instead costs 3dB that a centred clip does not pay — so the same fader
 * position would be quieter for having been panned. */
int    ss_media_channels(const char *path);
/* What a file is, decided by ffmpeg rather than by its extension. One or two
 * processes, so it is for a file somebody handed over on purpose — a drop, an
 * argument — never for every row of a directory listing. */
enum { SS_KIND_NONE = 0, SS_KIND_IMAGE, SS_KIND_VIDEO, SS_KIND_AUDIO };
int    ss_media_kind(const char *path);
/* max_edge 0 = full resolution. */
int ss_load(const char *path, ss_image *im, int max_edge);
/* Frame at a timestamp, for the video pages. */
int ss_load_frame(const char *path, double t, ss_image *im, int max_edge);
/* quality 1..100 for jpeg; ignored for png/tiff. bits 8 or 16. */
int ss_save(const char *path, const ss_image *im, int quality, int bits);
/* ss_save chooses its muxer from the output NAME, so this is the list the
 * window offers rather than a gate — kept here so the window cannot offer a
 * format this engine has no way to write. NULL-name terminated. */
typedef struct {
    const char *name;
    const char *ext;
    const char *label;
} ss_still_format;
const ss_still_format *ss_still_formats(void);

/* ------------------------------------------------------------- peaks -- */

/* An audio envelope, for drawing a waveform on a clip.
 *
 * `peak` is the loudest sample in each bucket and `rms` its average power,
 * both 0..1; a waveform drawn from peak alone is a solid block on anything
 * compressed, and one drawn from RMS alone hides the transients you are
 * looking for when you line a cut up to a beat. Real editors draw both, so
 * both are returned and the caller decides.
 *
 * Returns 0, or -1 if the file has no audio at all — which is not an error
 * worth a message, it is the answer for a photograph.
 *
 * The DECODE IS BOUNDED BY THE BUCKET COUNT, not by the length of the clip:
 * the sample rate is chosen so a two-hour source costs the same as a two
 * second one. A waveform is an envelope and nothing about it needs 48kHz. */
int ss_peaks(const char *path, double in, double out, int nbuckets,
             float *peak, float *rms);

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
/* SS_ROW_LOOK is a .cube or a .synlook. Listed for the same reason a project
 * is: neither is something the ENGINE can decode, so a picker that only
 * offered what it can decode left a LUT reachable by typing its path and no
 * other way. */
enum { SS_ROW_UP, SS_ROW_DIR, SS_ROW_IMAGE, SS_ROW_VIDEO, SS_ROW_AUDIO,
       SS_ROW_PROJECT, SS_ROW_LOOK };

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

/* The weights a family is asked for by name. Not a numeric 100..900: what
 * reaches drawtext is a FILE, and a family ships the faces it ships — asking
 * fontconfig for `weight=bold` and taking what it answers is the difference
 * between a caption in the wrong face and a graph that will not parse. */
enum { SS_FW_REGULAR, SS_FW_BOLD, SS_FW_LIGHT, SS_FW_ITALIC, SS_FW_BOLDITALIC };

/* How a retimed clip makes the frames that were never shot.
 *
 * `nearest` repeats and drops whole frames, which is what every cut has done
 * here until now and what a 2x speed-up wants. `blend` mixes the two frames
 * either side. `flow` asks minterpolate to estimate the motion between them
 * and build the frame in between, which is the only one that makes slow
 * motion look shot rather than stuttered — and the only one that costs
 * minutes rather than seconds, and can smear at a cut. */
enum { SS_RETIME_NEAREST, SS_RETIME_BLEND, SS_RETIME_FLOW };

/* The shape a fade takes. ffmpeg's own names, not invented ones: afade has
 * twenty-odd curves and these are the six anybody reaches for. `linear` is
 * afade's `tri`, and `qsin` is the equal-power one a crossfade wants — two
 * linear fades sum to a 3dB dip in the middle, which is audible on anything
 * continuous as a hole exactly where the cut is. */
enum { SS_AFADE_LINEAR, SS_AFADE_QSIN, SS_AFADE_HSIN,
       SS_AFADE_ESIN, SS_AFADE_LOG, SS_AFADE_EXP };
int         ss_afade_value(const char *s);
const char *ss_afade_name(int v);
const char *ss_afade_curve(int v);       /* what afade itself calls it */
int         ss_retime_value(const char *s);
const char *ss_retime_name(int v);

int         ss_textweight_value(const char *s);   /* -1 if it is not one */
const char *ss_textweight_name(int v);

/* ---- fonts ----
 *
 * A family NAME is what a person picks; a font FILE is what drawtext takes,
 * because `font=` only works in an ffmpeg built against fontconfig and fails
 * at export time when it is not. So the name is resolved here, once, through
 * fc-match — and when fontconfig is not installed at all this falls back to
 * the same shipped faces the program has always used, rather than handing
 * ffmpeg a path that does not exist.
 *
 * The returned pointer is to a static cache and stays valid; it is never
 * NULL, so a caption always renders in SOMETHING. */
/* Spawn argv and read its stdout as text. fork+execvp, never a shell. */
int         ss_capture(char *const argv[], char *out, size_t n);
/* Pass one of two for the stabiliser: watch the clip's source range and
 * write the per-frame transforms to `trf`. Not part of any graph — the graph
 * READS the file this leaves behind. `shakiness` is 1..10. */
int         ss_stabilise(const char *path, double in, double out,
                         const char *trf, int shakiness);

/* Whether this machine's ffmpeg knows `option` on `filter`. An unknown option
 * fails the whole graph to parse rather than degrading, so anything recent is
 * asked for before it is used. Cached; one fork per process. */
int         ss_ffmpeg_filter_has(const char *filter, const char *option);

const char *ss_font_file(const char *family, int weight);
/* The families this machine can draw with, sorted, one per line into `out`.
 * Returns how many there are, or 0 where fontconfig is missing. */
int         ss_font_families(char *out, size_t n);
/* Whether a family can actually be resolved. A project made elsewhere can
 * name a face this machine has not got, which renders in the default rather
 * than failing — the same rule a missing LUT and a missing effect follow. */
int         ss_font_have(const char *family);

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
/* The first six keep their numbers because a document can carry `trans` as an
 * integer; everything past them is a row in the table in timeline.c, which is
 * ffmpeg's xfade catalogue with its directions mirrored — ours says where the
 * incoming picture comes FROM. */
enum { SS_TRANS_NONE, SS_TRANS_DISSOLVE,
       SS_TRANS_WIPE_L, SS_TRANS_WIPE_R, SS_TRANS_WIPE_U, SS_TRANS_WIPE_D,
       SS_TRANS_DIP };

/* A graded moment. `t` is seconds into the CLIP, not into the timeline, so a
 * keyframed grade survives the clip being moved, trimmed at the tail, or
 * rippled — none of which change what the shot is doing. */
#define SS_MAX_KEYS 8

typedef struct {
    double     t;
    ss_develop dev;
} ss_gradekey;

/* ---- other people's effects ----
 *
 * A recipe is a text manifest naming an ffmpeg filter chain and the knobs on
 * it, so an effect is a FILE somebody can write in an editor and mail to you.
 * See fx.c for the format and for why every one of them is checked against a
 * whitelist before it is loaded.
 *
 * A clip carries a STACK of them, applied in order, after the grade. */
#define SS_MAX_FX         8
#define SS_MAX_FX_PARAMS 10

typedef struct {
    char   key[24], label[48];
    double def, lo, hi;
} ss_fx_param;

typedef struct {
    char name[32], label[48], group[32], about[160];
    int  nparam;
    ss_fx_param param[SS_MAX_FX_PARAMS];
    int  alpha;                 /* the chain can produce transparency */
    char filter[2048];
    char path[512];
} ss_fx;

/* The catalogue: what is installed, then the user's own, then anything named
 * in SYNSTUDIO_EFFECTS. A later one of the same name REPLACES an earlier. */
int          ss_fx_load(void);
int          ss_fx_count(void);
const ss_fx *ss_fx_at(int i);
const ss_fx *ss_fx_find(const char *name);
/* One file, parsed and checked. 0, or -1 with the reason in `err`. */
int          ss_fx_read(const char *path, ss_fx *out, char *err, size_t errn);
/* The chain with its parameters substituted, its labels made unique to `uid`,
 * and [$in]/[$out] replaced by the labels it is being spliced between. */
int          ss_fx_expand(const ss_fx *fx, const double *vals, int nvals,
                          int uid, const char *inlab, const char *outlab,
                          char *out, size_t n);

/* One effect ON a clip: which recipe, and where its knobs are set. The values
 * are positional against the recipe's own parameter list; the DOCUMENT stores
 * them by name, so a recipe that gains a parameter does not shift the meaning
 * of a project saved before it. */
typedef struct {
    char   name[32];
    int    on;
    double val[SS_MAX_FX_PARAMS];
    /* The parameters exactly as the document had them, kept ONLY while the
     * recipe is not installed. Dropping an effect this machine cannot render
     * would delete it from the project the next time it was saved — somebody
     * opening a colleague's cut would quietly throw their work away. */
    char   raw[256];
} ss_clip_fx;

/* ---- a property that moves ----
 *
 * A grade key holds a whole develop stack because colour has to be baked to a
 * cube; everything ELSE about a clip is one number, and ffmpeg will take an
 * expression for most of them. So a parameter key is a NAME from the clip
 * property table, a time and a value — and an animated scale is a string
 * handed to zoompan rather than forty-eight files on disk.
 *
 * `t` is seconds into the CLIP, the same as a grade key and for the same
 * reason: moving, trimming or rippling the clip must not change what the shot
 * is doing.
 *
 * `ease` describes how the value LEAVES this key toward the next one, so the
 * last key's ease is never read, and a `hold` key is how you get a step. */
enum { SS_EASE_LINEAR, SS_EASE_IN, SS_EASE_OUT, SS_EASE_INOUT, SS_EASE_HOLD };

#define SS_MAX_PKEYS 64

typedef struct {
    char   key[24];             /* a clip property key: "opacity", "xform.x" */
    double t, v;
    int    ease;
} ss_propkey;

typedef struct {
    int    kind;                /* SS_CLIP_* */
    char   path[1024];          /* media only */
    int    still;               /* a photograph, not a movie: needs -loop 1 */
    int    has_audio;           /* filled in before an export, never stored */
    int    achannels;           /* likewise: 1 = mono, and a pan must know */
    double src_in, src_out;     /* seconds into the source */
    double tl_in;               /* seconds on the timeline */
    double speed;               /* 1.0 = normal */
    float  gain_db;             /* audio */
    float  opacity;
    double fade_in, fade_out;   /* seconds */

    ss_xform xf;

    int    trans;               /* SS_TRANS_*, into this clip */
    double trans_dur;           /* seconds */
    float  trans_r, trans_g, trans_b;   /* what `dip` dips through */

    /* title / solid */
    char   text[512];           /* \n is a line break; the file escapes it */
    float  text_size;           /* fraction of frame height, 0 = 0.08 */
    float  text_r, text_g, text_b;
    int    text_pos;            /* SS_TEXT_* */
    float  col_r, col_g, col_b; /* SS_CLIP_SOLID, and a title's backdrop */
    float  col_a;               /* backdrop opacity behind a title */

    /* How the caption is DRAWN. Every one of these is a fraction of the font
     * size rather than a pixel count, so a title styled on a 1080 timeline
     * looks the same when the project is delivered at 4K — the same reason
     * text_size is a fraction of the frame height. */
    char   text_font[64];       /* family name; empty = the default face */
    int    text_weight;         /* SS_FW_* */
    float  text_border;         /* outline; < 0 means the old default */
    float  text_shadow;         /* drop shadow offset; 0 = none */
    float  text_box;            /* plate opacity behind the words; 0 = none */
    float  text_line;           /* line spacing; < 0 means the old default */
    float  text_roll;           /* credit roll, screen heights a second */

    /* ---- retime ----
     *
     * `speed` above is the constant case and stays the whole story for almost
     * every clip. These are the rest of it.
     *
     * A RAMP is keys on the `speed` property, and it is the one keyed
     * property whose axis is SOURCE seconds rather than output seconds —
     * because a ramp says "at this point in the shot, run this fast", and
     * because the output length is then the integral of 1/speed over the
     * source span rather than an equation to be solved. Everything that needs
     * either direction goes through ss_clip_retime, so the timeline, the
     * monitor and the export cannot disagree about when a frame plays.
     */
    int    reverse;             /* play the source backwards */
    double freeze;              /* hold ONE source frame; < 0 = off */
    int    retime;              /* SS_RETIME_*, how invented frames are made */

    /* ---- stabilisation ----
     *
     * Two passes, and the first one is not part of the export: vidstabdetect
     * reads the source and writes a .trf beside the project, and only then
     * can vidstabtransform be put in a graph. So `stab` means "there is an
     * analysis on disk for this clip", and it is set by the command that ran
     * it rather than by anybody typing it. */
    int    stab;                /* an analysis exists and is wanted */
    float  stab_smooth;         /* frames of smoothing, vidstabtransform */
    float  stab_zoom;           /* per cent, to hide the moving borders */

    /* ---- the sound of one clip ----
     *
     * Everything here is one ffmpeg filter with one knob on it, in the order
     * a dialogue chain is actually built: clean it, shape it, control it. A
     * compressor with eleven parameters is a compressor nobody sets; the
     * amounts below drive the parameters that matter and leave the rest at
     * values that are right for speech, which is what this is for.
     *
     * Zero means the filter is not in the graph at all — not that it is in
     * the graph doing nothing. */
    float  nr_audio;            /* afftdn, 0..100 */
    float  gate;                /* agate, 0..100 */
    float  eq_db[6];            /* 60, 200, 600, 2k, 6k, 12k — each ±18 dB */
    float  comp;                /* acompressor amount, 0..100 */
    float  comp_thresh;         /* dB, -60..0 */
    float  deess;               /* deesser, 0..100 */
    int    fade_shape;          /* SS_AFADE_*, both fades of this clip */

    int    has_grade;
    ss_develop grade;

    /* A grade that changes over the clip. Empty means `grade` above holds for
     * the whole thing, which is what almost every clip wants and what the
     * file looked like before keyframes existed. */
    int    nkeys;
    ss_gradekey key[SS_MAX_KEYS];

    /* Parameter keys, all properties in ONE list rather than a list per
     * property: a clip with a keyed opacity and nothing else then costs one
     * entry, and the count that matters — how much a clip can carry — is a
     * single number instead of a dozen. Kept sorted by property, then time. */
    int    npkeys;
    ss_propkey pkey[SS_MAX_PKEYS];

    int    nfx;
    ss_clip_fx fx[SS_MAX_FX];
} ss_clip;

void ss_clip_reset(ss_clip *c);

/* ---- a grade that moves ----
 *
 * The renderer SAMPLES a keyframed grade into a fixed number of steps and
 * holds each one for its span, because a 3D LUT is a static table and ffmpeg
 * has no way to fade between two of them. Everything that draws a graded
 * frame goes through these two functions, so the monitor and the export
 * quantise IDENTICALLY and cannot disagree about what a moment looks like —
 * the same reason the transform has one xform_at.
 *
 * The steps are what you see, on both. A grade ramp is therefore a staircase
 * by construction; the step is (the change across the clip) / steps, which
 * for any real grade move is far below a code value. */
int  ss_clip_grade_steps(const ss_clip *c);          /* 1 when it does not move */
/* The develop stack for step `s`. Returns 0 if the clip has no grade at all. */
int  ss_clip_grade_step(const ss_clip *c, int s, ss_develop *out);
/* Which step covers `tt` seconds into the clip. */
int  ss_clip_grade_step_at(const ss_clip *c, double tt);
/* Add, or replace one at the same instant. Returns its index, or -1. */
int  ss_clip_key_add(ss_clip *c, double t, const ss_develop *d);
int  ss_clip_key_remove(ss_clip *c, int i);

/* ---- and the same idea for everything that is not colour ----
 *
 * ss_clip_prop_at is the ONE place a keyed property becomes a number, the way
 * xform_at is the one place a transform does. The monitor calls it; the export
 * generates its filter expressions from the same key list, so a scrub and a
 * render cannot disagree about a move. Where a property must be quantised for
 * the export to express it at all — opacity, which no ffmpeg filter will take
 * an expression for — the quantisation happens IN HERE, so what the monitor
 * shows is what the export writes and not merely close to it. */
int    ss_clip_prop_animatable(const char *key);
double ss_clip_prop_at(const ss_clip *c, const char *key, double tt);
/* -1 if the property cannot be keyed or the clip is full; else the index of
 * the key within that property. A key at an instant that already has one
 * replaces it. */
int    ss_clip_prop_add(ss_clip *c, const char *key, double t, double v, int ease);
/* i < 0 removes every key of that property. 0 on success. */
int    ss_clip_prop_remove(ss_clip *c, const char *key, int i);
int    ss_clip_prop_nkeys(const ss_clip *c, const char *key);
int    ss_clip_prop_key(const ss_clip *c, const char *key, int i, ss_propkey *out);
int    ss_clip_prop_moves(const ss_clip *c, const char *key);  /* 2+ keys */
int    ss_clip_animated(const ss_clip *c);                     /* any at all */
void   ss_clip_prop_range(const ss_clip *c, const char *key, double *lo, double *hi);

/* The effect stack on a clip. `add` seeds every knob from the recipe's own
 * defaults, so an effect that has just landed does what its author meant. */
int    ss_clip_fx_add(ss_clip *c, const char *name, int at);
int    ss_clip_fx_remove(ss_clip *c, int i);
int    ss_clip_fx_move(ss_clip *c, int i, int to);
int    ss_clip_fx_set(ss_clip *c, int i, const char *key, double v);
int    ss_clip_fx_get(const ss_clip *c, int i, const char *key, double *v);
int    ss_ease_value(const char *name);        /* -1 if unknown */
const char *ss_ease_name(int ease);
void ss_xform_reset(ss_xform *x);
/* Length on the TIMELINE: the source span divided by the speed. */
double ss_clip_length(const ss_clip *c);

/* ---- the timebase, in ONE place ----
 *
 * A clip with a constant speed maps output time to source time by
 * multiplying. A clip with a speed RAMP does not, and every part of this
 * program needs the answer: the timeline to know how long the clip is, the
 * monitor to know which source frame to seek to, the export to write a setpts
 * expression, and the audio to know what tempo to run at.
 *
 * So it is sampled ONCE, here, into a table, and everything reads that table.
 * The alternative — an analytic integral in the length function and a
 * separate expression generator in the graph — is two implementations of the
 * same curve that agree until somebody changes an easing.
 *
 * `nseg` segments of constant speed, each covering a span of the SOURCE and a
 * span of the OUTPUT. Returns the number written, which is 1 for the
 * overwhelming majority of clips: a clip whose speed does not move is one
 * segment and costs nothing.
 */
#define SS_MAX_RETIME_SEG 64

typedef struct {
    double src0, src1;          /* seconds into the source, from src_in */
    double out0, out1;          /* seconds into the clip, from its start */
    double speed;               /* what runs during this segment */
} ss_retime_seg;

int    ss_clip_retime(const ss_clip *c, ss_retime_seg *seg, int max);
/* Which second of the SOURCE is playing `tt` seconds into the clip. Handles
 * the ramp, the constant speed and the reverse, so nothing outside has to.
 *
 * `fps` is needed for exactly one of those: reversing maps output frame k to
 * input frame N-1-k, so the source instant is one FRAME back from the mirror
 * of the output time. Without it the monitor shows the frame next door to the
 * one the export writes — which on anything with detail in it is visibly a
 * different picture, and on a slow pan is not, which is worse. */
double ss_clip_source_at(const ss_clip *c, double tt, double fps);
/* Whether this clip's speed moves at all. */
int    ss_clip_has_ramp(const ss_clip *c);

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
    int   animatable;       /* can carry parameter keys */
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
    /* `hidden` is about the PICTURE and `muted` is about the SOUND. They used
     * to be one condition — either dropped the whole track — so muting a video
     * track took its picture away and hiding one took the dialogue with it. */
    int    muted, hidden, solo;
    float  gain_db;             /* the track fader, -60..+24 */
    float  pan;                 /* -1 hard left .. +1 hard right */
    char   name[64];
    /* ---- ducking ----
     *
     * `duck_from` names the track whose sound pushes this one down — the
     * dialogue that a music bed gets out of the way of. -1 is off, which is
     * every track until somebody says otherwise.
     *
     * A track and not a clip, because that is what ducking IS: a relationship
     * between two layers of a mix, not a property of one shot. */
    int    duck_from;
    float  duck;                /* how far down, 0..100 */

    int    nclips, cap;
    ss_clip *clip;
} ss_track;

/* A note pinned to an instant. Not an edit — nothing renders differently for
 * one — which is exactly why they are the spine of any review: they are the
 * only thing in the document you can put where a problem is without changing
 * the cut to say so. */
#define SS_MAX_MARKERS 128
typedef struct {
    double t;
    int    colour;              /* 0..5, an index the window names */
    char   text[160];
} ss_marker;

typedef struct {
    char   name[256];
    int    w, h;
    double fps;
    float  master_db;           /* one fader after the mix */
    int    ntracks;
    ss_track track[SS_MAX_TRACKS];
    int    nmarkers;
    ss_marker marker[SS_MAX_MARKERS];
    /* Where this document's stabiliser analyses live: `<project>.stab`, set
     * when the project is opened and never written into the file. A .trf is
     * MEASURED DATA about a source, not a decision — it is regenerated by
     * asking again, it can be megabytes, and a path recorded in the document
     * would be wrong the moment the project moved. Empty means nobody has
     * opened this from a file, which is what a new document looks like. */
    char   stabdir[1024];

    /* The RENDER RANGE: in and out on the timeline, as a delivery decision
     * rather than an edit. `range_out <= range_in` means the whole thing,
     * which is what every project starts as and what most stay. It is in the
     * document because it is a property of the cut a person set while looking
     * at it, not something to retype on the command line each render. */
    double range_in, range_out;

    /* The loudness a DELIVERY is normalised to, in LUFS — 0 for none, which
     * is what every project starts as. A number here is a statement about the
     * file that leaves: -23 is EBU R128 broadcast, -14 is what the streaming
     * services normalise to anyway. It lives in the document because it is a
     * property of the deliverable, not of one render. */
    float  lufs;
} ss_timeline;

/* What actually gets rendered: the range if there is one, else 0..duration. */
void ss_timeline_range(const ss_timeline *t, double *in, double *out);

/* ---- delivery presets ----
 *
 * A row is a SIZE, a frame rate and a name — "YouTube 1080p" — not a second
 * encoder. The format table already decides how a picture is compressed; a
 * preset decides how big it is and how often, which is the half a person
 * actually chooses by naming a destination.
 *
 * Applied by rendering the whole composite at that size: every clip, the
 * base, the titles and the transitions are all built from the project's own
 * dimensions, so changing those changes all of them together and nothing has
 * to be scaled afterwards. */
typedef struct {
    const char *name;
    int    w, h;
    double fps;                 /* 0 = keep the project's */
    const char *label;
} ss_tl_preset;

const ss_tl_preset *ss_timeline_presets(void);          /* NULL-name terminated */
const ss_tl_preset *ss_timeline_preset(const char *name);

/* Burn-in: what gets written over the delivered picture. Bit flags, because
 * a timecode and a filename are usually both wanted and are one drawtext
 * each. */
enum { SS_BURN_TIMECODE = 1, SS_BURN_NAME = 2 };
int ss_burn_value(const char *s);        /* "timecode", "name", "both", "off" */

/* `<project>.stab`, the one place that spelling is decided. */
void ss_timeline_stabdir(const char *proj, char *out, size_t n);

int ss_timeline_mark(ss_timeline *t, double at, int colour, const char *text);
int ss_timeline_unmark(ss_timeline *t, int i);

/* ── History ─────────────────────────────────────────────────────────────────
 *
 * Undo is a stack of whole DOCUMENTS in `<project>.undo/`, not a stack of
 * inverse operations. A .syntl is a few kilobytes of text and every verb here
 * is a separate process that loads, changes and saves — there is no session to
 * hold a stack in, and an inverse for each of twenty verbs is twenty more
 * things that can be wrong in one direction only.
 *
 * On disk rather than in memory, so it survives the window being closed, the
 * program crashing, and an edit made from the command line in between.
 */
int ss_history_seed(const char *proj);   /* record the file as it is now */
int ss_history_push(const char *proj);   /* record the file as it has become */
int ss_history_undo(const char *proj);   /* 0 done, 1 nothing to undo, -1 error */
int ss_history_redo(const char *proj);
int ss_history_depth(const char *proj, int *undo, int *redo);

/* What a track contributes once mute, hide and solo have had their say.
 * Solo is a property of the WHOLE timeline: one soloed track mutes every other
 * one, which is the only behaviour anybody expects from the word. */
int ss_track_shows(const ss_timeline *t, int track);
int ss_track_sounds(const ss_timeline *t, int track);

/* Integrated loudness and true peak, both in the units everyone quotes:
 * LUFS and dBTP. Measured with ffmpeg's ebur128, which is the same meter the
 * broadcast standards are written against. Returns 0, or -1 if nothing there
 * has a sound track. `in`/`out` bound the measurement; pass 0 and -1 for all
 * of it. */
typedef struct {
    double lufs;                /* integrated, LUFS */
    double peak_db;             /* true peak, dBTP */
    double range;               /* loudness range, LU */
} ss_loudness;
int ss_media_loudness(const char *path, double in, double out, ss_loudness *l);

/* ── Recording ───────────────────────────────────────────────────────────────
 *
 * What can capture. `id` is what to hand `record --device`; `monitor` marks
 * the loopback of an OUTPUT, which records what the machine is playing rather
 * than what the room is saying — useful, and never what somebody asking for a
 * voiceover meant. */
typedef struct {
    char id[192];
    char name[192];
    int  monitor;
    int  is_default;
} ss_device;
int ss_devices(ss_device **out);        /* count, or -1 */

/* Record until `seconds` elapse or a signal arrives, whichever comes first.
 *
 * `on_level` is called with the elapsed time and the peak level in dB while it
 * runs, because the one thing a voiceover has to answer before the take is
 * whether the microphone is live at all. Throttled to about ten a second —
 * ffmpeg offers one per audio frame, which is forty-odd and says nothing more.
 *
 * `fmt` is ffmpeg's input format: "pulse" for a device, "lavfi" for a
 * generated signal, which is how this is tested on a machine with no
 * microphone in it. */
int ss_record(const char *path, const char *fmt, const char *device,
              double seconds, int channels,
              void (*on_level)(double t, double db, void *user), void *user);

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
/* One row per deliverable format. `fmt` is the muxer's extension, which is
 * not always the format's name — ProRes is a .mov. `v1..v4` are the encoder's
 * own arguments, in pairs, and a NULL ends them early. */
typedef struct {
    const char *name;
    const char *ext;
    const char *vcodec;
    const char *acodec;
    const char *v1, *v2, *v3, *v4;
    const char *pix;
    /* The subtitle codec this container takes. A soft subtitle stream is not
     * a picture, so it is the one thing about a delivery that the filter
     * graph knows nothing about — and every container spells it differently
     * (mp4 wants mov_text, Matroska wants srt, WebM wants WebVTT). NULL is a
     * container that cannot carry one, and `--subs` on that format is an
     * error said before the encode starts rather than after it. */
    const char *scodec;
    const char *label;
} ss_tl_format;

/* The table, NULL-name terminated, and the lookup the CLI and the window
 * share: a name if given, otherwise inferred from the output's extension,
 * otherwise the first row. NULL means the name was not one of them. */
const ss_tl_format *ss_timeline_formats(void);
const ss_tl_format *ss_timeline_format(const char *name, const char *out);

/* `subs` is a .srt shipped as a soft stream rather than burnt in — NULL for
 * none, and ignored on a preview. It is an argument and not a project field
 * because it is a property of the DELIVERY: the same cut ships with captions
 * to one place and without them to another. */
int    ss_timeline_ffmpeg(const ss_timeline *t, const char *out,
                          const char *lutdir, int preview,
                          const ss_tl_format *fmt, const char *subs,
                          int burn, char ***argv);

/* ---- title styles ----
 *
 * A lower third is not a feature, it is a handful of the fields above set
 * together: a size, a corner, a plate and a weight. So a style SETS those
 * fields on a clip and then gets out of the way — every one of them is still
 * a slider afterwards, which is the same bargain a look strikes with a grade.
 *
 * Built in rather than a file format, because unlike a look or an effect
 * there is nothing here a third party could not say in four `timeline set`
 * commands, and a catalogue on disk would be a format to maintain for no
 * reach it did not already have. */
typedef struct {
    const char *name;
    const char *label;
} ss_title_style;

const ss_title_style *ss_title_styles(void);        /* NULL-name terminated */
int  ss_title_style_apply(ss_clip *c, const char *name);   /* 0 ok, -1 no */

/* ---- subtitles ----
 *
 * A cue is a title clip. Not a fourth clip kind and not a track type of its
 * own: an imported caption then takes the same font, plate, placement, fades
 * and transform as one that was typed, and it is editable with the commands
 * that already exist. Burning in is therefore free — it is what a title
 * already does — and shipping a soft stream is a delivery option instead.
 *
 * Times are what the file says. A cue whose end is before its start, or that
 * overlaps the one before it, is kept as written: a subtitle file is somebody
 * else's output and quietly repairing it hides the mistake in it. */
int  ss_subs_import(ss_timeline *t, const char *file, int track,
                    char *err, size_t errn);        /* cues added, or -1 */
int  ss_subs_export(const ss_timeline *t, int track, const char *file);

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
/* `at` < 0 bakes everything an export will reach for. `at` >= 0 bakes only
 * what is on screen at that moment, and for a moving grade only the one step
 * under it — the monitor was baking every cube of every clip on every scrub
 * frame, which with a keyframed grade would be dozens of them per frame. */
int    ss_timeline_bake(const ss_timeline *t, const char *dir, double at);
void   ss_timeline_unbake(const ss_timeline *t, const char *dir);

/* Name to enum for the three things a clip line can say. -1 = not a name. */
int         ss_trans_value(const char *s);
int         ss_trans_count(void);
const char *ss_trans_name(int v);
const char *ss_trans_label(int v);
/* The xfade name this kind renders as, or NULL for the two that are not one:
 * `none`, and `dip`, which is a cut under a colour. */
const char *ss_trans_xfade(int v);
int    ss_clip_kind_value(const char *s);
int    ss_textpos_value(const char *s);

/* ----------------------------------------------------------------- util -- */

float ss_srgb_to_linear(float v);
float ss_linear_to_srgb(float v);
float ss_clampf(float v, float lo, float hi);
float ss_luma(float r, float g, float b);

#endif /* SYNSTUDIO_H */
