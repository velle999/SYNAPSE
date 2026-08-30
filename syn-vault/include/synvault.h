/* synvault.h — a password-locked folder, in userspace.
 *
 * ── What this is, and what it deliberately is not ────────────────────────────
 *
 * ⛔ THE CRYPTOGRAPHY IS NOT HERE AND MUST NEVER BE. This program creates
 * directories, asks for a password, and runs gocryptfs. It does not encrypt
 * anything itself, does not derive a key, does not choose a cipher. Every one
 * of those is a decision with a decade of published attacks behind it, and a
 * file locker that gets one subtly wrong looks identical to one that does not
 * — until somebody else reads the files.
 *
 * gocryptfs was chosen over the alternatives for one reason above the others:
 * it is genuinely USERSPACE. A LUKS container needs root to attach a loop
 * device and mount it, so a "vault" built on one is a polkit prompt away from
 * every open and close, and on a machine where polkit is unhappy it is nothing
 * at all. fscrypt needs the filesystem to have been made for it. This needs
 * FUSE and the user's own permissions, which is what "a vault for userspace
 * files" actually means.
 *
 * ── Where things live ────────────────────────────────────────────────────────
 *
 *   ~/.local/share/syn-vault/<name>.vault/   the ciphertext. Always present.
 *   ~/Vaults/<name>/                          the plaintext, ONLY while open.
 *
 * ⚠ THE TWO ARE NOT NESTED, deliberately. A mountpoint inside the directory
 * being encrypted is a loop, and a ciphertext directory inside the mountpoint
 * disappears from the user's view the moment it is mounted — which is how
 * somebody deletes the only copy of their data while tidying up.
 *
 * ⛔ AND THE MOUNTPOINT IS EMPTY WHEN CLOSED. It is a bare directory, so a file
 * saved into ~/Vaults/<name>/ while the vault is CLOSED lands on the ordinary
 * unencrypted disk and stays there, looking exactly like it is in the vault.
 * `syn-vault open` refuses to mount over a non-empty mountpoint for that
 * reason, and `status` reports it, because it is the one way to believe a file
 * is protected when it is not.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNVAULT_H
#define SYNVAULT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* The backend. A build may point this at a stub for testing; nothing else. */
#ifndef SYNVAULT_GOCRYPTFS
#define SYNVAULT_GOCRYPTFS "gocryptfs"
#endif
#ifndef SYNVAULT_FUSERMOUNT
#define SYNVAULT_FUSERMOUNT "fusermount"
#endif

/* ── output ─────────────────────────────────────────────────────────────── */

typedef enum { OUT_HUMAN, OUT_REC } out_mode_t;
extern out_mode_t g_out;

void warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void die(const char *fmt, ...)  __attribute__((format(printf, 1, 2), noreturn));
void rec_header(const char *fields);
void rec_row(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

char *xstrdup(const char *s);
char *xasprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *pct_encode(const char *s);

/* ── vaults ─────────────────────────────────────────────────────────────── */

/* ⛔ A NAME IS ONE PATH COMPONENT. It becomes a directory under two roots, so
 * "../" in it would put a vault anywhere on the disk — and the name is the one
 * thing a caller of this program controls. */
bool vault_name_ok(const char *name);

char *vault_cipher_dir(const char *name);   /* caller frees */
char *vault_mount_dir(const char *name);    /* caller frees */

bool vault_exists(const char *name);
bool vault_is_open(const char *name);

/* Reads a password without echoing it, or from stdin when stdin is not a
 * terminal — which is how the window passes one in. Caller frees; the buffer is
 * wiped by the caller after use. */
char *password_read(const char *prompt);

int cmd_create(const char *name);
int cmd_open(const char *name);
int cmd_close(const char *name);
int cmd_list(void);
int cmd_status(const char *name);
int cmd_gui(const char *name);

#endif /* SYNVAULT_H */
