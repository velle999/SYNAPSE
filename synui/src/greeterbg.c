/*
 * greeterbg.c — the login screen shows the lock screen's background
 *
 * The login screen and the lock screen are the same screen (greeter.c), and
 * they should be the same screen all the way down: one setting for the
 * background, not two. They were not, and the reason is a permission boundary
 * rather than an oversight.
 *
 * ⛔ THE GREETER CANNOT READ THE USER'S ANYTHING. greetd runs `synui --greeter`
 * as the unprivileged `greeter` account, whose home is `/`. So:
 *
 *   - `syn_config_path()` resolves to `//.config/synui/...`, which does not
 *     exist, and `synui_config_load()` falls through to `/etc/synui/synuirc` —
 *     the system defaults, which carry no `lock_*` lines at all. That is why
 *     the login screen has always been black while the lock screen showed a
 *     wallpaper.
 *   - and pointing it at the user's config would not help, because a home
 *     directory is 0700. Neither `~/.config/synui/synuirc` nor a wallpaper
 *     living under it — the default IS `~/.config/synui/wallpaper.png` — is
 *     readable by uid 963.
 *
 * ⚠ SO THE PATH IS NOT ENOUGH; THE PICTURE HAS TO BE COPIED. A design that
 * published only the settings would work for the shipped wallpapers in
 * /usr/share and fail silently for every picture a user actually chose, which
 * is the worst of both: it looks fixed on the developer's box.
 *
 * ── How it works ────────────────────────────────────────────────────────────
 *
 * The user's own session publishes what its lock screen would draw into
 * `/var/lib/synui/greeter/<uid>/`, which the package creates 1777 (sticky, like
 * /tmp) so a session can write it with no privilege and no prompt — a
 * background that needed authenticating would be a background nobody kept in
 * step. `synui --greeter` reads the directory belonging to the account it is
 * about to log in.
 *
 * This is the shape GNOME uses for user avatars on its login screen
 * (/var/lib/AccountsService/icons), for the same reason: the screen that needs
 * the file runs before the user who owns it exists as a session.
 *
 * ⚠ ONE SETTING, TWO READERS. Nothing here is editable. There is no
 * `greeter_background` key and there must not be one — the whole point is that
 * `lock_background`, `lock_image`, `lock_dim` and `lock_blur` decide both
 * screens. This file is a cache of an answer, not a second question.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "synui.h"

#define GREETER_BG_ROOT "/var/lib/synui/greeter"

/* A published picture is copied, so it is worth a ceiling: this is a login
 * screen decoding a file before anybody has authenticated, and a wallpaper is
 * a few megabytes. 64 MiB is far above any real photograph and far below
 * anything that would matter. */
#define GREETER_BG_MAX_BYTES (64 * 1024 * 1024)

/* ── Publishing (runs in the user's session) ─────────────────────────────── */

/* The publish root. SYNUI_GREETER_BG_DIR points it somewhere writable for the
 * tests, exactly as SYNUI_CONFIG and SYNUI_WINDOWS do for their files — the
 * real root is 1777 under /var/lib and a test rig has no business needing root
 * to exercise a code path that is about NOT needing root. */
static void greeterbg_dir(char *buf, size_t n, uid_t uid)
{
    const char *root = getenv("SYNUI_GREETER_BG_DIR");
    snprintf(buf, n, "%s/%u", (root && *root) ? root : GREETER_BG_ROOT,
             (unsigned)uid);
}

/* Copy `src` to `dst` through a temporary in the same directory, then rename.
 *
 * The rename is what makes a half-written picture impossible: the greeter may
 * read this directory at any moment — a laptop lid closing mid-copy is enough —
 * and a partial JPEG is a decode failure, which shows as a black login screen
 * with nothing in any log the user will ever see.
 */
