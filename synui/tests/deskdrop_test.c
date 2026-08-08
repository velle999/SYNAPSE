/*
 * deskdrop_test.c — the text/uri-list a drop hands the desktop.
 *
 * Everything on the wire side of deskdrop.c needs a client dragging a real file
 * across a real seat, which no headless rig has. What CAN go wrong without one
 * is the text: a uri-list is percent-encoded, CRLF-terminated, may carry
 * comments, and names files that may or may not be on this machine — and the
 * answer is a path the compositor is about to hand to `cp`. Getting any of it
 * wrong copies the wrong file, or nothing, or truncates a name into a different
 * one.
 *
 * So this includes deskdrop.c whole rather than linking it, which is what gets
 * at the parsing (all of it static, none of it reachable from synui.h), and
 * stubs the four things it calls outward. Nothing here starts a drag.
 *
 * Run as:
 *     ninja -C build && ./build/deskdrop_test
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* Before every include: deskdrop.c's own _GNU_SOURCE lands too late to reach
 * the headers this file has already pulled in, and pipe2() would go missing. */
#define _GNU_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "synui.h"

/* ── The compositor, stubbed ─────────────────────────────── */

void deskicons_reload(syn_server_t *s) { (void)s; }
void deskicons_place_dropped(syn_server_t *s, const char *const *names, int n,
                             int lx, int ly)
{
    (void)s; (void)names; (void)n; (void)lx; (void)ly;
}
void synui_child_reset_signals(void) { }
uint32_t notif_post(syn_server_t *s, const char *app, const char *summary,
                    const char *body, int urgency, int32_t expire,
                    uint32_t replaces)
{
    (void)s; (void)app; (void)summary; (void)body;
    (void)urgency; (void)expire; (void)replaces;
    return 1;
}

#include "deskdrop.c"

/* ── Helpers ─────────────────────────────────────────────── */

static void eq(const char *what, const char *got, const char *want)
{
    if (!got || strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL: %s → \"%s\", expected \"%s\"\n",
                what, got ? got : "(null)", want);
        abort();
    }
}

/* uri_to_path over a NUL-terminated URI, which is how every caller has it. */
static bool to_path(const char *uri, char *out, size_t n)
{
    return uri_to_path(uri, strlen(uri), out, n);
}

static void want_path(const char *uri, const char *expect)
{
    char out[PATH_MAX];
    if (!to_path(uri, out, sizeof(out))) {
        fprintf(stderr, "FAIL: %s was refused, expected \"%s\"\n", uri, expect);
        abort();
    }
    eq(uri, out, expect);
}

static void want_refused(const char *uri)
{
    char out[PATH_MAX];
    if (to_path(uri, out, sizeof(out))) {
        fprintf(stderr, "FAIL: %s was accepted as \"%s\"\n", uri, out);
        abort();
    }
}

