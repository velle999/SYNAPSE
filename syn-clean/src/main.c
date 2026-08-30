/* main.c — syn-clean's command line, and the window it can open.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "config.h"
#include "synclean.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef SYNCLEAN_DATADIR
#define SYNCLEAN_DATADIR "/usr/share/syn-clean"
#endif

static void usage(FILE *f)
{
	fprintf(f,
"syn-clean — free disk space, and destroy files you mean to destroy\n"
"\n"
"  syn-clean scan [what...]       what there is, and how big\n"
"  syn-clean clean <what...>      remove it\n"
"  syn-clean clean --all          every category that does not need root\n"
"  syn-clean shred <path...>      overwrite and delete, files or folders\n"
"  syn-clean gui                  the window\n"
"  syn-clean list                 the category names\n"
"\n"
"  --dry-run    say what would go, remove nothing\n"
"  --yes        do not ask\n"
"  --passes N   overwrites before deleting (shred, default 3)\n"
"  --rec        one record per line, for a front end\n"
"  --version    print the version\n"
"\n"
"⚠ SHRED CANNOT PROMISE WHAT ITS NAME SUGGESTS on this machine. Overwriting a\n"
"file only destroys the old bytes if they are rewritten in place, and btrfs —\n"
"what SynapseOS installs on — does not do that: it writes elsewhere and leaves\n"
"the original blocks until they are reused. Snapshots keep whole copies, and an\n"
"SSD's controller remaps blocks out of any program's reach. syn-clean shred\n"
"says which of these apply before it starts. Full-disk encryption is the thing\n"
"that actually makes a deleted file unreadable.\n");
}

static bool confirm(const char *what)
{
	if (g_yes || g_dry) return true;
	/* ⚠ A front end pipes; a pipe has nobody to answer, so --rec without --yes
	 * is refused rather than silently taken as consent. */
	if (!isatty(STDIN_FILENO)) {
		warn("nothing to ask on a pipe — pass --yes if you mean it");
		return false;
	}
	fprintf(stderr, "%s [y/N] ", what);
	fflush(stderr);
	char buf[16];
	if (!fgets(buf, sizeof buf, stdin)) return false;
	return buf[0] == 'y' || buf[0] == 'Y';
}

int cmd_scan(int nsel, char **sel)
{
	unsigned long long total = 0;
	rec_header("id\tlabel\twhat\tbytes\tfiles\troot\tlogins");

	for (size_t i = 0; i < g_ncategories; i++) {
		const category_t *c = &g_categories[i];
		if (nsel > 0) {
			bool want = false;
			for (int j = 0; j < nsel; j++)
				if (!strcmp(sel[j], c->id)) want = true;
			if (!want) continue;
		}
		unsigned long long b = 0, n = 0;
		category_measure(c, &b, &n);
		total += b;

		if (g_out == OUT_REC) {
			char *id = pct_encode(c->id), *la = pct_encode(c->label),
			     *wh = pct_encode(c->what);
			rec_row("%s\t%s\t%s\t%llu\t%llu\t%d\t%d", id, la, wh, b, n,
			        c->needs_root ? 1 : 0, c->loses_logins ? 1 : 0);
			free(id); free(la); free(wh);
		} else {
			char h[32];
			human_size(b, h, sizeof h);
			printf("  %-14s %10s  %6llu  %s%s\n", c->id, h, n, c->what,
			       c->needs_root ? "  (needs root)" : "");
		}
	}

	if (g_out != OUT_REC) {
		char h[32];
		human_size(total, h, sizeof h);
		printf("\n  %s could be freed.\n", h);
	}
	return 0;
}

int cmd_clean(int nsel, char **sel)
{
	bool all = false;
	for (int i = 0; i < nsel; i++)
		if (!strcmp(sel[i], "--all") || !strcmp(sel[i], "all")) all = true;

	/* ⛔ --all IS NOT "EVERY CATEGORY". Cookies sign the user out of every site
	 * they use, which is not a side effect of tidying up — it is the sort of
	 * thing somebody must choose by name. A sweep that included it would be a
	 * cleaner people learn not to run. */
	if (all) {
		if (!confirm("Clean every cache, thumbnail and trashed file?"))
			return 1;
	} else if (nsel > 0) {
		/* ⛔ AND A NAMED CATEGORY ASKS TOO. Naming one says which files, not
		 * that they should go — `syn-clean clean usercache` in a script, in a
		 * paste, or after a typo is still a directory tree that does not come
		 * back. The window has already asked by the time it gets here, which is
		 * exactly what its --yes means. */
		char q[256];
		snprintf(q, sizeof q, "Remove %d categor%s of files?",
		         nsel, nsel == 1 ? "y" : "ies");
		if (!confirm(q)) return 1;
	}

	unsigned long long total = 0;
	int rc = 0;
	for (size_t i = 0; i < g_ncategories; i++) {
		const category_t *c = &g_categories[i];
		bool want = false;
		if (all) {
			want = !c->needs_root && !c->loses_logins;
		} else {
			for (int j = 0; j < nsel; j++)
				if (!strcmp(sel[j], c->id)) want = true;
		}
		if (!want) continue;

		/* Asked a second time, and worth it: the question above said "how
		 * many categories", which is not the same as "you are about to be
		 * signed out of every site you use". */
		if (c->loses_logins) {
			if (!confirm("Deleting cookies signs you out everywhere. Go on?"))
				continue;
		}

		unsigned long long freed = 0;
		if (category_clean(c, &freed) != 0) rc = 1;
		total += freed;
		if (g_out == OUT_REC) {
			char *id = pct_encode(c->id);
			rec_row("%s\t%llu", id, freed);
			free(id);
		}
	}

	if (g_out != OUT_REC) {
		char h[32];
		human_size(total, h, sizeof h);
		printf("%s %s.\n", g_dry ? "Would free" : "Freed", h);
	}
	return rc;
}

