/* io.c — getting pixels in and out.
 *
 * Decoding and encoding are DELEGATED to ffmpeg as a subprocess, and camera
 * raw to libraw's dcraw_emu. Nothing here links either library, and that is a
 * decision with a scar behind it: a SynapseOS component that linked ffmpeg
 * stopped launching the day ffmpeg bumped a SONAME, silently, on an ordinary
 * system upgrade. A pipe has no ABI. The cost is one process per file and a
 * memcpy; the benefit is that this app cannot be broken by somebody else's
 * release.
 *
 * Every subprocess is fork+execvp with an argv ARRAY. No path from this file
 * ever reaches a shell, so a photograph called `; rm -rf ~` is just a
 * photograph with a strange name.
 */
#include "synstudio.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* ------------------------------------------------------------- spawning -- */

/* Run argv, read its stdout into buf until want bytes or EOF. stderr is
 * inherited so ffmpeg's own diagnostics reach the user; every invocation
 * passes -v error, so a working decode is silent. */
static ssize_t run_capture(char *const argv[], unsigned char *buf, size_t want)
{
    int fd[2];
    pid_t pid;
    size_t got = 0;
    int status;

    if (pipe(fd) != 0) return -1;
    pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }

    if (pid == 0) {
        int null = open("/dev/null", O_RDONLY);
        close(fd[0]);
        if (null >= 0) { dup2(null, STDIN_FILENO); close(null); }
        dup2(fd[1], STDOUT_FILENO);
        close(fd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(fd[1]);
    while (got < want) {
        ssize_t n = read(fd[0], buf + got, want - got);
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) break;
        got += (size_t)n;
    }
    /* Drain anything past `want` so the child sees EPIPE rather than blocking
     * forever on a full pipe while we wait for it. */
    {
        unsigned char sink[4096];
        while (read(fd[0], sink, sizeof sink) > 0) ;
    }
    close(fd[0]);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) ;

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
    return (ssize_t)got;
}

/* Run argv, writing data to its stdin. */
static int run_feed(char *const argv[], const unsigned char *data, size_t len)
{
    int fd[2];
    pid_t pid;
    size_t sent = 0;
    int status;
    void (*old)(int);

    if (pipe(fd) != 0) return -1;
    pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }

    if (pid == 0) {
        close(fd[1]);
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(fd[0]);
    /* If the child dies early the write gets SIGPIPE, which would kill US.
     * Take the EPIPE instead and let the exit status be the verdict. */
    old = signal(SIGPIPE, SIG_IGN);
    while (sent < len) {
        ssize_t n = write(fd[1], data + sent, len - sent);
        if (n < 0) { if (errno == EINTR) continue; break; }
        sent += (size_t)n;
    }
    close(fd[1]);
    signal(SIGPIPE, old);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) ;

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
    return sent == len ? 0 : -1;
}

/* Run argv purely for its effect on the filesystem. */
static int run_wait(char *const argv[])
{
    pid_t pid;
    int status;

    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, STDIN_FILENO);
            dup2(null, STDOUT_FILENO);
            close(null);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) ;
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

/* Same as run_capture but grows its own buffer — used for ffprobe's text. */
static char *run_text(char *const argv[])
{
    size_t cap = 8192, len = 0;
    char *buf = malloc(cap);
    int fd[2], status;
    pid_t pid;

    if (!buf) return NULL;
    if (pipe(fd) != 0) { free(buf); return NULL; }
    pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); free(buf); return NULL; }

    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);
        close(fd[0]);
        if (null >= 0) { dup2(null, STDIN_FILENO); dup2(null, STDERR_FILENO); close(null); }
        dup2(fd[1], STDOUT_FILENO);
        close(fd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(fd[1]);
    for (;;) {
        ssize_t n;
        if (len + 4096 > cap) {
            char *nb;
            if (cap > (1u << 22)) break;        /* probe output is never this big */
            cap *= 2;
            nb = realloc(buf, cap);
            if (!nb) break;
            buf = nb;
        }
        n = read(fd[0], buf + len, 4096);
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) break;
        len += (size_t)n;
    }
    close(fd[0]);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) ;
    buf[len < cap ? len : cap - 1] = '\0';

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) { free(buf); return NULL; }
    return buf;
}

/* ---------------------------------------------------------------- probe -- */

static const char *ext_of(const char *path)
{
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    if (!dot || (slash && dot < slash)) return "";
    return dot + 1;
}

