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

/* As above, with the child stderr discarded. For callers where a non-zero
 * exit is an expected ANSWER, not a fault. */
int run_capture_quiet(char *const argv[], char *out, size_t cap);

/* Run argv for its exit status, with stdout and stderr discarded. */
int run_quiet(char *const argv[]);

/* Run argv for its exit status, forwarding every line it writes — on either
 * stream — to OUR stdout as a "progress<TAB>text" record, flushed as it
 * arrives. For the writes that take minutes: installing a kernel, and making
 * one bootable when that means an AUR build. On a terminal the child is left
 * connected to it instead, so the CLI keeps synpkg's own live output.
 *
 * The GUI shows the latest record and any percentage in it. Discarding this
 * output is what made a five-minute install look like a hung window. */
int run_progress(char *const argv[]);

/* run_quiet, except under --dry-run it prints the command and changes
 * nothing. Every write in this app goes through here. */
int run_or_show(char *const argv[]);

/* run_or_show, with run_progress doing the running. */
int run_or_show_progress(char *const argv[]);

/* Is `cmd` on PATH? */
int have_cmd(const char *cmd);

/* First line of `path`, newline stripped, or NULL. Returned buffer is static
 * per call site — copy it if you need two at once. */
const char *read_line_file(const char *path, char *buf, size_t cap);

/* Pull the value that follows `key` in `text`, where the line looks like
 *   "   Key Name: value"
 * as localectl and timedatectl both print. Returns 1 on a hit. */
int scrape_field(const char *text, const char *key, char *out, size_t cap);

/* Room for a full path built under a search directory, plus a filename. */
#define PATH_CAP 512

/* ── Editing a configuration file directly ───────────────────────────────────
 *
 * For the two settings with no owning tool to hand the change to. See the
 * heading in src/util.c.
 */

/* Whole file into a malloc'd buffer, or NULL. Capped at 1 MB — an unbounded
 * read of whatever happens to sit at a path is how a settings app becomes the
 * thing that OOM-kills the session. Free it. */
char *slurp(const char *path);

/* The value of `key` inside `[group]` of desktop-entry INI. 1 on a hit. */
int ini_get(const char *text, const char *group, const char *key,
            char *out, size_t cap);

/* getenv(), treating an empty value as unset — which is what the XDG spec
 * says to do and what plain getenv() does not. */
const char *env_or(const char *name, const char *fallback);

/* $HOME-relative default for the XDG single-value variables. Returns `buf`. */
const char *home_sub(const char *sub, char *buf, size_t cap);

/* $XDG_CONFIG_HOME, or its default. */
void config_home(char *out, size_t cap);

/* Copy `path` to `path.pre-syn-settings`, ONCE ever (O_EXCL). The files this
 * app edits are hand-edited by real people; a backup that is rewritten on the
 * second run is not a backup. */
void backup_once(const char *path);

/* Replace `path` with `text` via a temporary file and rename(), keeping the
 * existing mode read from the DESCRIPTOR. 0 on success. */
int write_atomic(const char *path, const char *text);

/* mkdir -p for the parent directory of `path`. */
void ensure_parent(const char *path);

/* Collapse anything that would break TSV. Tabs and newlines become spaces,
 * because a value that contains one silently invents a column. */
void tsv_clean(char *s);

/* ── Which desktop is actually running ──────────────────────────────────────
 *
 * Some settings here belong to synui alone — the clock format, the terminal it
 * launches — and are written into synui's own configuration. On a SynapseOS
 * box running KDE or GNOME they are inert: the write succeeds and nothing
 * changes, because nothing in that session reads the file. A row asks these
 * before offering such a setting. See the heading in src/util.c for how the
 * answer is arrived at, and why no graphical session at all is not a refusal.
 */

/* The desktop this session is running, as a name fit for prose AND for an
 * action-column argument: "synui", "GNOME", "KDE", or "" when there is no
 * graphical session. Computed once. */
const char *syn_session_desktop(void);
int syn_session_is_synui(void);

/* For a synui-only row: NULL when this session can use the setting, otherwise
 * the ready-made action token — "unavailable:GNOME" — that greys the row in
 * the GUI and names the reason in the CLI table. */
const char *syn_synui_only(void);

/* ── Bootloaders ────────────────────────────────────────────────────────────
 *
 * SynapseOS installs one of three, and each answers "can I boot this kernel?"
 * somewhere different. See src/boot.c for why detection is by config file
 * rather than by installed package, and why matching needs the kernel release
 * and not just the image name.
 */
enum syn_bl { SYN_BL_NONE = 0, SYN_BL_LIMINE, SYN_BL_SYSTEMD, SYN_BL_GRUB };

struct syn_boot {
	enum syn_bl kind;
	char esp[256];    /* where the ESP is mounted — NOT always /boot */
	char conf[256];   /* limine.conf, grub.cfg, or the loader/entries dir */
};

