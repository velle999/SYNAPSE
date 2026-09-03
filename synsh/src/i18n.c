/*
 * i18n.c — language resolution, the message catalog, and UTF-8 folding.
 *
 * See i18n.h for why all three live together.
 *
 * ⚠ THE CATALOGS ARE DESIGNATED-INITIALISER ARRAYS, not lists of strings in
 * message order. A list would put every translation one slot out the first
 * time somebody inserted a message in the middle of SYNSH_MESSAGES, and each
 * language would be wrong in a different way — the sort of bug that only the
 * speaker of that language can see. Naming the slot makes that impossible, and
 * makes a partial translation legal: an unfilled slot is NULL and falls back
 * to the English the message was declared with.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "i18n.h"

/* ── The English text, from the declaration list ──────────── */
#define SYNSH_MSG_EN(id, en) [id] = en,
static const char *const MSG_EN[M_COUNT] = { SYNSH_MESSAGES(SYNSH_MSG_EN) };
#undef SYNSH_MSG_EN

/* ── Catalogs ─────────────────────────────────────────────── */
/*
 * One array per language. Only the slots that differ from English need to be
 * here; anything left out prints English rather than an id, because a shell
 * that answers "M_CANCELLED" is worse in every language than one that answers
 * "Cancelled."
 */

static const char *const MSG_DE[M_COUNT] = {
    [M_AI_ONLINE]         = "KI online",
    [M_AI_OFFLINE]        = "KI offline",
    [M_TYPE_NATURALLY]    = "sag einfach, was du willst, oder nutze Shell-Befehle",
    [M_SHELL_ONLY]        = "nur Shell-Modus",
    [M_CONNECTED]         = "synsh: mit synapd verbunden — KI online",
    [M_NOT_CONNECTED]     = "synsh: keine Verbindung zu synapd — nur Shell-Modus",
    [M_AI_UNAVAILABLE]    = "synsh: Warnung — synapd nicht erreichbar, KI-Funktionen deaktiviert",
    [M_AI_FAILED]         = "synsh: KI-Übersetzung fehlgeschlagen",
    [M_ASKING_AI]         = "Befehl fehlgeschlagen, frage die KI …",
    [M_RUN_CONFIRM]       = "Ausführen?",
    [M_CANCELLED]         = "Abgebrochen.",
    [M_EDIT_IN_SHELL]     = "In der Shell bearbeiten:",
    [M_NO_SHELL_EQUIV]    = "Kein passender Shell-Befehl gefunden.",
    [M_NOT_INSTALLED]     = "ist nicht installiert",
    [M_EXIT]              = "Ende",
    [M_TOO_MANY_ARGS]     = "zu viele Argumente",
    [M_SYNTAX_REDIR]      = "Syntaxfehler bei der Umleitung",
    [M_HELP_HEADLINE]     = "Tippe Befehle wie gewohnt — oder sag einfach, was du willst:",
    [M_HELP_REGULAR]      = "ein normaler Befehl",
    [M_HELP_NATURAL]      = "in eigenen Worten",
    [M_HELP_QUESTION]     = "eine Frage",
    [M_HELP_PREFIX]       = "! erzwingt einen Befehl, ? erzwingt die KI.",
    [M_HELP_BUILTINS]     = "Eingebaut:",
    [M_HELP_META]         = "Meta:",
    [M_HELP_ASK]          = "Frag in eigenen Worten",
    [M_HELP_ANSWERED]     = "das wird direkt beantwortet",
    [M_HELP_THE_TIME]     = "die Uhrzeit",
    [M_HELP_THE_DATE]     = "das Datum",
    [M_HELP_IN_BROWSER]   = "im Browser",
    [M_HELP_NO_BROWSER]   = "(kein Browser installiert)",
    [M_HELP_NONE]         = "(nichts installiert)",
    [M_HELP_NO_PLAYER]    = "(kein Player installiert — sudo pacman -S mpv)",
    [M_HELP_ALARM]        = "chibi weckt dich",
    [M_HELP_PACKAGES]     = "Pakete",
    [M_HELP_ARCH_SYNTAX]  = "Arch-Syntax, damit du sie dir nicht merken musst",
    [M_HELP_EVERYDAY]     = "Alltagsbefehle, in eigenen Worten",
    [M_HELP_ELSEWHERE]    = "Alles andere geht an synapd, das für DIESEN Rechner antwortet.",
    [M_HELP_DESTRUCTIVE]  = "Zerstörerisches bleibt absichtlich dort: es zeigt dir den Befehl\n  und wartet, statt bei deinen Dateien zu raten.",
    [M_HELP_LANGUAGES]    = "Verstanden wird:",
    [M_STATUS_ONLINE]     = "online",
    [M_STATUS_OFFLINE]    = "offline",
    [M_STATUS_ENABLED]    = "aktiviert",
    [M_STATUS_DISABLED]   = "deaktiviert",
    [M_STATUS_ON]         = "an",
    [M_STATUS_OFF]        = "aus",
    [M_STAT_COMMANDS]     = "Ausgeführte Befehle",
    [M_STAT_NL]           = "Anfragen in eigenen Worten",
    [M_STAT_ASSISTS]      = "KI-Hilfen",
    [M_LANG_IS]           = "Sprache:",
    [M_LANG_UNKNOWN]      = "unbekannte Sprache — möglich sind:",
    [M_SYNAPD_OFFLINE]    = "syn: synapd offline",
    [M_CD_NO_OLDPWD]      = "cd: OLDPWD ist nicht gesetzt",
    [M_NO_JOBS]           = "Keine Hintergrundaufträge.",
    [M_JOB_RUNNING]       = "Läuft",
    [M_JOB_STOPPED]       = "Angehalten",
    [M_JOB_DONE]          = "Fertig",
    [M_LABEL_SOCKET]      = "Socket",
    [M_LABEL_AI]          = "KI",
    [M_LABEL_EXPLAIN]     = "Erklären",
    [M_LABEL_SAFE]        = "Sicherer Modus",
    [M_SET_AI]            = "KI-Unterstützung:",
    [M_SET_EXPLAIN]       = "Erklärmodus:",
    [M_SET_SAFE]          = "Sicherer Modus:",
    [M_UNKNOWN_SUBCMD]    = "syn: unbekannter Unterbefehl '%s'. 'syn' zeigt die Hilfe.\n",
    [M_ALIAS_NOT_FOUND]   = "alias: %s: nicht gefunden\n",
    [M_ALIAS_BAD_NAME]    = "alias: ungültiger Alias-Name\n",
    [M_ALIAS_FULL]        = "alias: Tabelle voll (max. 128)\n",
    [M_UNALIAS_USAGE]     = "unalias: Aufruf: unalias Name [Name …]\n",
    [M_UNALIAS_NOT_FOUND] = "unalias: %s: nicht gefunden\n",
    [M_TOO_MANY_REDIR]    = "synsh: zu viele Umleitungen",
    [M_UNTERMINATED]      = "synsh: nicht abgeschlossenes %s\n",
    [M_NO_TERMINAL]       = "  synsh: kein Terminal installiert für: %s\n         sudo pacman -S syntty\n",
    [M_OPENING_TERM]      = "  öffne %s\n",
    [M_NO_BROWSER]        = "  synsh: kein Browser und kein xdg-open installiert\n         sudo pacman -S firefox\n",
    [M_OPENING_URL]       = "  %söffne%s %s\n",
    [M_OPENING_SYNFILES]  = "  %söffne%s %s in synfiles\n",
    [M_NO_FILEMANAGER]    = "  synsh: kein Dateimanager installiert\n         sudo pacman -S synfiles\n",
    [M_OPENING_IN]        = "  %söffne%s %s in %s\n",
    [M_RESUMING_CLIAMP]   = "  %scliamp wird fortgesetzt%s\n",
    [M_STARTING_CLIAMP]   = "  %scliamp wird gestartet%s\n",
    [M_NO_MUSIC_DIR]      = "  synsh: kein Musikverzeichnis (%s)\n",
    [M_PLAYING_WITH]      = "  %sspiele%s %s mit %s\n",
    [M_ALARM_WHAT_TIME]   = "  synsh: um wie viel Uhr? z. B. \"set alarm for 7:30am\"\n",
    [M_CANNOT_WRITE]      = "  synsh: %s kann nicht geschrieben werden: %s\n",
    [M_CANNOT_REPLACE]    = "  synsh: %s kann nicht ersetzt werden: %s\n",
    [M_ALARM_SET]         = "  %sWecker gestellt auf %s%s\n",
    [M_CHIBI_NOT_RUNNING] = "  %s(chibi läuft nicht — sie lässt ihn klingeln, starte sie also vorher)%s\n",
    [M_INIT_FAILED]       = "synsh: Initialisierung fehlgeschlagen",
    [M_CONNECTED_SHORT]   = "synsh: mit synapd verbunden",
    [M_TAGLINE]           = "Wo der Kernel denkt",
    [M_NO_ORPHANS]        = "  keine verwaisten Pakete",
};

static const char *const MSG_FR[M_COUNT] = {
    [M_AI_ONLINE]         = "IA en ligne",
    [M_AI_OFFLINE]        = "IA hors ligne",
    [M_TYPE_NATURALLY]    = "dites ce que vous voulez, ou utilisez des commandes shell",
    [M_SHELL_ONLY]        = "mode shell uniquement",
    [M_CONNECTED]         = "synsh : connecté à synapd — IA en ligne",
    [M_NOT_CONNECTED]     = "synsh : synapd injoignable — mode shell uniquement",
    [M_AI_UNAVAILABLE]    = "synsh : attention — synapd indisponible, fonctions IA désactivées",
    [M_AI_FAILED]         = "synsh : échec de la traduction par l'IA",
    [M_ASKING_AI]         = "commande en échec, je demande à l'IA…",
    [M_RUN_CONFIRM]       = "Exécuter ?",
    [M_CANCELLED]         = "Annulé.",
    [M_EDIT_IN_SHELL]     = "À modifier dans le shell :",
    [M_NO_SHELL_EQUIV]    = "Aucune commande shell ne correspond.",
    [M_NOT_INSTALLED]     = "n'est pas installé",
    [M_EXIT]              = "sortie",
    [M_TOO_MANY_ARGS]     = "trop d'arguments",
    [M_SYNTAX_REDIR]      = "erreur de syntaxe dans la redirection",
    [M_HELP_HEADLINE]     = "Tapez vos commandes normalement, ou dites simplement ce que vous voulez :",
    [M_HELP_REGULAR]      = "une commande classique",
    [M_HELP_NATURAL]      = "avec vos propres mots",
    [M_HELP_QUESTION]     = "une question",
    [M_HELP_PREFIX]       = "Préfixez par ! pour forcer une commande, ? pour forcer l'IA.",
    [M_HELP_BUILTINS]     = "Intégrées :",
    [M_HELP_META]         = "Méta :",
    [M_HELP_ASK]          = "Demandez avec vos propres mots",
    [M_HELP_ANSWERED]     = "voici ce qui reçoit une réponse directe",
    [M_HELP_THE_TIME]     = "l'heure",
    [M_HELP_THE_DATE]     = "la date",
    [M_HELP_IN_BROWSER]   = "dans votre navigateur",
    [M_HELP_NO_BROWSER]   = "(aucun navigateur installé)",
    [M_HELP_NONE]         = "(aucun installé)",
    [M_HELP_NO_PLAYER]    = "(aucun lecteur installé — sudo pacman -S mpv)",
    [M_HELP_ALARM]        = "chibi vous réveille",
    [M_HELP_PACKAGES]     = "Paquets",
    [M_HELP_ARCH_SYNTAX]  = "syntaxe Arch, pour ne pas avoir à la retenir",
    [M_HELP_EVERYDAY]     = "Commandes de tous les jours, avec vos propres mots",
    [M_HELP_ELSEWHERE]    = "Tout le reste va à synapd, qui répond pour CETTE machine.",
    [M_HELP_DESTRUCTIVE]  = "Ce qui détruit y reste exprès : la commande vous est montrée\n  et attend, plutôt que de deviner à propos de vos fichiers.",
    [M_HELP_LANGUAGES]    = "Langues comprises :",
    [M_STATUS_ONLINE]     = "en ligne",
    [M_STATUS_OFFLINE]    = "hors ligne",
    [M_STATUS_ENABLED]    = "activé",
    [M_STATUS_DISABLED]   = "désactivé",
    [M_STATUS_ON]         = "activé",
    [M_STATUS_OFF]        = "désactivé",
    [M_STAT_COMMANDS]     = "Commandes exécutées",
    [M_STAT_NL]           = "Demandes en langage courant",
    [M_STAT_ASSISTS]      = "Aides de l'IA",
    [M_LANG_IS]           = "Langue :",
    [M_LANG_UNKNOWN]      = "langue inconnue — essayez :",
    [M_SYNAPD_OFFLINE]    = "syn : synapd hors ligne",
    [M_CD_NO_OLDPWD]      = "cd : OLDPWD n'est pas défini",
    [M_NO_JOBS]           = "Aucune tâche en arrière-plan.",
    [M_JOB_RUNNING]       = "En cours",
    [M_JOB_STOPPED]       = "Arrêtée",
    [M_JOB_DONE]          = "Terminée",
    [M_LABEL_SOCKET]      = "socket",
    [M_LABEL_AI]          = "IA",
    [M_LABEL_EXPLAIN]     = "expliquer",
    [M_LABEL_SAFE]        = "mode sûr",
    [M_SET_AI]            = "Assistance IA :",
    [M_SET_EXPLAIN]       = "Mode explication :",
    [M_SET_SAFE]          = "Mode sûr :",
    [M_UNKNOWN_SUBCMD]    = "syn : sous-commande inconnue « %s ». Tapez « syn » pour l'aide.\n",
    [M_ALIAS_NOT_FOUND]   = "alias : %s : introuvable\n",
    [M_ALIAS_BAD_NAME]    = "alias : nom d'alias invalide\n",
    [M_ALIAS_FULL]        = "alias : table pleine (max 128)\n",
    [M_UNALIAS_USAGE]     = "unalias : usage : unalias nom [nom …]\n",
    [M_UNALIAS_NOT_FOUND] = "unalias : %s : introuvable\n",
    [M_TOO_MANY_REDIR]    = "synsh : trop de redirections",
    [M_UNTERMINATED]      = "synsh : %s non fermé\n",
    [M_NO_TERMINAL]       = "  synsh : aucun terminal installé pour lancer : %s\n         sudo pacman -S syntty\n",
    [M_OPENING_TERM]      = "  ouverture de %s\n",
    [M_NO_BROWSER]        = "  synsh : aucun navigateur ni xdg-open installé\n         sudo pacman -S firefox\n",
    [M_OPENING_URL]       = "  %souverture de%s %s\n",
    [M_OPENING_SYNFILES]  = "  %souverture de%s %s dans synfiles\n",
    [M_NO_FILEMANAGER]    = "  synsh : aucun gestionnaire de fichiers installé\n         sudo pacman -S synfiles\n",
    [M_OPENING_IN]        = "  %souverture de%s %s dans %s\n",
    [M_RESUMING_CLIAMP]   = "  %sreprise de cliamp%s\n",
    [M_STARTING_CLIAMP]   = "  %sdémarrage de cliamp%s\n",
    [M_NO_MUSIC_DIR]      = "  synsh : aucun dossier de musique (%s)\n",
    [M_PLAYING_WITH]      = "  %slecture de%s %s avec %s\n",
    [M_ALARM_WHAT_TIME]   = "  synsh : à quelle heure ? p. ex. \"set alarm for 7:30am\"\n",
    [M_CANNOT_WRITE]      = "  synsh : impossible d'écrire %s : %s\n",
    [M_CANNOT_REPLACE]    = "  synsh : impossible de remplacer %s : %s\n",
    [M_ALARM_SET]         = "  %salarme réglée pour %s%s\n",
    [M_CHIBI_NOT_RUNNING] = "  %s(chibi n'est pas lancée — c'est elle qui la fait sonner, alors démarrez-la avant)%s\n",
    [M_INIT_FAILED]       = "synsh : échec de l'initialisation",
    [M_CONNECTED_SHORT]   = "synsh : connecté à synapd",
    [M_TAGLINE]           = "Là où le noyau pense",
    [M_NO_ORPHANS]        = "  aucun paquet orphelin",
};

