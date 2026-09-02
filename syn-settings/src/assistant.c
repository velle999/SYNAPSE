/*
 * assistant.c — which service the assistant talks to, and its API key.
 *
 * ⛔ THIS WAS CLI-ONLY. `vibe provider anthropic` and `vibe key anthropic` have
 * worked for a while; there was no way to reach either without a terminal, so
 * "use Claude instead of the local model" was a feature only its author knew
 * about. velle, 2026-08-28: "add gui setup for cloud backends".
 *
 * ⚠ IT DRIVES vibe, IT DOES NOT REIMPLEMENT IT. The provider lives in the
 * launcher's env file and the key lives in the keyring — or a 0600 file, or an
 * environment variable, and `vibe key` is what knows which is answering. A
 * second reader here would be a second opinion about where a credential is,
 * which is the worst possible thing to have two of. Every row below is vibe's
 * own answer, parsed back.
 *
 * ── The key never travels in argv ───────────────────────────────────────────
 *
 * ⛔ /proc/<pid>/cmdline IS WORLD-READABLE. `syn-settings set key sk-ant-…`
 * would put a live API key where every account on the machine can read it for
 * as long as the process runs, and into any `ps` a user happens to have open.
 * /proc/<pid>/environ is 0400 and owner-only, so the secret crosses this
 * boundary in the environment instead, and is unset the moment it is read.
 *
 * ⚠ AND vibe TAKES IT ON STDIN, which is better still and is what this uses —
 * `vibe key <provider> -`. So the key is in this process's environment briefly
 * and in a pipe, and is never a command-line argument at any point in the
 * chain.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "synsettings.h"
#include "i18n.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * ⚠ THE LIST IS vibe's, AND IT IS CHECKED AGAINST vibe's. These are the five
 * `vibe provider` accepts; the two marked cloud are the two `vibe key` accepts.
 * A name here that vibe does not know would draw a button that fails on click,
 * which is the settings-app version of a dead link.
 */
static const struct { const char *id, *label, *blurb; bool cloud; } BACKENDS[] = {
	{ "synapd",    "SynapseOS (local)",
	  "the model on this machine, through synapd — private, and no account", false },
	{ "ollama",    "Ollama (local)",
	  "a local ollama server, if you run one", false },
	{ "llama_cpp", "llama.cpp (local)",
	  "a GGUF loaded in-process; needs python-llama-cpp", false },
	{ "anthropic", "Claude (Anthropic)",
	  "Claude over the official SDK — needs an API key, and every request "
	  "leaves this machine", true },
	{ "openai",    "OpenAI",
	  "GPT over the chat-completions API — needs an API key, and every "
	  "request leaves this machine", true },
};
#define NBACKENDS ((int)(sizeof(BACKENDS) / sizeof(*BACKENDS)))

static const char *backend_label(const char *id)
{
	for (int i = 0; i < NBACKENDS; i++)
		if (!strcmp(BACKENDS[i].id, id)) return BACKENDS[i].label;
	return id;
}

/* `vibe provider` with no argument prints "backend: <name>   (choices: …)". */
static void current_backend(char *out, size_t cap)
{
	out[0] = '\0';
	if (!have_cmd("vibe")) return;

	/* ⚠ QUIET. vibe warns "Input is not a terminal" on stderr whenever it is
	 * run without one, which is always from here; inherited, that lands in the
	 * settings window's own log three times per reload. */
	char buf[512] = "";
	char *argv[] = { (char *)"vibe", (char *)"provider", NULL };
	run_capture_quiet(argv, buf, sizeof buf);

	const char *p = strstr(buf, "backend:");
	if (!p) return;
	p += 8;
	while (*p == ' ' || *p == '\t') p++;
	size_t n = strcspn(p, " \t\r\n");
	if (n >= cap) n = cap - 1;
	memcpy(out, p, n);
	out[n] = '\0';
}

