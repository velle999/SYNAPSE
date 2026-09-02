/*
 * bt.c — Bluetooth (Super+B), a native BlueZ client.
 *
 * SYNAPSE had no Bluetooth at all: not a package, not a service, not a line of
 * UI, while the machine sat there with a live radio. This is the first thing
 * built under "make everything we can native from here on", so it talks
 * org.bluez over sd-bus directly rather than shelling out to bluetoothctl and
 * scraping its output, or bolting on blueman's GTK tray. The panel is drawn by
 * the compositor, so it can be handed the keyboard — see menu.c for why that
 * matters and what the alternative cost us.
 *
 * THE RULE THAT SHAPES THIS FILE: never block the wl_event_loop. A radio can
 * take seconds to answer, and one sync sd_bus_call() would freeze every window
 * on the desktop until it did. So there is not a single blocking call here —
 * every method is sd_bus_call_async(), and the panel repaints when the reply or
 * a PropertiesChanged signal arrives. The bus fd lives in the Wayland event
 * loop (screensaver.c's idiom), so a closed panel costs nothing.
 *
 * State is never computed locally and hoped for: it is read back from BlueZ's
 * ObjectManager and PropertiesChanged. Pressing "connect" does not mark a device
 * connected — BlueZ saying so does. Guessing is how a UI ends up disagreeing
 * with the hardware.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <systemd/sd-bus.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_damage_ring.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "synui.h"
#include "i18n.h"

#define BT_SVC    "org.bluez"
#define BT_ADAPT  "org.bluez.Adapter1"
#define BT_DEV    "org.bluez.Device1"
#define BT_BATT   "org.bluez.Battery1"

/* Our pairing agent. KeyboardDisplay is the capability that covers the most
 * hardware with one implementation: headphones and phones take the
 * RequestConfirmation path, and a Bluetooth keyboard takes DisplayPasskey (it
 * shows a number you type on the keyboard itself). A NoInputNoOutput agent would
 * be simpler and would quietly fail to pair a keyboard. */
#define BT_AGENT_PATH "/org/synui/bt/agent"
#define BT_AGENT_CAP  "KeyboardDisplay"

static struct {
    sd_bus                 *bus;
    struct wl_event_source *src;
    /* The agent request BlueZ is currently blocked on, kept so it can be
     * answered when the user decides. Reffed: it must outlive the callback. */
    sd_bus_message         *pending;
} bt;

static void bt_refresh(syn_server_t *s);

/* ── Repaint ─────────────────────────────────────────────── */

static void bt_repaint(syn_server_t *s)
{
    if (s->bt.visible) synui_render_bt(s);
}

/* ── Device list ─────────────────────────────────────────── */

static syn_bt_dev_t *dev_by_path(syn_bt_t *b, const char *path)
{
    for (int i = 0; i < b->count; i++)
        if (strcmp(b->devs[i].path, path) == 0) return &b->devs[i];
    return NULL;
}

static syn_bt_dev_t *dev_add(syn_bt_t *b, const char *path)
{
    syn_bt_dev_t *d = dev_by_path(b, path);
    if (d) return d;
    if (b->count >= BT_DEVICES_MAX) return NULL;
    d = &b->devs[b->count++];
    memset(d, 0, sizeof(*d));
    snprintf(d->path, sizeof(d->path), "%s", path);
    d->battery = -1;
    return d;
}

/* ── Naming ──────────────────────────────────────────────── */

/* Is this "name" just BlueZ spelling the address back at us?
 *
 * BlueZ's Alias property is never absent: with no alias set and no name learned
 * it falls back to the address with ':' turned into '-' (dev_property_get_alias
 * → g_strdelimit(dstaddr, ":", '-')), and Name is simply not published at all.
 * So a device that never advertised a name arrives here with a perfectly valid
 * Alias of "84-9E-56-B6-EE-FA" — which is why the panel looked like a list of
 * MAC addresses. There is no property to test for this: the only way to know an
 * alias is a stand-in is to notice it is the address. */
static int bt_name_is_address(const char *name, const char *addr)
{
    if (!name[0] || !addr[0]) return 0;
    size_t i = 0;
    for (; name[i] && addr[i]; i++) {
        char a = name[i], b = addr[i];
        if (b == ':' ? a != '-' && a != ':' : toupper((unsigned char)a) !=
                                              toupper((unsigned char)b))
            return 0;
    }
    return !name[i] && !addr[i];
}

/* BlueZ's Icon is a freedesktop icon name derived from the device's class or
 * appearance — the one thing a nameless device still tells us about itself.
 * "Headset · 84:9E:56:B6:EE:FA" is not a name, but it is an answer to "which of
 * these do I want?", which the address alone never was. */