static const char *const MSG_ES[M_COUNT] = {
    [M_AI_ONLINE]         = "IA en línea",
    [M_AI_OFFLINE]        = "IA desconectada",
    [M_TYPE_NATURALLY]    = "di lo que quieras, o usa órdenes de shell",
    [M_SHELL_ONLY]        = "modo solo shell",
    [M_CONNECTED]         = "synsh: conectado a synapd — IA en línea",
    [M_NOT_CONNECTED]     = "synsh: synapd no responde — modo solo shell",
    [M_AI_UNAVAILABLE]    = "synsh: aviso — synapd no disponible, funciones de IA desactivadas",
    [M_AI_FAILED]         = "synsh: la traducción de la IA falló",
    [M_ASKING_AI]         = "la orden falló, preguntando a la IA…",
    [M_RUN_CONFIRM]       = "¿Ejecutar?",
    [M_CANCELLED]         = "Cancelado.",
    [M_EDIT_IN_SHELL]     = "Edítalo en el shell:",
    [M_NO_SHELL_EQUIV]    = "No hay una orden de shell equivalente.",
    [M_NOT_INSTALLED]     = "no está instalado",
    [M_EXIT]              = "salida",
    [M_TOO_MANY_ARGS]     = "demasiados argumentos",
    [M_SYNTAX_REDIR]      = "error de sintaxis en la redirección",
    [M_HELP_HEADLINE]     = "Escribe órdenes como siempre, o simplemente di lo que quieres:",
    [M_HELP_REGULAR]      = "una orden normal",
    [M_HELP_NATURAL]      = "con tus propias palabras",
    [M_HELP_QUESTION]     = "una pregunta",
    [M_HELP_PREFIX]       = "Antepón ! para forzar una orden, ? para forzar la IA.",
    [M_HELP_BUILTINS]     = "Integradas:",
    [M_HELP_META]         = "Meta:",
    [M_HELP_ASK]          = "Pregunta con tus propias palabras",
    [M_HELP_ANSWERED]     = "esto se responde directamente",
    [M_HELP_THE_TIME]     = "la hora",
    [M_HELP_THE_DATE]     = "la fecha",
    [M_HELP_IN_BROWSER]   = "en tu navegador",
    [M_HELP_NO_BROWSER]   = "(no hay navegador instalado)",
    [M_HELP_NONE]         = "(ninguno instalado)",
    [M_HELP_NO_PLAYER]    = "(no hay reproductor instalado — sudo pacman -S mpv)",
    [M_HELP_ALARM]        = "chibi te despierta",
    [M_HELP_PACKAGES]     = "Paquetes",
    [M_HELP_ARCH_SYNTAX]  = "sintaxis de Arch, para no tener que recordarla",
    [M_HELP_EVERYDAY]     = "Órdenes de cada día, con tus propias palabras",
    [M_HELP_ELSEWHERE]    = "Todo lo demás va a synapd, que responde por ESTA máquina.",
    [M_HELP_DESTRUCTIVE]  = "Lo destructivo se queda allí a propósito: te enseña la orden\n  y espera, en vez de adivinar con tus archivos.",
    [M_HELP_LANGUAGES]    = "Se entiende en:",
    [M_STATUS_ONLINE]     = "en línea",
    [M_STATUS_OFFLINE]    = "desconectado",
    [M_STATUS_ENABLED]    = "activado",
    [M_STATUS_DISABLED]   = "desactivado",
    [M_STATUS_ON]         = "activado",
    [M_STATUS_OFF]        = "desactivado",
    [M_STAT_COMMANDS]     = "Órdenes ejecutadas",
    [M_STAT_NL]           = "Peticiones en lenguaje natural",
    [M_STAT_ASSISTS]      = "Ayudas de la IA",
    [M_LANG_IS]           = "Idioma:",
    [M_LANG_UNKNOWN]      = "idioma desconocido — prueba con:",
    [M_SYNAPD_OFFLINE]    = "syn: synapd desconectado",
    [M_CD_NO_OLDPWD]      = "cd: OLDPWD no está definido",
    [M_NO_JOBS]           = "No hay trabajos en segundo plano.",
    [M_JOB_RUNNING]       = "En ejecución",
    [M_JOB_STOPPED]       = "Detenido",
    [M_JOB_DONE]          = "Terminado",
    [M_LABEL_SOCKET]      = "socket",
    [M_LABEL_AI]          = "IA",
    [M_LABEL_EXPLAIN]     = "explicar",
    [M_LABEL_SAFE]        = "modo seguro",
    [M_SET_AI]            = "Asistencia de IA:",
    [M_SET_EXPLAIN]       = "Modo explicación:",
    [M_SET_SAFE]          = "Modo seguro:",
    [M_UNKNOWN_SUBCMD]    = "syn: subcomando desconocido '%s'. Escribe 'syn' para la ayuda.\n",
    [M_ALIAS_NOT_FOUND]   = "alias: %s: no encontrado\n",
    [M_ALIAS_BAD_NAME]    = "alias: nombre de alias no válido\n",
    [M_ALIAS_FULL]        = "alias: tabla llena (máx. 128)\n",
    [M_UNALIAS_USAGE]     = "unalias: uso: unalias nombre [nombre …]\n",
    [M_UNALIAS_NOT_FOUND] = "unalias: %s: no encontrado\n",
    [M_TOO_MANY_REDIR]    = "synsh: demasiadas redirecciones",
    [M_UNTERMINATED]      = "synsh: %s sin cerrar\n",
    [M_NO_TERMINAL]       = "  synsh: no hay ninguna terminal instalada para ejecutar: %s\n         sudo pacman -S syntty\n",
    [M_OPENING_TERM]      = "  abriendo %s\n",
    [M_NO_BROWSER]        = "  synsh: no hay navegador ni xdg-open instalados\n         sudo pacman -S firefox\n",
    [M_OPENING_URL]       = "  %sabriendo%s %s\n",
    [M_OPENING_SYNFILES]  = "  %sabriendo%s %s en synfiles\n",
    [M_NO_FILEMANAGER]    = "  synsh: no hay gestor de archivos instalado\n         sudo pacman -S synfiles\n",
    [M_OPENING_IN]        = "  %sabriendo%s %s en %s\n",
    [M_RESUMING_CLIAMP]   = "  %sreanudando cliamp%s\n",
    [M_STARTING_CLIAMP]   = "  %siniciando cliamp%s\n",
    [M_NO_MUSIC_DIR]      = "  synsh: no hay carpeta de música (%s)\n",
    [M_PLAYING_WITH]      = "  %sreproduciendo%s %s con %s\n",
    [M_ALARM_WHAT_TIME]   = "  synsh: ¿a qué hora? p. ej. \"set alarm for 7:30am\"\n",
    [M_CANNOT_WRITE]      = "  synsh: no se puede escribir %s: %s\n",
    [M_CANNOT_REPLACE]    = "  synsh: no se puede reemplazar %s: %s\n",
    [M_ALARM_SET]         = "  %salarma puesta para las %s%s\n",
    [M_CHIBI_NOT_RUNNING] = "  %s(chibi no está en ejecución — es ella quien la hace sonar, así que arráncala antes)%s\n",
    [M_INIT_FAILED]       = "synsh: fallo en la inicialización",
    [M_CONNECTED_SHORT]   = "synsh: conectado a synapd",
    [M_TAGLINE]           = "Donde piensa el núcleo",
    [M_NO_ORPHANS]        = "  no hay paquetes huérfanos",
};

static const char *const MSG_PT[M_COUNT] = {
    [M_AI_ONLINE]         = "IA online",
    [M_AI_OFFLINE]        = "IA offline",
    [M_TYPE_NATURALLY]    = "diga o que quiser, ou use comandos do shell",
    [M_SHELL_ONLY]        = "modo apenas shell",
    [M_CONNECTED]         = "synsh: conectado ao synapd — IA online",
    [M_NOT_CONNECTED]     = "synsh: synapd não responde — modo apenas shell",
    [M_AI_UNAVAILABLE]    = "synsh: aviso — synapd indisponível, recursos de IA desativados",
    [M_AI_FAILED]         = "synsh: a tradução da IA falhou",
    [M_ASKING_AI]         = "o comando falhou, perguntando à IA…",
    [M_RUN_CONFIRM]       = "Executar?",
    [M_CANCELLED]         = "Cancelado.",
    [M_EDIT_IN_SHELL]     = "Edite no shell:",
    [M_NO_SHELL_EQUIV]    = "Nenhum comando de shell equivalente.",
    [M_NOT_INSTALLED]     = "não está instalado",
    [M_EXIT]              = "saída",
    [M_TOO_MANY_ARGS]     = "argumentos demais",
    [M_SYNTAX_REDIR]      = "erro de sintaxe no redirecionamento",
    [M_HELP_HEADLINE]     = "Digite comandos normalmente, ou apenas diga o que quer:",
    [M_HELP_REGULAR]      = "um comando comum",
    [M_HELP_NATURAL]      = "com suas próprias palavras",
    [M_HELP_QUESTION]     = "uma pergunta",
    [M_HELP_PREFIX]       = "Prefixe com ! para forçar um comando, ? para forçar a IA.",
    [M_HELP_BUILTINS]     = "Internos:",
    [M_HELP_META]         = "Meta:",
    [M_HELP_ASK]          = "Pergunte com suas próprias palavras",
    [M_HELP_ANSWERED]     = "isto é respondido diretamente",
    [M_HELP_THE_TIME]     = "a hora",
    [M_HELP_THE_DATE]     = "a data",
    [M_HELP_IN_BROWSER]   = "no seu navegador",
    [M_HELP_NO_BROWSER]   = "(nenhum navegador instalado)",
    [M_HELP_NONE]         = "(nenhum instalado)",
    [M_HELP_NO_PLAYER]    = "(nenhum player instalado — sudo pacman -S mpv)",
    [M_HELP_ALARM]        = "o chibi acorda você",
    [M_HELP_PACKAGES]     = "Pacotes",
    [M_HELP_ARCH_SYNTAX]  = "sintaxe do Arch, para você não precisar decorar",
    [M_HELP_EVERYDAY]     = "Comandos do dia a dia, com suas próprias palavras",
    [M_HELP_ELSEWHERE]    = "Todo o resto vai para o synapd, que responde por ESTA máquina.",
    [M_HELP_DESTRUCTIVE]  = "O que destrói fica lá de propósito: ele mostra o comando\n  e espera, em vez de adivinhar sobre os seus arquivos.",
    [M_HELP_LANGUAGES]    = "Entendido em:",
    [M_STATUS_ONLINE]     = "online",
    [M_STATUS_OFFLINE]    = "offline",
    [M_STATUS_ENABLED]    = "ativado",
    [M_STATUS_DISABLED]   = "desativado",
    [M_STATUS_ON]         = "ligado",
    [M_STATUS_OFF]        = "desligado",
    [M_STAT_COMMANDS]     = "Comandos executados",
    [M_STAT_NL]           = "Pedidos em linguagem natural",
    [M_STAT_ASSISTS]      = "Ajudas da IA",
    [M_LANG_IS]           = "Idioma:",
    [M_LANG_UNKNOWN]      = "idioma desconhecido — tente:",
    [M_SYNAPD_OFFLINE]    = "syn: synapd offline",
    [M_CD_NO_OLDPWD]      = "cd: OLDPWD não está definido",
    [M_NO_JOBS]           = "Não há tarefas em segundo plano.",
    [M_JOB_RUNNING]       = "Em execução",
    [M_JOB_STOPPED]       = "Parado",
    [M_JOB_DONE]          = "Concluído",
    [M_LABEL_SOCKET]      = "socket",
    [M_LABEL_AI]          = "IA",
    [M_LABEL_EXPLAIN]     = "explicar",
    [M_LABEL_SAFE]        = "modo seguro",
    [M_SET_AI]            = "Assistência de IA:",
    [M_SET_EXPLAIN]       = "Modo explicação:",
    [M_SET_SAFE]          = "Modo seguro:",
    [M_UNKNOWN_SUBCMD]    = "syn: subcomando desconhecido '%s'. Escreva 'syn' para a ajuda.\n",
    [M_ALIAS_NOT_FOUND]   = "alias: %s: não encontrado\n",
    [M_ALIAS_BAD_NAME]    = "alias: nome de alias inválido\n",
    [M_ALIAS_FULL]        = "alias: tabela cheia (máx. 128)\n",
    [M_UNALIAS_USAGE]     = "unalias: uso: unalias nome [nome …]\n",
    [M_UNALIAS_NOT_FOUND] = "unalias: %s: não encontrado\n",
    [M_TOO_MANY_REDIR]    = "synsh: demasiadas redireções",
    [M_UNTERMINATED]      = "synsh: %s por fechar\n",
    [M_NO_TERMINAL]       = "  synsh: não há terminal instalado para executar: %s\n         sudo pacman -S syntty\n",
    [M_OPENING_TERM]      = "  a abrir %s\n",
    [M_NO_BROWSER]        = "  synsh: não há navegador nem xdg-open instalados\n         sudo pacman -S firefox\n",
    [M_OPENING_URL]       = "  %sa abrir%s %s\n",
    [M_OPENING_SYNFILES]  = "  %sa abrir%s %s no synfiles\n",
    [M_NO_FILEMANAGER]    = "  synsh: não há gestor de ficheiros instalado\n         sudo pacman -S synfiles\n",
    [M_OPENING_IN]        = "  %sa abrir%s %s no %s\n",
    [M_RESUMING_CLIAMP]   = "  %sa retomar o cliamp%s\n",
    [M_STARTING_CLIAMP]   = "  %sa iniciar o cliamp%s\n",
    [M_NO_MUSIC_DIR]      = "  synsh: não há pasta de música (%s)\n",
    [M_PLAYING_WITH]      = "  %sa reproduzir%s %s com %s\n",
    [M_ALARM_WHAT_TIME]   = "  synsh: a que horas? p. ex. \"set alarm for 7:30am\"\n",
    [M_CANNOT_WRITE]      = "  synsh: não é possível escrever %s: %s\n",
    [M_CANNOT_REPLACE]    = "  synsh: não é possível substituir %s: %s\n",
    [M_ALARM_SET]         = "  %salarme definido para as %s%s\n",
    [M_CHIBI_NOT_RUNNING] = "  %s(a chibi não está a correr — é ela que o faz tocar, portanto inicie-a antes)%s\n",
    [M_INIT_FAILED]       = "synsh: falha na inicialização",
    [M_CONNECTED_SHORT]   = "synsh: ligado ao synapd",
    [M_TAGLINE]           = "Onde o núcleo pensa",
    [M_NO_ORPHANS]        = "  não há pacotes órfãos",
};

