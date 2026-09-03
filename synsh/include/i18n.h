/*
 * i18n.h — what language this shell is speaking, and everything that follows
 * from the answer.
 *
 * Three separate jobs live here, and they are here together because they all
 * turn on the same fact:
 *
 *   1. WHICH LANGUAGE. Resolved once, from --lang, then SYNSH_LANG, then
 *      LC_ALL/LC_MESSAGES/LANG, then `set language` in synshrc. English when
 *      nothing says otherwise.
 *
 *   2. THE MESSAGES synsh prints. A compiled-in catalog rather than gettext:
 *      synsh is on the ISO and runs before /usr is necessarily complete, and a
 *      shell that cannot find its .mo files must still be able to say why. A
 *      missing translation falls back to English rather than printing an id.
 *
 *   3. FOLDING A LINE FOR MATCHING. Lowercasing with tolower(3) is an
 *      ASCII-only operation — every byte of "WIE SPÄT" above 127 came through
 *      untouched, so the intent tables could only ever match English. Folding
 *      decodes UTF-8, lowercases across Latin-1/Latin Extended-A/Cyrillic/
 *      Greek, and strips diacritics so that a person who cannot be bothered
 *      to type "qué" still gets an answer to "que hora es".
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYNSH_I18N_H
#define SYNSH_I18N_H

#include <stddef.h>
#include <stdbool.h>

/* The languages synsh has a catalog for. Deliberately the same set as
 * syn-install's LOCALE_ROWS — the installer asks the question, and a system
 * installed in Polish whose shell then answered in English would be the
 * "asked and then ignored" failure that table exists to prevent. */
typedef enum {
    LANG_EN = 0, LANG_DE, LANG_FR, LANG_ES, LANG_PT, LANG_IT, LANG_NL,
    LANG_PL, LANG_RU, LANG_JA, LANG_ZH, LANG_KO, LANG_HI, LANG_AR,
    LANG_COUNT
} synsh_lang_t;

/*
 * Every string synsh says to a person, in one list.
 *
 * X(id, english) — the enum, the English text and every catalog's shape are
 * generated from this, so a message cannot exist in one place and not another,
 * and a translation cannot silently slide onto the wrong id. Add here, and the
 * fourteen catalogs keep compiling with English in the new slot until somebody
 * translates it.
 */
