/*
 * weather.c — the weather, for the lock screen and the login screen.
 *
 * The desktop has had a weather widget for as long as it has had the Omarchy
 * plugin catalogue, and that widget is a CLIENT: it is not running on the lock
 * screen, and on the login screen nothing of the user's is running at all. Both
 * of those are screens people stand in front of on the way out of the door,
 * which is the moment the forecast is worth something.
 *
 * ⚠ ONE LOCATION ON THIS MACHINE. The place is NOT a new setting. It is the
 * file every weather widget here already reads and `omarchy-weather-location`
 * already writes —
 *
 *     ~/.local/state/omarchy/settings/weather.json
 *
 * — so setting your city once sets it for the bar widget, the radar plugin and
 * this. A `lock_weather_city` key would have been a second place to say the
 * same thing and a guaranteed way for the two to disagree.
 *
 * ⚠ OFF BY DEFAULT. This is the only part of the lock screen that touches the
 * network, on a distro that is careful about that; `lock_weather = on` (or the
 * Super+Z row) is a deliberate act. Nothing here runs, and no name is resolved,
 * until it is turned on.
 *
 * ── Shape ───────────────────────────────────────────────────────────────────
 *
 * The news idiom, smaller: a thread parked on a condvar does the libcurl work
 * so a slow DNS lookup cannot stall the compositor's event loop, hands the
 * result over under a mutex, and pokes a pipe whose read end sits in the
 * Wayland event loop. Only the main thread touches wlroots. The stop flag is
 * also the curl progress callback, so logout never waits out a connect timeout
 * — the lesson news.c took from the AI thread's 15s logout hang.
 *
 * The reading is cached to disk, so a lock screen shows the last known weather
 * the instant it comes up rather than a blank space that fills in a second
 * later, and so a machine with no network shows the last thing it knew (dimmed
 * — see `stale`) instead of nothing.
 *
 * ⚠ THE LOGIN SCREEN CANNOT READ ANY OF THIS. The greeter runs as another
 * account and can read neither the location file nor the cache, exactly as it
 * cannot read the wallpaper — so the user's session publishes both through
 * greeterbg.c and weather_adopt() seeds them here. Same permission boundary,
 * same answer, one implementation.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <curl/curl.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>

#include "synui.h"

/* Open-Meteo: no API key, no account, and a documented free tier for exactly
 * this kind of use. The geocoder is the same service, so a city name and a
 * forecast are one vendor rather than two. */
#define WX_FORECAST_URL "https://api.open-meteo.com/v1/forecast"
#define WX_GEOCODE_URL  "https://geocoding-api.open-meteo.com/v1/search"
/* Only ever asked for the city name, and only when nothing is configured —
 * the same call, from the same host, that omarchy-weather-location makes for
 * the same reason. */
#define WX_WHEREAMI_URL "https://wttr.in/?format=%l"

#define WX_UA           "synui-weather/1.0"
#define WX_CONNECT_SEC  6
#define WX_XFER_SEC     12
#define WX_MAX_BODY     (256 * 1024)

/* How often a running session re-asks. Weather is not a clock. */
#define WX_REFRESH_SEC  (20 * 60)
/* Past this the reading is drawn dimmed and labelled by its age rather than
 * presented as current — an eight-hour-old temperature shown as if it were now
 * is worse than no temperature. */
#define WX_STALE_SEC    (3 * 60 * 60)

/* Shared between the two threads. Everything the fetch thread writes is under
 * `lock`; everything else is set up before the thread starts or after it is
 * joined. */
static struct {
    syn_server_t           *server;
    pthread_t               thread;
    pthread_mutex_t         lock;
    pthread_cond_t          cv;
    atomic_int              stop, want;
    int                     running;   /* the fetch thread is alive */
    int                     inited;    /* the mutex/cond exist — weather_init */
    int                     pipe[2];
    struct wl_event_source *src;
    struct wl_event_source *timer;

    /* The place. `have_coords` is what separates "geocode this name first"
     * from "ask for the forecast now". */
    char    place[64];
    double  lat, lon;
    int     have_coords;
    int     unit_f;              /* resolved from cfg->lock_weather_unit */

