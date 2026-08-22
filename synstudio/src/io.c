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
#include <sys/prctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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

/* The one spawn-and-read-text path, for callers outside this file.
 *
 * Every subprocess in this program is fork+execvp with an argv ARRAY and no
 * shell anywhere, and a second implementation of that in another file is a
 * second place for a family name out of a project file to become a command
 * line. `out` is always NUL-terminated, even on failure. */
int ss_capture(char *const argv[], char *out, size_t n)
{
    ssize_t got;
    if (!out || n == 0) return -1;
    out[0] = '\0';
    got = run_capture(argv, (unsigned char *)out, n - 1);
    if (got < 0) return -1;
    out[got] = '\0';
    return (int)got;
}

/* Pass one of two: measure how a shot moves, and write it down.
 *
 * This is NOT part of the export graph and cannot be. vidstabtransform reads
 * a file of per-frame transforms that vidstabdetect had to produce by
 * watching the whole clip first, so the analysis is a command somebody runs
 * once and the result is a sidecar that outlives it. Re-running it is how you
 * change the analysis; deleting the file is how you turn it off.
 *
 * ⚠ Measured at the SOURCE's own size, because the numbers it writes are
 * pixels — which is why the transform has to run before anything scales the
 * picture in the graph too.
 */
int ss_stabilise(const char *path, double in, double out, const char *trf,
                 int shakiness)
{
    char *av[20], sbuf[64], ibuf[64], tbuf[64], vf[2200];
    int n = 0, status;
    pid_t pid;

    if (!path || !trf) return -1;
    if (shakiness < 1)  shakiness = 1;
    if (shakiness > 10) shakiness = 10;

    snprintf(ibuf, sizeof ibuf, "%.6f", in > 0 ? in : 0);
    snprintf(tbuf, sizeof tbuf, "%.6f", out > in ? out - in : 0);
    snprintf(sbuf, sizeof sbuf, "%d", shakiness);
    /* `result=` takes a path. It is not a filtergraph read from user text —
     * this is an argv array — but the colon and the backslash still separate
     * a filter's own options, so a path containing one would silently become
     * a different option. The caller builds this path; it is checked anyway. */
    if (strchr(trf, ':') || strchr(trf, '\\') || strchr(trf, '\'')) return -1;
    snprintf(vf, sizeof vf, "vidstabdetect=result=%s:shakiness=%s:accuracy=15",
             trf, sbuf);

    av[n++] = (char *)"ffmpeg";
    av[n++] = (char *)"-v";      av[n++] = (char *)"error";
    av[n++] = (char *)"-nostdin";
    av[n++] = (char *)"-y";
    if (in > 0) { av[n++] = (char *)"-ss"; av[n++] = ibuf; }
    if (out > in) { av[n++] = (char *)"-t"; av[n++] = tbuf; }
    av[n++] = (char *)"-i";      av[n++] = (char *)path;
    av[n++] = (char *)"-vf";     av[n++] = vf;
    av[n++] = (char *)"-f";      av[n++] = (char *)"null";
    av[n++] = (char *)"-";
    av[n] = NULL;

    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) { execvp(av[0], av); _exit(127); }
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) ;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;

    /* An ffmpeg without libvidstab exits non-zero above, so reaching here
     * with no file is the case where the filter ran and found nothing to
     * write — which is still a failure to the caller. */
    {
        FILE *fp = fopen(trf, "rb");
        if (!fp) return -1;
        fclose(fp);
    }
    return 0;
}

/* Does this machine's ffmpeg know that option on that filter?
 *
 * Asked rather than assumed, and asked ONCE. A filter option ffmpeg does not
 * recognise does not degrade — it fails the WHOLE graph to parse, at export
 * time, with a message about the filter and not about the feature somebody
 * turned on in the inspector. text_align arrived in drawtext in 2024, and
 * this program is expected to run on whatever ffmpeg the machine has.
 *
 * Cached because the answer cannot change while the process lives, and the
 * cost is a fork.
 */