/* ffmpeg reads DNG and a few others, but not most camera raw, and when it
 * fails it fails by producing a thumbnail-sized preview rather than an error
 * — which is far worse than refusing. Route the ones libraw owns to libraw. */
static int is_raw(const char *path)
{
    static const char *raws[] = {
        "cr2","cr3","crw","nef","nrw","arw","srf","sr2","raf","orf","rw2",
        "pef","dng","raw","rwl","srw","x3f","3fr","iiq","mos","mef","erf",
        "kdc","dcr","mrw", NULL
    };
    const char *e = ext_of(path);
    int i;
    for (i = 0; raws[i]; i++) if (!strcasecmp(e, raws[i])) return 1;
    return 0;
}

static int kv_int(const char *txt, const char *key)
{
    const char *p = strstr(txt, key);
    if (!p) return 0;
    return atoi(p + strlen(key));
}

static double kv_dbl(const char *txt, const char *key)
{
    const char *p = strstr(txt, key);
    if (!p) return 0.0;
    return atof(p + strlen(key));
}

static void kv_str(const char *txt, const char *key, char *out, size_t n)
{
    const char *p = strstr(txt, key);
    size_t i = 0;
    out[0] = '\0';
    if (!p) return;
    p += strlen(key);
    while (*p && *p != '\n' && *p != '\r' && i + 1 < n) out[i++] = *p++;
    out[i] = '\0';
}

int ss_probe_file(const char *path, ss_probe *p)
{
    char *txt;
    char *argv[] = {
        "ffprobe", "-v", "error",
        "-select_streams", "v:0",
        "-show_entries",
        "stream=width,height,codec_name,r_frame_rate,nb_frames:format=duration,format_name",
        "-of", "default=noprint_wrappers=1",
        (char *)path, NULL
    };

    memset(p, 0, sizeof(*p));

    if (is_raw(path)) {
        /* raw-identify is part of libraw and answers without decoding the
         * whole frame, which on a 60MP file is the difference between
         * instant and a second and a half. */
        char *ra[] = { "raw-identify", "-v", (char *)path, NULL };
        char *rt = run_text(ra);
        if (rt) {
            const char *dim = strstr(rt, "Full size: ");
            if (dim) sscanf(dim + 11, "%d x %d", &p->w, &p->h);
            snprintf(p->fmt, sizeof p->fmt, "raw");
            snprintf(p->codec, sizeof p->codec, "%s", ext_of(path));
            free(rt);
            if (p->w > 0 && p->h > 0) { p->ok = 1; return 0; }
        }
        /* fall through and let ffprobe try the embedded preview */
    }

    txt = run_text(argv);
    if (!txt) return -1;

    p->w = kv_int(txt, "width=");
    p->h = kv_int(txt, "height=");
    p->duration = kv_dbl(txt, "duration=");
    kv_str(txt, "codec_name=", p->codec, sizeof p->codec);
    kv_str(txt, "format_name=", p->fmt, sizeof p->fmt);

    {
        const char *fr = strstr(txt, "r_frame_rate=");
        if (fr) {
            int num = 0, den = 0;
            if (sscanf(fr + 13, "%d/%d", &num, &den) == 2 && den > 0)
                p->fps = (double)num / den;
        }
    }
    /* A still has a frame rate too (ffprobe reports 25/1 for a JPEG), so the
     * frame rate cannot decide this. Duration and the container can: a
     * single-image format is a still no matter what else it says. */
    p->is_video = (p->duration > 0.05) &&
                  strstr(p->fmt, "image2") == NULL &&
                  strstr(p->fmt, "png_pipe") == NULL &&
                  strstr(p->fmt, "jpeg_pipe") == NULL;

    free(txt);
    p->ok = (p->w > 0 && p->h > 0);
    return p->ok ? 0 : -1;
}

/* --------------------------------------------------------------- decode -- */

/* 16 bits per channel, not 8. The develop stack pushes tones around by whole
 * stops; an 8-bit source posterises visibly in a lifted shadow, and the file
 * on disk usually has more than 8 to give. */
