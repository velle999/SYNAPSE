/*
 * syn-remote-wol — read and set the magic-packet flag on a network interface.
 *
 * This is the "is it really armed" half of `syn-remote wakeable`. It exists
 * because the two obvious ways to do the job are both wrong here:
 *
 *   - ethtool(8) would be a new dependency for two ioctls, and it is not
 *     installed on a stock SynapseOS;
 *   - NetworkManager can persist the setting (and does — see the wrapper), but
 *     it only APPLIES it when a connection is activated, and activating the
 *     connection you are reaching the machine over drops the link underneath
 *     you. A remote desktop tool must not cut the cable to arm the cable.
 *
 * So: NetworkManager remembers, this applies, and this is also what `wakeable`
 * asks when it reports the state — because a setting and the thing it sets are
 * two different facts, and reporting the first as if it were the second is how
 * a switch says "on" for months with nothing behind it.
 *
 * ⚠ BOTH ioctls want CAP_NET_ADMIN, reading included: ETHTOOL_GWOL can return
 * the SecureOn password, so the kernel guards it like a write. Exit 77 says
 * exactly that, so the caller can escalate ONCE, deliberately, rather than
 * running everything through pkexec on the chance that it is needed.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <errno.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define EX_OK      0
#define EX_ERR     1
#define EX_UNSUPP  2
#define EX_PRIV   77

static int usage(void)
{
    fprintf(stderr,
            "usage: syn-remote-wol get <interface>\n"
            "       syn-remote-wol set <interface> on|off\n");
    return EX_ERR;
}

/* ⛔ Validated, not trusted, because this runs as root under pkexec and the
 * name arrives on argv from a session that is merely active. It never reaches
 * a path or a shell — the ioctl takes it by name — but a name is still the one
 * thing a caller controls, so it is held to what a netdev name can be. */
static int name_ok(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n >= IFNAMSIZ) return 0;
    if (!strcmp(s, ".") || !strcmp(s, "..")) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                 c == '.' || c == ':';
        if (!ok) return 0;
    }
    return 1;
}

static int wol_ioctl(const char *iface, struct ethtool_wolinfo *w)
{
    struct ifreq ifr;
    int fd, rc;

    memset(&ifr, 0, sizeof ifr);
    /* Length is already bounded by name_ok, so this cannot truncate. */
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ifr.ifr_data = (void *)w;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    rc = ioctl(fd, SIOCETHTOOL, &ifr);
    close(fd);
    return rc;
}

/* ⚠ TAB-SEPARATED, key then value, first row naming the columns — the same
 * shape every other machine-readable output in this package has, so the
 * wrapper reads it the way it reads `status --rec`. */
static int report(const char *iface, const struct ethtool_wolinfo *w)
{
    printf("field\tvalue\n");
    printf("interface\t%s\n", iface);
    printf("supported\t%s\n", (w->supported & WAKE_MAGIC) ? "yes" : "no");
    printf("magic\t%s\n",     (w->wolopts   & WAKE_MAGIC) ? "yes" : "no");
    return EX_OK;
}

static int fail(const char *what)
{
    fprintf(stderr, "syn-remote-wol: %s: %s\n", what, strerror(errno));
    return errno == EPERM || errno == EACCES ? EX_PRIV : EX_ERR;
}

int main(int argc, char **argv)
{
    struct ethtool_wolinfo w;
    const char *cmd, *iface;
    int on;

    if (argc < 3) return usage();
    cmd = argv[1];
    iface = argv[2];
    if (!name_ok(iface)) {
        fprintf(stderr, "syn-remote-wol: not an interface name: %s\n", iface);
        return EX_ERR;
    }

    memset(&w, 0, sizeof w);
    w.cmd = ETHTOOL_GWOL;
    if (wol_ioctl(iface, &w) < 0) return fail(iface);

    if (!strcmp(cmd, "get")) {
        if (argc != 3) return usage();
        return report(iface, &w);
    }

    if (strcmp(cmd, "set") || argc != 4) return usage();
    if (!strcmp(argv[3], "on")) on = 1;
    else if (!strcmp(argv[3], "off")) on = 0;
    else return usage();

    /* Said before anything is changed, and as its own exit code: a card that
     * cannot do magic packets is a different answer from one that can and is
     * switched off, and the caller has to be able to tell them apart. */
    if (on && !(w.supported & WAKE_MAGIC)) {
        fprintf(stderr, "syn-remote-wol: %s cannot be woken by a magic packet\n",
                iface);
        return EX_UNSUPP;
    }

    /* ⛔ EXACTLY MAGIC, not `wolopts | WAKE_MAGIC`. The other wake reasons this
     * card offers — ARP, unicast, broadcast — wake the machine for ordinary
     * background traffic, which is how "wake on LAN" turns into "never stays
     * asleep". Turning it off clears every one of them for the same reason:
     * whatever was set, this is the switch that owns the flag now. */
    memset(&w, 0, sizeof w);
    w.cmd = ETHTOOL_SWOL;
    w.wolopts = on ? WAKE_MAGIC : 0;
    if (wol_ioctl(iface, &w) < 0) return fail(iface);

    /* Read it BACK. A driver is free to accept the ioctl and keep its own
     * idea of the flag, and this is the one place that can catch that. */
    memset(&w, 0, sizeof w);
    w.cmd = ETHTOOL_GWOL;
    if (wol_ioctl(iface, &w) < 0) return fail(iface);
    return report(iface, &w);
}