int ss_ffmpeg_filter_has(const char *filter, const char *option)
{
    static char lastf[64];
    static char lasto[64];
    static int  lastv = -1;
    char help[262144], arg[128];
    char *av[6];
    const char *p2;
    size_t olen;

    if (!filter || !option) return 0;
    if (lastv >= 0 && !strcmp(lastf, filter) && !strcmp(lasto, option))
        return lastv;

    snprintf(arg, sizeof arg, "filter=%s", filter);
    av[0] = (char *)"ffmpeg";
    av[1] = (char *)"-hide_banner";
    av[2] = (char *)"-h";
    av[3] = arg;
    av[4] = NULL;

    lastv = 0;
    snprintf(lastf, sizeof lastf, "%s", filter);
    snprintf(lasto, sizeof lasto, "%s", option);
    if (ss_capture(av, help, sizeof help) < 0) return 0;

    /* The option name at the start of its own (indented) line, so `align`
     * does not answer for `text_align` and `text` does not answer for
     * `textfile`. */
    olen = strlen(option);
    for (p2 = help; (p2 = strstr(p2, option)) != NULL; p2 += olen) {
        const char *b = p2;
        while (b > help && (b[-1] == ' ' || b[-1] == '\t')) b--;
        if (b != help && b[-1] != '\n') continue;
        if (p2[olen] != ' ' && p2[olen] != '\t') continue;
        lastv = 1;
        break;
    }
    return lastv;
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

/* Same as run_capture but grows its own buffer — used for ffprobe's text.
 *
 * `want_stderr` because some of ffmpeg's answers are only written there:
 * ebur128 prints its whole summary — integrated loudness, range, true peak —
 * to stderr, so a meter that reads stdout reads nothing at all. */
static char *run_text_fd(char *const argv[], int want_stderr)
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
        if (null >= 0) {
            dup2(null, STDIN_FILENO);
            if (!want_stderr) dup2(null, STDERR_FILENO);
            close(null);
        }
        dup2(fd[1], STDOUT_FILENO);
        if (want_stderr) dup2(fd[1], STDERR_FILENO);
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

static char *run_text(char *const argv[])
{
    return run_text_fd(argv, 0);
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
    /* Any `<something>_pipe`, not the two that happened to come up first:
     * ffmpeg names a single-image demuxer that way for every format it has
     * one for — webp_pipe, tiff_pipe, qoi_pipe, jpegxl_pipe — and each one
     * that is not on the list is a photograph treated as a movie. */
    p->is_video = (p->duration > 0.05) &&
                  strstr(p->fmt, "image2") == NULL &&
                  strstr(p->fmt, "_pipe") == NULL;

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

double ss_media_duration(const char *path)
{
    char *argv[] = {
        "ffprobe", "-v", "error",
        "-show_entries", "format=duration",
        "-of", "default=noprint_wrappers=1",
        (char *)path, NULL
    };
    char *txt = run_text(argv);
    double d;

    if (!txt) return 0.0;
    d = kv_dbl(txt, "duration=");
    free(txt);
    return d > 0 ? d : 0.0;
}

int ss_media_channels(const char *path)
{
    char *argv[] = {
        "ffprobe", "-v", "error",
        "-select_streams", "a:0",
        "-show_entries", "stream=channels",
        "-of", "default=noprint_wrappers=1:nokey=1",
        (char *)path, NULL
    };
    char *txt = run_text(argv);
    int n;

    if (!txt) return 0;
    n = atoi(txt);
    free(txt);
    return n > 0 ? n : 0;
}

int ss_media_has_audio(const char *path)
{
    return ss_media_channels(path) > 0;
}

/* What a file IS, asked of ffmpeg rather than of its name.
 *
 * The extension tables in this file are how a DIRECTORY is listed, and they
 * have to be: one process per file would make opening a folder of a thousand
 * photographs cost a thousand of them. A file handed over deliberately — a
 * drop onto the timeline, a path on the command line — is one file and one
 * question, and asking is the only way to accept a format nobody thought to
 * put on a list. That is the difference between "the formats we listed" and
 * "the formats ffmpeg has".
 *
 * Camera raw answers first and without a process: ffprobe on most raw files
 * finds the embedded thumbnail and reports a picture the size of a postage
 * stamp, which is a wrong answer rather than a missing one.
 */
int ss_media_kind(const char *path)
{
    char *argv[] = {
        "ffprobe", "-v", "error",
        "-show_entries",
        "stream=codec_type:stream_disposition=attached_pic:"
        "format=format_name,duration",
        "-of", "default=noprint_wrappers=1",
        (char *)path, NULL
    };
    char *txt;
    const char *p;
    char fmt[256] = "";
    double dur = 0;
    int real_video = 0, have_audio = 0, pending_video = 0;

    if (!path || !*path) return SS_KIND_NONE;

    /* Camera raw answers first and without a process: ffprobe on most raw
     * files finds the embedded thumbnail and reports a picture the size of a
     * postage stamp, which is a wrong answer rather than a missing one. */
    if (is_raw(path)) return SS_KIND_IMAGE;

    txt = run_text(argv);
    if (!txt) return SS_KIND_NONE;

    for (p = txt; *p; ) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[256];

        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = '\0';

        if (!strncmp(line, "codec_type=", 11)) {
            /* A video stream whose disposition never arrived is a real one;
             * only an explicit attached_pic=1 takes that back. */
            if (pending_video) { real_video++; pending_video = 0; }
            if (!strcmp(line + 11, "video"))      pending_video = 1;
            else if (!strcmp(line + 11, "audio")) have_audio = 1;
        } else if (!strncmp(line, "DISPOSITION:attached_pic=", 25)) {
            /* Cover art is a VIDEO STREAM. Calling that a movie is how a
             * whole album ended up on the video track, one still frame long
             * as far as anything downstream could tell. */
            if (pending_video && line[25] == '0') real_video++;
            pending_video = 0;
        } else if (!strncmp(line, "format_name=", 12)) {
            snprintf(fmt, sizeof fmt, "%s", line + 12);
        } else if (!strncmp(line, "duration=", 9)) {
            dur = atof(line + 9);
        }

        if (!nl) break;
        p = nl + 1;
    }
    if (pending_video) real_video++;
    free(txt);

    if (real_video) {
        /* The same test ss_probe_file uses, so a file cannot be a still to one
         * half of this program and a movie to the other. */
        int still = dur <= 0.05
                    || strstr(fmt, "image2") != NULL
                    || strstr(fmt, "_pipe") != NULL;
        return still ? SS_KIND_IMAGE : SS_KIND_VIDEO;
    }
    if (have_audio) return SS_KIND_AUDIO;
    return SS_KIND_NONE;
}

/* -------------------------------------------------------------- peaks -- */

/* Fold a stream of s16le mono samples into buckets as it arrives.
 *
 * Streaming, not buffered, and that is the whole point: an envelope needs the
 * LOUDEST sample in each bucket, so the samples have to be seen at a rate
 * that still contains the signal — and at a rate that contains the signal, an
 * hour of audio is hundreds of megabytes. Reducing on the way past costs one
 * chunk of memory regardless of length.
 *
 * The first attempt did the obvious cheap thing instead: it asked ffmpeg to
 * resample down to about sixteen samples per bucket and took peaks of that.
 * Resampling LOW-PASSES. At 200Hz a 440Hz tone is not quieter, it is GONE —
 * the test file read as digital silence. Anything above half the chosen rate
 * simply is not in the data any more, which for music means the waveform
 * shows the bass line and nothing else. */
static int run_peaks(char *const argv[], size_t expect, int nbuckets,
                     float *peak, float *rms)
{
    int fd[2], status;
    pid_t pid;
    unsigned char chunk[16384];
    double *sum;
    size_t *cnt;
    size_t idx = 0;
    int carry = -1;              /* an odd byte left over from the last read */
    int b;

    sum = calloc((size_t)nbuckets, sizeof *sum);
    cnt = calloc((size_t)nbuckets, sizeof *cnt);
    if (!sum || !cnt) { free(sum); free(cnt); return -1; }
    for (b = 0; b < nbuckets; b++) peak[b] = 0.0f;

    if (pipe(fd) != 0) { free(sum); free(cnt); return -1; }
    pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); free(sum); free(cnt); return -1; }
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
        ssize_t n = read(fd[0], chunk, sizeof chunk);
        ssize_t i = 0;
        if (n < 0) { if (errno == EINTR) continue; break; }
        if (n == 0) break;

        /* A sample is two bytes and a read can split one. Carrying the odd
         * byte is not pedantry: losing it would shift every following sample
         * by one byte and turn the rest of the waveform into noise. */
        if (carry >= 0 && n > 0) {
            int v = (int)(short)(carry | (chunk[0] << 8));
            double a = (double)v / 32768.0, m = a < 0 ? -a : a;
            size_t bi = expect ? idx * (size_t)nbuckets / expect : 0;
            if (bi >= (size_t)nbuckets) bi = (size_t)nbuckets - 1;
            if (m > peak[bi]) peak[bi] = (float)m;
            sum[bi] += a * a;
            cnt[bi]++;
            idx++;
            carry = -1;
            i = 1;
        }
        for (; i + 1 < n; i += 2) {
            int v = (int)(short)(chunk[i] | (chunk[i + 1] << 8));
            double a = (double)v / 32768.0, m = a < 0 ? -a : a;
            size_t bi = expect ? idx * (size_t)nbuckets / expect : 0;
            if (bi >= (size_t)nbuckets) bi = (size_t)nbuckets - 1;
            if (m > peak[bi]) peak[bi] = (float)m;
            sum[bi] += a * a;
            cnt[bi]++;
            idx++;
        }
        if (i < n) carry = chunk[i];
    }
    close(fd[0]);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) ;

    for (b = 0; b < nbuckets; b++) {
        rms[b] = cnt[b] ? (float)sqrt(sum[b] / (double)cnt[b]) : 0.0f;
        if (rms[b] > 1.0f) rms[b] = 1.0f;
        if (peak[b] > 1.0f) peak[b] = 1.0f;
    }
    free(sum);
    free(cnt);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
    return idx > 0 ? 0 : -1;
}

