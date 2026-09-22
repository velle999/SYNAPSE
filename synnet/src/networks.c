/*
 * networks.c — which networks this machine trusts, and asking about new ones.
 *
 * See "Which networks are trusted" in synnet.h for the rule. This file answers
 * three questions for the firewall (which interfaces are on an untrusted
 * network right now), keeps the list of trusted connections, and — run as the
 * desktop user by synnet-ask.service — asks about a network nobody has
 * answered for.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include "../include/synnet.h"
#include "../include/i18n.h"

const char *synnet_fw_networks_path(void) {
    const char *e = getenv("SYNNET_FW_NETWORKS_FILE");
    return (e && *e) ? e : SYNNET_FW_NETWORKS;
}

const char *synnet_networks_state_path(void) {
    const char *e = getenv("SYNNET_NETWORKS_STATE_FILE");
    return (e && *e) ? e : SYNNET_NETWORKS_STATE;
}

/* The seams the test suite drives: a stub nmcli and a fake /sys/class/net. */
static const char *nmcli_bin(void) {
    const char *e = getenv("SYNNET_NMCLI");
    return (e && *e) ? e : "nmcli";
}

static const char *sysfs_net(void) {
    const char *e = getenv("SYNNET_SYSFS_NET");
    return (e && *e) ? e : "/sys/class/net";
}

int synnet_uuid_valid(const char *u) {
    if (!u || strlen(u) != 36) return 0;
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (u[i] != '-') return 0;
        } else if (!isxdigit((unsigned char)u[i])) {
            return 0;
        }
    }
    return 1;
}

/* The same rule monitor.c applies to names it pastes into the nft script: a
 * name outside it is never interpolated. */
static int ifname_ok(const char *n) {
    size_t len = strlen(n);
    if (len == 0 || len >= SYNNET_IFNAME_MAX) return 0;
    if (!isalnum((unsigned char)n[0]) && n[0] != '_') return 0;
    for (size_t i = 0; i < len; i++)
        if (!isalnum((unsigned char)n[i]) && n[i] != '_' && n[i] != '.' &&
            n[i] != '-')
            return 0;
    return 1;
}

/* A connection name goes into a file, a TSV state line and a dialog: no control
 * byte, no tab, no newline. */
static void name_clean(char *s) {
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || *s == 0x7f) *s = ' ';
}

/* One line of `nmcli -t -e yes` into its fields, in place. With -e yes a ':'
 * or a '\' inside a field arrives as "\:" / "\\" — a Wi-Fi name may hold
 * either, and a split on bare ':' would shift every field after it. */