    /* The reading. `when` is 0 until there has ever been one. */
    double  temp;
    int     code;                /* WMO weather code */
    time_t  when;
    int     failed;              /* last attempt did not land */

    /* The fetch thread's output, picked up by the main thread on the pipe. */
    double  got_temp;
    int     got_code;
    char    got_place[64];
    int     got_ok;
} wx;

/* ── The location file ───────────────────────────────────── */

/* Same path omarchy-weather-location writes, resolved against THIS process's
 * home — which is the point of the greeter's publish path, since the greeter's
 * home is `/`. */
static bool wx_loc_path(char *buf, size_t n)
{
    const char *home = getenv("HOME");
    if (!home || !*home) return false;
    snprintf(buf, n, "%s/.local/state/omarchy/settings/weather.json", home);
    return true;
}

/* A `"key": <number>` out of a small, known JSON document. Deliberately a
 * scanner and not a parser: these files are three fields written by a shell
 * script and by Open-Meteo, and linking a JSON library to read two doubles is
 * the trade greeter.c already declined for greetd's protocol. */
static bool json_number(const char *doc, const char *key, double *out)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);

    /* ⚠ KEEPS LOOKING PAST A STRING VALUE. Open-Meteo answers with a
     * `current_units` block that names every field it is about to send —
     *
     *     "current_units":{"temperature_2m":"°C","weather_code":"wmo code"},
     *     "current":{"temperature_2m":19.0,"weather_code":0}
     *
     * — so the FIRST "temperature_2m" in the document has the string "°C" after
     * it. A scanner that took the first hit and gave up read no temperature at
     * all and logged "no current conditions", which reads like the service was
     * down. The caller also scopes the search (see json_object), and this is
     * the belt to that pair of braces. */
    for (const char *p = strstr(doc, pat); p; p = strstr(p + 1, pat)) {
        const char *c = strchr(p + strlen(pat), ':');
        if (!c) return false;
        c++;
        while (*c == ' ' || *c == '\t') c++;
        char *end = NULL;
        double v = strtod(c, &end);
        if (end == c) continue;            /* a string, or null — try the next */
        *out = v;
        return true;
    }
    return false;
}

/* The body of a nested object: `"key":{` … Returns a pointer just inside the
 * brace, so a caller can scan a sub-document rather than the whole reply. NULL
 * when there is no such object, which the caller reads as "scan everything" —
 * the answer is still findable, just less precisely. */