/* Fill `out` with every bootloader that has a config actually present.
 * Returns how many. Plural on purpose: a machine can carry more than one. */
int syn_boot_detect(struct syn_boot *out, size_t max);

const char *syn_boot_name(enum syn_bl kind);

/* Would this bootloader boot the kernel from `pkg`? `release` may be "". */
int syn_boot_has_entry(const struct syn_boot *bl, const char *pkg,
                       const char *release);

/* The kernel release a package owns, from /usr/lib/modules/<rel>/pkgbase. */
int syn_kernel_release(const char *pkg, char *out, size_t cap);

/* Is this one of the kernels this app manages? The list lives in kernel.c;
 * pkg.c and boot.c both ask rather than keeping copies. */
int syn_kernel_known(const char *pkg);
/* The repository a kernel needs, or NULL when Arch ships it (the CachyOS ones
 * are the only ones that are not in core/extra). */
const char *syn_kernel_repo(const char *pkg);
/* Is [cachyos] configured? Asked of synpkg, which owns the answer. */
int syn_cachyos_enabled(void);

/* Would this bootloader boot `pkg` when nobody touches the menu?
 * 1 yes, 0 no, -1 cannot be told. Every loader keeps this answer somewhere
 * different, and for two of the three it is NOT where their own tool writes
 * it — see the note above syn_boot_is_default in src/boot.c. */
int syn_boot_is_default(const struct syn_boot *bl, const char *pkg,
                        const char *release);

/* Make a kernel bootable under the detected bootloader. Refuses without
 * --confirm; see the escalation note at the top of src/boot.c. */
int do_boot(int argc, char **argv);

/* Make a kernel the one that BOOTS. Same posture as do_boot: refuses without
 * --confirm, and --dry-run reports what would run so the dialogue and the
 * write are one code path. */
int do_default(int argc, char **argv);

/* ── Panes ──────────────────────────────────────────────────────────────── */
int pane_display(void);
int pane_region(void);
int pane_power(void);
int pane_system(void);
int pane_network(void);
int pane_bluetooth(void);
int pane_kernel(void);

/* Which application opens what, and — the part that matters — which layer
 * decided it. See the header of src/apps.c. */
int pane_apps(void);

/* The system clock beside the DESKTOP clock — what time it is, and how the
 * bar, the lock screen and the desktop widget write it. See src/time.c. */
int pane_time(void);
int pane_ai(void);
int pane_speech(void);

/* ── Writes ─────────────────────────────────────────────────────────────────
 *
 * Deliberately thin. Everything that needs privilege is handed to a systemd
 * tool that already does its own polkit check, so this binary is not setuid,
 * ships no polkit policy of its own, and cannot become a way to run something
 * as root that localectl would have refused.
 */
int do_set(int argc, char **argv);

/* `set app <role> <application>` — the default application for a role, written
 * into the user's own mimeapps.list. `terminal` is the odd one out and goes to
 * synuirc, because that is the file synui actually reads. */
int do_set_app(int argc, char **argv);

/* The applications that could take a role, for the one row the GUI is pointed
 * at — the same shape do_modes uses, and fetched the same way. */
int do_apps(int argc, char **argv);

/* The 12/24-hour, seconds and date-layout knobs, written to the user's own
 * ~/.config/synui/clock.state. No privilege is involved and none is asked for.
 * Routed here from do_set(). */
int do_set_clock(const char *key, const char *val);

/* `choices <key>` — the options a `choice:` row can take, as
 * `id<TAB>label<TAB>current`. Fetched when a row is picked, like `apps` and
 * `modes`, because the date layouts come from synui-clock rather than from
 * anything this package can hardcode. */
int do_choices(int argc, char **argv);

/* Is synnet's input firewall wanted? Reads /etc/synnet/firewall, where ABSENT
 * MEANS ON — the same rule synnet itself applies, and it has to be the same
 * one: a pane that read a missing file as "off" would report an unfiltered
 * machine that is in fact filtered, which is the failure this whole area
 * already had once. Defined in network.c, beside the rows that show it. */
int synnet_firewall_on(void);
int do_unit(int argc, char **argv);

/* Bring one interface up or down, wired or wireless. */
int do_device(int argc, char **argv);

/* Re-probe a DRM connector. Does NOT escalate — see src/probe.c. */
int do_probe(int argc, char **argv);

/* Display modes, via wlr-randr; synui persists whatever it sets. */
int do_modes(int argc, char **argv);
int do_mode(int argc, char **argv);

/* Install/remove, delegated to synpkg. Kernels only — see src/pkg.c. */
int do_pkg(int argc, char **argv);

extern int g_dry_run;

#endif /* SYNSETTINGS_H */