int ss_peaks(const char *path, double in, double out, int nbuckets,
             float *peak, float *rms)
{
    double dur = out - in;
    char ss[32], t[32];
    char *argv[28];
    int n = 0;
    /* Fixed, and high enough to still contain the signal. 8kHz keeps
     * everything below 4kHz, which is where the energy that shapes a waveform
     * lives; the reduction to buckets happens here rather than in the
     * resampler, so the loudest sample survives it. */
    const int rate = 8000;

    if (!path || !*path || nbuckets <= 0 || dur <= 0) return -1;

    snprintf(ss, sizeof ss, "%.6f", in);
    snprintf(t,  sizeof t,  "%.6f", dur);

    argv[n++] = "ffmpeg";
    argv[n++] = "-v"; argv[n++] = "error";
    argv[n++] = "-nostdin";
    if (in > 0.0) { argv[n++] = "-ss"; argv[n++] = ss; }
    argv[n++] = "-i"; argv[n++] = (char *)path;
    argv[n++] = "-t"; argv[n++] = t;
    /* No video, no subtitles, no data. Without these a clip whose first
     * stream is video makes ffmpeg try to put a picture in an s16le container
     * and fail the whole call instead of ignoring it. */
    argv[n++] = "-vn"; argv[n++] = "-sn"; argv[n++] = "-dn";
    argv[n++] = "-ac"; argv[n++] = "1";
    argv[n++] = "-ar"; argv[n++] = "8000";
    argv[n++] = "-f"; argv[n++] = "s16le";
    argv[n++] = "-";
    argv[n] = NULL;

    return run_peaks(argv, (size_t)(dur * rate), nbuckets, peak, rms);
}

