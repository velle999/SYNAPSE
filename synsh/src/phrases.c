/*
 * phrases.c — the phrase tables. See phrases.h for why they are shaped this
 * way; this file is data, and the one function at the bottom that reads it.
 *
 * WHAT GOES IN AND WHAT DOES NOT
 *
 *   - A phrase must be a REQUEST, not a noun. "play music" is in; bare
 *     "music"/"musique"/"音楽" is not, because it is equally somebody's
 *     directory and the intent would claim it. Time and date are the
 *     exception, and were before this file existed: "date" is date(1) and
 *     answering it with the date is the same answer either way.
 *
 *   - READ-ONLY, still. Nothing here deletes, moves or overwrites. The rule
 *     from intents.c stands in every language: if being wrong would cost you
 *     data, it belongs on the synapd path, which shows you the command and
 *     waits.
 *
 *   - No phrase may be a prefix of a real command with arguments. That is
 *     what the whole-line rule enforces, and the circumfix tables get it by
 *     requiring both ends to line up.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */

#include <string.h>

#include "phrases.h"
#include "i18n.h"

/* ── Time ─────────────────────────────────────────────────── */
const char *const SYNSH_P_TIME[] = {
    /* en */ "what time is it", "whats the time", "what's the time",
             "what is the time", "time", "the time", "got the time",
             "what's the time now", "current time",
    /* de */ "wie spät ist es", "wie spät", "wie viel uhr ist es",
             "wieviel uhr ist es", "uhrzeit", "die uhrzeit", "wie spät ist's",
    /* fr */ "quelle heure est-il", "quelle heure il est", "il est quelle heure",
             "l'heure", "heure", "quelle heure",
    /* es */ "qué hora es", "la hora", "qué hora", "dime la hora", "hora",
    /* pt */ "que horas são", "as horas", "que horas", "qual é a hora",
    /* it */ "che ore sono", "che ora è", "l'ora", "ora",
    /* nl */ "hoe laat is het", "hoe laat", "de tijd", "hoelaat",
    /* pl */ "która godzina", "godzina", "która jest godzina",
    /* ru */ "который час", "сколько времени", "время",
             "сколько сейчас времени",
    /* ja */ "今何時", "何時ですか", "今何時ですか", "いま何時",
    /* zh */ "现在几点", "几点了", "现在几点了", "现在时间",
    /* ko */ "지금 몇 시", "몇 시야", "몇 시예요", "지금 몇시",
    /* hi */ "क्या समय हुआ है", "समय क्या है", "कितने बजे हैं", "समय",
    /* ar */ "كم الساعة", "الساعة كم", "الوقت",
    NULL
};

/* ── Date ─────────────────────────────────────────────────── */
const char *const SYNSH_P_DATE[] = {
    /* en */ "what day is it", "whats the date", "what's the date",
             "what is the date", "what's today", "whats today", "date",
             "todays date", "today's date", "what's the date today",
    /* de */ "welcher tag ist heute", "welches datum ist heute", "datum",
             "das datum", "der wievielte ist heute", "welcher tag",
    /* fr */ "quel jour sommes-nous", "quelle est la date", "la date",
             "on est quel jour", "quel jour on est",
    /* es */ "qué día es hoy", "la fecha", "qué fecha es hoy", "qué día es",
             "fecha",
    /* pt */ "que dia é hoje", "a data", "qual é a data", "que dia",
    /* it */ "che giorno è oggi", "la data", "che data è oggi", "che giorno è",
    /* nl */ "welke dag is het", "wat is de datum", "de datum", "welke dag",
    /* pl */ "jaki dziś dzień", "jaki jest dzisiaj dzień", "data",
             "którego dziś mamy",
    /* ru */ "какое сегодня число", "какой сегодня день", "дата", "число",
    /* ja */ "今日は何日", "何日ですか", "今日の日付", "日付",
    /* zh */ "今天几号", "今天是几号", "今天日期", "日期",
    /* ko */ "오늘 며칠", "오늘 날짜", "며칠이야",
    /* hi */ "आज कौन सी तारीख है", "आज की तारीख", "तारीख",
    /* ar */ "ما تاريخ اليوم", "تاريخ اليوم", "التاريخ",
    NULL
};

