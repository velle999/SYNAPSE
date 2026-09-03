/* syn-settings — the Default Applications pane.
 *
 * "Which program opens this?" has, on a Linux desktop, about six answers
 * layered on top of each other, and the interesting bugs are always about
 * WHICH layer won. synfiles hit exactly that: a MimeType= line in a .desktop
 * file only makes an application a CANDIDATE, and with no mimeapps.list entry
 * anywhere the winner comes from mimeinfo.cache — "whoever declared the type"
 * — which on this machine was kitty, for inode/directory. Declaring the type
 * changed nothing, and nothing said so.
 *
 * So this pane does what the rest of this app does: it names the current
 * answer AND the file it came from, and distinguishes a real choice from a
 * fallback nobody made. "kitty, because it happened to be first in
 * mimeinfo.cache" and "kitty, because you picked it" are different facts and
 * they look identical in every other settings app.
 *
 * ── Precedence, which is the whole job ──────────────────────────────────────
 *
 * Per the freedesktop association spec, mimeapps.list is looked for in this
 * order, and the FIRST file with an entry for the type wins:
 *
 *   $XDG_CONFIG_HOME/$desktop-mimeapps.list, then mimeapps.list
 *   each $XDG_CONFIG_DIRS   (same two)
 *   $XDG_DATA_HOME/applications/  (same two)
 *   each $XDG_DATA_DIRS/applications/  (same two)
 *
 * $desktop comes from $XDG_CURRENT_DESKTOP, which on this OS is "SynapseOS" —
 * a value that has already broken one thing that assumed a familiar name
 * (reference_xdg_current_desktop_synapseos_breaks_portal_conf), so it is read
 * rather than assumed, and every colon-separated element is tried in order.
 *
 * An entry naming an application that is not installed is SKIPPED, not
 * reported: that is what the spec says to do, and a settings pane that showed
 * "Default: dolphin.desktop" for an uninstalled Dolphin would be describing a
 * line of text rather than what happens when you double-click.
 *
 * ── Writing ─────────────────────────────────────────────────────────────────
 *
 * Writes go to $XDG_CONFIG_HOME/mimeapps.list — the user's own file, the top
 * of that list, and the layer a person's choice belongs in. The rest of the
 * file is preserved line for line, because it is somebody's data and this app
 * is not its only editor.
 *
 * ── The terminal is not a mime type ─────────────────────────────────────────
 *
 * It is the one default here that no mimeapps.list decides. synui reads it
 * from `terminal =` in synuirc, and that is what actually launches when you
 * press the key — so setting an x-scheme-handler/terminal entry would be a
 * control that changes a file nothing on this desktop reads. It gets its own
 * small writer instead, and a SIGHUP, which is all synui needs to reparse.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"
#include "i18n.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_CAND    64

/* ── roles ──────────────────────────────────────────────────────────────────
 *
 * A role is a human question ("what opens my photos?") and the list of mime
 * types that answer it. Setting a role writes EVERY type in its list, because
 * a picture viewer that took over PNG and left JPEG behind is not a setting
 * anybody meant to make — and that half-applied state is precisely what the
 * existing ~/.config/mimeapps.list on this machine looks like when a file
 * manager writes one type at a time.
 *
 * The FIRST mime in each list is the one reported as "the current default".
 * It is the type the role is really about; the rest follow it.
 *
 * The lists are deliberately explicit rather than resolved through
 * /usr/share/mime/subclasses. Alias resolution belongs in a mime library, and
 * synfiles already carries one for its own menus; duplicating it here to save
 * typing six strings would buy a second implementation to disagree with.
 */
struct app_role {
	const char *id;
	const char *label;
	const char *detail;
	const char *mimes[12];
};