static bool greeterbg_copy(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY | O_CLOEXEC);
    if (in < 0) {
        wlr_log(WLR_INFO, "synui: greeter bg: cannot read %s (%s)",
                src, strerror(errno));
        return false;
    }

    struct stat st;
    if (fstat(in, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size > GREETER_BG_MAX_BYTES) {
        close(in);
        wlr_log(WLR_INFO, "synui: greeter bg: %s is not a regular file "
                          "under %d MiB", src, GREETER_BG_MAX_BYTES >> 20);
        return false;
    }

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
    int out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out < 0) { close(in); return false; }

    char b[64 * 1024];
    ssize_t got;
    bool ok = true;
    while ((got = read(in, b, sizeof(b))) > 0) {
        ssize_t put = 0;
        while (put < got) {
            ssize_t w = write(out, b + put, (size_t)(got - put));
            if (w <= 0) { ok = false; break; }
            put += w;
        }
        if (!ok) break;
    }
    if (got < 0) ok = false;
    close(in);
    if (ok) ok = (fsync(out) == 0);
    close(out);

    if (!ok || rename(tmp, dst) != 0) {
        unlink(tmp);
        wlr_log(WLR_ERROR, "synui: greeter bg: could not publish %s", dst);
        return false;
    }
    return true;
}

/*
 * Write what this user's lock screen would show, for the login screen to read.
 *
 * Cheap to call and safe to call often: the settings file is a few lines, and
 * the picture is copied only when the SOURCE has changed — compared by path,
 * size and mtime, because a wallpaper that cycles through a folder would
 * otherwise copy megabytes on every change and a wallpaper that never changes
 * would copy them on every login.
 */
void greeterbg_publish(syn_server_t *s)
{
    /* Never from the greeter itself. It has no user config to publish, its uid
     * is not a login account, and letting it write would let the login screen
     * overwrite the very thing it is supposed to be reading. */
    if (s->greeter) return;

    uid_t uid = getuid();
    if (uid < 1000) return;         /* not a human login account */

    char dir[256];
    greeterbg_dir(dir, sizeof(dir), uid);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        /* The common case on a box installed before this existed: the sticky
         * root is not there. INFO, not ERROR — the login screen simply keeps
         * the look it had, and a warning per session for a machine that has
         * not been updated yet is noise. */
        wlr_log(WLR_INFO, "synui: greeter bg: no %s (%s) — the login screen "
                          "keeps its own background", GREETER_BG_ROOT,
                strerror(errno));
        return;
    }

    /* What the lock screen resolves to, asked of lock.c so there is one answer
     * and not a second implementation of the DESKTOP case. NULL means black —
     * a legitimate published answer, and the reason the conf is written even
     * when there is no picture. */
    const char *src = lock_bg_source_path(s);

    char conf[512], img[512];
    snprintf(conf, sizeof(conf), "%s/background.conf", dir);
    snprintf(img,  sizeof(img),  "%s/background.img",  dir);

    bool have_img = false;
    if (src && *src) {
        struct stat ss, ds;
        char stamp[512], prev[512] = "";
        if (stat(src, &ss) == 0) {
            snprintf(stamp, sizeof(stamp), "%s %lld %lld", src,
                     (long long)ss.st_size, (long long)ss.st_mtime);
            char sf[512];
            snprintf(sf, sizeof(sf), "%s/background.src", dir);
            FILE *f = fopen(sf, "re");
            if (f) { if (!fgets(prev, sizeof(prev), f)) prev[0] = '\0';
                     fclose(f); prev[strcspn(prev, "\n")] = '\0'; }

            if (strcmp(prev, stamp) == 0 && stat(img, &ds) == 0) {
                have_img = true;             /* already published, unchanged */
            } else if (greeterbg_copy(src, img)) {
                have_img = true;
                FILE *w = fopen(sf, "we");
                if (w) { fprintf(w, "%s\n", stamp); fclose(w); }
            }
        }
    }
    if (!have_img) unlink(img);

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", conf);
    FILE *f = fopen(tmp, "we");
    if (!f) return;
    fprintf(f,
        "# synui — what this account's LOCK screen shows, published so the\n"
        "# LOGIN screen can show the same thing. Written by synui; read by\n"
        "# `synui --greeter`, which runs as another user and can read neither\n"
        "# your config nor your home directory.\n"
        "#\n"
        "# Not a setting. Edit lock_background / lock_image / lock_dim /\n"
        "# lock_blur in synuirc, or Super+Z, and this follows.\n"
        "image = %s\n"
        "dim = %d\n"
        "blur = %d\n",
        have_img ? "background.img" : "",
        s->config.lock_bg_dim, s->config.lock_bg_blur);
    fclose(f);
    if (rename(tmp, conf) != 0) unlink(tmp);

    wlr_log(WLR_DEBUG, "synui: greeter bg: published %s (%s)",
            dir, have_img ? "with a picture" : "plain");
}