static const char *bt_icon_label(const char *icon)
{
    static const struct { const char *icon, *label; } MAP[] = {
        { "audio-card",        N_("Audio")      },
        { "audio-headset",     N_("Headset")    },
        { "audio-headphones",  N_("Headphones") },
        { "camera-photo",      N_("Camera")     },
        { "camera-video",      N_("Camcorder")  },
        { "computer",          N_("Computer")   },
        { "input-gaming",      N_("Controller") },
        { "input-keyboard",    N_("Keyboard")   },
        { "input-mouse",       N_("Mouse")      },
        { "input-tablet",      N_("Tablet")     },
        { "modem",             N_("Modem")      },
        { "multimedia-player", N_("Player")     },
        { "network-wireless",  N_("Network")    },
        { "phone",             N_("Phone")      },
        { "printer",           N_("Printer")    },
        { "scanner",           N_("Scanner")    },
        { "video-display",     N_("Display")    },
    };
    for (unsigned i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++)
        if (strcmp(icon, MAP[i].icon) == 0) return MAP[i].label;
    return NULL;
}

/* How a device should be written in the list. */
void bt_dev_label(const syn_bt_dev_t *d, char *out, size_t n)
{
    int named = d->name[0] && !bt_name_is_address(d->name, d->addr);
    if (named) { snprintf(out, n, "%s", d->name); return; }

    /* Nameless: say what it is, and keep the address — it is the only thing
     * telling two nameless headsets apart. */
    const char *what = d->icon[0] ? bt_icon_label(d->icon) : NULL;
    if (what && d->addr[0]) snprintf(out, n, "%s \xc2\xb7 %s", _(what), d->addr);
    else if (what)          snprintf(out, n, "%s", _(what));
    else if (d->addr[0])    snprintf(out, n, "%s", d->addr);
    else                    snprintf(out, n, "%s", _("(unknown device)"));
}

/* Is this device worth listing at all?
 *
 * A scan in a populated building is mostly phones, watches and earbuds shouting
 * BLE advertisements under privacy addresses: no name, no class, no vendor, and
 * an address that is a different address in fifteen minutes. Nothing can be
 * learned about them and nothing can be done with them — measured here, every
 * device a scan turned up was one of these, which is why the panel read as a
 * list of MAC addresses. They are hidden by default and 'a' shows them.
 *
 * Hidden is not the same as unreachable, which is why the toggle exists: a real
 * gadget that advertises no name is rare but does exist, and it must still be
 * pairable. Anything with a fixed public address is listed even unnamed, for the
 * same reason — a fixed address belongs to a real device that will still be
 * there when you look again. */
static int dev_listable(const syn_bt_dev_t *d)
{
    if (d->paired || d->connected) return 1;    /* yours: always */
    if (d->icon[0]) return 1;                   /* said what it is */
    if (d->name[0] && !bt_name_is_address(d->name, d->addr)) return 1;
    return !d->random_addr;
}

/* Listable first, then connected, then paired, then strongest signal. What you
 * want to act on is almost always something you already own, and a scan
 * otherwise buries it under every phone in the building.
 *
 * The listable key is first so that the hidden ones gather at the end of the
 * array: everything else can then take "the first n" as the visible list. */
static int dev_cmp(const void *va, const void *vb)
{
    const syn_bt_dev_t *a = va, *b = vb;
    int al = dev_listable(a), bl = dev_listable(b);
    if (al != bl) return bl - al;
    if (a->connected != b->connected) return b->connected - a->connected;
    if (a->paired    != b->paired)    return b->paired - a->paired;
    if (a->has_rssi && b->has_rssi && a->rssi != b->rssi) return b->rssi - a->rssi;
    if (a->has_rssi != b->has_rssi)   return b->has_rssi - a->has_rssi;
    return strcasecmp(a->name, b->name);
}

/* How many rows the list shows. dev_cmp sorts the listable ones to the front, so
 * the visible list is always devs[0..n) and every index stays an index into
 * devs[] — no second array to keep in step. */
int bt_shown_count(const syn_bt_t *b)
{
    if (b->show_all) return b->count;
    int n = 0;
    while (n < b->count && dev_listable(&b->devs[n])) n++;
    return n;
}

/* Re-sorting moves rows under the cursor, and a scan fires PropertiesChanged
 * constantly — so keep the selection on the same *device*, not the same index,
 * or "connect" lands on whatever drifted into the row while you reached for
 * Enter.
 *
 * Until the cursor has actually been moved there is no such device to keep:
 * pinning to whatever happened to be discovered first leaves the highlight
 * sitting in the middle of the list as that device slides down it, which just
 * looks broken. So an untouched panel tracks the top row instead. */
static void bt_sort_keep_selection(syn_bt_t *b)
{
    char keep[160] = {0};
    if (b->touched && b->selected >= 0 && b->selected < b->count)
        snprintf(keep, sizeof(keep), "%s", b->devs[b->selected].path);

    qsort(b->devs, b->count, sizeof(b->devs[0]), dev_cmp);

    if (!b->touched) b->selected = 0;

    if (keep[0]) {
        for (int i = 0; i < b->count; i++)
            if (strcmp(b->devs[i].path, keep) == 0) { b->selected = i; break; }
    }
    /* Clamp to the shown list, not the array: a device that has just been
     * filtered out of view must not keep the highlight, or the keys would act on
     * a row that is not on screen. */
    int n = bt_shown_count(b);
    if (b->selected >= n) b->selected = n ? n - 1 : 0;
    if (b->selected < 0) b->selected = 0;
}

/* ── Property decoding ───────────────────────────────────── */