static const struct app_role g_roles[] = {
	{ "browser", N_("Web Browser"),
	  N_("http and https links, and .html files"),
	  { "x-scheme-handler/http", "x-scheme-handler/https", "text/html",
	    "application/xhtml+xml", NULL } },

	{ "mail", N_("Email"),
	  N_("mailto: links"),
	  { "x-scheme-handler/mailto", NULL } },

	{ "files", N_("File Manager"),
	  N_("folders — inode/directory, the one synfiles ships a default for"),
	  { "inode/directory", NULL } },

	{ "editor", N_("Text Editor"),
	  N_("plain text and source code"),
	  { "text/plain", "text/x-csrc", "text/x-chdr", "text/x-c++src",
	    "text/x-python", "text/x-shellscript", "text/markdown",
	    "application/json", "text/css", "application/xml", NULL } },

	{ "image", N_("Image Viewer"),
	  N_("photographs and drawings"),
	  { "image/png", "image/jpeg", "image/gif", "image/webp", "image/bmp",
	    "image/tiff", "image/svg+xml", NULL } },

	{ "audio", N_("Audio Player"),
	  N_("music and sound files"),
	  { "audio/mpeg", "audio/flac", "audio/x-wav", "audio/ogg",
	    "audio/mp4", "audio/x-vorbis+ogg", NULL } },

	{ "video", N_("Video Player"),
	  N_("films and clips"),
	  { "video/mp4", "video/x-matroska", "video/webm", "video/quicktime",
	    "video/x-msvideo", "video/mpeg", NULL } },

	{ "pdf", N_("Document Viewer"),
	  N_("PDF files"),
	  { "application/pdf", NULL } },

	{ "archive", N_("Archive Manager"),
	  N_("zip, tar and the rest"),
	  { "application/zip", "application/x-tar", "application/gzip",
	    "application/x-7z-compressed", "application/vnd.rar",
	    "application/x-xz", "application/zstd", "application/x-bzip2",
	    NULL } },

	/* Not a mime type. See the header. */
	{ "terminal", N_("Terminal"),
	  N_("what synui launches for a terminal — synuirc, not mimeapps.list"),
	  { NULL } },
};
static const size_t g_nroles = sizeof g_roles / sizeof g_roles[0];

static const struct app_role *role_by_id(const char *id)
{
	for (size_t i = 0; i < g_nroles; i++)
		if (!strcmp(g_roles[i].id, id))
			return &g_roles[i];
	return NULL;
}

static int role_is_terminal(const struct app_role *r)
{
	return !strcmp(r->id, "terminal");
}

/* ── the search path ────────────────────────────────────────────────────── */

/* Every XDG data dir plus the two home ones; 64 is far past any real system.
 * PATH_CAP lives in the header — util.c's file helpers size their buffers
 * from it. */
#define MAX_PATHS 64

struct pathlist {
	char v[MAX_PATHS][PATH_CAP];
	int n;
};

static void pl_add(struct pathlist *pl, const char *dir, const char *tail)
{
	if (pl->n >= MAX_PATHS || !dir || !*dir) return;
	if (snprintf(pl->v[pl->n], PATH_CAP, "%s%s", dir, tail) >= PATH_CAP)
		return;
	pl->n++;
}

/* Every directory that can hold .desktop files, highest priority first. */
static void app_dirs(struct pathlist *pl)
{
	char b[PATH_CAP];
	pl->n = 0;

	const char *dh = getenv("XDG_DATA_HOME");
	pl_add(pl, (dh && *dh) ? dh : home_sub("/.local/share", b, sizeof b),
	       "/applications");

	char *dirs = strdup(env_or("XDG_DATA_DIRS", "/usr/local/share:/usr/share"));
	if (!dirs) return;
	for (char *s = dirs, *tok; (tok = strsep(&s, ":")); )
		if (*tok) pl_add(pl, tok, "/applications");
	free(dirs);
}

/* Every mimeapps.list that can decide a default, in the order the spec says
 * to consult them. */
