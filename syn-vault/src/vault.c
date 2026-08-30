/* vault.c — creating, opening and closing a vault. See synvault.h.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synvault.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* ── names and paths ────────────────────────────────────────────────────── */

bool vault_name_ok(const char *name)
{
	if (!name || !*name) return false;
	if (strlen(name) > 64) return false;
	/* ⛔ ONE PATH COMPONENT, AND NOT A DOTFILE. "." and ".." are the obvious
	 * escapes; a leading dot merely hides the vault from the person who made
	 * it, which for the one directory holding their private files is its own
	 * kind of loss. */
	if (name[0] == '.') return false;
	for (const char *p = name; *p; p++) {
		if (*p == '/' || *p == '\\') return false;
		if ((unsigned char)*p < 0x20) return false;
	}
	return true;
}

static char *home(void)
{
	const char *h = getenv("SYNVAULT_HOME");
	if (h && *h) return xstrdup(h);
	h = getenv("HOME");
	if (!h || !*h) die("no HOME, so there is nowhere to keep a vault");
	return xstrdup(h);
}

char *vault_cipher_dir(const char *name)
{
	char *h = home();
	char *p = xasprintf("%s/.local/share/syn-vault/%s.vault", h, name);
	free(h);
	return p;
}

char *vault_mount_dir(const char *name)
{
	char *h = home();
	char *p = xasprintf("%s/Vaults/%s", h, name);
	free(h);
	return p;
}

/* mkdir -p, for a path this program composed. */
static bool mkpath(const char *path, mode_t mode)
{
	char *tmp = xstrdup(path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		if (mkdir(tmp, 0700) != 0 && errno != EEXIST) { free(tmp); return false; }
		*p = '/';
	}
	bool ok = (mkdir(tmp, mode) == 0 || errno == EEXIST);
	free(tmp);
	return ok;
}

static bool dir_empty(const char *path)
{
	DIR *d = opendir(path);
	if (!d) return true;               /* absent counts as empty */
	struct dirent *e;
	bool empty = true;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		empty = false;
		break;
	}
	closedir(d);
	return empty;
}

bool vault_exists(const char *name)
{
	char *c = vault_cipher_dir(name);
	/* gocryptfs.conf is the vault, not the directory — a bare directory is
	 * something else entirely and must not be reported as a locked vault. */
	char *conf = xasprintf("%s/gocryptfs.conf", c);
	struct stat st;
	bool yes = (stat(conf, &st) == 0 && S_ISREG(st.st_mode));
	free(conf);
	free(c);
	return yes;
}

/*
 * Is it mounted? Answered from /proc/self/mounts, not from whether the
 * directory has anything in it.
 *
 * ⛔ "IT HAS FILES IN IT" IS NOT THE SAME QUESTION, and the difference is the
 * dangerous case: a closed vault whose mountpoint someone dropped a file into
 * looks open by that test, so `close` would try to unmount a plain directory
 * and `status` would call unencrypted files protected.
 */
bool vault_is_open(const char *name)
{
	char *m = vault_mount_dir(name);
	char *real = realpath(m, NULL);
	const char *want = real ? real : m;

	FILE *f = fopen("/proc/self/mounts", "r");
	bool open_now = false;
	if (f) {
		char line[4096];
		while (fgets(line, sizeof line, f)) {
			/* "<what> <where> <type> ..." — the second field is the path, and
			 * it is escaped: a space is \040. Compared field-wise so a vault
			 * called "Tax 2024" is found. */
			char *sp = strchr(line, ' ');
			if (!sp) continue;
			char *where = sp + 1;
			char *end = strchr(where, ' ');
			if (!end) continue;
			*end = '\0';

			char un[4096];
			size_t w = 0;
			for (const char *p = where; *p && w < sizeof un - 1; p++) {
				if (p[0] == '\\' && p[1] == '0' && p[2] == '4' && p[3] == '0') {
					un[w++] = ' ';
					p += 3;
				} else {
					un[w++] = *p;
				}
			}
			un[w] = '\0';
			if (!strcmp(un, want)) { open_now = true; break; }
		}
		fclose(f);
	}
	free(real);
	free(m);
	return open_now;
}

/* ── running the backend ────────────────────────────────────────────────── */