/* Integrated loudness, loudness range and true peak.
 *
 * ebur128 is the meter the broadcast standards are written against, so this
 * is not "a number we computed" — it is the number a delivery spec means. It
 * has to DECODE the whole span to answer, which is why it is asked for a clip
 * on demand and never for a directory.
 *
 * The summary arrives on stderr, in a block that looks like:
 *
 *     [Parsed_ebur128_0 @ ...] Summary:
 *       Integrated loudness:
 *         I:         -18.4 LUFS
 *         Threshold: -28.6 LUFS
 *       Loudness range:
 *         LRA:         5.3 LU
 *       True peak:
 *         Peak:       -1.2 dBFS
 *
 * Parsed by finding "Summary:" first, so the per-frame lines above it — which
 * carry an `I:` of their own — cannot be mistaken for the answer.
 */
int ss_media_loudness(const char *path, double in, double out, ss_loudness *l)
{
    char ss[32], to[32];
    char *argv[16];
    char *txt;
    const char *sum, *p;
    int n = 0;

    if (!l) return -1;
    memset(l, 0, sizeof *l);
    if (!path || !*path) return -1;
    if (!ss_media_has_audio(path)) return -1;

    snprintf(ss, sizeof ss, "%.6f", in > 0 ? in : 0.0);
    snprintf(to, sizeof to, "%.6f", out > 0 ? out : 0.0);

    argv[n++] = "ffmpeg";
    argv[n++] = "-v"; argv[n++] = "info";
    argv[n++] = "-nostats";
    if (in > 0) { argv[n++] = "-ss"; argv[n++] = ss; }
    argv[n++] = "-i"; argv[n++] = (char *)path;
    if (out > 0) { argv[n++] = "-t"; argv[n++] = to; }
    argv[n++] = "-af"; argv[n++] = "ebur128=peak=true";
    argv[n++] = "-f"; argv[n++] = "null";
    argv[n++] = "-";
    argv[n] = NULL;

    txt = run_text_fd(argv, 1);
    if (!txt) return -1;

    sum = strstr(txt, "Summary:");
    if (!sum) { free(txt); return -1; }

    p = strstr(sum, "I:");
    if (p) l->lufs = atof(p + 2);
    p = strstr(sum, "LRA:");
    if (p) l->range = atof(p + 4);
    p = strstr(sum, "Peak:");
    if (p) l->peak_db = atof(p + 5);

    free(txt);
    /* Digital silence answers -inf, which ebur128 prints as a very large
     * negative number. Nothing downstream wants to do arithmetic on that. */
    if (l->lufs < -100.0) l->lufs = -100.0;
    if (l->peak_db < -100.0) l->peak_db = -100.0;
    return 0;
}