static void mimeapps_files(struct pathlist *pl)
{
	char b[PATH_CAP];
	pl->n = 0;

	/* $XDG_CURRENT_DESKTOP is colon-separated and ordered, most specific
	 * first — "SynapseOS" here, but never assumed to be. */
	char desks[256];
	snprintf(desks, sizeof desks, "%s", env_or("XDG_CURRENT_DESKTOP", ""));

	struct pathlist bases = { .n = 0 };
	const char *ch = getenv("XDG_CONFIG_HOME");
	pl_add(&bases, (ch && *ch) ? ch : home_sub("/.config", b, sizeof b), "");

	char *cdirs = strdup(env_or("XDG_CONFIG_DIRS", "/etc/xdg"));
	if (cdirs) {
		for (char *s = cdirs, *tok; (tok = strsep(&s, ":")); )
			if (*tok) pl_add(&bases, tok, "");
		free(cdirs);
	}

	struct pathlist adirs;
	app_dirs(&adirs);
	for (int i = 0; i < adirs.n; i++)
		pl_add(&bases, adirs.v[i], "");

	for (int i = 0; i < bases.n; i++) {
		char d[256];
		snprintf(d, sizeof d, "%s", desks);
		for (char *s = d, *tok; (tok = strsep(&s, ":")); ) {
			if (!*tok) continue;
			char tail[192];
			snprintf(tail, sizeof tail, "/%s-mimeapps.list", tok);
			pl_add(pl, bases.v[i], tail);
		}
		pl_add(pl, bases.v[i], "/mimeapps.list");
	}
}

/* Where this user's own choices belong. */
static void user_mimeapps(char *out, size_t cap)
{
	char b[PATH_CAP - 32];
	config_home(b, sizeof b);
	snprintf(out, cap, "%s/mimeapps.list", b);
}

/* ── applications ───────────────────────────────────────────────────────── */

static int desktop_path(const char *id, char *out, size_t cap)
{
	/* A NAME, never a path. "../../etc/something" must not become a lookup
	 * anywhere on the filesystem — the same rule synfiles enforces before it
	 * will run an Exec= line. */
	if (!id || !*id || strchr(id, '/')) return 0;

	struct pathlist pl;
	app_dirs(&pl);
	for (int i = 0; i < pl.n; i++) {
		char p[PATH_CAP + 128];
		snprintf(p, sizeof p, "%s/%s", pl.v[i], id);
		if (access(p, R_OK) == 0) {
			snprintf(out, cap, "%s", p);
			return 1;
		}
	}
	return 0;
}

/* The Name= a launcher shows, falling back to the id with its suffix removed
 * so a row is never blank. */
static void desktop_name(const char *id, char *out, size_t cap)
{
	char path[PATH_CAP + 128];
	char *text = NULL;

	if (desktop_path(id, path, sizeof path))
		text = slurp(path);

	if (text && ini_get(text, "Desktop Entry", "Name", out, cap) && out[0]) {
		free(text);
		tsv_clean(out);
		return;
	}
	free(text);

	snprintf(out, cap, "%s", id);
	char *dot = strstr(out, ".desktop");
	if (dot) *dot = '\0';
	tsv_clean(out);
}

static int desktop_hidden(const char *id)
{
	char path[PATH_CAP + 128];
	if (!desktop_path(id, path, sizeof path)) return 1;

	char *text = slurp(path);
	if (!text) return 1;

	char v[64];
	int hidden = (ini_get(text, "Desktop Entry", "NoDisplay", v, sizeof v)
	              && !strcmp(v, "true"))
	          || (ini_get(text, "Desktop Entry", "Hidden", v, sizeof v)
	              && !strcmp(v, "true"));
	free(text);
	return hidden;
}

/* ── what is the default right now ──────────────────────────────────────── */

/* 1 = a real choice, from `src`. 2 = nobody chose; this is only what
 * mimeinfo.cache happened to list first. 0 = nothing at all. */