static const char *const MSG_IT[M_COUNT] = {
    [M_AI_ONLINE]         = "IA in linea",
    [M_AI_OFFLINE]        = "IA non in linea",
    [M_TYPE_NATURALLY]    = "di' quello che vuoi, oppure usa i comandi della shell",
    [M_SHELL_ONLY]        = "solo shell",
    [M_CONNECTED]         = "synsh: connesso a synapd — IA in linea",
    [M_NOT_CONNECTED]     = "synsh: synapd non raggiungibile — solo shell",
    [M_AI_UNAVAILABLE]    = "synsh: attenzione — synapd non disponibile, funzioni IA disattivate",
    [M_AI_FAILED]         = "synsh: traduzione dell'IA fallita",
    [M_ASKING_AI]         = "comando fallito, chiedo all'IA…",
    [M_RUN_CONFIRM]       = "Eseguire?",
    [M_CANCELLED]         = "Annullato.",
    [M_EDIT_IN_SHELL]     = "Modifica nella shell:",
    [M_NO_SHELL_EQUIV]    = "Nessun comando di shell corrispondente.",
    [M_NOT_INSTALLED]     = "non è installato",
    [M_EXIT]              = "uscita",
    [M_TOO_MANY_ARGS]     = "troppi argomenti",
    [M_SYNTAX_REDIR]      = "errore di sintassi nella redirezione",
    [M_HELP_HEADLINE]     = "Scrivi i comandi come sempre, o di' semplicemente cosa vuoi:",
    [M_HELP_REGULAR]      = "un comando normale",
    [M_HELP_NATURAL]      = "con parole tue",
    [M_HELP_QUESTION]     = "una domanda",
    [M_HELP_PREFIX]       = "Anteponi ! per forzare un comando, ? per forzare l'IA.",
    [M_HELP_BUILTINS]     = "Integrati:",
    [M_HELP_META]         = "Meta:",
    [M_HELP_ASK]          = "Chiedi con parole tue",
    [M_HELP_ANSWERED]     = "a questo si risponde direttamente",
    [M_HELP_THE_TIME]     = "l'ora",
    [M_HELP_THE_DATE]     = "la data",
    [M_HELP_IN_BROWSER]   = "nel browser",
    [M_HELP_NO_BROWSER]   = "(nessun browser installato)",
    [M_HELP_NONE]         = "(nessuno installato)",
    [M_HELP_NO_PLAYER]    = "(nessun lettore installato — sudo pacman -S mpv)",
    [M_HELP_ALARM]        = "chibi ti sveglia",
    [M_HELP_PACKAGES]     = "Pacchetti",
    [M_HELP_ARCH_SYNTAX]  = "sintassi di Arch, così non devi ricordartela",
    [M_HELP_EVERYDAY]     = "Comandi di tutti i giorni, con parole tue",
    [M_HELP_ELSEWHERE]    = "Tutto il resto va a synapd, che risponde per QUESTA macchina.",
    [M_HELP_DESTRUCTIVE]  = "Le cose distruttive restano lì apposta: ti mostra il comando\n  e aspetta, invece di tirare a indovinare sui tuoi file.",
    [M_HELP_LANGUAGES]    = "Si capisce in:",
    [M_STATUS_ONLINE]     = "in linea",
    [M_STATUS_OFFLINE]    = "non in linea",
    [M_STATUS_ENABLED]    = "attivo",
    [M_STATUS_DISABLED]   = "disattivo",
    [M_STATUS_ON]         = "attivo",
    [M_STATUS_OFF]        = "disattivo",
    [M_STAT_COMMANDS]     = "Comandi eseguiti",
    [M_STAT_NL]           = "Richieste in linguaggio naturale",
    [M_STAT_ASSISTS]      = "Aiuti dell'IA",
    [M_LANG_IS]           = "Lingua:",
    [M_LANG_UNKNOWN]      = "lingua sconosciuta — prova con:",
    [M_SYNAPD_OFFLINE]    = "syn: synapd non in linea",
    [M_CD_NO_OLDPWD]      = "cd: OLDPWD non è impostato",
    [M_NO_JOBS]           = "Nessun processo in background.",
    [M_JOB_RUNNING]       = "In esecuzione",
    [M_JOB_STOPPED]       = "Fermato",
    [M_JOB_DONE]          = "Completato",
    [M_LABEL_SOCKET]      = "socket",
    [M_LABEL_AI]          = "IA",
    [M_LABEL_EXPLAIN]     = "spiega",
    [M_LABEL_SAFE]        = "modalità sicura",
    [M_SET_AI]            = "Assistenza IA:",
    [M_SET_EXPLAIN]       = "Modalità spiegazione:",
    [M_SET_SAFE]          = "Modalità sicura:",
    [M_UNKNOWN_SUBCMD]    = "syn: sottocomando sconosciuto '%s'. Digita 'syn' per l'aiuto.\n",
    [M_ALIAS_NOT_FOUND]   = "alias: %s: non trovato\n",
    [M_ALIAS_BAD_NAME]    = "alias: nome alias non valido\n",
    [M_ALIAS_FULL]        = "alias: tabella piena (max 128)\n",
    [M_UNALIAS_USAGE]     = "unalias: uso: unalias nome [nome …]\n",
    [M_UNALIAS_NOT_FOUND] = "unalias: %s: non trovato\n",
    [M_TOO_MANY_REDIR]    = "synsh: troppe ridirezioni",
    [M_UNTERMINATED]      = "synsh: %s non chiuso\n",
    [M_NO_TERMINAL]       = "  synsh: nessun terminale installato per eseguire: %s\n         sudo pacman -S syntty\n",
    [M_OPENING_TERM]      = "  apertura di %s\n",
    [M_NO_BROWSER]        = "  synsh: nessun browser e nessun xdg-open installati\n         sudo pacman -S firefox\n",
    [M_OPENING_URL]       = "  %sapertura di%s %s\n",
    [M_OPENING_SYNFILES]  = "  %sapertura di%s %s in synfiles\n",
    [M_NO_FILEMANAGER]    = "  synsh: nessun gestore di file installato\n         sudo pacman -S synfiles\n",
    [M_OPENING_IN]        = "  %sapertura di%s %s in %s\n",
    [M_RESUMING_CLIAMP]   = "  %sripresa di cliamp%s\n",
    [M_STARTING_CLIAMP]   = "  %savvio di cliamp%s\n",
    [M_NO_MUSIC_DIR]      = "  synsh: nessuna cartella di musica (%s)\n",
    [M_PLAYING_WITH]      = "  %sriproduzione di%s %s con %s\n",
    [M_ALARM_WHAT_TIME]   = "  synsh: a che ora? es. \"set alarm for 7:30am\"\n",
    [M_CANNOT_WRITE]      = "  synsh: impossibile scrivere %s: %s\n",
    [M_CANNOT_REPLACE]    = "  synsh: impossibile sostituire %s: %s\n",
    [M_ALARM_SET]         = "  %ssveglia impostata per le %s%s\n",
    [M_CHIBI_NOT_RUNNING] = "  %s(chibi non è in esecuzione — è lei a farla suonare, quindi avviala prima)%s\n",
    [M_INIT_FAILED]       = "synsh: inizializzazione fallita",
    [M_CONNECTED_SHORT]   = "synsh: connesso a synapd",
    [M_TAGLINE]           = "Dove pensa il kernel",
    [M_NO_ORPHANS]        = "  nessun pacchetto orfano",
};

static const char *const MSG_NL[M_COUNT] = {
    [M_AI_ONLINE]         = "AI online",
    [M_AI_OFFLINE]        = "AI offline",
    [M_TYPE_NATURALLY]    = "zeg gewoon wat je wilt, of gebruik shell-opdrachten",
    [M_SHELL_ONLY]        = "alleen-shell-modus",
    [M_CONNECTED]         = "synsh: verbonden met synapd — AI online",
    [M_NOT_CONNECTED]     = "synsh: geen verbinding met synapd — alleen-shell-modus",
    [M_AI_UNAVAILABLE]    = "synsh: waarschuwing — synapd niet beschikbaar, AI-functies uit",
    [M_AI_FAILED]         = "synsh: vertaling door de AI mislukt",
    [M_ASKING_AI]         = "opdracht mislukt, ik vraag het de AI…",
    [M_RUN_CONFIRM]       = "Uitvoeren?",
    [M_CANCELLED]         = "Geannuleerd.",
    [M_EDIT_IN_SHELL]     = "Bewerk in de shell:",
    [M_NO_SHELL_EQUIV]    = "Geen passende shell-opdracht gevonden.",
    [M_NOT_INSTALLED]     = "is niet geïnstalleerd",
    [M_EXIT]              = "einde",
    [M_TOO_MANY_ARGS]     = "te veel argumenten",
    [M_SYNTAX_REDIR]      = "syntaxfout bij de omleiding",
    [M_HELP_HEADLINE]     = "Typ opdrachten zoals altijd, of zeg gewoon wat je wilt:",
    [M_HELP_REGULAR]      = "een gewone opdracht",
    [M_HELP_NATURAL]      = "in je eigen woorden",
    [M_HELP_QUESTION]     = "een vraag",
    [M_HELP_PREFIX]       = "Zet er ! voor om een opdracht af te dwingen, ? voor de AI.",
    [M_HELP_BUILTINS]     = "Ingebouwd:",
    [M_HELP_META]         = "Meta:",
    [M_HELP_ASK]          = "Vraag het in je eigen woorden",
    [M_HELP_ANSWERED]     = "dit wordt rechtstreeks beantwoord",
    [M_HELP_THE_TIME]     = "de tijd",
    [M_HELP_THE_DATE]     = "de datum",
    [M_HELP_IN_BROWSER]   = "in je browser",
    [M_HELP_NO_BROWSER]   = "(geen browser geïnstalleerd)",
    [M_HELP_NONE]         = "(niets geïnstalleerd)",
    [M_HELP_NO_PLAYER]    = "(geen speler geïnstalleerd — sudo pacman -S mpv)",
    [M_HELP_ALARM]        = "chibi maakt je wakker",
    [M_HELP_PACKAGES]     = "Pakketten",
    [M_HELP_ARCH_SYNTAX]  = "Arch-syntaxis, zodat jij die niet hoeft te onthouden",
    [M_HELP_EVERYDAY]     = "Dagelijkse opdrachten, in je eigen woorden",
    [M_HELP_ELSEWHERE]    = "Al het andere gaat naar synapd, dat voor DEZE machine antwoordt.",
    [M_HELP_DESTRUCTIVE]  = "Destructieve dingen blijven daar met opzet: het toont je de\n  opdracht en wacht, in plaats van naar je bestanden te gissen.",
    [M_HELP_LANGUAGES]    = "Begrepen in:",
    [M_STATUS_ONLINE]     = "online",
    [M_STATUS_OFFLINE]    = "offline",
    [M_STATUS_ENABLED]    = "aan",
    [M_STATUS_DISABLED]   = "uit",
    [M_STATUS_ON]         = "aan",
    [M_STATUS_OFF]        = "uit",
    [M_STAT_COMMANDS]     = "Uitgevoerde opdrachten",
    [M_STAT_NL]           = "Vragen in gewone taal",
    [M_STAT_ASSISTS]      = "Hulp van de AI",
    [M_LANG_IS]           = "Taal:",
    [M_LANG_UNKNOWN]      = "onbekende taal — probeer:",
    [M_SYNAPD_OFFLINE]    = "syn: synapd offline",
    [M_CD_NO_OLDPWD]      = "cd: OLDPWD is niet ingesteld",
    [M_NO_JOBS]           = "Geen achtergrondtaken.",
    [M_JOB_RUNNING]       = "Actief",
    [M_JOB_STOPPED]       = "Gestopt",
    [M_JOB_DONE]          = "Klaar",
    [M_LABEL_SOCKET]      = "socket",
    [M_LABEL_AI]          = "AI",
    [M_LABEL_EXPLAIN]     = "uitleggen",
    [M_LABEL_SAFE]        = "veilige modus",
    [M_SET_AI]            = "AI-ondersteuning:",
    [M_SET_EXPLAIN]       = "Uitlegmodus:",
    [M_SET_SAFE]          = "Veilige modus:",
    [M_UNKNOWN_SUBCMD]    = "syn: onbekende subopdracht '%s'. Typ 'syn' voor hulp.\n",
    [M_ALIAS_NOT_FOUND]   = "alias: %s: niet gevonden\n",
    [M_ALIAS_BAD_NAME]    = "alias: ongeldige aliasnaam\n",
    [M_ALIAS_FULL]        = "alias: tabel vol (max 128)\n",
    [M_UNALIAS_USAGE]     = "unalias: gebruik: unalias naam [naam …]\n",
    [M_UNALIAS_NOT_FOUND] = "unalias: %s: niet gevonden\n",
    [M_TOO_MANY_REDIR]    = "synsh: te veel omleidingen",
    [M_UNTERMINATED]      = "synsh: niet afgesloten %s\n",
    [M_NO_TERMINAL]       = "  synsh: geen terminal geïnstalleerd om dit te draaien: %s\n         sudo pacman -S syntty\n",
    [M_OPENING_TERM]      = "  %s wordt geopend\n",
    [M_NO_BROWSER]        = "  synsh: geen browser en geen xdg-open geïnstalleerd\n         sudo pacman -S firefox\n",
    [M_OPENING_URL]       = "  %sopenen%s %s\n",
    [M_OPENING_SYNFILES]  = "  %sopenen%s %s in synfiles\n",
    [M_NO_FILEMANAGER]    = "  synsh: geen bestandsbeheerder geïnstalleerd\n         sudo pacman -S synfiles\n",
    [M_OPENING_IN]        = "  %sopenen%s %s in %s\n",
    [M_RESUMING_CLIAMP]   = "  %scliamp wordt hervat%s\n",
    [M_STARTING_CLIAMP]   = "  %scliamp wordt gestart%s\n",
    [M_NO_MUSIC_DIR]      = "  synsh: geen muziekmap (%s)\n",
    [M_PLAYING_WITH]      = "  %safspelen%s %s met %s\n",
    [M_ALARM_WHAT_TIME]   = "  synsh: hoe laat? bijv. \"set alarm for 7:30am\"\n",
    [M_CANNOT_WRITE]      = "  synsh: kan %s niet schrijven: %s\n",
    [M_CANNOT_REPLACE]    = "  synsh: kan %s niet vervangen: %s\n",
    [M_ALARM_SET]         = "  %swekker gezet op %s%s\n",
    [M_CHIBI_NOT_RUNNING] = "  %s(chibi draait niet — zij laat hem afgaan, dus start haar eerst)%s\n",
    [M_INIT_FAILED]       = "synsh: initialisatie mislukt",
    [M_CONNECTED_SHORT]   = "synsh: verbonden met synapd",
    [M_TAGLINE]           = "Waar de kernel denkt",
    [M_NO_ORPHANS]        = "  geen verweesde pakketten",
};