/* ── Music ────────────────────────────────────────────────── */
/* No bare nouns here — see the note at the top of this file. */
const char *const SYNSH_P_MUSIC[] = {
    /* en */ "play music", "play some music", "play my music",
             "put on music", "put on some music", "start the music",
    /* de */ "musik abspielen", "spiel musik", "spiel musik ab",
             "mach musik an", "musik an", "spiel etwas musik",
    /* fr */ "joue de la musique", "mets de la musique", "lance la musique",
             "mets un peu de musique",
    /* es */ "pon música", "pon algo de música", "reproduce música",
             "pon un poco de música",
    /* pt */ "toca música", "coloca música", "põe música", "toca uma música",
    /* it */ "metti della musica", "riproduci musica", "metti su la musica",
             "metti un po' di musica",
    /* nl */ "speel muziek", "speel muziek af", "zet muziek op", "muziek aan",
    /* pl */ "włącz muzykę", "puść muzykę", "odtwórz muzykę",
    /* ru */ "включи музыку", "поставь музыку", "включить музыку",
    /* ja */ "音楽をかけて", "音楽を再生", "音楽を流して", "音楽かけて",
    /* zh */ "播放音乐", "放音乐", "放点音乐", "来点音乐",
    /* ko */ "음악 틀어줘", "음악 재생", "음악 틀어",
    /* hi */ "संगीत चलाओ", "गाना चलाओ", "म्यूज़िक चलाओ",
    /* ar */ "شغل الموسيقى", "شغّل الموسيقى", "شغل موسيقى",
    NULL
};

/* ── File browser ─────────────────────────────────────────── */
const char *const SYNSH_P_FILES[] = {
    /* en */ "open the file browser", "open file browser",
             "open the file manager", "open file manager", "open files",
             "browse files", "file browser", "file manager",
    /* de */ "dateimanager öffnen", "öffne den dateimanager", "dateimanager",
             "dateibrowser", "dateien öffnen", "öffne die dateien",
    /* fr */ "ouvrir le gestionnaire de fichiers", "gestionnaire de fichiers",
             "ouvre le gestionnaire de fichiers", "ouvrir les fichiers",
             "explorateur de fichiers",
    /* es */ "abrir el gestor de archivos", "gestor de archivos",
             "abre el gestor de archivos", "abrir archivos",
             "explorador de archivos",
    /* pt */ "abrir o gerenciador de arquivos", "gerenciador de arquivos",
             "abre o gerenciador de arquivos", "abrir arquivos",
             "explorador de arquivos",
    /* it */ "apri il gestore file", "gestore file", "apri i file",
             "gestore di file",
    /* nl */ "open de bestandsbeheerder", "bestandsbeheerder",
             "open bestanden", "verkenner",
    /* pl */ "otwórz menedżer plików", "menedżer plików", "otwórz pliki",
    /* ru */ "открой файловый менеджер", "файловый менеджер",
             "открыть файлы", "проводник",
    /* ja */ "ファイルマネージャーを開く", "ファイルマネージャ",
             "ファイルを開く",
    /* zh */ "打开文件管理器", "文件管理器", "打开文件",
    /* ko */ "파일 관리자 열기", "파일 관리자", "파일 열기",
    /* hi */ "फ़ाइल मैनेजर खोलो", "फ़ाइल मैनेजर",
    /* ar */ "افتح مدير الملفات", "مدير الملفات",
    NULL
};

/* ── YouTube ──────────────────────────────────────────────── */
const char *const SYNSH_P_YOUTUBE[] = {
    /* en */ "open youtube", "youtube", "open yt", "launch youtube",
    /* de */ "youtube öffnen", "öffne youtube",
    /* fr */ "ouvrir youtube", "ouvre youtube",
    /* es */ "abrir youtube", "abre youtube",
    /* pt */ "abrir youtube", "abre o youtube",
    /* it */ "apri youtube",
    /* nl */ "open youtube",
    /* pl */ "otwórz youtube",
    /* ru */ "открой youtube", "открыть ютуб", "ютуб",
    /* ja */ "youtubeを開く", "ユーチューブ",
    /* zh */ "打开youtube", "打开油管",
    /* ko */ "유튜브 열기", "유튜브",
    /* hi */ "यूट्यूब खोलो", "यूट्यूब",
    /* ar */ "افتح يوتيوب", "يوتيوب",
    NULL
};