static int current_default(const char *mime, char *id, size_t idcap,
                           char *src, size_t srccap)
{
	id[0] = '\0';
	src[0] = '\0';

	struct pathlist pl;
	mimeapps_files(&pl);

	for (int i = 0; i < pl.n; i++) {
		char *text = slurp(pl.v[i]);
		if (!text) continue;

		char val[1024];
		if (ini_get(text, "Default Applications", mime, val, sizeof val)) {
			/* The value is a LIST, and the spec's rule is to walk it until
			 * one names an application that is actually installed. A
			 * settings pane reporting an uninstalled entry would be
			 * describing the file rather than what opens the file. */
			for (char *s = val, *tok; (tok = strsep(&s, ";")); ) {
				char p[PATH_CAP + 128];
				if (*tok && desktop_path(tok, p, sizeof p)) {
					snprintf(id, idcap, "%s", tok);
					snprintf(src, srccap, "%s", pl.v[i]);
					free(text);
					return 1;
				}
			}
		}
		free(text);
	}

	/* Nobody chose. Whoever declared the type in mimeinfo.cache gets it —
	 * the exact mechanism that made kitty the file manager. */
	struct pathlist ad;
	app_dirs(&ad);
	for (int i = 0; i < ad.n; i++) {
		char p[PATH_CAP + 32];
		snprintf(p, sizeof p, "%s/mimeinfo.cache", ad.v[i]);
		char *text = slurp(p);
		if (!text) continue;

		char val[4096];
		if (ini_get(text, "MIME Cache", mime, val, sizeof val)) {
			for (char *s = val, *tok; (tok = strsep(&s, ";")); ) {
				char q[PATH_CAP + 128];
				if (*tok && desktop_path(tok, q, sizeof q)) {
					snprintf(id, idcap, "%s", tok);
					snprintf(src, srccap, "%s", p);
					free(text);
					return 2;
				}
			}
		}
		free(text);
	}

	return 0;
}

/* ── the terminal, which lives somewhere else entirely ──────────────────── */

static void synuirc_path(char *out, size_t cap)
{
	char b[PATH_CAP - 32];
	config_home(b, sizeof b);
	snprintf(out, cap, "%s/synui/synuirc", b);
}

/* The terminals worth offering, best first — the same order synui's own
 * fallback chain uses, so the list and the fallback cannot disagree. Only the
 * ones actually on PATH are shown: a chooser offering software that is not
 * installed produces a setting that silently does nothing. */
static const char *g_terminals[] = {
	/* syntty first because it is the shipped default (synui 0.1.0-359) and
	 * the order here IS the order the list is offered in. kitty stays second:
	 * it was the default from 215 to 358, so it is what most existing
	 * machines have in synuirc, and foot behind it is the CPU-rendered
	 * rescue that works where an OpenGL terminal does not. */
	"syntty", "kitty", "foot", "alacritty", "ghostty", "wezterm", "konsole",
	"gnome-terminal", "xterm", NULL
};