static int decode_into(char *const argv[], int w, int h, ss_image *im,
                       int src_is_linear)
{
    size_t npx = (size_t)w * h;
    size_t want = npx * 8;              /* rgba64le */
    unsigned char *raw;
    ssize_t got;
    size_t i;

    if (w <= 0 || h <= 0) return -1;
    raw = malloc(want);
    if (!raw) return -1;

    got = run_capture(argv, raw, want);
    if (got < 0 || (size_t)got != want) { free(raw); return -1; }

    if (ss_image_init(im, w, h) != 0) { free(raw); return -1; }
    for (i = 0; i < npx; i++) {
        const unsigned char *s = raw + i * 8;
        float *d = im->px + i * 4;
        float r = (s[0] | (s[1] << 8)) / 65535.0f;
        float g = (s[2] | (s[3] << 8)) / 65535.0f;
        float b = (s[4] | (s[5] << 8)) / 65535.0f;
        if (src_is_linear) { d[0] = r; d[1] = g; d[2] = b; }
        else { d[0] = ss_srgb_to_linear(r);
               d[1] = ss_srgb_to_linear(g);
               d[2] = ss_srgb_to_linear(b); }
        d[3] = (s[6] | (s[7] << 8)) / 65535.0f;
    }
    free(raw);
    return 0;
}

/* Ask ffmpeg to do the downscale rather than decoding full size and shrinking
 * here. A 60-megapixel raw is 480MB of float once it is in our format; the
 * preview never needs to have existed at that size. */
static void fit_dims(int sw, int sh, int max_edge, int *dw, int *dh)
{
    int longest = sw > sh ? sw : sh;
    if (max_edge <= 0 || longest <= max_edge) { *dw = sw; *dh = sh; return; }
    if (sw >= sh) { *dw = max_edge; *dh = (int)((double)sh * max_edge / sw + 0.5); }
    else          { *dh = max_edge; *dw = (int)((double)sw * max_edge / sh + 0.5); }
    if (*dw < 1) *dw = 1;
    if (*dh < 1) *dh = 1;
    /* An odd dimension is legal for rawvideo; no rounding to even here,
     * because that would silently change the aspect ratio of a preview. */
}

static int load_via_ffmpeg(const char *path, double t, ss_image *im,
                           int max_edge, int src_is_linear)
{
    ss_probe p;
    char vf[64], ts[32];
    int dw, dh, n = 0;
    char *argv[24];

    if (ss_probe_file(path, &p) != 0) return -1;
    fit_dims(p.w, p.h, max_edge, &dw, &dh);

    argv[n++] = "ffmpeg";
    argv[n++] = "-v"; argv[n++] = "error";
    argv[n++] = "-nostdin";
    if (t > 0.0) {
        /* Before -i, so the seek is a keyframe seek and does not decode the
         * whole clip up to t. */
        snprintf(ts, sizeof ts, "%.4f", t);
        argv[n++] = "-ss"; argv[n++] = ts;
    }
    argv[n++] = "-i"; argv[n++] = (char *)path;
    argv[n++] = "-frames:v"; argv[n++] = "1";
    if (dw != p.w || dh != p.h) {
        snprintf(vf, sizeof vf, "scale=%d:%d:flags=area", dw, dh);
        argv[n++] = "-vf"; argv[n++] = vf;
    }
    argv[n++] = "-f"; argv[n++] = "rawvideo";
    argv[n++] = "-pix_fmt"; argv[n++] = "rgba64le";
    argv[n++] = "-";
    argv[n] = NULL;

    return decode_into(argv, dw, dh, im, src_is_linear);
}

/* Camera raw, through libraw.
 *
 * -4 is the important flag: LINEAR 16-bit with no auto-brightening. A raw file
 * is scene-referred by nature and this engine works scene-referred, so having
 * dcraw apply a display gamma and an automatic exposure lift only to undo it
 * here would throw away the highlight headroom that is the entire reason to
 * shoot raw. The TIFF it writes is fed to ffmpeg purely as a container.
 *
 * dcraw_emu writes NEXT TO ITS INPUT and the exact name depends on its own
 * suffix rules, so the input is symlinked into a private directory and
 * whatever appears in there is the output. That also keeps the app from ever
 * dropping a .tiff into the folder the photographs live in.
 */