#define SYNSH_MESSAGES(X)                                                     \
    X(M_AI_ONLINE,        "AI online")                                        \
    X(M_AI_OFFLINE,       "AI offline")                                       \
    X(M_TYPE_NATURALLY,   "type naturally or use shell commands")             \
    X(M_SHELL_ONLY,       "shell-only mode")                                  \
    X(M_CONNECTED,        "synsh: connected to synapd — AI online")           \
    X(M_NOT_CONNECTED,    "synsh: synapd not connected — running in shell-only mode") \
    X(M_AI_UNAVAILABLE,   "synsh: warning — synapd not available, AI features disabled") \
    X(M_AI_FAILED,        "synsh: AI translation failed")                     \
    X(M_ASKING_AI,        "command failed, asking AI...")                     \
    X(M_RUN_CONFIRM,      "Run?")                                             \
    X(M_CANCELLED,        "Cancelled.")                                       \
    X(M_EDIT_IN_SHELL,    "Edit in shell:")                                   \
    X(M_NO_SHELL_EQUIV,   "No shell equivalent found.")                       \
    X(M_NOT_INSTALLED,    "is not installed")                                 \
    X(M_EXIT,             "exit")                                             \
    X(M_TOO_MANY_ARGS,    "too many arguments")                               \
    X(M_SYNTAX_REDIR,     "syntax error near redirection")                    \
    X(M_HELP_HEADLINE,    "Type commands normally, or just say what you want:") \
    X(M_HELP_REGULAR,     "a regular command")                                \
    X(M_HELP_NATURAL,     "in your own words")                                \
    X(M_HELP_QUESTION,    "a question")                                       \
    X(M_HELP_PREFIX,      "Prefix with ! to force a command, ? to force the AI.") \
    X(M_HELP_BUILTINS,    "Built-ins:")                                       \
    X(M_HELP_META,        "Meta:")                                            \
    X(M_HELP_ASK,         "Ask in your own words")                            \
    X(M_HELP_ANSWERED,    "these are answered directly")                      \
    X(M_HELP_THE_TIME,    "the time")                                         \
    X(M_HELP_THE_DATE,    "the date")                                         \
    X(M_HELP_IN_BROWSER,  "in your browser")                                  \
    X(M_HELP_NO_BROWSER,  "(no browser installed)")                           \
    X(M_HELP_NONE,        "(none installed)")                                 \
    X(M_HELP_NO_PLAYER,   "(no player installed — sudo pacman -S mpv)")       \
    X(M_HELP_ALARM,       "chibi rings it")                                   \
    X(M_HELP_PACKAGES,    "Packages")                                         \
    X(M_HELP_ARCH_SYNTAX, "Arch syntax, so you don't have to remember it")    \
    X(M_HELP_EVERYDAY,    "Everyday commands, in your own words")             \
    X(M_HELP_ELSEWHERE,   "Anything else goes to synapd, which answers for THIS machine.") \
    X(M_HELP_DESTRUCTIVE, "Destructive things stay there on purpose: it shows you the\n  command and waits, rather than guessing at your files.") \
    X(M_HELP_LANGUAGES,   "Understood in:")                                   \
    X(M_STATUS_ONLINE,    "online")                                           \
    X(M_STATUS_OFFLINE,   "offline")                                          \
    X(M_STATUS_ENABLED,   "enabled")                                          \
    X(M_STATUS_DISABLED,  "disabled")                                         \
    X(M_STATUS_ON,        "on")                                               \
    X(M_STATUS_OFF,       "off")                                              \
    X(M_STAT_COMMANDS,    "Commands run")                                     \
    X(M_STAT_NL,          "Natural-language requests")                        \
    X(M_STAT_ASSISTS,     "AI assists")                                       \
    X(M_LANG_IS,          "Language:")                                        \
    X(M_LANG_UNKNOWN,     "unknown language — try one of:")                   \
    X(M_SYNAPD_OFFLINE,   "syn: synapd offline")                             \
    /*                                                                        \
     * ── everything below was English in every language until 0.1.0-29 ─────  \
     *                                                                        \
     * ⛔ AND NOT ONE OF THEM LOOKED WRONG. The catalog was complete, all      \
     * thirteen columns filled, and tests/lang_test.sh passed — because a      \
     * message that never reaches T() is not a MISSING translation, it is not  \
     * a message. `syn status` translated its VALUES through M_STATUS_* and    \
     * printed the labels beside them in English; `syn ai on` printed the      \
     * same two words as bare literals three lines from the T() that has them. \
     * The gate in lang_test.sh now reads the SOURCE and fails on a printed    \
     * English string that is not T().                                        \
     */                                                                       \
    X(M_CD_NO_OLDPWD,     "cd: OLDPWD not set")                               \
    X(M_NO_JOBS,          "No background jobs.")                              \
    X(M_JOB_RUNNING,      "Running")                                          \
    X(M_JOB_STOPPED,      "Stopped")                                          \
    X(M_JOB_DONE,         "Done")                                             \
    X(M_LABEL_SOCKET,     "socket")                                           \
    X(M_LABEL_AI,         "ai")                                               \
    X(M_LABEL_EXPLAIN,    "explain")                                          \
    X(M_LABEL_SAFE,       "safe mode")                                        \
    X(M_SET_AI,           "AI assistance:")                                   \
    X(M_SET_EXPLAIN,      "Explain mode:")                                    \
    X(M_SET_SAFE,         "Safe mode:")                                       \
    X(M_UNKNOWN_SUBCMD,   "syn: unknown subcommand '%s'. Try 'syn' for help.\n") \
    X(M_ALIAS_NOT_FOUND,  "alias: %s: not found\n")                           \
    X(M_ALIAS_BAD_NAME,   "alias: invalid alias name\n")                      \
    X(M_ALIAS_FULL,       "alias: table full (max 128)\n")                    \
    X(M_UNALIAS_USAGE,    "unalias: usage: unalias name [name ...]\n")        \
    X(M_UNALIAS_NOT_FOUND, "unalias: %s: not found\n")                        \
    X(M_TOO_MANY_REDIR,   "synsh: too many redirections")                     \
    X(M_UNTERMINATED,     "synsh: unterminated %s\n")                         \
    X(M_NO_TERMINAL,      "  synsh: no terminal installed to run: %s\n"       \
                          "         sudo pacman -S syntty\n")                 \
    X(M_OPENING_TERM,     "  opening %s\n")                                   \
    X(M_NO_BROWSER,       "  synsh: no browser and no xdg-open installed\n"   \
                          "         sudo pacman -S firefox\n")                \
    X(M_OPENING_URL,      "  %sopening%s %s\n")                               \
    X(M_OPENING_SYNFILES, "  %sopening%s %s in synfiles\n")                   \
    X(M_NO_FILEMANAGER,   "  synsh: no file manager installed\n"              \
                          "         sudo pacman -S synfiles\n")               \
    X(M_OPENING_IN,       "  %sopening%s %s in %s\n")                         \
    X(M_RESUMING_CLIAMP,  "  %sresuming%s cliamp\n")                          \
    X(M_STARTING_CLIAMP,  "  %sstarting%s cliamp\n")                          \
    X(M_NO_MUSIC_DIR,     "  synsh: no music directory (%s)\n")               \
    X(M_PLAYING_WITH,     "  %splaying%s %s with %s\n")                       \
    X(M_ALARM_WHAT_TIME,  "  synsh: what time? e.g. \"set alarm for 7:30am\"\n") \
    X(M_CANNOT_WRITE,     "  synsh: cannot write %s: %s\n")                   \
    X(M_CANNOT_REPLACE,   "  synsh: cannot replace %s: %s\n")                 \
    X(M_ALARM_SET,        "  %salarm set for %s%s\n")                         \
    X(M_CHIBI_NOT_RUNNING, "  %s(chibi is not running — she rings it, so start her before then)%s\n") \
    X(M_INIT_FAILED,      "synsh: initialization failed")                     \
    X(M_CONNECTED_SHORT,  "synsh: connected to synapd")                      \
    X(M_TAGLINE,          "Where the kernel thinks")                        \
    X(M_NO_ORPHANS,       "  no orphaned packages")