static int terminal_current(char *out, size_t cap, char *src, size_t srccap)
{
	char rc[PATH_CAP];
	synuirc_path(rc, sizeof rc);

	char *text = slurp(rc);
	if (text) {
		/* synuirc is `key = value`, not INI groups, and carries comments. */
		for (const char *p = text; p && *p; ) {
			const char *eol = strchr(p, '\n');
			size_t len = eol ? (size_t)(eol - p) : strlen(p);

			while (len && (*p == ' ' || *p == '\t')) { p++; len--; }
			if (len > 8 && !strncmp(p, "terminal", 8)) {
				const char *v = p + 8;
				size_t vl = len - 8;
				while (vl && (*v == ' ' || *v == '\t')) { v++; vl--; }
				if (vl && *v == '=') {
					v++; vl--;
					while (vl && (*v == ' ' || *v == '\t')) { v++; vl--; }
					while (vl && (v[vl - 1] == ' ' || v[vl - 1] == '\r'))
						vl--;
					if (vl) {
						if (vl >= cap) vl = cap - 1;
						memcpy(out, v, vl);
						out[vl] = '\0';
						tsv_clean(out);
						snprintf(src, srccap, "%s", rc);
						free(text);
						return 1;
					}
				}
			}
			p = eol ? eol + 1 : NULL;
		}
		free(text);
	}

	/* No knob set: synui falls back to its built-in chain, so report the one
	 * that would actually run rather than the word "unset". */
	for (int i = 0; g_terminals[i]; i++) {
		if (have_cmd(g_terminals[i])) {
			snprintf(out, cap, "%s", g_terminals[i]);
			/* ⚠ MARKED, because this cell is PROSE where every other
			 * value of `src` is a path. The window translates the
			 * detail column at the draw site, and a lookup can only
			 * find a cell that is a msgid.
			 *
			 * ⛔ AND IT IS ONLY EVER EMITTED ON A MACHINE WITH NO
			 * `terminal =` IN synuirc — which is a fresh install, and
			 * is not the box this is written on. It went unmarked
			 * because nothing here can draw it: the row said
			 * "chosen" and a path. It failed in a clean build root.
			 * tests/i18n_test.sh now collects this pane under an
			 * empty config as well, for the same reason it stubs
			 * fprintd and SYN_SETTINGS_LIBDIR. */
			snprintf(src, srccap, "%s",
			         N_("synui's built-in fallback — nothing set"));
			return 2;
		}
	}
	return 0;
}

/* ── the pane ───────────────────────────────────────────────────────────── */

/* One candidate row: the id a write would use, the name a person reads, and
 * whether it is the one in force. */
static void cand_row(const char *id, const char *name, const char *cur)
{
	char clean[256];
	snprintf(clean, sizeof clean, "%s", name);
	tsv_clean(clean);
	rec_row("%s\t%s\t%s", id, clean, strcmp(id, cur) ? "-" : "current");
}

static void emit_candidates(const struct app_role *r, const char *cur)
{
	if (role_is_terminal(r)) {
		/* Only what is on PATH. A chooser offering software that is not
		 * installed produces a setting that silently does nothing — which is
		 * the failure this whole pane exists to make visible. */
		for (int t = 0; g_terminals[t]; t++)
			if (have_cmd(g_terminals[t]))
				cand_row(g_terminals[t], g_terminals[t], cur);
		return;
	}

	char seen[MAX_CAND][128];
	int nseen = 0;

	struct pathlist ma, ad;
	mimeapps_files(&ma);
	app_dirs(&ad);

	/* Both places an association can be declared: somebody's explicit
	 * [Added Associations], and the cache built from every MimeType= line on
	 * the system. A candidate list drawn from only the second misses an
	 * application the user associated by hand. */
	for (int m = 0; r->mimes[m]; m++) {
		for (int pass = 0; pass < 2; pass++) {
			const struct pathlist *pl = pass ? &ad : &ma;
			for (int i = 0; i < pl->n; i++) {
				char p[PATH_CAP + 32];
				if (pass)
					snprintf(p, sizeof p, "%s/mimeinfo.cache", pl->v[i]);
				else
					snprintf(p, sizeof p, "%s", pl->v[i]);

				char *text = slurp(p);
				if (!text) continue;

				char val[4096];
				int got = ini_get(text, pass ? "MIME Cache" : "Added Associations",
				                  r->mimes[m], val, sizeof val)
				       || (!pass && ini_get(text, "Default Applications",
				                            r->mimes[m], val, sizeof val));
				if (got) {
					for (char *s = val, *tok; (tok = strsep(&s, ";")); ) {
						if (!*tok || nseen >= MAX_CAND) continue;

						int dup = 0;
						for (int k = 0; k < nseen && !dup; k++)
							dup = !strcmp(seen[k], tok);
						if (dup) continue;

						/* NoDisplay is how a helper entry hides itself.
						 * Offering it puts something in front of the user
						 * that nobody meant to be choosable — and it also
						 * skips anything not installed, since a missing
						 * file cannot be read. */
						if (desktop_hidden(tok)) continue;

						snprintf(seen[nseen], sizeof seen[nseen], "%s", tok);
						nseen++;

						char name[256];
						desktop_name(tok, name, sizeof name);
						cand_row(tok, name, cur);
					}
				}
				free(text);
			}
		}
	}
}

