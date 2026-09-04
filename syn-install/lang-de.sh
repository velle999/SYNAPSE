# Deutsch (de) — syn-install's own words.
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
  ["render.nix is missing — the 'syn' package is not installed here."]="render.nix fehlt — das Paket 'syn' ist hier nicht installiert."
  ["locale-gen failed. The live session stays in English; the install is
  unaffected, because it generates the locale inside the target."]="locale-gen ist fehlgeschlagen. Die Live-Sitzung bleibt auf Englisch; die
  Installation ist davon nicht betroffen, sie erzeugt die Locale im Zielsystem."
  ["  The keyboard, the clock, the fonts and the shell all follow this.
  You can change any of it later."]="  Tastatur, Uhr, Schriften und Shell richten sich danach.
  Alles davon lässt sich später ändern."
  ["Toggle [numbers, 'all', 'none', Enter = accept]:"]="Umschalten [Zahlen, 'all', 'none', Enter = übernehmen]:"
  ["--config needs a file"]="--config braucht eine Datei"
  ["syn-install must be run as root"]="syn-install muss als root laufen"
  ["  SynapseOS is running from the live image."]="  SynapseOS läuft vom Live-Image."
  ["Starting the desktop — the installer opens with it."]="Der Desktop wird gestartet — der Installer öffnet sich mit ihm."
  ["  This installer will:
    1. Partition a disk
    2. Install SynapseOS base system
    3. Install SynapseOS packages
    4. Create user account
    5. Choose desktop environment
    6. Configure system & bootloader"]="  Dieser Installer wird:
    1. Eine Festplatte partitionieren
    2. Das SynapseOS-Basissystem installieren
    3. Die SynapseOS-Pakete installieren
    4. Ein Benutzerkonto anlegen
    5. Eine Desktop-Umgebung wählen
    6. System und Bootloader einrichten"
  ["ALL DATA ON THE TARGET DISK WILL BE ERASED"]="ALLE DATEN AUF DER ZIELFESTPLATTE WERDEN GELÖSCHT"
  ["Press ENTER to continue or Ctrl+C to abort..."]="ENTER zum Fortfahren, Strg+C zum Abbrechen..."
  ["Checking network"]="Netzwerk wird geprüft"
  ["Network is up"]="Netzwerk ist verfügbar"
  ["  No network detected. Starting NetworkManager..."]="  Kein Netzwerk erkannt. NetworkManager wird gestartet..."
  ["  No connection — but this machine has Wi-Fi."]="  Keine Verbindung — aber dieser Rechner hat WLAN."
  ["Open the Wi-Fi picker (nmtui)? [Y/n]:"]="WLAN-Auswahl öffnen (nmtui)? [Y/n]:"
  ["No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."]="Keine Netzwerkverbindung und kein WLAN-Gerät zum Einrichten.
  SynapseOS lädt das Basissystem während der Installation herunter, also ein
  Kabel anschließen und neu starten."
  ["Network connected"]="Netzwerk verbunden"
  ["Step 1 — Select Target Disk"]="Schritt 1 — Zielfestplatte wählen"
  ["  Available disks:"]="  Verfügbare Festplatten:"
  ["Target disk (e.g. sda, vda, nvme0n1):"]="Zielfestplatte (z. B. sda, vda, nvme0n1):"
  ["Target disk is in use. Unmount its partitions and re-run."]="Die Zielfestplatte ist in Benutzung. Partitionen aushängen und neu starten."
  ["Boot mode: UEFI"]="Boot-Modus: UEFI"
  ["Boot mode: BIOS/Legacy"]="Boot-Modus: BIOS/Legacy"
  ["  Encrypts the root filesystem with LUKS2. You will be asked for the
  passphrase at every boot, before the system starts."]="  Verschlüsselt das Wurzeldateisystem mit LUKS2. Die Passphrase wird bei jedem
  Start abgefragt, bevor das System hochfährt."
  ["Encrypt the disk? [y/N]:"]="Festplatte verschlüsseln? [y/N]:"
  ["                          With encryption it is the BETTER choice: the
                          kernel lives on the EFI partition and only the
                          initramfs unlocks, so /boot needs no separate
                          unencrypted partition."]="                          Mit Verschlüsselung ist er die BESSERE Wahl: der
                          Kernel liegt auf der EFI-Partition und nur die
                          Initramfs entsperrt, /boot braucht also keine
                          eigene unverschlüsselte Partition."
  ["                          It copies each snapshot's kernel onto the EFI
                          partition, so that partition is made much
                          larger when snapshots are enabled."]="                          Es kopiert den Kernel jedes Snapshots auf die
                          EFI-Partition, die deshalb deutlich größer
                          angelegt wird, wenn Snapshots aktiv sind."
  ["  Snapshots are cheap but not free: they hold the old copy of
  anything that changes, so a disk near full stays near full."]="  Snapshots sind günstig, aber nicht umsonst: sie halten die alte Kopie von
  allem, was sich ändert — eine fast volle Platte bleibt also fast voll."
  ["Enable snapshots? [Y/n]:"]="Snapshots aktivieren? [Y/n]:"
  ["mkfs.ext4 is missing from this installer image — /boot cannot be created"]="mkfs.ext4 fehlt in diesem Installer-Image — /boot kann nicht angelegt werden"
  ["btrfs is missing from this installer image — subvolumes cannot be created"]="btrfs fehlt in diesem Installer-Image — Subvolumes können nicht angelegt werden"
  ["Are these correct? [Y/n]:"]="Ist das korrekt? [Y/n]:"
  ["Starting the questions over — the disk has not been touched."]="Die Fragen beginnen von vorn — die Festplatte wurde nicht angerührt."
  ["cryptsetup is not available on this installer image"]="cryptsetup ist in diesem Installer-Image nicht verfügbar"
  ["Encryption passphrase:"]="Passphrase für die Verschlüsselung:"
  ["Repeat passphrase:"]="Passphrase wiederholen:"
  ["Empty passphrase — that would leave the disk unprotected."]="Leere Passphrase — die Festplatte bliebe ungeschützt."
  ["Passphrases did not match — try again."]="Die Passphrasen stimmen nicht überein — bitte erneut."
  ["Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."]="Die Passphrase hat weniger als 8 Zeichen. Eine kurze nützt wenig gegen
  jemanden, der die Festplatte in der Hand hält."
  ["Use it anyway? [y/N]:"]="Trotzdem verwenden? [y/N]:"
  ["Encryption enabled — root will be LUKS2"]="Verschlüsselung aktiv — root wird LUKS2"
  ["cryptsetup open failed — the passphrase did not take"]="cryptsetup open ist fehlgeschlagen — die Passphrase wurde nicht angenommen"
  ["Failed to mount root"]="root konnte nicht eingehängt werden"
  ["  Creating btrfs subvolumes..."]="  btrfs-Subvolumes werden angelegt..."
  ["btrfs: could not create @"]="btrfs: @ konnte nicht angelegt werden"
  ["btrfs: could not create @home"]="btrfs: @home konnte nicht angelegt werden"
  ["btrfs: could not create @snapshots"]="btrfs: @snapshots konnte nicht angelegt werden"
  ["btrfs: could not create @var_log"]="btrfs: @var_log konnte nicht angelegt werden"
  ["btrfs: could not create @pkg"]="btrfs: @pkg konnte nicht angelegt werden"
  ["could not remount the btrfs root onto @"]="die btrfs-Wurzel konnte nicht auf @ neu eingehängt werden"
  ["Failed to mount @"]="@ konnte nicht eingehängt werden"
  ["Failed to mount @home"]="@home konnte nicht eingehängt werden"
  ["Failed to mount @snapshots"]="@snapshots konnte nicht eingehängt werden"
  ["Failed to mount @var_log"]="@var_log konnte nicht eingehängt werden"
  ["Failed to mount @pkg"]="@pkg konnte nicht eingehängt werden"
  ["This adds one partition in the free space. Back up anything irreplaceable first."]="Dabei wird eine Partition im freien Speicher angelegt. Sichern Sie vorher alles Unersetzliche."
  ["Type 'yes' to install alongside:"]="Zum Danebeninstallieren 'yes' eingeben:"
  ["Aborted"]="Abgebrochen"
  ["Failed to create the root partition"]="Die Root-Partition konnte nicht angelegt werden"
  ["Could not identify the new partition after creating it"]="Die neue Partition konnte nach dem Anlegen nicht identifiziert werden"
  ["Failed to format root partition"]="Die Root-Partition konnte nicht formatiert werden"
  ["Failed to mount the existing ESP"]="Die vorhandene ESP konnte nicht eingehängt werden"
  ["no partition editor on this image (cfdisk, fdisk and parted are all missing)"]="kein Partitionierungsprogramm in diesem Image (cfdisk, fdisk und parted fehlen alle)"
  ["  What this install needs:"]="  Was diese Installation braucht:"
  ["    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"]="    • eine EFI-Systempartition (Typ EF00 / Flag 'esp') — eine vorhandene kann weiterverwendet werden"
  ["  Skipping the partition editor (--config)."]="  Partitionierungsprogramm wird übersprungen (--config)."
  ["Format it? Everything on it is lost [y/N]:"]="Formatieren? Alles darauf geht verloren [y/N]:"
  ["Separate /boot partition:"]="Separate /boot-Partition:"
  ["Swap partition (blank for none):"]="Swap-Partition (leer für keine):"
  ["Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"]="Neu anlegen? Die UUID ändert sich und zerstört die fstab jenes Systems [y/N]:"
  ["Type 'yes' to format these:"]="Zum Formatieren 'yes' eingeben:"
  ["  Formatting EFI partition..."]="  EFI-Partition wird formatiert..."
  ["  Formatting /boot partition..."]="  /boot-Partition wird formatiert..."
  ["Failed to mount /boot"]="/boot konnte nicht eingehängt werden"
  ["Type 'yes' to confirm:"]="Zum Bestätigen 'yes' eingeben:"
  ["  Creating GPT partition table..."]="  GPT-Partitionstabelle wird angelegt..."
  ["Failed to format EFI partition"]="Die EFI-Partition konnte nicht formatiert werden"
  ["Failed to format boot partition"]="Die Boot-Partition konnte nicht formatiert werden"
  ["  Creating MBR partition table..."]="  MBR-Partitionstabelle wird angelegt..."
  ["Disk partitioned and mounted at /mnt"]="Festplatte partitioniert und unter /mnt eingehängt"
  ["Step 3 — Installing Base System"]="Schritt 3 — Basissystem wird installiert"
  ["  Initializing pacman keyring..."]="  pacman-Schlüsselbund wird eingerichtet..."
  ["  Running pacstrap (this may take several minutes)..."]="  pacstrap läuft (das kann einige Minuten dauern)..."
  ["pacstrap failed — check network connection"]="pacstrap fehlgeschlagen — Netzwerkverbindung prüfen"
  ["grub-install not found in chroot — attempting recovery..."]="grub-install im chroot nicht gefunden — es wird versucht, das zu beheben..."
  ["Could not install grub into target — check network"]="grub konnte nicht ins Zielsystem installiert werden — Netzwerk prüfen"
  ["Base system installed"]="Basissystem installiert"
  ["Step 4 — Choose What to Install"]="Schritt 4 — Auswählen, was installiert wird"
  ["  What should be installed alongside the SynapseOS core?"]="  Was soll neben dem SynapseOS-Kern installiert werden?"
  ["                   Bluetooth, printing, Wine, phone   (default)"]="                   Bluetooth, Drucken, Wine, Telefon   (Standard)"
  ["                   the ordinary software people install anyway"]="                   die übliche Software, die man ohnehin installiert"
  ["  Every preset except Minimal then asks WHICH AI model to download,
  and skipping it is one of the answers."]="  Jede Vorauswahl außer Minimal fragt danach, WELCHES KI-Modell geladen
  werden soll — und es zu überspringen ist eine der Antworten."
  ["Full install selected"]="Vollständige Installation gewählt"
  ["Minimal install selected"]="Minimale Installation gewählt"
  ["  Two kinds of question. First the packages, as pages of
  checkboxes; then the handful of options that are a whole
  subsystem rather than a package."]="  Zwei Arten von Fragen. Zuerst die Pakete, als Seiten mit
  Kästchen; dann die wenigen Optionen, die ein ganzes
  Subsystem sind und kein Paket."
  ["  And the software people install on the first evening anyway.
  All of it is in the Arch repositories; none of it is ours."]="  Und die Software, die man am ersten Abend ohnehin installiert.
  Alles davon steht in den Arch-Paketquellen; nichts davon ist von uns."
  ["  The rest is y/n. The default (shown in caps) is Standard."]="  Der Rest ist j/n. Die Vorgabe (in Großbuchstaben) ist Standard."
  ["syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."]="syn-update abgewählt: dieser Rechner hat dann keinen Weg mehr, ein
  weiteres SynapseOS-Paket zu erhalten. Das später zu beheben heißt, es von Hand
  von der ISO zu installieren — oder neu zu installieren."
  ["Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."]="Weder der Desktop noch der KI-Dienst wurde behalten. Das ist ein Arch-
  System mit ein paar SynapseOS-Werkzeugen darauf, was eine zulässige Antwort ist —
  aber nichts in der Dokumentation beschreibt den Rechner, den Sie bekommen."
  ["Custom install configured"]="Benutzerdefinierte Installation eingerichtet"
  ["Standard install selected"]="Standard-Installation gewählt"
  ["  synapd loads one model and everything AI in SynapseOS talks to it:
  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,
  over this connection, onto the disk you are installing to."]="  synapd lädt ein Modell, und alles KI-artige in SynapseOS spricht damit:
  synsh, das KI-Panel des Desktops, Chibi, Vibe. Es wird jetzt geladen,
  über diese Verbindung, auf die Platte, auf die Sie installieren."
  ["  A smaller model is not just faster and lighter: it follows
  instructions worse. synsh mistakes what you asked for, Vibe's code
  needs more fixing, Chibi loses the thread. Take the default unless
  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."]="  Ein kleineres Modell ist nicht nur schneller und leichter: es folgt
  Anweisungen schlechter. synsh versteht Ihre Anfrage falsch, Vibes Code
  braucht mehr Nacharbeit, Chibi verliert den Faden. Nehmen Sie die Vorgabe,
  außer Platte oder RAM sprechen dagegen — 7B will ~6 GB freies RAM."
  ["  Whatever you pick, it can be changed later: 'syn model download',
  or Super+C ▸ System ▸ AI model on the desktop."]="  Was Sie auch wählen, es lässt sich später ändern: 'syn model download',
  oder Super+C ▸ System ▸ KI-Modell auf dem Desktop."
  ["Install this selection? [Y/n]:"]="Diese Auswahl installieren? [Y/n]:"
  ["Choosing again — nothing has been installed yet."]="Erneute Auswahl — es wurde noch nichts installiert."
  ["Step 4b — Installing SynapseOS"]="Schritt 4b — SynapseOS wird installiert"
  ["Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."]="ILoveCandy konnte in /etc/pacman.conf nicht aktiviert werden (nur kosmetisch)."
  ["  Enabling [multilib] (32-bit repo, needed by Steam)..."]="  [multilib] wird aktiviert (32-Bit-Quelle, von Steam gebraucht)..."
  ["Could not sync the multilib database — Steam may fail to install"]="Die multilib-Datenbank konnte nicht synchronisiert werden — Steam lässt sich womöglich nicht installieren"
  ["Could not enable [multilib]; Steam will be skipped."]="[multilib] konnte nicht aktiviert werden; Steam wird übersprungen."
  ["Some SynapseOS packages failed to install — verifying below"]="Einige SynapseOS-Pakete ließen sich nicht installieren — wird unten geprüft"
  ["No SynapseOS packages were selected. This will be an Arch system."]="Es wurden keine SynapseOS-Pakete gewählt. Das wird ein Arch-System."
  ["SynapseOS packages installed"]="SynapseOS-Pakete installiert"
  ["Component selection recorded in /etc/synapseos/components.conf"]="Komponentenauswahl in /etc/synapseos/components.conf festgehalten"
  ["Step 5 — Create User Account"]="Schritt 5 — Benutzerkonto anlegen"
  ["  Create a user account for the installed system."]="  Legen Sie ein Benutzerkonto für das installierte System an."
  ["Username [default: syn]:"]="Benutzername [Vorgabe: syn]:"
  ["Full name (optional):"]="Vollständiger Name (optional):"
  ["Password:"]="Passwort:"
  ["Confirm password:"]="Passwort bestätigen:"
  ["Passwords do not match or are empty — try again"]="Die Passwörter stimmen nicht überein oder sind leer — bitte erneut"
  ["Step 6 — Desktop Environment"]="Schritt 6 — Desktop-Umgebung"
  ["  Choose a desktop environment:"]="  Wählen Sie eine Desktop-Umgebung:"
  ["  Installing KDE Plasma..."]="  KDE Plasma wird installiert..."
  ["Some KDE packages failed to install"]="Einige KDE-Pakete ließen sich nicht installieren"
  ["KDE Plasma installed"]="KDE Plasma installiert"
  ["  Installing GNOME..."]="  GNOME wird installiert..."
  ["Some GNOME packages failed to install"]="Einige GNOME-Pakete ließen sich nicht installieren"
  ["GNOME installed (session only — SynapseOS apps, not GNOME's)"]="GNOME installiert (nur die Sitzung — SynapseOS-Programme, nicht die von GNOME)"
  ["  Installing greetd (login screen) + desktop extras..."]="  greetd (Anmeldebildschirm) + Desktop-Zubehör werden installiert..."
  ["greetd failed to install — boot falls back to getty login"]="greetd ließ sich nicht installieren — der Start fällt auf die getty-Anmeldung zurück"
  ["SynapseUI selected (included)"]="SynapseUI gewählt (enthalten)"
  ["Installing Wine"]="Wine wird installiert"
  ["Wine installed"]="Wine installiert"
  ["wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."]="wine ließ sich nicht installieren — Windows-.exe/.msi laufen nicht.
  Später mit 'sudo pacman -S wine wine-mono' nachinstallieren."
  ["Configuring Video Driver"]="Grafiktreiber wird eingerichtet"
  ["  Virtual machine — installing mesa (synui uses pixman here)..."]="  Virtuelle Maschine — mesa wird installiert (synui nutzt hier pixman)..."
  ["mesa failed to install"]="mesa ließ sich nicht installieren"
  ["NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"]="Die NVIDIA-Treiberinstallation ist fehlgeschlagen — das System würde mit
  nouveau starten und synuis Renderer käme nie hoch"
  ["NVIDIA sleep services enabled (VRAM save/restore)"]="NVIDIA-Sleep-Dienste aktiviert (VRAM sichern/wiederherstellen)"
  ["Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"]="nvidia-{suspend,resume,hibernate} konnten nicht aktiviert werden — der
  Ruhezustand kann schwarz bleiben, falls NVreg_PreserveVideoMemoryAllocations später gesetzt wird"
  ["  synapd can run inference on this GPU instead of the CPU.
  This downloads the CUDA runtime (~4.7 GiB installed)."]="  synapd kann die Inferenz auf dieser GPU statt auf der CPU rechnen.
  Dabei wird die CUDA-Laufzeit geladen (~4,7 GiB installiert)."
  ["Enable GPU inference? [Y/n]:"]="GPU-Inferenz aktivieren? [Y/n]:"
  ["Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda"]="Es bleibt bei der CPU-Inferenz. Später umstellen mit:
  sudo pacman -S synapse-llama-cuda"
  ["  Installing synapse-llama-cuda (this takes a while)..."]="  synapse-llama-cuda wird installiert (das dauert eine Weile)..."
  ["This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"]="Diese ISO enthält keinen GPU-Build von llama, synapd rechnet also auf der CPU,
  trotz der NVIDIA-Karte. (Die ISO muss auf einem Host mit CUDA-Toolkit gebaut
  werden, damit es synapse-llama-cuda gibt.)"
  ["Video driver install failed — synui may fall back to software rendering"]="Die Grafiktreiberinstallation ist fehlgeschlagen — synui fällt womöglich auf Software-Rendering zurück"
  ["Video drivers installed"]="Grafiktreiber installiert"
  ["  Enabling GPU inference (synapse-llama-vulkan)..."]="  GPU-Inferenz wird aktiviert (synapse-llama-vulkan)..."
  ["This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"]="Diese ISO enthält keinen Vulkan-Build von llama, synapd rechnet also auf der CPU,
  trotz der AMD-/Intel-GPU. (Die ISO auf einem Host mit 'shaderc' +
  vulkan-headers bauen, damit es synapse-llama-vulkan gibt.)"
  ["Installing Steam and the game stack"]="Steam und der Spiele-Stack werden installiert"
  ["  Installing steam and the 32-bit runtime libraries..."]="  steam und die 32-Bit-Laufzeitbibliotheken werden installiert..."
  ["Steam installed (native multilib package)"]="Steam installiert (natives multilib-Paket)"
  ["steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."]="steam ließ sich nicht installieren. Das System ist ansonsten vollständig —
  später mit 'sudo pacman -S steam' nachinstallieren ([multilib] ist in
  /etc/pacman.conf bereits aktiviert)."
  ["  Installing the game stack (overlay, governor, micro-compositor)..."]="  Der Spiele-Stack wird installiert (Overlay, Governor, Mikro-Compositor)..."
  ["Game stack installed (mangohud, gamemode, gamescope)"]="Spiele-Stack installiert (mangohud, gamemode, gamescope)"
  ["The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"]="Der Spiele-Stack ließ sich nicht installieren. Steam funktioniert weiter; das
  FPS-Overlay, der CPU-/GPU-Governor und 'synui-game-run --gamescope' nicht.
  Später nachinstallieren mit:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
  ["Installing CachyOS Proton"]="CachyOS-Proton wird installiert"
  ["  Fetching the CachyOS keyring and mirrorlist..."]="  Schlüsselbund und Spiegelliste von CachyOS werden geholt..."
  ["  Trusting the CachyOS master key..."]="  Dem CachyOS-Hauptschlüssel wird vertraut..."
  ["Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"]="Der CachyOS-Hauptschlüssel konnte nicht von keyserver.ubuntu.com geholt werden.
  Ohne ihn lässt sich der signierte Schlüsselbund nicht installieren, CachyOS-Proton
  wird also übersprungen. Später hinzufügen mit:  synpkg cachyos enable-repo"
  ["  Master key pinned as expected — trusting it..."]="  Hauptschlüssel wie erwartet — es wird ihm vertraut..."
  ["[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."]="[cachyos] wurde hinzugefügt, führt aber keine Pakete — es wird wieder
  entfernt, damit es kein späteres Upgrade blockiert."
  ["The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."]="Der CachyOS-Schlüsselbund enthält nicht den erwarteten Hauptschlüssel.
  Es wird ihm nicht vertraut — die Paketquelle wurde NICHT hinzugefügt."
  ["  Installing proton-cachyos-slr (~340 MB download)..."]="  proton-cachyos-slr wird installiert (~340 MB Download)..."
  ["CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."]="CachyOS-Proton installiert — in Steam pro Spiel unter Eigenschaften →
  Kompatibilität auswählen, dort als 'proton-cachyos-… (steam linux runtime)'.
  Steam sucht nur beim Start danach, also Steam neu starten, falls es schon läuft."
  ["proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"]="proton-cachyos-slr ließ sich nicht installieren. Steam und Valves eigenes
  Proton sind nicht betroffen. Später nachinstallieren mit:
  sudo pacman -S proton-cachyos-slr"
  ["The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"]="Die Paketquelle [cachyos] ließ sich nicht aktivieren, CachyOS-Proton wurde
  daher übersprungen. Steam funktioniert weiter mit Valves Proton. Später hinzufügen:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
  ["Enabling BlackArch"]="BlackArch wird aktiviert"
  ["  Fetching the BlackArch bootstrap..."]="  Der BlackArch-Bootstrap wird geholt..."
  ["  Master key pinned as expected — running bootstrap..."]="  Hauptschlüssel wie erwartet — der Bootstrap wird ausgeführt..."
  ["blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."]="blackarch-keyring wurde nicht installiert — Schlüsselwechsel
  erreichen diesen Rechner nicht. Beheben mit 'sudo pacman -S blackarch-keyring'."
  ["The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."]="Die geladene strap.sh legt nicht den erwarteten Hauptschlüssel von BlackArch
  fest. Sie wird nicht ausgeführt — die Paketquelle wurde NICHT hinzugefügt."
  ["BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."]="BlackArch wurde nicht aktiviert. Das System ist ansonsten vollständig;
  später hinzufügen mit 'sudo syn arsenal --enable-repo'."
  ["Installing software"]="Software wird installiert"
  ["That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."]="Diese Transaktion ist fehlgeschlagen — jedes Paket wird einzeln erneut
  versucht, damit die funktionierenden ankommen und das defekte benannt wird."
  ["Software installed"]="Software installiert"
  ["Installing Flatpak apps"]="Flatpak-Programme werden installiert"
  ["flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."]="flatpak ließ sich nicht installieren — die Flatpak-Programme werden
  übersprungen. Sonst ist nichts betroffen."
  ["Could not add the flathub remote"]="Die flathub-Quelle konnte nicht hinzugefügt werden"
  ["Flatpak apps installed"]="Flatpak-Programme installiert"
  ["Configuring System"]="System wird eingerichtet"
  ["  fstab generated"]="  fstab erzeugt"
  ["Swap recorded in fstab"]="Swap in der fstab eingetragen"
  ["zram configured (compressed swap, half of RAM up to 8 GiB)"]="zram eingerichtet (komprimierter Swap, halbes RAM bis 8 GiB)"
  ["zram-generator is not installed in the target — no compressed swap"]="zram-generator ist im Zielsystem nicht installiert — kein komprimierter Swap"
  ["  Hostname: synapse"]="  Rechnername: synapse"
  ["Step 7 — Language & Region"]="Schritt 7 — Sprache & Region"
  ["   0) Other — enter a locale by hand"]="   0) Andere — eine Locale von Hand eingeben"
  ["Locale (e.g. sv_SE.UTF-8):"]="Locale (z. B. sv_SE.UTF-8):"
  ["Console keymap (e.g. sv-latin1):"]="Konsolen-Tastaturbelegung (z. B. sv-latin1):"
  ["Step 8 — Timezone"]="Schritt 8 — Zeitzone"
  ["   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"]="   0) Andere — einen beliebigen tzdata-Namen eingeben (z. B. Europe/Lisbon)"
  ["tzdata name (Region/City):"]="tzdata-Name (Region/Stadt):"
  ["  Did you mean:"]="  Meinten Sie:"
  ["  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"]="  Wählen Sie eine Nummer aus der Liste, oder siehe: ls /mnt/usr/share/zoneinfo"
  ["  os-release: copied from live system"]="  os-release: vom Live-System übernommen"
  ["  issue: copied from live system"]="  issue: vom Live-System übernommen"
  ["Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"]="Das Zieldateisystem ist nicht mehr beschreibbar (Plattenfehler? 'dmesg' prüfen) — Abbruch"
  ["sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"]="Das sudoers-Regelwerk ist nach dem Schreiben der Drop-ins ungültig — ein System, das kein sudo kann, wird nicht ausgeliefert"
  ["Could not relax pam_faillock in /etc/pam.d/system-auth (a tty-less sudo could still lock the account until reboot)."]="pam_faillock in /etc/pam.d/system-auth konnte nicht gelockert werden (ein sudo ohne tty könnte das Konto bis zum Neustart sperren)."
  ["could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"]="/var/lib/synapse-src konnte nicht vorab angelegt werden — der Updater fragt beim ersten Lauf nach einem Passwort"
  ["  Desktop: KDE Plasma (SDDM login screen)"]="  Desktop: KDE Plasma (SDDM-Anmeldebildschirm)"
  ["  GDM: SynapseOS logo on the login screen"]="  GDM: SynapseOS-Logo auf dem Anmeldebildschirm"
  ["  Desktop: GNOME (GDM login screen)"]="  Desktop: GNOME (GDM-Anmeldebildschirm)"
  ["  Desktop: TTY only"]="  Desktop: nur TTY"
  ["  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"]="  Desktop: SynapseUI (synui-Greeter — die Anmeldung spiegelt den Sperrbildschirm)"
  ["  motd: written for this installation"]="  motd: für diese Installation geschrieben"
  ["  note: syn-rgb.path is not installed; RGB lights stay off"]="  Hinweis: syn-rgb.path ist nicht installiert; die RGB-Beleuchtung bleibt aus"
  ["AI model"]="KI-Modell"
  ["  AI model skipped — install one later with: syn model download"]="  KI-Modell übersprungen — später eines installieren mit: syn model download"
  ["AI model installed"]="KI-Modell installiert"
  ["  the install, and everything else on the disk is already done."]="  der Installation, und alles andere auf der Platte ist bereits fertig."
  ["syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."]="syn-model ist nicht im Zielsystem, es wurde also kein Modell geladen.
  Es gehört zum Kernbestand; wurde es abgewählt, bleibt die KI untätig."
  ["Configuring Nix"]="Nix wird eingerichtet"
  ["Nix configured — /etc/synapseos/nix"]="Nix eingerichtet — /etc/synapseos/nix"
  ["  That is the download — a few hundred MB before any packages
  you add to home.nix. 'syn nix edit' opens it."]="  Das ist der Download — ein paar hundert MB, noch vor allen Paketen,
  die Sie in home.nix aufnehmen. 'syn nix edit' öffnet die Datei."
  ["nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."]="nix ist installiert, aber das Paket 'syn' ist nicht im Zielsystem, der
  Konfigurator wurde daher nicht eingerichtet. Nix selbst funktioniert;
  die Schicht /etc/synapseos/nix braucht 'syn'."
  ["nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."]="nix ließ sich nicht installieren — die deklarative Schicht steht nicht zur
  Verfügung. Später mit 'sudo pacman -S nix && sudo syn nix init' nachholen."
  ["  Generating initramfs..."]="  Initramfs wird erzeugt..."
  ["mkinitcpio failed — the installed system would not boot"]="mkinitcpio ist fehlgeschlagen — das installierte System würde nicht starten"
  ["initramfs missing after mkinitcpio — the installed system would not boot"]="Initramfs fehlt nach mkinitcpio — das installierte System würde nicht starten"
  ["System configured"]="System eingerichtet"
  ["Installing Bootloader"]="Bootloader wird installiert"
  ["grub-install (UEFI) failed"]="grub-install (UEFI) ist fehlgeschlagen"
  ["grub-install (BIOS) failed"]="grub-install (BIOS) ist fehlgeschlagen"
  ["  Generating GRUB config..."]="  GRUB-Konfiguration wird erzeugt..."
  ["grub-mkconfig failed"]="grub-mkconfig ist fehlgeschlagen"
  ["grub.cfg missing after install"]="grub.cfg fehlt nach der Installation"
  ["grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"]="grub.cfg enthält ein GRUB-Passwort — sie bleibt nur für root lesbar, die Einstellungs-App kann daher nichts über Booteinträge berichten"
  ["  Installing systemd-boot..."]="  systemd-boot wird installiert..."
  ["bootctl install failed"]="bootctl install ist fehlgeschlagen"
  ["  Registering systemd-boot with the firmware..."]="  systemd-boot wird bei der Firmware angemeldet..."
  ["efibootmgr entry not created — the removable-media path still applies"]="efibootmgr-Eintrag nicht angelegt — es gilt weiterhin der Wechselmedien-Pfad"
  ["could not read the root filesystem UUID"]="die UUID des Wurzeldateisystems konnte nicht gelesen werden"
  ["vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"]="vmlinuz-linux liegt nicht auf der ESP — systemd-boot fände nichts zum Starten"
  ["initramfs is not on the ESP — systemd-boot would find nothing to boot"]="Die Initramfs liegt nicht auf der ESP — systemd-boot fände nichts zum Starten"
  ["systemd-boot did not install its EFI binary"]="systemd-boot hat seine EFI-Datei nicht installiert"
  ["  Installing limine..."]="  limine wird installiert..."
  ["could not copy limine's EFI binary to the ESP"]="limines EFI-Datei konnte nicht auf die ESP kopiert werden"
  ["limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"]="limine-mkinitcpio-hook nicht installiert — ein später installierter Kernel bekommt KEINEN Booteintrag"
  ["vmlinuz-linux is not on the ESP — limine would find nothing to boot"]="vmlinuz-linux liegt nicht auf der ESP — limine fände nichts zum Starten"
  ["limine's EFI binary is not on the ESP"]="limines EFI-Datei liegt nicht auf der ESP"
  ["limine.conf has no kernel entry"]="limine.conf hat keinen Kernel-Eintrag"
  ["  Verifying the encrypted boot path..."]="  Der verschlüsselte Startpfad wird geprüft..."
  ["/boot is not a separate mount — an encrypted root needs a plain /boot"]="/boot ist kein eigener Mount — eine verschlüsselte Wurzel braucht ein unverschlüsseltes /boot"
  ["/boot is missing from fstab — it would not be mounted after boot"]="/boot fehlt in der fstab — es würde nach dem Start nicht eingehängt"
  ["Encrypted boot path verified"]="Verschlüsselter Startpfad geprüft"
  ["Configuring snapshots"]="Snapshots werden eingerichtet"
  ["snapper's config template is missing — snapshots cannot be configured"]="Die Konfigurationsvorlage von snapper fehlt — Snapshots lassen sich nicht einrichten"
  ["could not write /etc/snapper/configs/root"]="/etc/snapper/configs/root konnte nicht geschrieben werden"
  ["snapper does not see the 'root' config — snapshots would never be taken"]="snapper sieht die Konfiguration 'root' nicht — es würden nie Snapshots angelegt"
  ["snapper's root config was not tuned — timeline snapshots would fill the disk"]="Die root-Konfiguration von snapper wurde nicht angepasst — zeitgesteuerte Snapshots würden die Platte füllen"
  ["could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"]="grub-btrfsd konnte nicht aktiviert werden — Snapshots erscheinen nicht automatisch im Bootmenü"
  ["Snapshots enabled (snapper + snap-pac, bootable from GRUB)"]="Snapshots aktiviert (snapper + snap-pac, aus GRUB startbar)"
  ["could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"]="limine-snapper-sync konnte nicht aktiviert werden — Snapshots erreichen das Bootmenü nicht automatisch"
  ["could not take the post-install snapshot"]="Der Snapshot nach der Installation konnte nicht angelegt werden"
  ["could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"]="Der Snapshot-Abgleich beim ersten Start konnte nicht aktiviert werden — das Menü füllt sich stattdessen nach dem ersten Upgrade"
  ["Snapshots enabled (snapper + snap-pac, bootable from limine)"]="Snapshots aktiviert (snapper + snap-pac, aus limine startbar)"
  ["Bootloader installed"]="Bootloader installiert"
  ["  The root account is locked (no root login / su).
  Note: 3 wrong password attempts lock the account for 10 minutes."]="  Das root-Konto ist gesperrt (keine root-Anmeldung / kein su).
  Hinweis: nach 3 falschen Passworteingaben ist das Konto 10 Minuten gesperrt."
  ["You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."]="Die Passphrase der Verschlüsselung wird bei jedem Start abgefragt,
  VOR dem Anmeldebildschirm. Es gibt keinen Weg, sie wiederherzustellen."
  ["    syn-crypt status                    is this disk encrypted, and how
    sudo syn-crypt change-key           replace the passphrase
    sudo syn-crypt add-key              add a second one
    sudo syn-crypt backup-header FILE   save the LUKS header"]="    syn-crypt status                    ist diese Platte verschlüsselt, und wie
    sudo syn-crypt change-key           die Passphrase ersetzen
    sudo syn-crypt add-key              eine zweite hinzufügen
    sudo syn-crypt backup-header DATEI  den LUKS-Header sichern"
  ["  means the data is unrecoverable even with the right passphrase."]="  bedeutet, dass die Daten selbst mit der richtigen Passphrase unrettbar sind."
  ["Remove installation media and press ENTER to reboot..."]="Installationsmedium entfernen und ENTER zum Neustart drücken..."
  ["Install SynapseOS     — right here, in this terminal"]="SynapseOS installieren  — hier, in diesem Terminal"
  ["Install graphically   — starts the desktop first"]="Grafisch installieren   — startet zuerst den Desktop"
  ["Try the live desktop  — look around; install later"]="Live-Desktop probieren  — umsehen; später installieren"
  ["Target:"]="Ziel:"
  ["ALONGSIDE"]="DANEBEN"
  ["ERASE"]="LÖSCHEN"
  ["ADVANCED"]="ERWEITERT"
  ["Encrypt this installation?"]="Diese Installation verschlüsseln?"
  ["There is no recovery."]="Es gibt keine Wiederherstellung."
  ["Root filesystem"]="Wurzeldateisystem"
  ["ext4   — the default. Boring, proven, repairable by anything."]="ext4   — die Vorgabe. Langweilig, bewährt, von allem reparierbar."
  ["btrfs  — snapshots + zstd compression. Roll back a bad update
                    from the boot menu. Uses more RAM and more CPU."]="btrfs  — Snapshots + zstd-Kompression. Ein schlechtes Update aus dem
                    Bootmenü zurückrollen. Braucht mehr RAM und mehr CPU."
  ["xfs    — fast on large files. No snapshots, and it cannot be
                    SHRUNK once created."]="xfs    — schnell bei großen Dateien. Keine Snapshots, und einmal
                    angelegt NICHT verkleinerbar."
  ["f2fs   — built for flash. Good on SD cards and cheap SSDs;
                    unusual enough that fewer rescue tools know it."]="f2fs   — für Flash gebaut. Gut auf SD-Karten und günstigen SSDs;
                    ungewöhnlich genug, dass wenige Rettungswerkzeuge es kennen."
  ["Bootloader"]="Bootloader"
  ["GRUB          — the default. Detects other operating systems,
                          and the only one here that can boot a btrfs
                          snapshot."]="GRUB          — die Vorgabe. Erkennt andere Betriebssysteme und ist der
                          einzige hier, der einen btrfs-Snapshot starten
                          kann."
  ["systemd-boot  — minimal. No OS detection, no snapshot menu."]="systemd-boot  — minimal. Keine OS-Erkennung, kein Snapshot-Menü."
  ["limine        — modern and fast, and it CAN boot snapshots."]="limine        — modern und schnell, und er KANN Snapshots starten."
  ["Automatic snapshots?"]="Automatische Snapshots?"
  ["Review the plan — nothing has been written yet:"]="Prüfen Sie den Plan — es wurde noch nichts geschrieben:"
  ["nothing else is touched"]="sonst wird nichts angerührt"
  ["not"]="nicht"
  ["Partition"]="Partitionieren Sie"
  ["now."]="jetzt."
  ["Partitions now on"]="Partitionen jetzt auf"
  ["These partitions will be FORMATTED"]="Diese Partitionen werden FORMATIERT"
  ["Full      — Standard + Steam + Nix + more software"]="Voll      — Standard + Steam + Nix + mehr Software"
  ["Standard  — the SynapseOS suite, Firefox, AI model,"]="Standard  — die SynapseOS-Suite, Firefox, KI-Modell,"
  ["Minimal   — core daemons only: none of the above"]="Minimal   — nur die Kerndienste: nichts vom Obigen"
  ["Custom    — tick every package yourself, ours and"]="Eigene    — jedes Paket selbst ankreuzen, unsere und"
  ["Which AI model should this machine run?"]="Welches KI-Modell soll dieser Rechner betreiben?"
  ["Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"]="Mistral 7B Instruct   ~4,1 GB   empfohlen — darauf ist SynapseOS abgestimmt"
  ["Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"]="Phi-3 Mini 4K         ~2,2 GB   halb so groß, und merklich schwächer"
  ["Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"]="Qwen2 0.5B            ~0,4 GB   passt überall, antwortet auch danach"
  ["None                            skip it — nothing else changes"]="Keines                          überspringen — sonst ändert sich nichts"
  ["Installing:"]="Installiert wird:"
  ["SynapseUI  — AI-native Wayland compositor  (default)"]="SynapseUI  — KI-nativer Wayland-Compositor  (Vorgabe)"
  ["SynapseUI  — NOT AVAILABLE: synui was not selected"]="SynapseUI  — NICHT VERFÜGBAR: synui wurde nicht gewählt"
  ["KDE Plasma — Full-featured Wayland desktop"]="KDE Plasma — vollausgestatteter Wayland-Desktop"
  ["GNOME      — Clean, modern Wayland desktop"]="GNOME      — klarer, moderner Wayland-Desktop"
  ["TTY only   — No GUI (headless/server)"]="Nur TTY    — keine Oberfläche (headless/Server)"
  ["Disk:"]="Platte:"
  ["Boot:"]="Boot:"
  ["Encrypted:"]="Verschlüsselt:"
  ["Desktop:"]="Desktop:"
  ["User:"]="Benutzer:"
  ["Hostname:"]="Rechnername:"
  ["Back up the header to another machine."]="Sichern Sie den Header auf einem anderen Rechner."
  ["%s is mounted — unmount it first\\n"]="%s ist eingehängt — zuerst aushängen
"
  ["%s is %s MiB — %s needs at least %s MiB\\n"]="%s hat %s MiB — %s braucht mindestens %s MiB
"
  ["  Generating %s (a few seconds)...\\n"]="  %s wird erzeugt (ein paar Sekunden)...
"
  ["Language: %s  (%s, keyboard %s)\\n"]="Sprache: %s  (%s, Tastatur %s)
"
  ["  This disk already holds %s partition(s), an EFI System\\n  Partition (%s), and %s GiB of free space.\\n"]="  Diese Platte enthält bereits %s Partition(en), eine EFI-System-
  Partition (%s) und %s GiB freien Speicher.
"
  ["    1) Install %s — use the free space, keep everything else\\n"]="    1) %s installieren — den freien Speicher nutzen, alles andere behalten
"
  ["    2) %s the whole disk — delete every partition and all data\\n"]="    2) Die ganze Platte %s — jede Partition und alle Daten löschen
"
  ["    3) %s — partition this disk yourself, then pick the partitions\\n"]="    3) %s — diese Platte selbst partitionieren, dann die Partitionen wählen
"
  ["    1) %s the whole disk — delete every partition and all data  (default)\\n"]="    1) Die ganze Platte %s — jede Partition und alle Daten löschen  (Vorgabe)
"
  ["    2) %s — partition this disk yourself, then pick the partitions\\n"]="    2) %s — diese Platte selbst partitionieren, dann die Partitionen wählen
"
  ["  %s If you forget the passphrase the data is\\n  gone — no password reset, no support call, nothing.\\n"]="  %s Wenn Sie die Passphrase vergessen, sind die Daten
  weg — kein Zurücksetzen, kein Support-Anruf, nichts.
"
  ["  snapper takes a snapshot before and after every pacman\\n  transaction, and %s grows a menu to boot any of them. A\\n  bad upgrade becomes a reboot instead of a rescue USB.\\n"]="  snapper legt vor und nach jeder pacman-Transaktion einen Snapshot an,
  und %s bekommt ein Menü, um jeden davon zu starten. Ein
  schlechtes Upgrade wird so zum Neustart statt zum Rettungs-USB.
"
  ["    Disk          : %s\\n"]="    Platte        : %s
"
  ["    Firmware      : %s\\n"]="    Firmware      : %s
"
  ["    Filesystem    : %s\\n"]="    Dateisystem   : %s
"
  ["    Bootloader    : %s\\n"]="    Bootloader    : %s
"
  ["    Separate /boot: %s\\n"]="    Eigenes /boot: %s
"
  ["    Encryption    : %s\\n"]="    Verschlüsselung: %s
"
  ["    Snapshots     : %s\\n"]="    Snapshots     : %s
"
  ["  Encrypting %s (LUKS2)...\\n"]="  %s wird verschlüsselt (LUKS2)...
"
  ["  Formatting root partition (%s)...\\n"]="  Root-Partition wird formatiert (%s)...
"
  ["    • KEEP   all %s existing partition(s), including Windows\\n"]="    • BEHALTEN alle %s vorhandenen Partition(en), Windows eingeschlossen
"
  ["    • REUSE  %s as the EFI partition (mounted, %s formatted)\\n"]="    • WEITERNUTZEN %s als EFI-Partition (eingehängt, %s formatiert)
"
  ["    • CREATE a new ext4 root of ~%s GiB in the free space\\n"]="    • ANLEGEN einer neuen ext4-Wurzel von ~%s GiB im freien Speicher
"
  ["  Creating root partition in free space (%sMiB–%sMiB)...\\n"]="  Root-Partition wird im freien Speicher angelegt (%sMiB–%sMiB)...
"
  ["  Formatting new root (%s, ext4)...\\n"]="  Neue Wurzel wird formatiert (%s, ext4)...
"
  ["  %s %s %s The installer will re-read the table when you exit.\\n"]="  %s %s %s Der Installer liest die Tabelle beim Beenden neu ein.
"
  ["    • a root partition, at least %s GiB\\n"]="    • eine Root-Partition, mindestens %s GiB
"
  ["    • a separate /boot of ~1 GiB — %s with this layout cannot read the root\\n"]="    • ein eigenes /boot von ~1 GiB — %s kann bei diesem Aufbau die Wurzel nicht lesen
"
  ["  Starting %s on %s — write your changes before quitting.\\n"]="  %s wird auf %s gestartet — Änderungen vor dem Beenden schreiben.
"
  ["    %s is already swap — another system may resume from it.\\n"]="    %s ist bereits Swap — ein anderes System setzt womöglich daraus fort.
"
  ["  Everything else on %s is left untouched.\\n"]="  Alles andere auf %s bleibt unberührt.
"
  ["  Making swap on %s...\\n"]="  Swap wird auf %s angelegt...
"
  ["  Formatting EFI partition (%s MiB)...\\n"]="  EFI-Partition wird formatiert (%s MiB)...
"
  ["  NVIDIA GPU detected — installing %s (builds the module, takes a while)...\\n"]="  NVIDIA-GPU erkannt — %s wird installiert (baut das Modul, dauert eine Weile)...
"
  ["  Installing video stack: %s %s...\\n"]="  Grafik-Stack wird installiert: %s %s...
"
  ["  [cachyos] enabled (%s packages available)\\n"]="  [cachyos] aktiviert (%s Pakete verfügbar)
"
  ["  Language: %s  (chosen at boot)\\n"]="  Sprache: %s  (beim Start gewählt)
"
  ["  Locale:   %s   Keyboard: %s (console) / %s (desktop)\\n"]="  Locale:   %s   Tastatur: %s (Konsole) / %s (Desktop)
"
  ["  Installing fonts (%s)...\\n"]="  Schriften werden installiert (%s)...
"
  ["  Downloading the AI model (%s) — this is the long part of\\n"]="  Das KI-Modell wird geladen (%s) — das ist der lange Teil
"
  ["  Nothing is built yet. As %s, after the first boot:\\n"]="  Es ist noch nichts gebaut. Als %s, nach dem ersten Start:
"
  ["  Adding the %s hook to mkinitcpio...\\n"]="  Der %s-Hook wird zu mkinitcpio hinzugefügt...
"
  ["  Installing GRUB (%s)...\\n"]="  GRUB wird installiert (%s)...
"
  ["yes — LUKS2 on %s"]="ja — LUKS2 auf %s"
  ["  Admin: use %s with your user password.\\n"]="  Verwaltung: %s mit Ihrem Benutzerpasswort verwenden.
"
  ["  Manage it later with %s:\\n"]="  Später verwalten mit %s:
"
  ["  %s A damaged LUKS header\\n"]="  %s Ein beschädigter LUKS-Header
"
  ["%s is on the live/boot device — that is the installer's own media\\n"]="%s liegt auf dem Live-/Boot-Gerät — das ist das eigene Medium des Installers
"
  ["    %s is already FAT — it may hold another OS's bootloader.\\n"]="    %s ist bereits FAT — es könnte den Bootloader eines anderen Systems enthalten.
"
  ["  Creating user '%s'...\\n"]="  Benutzer '%s' wird angelegt...
"
  ["  User '%s' created (uid=%s)\\n"]="  Benutzer '%s' angelegt (uid=%s)
"
  ["  Log in as '%s' after reboot.\\n"]="  Melden Sie sich nach dem Neustart als '%s' an.
"
  ["  Type '%s' to get started.\\n"]="  Tippen Sie '%s', um loszulegen.
"
  ["Install SynapseOS"]="SynapseOS installieren"
  ["SynapseOS packages"]="SynapseOS-Pakete"
  ["Everything the system is made of. What you cannot drop is what something else you kept depends on — those are turned back on and named before anything is installed."]="Alles, woraus das System besteht. Was sich nicht abwählen lässt, wird von etwas anderem gebraucht, das Sie behalten haben — das wird wieder eingeschaltet und benannt, bevor irgendetwas installiert wird."
  ["SYNAPSE UI — the Wayland desktop"]="SYNAPSE UI — der Wayland-Desktop"
  ["synapd — the local AI daemon"]="synapd — der lokale KI-Dienst"
  ["synsh — the AI-native shell"]="synsh — die KI-native Shell"
  ["synguard + kernel module"]="synguard + Kernelmodul"
  ["synnet — network policy"]="synnet — Netzwerkregeln"
  ["Software — the package manager"]="Software — die Paketverwaltung"
  ["Files — the file manager"]="Dateien — der Dateimanager"
  ["Terminal (synui depends on it)"]="Terminal (synui braucht es)"
  ["Settings"]="Einstellungen"
  ["Disks"]="Laufwerke"
  ["Editor"]="Editor"
  ["Calendar"]="Kalender"
  ["File Vault — a locked folder"]="Datentresor — ein abgeschlossener Ordner"
  ["Disk Cleanup — caches, and secure delete"]="Aufräumen — Caches und sicheres Löschen"
  ["syn-update — how fixes arrive"]="syn-update — so kommen Korrekturen an"
  ["syn — the top-level CLI"]="syn — die oberste Kommandozeile"
  ["syn-model — fetch AI models"]="syn-model — KI-Modelle holen"
  ["syn-confine — the sandbox"]="syn-confine — die Sandbox"
  ["fetch — the About OS readout"]="fetch — die Systemübersicht"
  ["Arcade — overlay, pads, big screen"]="Arcade — Overlay, Gamepads, großer Bildschirm"
  ["cliamp — the music player"]="cliamp — der Musikspieler"
  ["Player — playlists, shuffle and history, on mpv"]="Player — Playlists, Zufall und Verlauf, auf mpv"
  ["Studio — photo darkroom and video"]="Studio — Fotolabor und Video"
  ["GeForce NOW — cloud gaming in a browser"]="GeForce NOW — Cloud-Gaming im Browser"
  ["Remote desktop — reach this machine from another"]="Fernzugriff — diesen Rechner von einem anderen aus erreichen"
  ["Arsenal — BlackArch browser"]="Arsenal — BlackArch-Browser"
  ["Chibi — voice companion"]="Chibi — Sprachbegleiter"
  ["Vibe — AI coding assistant"]="Vibe — KI-Programmierassistent"
  ["Animated wallpapers (~317 MB)"]="Animierte Hintergründe (~317 MB)"
  ["Nexus Chat (pulls in Firefox)"]="Nexus Chat (zieht Firefox mit)"
  ["TEPRIS (pulls in Firefox)"]="TEPRIS (zieht Firefox mit)"
  ["Web and communication"]="Web und Kommunikation"
  ["None of this is ours; every name is in the Arch repositories. Firefox is on by default because an installed SynapseOS used to arrive with no browser at all."]="Nichts davon stammt von uns; jeder Name steht in den Arch-Repositories. Firefox ist voreingestellt, weil ein installiertes SynapseOS früher ganz ohne Browser ankam."
  ["Thunderbird — mail"]="Thunderbird — E-Mail"
  ["KeePassXC — passwords"]="KeePassXC — Passwörter"
  ["Syncthing — file sync"]="Syncthing — Dateiabgleich"
  ["LocalSend — send to phone (Flatpak)"]="LocalSend — ans Handy senden (Flatpak)"
  ["Audio and video"]="Audio und Video"
  ["Office and graphics"]="Büro und Grafik"
  ["Development and admin"]="Entwicklung und Administration"
  ["VS Code (OSS build)"]="VS Code (OSS-Build)"
  ["7zip + unrar"]="7zip + unrar"
  ["Games, launchers and helpers"]="Spiele, Launcher und Helfer"
  ["Steam is in the options below rather than here: it is the only one that turns on a second architecture and a third repository."]="Steam steht unten bei den Optionen statt hier: es ist das einzige, das eine zweite Architektur und ein drittes Repository einschaltet."
  ["Prism — Minecraft"]="Prism — Minecraft"
  ["Dolphin — GameCube/Wii"]="Dolphin — GameCube/Wii"
  ["PPSSPP — PSP"]="PPSSPP — PSP"
  ["Space Cadet Pinball (Flatpak)"]="Space Cadet Pinball (Flatpak)"
  ["GOverlay — MangoHud"]="GOverlay — MangoHud"
  ["AntiMicroX — pad remap"]="AntiMicroX — Gamepad-Belegung"
  ["Welcome"]="Willkommen"
  ["Disk"]="Festplatte"
  ["Software"]="Software"
  ["Account"]="Konto"
  ["Region"]="Region"
  ["Summary"]="Übersicht"
  ["Install"]="Installation"
  ["the installer's own media"]="das Medium des Installers"
  ["%1 GiB — SynapseOS needs at least %2 GiB"]="%1 GiB — SynapseOS braucht mindestens %2 GiB"
  ["No connection. SynapseOS downloads the base system while it installs, so this needs a working network before it can start."]="Keine Verbindung. SynapseOS lädt das Basissystem während der Installation herunter, daher braucht es vorher ein funktionierendes Netzwerk."
  ["Choose a disk to install to."]="Wählen Sie eine Festplatte für die Installation."
  ["The encryption passphrase needs at least 8 characters."]="Die Passphrase braucht mindestens 8 Zeichen."
  ["With neither the package manager nor the desktop, this install has no way to add either one back. Keep at least one."]="Ohne Paketverwaltung und ohne Desktop hat diese Installation keine Möglichkeit, eines von beiden nachzuholen. Behalten Sie mindestens eines."
  ["A username is lower-case letters, digits, - and _, and cannot start with a digit."]="Ein Benutzername besteht aus Kleinbuchstaben, Ziffern, - und _ und darf nicht mit einer Ziffer beginnen."
  ["Set a password for the account."]="Vergeben Sie ein Passwort für das Konto."
  ["The two passwords do not match."]="Die beiden Passwörter stimmen nicht überein."
  ["A locale is needed, e.g. en_US.UTF-8."]="Eine Locale wird gebraucht, z. B. de_DE.UTF-8."
  ["A timezone is needed, e.g. Europe/Lisbon."]="Eine Zeitzone wird gebraucht, z. B. Europe/Berlin."
  ["printing"]="Drucken"
  ["%1 repo"]="%1-Repo"
  ["Mode"]="Modus"
  ["Filesystem"]="Dateisystem"
  ["%1 on LUKS2"]="%1 auf LUKS2"
  ["%1 + snapshots"]="%1 + Snapshots"
  ["none"]="keine"
  ["Desktop"]="Desktop"
  ["Locale"]="Locale"
  ["%1   keys %2 / %3"]="%1   Tasten %2 / %3"
  ["Timezone"]="Zeitzone"
  ["%1 package(s) — WITHOUT %2"]="%1 Paket(e) — OHNE %2"
  ["%1 package(s)"]="%1 Paket(e)"
  ["Options"]="Optionen"
  ["Could not write the install profile."]="Das Installationsprofil konnte nicht geschrieben werden."
  ["Installation complete."]="Installation abgeschlossen."
  ["Installation failed — see the log."]="Installation fehlgeschlagen — siehe Protokoll."
  ["No network connection"]="Keine Netzwerkverbindung"
  ["The base system is downloaded while it installs, so this cannot start offline. Plug in a cable or join a network, then press Re-check — the answers on these pages are kept."]="Das Basissystem wird während der Installation heruntergeladen, offline kann sie also nicht starten. Stecken Sie ein Kabel ein oder verbinden Sie sich mit einem Netzwerk und drücken Sie Erneut prüfen — die Antworten auf diesen Seiten bleiben erhalten."
  ["Checking…"]="Wird geprüft…"
  ["Re-check"]="Erneut prüfen"
  ["Wi-Fi settings"]="WLAN-Einstellungen"
  ["This asks for a disk, an account and a few preferences, then hands the answers to the same installer the text version runs. Nothing is written to any disk until the last page, and that page says exactly what it is about to do."]="Hier werden eine Festplatte, ein Konto und ein paar Einstellungen abgefragt und dann an denselben Installer übergeben, den auch die Textversion ausführt. Bis zur letzten Seite wird auf keine Festplatte geschrieben, und diese Seite sagt genau, was gleich passiert."
  ["A disk is partitioned and formatted"]="Eine Festplatte wird partitioniert und formatiert"
  ["The base system and the SynapseOS packages are installed"]="Das Basissystem und die SynapseOS-Pakete werden installiert"
  ["An account and a desktop are set up"]="Ein Konto und ein Desktop werden eingerichtet"
  ["A bootloader is written"]="Ein Bootloader wird geschrieben"
  ["Partitioning an existing layout by hand is the text installer's ADVANCED mode — quit this and run \`syn-install\` in a terminal for that."]="Ein bestehendes Layout von Hand zu partitionieren ist der ADVANCED-Modus des Textinstallers — dafür dieses Fenster schließen und \`syn-install\` im Terminal ausführen."
  ["Where should SynapseOS go?"]="Wohin soll SynapseOS?"
  ["The installer's own media is listed and cannot be chosen."]="Das Medium des Installers wird angezeigt und kann nicht gewählt werden."
  ["No disks found."]="Keine Festplatten gefunden."
  ["Erase the disk"]="Festplatte löschen"
  ["every partition and all data"]="jede Partition und alle Daten"
  ["Install alongside"]="Daneben installieren"
  ["use free space, UEFI only"]="freien Platz nutzen, nur UEFI"
  ["Snapshots"]="Snapshots"
  ["btrfs + limine only"]="nur btrfs + limine"
  ["Encrypt the disk"]="Festplatte verschlüsseln"
  ["Passphrase"]="Passphrase"
  ["8 characters or more"]="8 Zeichen oder mehr"
  ["What should be installed?"]="Was soll installiert werden?"
  ["The SynapseOS core — the compositor, the daemons and the applications it is built on — is installed by every choice here."]="Der SynapseOS-Kern — der Compositor, die Dienste und die Anwendungen, auf denen er aufbaut — wird bei jeder Auswahl hier installiert."
  ["Full"]="Voll"
  ["Standard + Steam + Nix + more software"]="Standard + Steam + Nix + mehr Software"
  ["Standard"]="Standard"
  ["the SynapseOS suite, Firefox, AI model, Bluetooth, printing, Wine, phone"]="die SynapseOS-Suite, Firefox, KI-Modell, Bluetooth, Drucken, Wine, Handy"
  ["Minimal"]="Minimal"
  ["core daemons only — no apps, no software, no model"]="nur die Kerndienste — keine Apps, keine Software, kein Modell"
  ["Custom"]="Eigene Auswahl"
  ["tick every package yourself, ours and the ordinary software"]="jedes Paket selbst ankreuzen, unsere und die gewöhnliche Software"
  ["(required)"]="(erforderlich)"
  ["Not packages: a repository, an architecture or a service. Each is a decision with a consequence that does not fit on a checkbox above."]="Keine Pakete: ein Repository, eine Architektur oder ein Dienst. Jedes ist eine Entscheidung mit Folgen, die auf kein Kästchen oben passen."
  ["Printing (CUPS)"]="Drucken (CUPS)"
  ["Wine — run Windows .exe/.msi"]="Wine — Windows-.exe/.msi ausführen"
  ["KDE Connect — pair a phone"]="KDE Connect — ein Handy koppeln"
  ["Steam + game stack + Proton (~3.1 GB)"]="Steam + Spiele-Stack + Proton (~3,1 GB)"
  ["BlackArch repo — ~5000 tools, none installed"]="BlackArch-Repo — ~5000 Werkzeuge, keines installiert"
  ["Nix + Home Manager"]="Nix + Home Manager"
  ["syn-update is off: this machine will have no way to receive another SynapseOS package. Fixing that later means installing it by hand from the ISO, or reinstalling."]="syn-update ist aus: dieser Rechner kann kein weiteres SynapseOS-Paket mehr empfangen. Das später zu beheben heißt, es von Hand vom ISO zu installieren oder neu zu installieren."
  ["synui is off: this will not be a SynapseOS desktop. The Desktop page offers KDE, GNOME or no GUI."]="synui ist aus: das wird kein SynapseOS-Desktop. Die Desktop-Seite bietet KDE, GNOME oder gar keine Oberfläche."
  ["AI model — downloaded during the install"]="KI-Modell — wird während der Installation geladen"
  ["~4.1 GB — recommended"]="~4,1 GB — empfohlen"
  ["~2.2 GB — weaker"]="~2,2 GB — schwächer"
  ["~0.4 GB — much weaker"]="~0,4 GB — deutlich schwächer"
  ["None"]="Keines"
  ["AI stays inert"]="KI bleibt untätig"
  ["NVIDIA GPU inference"]="NVIDIA-GPU-Inferenz"
  ["the CUDA runtime, ~4.7 GiB"]="die CUDA-Laufzeit, ~4,7 GiB"
  ["Who is this machine for?"]="Für wen ist dieser Rechner?"
  ["Username"]="Benutzername"
  ["lower-case, no spaces"]="klein geschrieben, keine Leerzeichen"
  ["Full name (optional)"]="Vollständiger Name (optional)"
  ["Password"]="Passwort"
  ["Password again"]="Passwort wiederholen"
  ["They do not match"]="Sie stimmen nicht überein"
  ["the native compositor"]="der eigene Compositor"
  ["synui is not selected"]="synui ist nicht ausgewählt"
  ["headless"]="ohne Oberfläche"
  ["Language, keyboard and time"]="Sprache, Tastatur und Zeit"
  ["Pick a language and the other three follow it. The console keymap and the desktop layout are separate on purpose — Swedish is 'sv-latin1' to the console and 'se' to the desktop — so they can be changed on their own afterwards."]="Wählen Sie eine Sprache, die anderen drei folgen ihr. Konsolenbelegung und Desktop-Layout sind absichtlich getrennt — Schwedisch heißt auf der Konsole 'sv-latin1' und auf dem Desktop 'se' — damit beide später einzeln geändert werden können."
  ["Language"]="Sprache"
  ["sets the keyboard and the fonts too"]="setzt auch Tastatur und Schriften"
  ["typed by hand — fonts cover as much as possible"]="von Hand eingegeben — Schriften decken so viel wie möglich ab"
  ["Sets the locale, both keyboard names and the font pack. Any locale glibc has can be typed instead."]="Setzt die Locale, beide Tastaturnamen und das Schriftpaket. Stattdessen lässt sich jede Locale eingeben, die glibc kennt."
  ["The common zones first, then every name tzdata ships."]="Zuerst die gängigen Zonen, dann jeder Name, den tzdata mitbringt."
  ["Console keymap"]="Konsolenbelegung"
  ["loadkeys — the text console and the greeter"]="loadkeys — Textkonsole und Anmeldebildschirm"
  ["Every keymap this image can load. This one names a file loadkeys has to find, which is why it is not the same list as the desktop layout."]="Jede Belegung, die dieses Abbild laden kann. Sie benennt eine Datei, die loadkeys finden muss — deshalb ist es nicht dieselbe Liste wie beim Desktop-Layout."
  ["Desktop layout"]="Desktop-Layout"
  ["XKB — the compositor"]="XKB — der Compositor"
  ["Desktop keyboard layout"]="Desktop-Tastaturlayout"
  ["The layouts xkbcommon can compile. 'uk' is a console keymap and not a layout here — the layout is 'gb'."]="Die Layouts, die xkbcommon übersetzen kann. 'uk' ist eine Konsolenbelegung und hier kein Layout — das Layout heißt 'gb'."
  ["Read this back"]="Noch einmal lesen"
  ["Nothing has been written yet. The next button is the one that starts."]="Bisher wurde nichts geschrieben. Der nächste Knopf ist der, der beginnt."
  ["EVERY PARTITION ON %1 WILL BE DELETED"]="JEDE PARTITION AUF %1 WIRD GELÖSCHT"
  ["SynapseOS will be installed into the free space on %1"]="SynapseOS wird in den freien Platz auf %1 installiert"
  ["SynapseOS is installed"]="SynapseOS ist installiert"
  ["The install stopped"]="Die Installation wurde abgebrochen"
  ["Installing SynapseOS"]="SynapseOS wird installiert"
  ["Reboot and remove the installation media."]="Neu starten und das Installationsmedium entfernen."
  ["The log below is the whole story — the last lines say why."]="Das Protokoll unten erzählt alles — die letzten Zeilen sagen warum."
  ["This takes a while: the base system and the packages are downloaded, and an AI model is gigabytes on its own."]="Das dauert: Basissystem und Pakete werden heruntergeladen, und ein KI-Modell ist für sich schon mehrere Gigabyte groß."
  ["Back"]="Zurück"
  ["Next"]="Weiter"
  ["Reboot"]="Neu starten"
  ["Close"]="Schließen"
  ["type to filter, or type a name that is not listed"]="tippen zum Filtern, oder einen nicht gelisteten Namen eingeben"
  ["Nothing to list on this image — type the name instead."]="Auf diesem Abbild gibt es nichts zu listen — den Namen eingeben."
  ["Nothing matches — the row below uses what you typed."]="Nichts passt — die Zeile unten nimmt das Eingetippte."
  ["Use “%1” as typed"]="„%1“ so übernehmen"
)