static const char *json_object(const char *doc, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(doc, pat);
    if (!p) return NULL;
    p = strchr(p + strlen(pat), ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return (*p == '{') ? p + 1 : NULL;
}

/* The same, for a `"key": "value"` string. Handles the backslash escapes the
 * location file's own writer emits (\" and \\) and nothing else, because
 * nothing else is emitted. */
static bool json_string(const char *doc, const char *key, char *out, size_t n)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(doc, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return false;
    p++;

    size_t o = 0;
    while (*p && *p != '"' && o + 1 < n) {
        if (*p == '\\' && p[1]) p++;
        out[o++] = *p++;
    }
    out[o] = '\0';
    return o > 0;
}

static void wx_read_location(void)
{
    char path[512];
    wx.place[0]     = '\0';
    wx.have_coords  = 0;

    if (!wx_loc_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "re");
    if (!f) return;                        /* nothing set: IP detection below */

    char doc[1024];
    size_t got = fread(doc, 1, sizeof(doc) - 1, f);
    fclose(f);
    doc[got] = '\0';

    json_string(doc, "name", wx.place, sizeof(wx.place));

    double la, lo;
    if (json_number(doc, "latitude", &la) && json_number(doc, "longitude", &lo)) {
        wx.lat = la;
        wx.lon = lo;
        wx.have_coords = 1;
    }
}

/* ── The cache ───────────────────────────────────────────── */

static bool wx_cache_path(char *buf, size_t n)
{
    return syn_config_path(buf, n, "weather.cache");
}

static void wx_cache_save(void)
{
    char path[512];
    if (!wx_cache_path(path, sizeof(path))) return;
    syn_config_ensure_dir();

    FILE *f = fopen(path, "we");
    if (!f) return;
    fprintf(f, "place=%s\n", wx.place);
    fprintf(f, "temp=%.1f\n", wx.temp);
    fprintf(f, "code=%d\n", wx.code);
    fprintf(f, "unit=%c\n", wx.unit_f ? 'F' : 'C');
    fprintf(f, "when=%lld\n", (long long)wx.when);
    fclose(f);
}

static void wx_cache_load(void)
{
    char path[512];
    if (!wx_cache_path(path, sizeof(path))) return;
    FILE *f = fopen(path, "re");
    if (!f) return;

    char line[256];
    char unit = 0;
    double temp = 0;
    int code = -1;
    long long when = 0;
    char place[64] = "";

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if      (!strncmp(line, "place=", 6)) snprintf(place, sizeof(place), "%s", line + 6);
        else if (!strncmp(line, "temp=", 5))  temp = strtod(line + 5, NULL);
        else if (!strncmp(line, "code=", 5))  code = atoi(line + 5);
        else if (!strncmp(line, "unit=", 5))  unit = line[5];
        else if (!strncmp(line, "when=", 5))  when = atoll(line + 5);
    }
    fclose(f);

    if (code < 0 || when <= 0) return;

    /* ⚠ A CACHE WRITTEN IN THE OTHER UNIT IS NOT A READING. Someone who
     * switched to Fahrenheit would otherwise see 12°F on the next lock screen
     * and a plausible-looking wrong number is worse than a blank one. Converted
     * rather than discarded — the reading is still true, only its label
     * changed. */
    if (unit == 'F' && !wx.unit_f)      temp = (temp - 32.0) * 5.0 / 9.0;
    else if (unit == 'C' && wx.unit_f)  temp = temp * 9.0 / 5.0 + 32.0;

    wx.temp = temp;
    wx.code = code;
    wx.when = (time_t)when;
    if (place[0] && !wx.place[0]) snprintf(wx.place, sizeof(wx.place), "%s", place);
}

/* ── Fetching (the thread) ───────────────────────────────── */

typedef struct { char *buf; size_t len, cap; } wxbuf_t;

static size_t wx_write(char *ptr, size_t sz, size_t nm, void *ud)
{
    wxbuf_t *d = ud;
    size_t add = sz * nm;
    if (d->len + add + 1 > WX_MAX_BODY) return 0;
    if (d->len + add + 1 > d->cap) {
        size_t cap = d->cap ? d->cap * 2 : 8192;
        while (cap < d->len + add + 1) cap *= 2;
        char *nb = realloc(d->buf, cap);
        if (!nb) return 0;
        d->buf = nb;
        d->cap = cap;
    }
    memcpy(d->buf + d->len, ptr, add);
    d->len += add;
    d->buf[d->len] = '\0';
    return add;
}

/* The stop flag as libcurl sees it — a non-zero return aborts the transfer, so
 * logout does not sit through a connect timeout. */
static int wx_progress(void *ud, curl_off_t dt, curl_off_t dn,
                       curl_off_t ut, curl_off_t un)
{
    (void)ud; (void)dt; (void)dn; (void)ut; (void)un;
    return atomic_load(&wx.stop) ? 1 : 0;
}

/* GET `url` into a freshly allocated body, or NULL. Caller frees. */
static char *wx_get(CURL *curl, const char *url)
{
    wxbuf_t d = {0};

    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wx_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &d);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, WX_UA);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)WX_CONNECT_SEC);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)WX_XFER_SEC);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, wx_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, NULL);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        free(d.buf);
        if (!atomic_load(&wx.stop))
            wlr_log(WLR_INFO, "synui: weather: %s: %s", url, curl_easy_strerror(rc));
        return NULL;
    }
    return d.buf;                          /* may be NULL on an empty body */
}

/* Percent-encode a city name for a query string. A place name is somebody
 * else's text — "Côte-Saint-Luc" is UTF-8 and legal — so everything outside
 * the unreserved set goes through as %XX rather than being dropped. */
static void url_escape(const char *in, char *out, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in && o + 4 < n; in++) {
        unsigned char c = (unsigned char)*in;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
}

/* Turn a place name into coordinates. Without them Open-Meteo cannot be asked
 * anything at all, so this is the step that makes a hand-written
 * {"name": "Malibu"} — a documented way to set the location — work. */
