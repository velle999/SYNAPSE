/*
 * arcade.h — syn-arcade, the SynapseOS game assistant.
 *
 * Three subsystems, one binary:
 *
 *   hud    the MangoHud overlay — what it shows, where it sits, and whether
 *          it is on. Driven live, inside a running game, by rewriting the
 *          config file MangoHud is watching (see hud.c for why that works).
 *   pads   game controllers — what is plugged in, what it is called, whether
 *          its sticks are centred, and what its deadzones should be.
 *   binds  the two compositor shortcuts that drive `hud`, written into
 *          synuirc as ordinary `bind =` lines.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYN_ARCADE_H
#define SYN_ARCADE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* ── util.c ──────────────────────────────────────────────────────────────── */

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* Percent-encode/decode, the suite's record encoding. Every field of every
 * record goes through pct_encode; see rec_row(). */
char *pct_encode(const char *s);
char *pct_decode(const char *s);

/* One record: N percent-encoded fields, tab separated, newline terminated.
 * The FIRST row a command prints names the columns, so a column added in C
 * reaches the GUI with no change to the QML. */
void rec_row(int nfields, ...);

void strip_trailing_newline(char *s);
char *trim(char *s);

/* Slurp a whole file. NULL on any failure; caller frees. */
char *read_file(const char *path);

/* Write `text` to `path`, creating parent directories as needed.
 *
 * ⚠ IN PLACE — open(O_TRUNC) then a single write(2) — and NOT via a temp file
 * and rename. That is deliberate and load-bearing for hud.c; the reasoning is
 * in the comment above hud_write_config(). Returns 0 on success, -errno. */
int write_file_inplace(const char *path, const char *text);

/* $HOME/<rel>, or false if HOME is unset/too long. */
bool home_path(char *buf, size_t n, const char *rel);
/* $XDG_CONFIG_HOME (or $HOME/.config)/<rel>. */
bool config_path(char *buf, size_t n, const char *rel);

bool file_exists(const char *path);
bool file_writable(const char *path);
/* mkdir -p on the DIRECTORY part of a file path. */
int mkdir_parents(const char *path);

/* ── hud.c ───────────────────────────────────────────────────────────────── */

/* The eight places MangoHud will put the overlay, in the order the cycle key
 * walks them: clockwise from the top-left corner. */
#define HUD_POS_COUNT 8
extern const char *const hud_positions[HUD_POS_COUNT];

/* Which file MangoHud will actually read.
 *
 * ⚠ NOT simply ~/.config/MangoHud/MangoHud.conf — MangoHud reads exactly ONE
 * config file, chosen by a precedence order in which /etc/MangoHud.conf beats
 * the user's. hud.c reimplements that order; hud_effective_config() is the
 * answer, and `syn-arcade hud path` prints it. */
bool hud_effective_config(char *buf, size_t n);

int cmd_hud(int argc, char **argv);

/* ── pad.c ───────────────────────────────────────────────────────────────── */

int cmd_pads(int argc, char **argv);

/* Every controller on the machine reduced to the words a menu needs — "up",
 * "accept", "back" — one per line on stdout, until the reader goes away.
 *
 * Lives in pad.c because that is where the evdev code is, and is exposed here
 * because big screen mode is what reads it. ⚠ It synthesises NOTHING: no
 * uinput device, no virtual keyboard, nothing the compositor or any other
 * application can see. See the comment above it for why that is not a detail. */
int pads_nav_stream(void);

/* ── binds.c ─────────────────────────────────────────────────────────────── */

/* Ask the running synui to re-read its config, so a just-written bind becomes
 * live without a logout. Declared here because hud.c's `ensure` path and the
 * installer both reach for it. */
int binds_reload(void);

/* Whether the managed block in synuirc starts big screen mode at login, and
 * setting it either way.
 *
 * ⚠ These live in binds.c and not in big.c because there is exactly ONE
 * managed block in that file, delimited by one pair of markers. Two writers
 * appending their own block would leave synui applying whichever it read last,
 * and neither command able to remove the other's — which is why the block is
 * read, changed and rewritten whole here rather than appended to from two
 * places. */
bool binds_autostart_get(void);
int  binds_autostart_set(bool on);

int cmd_binds(int argc, char **argv);

/* ── sdlmap.c ────────────────────────────────────────────────────────────── */

int cmd_map(int argc, char **argv);

/* ── big.c ───────────────────────────────────────────────────────────────── */

int cmd_big(int argc, char **argv);

/* Exit codes shared across the commands.
 *
 * 100 is "nothing to list" — an ANSWER, not a failure, and the GUI draws an
 * empty table for it rather than an error. syn-disks uses it the same way. */
#define EX_OK        0
#define EX_USAGE     2
#define EX_FAIL      1
#define EX_EMPTY     100

#endif /* SYN_ARCADE_H */