/* Copy a display string, dropping anything cairo would choke on. A device name
 * is attacker-controlled text straight off the air — any phone in range picks
 * it — and syn_show_text() poisons its whole context on invalid UTF-8, which
 * silently blanks every row drawn after it. So a name that is not clean UTF-8 is
 * not shown at all; the address still identifies the device. */
static void bt_copy_text(char *dst, size_t n, const char *src)
{
    if (!src) return;
    size_t len = strlen(src);
    if (len >= n) len = n - 1;
    if (news_utf8_trim(src, len) != len) return;   /* not clean — leave as-is */
    for (size_t i = 0; i < len; i++)
        if ((unsigned char)src[i] < 0x20) return;  /* control chars: same story */
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* One entry of an a{sv}, already inside the dict. */
static int bt_read_prop(sd_bus_message *m, const char *iface, syn_server_t *s,
                        const char *path)
{
    syn_bt_t *b = &s->bt;
    const char *key;
    int r = sd_bus_message_read(m, "s", &key);
    if (r < 0) return r;

    char type;
    const char *contents;
    r = sd_bus_message_peek_type(m, &type, &contents);
    if (r < 0) return r;

    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, contents);
    if (r < 0) return r;

    if (strcmp(iface, BT_ADAPT) == 0) {
        int v;
        if (strcmp(key, "Powered") == 0 && sd_bus_message_read(m, "b", &v) >= 0)
            b->powered = v;
        else if (strcmp(key, "Discovering") == 0 && sd_bus_message_read(m, "b", &v) >= 0)
            b->discovering = v;
        else if (strcmp(key, "Discoverable") == 0 && sd_bus_message_read(m, "b", &v) >= 0)
            b->discoverable = v;
        else
            sd_bus_message_skip(m, contents);
    } else if (strcmp(iface, BT_DEV) == 0) {
        syn_bt_dev_t *d = dev_add(b, path);
        if (!d) { sd_bus_message_skip(m, contents); goto out; }

        const char *sv;
        int v;
        int16_t rssi;
        if (strcmp(key, "Alias") == 0 && sd_bus_message_read(m, "s", &sv) >= 0)
            bt_copy_text(d->name, sizeof(d->name), sv);
        else if (strcmp(key, "Name") == 0 && sd_bus_message_read(m, "s", &sv) >= 0) {
            if (!d->name[0]) bt_copy_text(d->name, sizeof(d->name), sv);
        } else if (strcmp(key, "Address") == 0 && sd_bus_message_read(m, "s", &sv) >= 0)
            snprintf(d->addr, sizeof(d->addr), "%s", sv);
        else if (strcmp(key, "AddressType") == 0 && sd_bus_message_read(m, "s", &sv) >= 0)
            d->random_addr = strcmp(sv, "random") == 0;
        else if (strcmp(key, "Icon") == 0 && sd_bus_message_read(m, "s", &sv) >= 0)
            bt_copy_text(d->icon, sizeof(d->icon), sv);
        else if (strcmp(key, "Paired") == 0 && sd_bus_message_read(m, "b", &v) >= 0)
            d->paired = v;
        else if (strcmp(key, "Trusted") == 0 && sd_bus_message_read(m, "b", &v) >= 0)
            d->trusted = v;
        else if (strcmp(key, "Connected") == 0 && sd_bus_message_read(m, "b", &v) >= 0)
            d->connected = v;
        else if (strcmp(key, "Blocked") == 0 && sd_bus_message_read(m, "b", &v) >= 0)
            d->blocked = v;
        else if (strcmp(key, "RSSI") == 0 && sd_bus_message_read(m, "n", &rssi) >= 0) {
            d->rssi = rssi; d->has_rssi = 1;
        } else
            sd_bus_message_skip(m, contents);
    } else if (strcmp(iface, BT_BATT) == 0) {
        syn_bt_dev_t *d = dev_add(b, path);
        uint8_t pct;
        if (d && strcmp(key, "Percentage") == 0 && sd_bus_message_read(m, "y", &pct) >= 0)
            d->battery = pct;
        else
            sd_bus_message_skip(m, contents);
    } else {
        sd_bus_message_skip(m, contents);
    }

out:
    sd_bus_message_exit_container(m);
    return 0;
}