static int load_raw(const char *path, ss_image *im, int max_edge)
{
    char dir[] = "/tmp/synstudio-raw-XXXXXX";
    char link[sizeof dir + 16], out[sizeof dir + 320];
    char *argv[] = { "dcraw_emu", "-w", "-q", "3", "-4", "-T", "-o", "1",
                     link, NULL };
    DIR *dp;
    struct dirent *de;
    int rc = -1;

    if (!mkdtemp(dir)) return -1;
    snprintf(link, sizeof link, "%s/in.%s", dir, ext_of(path));

    /* An absolute path is required: the symlink is resolved relative to the
     * directory it lives in, not to our cwd. */
    if (path[0] == '/') {
        if (symlink(path, link) != 0) goto out;
    } else {
        char abs[4096], cwd[3072];
        if (!getcwd(cwd, sizeof cwd)) goto out;
        snprintf(abs, sizeof abs, "%s/%s", cwd, path);
        if (symlink(abs, link) != 0) goto out;
    }

    if (run_wait(argv) != 0) goto out;

    out[0] = '\0';
    dp = opendir(dir);
    if (dp) {
        while ((de = readdir(dp))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            if (!strncmp(de->d_name, "in.", 3) &&
                !strcmp(de->d_name + 3, ext_of(path))) continue;   /* the symlink */
            snprintf(out, sizeof out, "%s/%s", dir, de->d_name);
            break;
        }
        closedir(dp);
    }
    if (!out[0]) goto out;

    rc = load_via_ffmpeg(out, 0.0, im, max_edge, 1);
    unlink(out);
out:
    unlink(link);
    rmdir(dir);
    return rc;
}

int ss_load(const char *path, ss_image *im, int max_edge)
{
    if (is_raw(path) && strcasecmp(ext_of(path), "dng")) {
        if (load_raw(path, im, max_edge) == 0) return 0;
        /* Fall back rather than fail: a raw ffmpeg can read is better than
         * nothing, and the user is told which path ran by the pixel count. */
    }
    return load_via_ffmpeg(path, 0.0, im, max_edge, 0);
}

int ss_load_frame(const char *path, double t, ss_image *im, int max_edge)
{
    return load_via_ffmpeg(path, t, im, max_edge, 0);
}

/* --------------------------------------------------------------- encode -- */

int ss_save(const char *path, const ss_image *im, int quality, int bits)
{
    size_t npx = (size_t)im->w * im->h;
    unsigned char *raw;
    char size[32], q[16], *argv[24];
    int n = 0, rc;
    size_t i;
    int sixteen = (bits >= 16);

    if (npx == 0) return -1;
    raw = malloc(npx * (sixteen ? 8u : 4u));
    if (!raw) return -1;

    for (i = 0; i < npx; i++) {
        const float *s = im->px + i * 4;
        int c;
        for (c = 0; c < 4; c++) {
            /* Alpha is already linear coverage; only colour is encoded. */
            float v = (c == 3) ? s[3] : ss_linear_to_srgb(s[c]);
            v = ss_clampf(v, 0.0f, 1.0f);
            if (sixteen) {
                unsigned u = (unsigned)(v * 65535.0f + 0.5f);
                raw[i * 8 + c * 2]     = (unsigned char)(u & 0xff);
                raw[i * 8 + c * 2 + 1] = (unsigned char)(u >> 8);
            } else {
                raw[i * 4 + c] = (unsigned char)(v * 255.0f + 0.5f);
            }
        }
    }

    snprintf(size, sizeof size, "%dx%d", im->w, im->h);
    /* ffmpeg's -q:v for mjpeg runs 2 (best) to 31 (worst), the inverse of the
     * 1..100 everyone types. Map it here so the CLI can speak the usual
     * language. */
    snprintf(q, sizeof q, "%d",
             31 - (int)((ss_clampf((float)quality, 1.0f, 100.0f) - 1) / 99.0f * 29.0f));

    argv[n++] = "ffmpeg";
    argv[n++] = "-v"; argv[n++] = "error";
    argv[n++] = "-y";
    argv[n++] = "-f"; argv[n++] = "rawvideo";
    argv[n++] = "-pix_fmt"; argv[n++] = sixteen ? "rgba64le" : "rgba";
    argv[n++] = "-s"; argv[n++] = size;
    argv[n++] = "-i"; argv[n++] = "-";
    argv[n++] = "-frames:v"; argv[n++] = "1";
    argv[n++] = "-q:v"; argv[n++] = q;
    argv[n++] = (char *)path;
    argv[n] = NULL;

    rc = run_feed(argv, raw, npx * (sixteen ? 8u : 4u));
    free(raw);
    return rc;
}

/* ------------------------------------------------------------- browse -- */

/* Stills this engine can decode. Camera raw is is_raw()'s list and is not
 * repeated here; the two are unioned at the point of use. */
static int is_still(const char *e)
{
    static const char *ok[] = {
        "jpg","jpeg","jpe","png","tif","tiff","webp","bmp","gif","heic","heif",
        "avif","jxl","ppm","pgm","pnm","tga","exr","hdr", NULL
    };
    int i;
    for (i = 0; ok[i]; i++) if (!strcasecmp(e, ok[i])) return 1;
    return 0;
}