static bool wx_geocode(CURL *curl, const char *name, double *lat, double *lon,
                       char *resolved, size_t rn)
{
    char esc[192], url[512];
    url_escape(name, esc, sizeof(esc));
    snprintf(url, sizeof(url), "%s?name=%s&count=1&format=json",
             WX_GEOCODE_URL, esc);

    char *body = wx_get(curl, url);
    if (!body) return false;

    bool ok = json_number(body, "latitude", lat) &&
              json_number(body, "longitude", lon);
    /* The geocoder's spelling, not the typed one: "oslo" comes back "Oslo",
     * and it is the answer that gets drawn. */
    if (ok) json_string(body, "name", resolved, rn);
    free(body);
    return ok;
}

/* One full attempt: resolve a place if needed, then read the current
 * conditions. Runs on the thread; touches only `wx.got_*` (under the lock) and
 * the location fields it resolved. */
static bool wx_fetch(CURL *curl)
{
    char place[64];
    double lat, lon;
    int have;

    pthread_mutex_lock(&wx.lock);
    snprintf(place, sizeof(place), "%s", wx.place);
    lat  = wx.lat;
    lon  = wx.lon;
    have = wx.have_coords;
    pthread_mutex_unlock(&wx.lock);

    /* Nothing configured at all: ask where this machine is, the same way, from
     * the same host, that omarchy-weather-location does when it is asked with
     * no arguments. */
    if (!have && !place[0]) {
        char *body = wx_get(curl, WX_WHEREAMI_URL);
        if (body) {
            body[strcspn(body, ",\r\n")] = '\0';
            /* wttr.in answers with a bare city name; anything with a quote or
             * an angle bracket in it is an error page, not a place. */
            if (body[0] && !strpbrk(body, "<>\"{}"))
                snprintf(place, sizeof(place), "%s", body);
            free(body);
        }
        if (!place[0]) return false;
    }

    if (!have) {
        char resolved[64] = "";
        if (!wx_geocode(curl, place, &lat, &lon, resolved, sizeof(resolved)))
            return false;
        if (resolved[0]) snprintf(place, sizeof(place), "%s", resolved);
        have = 1;

        /* Kept, so the next refresh is one request instead of three. Not
         * written back to the location file — that file is the user's, and
         * this process is a reader of it. */
        pthread_mutex_lock(&wx.lock);
        wx.lat = lat;
        wx.lon = lon;
        wx.have_coords = 1;
        if (!wx.place[0]) snprintf(wx.place, sizeof(wx.place), "%s", place);
        pthread_mutex_unlock(&wx.lock);
    }

    char url[512];
    snprintf(url, sizeof(url),
             "%s?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code"
             "&temperature_unit=%s",
             WX_FORECAST_URL, lat, lon, wx.unit_f ? "fahrenheit" : "celsius");

    char *body = wx_get(curl, url);
    if (!body) return false;

    /* Scoped to the `current` object. Everything outside it is metadata about
     * the request, including a units block that names the same two fields. */
    const char *cur = json_object(body, "current");
    if (!cur) cur = body;

    double temp = 0, code = 0;
    bool ok = json_number(cur, "temperature_2m", &temp) &&
              json_number(cur, "weather_code", &code);
    free(body);
    if (!ok) {
        wlr_log(WLR_INFO, "synui: weather: no current conditions in the reply");
        return false;
    }

    pthread_mutex_lock(&wx.lock);
    wx.got_temp = temp;
    wx.got_code = (int)code;
    snprintf(wx.got_place, sizeof(wx.got_place), "%s", place);
    wx.got_ok = 1;
    pthread_mutex_unlock(&wx.lock);
    return true;
}

