/*
 * intents.c — the things people actually ask a shell, answered directly.
 *
 * Checked BEFORE ai_translate(). Everything here could in principle be routed
 * through synapd and turned into a shell command, and it was — but that costs a
 * model round-trip to be told the time, and the model does not know what is
 * installed. Asked to play music it will cheerfully suggest `rhythmbox` on a
 * box that has never had it. These are the requests common enough to be worth
 * answering exactly, out of the programs that are really here.
 *
 * The long tail still goes to synapd. What changed there is that the prompt now
 * carries synsh_intent_toolinfo() — the same resolved-from-$PATH list this file
 * acts on — so its suggestions are about this machine.
 *
 * Matching is deliberately dumb — normalise the line, compare it whole — and
 * strict on purpose: an intent may only claim a line that is nothing but the
 * phrase. `play` is a real program (sox ships it, chibi pulls sox in), so a
 * looser match would hijack `play music.wav` away from sox and into here.
 * Anything not recognised falls through to the model, which is the whole point
 * of having one.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>

#include "synsh.h"
#include "intents.h"
#include "exec.h"
#include "color.h"

/* ── Tool discovery ───────────────────────────────────────── */

/* Candidate programs, best first. Resolved against $PATH once.
 *
 * cliamp leads: it is the player SynapseOS actually ships for, and it is a TUI
 * with its own library (Plex/Navidrome/radio/...), so it neither needs nor
 * accepts a directory. Everything after it is a fallback that plays a folder. */
static const char *const music_players[] = {
    "cliamp", "mpv", "vlc", "audacious", "cmus", "mpc", "rhythmbox", NULL
};
static const char *const browsers[] = {
    "firefox", "chromium", "google-chrome-stable", "epiphany", NULL
};
/* synfiles is NOT in this list, and that is not an oversight — it is handled
 * by name in intent_files() because its browser is a SUBCOMMAND
 * (`synfiles gui DIR`); the bare binary is a command-line file tool and would
 * print usage into /dev/null. Keep the rest in step with
 * synui/systemd/synui-open-folder.sh, which makes the same choice the same
 * way. */
static const char *const file_managers[] = {
    "dolphin", "thunar", "nautilus", "pcmanfm", NULL
};
/* For the intents that need a tty when we haven't got one — see run_foreground().
 * $TERMINAL wins if it names something real; syntty is what SynapseOS ships as
 * the default, with kitty and then foot behind it — kitty is what installs
 * between 0.2.5 and 0.2.8 got, and foot is the rescue that renders on the CPU
 * where kitty's OpenGL does not, so a machine that has only one of the three
 * still gets a terminal. */
static const char *const terminals[] = {
    "syntty", "kitty", "foot", "alacritty", "konsole", "xterm", NULL
};

/* Is `prog` on $PATH? execvp-style lookup without spawning anything. */
static bool have(const char *prog)
{
    const char *path = getenv("PATH");
    if (!path || !*path) path = "/usr/bin:/bin";

    char buf[512];
    const char *p = path;
    while (*p) {
        const char *colon = strchr(p, ':');
        size_t dlen = colon ? (size_t)(colon - p) : strlen(p);
        if (dlen && dlen + strlen(prog) + 2 < sizeof(buf)) {
            memcpy(buf, p, dlen);
            buf[dlen] = '/';
            strcpy(buf + dlen + 1, prog);
            if (access(buf, X_OK) == 0) return true;
        }
        if (!colon) break;
        p = colon + 1;
    }
    return false;
}

/* First present entry of a candidate list, or NULL. */
static const char *first_present(const char *const *cands)
{
    for (int i = 0; cands[i]; i++)
        if (have(cands[i])) return cands[i];
    return NULL;
}

const char *synsh_intent_toolinfo(void)
{
    static char buf[512];
    static bool done = false;
    if (done) return buf;

    const char *mus = first_present(music_players);
    const char *web = first_present(browsers);
    const char *fm  = first_present(file_managers);
    /* Ask the same list run_foreground() will actually use, rather than naming
     * one terminal. This used to hardcode `have("foot")`, so a box with kitty
     * and no foot reported `terminal=NONE INSTALLED` to the model — which then
     * had no reason to offer any intent that needs a tty. */
    const char *trm = first_present(terminals);

    snprintf(buf, sizeof(buf),
        "browser=%s file-manager=%s music-player=%s terminal=%s opener=%s",
        web ? web : "NONE INSTALLED",
        fm  ? fm  : "NONE INSTALLED",
        mus ? mus : "NONE INSTALLED",
        trm ? trm : "NONE INSTALLED",
        have("xdg-open") ? "xdg-open" : "NONE INSTALLED");
    done = true;
    return buf;
}