/* ── System update ────────────────────────────────────────── */
const char *const SYNSH_P_UPDATE[] = {
    /* en */ "update", "update system", "update the system", "upgrade",
             "upgrade system", "upgrade the system", "update everything",
             "check for updates", "system update",
    /* de */ "aktualisieren", "system aktualisieren",
             "das system aktualisieren", "nach updates suchen",
             "updates suchen", "systemupdate", "alles aktualisieren",
    /* fr */ "mettre à jour", "mettre à jour le système", "mise à jour",
             "chercher les mises à jour", "tout mettre à jour",
    /* es */ "actualizar", "actualizar el sistema", "actualizar sistema",
             "buscar actualizaciones", "actualizar todo",
    /* pt */ "atualizar", "atualizar o sistema", "atualizar sistema",
             "procurar atualizações", "atualizar tudo",
    /* it */ "aggiorna", "aggiorna il sistema", "aggiornare il sistema",
             "cerca aggiornamenti", "aggiorna tutto",
    /* nl */ "bijwerken", "systeem bijwerken", "updaten",
             "controleer op updates", "alles bijwerken",
    /* pl */ "aktualizuj", "zaktualizuj system", "aktualizacja systemu",
             "sprawdź aktualizacje",
    /* ru */ "обнови систему", "обновить систему", "обновление",
             "проверить обновления", "обнови всё",
    /* ja */ "システムを更新", "更新", "アップデート", "更新を確認",
    /* zh */ "更新系统", "更新", "检查更新", "升级系统",
    /* ko */ "시스템 업데이트", "업데이트", "업데이트 확인",
    /* hi */ "सिस्टम अपडेट करो", "अपडेट करो", "अपडेट",
    /* ar */ "حدث النظام", "تحديث النظام", "تحديث",
    NULL
};

/* ── Orphaned packages ────────────────────────────────────── */
const char *const SYNSH_P_ORPHANS[] = {
    /* en */ "remove orphans", "clean orphans", "remove orphaned packages",
    /* de */ "verwaiste pakete entfernen", "waisen entfernen",
    /* fr */ "supprimer les paquets orphelins", "nettoyer les orphelins",
    /* es */ "eliminar paquetes huérfanos", "limpiar huérfanos",
    /* pt */ "remover pacotes órfãos", "limpar órfãos",
    /* it */ "rimuovi i pacchetti orfani", "pulisci gli orfani",
    /* nl */ "verweesde pakketten verwijderen",
    /* pl */ "usuń osierocone pakiety",
    /* ru */ "удалить осиротевшие пакеты", "удалить сироты",
    /* ja */ "孤立パッケージを削除",
    /* zh */ "删除孤立软件包", "清理孤立包",
    /* ko */ "고아 패키지 삭제",
    /* ar */ "احذف الحزم اليتيمة",
    NULL
};

/* ── "what can you do" ────────────────────────────────────── */
/*
 * ⚠ BARE "help" IS NOT HERE AND MUST NOT BE. `help` is a built-in, and intents
 * are checked BEFORE the classifier — an entry for it would take the built-in
 * away from itself. The non-English words for help have no such clash, which
 * is the whole reason this list is worth having: `hilfe` currently reaches
 * nothing at all.
 */
const char *const SYNSH_P_CANDO[] = {
    /* en */ "what can you do", "what can i say", "what can i ask",
    /* de */ "hilfe", "was kannst du", "was kannst du tun", "was kann ich sagen",
    /* fr */ "aide", "que peux-tu faire", "qu'est-ce que tu sais faire",
    /* es */ "ayuda", "qué puedes hacer", "qué puedo decir",
    /* pt */ "ajuda", "o que você pode fazer", "o que posso dizer",
    /* it */ "aiuto", "cosa sai fare", "cosa posso dire",
    /* nl */ "hulp", "wat kun je", "wat kan ik zeggen",
    /* pl */ "pomoc", "co potrafisz", "co mogę powiedzieć",
    /* ru */ "помощь", "что ты умеешь", "что можно сказать",
    /* ja */ "何ができる", "何ができますか", "ヘルプ",
    /* zh */ "帮助", "你能做什么", "我可以说什么",
    /* ko */ "도움말", "무엇을 할 수 있어", "뭘 할 수 있어",
    /* hi */ "मदद", "तुम क्या कर सकते हो",
    /* ar */ "مساعدة", "ماذا يمكنك أن تفعل",
    NULL
};

/* ── Packages: install ────────────────────────────────────── */
/*
 * `install` is coreutils, which is why intents.c also refuses any argument
 * carrying a flag or a path. The verb-final entries at the bottom are why this
 * is a circumfix table: "firefox をインストール" is not a prefix of anything.
 */
const synsh_circumfix_t SYNSH_C_INSTALL[] = {
    { "install",          "" },
    { "installiere",      "" },
    { "installier",       "" },
    { "installieren",     "" },
    { "installe",         "" },
    { "installer",        "" },
    { "instala",          "" },
    { "instalar",         "" },
    { "instale",          "" },
    { "installa",         "" },
    { "installare",       "" },
    { "installeer",       "" },
    { "installeren",      "" },
    { "zainstaluj",       "" },
    { "instaluj",         "" },
    { "установи",         "" },
    { "установить",       "" },
    { "поставь",          "" },
    { "安装",              "" },
    { "ثبت",              "" },
    /* verb-final */
    { "", "をインストール" },
    { "", "をインストールして" },
    { "", "インストール" },
    { "", "설치해줘" },
    { "", "설치" },
    { "", "इंस्टॉल करो" },
    { "", "स्थापित करो" },
    { NULL, NULL }
};