/* a{sv} */
static int bt_read_props(sd_bus_message *m, const char *iface, syn_server_t *s,
                         const char *path)
{
    int r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    if (r < 0) return r;
    while ((r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
        bt_read_prop(m, iface, s, path);
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return 0;
}

/* a{sa{sv}} — the interfaces of one object */
static int bt_read_ifaces(sd_bus_message *m, syn_server_t *s, const char *path)
{
    int r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sa{sv}}");
    if (r < 0) return r;

    while ((r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}")) > 0) {
        const char *iface;
        if (sd_bus_message_read(m, "s", &iface) >= 0) {
            if (strcmp(iface, BT_ADAPT) == 0 && !s->bt.has_adapter) {
                /* First adapter wins. Multi-adapter boxes exist; picking the
                 * first is what bluetoothctl's [default] does too. */
                s->bt.has_adapter = 1;
                snprintf(s->bt.adapter, sizeof(s->bt.adapter), "%s", path);
            }
            bt_read_props(m, iface, s, path);
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
    return 0;
}

/* ── Signals ─────────────────────────────────────────────── */

static int on_ifaces_added(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    const char *path;
    if (sd_bus_message_read(m, "o", &path) < 0) return 0;
    bt_read_ifaces(m, s, path);
    bt_sort_keep_selection(&s->bt);
    bt_repaint(s);
    return 0;
}

static int on_ifaces_removed(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    syn_bt_t *b = &s->bt;
    const char *path;
    if (sd_bus_message_read(m, "o", &path) < 0) return 0;

    /* Only drop the row if the Device1 interface itself went away — losing
     * Battery1 (a headset powering down) must not delete the device. */
    int drops_device = 0;
    if (sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "s") >= 0) {
        const char *iface;
        while (sd_bus_message_read(m, "s", &iface) > 0)
            if (strcmp(iface, BT_DEV) == 0) drops_device = 1;
        sd_bus_message_exit_container(m);
    }
    if (!drops_device) return 0;

    for (int i = 0; i < b->count; i++) {
        if (strcmp(b->devs[i].path, path) != 0) continue;
        memmove(&b->devs[i], &b->devs[i + 1],
                (size_t)(b->count - i - 1) * sizeof(b->devs[0]));
        b->count--;
        if (b->selected >= b->count) b->selected = b->count ? b->count - 1 : 0;
        break;
    }
    bt_repaint(s);
    return 0;
}

static int on_props_changed(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    const char *iface;
    if (sd_bus_message_read(m, "s", &iface) < 0) return 0;

    const char *path = sd_bus_message_get_path(m);
    if (!path) return 0;

    bt_read_props(m, iface, s, path);
    bt_sort_keep_selection(&s->bt);
    bt_repaint(s);
    return 0;
}

/* ── Async replies ───────────────────────────────────────── */

/* Every method lands here. BlueZ's errors are the useful ones — "Device not
 * ready", "Authentication Failed", "Already Exists" — so surface the message
 * rather than a generic failure: the whole point of the footer is to say what
 * the radio said. */
static int on_method_reply(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    const sd_bus_error *err = sd_bus_message_get_error(m);
    if (err) {
        const char *msg = err->message && *err->message ? err->message : err->name;
        snprintf(s->bt.status, sizeof(s->bt.status), "%s", msg ? msg : _("failed"));
        wlr_log(WLR_INFO, "synui: bt: %s", msg ? msg : "(error)");
    } else {
        s->bt.status[0] = '\0';
    }
    bt_repaint(s);
    return 0;
}

static void bt_call(syn_server_t *s, const char *path, const char *iface,
                    const char *method)
{
    if (!bt.bus) return;
    int r = sd_bus_call_method_async(bt.bus, NULL, BT_SVC, path, iface, method,
                                     on_method_reply, s, NULL);
    if (r < 0)
        snprintf(s->bt.status, sizeof(s->bt.status), "%s: %s", method, strerror(-r));
}

static void bt_set_bool(syn_server_t *s, const char *path, const char *iface,
                        const char *prop, int val)
{
    if (!bt.bus) return;
    /* No async setter in sd-bus, so build the Set() call by hand — the point is
     * that it must not block. */
    sd_bus_message *m = NULL;
    int r = sd_bus_message_new_method_call(bt.bus, &m, BT_SVC, path,
                                           "org.freedesktop.DBus.Properties", "Set");
    if (r < 0) goto err;
    r = sd_bus_message_append(m, "ss", iface, prop);
    if (r < 0) goto err;
    r = sd_bus_message_open_container(m, SD_BUS_TYPE_VARIANT, "b");
    if (r < 0) goto err;
    r = sd_bus_message_append(m, "b", val);
    if (r < 0) goto err;
    r = sd_bus_message_close_container(m);
    if (r < 0) goto err;

    r = sd_bus_call_async(bt.bus, NULL, m, on_method_reply, s, 0);
    if (r < 0) goto err;
    sd_bus_message_unref(m);
    return;
err:
    sd_bus_message_unref(m);
    snprintf(s->bt.status, sizeof(s->bt.status), "%s: %s", prop, strerror(-r));
}

/* ── ObjectManager sweep ─────────────────────────────────── */

static int on_managed_objects(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;

    if (sd_bus_message_get_error(m)) {
        const sd_bus_error *err = sd_bus_message_get_error(m);
        /* bluetoothd not running is the ordinary case on a box with no radio —
         * say so in the panel instead of looking broken. */
        snprintf(s->bt.status, sizeof(s->bt.status), "bluez: %s",
                 err->message ? err->message : err->name);
        bt_repaint(s);
        return 0;
    }

    int r = sd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{oa{sa{sv}}}");
    if (r < 0) return 0;
    while ((r = sd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY,
                                               "oa{sa{sv}}")) > 0) {
        const char *path;
        if (sd_bus_message_read(m, "o", &path) >= 0)
            bt_read_ifaces(m, s, path);
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);

    bt_sort_keep_selection(&s->bt);
    bt_repaint(s);
    return 0;
}

static void bt_refresh(syn_server_t *s)
{
    if (!bt.bus) return;
    sd_bus_call_method_async(bt.bus, NULL, BT_SVC, "/",
                             "org.freedesktop.DBus.ObjectManager",
                             "GetManagedObjects", on_managed_objects, s, NULL);
}

/* ── Pairing agent ───────────────────────────────────────── */

/* BlueZ blocks on the agent's reply, so a request that is never answered leaves
 * the pairing hung until it times out. Every path that raises an ask must
 * eventually call bt_answer(). */
static void bt_ask_clear(syn_server_t *s)
{
    s->bt.ask_kind = BT_ASK_NONE;
    s->bt.ask_dev[0] = '\0';
    s->bt.ask_detail[0] = '\0';
    if (bt.pending) { sd_bus_message_unref(bt.pending); bt.pending = NULL; }
}

static void bt_answer(syn_server_t *s, int accept)
{
    if (!bt.pending) { bt_ask_clear(s); return; }

    if (accept)
        sd_bus_reply_method_return(bt.pending, NULL);
    else
        sd_bus_reply_method_errorf(bt.pending, "org.bluez.Error.Rejected",
                                   "Rejected by user");
    bt_ask_clear(s);
    bt_repaint(s);
}

/* The device a request is about, in words, for the prompt. */
static void bt_ask_name(syn_server_t *s, const char *path, char *out, size_t n)
{
    syn_bt_dev_t *d = dev_by_path(&s->bt, path);
    if (d && d->name[0]) { snprintf(out, n, "%s", d->name); return; }
    if (d && d->addr[0]) { snprintf(out, n, "%s", d->addr); return; }
    const char *tail = strrchr(path, '/');
    snprintf(out, n, "%s", tail ? tail + 1 : path);
}

static int agent_request_confirmation(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    const char *path;
    uint32_t passkey;
    if (sd_bus_message_read(m, "ou", &path, &passkey) < 0) return -EINVAL;

    bt_ask_clear(s);
    bt.pending = sd_bus_message_ref(m);
    s->bt.ask_kind = BT_ASK_CONFIRM;
    s->bt.ask_passkey = passkey;
    bt_ask_name(s, path, s->bt.ask_dev, sizeof(s->bt.ask_dev));
    if (!s->bt.visible) bt_show(s);          /* it is blocked on us — surface it */
    bt_repaint(s);
    return 1;                                 /* reply comes later, from bt_answer */
}

static int agent_request_authorization(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    const char *path;
    if (sd_bus_message_read(m, "o", &path) < 0) return -EINVAL;

    bt_ask_clear(s);
    bt.pending = sd_bus_message_ref(m);
    s->bt.ask_kind = BT_ASK_AUTHORIZE;
    bt_ask_name(s, path, s->bt.ask_dev, sizeof(s->bt.ask_dev));
    if (!s->bt.visible) bt_show(s);
    bt_repaint(s);
    return 1;
}

static int agent_authorize_service(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    const char *path, *uuid;
    if (sd_bus_message_read(m, "os", &path, &uuid) < 0) return -EINVAL;

    /* A device you have trusted is one you have already said yes to; asking
     * again on every reconnect is how people learn to hit Enter blindly. */
    syn_bt_dev_t *d = dev_by_path(&s->bt, path);
    if (d && d->trusted) return sd_bus_reply_method_return(m, NULL);

    bt_ask_clear(s);
    bt.pending = sd_bus_message_ref(m);
    s->bt.ask_kind = BT_ASK_AUTHORIZE;
    bt_ask_name(s, path, s->bt.ask_dev, sizeof(s->bt.ask_dev));
    snprintf(s->bt.ask_detail, sizeof(s->bt.ask_detail), "service");
    if (!s->bt.visible) bt_show(s);
    bt_repaint(s);
    return 1;
}

/* Display-only: the number goes on the *other* device's keypad. Answered
 * immediately — BlueZ wants the reply now and reports the outcome separately. */
static int agent_display_passkey(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    const char *path;
    uint32_t passkey;
    uint16_t entered;
    if (sd_bus_message_read(m, "ouq", &path, &passkey, &entered) < 0) return -EINVAL;

    s->bt.ask_kind = BT_ASK_DISPLAY;
    s->bt.ask_passkey = passkey;
    bt_ask_name(s, path, s->bt.ask_dev, sizeof(s->bt.ask_dev));
    snprintf(s->bt.ask_detail, sizeof(s->bt.ask_detail), "passkey");
    if (!s->bt.visible) bt_show(s);
    bt_repaint(s);
    return sd_bus_reply_method_return(m, NULL);
}

static int agent_display_pincode(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    const char *path, *pin;
    if (sd_bus_message_read(m, "os", &path, &pin) < 0) return -EINVAL;

    s->bt.ask_kind = BT_ASK_DISPLAY;
    s->bt.ask_passkey = 0;
    bt_ask_name(s, path, s->bt.ask_dev, sizeof(s->bt.ask_dev));
    snprintf(s->bt.ask_detail, sizeof(s->bt.ask_detail), "%s", pin);
    if (!s->bt.visible) bt_show(s);
    bt_repaint(s);
    return sd_bus_reply_method_return(m, NULL);
}

static int agent_cancel(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    bt_ask_clear(s);
    snprintf(s->bt.status, sizeof(s->bt.status), "%s", _("pairing cancelled"));
    bt_repaint(s);
    return sd_bus_reply_method_return(m, NULL);
}

static int agent_release(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e;
    syn_server_t *s = data;
    bt_ask_clear(s);
    return sd_bus_reply_method_return(m, NULL);
}

/* RequestPinCode/RequestPasskey want text typed *here*, which this panel has no
 * field for. Declining is honest and rare: they are the legacy pre-SSP paths
 * (old car kits, ancient headsets). Modern devices take Confirmation or
 * DisplayPasskey, both of which work. Worth revisiting if a device needs it. */
static int agent_request_pincode(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)e; (void)m;
    syn_server_t *s = data;
    snprintf(s->bt.status, sizeof(s->bt.status), "%s",
             _("device wants a typed PIN \xc2\xb7 not supported yet"));
    bt_repaint(s);
    return sd_bus_error_set(e, "org.bluez.Error.Rejected", "PIN entry unsupported");
}