/* ── Matching ─────────────────────────────────────────────── */

/* Normalise for matching: lowercase, collapse runs of whitespace, drop leading
 * '?' (the force-AI prefix) and trailing '?' or '.'. */
static void normalize(char *dst, size_t n, const char *src)
{
    while (*src == '?' || isspace((unsigned char)*src)) src++;

    size_t i = 0;
    bool sp = false;
    for (; *src && i + 1 < n; src++) {
        if (isspace((unsigned char)*src)) { sp = i > 0; continue; }
        if (sp) { dst[i++] = ' '; sp = false; if (i + 1 >= n) break; }
        dst[i++] = (char)tolower((unsigned char)*src);
    }
    while (i > 0 && (dst[i-1] == '?' || dst[i-1] == '.' || dst[i-1] == '!')) i--;
    dst[i] = '\0';
}

/* Whole-line match against a NULL-terminated phrase list.
 *
 * Exact, NOT substring. `play` is a real program (sox ships it, chibi pulls sox
 * in), so a substring match on "play music" would hijack `play music.wav` away
 * from sox and into this file. An intent may only claim a line that is nothing
 * but the phrase; anything with arguments is somebody's actual command. */
static bool line_is(const char *line, const char *const *phrases)
{
    for (int i = 0; phrases[i]; i++)
        if (strcmp(line, phrases[i]) == 0) return true;
    return false;
}

/* Line starts with the phrase and has more after it — for the parameterised
 * intents ("set alarm for 7:30am"). */
static bool line_starts(const char *line, const char *phrase)
{
    size_t n = strlen(phrase);
    return strncmp(line, phrase, n) == 0 && (line[n] == '\0' || line[n] == ' ');
}

/* ── Launching ────────────────────────────────────────────── */

/* Detach a GUI program from the shell: synsh is usually the foreground job in
 * a terminal, and a browser inheriting the tty means closing the terminal
 * takes the browser with it. Double-fork so we do not leave a zombie either. */
/* Two arguments, for a program whose GUI is a subcommand. Kept as its own
 * function rather than a varargs one: every other caller passes a path and
 * nothing else, and a variadic launcher is a way to forget the NULL. */
static int launch_detached2(const char *cmd, const char *a1, const char *a2)
{
    pid_t p = fork();
    if (p < 0) { perror("synsh: fork"); return 1; }
    if (p == 0) {
        if (fork() != 0) _exit(0);
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }
        execlp(cmd, cmd, a1, a2, (char *)NULL);
        _exit(127);
    }
    int st;
    waitpid(p, &st, 0);
    return 0;
}

static int launch_detached(const char *cmd, const char *arg)
{
    pid_t p = fork();
    if (p < 0) { perror("synsh: fork"); return 1; }
    if (p == 0) {
        if (fork() != 0) _exit(0);          /* intermediate exits at once */
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }
        if (arg) execlp(cmd, cmd, arg, (char *)NULL);
        else     execlp(cmd, cmd, (char *)NULL);
        _exit(127);
    }
    int st;
    waitpid(p, &st, 0);                     /* reap the intermediate only */
    return 0;
}

/* ── Running things that need a terminal ──────────────────── */

/*
 * Have we got a tty to hand a full-screen program?
 *
 * synsh in a terminal: yes, and a TUI just takes over the screen. synsh spawned
 * by synui's Super+Space bar: no — stdout is a pipe the compositor drains into
 * the bar, and stdin is /dev/null. Handing cliamp that gets you a player that
 * dies instantly with nowhere to draw, which looks exactly like "the cmdbar
 * still can't play music".
 */