static const char *const MSG_PL[M_COUNT] = {
    [M_AI_ONLINE]         = "SI online",
    [M_AI_OFFLINE]        = "SI offline",
    [M_TYPE_NATURALLY]    = "powiedz, czego chcesz, albo użyj poleceń powłoki",
    [M_SHELL_ONLY]        = "tryb tylko powłoki",
    [M_CONNECTED]         = "synsh: połączono z synapd — SI online",
    [M_NOT_CONNECTED]     = "synsh: brak połączenia z synapd — tryb tylko powłoki",
    [M_AI_UNAVAILABLE]    = "synsh: uwaga — synapd niedostępny, funkcje SI wyłączone",
    [M_AI_FAILED]         = "synsh: tłumaczenie SI nie powiodło się",
    [M_ASKING_AI]         = "polecenie nie zadziałało, pytam SI…",
    [M_RUN_CONFIRM]       = "Uruchomić?",
    [M_CANCELLED]         = "Anulowano.",
    [M_EDIT_IN_SHELL]     = "Edytuj w powłoce:",
    [M_NO_SHELL_EQUIV]    = "Brak odpowiadającego polecenia powłoki.",
    [M_NOT_INSTALLED]     = "nie jest zainstalowany",
    [M_EXIT]              = "wyjście",
    [M_TOO_MANY_ARGS]     = "za dużo argumentów",
    [M_SYNTAX_REDIR]      = "błąd składni przy przekierowaniu",
    [M_HELP_HEADLINE]     = "Wpisuj polecenia jak zwykle albo po prostu powiedz, czego chcesz:",
    [M_HELP_REGULAR]      = "zwykłe polecenie",
    [M_HELP_NATURAL]      = "własnymi słowami",
    [M_HELP_QUESTION]     = "pytanie",
    [M_HELP_PREFIX]       = "! wymusza polecenie, ? wymusza SI.",
    [M_HELP_BUILTINS]     = "Wbudowane:",
    [M_HELP_META]         = "Meta:",
    [M_HELP_ASK]          = "Zapytaj własnymi słowami",
    [M_HELP_ANSWERED]     = "na to odpowiadamy od razu",
    [M_HELP_THE_TIME]     = "godzina",
    [M_HELP_THE_DATE]     = "data",
    [M_HELP_IN_BROWSER]   = "w przeglądarce",
    [M_HELP_NO_BROWSER]   = "(brak przeglądarki)",
    [M_HELP_NONE]         = "(nic nie zainstalowano)",
    [M_HELP_NO_PLAYER]    = "(brak odtwarzacza — sudo pacman -S mpv)",
    [M_HELP_ALARM]        = "chibi cię obudzi",
    [M_HELP_PACKAGES]     = "Pakiety",
    [M_HELP_ARCH_SYNTAX]  = "składnia Archa, żebyś nie musiał jej pamiętać",
    [M_HELP_EVERYDAY]     = "Codzienne polecenia, własnymi słowami",
    [M_HELP_ELSEWHERE]    = "Reszta trafia do synapd, który odpowiada za TĘ maszynę.",
    [M_HELP_DESTRUCTIVE]  = "Rzeczy niszczące zostają tam celowo: pokazuje polecenie\n  i czeka, zamiast zgadywać przy twoich plikach.",
    [M_HELP_LANGUAGES]    = "Rozumiane języki:",
    [M_STATUS_ONLINE]     = "online",
    [M_STATUS_OFFLINE]    = "offline",
    [M_STATUS_ENABLED]    = "włączone",
    [M_STATUS_DISABLED]   = "wyłączone",
    [M_STATUS_ON]         = "wł.",
    [M_STATUS_OFF]        = "wył.",
    [M_STAT_COMMANDS]     = "Wykonane polecenia",
    [M_STAT_NL]           = "Zapytania w języku naturalnym",
    [M_STAT_ASSISTS]      = "Pomoc SI",
    [M_LANG_IS]           = "Język:",
    [M_LANG_UNKNOWN]      = "nieznany język — spróbuj:",
    [M_SYNAPD_OFFLINE]    = "syn: synapd offline",
    [M_CD_NO_OLDPWD]      = "cd: OLDPWD nie jest ustawione",
    [M_NO_JOBS]           = "Brak zadań w tle.",
    [M_JOB_RUNNING]       = "Działa",
    [M_JOB_STOPPED]       = "Zatrzymane",
    [M_JOB_DONE]          = "Zakończone",
    [M_LABEL_SOCKET]      = "gniazdo",
    [M_LABEL_AI]          = "SI",
    [M_LABEL_EXPLAIN]     = "wyjaśnianie",
    [M_LABEL_SAFE]        = "tryb bezpieczny",
    [M_SET_AI]            = "Wsparcie SI:",
    [M_SET_EXPLAIN]       = "Tryb wyjaśniania:",
    [M_SET_SAFE]          = "Tryb bezpieczny:",
    [M_UNKNOWN_SUBCMD]    = "syn: nieznane podpolecenie '%s'. Wpisz 'syn', aby uzyskać pomoc.\n",
    [M_ALIAS_NOT_FOUND]   = "alias: %s: nie znaleziono\n",
    [M_ALIAS_BAD_NAME]    = "alias: nieprawidłowa nazwa aliasu\n",
    [M_ALIAS_FULL]        = "alias: tablica pełna (maks. 128)\n",
    [M_UNALIAS_USAGE]     = "unalias: użycie: unalias nazwa [nazwa …]\n",
    [M_UNALIAS_NOT_FOUND] = "unalias: %s: nie znaleziono\n",
    [M_TOO_MANY_REDIR]    = "synsh: za dużo przekierowań",
    [M_UNTERMINATED]      = "synsh: niezamknięty %s\n",
    [M_NO_TERMINAL]       = "  synsh: brak zainstalowanego terminala, aby uruchomić: %s\n         sudo pacman -S syntty\n",
    [M_OPENING_TERM]      = "  otwieram %s\n",
    [M_NO_BROWSER]        = "  synsh: brak przeglądarki i xdg-open\n         sudo pacman -S firefox\n",
    [M_OPENING_URL]       = "  %sotwieram%s %s\n",
    [M_OPENING_SYNFILES]  = "  %sotwieram%s %s w synfiles\n",
    [M_NO_FILEMANAGER]    = "  synsh: brak zainstalowanego menedżera plików\n         sudo pacman -S synfiles\n",
    [M_OPENING_IN]        = "  %sotwieram%s %s w %s\n",
    [M_RESUMING_CLIAMP]   = "  %swznawiam cliamp%s\n",
    [M_STARTING_CLIAMP]   = "  %suruchamiam cliamp%s\n",
    [M_NO_MUSIC_DIR]      = "  synsh: brak katalogu z muzyką (%s)\n",
    [M_PLAYING_WITH]      = "  %sodtwarzam%s %s w %s\n",
    [M_ALARM_WHAT_TIME]   = "  synsh: o której? np. \"set alarm for 7:30am\"\n",
    [M_CANNOT_WRITE]      = "  synsh: nie można zapisać %s: %s\n",
    [M_CANNOT_REPLACE]    = "  synsh: nie można zastąpić %s: %s\n",
    [M_ALARM_SET]         = "  %sbudzik ustawiony na %s%s\n",
    [M_CHIBI_NOT_RUNNING] = "  %s(chibi nie działa — to ona go uruchamia, więc włącz ją wcześniej)%s\n",
    [M_INIT_FAILED]       = "synsh: inicjalizacja nie powiodła się",
    [M_CONNECTED_SHORT]   = "synsh: połączono z synapd",
    [M_TAGLINE]           = "Tam, gdzie myśli jądro",
    [M_NO_ORPHANS]        = "  brak osieroconych pakietów",
};

static const char *const MSG_RU[M_COUNT] = {
    [M_AI_ONLINE]         = "ИИ на связи",
    [M_AI_OFFLINE]        = "ИИ недоступен",
    [M_TYPE_NATURALLY]    = "просто скажите, что нужно, или вводите команды",
    [M_SHELL_ONLY]        = "режим обычной оболочки",
    [M_CONNECTED]         = "synsh: соединение с synapd установлено — ИИ на связи",
    [M_NOT_CONNECTED]     = "synsh: synapd не отвечает — режим обычной оболочки",
    [M_AI_UNAVAILABLE]    = "synsh: предупреждение — synapd недоступен, функции ИИ отключены",
    [M_AI_FAILED]         = "synsh: не удалось перевести запрос",
    [M_ASKING_AI]         = "команда не сработала, спрашиваю ИИ…",
    [M_RUN_CONFIRM]       = "Выполнить?",
    [M_CANCELLED]         = "Отменено.",
    [M_EDIT_IN_SHELL]     = "Отредактируйте в оболочке:",
    [M_NO_SHELL_EQUIV]    = "Подходящей команды не нашлось.",
    [M_NOT_INSTALLED]     = "не установлен",
    [M_EXIT]              = "выход",
    [M_TOO_MANY_ARGS]     = "слишком много аргументов",
    [M_SYNTAX_REDIR]      = "синтаксическая ошибка в перенаправлении",
    [M_HELP_HEADLINE]     = "Вводите команды как обычно — или просто скажите, что нужно:",
    [M_HELP_REGULAR]      = "обычная команда",
    [M_HELP_NATURAL]      = "своими словами",
    [M_HELP_QUESTION]     = "вопрос",
    [M_HELP_PREFIX]       = "! — выполнить как команду, ? — отправить ИИ.",
    [M_HELP_BUILTINS]     = "Встроенные:",
    [M_HELP_META]         = "Мета:",
    [M_HELP_ASK]          = "Спросите своими словами",
    [M_HELP_ANSWERED]     = "на это отвечаем сразу",
    [M_HELP_THE_TIME]     = "текущее время",
    [M_HELP_THE_DATE]     = "сегодняшняя дата",
    [M_HELP_IN_BROWSER]   = "в браузере",
    [M_HELP_NO_BROWSER]   = "(браузер не установлен)",
    [M_HELP_NONE]         = "(ничего не установлено)",
    [M_HELP_NO_PLAYER]    = "(плеер не установлен — sudo pacman -S mpv)",
    [M_HELP_ALARM]        = "chibi разбудит",
    [M_HELP_PACKAGES]     = "Пакеты",
    [M_HELP_ARCH_SYNTAX]  = "синтаксис Arch, чтобы его не запоминать",
    [M_HELP_EVERYDAY]     = "Повседневные команды, своими словами",
    [M_HELP_ELSEWHERE]    = "Всё остальное уходит в synapd, который отвечает за ЭТУ машину.",
    [M_HELP_DESTRUCTIVE]  = "Разрушительное остаётся там намеренно: команда показывается\n  и ждёт, вместо того чтобы гадать о ваших файлах.",
    [M_HELP_LANGUAGES]    = "Понимает языки:",
    [M_STATUS_ONLINE]     = "на связи",
    [M_STATUS_OFFLINE]    = "недоступен",
    [M_STATUS_ENABLED]    = "включено",
    [M_STATUS_DISABLED]   = "выключено",
    [M_STATUS_ON]         = "вкл.",
    [M_STATUS_OFF]        = "выкл.",
    [M_STAT_COMMANDS]     = "Выполнено команд",
    [M_STAT_NL]           = "Запросов обычными словами",
    [M_STAT_ASSISTS]      = "Подсказок ИИ",
    [M_LANG_IS]           = "Язык:",
    [M_LANG_UNKNOWN]      = "неизвестный язык — попробуйте:",
    [M_SYNAPD_OFFLINE]    = "syn: synapd недоступен",
    [M_CD_NO_OLDPWD]      = "cd: OLDPWD не задан",
    [M_NO_JOBS]           = "Фоновых задач нет.",
    [M_JOB_RUNNING]       = "Работает",
    [M_JOB_STOPPED]       = "Остановлено",
    [M_JOB_DONE]          = "Завершено",
    [M_LABEL_SOCKET]      = "сокет",
    [M_LABEL_AI]          = "ИИ",
    [M_LABEL_EXPLAIN]     = "пояснение",
    [M_LABEL_SAFE]        = "безопасный режим",
    [M_SET_AI]            = "Помощь ИИ:",
    [M_SET_EXPLAIN]       = "Режим пояснений:",
    [M_SET_SAFE]          = "Безопасный режим:",
    [M_UNKNOWN_SUBCMD]    = "syn: неизвестная подкоманда «%s». Наберите «syn» для справки.\n",
    [M_ALIAS_NOT_FOUND]   = "alias: %s: не найден\n",
    [M_ALIAS_BAD_NAME]    = "alias: недопустимое имя псевдонима\n",
    [M_ALIAS_FULL]        = "alias: таблица заполнена (макс. 128)\n",
    [M_UNALIAS_USAGE]     = "unalias: использование: unalias имя [имя …]\n",
    [M_UNALIAS_NOT_FOUND] = "unalias: %s: не найден\n",
    [M_TOO_MANY_REDIR]    = "synsh: слишком много перенаправлений",
    [M_UNTERMINATED]      = "synsh: незакрытая %s\n",
    [M_NO_TERMINAL]       = "  synsh: не установлен терминал для запуска: %s\n         sudo pacman -S syntty\n",
    [M_OPENING_TERM]      = "  открываю %s\n",
    [M_NO_BROWSER]        = "  synsh: не установлены ни браузер, ни xdg-open\n         sudo pacman -S firefox\n",
    [M_OPENING_URL]       = "  %sоткрываю%s %s\n",
    [M_OPENING_SYNFILES]  = "  %sоткрываю%s %s в synfiles\n",
    [M_NO_FILEMANAGER]    = "  synsh: файловый менеджер не установлен\n         sudo pacman -S synfiles\n",
    [M_OPENING_IN]        = "  %sоткрываю%s %s в %s\n",
    [M_RESUMING_CLIAMP]   = "  %sпродолжаю cliamp%s\n",
    [M_STARTING_CLIAMP]   = "  %sзапускаю cliamp%s\n",
    [M_NO_MUSIC_DIR]      = "  synsh: нет каталога с музыкой (%s)\n",
    [M_PLAYING_WITH]      = "  %sвоспроизвожу%s %s через %s\n",
    [M_ALARM_WHAT_TIME]   = "  synsh: во сколько? например \"set alarm for 7:30am\"\n",
    [M_CANNOT_WRITE]      = "  synsh: не удаётся записать %s: %s\n",
    [M_CANNOT_REPLACE]    = "  synsh: не удаётся заменить %s: %s\n",
    [M_ALARM_SET]         = "  %sбудильник на %s%s\n",
    [M_CHIBI_NOT_RUNNING] = "  %s(chibi не запущена — звонит именно она, так что запустите её заранее)%s\n",
    [M_INIT_FAILED]       = "synsh: не удалась инициализация",
    [M_CONNECTED_SHORT]   = "synsh: подключено к synapd",
    [M_TAGLINE]           = "Где думает ядро",
    [M_NO_ORPHANS]        = "  осиротевших пакетов нет",
};