/*
 * Where one provider's key is, in vibe's own word: keyring, file, environment
 * or "not set". `vibe key` with no argument prints a line per provider.
 *
 * ⚠ THE WORD, NOT THE KEY. Nothing in this file ever reads a key's VALUE, and
 * there is no code path that could: a settings table that can print a secret
 * is one screenshot away from leaking one.
 */
static void key_where(const char *buf, const char *prov, char *out, size_t cap)
{
	snprintf(out, cap, "unknown");

	char want[64];
	snprintf(want, sizeof want, "%s:", prov);
	const char *p = buf ? strstr(buf, want) : NULL;
	if (!p) return;
	p += strlen(want);
	while (*p == ' ' || *p == '\t') p++;
	/* Up to the end of the line, but stop before the "(path)" note — the
	 * path of a key file is not a secret, but it is not the answer either. */
	size_t n = strcspn(p, "(\r\n");
	while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
	if (n >= cap) n = cap - 1;
	memcpy(out, p, n);
	out[n] = '\0';
}

int pane_assistant(void)
{
	rec_header("kind\tkey\tvalue\tstate\tdetail\taction");

	if (!have_cmd("vibe")) {
		rec_row("assistant\t%s\tnot installed\tbad\t%s\t-",
		        N_("vibe"),
		        N_("the assistant is not on this machine, so there is no backend to choose"));
		return 0;
	}

	char now[64] = "";
	current_backend(now, sizeof now);

	rec_row("assistant\t%s\t%s\t%s\t%s\tchoice:assistant-backend",
	        N_("backend"), now[0] ? backend_label(now) : "unknown", now[0] ? "ok" : "-",
	        N_("which service the assistant sends your messages to"));

	/*
	 * ⚠ THE KEY ROWS ARE ALWAYS DRAWN, not only for the selected backend.
	 * Setting a key and choosing a backend are two steps in either order, and
	 * a row that appears only once you have already switched is a row you
	 * cannot use to prepare the switch — you would have to move the assistant
	 * onto a provider it cannot reach yet to be offered the field.
	 */
	/*
	 * ⚠ ASKED ONCE, NOT ONCE PER PROVIDER. `vibe key` prints a line for every
	 * provider it knows, so calling it inside the loop below forked a python
	 * interpreter per row — for a pane that redraws on every reload, to answer
	 * a question one invocation already answered in full.
	 */
	char keys[1024] = "";
	char *kargv[] = { (char *)"vibe", (char *)"key", NULL };
	run_capture_quiet(kargv, keys, sizeof keys);

	for (int i = 0; i < NBACKENDS; i++) {
		if (!BACKENDS[i].cloud) continue;

		char w[128];
		key_where(keys, BACKENDS[i].id, w, sizeof w);
		bool set = strcmp(w, "not set") != 0 && strcmp(w, "unknown") != 0;
		bool live = !strcmp(now, BACKENDS[i].id);

		rec_row("key\t%s API key\t%s\t%s\t%s\tsecret:%s",
		        BACKENDS[i].label, w,
		        set ? "ok" : (live ? "bad" : "warn"),
		        set ? "stored — click to replace it, or clear the box to "
		              "remove it"
		            : (live ? "the assistant is set to this backend and has no "
		                      "key, so every request will fail"
		                    : N_("no key stored; set one before switching to this "
		                      "backend")),
		        BACKENDS[i].id);
	}

	return 0;
}

/* ── choices / set, for the generic picker ──────────────────────────────── */

int assistant_choices(void)
{
	char now[64] = "";
	current_backend(now, sizeof now);

	/* id \t label \t current|- — the shape `choices` promises everywhere. */
	for (int i = 0; i < NBACKENDS; i++)
		printf("%s\t%s\t%s\n", BACKENDS[i].id, BACKENDS[i].label,
		       !strcmp(BACKENDS[i].id, now) ? "current" : "-");
	return 0;
}