static bool have_tty(void)
{
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

/* $TERMINAL if it is real, else the first one installed. */
static const char *find_terminal(void)
{
    const char *t = getenv("TERMINAL");
    if (t && *t && have(t)) return t;
    return first_present(terminals);
}

/*
 * Run a foreground/interactive command — a TUI, or anything that will prompt.
 *
 * With a tty, that is just execute_command_line(): the command inherits our
 * terminal, which is what someone typing into synsh wants, signal resets and
 * job control included.
 *
 * Without one, the command still has to run *somewhere a person can see and
 * type into*, so we open a terminal for it. `keep_open` is for the ones whose
 * output is the point (pacman): the shell holds the window after the command
 * exits so the result does not flash past. A TUI does not need that — it owns
 * the window until you quit it.
 *
 * The command goes to `sh -c` as ONE argv element, never interpolated into a
 * command string: these fragments contain their own quotes (see the orphans
 * intent) and re-quoting them for a second round of shell parsing is how you
 * get a subtly different command than the one you printed.
 */
static int run_foreground(synsh_state_t *s, const char *cmd, bool keep_open)
{
    if (have_tty())
        return execute_command_line(s, cmd);

    const char *term = find_terminal();
    if (!term) {
        fprintf(stderr, "  synsh: no terminal installed to run: %s\n"
                        "         sudo pacman -S syntty\n", cmd);
        return 1;
    }

    char wrapped[SYNSH_MAX_LINE + 128];
    if (keep_open)
        snprintf(wrapped, sizeof(wrapped),
                 "%s; printf '\\n[done — press enter]'; read _", cmd);
    else
        snprintf(wrapped, sizeof(wrapped), "%s", cmd);

    /* Say where it went. Without this the bar reports nothing and a terminal
     * appears on some other workspace, unexplained. */
    printf("  opening %s\n", term);
    fflush(stdout);

    pid_t p = fork();
    if (p < 0) { perror("synsh: fork"); return 1; }
    if (p == 0) {
        if (fork() != 0) _exit(0);          /* intermediate exits at once */
        setsid();
        /* The terminal must not inherit the caller's pipe: synui waits on EOF
         * to decide the command finished, and a terminal holding the write end
         * open would defer that until the window closed. */
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > 2) close(devnull);
        }
        execlp(term, term, "-e", "sh", "-c", wrapped, (char *)NULL);
        _exit(127);
    }
    int st;
    waitpid(p, &st, 0);                     /* reap the intermediate only */
    return 0;
}

/* ── Intent: time / date ──────────────────────────────────── */

static int intent_time(synsh_state_t *s, bool want_date)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[128];
    strftime(buf, sizeof(buf),
             want_date ? "%A, %e %B %Y" : "%-I:%M %p", &tm);
    /* Squeeze the space strftime's %e leaves on single-digit days. */
    printf("  %s%s%s\n", COLOR_BRAND, buf, COLOR_RESET);
    return 0;
}

/* ── Intent: open a URL / youtube ─────────────────────────── */

static int intent_url(synsh_state_t *s, const char *url)
{
    const char *opener = have("xdg-open") ? "xdg-open" : first_present(browsers);
    if (!opener) {
        fprintf(stderr, "  synsh: no browser and no xdg-open installed\n"
                        "         sudo pacman -S firefox\n");
        return 1;
    }
    printf("  %sopening%s %s\n", COLOR_DIM, COLOR_RESET, url);
    return launch_detached(opener, url);
}

/* ── Intent: file browser ─────────────────────────────────── */

static int intent_files(synsh_state_t *s)
{
    const char *where = s->cwd ? s->cwd : (s->home ? s->home : "/");

    /* SynapseOS's own, and the distribution default for inode/directory since
     * 2026-08-10, so it answers first. */
    if (have("synfiles")) {
        printf("  %sopening%s %s in synfiles\n", COLOR_DIM, COLOR_RESET, where);
        return launch_detached2("synfiles", "gui", where);
    }

    const char *fm = first_present(file_managers);
    if (!fm) {
        /* xdg-open on a directory reaches whatever IS registered, which may be
         * something not in our candidate list. */
        if (have("xdg-open")) fm = "xdg-open";
        else {
            fprintf(stderr, "  synsh: no file manager installed\n"
                            "         sudo pacman -S synfiles\n");
            return 1;
        }
    }
    printf("  %sopening%s %s in %s\n", COLOR_DIM, COLOR_RESET, where, fm);
    return launch_detached(fm, where);
}

/* ── Intent: music ────────────────────────────────────────── */

/* Is a cliamp instance live? It serves IPC on a unix socket, and `cliamp play`
 * only means "resume" if something is there to hear it.
 *
 * This must connect(), not stat(). cliamp does not unlink the socket on the way
 * out, so the node routinely outlives the process — an existence check reports
 * a player that died days ago. A dead socket refuses the connection; that is
 * the only answer worth trusting. `cliamp status` is no help either: it prints
 * "cliamp is not running" and still exits 0. */