/* -------------------------------------------------------------- record -- */

/* What can capture, from ffmpeg's own enumeration rather than from pactl —
 * the same reason nothing here links a library: the program that will do the
 * recording is the program that should say what it can record from.
 *
 *     Auto-detected sources for pulse:
 *       alsa_output.pci-….monitor [Monitor of Analog Stereo] (none)
 *     * alsa_input.pci-….analog-stereo [Analog Stereo] (none)
 *
 * The leading `*` is the default. A `.monitor` is the loopback of an output:
 * it records what the machine is PLAYING, which is a real thing to want and
 * never what somebody asking for a voiceover meant, so it is marked rather
 * than hidden.
 */
int ss_devices(ss_device **out)
{
    char *argv[] = { "ffmpeg", "-hide_banner", "-sources", "pulse", NULL };
    char *txt;
    const char *p;
    ss_device *d = NULL;
    int n = 0, cap = 0;

    if (!out) return -1;
    *out = NULL;

    txt = run_text_fd(argv, 1);
    if (!txt) return -1;

    for (p = txt; *p; ) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[512], *br, *id;
        int def = 0;

        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        if (nl) p = nl + 1; else p += len;

        id = line;
        while (*id == ' ' || *id == '\t') id++;
        if (*id == '*') { def = 1; id++; while (*id == ' ') id++; }
        /* Anything without a [name] is the banner or a blank. */
        br = strchr(id, '[');
        if (!br || br == id) continue;

        if (n == cap) {
            ss_device *nd = realloc(d, sizeof *nd * (cap ? cap * 2 : 8));
            if (!nd) { free(d); free(txt); return -1; }
            d = nd; cap = cap ? cap * 2 : 8;
        }
        memset(&d[n], 0, sizeof d[n]);
        {
            size_t idlen = (size_t)(br - id);
            char *close;
            while (idlen && (id[idlen - 1] == ' ' || id[idlen - 1] == '\t')) idlen--;
            if (idlen >= sizeof d[n].id) idlen = sizeof d[n].id - 1;
            memcpy(d[n].id, id, idlen);
            d[n].id[idlen] = '\0';

            close = strchr(br + 1, ']');
            if (close) {
                size_t nl2 = (size_t)(close - br - 1);
                if (nl2 >= sizeof d[n].name) nl2 = sizeof d[n].name - 1;
                memcpy(d[n].name, br + 1, nl2);
                d[n].name[nl2] = '\0';
            }
        }
        d[n].monitor = strstr(d[n].id, ".monitor") != NULL;
        d[n].is_default = def;
        n++;
    }

    free(txt);
    *out = d;
    return n;
}

/* The child, so a signal handler can reach it. One recording at a time is not
 * a limitation worth a structure: a second microphone in the same window is
 * not a thing this program does. */
static volatile sig_atomic_t rec_child = -1;
static volatile sig_atomic_t rec_stop;

static void rec_signal(int sig)
{
    rec_stop = 1;
    /* SIGINT rather than the signal we got: ffmpeg finalises the file on it,
     * and a WAV whose header never got its real length is a take that plays
     * back as three seconds of an eleven minute read. */
    if (rec_child > 0) kill((pid_t)rec_child, SIGINT);
    (void)sig;
}