static int nm_split(char *line, char **f, int max) {
    int n = 0;
    char *w = line, *r = line;
    f[n++] = w;
    while (*r && *r != '\n') {
        if (*r == '\\' && r[1]) {
            *w++ = r[1];
            r += 2;
        } else if (*r == ':') {
            *w++ = '\0';
            r++;
            if (n < max) f[n++] = w;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
    return n;
}

/* ── the trusted list ───────────────────────────────────────────────────── */

static int trusted_has(const char *uuid) {
    FILE *f = fopen(synnet_fw_networks_path(), "r");
    if (!f) return 0;
    char line[512];
    int hit = 0;
    while (!hit && fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#') continue;
        if (strncasecmp(p, uuid, 36) == 0 &&
            (p[36] == '\0' || isspace((unsigned char)p[36])))
            hit = 1;
    }
    fclose(f);
    return hit;
}

int synnet_trusted_network_set(const char *uuid, const char *name, int trusted) {
    if (!synnet_uuid_valid(uuid)) return -2;

    const char *path = synnet_fw_networks_path();
    char tmp[PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
        return -1;
    FILE *out = fopen(tmp, "w");
    if (!out) return -1;
    fchmod(fileno(out), 0644);

    /* Everything but this uuid is kept as it was — comments included. */
    FILE *in = fopen(path, "r");
    int wrote_header = 0;
    if (in) {
        char line[512];
        while (fgets(line, sizeof(line), in)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p != '#' && strncasecmp(p, uuid, 36) == 0 &&
                (p[36] == '\0' || isspace((unsigned char)p[36])))
                continue;
            fputs(line, out);
            if (line[strlen(line) - 1] != '\n') fputc('\n', out);
            wrote_header = 1;
        }
        fclose(in);
    }
    if (!wrote_header)
        fputs("# NetworkManager connections synnet trusts: `<uuid> <name>`, one\n"
              "# per line. The name is only a reminder. `synnet --trust-network`\n"
              "# and Settings ▸ Network write this file.\n", out);
    if (trusted) {
        char n[SYNNET_NETNAME_MAX];
        snprintf(n, sizeof(n), "%s", name ? name : "");
        name_clean(n);
        fprintf(out, "%s %s\n", uuid, n);
    }
    if (fclose(out) != 0 || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

/* ── what NetworkManager says ───────────────────────────────────────────── */

int synnet_networks_scan(synnet_net_t *out, size_t max) {
    char cmd[PATH_MAX + 128];
    snprintf(cmd, sizeof(cmd),
             "'%s' -t -e yes -f DEVICE,TYPE,STATE,CON-UUID,CONNECTION "
             "device status 2>/dev/null", nmcli_bin());
    FILE *p = popen(cmd, "r");
    if (!p) return -1;

    int n = 0;
    char line[1024];
    while (fgets(line, sizeof(line), p)) {
        char *f[5] = { 0 };
        if (nm_split(line, f, 5) < 5) continue;
        const char *dev = f[0], *state = f[2], *uuid = f[3], *name = f[4];

        if (!ifname_ok(dev)) continue;
        /* ⚠ PHYSICAL ONLY. A bridge, veth, tun or the tailnet is not a network
         * a stranger is on; the old private-source rule keeps working there. */
        char dp[PATH_MAX];
        snprintf(dp, sizeof(dp), "%s/%s/device", sysfs_net(), dev);
        if (access(dp, F_OK) != 0) continue;
        /* NetworkManager does not manage it, so nothing here knows what it is
         * connected to: leave it to the old rule rather than cut it off. */
        if (strncmp(state, "unmanaged", 9) == 0) continue;

        if ((size_t)n >= max) break;
        synnet_net_t *e = &out[n++];
        memset(e, 0, sizeof(*e));
        snprintf(e->dev, sizeof(e->dev), "%s", dev);
        if (synnet_uuid_valid(uuid)) {
            snprintf(e->uuid, sizeof(e->uuid), "%s", uuid);
            snprintf(e->name, sizeof(e->name), "%s", name);
            name_clean(e->name);
            e->trusted = trusted_has(e->uuid);
        }
    }
    int st = pclose(p);
    /* nmcli that ran and failed — NetworkManager not running — is as good as
     * no answer at all. */
    if (st == -1 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) return -1;
    return n;
}

void synnet_networks_publish(const synnet_net_t *nets, int n) {
    const char *path = synnet_networks_state_path();
    char dir[PATH_MAX], tmp[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) return;
    }
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return;
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fchmod(fileno(f), 0644);
    /* A PROTOCOL, like firewall.state: synnet --ask and syn-settings read it.
     * `#` lines are notes. A device with no connection has no uuid and nothing
     * to ask about, so it is not listed. */
    if (n < 0)
        fputs("# NetworkManager did not answer: private-range sources are "
              "accepted on every interface\n", f);
    for (int i = 0; i < n; i++)
        if (nets[i].uuid[0])
            fprintf(f, "%s\t%s\t%s\t%s\n", nets[i].uuid,
                    nets[i].trusted ? "trusted" : "untrusted",
                    nets[i].dev, nets[i].name);
    fclose(f);
    if (rename(tmp, path) != 0) unlink(tmp);
}

int synnet_network_resolve(const char *arg, char *uuid, size_t usz,
                           char *name, size_t nsz) {
    char cmd[PATH_MAX + 64];
    snprintf(cmd, sizeof(cmd), "'%s' -t -e yes -f UUID,NAME connection show "
             "2>/dev/null", nmcli_bin());
    int by_uuid = synnet_uuid_valid(arg), hits = 0;
    if (by_uuid) {
        snprintf(uuid, usz, "%s", arg);
        if (nsz) name[0] = '\0';
    }
    FILE *p = popen(cmd, "r");
    if (p) {
        char line[1024];
        while (fgets(line, sizeof(line), p)) {
            char *f[2] = { 0 };
            if (nm_split(line, f, 2) < 2 || !synnet_uuid_valid(f[0])) continue;
            if (by_uuid ? strcasecmp(f[0], arg) == 0 : strcmp(f[1], arg) == 0) {
                hits++;
                snprintf(uuid, usz, "%s", f[0]);
                snprintf(name, nsz, "%s", f[1]);
                name_clean(name);
            }
        }
        pclose(p);
    }
    if (by_uuid) return 0;   /* a uuid stands on its own, known or not */
    return hits == 0 ? -1 : hits > 1 ? -2 : 0;
}

/* ── --networks ─────────────────────────────────────────────────────────── */

int synnet_networks_list(void) {
    synnet_net_t nets[SYNNET_MAX_NETWORKS];
    int n = synnet_networks_scan(nets, SYNNET_MAX_NETWORKS);

    if (n < 0) {
        fputs(_("NetworkManager did not answer, so no network is known: "
                "private-range sources are accepted on every interface.\n"),
              stdout);
    } else {
        int shown = 0;
        for (int i = 0; i < n; i++) {
            if (!nets[i].uuid[0]) continue;
            if (!shown++) fputs(_("On now:\n"), stdout);
            /* Two whole sentences, not a word in a slot. */
            if (nets[i].trusted)
                printf(_("  %-10s %s — trusted\n"), nets[i].dev, nets[i].name);
            else
                printf(_("  %-10s %s — NOT trusted: only replies, ping, "
                         "DHCP and open ports get in\n"), nets[i].dev, nets[i].name);
        }
        if (!shown) fputs(_("Not connected to any network.\n"), stdout);
    }

    FILE *f = fopen(synnet_fw_networks_path(), "r");
    int listed = 0;
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            char u[37];
            if (*p == '#' || strlen(p) < 36) continue;
            memcpy(u, p, 36);
            u[36] = '\0';
            if (!synnet_uuid_valid(u)) continue;
            char *nm = p + 36;
            while (*nm == ' ' || *nm == '\t') nm++;
            nm[strcspn(nm, "\r\n")] = '\0';
            if (!listed++) fputs(_("Trusted networks:\n"), stdout);
            printf("  %s  %s\n", u, nm);
        }
        fclose(f);
    }
    if (!listed) fputs(_("No network is trusted yet.\n"), stdout);
    return 0;
}

/* ── --ask, as the desktop user ─────────────────────────────────────────── */

static int run_wait(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) { dup2(nul, 0); close(nul); }
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int state_dir(char *out, size_t sz) {
    const char *x = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");
    if (x && *x) snprintf(out, sz, "%s/synnet", x);
    else if (home && *home) snprintf(out, sz, "%s/.local/state/synnet", home);
    else return -1;
    /* mkdir -p, two levels deep at most below $HOME */
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", out);
    for (char *s = tmp + 1; *s; s++)
        if (*s == '/') { *s = '\0'; mkdir(tmp, 0700); *s = '/'; }
    if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int declined_has(const char *file, const char *uuid) {
    FILE *f = fopen(file, "r");
    if (!f) return 0;
    char line[128];
    int hit = 0;
    while (!hit && fgets(line, sizeof(line), f))
        hit = strncasecmp(line, uuid, 36) == 0;
    fclose(f);
    return hit;
}

int synnet_ask(void) {
    /* No display, no question: the unit also starts at login before the
     * compositor has exported one, and an unanswered network is asked about
     * on the next change or the next login. */
    if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY")) return 0;
    if (geteuid() == 0) {
        fputs(_("synnet: --ask asks the desktop user; run it as them\n"), stderr);
        return 1;
    }

    char dir[PATH_MAX], lock[PATH_MAX + 16], declined[PATH_MAX + 16];
    if (state_dir(dir, sizeof(dir)) != 0) return 1;
    snprintf(lock, sizeof(lock), "%s/.ask.lock", dir);
    snprintf(declined, sizeof(declined), "%s/declined", dir);

    /* One dialog at a time: the path unit fires on every rewrite, and a burst
     * of them must not stack the same question. */
    int lk = open(lock, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lk < 0 || flock(lk, LOCK_EX | LOCK_NB) != 0) return 0;

    FILE *f = fopen(synnet_networks_state_path(), "r");
    if (!f) { close(lk); return 0; }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        char *uuid = strtok(line, "\t\n");
        char *trust = strtok(NULL, "\t\n");
        char *dev = strtok(NULL, "\t\n");
        char *name = strtok(NULL, "\n");
        (void)dev;
        if (!uuid || !trust || !synnet_uuid_valid(uuid)) continue;
        if (strcmp(trust, "untrusted") != 0) continue;
        if (declined_has(declined, uuid)) continue;

        char text[1024], title[128], yes[64], no[64];
        snprintf(text, sizeof(text),
                 _("Trust the network “%s”?\n\n"
                   "On a trusted network, other devices on it can reach this "
                   "computer's shared services — file sharing, media servers, "
                   "remote desktop. Trust your own networks, such as home or "
                   "work. Don't trust public Wi-Fi."),
                 name && *name ? name : uuid);
        snprintf(title, sizeof(title), "%s", _("New network"));
        snprintf(yes, sizeof(yes), "--ok-label=%s", _("Trust"));
        snprintf(no, sizeof(no), "--cancel-label=%s", _("Don't trust"));
        char titlearg[160], textarg[1100];
        snprintf(titlearg, sizeof(titlearg), "--title=%s", title);
        snprintf(textarg, sizeof(textarg), "--text=%s", text);

        char *z[] = { (char *)"zenity", (char *)"--question",
                      (char *)"--no-markup", (char *)"--icon=network-wireless",
                      titlearg, textarg, yes, no, NULL };
        int rc = run_wait(z);
        if (rc == 0) {
            /* pkexec asks for the password; a cancelled prompt records nothing,
             * so the question comes back next time rather than being lost. */
            char *t[] = { (char *)"pkexec", (char *)"/usr/bin/synnet",
                          (char *)"--trust-network", uuid, NULL };
            run_wait(t);
        } else if (rc == 1) {
            FILE *d = fopen(declined, "a");
            if (d) { fprintf(d, "%s\n", uuid); fclose(d); }
        }
        /* anything else — zenity missing, killed, timed out — asks again */
    }
    fclose(f);
    close(lk);
    return 0;
}