static bool cliamp_running(synsh_state_t *s)
{
    const char *cfg = getenv("XDG_CONFIG_HOME");
    char path[512];
    if (cfg && *cfg)
        snprintf(path, sizeof(path), "%s/cliamp/cliamp.sock", cfg);
    else
        snprintf(path, sizeof(path), "%s/.config/cliamp/cliamp.sock",
                 s->home ? s->home : ".");

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    if (strlen(path) >= sizeof(addr.sun_path)) return false;
    memcpy(addr.sun_path, path, strlen(path) + 1);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    bool live = connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
    close(fd);
    return live;
}

static int intent_music(synsh_state_t *s)
{
    const char *player = first_present(music_players);
    if (!player) {
        /* Say so rather than let the model invent a player. */
        fprintf(stderr,
            "  synsh: no music player is installed.\n"
            "         sudo pacman -S mpv        # plays anything, no GUI needed\n"
            "         sudo pacman -S audacious  # if you want a library + playlists\n");
        return 1;
    }

    /* cliamp is not a play-this-folder program, so it skips every directory
     * check below: its library is whatever provider it is configured against,
     * and a machine with no ~/Music is its normal case, not an error.
     *
     * It is also a TUI, so it runs in the FOREGROUND — launch_detached() would
     * hand a full-screen interface /dev/null and a lost tty. run_foreground()
     * is what a typed command goes through when we have a terminal, and opens
     * one when we do not (synui's command bar). */
    if (strcmp(player, "cliamp") == 0) {
        if (cliamp_running(s)) {
            /* Resume is a one-shot IPC call, not the TUI — it needs no terminal
             * either way, and opening one for it would leave an empty window. */
            printf("  %sresuming%s cliamp\n", COLOR_DIM, COLOR_RESET);
            return execute_pipeline(s, "cliamp play");
        }
        printf("  %sstarting%s cliamp\n", COLOR_DIM, COLOR_RESET);
        return run_foreground(s, "cliamp --auto-play --shuffle", false);
    }

    /* Look where music actually lives. XDG first, then the usual suspects. */
    const char *xdg = getenv("XDG_MUSIC_DIR");
    char dir[512];
    if (xdg && *xdg) {
        snprintf(dir, sizeof(dir), "%s", xdg);
    } else {
        snprintf(dir, sizeof(dir), "%s/Music", s->home ? s->home : ".");
    }

    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "  synsh: no music directory (%s)\n", dir);
        return 1;
    }

    printf("  %splaying%s %s with %s\n", COLOR_DIM, COLOR_RESET, dir, player);
    /* mpv understands a directory; the others mostly do too. Shuffle is what
     * "play music" means when you did not name anything. */
    if (strcmp(player, "mpv") == 0) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "mpv --no-video --shuffle --really-quiet '%s' &", dir);
        return system(cmd) == 0 ? 0 : 1;
    }
    return launch_detached(player, dir);
}

/* ── Intent: alarm (chibi's) ──────────────────────────────── */

/* Parse "7", "7am", "7:30", "7:30 pm", "19:30" out of the line. Returns false
 * if there is no time in it at all. */
static bool parse_clock(const char *lower, int *out_h, int *out_m)
{
    const char *p = lower;
    while (*p) {
        if (isdigit((unsigned char)*p)) {
            /* Don't take the "30" of "in 30 minutes" as a clock time. */
            int h = 0, m = 0;
            int n = 0;
            if (sscanf(p, "%d:%d%n", &h, &m, &n) == 2) {
                p += n;
            } else if (sscanf(p, "%d%n", &h, &n) == 1) {
                p += n;
                m = 0;
            } else {
                p++;
                continue;
            }
            if (h < 0 || h > 23 || m < 0 || m > 59) return false;

            /* am/pm may follow with or without a space. */
            const char *q = p;
            while (*q == ' ') q++;
            if (strncmp(q, "pm", 2) == 0 && h < 12) h += 12;
            else if (strncmp(q, "am", 2) == 0 && h == 12) h = 0;

            *out_h = h; *out_m = m;
            return true;
        }
        p++;
    }
    return false;
}