int ss_record(const char *path, const char *fmt, const char *device,
              double seconds, int channels,
              void (*on_level)(double t, double db, void *user), void *user)
{
    char lim[32], ch[16], af[256];
    char *argv[24];
    int fd[2], status, n = 0, rc = -1;
    pid_t pid;
    struct sigaction sa, old_int, old_term;
    char buf[4096];
    size_t have = 0;
    double last_emit = -1.0, t = 0.0, db = -120.0;

    if (!path || !*path) return -1;
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;

    snprintf(lim, sizeof lim, "%.3f", seconds > 0 ? seconds : 3600.0);
    snprintf(ch, sizeof ch, "%d", channels);
    /* reset=5 is about a tenth of a second of audio frames, which is the rate
     * a meter has to move at to look like a meter.
     *
     * ⚠ `direct=1` is the whole difference between a meter and a receipt.
     * Without it `ametadata=print` writes through avio's 4KB buffer, so a
     * take under about eight seconds delivers NOTHING until ffmpeg exits and
     * then everything at once — which looks exactly like a microphone that
     * was not live, right up until the moment it is too late to matter. */
    snprintf(af, sizeof af,
             "astats=metadata=1:reset=5,"
             "ametadata=print:key=lavfi.astats.Overall.Peak_level:file=-:direct=1");

    argv[n++] = "ffmpeg";
    argv[n++] = "-hide_banner";
    argv[n++] = "-v"; argv[n++] = "error";
    /* A generated source has no clock and will produce an hour of audio in a
     * few seconds, which is not recording — it is rendering. `-re` gives it
     * the one property that makes it stand in for a microphone: it happens at
     * the speed the room does. A real device paces itself and must not get
     * this, or every take drifts. */
    if (fmt && !strcmp(fmt, "lavfi")) argv[n++] = "-re";
    argv[n++] = "-f"; argv[n++] = (char *)(fmt && *fmt ? fmt : "pulse");
    argv[n++] = "-i"; argv[n++] = (char *)(device && *device ? device : "default");
    argv[n++] = "-ac"; argv[n++] = ch;
    argv[n++] = "-ar"; argv[n++] = "48000";
    argv[n++] = "-t"; argv[n++] = lim;
    argv[n++] = "-af"; argv[n++] = af;
    argv[n++] = "-y"; argv[n++] = (char *)path;
    argv[n] = NULL;

    if (pipe(fd) != 0) return -1;
    pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }

    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);
        /* If this process dies without running its handler — SIGKILL, a
         * crash, a terminal closing on it — nothing else would ever tell
         * ffmpeg to stop, and it would sit there holding the microphone open
         * for the rest of the session. Found exactly that way: an orphan from
         * a killed test was still recording an hour later.
         *
         * SIGINT rather than SIGTERM, because it is the one ffmpeg treats as
         * "finish the file". */
        prctl(PR_SET_PDEATHSIG, SIGINT);
        /* The parent may already be gone by the time we get here, in which
         * case the disposition above will never fire. */
        if (getppid() == 1) _exit(0);
        close(fd[0]);
        if (null >= 0) { dup2(null, STDIN_FILENO); close(null); }
        dup2(fd[1], STDOUT_FILENO);
        dup2(fd[1], STDERR_FILENO);
        close(fd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(fd[1]);
    rec_child = (sig_atomic_t)pid;
    rec_stop = 0;

    /* Stopping is the ORDINARY end of a take, not a failure, so the signal is
     * caught rather than left to kill this process — which would leave ffmpeg
     * orphaned and still holding the microphone. */
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = rec_signal;
    sigaction(SIGINT, &sa, &old_int);
    sigaction(SIGTERM, &sa, &old_term);

    for (;;) {
        ssize_t got = read(fd[0], buf + have, sizeof buf - have - 1);
        char *line, *nl;

        if (got < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (got == 0) break;
        have += (size_t)got;
        buf[have] = '\0';

        line = buf;
        while ((nl = strchr(line, '\n'))) {
            *nl = '\0';
            if (!strncmp(line, "frame:", 6)) {
                const char *pt = strstr(line, "pts_time:");
                if (pt) t = atof(pt + 9);
            } else if (!strncmp(line, "lavfi.astats.Overall.Peak_level=", 32)) {
                db = atof(line + 32);
                /* ffmpeg offers one of these per audio frame — forty-odd a
                 * second, all saying the same thing. */
                if (last_emit < 0 || t - last_emit >= 0.1) {
                    last_emit = t;
                    if (on_level) on_level(t, db, user);
                }
            }
            line = nl + 1;
        }
        have = strlen(line);
        memmove(buf, line, have + 1);
    }

    close(fd[0]);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) ;
    rec_child = -1;
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);

    /* Interrupted on purpose is a finished take. ffmpeg exits 255 when it is
     * asked to stop, and calling that a failure would throw the recording
     * away at exactly the moment somebody finished speaking. */
    if (rec_stop) rc = 0;
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) rc = 0;
    return rc;
}