static int agent_request_passkey(sd_bus_message *m, void *data, sd_bus_error *e)
{
    (void)m;
    syn_server_t *s = data;
    snprintf(s->bt.status, sizeof(s->bt.status), "%s",
             _("device wants a typed passkey \xc2\xb7 not supported yet"));
    bt_repaint(s);
    return sd_bus_error_set(e, "org.bluez.Error.Rejected", "passkey entry unsupported");
}

static const sd_bus_vtable agent_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Release",              "",    "",  agent_release,               0),
    SD_BUS_METHOD("RequestPinCode",       "o",   "s", agent_request_pincode,       0),
    SD_BUS_METHOD("DisplayPinCode",       "os",  "",  agent_display_pincode,       0),
    SD_BUS_METHOD("RequestPasskey",       "o",   "u", agent_request_passkey,       0),
    SD_BUS_METHOD("DisplayPasskey",       "ouq", "",  agent_display_passkey,       0),
    SD_BUS_METHOD("RequestConfirmation",  "ou",  "",  agent_request_confirmation,  0),
    SD_BUS_METHOD("RequestAuthorization", "o",   "",  agent_request_authorization, 0),
    SD_BUS_METHOD("AuthorizeService",     "os",  "",  agent_authorize_service,     0),
    SD_BUS_METHOD("Cancel",               "",    "",  agent_cancel,                0),
    SD_BUS_VTABLE_END
};

