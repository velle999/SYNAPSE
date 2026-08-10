/* syn-settings — SynapseOS system settings.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNSETTINGS_H
#define SYNSETTINGS_H

#include <dirent.h>
#include <stdio.h>

/* ── The record protocol ────────────────────────────────────────────────────
 *
 * Every reader prints TSV: one header line naming the columns, then rows. It
 * is the same shape synfiles and synpkg use, for the same reason — the GUI is
 * one consumer of a format anything else can parse, and `syn-settings --rec
 * region | column -t` has to stay a useful thing to type.
 *
 * A value is NEVER empty in a way that shifts columns: an unknown reads
 * "unknown" and an unsupported one reads "-". A missing tool is a value, not
 * a failure — a box without NetworkManager still has a Region pane.
 */
void rec_header(const char *cols);
void rec_row(const char *fmt, ...);

/* ── Shelling out ───────────────────────────────────────────────────────────
 *
 * Every reader here is a front-end onto a tool that already owns the answer
 * (localectl, timedatectl, systemctl, synctl). Parsing their output is not
 * elegant, and it is still right: reimplementing what localectl knows about
 * vconsole vs X11 keymaps is how a settings app starts disagreeing with the
 * system it is supposed to describe.
 */

/* Run argv, capture the first `cap` bytes of stdout into `out`. Returns the
 * exit status, or -1 if the command could not be run at all. stderr is left
 * alone so a real error is still visible on a terminal. */
int run_capture(char *const argv[], char *out, size_t cap);

/* Run argv for its exit status, with stdout and stderr discarded. */
int run_quiet(char *const argv[]);

/* Is `cmd` on PATH? */
int have_cmd(const char *cmd);

/* First line of `path`, newline stripped, or NULL. Returned buffer is static
 * per call site — copy it if you need two at once. */
const char *read_line_file(const char *path, char *buf, size_t cap);

/* Pull the value that follows `key` in `text`, where the line looks like
 *   "   Key Name: value"
 * as localectl and timedatectl both print. Returns 1 on a hit. */
int scrape_field(const char *text, const char *key, char *out, size_t cap);

/* Collapse anything that would break TSV. Tabs and newlines become spaces,
 * because a value that contains one silently invents a column. */
void tsv_clean(char *s);

/* ── Panes ──────────────────────────────────────────────────────────────── */
int pane_display(void);
int pane_region(void);
int pane_power(void);
int pane_system(void);
int pane_network(void);
int pane_bluetooth(void);

/* ── Writes ─────────────────────────────────────────────────────────────────
 *
 * Deliberately thin. Everything that needs privilege is handed to a systemd
 * tool that already does its own polkit check, so this binary is not setuid,
 * ships no polkit policy of its own, and cannot become a way to run something
 * as root that localectl would have refused.
 */
int do_set(int argc, char **argv);
int do_unit(int argc, char **argv);

/* Re-probe a DRM connector. Does NOT escalate — see src/probe.c. */
int do_probe(int argc, char **argv);

extern int g_dry_run;

#endif /* SYNSETTINGS_H */