static int intent_alarm(synsh_state_t *s, const char *lower)
{
    int h, m;
    if (!parse_clock(lower, &h, &m)) {
        fprintf(stderr, "  synsh: what time? e.g. \"set alarm for 7:30am\"\n");
        return 1;
    }

    /* Next occurrence of h:m. */
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = 0;
    time_t when = mktime(&tm);
    if (when <= now) when += 24 * 60 * 60;
    localtime_r(&when, &tm);

    char iso[64], pretty[64];
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%S", &tm);
    strftime(pretty, sizeof(pretty), "%-I:%M %p", &tm);

    /* Hand it to chibi rather than growing an alarm of our own: she already has
     * one, with a voice. Her AlarmManager owns ~/.chibi-alarms.json and reloads
     * it when the mtime changes, so appending here is the whole integration.
     *
     * Deliberately not a systemd timer: a timer would fire whether or not chibi
     * is running, and then there would be two alarm systems to keep in step. If
     * chibi is not running this says so instead of pretending. */
    char path[512];
    snprintf(path, sizeof(path), "%s/.chibi-alarms.json",
             s->home ? s->home : ".");

    /* Keys must match chibi's Alarm dataclass EXACTLY: from_dict() is
     * Alarm(**d), so one key it does not declare raises TypeError, _load()
     * catches it as a load error, and every alarm in the file is lost. The
     * fields are time/label/enabled/repeating/repeat_days — "repeating", not
     * "repeat_daily". Verified by round-tripping this through from_dict().
     *
     * The file is small and rewritten wholesale by both sides; read what is
     * there, splice one entry in, write it back atomically. */
    char existing[8192] = {0};
    FILE *f = fopen(path, "r");
    if (f) {
        size_t n = fread(existing, 1, sizeof(existing) - 1, f);
        existing[n] = '\0';
        fclose(f);
    }

    /* Splice into the alarms array: everything up to its ']' is kept.
     *
     * has_entries decides whether our entry needs a leading comma, and it must
     * be judged BETWEEN the brackets — scanning from the start of the file
     * finds the outer object's own '{' and writes `"alarms": [,` into an empty
     * file, which is invalid JSON that chibi's _load() swallows as a load error
     * and answers by dropping every alarm. */
    char *open_br = strchr(existing, '[');
    char *close   = strrchr(existing, ']');
    if (close && (!open_br || close < open_br)) close = NULL;   /* malformed */
    bool has_entries = false;
    if (open_br && close) {
        for (char *q = open_br + 1; q < close; q++)
            if (*q == '{') { has_entries = true; break; }
    }

    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.synsh.tmp", path);
    FILE *out = fopen(tmp, "w");
    if (!out) {
        fprintf(stderr, "  synsh: cannot write %s: %s\n", path, strerror(errno));
        return 1;
    }

    if (close) {
        /* Splice before the closing bracket, keeping what chibi already had. */
        *close = '\0';
        fprintf(out, "%s%s\n    {\n"
                     "      \"time\": \"%s\",\n"
                     "      \"label\": \"\",\n"
                     "      \"enabled\": true,\n"
                     "      \"repeating\": false\n"
                     "    }\n  ]\n}\n",
                existing, has_entries ? "," : "", iso);
    } else {
        fprintf(out, "{\n  \"alarms\": [\n    {\n"
                     "      \"time\": \"%s\",\n"
                     "      \"label\": \"\",\n"
                     "      \"enabled\": true,\n"
                     "      \"repeating\": false\n"
                     "    }\n  ]\n}\n", iso);
    }
    fclose(out);
    if (rename(tmp, path) != 0) {
        fprintf(stderr, "  synsh: cannot replace %s: %s\n", path, strerror(errno));
        unlink(tmp);
        return 1;
    }

    printf("  %salarm set for %s%s\n", COLOR_BRAND, pretty, COLOR_RESET);
    if (system("pgrep -u \"$(id -u)\" -f 'python3 main.py' >/dev/null 2>&1") != 0)
        printf("  %s(chibi is not running — she rings it, so start her before then)%s\n",
               COLOR_DIM, COLOR_RESET);
    return 0;
}

/* ── Intent: packages ─────────────────────────────────────── */

/* The words after `verb`, or NULL if the line does not start with it. */
static const char *rest_after(const char *line, const char *verb)
{
    size_t n = strlen(verb);
    if (strncmp(line, verb, n) != 0 || line[n] != ' ') return NULL;
    const char *p = line + n;
    while (*p == ' ') p++;
    return *p ? p : NULL;
}