static void *wx_thread_fn(void *data)
{
    (void)data;
    CURL *curl = curl_easy_init();
    if (!curl) {
        wlr_log(WLR_ERROR, "synui: weather: curl init failed");
        return NULL;
    }

    while (!atomic_load(&wx.stop)) {
        pthread_mutex_lock(&wx.lock);
        while (!atomic_load(&wx.stop) && !atomic_load(&wx.want))
            pthread_cond_wait(&wx.cv, &wx.lock);
        pthread_mutex_unlock(&wx.lock);
        if (atomic_load(&wx.stop)) break;
        atomic_store(&wx.want, 0);

        bool ok = wx_fetch(curl);
        if (atomic_load(&wx.stop)) break;

        if (!ok) {
            pthread_mutex_lock(&wx.lock);
            wx.got_ok = 0;
            pthread_mutex_unlock(&wx.lock);
        }

        char byte = 1;
        if (write(wx.pipe[1], &byte, 1) < 0 && errno != EAGAIN)
            wlr_log(WLR_ERROR, "synui: weather: pipe write failed");
    }

    curl_easy_cleanup(curl);
    return NULL;
}

/* ── The main thread's side ──────────────────────────────── */

static int wx_readable(int fd, uint32_t mask, void *data)
{
    (void)mask;
    syn_server_t *s = data;

    char drain[64];
    while (read(fd, drain, sizeof(drain)) > 0) { }

    pthread_mutex_lock(&wx.lock);
    int ok = wx.got_ok;
    if (ok) {
        wx.temp = wx.got_temp;
        wx.code = wx.got_code;
        wx.when = time(NULL);
        if (wx.got_place[0])
            snprintf(wx.place, sizeof(wx.place), "%s", wx.got_place);
    }
    wx.got_ok = 0;
    pthread_mutex_unlock(&wx.lock);

    wx.failed = !ok;
    if (ok) wx_cache_save();

    /* The only thing that draws it. Off the lock this is one branch per
     * refresh, which is three an hour. */
    if (s->nlock.active) lock_render(s);
    return 0;
}

/* Ask the thread for a reading. `force` skips the "we already have a fresh
 * one" test — the Super+Z row uses it, so turning the feature on says
 * something immediately rather than up to twenty minutes later. */
void weather_refresh(syn_server_t *s, bool force)
{
    if (!wx.running) return;
    if (!s->config.lock_weather) return;

    if (!force && wx.when && time(NULL) - wx.when < WX_REFRESH_SEC) return;

    pthread_mutex_lock(&wx.lock);
    /* ⚠ THE UNIT IS RE-READ HERE, not only at init. The Super+Z row writes
     * cfg->lock_weather_unit_f and asks for a refresh; without this the request
     * would go out asking for the unit that was configured at STARTUP, and
     * switching to °F would answer with a Celsius number labelled °F — a
     * plausible wrong temperature, which is the worst kind. Under the lock,
     * because the fetch thread reads it. */
    wx.unit_f = s->config.lock_weather_unit_f;
    atomic_store(&wx.want, 1);
    pthread_cond_signal(&wx.cv);
    pthread_mutex_unlock(&wx.lock);
}

static int wx_tick(void *data)
{
    syn_server_t *s = data;
    weather_refresh(s, false);
    if (wx.timer)
        wl_event_source_timer_update(wx.timer, WX_REFRESH_SEC * 1000);
    return 0;
}

/* ── WMO codes ───────────────────────────────────────────────
 *
 * Open-Meteo answers with a WMO code, which is a number. The words and the
 * picture both come from this one table, because a second mapping — the icon
 * chosen here, the label chosen in lock.c — is two things to keep in step and
 * a guaranteed way to draw a sun over the word "Snow".
 */
static void wx_describe(int code, const char **text, syn_weather_icon_t *icon)
{
    switch (code) {
    case 0:              *text = "Clear";           *icon = SYN_WX_SUN;   return;
    case 1:              *text = "Mainly clear";    *icon = SYN_WX_SUN;   return;
    case 2:              *text = "Partly cloudy";   *icon = SYN_WX_PARTLY; return;
    case 3:              *text = "Overcast";        *icon = SYN_WX_CLOUD; return;
    case 45: case 48:    *text = "Fog";             *icon = SYN_WX_FOG;   return;
    case 51: case 53: case 55:
                         *text = "Drizzle";         *icon = SYN_WX_RAIN;  return;
    case 56: case 57:    *text = "Freezing drizzle"; *icon = SYN_WX_RAIN; return;
    case 61: case 63: case 65:
                         *text = "Rain";            *icon = SYN_WX_RAIN;  return;
    case 66: case 67:    *text = "Freezing rain";   *icon = SYN_WX_RAIN;  return;
    case 71: case 73: case 75: case 77:
                         *text = "Snow";            *icon = SYN_WX_SNOW;  return;
    case 80: case 81: case 82:
                         *text = "Rain showers";    *icon = SYN_WX_RAIN;  return;
    case 85: case 86:    *text = "Snow showers";    *icon = SYN_WX_SNOW;  return;
    case 95:             *text = "Thunderstorm";    *icon = SYN_WX_STORM; return;
    case 96: case 99:    *text = "Thunderstorm, hail"; *icon = SYN_WX_STORM; return;
    /* The list is WMO's and it is closed, so anything else is a code that did
     * not exist when this was written — say nothing rather than guess. */
    default:             *text = "";                *icon = SYN_WX_CLOUD; return;
    }
}