/*
 * The backend's own stderr, silenced for a front end.
 *
 * ⛔ ONLY IN --rec MODE, AND ONLY FOR THE CHILD. A person at a terminal wants
 * gocryptfs's detail: "failed to unlock master key: cipher: message
 * authentication failed" above "Password incorrect." says a wrong password and
 * a corrupted vault apart, and throwing it away would be inventing a friendlier
 * message at the cost of the difference. A window has nowhere to put three
 * lines of it — it shows the text in a one-line panel under the password box —
 * so in --rec mode the child says nothing and this program supplies the one
 * sentence itself. See gocryptfs_reason().
 */
static void hush_backend(void)
{
	if (g_out != OUT_REC) return;
	int null = open("/dev/null", O_WRONLY);
	if (null < 0) return;
	dup2(null, STDERR_FILENO);
	if (null > STDERR_FILENO) close(null);
}

/*
 * gocryptfs's exit code as a sentence somebody can act on.
 *
 * ⚠ CONFIRMED AGAINST gocryptfs v2.6.1, not read off a table: 12 wrong
 * password, 11 unreadable gocryptfs.conf, 10 mountpoint not empty, 6 missing
 * or unusable cipherdir. An unknown code returns NULL rather than a guess —
 * the caller then says which code it was, which is at least true.
 */
static const char *gocryptfs_reason(int rc)
{
	switch (rc) {
	case 12:  return "That password is not right.";
	case 11:  return "This vault's settings file is missing or unreadable.";
	case 10:  return "There are already files where this vault opens, so it was left shut.";
	case 6:   return "This vault's encrypted files are missing.";
	case 127: return "gocryptfs is not installed.";
	default:  return NULL;
	}
}

/*
 * Run argv with `pw` on its stdin, and wait.
 *
 * ⛔ THE PASSWORD GOES DOWN A PIPE, NEVER IN argv. See password_read().
 * ⚠ AND THE WRITE END IS CLOSED IN THE PARENT BEFORE THE WAIT. gocryptfs reads
 * until EOF; a parent still holding the write end waits for a child that is
 * waiting for the parent, and the whole program stops with no output at all.
 */
static int run_with_password(char *const argv[], const char *pw)
{
	int fds[2];
	if (pipe(fds) != 0) { warn("pipe: %s", strerror(errno)); return -1; }

	pid_t pid = fork();
	if (pid < 0) { warn("fork: %s", strerror(errno)); close(fds[0]); close(fds[1]); return -1; }

	if (pid == 0) {
		close(fds[1]);
		dup2(fds[0], STDIN_FILENO);
		if (fds[0] > STDIN_FILENO) close(fds[0]);
		hush_backend();
		execvp(argv[0], argv);
		_exit(127);
	}

	close(fds[0]);
	if (pw) {
		size_t len = strlen(pw);
		ssize_t w = write(fds[1], pw, len);
		if (w > 0) w = write(fds[1], "\n", 1);
		(void)w;
	}
	close(fds[1]);

	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) { }
	if (WIFEXITED(st)) return WEXITSTATUS(st);
	return -1;
}

static int run_plain(char *const argv[])
{
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) { hush_backend(); execvp(argv[0], argv); _exit(127); }
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) { }
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static void wipe(char *s)
{
	if (!s) return;
	memset(s, 0, strlen(s));
	free(s);
}

static bool have(const char *cmd)
{
	const char *path = getenv("PATH");
	if (!path) return false;
	char *copy = xstrdup(path);
	bool found = false;
	for (char *tok = strtok(copy, ":"); tok && !found; tok = strtok(NULL, ":")) {
		char *full = xasprintf("%s/%s", tok, cmd);
		found = (access(full, X_OK) == 0);
		free(full);
	}
	free(copy);
	return found;
}

static bool backend_ready(void)
{
	if (!have(SYNVAULT_GOCRYPTFS)) {
		warn("gocryptfs is not installed, and it is what does the encrypting.\n"
		     "  synpkg install gocryptfs");
		return false;
	}
	return true;
}

/* ── the commands ───────────────────────────────────────────────────────── */

