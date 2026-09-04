# Nederlands (nl) — syn-install's own words.
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
  ["render.nix is missing — the 'syn' package is not installed here."]="render.nix ontbreekt — het pakket 'syn' is hier niet geïnstalleerd."
  ["locale-gen failed. The live session stays in English; the install is
  unaffected, because it generates the locale inside the target."]="locale-gen is mislukt. De live-sessie blijft in het Engels; de installatie
  heeft er geen last van, want die maakt de locale in het doelsysteem aan."
  ["  The keyboard, the clock, the fonts and the shell all follow this.
  You can change any of it later."]="  Het toetsenbord, de klok, de lettertypen en de shell volgen hieruit.
  Dat kan later allemaal nog veranderd worden."
  ["Toggle [numbers, 'all', 'none', Enter = accept]:"]="Omschakelen [nummers, 'all', 'none', Enter = akkoord]:"
  ["--config needs a file"]="--config heeft een bestand nodig"
  ["syn-install must be run as root"]="syn-install moet als root draaien"
  ["  SynapseOS is running from the live image."]="  SynapseOS draait vanaf het live-image."
  ["Starting the desktop — the installer opens with it."]="Het bureaublad wordt gestart — de installatie opent ermee."
  ["  This installer will:
    1. Partition a disk
    2. Install SynapseOS base system
    3. Install SynapseOS packages
    4. Create user account
    5. Choose desktop environment
    6. Configure system & bootloader"]="  Deze installatie gaat:
    1. Een schijf partitioneren
    2. Het SynapseOS-basissysteem installeren
    3. De SynapseOS-pakketten installeren
    4. Een gebruikersaccount aanmaken
    5. Een bureaubladomgeving kiezen
    6. Systeem en opstartlader instellen"
  ["ALL DATA ON THE TARGET DISK WILL BE ERASED"]="ALLE GEGEVENS OP DE DOELSCHIJF WORDEN GEWIST"
  ["Press ENTER to continue or Ctrl+C to abort..."]="Druk op ENTER om door te gaan, of Ctrl+C om af te breken..."
  ["Checking network"]="Netwerk wordt gecontroleerd"
  ["Network is up"]="Het netwerk werkt"
  ["  No network detected. Starting NetworkManager..."]="  Geen netwerk gevonden. NetworkManager wordt gestart..."
  ["  No connection — but this machine has Wi-Fi."]="  Geen verbinding — maar deze machine heeft wifi."
  ["Open the Wi-Fi picker (nmtui)? [Y/n]:"]="De wifi-kiezer (nmtui) openen? [Y/n]:"
  ["No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."]="Geen netwerkverbinding en geen wifi-apparaat om in te stellen.
  SynapseOS downloadt het basissysteem tijdens de installatie, dus sluit een
  kabel aan en start opnieuw."
  ["Network connected"]="Netwerk verbonden"
  ["Step 1 — Select Target Disk"]="Stap 1 — Kies de doelschijf"
  ["  Available disks:"]="  Beschikbare schijven:"
  ["Target disk (e.g. sda, vda, nvme0n1):"]="Doelschijf (bijv. sda, vda, nvme0n1):"
  ["Target disk is in use. Unmount its partitions and re-run."]="De doelschijf is in gebruik. Ontkoppel de partities en start opnieuw."
  ["Boot mode: UEFI"]="Opstartmodus: UEFI"
  ["Boot mode: BIOS/Legacy"]="Opstartmodus: BIOS/Legacy"
  ["  Encrypts the root filesystem with LUKS2. You will be asked for the
  passphrase at every boot, before the system starts."]="  Versleutelt het hoofdbestandssysteem met LUKS2. De wachtwoordzin wordt bij
  elke start gevraagd, voordat het systeem begint."
  ["Encrypt the disk? [y/N]:"]="De schijf versleutelen? [y/N]:"
  ["                          With encryption it is the BETTER choice: the
                          kernel lives on the EFI partition and only the
                          initramfs unlocks, so /boot needs no separate
                          unencrypted partition."]="                          Met versleuteling is dit de BETERE keuze: de
                          kernel staat op de EFI-partitie en alleen de
                          initramfs ontgrendelt, dus /boot heeft geen
                          aparte onversleutelde partitie nodig."
  ["                          It copies each snapshot's kernel onto the EFI
                          partition, so that partition is made much
                          larger when snapshots are enabled."]="                          Het kopieert de kernel van elke momentopname
                          naar de EFI-partitie, dus die partitie wordt
                          veel groter aangemaakt met momentopnamen aan."
  ["  Snapshots are cheap but not free: they hold the old copy of
  anything that changes, so a disk near full stays near full."]="  Momentopnamen zijn goedkoop maar niet gratis: ze houden de oude kopie van
  alles wat verandert, dus een bijna volle schijf blijft bijna vol."
  ["Enable snapshots? [Y/n]:"]="Momentopnamen inschakelen? [Y/n]:"
  ["mkfs.ext4 is missing from this installer image — /boot cannot be created"]="mkfs.ext4 ontbreekt in dit installatie-image — /boot kan niet worden aangemaakt"
  ["btrfs is missing from this installer image — subvolumes cannot be created"]="btrfs ontbreekt in dit installatie-image — subvolumes kunnen niet worden aangemaakt"
  ["Are these correct? [Y/n]:"]="Klopt dit? [Y/n]:"
  ["Starting the questions over — the disk has not been touched."]="De vragen beginnen opnieuw — de schijf is niet aangeraakt."
  ["cryptsetup is not available on this installer image"]="cryptsetup is niet beschikbaar op dit installatie-image"
  ["Encryption passphrase:"]="Wachtwoordzin voor versleuteling:"
  ["Repeat passphrase:"]="Herhaal de wachtwoordzin:"
  ["Empty passphrase — that would leave the disk unprotected."]="Lege wachtwoordzin — dat zou de schijf onbeschermd laten."
  ["Passphrases did not match — try again."]="De wachtwoordzinnen komen niet overeen — probeer opnieuw."
  ["Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."]="De wachtwoordzin is korter dan 8 tekens. Een korte is weinig waard
  tegen iemand die de schijf in handen heeft."
  ["Use it anyway? [y/N]:"]="Toch gebruiken? [y/N]:"
  ["Encryption enabled — root will be LUKS2"]="Versleuteling aan — de hoofdpartitie wordt LUKS2"
  ["cryptsetup open failed — the passphrase did not take"]="cryptsetup open is mislukt — de wachtwoordzin werd niet geaccepteerd"
  ["Failed to mount root"]="Aankoppelen van de hoofdpartitie mislukt"
  ["  Creating btrfs subvolumes..."]="  btrfs-subvolumes worden aangemaakt..."
  ["btrfs: could not create @"]="btrfs: kon @ niet aanmaken"
  ["btrfs: could not create @home"]="btrfs: kon @home niet aanmaken"
  ["btrfs: could not create @snapshots"]="btrfs: kon @snapshots niet aanmaken"
  ["btrfs: could not create @var_log"]="btrfs: kon @var_log niet aanmaken"
  ["btrfs: could not create @pkg"]="btrfs: kon @pkg niet aanmaken"
  ["could not remount the btrfs root onto @"]="kon de btrfs-hoofdpartitie niet opnieuw op @ aankoppelen"
  ["Failed to mount @"]="Aankoppelen van @ mislukt"
  ["Failed to mount @home"]="Aankoppelen van @home mislukt"
  ["Failed to mount @snapshots"]="Aankoppelen van @snapshots mislukt"
  ["Failed to mount @var_log"]="Aankoppelen van @var_log mislukt"
  ["Failed to mount @pkg"]="Aankoppelen van @pkg mislukt"
  ["This adds one partition in the free space. Back up anything irreplaceable first."]="Dit voegt één partitie toe in de vrije ruimte. Maak eerst een kopie van alles wat onvervangbaar is."
  ["Type 'yes' to install alongside:"]="Typ 'yes' om ernaast te installeren:"
  ["Aborted"]="Afgebroken"
  ["Failed to create the root partition"]="Aanmaken van de hoofdpartitie mislukt"
  ["Could not identify the new partition after creating it"]="Kon de nieuwe partitie na het aanmaken niet herkennen"
  ["Failed to format root partition"]="Formatteren van de hoofdpartitie mislukt"
  ["Failed to mount the existing ESP"]="Aankoppelen van de bestaande ESP mislukt"
  ["no partition editor on this image (cfdisk, fdisk and parted are all missing)"]="geen partitiebewerker op dit image (cfdisk, fdisk en parted ontbreken alle drie)"
  ["  What this install needs:"]="  Wat deze installatie nodig heeft:"
  ["    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"]="    • een EFI-systeempartitie (type EF00 / vlag 'esp') — een bestaande kan hergebruikt worden"
  ["  Skipping the partition editor (--config)."]="  De partitiebewerker wordt overgeslagen (--config)."
  ["Format it? Everything on it is lost [y/N]:"]="Formatteren? Alles erop gaat verloren [y/N]:"
  ["Separate /boot partition:"]="Aparte /boot-partitie:"
  ["Swap partition (blank for none):"]="Wisselpartitie (leeg voor geen):"
  ["Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"]="Opnieuw maken? De UUID verandert en breekt de fstab van dat systeem [y/N]:"
  ["Type 'yes' to format these:"]="Typ 'yes' om deze te formatteren:"
  ["  Formatting EFI partition..."]="  De EFI-partitie wordt geformatteerd..."
  ["  Formatting /boot partition..."]="  De /boot-partitie wordt geformatteerd..."
  ["Failed to mount /boot"]="Aankoppelen van /boot mislukt"
  ["Type 'yes' to confirm:"]="Typ 'yes' om te bevestigen:"
  ["  Creating GPT partition table..."]="  De GPT-partitietabel wordt aangemaakt..."
  ["Failed to format EFI partition"]="Formatteren van de EFI-partitie mislukt"
  ["Failed to format boot partition"]="Formatteren van de opstartpartitie mislukt"
  ["  Creating MBR partition table..."]="  De MBR-partitietabel wordt aangemaakt..."
  ["Disk partitioned and mounted at /mnt"]="Schijf gepartitioneerd en aangekoppeld op /mnt"
  ["Step 3 — Installing Base System"]="Stap 3 — Het basissysteem wordt geïnstalleerd"
  ["  Initializing pacman keyring..."]="  De sleutelbos van pacman wordt opgezet..."
  ["  Running pacstrap (this may take several minutes)..."]="  pacstrap draait (dit kan enkele minuten duren)..."
  ["pacstrap failed — check network connection"]="pacstrap is mislukt — controleer de netwerkverbinding"
  ["grub-install not found in chroot — attempting recovery..."]="grub-install niet gevonden in de chroot — er wordt geprobeerd te herstellen..."
  ["Could not install grub into target — check network"]="Kon grub niet in het doelsysteem installeren — controleer het netwerk"
  ["Base system installed"]="Basissysteem geïnstalleerd"
  ["Step 4 — Choose What to Install"]="Stap 4 — Kies wat er geïnstalleerd wordt"
  ["  What should be installed alongside the SynapseOS core?"]="  Wat moet er naast de kern van SynapseOS geïnstalleerd worden?"
  ["                   Bluetooth, printing, Wine, phone   (default)"]="                   Bluetooth, printen, Wine, telefoon   (standaard)"
  ["                   the ordinary software people install anyway"]="                   de gewone software die mensen toch installeren"
  ["  Every preset except Minimal then asks WHICH AI model to download,
  and skipping it is one of the answers."]="  Elke voorinstelling behalve Minimaal vraagt daarna WELK AI-model
  gedownload moet worden, en overslaan is een van de antwoorden."
  ["Full install selected"]="Volledige installatie gekozen"
  ["Minimal install selected"]="Minimale installatie gekozen"
  ["  Two kinds of question. First the packages, as pages of
  checkboxes; then the handful of options that are a whole
  subsystem rather than a package."]="  Twee soorten vragen. Eerst de pakketten, als pagina's met
  vinkjes; daarna de handvol opties die een heel subsysteem
  zijn in plaats van een pakket."
  ["  And the software people install on the first evening anyway.
  All of it is in the Arch repositories; none of it is ours."]="  En de software die mensen de eerste avond toch installeren.
  Alles zit in de Arch-pakketbronnen; niets ervan is van ons."
  ["  The rest is y/n. The default (shown in caps) is Standard."]="  De rest is j/n. De standaardkeuze (in hoofdletters) is Standaard."
  ["syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."]="syn-update uitgevinkt: deze machine heeft dan geen enkele manier om
  nog een SynapseOS-pakket te ontvangen. Dat later herstellen betekent het met de hand
  vanaf de ISO installeren, of opnieuw installeren."
  ["Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."]="Noch het bureaublad noch de AI-daemon is behouden. Dat is een Arch-
  systeem met wat SynapseOS-gereedschap erop, wat een toegestaan antwoord is —
  maar niets in de documentatie beschrijft de machine die je dan krijgt."
  ["Custom install configured"]="Aangepaste installatie ingesteld"
  ["Standard install selected"]="Standaardinstallatie gekozen"
  ["  synapd loads one model and everything AI in SynapseOS talks to it:
  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,
  over this connection, onto the disk you are installing to."]="  synapd laadt één model en alles wat AI is in SynapseOS praat ermee:
  synsh, het AI-paneel van het bureaublad, Chibi, Vibe. Het wordt nu gedownload,
  over deze verbinding, naar de schijf waarop je installeert."
  ["  A smaller model is not just faster and lighter: it follows
  instructions worse. synsh mistakes what you asked for, Vibe's code
  needs more fixing, Chibi loses the thread. Take the default unless
  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."]="  Een kleiner model is niet alleen sneller en lichter: het volgt
  instructies slechter. synsh begrijpt je vraag verkeerd, de code van Vibe
  heeft meer correctie nodig, Chibi raakt de draad kwijt. Neem de standaard tenzij
  de schijf of het geheugen anders zeggen — 7B wil ~6 GB vrij RAM."
  ["  Whatever you pick, it can be changed later: 'syn model download',
  or Super+C ▸ System ▸ AI model on the desktop."]="  Wat je ook kiest, het kan later veranderd worden: 'syn model download',
  of Super+C ▸ Systeem ▸ AI-model op het bureaublad."
  ["Install this selection? [Y/n]:"]="Deze selectie installeren? [Y/n]:"
  ["Choosing again — nothing has been installed yet."]="Opnieuw kiezen — er is nog niets geïnstalleerd."
  ["Step 4b — Installing SynapseOS"]="Stap 4b — SynapseOS wordt geïnstalleerd"
  ["Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."]="Kon ILoveCandy niet inschakelen in /etc/pacman.conf (alleen cosmetisch)."
  ["  Enabling [multilib] (32-bit repo, needed by Steam)..."]="  [multilib] wordt ingeschakeld (32-bits bron, nodig voor Steam)..."
  ["Could not sync the multilib database — Steam may fail to install"]="Kon de multilib-database niet synchroniseren — Steam wordt mogelijk niet geïnstalleerd"
  ["Could not enable [multilib]; Steam will be skipped."]="Kon [multilib] niet inschakelen; Steam wordt overgeslagen."
  ["Some SynapseOS packages failed to install — verifying below"]="Sommige SynapseOS-pakketten konden niet geïnstalleerd worden — hieronder wordt het gecontroleerd"
  ["No SynapseOS packages were selected. This will be an Arch system."]="Er zijn geen SynapseOS-pakketten gekozen. Dit wordt een Arch-systeem."
  ["SynapseOS packages installed"]="SynapseOS-pakketten geïnstalleerd"
  ["Component selection recorded in /etc/synapseos/components.conf"]="Componentkeuze vastgelegd in /etc/synapseos/components.conf"
  ["Step 5 — Create User Account"]="Stap 5 — Maak een gebruikersaccount aan"
  ["  Create a user account for the installed system."]="  Maak een gebruikersaccount voor het geïnstalleerde systeem."
  ["Username [default: syn]:"]="Gebruikersnaam [standaard: syn]:"
  ["Full name (optional):"]="Volledige naam (optioneel):"
  ["Password:"]="Wachtwoord:"
  ["Confirm password:"]="Bevestig het wachtwoord:"
  ["Passwords do not match or are empty — try again"]="De wachtwoorden komen niet overeen of zijn leeg — probeer opnieuw"
  ["Step 6 — Desktop Environment"]="Stap 6 — Bureaubladomgeving"
  ["  Choose a desktop environment:"]="  Kies een bureaubladomgeving:"
  ["  Installing KDE Plasma..."]="  KDE Plasma wordt geïnstalleerd..."
  ["Some KDE packages failed to install"]="Sommige KDE-pakketten konden niet geïnstalleerd worden"
  ["KDE Plasma installed"]="KDE Plasma geïnstalleerd"
  ["  Installing GNOME..."]="  GNOME wordt geïnstalleerd..."
  ["Some GNOME packages failed to install"]="Sommige GNOME-pakketten konden niet geïnstalleerd worden"
  ["GNOME installed (session only — SynapseOS apps, not GNOME's)"]="GNOME geïnstalleerd (alleen de sessie — SynapseOS-programma's, niet die van GNOME)"
  ["  Installing greetd (login screen) + desktop extras..."]="  greetd (aanmeldscherm) + extra's voor het bureaublad worden geïnstalleerd..."
  ["greetd failed to install — boot falls back to getty login"]="greetd kon niet geïnstalleerd worden — het opstarten valt terug op aanmelden via getty"
  ["SynapseUI selected (included)"]="SynapseUI gekozen (inbegrepen)"
  ["Installing Wine"]="Wine wordt geïnstalleerd"
  ["Wine installed"]="Wine geïnstalleerd"
  ["wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."]="wine kon niet geïnstalleerd worden — Windows-.exe/.msi zullen niet draaien.
  Installeer het later met 'sudo pacman -S wine wine-mono'."
  ["Configuring Video Driver"]="Videostuurprogramma wordt ingesteld"
  ["  Virtual machine — installing mesa (synui uses pixman here)..."]="  Virtuele machine — mesa wordt geïnstalleerd (synui gebruikt hier pixman)..."
  ["mesa failed to install"]="mesa kon niet geïnstalleerd worden"
  ["NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"]="De installatie van het NVIDIA-stuurprogramma is mislukt — het systeem zou op
  nouveau opstarten en de renderer van synui zou nooit beginnen"
  ["NVIDIA sleep services enabled (VRAM save/restore)"]="NVIDIA-slaapdiensten ingeschakeld (VRAM opslaan/herstellen)"
  ["Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"]="Kon nvidia-{suspend,resume,hibernate} niet inschakelen — de slaapstand
  kan zwart blijven als NVreg_PreserveVideoMemoryAllocations later wordt aangezet"
  ["  synapd can run inference on this GPU instead of the CPU.
  This downloads the CUDA runtime (~4.7 GiB installed)."]="  synapd kan de inferentie op deze GPU doen in plaats van op de CPU.
  Dit downloadt de CUDA-omgeving (~4,7 GiB geïnstalleerd)."
  ["Enable GPU inference? [Y/n]:"]="GPU-inferentie inschakelen? [Y/n]:"
  ["Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda"]="De inferentie blijft op de CPU. Later omzetten met:
  sudo pacman -S synapse-llama-cuda"
  ["  Installing synapse-llama-cuda (this takes a while)..."]="  synapse-llama-cuda wordt geïnstalleerd (dit duurt even)..."
  ["This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"]="Deze ISO bevat geen GPU-versie van llama, dus synapd draait op de CPU
  ondanks de NVIDIA-kaart. (De ISO moet gebouwd worden op een host met de CUDA-
  toolkit, anders bestaat synapse-llama-cuda niet.)"
  ["Video driver install failed — synui may fall back to software rendering"]="De installatie van het videostuurprogramma is mislukt — synui valt mogelijk terug op softwarematig renderen"
  ["Video drivers installed"]="Videostuurprogramma's geïnstalleerd"
  ["  Enabling GPU inference (synapse-llama-vulkan)..."]="  GPU-inferentie wordt ingeschakeld (synapse-llama-vulkan)..."
  ["This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"]="Deze ISO bevat geen Vulkan-versie van llama, dus synapd draait op de CPU
  ondanks de AMD/Intel-GPU. (Bouw de ISO op een host met 'shaderc' +
  vulkan-headers, anders bestaat synapse-llama-vulkan niet.)"
  ["Installing Steam and the game stack"]="Steam en de spellenlaag worden geïnstalleerd"
  ["  Installing steam and the 32-bit runtime libraries..."]="  steam en de 32-bits runtimebibliotheken worden geïnstalleerd..."
  ["Steam installed (native multilib package)"]="Steam geïnstalleerd (native multilib-pakket)"
  ["steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."]="steam kon niet geïnstalleerd worden. Het systeem is verder compleet —
  installeer het later met 'sudo pacman -S steam' ([multilib] staat al
  aan in /etc/pacman.conf)."
  ["  Installing the game stack (overlay, governor, micro-compositor)..."]="  De spellenlaag wordt geïnstalleerd (overlay, governor, micro-compositor)..."
  ["Game stack installed (mangohud, gamemode, gamescope)"]="Spellenlaag geïnstalleerd (mangohud, gamemode, gamescope)"
  ["The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"]="De spellenlaag kon niet geïnstalleerd worden. Steam werkt gewoon; de FPS-
  overlay, de CPU/GPU-governor en 'synui-game-run --gamescope' niet.
  Installeer ze later met:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
  ["Installing CachyOS Proton"]="CachyOS Proton wordt geïnstalleerd"
  ["  Fetching the CachyOS keyring and mirrorlist..."]="  De sleutelbos en spiegellijst van CachyOS worden opgehaald..."
  ["  Trusting the CachyOS master key..."]="  De hoofdsleutel van CachyOS wordt vertrouwd..."
  ["Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"]="Kon de hoofdsleutel van CachyOS niet ophalen bij keyserver.ubuntu.com.
  Zonder die sleutel kan de ondertekende sleutelbos niet geïnstalleerd worden, dus
  CachyOS Proton wordt overgeslagen. Voeg het later toe met:  synpkg cachyos enable-repo"
  ["  Master key pinned as expected — trusting it..."]="  Hoofdsleutel zoals verwacht — hij wordt vertrouwd..."
  ["[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."]="[cachyos] is toegevoegd maar bevat geen pakketten — hij wordt weer
  verwijderd zodat hij een latere upgrade niet blokkeert."
  ["The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."]="De sleutelbos van CachyOS bevat niet de verwachte hoofdsleutel.
  Hij wordt niet vertrouwd — de pakketbron is NIET toegevoegd."
  ["  Installing proton-cachyos-slr (~340 MB download)..."]="  proton-cachyos-slr wordt geïnstalleerd (~340 MB download)..."
  ["CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."]="CachyOS Proton geïnstalleerd — kies het per spel in Steam onder
  Eigenschappen → Compatibiliteit, vermeld als 'proton-cachyos-… (steam linux runtime)'.
  Steam zoekt er alleen bij het opstarten naar, dus herstart Steam als het al draait."
  ["proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"]="proton-cachyos-slr kon niet geïnstalleerd worden. Steam en Valve's eigen
  Proton hebben er geen last van. Installeer het later met:
  sudo pacman -S proton-cachyos-slr"
  ["The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"]="De pakketbron [cachyos] kon niet ingeschakeld worden, dus CachyOS Proton
  is overgeslagen. Steam werkt gewoon met Valve's Proton. Later toevoegen:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
  ["Enabling BlackArch"]="BlackArch wordt ingeschakeld"
  ["  Fetching the BlackArch bootstrap..."]="  De BlackArch-bootstrap wordt opgehaald..."
  ["  Master key pinned as expected — running bootstrap..."]="  Hoofdsleutel zoals verwacht — de bootstrap wordt uitgevoerd..."
  ["blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."]="blackarch-keyring is niet geïnstalleerd — sleutelwisselingen
  bereiken deze machine niet. Herstel met 'sudo pacman -S blackarch-keyring'."
  ["The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."]="De gedownloade strap.sh legt niet de verwachte hoofdsleutel van BlackArch
  vast. Hij wordt niet uitgevoerd — de pakketbron is NIET toegevoegd."
  ["BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."]="BlackArch is niet ingeschakeld. Het systeem is verder compleet;
  voeg het later toe met 'sudo syn arsenal --enable-repo'."
  ["Installing software"]="Software wordt geïnstalleerd"
  ["That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."]="Die transactie is mislukt — elk pakket wordt apart opnieuw geprobeerd, zodat
  de goede toch aankomen en het pakket dat niet deugt bij naam genoemd wordt."
  ["Software installed"]="Software geïnstalleerd"
  ["Installing Flatpak apps"]="Flatpak-programma's worden geïnstalleerd"
  ["flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."]="flatpak kon niet geïnstalleerd worden — de Flatpak-programma's worden overgeslagen.
  Verder heeft niets er last van."
  ["Could not add the flathub remote"]="Kon de flathub-bron niet toevoegen"
  ["Flatpak apps installed"]="Flatpak-programma's geïnstalleerd"
  ["Configuring System"]="Systeem wordt ingesteld"
  ["  fstab generated"]="  fstab aangemaakt"
  ["Swap recorded in fstab"]="Wisselgeheugen vastgelegd in de fstab"
  ["zram configured (compressed swap, half of RAM up to 8 GiB)"]="zram ingesteld (gecomprimeerd wisselgeheugen, de helft van het RAM tot 8 GiB)"
  ["zram-generator is not installed in the target — no compressed swap"]="zram-generator is niet in het doelsysteem geïnstalleerd — geen gecomprimeerd wisselgeheugen"
  ["  Hostname: synapse"]="  Computernaam: synapse"
  ["Step 7 — Language & Region"]="Stap 7 — Taal en regio"
  ["   0) Other — enter a locale by hand"]="   0) Anders — zelf een locale invoeren"
  ["Locale (e.g. sv_SE.UTF-8):"]="Locale (bijv. sv_SE.UTF-8):"
  ["Console keymap (e.g. sv-latin1):"]="Consoletoetsenbord (bijv. sv-latin1):"
  ["Step 8 — Timezone"]="Stap 8 — Tijdzone"
  ["   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"]="   0) Anders — een willekeurige tzdata-naam invoeren (bijv. Europe/Lisbon)"
  ["tzdata name (Region/City):"]="tzdata-naam (Regio/Stad):"
  ["  Did you mean:"]="  Bedoelde je:"
  ["  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"]="  Kies een nummer uit de lijst, of zie: ls /mnt/usr/share/zoneinfo"
  ["  os-release: copied from live system"]="  os-release: overgenomen van het live-systeem"
  ["  issue: copied from live system"]="  issue: overgenomen van het live-systeem"
  ["Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"]="Het doelbestandssysteem is niet meer beschrijfbaar (schijffouten? kijk in 'dmesg') — afgebroken"
  ["sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"]="De sudoers-regels zijn ongeldig na het schrijven van de drop-ins — er wordt geen systeem afgeleverd dat geen sudo kan"
  ["Could not relax pam_faillock in /etc/pam.d/system-auth (a tty-less sudo could still lock the account until reboot)."]="Kon pam_faillock in /etc/pam.d/system-auth niet versoepelen (een sudo zonder tty kan het account nog steeds tot de herstart blokkeren)."
  ["could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"]="kon /var/lib/synapse-src niet vooraf aanmaken — de updater vraagt bij de eerste keer om een wachtwoord"
  ["  Desktop: KDE Plasma (SDDM login screen)"]="  Bureaublad: KDE Plasma (SDDM-aanmeldscherm)"
  ["  GDM: SynapseOS logo on the login screen"]="  GDM: SynapseOS-logo op het aanmeldscherm"
  ["  Desktop: GNOME (GDM login screen)"]="  Bureaublad: GNOME (GDM-aanmeldscherm)"
  ["  Desktop: TTY only"]="  Bureaublad: alleen TTY"
  ["  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"]="  Bureaublad: SynapseUI (synui-greeter — het aanmelden spiegelt het vergrendelscherm)"
  ["  motd: written for this installation"]="  motd: geschreven voor deze installatie"
  ["  note: syn-rgb.path is not installed; RGB lights stay off"]="  let op: syn-rgb.path is niet geïnstalleerd; de RGB-verlichting blijft uit"
  ["AI model"]="AI-model"
  ["  AI model skipped — install one later with: syn model download"]="  AI-model overgeslagen — installeer er later een met: syn model download"
  ["AI model installed"]="AI-model geïnstalleerd"
  ["  the install, and everything else on the disk is already done."]="  van de installatie, en al het andere op de schijf is al klaar."
  ["syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."]="syn-model staat niet op het doelsysteem, dus er is geen model gedownload.
  Het hoort bij de kern; als het is uitgevinkt, blijft de AI stil."
  ["Configuring Nix"]="Nix wordt ingesteld"
  ["Nix configured — /etc/synapseos/nix"]="Nix ingesteld — /etc/synapseos/nix"
  ["  That is the download — a few hundred MB before any packages
  you add to home.nix. 'syn nix edit' opens it."]="  Dat is de download — een paar honderd MB nog voor elk pakket
  dat je aan home.nix toevoegt. 'syn nix edit' opent het."
  ["nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."]="nix is geïnstalleerd, maar het pakket 'syn' staat niet op het doelsysteem, dus
  de configurator is niet opgezet. Nix zelf werkt;
  de laag /etc/synapseos/nix heeft 'syn' nodig."
  ["nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."]="nix kon niet geïnstalleerd worden — de declaratieve laag is niet beschikbaar.
  Installeer die later met 'sudo pacman -S nix && sudo syn nix init'."
  ["  Generating initramfs..."]="  Initramfs wordt aangemaakt..."
  ["mkinitcpio failed — the installed system would not boot"]="mkinitcpio is mislukt — het geïnstalleerde systeem zou niet opstarten"
  ["initramfs missing after mkinitcpio — the installed system would not boot"]="initramfs ontbreekt na mkinitcpio — het geïnstalleerde systeem zou niet opstarten"
  ["System configured"]="Systeem ingesteld"
  ["Installing Bootloader"]="Opstartlader wordt geïnstalleerd"
  ["grub-install (UEFI) failed"]="grub-install (UEFI) is mislukt"
  ["grub-install (BIOS) failed"]="grub-install (BIOS) is mislukt"
  ["  Generating GRUB config..."]="  De GRUB-configuratie wordt aangemaakt..."
  ["grub-mkconfig failed"]="grub-mkconfig is mislukt"
  ["grub.cfg missing after install"]="grub.cfg ontbreekt na de installatie"
  ["grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"]="grub.cfg bevat een GRUB-wachtwoord — het blijft alleen voor root leesbaar, dus de instellingen-app kan niets over opstartitems melden"
  ["  Installing systemd-boot..."]="  systemd-boot wordt geïnstalleerd..."
  ["bootctl install failed"]="bootctl install is mislukt"
  ["  Registering systemd-boot with the firmware..."]="  systemd-boot wordt bij de firmware aangemeld..."
  ["efibootmgr entry not created — the removable-media path still applies"]="efibootmgr-item niet aangemaakt — het pad voor verwisselbare media geldt nog steeds"
  ["could not read the root filesystem UUID"]="kon de UUID van het hoofdbestandssysteem niet lezen"
  ["vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"]="vmlinuz-linux staat niet op de ESP — systemd-boot zou niets vinden om op te starten"
  ["initramfs is not on the ESP — systemd-boot would find nothing to boot"]="de initramfs staat niet op de ESP — systemd-boot zou niets vinden om op te starten"
  ["systemd-boot did not install its EFI binary"]="systemd-boot heeft zijn EFI-bestand niet geïnstalleerd"
  ["  Installing limine..."]="  limine wordt geïnstalleerd..."
  ["could not copy limine's EFI binary to the ESP"]="kon het EFI-bestand van limine niet naar de ESP kopiëren"
  ["limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"]="limine-mkinitcpio-hook niet geïnstalleerd — een later geïnstalleerde kernel krijgt GEEN opstartitem"
  ["vmlinuz-linux is not on the ESP — limine would find nothing to boot"]="vmlinuz-linux staat niet op de ESP — limine zou niets vinden om op te starten"
  ["limine's EFI binary is not on the ESP"]="het EFI-bestand van limine staat niet op de ESP"
  ["limine.conf has no kernel entry"]="limine.conf heeft geen enkel kernelitem"
  ["  Verifying the encrypted boot path..."]="  Het versleutelde opstartpad wordt gecontroleerd..."
  ["/boot is not a separate mount — an encrypted root needs a plain /boot"]="/boot is geen apart aankoppelpunt — een versleutelde hoofdpartitie heeft een onversleuteld /boot nodig"
  ["/boot is missing from fstab — it would not be mounted after boot"]="/boot ontbreekt in de fstab — het zou na het opstarten niet aangekoppeld worden"
  ["Encrypted boot path verified"]="Versleuteld opstartpad gecontroleerd"
  ["Configuring snapshots"]="Momentopnamen worden ingesteld"
  ["snapper's config template is missing — snapshots cannot be configured"]="het configuratiesjabloon van snapper ontbreekt — momentopnamen kunnen niet ingesteld worden"
  ["could not write /etc/snapper/configs/root"]="kon /etc/snapper/configs/root niet schrijven"
  ["snapper does not see the 'root' config — snapshots would never be taken"]="snapper ziet de configuratie 'root' niet — er zou nooit een momentopname genomen worden"
  ["snapper's root config was not tuned — timeline snapshots would fill the disk"]="de root-configuratie van snapper is niet bijgesteld — periodieke momentopnamen zouden de schijf vullen"
  ["could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"]="kon grub-btrfsd niet inschakelen — momentopnamen verschijnen niet vanzelf in het opstartmenu"
  ["Snapshots enabled (snapper + snap-pac, bootable from GRUB)"]="Momentopnamen ingeschakeld (snapper + snap-pac, opstartbaar vanuit GRUB)"
  ["could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"]="kon limine-snapper-sync niet inschakelen — momentopnamen bereiken het opstartmenu niet vanzelf"
  ["could not take the post-install snapshot"]="kon de momentopname na de installatie niet nemen"
  ["could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"]="kon het synchroniseren van momentopnamen bij de eerste start niet inschakelen — het menu vult zich in plaats daarvan na de eerste upgrade"
  ["Snapshots enabled (snapper + snap-pac, bootable from limine)"]="Momentopnamen ingeschakeld (snapper + snap-pac, opstartbaar vanuit limine)"
  ["Bootloader installed"]="Opstartlader geïnstalleerd"
  ["  The root account is locked (no root login / su).
  Note: 3 wrong password attempts lock the account for 10 minutes."]="  Het root-account is vergrendeld (geen root-aanmelding / su).
  Let op: 3 verkeerde wachtwoorden vergrendelen het account 10 minuten."
  ["You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."]="De wachtwoordzin voor de versleuteling wordt bij elke start gevraagd,
  VOOR het aanmeldscherm. Er is geen manier om hem terug te halen."
  ["    syn-crypt status                    is this disk encrypted, and how
    sudo syn-crypt change-key           replace the passphrase
    sudo syn-crypt add-key              add a second one
    sudo syn-crypt backup-header FILE   save the LUKS header"]="    syn-crypt status                    is deze schijf versleuteld, en hoe
    sudo syn-crypt change-key           de wachtwoordzin vervangen
    sudo syn-crypt add-key              een tweede toevoegen
    sudo syn-crypt backup-header BESTAND  de LUKS-header bewaren"
  ["  means the data is unrecoverable even with the right passphrase."]="  betekent dat de gegevens onherstelbaar zijn, zelfs met de juiste wachtwoordzin."
  ["Remove installation media and press ENTER to reboot..."]="Verwijder het installatiemedium en druk op ENTER om te herstarten..."
  ["Install SynapseOS     — right here, in this terminal"]="SynapseOS installeren   — hier, in deze terminal"
  ["Install graphically   — starts the desktop first"]="Grafisch installeren    — start eerst het bureaublad"
  ["Try the live desktop  — look around; install later"]="Live-bureaublad proberen — rondkijken; later installeren"
  ["Target:"]="Doel:"
  ["ALONGSIDE"]="ERNAAST"
  ["ERASE"]="WISSEN"
  ["ADVANCED"]="GEAVANCEERD"
  ["Encrypt this installation?"]="Deze installatie versleutelen?"
  ["There is no recovery."]="Er is geen herstel mogelijk."
  ["Root filesystem"]="Hoofdbestandssysteem"
  ["ext4   — the default. Boring, proven, repairable by anything."]="ext4   — de standaard. Saai, beproefd, door van alles te repareren."
  ["btrfs  — snapshots + zstd compression. Roll back a bad update
                    from the boot menu. Uses more RAM and more CPU."]="btrfs  — momentopnamen + zstd-compressie. Draai een slechte update terug
                    vanuit het opstartmenu. Meer RAM en meer CPU."
  ["xfs    — fast on large files. No snapshots, and it cannot be
                    SHRUNK once created."]="xfs    — snel met grote bestanden. Geen momentopnamen, en eenmaal
                    aangemaakt niet te VERKLEINEN."
  ["f2fs   — built for flash. Good on SD cards and cheap SSDs;
                    unusual enough that fewer rescue tools know it."]="f2fs   — gemaakt voor flash. Goed op SD-kaarten en goedkope SSD's;
                    ongebruikelijk genoeg dat weinig reddingsgereedschap hem kent."
  ["Bootloader"]="Opstartlader"
  ["GRUB          — the default. Detects other operating systems,
                          and the only one here that can boot a btrfs
                          snapshot."]="GRUB          — de standaard. Herkent andere besturingssystemen,
                          en de enige hier die een btrfs-momentopname
                          kan opstarten."
  ["systemd-boot  — minimal. No OS detection, no snapshot menu."]="systemd-boot  — minimaal. Geen OS-herkenning, geen momentopnamemenu."
  ["limine        — modern and fast, and it CAN boot snapshots."]="limine        — modern en snel, en hij KAN momentopnamen opstarten."
  ["Automatic snapshots?"]="Automatische momentopnamen?"
  ["Review the plan — nothing has been written yet:"]="Bekijk het plan — er is nog niets geschreven:"
  ["nothing else is touched"]="er wordt niets anders aangeraakt"
  ["not"]="niet"
  ["Partition"]="Partitioneer"
  ["now."]="nu."
  ["Partitions now on"]="Partities nu op"
  ["These partitions will be FORMATTED"]="Deze partities worden GEFORMATTEERD"
  ["Full      — Standard + Steam + Nix + more software"]="Volledig  — Standaard + Steam + Nix + meer software"
  ["Standard  — the SynapseOS suite, Firefox, AI model,"]="Standaard — de SynapseOS-suite, Firefox, AI-model,"
  ["Minimal   — core daemons only: none of the above"]="Minimaal  — alleen de kerndiensten: niets van het bovenstaande"
  ["Custom    — tick every package yourself, ours and"]="Eigen     — elk pakket zelf aanvinken, de onze en"
  ["Which AI model should this machine run?"]="Welk AI-model moet deze machine draaien?"
  ["Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"]="Mistral 7B Instruct   ~4,1 GB   aanbevolen — hierop is SynapseOS afgestemd"
  ["Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"]="Phi-3 Mini 4K         ~2,2 GB   half zo groot, en merkbaar zwakker"
  ["Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"]="Qwen2 0.5B            ~0,4 GB   past overal, en antwoordt daarnaar"
  ["None                            skip it — nothing else changes"]="Geen                            overslaan — verder verandert er niets"
  ["Installing:"]="Wordt geïnstalleerd:"
  ["SynapseUI  — AI-native Wayland compositor  (default)"]="SynapseUI  — AI-native Wayland-compositor  (standaard)"
  ["SynapseUI  — NOT AVAILABLE: synui was not selected"]="SynapseUI  — NIET BESCHIKBAAR: synui is niet gekozen"
  ["KDE Plasma — Full-featured Wayland desktop"]="KDE Plasma — volwaardig Wayland-bureaublad"
  ["GNOME      — Clean, modern Wayland desktop"]="GNOME      — strak, modern Wayland-bureaublad"
  ["TTY only   — No GUI (headless/server)"]="Alleen TTY — geen grafische omgeving (headless/server)"
  ["Disk:"]="Schijf:"
  ["Boot:"]="Opstarten:"
  ["Encrypted:"]="Versleuteld:"
  ["Desktop:"]="Bureaublad:"
  ["User:"]="Gebruiker:"
  ["Hostname:"]="Computernaam:"
  ["Back up the header to another machine."]="Bewaar de header op een andere machine."
  ["%s is mounted — unmount it first\\n"]="%s is aangekoppeld — ontkoppel hem eerst
"
  ["%s is %s MiB — %s needs at least %s MiB\\n"]="%s is %s MiB — %s heeft er minstens %s nodig
"
  ["  Generating %s (a few seconds)...\\n"]="  %s wordt aangemaakt (een paar seconden)...
"
  ["Language: %s  (%s, keyboard %s)\\n"]="Taal: %s  (%s, toetsenbord %s)
"
  ["  This disk already holds %s partition(s), an EFI System\\n  Partition (%s), and %s GiB of free space.\\n"]="  Deze schijf bevat al %s partitie(s), een EFI-systeem-
  partitie (%s), en %s GiB vrije ruimte.
"
  ["    1) Install %s — use the free space, keep everything else\\n"]="    1) %s installeren — de vrije ruimte gebruiken, al het andere houden
"
  ["    2) %s the whole disk — delete every partition and all data\\n"]="    2) De hele schijf %s — elke partitie en alle gegevens verwijderen
"
  ["    3) %s — partition this disk yourself, then pick the partitions\\n"]="    3) %s — deze schijf zelf partitioneren, dan de partities kiezen
"
  ["    1) %s the whole disk — delete every partition and all data  (default)\\n"]="    1) De hele schijf %s — elke partitie en alle gegevens verwijderen  (standaard)
"
  ["    2) %s — partition this disk yourself, then pick the partitions\\n"]="    2) %s — deze schijf zelf partitioneren, dan de partities kiezen
"
  ["  %s If you forget the passphrase the data is\\n  gone — no password reset, no support call, nothing.\\n"]="  %s Als je de wachtwoordzin vergeet zijn de gegevens
  weg — geen herstel, geen belletje naar de helpdesk, niets.
"
  ["  snapper takes a snapshot before and after every pacman\\n  transaction, and %s grows a menu to boot any of them. A\\n  bad upgrade becomes a reboot instead of a rescue USB.\\n"]="  snapper neemt een momentopname voor en na elke pacman-transactie,
  en %s krijgt een menu om er een van op te starten. Een
  slechte upgrade wordt zo een herstart in plaats van een reddings-USB.
"
  ["    Disk          : %s\\n"]="    Schijf        : %s
"
  ["    Firmware      : %s\\n"]="    Firmware      : %s
"
  ["    Filesystem    : %s\\n"]="    Bestandssyst. : %s
"
  ["    Bootloader    : %s\\n"]="    Opstartlader  : %s
"
  ["    Separate /boot: %s\\n"]="    Apart /boot: %s
"
  ["    Encryption    : %s\\n"]="    Versleuteling : %s
"
  ["    Snapshots     : %s\\n"]="    Momentopnamen : %s
"
  ["  Encrypting %s (LUKS2)...\\n"]="  %s wordt versleuteld (LUKS2)...
"
  ["  Formatting root partition (%s)...\\n"]="  De hoofdpartitie wordt geformatteerd (%s)...
"
  ["    • KEEP   all %s existing partition(s), including Windows\\n"]="    • BEHOUDEN  alle %s bestaande partities, Windows inbegrepen
"
  ["    • REUSE  %s as the EFI partition (mounted, %s formatted)\\n"]="    • HERGEBRUIK %s als EFI-partitie (aangekoppeld, %s geformatteerd)
"
  ["    • CREATE a new ext4 root of ~%s GiB in the free space\\n"]="    • AANMAKEN  een nieuwe ext4-hoofdpartitie van ~%s GiB in de vrije ruimte
"
  ["  Creating root partition in free space (%sMiB–%sMiB)...\\n"]="  De hoofdpartitie wordt in de vrije ruimte aangemaakt (%s MiB–%s MiB)...
"
  ["  Formatting new root (%s, ext4)...\\n"]="  De nieuwe hoofdpartitie wordt geformatteerd (%s, ext4)...
"
  ["  %s %s %s The installer will re-read the table when you exit.\\n"]="  %s %s %s De installatie leest de tabel opnieuw zodra je afsluit.
"
  ["    • a root partition, at least %s GiB\\n"]="    • een hoofdpartitie, minstens %s GiB
"
  ["    • a separate /boot of ~1 GiB — %s with this layout cannot read the root\\n"]="    • een apart /boot van ~1 GiB — %s kan met deze indeling de hoofdpartitie niet lezen
"
  ["  Starting %s on %s — write your changes before quitting.\\n"]="  %s wordt gestart op %s — schrijf je wijzigingen weg voor je afsluit.
"
  ["    %s is already swap — another system may resume from it.\\n"]="    %s is al wisselgeheugen — een ander systeem hervat er misschien uit.
"
  ["  Everything else on %s is left untouched.\\n"]="  Al het andere op %s blijft onaangeroerd.
"
  ["  Making swap on %s...\\n"]="  Wisselgeheugen wordt aangemaakt op %s...
"
  ["  Formatting EFI partition (%s MiB)...\\n"]="  De EFI-partitie wordt geformatteerd (%s MiB)...
"
  ["  NVIDIA GPU detected — installing %s (builds the module, takes a while)...\\n"]="  NVIDIA-GPU gevonden — %s wordt geïnstalleerd (bouwt de module, duurt even)...
"
  ["  Installing video stack: %s %s...\\n"]="  Videolaag wordt geïnstalleerd: %s %s...
"
  ["  [cachyos] enabled (%s packages available)\\n"]="  [cachyos] ingeschakeld (%s pakketten beschikbaar)
"
  ["  Language: %s  (chosen at boot)\\n"]="  Taal: %s  (bij het opstarten gekozen)
"
  ["  Locale:   %s   Keyboard: %s (console) / %s (desktop)\\n"]="  Locale:   %s   Toetsenbord: %s (console) / %s (bureaublad)
"
  ["  Installing fonts (%s)...\\n"]="  Lettertypen worden geïnstalleerd (%s)...
"
  ["  Downloading the AI model (%s) — this is the long part of\\n"]="  Het AI-model wordt gedownload (%s) — dit is het lange deel van
"
  ["  Nothing is built yet. As %s, after the first boot:\\n"]="  Er is nog niets gebouwd. Als %s, na de eerste start:
"
  ["  Adding the %s hook to mkinitcpio...\\n"]="  De hook %s wordt aan mkinitcpio toegevoegd...
"
  ["  Installing GRUB (%s)...\\n"]="  GRUB wordt geïnstalleerd (%s)...
"
  ["yes — LUKS2 on %s"]="ja — LUKS2 op %s"
  ["  Admin: use %s with your user password.\\n"]="  Beheer: gebruik %s met je gebruikerswachtwoord.
"
  ["  Manage it later with %s:\\n"]="  Beheer het later met %s:
"
  ["  %s A damaged LUKS header\\n"]="  %s Een beschadigde LUKS-header
"
  ["%s is on the live/boot device — that is the installer's own media\\n"]="%s staat op het live-/opstartapparaat — dat is het eigen medium van de installatie
"
  ["    %s is already FAT — it may hold another OS's bootloader.\\n"]="    %s is al FAT — er kan de opstartlader van een ander systeem op staan.
"
  ["  Creating user '%s'...\\n"]="  Gebruiker '%s' wordt aangemaakt...
"
  ["  User '%s' created (uid=%s)\\n"]="  Gebruiker '%s' aangemaakt (uid=%s)
"
  ["  Log in as '%s' after reboot.\\n"]="  Meld je na de herstart aan als '%s'.
"
  ["  Type '%s' to get started.\\n"]="  Typ '%s' om te beginnen.
"
  ["Install SynapseOS"]="SynapseOS installeren"
  ["SynapseOS packages"]="SynapseOS-pakketten"
  ["Everything the system is made of. What you cannot drop is what something else you kept depends on — those are turned back on and named before anything is installed."]="Alles waaruit het systeem bestaat. Wat je niet kunt weglaten, is waar iets anders dat je hebt gehouden van afhangt — dat wordt weer aangezet en genoemd voordat er iets wordt geïnstalleerd."
  ["SYNAPSE UI — the Wayland desktop"]="SYNAPSE UI — het Wayland-bureaublad"
  ["synapd — the local AI daemon"]="synapd — de lokale AI-dienst"
  ["synsh — the AI-native shell"]="synsh — de AI-native shell"
  ["synguard + kernel module"]="synguard + kernelmodule"
  ["synnet — network policy"]="synnet — netwerkregels"
  ["Software — the package manager"]="Software — het pakketbeheer"
  ["Files — the file manager"]="Bestanden — de bestandsbeheerder"
  ["Terminal (synui depends on it)"]="Terminal (synui heeft die nodig)"
  ["Settings"]="Instellingen"
  ["Disks"]="Schijven"
  ["Editor"]="Editor"
  ["Calendar"]="Agenda"
  ["File Vault — a locked folder"]="Kluis — een vergrendelde map"
  ["Disk Cleanup — caches, and secure delete"]="Schijfopruiming — caches en veilig wissen"
  ["syn-update — how fixes arrive"]="syn-update — hoe verbeteringen binnenkomen"
  ["syn — the top-level CLI"]="syn — de bovenste opdrachtregel"
  ["syn-model — fetch AI models"]="syn-model — AI-modellen ophalen"
  ["syn-confine — the sandbox"]="syn-confine — de sandbox"
  ["fetch — the About OS readout"]="fetch — het systeemoverzicht"
  ["Arcade — overlay, pads, big screen"]="Arcade — overlay, controllers, groot scherm"
  ["cliamp — the music player"]="cliamp — de muziekspeler"
  ["Player — playlists, shuffle and history, on mpv"]="Player — afspeellijsten, willekeurig en geschiedenis, op mpv"
  ["Studio — photo darkroom and video"]="Studio — fotodonkerekamer en video"
  ["GeForce NOW — cloud gaming in a browser"]="GeForce NOW — cloudgamen in een browser"
  ["Arsenal — BlackArch browser"]="Arsenal — door BlackArch bladeren"
  ["Chibi — voice companion"]="Chibi — spraakmaatje"
  ["Vibe — AI coding assistant"]="Vibe — AI-programmeerassistent"
  ["Animated wallpapers (~317 MB)"]="Bewegende achtergronden (~317 MB)"
  ["Nexus Chat (pulls in Firefox)"]="Nexus Chat (haalt Firefox mee)"
  ["TEPRIS (pulls in Firefox)"]="TEPRIS (haalt Firefox mee)"
  ["Web and communication"]="Web en communicatie"
  ["None of this is ours; every name is in the Arch repositories. Firefox is on by default because an installed SynapseOS used to arrive with no browser at all."]="Niets hiervan is van ons; elke naam staat in de Arch-repository's. Firefox staat standaard aan omdat een geïnstalleerde SynapseOS vroeger zonder browser aankwam."
  ["Thunderbird — mail"]="Thunderbird — e-mail"
  ["KeePassXC — passwords"]="KeePassXC — wachtwoorden"
  ["Syncthing — file sync"]="Syncthing — bestandssynchronisatie"
  ["LocalSend — send to phone (Flatpak)"]="LocalSend — naar de telefoon sturen (Flatpak)"
  ["Audio and video"]="Audio en video"
  ["Office and graphics"]="Kantoor en grafisch"
  ["Development and admin"]="Ontwikkeling en beheer"
  ["VS Code (OSS build)"]="VS Code (OSS-build)"
  ["7zip + unrar"]="7zip + unrar"
  ["Games, launchers and helpers"]="Spellen, starters en hulpjes"
  ["Steam is in the options below rather than here: it is the only one that turns on a second architecture and a third repository."]="Steam staat hieronder bij de opties in plaats van hier: het is het enige dat een tweede architectuur en een derde repository aanzet."
  ["Prism — Minecraft"]="Prism — Minecraft"
  ["Dolphin — GameCube/Wii"]="Dolphin — GameCube/Wii"
  ["PPSSPP — PSP"]="PPSSPP — PSP"
  ["Space Cadet Pinball (Flatpak)"]="Space Cadet Pinball (Flatpak)"
  ["GOverlay — MangoHud"]="GOverlay — MangoHud"
  ["AntiMicroX — pad remap"]="AntiMicroX — controllers opnieuw indelen"
  ["Welcome"]="Welkom"
  ["Disk"]="Schijf"
  ["Software"]="Software"
  ["Account"]="Account"
  ["Region"]="Regio"
  ["Summary"]="Overzicht"
  ["Install"]="Installatie"
  ["the installer's own media"]="de eigen schijf van de installer"
  ["%1 GiB — SynapseOS needs at least %2 GiB"]="%1 GiB — SynapseOS heeft minstens %2 GiB nodig"
  ["No connection. SynapseOS downloads the base system while it installs, so this needs a working network before it can start."]="Geen verbinding. SynapseOS haalt het basissysteem op tijdens het installeren, dus hiervoor is een werkend netwerk nodig voordat het kan beginnen."
  ["Choose a disk to install to."]="Kies een schijf om op te installeren."
  ["The encryption passphrase needs at least 8 characters."]="De versleutelwachtwoordzin heeft minstens 8 tekens nodig."
  ["With neither the package manager nor the desktop, this install has no way to add either one back. Keep at least one."]="Zonder pakketbeheer en zonder bureaublad heeft deze installatie geen manier om er een van beide bij te zetten. Houd er minstens één."
  ["A username is lower-case letters, digits, - and _, and cannot start with a digit."]="Een gebruikersnaam bestaat uit kleine letters, cijfers, - en _, en mag niet met een cijfer beginnen."
  ["Set a password for the account."]="Stel een wachtwoord in voor het account."
  ["The two passwords do not match."]="De twee wachtwoorden komen niet overeen."
  ["A locale is needed, e.g. en_US.UTF-8."]="Er is een locale nodig, bijv. nl_NL.UTF-8."
  ["A timezone is needed, e.g. Europe/Lisbon."]="Er is een tijdzone nodig, bijv. Europe/Amsterdam."
  ["printing"]="afdrukken"
  ["%1 repo"]="%1-repo"
  ["Mode"]="Modus"
  ["Filesystem"]="Bestandssysteem"
  ["%1 on LUKS2"]="%1 op LUKS2"
  ["%1 + snapshots"]="%1 + momentopnamen"
  ["none"]="geen"
  ["Desktop"]="Bureaublad"
  ["Locale"]="Locale"
  ["%1   keys %2 / %3"]="%1   toetsen %2 / %3"
  ["Timezone"]="Tijdzone"
  ["%1 package(s) — WITHOUT %2"]="%1 pakket(ten) — ZONDER %2"
  ["%1 package(s)"]="%1 pakket(ten)"
  ["Options"]="Opties"
  ["Could not write the install profile."]="Het installatieprofiel kon niet worden geschreven."
  ["Installation complete."]="Installatie voltooid."
  ["Installation failed — see the log."]="Installatie mislukt — zie het logboek."
  ["No network connection"]="Geen netwerkverbinding"
  ["The base system is downloaded while it installs, so this cannot start offline. Plug in a cable or join a network, then press Re-check — the answers on these pages are kept."]="Het basissysteem wordt tijdens het installeren opgehaald, dus dit kan niet offline beginnen. Sluit een kabel aan of ga op een netwerk, en druk dan op Opnieuw controleren — de antwoorden op deze pagina's blijven staan."
  ["Checking…"]="Bezig met controleren…"
  ["Re-check"]="Opnieuw controleren"
  ["Wi-Fi settings"]="Wifi-instellingen"
  ["This asks for a disk, an account and a few preferences, then hands the answers to the same installer the text version runs. Nothing is written to any disk until the last page, and that page says exactly what it is about to do."]="Hier worden een schijf, een account en een paar voorkeuren gevraagd, en daarna gaan de antwoorden naar dezelfde installer die de tekstversie draait. Er wordt naar geen enkele schijf geschreven tot de laatste pagina, en die pagina zegt precies wat er gaat gebeuren."
  ["A disk is partitioned and formatted"]="Een schijf wordt gepartitioneerd en geformatteerd"
  ["The base system and the SynapseOS packages are installed"]="Het basissysteem en de SynapseOS-pakketten worden geïnstalleerd"
  ["An account and a desktop are set up"]="Een account en een bureaublad worden ingericht"
  ["A bootloader is written"]="Er wordt een bootloader geschreven"
  ["Partitioning an existing layout by hand is the text installer's ADVANCED mode — quit this and run \`syn-install\` in a terminal for that."]="Een bestaande indeling met de hand partitioneren is de ADVANCED-modus van de tekstinstaller — sluit dit daarvoor en voer \`syn-install\` uit in een terminal."
  ["Where should SynapseOS go?"]="Waar moet SynapseOS heen?"
  ["The installer's own media is listed and cannot be chosen."]="De eigen schijf van de installer staat in de lijst en kan niet worden gekozen."
  ["No disks found."]="Geen schijven gevonden."
  ["Erase the disk"]="De schijf wissen"
  ["every partition and all data"]="elke partitie en alle gegevens"
  ["Install alongside"]="Ernaast installeren"
  ["use free space, UEFI only"]="vrije ruimte gebruiken, alleen UEFI"
  ["Snapshots"]="Momentopnamen"
  ["btrfs + limine only"]="alleen btrfs + limine"
  ["Encrypt the disk"]="De schijf versleutelen"
  ["Passphrase"]="Wachtwoordzin"
  ["8 characters or more"]="8 tekens of meer"
  ["What should be installed?"]="Wat moet er worden geïnstalleerd?"
  ["The SynapseOS core — the compositor, the daemons and the applications it is built on — is installed by every choice here."]="De kern van SynapseOS — de compositor, de diensten en de toepassingen waarop hij is gebouwd — wordt bij elke keuze hier geïnstalleerd."
  ["Full"]="Volledig"
  ["Standard + Steam + Nix + more software"]="Standaard + Steam + Nix + meer software"
  ["Standard"]="Standaard"
  ["the SynapseOS suite, Firefox, AI model, Bluetooth, printing, Wine, phone"]="de SynapseOS-suite, Firefox, AI-model, bluetooth, afdrukken, Wine, telefoon"
  ["Minimal"]="Minimaal"
  ["core daemons only — no apps, no software, no model"]="alleen de kerndiensten — geen apps, geen software, geen model"
  ["Custom"]="Eigen keuze"
  ["tick every package yourself, ours and the ordinary software"]="elk pakket zelf aanvinken, dat van ons en de gewone software"
  ["(required)"]="(vereist)"
  ["Not packages: a repository, an architecture or a service. Each is a decision with a consequence that does not fit on a checkbox above."]="Geen pakketten: een repository, een architectuur of een dienst. Elk is een beslissing met een gevolg dat niet op een vakje hierboven past."
  ["Printing (CUPS)"]="Afdrukken (CUPS)"
  ["Wine — run Windows .exe/.msi"]="Wine — Windows-.exe/.msi draaien"
  ["KDE Connect — pair a phone"]="KDE Connect — een telefoon koppelen"
  ["Steam + game stack + Proton (~3.1 GB)"]="Steam + spellenstapel + Proton (~3,1 GB)"
  ["BlackArch repo — ~5000 tools, none installed"]="BlackArch-repo — ~5000 gereedschappen, geen ervan geïnstalleerd"
  ["Nix + Home Manager"]="Nix + Home Manager"
  ["syn-update is off: this machine will have no way to receive another SynapseOS package. Fixing that later means installing it by hand from the ISO, or reinstalling."]="syn-update staat uit: deze machine kan geen enkel SynapseOS-pakket meer ontvangen. Dat later herstellen betekent het met de hand vanaf de ISO installeren, of opnieuw installeren."
  ["synui is off: this will not be a SynapseOS desktop. The Desktop page offers KDE, GNOME or no GUI."]="synui staat uit: dit wordt geen SynapseOS-bureaublad. De pagina Bureaublad biedt KDE, GNOME of helemaal geen grafische schil."
  ["AI model — downloaded during the install"]="AI-model — tijdens de installatie opgehaald"
  ["~4.1 GB — recommended"]="~4,1 GB — aanbevolen"
  ["~2.2 GB — weaker"]="~2,2 GB — zwakker"
  ["~0.4 GB — much weaker"]="~0,4 GB — veel zwakker"
  ["None"]="Geen"
  ["AI stays inert"]="AI blijft werkloos"
  ["NVIDIA GPU inference"]="NVIDIA-GPU-inferentie"
  ["the CUDA runtime, ~4.7 GiB"]="de CUDA-runtime, ~4,7 GiB"
  ["Who is this machine for?"]="Voor wie is deze machine?"
  ["Username"]="Gebruikersnaam"
  ["lower-case, no spaces"]="kleine letters, geen spaties"
  ["Full name (optional)"]="Volledige naam (optioneel)"
  ["Password"]="Wachtwoord"
  ["Password again"]="Wachtwoord nogmaals"
  ["They do not match"]="Ze komen niet overeen"
  ["the native compositor"]="de eigen compositor"
  ["synui is not selected"]="synui is niet geselecteerd"
  ["headless"]="zonder grafische schil"
  ["Language, keyboard and time"]="Taal, toetsenbord en tijd"
  ["Pick a language and the other three follow it. The console keymap and the desktop layout are separate on purpose — Swedish is 'sv-latin1' to the console and 'se' to the desktop — so they can be changed on their own afterwards."]="Kies een taal en de andere drie volgen. De console-toetsindeling en de bureaubladindeling staan met opzet los van elkaar — Zweeds heet 'sv-latin1' op de console en 'se' op het bureaublad — zodat ze achteraf apart te wijzigen zijn."
  ["Language"]="Taal"
  ["sets the keyboard and the fonts too"]="zet ook het toetsenbord en de lettertypen"
  ["typed by hand — fonts cover as much as possible"]="met de hand ingetypt — de lettertypen dekken zoveel mogelijk"
  ["Sets the locale, both keyboard names and the font pack. Any locale glibc has can be typed instead."]="Zet de locale, allebei de toetsenbordnamen en het lettertypepakket. In plaats daarvan kan elke locale worden getypt die glibc kent."
  ["The common zones first, then every name tzdata ships."]="Eerst de gangbare zones, daarna elke naam die tzdata meebrengt."
  ["Console keymap"]="Console-toetsindeling"
  ["loadkeys — the text console and the greeter"]="loadkeys — de tekstconsole en het aanmeldscherm"
  ["Every keymap this image can load. This one names a file loadkeys has to find, which is why it is not the same list as the desktop layout."]="Elke toetsindeling die dit image kan laden. Deze noemt een bestand dat loadkeys moet vinden, en daarom is het niet dezelfde lijst als de bureaubladindeling."
  ["Desktop layout"]="Bureaubladindeling"
  ["XKB — the compositor"]="XKB — de compositor"
  ["Desktop keyboard layout"]="Toetsenbordindeling van het bureaublad"
  ["The layouts xkbcommon can compile. 'uk' is a console keymap and not a layout here — the layout is 'gb'."]="De indelingen die xkbcommon kan compileren. 'uk' is een console-toetsindeling en hier geen indeling — de indeling heet 'gb'."
  ["Read this back"]="Alles nog eens nalezen"
  ["Nothing has been written yet. The next button is the one that starts."]="Er is nog niets geschreven. De volgende knop is degene die begint."
  ["EVERY PARTITION ON %1 WILL BE DELETED"]="ELKE PARTITIE OP %1 WORDT VERWIJDERD"
  ["SynapseOS will be installed into the free space on %1"]="SynapseOS wordt in de vrije ruimte op %1 geïnstalleerd"
  ["SynapseOS is installed"]="SynapseOS is geïnstalleerd"
  ["The install stopped"]="De installatie is gestopt"
  ["Installing SynapseOS"]="SynapseOS wordt geïnstalleerd"
  ["Reboot and remove the installation media."]="Start opnieuw op en haal het installatiemedium eruit."
  ["The log below is the whole story — the last lines say why."]="Het logboek hieronder vertelt het hele verhaal — de laatste regels zeggen waarom."
  ["This takes a while: the base system and the packages are downloaded, and an AI model is gigabytes on its own."]="Dit duurt even: het basissysteem en de pakketten worden opgehaald, en een AI-model is op zichzelf al gigabytes."
  ["Back"]="Terug"
  ["Next"]="Verder"
  ["Reboot"]="Opnieuw opstarten"
  ["Close"]="Sluiten"
  ["type to filter, or type a name that is not listed"]="typ om te filteren, of typ een naam die er niet bij staat"
  ["Nothing to list on this image — type the name instead."]="Op dit image valt er niets op te sommen — typ in plaats daarvan de naam."
  ["Nothing matches — the row below uses what you typed."]="Niets komt overeen — de regel hieronder gebruikt wat je hebt getypt."
  ["Use “%1” as typed"]="“%1” gebruiken zoals getypt"
)