static const char *const MSG_JA[M_COUNT] = {
    [M_AI_ONLINE]         = "AI 接続中",
    [M_AI_OFFLINE]        = "AI 未接続",
    [M_TYPE_NATURALLY]    = "やりたいことを普通に書くか、シェルコマンドをどうぞ",
    [M_SHELL_ONLY]        = "シェルのみのモード",
    [M_CONNECTED]         = "synsh: synapd に接続しました — AI 接続中",
    [M_NOT_CONNECTED]     = "synsh: synapd に接続できません — シェルのみのモード",
    [M_AI_UNAVAILABLE]    = "synsh: 警告 — synapd が使えないため AI 機能は無効です",
    [M_AI_FAILED]         = "synsh: AI による変換に失敗しました",
    [M_ASKING_AI]         = "コマンドが失敗しました。AI に聞いています…",
    [M_RUN_CONFIRM]       = "実行しますか?",
    [M_CANCELLED]         = "取り消しました。",
    [M_EDIT_IN_SHELL]     = "シェルで編集:",
    [M_NO_SHELL_EQUIV]    = "対応するシェルコマンドはありません。",
    [M_NOT_INSTALLED]     = "はインストールされていません",
    [M_EXIT]              = "終了",
    [M_TOO_MANY_ARGS]     = "引数が多すぎます",
    [M_SYNTAX_REDIR]      = "リダイレクトの構文エラー",
    [M_HELP_HEADLINE]     = "いつも通りコマンドを打つか、やりたいことをそのまま書いてください:",
    [M_HELP_REGULAR]      = "普通のコマンド",
    [M_HELP_NATURAL]      = "自分のことばで",
    [M_HELP_QUESTION]     = "質問",
    [M_HELP_PREFIX]       = "! を付けるとコマンド、? を付けると AI に送ります。",
    [M_HELP_BUILTINS]     = "組み込み:",
    [M_HELP_META]         = "メタ:",
    [M_HELP_ASK]          = "自分のことばで聞いてください",
    [M_HELP_ANSWERED]     = "これは直接答えます",
    [M_HELP_THE_TIME]     = "現在の時刻",
    [M_HELP_THE_DATE]     = "今日の日付",
    [M_HELP_IN_BROWSER]   = "ブラウザで開く",
    [M_HELP_NO_BROWSER]   = "(ブラウザ未インストール)",
    [M_HELP_NONE]         = "(未インストール)",
    [M_HELP_NO_PLAYER]    = "(プレーヤー未インストール — sudo pacman -S mpv)",
    [M_HELP_ALARM]        = "chibi が鳴らします",
    [M_HELP_PACKAGES]     = "パッケージ",
    [M_HELP_ARCH_SYNTAX]  = "Arch の書き方は覚えなくて大丈夫",
    [M_HELP_EVERYDAY]     = "毎日使うコマンドを、自分のことばで",
    [M_HELP_ELSEWHERE]    = "それ以外は synapd が、この機械について答えます。",
    [M_HELP_DESTRUCTIVE]  = "破壊的な操作はあえてそちらに残してあります。コマンドを見せて\n  待つので、ファイルを勝手に推測しません。",
    [M_HELP_LANGUAGES]    = "使える言語:",
    [M_STATUS_ONLINE]     = "接続中",
    [M_STATUS_OFFLINE]    = "未接続",
    [M_STATUS_ENABLED]    = "有効",
    [M_STATUS_DISABLED]   = "無効",
    [M_STATUS_ON]         = "オン",
    [M_STATUS_OFF]        = "オフ",
    [M_STAT_COMMANDS]     = "実行したコマンド",
    [M_STAT_NL]           = "ことばでの依頼",
    [M_STAT_ASSISTS]      = "AI の補助",
    [M_LANG_IS]           = "言語:",
    [M_LANG_UNKNOWN]      = "不明な言語です — 次のいずれかを:",
    [M_SYNAPD_OFFLINE]    = "syn: synapd は未接続です",
    [M_CD_NO_OLDPWD]      = "cd: OLDPWD が設定されていません",
    [M_NO_JOBS]           = "バックグラウンドジョブはありません。",
    [M_JOB_RUNNING]       = "実行中",
    [M_JOB_STOPPED]       = "停止中",
    [M_JOB_DONE]          = "完了",
    [M_LABEL_SOCKET]      = "ソケット",
    [M_LABEL_AI]          = "AI",
    [M_LABEL_EXPLAIN]     = "説明",
    [M_LABEL_SAFE]        = "セーフモード",
    [M_SET_AI]            = "AI 支援:",
    [M_SET_EXPLAIN]       = "説明モード:",
    [M_SET_SAFE]          = "セーフモード:",
    [M_UNKNOWN_SUBCMD]    = "syn: 不明なサブコマンド '%s'。ヘルプは 'syn' で表示できます。\n",
    [M_ALIAS_NOT_FOUND]   = "alias: %s: 見つかりません\n",
    [M_ALIAS_BAD_NAME]    = "alias: 不正なエイリアス名です\n",
    [M_ALIAS_FULL]        = "alias: テーブルがいっぱいです (最大 128)\n",
    [M_UNALIAS_USAGE]     = "unalias: 使い方: unalias 名前 [名前 …]\n",
    [M_UNALIAS_NOT_FOUND] = "unalias: %s: 見つかりません\n",
    [M_TOO_MANY_REDIR]    = "synsh: リダイレクトが多すぎます",
    [M_UNTERMINATED]      = "synsh: %s が閉じられていません\n",
    [M_NO_TERMINAL]       = "  synsh: 実行する端末がインストールされていません: %s\n         sudo pacman -S syntty\n",
    [M_OPENING_TERM]      = "  %s を開いています\n",
    [M_NO_BROWSER]        = "  synsh: ブラウザーも xdg-open もインストールされていません\n         sudo pacman -S firefox\n",
    [M_OPENING_URL]       = "  %s開いています%s %s\n",
    [M_OPENING_SYNFILES]  = "  %s開いています%s %s を synfiles で\n",
    [M_NO_FILEMANAGER]    = "  synsh: ファイルマネージャーがインストールされていません\n         sudo pacman -S synfiles\n",
    [M_OPENING_IN]        = "  %s開いています%s %s を %s で\n",
    [M_RESUMING_CLIAMP]   = "  %scliamp を再開しています%s\n",
    [M_STARTING_CLIAMP]   = "  %scliamp を起動しています%s\n",
    [M_NO_MUSIC_DIR]      = "  synsh: 音楽ディレクトリがありません (%s)\n",
    [M_PLAYING_WITH]      = "  %s再生しています%s %s を %s で\n",
    [M_ALARM_WHAT_TIME]   = "  synsh: 何時ですか? 例: \"set alarm for 7:30am\"\n",
    [M_CANNOT_WRITE]      = "  synsh: %s に書き込めません: %s\n",
    [M_CANNOT_REPLACE]    = "  synsh: %s を置き換えられません: %s\n",
    [M_ALARM_SET]         = "  %s%s にアラームを設定しました%s\n",
    [M_CHIBI_NOT_RUNNING] = "  %s(chibi が動いていません — 鳴らすのは chibi なので、その前に起動してください)%s\n",
    [M_INIT_FAILED]       = "synsh: 初期化に失敗しました",
    [M_CONNECTED_SHORT]   = "synsh: synapd に接続しました",
    [M_TAGLINE]           = "カーネルが考える場所",
    [M_NO_ORPHANS]        = "  孤立したパッケージはありません",
};

static const char *const MSG_ZH[M_COUNT] = {
    [M_AI_ONLINE]         = "AI 已连接",
    [M_AI_OFFLINE]        = "AI 未连接",
    [M_TYPE_NATURALLY]    = "直接说你想做什么，或者输入 shell 命令",
    [M_SHELL_ONLY]        = "仅 shell 模式",
    [M_CONNECTED]         = "synsh: 已连接 synapd — AI 已连接",
    [M_NOT_CONNECTED]     = "synsh: 无法连接 synapd — 仅 shell 模式",
    [M_AI_UNAVAILABLE]    = "synsh: 警告 — synapd 不可用，AI 功能已关闭",
    [M_AI_FAILED]         = "synsh: AI 翻译失败",
    [M_ASKING_AI]         = "命令失败，正在询问 AI…",
    [M_RUN_CONFIRM]       = "要运行吗?",
    [M_CANCELLED]         = "已取消。",
    [M_EDIT_IN_SHELL]     = "在 shell 中编辑:",
    [M_NO_SHELL_EQUIV]    = "没有对应的 shell 命令。",
    [M_NOT_INSTALLED]     = "尚未安装",
    [M_EXIT]              = "退出",
    [M_TOO_MANY_ARGS]     = "参数太多",
    [M_SYNTAX_REDIR]      = "重定向语法错误",
    [M_HELP_HEADLINE]     = "照常输入命令，或者直接说出你想做的事:",
    [M_HELP_REGULAR]      = "普通命令",
    [M_HELP_NATURAL]      = "用你自己的话",
    [M_HELP_QUESTION]     = "一个问题",
    [M_HELP_PREFIX]       = "以 ! 开头强制当作命令，以 ? 开头强制交给 AI。",
    [M_HELP_BUILTINS]     = "内置命令:",
    [M_HELP_META]         = "元命令:",
    [M_HELP_ASK]          = "用你自己的话来问",
    [M_HELP_ANSWERED]     = "这些会直接回答",
    [M_HELP_THE_TIME]     = "现在几点",
    [M_HELP_THE_DATE]     = "今天日期",
    [M_HELP_IN_BROWSER]   = "在浏览器中打开",
    [M_HELP_NO_BROWSER]   = "(未安装浏览器)",
    [M_HELP_NONE]         = "(未安装)",
    [M_HELP_NO_PLAYER]    = "(未安装播放器 — sudo pacman -S mpv)",
    [M_HELP_ALARM]        = "由 chibi 叫醒你",
    [M_HELP_PACKAGES]     = "软件包",
    [M_HELP_ARCH_SYNTAX]  = "Arch 的写法，不用你去记",
    [M_HELP_EVERYDAY]     = "日常命令，用你自己的话",
    [M_HELP_ELSEWHERE]    = "其他的都交给 synapd，它针对这台机器回答。",
    [M_HELP_DESTRUCTIVE]  = "破坏性的操作故意留在那边：它会把命令给你看并等待，\n  而不是对你的文件乱猜。",
    [M_HELP_LANGUAGES]    = "可用语言:",
    [M_STATUS_ONLINE]     = "已连接",
    [M_STATUS_OFFLINE]    = "未连接",
    [M_STATUS_ENABLED]    = "已启用",
    [M_STATUS_DISABLED]   = "已停用",
    [M_STATUS_ON]         = "开",
    [M_STATUS_OFF]        = "关",
    [M_STAT_COMMANDS]     = "执行过的命令",
    [M_STAT_NL]           = "自然语言请求",
    [M_STAT_ASSISTS]      = "AI 协助",
    [M_LANG_IS]           = "语言:",
    [M_LANG_UNKNOWN]      = "未知语言 — 可以试试:",
    [M_SYNAPD_OFFLINE]    = "syn: synapd 未连接",
    [M_CD_NO_OLDPWD]      = "cd: 未设置 OLDPWD",
    [M_NO_JOBS]           = "没有后台作业。",
    [M_JOB_RUNNING]       = "运行中",
    [M_JOB_STOPPED]       = "已停止",
    [M_JOB_DONE]          = "已完成",
    [M_LABEL_SOCKET]      = "套接字",
    [M_LABEL_AI]          = "AI",
    [M_LABEL_EXPLAIN]     = "解释",
    [M_LABEL_SAFE]        = "安全模式",
    [M_SET_AI]            = "AI 辅助:",
    [M_SET_EXPLAIN]       = "解释模式:",
    [M_SET_SAFE]          = "安全模式:",
    [M_UNKNOWN_SUBCMD]    = "syn: 未知子命令 '%s'。输入 'syn' 查看帮助。\n",
    [M_ALIAS_NOT_FOUND]   = "alias: %s: 未找到\n",
    [M_ALIAS_BAD_NAME]    = "alias: 别名无效\n",
    [M_ALIAS_FULL]        = "alias: 表已满（最多 128）\n",
    [M_UNALIAS_USAGE]     = "unalias: 用法: unalias 名称 [名称 …]\n",
    [M_UNALIAS_NOT_FOUND] = "unalias: %s: 未找到\n",
    [M_TOO_MANY_REDIR]    = "synsh: 重定向过多",
    [M_UNTERMINATED]      = "synsh: %s 未闭合\n",
    [M_NO_TERMINAL]       = "  synsh: 没有安装可用于运行的终端: %s\n         sudo pacman -S syntty\n",
    [M_OPENING_TERM]      = "  正在打开 %s\n",
    [M_NO_BROWSER]        = "  synsh: 未安装浏览器，也没有 xdg-open\n         sudo pacman -S firefox\n",
    [M_OPENING_URL]       = "  %s正在打开%s %s\n",
    [M_OPENING_SYNFILES]  = "  %s正在打开%s %s（在 synfiles 中）\n",
    [M_NO_FILEMANAGER]    = "  synsh: 未安装文件管理器\n         sudo pacman -S synfiles\n",
    [M_OPENING_IN]        = "  %s正在打开%s %s（在 %s 中）\n",
    [M_RESUMING_CLIAMP]   = "  %s正在恢复 cliamp%s\n",
    [M_STARTING_CLIAMP]   = "  %s正在启动 cliamp%s\n",
    [M_NO_MUSIC_DIR]      = "  synsh: 没有音乐目录 (%s)\n",
    [M_PLAYING_WITH]      = "  %s正在播放%s %s（使用 %s）\n",
    [M_ALARM_WHAT_TIME]   = "  synsh: 几点? 例如 \"set alarm for 7:30am\"\n",
    [M_CANNOT_WRITE]      = "  synsh: 无法写入 %s: %s\n",
    [M_CANNOT_REPLACE]    = "  synsh: 无法替换 %s: %s\n",
    [M_ALARM_SET]         = "  %s闹钟已设为 %s%s\n",
    [M_CHIBI_NOT_RUNNING] = "  %s(chibi 没有在运行 — 闹钟由她来响，请在那之前启动她)%s\n",
    [M_INIT_FAILED]       = "synsh: 初始化失败",
    [M_CONNECTED_SHORT]   = "synsh: 已连接到 synapd",
    [M_TAGLINE]           = "内核思考之处",
    [M_NO_ORPHANS]        = "  没有孤立的软件包",
};