/* ── Packages: uninstall ──────────────────────────────────── */
const synsh_circumfix_t SYNSH_C_UNINSTALL[] = {
    { "uninstall",             "" },
    { "remove package",        "" },
    { "deinstalliere",         "" },
    { "deinstallieren",        "" },
    { "paket entfernen",       "" },
    { "désinstalle",           "" },
    { "désinstaller",          "" },
    { "desinstala",            "" },
    { "desinstalar",           "" },
    { "disinstalla",           "" },
    { "verwijder pakket",      "" },
    { "deinstalleer",          "" },
    { "odinstaluj",            "" },
    { "удали пакет",           "" },
    { "удалить пакет",         "" },
    { "卸载",                   "" },
    { "أزل",                   "" },
    { "", "をアンインストール" },
    { "", "アンインストール" },
    { "", "삭제해줘" },
    { "", "제거" },
    { "", "अनइंस्टॉल करो" },
    { NULL, NULL }
};

/* ── Packages: search ─────────────────────────────────────── */
/* Longest first: "search for X" would otherwise be answered with an argument
 * that begins "for". */
const synsh_circumfix_t SYNSH_C_SEARCH[] = {
    { "search for",            "" },
    { "look for",              "" },
    { "search",                "" },
    { "suche nach",            "" },
    { "such nach",             "" },
    { "suche",                 "" },
    { "chercher",              "" },
    { "cherche",               "" },
    { "rechercher",            "" },
    { "busca",                 "" },
    { "buscar",                "" },
    { "procurar",              "" },
    { "procura",               "" },
    { "cerca",                 "" },
    { "cercare",               "" },
    { "zoek naar",             "" },
    { "zoek",                  "" },
    { "szukaj",                "" },
    { "wyszukaj",              "" },
    { "найди",                 "" },
    { "найти",                 "" },
    { "поиск",                 "" },
    { "搜索",                   "" },
    { "查找",                   "" },
    { "ابحث عن",               "" },
    { "", "を検索" },
    { "", "検索" },
    { "", "검색" },
    { "", "खोजो" },
    { NULL, NULL }
};

/* ── Packages: "is X installed" ───────────────────────────── */
const synsh_circumfix_t SYNSH_C_ISINSTALLED[] = {
    { "is",            "installed" },
    { "ist",           "installiert" },
    { "est-ce que",    "est installé" },
    { "est-ce que",    "est installe" },
    { "",              "est-il installé" },
    { "está",          "instalado" },
    { "",              "está instalado" },
    { "",              "está instalada" },
    { "",              "está instalado?" },
    { "",              "è installato" },
    { "",              "is geïnstalleerd" },
    { "is",            "geïnstalleerd" },
    { "",              "jest zainstalowany" },
    { "",              "установлен" },
    { "",              "установлена" },
    { "",              "はインストールされている" },
    { "",              "はインストール済み" },
    { "",              "装了吗" },
    { "",              "安装了吗" },
    { "",              "설치되어 있어" },
    { "",              "설치돼 있나" },
    { "",              "इंस्टॉल है" },
    { "هل",            "مثبت" },
    { NULL, NULL }
};

/* ── Alarms ───────────────────────────────────────────────── */
/*
 * The clock is read out of the WHOLE line by parse_clock(), so these entries
 * only have to recognise that an alarm is what is being asked for. That is why
 * a bare verb-final "起こして" is enough — the "7時に" in front of it is what
 * parse_clock reads.
 */
const synsh_circumfix_t SYNSH_C_ALARM[] = {
    { "set an alarm",     "" },
    { "set alarm",        "" },
    { "wake me",          "" },
    { "stell einen wecker", "" },
    { "stelle einen wecker", "" },
    { "wecker",           "" },
    { "weck mich",        "" },
    { "réveille-moi",     "" },
    { "mets un réveil",   "" },
    { "règle un réveil",  "" },
    { "despiértame",      "" },
    { "pon una alarma",   "" },
    { "pon un despertador", "" },
    { "me acorda",        "" },
    { "coloca um alarme", "" },
    { "svegliami",        "" },
    { "metti una sveglia", "" },
    { "wek me",           "" },
    { "zet een wekker",   "" },
    { "obudź mnie",       "" },
    { "ustaw budzik",     "" },
    { "разбуди меня",     "" },
    { "поставь будильник", "" },
    { "设个闹钟",           "" },
    { "定个闹钟",           "" },
    { "أيقظني",           "" },
    { "", "に起こして" },
    { "", "起こして" },
    { "", "にアラーム" },
    { "", "깨워줘" },
    { "", "알람 맞춰줘" },
    { "", "बजे जगाना" },
    { NULL, NULL }
};