int cmd_create(const char *name)
{
	if (!vault_name_ok(name)) { warn("that is not a usable vault name"); return 2; }
	if (!backend_ready()) return 1;

	if (vault_exists(name)) {
		warn("'%s' already exists — syn-vault open %s", name, name);
		return 1;
	}

	char *cipher = vault_cipher_dir(name);
	char *mount = vault_mount_dir(name);

	/* ⛔ 0700 ON BOTH. The ciphertext does not need protecting by permissions —
	 * that is what the encryption is for — but the mountpoint holds the
	 * PLAINTEXT while the vault is open, and a 0755 mountpoint would publish it
	 * to every other account on the machine for exactly as long as it is
	 * useful. */
	if (!mkpath(cipher, 0700) || !mkpath(mount, 0700)) {
		warn("could not make the vault's directories: %s", strerror(errno));
		free(cipher); free(mount);
		return 1;
	}

	char *pw = password_read("A password for this vault: ");
	if (!pw || !*pw) {
		warn("no password, so nothing was created");
		wipe(pw); free(cipher); free(mount);
		return 1;
	}

	/* ⚠ ASKED TWICE, when there is somebody to ask. A vault whose password was
	 * mistyped at creation cannot be opened and cannot be recovered — there is
	 * no other copy of the key. Piped input is the window, which does its own
	 * confirming. */
	if (isatty(STDIN_FILENO)) {
		char *again = password_read("And again: ");
		bool same = again && !strcmp(pw, again);
		wipe(again);
		if (!same) {
			warn("those did not match, so nothing was created");
			wipe(pw); free(cipher); free(mount);
			return 1;
		}
	}

	char *argv[] = { (char *)SYNVAULT_GOCRYPTFS, (char *)"-init", (char *)"-q",
	                 cipher, NULL };
	int rc = run_with_password(argv, pw);
	wipe(pw);

	if (rc != 0) {
		const char *why = gocryptfs_reason(rc);
		if (g_out == OUT_REC && why)
			warn("%s", why);
		else
			warn("gocryptfs could not create the vault (exit %d)", rc);
		free(cipher); free(mount);
		return 1;
	}

	if (g_out == OUT_REC) {
		rec_header("name\tcipher\tmount");
		char *n = pct_encode(name), *c = pct_encode(cipher), *m = pct_encode(mount);
		rec_row("%s\t%s\t%s", n, c, m);
		free(n); free(c); free(m);
	} else {
		printf("Made the vault '%s'.\n\n"
		       "  syn-vault open %s   unlocks it at %s\n"
		       "  syn-vault close %s  locks it again\n\n"
		       "⚠ There is no way back in without the password. Nothing else has a copy.\n",
		       name, name, mount, name);
	}

	free(cipher); free(mount);
	return 0;
}

int cmd_open(const char *name)
{
	if (!vault_name_ok(name)) { warn("that is not a usable vault name"); return 2; }
	if (!backend_ready()) return 1;

	if (!vault_exists(name)) {
		warn("there is no vault called '%s' — syn-vault create %s", name, name);
		return 1;
	}
	if (vault_is_open(name)) {
		if (g_out != OUT_REC) printf("'%s' is already open.\n", name);
		return 0;
	}

	char *cipher = vault_cipher_dir(name);
	char *mount = vault_mount_dir(name);

	if (!mkpath(mount, 0700)) {
		warn("could not make %s: %s", mount, strerror(errno));
		free(cipher); free(mount);
		return 1;
	}

	/* ⛔ REFUSED OVER A NON-EMPTY MOUNTPOINT. Mounting on top of files does not
	 * delete them, it HIDES them — so anything saved into the mountpoint while
	 * the vault was closed sits unencrypted on the disk, invisible, while the
	 * person believes it is inside the vault. Better to refuse and say where
	 * the files are. */
	if (!dir_empty(mount)) {
		warn("%s already has files in it, and they are NOT in the vault — they were\n"
		     "  saved while it was closed, so they are on the ordinary disk. Move them\n"
		     "  aside, open the vault, then move them in.", mount);
		free(cipher); free(mount);
		return 1;
	}

	char *pw = password_read("Password: ");
	if (!pw || !*pw) {
		warn("no password given");
		wipe(pw); free(cipher); free(mount);
		return 1;
	}

	char *argv[] = { (char *)SYNVAULT_GOCRYPTFS, (char *)"-q", cipher, mount, NULL };
	int rc = run_with_password(argv, pw);
	wipe(pw);

	if (rc != 0) {
		/* At a terminal, gocryptfs has already said why on stderr and that
		 * detail is worth more than a friendlier sentence — so this only adds
		 * which vault and which code. A front end saw none of it (hush_backend)
		 * and has one line to fill, so it gets the sentence instead. */
		const char *why = gocryptfs_reason(rc);
		if (g_out == OUT_REC && why)
			warn("%s", why);
		else
			warn("could not open '%s' (exit %d)", name, rc);
		free(cipher); free(mount);
		return 1;
	}

	if (g_out != OUT_REC) printf("'%s' is open at %s\n", name, mount);
	free(cipher); free(mount);
	return 0;
}

