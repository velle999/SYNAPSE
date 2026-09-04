# Italiano (it) — syn-install's own words.
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
  ["render.nix is missing — the 'syn' package is not installed here."]="render.nix manca — il pacchetto 'syn' non è installato qui."
  ["locale-gen failed. The live session stays in English; the install is
  unaffected, because it generates the locale inside the target."]="locale-gen è fallito. La sessione live resta in inglese; l'installazione
  non ne risente, perché genera la locale dentro il sistema di destinazione."
  ["  The keyboard, the clock, the fonts and the shell all follow this.
  You can change any of it later."]="  La tastiera, l'ora, i caratteri e la shell seguono tutti questa scelta.
  Tutto si può cambiare più tardi."
  ["Toggle [numbers, 'all', 'none', Enter = accept]:"]="Commuta [numeri, 'all', 'none', Invio = accetta]:"
  ["--config needs a file"]="--config ha bisogno di un file"
  ["syn-install must be run as root"]="syn-install va eseguito come root"
  ["  SynapseOS is running from the live image."]="  SynapseOS sta girando dall'immagine live."
  ["Starting the desktop — the installer opens with it."]="Avvio del desktop — l'installatore si apre con lui."
  ["  This installer will:
    1. Partition a disk
    2. Install SynapseOS base system
    3. Install SynapseOS packages
    4. Create user account
    5. Choose desktop environment
    6. Configure system & bootloader"]="  Questo installatore farà:
    1. Partizionare un disco
    2. Installare il sistema base di SynapseOS
    3. Installare i pacchetti di SynapseOS
    4. Creare un account utente
    5. Scegliere un ambiente desktop
    6. Configurare il sistema e il bootloader"
  ["ALL DATA ON THE TARGET DISK WILL BE ERASED"]="TUTTI I DATI SUL DISCO DI DESTINAZIONE SARANNO CANCELLATI"
  ["Press ENTER to continue or Ctrl+C to abort..."]="Premi INVIO per continuare o Ctrl+C per interrompere..."
  ["Checking network"]="Controllo della rete"
  ["Network is up"]="La rete è attiva"
  ["  No network detected. Starting NetworkManager..."]="  Nessuna rete rilevata. Avvio di NetworkManager..."
  ["  No connection — but this machine has Wi-Fi."]="  Nessuna connessione — ma questa macchina ha il Wi-Fi."
  ["Open the Wi-Fi picker (nmtui)? [Y/n]:"]="Aprire il selettore Wi-Fi (nmtui)? [Y/n]:"
  ["No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."]="Nessuna connessione di rete e nessun dispositivo Wi-Fi da configurare.
  SynapseOS scarica il sistema base durante l'installazione: collega un cavo
  e riavvia l'installatore."
  ["Network connected"]="Rete connessa"
  ["Step 1 — Select Target Disk"]="Passo 1 — Scegli il disco di destinazione"
  ["  Available disks:"]="  Dischi disponibili:"
  ["Target disk (e.g. sda, vda, nvme0n1):"]="Disco di destinazione (es. sda, vda, nvme0n1):"
  ["Target disk is in use. Unmount its partitions and re-run."]="Il disco di destinazione è in uso. Smonta le sue partizioni e riavvia."
  ["Boot mode: UEFI"]="Modalità di avvio: UEFI"
  ["Boot mode: BIOS/Legacy"]="Modalità di avvio: BIOS/Legacy"
  ["  Encrypts the root filesystem with LUKS2. You will be asked for the
  passphrase at every boot, before the system starts."]="  Cifra il filesystem radice con LUKS2. La passphrase verrà chiesta a ogni
  avvio, prima che il sistema parta."
  ["Encrypt the disk? [y/N]:"]="Cifrare il disco? [y/N]:"
  ["                          With encryption it is the BETTER choice: the
                          kernel lives on the EFI partition and only the
                          initramfs unlocks, so /boot needs no separate
                          unencrypted partition."]="                          Con la cifratura è la scelta MIGLIORE: il kernel
                          sta sulla partizione EFI e solo l'initramfs
                          sblocca, quindi /boot non ha bisogno di una
                          partizione non cifrata separata."
  ["                          It copies each snapshot's kernel onto the EFI
                          partition, so that partition is made much
                          larger when snapshots are enabled."]="                          Copia il kernel di ogni istantanea sulla
                          partizione EFI, quindi quella partizione viene
                          creata molto più grande con le istantanee attive."
  ["  Snapshots are cheap but not free: they hold the old copy of
  anything that changes, so a disk near full stays near full."]="  Le istantanee costano poco ma non nulla: tengono la vecchia copia di
  tutto ciò che cambia, quindi un disco quasi pieno resta quasi pieno."
  ["Enable snapshots? [Y/n]:"]="Attivare le istantanee? [Y/n]:"
  ["mkfs.ext4 is missing from this installer image — /boot cannot be created"]="mkfs.ext4 manca in questa immagine dell'installatore — /boot non può essere creato"
  ["btrfs is missing from this installer image — subvolumes cannot be created"]="btrfs manca in questa immagine dell'installatore — i subvolumi non possono essere creati"
  ["Are these correct? [Y/n]:"]="È corretto? [Y/n]:"
  ["Starting the questions over — the disk has not been touched."]="Si ricominciano le domande — il disco non è stato toccato."
  ["cryptsetup is not available on this installer image"]="cryptsetup non è disponibile su questa immagine dell'installatore"
  ["Encryption passphrase:"]="Passphrase di cifratura:"
  ["Repeat passphrase:"]="Ripeti la passphrase:"
  ["Empty passphrase — that would leave the disk unprotected."]="Passphrase vuota — lascerebbe il disco senza protezione."
  ["Passphrases did not match — try again."]="Le passphrase non coincidono — riprova."
  ["Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."]="La passphrase ha meno di 8 caratteri. Una corta vale poco
  contro chi ha il disco tra le mani."
  ["Use it anyway? [y/N]:"]="Usarla lo stesso? [y/N]:"
  ["Encryption enabled — root will be LUKS2"]="Cifratura attiva — la radice sarà LUKS2"
  ["cryptsetup open failed — the passphrase did not take"]="cryptsetup open è fallito — la passphrase non è stata accettata"
  ["Failed to mount root"]="Montaggio della radice fallito"
  ["  Creating btrfs subvolumes..."]="  Creazione dei subvolumi btrfs..."
  ["btrfs: could not create @"]="btrfs: impossibile creare @"
  ["btrfs: could not create @home"]="btrfs: impossibile creare @home"
  ["btrfs: could not create @snapshots"]="btrfs: impossibile creare @snapshots"
  ["btrfs: could not create @var_log"]="btrfs: impossibile creare @var_log"
  ["btrfs: could not create @pkg"]="btrfs: impossibile creare @pkg"
  ["could not remount the btrfs root onto @"]="impossibile rimontare la radice btrfs su @"
  ["Failed to mount @"]="Montaggio di @ fallito"
  ["Failed to mount @home"]="Montaggio di @home fallito"
  ["Failed to mount @snapshots"]="Montaggio di @snapshots fallito"
  ["Failed to mount @var_log"]="Montaggio di @var_log fallito"
  ["Failed to mount @pkg"]="Montaggio di @pkg fallito"
  ["This adds one partition in the free space. Back up anything irreplaceable first."]="Questo aggiunge una partizione nello spazio libero. Prima fai una copia di tutto ciò che è insostituibile."
  ["Type 'yes' to install alongside:"]="Scrivi 'yes' per installare accanto:"
  ["Aborted"]="Interrotto"
  ["Failed to create the root partition"]="Creazione della partizione radice fallita"
  ["Could not identify the new partition after creating it"]="Impossibile identificare la nuova partizione dopo averla creata"
  ["Failed to format root partition"]="Formattazione della partizione radice fallita"
  ["Failed to mount the existing ESP"]="Montaggio della ESP esistente fallito"
  ["no partition editor on this image (cfdisk, fdisk and parted are all missing)"]="nessun editor di partizioni su questa immagine (mancano cfdisk, fdisk e parted)"
  ["  What this install needs:"]="  Cosa serve a questa installazione:"
  ["    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"]="    • una partizione di sistema EFI (tipo EF00 / flag 'esp') — se ne può riusare una esistente"
  ["  Skipping the partition editor (--config)."]="  Editor di partizioni saltato (--config)."
  ["Format it? Everything on it is lost [y/N]:"]="Formattarla? Tutto ciò che contiene va perso [y/N]:"
  ["Separate /boot partition:"]="Partizione /boot separata:"
  ["Swap partition (blank for none):"]="Partizione di swap (vuoto per nessuna):"
  ["Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"]="Rifarla? Il suo UUID cambia e rompe la fstab di quel sistema [y/N]:"
  ["Type 'yes' to format these:"]="Scrivi 'yes' per formattarle:"
  ["  Formatting EFI partition..."]="  Formattazione della partizione EFI..."
  ["  Formatting /boot partition..."]="  Formattazione della partizione /boot..."
  ["Failed to mount /boot"]="Montaggio di /boot fallito"
  ["Type 'yes' to confirm:"]="Scrivi 'yes' per confermare:"
  ["  Creating GPT partition table..."]="  Creazione della tabella delle partizioni GPT..."
  ["Failed to format EFI partition"]="Formattazione della partizione EFI fallita"
  ["Failed to format boot partition"]="Formattazione della partizione di avvio fallita"
  ["  Creating MBR partition table..."]="  Creazione della tabella delle partizioni MBR..."
  ["Disk partitioned and mounted at /mnt"]="Disco partizionato e montato su /mnt"
  ["Step 3 — Installing Base System"]="Passo 3 — Installazione del sistema base"
  ["  Initializing pacman keyring..."]="  Inizializzazione del portachiavi di pacman..."
  ["  Running pacstrap (this may take several minutes)..."]="  pacstrap in corso (può richiedere diversi minuti)..."
  ["pacstrap failed — check network connection"]="pacstrap è fallito — controlla la connessione di rete"
  ["grub-install not found in chroot — attempting recovery..."]="grub-install non trovato nel chroot — tentativo di recupero..."
  ["Could not install grub into target — check network"]="Impossibile installare grub nel sistema di destinazione — controlla la rete"
  ["Base system installed"]="Sistema base installato"
  ["Step 4 — Choose What to Install"]="Passo 4 — Scegli cosa installare"
  ["  What should be installed alongside the SynapseOS core?"]="  Che cosa va installato accanto al nucleo di SynapseOS?"
  ["                   Bluetooth, printing, Wine, phone   (default)"]="                   Bluetooth, stampa, Wine, telefono   (predefinito)"
  ["                   the ordinary software people install anyway"]="                   il software comune che si installa comunque"
  ["  Every preset except Minimal then asks WHICH AI model to download,
  and skipping it is one of the answers."]="  Ogni preselezione tranne Minimale chiede poi QUALE modello di IA
  scaricare, e saltarlo è una delle risposte."
  ["Full install selected"]="Installazione completa scelta"
  ["Minimal install selected"]="Installazione minimale scelta"
  ["  Two kinds of question. First the packages, as pages of
  checkboxes; then the handful of options that are a whole
  subsystem rather than a package."]="  Due tipi di domanda. Prima i pacchetti, in pagine di
  caselle; poi la manciata di opzioni che sono un intero
  sottosistema e non un pacchetto."
  ["  And the software people install on the first evening anyway.
  All of it is in the Arch repositories; none of it is ours."]="  E il software che si installa comunque la prima sera.
  È tutto nei repository di Arch; niente di questo è nostro."
  ["  The rest is y/n. The default (shown in caps) is Standard."]="  Il resto è s/n. Il valore predefinito (in maiuscolo) è Standard."
  ["syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."]="syn-update deselezionato: questa macchina non avrà modo di ricevere
  un altro pacchetto SynapseOS. Rimediare più tardi significa installarlo a mano
  dalla ISO, o reinstallare."
  ["Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."]="Non sono stati tenuti né il desktop né il demone di IA. Quello è un sistema
  Arch con qualche strumento SynapseOS sopra, il che è una risposta ammessa —
  ma niente nella documentazione descriverà la macchina che ottieni."
  ["Custom install configured"]="Installazione personalizzata configurata"
  ["Standard install selected"]="Installazione standard scelta"
  ["  synapd loads one model and everything AI in SynapseOS talks to it:
  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,
  over this connection, onto the disk you are installing to."]="  synapd carica un modello e tutto ciò che è IA in SynapseOS parla con lui:
  synsh, il pannello IA del desktop, Chibi, Vibe. Viene scaricato adesso,
  su questa connessione, sul disco su cui stai installando."
  ["  A smaller model is not just faster and lighter: it follows
  instructions worse. synsh mistakes what you asked for, Vibe's code
  needs more fixing, Chibi loses the thread. Take the default unless
  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."]="  Un modello più piccolo non è solo più veloce e leggero: segue peggio
  le istruzioni. synsh fraintende ciò che hai chiesto, il codice di Vibe
  va corretto di più, Chibi perde il filo. Prendi il predefinito a meno che
  il disco o la RAM dicano altro — 7B vuole ~6 GB di RAM libera."
  ["  Whatever you pick, it can be changed later: 'syn model download',
  or Super+C ▸ System ▸ AI model on the desktop."]="  Qualunque cosa scegli, si può cambiare dopo: 'syn model download',
  o Super+C ▸ Sistema ▸ Modello di IA sul desktop."
  ["Install this selection? [Y/n]:"]="Installare questa selezione? [Y/n]:"
  ["Choosing again — nothing has been installed yet."]="Si sceglie di nuovo — non è stato installato ancora nulla."
  ["Step 4b — Installing SynapseOS"]="Passo 4b — Installazione di SynapseOS"
  ["Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."]="Impossibile attivare ILoveCandy in /etc/pacman.conf (solo estetico)."
  ["  Enabling [multilib] (32-bit repo, needed by Steam)..."]="  Attivazione di [multilib] (repository a 32 bit, serve a Steam)..."
  ["Could not sync the multilib database — Steam may fail to install"]="Impossibile sincronizzare il database multilib — Steam potrebbe non installarsi"
  ["Could not enable [multilib]; Steam will be skipped."]="Impossibile attivare [multilib]; Steam verrà saltato."
  ["Some SynapseOS packages failed to install — verifying below"]="Alcuni pacchetti SynapseOS non si sono installati — verifica sotto"
  ["No SynapseOS packages were selected. This will be an Arch system."]="Non è stato selezionato nessun pacchetto SynapseOS. Questo sarà un sistema Arch."
  ["SynapseOS packages installed"]="Pacchetti SynapseOS installati"
  ["Component selection recorded in /etc/synapseos/components.conf"]="Selezione dei componenti registrata in /etc/synapseos/components.conf"
  ["Step 5 — Create User Account"]="Passo 5 — Crea l'account utente"
  ["  Create a user account for the installed system."]="  Crea un account utente per il sistema installato."
  ["Username [default: syn]:"]="Nome utente [predefinito: syn]:"
  ["Full name (optional):"]="Nome completo (facoltativo):"
  ["Password:"]="Password:"
  ["Confirm password:"]="Conferma la password:"
  ["Passwords do not match or are empty — try again"]="Le password non coincidono o sono vuote — riprova"
  ["Step 6 — Desktop Environment"]="Passo 6 — Ambiente desktop"
  ["  Choose a desktop environment:"]="  Scegli un ambiente desktop:"
  ["  Installing KDE Plasma..."]="  Installazione di KDE Plasma..."
  ["Some KDE packages failed to install"]="Alcuni pacchetti KDE non si sono installati"
  ["KDE Plasma installed"]="KDE Plasma installato"
  ["  Installing GNOME..."]="  Installazione di GNOME..."
  ["Some GNOME packages failed to install"]="Alcuni pacchetti GNOME non si sono installati"
  ["GNOME installed (session only — SynapseOS apps, not GNOME's)"]="GNOME installato (solo la sessione — le applicazioni di SynapseOS, non quelle di GNOME)"
  ["  Installing greetd (login screen) + desktop extras..."]="  Installazione di greetd (schermata di accesso) + extra del desktop..."
  ["greetd failed to install — boot falls back to getty login"]="greetd non si è installato — l'avvio ricade sull'accesso via getty"
  ["SynapseUI selected (included)"]="SynapseUI scelto (incluso)"
  ["Installing Wine"]="Installazione di Wine"
  ["Wine installed"]="Wine installato"
  ["wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."]="wine non si è installato — gli .exe/.msi di Windows non funzioneranno.
  Installalo più tardi con 'sudo pacman -S wine wine-mono'."
  ["Configuring Video Driver"]="Configurazione del driver video"
  ["  Virtual machine — installing mesa (synui uses pixman here)..."]="  Macchina virtuale — installazione di mesa (qui synui usa pixman)..."
  ["mesa failed to install"]="mesa non si è installato"
  ["NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"]="L'installazione del driver NVIDIA è fallita — il sistema si avvierebbe su
  nouveau e il renderer di synui non partirebbe mai"
  ["NVIDIA sleep services enabled (VRAM save/restore)"]="Servizi di sospensione NVIDIA attivati (salvataggio/ripristino della VRAM)"
  ["Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"]="Impossibile attivare nvidia-{suspend,resume,hibernate} — la sospensione
  può dare schermo nero se NVreg_PreserveVideoMemoryAllocations viene attivato dopo"
  ["  synapd can run inference on this GPU instead of the CPU.
  This downloads the CUDA runtime (~4.7 GiB installed)."]="  synapd può fare l'inferenza su questa GPU invece che sulla CPU.
  Questo scarica l'ambiente CUDA (~4,7 GiB installati)."
  ["Enable GPU inference? [Y/n]:"]="Attivare l'inferenza su GPU? [Y/n]:"
  ["Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda"]="Si resta sull'inferenza da CPU. Per cambiare più tardi:
  sudo pacman -S synapse-llama-cuda"
  ["  Installing synapse-llama-cuda (this takes a while)..."]="  Installazione di synapse-llama-cuda (ci vuole un po')..."
  ["This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"]="Questa ISO non contiene una build GPU di llama, quindi synapd girerà sulla CPU
  nonostante la scheda NVIDIA. (La ISO va costruita su un host con il toolkit
  CUDA perché synapse-llama-cuda esista.)"
  ["Video driver install failed — synui may fall back to software rendering"]="L'installazione del driver video è fallita — synui potrebbe ricadere sul rendering software"
  ["Video drivers installed"]="Driver video installati"
  ["  Enabling GPU inference (synapse-llama-vulkan)..."]="  Attivazione dell'inferenza su GPU (synapse-llama-vulkan)..."
  ["This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"]="Questa ISO non contiene una build Vulkan di llama, quindi synapd girerà sulla CPU
  nonostante la GPU AMD/Intel. (Costruisci la ISO su un host con 'shaderc' +
  vulkan-headers perché synapse-llama-vulkan esista.)"
  ["Installing Steam and the game stack"]="Installazione di Steam e dello stack di gioco"
  ["  Installing steam and the 32-bit runtime libraries..."]="  Installazione di steam e delle librerie a 32 bit..."
  ["Steam installed (native multilib package)"]="Steam installato (pacchetto multilib nativo)"
  ["steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."]="steam non si è installato. Per il resto il sistema è completo —
  installalo più tardi con 'sudo pacman -S steam' ([multilib] è già
  attivo in /etc/pacman.conf)."
  ["  Installing the game stack (overlay, governor, micro-compositor)..."]="  Installazione dello stack di gioco (overlay, governor, micro-compositor)..."
  ["Game stack installed (mangohud, gamemode, gamescope)"]="Stack di gioco installato (mangohud, gamemode, gamescope)"
  ["The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"]="Lo stack di gioco non si è installato. Steam funziona lo stesso; l'overlay
  degli FPS, il governor CPU/GPU e 'synui-game-run --gamescope' no.
  Installali più tardi con:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
  ["Installing CachyOS Proton"]="Installazione di CachyOS Proton"
  ["  Fetching the CachyOS keyring and mirrorlist..."]="  Recupero del portachiavi e della lista dei mirror di CachyOS..."
  ["  Trusting the CachyOS master key..."]="  La chiave madre di CachyOS viene ritenuta affidabile..."
  ["Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"]="Impossibile recuperare la chiave madre di CachyOS da keyserver.ubuntu.com.
  Senza di essa il portachiavi firmato non può essere installato, quindi CachyOS
  Proton viene saltato. Aggiungilo dopo con:  synpkg cachyos enable-repo"
  ["  Master key pinned as expected — trusting it..."]="  Chiave madre come previsto — viene ritenuta affidabile..."
  ["[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."]="[cachyos] è stato aggiunto ma non elenca pacchetti — viene rimosso
  di nuovo perché non blocchi un aggiornamento futuro."
  ["The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."]="Il portachiavi di CachyOS non porta la chiave madre attesa.
  Non le si dà fiducia — il repository NON è stato aggiunto."
  ["  Installing proton-cachyos-slr (~340 MB download)..."]="  Installazione di proton-cachyos-slr (~340 MB di download)..."
  ["CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."]="CachyOS Proton installato — scegli per gioco in Steam sotto Proprietà →
  Compatibilità, elencato come 'proton-cachyos-… (steam linux runtime)'.
  Steam lo cerca solo all'avvio: riavvialo se è già aperto."
  ["proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"]="proton-cachyos-slr non si è installato. Steam e il Proton di Valve
  non ne risentono. Installalo dopo con:
  sudo pacman -S proton-cachyos-slr"
  ["The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"]="Il repository [cachyos] non si è potuto attivare, quindi CachyOS Proton
  è stato saltato. Steam funziona lo stesso con il Proton di Valve. Per aggiungerlo dopo:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
  ["Enabling BlackArch"]="Attivazione di BlackArch"
  ["  Fetching the BlackArch bootstrap..."]="  Recupero del bootstrap di BlackArch..."
  ["  Master key pinned as expected — running bootstrap..."]="  Chiave madre come previsto — esecuzione del bootstrap..."
  ["blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."]="blackarch-keyring non si è installato — i cambi di chiave
  non arriveranno a questa macchina. Rimedia con 'sudo pacman -S blackarch-keyring'."
  ["The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."]="Lo strap.sh scaricato non fissa la chiave madre attesa di BlackArch.
  Non viene eseguito — il repository NON è stato aggiunto."
  ["BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."]="BlackArch non è stato attivato. Per il resto il sistema è completo;
  aggiungilo dopo con 'sudo syn arsenal --enable-repo'."
  ["Installing software"]="Installazione del software"
  ["That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."]="Quella transazione è fallita — ogni pacchetto viene ritentato da solo, così
  quelli a posto arrivano lo stesso e quello che non lo è viene nominato."
  ["Software installed"]="Software installato"
  ["Installing Flatpak apps"]="Installazione delle applicazioni Flatpak"
  ["flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."]="flatpak non si è potuto installare — le applicazioni Flatpak sono saltate.
  Nient'altro ne risente."
  ["Could not add the flathub remote"]="Impossibile aggiungere il remoto flathub"
  ["Flatpak apps installed"]="Applicazioni Flatpak installate"
  ["Configuring System"]="Configurazione del sistema"
  ["  fstab generated"]="  fstab generata"
  ["Swap recorded in fstab"]="Swap registrato nella fstab"
  ["zram configured (compressed swap, half of RAM up to 8 GiB)"]="zram configurato (swap compresso, metà della RAM fino a 8 GiB)"
  ["zram-generator is not installed in the target — no compressed swap"]="zram-generator non è installato nel sistema di destinazione — nessuno swap compresso"
  ["  Hostname: synapse"]="  Nome host: synapse"
  ["Step 7 — Language & Region"]="Passo 7 — Lingua e regione"
  ["   0) Other — enter a locale by hand"]="   0) Altro — inserire una locale a mano"
  ["Locale (e.g. sv_SE.UTF-8):"]="Locale (es. sv_SE.UTF-8):"
  ["Console keymap (e.g. sv-latin1):"]="Mappa di tastiera della console (es. sv-latin1):"
  ["Step 8 — Timezone"]="Passo 8 — Fuso orario"
  ["   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"]="   0) Altro — inserire un qualunque nome tzdata (es. Europe/Lisbon)"
  ["tzdata name (Region/City):"]="Nome tzdata (Regione/Città):"
  ["  Did you mean:"]="  Intendevi:"
  ["  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"]="  Scegli un numero dall'elenco, oppure vedi: ls /mnt/usr/share/zoneinfo"
  ["  os-release: copied from live system"]="  os-release: copiato dal sistema live"
  ["  issue: copied from live system"]="  issue: copiato dal sistema live"
  ["Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"]="Il filesystem di destinazione non è più scrivibile (errori del disco? guarda 'dmesg') — interrotto"
  ["sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"]="Il set di regole sudoers non è valido dopo la scrittura dei drop-in — non si consegna un sistema che non può usare sudo"
  ["Could not relax pam_faillock in /etc/pam.d/system-auth (a tty-less sudo could still lock the account until reboot)."]="Impossibile allentare pam_faillock in /etc/pam.d/system-auth (un sudo senza tty potrebbe ancora bloccare l'account fino al riavvio)."
  ["could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"]="impossibile pre-creare /var/lib/synapse-src — l'aggiornatore chiederà una password al primo avvio"
  ["  Desktop: KDE Plasma (SDDM login screen)"]="  Desktop: KDE Plasma (schermata di accesso SDDM)"
  ["  GDM: SynapseOS logo on the login screen"]="  GDM: logo SynapseOS nella schermata di accesso"
  ["  Desktop: GNOME (GDM login screen)"]="  Desktop: GNOME (schermata di accesso GDM)"
  ["  Desktop: TTY only"]="  Desktop: solo TTY"
  ["  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"]="  Desktop: SynapseUI (greeter di synui — l'accesso rispecchia il blocco schermo)"
  ["  motd: written for this installation"]="  motd: scritto per questa installazione"
  ["  note: syn-rgb.path is not installed; RGB lights stay off"]="  nota: syn-rgb.path non è installato; le luci RGB restano spente"
  ["AI model"]="Modello di IA"
  ["  AI model skipped — install one later with: syn model download"]="  Modello di IA saltato — installane uno dopo con: syn model download"
  ["AI model installed"]="Modello di IA installato"
  ["  the install, and everything else on the disk is already done."]="  dell'installazione, e tutto il resto sul disco è già fatto."
  ["syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."]="syn-model non è sul sistema di destinazione, quindi non è stato scaricato alcun modello.
  Fa parte del nucleo; se è stato deselezionato, l'IA resta inerte."
  ["Configuring Nix"]="Configurazione di Nix"
  ["Nix configured — /etc/synapseos/nix"]="Nix configurato — /etc/synapseos/nix"
  ["  That is the download — a few hundred MB before any packages
  you add to home.nix. 'syn nix edit' opens it."]="  Quello è il download — qualche centinaio di MB prima di ogni pacchetto
  che aggiungi a home.nix. 'syn nix edit' lo apre."
  ["nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."]="nix è installato, ma il pacchetto 'syn' non è sul sistema di destinazione, quindi
  il configuratore non è stato preparato. Nix in sé funziona;
  lo strato /etc/synapseos/nix ha bisogno di 'syn'."
  ["nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."]="nix non si è installato — lo strato dichiarativo non è disponibile.
  Installalo dopo con 'sudo pacman -S nix && sudo syn nix init'."
  ["  Generating initramfs..."]="  Generazione dell'initramfs..."
  ["mkinitcpio failed — the installed system would not boot"]="mkinitcpio è fallito — il sistema installato non si avvierebbe"
  ["initramfs missing after mkinitcpio — the installed system would not boot"]="initramfs assente dopo mkinitcpio — il sistema installato non si avvierebbe"
  ["System configured"]="Sistema configurato"
  ["Installing Bootloader"]="Installazione del bootloader"
  ["grub-install (UEFI) failed"]="grub-install (UEFI) è fallito"
  ["grub-install (BIOS) failed"]="grub-install (BIOS) è fallito"
  ["  Generating GRUB config..."]="  Generazione della configurazione di GRUB..."
  ["grub-mkconfig failed"]="grub-mkconfig è fallito"
  ["grub.cfg missing after install"]="grub.cfg assente dopo l'installazione"
  ["grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"]="grub.cfg contiene una password di GRUB — resta leggibile solo da root, quindi l'app delle impostazioni non può riferire sulle voci di avvio"
  ["  Installing systemd-boot..."]="  Installazione di systemd-boot..."
  ["bootctl install failed"]="bootctl install è fallito"
  ["  Registering systemd-boot with the firmware..."]="  Registrazione di systemd-boot presso il firmware..."
  ["efibootmgr entry not created — the removable-media path still applies"]="voce efibootmgr non creata — vale ancora il percorso per supporti rimovibili"
  ["could not read the root filesystem UUID"]="impossibile leggere l'UUID del filesystem radice"
  ["vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"]="vmlinuz-linux non è sulla ESP — systemd-boot non troverebbe nulla da avviare"
  ["initramfs is not on the ESP — systemd-boot would find nothing to boot"]="l'initramfs non è sulla ESP — systemd-boot non troverebbe nulla da avviare"
  ["systemd-boot did not install its EFI binary"]="systemd-boot non ha installato il suo binario EFI"
  ["  Installing limine..."]="  Installazione di limine..."
  ["could not copy limine's EFI binary to the ESP"]="impossibile copiare il binario EFI di limine sulla ESP"
  ["limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"]="limine-mkinitcpio-hook non installato — un kernel installato più tardi NON avrà una voce di avvio"
  ["vmlinuz-linux is not on the ESP — limine would find nothing to boot"]="vmlinuz-linux non è sulla ESP — limine non troverebbe nulla da avviare"
  ["limine's EFI binary is not on the ESP"]="il binario EFI di limine non è sulla ESP"
  ["limine.conf has no kernel entry"]="limine.conf non ha nessuna voce di kernel"
  ["  Verifying the encrypted boot path..."]="  Verifica del percorso di avvio cifrato..."
  ["/boot is not a separate mount — an encrypted root needs a plain /boot"]="/boot non è un montaggio separato — una radice cifrata ha bisogno di un /boot in chiaro"
  ["/boot is missing from fstab — it would not be mounted after boot"]="/boot manca dalla fstab — non verrebbe montato dopo l'avvio"
  ["Encrypted boot path verified"]="Percorso di avvio cifrato verificato"
  ["Configuring snapshots"]="Configurazione delle istantanee"
  ["snapper's config template is missing — snapshots cannot be configured"]="il modello di configurazione di snapper manca — le istantanee non si possono configurare"
  ["could not write /etc/snapper/configs/root"]="impossibile scrivere /etc/snapper/configs/root"
  ["snapper does not see the 'root' config — snapshots would never be taken"]="snapper non vede la configurazione 'root' — non verrebbe mai presa un'istantanea"
  ["snapper's root config was not tuned — timeline snapshots would fill the disk"]="la configurazione root di snapper non è stata regolata — le istantanee periodiche riempirebbero il disco"
  ["could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"]="impossibile attivare grub-btrfsd — le istantanee non compariranno da sole nel menu di avvio"
  ["Snapshots enabled (snapper + snap-pac, bootable from GRUB)"]="Istantanee attive (snapper + snap-pac, avviabili da GRUB)"
  ["could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"]="impossibile attivare limine-snapper-sync — le istantanee non arriveranno da sole al menu di avvio"
  ["could not take the post-install snapshot"]="impossibile prendere l'istantanea successiva all'installazione"
  ["could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"]="impossibile attivare la sincronizzazione delle istantanee al primo avvio — il menu si riempie invece dopo il primo aggiornamento"
  ["Snapshots enabled (snapper + snap-pac, bootable from limine)"]="Istantanee attive (snapper + snap-pac, avviabili da limine)"
  ["Bootloader installed"]="Bootloader installato"
  ["  The root account is locked (no root login / su).
  Note: 3 wrong password attempts lock the account for 10 minutes."]="  L'account root è bloccato (nessun accesso root / su).
  Nota: 3 password sbagliate bloccano l'account per 10 minuti."
  ["You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."]="La passphrase di cifratura verrà chiesta a ogni avvio,
  PRIMA della schermata di accesso. Non c'è modo di recuperarla."
  ["    syn-crypt status                    is this disk encrypted, and how
    sudo syn-crypt change-key           replace the passphrase
    sudo syn-crypt add-key              add a second one
    sudo syn-crypt backup-header FILE   save the LUKS header"]="    syn-crypt status                    questo disco è cifrato, e come
    sudo syn-crypt change-key           sostituire la passphrase
    sudo syn-crypt add-key              aggiungerne una seconda
    sudo syn-crypt backup-header FILE   salvare l'intestazione LUKS"
  ["  means the data is unrecoverable even with the right passphrase."]="  significa che i dati sono irrecuperabili anche con la passphrase giusta."
  ["Remove installation media and press ENTER to reboot..."]="Rimuovi il supporto d'installazione e premi INVIO per riavviare..."
  ["Install SynapseOS     — right here, in this terminal"]="Installa SynapseOS      — qui, in questo terminale"
  ["Install graphically   — starts the desktop first"]="Installa in grafica     — avvia prima il desktop"
  ["Try the live desktop  — look around; install later"]="Prova il desktop live   — dai un'occhiata; installa dopo"
  ["Target:"]="Destinazione:"
  ["ALONGSIDE"]="ACCANTO"
  ["ERASE"]="CANCELLA"
  ["ADVANCED"]="AVANZATO"
  ["Encrypt this installation?"]="Cifrare questa installazione?"
  ["There is no recovery."]="Non c'è modo di recuperare."
  ["Root filesystem"]="Filesystem radice"
  ["ext4   — the default. Boring, proven, repairable by anything."]="ext4   — il predefinito. Noioso, collaudato, riparabile da qualsiasi cosa."
  ["btrfs  — snapshots + zstd compression. Roll back a bad update
                    from the boot menu. Uses more RAM and more CPU."]="btrfs  — istantanee + compressione zstd. Torna indietro da un brutto
                    aggiornamento dal menu di avvio. Più RAM e più CPU."
  ["xfs    — fast on large files. No snapshots, and it cannot be
                    SHRUNK once created."]="xfs    — veloce sui file grandi. Niente istantanee, e una volta creato
                    non si può RIDURRE."
  ["f2fs   — built for flash. Good on SD cards and cheap SSDs;
                    unusual enough that fewer rescue tools know it."]="f2fs   — fatto per la flash. Buono su schede SD e SSD economici;
                    abbastanza insolito che pochi strumenti di soccorso lo conoscano."
  ["Bootloader"]="Bootloader"
  ["GRUB          — the default. Detects other operating systems,
                          and the only one here that can boot a btrfs
                          snapshot."]="GRUB          — il predefinito. Rileva altri sistemi operativi,
                          ed è l'unico qui capace di avviare un'istantanea
                          btrfs."
  ["systemd-boot  — minimal. No OS detection, no snapshot menu."]="systemd-boot  — minimale. Nessun rilevamento di SO, nessun menu di istantanee."
  ["limine        — modern and fast, and it CAN boot snapshots."]="limine        — moderno e veloce, e PUÒ avviare le istantanee."
  ["Automatic snapshots?"]="Istantanee automatiche?"
  ["Review the plan — nothing has been written yet:"]="Controlla il piano — non è ancora stato scritto nulla:"
  ["nothing else is touched"]="nient'altro viene toccato"
  ["not"]="non verrà"
  ["Partition"]="Partiziona"
  ["now."]="adesso."
  ["Partitions now on"]="Partizioni adesso su"
  ["These partitions will be FORMATTED"]="Queste partizioni saranno FORMATTATE"
  ["Full      — Standard + Steam + Nix + more software"]="Completa  — Standard + Steam + Nix + altro software"
  ["Standard  — the SynapseOS suite, Firefox, AI model,"]="Standard  — la suite SynapseOS, Firefox, modello di IA,"
  ["Minimal   — core daemons only: none of the above"]="Minimale  — solo i demoni del nucleo: niente di quanto sopra"
  ["Custom    — tick every package yourself, ours and"]="Su misura — spunta ogni pacchetto da te, i nostri e"
  ["Which AI model should this machine run?"]="Quale modello di IA deve far girare questa macchina?"
  ["Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"]="Mistral 7B Instruct   ~4,1 GB   consigliato — è quello su cui SynapseOS è tarato"
  ["Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"]="Phi-3 Mini 4K         ~2,2 GB   metà della dimensione, e nettamente più debole"
  ["Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"]="Qwen2 0.5B            ~0,4 GB   sta ovunque, e risponde di conseguenza"
  ["None                            skip it — nothing else changes"]="Nessuno                         saltarlo — non cambia nient'altro"
  ["Installing:"]="Installazione di:"
  ["SynapseUI  — AI-native Wayland compositor  (default)"]="SynapseUI  — compositor Wayland nativo per l'IA  (predefinito)"
  ["SynapseUI  — NOT AVAILABLE: synui was not selected"]="SynapseUI  — NON DISPONIBILE: synui non è stato selezionato"
  ["KDE Plasma — Full-featured Wayland desktop"]="KDE Plasma — desktop Wayland completo"
  ["GNOME      — Clean, modern Wayland desktop"]="GNOME      — desktop Wayland pulito e moderno"
  ["TTY only   — No GUI (headless/server)"]="Solo TTY   — nessuna interfaccia grafica (headless/server)"
  ["Disk:"]="Disco:"
  ["Boot:"]="Avvio:"
  ["Encrypted:"]="Cifrato:"
  ["Desktop:"]="Desktop:"
  ["User:"]="Utente:"
  ["Hostname:"]="Nome host:"
  ["Back up the header to another machine."]="Fai una copia dell'intestazione su un'altra macchina."
  ["%s is mounted — unmount it first\\n"]="%s è montato — smontalo prima
"
  ["%s is %s MiB — %s needs at least %s MiB\\n"]="%s è di %s MiB — %s ne richiede almeno %s
"
  ["  Generating %s (a few seconds)...\\n"]="  Generazione di %s (qualche secondo)...
"
  ["Language: %s  (%s, keyboard %s)\\n"]="Lingua: %s  (%s, tastiera %s)
"
  ["  This disk already holds %s partition(s), an EFI System\\n  Partition (%s), and %s GiB of free space.\\n"]="  Questo disco contiene già %s partizione/i, una partizione di
  sistema EFI (%s), e %s GiB di spazio libero.
"
  ["    1) Install %s — use the free space, keep everything else\\n"]="    1) Installa %s — usa lo spazio libero, tieni tutto il resto
"
  ["    2) %s the whole disk — delete every partition and all data\\n"]="    2) %s l'intero disco — elimina ogni partizione e tutti i dati
"
  ["    3) %s — partition this disk yourself, then pick the partitions\\n"]="    3) %s — partiziona tu stesso questo disco, poi scegli le partizioni
"
  ["    1) %s the whole disk — delete every partition and all data  (default)\\n"]="    1) %s l'intero disco — elimina ogni partizione e tutti i dati  (predefinito)
"
  ["    2) %s — partition this disk yourself, then pick the partitions\\n"]="    2) %s — partiziona tu stesso questo disco, poi scegli le partizioni
"
  ["  %s If you forget the passphrase the data is\\n  gone — no password reset, no support call, nothing.\\n"]="  %s Se dimentichi la passphrase i dati sono
  persi — nessun ripristino, nessuna chiamata all'assistenza, niente.
"
  ["  snapper takes a snapshot before and after every pacman\\n  transaction, and %s grows a menu to boot any of them. A\\n  bad upgrade becomes a reboot instead of a rescue USB.\\n"]="  snapper prende un'istantanea prima e dopo ogni transazione di
  pacman, e %s ottiene un menu per avviarne una qualsiasi. Un
  aggiornamento sbagliato diventa un riavvio invece di una chiavetta di soccorso.
"
  ["    Disk          : %s\\n"]="    Disco         : %s
"
  ["    Firmware      : %s\\n"]="    Firmware      : %s
"
  ["    Filesystem    : %s\\n"]="    Filesystem    : %s
"
  ["    Bootloader    : %s\\n"]="    Bootloader    : %s
"
  ["    Separate /boot: %s\\n"]="    /boot separato: %s
"
  ["    Encryption    : %s\\n"]="    Cifratura     : %s
"
  ["    Snapshots     : %s\\n"]="    Istantanee    : %s
"
  ["  Encrypting %s (LUKS2)...\\n"]="  Cifratura di %s (LUKS2)...
"
  ["  Formatting root partition (%s)...\\n"]="  Formattazione della partizione radice (%s)...
"
  ["    • KEEP   all %s existing partition(s), including Windows\\n"]="    • TIENI    tutte le %s partizioni esistenti, Windows compreso
"
  ["    • REUSE  %s as the EFI partition (mounted, %s formatted)\\n"]="    • RIUSA    %s come partizione EFI (montata, %s formattata)
"
  ["    • CREATE a new ext4 root of ~%s GiB in the free space\\n"]="    • CREA     una nuova radice ext4 di ~%s GiB nello spazio libero
"
  ["  Creating root partition in free space (%sMiB–%sMiB)...\\n"]="  Creazione della partizione radice nello spazio libero (%s MiB–%s MiB)...
"
  ["  Formatting new root (%s, ext4)...\\n"]="  Formattazione della nuova radice (%s, ext4)...
"
  ["  %s %s %s The installer will re-read the table when you exit.\\n"]="  %s %s %s L'installatore rileggerà la tabella quando esci.
"
  ["    • a root partition, at least %s GiB\\n"]="    • una partizione radice, di almeno %s GiB
"
  ["    • a separate /boot of ~1 GiB — %s with this layout cannot read the root\\n"]="    • un /boot separato di ~1 GiB — %s con questa disposizione non può leggere la radice
"
  ["  Starting %s on %s — write your changes before quitting.\\n"]="  Avvio di %s su %s — scrivi le modifiche prima di uscire.
"
  ["    %s is already swap — another system may resume from it.\\n"]="    %s è già swap — un altro sistema potrebbe riprendere da lì.
"
  ["  Everything else on %s is left untouched.\\n"]="  Tutto il resto su %s resta intatto.
"
  ["  Making swap on %s...\\n"]="  Creazione dello swap su %s...
"
  ["  Formatting EFI partition (%s MiB)...\\n"]="  Formattazione della partizione EFI (%s MiB)...
"
  ["  NVIDIA GPU detected — installing %s (builds the module, takes a while)...\\n"]="  GPU NVIDIA rilevata — installazione di %s (compila il modulo, ci vuole un po')...
"
  ["  Installing video stack: %s %s...\\n"]="  Installazione dello stack video: %s %s...
"
  ["  [cachyos] enabled (%s packages available)\\n"]="  [cachyos] attivo (%s pacchetti disponibili)
"
  ["  Language: %s  (chosen at boot)\\n"]="  Lingua: %s  (scelta all'avvio)
"
  ["  Locale:   %s   Keyboard: %s (console) / %s (desktop)\\n"]="  Locale:   %s   Tastiera: %s (console) / %s (desktop)
"
  ["  Installing fonts (%s)...\\n"]="  Installazione dei caratteri (%s)...
"
  ["  Downloading the AI model (%s) — this is the long part of\\n"]="  Scaricamento del modello di IA (%s) — è questa la parte lunga
"
  ["  Nothing is built yet. As %s, after the first boot:\\n"]="  Non è ancora stato costruito nulla. Come %s, dopo il primo avvio:
"
  ["  Adding the %s hook to mkinitcpio...\\n"]="  Aggiunta dell'hook %s a mkinitcpio...
"
  ["  Installing GRUB (%s)...\\n"]="  Installazione di GRUB (%s)...
"
  ["yes — LUKS2 on %s"]="sì — LUKS2 su %s"
  ["  Admin: use %s with your user password.\\n"]="  Amministrazione: usa %s con la tua password utente.
"
  ["  Manage it later with %s:\\n"]="  Gestiscilo più tardi con %s:
"
  ["  %s A damaged LUKS header\\n"]="  %s Un'intestazione LUKS danneggiata
"
  ["%s is on the live/boot device — that is the installer's own media\\n"]="%s è sul dispositivo live/di avvio — è il supporto dell'installatore stesso
"
  ["    %s is already FAT — it may hold another OS's bootloader.\\n"]="    %s è già FAT — potrebbe contenere il bootloader di un altro sistema.
"
  ["  Creating user '%s'...\\n"]="  Creazione dell'utente '%s'...
"
  ["  User '%s' created (uid=%s)\\n"]="  Utente '%s' creato (uid=%s)
"
  ["  Log in as '%s' after reboot.\\n"]="  Accedi come '%s' dopo il riavvio.
"
  ["  Type '%s' to get started.\\n"]="  Scrivi '%s' per iniziare.
"
  ["Install SynapseOS"]="Installa SynapseOS"
  ["SynapseOS packages"]="Pacchetti SynapseOS"
  ["Everything the system is made of. What you cannot drop is what something else you kept depends on — those are turned back on and named before anything is installed."]="Tutto ciò di cui è fatto il sistema. Quello che non si può togliere serve a qualcos'altro che hai tenuto — viene riacceso e nominato prima che si installi qualsiasi cosa."
  ["SYNAPSE UI — the Wayland desktop"]="SYNAPSE UI — il desktop Wayland"
  ["synapd — the local AI daemon"]="synapd — il servizio IA locale"
  ["synsh — the AI-native shell"]="synsh — la shell nativa per l'IA"
  ["synguard + kernel module"]="synguard + modulo del kernel"
  ["synnet — network policy"]="synnet — regole di rete"
  ["Software — the package manager"]="Software — il gestore di pacchetti"
  ["Files — the file manager"]="File — il gestore di file"
  ["Terminal (synui depends on it)"]="Terminale (synui ne ha bisogno)"
  ["Settings"]="Impostazioni"
  ["Disks"]="Dischi"
  ["Editor"]="Editor"
  ["Calendar"]="Calendario"
  ["File Vault — a locked folder"]="Cassaforte — una cartella chiusa a chiave"
  ["Disk Cleanup — caches, and secure delete"]="Pulizia disco — cache ed eliminazione sicura"
  ["syn-update — how fixes arrive"]="syn-update — come arrivano le correzioni"
  ["syn — the top-level CLI"]="syn — la riga di comando principale"
  ["syn-model — fetch AI models"]="syn-model — scaricare modelli IA"
  ["syn-confine — the sandbox"]="syn-confine — la sandbox"
  ["fetch — the About OS readout"]="fetch — il riepilogo del sistema"
  ["Arcade — overlay, pads, big screen"]="Arcade — overlay, controller, schermo grande"
  ["cliamp — the music player"]="cliamp — il lettore musicale"
  ["Player — playlists, shuffle and history, on mpv"]="Player — playlist, casuale e cronologia, su mpv"
  ["Studio — photo darkroom and video"]="Studio — camera oscura e video"
  ["GeForce NOW — cloud gaming in a browser"]="GeForce NOW — gioco in cloud nel browser"
  ["Remote desktop — reach this machine from another"]="Desktop remoto — raggiungere questa macchina da un'altra"
  ["Arsenal — BlackArch browser"]="Arsenal — sfoglia BlackArch"
  ["Chibi — voice companion"]="Chibi — compagno vocale"
  ["Vibe — AI coding assistant"]="Vibe — assistente di programmazione IA"
  ["Animated wallpapers (~317 MB)"]="Sfondi animati (~317 MB)"
  ["Nexus Chat (pulls in Firefox)"]="Nexus Chat (porta con sé Firefox)"
  ["TEPRIS (pulls in Firefox)"]="TEPRIS (porta con sé Firefox)"
  ["Web and communication"]="Web e comunicazione"
  ["None of this is ours; every name is in the Arch repositories. Firefox is on by default because an installed SynapseOS used to arrive with no browser at all."]="Niente di tutto questo è nostro; ogni nome sta nei repository di Arch. Firefox è attivo di default perché un SynapseOS installato arrivava senza nessun browser."
  ["Thunderbird — mail"]="Thunderbird — posta"
  ["KeePassXC — passwords"]="KeePassXC — password"
  ["Syncthing — file sync"]="Syncthing — sincronizzazione file"
  ["LocalSend — send to phone (Flatpak)"]="LocalSend — inviare al telefono (Flatpak)"
  ["Audio and video"]="Audio e video"
  ["Office and graphics"]="Ufficio e grafica"
  ["Development and admin"]="Sviluppo e amministrazione"
  ["VS Code (OSS build)"]="VS Code (build OSS)"
  ["7zip + unrar"]="7zip + unrar"
  ["Games, launchers and helpers"]="Giochi, launcher e utilità"
  ["Steam is in the options below rather than here: it is the only one that turns on a second architecture and a third repository."]="Steam sta tra le opzioni qui sotto e non qui: è l'unico che accende una seconda architettura e un terzo repository."
  ["Prism — Minecraft"]="Prism — Minecraft"
  ["Dolphin — GameCube/Wii"]="Dolphin — GameCube/Wii"
  ["PPSSPP — PSP"]="PPSSPP — PSP"
  ["Space Cadet Pinball (Flatpak)"]="Space Cadet Pinball (Flatpak)"
  ["GOverlay — MangoHud"]="GOverlay — MangoHud"
  ["AntiMicroX — pad remap"]="AntiMicroX — rimappare i controller"
  ["Welcome"]="Benvenuto"
  ["Disk"]="Disco"
  ["Software"]="Software"
  ["Account"]="Account"
  ["Region"]="Regione"
  ["Summary"]="Riepilogo"
  ["Install"]="Installazione"
  ["the installer's own media"]="il supporto dell'installer stesso"
  ["%1 GiB — SynapseOS needs at least %2 GiB"]="%1 GiB — SynapseOS ha bisogno di almeno %2 GiB"
  ["No connection. SynapseOS downloads the base system while it installs, so this needs a working network before it can start."]="Nessuna connessione. SynapseOS scarica il sistema di base mentre installa, quindi serve una rete funzionante prima di poter iniziare."
  ["Choose a disk to install to."]="Scegli un disco su cui installare."
  ["The encryption passphrase needs at least 8 characters."]="La passphrase di cifratura richiede almeno 8 caratteri."
  ["With neither the package manager nor the desktop, this install has no way to add either one back. Keep at least one."]="Senza né il gestore di pacchetti né il desktop, questa installazione non ha modo di riaggiungere nessuno dei due. Tienine almeno uno."
  ["A username is lower-case letters, digits, - and _, and cannot start with a digit."]="Un nome utente è fatto di lettere minuscole, cifre, - e _, e non può iniziare con una cifra."
  ["Set a password for the account."]="Imposta una password per l'account."
  ["The two passwords do not match."]="Le due password non coincidono."
  ["A locale is needed, e.g. en_US.UTF-8."]="Serve un locale, ad es. it_IT.UTF-8."
  ["A timezone is needed, e.g. Europe/Lisbon."]="Serve un fuso orario, ad es. Europe/Rome."
  ["printing"]="stampa"
  ["%1 repo"]="repo %1"
  ["Mode"]="Modalità"
  ["Filesystem"]="Filesystem"
  ["%1 on LUKS2"]="%1 su LUKS2"
  ["%1 + snapshots"]="%1 + snapshot"
  ["none"]="nessuno"
  ["Desktop"]="Desktop"
  ["Locale"]="Locale"
  ["%1   keys %2 / %3"]="%1   tasti %2 / %3"
  ["Timezone"]="Fuso orario"
  ["%1 package(s) — WITHOUT %2"]="%1 pacchetto/i — SENZA %2"
  ["%1 package(s)"]="%1 pacchetto/i"
  ["Options"]="Opzioni"
  ["Could not write the install profile."]="Impossibile scrivere il profilo di installazione."
  ["Installation complete."]="Installazione completata."
  ["Installation failed — see the log."]="Installazione fallita — vedi il registro."
  ["No network connection"]="Nessuna connessione di rete"
  ["The base system is downloaded while it installs, so this cannot start offline. Plug in a cable or join a network, then press Re-check — the answers on these pages are kept."]="Il sistema di base viene scaricato durante l'installazione, quindi non può partire offline. Collega un cavo o entra in una rete, poi premi Ricontrolla — le risposte di queste pagine restano."
  ["Checking…"]="Controllo…"
  ["Re-check"]="Ricontrolla"
  ["Wi-Fi settings"]="Impostazioni Wi-Fi"
  ["This asks for a disk, an account and a few preferences, then hands the answers to the same installer the text version runs. Nothing is written to any disk until the last page, and that page says exactly what it is about to do."]="Qui si chiedono un disco, un account e qualche preferenza, poi le risposte vanno allo stesso installer che esegue la versione testuale. Niente viene scritto su alcun disco fino all'ultima pagina, e quella pagina dice esattamente cosa sta per fare."
  ["A disk is partitioned and formatted"]="Un disco viene partizionato e formattato"
  ["The base system and the SynapseOS packages are installed"]="Il sistema di base e i pacchetti SynapseOS vengono installati"
  ["An account and a desktop are set up"]="Un account e un desktop vengono configurati"
  ["A bootloader is written"]="Un bootloader viene scritto"
  ["Partitioning an existing layout by hand is the text installer's ADVANCED mode — quit this and run \`syn-install\` in a terminal for that."]="Partizionare a mano uno schema esistente è la modalità ADVANCED dell'installer testuale — per quello chiudi questa finestra ed esegui \`syn-install\` in un terminale."
  ["Where should SynapseOS go?"]="Dove deve andare SynapseOS?"
  ["The installer's own media is listed and cannot be chosen."]="Il supporto dell'installer stesso è elencato e non può essere scelto."
  ["No disks found."]="Nessun disco trovato."
  ["Erase the disk"]="Cancella il disco"
  ["every partition and all data"]="ogni partizione e tutti i dati"
  ["Install alongside"]="Installa accanto"
  ["use free space, UEFI only"]="usa lo spazio libero, solo UEFI"
  ["Snapshots"]="Snapshot"
  ["btrfs + limine only"]="solo btrfs + limine"
  ["Encrypt the disk"]="Cifra il disco"
  ["Passphrase"]="Passphrase"
  ["8 characters or more"]="8 caratteri o più"
  ["What should be installed?"]="Cosa va installato?"
  ["The SynapseOS core — the compositor, the daemons and the applications it is built on — is installed by every choice here."]="Il nucleo di SynapseOS — il compositor, i servizi e le applicazioni su cui è costruito — viene installato da ogni scelta qui."
  ["Full"]="Completa"
  ["Standard + Steam + Nix + more software"]="Standard + Steam + Nix + più software"
  ["Standard"]="Standard"
  ["the SynapseOS suite, Firefox, AI model, Bluetooth, printing, Wine, phone"]="la suite SynapseOS, Firefox, modello IA, Bluetooth, stampa, Wine, telefono"
  ["Minimal"]="Minima"
  ["core daemons only — no apps, no software, no model"]="solo i servizi del nucleo — niente app, niente software, niente modello"
  ["Custom"]="Personalizzata"
  ["tick every package yourself, ours and the ordinary software"]="spuntare ogni pacchetto a mano, i nostri e il software comune"
  ["(required)"]="(obbligatorio)"
  ["Not packages: a repository, an architecture or a service. Each is a decision with a consequence that does not fit on a checkbox above."]="Non sono pacchetti: un repository, un'architettura o un servizio. Ognuno è una decisione con una conseguenza che non sta in una casella qui sopra."
  ["Printing (CUPS)"]="Stampa (CUPS)"
  ["Wine — run Windows .exe/.msi"]="Wine — eseguire .exe/.msi di Windows"
  ["KDE Connect — pair a phone"]="KDE Connect — associare un telefono"
  ["Steam + game stack + Proton (~3.1 GB)"]="Steam + stack di gioco + Proton (~3,1 GB)"
  ["BlackArch repo — ~5000 tools, none installed"]="Repo BlackArch — ~5000 strumenti, nessuno installato"
  ["Nix + Home Manager"]="Nix + Home Manager"
  ["syn-update is off: this machine will have no way to receive another SynapseOS package. Fixing that later means installing it by hand from the ISO, or reinstalling."]="syn-update è spento: questa macchina non avrà modo di ricevere un altro pacchetto SynapseOS. Rimediare dopo significa installarlo a mano dalla ISO, o reinstallare."
  ["synui is off: this will not be a SynapseOS desktop. The Desktop page offers KDE, GNOME or no GUI."]="synui è spento: questo non sarà un desktop SynapseOS. La pagina Desktop offre KDE, GNOME o nessuna interfaccia."
  ["AI model — downloaded during the install"]="Modello IA — scaricato durante l'installazione"
  ["~4.1 GB — recommended"]="~4,1 GB — consigliato"
  ["~2.2 GB — weaker"]="~2,2 GB — più debole"
  ["~0.4 GB — much weaker"]="~0,4 GB — molto più debole"
  ["None"]="Nessuno"
  ["AI stays inert"]="l'IA resta inerte"
  ["NVIDIA GPU inference"]="Inferenza su GPU NVIDIA"
  ["the CUDA runtime, ~4.7 GiB"]="il runtime CUDA, ~4,7 GiB"
  ["Who is this machine for?"]="Per chi è questa macchina?"
  ["Username"]="Nome utente"
  ["lower-case, no spaces"]="minuscolo, senza spazi"
  ["Full name (optional)"]="Nome completo (facoltativo)"
  ["Password"]="Password"
  ["Password again"]="Password di nuovo"
  ["They do not match"]="Non coincidono"
  ["the native compositor"]="il compositor nativo"
  ["synui is not selected"]="synui non è selezionato"
  ["headless"]="senza interfaccia"
  ["Language, keyboard and time"]="Lingua, tastiera e ora"
  ["Pick a language and the other three follow it. The console keymap and the desktop layout are separate on purpose — Swedish is 'sv-latin1' to the console and 'se' to the desktop — so they can be changed on their own afterwards."]="Scegli una lingua e le altre tre la seguono. La mappa di tastiera della console e il layout del desktop sono separati apposta — lo svedese è 'sv-latin1' per la console e 'se' per il desktop — così si possono cambiare singolarmente dopo."
  ["Language"]="Lingua"
  ["sets the keyboard and the fonts too"]="imposta anche la tastiera e i caratteri"
  ["typed by hand — fonts cover as much as possible"]="scritto a mano — i caratteri coprono il più possibile"
  ["Sets the locale, both keyboard names and the font pack. Any locale glibc has can be typed instead."]="Imposta il locale, entrambi i nomi di tastiera e il pacchetto di caratteri. Al suo posto si può scrivere qualsiasi locale che glibc conosca."
  ["The common zones first, then every name tzdata ships."]="Prima le zone comuni, poi ogni nome che tzdata porta con sé."
  ["Console keymap"]="Mappa di tastiera della console"
  ["loadkeys — the text console and the greeter"]="loadkeys — la console testuale e la schermata di accesso"
  ["Every keymap this image can load. This one names a file loadkeys has to find, which is why it is not the same list as the desktop layout."]="Ogni mappa di tastiera che questa immagine può caricare. Questa nomina un file che loadkeys deve trovare, ed è per questo che non è la stessa lista del layout del desktop."
  ["Desktop layout"]="Layout del desktop"
  ["XKB — the compositor"]="XKB — il compositor"
  ["Desktop keyboard layout"]="Layout di tastiera del desktop"
  ["The layouts xkbcommon can compile. 'uk' is a console keymap and not a layout here — the layout is 'gb'."]="I layout che xkbcommon può compilare. 'uk' è una mappa di tastiera della console e qui non è un layout — il layout è 'gb'."
  ["Read this back"]="Rileggi tutto"
  ["Nothing has been written yet. The next button is the one that starts."]="Finora non è stato scritto niente. Il prossimo pulsante è quello che comincia."
  ["EVERY PARTITION ON %1 WILL BE DELETED"]="OGNI PARTIZIONE SU %1 SARÀ CANCELLATA"
  ["SynapseOS will be installed into the free space on %1"]="SynapseOS sarà installato nello spazio libero su %1"
  ["SynapseOS is installed"]="SynapseOS è installato"
  ["The install stopped"]="L'installazione si è fermata"
  ["Installing SynapseOS"]="Installazione di SynapseOS"
  ["Reboot and remove the installation media."]="Riavvia e togli il supporto di installazione."
  ["The log below is the whole story — the last lines say why."]="Il registro qui sotto racconta tutto — le ultime righe dicono perché."
  ["This takes a while: the base system and the packages are downloaded, and an AI model is gigabytes on its own."]="Ci vuole un po': il sistema di base e i pacchetti vengono scaricati, e un modello IA da solo è di diversi gigabyte."
  ["Back"]="Indietro"
  ["Next"]="Avanti"
  ["Reboot"]="Riavvia"
  ["Close"]="Chiudi"
  ["type to filter, or type a name that is not listed"]="scrivi per filtrare, o scrivi un nome che non è elencato"
  ["Nothing to list on this image — type the name instead."]="Niente da elencare su questa immagine — scrivi invece il nome."
  ["Nothing matches — the row below uses what you typed."]="Nessuna corrispondenza — la riga qui sotto usa quello che hai scritto."
  ["Use “%1” as typed"]="Usa “%1” così com'è"
)
