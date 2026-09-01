# Русский (ru) — syn-install's own words.
#
# ⚠ THE KEY IS THE ENGLISH SENTENCE. A translation whose key matches nothing in
# syn-install.sh is dead text that will never appear again, and it is the only
# evidence that an English string was edited — tests/i18n_test.sh fails on one.
#
# An entry left out prints the English. That is not a failure state: an
# installer that says one screen in English is usable, and one that says a
# screen wrong is not.
#
# ⚠ THE ANSWER KEYS STAY [Y/n], [y/N] AND 'yes'. The prompts around them are
# translated; the letters are not, because the script compares against y/n and
# the literal word yes. A translated key would be a question whose own answer
# does not work.
#
# Edit by hand, or with tools/i18n-fill.py, which matches on the English rather
# than on a line number.
#
# SPDX-License-Identifier: GPL-2.0-or-later

declare -gA SYN_T=(
  ["render.nix is missing — the 'syn' package is not installed here."]="render.nix отсутствует — пакет 'syn' здесь не установлен."
  ["locale-gen failed. The live session stays in English; the install is
  unaffected, because it generates the locale inside the target."]="locale-gen не сработал. Live-сеанс останется на английском; на установку
  это не влияет, она создаёт локаль внутри целевой системы."
  ["  The keyboard, the clock, the fonts and the shell all follow this.
  You can change any of it later."]="  От этого зависят клавиатура, часы, шрифты и оболочка.
  Всё это можно изменить позже."
  ["Toggle [numbers, 'all', 'none', Enter = accept]:"]="Переключить [номера, 'all', 'none', Enter = принять]:"
  ["--config needs a file"]="--config требует файл"
  ["syn-install must be run as root"]="syn-install нужно запускать от root"
  ["  SynapseOS is running from the live image."]="  SynapseOS работает с live-образа."
  ["Starting the desktop — the installer opens with it."]="Запускается рабочий стол — установщик откроется вместе с ним."
  ["  This installer will:
    1. Partition a disk
    2. Install SynapseOS base system
    3. Install SynapseOS packages
    4. Create user account
    5. Choose desktop environment
    6. Configure system & bootloader"]="  Этот установщик:
    1. Разметит диск
    2. Установит базовую систему SynapseOS
    3. Установит пакеты SynapseOS
    4. Создаст учётную запись пользователя
    5. Даст выбрать рабочее окружение
    6. Настроит систему и загрузчик"
  ["ALL DATA ON THE TARGET DISK WILL BE ERASED"]="ВСЕ ДАННЫЕ НА ЦЕЛЕВОМ ДИСКЕ БУДУТ СТЁРТЫ"
  ["Press ENTER to continue or Ctrl+C to abort..."]="Нажмите ENTER, чтобы продолжить, или Ctrl+C, чтобы прервать..."
  ["Checking network"]="Проверка сети"
  ["Network is up"]="Сеть работает"
  ["  No network detected. Starting NetworkManager..."]="  Сеть не обнаружена. Запускается NetworkManager..."
  ["  No connection — but this machine has Wi-Fi."]="  Соединения нет — но у этой машины есть Wi-Fi."
  ["Open the Wi-Fi picker (nmtui)? [Y/n]:"]="Открыть выбор сети Wi-Fi (nmtui)? [Y/n]:"
  ["No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."]="Нет сетевого соединения и нет устройства Wi-Fi для настройки.
  SynapseOS скачивает базовую систему во время установки, так что подключите
  кабель и запустите заново."
  ["Network connected"]="Сеть подключена"
  ["Step 1 — Select Target Disk"]="Шаг 1 — Выбор целевого диска"
  ["  Available disks:"]="  Доступные диски:"
  ["Target disk (e.g. sda, vda, nvme0n1):"]="Целевой диск (например sda, vda, nvme0n1):"
  ["Target disk is in use. Unmount its partitions and re-run."]="Целевой диск занят. Отмонтируйте его разделы и запустите заново."
  ["Boot mode: UEFI"]="Режим загрузки: UEFI"
  ["Boot mode: BIOS/Legacy"]="Режим загрузки: BIOS/Legacy"
  ["  Encrypts the root filesystem with LUKS2. You will be asked for the
  passphrase at every boot, before the system starts."]="  Шифрует корневую файловую систему с помощью LUKS2. Пароль будет
  спрашиваться при каждой загрузке, до запуска системы."
  ["Encrypt the disk? [y/N]:"]="Зашифровать диск? [y/N]:"
  ["                          With encryption it is the BETTER choice: the
                          kernel lives on the EFI partition and only the
                          initramfs unlocks, so /boot needs no separate
                          unencrypted partition."]="                          С шифрованием это ЛУЧШИЙ выбор: ядро лежит
                          на разделе EFI и разблокирует только
                          initramfs, поэтому /boot не нужен отдельный
                          незашифрованный раздел."
  ["                          It copies each snapshot's kernel onto the EFI
                          partition, so that partition is made much
                          larger when snapshots are enabled."]="                          Он копирует ядро каждого снимка на раздел
                          EFI, поэтому этот раздел создаётся заметно
                          больше, когда снимки включены."
  ["  Snapshots are cheap but not free: they hold the old copy of
  anything that changes, so a disk near full stays near full."]="  Снимки дёшевы, но не бесплатны: они хранят старую копию всего, что
  меняется, так что почти полный диск таким и останется."
  ["Enable snapshots? [Y/n]:"]="Включить снимки? [Y/n]:"
  ["mkfs.ext4 is missing from this installer image — /boot cannot be created"]="в этом образе установщика нет mkfs.ext4 — /boot создать нельзя"
  ["btrfs is missing from this installer image — subvolumes cannot be created"]="в этом образе установщика нет btrfs — подтома создать нельзя"
  ["Are these correct? [Y/n]:"]="Всё верно? [Y/n]:"
  ["Starting the questions over — the disk has not been touched."]="Вопросы начинаются заново — диск не тронут."
  ["cryptsetup is not available on this installer image"]="cryptsetup недоступен в этом образе установщика"
  ["Encryption passphrase:"]="Пароль шифрования:"
  ["Repeat passphrase:"]="Повторите пароль:"
  ["Empty passphrase — that would leave the disk unprotected."]="Пустой пароль — диск остался бы без защиты."
  ["Passphrases did not match — try again."]="Пароли не совпали — попробуйте ещё раз."
  ["Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."]="В пароле меньше 8 символов. Короткий мало чего стоит против
  того, у кого диск в руках."
  ["Use it anyway? [y/N]:"]="Всё равно использовать? [y/N]:"
  ["Encryption enabled — root will be LUKS2"]="Шифрование включено — корень будет на LUKS2"
  ["cryptsetup open failed — the passphrase did not take"]="cryptsetup open не сработал — пароль не принят"
  ["Failed to mount root"]="Не удалось смонтировать корень"
  ["  Creating btrfs subvolumes..."]="  Создаются подтома btrfs..."
  ["btrfs: could not create @"]="btrfs: не удалось создать @"
  ["btrfs: could not create @home"]="btrfs: не удалось создать @home"
  ["btrfs: could not create @snapshots"]="btrfs: не удалось создать @snapshots"
  ["btrfs: could not create @var_log"]="btrfs: не удалось создать @var_log"
  ["btrfs: could not create @pkg"]="btrfs: не удалось создать @pkg"
  ["could not remount the btrfs root onto @"]="не удалось перемонтировать корень btrfs в @"
  ["Failed to mount @"]="Не удалось смонтировать @"
  ["Failed to mount @home"]="Не удалось смонтировать @home"
  ["Failed to mount @snapshots"]="Не удалось смонтировать @snapshots"
  ["Failed to mount @var_log"]="Не удалось смонтировать @var_log"
  ["Failed to mount @pkg"]="Не удалось смонтировать @pkg"
  ["This adds one partition in the free space. Back up anything irreplaceable first."]="Это добавит один раздел в свободном месте. Сначала сохраните всё незаменимое."
  ["Type 'yes' to install alongside:"]="Введите 'yes', чтобы установить рядом:"
  ["Aborted"]="Прервано"
  ["Failed to create the root partition"]="Не удалось создать корневой раздел"
  ["Could not identify the new partition after creating it"]="Не удалось опознать новый раздел после его создания"
  ["Failed to format root partition"]="Не удалось отформатировать корневой раздел"
  ["Failed to mount the existing ESP"]="Не удалось смонтировать существующий ESP"
  ["no partition editor on this image (cfdisk, fdisk and parted are all missing)"]="в этом образе нет редактора разделов (нет ни cfdisk, ни fdisk, ни parted)"
  ["  What this install needs:"]="  Что нужно этой установке:"
  ["    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"]="    • системный раздел EFI (тип EF00 / флаг 'esp') — можно использовать существующий"
  ["  Skipping the partition editor (--config)."]="  Редактор разделов пропущен (--config)."
  ["Format it? Everything on it is lost [y/N]:"]="Форматировать? Всё, что на нём есть, будет потеряно [y/N]:"
  ["Separate /boot partition:"]="Отдельный раздел /boot:"
  ["Swap partition (blank for none):"]="Раздел подкачки (пусто — без него):"
  ["Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"]="Пересоздать? Его UUID изменится и сломает fstab той системы [y/N]:"
  ["Type 'yes' to format these:"]="Введите 'yes', чтобы отформатировать их:"
  ["  Formatting EFI partition..."]="  Форматируется раздел EFI..."
  ["  Formatting /boot partition..."]="  Форматируется раздел /boot..."
  ["Failed to mount /boot"]="Не удалось смонтировать /boot"
  ["Type 'yes' to confirm:"]="Введите 'yes' для подтверждения:"
  ["  Creating GPT partition table..."]="  Создаётся таблица разделов GPT..."
  ["Failed to format EFI partition"]="Не удалось отформатировать раздел EFI"
  ["Failed to format boot partition"]="Не удалось отформатировать загрузочный раздел"
  ["  Creating MBR partition table..."]="  Создаётся таблица разделов MBR..."
  ["Disk partitioned and mounted at /mnt"]="Диск размечен и смонтирован в /mnt"
  ["Step 3 — Installing Base System"]="Шаг 3 — Установка базовой системы"
  ["  Initializing pacman keyring..."]="  Готовится связка ключей pacman..."
  ["  Running pacstrap (this may take several minutes)..."]="  Идёт pacstrap (это может занять несколько минут)..."
  ["pacstrap failed — check network connection"]="pacstrap не сработал — проверьте сетевое соединение"
  ["grub-install not found in chroot — attempting recovery..."]="grub-install не найден в chroot — попытка восстановления..."
  ["Could not install grub into target — check network"]="Не удалось установить grub в целевую систему — проверьте сеть"
  ["Base system installed"]="Базовая система установлена"
  ["Step 4 — Choose What to Install"]="Шаг 4 — Выбор того, что установить"
  ["  What should be installed alongside the SynapseOS core?"]="  Что установить рядом с ядром SynapseOS?"
  ["                   Bluetooth, printing, Wine, phone   (default)"]="                   Bluetooth, печать, Wine, телефон   (по умолчанию)"
  ["                   the ordinary software people install anyway"]="                   обычные программы, которые всё равно ставят"
  ["  Every preset except Minimal then asks WHICH AI model to download,
  and skipping it is one of the answers."]="  Все наборы, кроме Минимального, дальше спрашивают, КАКУЮ модель ИИ
  скачать, и пропустить её — один из ответов."
  ["Full install selected"]="Выбрана полная установка"
  ["Minimal install selected"]="Выбрана минимальная установка"
  ["  Two kinds of question. First the packages, as pages of
  checkboxes; then the handful of options that are a whole
  subsystem rather than a package."]="  Два рода вопросов. Сначала пакеты, страницами с флажками;
  потом горстка настроек, которые представляют собой целую
  подсистему, а не пакет."
  ["  And the software people install on the first evening anyway.
  All of it is in the Arch repositories; none of it is ours."]="  И программы, которые в первый же вечер всё равно ставят.
  Всё это в репозиториях Arch; ничего из этого не наше."
  ["  The rest is y/n. The default (shown in caps) is Standard."]="  Остальное — д/н. Значение по умолчанию (заглавными) — Стандарт."
  ["syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."]="syn-update снят: у этой машины не останется способа получить
  следующий пакет SynapseOS. Исправить это позже означает поставить его вручную
  с ISO или переустановить систему."
  ["Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."]="Не оставлены ни рабочий стол, ни служба ИИ. Это система Arch
  с несколькими инструментами SynapseOS, что является допустимым ответом —
  но ни в какой документации не описана машина, которая при этом получится."
  ["Custom install configured"]="Своя установка настроена"
  ["Standard install selected"]="Выбрана стандартная установка"
  ["  synapd loads one model and everything AI in SynapseOS talks to it:
  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,
  over this connection, onto the disk you are installing to."]="  synapd загружает одну модель, и всё, что связано с ИИ в SynapseOS, говорит с ней:
  synsh, панель ИИ на рабочем столе, Chibi, Vibe. Она скачивается сейчас,
  по этому соединению, на диск, на который вы ставите систему."
  ["  A smaller model is not just faster and lighter: it follows
  instructions worse. synsh mistakes what you asked for, Vibe's code
  needs more fixing, Chibi loses the thread. Take the default unless
  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."]="  Модель поменьше не просто быстрее и легче: она хуже следует
  указаниям. synsh неверно понимает просьбу, код Vibe требует больше
  правок, Chibi теряет нить. Берите значение по умолчанию, если только
  диск или память не говорят иначе — 7B хочет ~6 ГБ свободной памяти."
  ["  Whatever you pick, it can be changed later: 'syn model download',
  or Super+C ▸ System ▸ AI model on the desktop."]="  Что бы вы ни выбрали, это можно поменять позже: 'syn model download'
  или Super+C ▸ Система ▸ Модель ИИ на рабочем столе."
  ["Install this selection? [Y/n]:"]="Установить этот набор? [Y/n]:"
  ["Choosing again — nothing has been installed yet."]="Выбираем заново — пока ничего не установлено."
  ["Step 4b — Installing SynapseOS"]="Шаг 4b — Установка SynapseOS"
  ["Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."]="Не удалось включить ILoveCandy в /etc/pacman.conf (только внешний вид)."
  ["  Enabling [multilib] (32-bit repo, needed by Steam)..."]="  Включается [multilib] (32-битный репозиторий, нужен Steam)..."
  ["Could not sync the multilib database — Steam may fail to install"]="Не удалось синхронизировать базу multilib — Steam может не установиться"
  ["Could not enable [multilib]; Steam will be skipped."]="Не удалось включить [multilib]; Steam будет пропущен."
  ["Some SynapseOS packages failed to install — verifying below"]="Некоторые пакеты SynapseOS не установились — проверка ниже"
  ["No SynapseOS packages were selected. This will be an Arch system."]="Пакеты SynapseOS не выбраны. Это будет система Arch."
  ["SynapseOS packages installed"]="Пакеты SynapseOS установлены"
  ["Component selection recorded in /etc/synapseos/components.conf"]="Выбор компонентов записан в /etc/synapseos/components.conf"
  ["Step 5 — Create User Account"]="Шаг 5 — Создание учётной записи"
  ["  Create a user account for the installed system."]="  Создайте учётную запись для устанавливаемой системы."
  ["Username [default: syn]:"]="Имя пользователя [по умолчанию: syn]:"
  ["Full name (optional):"]="Полное имя (необязательно):"
  ["Password:"]="Пароль:"
  ["Confirm password:"]="Подтвердите пароль:"
  ["Passwords do not match or are empty — try again"]="Пароли не совпадают или пусты — попробуйте ещё раз"
  ["Step 6 — Desktop Environment"]="Шаг 6 — Рабочее окружение"
  ["  Choose a desktop environment:"]="  Выберите рабочее окружение:"
  ["  Installing KDE Plasma..."]="  Устанавливается KDE Plasma..."
  ["Some KDE packages failed to install"]="Некоторые пакеты KDE не установились"
  ["KDE Plasma installed"]="KDE Plasma установлена"
  ["  Installing GNOME..."]="  Устанавливается GNOME..."
  ["Some GNOME packages failed to install"]="Некоторые пакеты GNOME не установились"
  ["GNOME installed (session only — SynapseOS apps, not GNOME's)"]="GNOME установлен (только сеанс — программы SynapseOS, а не программы GNOME)"
  ["  Installing greetd (login screen) + desktop extras..."]="  Устанавливается greetd (экран входа) и дополнения рабочего стола..."
  ["greetd failed to install — boot falls back to getty login"]="greetd не установился — загрузка вернётся ко входу через getty"
  ["SynapseUI selected (included)"]="Выбран SynapseUI (входит в состав)"
  ["Installing Wine"]="Устанавливается Wine"
  ["Wine installed"]="Wine установлен"
  ["wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."]="wine не установился — файлы .exe/.msi из Windows не запустятся.
  Установите его позже: 'sudo pacman -S wine wine-mono'."
  ["Configuring Video Driver"]="Настройка видеодрайвера"
  ["  Virtual machine — installing mesa (synui uses pixman here)..."]="  Виртуальная машина — устанавливается mesa (synui использует здесь pixman)..."
  ["mesa failed to install"]="mesa не установилась"
  ["NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"]="Установка драйвера NVIDIA не удалась — система загрузилась бы на
  nouveau, а отрисовщик synui так и не запустился бы"
  ["NVIDIA sleep services enabled (VRAM save/restore)"]="Службы сна NVIDIA включены (сохранение/восстановление видеопамяти)"
  ["Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"]="Не удалось включить nvidia-{suspend,resume,hibernate} — при засыпании
  экран может остаться чёрным, если позже включить NVreg_PreserveVideoMemoryAllocations"
  ["  synapd can run inference on this GPU instead of the CPU.
  This downloads the CUDA runtime (~4.7 GiB installed)."]="  synapd может считать на этом GPU вместо процессора.
  Для этого скачивается среда CUDA (~4,7 ГиБ после установки)."
  ["Enable GPU inference? [Y/n]:"]="Включить вычисления на GPU? [Y/n]:"
  ["Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda"]="Остаёмся на вычислениях процессором. Переключиться позже:
  sudo pacman -S synapse-llama-cuda"
  ["  Installing synapse-llama-cuda (this takes a while)..."]="  Устанавливается synapse-llama-cuda (это займёт время)..."
  ["This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"]="В этом ISO нет сборки llama для GPU, поэтому synapd будет считать на
  процессоре несмотря на карту NVIDIA. (ISO нужно собирать на машине с набором
  CUDA, иначе synapse-llama-cuda просто не существует.)"
  ["Video driver install failed — synui may fall back to software rendering"]="Установка видеодрайвера не удалась — synui может перейти на программную отрисовку"
  ["Video drivers installed"]="Видеодрайверы установлены"
  ["  Enabling GPU inference (synapse-llama-vulkan)..."]="  Включаются вычисления на GPU (synapse-llama-vulkan)..."
  ["This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"]="В этом ISO нет сборки llama для Vulkan, поэтому synapd будет считать на
  процессоре несмотря на GPU AMD/Intel. (Собирайте ISO на машине с 'shaderc' +
  vulkan-headers, иначе synapse-llama-vulkan не существует.)"
  ["Installing Steam and the game stack"]="Установка Steam и игрового набора"
  ["  Installing steam and the 32-bit runtime libraries..."]="  Устанавливаются steam и 32-битные библиотеки..."
  ["Steam installed (native multilib package)"]="Steam установлен (родной пакет multilib)"
  ["steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."]="steam не установился. В остальном система полная —
  установите его позже: 'sudo pacman -S steam' ([multilib] уже
  включён в /etc/pacman.conf)."
  ["  Installing the game stack (overlay, governor, micro-compositor)..."]="  Устанавливается игровой набор (оверлей, регулятор, микрокомпозитор)..."
  ["Game stack installed (mangohud, gamemode, gamescope)"]="Игровой набор установлен (mangohud, gamemode, gamescope)"
  ["The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"]="Игровой набор не установился. Steam всё равно работает; оверлей
  FPS, регулятор CPU/GPU и 'synui-game-run --gamescope' — нет.
  Установите их позже:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
  ["Installing CachyOS Proton"]="Устанавливается CachyOS Proton"
  ["  Fetching the CachyOS keyring and mirrorlist..."]="  Загружаются связка ключей и список зеркал CachyOS..."
  ["  Trusting the CachyOS master key..."]="  Главному ключу CachyOS назначается доверие..."
  ["Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"]="Не удалось получить главный ключ CachyOS с keyserver.ubuntu.com.
  Без него подписанную связку ключей установить нельзя, поэтому CachyOS Proton
  пропущен. Добавьте его позже:  synpkg cachyos enable-repo"
  ["  Master key pinned as expected — trusting it..."]="  Главный ключ соответствует ожидаемому — назначается доверие..."
  ["[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."]="[cachyos] был добавлен, но не содержит пакетов — он убирается
  обратно, чтобы не мешать будущему обновлению."
  ["The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."]="Связка ключей CachyOS не содержит ожидаемого главного ключа.
  Доверие не назначено — репозиторий НЕ добавлен."
  ["  Installing proton-cachyos-slr (~340 MB download)..."]="  Устанавливается proton-cachyos-slr (~340 МБ загрузки)..."
  ["CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."]="CachyOS Proton установлен — выбирайте его для каждой игры в Steam:
  Свойства → Совместимость, в списке как 'proton-cachyos-… (steam linux runtime)'.
  Steam ищет его только при запуске, поэтому перезапустите Steam, если он уже открыт."
  ["proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"]="proton-cachyos-slr не установился. Steam и собственный Proton от Valve
  не затронуты. Установите его позже:
  sudo pacman -S proton-cachyos-slr"
  ["The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"]="Репозиторий [cachyos] не удалось включить, поэтому CachyOS Proton
  пропущен. Steam по-прежнему работает с Proton от Valve. Добавить позже:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
  ["Enabling BlackArch"]="Включается BlackArch"
  ["  Fetching the BlackArch bootstrap..."]="  Загружается сценарий начальной настройки BlackArch..."
  ["  Master key pinned as expected — running bootstrap..."]="  Главный ключ соответствует ожидаемому — сценарий запускается..."
  ["blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."]="blackarch-keyring не установился — смены ключей
  до этой машины не дойдут. Исправьте: 'sudo pacman -S blackarch-keyring'."
  ["The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."]="Загруженный strap.sh не закрепляет ожидаемый главный ключ BlackArch.
  Запуск отклонён — репозиторий НЕ добавлен."
  ["BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."]="BlackArch не был включён. В остальном система полная;
  добавьте его позже: 'sudo syn arsenal --enable-repo'."
  ["Installing software"]="Установка программ"
  ["That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."]="Эта операция не удалась — каждый пакет пробуется отдельно, чтобы
  исправные всё-таки встали, а неисправный был назван по имени."
  ["Software installed"]="Программы установлены"
  ["Installing Flatpak apps"]="Установка приложений Flatpak"
  ["flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."]="не удалось установить flatpak — приложения Flatpak пропущены.
  Больше это ни на что не влияет."
  ["Could not add the flathub remote"]="Не удалось добавить источник flathub"
  ["Flatpak apps installed"]="Приложения Flatpak установлены"
  ["Configuring System"]="Настройка системы"
  ["  fstab generated"]="  fstab создан"
  ["Swap recorded in fstab"]="Подкачка записана в fstab"
  ["zram configured (compressed swap, half of RAM up to 8 GiB)"]="zram настроен (сжатая подкачка, половина ОЗУ, но не больше 8 ГиБ)"
  ["zram-generator is not installed in the target — no compressed swap"]="zram-generator не установлен в целевой системе — сжатой подкачки не будет"
  ["  Hostname: synapse"]="  Имя машины: synapse"
  ["Step 7 — Language & Region"]="Шаг 7 — Язык и регион"
  ["   0) Other — enter a locale by hand"]="   0) Другое — ввести локаль вручную"
  ["Locale (e.g. sv_SE.UTF-8):"]="Локаль (например sv_SE.UTF-8):"
  ["Console keymap (e.g. sv-latin1):"]="Раскладка консоли (например sv-latin1):"
  ["Step 8 — Timezone"]="Шаг 8 — Часовой пояс"
  ["   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"]="   0) Другой — ввести любое имя из tzdata (например Europe/Lisbon)"
  ["tzdata name (Region/City):"]="Имя tzdata (Регион/Город):"
  ["  Did you mean:"]="  Возможно, имелось в виду:"
  ["  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"]="  Выберите номер из списка или посмотрите: ls /mnt/usr/share/zoneinfo"
  ["  os-release: copied from live system"]="  os-release: скопирован из live-системы"
  ["  issue: copied from live system"]="  issue: скопирован из live-системы"
  ["Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"]="Целевая файловая система больше не доступна для записи (ошибки диска? смотрите 'dmesg') — прерывание"
  ["sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"]="Набор правил sudoers недействителен после записи дополнений — система, которая не умеет sudo, не отдаётся"
  ["Could not relax pam_faillock in /etc/pam.d/system-auth (a tty-less sudo could still lock the account until reboot)."]="Не удалось смягчить pam_faillock в /etc/pam.d/system-auth (sudo без терминала всё ещё может заблокировать учётную запись до перезагрузки)."
  ["could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"]="не удалось заранее создать /var/lib/synapse-src — обновлятор спросит пароль при первом запуске"
  ["  Desktop: KDE Plasma (SDDM login screen)"]="  Рабочий стол: KDE Plasma (экран входа SDDM)"
  ["  GDM: SynapseOS logo on the login screen"]="  GDM: логотип SynapseOS на экране входа"
  ["  Desktop: GNOME (GDM login screen)"]="  Рабочий стол: GNOME (экран входа GDM)"
  ["  Desktop: TTY only"]="  Рабочий стол: только TTY"
  ["  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"]="  Рабочий стол: SynapseUI (приветствие synui — вход повторяет экран блокировки)"
  ["  motd: written for this installation"]="  motd: написан для этой установки"
  ["  note: syn-rgb.path is not installed; RGB lights stay off"]="  примечание: syn-rgb.path не установлен; подсветка RGB останется выключенной"
  ["AI model"]="Модель ИИ"
  ["  AI model skipped — install one later with: syn model download"]="  Модель ИИ пропущена — установите её позже: syn model download"
  ["AI model installed"]="Модель ИИ установлена"
  ["  the install, and everything else on the disk is already done."]="  установки, а всё остальное на диске уже сделано."
  ["syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."]="syn-model нет в целевой системе, поэтому модель не скачана.
  Он входит в основной набор; если его сняли, ИИ останется бездействующим."
  ["Configuring Nix"]="Настройка Nix"
  ["Nix configured — /etc/synapseos/nix"]="Nix настроен — /etc/synapseos/nix"
  ["  That is the download — a few hundred MB before any packages
  you add to home.nix. 'syn nix edit' opens it."]="  Вот это и есть загрузка — несколько сотен МБ ещё до любых пакетов,
  которые вы добавите в home.nix. 'syn nix edit' открывает файл."
  ["nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."]="nix установлен, но пакета 'syn' нет в целевой системе, поэтому
  настройщик не подготовлен. Сам Nix работает;
  слою /etc/synapseos/nix нужен 'syn'."
  ["nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."]="nix не установился — декларативного слоя нет.
  Установите его позже: 'sudo pacman -S nix && sudo syn nix init'."
  ["  Generating initramfs..."]="  Создаётся initramfs..."
  ["mkinitcpio failed — the installed system would not boot"]="mkinitcpio не сработал — установленная система не загрузилась бы"
  ["initramfs missing after mkinitcpio — the installed system would not boot"]="после mkinitcpio нет initramfs — установленная система не загрузилась бы"
  ["System configured"]="Система настроена"
  ["Installing Bootloader"]="Установка загрузчика"
  ["grub-install (UEFI) failed"]="grub-install (UEFI) не сработал"
  ["grub-install (BIOS) failed"]="grub-install (BIOS) не сработал"
  ["  Generating GRUB config..."]="  Создаётся конфигурация GRUB..."
  ["grub-mkconfig failed"]="grub-mkconfig не сработал"
  ["grub.cfg missing after install"]="после установки нет grub.cfg"
  ["grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"]="в grub.cfg записан пароль GRUB — файл остаётся доступным только root, поэтому приложение настроек не может рассказать о пунктах загрузки"
  ["  Installing systemd-boot..."]="  Устанавливается systemd-boot..."
  ["bootctl install failed"]="bootctl install не сработал"
  ["  Registering systemd-boot with the firmware..."]="  systemd-boot регистрируется в прошивке..."
  ["efibootmgr entry not created — the removable-media path still applies"]="запись efibootmgr не создана — по-прежнему действует путь для съёмного носителя"
  ["could not read the root filesystem UUID"]="не удалось прочитать UUID корневой файловой системы"
  ["vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"]="vmlinuz-linux нет на ESP — systemd-boot не нашёл бы, что загружать"
  ["initramfs is not on the ESP — systemd-boot would find nothing to boot"]="initramfs нет на ESP — systemd-boot не нашёл бы, что загружать"
  ["systemd-boot did not install its EFI binary"]="systemd-boot не установил свой EFI-файл"
  ["  Installing limine..."]="  Устанавливается limine..."
  ["could not copy limine's EFI binary to the ESP"]="не удалось скопировать EFI-файл limine на ESP"
  ["limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"]="limine-mkinitcpio-hook не установлен — у ядра, установленного позже, НЕ будет пункта загрузки"
  ["vmlinuz-linux is not on the ESP — limine would find nothing to boot"]="vmlinuz-linux нет на ESP — limine не нашёл бы, что загружать"
  ["limine's EFI binary is not on the ESP"]="EFI-файла limine нет на ESP"
  ["limine.conf has no kernel entry"]="в limine.conf нет ни одного пункта с ядром"
  ["  Verifying the encrypted boot path..."]="  Проверяется зашифрованный путь загрузки..."
  ["/boot is not a separate mount — an encrypted root needs a plain /boot"]="/boot не отдельная точка монтирования — зашифрованному корню нужен незашифрованный /boot"
  ["/boot is missing from fstab — it would not be mounted after boot"]="/boot нет в fstab — после загрузки он не был бы смонтирован"
  ["Encrypted boot path verified"]="Зашифрованный путь загрузки проверен"
  ["Configuring snapshots"]="Настройка снимков"
  ["snapper's config template is missing — snapshots cannot be configured"]="шаблон настроек snapper отсутствует — снимки настроить нельзя"
  ["could not write /etc/snapper/configs/root"]="не удалось записать /etc/snapper/configs/root"
  ["snapper does not see the 'root' config — snapshots would never be taken"]="snapper не видит настройку 'root' — снимки никогда бы не создавались"
  ["snapper's root config was not tuned — timeline snapshots would fill the disk"]="настройка root у snapper не подправлена — периодические снимки заполнили бы диск"
  ["could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"]="не удалось включить grub-btrfsd — снимки не будут появляться в меню загрузки сами"
  ["Snapshots enabled (snapper + snap-pac, bootable from GRUB)"]="Снимки включены (snapper + snap-pac, загружаются из GRUB)"
  ["could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"]="не удалось включить limine-snapper-sync — снимки не попадут в меню загрузки сами"
  ["could not take the post-install snapshot"]="не удалось сделать снимок после установки"
  ["could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"]="не удалось включить синхронизацию снимков при первой загрузке — меню заполнится после первого обновления"
  ["Snapshots enabled (snapper + snap-pac, bootable from limine)"]="Снимки включены (snapper + snap-pac, загружаются из limine)"
  ["Bootloader installed"]="Загрузчик установлен"
  ["  The root account is locked (no root login / su).
  Note: 3 wrong password attempts lock the account for 10 minutes."]="  Учётная запись root заблокирована (нет входа под root и su).
  Примечание: 3 неверных пароля блокируют учётную запись на 10 минут."
  ["You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."]="Пароль шифрования будет спрашиваться при каждой загрузке,
  ДО экрана входа. Восстановить его нельзя никак."
  ["    syn-crypt status                    is this disk encrypted, and how
    sudo syn-crypt change-key           replace the passphrase
    sudo syn-crypt add-key              add a second one
    sudo syn-crypt backup-header FILE   save the LUKS header"]="    syn-crypt status                    зашифрован ли этот диск и как
    sudo syn-crypt change-key           заменить пароль
    sudo syn-crypt add-key              добавить второй
    sudo syn-crypt backup-header ФАЙЛ   сохранить заголовок LUKS"
  ["  means the data is unrecoverable even with the right passphrase."]="  означает, что данные не восстановить даже с правильным паролем."
  ["Remove installation media and press ENTER to reboot..."]="Извлеките установочный носитель и нажмите ENTER для перезагрузки..."
  ["Install SynapseOS     — right here, in this terminal"]="Установить SynapseOS     — прямо здесь, в этом терминале"
  ["Install graphically   — starts the desktop first"]="Установить графически    — сначала запускает рабочий стол"
  ["Try the live desktop  — look around; install later"]="Попробовать live-систему — осмотреться; установить потом"
  ["Target:"]="Цель:"
  ["ALONGSIDE"]="РЯДОМ"
  ["ERASE"]="СТЕРЕТЬ"
  ["ADVANCED"]="ПОДРОБНО"
  ["Encrypt this installation?"]="Зашифровать эту установку?"
  ["There is no recovery."]="Восстановления не будет."
  ["Root filesystem"]="Корневая файловая система"
  ["ext4   — the default. Boring, proven, repairable by anything."]="ext4   — по умолчанию. Скучная, проверенная, чинится чем угодно."
  ["btrfs  — snapshots + zstd compression. Roll back a bad update
                    from the boot menu. Uses more RAM and more CPU."]="btrfs  — снимки + сжатие zstd. Откат неудачного обновления
                    прямо из меню загрузки. Больше ОЗУ и больше процессора."
  ["xfs    — fast on large files. No snapshots, and it cannot be
                    SHRUNK once created."]="xfs    — быстрая на больших файлах. Снимков нет, и её нельзя
                    УМЕНЬШИТЬ после создания."
  ["f2fs   — built for flash. Good on SD cards and cheap SSDs;
                    unusual enough that fewer rescue tools know it."]="f2fs   — сделана под флеш-память. Хороша на картах SD и дешёвых SSD;
                    достаточно редкая, чтобы её знали немногие спасательные средства."
  ["Bootloader"]="Загрузчик"
  ["GRUB          — the default. Detects other operating systems,
                          and the only one here that can boot a btrfs
                          snapshot."]="GRUB          — по умолчанию. Находит другие операционные системы
                          и единственный здесь умеет загружать снимок
                          btrfs."
  ["systemd-boot  — minimal. No OS detection, no snapshot menu."]="systemd-boot  — минимальный. Ни поиска систем, ни меню снимков."
  ["limine        — modern and fast, and it CAN boot snapshots."]="limine        — современный и быстрый, и он УМЕЕТ грузить снимки."
  ["Automatic snapshots?"]="Автоматические снимки?"
  ["Review the plan — nothing has been written yet:"]="Проверьте план — пока ничего не записано:"
  ["nothing else is touched"]="больше ничего не трогается"
  ["not"]="не будет"
  ["Partition"]="Разметьте"
  ["now."]="сейчас."
  ["Partitions now on"]="Разделы сейчас на"
  ["These partitions will be FORMATTED"]="Эти разделы будут ОТФОРМАТИРОВАНЫ"
  ["Full      — Standard + Steam + Nix + more software"]="Полная    — Стандарт + Steam + Nix + больше программ"
  ["Standard  — the SynapseOS suite, Firefox, AI model,"]="Стандарт  — набор SynapseOS, Firefox, модель ИИ,"
  ["Minimal   — core daemons only: none of the above"]="Минимум   — только основные службы: ничего из перечисленного"
  ["Custom    — tick every package yourself, ours and"]="Своя      — отметить каждый пакет самому, наши и"
  ["Which AI model should this machine run?"]="Какую модель ИИ должна запускать эта машина?"
  ["Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"]="Mistral 7B Instruct   ~4,1 ГБ   рекомендуется — под неё настроен SynapseOS"
  ["Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"]="Phi-3 Mini 4K         ~2,2 ГБ   вдвое меньше и заметно слабее"
  ["Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"]="Qwen2 0.5B            ~0,4 ГБ   влезет куда угодно и отвечает соответственно"
  ["None                            skip it — nothing else changes"]="Никакой                         пропустить — больше ничего не меняется"
  ["Installing:"]="Устанавливается:"
  ["SynapseUI  — AI-native Wayland compositor  (default)"]="SynapseUI  — композитор Wayland, созданный под ИИ  (по умолчанию)"
  ["SynapseUI  — NOT AVAILABLE: synui was not selected"]="SynapseUI  — НЕДОСТУПЕН: synui не был выбран"
  ["KDE Plasma — Full-featured Wayland desktop"]="KDE Plasma — полнофункциональный рабочий стол Wayland"
  ["GNOME      — Clean, modern Wayland desktop"]="GNOME      — чистый современный рабочий стол Wayland"
  ["TTY only   — No GUI (headless/server)"]="Только TTY — без графики (без экрана/сервер)"
  ["Disk:"]="Диск:"
  ["Boot:"]="Загрузка:"
  ["Encrypted:"]="Зашифрован:"
  ["Desktop:"]="Рабочий стол:"
  ["User:"]="Пользователь:"
  ["Hostname:"]="Имя машины:"
  ["Back up the header to another machine."]="Сохраните заголовок на другой машине."
  ["%s is mounted — unmount it first\\n"]="%s смонтирован — сначала отмонтируйте его
"
  ["%s is %s MiB — %s needs at least %s MiB\\n"]="%s имеет %s МиБ — для %s нужно не меньше %s МиБ
"
  ["  Generating %s (a few seconds)...\\n"]="  Создаётся %s (несколько секунд)...
"
  ["Language: %s  (%s, keyboard %s)\\n"]="Язык: %s  (%s, клавиатура %s)
"
  ["  This disk already holds %s partition(s), an EFI System\\n  Partition (%s), and %s GiB of free space.\\n"]="  На этом диске уже %s раздел(ов), системный раздел
  EFI (%s) и %s ГиБ свободного места.
"
  ["    1) Install %s — use the free space, keep everything else\\n"]="    1) Установить %s — занять свободное место, сохранить всё остальное
"
  ["    2) %s the whole disk — delete every partition and all data\\n"]="    2) %s весь диск — удалить каждый раздел и все данные
"
  ["    3) %s — partition this disk yourself, then pick the partitions\\n"]="    3) %s — разметить этот диск самостоятельно, затем выбрать разделы
"
  ["    1) %s the whole disk — delete every partition and all data  (default)\\n"]="    1) %s весь диск — удалить каждый раздел и все данные  (по умолчанию)
"
  ["    2) %s — partition this disk yourself, then pick the partitions\\n"]="    2) %s — разметить этот диск самостоятельно, затем выбрать разделы
"
  ["  %s If you forget the passphrase the data is\\n  gone — no password reset, no support call, nothing.\\n"]="  %s Если вы забудете пароль, данные пропали —
  без сброса, без звонка в поддержку, никак.
"
  ["  snapper takes a snapshot before and after every pacman\\n  transaction, and %s grows a menu to boot any of them. A\\n  bad upgrade becomes a reboot instead of a rescue USB.\\n"]="  snapper делает снимок до и после каждой операции pacman,
  а %s получает меню, из которого можно загрузить любой из них.
  Неудачное обновление становится перезагрузкой, а не спасательной флешкой.
"
  ["    Disk          : %s\\n"]="    Диск          : %s
"
  ["    Firmware      : %s\\n"]="    Прошивка      : %s
"
  ["    Filesystem    : %s\\n"]="    Файловая сист.: %s
"
  ["    Bootloader    : %s\\n"]="    Загрузчик     : %s
"
  ["    Separate /boot: %s\\n"]="    Отдельный /boot: %s
"
  ["    Encryption    : %s\\n"]="    Шифрование    : %s
"
  ["    Snapshots     : %s\\n"]="    Снимки        : %s
"
  ["  Encrypting %s (LUKS2)...\\n"]="  Шифруется %s (LUKS2)...
"
  ["  Formatting root partition (%s)...\\n"]="  Форматируется корневой раздел (%s)...
"
  ["    • KEEP   all %s existing partition(s), including Windows\\n"]="    • СОХРАНИТЬ все %s существующих раздела(ов), включая Windows
"
  ["    • REUSE  %s as the EFI partition (mounted, %s formatted)\\n"]="    • ИСПОЛЬЗОВАТЬ %s как раздел EFI (смонтирован, %s форматируется)
"
  ["    • CREATE a new ext4 root of ~%s GiB in the free space\\n"]="    • СОЗДАТЬ   новый корень ext4 примерно на %s ГиБ в свободном месте
"
  ["  Creating root partition in free space (%sMiB–%sMiB)...\\n"]="  Корневой раздел создаётся в свободном месте (%s МиБ–%s МиБ)...
"
  ["  Formatting new root (%s, ext4)...\\n"]="  Форматируется новый корень (%s, ext4)...
"
  ["  %s %s %s The installer will re-read the table when you exit.\\n"]="  %s %s %s Установщик перечитает таблицу, когда вы выйдете.
"
  ["    • a root partition, at least %s GiB\\n"]="    • корневой раздел, не меньше %s ГиБ
"
  ["    • a separate /boot of ~1 GiB — %s with this layout cannot read the root\\n"]="    • отдельный /boot примерно на 1 ГиБ — %s при такой схеме не прочитает корень
"
  ["  Starting %s on %s — write your changes before quitting.\\n"]="  Запускается %s на %s — сохраните изменения перед выходом.
"
  ["    %s is already swap — another system may resume from it.\\n"]="    %s уже раздел подкачки — из него может просыпаться другая система.
"
  ["  Everything else on %s is left untouched.\\n"]="  Всё остальное на %s остаётся нетронутым.
"
  ["  Making swap on %s...\\n"]="  Создаётся подкачка на %s...
"
  ["  Formatting EFI partition (%s MiB)...\\n"]="  Форматируется раздел EFI (%s МиБ)...
"
  ["  NVIDIA GPU detected — installing %s (builds the module, takes a while)...\\n"]="  Обнаружен GPU NVIDIA — устанавливается %s (собирает модуль, это займёт время)...
"
  ["  Installing video stack: %s %s...\\n"]="  Устанавливается графический набор: %s %s...
"
  ["  [cachyos] enabled (%s packages available)\\n"]="  [cachyos] включён (доступно пакетов: %s)
"
  ["  Language: %s  (chosen at boot)\\n"]="  Язык: %s  (выбран при загрузке)
"
  ["  Locale:   %s   Keyboard: %s (console) / %s (desktop)\\n"]="  Локаль:   %s   Клавиатура: %s (консоль) / %s (рабочий стол)
"
  ["  Installing fonts (%s)...\\n"]="  Устанавливаются шрифты (%s)...
"
  ["  Downloading the AI model (%s) — this is the long part of\\n"]="  Скачивается модель ИИ (%s) — это самая долгая часть
"
  ["  Nothing is built yet. As %s, after the first boot:\\n"]="  Пока ничего не собрано. От имени %s, после первой загрузки:
"
  ["  Adding the %s hook to mkinitcpio...\\n"]="  В mkinitcpio добавляется хук %s...
"
  ["  Installing GRUB (%s)...\\n"]="  Устанавливается GRUB (%s)...
"
  ["yes — LUKS2 on %s"]="да — LUKS2 на %s"
  ["  Admin: use %s with your user password.\\n"]="  Администрирование: используйте %s со своим пользовательским паролем.
"
  ["  Manage it later with %s:\\n"]="  Управлять этим позже через %s:
"
  ["  %s A damaged LUKS header\\n"]="  %s Повреждённый заголовок LUKS
"
  ["%s is on the live/boot device — that is the installer's own media\\n"]="%s находится на live-/загрузочном устройстве — это собственный носитель установщика
"
  ["    %s is already FAT — it may hold another OS's bootloader.\\n"]="    %s уже в FAT — на нём может лежать загрузчик другой системы.
"
  ["  Creating user '%s'...\\n"]="  Создаётся пользователь '%s'...
"
  ["  User '%s' created (uid=%s)\\n"]="  Пользователь '%s' создан (uid=%s)
"
  ["  Log in as '%s' after reboot.\\n"]="  После перезагрузки войдите как '%s'.
"
  ["  Type '%s' to get started.\\n"]="  Наберите '%s', чтобы начать.
"
)