/* ⚠ UNDER THE LOCK, cheap as it looks. `place` is written by the fetch thread
 * (it is resolved there, from the geocoder), and this runs on the compositor's
 * render path — an unsynchronised read of a string another thread is snprintf-ing
 * into is a torn place name at best. The temperature and the code are written
 * only by the main thread, but taking the lock for part of a struct and not the
 * rest is how the next person gets it wrong. */
bool weather_current(syn_weather_now_t *out)
{
    if (!wx.inited) return false;

    pthread_mutex_lock(&wx.lock);
    if (!wx.when) { pthread_mutex_unlock(&wx.lock); return false; }

    memset(out, 0, sizeof(*out));
    const char *text = "";
    wx_describe(wx.code, &text, &out->icon);

    snprintf(out->place, sizeof(out->place), "%s", wx.place);
    snprintf(out->cond,  sizeof(out->cond),  "%s", text);
    /* Rounded, not truncated: -0.4 °C is 0°, not -0°, and lround gets both
     * signs right where a cast to int does not. */
    out->temp  = (int)lround(wx.temp);
    out->unit  = wx.unit_f ? 'F' : 'C';
    out->age   = time(NULL) - wx.when;
    out->stale = out->age > WX_STALE_SEC;
    pthread_mutex_unlock(&wx.lock);
    return true;
}

/* ── Drawing the icon ────────────────────────────────────────
 *
 * Cairo paths, not glyphs. ☀ and ☁ are in DejaVu but ⛈ and most of the rest of
 * the weather block are not, and the lock screen's font fallback would answer
 * with whatever fontconfig had — a different look per machine, or a missing
 * glyph on the ISO, which ships a small font set. Six shapes drawn by hand are
 * about eighty lines and look the same everywhere.
 *
 * Drawn in a 1x1 box at (x, y) scaled by `size`, in the CURRENT source colour,
 * so the caller's ink ladder decides the contrast.
 */