int assistant_set_backend(const char *id)
{
	if (!id || !*id) {
		fprintf(stderr, "syn-settings: assistant-backend: which backend?\n");
		return 2;
	}
	bool known = false;
	for (int i = 0; i < NBACKENDS; i++)
		if (!strcmp(BACKENDS[i].id, id)) { known = true; break; }
	if (!known) {
		fprintf(stderr, "syn-settings: assistant-backend: '%s' is not one vibe "
		                "knows\n", id);
		return 2;
	}
	if (!have_cmd("vibe")) {
		fprintf(stderr, "syn-settings: assistant-backend: vibe is not "
		                "installed\n");
		return 1;
	}
	char *argv[] = { (char *)"vibe", (char *)"provider", (char *)id, NULL };
	return run_progress(argv) == 0 ? 0 : 1;
}

/* ── the key ────────────────────────────────────────────────────────────── */

/*
 * Hand `secret` to `vibe key <prov> -` down a pipe.
 *
 * ⚠ SIGPIPE IS IGNORED ACROSS THE WRITE. If vibe exits early — a provider it
 * refuses, a keyring that is not there — the write lands on a closed pipe and
 * the DEFAULT disposition kills this process outright, so the settings window
 * would see its helper die with no message rather than the error vibe printed.
 */
static int pipe_key(const char *prov, const char *secret)
{
	int fds[2];
	if (pipe(fds) != 0) return 1;

	void (*prev)(int) = signal(SIGPIPE, SIG_IGN);

	pid_t pid = fork();
	if (pid < 0) { close(fds[0]); close(fds[1]); signal(SIGPIPE, prev); return 1; }

	if (pid == 0) {
		close(fds[1]);
		dup2(fds[0], STDIN_FILENO);
		if (fds[0] > STDIN_FILENO) close(fds[0]);
		execlp("vibe", "vibe", "key", prov, "-", (char *)NULL);
		_exit(127);
	}

	close(fds[0]);
	size_t len = strlen(secret);
	const char *p = secret;
	while (len) {
		ssize_t n = write(fds[1], p, len);
		if (n <= 0) break;
		p += n; len -= (size_t)n;
	}
	if (write(fds[1], "\n", 1) < 0) { /* vibe already gone; its status tells us */ }
	close(fds[1]);

	int st = 0;
	waitpid(pid, &st, 0);
	signal(SIGPIPE, prev);
	return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : 1;
}

/*
 * `syn-settings assistant-key <provider>` — the key comes in the ENVIRONMENT.
 *
 * ⛔ NOT IN argv, for the reason in this file's header: /proc/<pid>/cmdline is
 * world-readable and /proc/<pid>/environ is not. An EMPTY or absent value means
 * "remove it", which is what an emptied box in the window sends.
 */
int assistant_key(const char *prov)
{
	if (!prov || !*prov) {
		fprintf(stderr, "syn-settings: assistant-key: which provider?\n");
		return 2;
	}
	bool cloud = false;
	for (int i = 0; i < NBACKENDS; i++)
		if (!strcmp(BACKENDS[i].id, prov) && BACKENDS[i].cloud) cloud = true;
	if (!cloud) {
		fprintf(stderr, "syn-settings: assistant-key: '%s' takes no API key\n",
		        prov);
		return 2;
	}
	if (!have_cmd("vibe")) {
		fprintf(stderr, "syn-settings: assistant-key: vibe is not installed\n");
		return 1;
	}

	const char *secret = getenv("SYN_SETTINGS_SECRET");
	/* Read once and dropped immediately: nothing below this line needs it in
	 * the environment, and a child spawned later would inherit it. */
	char *copy = secret ? strdup(secret) : NULL;
	unsetenv("SYN_SETTINGS_SECRET");

	int rc;
	if (!copy || !*copy) {
		char *argv[] = { (char *)"vibe", (char *)"key", (char *)prov,
		                 (char *)"--forget", NULL };
		rc = run_progress(argv) == 0 ? 0 : 1;
	} else {
		rc = pipe_key(prov, copy);
	}

	if (copy) {
		/* Overwritten before it is freed. Not a guarantee against a determined
		 * reader of this process's memory — it is one instruction of hygiene
		 * for a string that was a live credential a moment ago. */
		memset(copy, 0, strlen(copy));
		free(copy);
	}
	return rc;
}
