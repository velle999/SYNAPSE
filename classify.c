/*
 * classify.c — Input line classifier
 *
 * Decides whether a line of user input is:
 *   INPUT_SHELL   — a POSIX shell command
 *   INPUT_BUILTIN — a synsh built-in
 *   INPUT_AI      — natural language
 *   INPUT_HYBRID  — ambiguous (try shell, fall back to AI)
 *
 * Classification heuristics:
 *
 * 1. Explicit prefixes (highest priority):
 *    '?' prefix  → always AI
 *    '!' prefix  → always shell (force shell mode)
 *
 * 2. Built-in keyword check (syn, cd, exit, etc.)
 *
 * 3. Known shell patterns:
 *    - starts with known command (ls, git, make, ...)
 *    - contains shell operators (|, >, <, &, ;, $, `)
 *    - starts with ./ or / (path execution)
 *    - starts with a quoted string
 *
 * 4. Natural language heuristics:
 *    - multiple lowercase words without shell operators
 *    - contains modal verbs (show, list, find, delete, ...)
 *    - question words (what, how, why, where, when, ...)
 *    - no capital letters + no special chars = likely NL
 *
 * 5. Default: INPUT_HYBRID (try shell, fallback to AI)
 *
 * SynapseOS Project — GPLv2
 * https://synapseos.dev
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "synsh.h"
#include "classify.h"

/* ── Known shell commands ─────────────────────────────────── */
/*
 * A subset of common commands. Not exhaustive — we supplement
 * with PATH lookup in the HYBRID path.
 */
static const char *const shell_commands[] = {
    /* core utils */
    "ls", "ll", "la", "cat", "echo", "printf", "grep", "awk", "sed",
    "cut", "sort", "uniq", "wc", "head", "tail", "tee", "xargs",
    /* fs */
    "cp", "mv", "rm", "mkdir", "rmdir", "ln", "touch", "chmod",
    "chown", "chgrp", "find", "locate", "which", "whereis",
    "stat", "file", "du", "df", "mount", "umount",
    /* process */
    "ps", "top", "htop", "kill", "killall", "pkill", "pgrep",
    "nice", "renice", "nohup", "jobs", "fg", "bg", "wait",
    /* network */
    "ip", "ss", "netstat", "ping", "curl", "wget", "ssh", "scp",
    "rsync", "nc", "nmap", "dig", "nslookup", "traceroute",
    /* package */
    "pacman", "yay", "apt", "apt-get", "dnf", "brew", "pip",
    "npm", "cargo", "go",
    /* dev */
    "git", "make", "cmake", "gcc", "clang", "cc", "ld", "ar",
    "gdb", "valgrind", "strace", "ltrace", "objdump",
    "vim", "nvim", "nano", "emacs", "code",
    /* system */
    "systemctl", "journalctl", "dmesg", "lsmod", "modprobe",
    "insmod", "rmmod", "lspci", "lsusb", "lsblk", "blkid",
    "fdisk", "parted", "dd", "sync", "reboot", "shutdown",
    /* misc */
    "tar", "zip", "unzip", "gzip", "gunzip", "bzip2",
    "ssh-keygen", "gpg", "openssl", "base64",
    "man", "info", "help", "history",
    "date", "cal", "bc", "python", "python3", "perl", "ruby",
    "bash", "sh", "zsh", "fish", "env", "export", "source",
    NULL
};

/* ── Built-in keywords ────────────────────────────────────── */
static const char *const builtins[] = {
    "syn", "cd", "exit", "quit", "bye",
    "help", "history", "alias", "unalias",
    "export", "unset", "set", "source", ".",
    "exec", "eval", "read", "wait",
    NULL
};

/* ── Natural language trigger words ──────────────────────── */
static const char *const nl_words[] = {
    /* questions */
    "what", "how", "why", "where", "when", "which", "who",
    "can", "could", "would", "should", "is", "are", "do",
    /* imperatives that map to ops */
    "show", "list", "display", "print", "tell",
    "find", "search", "look",
    "create", "make", "build", "generate",
    "delete", "remove", "clean", "clear", "wipe",
    "install", "uninstall", "update", "upgrade",
    "start", "stop", "restart", "enable", "disable",
    "check", "verify", "test", "debug", "fix",
    "open", "close", "run", "execute", "launch",
    "move", "copy", "rename",
    "compress", "extract", "archive",
    "download", "upload", "send", "fetch",
    "connect", "disconnect",
    "monitor", "watch", "track",
    NULL
};