int cmd_shred(int npaths, char **paths, int passes)
{
	if (npaths < 1) { warn("shred needs a path"); return 2; }

	/* ⛔ THE GROUND IS REPORTED BEFORE ANYTHING IS TOUCHED, and it is reported
	 * from the FIRST path's own filesystem rather than from /: somebody
	 * shredding a file on a USB stick is on a different filesystem from the one
	 * this program is installed on, and the answer that matters is the file's. */
	shred_ground_t g;
	shred_ground(paths[0], &g);

	if (g_out == OUT_REC) {
		rec_header("fstype\tcow\tsnapshots");
		rec_row("%s\t%d\t%d", g.fstype, g.cow ? 1 : 0, g.snapshots ? 1 : 0);
	} else if (g.cow) {
		warn("%s is copy-on-write: overwriting does NOT replace the old blocks.", g.fstype);
		if (g.snapshots)
			warn("  and /.snapshots holds read-only copies this cannot reach.");
		warn("  The file will be gone. Recovering its contents from the raw disk");
		warn("  may still be possible. Full-disk encryption is the real answer.");
	}

	if (!g_yes && !g_dry) {
		char q[512];
		snprintf(q, sizeof q, "Destroy %d item(s)? This cannot be undone.", npaths);
		if (!confirm(q)) return 1;
	}

	unsigned long long bytes = 0;
	int rc = 0;
	for (int i = 0; i < npaths; i++)
		if (shred_path(paths[i], passes, &bytes) != 0) rc = 1;

	if (g_out != OUT_REC) {
		char h[32];
		human_size(bytes, h, sizeof h);
		if (g_dry) printf("Would overwrite and delete %s.\n", h);
		else if (g.cow)
			printf("Deleted %s, overwritten %d time(s) — see the warning above.\n",
			       h, passes);
		else
			printf("Overwritten %d time(s) and deleted: %s.\n", passes, h);
	}
	return rc;
}

static int cmd_list(void)
{
	rec_header("id\tlabel\troot");
	for (size_t i = 0; i < g_ncategories; i++) {
		const category_t *c = &g_categories[i];
		if (g_out == OUT_REC) {
			char *id = pct_encode(c->id), *la = pct_encode(c->label);
			rec_row("%s\t%s\t%d", id, la, c->needs_root ? 1 : 0);
			free(id); free(la);
		} else {
			printf("  %-14s %s\n", c->id, c->label);
		}
	}
	return 0;
}

int cmd_gui(void)
{
	if (!getenv("WAYLAND_DISPLAY") && !getenv("DISPLAY"))
		die("no display — syn-clean gui needs a graphical session");
	if (access("/usr/bin/quickshell", X_OK) != 0 &&
	    access("/usr/local/bin/quickshell", X_OK) != 0)
		die("quickshell is not installed — synpkg install quickshell");

	/* The window's own Wayland identity, so the dock resolves its .desktop and
	 * it does not inherit the app_id of whatever launched it. */
	setenv("QS_APP_ID", "syn-clean", 1);

	const char *qml = SYNCLEAN_DATADIR "/syn-clean.qml";
	if (access(qml, R_OK) != 0 && access("data/syn-clean.qml", R_OK) == 0)
		qml = "data/syn-clean.qml";

	char *child[] = { (char *)"quickshell", (char *)"-p", (char *)qml, NULL };
	execvp(child[0], child);
	die("could not start quickshell");
	return 1;
}

int main(int argc, char **argv)
{
	char *pos[64];
	int n = 0, passes = 3;

	for (int i = 1; i < argc; i++) {
		char *v = argv[i];
		if (!strcmp(v, "--rec"))      { g_out = OUT_REC; continue; }
		if (!strcmp(v, "--dry-run"))  { g_dry = true;    continue; }
		if (!strcmp(v, "--yes") || !strcmp(v, "-y")) { g_yes = true; continue; }
		if (!strncmp(v, "--passes=", 9)) { passes = atoi(v + 9); continue; }
		if (!strcmp(v, "--passes") && i + 1 < argc) { passes = atoi(argv[++i]); continue; }
		if (!strcmp(v, "--all"))      { if (n < 64) pos[n++] = v; continue; }
		if (!strcmp(v, "--help") || !strcmp(v, "-h")) { usage(stdout); return 0; }
		if (!strcmp(v, "--version")) { printf("syn-clean %s\n", SYNCLEAN_VERSION); return 0; }
		if (v[0] == '-' && v[1] == '-') {
			warn("unknown option '%s'", v);
			usage(stderr);
			return 2;
		}
		if (n < 64) pos[n++] = v;
	}

	if (passes < 1) passes = 1;
	if (passes > 35) passes = 35;   /* beyond this is superstition, not security */

	if (n == 0) { usage(stdout); return 0; }

	const char *c = pos[0];
	if (!strcmp(c, "scan"))  return cmd_scan(n - 1, pos + 1);
	if (!strcmp(c, "clean")) return cmd_clean(n - 1, pos + 1);
	if (!strcmp(c, "shred")) return cmd_shred(n - 1, pos + 1, passes);
	if (!strcmp(c, "list"))  return cmd_list();
	if (!strcmp(c, "gui"))   return cmd_gui();

	warn("unknown command '%s'", c);
	usage(stderr);
	return 2;
}