/* Do these arguments look like package names, or somebody's real command?
 *
 * `install` is coreutils — `install -m 644 src /usr/bin/` — so this intent may
 * only claim a line whose arguments are bare names. A flag or a path means the
 * user meant coreutils, and we must not swallow it. (`uninstall`, `search` and
 * `upgrade` are not programs at all, so they carry no such risk.) */
static bool looks_like_packages(const char *args)
{
    if (!args || !*args) return false;
    const char *p = args;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (*p == '-') return false;                /* a flag: coreutils */
        while (*p && *p != ' ') {
            if (*p == '/' || *p == '~') return false;  /* a path: coreutils */
            p++;
        }
    }
    return true;
}

/* Show the command, then run it through synsh's own executor so it behaves like
 * anything else typed here. pacman does its own confirming, which is why these
 * do not add a second prompt on top.
 *
 * The sudo ones go through run_foreground(), because that confirming is the
 * whole point: pacman asks "Proceed with installation? [Y/n]" and sudo asks for
 * a password, and both need somewhere to read the answer from. With no tty
 * (synui's command bar) that opens a terminal, rather than feeding pacman
 * /dev/null and reporting whatever it does when its prompt hits EOF.
 *
 * The read-only queries — `pacman -Ss`, `pacman -Q` — deliberately do not.
 * Nothing prompts, so a terminal would only flash their answer up in a window
 * of its own instead of leaving it where it was asked for. Testing for sudo
 * anywhere in the line, not just at the front: the orphans intent buries it
 * mid-pipeline. */
static int run_pkg(synsh_state_t *s, const char *cmd)
{
    printf("  %s%s%s\n", COLOR_CMD, cmd, COLOR_RESET);
    /* The child writes straight to fd 1 while this printf sits in our stdio
     * buffer, so without the flush the command appears *after* its own output. */
    fflush(stdout);
    if (strstr(cmd, "sudo "))
        return run_foreground(s, cmd, true);
    return execute_command_line(s, cmd);
}

static int intent_pkg(synsh_state_t *s, const char *prefix, const char *args)
{
    char cmd[SYNSH_MAX_LINE];
    if (args && *args) snprintf(cmd, sizeof(cmd), "%s %s", prefix, args);
    else               snprintf(cmd, sizeof(cmd), "%s", prefix);
    return run_pkg(s, cmd);
}

/* ── Plain English for the commands people run constantly ─── */

/*
 * Every one of these already worked: they classify as natural language and
 * synapd turns them into the same command. But paying half a second and a 7B
 * model to be told what `pwd` says is silly, and the answer should not change
 * because the model had a bad day.
 *
 * READ-ONLY, deliberately. Nothing here deletes, moves or overwrites anything.
 * "delete the logs" stays on the synapd path, which prints the command and
 * waits for you — a deterministic `rm` would be a worse idea than a slow one,
 * not a better one. The rule for adding to this table: if being wrong would
 * cost you data, it does not belong here.
 */
static const struct {
    const char *const *phrases;
    const char *cmd;
} english_cmds[] = {
    { (const char *const[]){ "list files", "show files", "list the files",
                             "show me the files", "what files are here",
                             "whats in here", "what's in here",
                             "what is in this directory", "list directory", NULL },
      "ls -lh --color=auto" },

    { (const char *const[]){ "list all files", "show hidden files",
                             "show all files", "list everything", NULL },
      "ls -lha --color=auto" },

    { (const char *const[]){ "where am i", "what directory am i in",
                             "current directory", "print working directory",
                             "which directory is this", NULL },
      "pwd" },

    { (const char *const[]){ "disk space", "free space", "how much disk space",
                             "how much space is left", "show disk usage",
                             "disk usage", "df", NULL },
      "df -h" },

    { (const char *const[]){ "memory usage", "how much memory", "how much ram",
                             "ram usage", "free memory", "show memory", NULL },
      "free -h" },

    { (const char *const[]){ "whats running", "what's running",
                             "show processes", "list processes",
                             "what is running", NULL },
      "ps -eo pid,pcpu,pmem,comm --sort=-pcpu | head -15" },

    { (const char *const[]){ "who am i", "what user am i", "my username", NULL },
      "whoami" },

    { (const char *const[]){ "my ip", "my ip address", "what's my ip",
                             "whats my ip", "ip address", "show my ip", NULL },
      "ip -brief address" },

    { (const char *const[]){ "uptime", "how long has this been up",
                             "how long has the system been running", NULL },
      "uptime -p" },

    { (const char *const[]){ "kernel version", "what kernel", "what kernel am i running",
                             "kernel", NULL },
      "uname -r" },

    { (const char *const[]){ "show the log", "show the logs", "system log",
                             "recent errors", "show errors", NULL },
      /* journalctl, never /var/log/syslog — this is not Debian. */
      "journalctl -p err -n 30 --no-pager" },

    { (const char *const[]){ "failed services", "what failed", "show failed", NULL },
      "systemctl --failed --no-pager" },

    { (const char *const[]){ "gpu", "gpu usage", "show gpu", "graphics card", NULL },
      "nvidia-smi" },

    { (const char *const[]){ "temperature", "temps", "how hot", "cpu temperature", NULL },
      "sensors" },

    { NULL, NULL }
};