void weather_draw_icon(cairo_t *cr, syn_weather_icon_t icon,
                       double x, double y, double size)
{
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, size, size);
    cairo_set_line_width(cr, 0.07);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    const bool sun   = (icon == SYN_WX_SUN || icon == SYN_WX_PARTLY);
    const bool cloud = (icon != SYN_WX_SUN && icon != SYN_WX_FOG);

    if (sun) {
        /* Off to the top-left when a cloud shares the box, centred when it has
         * the box to itself. */
        double cx = (icon == SYN_WX_PARTLY) ? 0.36 : 0.5;
        double cy = (icon == SYN_WX_PARTLY) ? 0.34 : 0.46;
        double r  = (icon == SYN_WX_PARTLY) ? 0.17 : 0.22;

        for (int i = 0; i < 8; i++) {
            double a = i * (3.14159265 / 4.0);
            cairo_move_to(cr, cx + cos(a) * (r + 0.09), cy + sin(a) * (r + 0.09));
            cairo_line_to(cr, cx + cos(a) * (r + 0.20), cy + sin(a) * (r + 0.20));
        }
        cairo_stroke(cr);
        cairo_arc(cr, cx, cy, r, 0, 2 * 3.14159265);
        cairo_fill(cr);
    }

    if (cloud) {
        /* Three overlapping discs on a bar — the shape everyone draws, and the
         * only one that reads as a cloud at 28 px. */
        double base = (icon == SYN_WX_CLOUD || icon == SYN_WX_PARTLY) ? 0.66 : 0.56;
        cairo_new_path(cr);
        cairo_arc(cr, 0.34, base - 0.10, 0.16, 0, 2 * 3.14159265);
        cairo_close_path(cr);
        cairo_arc(cr, 0.55, base - 0.16, 0.20, 0, 2 * 3.14159265);
        cairo_close_path(cr);
        cairo_arc(cr, 0.72, base - 0.08, 0.14, 0, 2 * 3.14159265);
        cairo_close_path(cr);
        cairo_rectangle(cr, 0.20, base - 0.10, 0.62, 0.16);
        cairo_fill(cr);
    }

    switch (icon) {
    case SYN_WX_RAIN:
        for (int i = 0; i < 3; i++) {
            double dx = 0.30 + i * 0.19;
            cairo_move_to(cr, dx, 0.70);
            cairo_line_to(cr, dx - 0.07, 0.92);
        }
        cairo_stroke(cr);
        break;
    case SYN_WX_SNOW:
        for (int i = 0; i < 3; i++) {
            double dx = 0.30 + i * 0.19;
            cairo_arc(cr, dx, 0.80 + (i == 1 ? 0.08 : 0.0), 0.05,
                      0, 2 * 3.14159265);
            cairo_fill(cr);
        }
        break;
    case SYN_WX_STORM:
        cairo_move_to(cr, 0.54, 0.66);
        cairo_line_to(cr, 0.40, 0.86);
        cairo_line_to(cr, 0.52, 0.86);
        cairo_line_to(cr, 0.42, 1.02);
        cairo_line_to(cr, 0.66, 0.78);
        cairo_line_to(cr, 0.53, 0.78);
        cairo_close_path(cr);
        cairo_fill(cr);
        break;
    case SYN_WX_FOG:
        for (int i = 0; i < 4; i++) {
            double yy = 0.32 + i * 0.15;
            cairo_move_to(cr, 0.16 + (i % 2) * 0.10, yy);
            cairo_line_to(cr, 0.84 - (i % 2) * 0.08, yy);
        }
        cairo_stroke(cr);
        break;
    default:
        break;
    }

    cairo_restore(cr);
}

/* ── The greeter's copy ──────────────────────────────────── */

/*
 * What greeterbg.c publishes, so the LOGIN screen can show this user's
 * weather. Answers false when there is nothing worth publishing.
 */
bool weather_publish_state(char *place, size_t pn, double *lat, double *lon,
                           int *have_coords, double *temp, int *code,
                           long long *when, char *unit)
{
    if (!wx.inited) return false;

    pthread_mutex_lock(&wx.lock);
    if (!wx.place[0] && !wx.have_coords) {
        pthread_mutex_unlock(&wx.lock);
        return false;
    }
    snprintf(place, pn, "%s", wx.place);
    *lat = wx.lat;
    *lon = wx.lon;
    *have_coords = wx.have_coords;
    *temp = wx.temp;
    *code = wx.code;
    *when = (long long)wx.when;
    *unit = wx.unit_f ? 'F' : 'C';
    pthread_mutex_unlock(&wx.lock);
    return true;
}

/*
 * The inverse, run by the greeter. Seeds the location the user's session
 * resolved AND the last reading it had, so the login screen draws something the
 * instant it comes up and then refreshes it on its own.
 *
 * Its own cache file is never consulted here: the greeter account has none, and
 * whatever it might have would belong to a different person's location.
 */
void weather_adopt(const char *place, double lat, double lon, int have_coords,
                   double temp, int code, long long when, char unit)
{
    if (!wx.inited) return;
    pthread_mutex_lock(&wx.lock);
    if (place && *place) snprintf(wx.place, sizeof(wx.place), "%s", place);
    if (have_coords) { wx.lat = lat; wx.lon = lon; wx.have_coords = 1; }
    if (when > 0) {
        if (unit == 'F' && !wx.unit_f)      temp = (temp - 32.0) * 5.0 / 9.0;
        else if (unit == 'C' && wx.unit_f)  temp = temp * 9.0 / 5.0 + 32.0;
        wx.temp = temp;
        wx.code = code;
        wx.when = (time_t)when;
    }
    pthread_mutex_unlock(&wx.lock);
}