/* ── Shell operator chars ─────────────────────────────────── */
static int has_shell_operators(const char *s) {
    return (strchr(s, '|') || strchr(s, '>') ||
            strchr(s, '<') || strchr(s, ';') ||
            strchr(s, '&') || strchr(s, '$') ||
            strchr(s, '`') || strchr(s, '{') ||
            strchr(s, '}') || strchr(s, '[') ||
            strchr(s, '('));
}

/* ── Extract first word ───────────────────────────────────── */
static void first_word(const char *line, char *out, size_t out_len) {
    while (*line && isspace((unsigned char)*line)) line++;
    size_t i = 0;
    while (*line && !isspace((unsigned char)*line) && i < out_len - 1)
        out[i++] = *line++;
    out[i] = '\0';
}

/* ── Check if command exists in PATH ─────────────────────── */
static int cmd_in_path(const char *cmd) {
    /* Quick check: if it contains '/', treat as path execution */
    if (strchr(cmd, '/')) return 1;

    const char *path_env = getenv("PATH");
    if (!path_env) return 0;

    char path_copy[4096];
    strncpy(path_copy, path_env, sizeof(path_copy) - 1);

    char full[512];
    char *dir = strtok(path_copy, ":");
    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, cmd);
        if (access(full, X_OK) == 0) return 1;
        dir = strtok(NULL, ":");
    }
    return 0;
}

/* ── Count words ──────────────────────────────────────────── */
static int word_count(const char *s) {
    int count = 0, in_word = 0;
    while (*s) {
        if (isspace((unsigned char)*s)) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            count++;
        }
        s++;
    }
    return count;
}

/* ── Classify ─────────────────────────────────────────────── */
input_class_t classify_input(const char *line) {
    if (!line || !*line) return INPUT_SHELL;

    /* Skip leading whitespace */
    while (*line && isspace((unsigned char)*line)) line++;

    /* 1. Explicit prefix overrides */
    if (line[0] == '?') return INPUT_AI;
    if (line[0] == '!') return INPUT_SHELL;

    /* 2. Extract first word */
    char first[128] = {0};
    first_word(line, first, sizeof(first));

    if (!first[0]) return INPUT_SHELL;

    /* 3. Built-in check */
    for (int i = 0; builtins[i]; i++) {
        if (strcmp(first, builtins[i]) == 0)
            return INPUT_BUILTIN;
    }

    /* 4. Shell operator check — if present, must be shell */
    if (has_shell_operators(line))
        return INPUT_SHELL;

    /* 5. Path execution: starts with ./ or / */
    if (first[0] == '/' || (first[0] == '.' && first[1] == '/'))
        return INPUT_SHELL;

    /* 6. Quoted first word */
    if (line[0] == '"' || line[0] == '\'')
        return INPUT_SHELL;

    /* 7. Known shell command */
    for (int i = 0; shell_commands[i]; i++) {
        if (strcmp(first, shell_commands[i]) == 0)
            return INPUT_SHELL;
    }

    /* 8. Command exists in PATH */
    if (cmd_in_path(first))
        return INPUT_HYBRID;

    /* 9. Natural language heuristics */

    /* NL trigger word as first word */
    for (int i = 0; nl_words[i]; i++) {
        if (strcasecmp(first, nl_words[i]) == 0)
            return INPUT_AI;
    }

    /* Multi-word, no operators, mostly lowercase → likely NL */
    int words = word_count(line);
    if (words >= 3) {
        int has_upper = 0;
        for (const char *p = line; *p; p++)
            if (isupper((unsigned char)*p)) { has_upper = 1; break; }

        /* Count alphabetic vs special characters */
        int alpha = 0, special = 0;
        for (const char *p = line; *p; p++) {
            if (isalpha((unsigned char)*p) || isspace((unsigned char)*p)) alpha++;
            else if (*p != '-' && *p != '_' && *p != '.') special++;
        }

        if (!has_upper && special == 0 && alpha > 0)
            return INPUT_AI;
    }

    /* 10. Single unrecognized word — try shell, fall back to AI */
    return INPUT_HYBRID;
}