/* Candidates for ONE role, in the shape `modes` uses for one connector:
 * fetched when a row is picked rather than carried in the table. A role has a
 * dozen applications and they belong to the row you chose, not to every row.
 *
 *   id<TAB>name<TAB>current|-
 */
int do_apps(int argc, char **argv)
{
	if (argc < 1) {
		fprintf(stderr, "usage: syn-settings apps <role>\n");
		return 2;
	}

	const struct app_role *r = role_by_id(argv[0]);
	if (!r) {
		fprintf(stderr, "syn-settings: unknown role '%s'\n", argv[0]);
		return 2;
	}

	char cur[256] = "", src[PATH_CAP + 128] = "";
	if (role_is_terminal(r))
		terminal_current(cur, sizeof cur, src, sizeof src);
	else
		current_default(r->mimes[0], cur, sizeof cur, src, sizeof src);

	emit_candidates(r, cur);
	return 0;
}

int pane_apps(void)
{
	rec_header("role\tapplication\tstate\tcovers\tdetail\taction");

	for (size_t i = 0; i < g_nroles; i++) {
		const struct app_role *r = &g_roles[i];
		char id[256] = "", name[256] = "";
		/* src holds a full path built under a search directory, so it is
		 * sized for the longest one plus a filename rather than for a
		 * directory. */
		char src[PATH_CAP + 128] = "";
		int how;

		if (role_is_terminal(r)) {
			how = terminal_current(id, sizeof id, src, sizeof src);
			snprintf(name, sizeof name, "%s", id);
		} else {
			how = current_default(r->mimes[0], id, sizeof id,
			                      src, sizeof src);
			if (how) desktop_name(id, name, sizeof name);
		}

		/* THE distinction this pane exists to draw. "chosen" is a decision
		 * somebody made and can undo; "fallback" is what happens when nobody
		 * has decided, and it changes the day a package is installed. They
		 * look identical in every other settings app, and the difference is
		 * the whole reason kitty was the file manager. */
		const char *state = how == 1 ? "chosen"
		                  : how == 2 ? "fallback" : "none";
		const char *detail = how ? src : N_("nothing installed handles this");

		/* Every role above this one is decided by mimeapps.list, which every
		 * desktop reads — those rows are settable wherever this app runs.
		 * `terminal` is the odd one out in this way too: it is written to
		 * synuirc and read by synui, so under KDE or GNOME setting it would
		 * report success and change nothing about what those desktops open.
		 * See syn_synui_only() in src/util.c. */
		char act[80];
		/* Sized off src, which is a full path and the same PATH_CAP + 128 the
		 * declaration above uses, plus room for the sentence around it. */
		char why[PATH_CAP + 320];
		const char *elsewhere = role_is_terminal(r) ? syn_synui_only() : NULL;
		if (elsewhere) {
			snprintf(act, sizeof act, "%s", elsewhere);
			snprintf(why, sizeof why,
			         "%s — read by synui, and %s is the session running",
			         how ? src : "synuirc", syn_session_desktop());
			detail = why;
		} else {
			snprintf(act, sizeof act, "app:%s", r->id);
		}

		rec_row("%s\t%s\t%s\t%s\t%s\t%s",
		        r->label, name[0] ? name : "-", state, r->detail,
		        detail, act);
	}

	return 0;
}

/* ── writing ────────────────────────────────────────────────────────────── */

/* Is `key` one of this role's mime types? */
static int role_owns(const struct app_role *r, const char *key)
{
	for (int i = 0; r->mimes[i]; i++)
		if (!strcmp(r->mimes[i], key))
			return 1;
	return 0;
}