/* ------------------------------------------------------------ formats -- */

/* What a developed photograph can be written as.
 *
 * ss_save pipes raw pixels into ffmpeg and lets the OUTPUT NAME choose the
 * muxer, so this table is not a gate on anything — it is the list the window
 * offers, and it lives here rather than in the QML for the same reason every
 * other table does: the window must not be able to offer something this
 * engine cannot write.
 */
static const ss_still_format still_formats[] = {
    { "jpeg", "jpg",  "JPEG — small, 8-bit, lossy" },
    { "png",  "png",  "PNG — lossless, 8 or 16 bit" },
    { "tiff", "tif",  "TIFF — lossless, for another editor" },
    { "webp", "webp", "WebP — small and lossless-capable" },
    { "bmp",  "bmp",  "BMP — uncompressed, 8-bit" },
    { NULL, NULL, NULL }
};

const ss_still_format *ss_still_formats(void)
{
    return still_formats;
}

/* ------------------------------------------------------------- browse -- */

/* Stills this engine can decode. Camera raw is is_raw()'s list and is not
 * repeated here; the two are unioned at the point of use. */
static int is_still(const char *e)
{
    static const char *ok[] = {
        "jpg","jpeg","jpe","jfif","png","apng","tif","tiff","webp","bmp","dib",
        "gif","heic","heif","avif","jxl","jp2","j2k","jpf","jpx","ppm","pgm",
        "pbm","pnm","pam","pfm","tga","targa","exr","hdr","pic","pcx","sgi",
        "ras","xbm","xpm","xwd","dds","ico","qoi","svg","psd", NULL
    };
    int i;
    for (i = 0; ok[i]; i++) if (!strcasecmp(e, ok[i])) return 1;
    return 0;
}

/* Audio this engine can put on a track. Not a subset of the movie list: a
 * timeline needs a music bed and a voiceover, and neither has a picture. */
static int is_audio(const char *e)
{
    static const char *ok[] = {
        "mp3","mp2","mpa","wav","w64","flac","ogg","oga","opus","spx","m4a",
        "m4b","aac","adts","ac3","eac3","dts","dtshd","thd","mlp","wma","aiff",
        "aif","aifc","ape","wv","tta","tak","mka","caf","au","snd","amr","awb",
        "gsm","ra","mpc","shn","dsf","dff","voc","8svx","xa","alac", NULL
    };
    int i;
    for (i = 0; ok[i]; i++) if (!strcasecmp(e, ok[i])) return 1;
    return 0;
}

static int is_movie(const char *e)
{
    static const char *ok[] = {
        "mp4","m4v","mov","qt","mkv","mk3d","webm","avi","divx","wmv","asf",
        "flv","f4v","swf","mts","m2ts","m2t","ts","mpg","mpeg","mpe","m1v",
        "m2v","mpv","vob","evo","ogv","ogm","ogx","3gp","3g2","mxf","dv","dif",
        "rm","rmvb","nut","y4m","yuv","gxf","roq","nsv","amv","mtv","viv",
        "braw","r3d","avchd","insv","mjpeg","mjpg","h264","h265","hevc","av1",
        "ivf","vp8","vp9","webp_anim", NULL
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
            /* A project is listed too. The picker is how anything gets
             * opened, and a timeline that cannot be reached from it is a
             * document you can only open by typing its path. */
            if (!strcasecmp(e, "syntl"))           type = SS_ROW_PROJECT;
            else if (!strcasecmp(e, "cube") || !strcasecmp(e, "synlook"))
                                                   type = SS_ROW_LOOK;
            else if (is_still(e) || is_raw(de->d_name)) type = SS_ROW_IMAGE;
            else if (is_movie(e))                  type = SS_ROW_VIDEO;
            else if (is_audio(e))                  type = SS_ROW_AUDIO;
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