/* ── The everyday commands ────────────────────────────────── */
/*
 * READ-ONLY, deliberately — the rule from intents.c, unchanged: if being
 * wrong would cost you data, it does not belong here. "delete the logs" stays
 * on the synapd path, which prints the command and waits.
 */
const synsh_everyday_t SYNSH_EVERYDAY[] = {

    { (const char *const[]){
        /* en */ "list files", "show files", "list the files",
                 "show me the files", "what files are here", "whats in here",
                 "what's in here", "what is in this directory",
                 "list directory",
        /* de */ "dateien auflisten", "zeig mir die dateien", "was ist hier",
                 "dateien anzeigen", "was liegt hier",
        /* fr */ "lister les fichiers", "montre les fichiers",
                 "qu'est-ce qu'il y a ici", "afficher les fichiers",
        /* es */ "listar archivos", "muestra los archivos", "qué hay aquí",
                 "lista de archivos",
        /* pt */ "listar arquivos", "mostra os arquivos", "o que tem aqui",
                 "listar os arquivos",
        /* it */ "elenca i file", "mostra i file", "cosa c'è qui",
        /* nl */ "toon bestanden", "laat de bestanden zien", "wat staat hier",
        /* pl */ "pokaż pliki", "lista plików", "co tu jest",
        /* ru */ "покажи файлы", "список файлов", "что здесь",
        /* ja */ "ファイル一覧", "ファイルを表示", "ここに何がある",
        /* zh */ "列出文件", "显示文件", "这里有什么",
        /* ko */ "파일 목록", "파일 보여줘",
        /* hi */ "फ़ाइलें दिखाओ", "फ़ाइलों की सूची",
        /* ar */ "اعرض الملفات", "قائمة الملفات",
        NULL },
      "ls -lh --color=auto" },

    { (const char *const[]){
        /* en */ "list all files", "show hidden files", "show all files",
                 "list everything",
        /* de */ "alle dateien anzeigen", "versteckte dateien anzeigen",
                 "zeig auch versteckte dateien",
        /* fr */ "afficher tous les fichiers", "montrer les fichiers cachés",
        /* es */ "mostrar todos los archivos", "mostrar archivos ocultos",
        /* pt */ "mostrar todos os arquivos", "mostrar arquivos ocultos",
        /* it */ "mostra tutti i file", "mostra i file nascosti",
        /* nl */ "toon alle bestanden", "toon verborgen bestanden",
        /* pl */ "pokaż wszystkie pliki", "pokaż ukryte pliki",
        /* ru */ "показать все файлы", "показать скрытые файлы",
        /* ja */ "隠しファイルも表示", "すべてのファイルを表示",
        /* zh */ "显示所有文件", "显示隐藏文件",
        /* ko */ "숨김 파일도 보여줘",
        /* ar */ "اعرض كل الملفات",
        NULL },
      "ls -lha --color=auto" },

    { (const char *const[]){
        /* en */ "where am i", "what directory am i in", "current directory",
                 "print working directory", "which directory is this",
        /* de */ "wo bin ich", "in welchem verzeichnis bin ich",
                 "aktuelles verzeichnis",
        /* fr */ "où suis-je", "dans quel dossier suis-je",
                 "dossier courant", "répertoire courant",
        /* es */ "dónde estoy", "en qué directorio estoy",
                 "directorio actual", "carpeta actual",
        /* pt */ "onde estou", "em que diretório estou", "diretório atual",
                 "pasta atual",
        /* it */ "dove sono", "in quale cartella sono", "cartella corrente",
        /* nl */ "waar ben ik", "huidige map", "in welke map ben ik",
        /* pl */ "gdzie jestem", "bieżący katalog", "w jakim katalogu jestem",
        /* ru */ "где я", "текущий каталог", "в какой я папке",
        /* ja */ "今どこ", "カレントディレクトリ", "現在のディレクトリ",
        /* zh */ "我在哪", "当前目录", "现在在哪个目录",
        /* ko */ "여기 어디", "현재 디렉터리",
        /* hi */ "मैं कहाँ हूँ", "वर्तमान डायरेक्टरी",
        /* ar */ "أين أنا", "المجلد الحالي",
        NULL },
      "pwd" },

    { (const char *const[]){
        /* en */ "disk space", "free space", "how much disk space",
                 "how much space is left", "show disk usage", "disk usage",
                 "df",
        /* de */ "speicherplatz", "freier speicherplatz",
                 "wie viel platz ist noch frei", "festplattenbelegung",
        /* fr */ "espace disque", "espace libre",
                 "combien d'espace reste-t-il", "utilisation du disque",
        /* es */ "espacio en disco", "espacio libre",
                 "cuánto espacio queda", "uso del disco",
        /* pt */ "espaço em disco", "espaço livre", "quanto espaço sobrou",
                 "uso do disco",
        /* it */ "spazio su disco", "spazio libero", "quanto spazio resta",
        /* nl */ "schijfruimte", "vrije ruimte", "hoeveel ruimte is er nog",
        /* pl */ "miejsce na dysku", "wolne miejsce", "ile miejsca zostało",
        /* ru */ "место на диске", "свободное место", "сколько места осталось",
        /* ja */ "ディスクの空き", "空き容量", "ディスク使用量",
        /* zh */ "磁盘空间", "剩余空间", "磁盘使用情况",
        /* ko */ "디스크 공간", "남은 용량",
        /* hi */ "डिस्क स्पेस", "कितनी जगह बची है",
        /* ar */ "مساحة القرص", "المساحة الحرة",
        NULL },
      "df -h" },

    { (const char *const[]){
        /* en */ "memory usage", "how much memory", "how much ram",
                 "ram usage", "free memory", "show memory",
        /* de */ "speicherverbrauch", "wie viel arbeitsspeicher",
                 "wie viel ram", "freier arbeitsspeicher",
        /* fr */ "utilisation de la mémoire", "combien de mémoire",
                 "mémoire libre", "combien de ram",
        /* es */ "uso de memoria", "cuánta memoria", "cuánta ram",
                 "memoria libre",
        /* pt */ "uso de memória", "quanta memória", "quanta ram",
                 "memória livre",
        /* it */ "uso della memoria", "quanta memoria", "memoria libera",
        /* nl */ "geheugengebruik", "hoeveel geheugen", "vrij geheugen",
        /* pl */ "zużycie pamięci", "ile pamięci", "wolna pamięć",
        /* ru */ "использование памяти", "сколько памяти",
                 "свободная память",
        /* ja */ "メモリ使用量", "メモリの空き", "メモリはどれくらい",
        /* zh */ "内存使用", "剩余内存", "内存还有多少",
        /* ko */ "메모리 사용량", "남은 메모리",
        /* hi */ "मेमोरी उपयोग", "कितनी रैम",
        /* ar */ "استخدام الذاكرة", "كم الذاكرة",
        NULL },
      "free -h" },

    { (const char *const[]){
        /* en */ "whats running", "what's running", "show processes",
                 "list processes", "what is running",
        /* de */ "was läuft", "laufende prozesse", "prozesse anzeigen",
        /* fr */ "qu'est-ce qui tourne", "processus en cours",
                 "afficher les processus",
        /* es */ "qué se está ejecutando", "procesos en ejecución",
                 "mostrar procesos",
        /* pt */ "o que está rodando", "processos em execução",
                 "mostrar processos",
        /* it */ "cosa sta girando", "processi in esecuzione",
                 "mostra i processi",
        /* nl */ "wat draait er", "lopende processen", "toon processen",
        /* pl */ "co działa", "uruchomione procesy", "pokaż procesy",
        /* ru */ "что запущено", "запущенные процессы", "покажи процессы",
        /* ja */ "何が動いている", "プロセス一覧", "実行中のプロセス",
        /* zh */ "有什么在运行", "进程列表", "显示进程",
        /* ko */ "실행 중인 것", "프로세스 목록",
        /* hi */ "क्या चल रहा है", "प्रोसेस दिखाओ",
        /* ar */ "ما الذي يعمل", "اعرض العمليات",
        NULL },
      "ps -eo pid,pcpu,pmem,comm --sort=-pcpu | head -15" },

    { (const char *const[]){
        /* en */ "who am i", "what user am i", "my username",
        /* de */ "wer bin ich", "mein benutzername",
        /* fr */ "qui suis-je", "mon nom d'utilisateur",
        /* es */ "quién soy", "mi usuario", "mi nombre de usuario",
        /* pt */ "quem sou eu", "meu usuário",
        /* it */ "chi sono", "il mio utente",
        /* nl */ "wie ben ik", "mijn gebruikersnaam",
        /* pl */ "kim jestem", "moja nazwa użytkownika",
        /* ru */ "кто я", "мое имя пользователя",
        /* ja */ "私は誰", "ユーザー名",
        /* zh */ "我是谁", "我的用户名",
        /* ko */ "나 누구",
        /* ar */ "من أنا",
        NULL },
      "whoami" },

    { (const char *const[]){
        /* en */ "my ip", "my ip address", "what's my ip", "whats my ip",
                 "ip address", "show my ip",
        /* de */ "meine ip", "meine ip-adresse", "wie lautet meine ip",
        /* fr */ "mon ip", "mon adresse ip", "quelle est mon ip",
        /* es */ "mi ip", "mi dirección ip", "cuál es mi ip",
        /* pt */ "meu ip", "meu endereço ip", "qual é o meu ip",
        /* it */ "il mio ip", "il mio indirizzo ip",
        /* nl */ "mijn ip", "mijn ip-adres",
        /* pl */ "moje ip", "mój adres ip",
        /* ru */ "мой ip", "мой ip адрес", "какой у меня ip",
        /* ja */ "私のip", "ipアドレス",
        /* zh */ "我的ip", "ip地址",
        /* ko */ "내 ip", "ip 주소",
        /* hi */ "मेरा आईपी",
        /* ar */ "عنوان الآي بي",
        NULL },
      "ip -brief address" },

    { (const char *const[]){
        /* en */ "uptime", "how long has this been up",
                 "how long has the system been running",
        /* de */ "laufzeit", "wie lange läuft das system schon",
        /* fr */ "depuis combien de temps", "temps de fonctionnement",
        /* es */ "cuánto lleva encendido", "tiempo encendido",
        /* pt */ "há quanto tempo está ligado", "tempo ligado",
        /* it */ "da quanto tempo è acceso",
        /* nl */ "hoe lang draait dit al",
        /* pl */ "jak długo działa system",
        /* ru */ "сколько работает система", "время работы",
        /* ja */ "稼働時間", "どれくらい動いている",
        /* zh */ "运行了多久", "开机时间",
        /* ko */ "얼마나 켜져 있었어",
        /* ar */ "مدة التشغيل",
        NULL },
      "uptime -p" },

    { (const char *const[]){
        /* en */ "kernel version", "what kernel", "what kernel am i running",
                 "kernel",
        /* de */ "kernel-version", "welcher kernel", "welchen kernel habe ich",
        /* fr */ "version du noyau", "quel noyau",
        /* es */ "versión del kernel", "qué kernel", "versión del núcleo",
        /* pt */ "versão do kernel", "qual kernel",
        /* it */ "versione del kernel", "quale kernel",
        /* nl */ "kernelversie", "welke kernel",
        /* pl */ "wersja jądra", "jakie jądro",
        /* ru */ "версия ядра", "какое ядро",
        /* ja */ "カーネルのバージョン", "カーネル",
        /* zh */ "内核版本", "什么内核",
        /* ko */ "커널 버전",
        /* ar */ "إصدار النواة",
        NULL },
      "uname -r" },

    { (const char *const[]){
        /* en */ "show the log", "show the logs", "system log",
                 "recent errors", "show errors",
        /* de */ "zeig das log", "systemprotokoll", "letzte fehler",
                 "fehler anzeigen",
        /* fr */ "montre les journaux", "journal système",
                 "erreurs récentes", "afficher les erreurs",
        /* es */ "muestra el registro", "registro del sistema",
                 "errores recientes", "mostrar errores",
        /* pt */ "mostra os logs", "log do sistema", "erros recentes",
        /* it */ "mostra i log", "registro di sistema", "errori recenti",
        /* nl */ "toon de logs", "systeemlog", "recente fouten",
        /* pl */ "pokaż logi", "dziennik systemu", "ostatnie błędy",
        /* ru */ "покажи логи", "системный журнал", "последние ошибки",
        /* ja */ "ログを表示", "システムログ", "最近のエラー",
        /* zh */ "显示日志", "系统日志", "最近的错误",
        /* ko */ "로그 보여줘", "시스템 로그",
        /* ar */ "اعرض السجل", "سجل النظام",
        NULL },
      /* journalctl, never /var/log/syslog — this is not Debian. */
      "journalctl -p err -n 30 --no-pager" },

    { (const char *const[]){
        /* en */ "failed services", "what failed", "show failed",
        /* de */ "fehlgeschlagene dienste", "was ist fehlgeschlagen",
        /* fr */ "services en échec", "qu'est-ce qui a échoué",
        /* es */ "servicios fallidos", "qué falló",
        /* pt */ "serviços com falha", "o que falhou",
        /* it */ "servizi falliti", "cosa è fallito",
        /* nl */ "mislukte services", "wat is er mislukt",
        /* pl */ "nieudane usługi", "co się nie udało",
        /* ru */ "неудачные службы", "что упало",
        /* ja */ "失敗したサービス",
        /* zh */ "失败的服务", "哪些服务失败了",
        /* ko */ "실패한 서비스",
        /* ar */ "الخدمات الفاشلة",
        NULL },
      "systemctl --failed --no-pager" },

    { (const char *const[]){
        /* en */ "gpu", "gpu usage", "show gpu", "graphics card",
        /* de */ "grafikkarte", "gpu-auslastung",
        /* fr */ "carte graphique", "utilisation du gpu",
        /* es */ "tarjeta gráfica", "uso de la gpu",
        /* pt */ "placa de vídeo", "uso da gpu",
        /* it */ "scheda grafica", "uso della gpu",
        /* nl */ "videokaart", "gpu-gebruik",
        /* pl */ "karta graficzna", "użycie gpu",
        /* ru */ "видеокарта", "загрузка gpu",
        /* ja */ "グラフィックカード", "gpu使用率",
        /* zh */ "显卡", "显卡占用",
        /* ko */ "그래픽 카드",
        /* ar */ "كرت الشاشة",
        NULL },
      "nvidia-smi" },

    { (const char *const[]){
        /* en */ "temperature", "temps", "how hot", "cpu temperature",
        /* de */ "temperatur", "wie warm ist es", "cpu-temperatur",
        /* fr */ "température", "température du processeur",
        /* es */ "temperatura", "temperatura de la cpu",
        /* pt */ "temperatura", "temperatura da cpu",
        /* it */ "temperatura", "temperatura della cpu",
        /* nl */ "temperatuur", "hoe warm",
        /* pl */ "temperatura", "temperatura procesora",
        /* ru */ "температура", "температура процессора",
        /* ja */ "温度", "cpuの温度",
        /* zh */ "温度", "cpu温度",
        /* ko */ "온도",
        /* ar */ "درجة الحرارة",
        NULL },
      "sensors" },

    { NULL, NULL }
};