/* ── Actions ─────────────────────────────────────────────── */

static syn_bt_dev_t *bt_sel(syn_bt_t *b)
{
    if (b->selected < 0 || b->selected >= bt_shown_count(b)) return NULL;
    return &b->devs[b->selected];
}

static void bt_scan_set(syn_server_t *s, int on)
{
    if (!s->bt.has_adapter) return;
    bt_call(s, s->bt.adapter, BT_ADAPT, on ? "StartDiscovery" : "StopDiscovery");
}

/* ── Event loop ──────────────────────────────────────────── */

static int bt_readable(int fd, uint32_t mask, void *data)
{
    (void)fd; (void)mask;
    syn_server_t *s = data;

    for (;;) {
        int r = sd_bus_process(bt.bus, NULL);
        if (r > 0) continue;
        if (r == 0) break;
        wlr_log(WLR_ERROR, "synui: bt: bus error: %s — disabling", strerror(-r));
        bt_finish(s);
        return 0;
    }
    sd_bus_flush(bt.bus);
    return 0;
}

void bt_init(syn_server_t *s)
{
    memset(&bt, 0, sizeof(bt));
    memset(&s->bt, 0, sizeof(s->bt));

    /* BlueZ is on the SYSTEM bus, not the session bus. An active local session
     * gets Adapter1/Device1 through polkit's allow_active, so no root and no
     * password prompt — verified: Powered and StartDiscovery both work as velle. */
    int r = sd_bus_open_system(&bt.bus);
    if (r < 0) {
        wlr_log(WLR_INFO, "synui: bt: no system bus (%s) — Bluetooth unavailable",
                strerror(-r));
        bt.bus = NULL;
        return;
    }

    r = sd_bus_add_object_vtable(bt.bus, NULL, BT_AGENT_PATH, "org.bluez.Agent1",
                                 agent_vtable, s);
    if (r < 0) {
        wlr_log(WLR_ERROR, "synui: bt: cannot export agent: %s", strerror(-r));
        goto fail;
    }

    sd_bus_match_signal(bt.bus, NULL, BT_SVC, "/",
                        "org.freedesktop.DBus.ObjectManager", "InterfacesAdded",
                        on_ifaces_added, s);
    sd_bus_match_signal(bt.bus, NULL, BT_SVC, "/",
                        "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved",
                        on_ifaces_removed, s);
    /* No path filter: every adapter and device property change comes through
     * here, and the object path on the message says which. */
    sd_bus_match_signal(bt.bus, NULL, BT_SVC, NULL,
                        "org.freedesktop.DBus.Properties", "PropertiesChanged",
                        on_props_changed, s);

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    bt.src = wl_event_loop_add_fd(loop, sd_bus_get_fd(bt.bus), WL_EVENT_READABLE,
                                  bt_readable, s);
    if (!bt.src) goto fail;

    /* Register the agent as default so BlueZ routes pairing here rather than to
     * a bluetoothctl someone left open. Async like everything else — if bluez is
     * not up yet this simply fails and pairing falls back to whatever is. */
    sd_bus_call_method_async(bt.bus, NULL, BT_SVC, "/org/bluez",
                             "org.bluez.AgentManager1", "RegisterAgent",
                             on_method_reply, s, "os", BT_AGENT_PATH, BT_AGENT_CAP);
    sd_bus_call_method_async(bt.bus, NULL, BT_SVC, "/org/bluez",
                             "org.bluez.AgentManager1", "RequestDefaultAgent",
                             on_method_reply, s, "o", BT_AGENT_PATH);

    bt_refresh(s);
    wlr_log(WLR_INFO, "synui: bt: BlueZ client up (agent %s)", BT_AGENT_PATH);
    return;

fail:
    sd_bus_unref(bt.bus);
    bt.bus = NULL;
}