static const char *const MSG_KO[M_COUNT] = {
    [M_AI_ONLINE]         = "AI 연결됨",
    [M_AI_OFFLINE]        = "AI 연결 안 됨",
    [M_TYPE_NATURALLY]    = "하고 싶은 것을 그대로 쓰거나 셸 명령을 입력하세요",
    [M_SHELL_ONLY]        = "셸 전용 모드",
    [M_CONNECTED]         = "synsh: synapd에 연결됨 — AI 연결됨",
    [M_NOT_CONNECTED]     = "synsh: synapd에 연결할 수 없음 — 셸 전용 모드",
    [M_AI_UNAVAILABLE]    = "synsh: 경고 — synapd를 쓸 수 없어 AI 기능이 꺼졌습니다",
    [M_AI_FAILED]         = "synsh: AI 변환에 실패했습니다",
    [M_ASKING_AI]         = "명령이 실패했습니다. AI에게 묻는 중…",
    [M_RUN_CONFIRM]       = "실행할까요?",
    [M_CANCELLED]         = "취소했습니다.",
    [M_EDIT_IN_SHELL]     = "셸에서 편집:",
    [M_NO_SHELL_EQUIV]    = "해당하는 셸 명령이 없습니다.",
    [M_NOT_INSTALLED]     = "이(가) 설치되어 있지 않습니다",
    [M_EXIT]              = "종료",
    [M_TOO_MANY_ARGS]     = "인자가 너무 많습니다",
    [M_SYNTAX_REDIR]      = "리다이렉션 문법 오류",
    [M_HELP_HEADLINE]     = "평소처럼 명령을 쓰거나, 하고 싶은 것을 그대로 말하세요:",
    [M_HELP_REGULAR]      = "보통 명령",
    [M_HELP_NATURAL]      = "자기 말로",
    [M_HELP_QUESTION]     = "질문",
    [M_HELP_PREFIX]       = "! 를 앞에 붙이면 명령, ? 를 붙이면 AI로 보냅니다.",
    [M_HELP_BUILTINS]     = "내장:",
    [M_HELP_META]         = "메타:",
    [M_HELP_ASK]          = "자기 말로 물어보세요",
    [M_HELP_ANSWERED]     = "이런 것은 바로 대답합니다",
    [M_HELP_THE_TIME]     = "현재 시각",
    [M_HELP_THE_DATE]     = "오늘 날짜",
    [M_HELP_IN_BROWSER]   = "브라우저에서 열기",
    [M_HELP_NO_BROWSER]   = "(브라우저 없음)",
    [M_HELP_NONE]         = "(설치되지 않음)",
    [M_HELP_NO_PLAYER]    = "(플레이어 없음 — sudo pacman -S mpv)",
    [M_HELP_ALARM]        = "chibi가 깨웁니다",
    [M_HELP_PACKAGES]     = "패키지",
    [M_HELP_ARCH_SYNTAX]  = "Arch 문법은 외우지 않아도 됩니다",
    [M_HELP_EVERYDAY]     = "매일 쓰는 명령을 자기 말로",
    [M_HELP_ELSEWHERE]    = "나머지는 synapd가 이 기계에 맞춰 답합니다.",
    [M_HELP_DESTRUCTIVE]  = "파괴적인 일은 일부러 그쪽에 남겨 둡니다: 명령을 보여 주고\n  기다릴 뿐, 당신의 파일을 추측하지 않습니다.",
    [M_HELP_LANGUAGES]    = "이해하는 언어:",
    [M_STATUS_ONLINE]     = "연결됨",
    [M_STATUS_OFFLINE]    = "연결 안 됨",
    [M_STATUS_ENABLED]    = "켜짐",
    [M_STATUS_DISABLED]   = "꺼짐",
    [M_STATUS_ON]         = "켜짐",
    [M_STATUS_OFF]        = "꺼짐",
    [M_STAT_COMMANDS]     = "실행한 명령",
    [M_STAT_NL]           = "말로 한 요청",
    [M_STAT_ASSISTS]      = "AI 도움",
    [M_LANG_IS]           = "언어:",
    [M_LANG_UNKNOWN]      = "모르는 언어입니다 — 이 중에서:",
    [M_SYNAPD_OFFLINE]    = "syn: synapd 연결 안 됨",
    [M_CD_NO_OLDPWD]      = "cd: OLDPWD가 설정되지 않았습니다",
    [M_NO_JOBS]           = "백그라운드 작업이 없습니다.",
    [M_JOB_RUNNING]       = "실행 중",
    [M_JOB_STOPPED]       = "중지됨",
    [M_JOB_DONE]          = "완료",
    [M_LABEL_SOCKET]      = "소켓",
    [M_LABEL_AI]          = "AI",
    [M_LABEL_EXPLAIN]     = "설명",
    [M_LABEL_SAFE]        = "안전 모드",
    [M_SET_AI]            = "AI 지원:",
    [M_SET_EXPLAIN]       = "설명 모드:",
    [M_SET_SAFE]          = "안전 모드:",
    [M_UNKNOWN_SUBCMD]    = "syn: 알 수 없는 하위 명령 '%s'. 도움말은 'syn'을 입력하십시오.\n",
    [M_ALIAS_NOT_FOUND]   = "alias: %s: 찾을 수 없습니다\n",
    [M_ALIAS_BAD_NAME]    = "alias: 잘못된 별칭 이름\n",
    [M_ALIAS_FULL]        = "alias: 테이블이 가득 찼습니다 (최대 128)\n",
    [M_UNALIAS_USAGE]     = "unalias: 사용법: unalias 이름 [이름 …]\n",
    [M_UNALIAS_NOT_FOUND] = "unalias: %s: 찾을 수 없습니다\n",
    [M_TOO_MANY_REDIR]    = "synsh: 리디렉션이 너무 많습니다",
    [M_UNTERMINATED]      = "synsh: %s이(가) 닫히지 않았습니다\n",
    [M_NO_TERMINAL]       = "  synsh: 실행할 터미널이 설치되어 있지 않습니다: %s\n         sudo pacman -S syntty\n",
    [M_OPENING_TERM]      = "  %s 여는 중\n",
    [M_NO_BROWSER]        = "  synsh: 브라우저도 xdg-open도 설치되어 있지 않습니다\n         sudo pacman -S firefox\n",
    [M_OPENING_URL]       = "  %s여는 중%s %s\n",
    [M_OPENING_SYNFILES]  = "  %s여는 중%s %s (synfiles)\n",
    [M_NO_FILEMANAGER]    = "  synsh: 파일 관리자가 설치되어 있지 않습니다\n         sudo pacman -S synfiles\n",
    [M_OPENING_IN]        = "  %s여는 중%s %s (%s)\n",
    [M_RESUMING_CLIAMP]   = "  %scliamp 재개 중%s\n",
    [M_STARTING_CLIAMP]   = "  %scliamp 시작 중%s\n",
    [M_NO_MUSIC_DIR]      = "  synsh: 음악 디렉터리가 없습니다 (%s)\n",
    [M_PLAYING_WITH]      = "  %s재생 중%s %s (%s)\n",
    [M_ALARM_WHAT_TIME]   = "  synsh: 몇 시입니까? 예: \"set alarm for 7:30am\"\n",
    [M_CANNOT_WRITE]      = "  synsh: %s에 쓸 수 없습니다: %s\n",
    [M_CANNOT_REPLACE]    = "  synsh: %s을(를) 바꿀 수 없습니다: %s\n",
    [M_ALARM_SET]         = "  %s%s에 알람을 설정했습니다%s\n",
    [M_CHIBI_NOT_RUNNING] = "  %s(chibi가 실행 중이 아닙니다 — 알람은 chibi가 울리므로 그 전에 실행하십시오)%s\n",
    [M_INIT_FAILED]       = "synsh: 초기화에 실패했습니다",
    [M_CONNECTED_SHORT]   = "synsh: synapd에 연결됨",
    [M_TAGLINE]           = "커널이 생각하는 곳",
    [M_NO_ORPHANS]        = "  고아 패키지가 없습니다",
};

static const char *const MSG_HI[M_COUNT] = {
    [M_AI_ONLINE]         = "AI ऑनलाइन",
    [M_AI_OFFLINE]        = "AI ऑफ़लाइन",
    [M_TYPE_NATURALLY]    = "जो चाहिए वही लिखिए, या शेल कमांड दीजिए",
    [M_SHELL_ONLY]        = "सिर्फ़ शेल मोड",
    [M_CONNECTED]         = "synsh: synapd से जुड़ गया — AI ऑनलाइन",
    [M_NOT_CONNECTED]     = "synsh: synapd से संपर्क नहीं — सिर्फ़ शेल मोड",
    [M_AI_UNAVAILABLE]    = "synsh: चेतावनी — synapd उपलब्ध नहीं, AI सुविधाएँ बंद",
    [M_AI_FAILED]         = "synsh: AI अनुवाद विफल",
    [M_ASKING_AI]         = "कमांड विफल, AI से पूछ रहे हैं…",
    [M_RUN_CONFIRM]       = "चलाएँ?",
    [M_CANCELLED]         = "रद्द किया।",
    [M_EDIT_IN_SHELL]     = "शेल में संपादित करें:",
    [M_NO_SHELL_EQUIV]    = "इसके लिए कोई शेल कमांड नहीं है।",
    [M_NOT_INSTALLED]     = "स्थापित नहीं है",
    [M_EXIT]              = "निकास",
    [M_TOO_MANY_ARGS]     = "बहुत ज़्यादा आर्ग्युमेंट",
    [M_SYNTAX_REDIR]      = "रीडायरेक्शन में सिंटैक्स त्रुटि",
    [M_HELP_HEADLINE]     = "कमांड हमेशा की तरह लिखिए, या बस बता दीजिए कि क्या चाहिए:",
    [M_HELP_REGULAR]      = "सामान्य कमांड",
    [M_HELP_NATURAL]      = "अपने शब्दों में",
    [M_HELP_QUESTION]     = "एक सवाल",
    [M_HELP_PREFIX]       = "! लगाने पर कमांड, ? लगाने पर AI।",
    [M_HELP_BUILTINS]     = "अंतर्निर्मित:",
    [M_HELP_META]         = "मेटा:",
    [M_HELP_ASK]          = "अपने शब्दों में पूछिए",
    [M_HELP_ANSWERED]     = "इनका जवाब सीधे मिलता है",
    [M_HELP_THE_TIME]     = "समय",
    [M_HELP_THE_DATE]     = "तारीख़",
    [M_HELP_IN_BROWSER]   = "आपके ब्राउज़र में",
    [M_HELP_NO_BROWSER]   = "(कोई ब्राउज़र नहीं)",
    [M_HELP_NONE]         = "(कुछ स्थापित नहीं)",
    [M_HELP_NO_PLAYER]    = "(कोई प्लेयर नहीं — sudo pacman -S mpv)",
    [M_HELP_ALARM]        = "chibi जगाएगा",
    [M_HELP_PACKAGES]     = "पैकेज",
    [M_HELP_ARCH_SYNTAX]  = "Arch का सिंटैक्स, ताकि आपको याद न रखना पड़े",
    [M_HELP_EVERYDAY]     = "रोज़मर्रा के कमांड, अपने शब्दों में",
    [M_HELP_ELSEWHERE]    = "बाकी सब synapd के पास जाता है, जो इसी मशीन के बारे में जवाब देता है।",
    [M_HELP_DESTRUCTIVE]  = "नुक़सान पहुँचाने वाली चीज़ें जान-बूझकर वहीं रहती हैं: कमांड\n  दिखाकर रुकता है, आपकी फ़ाइलों के बारे में अंदाज़ा नहीं लगाता।",
    [M_HELP_LANGUAGES]    = "समझी जाने वाली भाषाएँ:",
    [M_STATUS_ONLINE]     = "ऑनलाइन",
    [M_STATUS_OFFLINE]    = "ऑफ़लाइन",
    [M_STATUS_ENABLED]    = "चालू",
    [M_STATUS_DISABLED]   = "बंद",
    [M_STATUS_ON]         = "चालू",
    [M_STATUS_OFF]        = "बंद",
    [M_STAT_COMMANDS]     = "चलाए गए कमांड",
    [M_STAT_NL]           = "अपने शब्दों में पूछे गए सवाल",
    [M_STAT_ASSISTS]      = "AI की मदद",
    [M_LANG_IS]           = "भाषा:",
    [M_LANG_UNKNOWN]      = "अनजान भाषा — इनमें से चुनिए:",
    [M_SYNAPD_OFFLINE]    = "syn: synapd ऑफ़लाइन",
    [M_CD_NO_OLDPWD]      = "cd: OLDPWD सेट नहीं है",
    [M_NO_JOBS]           = "कोई पृष्ठभूमि कार्य नहीं।",
    [M_JOB_RUNNING]       = "चल रहा",
    [M_JOB_STOPPED]       = "रुका",
    [M_JOB_DONE]          = "पूरा",
    [M_LABEL_SOCKET]      = "सॉकेट",
    [M_LABEL_AI]          = "AI",
    [M_LABEL_EXPLAIN]     = "व्याख्या",
    [M_LABEL_SAFE]        = "सुरक्षित मोड",
    [M_SET_AI]            = "AI सहायता:",
    [M_SET_EXPLAIN]       = "व्याख्या मोड:",
    [M_SET_SAFE]          = "सुरक्षित मोड:",
    [M_UNKNOWN_SUBCMD]    = "syn: अज्ञात उपकमांड '%s'। सहायता के लिए 'syn' टाइप करें।\n",
    [M_ALIAS_NOT_FOUND]   = "alias: %s: नहीं मिला\n",
    [M_ALIAS_BAD_NAME]    = "alias: अमान्य उपनाम\n",
    [M_ALIAS_FULL]        = "alias: तालिका भरी है (अधिकतम 128)\n",
    [M_UNALIAS_USAGE]     = "unalias: उपयोग: unalias नाम [नाम …]\n",
    [M_UNALIAS_NOT_FOUND] = "unalias: %s: नहीं मिला\n",
    [M_TOO_MANY_REDIR]    = "synsh: बहुत अधिक पुनर्निर्देशन",
    [M_UNTERMINATED]      = "synsh: %s बंद नहीं हुआ\n",
    [M_NO_TERMINAL]       = "  synsh: चलाने के लिए कोई टर्मिनल संस्थापित नहीं: %s\n         sudo pacman -S syntty\n",
    [M_OPENING_TERM]      = "  %s खोल रहे हैं\n",
    [M_NO_BROWSER]        = "  synsh: न कोई ब्राउज़र है न xdg-open\n         sudo pacman -S firefox\n",
    [M_OPENING_URL]       = "  %sखोल रहे हैं%s %s\n",
    [M_OPENING_SYNFILES]  = "  %sखोल रहे हैं%s %s synfiles में\n",
    [M_NO_FILEMANAGER]    = "  synsh: कोई फ़ाइल प्रबंधक संस्थापित नहीं\n         sudo pacman -S synfiles\n",
    [M_OPENING_IN]        = "  %sखोल रहे हैं%s %s %s में\n",
    [M_RESUMING_CLIAMP]   = "  %scliamp फिर से शुरू%s\n",
    [M_STARTING_CLIAMP]   = "  %scliamp शुरू हो रहा है%s\n",
    [M_NO_MUSIC_DIR]      = "  synsh: कोई संगीत निर्देशिका नहीं (%s)\n",
    [M_PLAYING_WITH]      = "  %sचला रहे हैं%s %s %s से\n",
    [M_ALARM_WHAT_TIME]   = "  synsh: कितने बजे? जैसे \"set alarm for 7:30am\"\n",
    [M_CANNOT_WRITE]      = "  synsh: %s लिखा नहीं जा सका: %s\n",
    [M_CANNOT_REPLACE]    = "  synsh: %s बदला नहीं जा सका: %s\n",
    [M_ALARM_SET]         = "  %s%s के लिए अलार्म सेट%s\n",
    [M_CHIBI_NOT_RUNNING] = "  %s(chibi चल नहीं रही — अलार्म वही बजाती है, इसलिए उससे पहले उसे शुरू करें)%s\n",
    [M_INIT_FAILED]       = "synsh: आरंभीकरण विफल",
    [M_CONNECTED_SHORT]   = "synsh: synapd से जुड़ा",
    [M_TAGLINE]           = "जहाँ कर्नेल सोचता है",
    [M_NO_ORPHANS]        = "  कोई अनाथ पैकेज नहीं",
};