/* ── The circumfix matcher ────────────────────────────────── */
/*
 * A boundary is required at each end so that a phrase cannot claim a longer
 * word: "install" must not match "installation notes". For a Latin-script
 * phrase that boundary is a space; for one ending in a non-ASCII character it
 * is not, because Japanese and Chinese do not put spaces between words and
 * demanding one there would mean these entries never matched anything.
 */
static bool ends_ascii(const char *s, size_t n)
{
    return n > 0 && (unsigned char)s[n - 1] < 0x80;
}

static bool starts_ascii(const char *s)
{
    return s && (unsigned char)s[0] < 0x80;
}

bool synsh_phrase_arg(const char *folded, const synsh_circumfix_t *tab,
                      char *arg, size_t n)
{
    if (!folded || !tab || !arg || !n) return false;
    size_t llen = strlen(folded);

    for (int i = 0; tab[i].before; i++)
    for (int spelling = 0; spelling < 2; spelling++) {
        /* Pass 0 strips accents, pass 1 uses the German ae/oe/ue spelling —
         * both are correct German and people type both. See strip_accent(). */
        char b[256], a[256];
        if (spelling) {
            synsh_fold_translit(b, sizeof(b), tab[i].before);
            synsh_fold_translit(a, sizeof(a), tab[i].after);
        } else {
            synsh_fold(b, sizeof(b), tab[i].before);
            synsh_fold(a, sizeof(a), tab[i].after);
        }

        size_t bl = strlen(b), al = strlen(a);
        if (!bl && !al) continue;                 /* would claim every line */
        if (bl + al > llen) continue;

        if (bl) {
            if (strncmp(folded, b, bl) != 0) continue;
            /* Boundary after the opening phrase. */
            if (ends_ascii(b, bl) && folded[bl] && folded[bl] != ' ') continue;
        }
        if (al) {
            if (strcmp(folded + llen - al, a) != 0) continue;
            /* Boundary before the closing phrase. */
            if (starts_ascii(a) && llen - al > 0 && folded[llen - al - 1] != ' ')
                continue;
        }

        /* What is left between the two ends is the argument. */
        size_t s = bl, e = llen - al;
        while (s < e && folded[s] == ' ') s++;
        while (e > s && folded[e - 1] == ' ') e--;
        size_t len = e - s;
        if (len >= n) len = n - 1;
        memcpy(arg, folded + s, len);
        arg[len] = '\0';
        return true;
    }
    return false;
}