/* ── Lifecycle ───────────────────────────────────────────── */

void weather_init(syn_server_t *s)
{
    memset(&wx, 0, sizeof(wx));
    wx.server  = s;
    wx.pipe[0] = wx.pipe[1] = -1;
    wx.unit_f  = s->config.lock_weather_unit_f;

    /* ⚠ THE MUTEX IS CREATED FIRST, ahead of every failure path below.
     * weather_current() and weather_publish_state() take it, and they are
     * called whether or not the thread ever started — a rig with no network, a
     * pipe2 that failed, a greeter. A lock created only on the success path is
     * a lock half the callers would be taking on uninitialised memory. */
    pthread_mutex_init(&wx.lock, NULL);
    pthread_cond_init(&wx.cv, NULL);
    wx.inited = 1;

    /* ⚠ Read even when the feature is off, and this is the ONLY thing that
     * runs then: a location file and a cached reading are both LOCAL, and
     * having them loaded is what lets turning the row on in Super+Z draw
     * something at once instead of after the first fetch. Nothing goes near the
     * network until weather_refresh(), which returns immediately while
     * lock_weather is off. */
    wx_read_location();
    wx_cache_load();

    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (pipe2(wx.pipe, O_CLOEXEC) < 0) {
        wlr_log(WLR_ERROR, "synui: weather: pipe() failed");
        wx.pipe[0] = wx.pipe[1] = -1;
        return;
    }
    fcntl(wx.pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(wx.pipe[1], F_SETFL, O_NONBLOCK);

    struct wl_event_loop *loop = wl_display_get_event_loop(s->display);
    wx.src   = wl_event_loop_add_fd(loop, wx.pipe[0], WL_EVENT_READABLE,
                                    wx_readable, s);
    wx.timer = wl_event_loop_add_timer(loop, wx_tick, s);

    if (pthread_create(&wx.thread, NULL, wx_thread_fn, s) != 0) {
        wlr_log(WLR_ERROR, "synui: weather: thread failed");
        if (wx.src)   { wl_event_source_remove(wx.src);   wx.src = NULL; }
        if (wx.timer) { wl_event_source_remove(wx.timer); wx.timer = NULL; }
        close(wx.pipe[0]); close(wx.pipe[1]);
        wx.pipe[0] = wx.pipe[1] = -1;
        return;                     /* the lock stays: the readers still use it */
    }
    wx.running = 1;

    if (s->config.lock_weather) {
        weather_refresh(s, false);
        wl_event_source_timer_update(wx.timer, WX_REFRESH_SEC * 1000);
    }
}

/* Called when lock_weather is turned on at runtime (the Super+Z row), so the
 * timer starts and a reading is asked for straight away. */
void weather_enabled_changed(syn_server_t *s)
{
    if (!wx.running) return;
    if (s->config.lock_weather) {
        weather_refresh(s, true);
        if (wx.timer) wl_event_source_timer_update(wx.timer, WX_REFRESH_SEC * 1000);
    } else if (wx.timer) {
        wl_event_source_timer_update(wx.timer, 0);   /* disarm */
    }
}

void weather_finish(syn_server_t *s)
{
    (void)s;
    if (wx.running) {
        atomic_store(&wx.stop, 1);
        pthread_mutex_lock(&wx.lock);
        pthread_cond_signal(&wx.cv);      /* out of the idle wait… */
        pthread_mutex_unlock(&wx.lock);
        pthread_join(wx.thread, NULL);    /* …and wx_progress aborts a transfer */
        wx.running = 0;
    }
    if (wx.inited) {
        pthread_mutex_destroy(&wx.lock);
        pthread_cond_destroy(&wx.cv);
        wx.inited = 0;
    }
    if (wx.src)   { wl_event_source_remove(wx.src);   wx.src = NULL; }
    if (wx.timer) { wl_event_source_remove(wx.timer); wx.timer = NULL; }
    if (wx.pipe[0] >= 0) { close(wx.pipe[0]); wx.pipe[0] = -1; }
    if (wx.pipe[1] >= 0) { close(wx.pipe[1]); wx.pipe[1] = -1; }
}
