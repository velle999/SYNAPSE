# Polski (pl) — syn-install's own words.
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
  ["render.nix is missing — the 'syn' package is not installed here."]="brakuje render.nix — pakiet 'syn' nie jest tu zainstalowany."
  ["locale-gen failed. The live session stays in English; the install is
  unaffected, because it generates the locale inside the target."]="locale-gen nie powiódł się. Sesja live zostaje po angielsku; instalacja
  nie jest tym dotknięta, bo generuje lokalizację w systemie docelowym."
  ["  The keyboard, the clock, the fonts and the shell all follow this.
  You can change any of it later."]="  Klawiatura, zegar, czcionki i powłoka wynikają z tego wyboru.
  Wszystko to można później zmienić."
  ["Toggle [numbers, 'all', 'none', Enter = accept]:"]="Przełącz [numery, 'all', 'none', Enter = zatwierdź]:"
  ["--config needs a file"]="--config wymaga pliku"
  ["syn-install must be run as root"]="syn-install musi być uruchomiony jako root"
  ["  SynapseOS is running from the live image."]="  SynapseOS działa z obrazu live."
  ["Starting the desktop — the installer opens with it."]="Uruchamianie pulpitu — instalator otworzy się razem z nim."
  ["  This installer will:
    1. Partition a disk
    2. Install SynapseOS base system
    3. Install SynapseOS packages
    4. Create user account
    5. Choose desktop environment
    6. Configure system & bootloader"]="  Ten instalator:
    1. Podzieli dysk na partycje
    2. Zainstaluje system podstawowy SynapseOS
    3. Zainstaluje pakiety SynapseOS
    4. Utworzy konto użytkownika
    5. Pozwoli wybrać środowisko graficzne
    6. Skonfiguruje system i program rozruchowy"
  ["ALL DATA ON THE TARGET DISK WILL BE ERASED"]="WSZYSTKIE DANE NA DYSKU DOCELOWYM ZOSTANĄ USUNIĘTE"
  ["Press ENTER to continue or Ctrl+C to abort..."]="Naciśnij ENTER, aby kontynuować, albo Ctrl+C, aby przerwać..."
  ["Checking network"]="Sprawdzanie sieci"
  ["Network is up"]="Sieć działa"
  ["  No network detected. Starting NetworkManager..."]="  Nie wykryto sieci. Uruchamianie NetworkManagera..."
  ["  No connection — but this machine has Wi-Fi."]="  Brak połączenia — ale ta maszyna ma Wi-Fi."
  ["Open the Wi-Fi picker (nmtui)? [Y/n]:"]="Otworzyć wybór sieci Wi-Fi (nmtui)? [Y/n]:"
  ["No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."]="Brak połączenia sieciowego i brak urządzenia Wi-Fi do skonfigurowania.
  SynapseOS pobiera system podstawowy w trakcie instalacji, więc podłącz
  kabel i uruchom ponownie."
  ["Network connected"]="Sieć połączona"
  ["Step 1 — Select Target Disk"]="Krok 1 — Wybierz dysk docelowy"
  ["  Available disks:"]="  Dostępne dyski:"
  ["Target disk (e.g. sda, vda, nvme0n1):"]="Dysk docelowy (np. sda, vda, nvme0n1):"
  ["Target disk is in use. Unmount its partitions and re-run."]="Dysk docelowy jest w użyciu. Odmontuj jego partycje i uruchom ponownie."
  ["Boot mode: UEFI"]="Tryb rozruchu: UEFI"
  ["Boot mode: BIOS/Legacy"]="Tryb rozruchu: BIOS/Legacy"
  ["  Encrypts the root filesystem with LUKS2. You will be asked for the
  passphrase at every boot, before the system starts."]="  Szyfruje główny system plików za pomocą LUKS2. Hasło będzie pytane przy
  każdym uruchomieniu, zanim system wystartuje."
  ["Encrypt the disk? [y/N]:"]="Zaszyfrować dysk? [y/N]:"
  ["                          With encryption it is the BETTER choice: the
                          kernel lives on the EFI partition and only the
                          initramfs unlocks, so /boot needs no separate
                          unencrypted partition."]="                          Przy szyfrowaniu to LEPSZY wybór: jądro leży
                          na partycji EFI i tylko initramfs odblokowuje,
                          więc /boot nie potrzebuje osobnej
                          niezaszyfrowanej partycji."
  ["                          It copies each snapshot's kernel onto the EFI
                          partition, so that partition is made much
                          larger when snapshots are enabled."]="                          Kopiuje jądro każdej migawki na partycję EFI,
                          więc ta partycja jest tworzona znacznie
                          większa, gdy migawki są włączone."
  ["  Snapshots are cheap but not free: they hold the old copy of
  anything that changes, so a disk near full stays near full."]="  Migawki są tanie, ale nie darmowe: przechowują starą kopię wszystkiego,
  co się zmienia, więc prawie pełny dysk pozostaje prawie pełny."
  ["Enable snapshots? [Y/n]:"]="Włączyć migawki? [Y/n]:"
  ["mkfs.ext4 is missing from this installer image — /boot cannot be created"]="w tym obrazie instalatora brakuje mkfs.ext4 — nie można utworzyć /boot"
  ["btrfs is missing from this installer image — subvolumes cannot be created"]="w tym obrazie instalatora brakuje btrfs — nie można utworzyć podwoluminów"
  ["Are these correct? [Y/n]:"]="Czy to się zgadza? [Y/n]:"
  ["Starting the questions over — the disk has not been touched."]="Zaczynamy pytania od nowa — dysk nie został tknięty."
  ["cryptsetup is not available on this installer image"]="cryptsetup nie jest dostępny w tym obrazie instalatora"
  ["Encryption passphrase:"]="Hasło szyfrowania:"
  ["Repeat passphrase:"]="Powtórz hasło:"
  ["Empty passphrase — that would leave the disk unprotected."]="Puste hasło — dysk zostałby bez ochrony."
  ["Passphrases did not match — try again."]="Hasła nie są zgodne — spróbuj ponownie."
  ["Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."]="Hasło ma mniej niż 8 znaków. Krótkie niewiele daje wobec kogoś,
  kto ma dysk w ręku."
  ["Use it anyway? [y/N]:"]="Użyć mimo to? [y/N]:"
  ["Encryption enabled — root will be LUKS2"]="Szyfrowanie włączone — katalog główny będzie na LUKS2"
  ["cryptsetup open failed — the passphrase did not take"]="cryptsetup open nie powiódł się — hasło nie zostało przyjęte"
  ["Failed to mount root"]="Nie udało się zamontować katalogu głównego"
  ["  Creating btrfs subvolumes..."]="  Tworzenie podwoluminów btrfs..."
  ["btrfs: could not create @"]="btrfs: nie udało się utworzyć @"
  ["btrfs: could not create @home"]="btrfs: nie udało się utworzyć @home"
  ["btrfs: could not create @snapshots"]="btrfs: nie udało się utworzyć @snapshots"
  ["btrfs: could not create @var_log"]="btrfs: nie udało się utworzyć @var_log"
  ["btrfs: could not create @pkg"]="btrfs: nie udało się utworzyć @pkg"
  ["could not remount the btrfs root onto @"]="nie udało się ponownie zamontować katalogu głównego btrfs na @"
  ["Failed to mount @"]="Nie udało się zamontować @"
  ["Failed to mount @home"]="Nie udało się zamontować @home"
  ["Failed to mount @snapshots"]="Nie udało się zamontować @snapshots"
  ["Failed to mount @var_log"]="Nie udało się zamontować @var_log"
  ["Failed to mount @pkg"]="Nie udało się zamontować @pkg"
  ["This adds one partition in the free space. Back up anything irreplaceable first."]="To doda jedną partycję w wolnym miejscu. Najpierw zrób kopię wszystkiego, czego nie da się odtworzyć."
  ["Type 'yes' to install alongside:"]="Wpisz 'yes', aby zainstalować obok:"
  ["Aborted"]="Przerwano"
  ["Failed to create the root partition"]="Nie udało się utworzyć partycji głównej"
  ["Could not identify the new partition after creating it"]="Nie udało się rozpoznać nowej partycji po jej utworzeniu"
  ["Failed to format root partition"]="Nie udało się sformatować partycji głównej"
  ["Failed to mount the existing ESP"]="Nie udało się zamontować istniejącej partycji ESP"
  ["no partition editor on this image (cfdisk, fdisk and parted are all missing)"]="brak edytora partycji w tym obrazie (nie ma ani cfdisk, ani fdisk, ani parted)"
  ["  What this install needs:"]="  Czego wymaga ta instalacja:"
  ["    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"]="    • partycji systemowej EFI (typ EF00 / flaga 'esp') — można użyć istniejącej"
  ["  Skipping the partition editor (--config)."]="  Pomijanie edytora partycji (--config)."
  ["Format it? Everything on it is lost [y/N]:"]="Sformatować? Wszystko, co na niej jest, przepadnie [y/N]:"
  ["Separate /boot partition:"]="Osobna partycja /boot:"
  ["Swap partition (blank for none):"]="Partycja wymiany (puste = żadna):"
  ["Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"]="Utworzyć od nowa? Jej UUID się zmieni i zepsuje fstab tamtego systemu [y/N]:"
  ["Type 'yes' to format these:"]="Wpisz 'yes', aby je sformatować:"
  ["  Formatting EFI partition..."]="  Formatowanie partycji EFI..."
  ["  Formatting /boot partition..."]="  Formatowanie partycji /boot..."
  ["Failed to mount /boot"]="Nie udało się zamontować /boot"
  ["Type 'yes' to confirm:"]="Wpisz 'yes', aby potwierdzić:"
  ["  Creating GPT partition table..."]="  Tworzenie tablicy partycji GPT..."
  ["Failed to format EFI partition"]="Nie udało się sformatować partycji EFI"
  ["Failed to format boot partition"]="Nie udało się sformatować partycji rozruchowej"
  ["  Creating MBR partition table..."]="  Tworzenie tablicy partycji MBR..."
  ["Disk partitioned and mounted at /mnt"]="Dysk podzielony na partycje i zamontowany w /mnt"
  ["Step 3 — Installing Base System"]="Krok 3 — Instalacja systemu podstawowego"
  ["  Initializing pacman keyring..."]="  Przygotowywanie zbioru kluczy pacmana..."
  ["  Running pacstrap (this may take several minutes)..."]="  Trwa pacstrap (to może potrwać kilka minut)..."
  ["pacstrap failed — check network connection"]="pacstrap nie powiódł się — sprawdź połączenie sieciowe"
  ["grub-install not found in chroot — attempting recovery..."]="nie znaleziono grub-install w chroocie — próba naprawy..."
  ["Could not install grub into target — check network"]="Nie udało się zainstalować gruba w systemie docelowym — sprawdź sieć"
  ["Base system installed"]="System podstawowy zainstalowany"
  ["Step 4 — Choose What to Install"]="Krok 4 — Wybierz, co zainstalować"
  ["  What should be installed alongside the SynapseOS core?"]="  Co ma zostać zainstalowane obok rdzenia SynapseOS?"
  ["                   Bluetooth, printing, Wine, phone   (default)"]="                   Bluetooth, drukowanie, Wine, telefon   (domyślnie)"
  ["                   the ordinary software people install anyway"]="                   zwykłe oprogramowanie, które i tak się instaluje"
  ["  Every preset except Minimal then asks WHICH AI model to download,
  and skipping it is one of the answers."]="  Każdy zestaw poza Minimalnym pyta potem, KTÓRY model SI pobrać,
  a pominięcie go jest jedną z odpowiedzi."
  ["Full install selected"]="Wybrano instalację pełną"
  ["Minimal install selected"]="Wybrano instalację minimalną"
  ["  Two kinds of question. First the packages, as pages of
  checkboxes; then the handful of options that are a whole
  subsystem rather than a package."]="  Dwa rodzaje pytań. Najpierw pakiety, jako strony z polami
  wyboru; potem garść opcji, które są całym podsystemem,
  a nie pakietem."
  ["  And the software people install on the first evening anyway.
  All of it is in the Arch repositories; none of it is ours."]="  I oprogramowanie, które ludzie i tak instalują pierwszego wieczoru.
  Wszystko jest w repozytoriach Archa; nic z tego nie jest nasze."
  ["  The rest is y/n. The default (shown in caps) is Standard."]="  Reszta to t/n. Wartość domyślna (wielkimi literami) to Standard."
  ["syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."]="syn-update odznaczony: ta maszyna nie będzie miała jak otrzymać
  kolejnego pakietu SynapseOS. Naprawa tego później oznacza instalację ręczną
  z ISO albo ponowną instalację."
  ["Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."]="Nie zachowano ani pulpitu, ani demona SI. To system Arch
  z kilkoma narzędziami SynapseOS, co jest dopuszczalną odpowiedzią —
  ale nic w dokumentacji nie opisze maszyny, którą wtedy dostajesz."
  ["Custom install configured"]="Instalacja własna skonfigurowana"
  ["Standard install selected"]="Wybrano instalację standardową"
  ["  synapd loads one model and everything AI in SynapseOS talks to it:
  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,
  over this connection, onto the disk you are installing to."]="  synapd ładuje jeden model i wszystko, co w SynapseOS dotyczy SI, z nim rozmawia:
  synsh, panel SI na pulpicie, Chibi, Vibe. Jest pobierany teraz,
  przez to połączenie, na dysk, na który instalujesz."
  ["  A smaller model is not just faster and lighter: it follows
  instructions worse. synsh mistakes what you asked for, Vibe's code
  needs more fixing, Chibi loses the thread. Take the default unless
  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."]="  Mniejszy model jest nie tylko szybszy i lżejszy: gorzej trzyma się
  poleceń. synsh myli to, o co prosiłeś, kod Vibe wymaga więcej
  poprawek, Chibi gubi wątek. Weź domyślny, chyba że dysk
  albo pamięć mówią inaczej — 7B chce ~6 GB wolnego RAM-u."
  ["  Whatever you pick, it can be changed later: 'syn model download',
  or Super+C ▸ System ▸ AI model on the desktop."]="  Cokolwiek wybierzesz, można to później zmienić: 'syn model download',
  albo Super+C ▸ System ▸ Model SI na pulpicie."
  ["Install this selection? [Y/n]:"]="Zainstalować ten wybór? [Y/n]:"
  ["Choosing again — nothing has been installed yet."]="Wybieramy jeszcze raz — nic jeszcze nie zostało zainstalowane."
  ["Step 4b — Installing SynapseOS"]="Krok 4b — Instalacja SynapseOS"
  ["Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."]="Nie udało się włączyć ILoveCandy w /etc/pacman.conf (tylko wygląd)."
  ["  Enabling [multilib] (32-bit repo, needed by Steam)..."]="  Włączanie [multilib] (repozytorium 32-bitowe, potrzebne Steamowi)..."
  ["Could not sync the multilib database — Steam may fail to install"]="Nie udało się zsynchronizować bazy multilib — Steam może się nie zainstalować"
  ["Could not enable [multilib]; Steam will be skipped."]="Nie udało się włączyć [multilib]; Steam zostanie pominięty."
  ["Some SynapseOS packages failed to install — verifying below"]="Niektóre pakiety SynapseOS nie zainstalowały się — sprawdzenie poniżej"
  ["No SynapseOS packages were selected. This will be an Arch system."]="Nie wybrano żadnego pakietu SynapseOS. To będzie system Arch."
  ["SynapseOS packages installed"]="Pakiety SynapseOS zainstalowane"
  ["Component selection recorded in /etc/synapseos/components.conf"]="Wybór komponentów zapisany w /etc/synapseos/components.conf"
  ["Step 5 — Create User Account"]="Krok 5 — Utwórz konto użytkownika"
  ["  Create a user account for the installed system."]="  Utwórz konto użytkownika dla instalowanego systemu."
  ["Username [default: syn]:"]="Nazwa użytkownika [domyślnie: syn]:"
  ["Full name (optional):"]="Imię i nazwisko (opcjonalnie):"
  ["Password:"]="Hasło:"
  ["Confirm password:"]="Potwierdź hasło:"
  ["Passwords do not match or are empty — try again"]="Hasła nie są zgodne albo są puste — spróbuj ponownie"
  ["Step 6 — Desktop Environment"]="Krok 6 — Środowisko graficzne"
  ["  Choose a desktop environment:"]="  Wybierz środowisko graficzne:"
  ["  Installing KDE Plasma..."]="  Instalowanie KDE Plasma..."
  ["Some KDE packages failed to install"]="Niektóre pakiety KDE nie zainstalowały się"
  ["KDE Plasma installed"]="KDE Plasma zainstalowane"
  ["  Installing GNOME..."]="  Instalowanie GNOME..."
  ["Some GNOME packages failed to install"]="Niektóre pakiety GNOME nie zainstalowały się"
  ["GNOME installed (session only — SynapseOS apps, not GNOME's)"]="GNOME zainstalowane (tylko sesja — programy SynapseOS, nie te z GNOME)"
  ["  Installing greetd (login screen) + desktop extras..."]="  Instalowanie greetd (ekran logowania) + dodatków pulpitu..."
  ["greetd failed to install — boot falls back to getty login"]="greetd się nie zainstalował — rozruch wraca do logowania przez getty"
  ["SynapseUI selected (included)"]="Wybrano SynapseUI (w zestawie)"
  ["Installing Wine"]="Instalowanie Wine"
  ["Wine installed"]="Wine zainstalowane"
  ["wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."]="wine się nie zainstalował — pliki .exe/.msi z Windowsa nie będą działać.
  Zainstaluj go później: 'sudo pacman -S wine wine-mono'."
  ["Configuring Video Driver"]="Konfigurowanie sterownika grafiki"
  ["  Virtual machine — installing mesa (synui uses pixman here)..."]="  Maszyna wirtualna — instalowanie mesa (synui używa tu pixmana)..."
  ["mesa failed to install"]="mesa się nie zainstalowała"
  ["NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"]="Instalacja sterownika NVIDIA nie powiodła się — system uruchomiłby się
  na nouveau, a renderer synui nigdy by nie wystartował"
  ["NVIDIA sleep services enabled (VRAM save/restore)"]="Usługi uśpienia NVIDIA włączone (zapis/odtworzenie VRAM-u)"
  ["Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"]="Nie udało się włączyć nvidia-{suspend,resume,hibernate} — uśpienie
  może dać czarny ekran, jeśli później włączy się NVreg_PreserveVideoMemoryAllocations"
  ["  synapd can run inference on this GPU instead of the CPU.
  This downloads the CUDA runtime (~4.7 GiB installed)."]="  synapd może liczyć na tym GPU zamiast na procesorze.
  To pobiera środowisko CUDA (~4,7 GiB po instalacji)."
  ["Enable GPU inference? [Y/n]:"]="Włączyć obliczenia na GPU? [Y/n]:"
  ["Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda"]="Zostajemy przy obliczeniach na procesorze. Zmiana później:
  sudo pacman -S synapse-llama-cuda"
  ["  Installing synapse-llama-cuda (this takes a while)..."]="  Instalowanie synapse-llama-cuda (to chwilę potrwa)..."
  ["This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"]="Ten obraz ISO nie zawiera wersji llama dla GPU, więc synapd będzie liczył na
  procesorze mimo karty NVIDIA. (ISO trzeba zbudować na maszynie z zestawem
  CUDA, żeby synapse-llama-cuda w ogóle istniał.)"
  ["Video driver install failed — synui may fall back to software rendering"]="Instalacja sterownika grafiki nie powiodła się — synui może przejść na rysowanie programowe"
  ["Video drivers installed"]="Sterowniki grafiki zainstalowane"
  ["  Enabling GPU inference (synapse-llama-vulkan)..."]="  Włączanie obliczeń na GPU (synapse-llama-vulkan)..."
  ["This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"]="Ten obraz ISO nie zawiera wersji llama dla Vulkana, więc synapd będzie liczył na
  procesorze mimo GPU AMD/Intel. (Zbuduj ISO na maszynie z 'shaderc' +
  vulkan-headers, żeby synapse-llama-vulkan istniał.)"
  ["Installing Steam and the game stack"]="Instalowanie Steama i warstwy do gier"
  ["  Installing steam and the 32-bit runtime libraries..."]="  Instalowanie steama i 32-bitowych bibliotek uruchomieniowych..."
  ["Steam installed (native multilib package)"]="Steam zainstalowany (natywny pakiet multilib)"
  ["steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."]="steam się nie zainstalował. Poza tym system jest kompletny —
  zainstaluj go później: 'sudo pacman -S steam' ([multilib] jest już
  włączone w /etc/pacman.conf)."
  ["  Installing the game stack (overlay, governor, micro-compositor)..."]="  Instalowanie warstwy do gier (nakładka, regulator, mikrokompozytor)..."
  ["Game stack installed (mangohud, gamemode, gamescope)"]="Warstwa do gier zainstalowana (mangohud, gamemode, gamescope)"
  ["The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"]="Warstwa do gier się nie zainstalowała. Steam nadal działa; nakładka FPS,
  regulator CPU/GPU i 'synui-game-run --gamescope' nie.
  Zainstaluj je później:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
  ["Installing CachyOS Proton"]="Instalowanie CachyOS Proton"
  ["  Fetching the CachyOS keyring and mirrorlist..."]="  Pobieranie zbioru kluczy i listy serwerów CachyOS..."
  ["  Trusting the CachyOS master key..."]="  Nadawanie zaufania kluczowi głównemu CachyOS..."
  ["Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"]="Nie udało się pobrać klucza głównego CachyOS z keyserver.ubuntu.com.
  Bez niego nie da się zainstalować podpisanego zbioru kluczy, więc CachyOS
  Proton pominięto. Dodaj go później:  synpkg cachyos enable-repo"
  ["  Master key pinned as expected — trusting it..."]="  Klucz główny zgodny z oczekiwaniem — nadawanie zaufania..."
  ["[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."]="[cachyos] zostało dodane, ale nie zawiera żadnych pakietów — jest
  usuwane z powrotem, żeby nie blokowało późniejszej aktualizacji."
  ["The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."]="Zbiór kluczy CachyOS nie zawiera oczekiwanego klucza głównego.
  Odmowa zaufania — repozytorium NIE zostało dodane."
  ["  Installing proton-cachyos-slr (~340 MB download)..."]="  Instalowanie proton-cachyos-slr (~340 MB do pobrania)..."
  ["CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."]="CachyOS Proton zainstalowany — wybierz go dla danej gry w Steamie w
  Właściwości → Zgodność, na liście jako 'proton-cachyos-… (steam linux runtime)'.
  Steam szuka go tylko przy starcie, więc uruchom go ponownie, jeśli już działa."
  ["proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"]="proton-cachyos-slr się nie zainstalował. Steam i Proton od Valve
  są nietknięte. Zainstaluj go później:
  sudo pacman -S proton-cachyos-slr"
  ["The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"]="Nie udało się włączyć repozytorium [cachyos], więc CachyOS Proton
  pominięto. Steam nadal działa z Protonem od Valve. Aby dodać później:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
  ["Enabling BlackArch"]="Włączanie BlackArch"
  ["  Fetching the BlackArch bootstrap..."]="  Pobieranie skryptu startowego BlackArch..."
  ["  Master key pinned as expected — running bootstrap..."]="  Klucz główny zgodny z oczekiwaniem — uruchamianie skryptu..."
  ["blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."]="blackarch-keyring się nie zainstalował — wymiany kluczy
  nie dotrą do tej maszyny. Napraw: 'sudo pacman -S blackarch-keyring'."
  ["The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."]="Pobrany strap.sh nie ustala oczekiwanego klucza głównego BlackArch.
  Odmowa uruchomienia — repozytorium NIE zostało dodane."
  ["BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."]="BlackArch nie został włączony. Poza tym system jest kompletny;
  dodaj go później: 'sudo syn arsenal --enable-repo'."
  ["Installing software"]="Instalowanie oprogramowania"
  ["That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."]="Ta transakcja nie powiodła się — każdy pakiet jest próbowany osobno, żeby
  te sprawne i tak trafiły na miejsce, a ten wadliwy został nazwany."
  ["Software installed"]="Oprogramowanie zainstalowane"
  ["Installing Flatpak apps"]="Instalowanie programów Flatpak"
  ["flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."]="nie udało się zainstalować flatpaka — programy Flatpak zostają pominięte.
  Nic innego to nie dotyka."
  ["Could not add the flathub remote"]="Nie udało się dodać zdalnego repozytorium flathub"
  ["Flatpak apps installed"]="Programy Flatpak zainstalowane"
  ["Configuring System"]="Konfigurowanie systemu"
  ["  fstab generated"]="  fstab wygenerowana"
  ["Swap recorded in fstab"]="Wymiana zapisana w fstab"
  ["zram configured (compressed swap, half of RAM up to 8 GiB)"]="zram skonfigurowany (skompresowana wymiana, połowa RAM-u do 8 GiB)"
  ["zram-generator is not installed in the target — no compressed swap"]="zram-generator nie jest zainstalowany w systemie docelowym — brak skompresowanej wymiany"
  ["  Hostname: synapse"]="  Nazwa komputera: synapse"
  ["Step 7 — Language & Region"]="Krok 7 — Język i region"
  ["   0) Other — enter a locale by hand"]="   0) Inne — wpisać lokalizację ręcznie"
  ["Locale (e.g. sv_SE.UTF-8):"]="Lokalizacja (np. sv_SE.UTF-8):"
  ["Console keymap (e.g. sv-latin1):"]="Układ klawiatury konsoli (np. sv-latin1):"
  ["Step 8 — Timezone"]="Krok 8 — Strefa czasowa"
  ["   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"]="   0) Inna — wpisać dowolną nazwę tzdata (np. Europe/Lisbon)"
  ["tzdata name (Region/City):"]="Nazwa tzdata (Region/Miasto):"
  ["  Did you mean:"]="  Czy chodziło o:"
  ["  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"]="  Wybierz numer z listy albo zobacz: ls /mnt/usr/share/zoneinfo"
  ["  os-release: copied from live system"]="  os-release: skopiowany z systemu live"
  ["  issue: copied from live system"]="  issue: skopiowany z systemu live"
  ["Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"]="Docelowy system plików nie jest już zapisywalny (błędy dysku? sprawdź 'dmesg') — przerwanie"
  ["sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"]="Zestaw reguł sudoers jest nieprawidłowy po zapisaniu plików uzupełniających — nie wypuszczamy systemu, który nie potrafi sudo"
  ["Could not relax pam_faillock in /etc/pam.d/system-auth (a tty-less sudo could still lock the account until reboot)."]="Nie udało się rozluźnić pam_faillock w /etc/pam.d/system-auth (sudo bez terminala nadal mogłoby zablokować konto do ponownego uruchomienia)."
  ["could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"]="nie udało się wcześniej utworzyć /var/lib/synapse-src — narzędzie aktualizacji poprosi o hasło przy pierwszym uruchomieniu"
  ["  Desktop: KDE Plasma (SDDM login screen)"]="  Pulpit: KDE Plasma (ekran logowania SDDM)"
  ["  GDM: SynapseOS logo on the login screen"]="  GDM: logo SynapseOS na ekranie logowania"
  ["  Desktop: GNOME (GDM login screen)"]="  Pulpit: GNOME (ekran logowania GDM)"
  ["  Desktop: TTY only"]="  Pulpit: tylko TTY"
  ["  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"]="  Pulpit: SynapseUI (powitanie synui — logowanie odbija ekran blokady)"
  ["  motd: written for this installation"]="  motd: napisany dla tej instalacji"
  ["  note: syn-rgb.path is not installed; RGB lights stay off"]="  uwaga: syn-rgb.path nie jest zainstalowany; podświetlenie RGB pozostaje wyłączone"
  ["AI model"]="Model SI"
  ["  AI model skipped — install one later with: syn model download"]="  Model SI pominięty — zainstaluj go później: syn model download"
  ["AI model installed"]="Model SI zainstalowany"
  ["  the install, and everything else on the disk is already done."]="  instalacji, a wszystko inne na dysku jest już zrobione."
  ["syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."]="syn-model nie ma w systemie docelowym, więc nie pobrano żadnego modelu.
  Należy do zestawu podstawowego; jeśli został odznaczony, SI pozostaje bezczynna."
  ["Configuring Nix"]="Konfigurowanie Nix"
  ["Nix configured — /etc/synapseos/nix"]="Nix skonfigurowany — /etc/synapseos/nix"
  ["  That is the download — a few hundred MB before any packages
  you add to home.nix. 'syn nix edit' opens it."]="  To właśnie jest pobieranie — kilkaset MB, zanim dojdą jakiekolwiek pakiety,
  które dodasz do home.nix. 'syn nix edit' otwiera ten plik."
  ["nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."]="nix jest zainstalowany, ale pakietu 'syn' nie ma w systemie docelowym, więc
  konfigurator nie został przygotowany. Sam Nix działa;
  warstwa /etc/synapseos/nix potrzebuje 'syn'."
  ["nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."]="nix się nie zainstalował — warstwa deklaratywna jest niedostępna.
  Zainstaluj ją później: 'sudo pacman -S nix && sudo syn nix init'."
  ["  Generating initramfs..."]="  Generowanie initramfs..."
  ["mkinitcpio failed — the installed system would not boot"]="mkinitcpio nie powiódł się — zainstalowany system by się nie uruchomił"
  ["initramfs missing after mkinitcpio — the installed system would not boot"]="brak initramfs po mkinitcpio — zainstalowany system by się nie uruchomił"
  ["System configured"]="System skonfigurowany"
  ["Installing Bootloader"]="Instalowanie programu rozruchowego"
  ["grub-install (UEFI) failed"]="grub-install (UEFI) nie powiódł się"
  ["grub-install (BIOS) failed"]="grub-install (BIOS) nie powiódł się"
  ["  Generating GRUB config..."]="  Generowanie konfiguracji GRUB-a..."
  ["grub-mkconfig failed"]="grub-mkconfig nie powiódł się"
  ["grub.cfg missing after install"]="brak grub.cfg po instalacji"
  ["grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"]="grub.cfg zawiera hasło GRUB-a — pozostaje czytelny tylko dla roota, więc aplikacja ustawień nie może nic powiedzieć o wpisach rozruchowych"
  ["  Installing systemd-boot..."]="  Instalowanie systemd-boot..."
  ["bootctl install failed"]="bootctl install nie powiódł się"
  ["  Registering systemd-boot with the firmware..."]="  Rejestrowanie systemd-boot w oprogramowaniu układowym..."
  ["efibootmgr entry not created — the removable-media path still applies"]="nie utworzono wpisu efibootmgr — nadal obowiązuje ścieżka nośnika wymiennego"
  ["could not read the root filesystem UUID"]="nie udało się odczytać UUID głównego systemu plików"
  ["vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"]="vmlinuz-linux nie ma na partycji ESP — systemd-boot nie znalazłby czego uruchomić"
  ["initramfs is not on the ESP — systemd-boot would find nothing to boot"]="initramfs nie ma na partycji ESP — systemd-boot nie znalazłby czego uruchomić"
  ["systemd-boot did not install its EFI binary"]="systemd-boot nie zainstalował swojego pliku EFI"
  ["  Installing limine..."]="  Instalowanie limine..."
  ["could not copy limine's EFI binary to the ESP"]="nie udało się skopiować pliku EFI limine na partycję ESP"
  ["limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"]="limine-mkinitcpio-hook nie zainstalowany — jądro zainstalowane później NIE dostanie wpisu rozruchowego"
  ["vmlinuz-linux is not on the ESP — limine would find nothing to boot"]="vmlinuz-linux nie ma na partycji ESP — limine nie znalazłby czego uruchomić"
  ["limine's EFI binary is not on the ESP"]="pliku EFI limine nie ma na partycji ESP"
  ["limine.conf has no kernel entry"]="limine.conf nie ma żadnego wpisu jądra"
  ["  Verifying the encrypted boot path..."]="  Sprawdzanie zaszyfrowanej ścieżki rozruchu..."
  ["/boot is not a separate mount — an encrypted root needs a plain /boot"]="/boot nie jest osobnym punktem montowania — zaszyfrowany katalog główny wymaga niezaszyfrowanego /boot"
  ["/boot is missing from fstab — it would not be mounted after boot"]="brak /boot w fstab — nie zostałby zamontowany po uruchomieniu"
  ["Encrypted boot path verified"]="Zaszyfrowana ścieżka rozruchu sprawdzona"
  ["Configuring snapshots"]="Konfigurowanie migawek"
  ["snapper's config template is missing — snapshots cannot be configured"]="brakuje szablonu konfiguracji snappera — nie można skonfigurować migawek"
  ["could not write /etc/snapper/configs/root"]="nie udało się zapisać /etc/snapper/configs/root"
  ["snapper does not see the 'root' config — snapshots would never be taken"]="snapper nie widzi konfiguracji 'root' — migawki nigdy by nie powstawały"
  ["snapper's root config was not tuned — timeline snapshots would fill the disk"]="konfiguracja root snappera nie została dostrojona — cykliczne migawki zapełniłyby dysk"
  ["could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"]="nie udało się włączyć grub-btrfsd — migawki nie pojawią się same w menu rozruchowym"
  ["Snapshots enabled (snapper + snap-pac, bootable from GRUB)"]="Migawki włączone (snapper + snap-pac, uruchamialne z GRUB-a)"
  ["could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"]="nie udało się włączyć limine-snapper-sync — migawki same nie trafią do menu rozruchowego"
  ["could not take the post-install snapshot"]="nie udało się zrobić migawki po instalacji"
  ["could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"]="nie udało się włączyć synchronizacji migawek przy pierwszym uruchomieniu — menu zapełni się dopiero po pierwszej aktualizacji"
  ["Snapshots enabled (snapper + snap-pac, bootable from limine)"]="Migawki włączone (snapper + snap-pac, uruchamialne z limine)"
  ["Bootloader installed"]="Program rozruchowy zainstalowany"
  ["  The root account is locked (no root login / su).
  Note: 3 wrong password attempts lock the account for 10 minutes."]="  Konto root jest zablokowane (brak logowania na roota / su).
  Uwaga: 3 błędne hasła blokują konto na 10 minut."
  ["You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."]="Hasło szyfrowania będzie pytane przy każdym uruchomieniu,
  PRZED ekranem logowania. Nie da się go odzyskać."
  ["    syn-crypt status                    is this disk encrypted, and how
    sudo syn-crypt change-key           replace the passphrase
    sudo syn-crypt add-key              add a second one
    sudo syn-crypt backup-header FILE   save the LUKS header"]="    syn-crypt status                    czy ten dysk jest zaszyfrowany i jak
    sudo syn-crypt change-key           zmienić hasło
    sudo syn-crypt add-key              dodać drugie
    sudo syn-crypt backup-header PLIK   zapisać nagłówek LUKS"
  ["  means the data is unrecoverable even with the right passphrase."]="  oznacza, że danych nie da się odzyskać nawet z właściwym hasłem."
  ["Remove installation media and press ENTER to reboot..."]="Wyjmij nośnik instalacyjny i naciśnij ENTER, aby uruchomić ponownie..."
  ["Install SynapseOS     — right here, in this terminal"]="Zainstaluj SynapseOS    — tutaj, w tym terminalu"
  ["Install graphically   — starts the desktop first"]="Instalacja graficzna    — najpierw uruchamia pulpit"
  ["Try the live desktop  — look around; install later"]="Wypróbuj pulpit live    — rozejrzyj się; zainstaluj później"
  ["Target:"]="Cel:"
  ["ALONGSIDE"]="OBOK"
  ["ERASE"]="WYMAŻ"
  ["ADVANCED"]="ZAAWANSOWANE"
  ["Encrypt this installation?"]="Zaszyfrować tę instalację?"
  ["There is no recovery."]="Nie ma odzyskiwania."
  ["Root filesystem"]="Główny system plików"
  ["ext4   — the default. Boring, proven, repairable by anything."]="ext4   — domyślny. Nudny, sprawdzony, naprawialny czymkolwiek."
  ["btrfs  — snapshots + zstd compression. Roll back a bad update
                    from the boot menu. Uses more RAM and more CPU."]="btrfs  — migawki + kompresja zstd. Cofnij złą aktualizację
                    z menu rozruchowego. Więcej RAM-u i więcej procesora."
  ["xfs    — fast on large files. No snapshots, and it cannot be
                    SHRUNK once created."]="xfs    — szybki przy dużych plikach. Bez migawek i po utworzeniu
                    nie da się go ZMNIEJSZYĆ."
  ["f2fs   — built for flash. Good on SD cards and cheap SSDs;
                    unusual enough that fewer rescue tools know it."]="f2fs   — zrobiony pod pamięć flash. Dobry na kartach SD i tanich SSD;
                    na tyle rzadki, że mało narzędzi ratunkowych go zna."
  ["Bootloader"]="Program rozruchowy"
  ["GRUB          — the default. Detects other operating systems,
                          and the only one here that can boot a btrfs
                          snapshot."]="GRUB          — domyślny. Wykrywa inne systemy operacyjne i jako
                          jedyny tutaj potrafi uruchomić migawkę
                          btrfs."
  ["systemd-boot  — minimal. No OS detection, no snapshot menu."]="systemd-boot  — minimalny. Bez wykrywania systemów, bez menu migawek."
  ["limine        — modern and fast, and it CAN boot snapshots."]="limine        — nowoczesny i szybki, i POTRAFI uruchamiać migawki."
  ["Automatic snapshots?"]="Automatyczne migawki?"
  ["Review the plan — nothing has been written yet:"]="Sprawdź plan — nic jeszcze nie zostało zapisane:"
  ["nothing else is touched"]="nic innego nie jest ruszane"
  ["not"]="nie zostanie"
  ["Partition"]="Podziel"
  ["now."]="teraz."
  ["Partitions now on"]="Partycje teraz na"
  ["These partitions will be FORMATTED"]="Te partycje zostaną SFORMATOWANE"
  ["Full      — Standard + Steam + Nix + more software"]="Pełna     — Standard + Steam + Nix + więcej oprogramowania"
  ["Standard  — the SynapseOS suite, Firefox, AI model,"]="Standard  — pakiet SynapseOS, Firefox, model SI,"
  ["Minimal   — core daemons only: none of the above"]="Minimalna — tylko demony rdzenia: nic z powyższych"
  ["Custom    — tick every package yourself, ours and"]="Własna    — zaznacz każdy pakiet sam, nasze i"
  ["Which AI model should this machine run?"]="Który model SI ma działać na tej maszynie?"
  ["Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"]="Mistral 7B Instruct   ~4,1 GB   zalecany — pod niego dostrojono SynapseOS"
  ["Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"]="Phi-3 Mini 4K         ~2,2 GB   o połowę mniejszy i zauważalnie słabszy"
  ["Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"]="Qwen2 0.5B            ~0,4 GB   zmieści się wszędzie i tak też odpowiada"
  ["None                            skip it — nothing else changes"]="Żaden                           pominąć — nic innego się nie zmienia"
  ["Installing:"]="Instalowane:"
  ["SynapseUI  — AI-native Wayland compositor  (default)"]="SynapseUI  — kompozytor Wayland stworzony pod SI  (domyślnie)"
  ["SynapseUI  — NOT AVAILABLE: synui was not selected"]="SynapseUI  — NIEDOSTĘPNE: synui nie został wybrany"
  ["KDE Plasma — Full-featured Wayland desktop"]="KDE Plasma — pełny pulpit Wayland"
  ["GNOME      — Clean, modern Wayland desktop"]="GNOME      — czysty, nowoczesny pulpit Wayland"
  ["TTY only   — No GUI (headless/server)"]="Tylko TTY  — bez interfejsu graficznego (bezgłowy/serwer)"
  ["Disk:"]="Dysk:"
  ["Boot:"]="Rozruch:"
  ["Encrypted:"]="Zaszyfrowany:"
  ["Desktop:"]="Pulpit:"
  ["User:"]="Użytkownik:"
  ["Hostname:"]="Nazwa komputera:"
  ["Back up the header to another machine."]="Zapisz nagłówek na innej maszynie."
  ["%s is mounted — unmount it first\\n"]="%s jest zamontowany — najpierw go odmontuj
"
  ["%s is %s MiB — %s needs at least %s MiB\\n"]="%s ma %s MiB — %s wymaga co najmniej %s MiB
"
  ["  Generating %s (a few seconds)...\\n"]="  Generowanie %s (kilka sekund)...
"
  ["Language: %s  (%s, keyboard %s)\\n"]="Język: %s  (%s, klawiatura %s)
"
  ["  This disk already holds %s partition(s), an EFI System\\n  Partition (%s), and %s GiB of free space.\\n"]="  Ten dysk zawiera już %s partycji, partycję systemową
  EFI (%s) oraz %s GiB wolnego miejsca.
"
  ["    1) Install %s — use the free space, keep everything else\\n"]="    1) Zainstaluj %s — użyj wolnego miejsca, zachowaj całą resztę
"
  ["    2) %s the whole disk — delete every partition and all data\\n"]="    2) %s cały dysk — usuń każdą partycję i wszystkie dane
"
  ["    3) %s — partition this disk yourself, then pick the partitions\\n"]="    3) %s — podziel ten dysk samodzielnie, potem wybierz partycje
"
  ["    1) %s the whole disk — delete every partition and all data  (default)\\n"]="    1) %s cały dysk — usuń każdą partycję i wszystkie dane  (domyślnie)
"
  ["    2) %s — partition this disk yourself, then pick the partitions\\n"]="    2) %s — podziel ten dysk samodzielnie, potem wybierz partycje
"
  ["  %s If you forget the passphrase the data is\\n  gone — no password reset, no support call, nothing.\\n"]="  %s Jeśli zapomnisz hasła, dane są stracone —
  bez resetu, bez telefonu do pomocy technicznej, bez niczego.
"
  ["  snapper takes a snapshot before and after every pacman\\n  transaction, and %s grows a menu to boot any of them. A\\n  bad upgrade becomes a reboot instead of a rescue USB.\\n"]="  snapper robi migawkę przed każdą transakcją pacmana i po niej,
  a %s dostaje menu, z którego można uruchomić dowolną z nich. Zła
  aktualizacja staje się ponownym uruchomieniem, a nie pendrivem ratunkowym.
"
  ["    Disk          : %s\\n"]="    Dysk          : %s
"
  ["    Firmware      : %s\\n"]="    Firmware      : %s
"
  ["    Filesystem    : %s\\n"]="    System plików : %s
"
  ["    Bootloader    : %s\\n"]="    Rozruch       : %s
"
  ["    Separate /boot: %s\\n"]="    Osobny /boot: %s
"
  ["    Encryption    : %s\\n"]="    Szyfrowanie   : %s
"
  ["    Snapshots     : %s\\n"]="    Migawki       : %s
"
  ["  Encrypting %s (LUKS2)...\\n"]="  Szyfrowanie %s (LUKS2)...
"
  ["  Formatting root partition (%s)...\\n"]="  Formatowanie partycji głównej (%s)...
"
  ["    • KEEP   all %s existing partition(s), including Windows\\n"]="    • ZACHOWAJ wszystkie %s istniejące partycje, łącznie z Windowsem
"
  ["    • REUSE  %s as the EFI partition (mounted, %s formatted)\\n"]="    • UŻYJ PONOWNIE %s jako partycji EFI (zamontowana, %s formatowana)
"
  ["    • CREATE a new ext4 root of ~%s GiB in the free space\\n"]="    • UTWÓRZ   nową partycję główną ext4 o ~%s GiB w wolnym miejscu
"
  ["  Creating root partition in free space (%sMiB–%sMiB)...\\n"]="  Tworzenie partycji głównej w wolnym miejscu (%s MiB–%s MiB)...
"
  ["  Formatting new root (%s, ext4)...\\n"]="  Formatowanie nowej partycji głównej (%s, ext4)...
"
  ["  %s %s %s The installer will re-read the table when you exit.\\n"]="  %s %s %s Instalator ponownie odczyta tablicę, gdy wyjdziesz.
"
  ["    • a root partition, at least %s GiB\\n"]="    • partycji głównej, co najmniej %s GiB
"
  ["    • a separate /boot of ~1 GiB — %s with this layout cannot read the root\\n"]="    • osobnego /boot o ~1 GiB — %s przy tym układzie nie odczyta partycji głównej
"
  ["  Starting %s on %s — write your changes before quitting.\\n"]="  Uruchamianie %s na %s — zapisz zmiany przed wyjściem.
"
  ["    %s is already swap — another system may resume from it.\\n"]="    %s jest już partycją wymiany — inny system może z niej wznawiać.
"
  ["  Everything else on %s is left untouched.\\n"]="  Cała reszta na %s pozostaje nietknięta.
"
  ["  Making swap on %s...\\n"]="  Tworzenie wymiany na %s...
"
  ["  Formatting EFI partition (%s MiB)...\\n"]="  Formatowanie partycji EFI (%s MiB)...
"
  ["  NVIDIA GPU detected — installing %s (builds the module, takes a while)...\\n"]="  Wykryto GPU NVIDIA — instalowanie %s (buduje moduł, chwilę to potrwa)...
"
  ["  Installing video stack: %s %s...\\n"]="  Instalowanie warstwy graficznej: %s %s...
"
  ["  [cachyos] enabled (%s packages available)\\n"]="  [cachyos] włączone (dostępnych pakietów: %s)
"
  ["  Language: %s  (chosen at boot)\\n"]="  Język: %s  (wybrany przy uruchomieniu)
"
  ["  Locale:   %s   Keyboard: %s (console) / %s (desktop)\\n"]="  Lokalizacja: %s   Klawiatura: %s (konsola) / %s (pulpit)
"
  ["  Installing fonts (%s)...\\n"]="  Instalowanie czcionek (%s)...
"
  ["  Downloading the AI model (%s) — this is the long part of\\n"]="  Pobieranie modelu SI (%s) — to jest ta długa część
"
  ["  Nothing is built yet. As %s, after the first boot:\\n"]="  Nic jeszcze nie zbudowano. Jako %s, po pierwszym uruchomieniu:
"
  ["  Adding the %s hook to mkinitcpio...\\n"]="  Dodawanie haka %s do mkinitcpio...
"
  ["  Installing GRUB (%s)...\\n"]="  Instalowanie GRUB-a (%s)...
"
  ["yes — LUKS2 on %s"]="tak — LUKS2 na %s"
  ["  Admin: use %s with your user password.\\n"]="  Administracja: użyj %s ze swoim hasłem użytkownika.
"
  ["  Manage it later with %s:\\n"]="  Zarządzaj tym później przez %s:
"
  ["  %s A damaged LUKS header\\n"]="  %s Uszkodzony nagłówek LUKS
"
  ["%s is on the live/boot device — that is the installer's own media\\n"]="%s znajduje się na nośniku live/rozruchowym — to własny nośnik instalatora
"
  ["    %s is already FAT — it may hold another OS's bootloader.\\n"]="    %s jest już w formacie FAT — może zawierać program rozruchowy innego systemu.
"
  ["  Creating user '%s'...\\n"]="  Tworzenie użytkownika '%s'...
"
  ["  User '%s' created (uid=%s)\\n"]="  Użytkownik '%s' utworzony (uid=%s)
"
  ["  Log in as '%s' after reboot.\\n"]="  Po ponownym uruchomieniu zaloguj się jako '%s'.
"
  ["  Type '%s' to get started.\\n"]="  Wpisz '%s', aby zacząć.
"
  ["Install SynapseOS"]="Zainstaluj SynapseOS"
  ["SynapseOS packages"]="Pakiety SynapseOS"
  ["Everything the system is made of. What you cannot drop is what something else you kept depends on — those are turned back on and named before anything is installed."]="Wszystko, z czego składa się system. To, czego nie da się odznaczyć, jest potrzebne czemuś innemu, co zostało zachowane — takie pozycje są z powrotem włączane i wymieniane z nazwy, zanim cokolwiek zostanie zainstalowane."
  ["SYNAPSE UI — the Wayland desktop"]="SYNAPSE UI — pulpit Wayland"
  ["synapd — the local AI daemon"]="synapd — lokalna usługa SI"
  ["synsh — the AI-native shell"]="synsh — powłoka natywna dla SI"
  ["synguard + kernel module"]="synguard + moduł jądra"
  ["synnet — network policy"]="synnet — reguły sieciowe"
  ["Software — the package manager"]="Software — menedżer pakietów"
  ["Files — the file manager"]="Pliki — menedżer plików"
  ["Terminal (synui depends on it)"]="Terminal (synui go potrzebuje)"
  ["Settings"]="Ustawienia"
  ["Disks"]="Dyski"
  ["Editor"]="Edytor"
  ["Calendar"]="Kalendarz"
  ["File Vault — a locked folder"]="Sejf — zamknięty katalog"
  ["Disk Cleanup — caches, and secure delete"]="Czyszczenie dysku — pamięci podręczne i bezpieczne usuwanie"
  ["syn-update — how fixes arrive"]="syn-update — tędy przychodzą poprawki"
  ["syn — the top-level CLI"]="syn — główna linia poleceń"
  ["syn-model — fetch AI models"]="syn-model — pobieranie modeli SI"
  ["syn-confine — the sandbox"]="syn-confine — piaskownica"
  ["fetch — the About OS readout"]="fetch — podsumowanie systemu"
  ["Arcade — overlay, pads, big screen"]="Arcade — nakładka, pady, duży ekran"
  ["cliamp — the music player"]="cliamp — odtwarzacz muzyki"
  ["Player — playlists, shuffle and history, on mpv"]="Player — playlisty, losowanie i historia, na mpv"
  ["Studio — photo darkroom and video"]="Studio — ciemnia fotograficzna i wideo"
  ["GeForce NOW — cloud gaming in a browser"]="GeForce NOW — granie w chmurze w przeglądarce"
  ["Arsenal — BlackArch browser"]="Arsenal — przeglądanie BlackArch"
  ["Chibi — voice companion"]="Chibi — towarzysz głosowy"
  ["Vibe — AI coding assistant"]="Vibe — asystent programowania SI"
  ["Animated wallpapers (~317 MB)"]="Animowane tapety (~317 MB)"
  ["Nexus Chat (pulls in Firefox)"]="Nexus Chat (pociąga za sobą Firefoksa)"
  ["TEPRIS (pulls in Firefox)"]="TEPRIS (pociąga za sobą Firefoksa)"
  ["Web and communication"]="Sieć i komunikacja"
  ["None of this is ours; every name is in the Arch repositories. Firefox is on by default because an installed SynapseOS used to arrive with no browser at all."]="Nic z tego nie jest nasze; każda nazwa jest w repozytoriach Archa. Firefox jest domyślnie włączony, bo zainstalowany SynapseOS przychodził kiedyś zupełnie bez przeglądarki."
  ["Thunderbird — mail"]="Thunderbird — poczta"
  ["KeePassXC — passwords"]="KeePassXC — hasła"
  ["Syncthing — file sync"]="Syncthing — synchronizacja plików"
  ["LocalSend — send to phone (Flatpak)"]="LocalSend — wysyłanie na telefon (Flatpak)"
  ["Audio and video"]="Dźwięk i wideo"
  ["Office and graphics"]="Biuro i grafika"
  ["Development and admin"]="Programowanie i administracja"
  ["VS Code (OSS build)"]="VS Code (wydanie OSS)"
  ["7zip + unrar"]="7zip + unrar"
  ["Games, launchers and helpers"]="Gry, launchery i narzędzia"
  ["Steam is in the options below rather than here: it is the only one that turns on a second architecture and a third repository."]="Steam jest w opcjach poniżej, a nie tutaj: jako jedyny włącza drugą architekturę i trzecie repozytorium."
  ["Prism — Minecraft"]="Prism — Minecraft"
  ["Dolphin — GameCube/Wii"]="Dolphin — GameCube/Wii"
  ["PPSSPP — PSP"]="PPSSPP — PSP"
  ["Space Cadet Pinball (Flatpak)"]="Space Cadet Pinball (Flatpak)"
  ["GOverlay — MangoHud"]="GOverlay — MangoHud"
  ["AntiMicroX — pad remap"]="AntiMicroX — mapowanie padów"
  ["Welcome"]="Powitanie"
  ["Disk"]="Dysk"
  ["Software"]="Oprogramowanie"
  ["Account"]="Konto"
  ["Region"]="Region"
  ["Summary"]="Podsumowanie"
  ["Install"]="Instalacja"
  ["the installer's own media"]="nośnik samego instalatora"
  ["%1 GiB — SynapseOS needs at least %2 GiB"]="%1 GiB — SynapseOS potrzebuje co najmniej %2 GiB"
  ["No connection. SynapseOS downloads the base system while it installs, so this needs a working network before it can start."]="Brak połączenia. SynapseOS pobiera system podstawowy w trakcie instalacji, więc potrzebuje działającej sieci, zanim będzie mógł się zacząć."
  ["Choose a disk to install to."]="Wybierz dysk do instalacji."
  ["The encryption passphrase needs at least 8 characters."]="Hasło szyfrowania musi mieć co najmniej 8 znaków."
  ["With neither the package manager nor the desktop, this install has no way to add either one back. Keep at least one."]="Bez menedżera pakietów i bez pulpitu ta instalacja nie ma jak dodać z powrotem żadnego z nich. Zostaw przynajmniej jedno."
  ["A username is lower-case letters, digits, - and _, and cannot start with a digit."]="Nazwa użytkownika składa się z małych liter, cyfr, - i _, i nie może zaczynać się od cyfry."
  ["Set a password for the account."]="Ustaw hasło dla konta."
  ["The two passwords do not match."]="Oba hasła nie są takie same."
  ["A locale is needed, e.g. en_US.UTF-8."]="Potrzebna jest lokalizacja, np. pl_PL.UTF-8."
  ["A timezone is needed, e.g. Europe/Lisbon."]="Potrzebna jest strefa czasowa, np. Europe/Warsaw."
  ["printing"]="drukowanie"
  ["%1 repo"]="repozytorium %1"
  ["Mode"]="Tryb"
  ["Filesystem"]="System plików"
  ["%1 on LUKS2"]="%1 na LUKS2"
  ["%1 + snapshots"]="%1 + migawki"
  ["none"]="brak"
  ["Desktop"]="Pulpit"
  ["Locale"]="Lokalizacja"
  ["%1   keys %2 / %3"]="%1   klawisze %2 / %3"
  ["Timezone"]="Strefa czasowa"
  ["%1 package(s) — WITHOUT %2"]="%1 pakiet(ów) — BEZ %2"
  ["%1 package(s)"]="%1 pakiet(ów)"
  ["Options"]="Opcje"
  ["Could not write the install profile."]="Nie udało się zapisać profilu instalacji."
  ["Installation complete."]="Instalacja zakończona."
  ["Installation failed — see the log."]="Instalacja nie powiodła się — zobacz dziennik."
  ["No network connection"]="Brak połączenia sieciowego"
  ["The base system is downloaded while it installs, so this cannot start offline. Plug in a cable or join a network, then press Re-check — the answers on these pages are kept."]="System podstawowy jest pobierany w trakcie instalacji, więc bez sieci nie da się zacząć. Podłącz kabel albo połącz się z siecią i naciśnij Sprawdź ponownie — odpowiedzi z tych stron zostaną zachowane."
  ["Checking…"]="Sprawdzanie…"
  ["Re-check"]="Sprawdź ponownie"
  ["Wi-Fi settings"]="Ustawienia Wi-Fi"
  ["This asks for a disk, an account and a few preferences, then hands the answers to the same installer the text version runs. Nothing is written to any disk until the last page, and that page says exactly what it is about to do."]="Tutaj pytamy o dysk, konto i kilka ustawień, a potem przekazujemy odpowiedzi temu samemu instalatorowi, który uruchamia wersja tekstowa. Aż do ostatniej strony nic nie jest zapisywane na żaden dysk, a ta strona mówi dokładnie, co zaraz zrobi."
  ["A disk is partitioned and formatted"]="Dysk zostaje podzielony na partycje i sformatowany"
  ["The base system and the SynapseOS packages are installed"]="System podstawowy i pakiety SynapseOS zostają zainstalowane"
  ["An account and a desktop are set up"]="Konto i pulpit zostają skonfigurowane"
  ["A bootloader is written"]="Zostaje zapisany bootloader"
  ["Partitioning an existing layout by hand is the text installer's ADVANCED mode — quit this and run \`syn-install\` in a terminal for that."]="Ręczne partycjonowanie istniejącego układu to tryb ADVANCED instalatora tekstowego — zamknij to okno i uruchom \`syn-install\` w terminalu."
  ["Where should SynapseOS go?"]="Gdzie ma trafić SynapseOS?"
  ["The installer's own media is listed and cannot be chosen."]="Nośnik samego instalatora jest wypisany i nie można go wybrać."
  ["No disks found."]="Nie znaleziono dysków."
  ["Erase the disk"]="Wymaż dysk"
  ["every partition and all data"]="każdą partycję i wszystkie dane"
  ["Install alongside"]="Zainstaluj obok"
  ["use free space, UEFI only"]="użyj wolnego miejsca, tylko UEFI"
  ["Snapshots"]="Migawki"
  ["btrfs + limine only"]="tylko btrfs + limine"
  ["Encrypt the disk"]="Zaszyfruj dysk"
  ["Passphrase"]="Hasło"
  ["8 characters or more"]="8 znaków lub więcej"
  ["What should be installed?"]="Co ma zostać zainstalowane?"
  ["The SynapseOS core — the compositor, the daemons and the applications it is built on — is installed by every choice here."]="Rdzeń SynapseOS — kompozytor, usługi i aplikacje, na których jest zbudowany — jest instalowany przy każdym wyborze tutaj."
  ["Full"]="Pełna"
  ["Standard + Steam + Nix + more software"]="Standardowa + Steam + Nix + więcej oprogramowania"
  ["Standard"]="Standardowa"
  ["the SynapseOS suite, Firefox, AI model, Bluetooth, printing, Wine, phone"]="pakiet SynapseOS, Firefox, model SI, Bluetooth, drukowanie, Wine, telefon"
  ["Minimal"]="Minimalna"
  ["core daemons only — no apps, no software, no model"]="tylko usługi rdzenia — bez aplikacji, bez oprogramowania, bez modelu"
  ["Custom"]="Własna"
  ["tick every package yourself, ours and the ordinary software"]="zaznacz każdy pakiet samodzielnie, nasz i zwykłe oprogramowanie"
  ["(required)"]="(wymagane)"
  ["Not packages: a repository, an architecture or a service. Each is a decision with a consequence that does not fit on a checkbox above."]="To nie są pakiety: repozytorium, architektura albo usługa. Każde z nich to decyzja o skutkach, które nie mieszczą się w kratce powyżej."
  ["Printing (CUPS)"]="Drukowanie (CUPS)"
  ["Wine — run Windows .exe/.msi"]="Wine — uruchamianie .exe/.msi z Windowsa"
  ["KDE Connect — pair a phone"]="KDE Connect — sparowanie telefonu"
  ["Steam + game stack + Proton (~3.1 GB)"]="Steam + zestaw do gier + Proton (~3,1 GB)"
  ["BlackArch repo — ~5000 tools, none installed"]="Repozytorium BlackArch — ~5000 narzędzi, żadne nie instalowane"
  ["Nix + Home Manager"]="Nix + Home Manager"
  ["syn-update is off: this machine will have no way to receive another SynapseOS package. Fixing that later means installing it by hand from the ISO, or reinstalling."]="syn-update jest wyłączony: ta maszyna nie będzie miała jak otrzymać kolejnego pakietu SynapseOS. Naprawienie tego później oznacza ręczną instalację z obrazu ISO albo instalację od nowa."
  ["synui is off: this will not be a SynapseOS desktop. The Desktop page offers KDE, GNOME or no GUI."]="synui jest wyłączony: to nie będzie pulpit SynapseOS. Strona Pulpit oferuje KDE, GNOME albo brak interfejsu graficznego."
  ["AI model — downloaded during the install"]="Model SI — pobierany w trakcie instalacji"
  ["~4.1 GB — recommended"]="~4,1 GB — zalecany"
  ["~2.2 GB — weaker"]="~2,2 GB — słabszy"
  ["~0.4 GB — much weaker"]="~0,4 GB — dużo słabszy"
  ["None"]="Brak"
  ["AI stays inert"]="SI pozostaje bezczynna"
  ["NVIDIA GPU inference"]="Wnioskowanie na GPU NVIDIA"
  ["the CUDA runtime, ~4.7 GiB"]="środowisko CUDA, ~4,7 GiB"
  ["Who is this machine for?"]="Dla kogo jest ta maszyna?"
  ["Username"]="Nazwa użytkownika"
  ["lower-case, no spaces"]="małe litery, bez spacji"
  ["Full name (optional)"]="Imię i nazwisko (opcjonalnie)"
  ["Password"]="Hasło"
  ["Password again"]="Hasło ponownie"
  ["They do not match"]="Nie są takie same"
  ["the native compositor"]="własny kompozytor"
  ["synui is not selected"]="synui nie jest wybrany"
  ["headless"]="bez interfejsu graficznego"
  ["Language, keyboard and time"]="Język, klawiatura i czas"
  ["Pick a language and the other three follow it. The console keymap and the desktop layout are separate on purpose — Swedish is 'sv-latin1' to the console and 'se' to the desktop — so they can be changed on their own afterwards."]="Wybierz język, a pozostałe trzy pójdą za nim. Układ klawiszy konsoli i układ pulpitu są celowo osobne — szwedzki to 'sv-latin1' dla konsoli i 'se' dla pulpitu — żeby dało się je później zmieniać niezależnie."
  ["Language"]="Język"
  ["sets the keyboard and the fonts too"]="ustawia też klawiaturę i czcionki"
  ["typed by hand — fonts cover as much as possible"]="wpisane ręcznie — czcionki pokrywają tyle, ile się da"
  ["Sets the locale, both keyboard names and the font pack. Any locale glibc has can be typed instead."]="Ustawia lokalizację, obie nazwy klawiatury i paczkę czcionek. Zamiast tego można wpisać dowolną lokalizację, którą zna glibc."
  ["The common zones first, then every name tzdata ships."]="Najpierw popularne strefy, potem każda nazwa, którą przynosi tzdata."
  ["Console keymap"]="Układ klawiszy konsoli"
  ["loadkeys — the text console and the greeter"]="loadkeys — konsola tekstowa i ekran logowania"
  ["Every keymap this image can load. This one names a file loadkeys has to find, which is why it is not the same list as the desktop layout."]="Każdy układ klawiszy, który ten obraz potrafi wczytać. Ten wskazuje plik, który loadkeys musi znaleźć — dlatego nie jest to ta sama lista co układ pulpitu."
  ["Desktop layout"]="Układ pulpitu"
  ["XKB — the compositor"]="XKB — kompozytor"
  ["Desktop keyboard layout"]="Układ klawiatury pulpitu"
  ["The layouts xkbcommon can compile. 'uk' is a console keymap and not a layout here — the layout is 'gb'."]="Układy, które potrafi skompilować xkbcommon. 'uk' to układ klawiszy konsoli i tutaj nie jest układem — układ nazywa się 'gb'."
  ["Read this back"]="Przeczytaj to jeszcze raz"
  ["Nothing has been written yet. The next button is the one that starts."]="Nic jeszcze nie zostało zapisane. Następny przycisk jest tym, który zaczyna."
  ["EVERY PARTITION ON %1 WILL BE DELETED"]="KAŻDA PARTYCJA NA %1 ZOSTANIE USUNIĘTA"
  ["SynapseOS will be installed into the free space on %1"]="SynapseOS zostanie zainstalowany w wolnym miejscu na %1"
  ["SynapseOS is installed"]="SynapseOS jest zainstalowany"
  ["The install stopped"]="Instalacja została przerwana"
  ["Installing SynapseOS"]="Instalowanie SynapseOS"
  ["Reboot and remove the installation media."]="Uruchom ponownie i wyjmij nośnik instalacyjny."
  ["The log below is the whole story — the last lines say why."]="Dziennik poniżej opowiada całą historię — ostatnie wiersze mówią dlaczego."
  ["This takes a while: the base system and the packages are downloaded, and an AI model is gigabytes on its own."]="To chwilę potrwa: system podstawowy i pakiety są pobierane, a sam model SI to kilka gigabajtów."
  ["Back"]="Wstecz"
  ["Next"]="Dalej"
  ["Reboot"]="Uruchom ponownie"
  ["Close"]="Zamknij"
  ["type to filter, or type a name that is not listed"]="pisz, aby filtrować, albo wpisz nazwę, której nie ma na liście"
  ["Nothing to list on this image — type the name instead."]="Na tym obrazie nie ma czego wypisać — wpisz zamiast tego nazwę."
  ["Nothing matches — the row below uses what you typed."]="Nic nie pasuje — wiersz poniżej użyje tego, co wpisano."
  ["Use “%1” as typed"]="Użyj „%1” tak, jak wpisano"
)