int cmd_close(const char *name)
{
	if (!vault_name_ok(name)) { warn("that is not a usable vault name"); return 2; }

	if (!vault_is_open(name)) {
		if (g_out != OUT_REC) printf("'%s' is not open.\n", name);
		return 0;
	}

	char *mount = vault_mount_dir(name);
	char *argv[] = { (char *)SYNVAULT_FUSERMOUNT, (char *)"-u", mount, NULL };
	int rc = run_plain(argv);

	if (rc != 0) {
		/* ⚠ THE USUAL CAUSE IS A PROGRAM STILL IN THERE, and saying so is more
		 * use than the exit status: a file manager sitting in the folder, or a
		 * terminal whose working directory is inside it, holds the mount. */
		warn("could not close '%s' — something is still using it.\n"
		     "  Close anything open in %s and try again.", name, mount);
		free(mount);
		return 1;
	}

	if (g_out != OUT_REC) printf("'%s' is locked.\n", name);
	free(mount);
	return 0;
}

int cmd_status(const char *name)
{
	if (!vault_name_ok(name)) { warn("that is not a usable vault name"); return 2; }

	bool exists = vault_exists(name);
	bool open_now = exists && vault_is_open(name);
	char *mount = vault_mount_dir(name);
	bool stray = !open_now && !dir_empty(mount);

	if (g_out == OUT_REC) {
		char *n = pct_encode(name), *m = pct_encode(mount);
		rec_header("name\tstate\tmount\tstray");
		rec_row("%s\t%s\t%s\t%d", n,
		        !exists ? "none" : (open_now ? "open" : "locked"), m, stray ? 1 : 0);
		free(n); free(m);
	} else if (!exists) {
		printf("There is no vault called '%s'.\n", name);
	} else {
		printf("'%s' is %s.\n", name, open_now ? "open" : "locked");
		if (stray)
			printf("\n⚠ %s has files in it while the vault is closed.\n"
			       "  They are NOT encrypted — they are on the ordinary disk.\n", mount);
	}

	free(mount);
	return exists ? 0 : 1;
}

int cmd_list(void)
{
	char *h = home();
	char *root = xasprintf("%s/.local/share/syn-vault", h);
	free(h);

	DIR *d = opendir(root);
	if (g_out == OUT_REC) rec_header("name\tstate\tmount\tstray");

	int n = 0;
	if (d) {
		struct dirent *e;
		while ((e = readdir(d))) {
			const char *dn = e->d_name;
			size_t len = strlen(dn);
			if (len <= 6 || strcmp(dn + len - 6, ".vault") != 0) continue;

			char *name = xstrdup(dn);
			name[len - 6] = '\0';
			if (!vault_name_ok(name) || !vault_exists(name)) { free(name); continue; }

			bool open_now = vault_is_open(name);
			char *mount = vault_mount_dir(name);
			bool stray = !open_now && !dir_empty(mount);

			if (g_out == OUT_REC) {
				char *pn = pct_encode(name), *pm = pct_encode(mount);
				rec_row("%s\t%s\t%s\t%d", pn, open_now ? "open" : "locked", pm,
				        stray ? 1 : 0);
				free(pn); free(pm);
			} else {
				printf("  [%s] %-24s %s%s\n", open_now ? "open  " : "locked",
				       name, mount, stray ? "   ⚠ unencrypted files here" : "");
			}
			free(mount);
			free(name);
			n++;
		}
		closedir(d);
	}

	if (g_out != OUT_REC && n == 0)
		printf("No vaults yet.\n\n  syn-vault create <name>\n");

	free(root);
	return 0;
}