void bt_finish(syn_server_t *s)
{
    if (bt.pending) { sd_bus_message_unref(bt.pending); bt.pending = NULL; }
    if (bt.src) { wl_event_source_remove(bt.src); bt.src = NULL; }
    if (bt.bus) { sd_bus_unref(bt.bus); bt.bus = NULL; }
    s->bt.visible = 0;
}

/* ── Panel ───────────────────────────────────────────────── */

void bt_show(syn_server_t *s)
{
    s->bt.visible = 1;
    s->bt.touched = 0;
    s->bt.status[0] = '\0';
    bt_refresh(s);          /* the world may have moved while the panel was shut */
    synui_render_bt(s);
}

void bt_hide(syn_server_t *s)
{
    /* Stop scanning on the way out. BlueZ ties discovery to our bus connection,
     * not to the panel, so a scan left running would keep the radio busy (and
     * drain a laptop) for as long as synui lives. */
    if (s->bt.discovering) bt_scan_set(s, 0);
    s->bt.visible = 0;
    synui_render_bt(s);
    ctlpanel_child_closed(s, "bluetooth");
}

void bt_toggle(syn_server_t *s)
{
    if (s->bt.visible) bt_hide(s);
    else               bt_show(s);
}

static void bt_move(syn_bt_t *b, int dir)
{
    int n = b->selected + dir;
    if (n < 0 || n >= bt_shown_count(b)) return;
    b->touched = 1;
    b->selected = n;
    if (b->selected < b->scroll) b->scroll = b->selected;
    if (b->selected >= b->scroll + BT_ROWS) b->scroll = b->selected - BT_ROWS + 1;
}

/* ── Pointer ─────────────────────────────────────────────── */
/* As in menu.c, and for the same reason: a panel the compositor draws is one it
 * can also hand the pointer to. Hovering selects and the wheel scrolls; a click
 * selects and no more. The actions here are several (connect, pair, trust,
 * forget) and a single click cannot mean all of them, so it means the one thing
 * it unambiguously can — this is what you point at — and the keys named in the
 * footer still do the rest. */

/* The device row drawn at the top of the panel — see menu_first_row. */
int bt_first_row(const syn_bt_t *b)
{
    int max_scroll = bt_shown_count(b) - BT_ROWS;
    if (max_scroll < 0) max_scroll = 0;
    int first = b->scroll;
    if (first > max_scroll) first = max_scroll;
    if (first < 0) first = 0;
    return first;
}

static int bt_in_panel(const syn_bt_t *b, double lx, double ly)
{
    return lx >= b->x && lx < b->x + b->w && ly >= b->y && ly < b->y + b->h;
}

/* devs[] index under (lx,ly), or -1 for the chrome or outside. */
static int bt_row_at(const syn_bt_t *b, double lx, double ly)
{
    if (!bt_in_panel(b, lx, ly)) return -1;
    /* A pairing prompt replaces the list entirely; there are no rows to hit. */
    if (b->ask_kind != BT_ASK_NONE) return -1;

    double top = b->y + BT_TOP - BT_ROW_ASC;
    if (ly < top) return -1;
    int i = (int)((ly - top) / BT_ROW_H);
    if (i < 0 || i >= BT_ROWS) return -1;

    int row = bt_first_row(b) + i;
    return row < bt_shown_count(b) ? row : -1;
}

void bt_motion(syn_server_t *s, double lx, double ly)
{
    syn_bt_t *b = &s->bt;
    if (!b->visible) return;
    int row = bt_row_at(b, lx, ly);
    if (row < 0 || row == b->selected) return;
    b->selected = row;
    b->touched  = 1;      /* a refresh must now keep the selection on its device */
    synui_render_bt(s);
}

void bt_click(syn_server_t *s, double lx, double ly)
{
    syn_bt_t *b = &s->bt;
    if (!b->visible) return;

    /* A prompt has BlueZ blocked waiting on y/n, so a click cannot dismiss the
     * panel out from under it — the keys are the only way through. */
    if (!bt_in_panel(b, lx, ly)) {
        if (b->ask_kind == BT_ASK_NONE) bt_hide(s);
        return;
    }
    bt_motion(s, lx, ly);
}