static const char *const MSG_AR[M_COUNT] = {
    [M_AI_ONLINE]         = "الذكاء الاصطناعي متصل",
    [M_AI_OFFLINE]        = "الذكاء الاصطناعي غير متصل",
    [M_TYPE_NATURALLY]    = "اكتب ما تريد بلغتك، أو استخدم أوامر الصدفة",
    [M_SHELL_ONLY]        = "وضع الصدفة فقط",
    [M_CONNECTED]         = "synsh: تم الاتصال بـ synapd — الذكاء الاصطناعي متصل",
    [M_NOT_CONNECTED]     = "synsh: تعذّر الاتصال بـ synapd — وضع الصدفة فقط",
    [M_AI_UNAVAILABLE]    = "synsh: تحذير — synapd غير متاح، ميزات الذكاء الاصطناعي معطّلة",
    [M_AI_FAILED]         = "synsh: فشلت الترجمة بالذكاء الاصطناعي",
    [M_ASKING_AI]         = "فشل الأمر، أسأل الذكاء الاصطناعي…",
    [M_RUN_CONFIRM]       = "أنفّذه؟",
    [M_CANCELLED]         = "أُلغي.",
    [M_EDIT_IN_SHELL]     = "حرّره في الصدفة:",
    [M_NO_SHELL_EQUIV]    = "لا يوجد أمر صدفة مقابل.",
    [M_NOT_INSTALLED]     = "غير مثبَّت",
    [M_EXIT]              = "خروج",
    [M_TOO_MANY_ARGS]     = "عدد الوسائط كبير جدًا",
    [M_SYNTAX_REDIR]      = "خطأ نحوي في إعادة التوجيه",
    [M_HELP_HEADLINE]     = "اكتب الأوامر كالمعتاد، أو قل ببساطة ما تريد:",
    [M_HELP_REGULAR]      = "أمر عادي",
    [M_HELP_NATURAL]      = "بكلماتك أنت",
    [M_HELP_QUESTION]     = "سؤال",
    [M_HELP_PREFIX]       = "ابدأ بـ ! لفرض أمر، وبـ ? لفرض الذكاء الاصطناعي.",
    [M_HELP_BUILTINS]     = "المدمجة:",
    [M_HELP_META]         = "الميتا:",
    [M_HELP_ASK]          = "اسأل بكلماتك أنت",
    [M_HELP_ANSWERED]     = "هذه يُجاب عنها مباشرة",
    [M_HELP_THE_TIME]     = "الوقت",
    [M_HELP_THE_DATE]     = "التاريخ",
    [M_HELP_IN_BROWSER]   = "في متصفّحك",
    [M_HELP_NO_BROWSER]   = "(لا يوجد متصفّح مثبَّت)",
    [M_HELP_NONE]         = "(غير مثبَّت)",
    [M_HELP_NO_PLAYER]    = "(لا يوجد مشغّل — sudo pacman -S mpv)",
    [M_HELP_ALARM]        = "chibi يوقظك",
    [M_HELP_PACKAGES]     = "الحزم",
    [M_HELP_ARCH_SYNTAX]  = "صياغة Arch، حتى لا تضطر إلى حفظها",
    [M_HELP_EVERYDAY]     = "أوامر يومية، بكلماتك أنت",
    [M_HELP_ELSEWHERE]    = "كل ما عدا ذلك يذهب إلى synapd، الذي يجيب عن هذا الجهاز.",
    [M_HELP_DESTRUCTIVE]  = "الأمور المدمِّرة تبقى هناك عمدًا: يعرض عليك الأمر وينتظر،\n  بدل التخمين بشأن ملفاتك.",
    [M_HELP_LANGUAGES]    = "اللغات المفهومة:",
    [M_STATUS_ONLINE]     = "متصل",
    [M_STATUS_OFFLINE]    = "غير متصل",
    [M_STATUS_ENABLED]    = "مفعَّل",
    [M_STATUS_DISABLED]   = "معطَّل",
    [M_STATUS_ON]         = "تشغيل",
    [M_STATUS_OFF]        = "إيقاف",
    [M_STAT_COMMANDS]     = "الأوامر المنفَّذة",
    [M_STAT_NL]           = "الطلبات بلغة عادية",
    [M_STAT_ASSISTS]      = "مساعدات الذكاء الاصطناعي",
    [M_LANG_IS]           = "اللغة:",
    [M_LANG_UNKNOWN]      = "لغة غير معروفة — جرّب:",
    [M_SYNAPD_OFFLINE]    = "syn: synapd غير متصل",
    [M_CD_NO_OLDPWD]      = "cd: لم يُضبط OLDPWD",
    [M_NO_JOBS]           = "لا مهام في الخلفية.",
    [M_JOB_RUNNING]       = "يعمل",
    [M_JOB_STOPPED]       = "متوقّف",
    [M_JOB_DONE]          = "منتهٍ",
    [M_LABEL_SOCKET]      = "المقبس",
    [M_LABEL_AI]          = "الذكاء الاصطناعي",
    [M_LABEL_EXPLAIN]     = "الشرح",
    [M_LABEL_SAFE]        = "الوضع الآمن",
    [M_SET_AI]            = "مساعدة الذكاء الاصطناعي:",
    [M_SET_EXPLAIN]       = "وضع الشرح:",
    [M_SET_SAFE]          = "الوضع الآمن:",
    [M_UNKNOWN_SUBCMD]    = "syn: أمر فرعي غير معروف '%s'. اكتب 'syn' للمساعدة.\n",
    [M_ALIAS_NOT_FOUND]   = "alias: %s: غير موجود\n",
    [M_ALIAS_BAD_NAME]    = "alias: اسم اختصار غير صالح\n",
    [M_ALIAS_FULL]        = "alias: الجدول ممتلئ (بحد أقصى 128)\n",
    [M_UNALIAS_USAGE]     = "unalias: الاستعمال: unalias اسم [اسم …]\n",
    [M_UNALIAS_NOT_FOUND] = "unalias: %s: غير موجود\n",
    [M_TOO_MANY_REDIR]    = "synsh: عدد التحويلات كبير جداً",
    [M_UNTERMINATED]      = "synsh: %s غير مغلق\n",
    [M_NO_TERMINAL]       = "  synsh: لا طرفية مثبَّتة لتشغيل: %s\n         sudo pacman -S syntty\n",
    [M_OPENING_TERM]      = "  يجري فتح %s\n",
    [M_NO_BROWSER]        = "  synsh: لا متصفح ولا xdg-open مثبَّت\n         sudo pacman -S firefox\n",
    [M_OPENING_URL]       = "  %sيجري فتح%s %s\n",
    [M_OPENING_SYNFILES]  = "  %sيجري فتح%s %s في synfiles\n",
    [M_NO_FILEMANAGER]    = "  synsh: لا مدير ملفات مثبَّت\n         sudo pacman -S synfiles\n",
    [M_OPENING_IN]        = "  %sيجري فتح%s %s في %s\n",
    [M_RESUMING_CLIAMP]   = "  %sاستئناف cliamp%s\n",
    [M_STARTING_CLIAMP]   = "  %sبدء تشغيل cliamp%s\n",
    [M_NO_MUSIC_DIR]      = "  synsh: لا مجلد موسيقى (%s)\n",
    [M_PLAYING_WITH]      = "  %sيجري تشغيل%s %s بواسطة %s\n",
    [M_ALARM_WHAT_TIME]   = "  synsh: في أي ساعة؟ مثلاً \"set alarm for 7:30am\"\n",
    [M_CANNOT_WRITE]      = "  synsh: تعذّرت الكتابة إلى %s: %s\n",
    [M_CANNOT_REPLACE]    = "  synsh: تعذّر استبدال %s: %s\n",
    [M_ALARM_SET]         = "  %sضُبط المنبّه على %s%s\n",
    [M_CHIBI_NOT_RUNNING] = "  %s(chibi لا تعمل — هي التي تُرِنّ المنبّه، فشغّلها قبل ذلك)%s\n",
    [M_INIT_FAILED]       = "synsh: فشلت التهيئة",
    [M_CONNECTED_SHORT]   = "synsh: مُتّصل بـ synapd",
    [M_TAGLINE]           = "حيث تفكّر النواة",
    [M_NO_ORPHANS]        = "  لا حزم يتيمة",
};

/* ── The language table ───────────────────────────────────── */
/*
 * code / endonym / English name / catalog, in enum order.
 *
 * The English name is not decoration: ai_translate() writes its prompt in
 * English and has to name the reply language in a word the model will act on,
 * and "Deutsch" is markedly less reliable there than "German".
 */
static const struct {
    const char         *code;
    const char         *endonym;
    const char         *english;
    const char *const  *msgs;
} LANGS[LANG_COUNT] = {
    [LANG_EN] = { "en", "English",    "English",    NULL   },
    [LANG_DE] = { "de", "Deutsch",    "German",     MSG_DE },
    [LANG_FR] = { "fr", "Français",   "French",     MSG_FR },
    [LANG_ES] = { "es", "Español",    "Spanish",    MSG_ES },
    [LANG_PT] = { "pt", "Português",  "Portuguese", MSG_PT },
    [LANG_IT] = { "it", "Italiano",   "Italian",    MSG_IT },
    [LANG_NL] = { "nl", "Nederlands", "Dutch",      MSG_NL },
    [LANG_PL] = { "pl", "Polski",     "Polish",     MSG_PL },
    [LANG_RU] = { "ru", "Русский",    "Russian",    MSG_RU },
    [LANG_JA] = { "ja", "日本語",      "Japanese",   MSG_JA },
    [LANG_ZH] = { "zh", "中文",        "Chinese",    MSG_ZH },
    [LANG_KO] = { "ko", "한국어",      "Korean",     MSG_KO },
    [LANG_HI] = { "hi", "हिन्दी",       "Hindi",      MSG_HI },
    [LANG_AR] = { "ar", "العربية",     "Arabic",     MSG_AR },
};

static synsh_lang_t g_lang = LANG_EN;

synsh_lang_t synsh_lang(void)                     { return g_lang; }
const char  *synsh_lang_code(synsh_lang_t l)      { return l < LANG_COUNT ? LANGS[l].code    : "en"; }
const char  *synsh_lang_name(synsh_lang_t l)      { return l < LANG_COUNT ? LANGS[l].endonym : "English"; }
const char  *synsh_lang_english_name(synsh_lang_t l) { return l < LANG_COUNT ? LANGS[l].english : "English"; }

const char *synsh_msg(synsh_msg_t id)
{
    if (id >= M_COUNT) return "";
    const char *const *cat = LANGS[g_lang].msgs;
    if (cat && cat[id]) return cat[id];
    return MSG_EN[id];   /* untranslated slot — English, never an id */
}

synsh_lang_t synsh_lang_from_string(const char *s)
{
    if (!s || !*s) return LANG_COUNT;

    /* "de_DE.UTF-8@euro" and "de" both have to answer German, so compare only
     * the primary subtag. "C" and "POSIX" are not languages: they are the
     * absence of one, and mean English here. */
    char tag[16];
    size_t i = 0;
    while (s[i] && s[i] != '_' && s[i] != '-' && s[i] != '.' && s[i] != '@' &&
           i < sizeof(tag) - 1) {
        tag[i] = (char)tolower((unsigned char)s[i]);
        i++;
    }
    tag[i] = '\0';
    if (!*tag) return LANG_COUNT;
    if (strcmp(tag, "c") == 0 || strcmp(tag, "posix") == 0) return LANG_EN;

    for (int l = 0; l < LANG_COUNT; l++)
        if (strcmp(tag, LANGS[l].code) == 0) return (synsh_lang_t)l;

    /* Also accept the names, so `set language German` and `set language
     * Deutsch` both work — people write the language, not the ISO code. */
    for (int l = 0; l < LANG_COUNT; l++) {
        if (strcasecmp(s, LANGS[l].english) == 0) return (synsh_lang_t)l;
        if (strcmp(s, LANGS[l].endonym) == 0)     return (synsh_lang_t)l;
    }

    /* A few tags that are the same language under another name. zh_Hans and
     * the Brazilian/Latin-American variants arrive here constantly. */
    if (strncmp(tag, "zh", 2) == 0) return LANG_ZH;
    if (strncmp(tag, "pt", 2) == 0) return LANG_PT;

    return LANG_COUNT;
}

synsh_lang_t synsh_i18n_init(const char *want)
{
    if (want && *want) {
        synsh_lang_t l = synsh_lang_from_string(want);
        if (l != LANG_COUNT) { g_lang = l; return g_lang; }
        return g_lang;   /* unknown: keep what we had, the caller reports it */
    }

    /* Environment, in the order the C library itself resolves messages:
     * LC_ALL beats LC_MESSAGES beats LANG. SYNSH_LANG sits above all three so
     * that one shell can be told to speak something else without moving the
     * rest of the session's locale — which is what a test rig needs. */
    const char *env = getenv("SYNSH_LANG");
    if (!env || !*env) env = getenv("LC_ALL");
    if (!env || !*env) env = getenv("LC_MESSAGES");
    if (!env || !*env) env = getenv("LANG");

    synsh_lang_t l = synsh_lang_from_string(env);
    g_lang = (l == LANG_COUNT) ? LANG_EN : l;
    return g_lang;
}

