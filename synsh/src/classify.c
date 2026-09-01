#include <unistd.h>
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
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "synsh.h"
#include "classify.h"
#include "i18n.h"

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
/*
 * The FIRST word of a line, in the languages SynapseOS installs in.
 *
 * ⚠ EVERY LANGUAGE IN ONE LIST, like the phrase tables — and for the same
 * reason: this runs before anything knows or could reliably guess which
 * language the line is in. A three-word command line carries nowhere near
 * enough signal to detect a language from, and being wrong here means routing
 * a plain request to execve and answering it with "command not found".
 *
 * ⚠ AND EVERY ENTRY IS CHECKED AGAINST $PATH FIRST (step 8 above), so a word
 * that is also a program still runs the program. That is what lets "open",
 * "find", "install" and "test" sit in this list safely.
 */
static const char *const nl_words[] = {
    /* en — questions */
    "what", "how", "why", "where", "when", "which", "who",
    "can", "could", "would", "should", "is", "are", "do", "does",
    /* en — imperatives that map to operations */
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
    /* de */
    "was", "wie", "warum", "wo", "wann", "welche", "welcher", "welches", "wer",
    "kann", "kannst", "zeig", "zeige", "liste", "finde", "suche", "such",
    "mach", "mache", "erstelle", "lösche", "entferne", "installiere",
    "starte", "stoppe", "prüfe", "öffne", "schließe", "kopiere", "verschiebe",
    "aktualisiere", "gib", "sag", "wieviel", "wie viel",
    /* fr */
    "quoi", "comment", "pourquoi", "où", "quand", "quel", "quelle", "qui",
    "peux", "peux-tu", "montre", "affiche", "liste", "trouve", "cherche",
    "crée", "supprime", "installe", "démarre", "arrête", "ouvre", "ferme",
    "copie", "déplace", "mets", "donne", "dis", "combien",
    /* es */
    "qué", "cómo", "por", "dónde", "cuándo", "cuál", "quién", "cuánto",
    "puedes", "muestra", "lista", "busca", "encuentra", "crea", "borra",
    "elimina", "instala", "inicia", "detén", "abre", "cierra", "copia",
    "mueve", "pon", "dime", "dame",
    /* pt */
    "que", "como", "porque", "onde", "quando", "qual", "quem", "quanto",
    "mostra", "mostre", "lista", "procura", "encontra", "cria", "apaga",
    "remove", "instala", "inicia", "para", "abre", "fecha", "copia", "move",
    "coloca", "diga", "diz",
    /* it */
    "cosa", "come", "perché", "dove", "quando", "quale", "chi", "quanto",
    "puoi", "mostra", "elenca", "trova", "cerca", "crea", "elimina",
    "installa", "avvia", "ferma", "apri", "chiudi", "copia", "sposta",
    "metti", "dimmi",
    /* nl */
    "wat", "hoe", "waarom", "waar", "wanneer", "welke", "wie", "hoeveel",
    "kun", "kan", "toon", "laat", "zoek", "vind", "maak", "verwijder",
    "installeer", "start", "stop", "open", "sluit", "kopieer", "verplaats",
    "zet", "geef", "vertel",
    /* pl */
    "co", "jak", "dlaczego", "gdzie", "kiedy", "który", "która", "kto", "ile",
    "pokaż", "wyświetl", "znajdź", "szukaj", "utwórz", "usuń", "zainstaluj",
    "uruchom", "zatrzymaj", "otwórz", "zamknij", "skopiuj", "przenieś",
    "ustaw", "powiedz",
    /* ru */
    "что", "как", "почему", "где", "когда", "какой", "какая", "кто",
    "сколько", "покажи", "выведи", "найди", "создай", "удали", "установи",
    "запусти", "останови", "открой", "закрой", "скопируй", "перемести",
    "поставь", "скажи", "дай",
    /* ar */
    "ما", "ماذا", "كيف", "لماذا", "أين", "متى", "أي", "من", "كم",
    "اعرض", "أظهر", "ابحث", "أنشئ", "احذف", "ثبت", "شغل", "أوقف", "افتح",
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
/*
 * ⚠ strncpy(buf, PATH, sizeof(buf)-1) DOES NOT TERMINATE a string that filled
 * the buffer, and this had no explicit terminator: a PATH of 4095 bytes or
 * more — which a Nix or toolchain-heavy environment reaches easily — left
 * strtok() walking off the end of the array. Walk the variable in place
 * instead; there is then no copy to get wrong and no length to exceed.
 */
static int cmd_in_path(const char *cmd) {
    /* Quick check: if it contains '/', treat as path execution */
    if (strchr(cmd, '/')) return 1;

    const char *path_env = getenv("PATH");
    if (!path_env || !*path_env) return 0;

    size_t clen = strlen(cmd);
    char full[4096];
    const char *p = path_env;
    while (*p) {
        const char *colon = strchr(p, ':');
        size_t dlen = colon ? (size_t)(colon - p) : strlen(p);
        if (dlen && dlen + clen + 2 <= sizeof(full)) {
            memcpy(full, p, dlen);
            full[dlen] = '/';
            memcpy(full + dlen + 1, cmd, clen + 1);
            if (access(full, X_OK) == 0) return 1;
        }
        if (!colon) break;
        p = colon + 1;
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

    /* 1. Explicit prefix overrides.
     *
     * '¿' joins '?' because a Spanish question is written with it, and a line
     * that opens one is not a command in any language. It is two bytes in
     * UTF-8, which is why it is compared as a string. */
    if (line[0] == '?') return INPUT_AI;
    if (strncmp(line, "\xc2\xbf", 2) == 0) return INPUT_AI;
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

    /* NL trigger word as first word. Folded on both sides: strcasecmp only
     * knows ASCII, so "Покажи" and "покажи" were two different words to it and
     * every capitalised non-Latin request fell through to the shell. */
    char folded_first[128];
    synsh_fold(folded_first, sizeof(folded_first), first);
    for (int i = 0; nl_words[i]; i++) {
        char fw[128];
        synsh_fold(fw, sizeof(fw), nl_words[i]);
        if (strcmp(folded_first, fw) == 0)
            return INPUT_AI;
    }

    /*
     * Several words, no shell operators, nothing but letters → prose.
     *
     * ⚠ TWO THINGS HERE WERE ENGLISH-ONLY AND BOTH FAILED SILENTLY.
     *
     * The special-character count treated every byte above 127 as punctuation,
     * so "wie spät ist es" scored two specials for its ä alone and could never
     * qualify — every accented language was pushed onto the HYBRID path, which
     * runs the line as a command first and only asks the model after the shell
     * has printed "command not found".
     *
     * And the test demanded NO capital letter anywhere, which is a rule about
     * English orthography: German capitalises every noun, so a correctly
     * spelled German request was disqualified by being correctly spelled.
     * Nothing replaces it — by the time we are here the first word is neither a
     * known command nor anything on $PATH, and three such words in a row are
     * prose whatever their case.
     */
    int words = word_count(line);

    /*
     * ⚠ COUNTING WORDS IS COUNTING SPACES, AND JAPANESE HAS NONE.
     * "何かおすすめの設定はある" is a full sentence and one "word" by that
     * measure, so the word test could never fire for Japanese, Chinese or
     * Thai — the three scripts whose users are least able to fall back on
     * typing the English. Characters are counted as well as words: a run of
     * several multi-byte characters with no shell operator in sight is prose
     * in any script, and by this point in the function we already know the
     * first word is neither a known command nor anything on $PATH.
     */
    int mbchars = 0;
    for (const char *p = line; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 0xC0 && c <= 0xF7) mbchars++;
    }

    if (words >= 3 || mbchars >= 3 || (mbchars && words >= 2)) {
        int alpha = 0, special = 0;
        for (const char *p = line; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (isalpha(c) || isspace(c) || synsh_utf8_is_letterish(c)) alpha++;
            else if (*p != '-' && *p != '_' && *p != '.' && *p != ',' &&
                     *p != '\'' && *p != '?' && *p != '!') special++;
        }

        if (special == 0 && alpha > 0)
            return INPUT_AI;
    }

    /* 10. Single unrecognized word — try shell, fall back to AI */
    return INPUT_HYBRID;
}