static int is_movie(const char *e)
{
    static const char *ok[] = {
        "mp4","m4v","mov","mkv","webm","avi","wmv","flv","mts","m2ts","ts",
        "mpg","mpeg","vob","ogv","3gp","mxf","dv","braw","r3d", NULL
    };
    int i;
    for (i = 0; ok[i]; i++) if (!strcasecmp(e, ok[i])) return 1;
    return 0;
}

/* Parent, then directories, then files; case-insensitive by name within each
 * band. Sorting on the enum directly would be wrong the moment a value is
 * inserted, so the band is spelled out. */
static int row_band(int type)
{
    if (type == SS_ROW_UP)  return 0;
    if (type == SS_ROW_DIR) return 1;
    return 2;
}

static int row_cmp(const void *a, const void *b)
{
    const ss_row *x = a, *y = b;
    int bx = row_band(x->type), by = row_band(y->type);
    if (bx != by) return bx - by;
    return strcasecmp(x->name, y->name);
}

int ss_browse(const char *dir, ss_row **rows, char abs_out[1024])
{
    char req[1024];
    ss_row *r = NULL;
    int n = 0, cap = 0;
    DIR *dp;
    struct dirent *de;

    if (!dir || !*dir) dir = ".";

    /* ~ is expanded here rather than left to a shell, because no path in this
     * program ever reaches one. */
    if (dir[0] == '~' && (dir[1] == '/' || dir[1] == '\0')) {
        const char *home = getenv("HOME");
        if (!home) home = "/";
        snprintf(req, sizeof req, "%s%s", home, dir + 1);
    } else {
        snprintf(req, sizeof req, "%s", dir);
    }

    /* realpath resolves . .. and symlinks, so the window's idea of where it is
     * matches the filesystem's and `..` from a symlinked directory goes where
     * the user can see it went.
     *
     * ⚠ realpath with a non-NULL buffer writes up to PATH_MAX, which is 4096
     * here — handing it the caller's 1024 is a stack smash on a deep path, and
     * one that only fires for somebody with long directory names. Resolve into
     * a full-size buffer and refuse anything that will not fit. */
    {
        char full_res[PATH_MAX];
        if (!realpath(req, full_res)) return -1;
        if (strlen(full_res) >= 1024) { errno = ENAMETOOLONG; return -1; }
        memcpy(abs_out, full_res, strlen(full_res) + 1);
    }

    dp = opendir(abs_out);
    if (!dp) return -1;

    /* The parent, unless we are at the root and there is none. */
    if (strcmp(abs_out, "/")) {
        char up[1024];
        char *slash;
        snprintf(up, sizeof up, "%s", abs_out);
        slash = strrchr(up, '/');
        if (slash) *(slash == up ? slash + 1 : slash) = '\0';
        r = malloc(sizeof *r * (cap = 64));
        if (!r) { closedir(dp); return -1; }
        r[n].type = SS_ROW_UP;
        snprintf(r[n].name, sizeof r[n].name, "..");
        snprintf(r[n].path, sizeof r[n].path, "%s", up);
        n++;
    }

    while ((de = readdir(dp))) {
        char full[1024];
        struct stat st;
        int type;

        /* Dotfiles are skipped, including . and .. — the parent is the row
         * built above, which is one row rather than one per level. */
        if (de->d_name[0] == '.') continue;

        if ((size_t)snprintf(full, sizeof full, "%s/%s",
                             strcmp(abs_out, "/") ? abs_out : "",
                             de->d_name) >= sizeof full) continue;

        /* stat, not d_type: d_type is DT_UNKNOWN on some filesystems and is
         * DT_LNK for a symlink to a directory, which should still be a row you
         * can walk into. */
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            type = SS_ROW_DIR;
        } else if (S_ISREG(st.st_mode)) {
            const char *e = ext_of(de->d_name);
            if (is_still(e) || is_raw(de->d_name)) type = SS_ROW_IMAGE;
            else if (is_movie(e))                  type = SS_ROW_VIDEO;
            else continue;
        } else {
            continue;
        }

        if (n == cap) {
            ss_row *g = realloc(r, sizeof *g * (cap = cap ? cap * 2 : 64));
            if (!g) { free(r); closedir(dp); return -1; }
            r = g;
        }
        r[n].type = type;
        snprintf(r[n].name, sizeof r[n].name, "%s", de->d_name);
        snprintf(r[n].path, sizeof r[n].path, "%s", full);
        n++;
    }
    closedir(dp);

    if (n > 1) qsort(r, (size_t)n, sizeof *r, row_cmp);
    *rows = r;
    return n;
}