/* ── UTF-8 ────────────────────────────────────────────────── */

/* Decode one code point. Advances *p past it. An invalid byte decodes as
 * itself and advances one — folding must never lose or reorder input it does
 * not understand, because what it is handed is somebody's command line. */
static unsigned decode(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    unsigned c = s[0];
    int extra;

    if (c < 0x80)            { *p += 1; return c; }
    else if ((c & 0xE0) == 0xC0) { extra = 1; c &= 0x1F; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; c &= 0x0F; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; c &= 0x07; }
    else                     { *p += 1; return s[0]; }

    for (int i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) { *p += 1; return s[0]; }  /* truncated */
        c = (c << 6) | (s[i] & 0x3F);
    }
    *p += extra + 1;
    return c;
}

/*
 * How many COLUMNS a string occupies, which is not how many bytes it is and
 * not how many code points either.
 *
 * ⛔ THE BANNER BOX IS DRAWN WITH A COMPUTED PAD, and before 0.1.0-29 that pad
 * was the literal 25 — the length of "Where the kernel thinks". Any other
 * tagline made the right-hand │ land in the wrong column, which is why the line
 * had stayed English while everything around it was translated. A byte count
 * would be just as wrong: "ソケット" is 12 bytes, 4 code points and 8 columns.
 *
 * ⚠ NO wcswidth(3), DELIBERATELY. It needs setlocale(LC_CTYPE, "") and synsh
 * does not call setlocale at all — its catalog is compiled in precisely so that
 * it works before /usr is complete. Under the C locale mbrtowc() rejects every
 * UTF-8 lead byte and the answer would be zero.
 *
 * The rule is the usual one: combining marks take no room, the East Asian Wide
 * and Fullwidth blocks take two, everything else takes one. Arabic and
 * Devanagari are one column per base letter with their marks at zero, which is
 * what a terminal actually does with them.
 */
int synsh_disp_width(const char *s)
{
    int w = 0;
    const char *p = s;
    while (*p) {
        unsigned c = decode(&p);
        if (c == 0) break;
        /* Combining marks and other zero-width: Devanagari/Arabic vowel signs,
         * variation selectors, the joiners. */
        /*
         * ⛔ Mn AND Me TAKE NO ROOM; Mc TAKES ONE. That distinction is the
         * whole of Devanagari here: U+094D (virama) is nonspacing and U+093E
         * (ा) is a SPACING combining mark that occupies a cell. Zeroing the
         * whole 093A–094F block — which the first version of this did — makes
         * "जहाँ कर्नेल सोचता है" 13 columns where a terminal draws 16, and the
         * banner's right-hand │ lands three columns late. Every range below
         * agrees with glibc's wcswidth() for all thirteen taglines; that is
         * how it was checked, because wcswidth() itself is unusable here (it
         * needs a setlocale() this program deliberately never calls).
         */
        if ((c >= 0x0300 && c <= 0x036F) || (c >= 0x0483 && c <= 0x0489) ||
            (c >= 0x0591 && c <= 0x05BD) || (c >= 0x0610 && c <= 0x061A) ||
            (c >= 0x064B && c <= 0x065F) || c == 0x0670 ||
            (c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E4) ||
            (c >= 0x0900 && c <= 0x0902) || c == 0x093A || c == 0x093C ||
            (c >= 0x0941 && c <= 0x0948) || c == 0x094D ||
            (c >= 0x0951 && c <= 0x0957) || (c >= 0x0962 && c <= 0x0963) ||
            (c >= 0x200B && c <= 0x200F) || (c >= 0xFE00 && c <= 0xFE0F))
            continue;
        if ((c >= 0x1100 && c <= 0x115F) ||   /* Hangul Jamo */
            (c >= 0x2E80 && c <= 0xA4CF) ||   /* CJK radicals … Yi */
            (c >= 0xAC00 && c <= 0xD7A3) ||   /* Hangul syllables */
            (c >= 0xF900 && c <= 0xFAFF) ||   /* CJK compatibility */
            (c >= 0xFE30 && c <= 0xFE6F) ||   /* CJK compatibility forms */
            (c >= 0xFF00 && c <= 0xFF60) ||   /* fullwidth forms */
            (c >= 0xFFE0 && c <= 0xFFE6) ||
            (c >= 0x1F300 && c <= 0x1F64F) || /* emoji */
            (c >= 0x20000 && c <= 0x3FFFD))
            w += 2;
        else
            w += 1;
    }
    return w;
}

/* Append one code point as UTF-8, if it fits. Returns bytes written. */
static size_t encode(char *dst, size_t room, unsigned c)
{
    if (c < 0x80)      { if (room < 1) return 0; dst[0] = (char)c; return 1; }
    if (c < 0x800)     { if (room < 2) return 0;
                         dst[0] = (char)(0xC0 | (c >> 6));
                         dst[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000)   { if (room < 3) return 0;
                         dst[0] = (char)(0xE0 | (c >> 12));
                         dst[1] = (char)(0x80 | ((c >> 6) & 0x3F));
                         dst[2] = (char)(0x80 | (c & 0x3F)); return 3; }
    if (room < 4) return 0;
    dst[0] = (char)(0xF0 | (c >> 18));
    dst[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    dst[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    dst[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

/* Lowercase, across the scripts that have case and that this shell's languages
 * are written in. Everything else is returned untouched — CJK, Hangul, Arabic
 * and Devanagari are caseless, so there is nothing here for them to get wrong. */
static unsigned lower_cp(unsigned c)
{
    if (c < 0x80)                       return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;    /* Latin-1 */
    if (c >= 0x100 && c <= 0x137)       return (c % 2 == 0) ? c + 1 : c;
    if (c >= 0x139 && c <= 0x148)       return (c % 2 == 1) ? c + 1 : c;
    if (c >= 0x14A && c <= 0x177)       return (c % 2 == 0) ? c + 1 : c;
    if (c == 0x178)                     return 0xFF;           /* Ÿ */
    if (c >= 0x179 && c <= 0x17E)       return (c % 2 == 1) ? c + 1 : c;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32; /* Greek */
    if (c >= 0x400 && c <= 0x40F)       return c + 80;         /* Ё … */
    if (c >= 0x410 && c <= 0x42F)       return c + 32;         /* А–Я */
    if (c >= 0xFF21 && c <= 0xFF3A)     return c + 32;         /* full-width */
    return c;
}

/*
 * Strip the accent off a lowercase code point.
 *
 * ⚠ THE POINT IS THE TYPIST, NOT THE ALPHABET. The phrase tables are spelled
 * properly — "qué hora es", "wie spät ist es" — and a person in a hurry types
 * "que hora es" and "wie spaet ist es". Folding both sides through here makes
 * the table readable AND the match forgiving, which no single spelling can be.
 *
 * Returns a replacement string (ASCII, or UTF-8 for ё→е) or NULL to keep the
 * character. Multi-character replacements are why this is not a code point
 * function: ß is "ss" and æ is "ae".
 */
static const char *strip_accent(unsigned c, bool translit)
{
    /*
     * ⚠ GERMAN HAS TWO CORRECT SPELLINGS AND PEOPLE USE BOTH. Where the accent
     * cannot be typed, ä is written "ae", ö "oe" and ü "ue" — that is the
     * standard substitution, not a mistake, and "wie spaet ist es" is what
     * comes out of a keyboard without an umlaut key. Stripping to "a" and
     * transliterating to "ae" are both right and they disagree, so a phrase is
     * folded BOTH ways and the line has to match either (see synsh_fold_eq).
     * Doing it in the folder rather than by adding a second spelling of every
     * German phrase keeps the tables readable and cannot be forgotten on the
     * next entry somebody adds.
     */
    if (translit) {
        switch (c) {
        case 0xE4: return "ae";
        case 0xF6: return "oe";
        case 0xFC: return "ue";
        default: break;
        }
    }
    switch (c) {
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return "a";
    case 0xE6: return "ae";
    case 0xE7: return "c";
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return "e";
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return "i";
    case 0xF0: return "d";
    case 0xF1: return "n";
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8: return "o";
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return "u";
    case 0xFD: case 0xFF: return "y";
    case 0xFE: return "th";
    case 0xDF: return "ss";
    /* Latin Extended-A, lowercase halves only — the caller has lowercased. */
    case 0x101: case 0x103: case 0x105: return "a";
    case 0x107: case 0x109: case 0x10B: case 0x10D: return "c";
    case 0x10F: case 0x111: return "d";
    case 0x113: case 0x115: case 0x117: case 0x119: case 0x11B: return "e";
    case 0x11D: case 0x11F: case 0x121: case 0x123: return "g";
    case 0x125: case 0x127: return "h";
    case 0x129: case 0x12B: case 0x12D: case 0x12F: case 0x131: return "i";
    case 0x133: return "ij";
    case 0x135: return "j";
    case 0x137: return "k";
    case 0x13A: case 0x13C: case 0x13E: case 0x140: case 0x142: return "l";
    case 0x144: case 0x146: case 0x148: case 0x14B: return "n";
    case 0x14D: case 0x14F: case 0x151: return "o";
    case 0x153: return "oe";
    case 0x155: case 0x157: case 0x159: return "r";
    case 0x15B: case 0x15D: case 0x15F: case 0x161: return "s";
    case 0x163: case 0x165: case 0x167: return "t";
    case 0x169: case 0x16B: case 0x16D: case 0x16F: case 0x171: case 0x173: return "u";
    case 0x175: return "w";
    case 0x177: return "y";
    case 0x17A: case 0x17C: case 0x17E: return "z";
    /* Russian ё and е are the same letter to everybody who is not a
     * lexicographer, and people type whichever their keyboard offers. */
    case 0x451: return "\xd0\xb5";
    default: return NULL;
    }
}

/* The characters that only ever mark a sentence as a question, a statement or
 * a shout. Dropped at the ENDS of the line, never in the middle: a full stop
 * inside a line belongs to a filename or a version number. */
static bool is_sentence_mark(unsigned c)
{
    switch (c) {
    case '?': case '!': case '.': case ',':
    case 0xBF:   /* ¿ */
    case 0xA1:   /* ¡ */
    case 0x3002: /* 。 */
    case 0x3001: /* 、 */
    case 0xFF1F: /* ？ */
    case 0xFF01: /* ！ */
    case 0xFF0E: /* ． */
    case 0xFF0C: /* ， */
    case 0x061F: /* ؟ Arabic question mark */
    case 0x06D4: /* ۔ Arabic full stop */
    case 0x0964: /* । Devanagari danda */
        return true;
    default: return false;
    }
}

static void fold_ex(char *dst, size_t n, const char *src, bool translit)
{
    if (!n) return;
    if (!src) { dst[0] = '\0'; return; }

    /* Pass 1: lowercase, strip accents, collapse whitespace. Code points are
     * decoded here rather than bytes compared, which is the whole difference
     * between this and the tolower() loop it replaced. */
    size_t i = 0;
    bool pending_space = false;
    bool started = false;

    const char *p = src;
    while (*p) {
        const char *before = p;
        unsigned c = decode(&p);
        (void)before;

        /* Whitespace, including the ideographic space people's IMEs emit. */
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x3000) {
            if (started) pending_space = true;
            continue;
        }

        c = lower_cp(c);

        if (pending_space && i + 1 < n) { dst[i++] = ' '; pending_space = false; }
        else if (pending_space)         break;

        const char *rep = strip_accent(c, translit);
        if (rep) {
            size_t rl = strlen(rep);
            if (i + rl >= n) break;
            memcpy(dst + i, rep, rl);
            i += rl;
        } else {
            size_t w = encode(dst + i, n - 1 - i, c);
            if (!w) break;
            i += w;
        }
        started = true;
    }
    dst[i] = '\0';

    /* Pass 2: trim the sentence marks off both ends. Done here rather than in
     * the loop because "what's the time?" needs the '?' gone and
     * "8.5" does not — position is the only thing that distinguishes them. */
    size_t start = 0;
    while (dst[start]) {
        const char *q = dst + start;
        const char *after = q;
        unsigned c = decode(&after);
        if (!is_sentence_mark(c) && c != ' ') break;
        start += (size_t)(after - q);
    }
    if (start) memmove(dst, dst + start, strlen(dst + start) + 1);

    /* Trailing: walk back over continuation bytes to find each character's
     * start, then test it. */
    size_t len = strlen(dst);
    while (len) {
        size_t cs = len - 1;
        while (cs > 0 && ((unsigned char)dst[cs] & 0xC0) == 0x80) cs--;
        const char *q = dst + cs;
        unsigned c = decode(&q);
        if (!is_sentence_mark(c) && c != ' ') break;
        dst[cs] = '\0';
        len = cs;
    }
}

void synsh_fold(char *dst, size_t n, const char *src)
{
    fold_ex(dst, n, src, false);
}

void synsh_fold_translit(char *dst, size_t n, const char *src)
{
    fold_ex(dst, n, src, true);
}

bool synsh_fold_eq(const char *folded_line, const char *phrase)
{
    char f[512];
    fold_ex(f, sizeof(f), phrase, false);
    if (strcmp(folded_line, f) == 0) return true;
    fold_ex(f, sizeof(f), phrase, true);
    return strcmp(folded_line, f) == 0;
}

bool synsh_fold_in(const char *folded_line, const char *const *phrases)
{
    for (int i = 0; phrases[i]; i++)
        if (synsh_fold_eq(folded_line, phrases[i])) return true;
    return false;
}

static const char *fold_after_one(const char *folded_line, const char *f)
{
    size_t n = strlen(f);
    if (!n) return NULL;
    if (strncmp(folded_line, f, n) != 0) return NULL;

    /* A word boundary, or the end. Without this, "install" would claim
     * "installation notes" — the same substring trap line_is() exists to
     * avoid, one level down. */
    if (folded_line[n] == '\0') return folded_line + n;
    if (folded_line[n] != ' ')  return NULL;
    const char *rest = folded_line + n;
    while (*rest == ' ') rest++;
    return rest;
}

const char *synsh_fold_after(const char *folded_line, const char *phrase)
{
    char f[512];
    fold_ex(f, sizeof(f), phrase, false);
    const char *r = fold_after_one(folded_line, f);
    if (r) return r;
    fold_ex(f, sizeof(f), phrase, true);
    return fold_after_one(folded_line, f);
}

bool synsh_utf8_is_letterish(unsigned char c)
{
    /* Every byte of a multi-byte sequence: lead bytes are 0xC0-0xF7 and
     * continuations are 0x80-0xBF. classify.c counts characters to decide
     * whether a line is prose, and without this every accented letter counted
     * as two pieces of punctuation and pushed the line out of the
     * natural-language branch. */
    return c >= 0x80;
}