void bt_scroll(syn_server_t *s, double delta)
{
    syn_bt_t *b = &s->bt;
    if (!b->visible || delta == 0) return;
    if (b->ask_kind != BT_ASK_NONE) return;
    int shown = bt_shown_count(b);
    if (shown <= BT_ROWS) return;

    b->scroll += delta > 0 ? 3 : -3;
    int max_scroll = shown - BT_ROWS;
    if (b->scroll > max_scroll) b->scroll = max_scroll;
    if (b->scroll < 0) b->scroll = 0;

    /* Keep the selection on a row that is still on screen: the footer's keys all
     * act on it, and acting on something scrolled out of sight is how you
     * disconnect the wrong headset. */
    int first = bt_first_row(b), last = first + BT_ROWS - 1;
    if (last >= shown) last = shown - 1;
    if (b->selected < first) { b->selected = first; b->touched = 1; }
    if (b->selected > last)  { b->selected = last;  b->touched = 1; }

    synui_render_bt(s);
}

int bt_key(syn_server_t *s, xkb_keysym_t sym, uint32_t mods)
{
    syn_bt_t *b = &s->bt;
    if (!b->visible) return 0;

    if (mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))
        return 0;

    /* A pairing prompt is modal within the panel: BlueZ is blocked on the answer,
     * so nothing else should be reachable until it has one. */
    if (b->ask_kind == BT_ASK_CONFIRM || b->ask_kind == BT_ASK_AUTHORIZE) {
        switch (sym) {
        case XKB_KEY_y: case XKB_KEY_Y: case XKB_KEY_Return: case XKB_KEY_KP_Enter:
            bt_answer(s, 1); return 1;
        case XKB_KEY_n: case XKB_KEY_N: case XKB_KEY_Escape:
            bt_answer(s, 0); return 1;
        default:
            return 1;
        }
    }
    if (b->ask_kind == BT_ASK_DISPLAY) {
        /* Informational — any key dismisses it. */
        b->ask_kind = BT_ASK_NONE;
        synui_render_bt(s);
        return 1;
    }

    syn_bt_dev_t *d;
    switch (sym) {
    case XKB_KEY_Escape:
    case XKB_KEY_q:
        bt_hide(s);
        return 1;

    case XKB_KEY_Up:
    case XKB_KEY_k:
        bt_move(b, -1); synui_render_bt(s); return 1;
    case XKB_KEY_Down:
    case XKB_KEY_j:
        bt_move(b, +1); synui_render_bt(s); return 1;

    case XKB_KEY_o:                       /* radio power */
        if (b->has_adapter) bt_set_bool(s, b->adapter, BT_ADAPT, "Powered", !b->powered);
        return 1;

    case XKB_KEY_a:                       /* show the anonymous advertisers too */
        b->show_all = !b->show_all;
        b->scroll = 0;
        /* The shown list just grew or shrank under the selection; re-sort clamps
         * it back onto a row that is actually on screen (see dev_cmp/bt_move). */
        bt_sort_keep_selection(b);
        synui_render_bt(s);
        return 1;

    case XKB_KEY_s:                       /* scan */
        if (!b->powered) {
            snprintf(b->status, sizeof(b->status), "radio is off \xc2\xb7 press o");
            synui_render_bt(s);
            return 1;
        }
        bt_scan_set(s, !b->discovering);
        return 1;

    case XKB_KEY_Return:                  /* connect / disconnect */
    case XKB_KEY_KP_Enter:
        d = bt_sel(b);
        if (!d) return 1;
        if (d->connected) {
            snprintf(b->status, sizeof(b->status), "disconnecting %s\xe2\x80\xa6", d->name);
            bt_call(s, d->path, BT_DEV, "Disconnect");
        } else {
            /* Connect() on an unpaired device fails on most hardware, so pair
             * first and let the agent drive: one key does the obvious thing. */
            if (!d->paired) {
                snprintf(b->status, sizeof(b->status), "pairing %s\xe2\x80\xa6", d->name);
                bt_call(s, d->path, BT_DEV, "Pair");
            } else {
                snprintf(b->status, sizeof(b->status), "connecting %s\xe2\x80\xa6", d->name);
                bt_call(s, d->path, BT_DEV, "Connect");
            }
        }
        synui_render_bt(s);
        return 1;

    case XKB_KEY_p:                       /* explicit pair */
        d = bt_sel(b);
        if (!d) return 1;
        snprintf(b->status, sizeof(b->status), "pairing %s\xe2\x80\xa6", d->name);
        bt_call(s, d->path, BT_DEV, "Pair");
        synui_render_bt(s);
        return 1;

    case XKB_KEY_t:                       /* trust: reconnects without asking */
        d = bt_sel(b);
        if (!d) return 1;
        bt_set_bool(s, d->path, BT_DEV, "Trusted", !d->trusted);
        return 1;

    case XKB_KEY_r:                       /* remove/forget */
        d = bt_sel(b);
        if (!d || !b->has_adapter) return 1;
        /* RemoveDevice is the adapter's method and takes the device path — the
         * device has no Remove of its own. */
        if (bt.bus) {
            snprintf(b->status, sizeof(b->status), "removing %s\xe2\x80\xa6", d->name);
            sd_bus_call_method_async(bt.bus, NULL, BT_SVC, b->adapter, BT_ADAPT,
                                     "RemoveDevice", on_method_reply, s,
                                     "o", d->path);
        }
        synui_render_bt(s);
        return 1;

    default:
        return 1;   /* modal */
    }
}