static int set_mime_default(const struct app_role *r, const char *id)
{
	char path[PATH_CAP];
	user_mimeapps(path, sizeof path);

	char *text = slurp(path);          /* NULL is fine: a first choice */
	size_t cap = (text ? strlen(text) : 0) + 4096;
	char *out = malloc(cap);
	if (!out) { free(text); return 1; }
	out[0] = '\0';

	size_t n = 0;
	int in_defaults = 0, wrote = 0, seen_defaults = 0;

	/* Rewritten line by line rather than parsed and regenerated. The rest of
	 * this file is somebody's data — [Added Associations], types this app
	 * knows nothing about, their comments and their ordering — and a settings
	 * app that normalises a user's file every time it is touched is a settings
	 * app that loses things. */
	for (const char *p = text; p && *p; ) {
		const char *eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol - p) : strlen(p);

		int is_group = len && *p == '[';
		if (is_group && in_defaults && !wrote) {
			/* Leaving the group: our entries go at its end. */
			for (int i = 0; r->mimes[i]; i++)
				n += (size_t)snprintf(out + n, cap - n, "%s=%s\n",
				                      r->mimes[i], id);
			wrote = 1;
		}
		if (is_group) {
			char name[128];
			size_t nl = len - 1;
			const char *close = memchr(p, ']', len);
			if (close) nl = (size_t)(close - p) - 1;
			if (nl >= sizeof name) nl = sizeof name - 1;
			memcpy(name, p + 1, nl);
			name[nl] = '\0';
			in_defaults = !strcmp(name, "Default Applications");
			if (in_defaults) seen_defaults = 1;
		}

		/* Drop this role's own keys wherever they were; they are re-emitted
		 * together at the end of the group. */
		int drop = 0;
		if (in_defaults && !is_group) {
			const char *eq = memchr(p, '=', len);
			if (eq) {
				char key[256];
				size_t kl = (size_t)(eq - p);
				if (kl < sizeof key) {
					memcpy(key, p, kl);
					key[kl] = '\0';
					drop = role_owns(r, key);
				}
			}
		}

		if (!drop) {
			if (n + len + 2 >= cap) break;
			memcpy(out + n, p, len);
			n += len;
			out[n++] = '\n';
			out[n] = '\0';
		}

		p = eol ? eol + 1 : NULL;
	}

	if (!seen_defaults) {
		/* The blank line only separates; on an empty file it would just be a
		 * leading one. */
		if (n && out[n - 1] != '\n') out[n++] = '\n';
		n += (size_t)snprintf(out + n, cap - n, "%s[Default Applications]\n",
		                      n ? "\n" : "");
	}
	if (!wrote) {
		for (int i = 0; r->mimes[i]; i++)
			n += (size_t)snprintf(out + n, cap - n, "%s=%s\n",
			                      r->mimes[i], id);
	}

	free(text);

	if (g_dry_run) {
		printf("would write %s:\n%s", path, out);
		free(out);
		return 0;
	}

	ensure_parent(path);
	backup_once(path);
	int rc = write_atomic(path, out);
	free(out);
	return rc;
}

/* The terminal knob, in synuirc, plus the SIGHUP synui needs to notice.
 * synui_config_reload() reparses the whole file and the terminal is read at
 * use, so no logout is involved — but without the signal the change takes
 * effect at the next login and reads as "it did nothing". */