/* ── Dispatch ─────────────────────────────────────────────── */

/*
 * Claim a line: report that it is ours, and act on it unless we are only being
 * asked whether it is.
 *
 * The check-only mode exists for synui's command bar, which must decide between
 * this file and a synapd round trip *before* it commits to either — and must do
 * it from another process, so the answer has to come from the same table that
 * does the work. Anything that recognises lines twice drifts.
 *
 * Every claim goes through here so that matching and acting cannot disagree:
 * the branch that says "mine" is the branch that runs it.
 */
#define CLAIM(call) do {                        \
        if (!check_only) *exit_code = (call);   \
        return 1;                               \
    } while (0)

int synsh_intent(synsh_state_t *s, const char *line, int *exit_code,
                 bool check_only)
{
    char p[SYNSH_MAX_LINE];
    normalize(p, sizeof(p), line);
    if (!*p) return 0;

    /* Whole-line phrases only — see line_is(). Every one of these is a
     * complete sentence a person types; none is a prefix of a real command. */
    static const char *const time_p[]  = {
        "what time is it", "whats the time", "what's the time",
        "what is the time", "time", "the time", NULL };
    static const char *const date_p[]  = {
        "what day is it", "whats the date", "what's the date",
        "what is the date", "what's today", "whats today", "date",
        "todays date", "today's date", NULL };
    static const char *const music_p[] = {
        "play music", "play some music", "play my music",
        "put on music", "put on some music", NULL };
    static const char *const files_p[] = {
        "open the file browser", "open file browser", "open the file manager",
        "open file manager", "open files", "browse files",
        "file browser", "file manager", NULL };
    static const char *const yt_p[]    = {
        "open youtube", "youtube", "open yt", "launch youtube", NULL };

    if (line_is(p, time_p))  CLAIM(intent_time(s, false));
    if (line_is(p, date_p))  CLAIM(intent_time(s, true));
    if (line_is(p, music_p)) CLAIM(intent_music(s));
    if (line_is(p, files_p)) CLAIM(intent_files(s));
    if (line_is(p, yt_p))    CLAIM(intent_url(s, "https://www.youtube.com"));

    /* Parameterised: "set alarm for 7:30am", "wake me at 7". */
    if (line_starts(p, "set alarm") || line_starts(p, "set an alarm") ||
        line_starts(p, "wake me"))
        CLAIM(intent_alarm(s, p));

    /* ── Packages ──
     * Arch syntax, which is most of the reason these are here at all: asked to
     * install something, a model trained on the whole internet reaches for
     * apt-get, and the suggestion looks perfectly reasonable right up until you
     * run it. Package names on Arch are lowercase, so normalising the line does
     * not cost us anything here.
     *
     * -S --needed, not plain -S: re-installing something already at the right
     * version is a pointless download and, mid-upgrade, a partial-upgrade risk. */
    const char *a;
    if ((a = rest_after(p, "install")) && looks_like_packages(a))
        CLAIM(intent_pkg(s, "sudo pacman -S --needed", a));
    if ((a = rest_after(p, "uninstall")) || (a = rest_after(p, "remove package")))
        /* -Rns: the package, its now-orphaned deps, and its config. Plain -R
         * leaves both behind and that is never what "uninstall" means. */
        CLAIM(intent_pkg(s, "sudo pacman -Rns", a));
    if ((a = rest_after(p, "search for")) || (a = rest_after(p, "search")))
        CLAIM(intent_pkg(s, "pacman -Ss", a));
    if ((a = rest_after(p, "is")) && line_starts(p, "is")) {
        /* "is firefox installed" */
        size_t la = strlen(a);
        const char *suffix = " installed";
        size_t ls = strlen(suffix);
        if (la > ls && strcmp(a + la - ls, suffix) == 0) {
            char pkg[256];
            size_t n = la - ls;
            if (n < sizeof(pkg)) {
                memcpy(pkg, a, n); pkg[n] = '\0';
                if (looks_like_packages(pkg))
                    CLAIM(intent_pkg(s, "pacman -Q", pkg));
            }
        }
    }

    static const char *const update_p[] = {
        "update", "update system", "update the system", "upgrade",
        "upgrade system", "upgrade the system", "update everything",
        "check for updates", "system update", NULL };
    if (line_is(p, update_p))
        /* Always -Syu. A bare -Sy syncs the databases without upgrading, which
         * is a partial upgrade, which is how Arch breaks. */
        CLAIM(intent_pkg(s, "sudo pacman -Syu", NULL));

    /* Plain English for the everyday commands. Last, so a more specific intent
     * above always wins. */
    for (int i = 0; english_cmds[i].phrases; i++) {
        if (line_is(p, english_cmds[i].phrases)) {
            if (check_only) return 1;
            const char *cmd = english_cmds[i].cmd;
            /* Say what it ran. The point of a natural-language shell is that
             * you learn the command, not that it stays hidden. */
            char first[64] = {0};
            sscanf(cmd, "%63s", first);
            if (*first && !have(first) && strchr(first, '|') == NULL) {
                fprintf(stderr, "  synsh: %s is not installed\n", first);
                *exit_code = 127;
                return 1;
            }
            printf("  %s%s%s\n", COLOR_DIM, cmd, COLOR_RESET);
            fflush(stdout);   /* see run_pkg(): child output would race ours */
            *exit_code = execute_command_line(s, cmd);
            return 1;
        }
    }

    static const char *const orphan_p[] = {
        "remove orphans", "clean orphans", "remove orphaned packages", NULL };
    if (line_is(p, orphan_p))
        /* pacman -Qtdq lists true orphans; the guard keeps -Rns from being run
         * with an empty argument list, which would make it prompt for stdin. */
        CLAIM(run_pkg(s,
            "pacman -Qtdq >/dev/null 2>&1 && sudo pacman -Rns $(pacman -Qtdq) "
            "|| echo '  no orphaned packages'"));

    return 0;   /* not ours — let synapd have it */
}