int main(void)
{
    /* ── 1. Plain file:// URIs ──────────────────────────── */
    want_path("file:///home/velle/notes.txt", "/home/velle/notes.txt");
    want_path("file:///home/velle/Music",     "/home/velle/Music");
    /* The empty authority is the common spelling, "localhost" the other legal
     * one; both mean this machine. */
    want_path("file://localhost/etc/hosts", "/etc/hosts");
    /* Schemes are case-insensitive, and GTK has shipped both spellings. */
    want_path("FILE:///tmp/x", "/tmp/x");
    printf("ok 1 — local file:// URIs resolve to their paths\n");

    /* ── 2. Percent-encoding ────────────────────────────── */
    /*
     * A file manager encodes everything outside the unreserved set, so spaces,
     * '#', and non-ASCII all arrive as %XX. Decoding them wrong does not fail
     * loudly — it names a file that is not there, and the drop silently copies
     * nothing.
     */
    want_path("file:///home/velle/my%20holiday%20photo.jpg",
              "/home/velle/my holiday photo.jpg");
    want_path("file:///tmp/a%23b", "/tmp/a#b");
    want_path("file:///tmp/%C3%A5%C3%A4%C3%B6", "/tmp/åäö");
    /* Hex is case-insensitive. */
    want_path("file:///tmp/a%2fb", "/tmp/a/b");
    want_path("file:///tmp/a%2Fb", "/tmp/a/b");
    /* A stray '%' that is not an escape stays a '%' rather than eating what
     * follows it — "100%" is a legal filename. */
    want_path("file:///tmp/100%", "/tmp/100%");
    want_path("file:///tmp/100%zz", "/tmp/100%zz");
    printf("ok 2 — percent-escapes decode, and a bare %% survives\n");

    /* ── 3. What is not ours to copy ────────────────────── */
    /*
     * A drag from Firefox carries an http URL and a drag from a remote mount
     * can carry another host. Neither names a file this process can read, and
     * "refused" is what makes the drop say so instead of half-doing it.
     */
    want_refused("http://example.com/x.png");
    want_refused("https://example.com/");
    want_refused("file://otherhost/etc/hosts");
    want_refused("file://");
    want_refused("file:///");          /* the root directory is not a file */
    want_refused("");
    want_refused("/home/velle/notes.txt");   /* a bare path is not a URI */
    /* A NUL in the middle would truncate the path into a different one at the
     * open(), so the whole URI goes. */
    want_refused("file:///tmp/a%00b");
    printf("ok 3 — remote, non-file and malformed URIs are refused\n");

    /* ── 4. A path too long is refused, never truncated ─── */
    /*
     * Truncating names a DIFFERENT file, and this path is going to `cp`. Better
     * to drop one file out of the drag than to copy the wrong thing.
     */
    {
        char uri[64], out[32];
        snprintf(uri, sizeof(uri), "file:///tmp/%s", "0123456789abcdefghij");
        assert(!to_path(uri, out, 16));       /* the path needs 26 bytes */
        assert(to_path(uri, out, sizeof(out)));
        eq("long path", out, "/tmp/0123456789abcdefghij");
    }
    printf("ok 4 — a path that does not fit is refused, not cut short\n");

    /* ── 5. The name a path ends in ─────────────────────── */
    {
        char p[PATH_MAX];
        snprintf(p, sizeof(p), "%s", "/home/velle/Music");
        eq("basename", path_base(p), "Music");
        /* A directory dragged out of a file manager often arrives with its
         * trailing slash; "" is not a name to create. */
        snprintf(p, sizeof(p), "%s", "/home/velle/Music/");
        eq("trailing slash", path_base(p), "Music");
        snprintf(p, sizeof(p), "%s", "/home/velle/Music///");
        eq("trailing slashes", path_base(p), "Music");
        snprintf(p, sizeof(p), "%s", "/");
        assert(path_base(p) == NULL);
    }
    printf("ok 5 — the basename survives trailing slashes; / has none\n");

    /* ── 6. Walking the list ────────────────────────────── */
    /*
     * Multi-file drags are CRLF-separated with a trailing terminator, single
     * ones often have none, and the spec's '#' comments do turn up. A splitter
     * that mishandled any of it would feed "\r" or "#comment" to the URI parser
     * and lose a real file to the refusal count.
     */
    {
        char list[] = "# a comment\r\n"
                      "file:///tmp/one\r\n"
                      "\r\n"
                      "file:///tmp/two\r\n";
        char *cursor = list;
        const char *line;
        size_t len;
        int n = 0;
        const char *want[] = { "file:///tmp/one", "file:///tmp/two" };

        while (uri_list_next(&cursor, &line, &len)) {
            assert(n < 2);
            if (len != strlen(want[n]) || strncmp(line, want[n], len) != 0) {
                fprintf(stderr, "FAIL: line %d = \"%.*s\"\n", n, (int)len, line);
                abort();
            }
            n++;
        }
        assert(n == 2);
    }
    {
        /* One URI, no terminator at all — what a single-file drag looks like. */
        char list[] = "file:///tmp/only";
        char *cursor = list;
        const char *line;
        size_t len;
        assert(uri_list_next(&cursor, &line, &len));
        assert(len == strlen("file:///tmp/only"));
        assert(!uri_list_next(&cursor, &line, &len));
    }
    {
        /* Nothing but comments and blanks yields nothing, rather than looping. */
        char list[] = "#one\n\n#two\n\n";
        char *cursor = list;
        const char *line;
        size_t len;
        assert(!uri_list_next(&cursor, &line, &len));
    }
    printf("ok 6 — the list walk skips comments and blanks, CRLF or LF\n");

    /* ── 7. A drop never overwrites ─────────────────────── */
    /*
     * Two photo.jpg on one desktop is two files. A `cp` straight onto the
     * existing name would destroy the first with nothing to undo it, so the
     * destination is de-duplicated before the child is forked — and the suffix
     * goes before the extension, or the copy stops being a .jpg.
     */
    {
        char dir[] = "/tmp/synui-deskdrop-XXXXXX";
        assert(mkdtemp(dir));

        char out[PATH_MAX], want[PATH_MAX];

        assert(dest_for(dir, "photo.jpg", out, sizeof(out)));
        snprintf(want, sizeof(want), "%s/photo.jpg", dir);
        eq("fresh name", out, want);

        assert(fclose(fopen(out, "w")) == 0);
        assert(dest_for(dir, "photo.jpg", out, sizeof(out)));
        snprintf(want, sizeof(want), "%s/photo (copy).jpg", dir);
        eq("first collision", out, want);

        assert(fclose(fopen(out, "w")) == 0);
        assert(dest_for(dir, "photo.jpg", out, sizeof(out)));
        snprintf(want, sizeof(want), "%s/photo (copy 2).jpg", dir);
        eq("second collision", out, want);

        /* No extension: the suffix simply goes on the end. */
        snprintf(want, sizeof(want), "%s/README", dir);
        assert(fclose(fopen(want, "w")) == 0);
        assert(dest_for(dir, "README", out, sizeof(out)));
        snprintf(want, sizeof(want), "%s/README (copy)", dir);
        eq("no extension", out, want);

        /* A leading dot is a hidden file, not an extension — ".bashrc (copy)",
         * never " (copy).bashrc". */
        snprintf(want, sizeof(want), "%s/.bashrc", dir);
        assert(fclose(fopen(want, "w")) == 0);
        assert(dest_for(dir, ".bashrc", out, sizeof(out)));
        snprintf(want, sizeof(want), "%s/.bashrc (copy)", dir);
        eq("dotfile", out, want);

        /* A dangling symlink is still something in the way: the check is
         * lstat, so a drop cannot land on top of one and follow it. */
        snprintf(want, sizeof(want), "%s/link.txt", dir);
        assert(symlink("/nonexistent", want) == 0);
        assert(dest_for(dir, "link.txt", out, sizeof(out)));
        snprintf(want, sizeof(want), "%s/link (copy).txt", dir);
        eq("dangling symlink", out, want);

        /* Clean up whatever the checks above created. */
        static const char *made[] = {
            "photo.jpg", "photo (copy).jpg", "README", ".bashrc", "link.txt",
        };
        for (unsigned i = 0; i < sizeof(made) / sizeof(made[0]); i++) {
            snprintf(want, sizeof(want), "%s/%s", dir, made[i]);
            unlink(want);
        }
        assert(rmdir(dir) == 0);
    }
    printf("ok 7 — a colliding drop becomes \"(copy)\", extension intact\n");

    printf("\nall deskdrop uri-list checks passed\n");
    return 0;
}