static int set_terminal(const char *term)
{
	for (int i = 0; ; i++) {
		if (!g_terminals[i]) {
			fprintf(stderr, "syn-settings: '%s' is not a terminal this "
			                "knows about\n", term);
			return 2;
		}
		if (!strcmp(g_terminals[i], term)) break;
	}
	if (!have_cmd(term)) {
		fprintf(stderr, "syn-settings: %s is not installed\n", term);
		return 2;
	}

	char path[PATH_CAP];
	synuirc_path(path, sizeof path);

	char *text = slurp(path);
	size_t cap = (text ? strlen(text) : 0) + 512;
	char *out = malloc(cap);
	if (!out) { free(text); return 1; }
	out[0] = '\0';

	size_t n = 0;
	int replaced = 0;

	for (const char *p = text; p && *p; ) {
		const char *eol = strchr(p, '\n');
		size_t len = eol ? (size_t)(eol - p) : strlen(p);

		/* Only a real assignment, and only the first one. A commented
		 * "# terminal = foot" is documentation, and rewriting it would
		 * silently turn a comment into a setting. */
		const char *s = p;
		size_t sl = len;
		while (sl && (*s == ' ' || *s == '\t')) { s++; sl--; }

		int is_knob = 0;
		if (!replaced && sl > 8 && !strncmp(s, "terminal", 8)) {
			const char *v = s + 8;
			size_t vl = sl - 8;
			while (vl && (*v == ' ' || *v == '\t')) { v++; vl--; }
			is_knob = vl && *v == '=';
		}

		if (is_knob) {
			n += (size_t)snprintf(out + n, cap - n, "terminal = %s\n", term);
			replaced = 1;
		} else {
			if (n + len + 2 >= cap) break;
			memcpy(out + n, p, len);
			n += len;
			out[n++] = '\n';
			out[n] = '\0';
		}

		p = eol ? eol + 1 : NULL;
	}

	if (!replaced) {
		if (n && out[n - 1] != '\n') out[n++] = '\n';
		n += (size_t)snprintf(out + n, cap - n, "terminal = %s\n", term);
	}

	free(text);

	if (g_dry_run) {
		printf("would write %s:\n%s", path, out);
		printf("would run: pkill -HUP -x synui\n");
		free(out);
		return 0;
	}

	ensure_parent(path);
	backup_once(path);
	int rc = write_atomic(path, out);
	free(out);
	if (rc) return rc;

	/* -x, matching the executable NAME and not a command line: `pkill -f
	 * synui` matches the shell that invoked this and kills the caller
	 * (reference_pkill_f_matches_own_shell). A missing synui is not an
	 * error — this still works over SSH, and the file is written either
	 * way. */
	char *argv[] = { (char *)"pkill", (char *)"-HUP", (char *)"-x",
	                 (char *)"synui", NULL };
	run_quiet(argv);

	/* The GUI greys this row outside synui, off the same helper; the CLI has
	 * no such gate. Without this line a user typing it from a GNOME session
	 * gets a clean exit, a written file, and a Ctrl+Alt+T that still opens
	 * whatever GNOME opens — success reported, nothing changed. */
	if (syn_synui_only())
		printf("written — %s is the session running and launches its own "
		       "terminal; this applies under synui\n", syn_session_desktop());
	return 0;
}

int do_set_app(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr,
		        "usage: syn-settings set app <role> <application>\n"
		        "  roles:");
		for (size_t i = 0; i < g_nroles; i++)
			fprintf(stderr, " %s", g_roles[i].id);
		fprintf(stderr, "\n  the application is a .desktop NAME "
		                "(firefox.desktop), or for `terminal` a "
		                "command (syntty)\n");
		return 2;
	}

	const struct app_role *r = role_by_id(argv[0]);
	if (!r) {
		fprintf(stderr, "syn-settings: unknown role '%s'\n", argv[0]);
		return 2;
	}

	if (role_is_terminal(r))
		return set_terminal(argv[1]);

	/* The application must EXIST. Writing a default that names nothing
	 * installed produces a file that reads like a setting and behaves like no
	 * setting at all — the spec skips such entries, so the old default keeps
	 * winning and the pane keeps showing it. */
	char path[PATH_CAP + 128];
	if (!desktop_path(argv[1], path, sizeof path)) {
		fprintf(stderr, "syn-settings: no installed application called "
		                "'%s'\n", argv[1]);
		return 2;
	}

	return set_mime_default(r, argv[1]);
}