/* ── Adopting (runs in the greeter) ──────────────────────────────────────── */

/*
 * Take the published background for `user`, if there is one.
 *
 * ⚠ THE DIRECTORY MUST BELONG TO THAT ACCOUNT. The root is sticky, so only its
 * owner can have created a subdirectory — but "so it cannot happen" is not a
 * check, and this code decodes an image before anybody has logged in. An
 * ownership test costs one stat and makes the trust explicit: this picture was
 * put here by the account that is about to sign in, which is the same trust as
 * that account's own wallpaper.
 */
void greeterbg_adopt(syn_server_t *s, const char *user)
{
    if (!user || !*user) return;

    struct passwd *pw = getpwnam(user);
    if (!pw || pw->pw_uid < 1000) return;

    char dir[256];
    greeterbg_dir(dir, sizeof(dir), pw->pw_uid);

    struct stat st;
    if (lstat(dir, &st) != 0) return;            /* nothing published */
    if (!S_ISDIR(st.st_mode) || st.st_uid != pw->pw_uid) {
        wlr_log(WLR_ERROR, "synui: greeter bg: %s is not a directory owned by "
                           "%s (uid %u) — ignoring it", dir, user,
                (unsigned)pw->pw_uid);
        return;
    }

    char conf[512];
    snprintf(conf, sizeof(conf), "%s/background.conf", dir);
    FILE *f = fopen(conf, "re");
    if (!f) return;

    char line[512], image[256] = "";
    int dim = -1, blur = -1;
    while (fgets(line, sizeof(line), f)) {
        char v[256];
        int n;
        if (sscanf(line, " image = %255[^\n]", v) == 1) {
            /* A bare name, resolved against the directory it came from. The
             * published file never carries an absolute path, so a doctored one
             * cannot point the greeter at /etc/shadow and ask cairo to decode
             * it. */
            char *e = v + strlen(v);
            while (e > v && (e[-1] == ' ' || e[-1] == '\r')) *--e = '\0';
            if (*v && !strchr(v, '/'))
                snprintf(image, sizeof(image), "%s/%s", dir, v);
        } else if (sscanf(line, " dim = %d", &n) == 1) {
            dim = n;
        } else if (sscanf(line, " blur = %d", &n) == 1) {
            blur = n;
        }
    }
    fclose(f);

    if (image[0] && access(image, R_OK) == 0) {
        s->config.lock_bg = SYN_LOCK_BG_IMAGE;
        snprintf(s->config.lock_bg_image, sizeof(s->config.lock_bg_image),
                 "%s", image);
    } else {
        /* Published as plain — the user's lock screen is black, so the login
         * screen is too. DESKTOP would be wrong here: the greeter has no
         * wallpaper of its own to fall back to and would end up black anyway,
         * by accident rather than because it was told to. */
        s->config.lock_bg = SYN_LOCK_BG_BLACK;
        s->config.lock_bg_image[0] = '\0';
    }
    if (dim  >= 0 && dim  <= 100) s->config.lock_bg_dim  = dim;
    if (blur >= 0 && blur <= 64)  s->config.lock_bg_blur = blur;

    wlr_log(WLR_INFO, "synui: greeter bg: showing %s's lock background "
                      "(%s, dim %d, blur %d)", user,
            image[0] ? "picture" : "plain",
            s->config.lock_bg_dim, s->config.lock_bg_blur);
}