#define SYNSH_MSG_ENUM(id, en) id,
typedef enum { SYNSH_MESSAGES(SYNSH_MSG_ENUM) M_COUNT } synsh_msg_t;
#undef SYNSH_MSG_ENUM

/* Resolve the language. `want` is a code from --lang or `set language`, or
 * NULL to read the environment. Safe to call more than once — the last
 * explicit answer wins, which is what makes `set language` in a user rc able
 * to override a system rc. Returns the language actually selected. */
synsh_lang_t synsh_i18n_init(const char *want);

synsh_lang_t synsh_lang(void);
/* "en", "de", … — the tag, for the rc file and for `syn lang`. */
const char  *synsh_lang_code(synsh_lang_t l);
/* "English", "Deutsch", … — the endonym, for people. */
const char  *synsh_lang_name(synsh_lang_t l);
/* "English", "German", … — for the AI prompt, which is written in English and
 * has to name the reply language in a word the model will recognise. */
const char  *synsh_lang_english_name(synsh_lang_t l);
/* The code, or LANG_COUNT if nothing matches. Accepts "de", "de_DE",
 * "de_DE.UTF-8", "German", "Deutsch". */
synsh_lang_t synsh_lang_from_string(const char *s);

/* The catalog lookup. Never returns NULL: an untranslated slot falls back to
 * the English text the message was declared with. */
const char *synsh_msg(synsh_msg_t id);
#define T(id) synsh_msg(id)

/*
 * Fold `src` into `dst` for matching: decode UTF-8, lowercase, strip
 * diacritics, collapse whitespace runs to one space, drop the punctuation that
 * only ever marks a sentence as a question or an exclamation — including the
 * ones that lead ("¿", "¡") and the full-width CJK forms, which is why this
 * cannot be a byte loop.
 *
 * Anything caseless and unaccented (CJK, Arabic, Devanagari, Hangul) comes
 * through unchanged, which is exactly right: those scripts need no folding and
 * the tables are written in them directly.
 */
/* How many COLUMNS this string takes on a terminal — not bytes, not code
 * points. The banner box pads to a column count, and a byte count pads a CJK
 * tagline to less than half its width. See the note in i18n.c. */
int synsh_disp_width(const char *s);

void synsh_fold(char *dst, size_t n, const char *src);

/* The same, with the German ae/oe/ue transliteration applied instead of the
 * plain accent strip — see strip_accent(). Callers that compare a PHRASE
 * against a folded line must try both; synsh_fold_eq() and the circumfix
 * matcher do. */
void synsh_fold_translit(char *dst, size_t n, const char *src);

/* Does this folded line equal `phrase`, once `phrase` is folded the same way?
 * The phrase tables are written the way a person spells things — "qué hora
 * es", not "que hora es" — and folding both sides is what lets them stay
 * readable while still matching an unaccented typist. */
bool synsh_fold_eq(const char *folded_line, const char *phrase);

/* Whole-line match against a NULL-terminated phrase list, folding each entry. */
bool synsh_fold_in(const char *folded_line, const char *const *phrases);

/* Does the folded line start with `phrase` (folded), on a word boundary?
 * Returns the rest of the line after it, or NULL. The rest may be empty. */
const char *synsh_fold_after(const char *folded_line, const char *phrase);

/* Is this byte the start of a multi-byte UTF-8 sequence, or a continuation?
 * classify.c needs it to stop counting "ä" as two pieces of punctuation. */
bool synsh_utf8_is_letterish(unsigned char c);

#endif /* SYNSH_I18N_H */
