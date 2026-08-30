/* synplay.h — syn-play's one header.
 *
 * A FRONT END, and deliberately nothing more. mpv plays the media, keeps the
 * playlist, shuffles it, resumes where it stopped and reads m3u; this program
 * gives that a queue you can see, a history it does not keep, and three faces.
 *
 * ⛔ THE RULE FOR EVERY FEATURE HERE: ASK MPV FIRST.
 * Shuffle is `playlist-shuffle`. Resume is `--save-position-on-quit`. A
 * playlist is an m3u8 mpv loads with `loadlist`. Re-implementing any of those
 * would be a second idea of what the playlist is, disagreeing with the one
 * actually playing the audio.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNPLAY_H
#define SYNPLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>
#include <time.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ── output shape ───────────────────────────────────────────────────────── */
typedef enum { OUT_HUMAN, OUT_REC } sp_out_t;
extern sp_out_t g_out;

/* ── util.c ─────────────────────────────────────────────────────────────── */
void  die(const char *fmt, ...);
void  warn(const char *fmt, ...);

/* XDG paths, created on demand. Each returns a pointer to a static buffer. */
const char *sp_data_dir(void);        /* ~/.local/share/syn-play           */
const char *sp_config_dir(void);      /* ~/.config/syn-play                */
const char *sp_playlist_dir(void);    /* <data>/playlists                  */
const char *sp_history_path(void);    /* <data>/history.tsv                */
const char *sp_socket_path(void);     /* $XDG_RUNTIME_DIR/syn-play.sock    */
int         sp_mkdirs(const char *path);

/* Percent-encoding, because a path may contain a tab and a title may contain a
 * newline, and both of those are the record separators used here and on the
 * wire to the window. Same encoding vibe's engine uses, for the same reason. */
void  sp_enc(const char *in, char *out, size_t cap);
void  sp_dec(const char *in, char *out, size_t cap);

/* Trailing-newline-safe line reader. Returns false at EOF. */
bool  sp_getline(FILE *f, char *buf, size_t cap);

/* The basename a human would call this, URL or path. */
void  sp_pretty_title(const char *path, char *out, size_t cap);

/* ── json.c ─────────────────────────────────────────────────────────────── */
/*
 * ⛔ A REAL SCANNER, NOT strstr("\"key\"").
 *
 * mpv answers `get_property path` with the FILENAME, and a filename may
 * contain `"data":` or `"event":` or any other key spelled out. A reader that
 * searches the blob for a key finds it inside somebody's value and reports the
 * wrong thing — or, having found a `,` where a `:` belonged, reports the field
 * absent when it is right there. That exact bug cost the greeter five rounds
 * and days of "broken hardware"; it is not repeated here.
 *
 * These walk the TOP LEVEL of one object: strings are skipped as strings,
 * nested objects and arrays are skipped whole, and only a key at depth 1 can
 * match. Every one returns false when the field is not there — which is then
 * the truth rather than a parse giving up.
 */
bool sp_json_str(const char *json, const char *key, char *out, size_t cap);
bool sp_json_num(const char *json, const char *key, double *out);
bool sp_json_bool(const char *json, const char *key, bool *out);
/* The raw span of a field's value — an array or object, for the caller to
 * walk. `len` is set to its byte length. Returns NULL when absent. */
const char *sp_json_raw(const char *json, const char *key, size_t *len);
/* Wrap a string as a JSON literal, escaping what has to be escaped. A path may
 * contain a quote or a backslash, and mpv is being handed it inside a command
 * — an unescaped one ends the string early and mpv is asked to play something
 * that was never named. */
void sp_json_quote(const char *in, char *out, size_t cap);

/* Walk the elements of a top-level JSON array. `idx` counts from 0. Returns
 * NULL past the end. */
const char *sp_json_elem(const char *array, int idx, size_t *len);

/* ── ipc.c ──────────────────────────────────────────────────────────────── */
/* Connect to a running mpv. -1 when there is none, without complaint: no
 * session running is the ordinary case, not an error. */
int  sp_connect(void);
/* Connect, starting mpv first if nothing answers. */
int  sp_connect_or_start(void);
bool sp_session_live(void);

/* One command, one reply. `reply` may be NULL. Returns false on any transport
 * failure or an mpv `error` that is not "success". */
bool sp_cmd(int fd, const char *json_args, char *reply, size_t cap);
/* Convenience wrappers over sp_cmd. */
bool sp_get_str(int fd, const char *prop, char *out, size_t cap);
bool sp_get_num(int fd, const char *prop, double *out);
bool sp_get_bool(int fd, const char *prop, bool *out);
bool sp_set_bool(int fd, const char *prop, bool v);

/* The name to put on screen for what is playing.
 *
 * ⚠ `media-title` IS NOT ALWAYS METADATA. mpv falls back to the bare filename
 * when a file carries no tags — extension and all — so taking it blindly shows
 * `Silence One.wav` for a file this program would otherwise call `Silence One`,
 * and only for the untagged files, which is the confusing half. Used when it is
 * real metadata, and the prettied filename when it is not. */
void sp_now_title(int fd, const char *path, char *out, size_t cap);

/* Spawn mpv, detached, listening on the socket. Returns false if it could not
 * be started or never came up. */
bool sp_start_mpv(void);

/* ── playlist.c ─────────────────────────────────────────────────────────── */
typedef struct {
	char path[PATH_MAX];
	char title[512];
	bool current;
} sp_entry_t;

/* The playlist mpv is holding. Returns the number filled, -1 if not running. */
int  sp_queue(int fd, sp_entry_t *out, int max);

/* ⛔ THE SILENT CORE, which is what `serve` calls. A function that prints goes
 * down the protocol pipe, and one that die()s takes the engine — and the window
 * with it. The verbs below are these plus words. */
int  sp_playlist_store(int fd, const char *name, const char **why);
bool sp_playlist_unlink(const char *name);
bool sp_playlist_open(int fd, const char *name, bool append);

int  sp_playlist_save(int fd, const char *name);
int  sp_playlist_load(int fd, const char *name, bool append);
int  sp_playlist_list(void);
int  sp_playlist_rm(const char *name);
/* <playlists>/<name>.m3u8, with the name sanitised. False if it is not a name
 * that can be a file. */
bool sp_playlist_path(const char *name, char *out, size_t cap);

/* ── history.c ──────────────────────────────────────────────────────────── */
typedef struct {
	time_t when;
	double pos;
	double dur;
	char   path[PATH_MAX];
	char   title[512];
} sp_hist_t;

/* Newest first. Returns how many were filled. */
int  sp_history_read(sp_hist_t *out, int max);
/* Records a play, or updates the one already there — one row per path, moved
 * to the front. Position 0 does not overwrite a real one. */
void sp_history_note(const char *path, const char *title, double pos, double dur);
void sp_history_clear(void);

/* ── open.c ─────────────────────────────────────────────────────────────── */
/* Where quick-open looks. Reads <config>/roots, or the XDG media dirs. */
int  sp_roots(char roots[][PATH_MAX], int max);
/* Rank candidates for `query` over history and the roots. Newest/best first. */
int  sp_find(const char *query, sp_entry_t *out, int max);
bool sp_is_media(const char *name);

/* ── tui.c / gui / serve ────────────────────────────────────────────────── */
int  sp_tui(void);
int  sp_serve(void);
int  sp_watch(void);

#endif /* SYNPLAY_H */