void synsh_intent_help(synsh_state_t *s)
{
    printf("\n  %sAsk in plain English%s — these are answered directly:\n",
           COLOR_BOLD, COLOR_RESET);
    printf("    what time is it        the time\n");
    printf("    what day is it         the date\n");
    printf("    open youtube           %s\n",
           first_present(browsers) ? "in your browser" : "(no browser installed)");
    printf("    open the file browser  %s\n",
           first_present(file_managers) ? first_present(file_managers) : "(none installed)");
    printf("    play music             %s\n",
           first_present(music_players) ? first_present(music_players)
                                        : "(no player installed — sudo pacman -S mpv)");
    printf("    set alarm for 7:30am   chibi rings it\n");
    printf("\n  %sPackages%s (Arch syntax, so you don't have to remember it):\n",
           COLOR_BOLD, COLOR_RESET);
    printf("    install mpv            pacman -S --needed\n");
    printf("    uninstall mpv          pacman -Rns\n");
    printf("    search mpv             pacman -Ss\n");
    printf("    update                 pacman -Syu\n");
    printf("    is firefox installed   pacman -Q\n");
    printf("\n  %sEveryday commands%s, in English:\n", COLOR_BOLD, COLOR_RESET);
    printf("    where am i · list files · disk space · how much memory\n");
    printf("    what's running · my ip · uptime · what kernel\n");
    printf("    show the logs · failed services · gpu · temperature\n");
    printf("\n  Anything else goes to synapd, which answers for %sthis%s machine.\n"
           "  Destructive things stay there on purpose: it shows you the command\n"
           "  and waits, rather than guessing at your files.\n\n",
           COLOR_BOLD, COLOR_RESET);
}
