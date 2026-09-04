# Français (fr) — syn-install's own words.
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
  ["render.nix is missing — the 'syn' package is not installed here."]="render.nix est absent — le paquet 'syn' n'est pas installé ici."
  ["locale-gen failed. The live session stays in English; the install is
  unaffected, because it generates the locale inside the target."]="locale-gen a échoué. La session live reste en anglais ; l'installation
  n'est pas affectée, elle génère la locale dans le système cible."
  ["  The keyboard, the clock, the fonts and the shell all follow this.
  You can change any of it later."]="  Le clavier, l'heure, les polices et le shell en découlent tous.
  Tout cela peut être changé plus tard."
  ["Toggle [numbers, 'all', 'none', Enter = accept]:"]="Basculer [numéros, 'all', 'none', Entrée = valider] :"
  ["--config needs a file"]="--config a besoin d'un fichier"
  ["syn-install must be run as root"]="syn-install doit être lancé en root"
  ["  SynapseOS is running from the live image."]="  SynapseOS tourne depuis l'image live."
  ["Starting the desktop — the installer opens with it."]="Démarrage du bureau — l'installateur s'ouvre avec lui."
  ["  This installer will:
    1. Partition a disk
    2. Install SynapseOS base system
    3. Install SynapseOS packages
    4. Create user account
    5. Choose desktop environment
    6. Configure system & bootloader"]="  Cet installateur va :
    1. Partitionner un disque
    2. Installer le système de base SynapseOS
    3. Installer les paquets SynapseOS
    4. Créer un compte utilisateur
    5. Choisir un environnement de bureau
    6. Configurer le système et l'amorçage"
  ["ALL DATA ON THE TARGET DISK WILL BE ERASED"]="TOUTES LES DONNÉES DU DISQUE CIBLE SERONT EFFACÉES"
  ["Press ENTER to continue or Ctrl+C to abort..."]="Appuyez sur ENTRÉE pour continuer, ou Ctrl+C pour abandonner..."
  ["Checking network"]="Vérification du réseau"
  ["Network is up"]="Le réseau est actif"
  ["  No network detected. Starting NetworkManager..."]="  Aucun réseau détecté. Démarrage de NetworkManager..."
  ["  No connection — but this machine has Wi-Fi."]="  Pas de connexion — mais cette machine a du Wi-Fi."
  ["Open the Wi-Fi picker (nmtui)? [Y/n]:"]="Ouvrir le sélecteur Wi-Fi (nmtui) ? [Y/n] :"
  ["No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."]="Pas de connexion réseau, et aucun périphérique Wi-Fi à configurer.
  SynapseOS télécharge le système de base pendant l'installation : branchez
  un câble et relancez."
  ["Network connected"]="Réseau connecté"
  ["Step 1 — Select Target Disk"]="Étape 1 — Choisir le disque cible"
  ["  Available disks:"]="  Disques disponibles :"
  ["Target disk (e.g. sda, vda, nvme0n1):"]="Disque cible (par ex. sda, vda, nvme0n1) :"
  ["Target disk is in use. Unmount its partitions and re-run."]="Le disque cible est utilisé. Démontez ses partitions et relancez."
  ["Boot mode: UEFI"]="Mode d'amorçage : UEFI"
  ["Boot mode: BIOS/Legacy"]="Mode d'amorçage : BIOS/Legacy"
  ["  Encrypts the root filesystem with LUKS2. You will be asked for the
  passphrase at every boot, before the system starts."]="  Chiffre le système de fichiers racine avec LUKS2. La phrase secrète sera
  demandée à chaque démarrage, avant que le système ne se lance."
  ["Encrypt the disk? [y/N]:"]="Chiffrer le disque ? [y/N] :"
  ["                          With encryption it is the BETTER choice: the
                          kernel lives on the EFI partition and only the
                          initramfs unlocks, so /boot needs no separate
                          unencrypted partition."]="                          Avec le chiffrement c'est le MEILLEUR choix : le
                          noyau est sur la partition EFI et seul l'initramfs
                          déverrouille, donc /boot n'a pas besoin d'une
                          partition non chiffrée séparée."
  ["                          It copies each snapshot's kernel onto the EFI
                          partition, so that partition is made much
                          larger when snapshots are enabled."]="                          Il copie le noyau de chaque instantané sur la
                          partition EFI, qui est donc créée bien plus
                          grande quand les instantanés sont activés."
  ["  Snapshots are cheap but not free: they hold the old copy of
  anything that changes, so a disk near full stays near full."]="  Les instantanés sont bon marché mais pas gratuits : ils gardent l'ancienne
  copie de ce qui change, donc un disque presque plein le reste."
  ["Enable snapshots? [Y/n]:"]="Activer les instantanés ? [Y/n] :"
  ["mkfs.ext4 is missing from this installer image — /boot cannot be created"]="mkfs.ext4 est absent de cette image d'installation — /boot ne peut pas être créé"
  ["btrfs is missing from this installer image — subvolumes cannot be created"]="btrfs est absent de cette image d'installation — les sous-volumes ne peuvent pas être créés"
  ["Are these correct? [Y/n]:"]="Est-ce correct ? [Y/n] :"
  ["Starting the questions over — the disk has not been touched."]="On reprend les questions — le disque n'a pas été touché."
  ["cryptsetup is not available on this installer image"]="cryptsetup n'est pas disponible sur cette image d'installation"
  ["Encryption passphrase:"]="Phrase secrète de chiffrement :"
  ["Repeat passphrase:"]="Répétez la phrase secrète :"
  ["Empty passphrase — that would leave the disk unprotected."]="Phrase secrète vide — cela laisserait le disque sans protection."
  ["Passphrases did not match — try again."]="Les phrases secrètes ne correspondent pas — recommencez."
  ["Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."]="La phrase secrète fait moins de 8 caractères. Une phrase courte vaut peu
  face à quelqu'un qui a le disque en main."
  ["Use it anyway? [y/N]:"]="L'utiliser quand même ? [y/N] :"
  ["Encryption enabled — root will be LUKS2"]="Chiffrement activé — la racine sera en LUKS2"
  ["cryptsetup open failed — the passphrase did not take"]="cryptsetup open a échoué — la phrase secrète n'a pas été acceptée"
  ["Failed to mount root"]="Échec du montage de la racine"
  ["  Creating btrfs subvolumes..."]="  Création des sous-volumes btrfs..."
  ["btrfs: could not create @"]="btrfs : impossible de créer @"
  ["btrfs: could not create @home"]="btrfs : impossible de créer @home"
  ["btrfs: could not create @snapshots"]="btrfs : impossible de créer @snapshots"
  ["btrfs: could not create @var_log"]="btrfs : impossible de créer @var_log"
  ["btrfs: could not create @pkg"]="btrfs : impossible de créer @pkg"
  ["could not remount the btrfs root onto @"]="impossible de remonter la racine btrfs sur @"
  ["Failed to mount @"]="Échec du montage de @"
  ["Failed to mount @home"]="Échec du montage de @home"
  ["Failed to mount @snapshots"]="Échec du montage de @snapshots"
  ["Failed to mount @var_log"]="Échec du montage de @var_log"
  ["Failed to mount @pkg"]="Échec du montage de @pkg"
  ["This adds one partition in the free space. Back up anything irreplaceable first."]="Cela ajoute une partition dans l'espace libre. Sauvegardez d'abord tout ce qui est irremplaçable."
  ["Type 'yes' to install alongside:"]="Tapez 'yes' pour installer à côté :"
  ["Aborted"]="Abandonné"
  ["Failed to create the root partition"]="Échec de la création de la partition racine"
  ["Could not identify the new partition after creating it"]="Impossible d'identifier la nouvelle partition après sa création"
  ["Failed to format root partition"]="Échec du formatage de la partition racine"
  ["Failed to mount the existing ESP"]="Échec du montage de l'ESP existante"
  ["no partition editor on this image (cfdisk, fdisk and parted are all missing)"]="aucun éditeur de partitions sur cette image (cfdisk, fdisk et parted sont tous absents)"
  ["  What this install needs:"]="  Ce dont cette installation a besoin :"
  ["    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"]="    • une partition système EFI (type EF00 / drapeau 'esp') — une partition existante peut être réutilisée"
  ["  Skipping the partition editor (--config)."]="  L'éditeur de partitions est ignoré (--config)."
  ["Format it? Everything on it is lost [y/N]:"]="Le formater ? Tout ce qu'il contient est perdu [y/N] :"
  ["Separate /boot partition:"]="Partition /boot séparée :"
  ["Swap partition (blank for none):"]="Partition d'échange (vide pour aucune) :"
  ["Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"]="La recréer ? Son UUID change, ce qui casse la fstab de ce système-là [y/N] :"
  ["Type 'yes' to format these:"]="Tapez 'yes' pour les formater :"
  ["  Formatting EFI partition..."]="  Formatage de la partition EFI..."
  ["  Formatting /boot partition..."]="  Formatage de la partition /boot..."
  ["Failed to mount /boot"]="Échec du montage de /boot"
  ["Type 'yes' to confirm:"]="Tapez 'yes' pour confirmer :"
  ["  Creating GPT partition table..."]="  Création de la table de partitions GPT..."
  ["Failed to format EFI partition"]="Échec du formatage de la partition EFI"
  ["Failed to format boot partition"]="Échec du formatage de la partition de démarrage"
  ["  Creating MBR partition table..."]="  Création de la table de partitions MBR..."
  ["Disk partitioned and mounted at /mnt"]="Disque partitionné et monté sur /mnt"
  ["Step 3 — Installing Base System"]="Étape 3 — Installation du système de base"
  ["  Initializing pacman keyring..."]="  Initialisation du trousseau pacman..."
  ["  Running pacstrap (this may take several minutes)..."]="  pacstrap est en cours (cela peut prendre plusieurs minutes)..."
  ["pacstrap failed — check network connection"]="pacstrap a échoué — vérifiez la connexion réseau"
  ["grub-install not found in chroot — attempting recovery..."]="grub-install introuvable dans le chroot — tentative de récupération..."
  ["Could not install grub into target — check network"]="Impossible d'installer grub dans le système cible — vérifiez le réseau"
  ["Base system installed"]="Système de base installé"
  ["Step 4 — Choose What to Install"]="Étape 4 — Choisir ce qui sera installé"
  ["  What should be installed alongside the SynapseOS core?"]="  Que faut-il installer à côté du cœur de SynapseOS ?"
  ["                   Bluetooth, printing, Wine, phone   (default)"]="                   Bluetooth, impression, Wine, téléphone   (par défaut)"
  ["                   the ordinary software people install anyway"]="                   les logiciels que l'on installe de toute façon"
  ["  Every preset except Minimal then asks WHICH AI model to download,
  and skipping it is one of the answers."]="  Toutes les formules sauf Minimal demandent ensuite QUEL modèle d'IA
  télécharger, et le passer est l'une des réponses."
  ["Full install selected"]="Installation complète choisie"
  ["Minimal install selected"]="Installation minimale choisie"
  ["  Two kinds of question. First the packages, as pages of
  checkboxes; then the handful of options that are a whole
  subsystem rather than a package."]="  Deux sortes de questions. D'abord les paquets, en pages de
  cases à cocher ; ensuite les quelques options qui sont un
  sous-système entier plutôt qu'un paquet."
  ["  And the software people install on the first evening anyway.
  All of it is in the Arch repositories; none of it is ours."]="  Et les logiciels que l'on installe le premier soir de toute façon.
  Tout est dans les dépôts Arch ; rien de tout cela n'est de nous."
  ["  The rest is y/n. The default (shown in caps) is Standard."]="  Le reste est en o/n. Le défaut (en majuscules) est Standard."
  ["syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."]="syn-update désélectionné : cette machine n'aura aucun moyen de recevoir
  un autre paquet SynapseOS. Y remédier plus tard veut dire l'installer à la main
  depuis l'ISO, ou tout réinstaller."
  ["Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."]="Ni le bureau ni le démon d'IA n'ont été gardés. C'est un système
  Arch avec quelques outils SynapseOS dessus, ce qui est une réponse acceptée —
  mais rien dans la documentation ne décrira la machine que vous obtenez."
  ["Custom install configured"]="Installation personnalisée configurée"
  ["Standard install selected"]="Installation standard choisie"
  ["  synapd loads one model and everything AI in SynapseOS talks to it:
  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,
  over this connection, onto the disk you are installing to."]="  synapd charge un modèle et tout ce qui touche à l'IA dans SynapseOS lui parle :
  synsh, le panneau IA du bureau, Chibi, Vibe. Il est téléchargé maintenant,
  par cette connexion, sur le disque où vous installez."
  ["  A smaller model is not just faster and lighter: it follows
  instructions worse. synsh mistakes what you asked for, Vibe's code
  needs more fixing, Chibi loses the thread. Take the default unless
  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."]="  Un modèle plus petit n'est pas seulement plus rapide et plus léger : il suit
  moins bien les instructions. synsh se trompe sur votre demande, le code de Vibe
  demande plus de corrections, Chibi perd le fil. Prenez le défaut, sauf si
  le disque ou la RAM disent le contraire — 7B veut ~6 Go de RAM libre."
  ["  Whatever you pick, it can be changed later: 'syn model download',
  or Super+C ▸ System ▸ AI model on the desktop."]="  Quoi que vous choisissiez, cela peut changer plus tard : 'syn model download',
  ou Super+C ▸ Système ▸ Modèle d'IA sur le bureau."
  ["Install this selection? [Y/n]:"]="Installer cette sélection ? [Y/n] :"
  ["Choosing again — nothing has been installed yet."]="On recommence le choix — rien n'a encore été installé."
  ["Step 4b — Installing SynapseOS"]="Étape 4b — Installation de SynapseOS"
  ["Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."]="Impossible d'activer ILoveCandy dans /etc/pacman.conf (purement cosmétique)."
  ["  Enabling [multilib] (32-bit repo, needed by Steam)..."]="  Activation de [multilib] (dépôt 32 bits, nécessaire à Steam)..."
  ["Could not sync the multilib database — Steam may fail to install"]="Impossible de synchroniser la base multilib — Steam pourrait ne pas s'installer"
  ["Could not enable [multilib]; Steam will be skipped."]="Impossible d'activer [multilib] ; Steam sera ignoré."
  ["Some SynapseOS packages failed to install — verifying below"]="Certains paquets SynapseOS n'ont pas pu être installés — vérification ci-dessous"
  ["No SynapseOS packages were selected. This will be an Arch system."]="Aucun paquet SynapseOS n'a été sélectionné. Ce sera un système Arch."
  ["SynapseOS packages installed"]="Paquets SynapseOS installés"
  ["Component selection recorded in /etc/synapseos/components.conf"]="Sélection des composants enregistrée dans /etc/synapseos/components.conf"
  ["Step 5 — Create User Account"]="Étape 5 — Créer un compte utilisateur"
  ["  Create a user account for the installed system."]="  Créez un compte utilisateur pour le système installé."
  ["Username [default: syn]:"]="Nom d'utilisateur [par défaut : syn] :"
  ["Full name (optional):"]="Nom complet (facultatif) :"
  ["Password:"]="Mot de passe :"
  ["Confirm password:"]="Confirmez le mot de passe :"
  ["Passwords do not match or are empty — try again"]="Les mots de passe ne correspondent pas ou sont vides — recommencez"
  ["Step 6 — Desktop Environment"]="Étape 6 — Environnement de bureau"
  ["  Choose a desktop environment:"]="  Choisissez un environnement de bureau :"
  ["  Installing KDE Plasma..."]="  Installation de KDE Plasma..."
  ["Some KDE packages failed to install"]="Certains paquets KDE n'ont pas pu être installés"
  ["KDE Plasma installed"]="KDE Plasma installé"
  ["  Installing GNOME..."]="  Installation de GNOME..."
  ["Some GNOME packages failed to install"]="Certains paquets GNOME n'ont pas pu être installés"
  ["GNOME installed (session only — SynapseOS apps, not GNOME's)"]="GNOME installé (la session seulement — les applications SynapseOS, pas celles de GNOME)"
  ["  Installing greetd (login screen) + desktop extras..."]="  Installation de greetd (écran de connexion) + extras du bureau..."
  ["greetd failed to install — boot falls back to getty login"]="greetd n'a pas pu être installé — le démarrage retombe sur la connexion getty"
  ["SynapseUI selected (included)"]="SynapseUI choisi (inclus)"
  ["Installing Wine"]="Installation de Wine"
  ["Wine installed"]="Wine installé"
  ["wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."]="wine n'a pas pu être installé — les .exe/.msi Windows ne fonctionneront pas.
  Installez-le plus tard avec 'sudo pacman -S wine wine-mono'."
  ["Configuring Video Driver"]="Configuration du pilote vidéo"
  ["  Virtual machine — installing mesa (synui uses pixman here)..."]="  Machine virtuelle — installation de mesa (synui utilise pixman ici)..."
  ["mesa failed to install"]="mesa n'a pas pu être installé"
  ["NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"]="L'installation du pilote NVIDIA a échoué — le système démarrerait sur
  nouveau et le moteur de rendu de synui ne se lancerait jamais"
  ["NVIDIA sleep services enabled (VRAM save/restore)"]="Services de veille NVIDIA activés (sauvegarde/restauration de la VRAM)"
  ["Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"]="Impossible d'activer nvidia-{suspend,resume,hibernate} — la veille
  peut donner un écran noir si NVreg_PreserveVideoMemoryAllocations est activé plus tard"
  ["  synapd can run inference on this GPU instead of the CPU.
  This downloads the CUDA runtime (~4.7 GiB installed)."]="  synapd peut faire l'inférence sur ce GPU au lieu du CPU.
  Cela télécharge l'environnement CUDA (~4,7 Gio installés)."
  ["Enable GPU inference? [Y/n]:"]="Activer l'inférence GPU ? [Y/n] :"
  ["Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda"]="On garde l'inférence CPU. Pour changer plus tard :
  sudo pacman -S synapse-llama-cuda"
  ["  Installing synapse-llama-cuda (this takes a while)..."]="  Installation de synapse-llama-cuda (cela prend un moment)..."
  ["This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"]="Cette ISO ne contient aucune version GPU de llama, synapd tournera donc sur le CPU
  malgré la carte NVIDIA. (L'ISO doit être construite sur un hôte disposant du
  toolkit CUDA pour que synapse-llama-cuda existe.)"
  ["Video driver install failed — synui may fall back to software rendering"]="L'installation du pilote vidéo a échoué — synui pourrait retomber sur le rendu logiciel"
  ["Video drivers installed"]="Pilotes vidéo installés"
  ["  Enabling GPU inference (synapse-llama-vulkan)..."]="  Activation de l'inférence GPU (synapse-llama-vulkan)..."
  ["This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"]="Cette ISO ne contient aucune version Vulkan de llama, synapd tournera donc sur le CPU
  malgré le GPU AMD/Intel. (Construisez l'ISO sur un hôte avec 'shaderc' +
  vulkan-headers pour que synapse-llama-vulkan existe.)"
  ["Installing Steam and the game stack"]="Installation de Steam et de la pile de jeu"
  ["  Installing steam and the 32-bit runtime libraries..."]="  Installation de steam et des bibliothèques 32 bits..."
  ["Steam installed (native multilib package)"]="Steam installé (paquet multilib natif)"
  ["steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."]="steam n'a pas pu être installé. Le système est complet par ailleurs —
  installez-le plus tard avec 'sudo pacman -S steam' ([multilib] est déjà
  activé dans /etc/pacman.conf)."
  ["  Installing the game stack (overlay, governor, micro-compositor)..."]="  Installation de la pile de jeu (surcouche, gouverneur, micro-compositeur)..."
  ["Game stack installed (mangohud, gamemode, gamescope)"]="Pile de jeu installée (mangohud, gamemode, gamescope)"
  ["The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"]="La pile de jeu n'a pas pu être installée. Steam fonctionne toujours ; la
  surcouche FPS, le gouverneur CPU/GPU et 'synui-game-run --gamescope' non.
  Installez-la plus tard avec :
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
  ["Installing CachyOS Proton"]="Installation de CachyOS Proton"
  ["  Fetching the CachyOS keyring and mirrorlist..."]="  Récupération du trousseau et de la liste de miroirs CachyOS..."
  ["  Trusting the CachyOS master key..."]="  La clé maîtresse CachyOS est déclarée de confiance..."
  ["Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"]="Impossible de récupérer la clé maîtresse CachyOS depuis keyserver.ubuntu.com.
  Le trousseau signé ne peut pas être installé sans elle, CachyOS Proton est
  donc ignoré. À ajouter plus tard avec :  synpkg cachyos enable-repo"
  ["  Master key pinned as expected — trusting it..."]="  Clé maîtresse conforme à l'attendu — elle est déclarée de confiance..."
  ["[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."]="[cachyos] a été ajouté mais ne liste aucun paquet — il est retiré
  pour qu'il ne bloque pas une mise à jour ultérieure."
  ["The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."]="Le trousseau CachyOS ne porte pas la clé maîtresse attendue.
  Refus de lui faire confiance — le dépôt n'a PAS été ajouté."
  ["  Installing proton-cachyos-slr (~340 MB download)..."]="  Installation de proton-cachyos-slr (~340 Mo de téléchargement)..."
  ["CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."]="CachyOS Proton installé — à choisir par jeu dans Steam sous
  Propriétés → Compatibilité, listé comme 'proton-cachyos-… (steam linux runtime)'.
  Steam ne le cherche qu'au démarrage : relancez Steam s'il tourne déjà."
  ["proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"]="proton-cachyos-slr n'a pas pu être installé. Steam et le Proton de Valve
  ne sont pas affectés. Installez-le plus tard avec :
  sudo pacman -S proton-cachyos-slr"
  ["The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"]="Le dépôt [cachyos] n'a pas pu être activé, CachyOS Proton a donc
  été ignoré. Steam fonctionne toujours avec le Proton de Valve. Pour l'ajouter plus tard :
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
  ["Enabling BlackArch"]="Activation de BlackArch"
  ["  Fetching the BlackArch bootstrap..."]="  Récupération de l'amorce BlackArch..."
  ["  Master key pinned as expected — running bootstrap..."]="  Clé maîtresse conforme à l'attendu — l'amorce est lancée..."
  ["blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."]="blackarch-keyring ne s'est pas installé — les rotations de clés
  n'atteindront pas cette machine. Corrigez avec 'sudo pacman -S blackarch-keyring'."
  ["The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."]="Le strap.sh téléchargé ne fixe pas la clé maîtresse attendue de
  BlackArch. Refus de l'exécuter — le dépôt n'a PAS été ajouté."
  ["BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."]="BlackArch n'a pas été activé. Le système est complet par ailleurs ;
  ajoutez-le plus tard avec 'sudo syn arsenal --enable-repo'."
  ["Installing software"]="Installation des logiciels"
  ["That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."]="Cette transaction a échoué — chaque paquet est retenté seul pour que
  ceux qui vont bien arrivent quand même, et que celui qui ne va pas soit nommé."
  ["Software installed"]="Logiciels installés"
  ["Installing Flatpak apps"]="Installation des applications Flatpak"
  ["flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."]="flatpak n'a pas pu être installé — les applications Flatpak sont ignorées.
  Rien d'autre n'est affecté."
  ["Could not add the flathub remote"]="Impossible d'ajouter le dépôt distant flathub"
  ["Flatpak apps installed"]="Applications Flatpak installées"
  ["Configuring System"]="Configuration du système"
  ["  fstab generated"]="  fstab généré"
  ["Swap recorded in fstab"]="Espace d'échange inscrit dans la fstab"
  ["zram configured (compressed swap, half of RAM up to 8 GiB)"]="zram configuré (échange compressé, la moitié de la RAM jusqu'à 8 Gio)"
  ["zram-generator is not installed in the target — no compressed swap"]="zram-generator n'est pas installé dans le système cible — pas d'échange compressé"
  ["  Hostname: synapse"]="  Nom d'hôte : synapse"
  ["Step 7 — Language & Region"]="Étape 7 — Langue et région"
  ["   0) Other — enter a locale by hand"]="   0) Autre — saisir une locale à la main"
  ["Locale (e.g. sv_SE.UTF-8):"]="Locale (par ex. sv_SE.UTF-8) :"
  ["Console keymap (e.g. sv-latin1):"]="Disposition clavier console (par ex. sv-latin1) :"
  ["Step 8 — Timezone"]="Étape 8 — Fuseau horaire"
  ["   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"]="   0) Autre — saisir un nom tzdata quelconque (par ex. Europe/Lisbon)"
  ["tzdata name (Region/City):"]="Nom tzdata (Région/Ville) :"
  ["  Did you mean:"]="  Vouliez-vous dire :"
  ["  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"]="  Choisissez un numéro dans la liste, ou voyez : ls /mnt/usr/share/zoneinfo"
  ["  os-release: copied from live system"]="  os-release : copié depuis le système live"
  ["  issue: copied from live system"]="  issue : copié depuis le système live"
  ["Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"]="Le système de fichiers cible n'est plus inscriptible (erreurs disque ? voyez 'dmesg') — abandon"
  ["sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"]="Le jeu de règles sudoers est invalide après l'écriture des drop-ins — refus de livrer un système incapable de sudo"
  ["Could not relax pam_faillock in /etc/pam.d/system-auth (a tty-less sudo could still lock the account until reboot)."]="Impossible d'assouplir pam_faillock dans /etc/pam.d/system-auth (un sudo sans tty pourrait encore verrouiller le compte jusqu'au redémarrage)."
  ["could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"]="impossible de pré-créer /var/lib/synapse-src — l'outil de mise à jour demandera un mot de passe au premier lancement"
  ["  Desktop: KDE Plasma (SDDM login screen)"]="  Bureau : KDE Plasma (écran de connexion SDDM)"
  ["  GDM: SynapseOS logo on the login screen"]="  GDM : logo SynapseOS sur l'écran de connexion"
  ["  Desktop: GNOME (GDM login screen)"]="  Bureau : GNOME (écran de connexion GDM)"
  ["  Desktop: TTY only"]="  Bureau : TTY seulement"
  ["  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"]="  Bureau : SynapseUI (accueil synui — la connexion reflète l'écran de verrouillage)"
  ["  motd: written for this installation"]="  motd : écrit pour cette installation"
  ["  note: syn-rgb.path is not installed; RGB lights stay off"]="  note : syn-rgb.path n'est pas installé ; les LED RGB restent éteintes"
  ["AI model"]="Modèle d'IA"
  ["  AI model skipped — install one later with: syn model download"]="  Modèle d'IA ignoré — installez-en un plus tard avec : syn model download"
  ["AI model installed"]="Modèle d'IA installé"
  ["  the install, and everything else on the disk is already done."]="  de l'installation, et tout le reste sur le disque est déjà fait."
  ["syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."]="syn-model n'est pas sur le système cible, aucun modèle n'a donc été téléchargé.
  Il fait partie du cœur ; s'il a été désélectionné, l'IA reste inerte."
  ["Configuring Nix"]="Configuration de Nix"
  ["Nix configured — /etc/synapseos/nix"]="Nix configuré — /etc/synapseos/nix"
  ["  That is the download — a few hundred MB before any packages
  you add to home.nix. 'syn nix edit' opens it."]="  C'est cela le téléchargement — quelques centaines de Mo avant tout paquet
  que vous ajoutez à home.nix. 'syn nix edit' l'ouvre."
  ["nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."]="nix est installé, mais le paquet 'syn' n'est pas sur le système cible, le
  configurateur n'a donc pas été mis en place. Nix lui-même fonctionne ;
  la couche /etc/synapseos/nix a besoin de 'syn'."
  ["nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."]="nix n'a pas pu être installé — la couche déclarative n'est pas disponible.
  Installez-la plus tard avec 'sudo pacman -S nix && sudo syn nix init'."
  ["  Generating initramfs..."]="  Génération de l'initramfs..."
  ["mkinitcpio failed — the installed system would not boot"]="mkinitcpio a échoué — le système installé ne démarrerait pas"
  ["initramfs missing after mkinitcpio — the installed system would not boot"]="initramfs absent après mkinitcpio — le système installé ne démarrerait pas"
  ["System configured"]="Système configuré"
  ["Installing Bootloader"]="Installation du chargeur d'amorçage"
  ["grub-install (UEFI) failed"]="grub-install (UEFI) a échoué"
  ["grub-install (BIOS) failed"]="grub-install (BIOS) a échoué"
  ["  Generating GRUB config..."]="  Génération de la configuration GRUB..."
  ["grub-mkconfig failed"]="grub-mkconfig a échoué"
  ["grub.cfg missing after install"]="grub.cfg absent après l'installation"
  ["grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"]="grub.cfg contient un mot de passe GRUB — laissé lisible par root seul, l'application de réglages ne peut donc rien dire des entrées d'amorçage"
  ["  Installing systemd-boot..."]="  Installation de systemd-boot..."
  ["bootctl install failed"]="bootctl install a échoué"
  ["  Registering systemd-boot with the firmware..."]="  Enregistrement de systemd-boot auprès du micrologiciel..."
  ["efibootmgr entry not created — the removable-media path still applies"]="entrée efibootmgr non créée — le chemin « support amovible » s'applique toujours"
  ["could not read the root filesystem UUID"]="impossible de lire l'UUID du système de fichiers racine"
  ["vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"]="vmlinuz-linux n'est pas sur l'ESP — systemd-boot ne trouverait rien à démarrer"
  ["initramfs is not on the ESP — systemd-boot would find nothing to boot"]="l'initramfs n'est pas sur l'ESP — systemd-boot ne trouverait rien à démarrer"
  ["systemd-boot did not install its EFI binary"]="systemd-boot n'a pas installé son binaire EFI"
  ["  Installing limine..."]="  Installation de limine..."
  ["could not copy limine's EFI binary to the ESP"]="impossible de copier le binaire EFI de limine sur l'ESP"
  ["limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"]="limine-mkinitcpio-hook non installé — un noyau installé plus tard n'aura PAS d'entrée d'amorçage"
  ["vmlinuz-linux is not on the ESP — limine would find nothing to boot"]="vmlinuz-linux n'est pas sur l'ESP — limine ne trouverait rien à démarrer"
  ["limine's EFI binary is not on the ESP"]="le binaire EFI de limine n'est pas sur l'ESP"
  ["limine.conf has no kernel entry"]="limine.conf n'a aucune entrée de noyau"
  ["  Verifying the encrypted boot path..."]="  Vérification du chemin d'amorçage chiffré..."
  ["/boot is not a separate mount — an encrypted root needs a plain /boot"]="/boot n'est pas un point de montage séparé — une racine chiffrée a besoin d'un /boot en clair"
  ["/boot is missing from fstab — it would not be mounted after boot"]="/boot est absent de la fstab — il ne serait pas monté après le démarrage"
  ["Encrypted boot path verified"]="Chemin d'amorçage chiffré vérifié"
  ["Configuring snapshots"]="Configuration des instantanés"
  ["snapper's config template is missing — snapshots cannot be configured"]="le modèle de configuration de snapper est absent — les instantanés ne peuvent pas être configurés"
  ["could not write /etc/snapper/configs/root"]="impossible d'écrire /etc/snapper/configs/root"
  ["snapper does not see the 'root' config — snapshots would never be taken"]="snapper ne voit pas la configuration 'root' — aucun instantané ne serait jamais pris"
  ["snapper's root config was not tuned — timeline snapshots would fill the disk"]="la configuration root de snapper n'a pas été ajustée — les instantanés périodiques rempliraient le disque"
  ["could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"]="impossible d'activer grub-btrfsd — les instantanés n'apparaîtront pas automatiquement dans le menu d'amorçage"
  ["Snapshots enabled (snapper + snap-pac, bootable from GRUB)"]="Instantanés activés (snapper + snap-pac, amorçables depuis GRUB)"
  ["could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"]="impossible d'activer limine-snapper-sync — les instantanés n'atteindront pas automatiquement le menu d'amorçage"
  ["could not take the post-install snapshot"]="impossible de prendre l'instantané d'après-installation"
  ["could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"]="impossible d'activer la synchronisation des instantanés au premier démarrage — le menu se remplit après la première mise à jour à la place"
  ["Snapshots enabled (snapper + snap-pac, bootable from limine)"]="Instantanés activés (snapper + snap-pac, amorçables depuis limine)"
  ["Bootloader installed"]="Chargeur d'amorçage installé"
  ["  The root account is locked (no root login / su).
  Note: 3 wrong password attempts lock the account for 10 minutes."]="  Le compte root est verrouillé (pas de connexion root / su).
  Note : 3 mots de passe erronés verrouillent le compte pendant 10 minutes."
  ["You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."]="La phrase secrète de chiffrement sera demandée à chaque démarrage,
  AVANT l'écran de connexion. Il n'y a aucun moyen de la récupérer."
  ["    syn-crypt status                    is this disk encrypted, and how
    sudo syn-crypt change-key           replace the passphrase
    sudo syn-crypt add-key              add a second one
    sudo syn-crypt backup-header FILE   save the LUKS header"]="    syn-crypt status                    ce disque est-il chiffré, et comment
    sudo syn-crypt change-key           remplacer la phrase secrète
    sudo syn-crypt add-key              en ajouter une deuxième
    sudo syn-crypt backup-header FICHIER  sauvegarder l'en-tête LUKS"
  ["  means the data is unrecoverable even with the right passphrase."]="  signifie que les données sont irrécupérables même avec la bonne phrase secrète."
  ["Remove installation media and press ENTER to reboot..."]="Retirez le support d'installation et appuyez sur ENTRÉE pour redémarrer..."
  ["Install SynapseOS     — right here, in this terminal"]="Installer SynapseOS     — ici même, dans ce terminal"
  ["Install graphically   — starts the desktop first"]="Installer en graphique   — démarre le bureau d'abord"
  ["Try the live desktop  — look around; install later"]="Essayer le bureau live   — regarder ; installer plus tard"
  ["Target:"]="Cible :"
  ["ALONGSIDE"]="À CÔTÉ"
  ["ERASE"]="EFFACER"
  ["ADVANCED"]="AVANCÉ"
  ["Encrypt this installation?"]="Chiffrer cette installation ?"
  ["There is no recovery."]="Il n'y a aucune récupération."
  ["Root filesystem"]="Système de fichiers racine"
  ["ext4   — the default. Boring, proven, repairable by anything."]="ext4   — le défaut. Ennuyeux, éprouvé, réparable par n'importe quoi."
  ["btrfs  — snapshots + zstd compression. Roll back a bad update
                    from the boot menu. Uses more RAM and more CPU."]="btrfs  — instantanés + compression zstd. Annuler une mauvaise mise à jour
                    depuis le menu d'amorçage. Plus de RAM et plus de CPU."
  ["xfs    — fast on large files. No snapshots, and it cannot be
                    SHRUNK once created."]="xfs    — rapide sur les gros fichiers. Pas d'instantanés, et il ne peut
                    pas être RÉDUIT une fois créé."
  ["f2fs   — built for flash. Good on SD cards and cheap SSDs;
                    unusual enough that fewer rescue tools know it."]="f2fs   — fait pour la mémoire flash. Bon sur cartes SD et SSD bon marché ;
                    assez rare pour que peu d'outils de secours le connaissent."
  ["Bootloader"]="Chargeur d'amorçage"
  ["GRUB          — the default. Detects other operating systems,
                          and the only one here that can boot a btrfs
                          snapshot."]="GRUB          — le défaut. Détecte les autres systèmes d'exploitation,
                          et le seul ici capable de démarrer un instantané
                          btrfs."
  ["systemd-boot  — minimal. No OS detection, no snapshot menu."]="systemd-boot  — minimal. Aucune détection d'OS, aucun menu d'instantanés."
  ["limine        — modern and fast, and it CAN boot snapshots."]="limine        — moderne et rapide, et il PEUT démarrer des instantanés."
  ["Automatic snapshots?"]="Instantanés automatiques ?"
  ["Review the plan — nothing has been written yet:"]="Vérifiez le plan — rien n'a encore été écrit :"
  ["nothing else is touched"]="rien d'autre n'est touché"
  ["not"]="ne sera pas"
  ["Partition"]="Partitionnez"
  ["now."]="maintenant."
  ["Partitions now on"]="Partitions à présent sur"
  ["These partitions will be FORMATTED"]="Ces partitions seront FORMATÉES"
  ["Full      — Standard + Steam + Nix + more software"]="Complet   — Standard + Steam + Nix + plus de logiciels"
  ["Standard  — the SynapseOS suite, Firefox, AI model,"]="Standard  — la suite SynapseOS, Firefox, modèle d'IA,"
  ["Minimal   — core daemons only: none of the above"]="Minimal   — les démons du cœur seulement : rien de ce qui précède"
  ["Custom    — tick every package yourself, ours and"]="Perso     — cocher chaque paquet soi-même, les nôtres et"
  ["Which AI model should this machine run?"]="Quel modèle d'IA cette machine doit-elle faire tourner ?"
  ["Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"]="Mistral 7B Instruct   ~4,1 Go   recommandé — celui sur lequel SynapseOS est réglé"
  ["Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"]="Phi-3 Mini 4K         ~2,2 Go   moitié moins gros, et nettement plus faible"
  ["Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"]="Qwen2 0.5B            ~0,4 Go   passe partout, et répond en conséquence"
  ["None                            skip it — nothing else changes"]="Aucun                           le passer — rien d'autre ne change"
  ["Installing:"]="Installation de :"
  ["SynapseUI  — AI-native Wayland compositor  (default)"]="SynapseUI  — compositeur Wayland natif pour l'IA  (défaut)"
  ["SynapseUI  — NOT AVAILABLE: synui was not selected"]="SynapseUI  — INDISPONIBLE : synui n'a pas été sélectionné"
  ["KDE Plasma — Full-featured Wayland desktop"]="KDE Plasma — bureau Wayland complet"
  ["GNOME      — Clean, modern Wayland desktop"]="GNOME      — bureau Wayland épuré et moderne"
  ["TTY only   — No GUI (headless/server)"]="TTY seul   — aucune interface (sans écran/serveur)"
  ["Disk:"]="Disque :"
  ["Boot:"]="Amorçage :"
  ["Encrypted:"]="Chiffré :"
  ["Desktop:"]="Bureau :"
  ["User:"]="Utilisateur :"
  ["Hostname:"]="Nom d'hôte :"
  ["Back up the header to another machine."]="Sauvegardez l'en-tête sur une autre machine."
  ["%s is mounted — unmount it first\\n"]="%s est monté — démontez-le d'abord
"
  ["%s is %s MiB — %s needs at least %s MiB\\n"]="%s fait %s Mio — %s a besoin d'au moins %s Mio
"
  ["  Generating %s (a few seconds)...\\n"]="  Génération de %s (quelques secondes)...
"
  ["Language: %s  (%s, keyboard %s)\\n"]="Langue : %s  (%s, clavier %s)
"
  ["  This disk already holds %s partition(s), an EFI System\\n  Partition (%s), and %s GiB of free space.\\n"]="  Ce disque contient déjà %s partition(s), une partition
  système EFI (%s), et %s Gio d'espace libre.
"
  ["    1) Install %s — use the free space, keep everything else\\n"]="    1) Installer %s — utiliser l'espace libre, garder tout le reste
"
  ["    2) %s the whole disk — delete every partition and all data\\n"]="    2) %s tout le disque — supprimer chaque partition et toutes les données
"
  ["    3) %s — partition this disk yourself, then pick the partitions\\n"]="    3) %s — partitionner ce disque soi-même, puis choisir les partitions
"
  ["    1) %s the whole disk — delete every partition and all data  (default)\\n"]="    1) %s tout le disque — supprimer chaque partition et toutes les données  (défaut)
"
  ["    2) %s — partition this disk yourself, then pick the partitions\\n"]="    2) %s — partitionner ce disque soi-même, puis choisir les partitions
"
  ["  %s If you forget the passphrase the data is\\n  gone — no password reset, no support call, nothing.\\n"]="  %s Si vous oubliez la phrase secrète, les données sont
  perdues — pas de réinitialisation, pas d'appel au support, rien.
"
  ["  snapper takes a snapshot before and after every pacman\\n  transaction, and %s grows a menu to boot any of them. A\\n  bad upgrade becomes a reboot instead of a rescue USB.\\n"]="  snapper prend un instantané avant et après chaque transaction
  pacman, et %s obtient un menu pour démarrer n'importe lequel. Une
  mauvaise mise à jour devient un redémarrage au lieu d'une clé de secours.
"
  ["    Disk          : %s\\n"]="    Disque        : %s
"
  ["    Firmware      : %s\\n"]="    Micrologiciel : %s
"
  ["    Filesystem    : %s\\n"]="    Système fich. : %s
"
  ["    Bootloader    : %s\\n"]="    Amorçage      : %s
"
  ["    Separate /boot: %s\\n"]="    /boot séparé : %s
"
  ["    Encryption    : %s\\n"]="    Chiffrement   : %s
"
  ["    Snapshots     : %s\\n"]="    Instantanés   : %s
"
  ["  Encrypting %s (LUKS2)...\\n"]="  Chiffrement de %s (LUKS2)...
"
  ["  Formatting root partition (%s)...\\n"]="  Formatage de la partition racine (%s)...
"
  ["    • KEEP   all %s existing partition(s), including Windows\\n"]="    • GARDER  les %s partition(s) existantes, Windows compris
"
  ["    • REUSE  %s as the EFI partition (mounted, %s formatted)\\n"]="    • RÉUTILISER  %s comme partition EFI (montée, %s formatée)
"
  ["    • CREATE a new ext4 root of ~%s GiB in the free space\\n"]="    • CRÉER   une nouvelle racine ext4 d'environ %s Gio dans l'espace libre
"
  ["  Creating root partition in free space (%sMiB–%sMiB)...\\n"]="  Création de la partition racine dans l'espace libre (%s Mio–%s Mio)...
"
  ["  Formatting new root (%s, ext4)...\\n"]="  Formatage de la nouvelle racine (%s, ext4)...
"
  ["  %s %s %s The installer will re-read the table when you exit.\\n"]="  %s %s %s L'installateur relira la table en quittant.
"
  ["    • a root partition, at least %s GiB\\n"]="    • une partition racine, au moins %s Gio
"
  ["    • a separate /boot of ~1 GiB — %s with this layout cannot read the root\\n"]="    • un /boot séparé d'environ 1 Gio — %s ne peut pas lire la racine avec cette disposition
"
  ["  Starting %s on %s — write your changes before quitting.\\n"]="  Lancement de %s sur %s — écrivez vos changements avant de quitter.
"
  ["    %s is already swap — another system may resume from it.\\n"]="    %s est déjà de l'échange — un autre système en reprend peut-être.
"
  ["  Everything else on %s is left untouched.\\n"]="  Tout le reste sur %s est laissé intact.
"
  ["  Making swap on %s...\\n"]="  Création de l'échange sur %s...
"
  ["  Formatting EFI partition (%s MiB)...\\n"]="  Formatage de la partition EFI (%s Mio)...
"
  ["  NVIDIA GPU detected — installing %s (builds the module, takes a while)...\\n"]="  GPU NVIDIA détecté — installation de %s (compile le module, cela prend un moment)...
"
  ["  Installing video stack: %s %s...\\n"]="  Installation de la pile vidéo : %s %s...
"
  ["  [cachyos] enabled (%s packages available)\\n"]="  [cachyos] activé (%s paquets disponibles)
"
  ["  Language: %s  (chosen at boot)\\n"]="  Langue : %s  (choisie au démarrage)
"
  ["  Locale:   %s   Keyboard: %s (console) / %s (desktop)\\n"]="  Locale :  %s   Clavier : %s (console) / %s (bureau)
"
  ["  Installing fonts (%s)...\\n"]="  Installation des polices (%s)...
"
  ["  Downloading the AI model (%s) — this is the long part of\\n"]="  Téléchargement du modèle d'IA (%s) — c'est la partie longue de
"
  ["  Nothing is built yet. As %s, after the first boot:\\n"]="  Rien n'est encore construit. En tant que %s, après le premier démarrage :
"
  ["  Adding the %s hook to mkinitcpio...\\n"]="  Ajout du hook %s à mkinitcpio...
"
  ["  Installing GRUB (%s)...\\n"]="  Installation de GRUB (%s)...
"
  ["yes — LUKS2 on %s"]="oui — LUKS2 sur %s"
  ["  Admin: use %s with your user password.\\n"]="  Administration : utilisez %s avec votre mot de passe utilisateur.
"
  ["  Manage it later with %s:\\n"]="  À gérer plus tard avec %s :
"
  ["  %s A damaged LUKS header\\n"]="  %s Un en-tête LUKS endommagé
"
  ["%s is on the live/boot device — that is the installer's own media\\n"]="%s est sur le périphérique live/d'amorçage — c'est le support de l'installateur lui-même
"
  ["    %s is already FAT — it may hold another OS's bootloader.\\n"]="    %s est déjà en FAT — il contient peut-être le chargeur d'un autre système.
"
  ["  Creating user '%s'...\\n"]="  Création de l'utilisateur '%s'...
"
  ["  User '%s' created (uid=%s)\\n"]="  Utilisateur '%s' créé (uid=%s)
"
  ["  Log in as '%s' after reboot.\\n"]="  Connectez-vous en tant que '%s' après le redémarrage.
"
  ["  Type '%s' to get started.\\n"]="  Tapez '%s' pour commencer.
"
  ["Install SynapseOS"]="Installer SynapseOS"
  ["SynapseOS packages"]="Paquets SynapseOS"
  ["Everything the system is made of. What you cannot drop is what something else you kept depends on — those are turned back on and named before anything is installed."]="Tout ce dont le système est fait. Ce que vous ne pouvez pas retirer est ce dont dépend autre chose que vous avez gardé — ces éléments sont réactivés et nommés avant toute installation."
  ["SYNAPSE UI — the Wayland desktop"]="SYNAPSE UI — le bureau Wayland"
  ["synapd — the local AI daemon"]="synapd — le démon d'IA local"
  ["synsh — the AI-native shell"]="synsh — le shell natif IA"
  ["synguard + kernel module"]="synguard + module noyau"
  ["synnet — network policy"]="synnet — politique réseau"
  ["Software — the package manager"]="Logiciels — le gestionnaire de paquets"
  ["Files — the file manager"]="Fichiers — le gestionnaire de fichiers"
  ["Terminal (synui depends on it)"]="Terminal (synui en dépend)"
  ["Settings"]="Paramètres"
  ["Disks"]="Disques"
  ["Editor"]="Éditeur"
  ["Calendar"]="Agenda"
  ["File Vault — a locked folder"]="Coffre — un dossier verrouillé"
  ["Disk Cleanup — caches, and secure delete"]="Nettoyage — caches et suppression sécurisée"
  ["syn-update — how fixes arrive"]="syn-update — par où arrivent les correctifs"
  ["syn — the top-level CLI"]="syn — la ligne de commande principale"
  ["syn-model — fetch AI models"]="syn-model — récupérer des modèles d'IA"
  ["syn-confine — the sandbox"]="syn-confine — le bac à sable"
  ["fetch — the About OS readout"]="fetch — le récapitulatif du système"
  ["Arcade — overlay, pads, big screen"]="Arcade — surcouche, manettes, grand écran"
  ["cliamp — the music player"]="cliamp — le lecteur de musique"
  ["Player — playlists, shuffle and history, on mpv"]="Player — listes, aléatoire et historique, sur mpv"
  ["Studio — photo darkroom and video"]="Studio — labo photo et vidéo"
  ["GeForce NOW — cloud gaming in a browser"]="GeForce NOW — jeu en nuage dans un navigateur"
  ["Arsenal — BlackArch browser"]="Arsenal — explorateur BlackArch"
  ["Chibi — voice companion"]="Chibi — compagnon vocal"
  ["Vibe — AI coding assistant"]="Vibe — assistant de code IA"
  ["Animated wallpapers (~317 MB)"]="Fonds d'écran animés (~317 Mo)"
  ["Nexus Chat (pulls in Firefox)"]="Nexus Chat (entraîne Firefox)"
  ["TEPRIS (pulls in Firefox)"]="TEPRIS (entraîne Firefox)"
  ["Web and communication"]="Web et communication"
  ["None of this is ours; every name is in the Arch repositories. Firefox is on by default because an installed SynapseOS used to arrive with no browser at all."]="Rien de tout cela n'est à nous ; chaque nom vient des dépôts Arch. Firefox est activé par défaut parce qu'un SynapseOS installé arrivait autrefois sans aucun navigateur."
  ["Thunderbird — mail"]="Thunderbird — courrier"
  ["KeePassXC — passwords"]="KeePassXC — mots de passe"
  ["Syncthing — file sync"]="Syncthing — synchronisation de fichiers"
  ["LocalSend — send to phone (Flatpak)"]="LocalSend — envoyer au téléphone (Flatpak)"
  ["Audio and video"]="Audio et vidéo"
  ["Office and graphics"]="Bureautique et graphisme"
  ["Development and admin"]="Développement et administration"
  ["VS Code (OSS build)"]="VS Code (version OSS)"
  ["7zip + unrar"]="7zip + unrar"
  ["Games, launchers and helpers"]="Jeux, lanceurs et utilitaires"
  ["Steam is in the options below rather than here: it is the only one that turns on a second architecture and a third repository."]="Steam est dans les options plus bas plutôt qu'ici : c'est le seul qui active une seconde architecture et un troisième dépôt."
  ["Prism — Minecraft"]="Prism — Minecraft"
  ["Dolphin — GameCube/Wii"]="Dolphin — GameCube/Wii"
  ["PPSSPP — PSP"]="PPSSPP — PSP"
  ["Space Cadet Pinball (Flatpak)"]="Space Cadet Pinball (Flatpak)"
  ["GOverlay — MangoHud"]="GOverlay — MangoHud"
  ["AntiMicroX — pad remap"]="AntiMicroX — remappage de manette"
  ["No connection. SynapseOS downloads the base system while it installs, so this needs a working network before it can start."]="Pas de connexion. SynapseOS télécharge le système de base pendant l'installation, il faut donc un réseau qui fonctionne avant de commencer."
  ["Choose a disk to install to."]="Choisissez un disque pour l'installation."
  ["The encryption passphrase needs at least 8 characters."]="La phrase de passe doit faire au moins 8 caractères."
  ["With neither the package manager nor the desktop, this install has no way to add either one back. Keep at least one."]="Sans le gestionnaire de paquets ni le bureau, cette installation n'a aucun moyen de rajouter l'un ou l'autre. Gardez-en au moins un."
  ["A username is lower-case letters, digits, - and _, and cannot start with a digit."]="Un nom d'utilisateur se compose de minuscules, de chiffres, de - et de _, et ne peut pas commencer par un chiffre."
  ["Set a password for the account."]="Définissez un mot de passe pour le compte."
  ["The two passwords do not match."]="Les deux mots de passe ne correspondent pas."
  ["A locale is needed, e.g. en_US.UTF-8."]="Une locale est nécessaire, p. ex. fr_FR.UTF-8."
  ["A timezone is needed, e.g. Europe/Lisbon."]="Un fuseau horaire est nécessaire, p. ex. Europe/Paris."
  ["printing"]="impression"
  ["%1 repo"]="dépôt %1"
  ["Disk"]="Disque"
  ["Mode"]="Mode"
  ["Filesystem"]="Système de fichiers"
  ["%1 on LUKS2"]="%1 sur LUKS2"
  ["%1 + snapshots"]="%1 + instantanés"
  ["Install"]="Installation"
  ["none"]="aucun"
  ["Account"]="Compte"
  ["Desktop"]="Bureau"
  ["Locale"]="Locale"
  ["%1   keys %2 / %3"]="%1   claviers %2 / %3"
  ["Timezone"]="Fuseau horaire"
  ["%1 package(s) — WITHOUT %2"]="%1 paquet(s) — SANS %2"
  ["%1 package(s)"]="%1 paquet(s)"
  ["Software"]="Logiciels"
  ["Options"]="Options"
  ["Could not write the install profile."]="Impossible d'écrire le profil d'installation."
  ["Installation complete."]="Installation terminée."
  ["Installation failed — see the log."]="Échec de l'installation — voir le journal."
  ["No network connection"]="Pas de connexion réseau"
  ["The base system is downloaded while it installs, so this cannot start offline. Plug in a cable or join a network, then press Re-check — the answers on these pages are kept."]="Le système de base se télécharge pendant l'installation : impossible de commencer hors ligne. Branchez un câble ou rejoignez un réseau, puis appuyez sur Revérifier — les réponses de ces pages sont conservées."
  ["Wi-Fi settings"]="Paramètres Wi-Fi"
  ["This asks for a disk, an account and a few preferences, then hands the answers to the same installer the text version runs. Nothing is written to any disk until the last page, and that page says exactly what it is about to do."]="On vous demande un disque, un compte et quelques préférences, puis les réponses sont remises au même installateur que celui de la version texte. Rien n'est écrit sur aucun disque avant la dernière page, et cette page dit exactement ce qui va se passer."
  ["A disk is partitioned and formatted"]="Un disque est partitionné et formaté"
  ["The base system and the SynapseOS packages are installed"]="Le système de base et les paquets SynapseOS sont installés"
  ["An account and a desktop are set up"]="Un compte et un bureau sont configurés"
  ["A bootloader is written"]="Un chargeur d'amorçage est écrit"
  ["Partitioning an existing layout by hand is the text installer's ADVANCED mode — quit this and run \`syn-install\` in a terminal for that."]="Partitionner à la main un agencement existant, c'est le mode ADVANCED de l'installateur texte — quittez cette fenêtre et lancez \`syn-install\` dans un terminal pour cela."
  ["Where should SynapseOS go?"]="Où installer SynapseOS ?"
  ["The installer's own media is listed and cannot be chosen."]="Le support de l'installateur est listé et ne peut pas être choisi."
  ["No disks found."]="Aucun disque trouvé."
  ["Erase the disk"]="Effacer le disque"
  ["every partition and all data"]="toutes les partitions et toutes les données"
  ["Install alongside"]="Installer à côté"
  ["use free space, UEFI only"]="utiliser l'espace libre, UEFI seulement"
  ["Snapshots"]="Instantanés"
  ["btrfs + limine only"]="btrfs + limine uniquement"
  ["Encrypt the disk"]="Chiffrer le disque"
  ["Passphrase"]="Phrase de passe"
  ["8 characters or more"]="8 caractères ou plus"
  ["What should be installed?"]="Que faut-il installer ?"
  ["The SynapseOS core — the compositor, the daemons and the applications it is built on — is installed by every choice here."]="Le cœur de SynapseOS — le compositeur, les démons et les applications sur lesquelles il repose — est installé par chacun des choix ci-dessous."
  ["Full"]="Complet"
  ["Standard + Steam + Nix + more software"]="Standard + Steam + Nix + plus de logiciels"
  ["Standard"]="Standard"
  ["the SynapseOS suite, Firefox, AI model, Bluetooth, printing, Wine, phone"]="la suite SynapseOS, Firefox, modèle d'IA, Bluetooth, impression, Wine, téléphone"
  ["Minimal"]="Minimal"
  ["core daemons only — no apps, no software, no model"]="les démons seuls — aucune application, aucun logiciel, aucun modèle"
  ["Custom"]="Personnalisé"
  ["tick every package yourself, ours and the ordinary software"]="cocher chaque paquet vous-même, les nôtres et les logiciels ordinaires"
  ["Not packages: a repository, an architecture or a service. Each is a decision with a consequence that does not fit on a checkbox above."]="Pas des paquets : un dépôt, une architecture ou un service. Chacun est une décision dont la conséquence ne tient pas dans une case à cocher ci-dessus."
  ["Printing (CUPS)"]="Impression (CUPS)"
  ["Wine — run Windows .exe/.msi"]="Wine — exécuter des .exe/.msi Windows"
  ["KDE Connect — pair a phone"]="KDE Connect — appairer un téléphone"
  ["Steam + game stack + Proton (~3.1 GB)"]="Steam + pile de jeu + Proton (~3,1 Go)"
  ["BlackArch repo — ~5000 tools, none installed"]="Dépôt BlackArch — ~5000 outils, aucun installé"
  ["Nix + Home Manager"]="Nix + Home Manager"
  ["syn-update is off: this machine will have no way to receive another SynapseOS package. Fixing that later means installing it by hand from the ISO, or reinstalling."]="syn-update est désactivé : cette machine n'aura aucun moyen de recevoir un autre paquet SynapseOS. Y remédier plus tard suppose de l'installer à la main depuis l'ISO, ou de réinstaller."
  ["synui is off: this will not be a SynapseOS desktop. The Desktop page offers KDE, GNOME or no GUI."]="synui est désactivé : ce ne sera pas un bureau SynapseOS. La page Bureau propose KDE, GNOME ou aucune interface."
  ["AI model — downloaded during the install"]="Modèle d'IA — téléchargé pendant l'installation"
  ["~4.1 GB — recommended"]="~4,1 Go — recommandé"
  ["~2.2 GB — weaker"]="~2,2 Go — plus faible"
  ["~0.4 GB — much weaker"]="~0,4 Go — bien plus faible"
  ["None"]="Aucun"
  ["AI stays inert"]="l'IA reste inactive"
  ["NVIDIA GPU inference"]="Inférence sur GPU NVIDIA"
  ["the CUDA runtime, ~4.7 GiB"]="l'exécutif CUDA, ~4,7 Gio"
  ["Who is this machine for?"]="À qui est cette machine ?"
  ["Username"]="Nom d'utilisateur"
  ["lower-case, no spaces"]="minuscules, sans espaces"
  ["Full name (optional)"]="Nom complet (facultatif)"
  ["Password"]="Mot de passe"
  ["Password again"]="Mot de passe (confirmation)"
  ["They do not match"]="Ils ne correspondent pas"
  ["the native compositor"]="le compositeur natif"
  ["synui is not selected"]="synui n'est pas sélectionné"
  ["headless"]="sans interface"
  ["Language, keyboard and time"]="Langue, clavier et heure"
  ["Pick a language and the other three follow it. The console keymap and the desktop layout are separate on purpose — Swedish is 'sv-latin1' to the console and 'se' to the desktop — so they can be changed on their own afterwards."]="Choisissez une langue et les trois autres suivent. La disposition console et celle du bureau sont séparées à dessein — le suédois est 'sv-latin1' pour la console et 'se' pour le bureau — afin de pouvoir les changer séparément ensuite."
  ["Language"]="Langue"
  ["sets the keyboard and the fonts too"]="règle aussi le clavier et les polices"
  ["typed by hand — fonts cover as much as possible"]="saisi à la main — les polices couvrent le plus possible"
  ["Sets the locale, both keyboard names and the font pack. Any locale glibc has can be typed instead."]="Règle la locale, les deux noms de clavier et le pack de polices. Toute locale connue de la glibc peut être saisie à la place."
  ["The common zones first, then every name tzdata ships."]="Les fuseaux courants d'abord, puis tous les noms fournis par tzdata."
  ["Console keymap"]="Disposition console"
  ["loadkeys — the text console and the greeter"]="loadkeys — la console texte et l'écran de connexion"
  ["Every keymap this image can load. This one names a file loadkeys has to find, which is why it is not the same list as the desktop layout."]="Toutes les dispositions que cette image peut charger. Celle-ci nomme un fichier que loadkeys doit trouver : ce n'est donc pas la même liste que pour le bureau."
  ["Desktop layout"]="Disposition bureau"
  ["XKB — the compositor"]="XKB — le compositeur"
  ["Desktop keyboard layout"]="Disposition clavier du bureau"
  ["The layouts xkbcommon can compile. 'uk' is a console keymap and not a layout here — the layout is 'gb'."]="Les dispositions que xkbcommon sait compiler. 'uk' est une disposition console et non une disposition ici — celle-ci s'appelle 'gb'."
  ["Read this back"]="Relisez ceci"
  ["Nothing has been written yet. The next button is the one that starts."]="Rien n'a encore été écrit. Le bouton suivant est celui qui lance."
  ["EVERY PARTITION ON %1 WILL BE DELETED"]="TOUTES LES PARTITIONS DE %1 SERONT SUPPRIMÉES"
  ["SynapseOS will be installed into the free space on %1"]="SynapseOS sera installé dans l'espace libre de %1"
  ["SynapseOS is installed"]="SynapseOS est installé"
  ["The install stopped"]="L'installation s'est arrêtée"
  ["Installing SynapseOS"]="Installation de SynapseOS"
  ["Reboot and remove the installation media."]="Redémarrez et retirez le support d'installation."
  ["The log below is the whole story — the last lines say why."]="Le journal ci-dessous raconte tout — les dernières lignes disent pourquoi."
  ["This takes a while: the base system and the packages are downloaded, and an AI model is gigabytes on its own."]="Cela prend du temps : le système de base et les paquets sont téléchargés, et un modèle d'IA pèse à lui seul plusieurs gigaoctets."
  ["Back"]="Retour"
  ["Next"]="Suivant"
  ["Reboot"]="Redémarrer"
  ["Close"]="Fermer"
  ["type to filter, or type a name that is not listed"]="tapez pour filtrer, ou saisissez un nom absent de la liste"
  ["Nothing to list on this image — type the name instead."]="Rien à lister sur cette image — saisissez plutôt le nom."
  ["Nothing matches — the row below uses what you typed."]="Aucune correspondance — la ligne ci-dessous reprend ce que vous avez tapé."
  ["Use “%1” as typed"]="Utiliser « %1 » tel quel"
)
