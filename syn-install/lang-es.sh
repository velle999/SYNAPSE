# Español (es) — syn-install's own words.
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
  ["render.nix is missing — the 'syn' package is not installed here."]="render.nix no está — el paquete 'syn' no está instalado aquí."
  ["locale-gen failed. The live session stays in English; the install is
  unaffected, because it generates the locale inside the target."]="locale-gen falló. La sesión en vivo sigue en inglés; la instalación
  no se ve afectada, porque genera la locale dentro del sistema destino."
  ["  The keyboard, the clock, the fonts and the shell all follow this.
  You can change any of it later."]="  El teclado, la hora, las tipografías y el shell dependen de esto.
  Todo ello se puede cambiar más tarde."
  ["Toggle [numbers, 'all', 'none', Enter = accept]:"]="Alternar [números, 'all', 'none', Intro = aceptar]:"
  ["--config needs a file"]="--config necesita un archivo"
  ["syn-install must be run as root"]="syn-install debe ejecutarse como root"
  ["  SynapseOS is running from the live image."]="  SynapseOS está corriendo desde la imagen en vivo."
  ["Starting the desktop — the installer opens with it."]="Iniciando el escritorio — el instalador se abre con él."
  ["  This installer will:
    1. Partition a disk
    2. Install SynapseOS base system
    3. Install SynapseOS packages
    4. Create user account
    5. Choose desktop environment
    6. Configure system & bootloader"]="  Este instalador va a:
    1. Particionar un disco
    2. Instalar el sistema base de SynapseOS
    3. Instalar los paquetes de SynapseOS
    4. Crear una cuenta de usuario
    5. Elegir un entorno de escritorio
    6. Configurar el sistema y el arranque"
  ["ALL DATA ON THE TARGET DISK WILL BE ERASED"]="TODOS LOS DATOS DEL DISCO DESTINO SERÁN BORRADOS"
  ["Press ENTER to continue or Ctrl+C to abort..."]="Pulse INTRO para continuar o Ctrl+C para abortar..."
  ["Checking network"]="Comprobando la red"
  ["Network is up"]="La red está activa"
  ["  No network detected. Starting NetworkManager..."]="  No se detecta red. Iniciando NetworkManager..."
  ["  No connection — but this machine has Wi-Fi."]="  Sin conexión — pero esta máquina tiene Wi-Fi."
  ["Open the Wi-Fi picker (nmtui)? [Y/n]:"]="¿Abrir el selector de Wi-Fi (nmtui)? [Y/n]:"
  ["No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."]="Sin conexión de red y sin dispositivo Wi-Fi que configurar.
  SynapseOS descarga el sistema base durante la instalación, así que conecte
  un cable y vuelva a ejecutarlo."
  ["Network connected"]="Red conectada"
  ["Step 1 — Select Target Disk"]="Paso 1 — Elegir el disco destino"
  ["  Available disks:"]="  Discos disponibles:"
  ["Target disk (e.g. sda, vda, nvme0n1):"]="Disco destino (p. ej. sda, vda, nvme0n1):"
  ["Target disk is in use. Unmount its partitions and re-run."]="El disco destino está en uso. Desmonte sus particiones y vuelva a ejecutarlo."
  ["Boot mode: UEFI"]="Modo de arranque: UEFI"
  ["Boot mode: BIOS/Legacy"]="Modo de arranque: BIOS/Legacy"
  ["  Encrypts the root filesystem with LUKS2. You will be asked for the
  passphrase at every boot, before the system starts."]="  Cifra el sistema de archivos raíz con LUKS2. Se le pedirá la frase de paso
  en cada arranque, antes de que el sistema se inicie."
  ["Encrypt the disk? [y/N]:"]="¿Cifrar el disco? [y/N]:"
  ["                          With encryption it is the BETTER choice: the
                          kernel lives on the EFI partition and only the
                          initramfs unlocks, so /boot needs no separate
                          unencrypted partition."]="                          Con cifrado es la MEJOR opción: el núcleo
                          vive en la partición EFI y solo el initramfs
                          desbloquea, así que /boot no necesita una
                          partición sin cifrar aparte."
  ["                          It copies each snapshot's kernel onto the EFI
                          partition, so that partition is made much
                          larger when snapshots are enabled."]="                          Copia el núcleo de cada instantánea a la
                          partición EFI, por eso esa partición se crea
                          mucho más grande cuando hay instantáneas."
  ["  Snapshots are cheap but not free: they hold the old copy of
  anything that changes, so a disk near full stays near full."]="  Las instantáneas son baratas pero no gratis: guardan la copia antigua de
  todo lo que cambia, así que un disco casi lleno sigue casi lleno."
  ["Enable snapshots? [Y/n]:"]="¿Activar las instantáneas? [Y/n]:"
  ["mkfs.ext4 is missing from this installer image — /boot cannot be created"]="mkfs.ext4 falta en esta imagen del instalador — no se puede crear /boot"
  ["btrfs is missing from this installer image — subvolumes cannot be created"]="btrfs falta en esta imagen del instalador — no se pueden crear subvolúmenes"
  ["Are these correct? [Y/n]:"]="¿Es correcto? [Y/n]:"
  ["Starting the questions over — the disk has not been touched."]="Volvemos a empezar las preguntas — el disco no se ha tocado."
  ["cryptsetup is not available on this installer image"]="cryptsetup no está disponible en esta imagen del instalador"
  ["Encryption passphrase:"]="Frase de paso del cifrado:"
  ["Repeat passphrase:"]="Repita la frase de paso:"
  ["Empty passphrase — that would leave the disk unprotected."]="Frase de paso vacía — eso dejaría el disco sin protección."
  ["Passphrases did not match — try again."]="Las frases de paso no coinciden — inténtelo de nuevo."
  ["Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."]="La frase de paso tiene menos de 8 caracteres. Una corta vale poco
  frente a alguien que tiene el disco en la mano."
  ["Use it anyway? [y/N]:"]="¿Usarla igualmente? [y/N]:"
  ["Encryption enabled — root will be LUKS2"]="Cifrado activado — la raíz será LUKS2"
  ["cryptsetup open failed — the passphrase did not take"]="cryptsetup open falló — la frase de paso no fue aceptada"
  ["Failed to mount root"]="No se pudo montar la raíz"
  ["  Creating btrfs subvolumes..."]="  Creando subvolúmenes btrfs..."
  ["btrfs: could not create @"]="btrfs: no se pudo crear @"
  ["btrfs: could not create @home"]="btrfs: no se pudo crear @home"
  ["btrfs: could not create @snapshots"]="btrfs: no se pudo crear @snapshots"
  ["btrfs: could not create @var_log"]="btrfs: no se pudo crear @var_log"
  ["btrfs: could not create @pkg"]="btrfs: no se pudo crear @pkg"
  ["could not remount the btrfs root onto @"]="no se pudo volver a montar la raíz btrfs en @"
  ["Failed to mount @"]="No se pudo montar @"
  ["Failed to mount @home"]="No se pudo montar @home"
  ["Failed to mount @snapshots"]="No se pudo montar @snapshots"
  ["Failed to mount @var_log"]="No se pudo montar @var_log"
  ["Failed to mount @pkg"]="No se pudo montar @pkg"
  ["This adds one partition in the free space. Back up anything irreplaceable first."]="Esto añade una partición en el espacio libre. Haga copia de todo lo irreemplazable antes."
  ["Type 'yes' to install alongside:"]="Escriba 'yes' para instalar al lado:"
  ["Aborted"]="Abortado"
  ["Failed to create the root partition"]="No se pudo crear la partición raíz"
  ["Could not identify the new partition after creating it"]="No se pudo identificar la nueva partición tras crearla"
  ["Failed to format root partition"]="No se pudo formatear la partición raíz"
  ["Failed to mount the existing ESP"]="No se pudo montar la ESP existente"
  ["no partition editor on this image (cfdisk, fdisk and parted are all missing)"]="no hay editor de particiones en esta imagen (faltan cfdisk, fdisk y parted)"
  ["  What this install needs:"]="  Lo que esta instalación necesita:"
  ["    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"]="    • una partición de sistema EFI (tipo EF00 / bandera 'esp') — se puede reutilizar una existente"
  ["  Skipping the partition editor (--config)."]="  Se omite el editor de particiones (--config)."
  ["Format it? Everything on it is lost [y/N]:"]="¿Formatearla? Todo lo que contiene se pierde [y/N]:"
  ["Separate /boot partition:"]="Partición /boot aparte:"
  ["Swap partition (blank for none):"]="Partición de intercambio (vacío para ninguna):"
  ["Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"]="¿Rehacerla? Su UUID cambia y rompe la fstab de aquel sistema [y/N]:"
  ["Type 'yes' to format these:"]="Escriba 'yes' para formatarlas:"
  ["  Formatting EFI partition..."]="  Formateando la partición EFI..."
  ["  Formatting /boot partition..."]="  Formateando la partición /boot..."
  ["Failed to mount /boot"]="No se pudo montar /boot"
  ["Type 'yes' to confirm:"]="Escriba 'yes' para confirmar:"
  ["  Creating GPT partition table..."]="  Creando la tabla de particiones GPT..."
  ["Failed to format EFI partition"]="No se pudo formatear la partición EFI"
  ["Failed to format boot partition"]="No se pudo formatear la partición de arranque"
  ["  Creating MBR partition table..."]="  Creando la tabla de particiones MBR..."
  ["Disk partitioned and mounted at /mnt"]="Disco particionado y montado en /mnt"
  ["Step 3 — Installing Base System"]="Paso 3 — Instalando el sistema base"
  ["  Initializing pacman keyring..."]="  Inicializando el llavero de pacman..."
  ["  Running pacstrap (this may take several minutes)..."]="  Ejecutando pacstrap (esto puede tardar varios minutos)..."
  ["pacstrap failed — check network connection"]="pacstrap falló — compruebe la conexión de red"
  ["grub-install not found in chroot — attempting recovery..."]="grub-install no se encuentra en el chroot — intentando recuperarlo..."
  ["Could not install grub into target — check network"]="No se pudo instalar grub en el destino — compruebe la red"
  ["Base system installed"]="Sistema base instalado"
  ["Step 4 — Choose What to Install"]="Paso 4 — Elegir qué instalar"
  ["  What should be installed alongside the SynapseOS core?"]="  ¿Qué debería instalarse junto al núcleo de SynapseOS?"
  ["                   Bluetooth, printing, Wine, phone   (default)"]="                   Bluetooth, impresión, Wine, teléfono   (por defecto)"
  ["                   the ordinary software people install anyway"]="                   el software corriente que se instala igualmente"
  ["  Every preset except Minimal then asks WHICH AI model to download,
  and skipping it is one of the answers."]="  Todos los preajustes salvo Mínimo preguntan luego QUÉ modelo de IA
  descargar, y saltárselo es una de las respuestas."
  ["Full install selected"]="Instalación completa elegida"
  ["Minimal install selected"]="Instalación mínima elegida"
  ["  Two kinds of question. First the packages, as pages of
  checkboxes; then the handful of options that are a whole
  subsystem rather than a package."]="  Dos clases de pregunta. Primero los paquetes, en páginas de
  casillas; después el puñado de opciones que son un subsistema
  entero y no un paquete."
  ["  And the software people install on the first evening anyway.
  All of it is in the Arch repositories; none of it is ours."]="  Y el software que la gente instala la primera tarde de todos modos.
  Todo está en los repositorios de Arch; nada de ello es nuestro."
  ["  The rest is y/n. The default (shown in caps) is Standard."]="  El resto es s/n. El valor por defecto (en mayúsculas) es Estándar."
  ["syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."]="syn-update desmarcado: esta máquina no tendrá manera de recibir
  otro paquete de SynapseOS. Arreglarlo más tarde significa instalarlo a mano
  desde la ISO, o reinstalar."
  ["Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."]="No se conservó ni el escritorio ni el demonio de IA. Eso es un sistema
  Arch con algunas herramientas de SynapseOS encima, lo cual es una respuesta válida —
  pero nada en la documentación describirá la máquina que le queda."
  ["Custom install configured"]="Instalación personalizada configurada"
  ["Standard install selected"]="Instalación estándar elegida"
  ["  synapd loads one model and everything AI in SynapseOS talks to it:
  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,
  over this connection, onto the disk you are installing to."]="  synapd carga un modelo y todo lo de IA en SynapseOS habla con él:
  synsh, el panel de IA del escritorio, Chibi, Vibe. Se descarga ahora,
  por esta conexión, al disco en el que está instalando."
  ["  A smaller model is not just faster and lighter: it follows
  instructions worse. synsh mistakes what you asked for, Vibe's code
  needs more fixing, Chibi loses the thread. Take the default unless
  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."]="  Un modelo más pequeño no es solo más rápido y ligero: sigue peor
  las instrucciones. synsh confunde lo que pidió, el código de Vibe
  necesita más arreglos, Chibi pierde el hilo. Tome el valor por defecto salvo
  que el disco o la RAM digan lo contrario — 7B quiere ~6 GB de RAM libres."
  ["  Whatever you pick, it can be changed later: 'syn model download',
  or Super+C ▸ System ▸ AI model on the desktop."]="  Elija lo que elija, se puede cambiar luego: 'syn model download',
  o Súper+C ▸ Sistema ▸ Modelo de IA en el escritorio."
  ["Install this selection? [Y/n]:"]="¿Instalar esta selección? [Y/n]:"
  ["Choosing again — nothing has been installed yet."]="Elegimos de nuevo — todavía no se ha instalado nada."
  ["Step 4b — Installing SynapseOS"]="Paso 4b — Instalando SynapseOS"
  ["Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."]="No se pudo activar ILoveCandy en /etc/pacman.conf (solo estético)."
  ["  Enabling [multilib] (32-bit repo, needed by Steam)..."]="  Activando [multilib] (repositorio de 32 bits, lo necesita Steam)..."
  ["Could not sync the multilib database — Steam may fail to install"]="No se pudo sincronizar la base de datos multilib — Steam podría no instalarse"
  ["Could not enable [multilib]; Steam will be skipped."]="No se pudo activar [multilib]; se omitirá Steam."
  ["Some SynapseOS packages failed to install — verifying below"]="Algunos paquetes de SynapseOS no se instalaron — se comprueba abajo"
  ["No SynapseOS packages were selected. This will be an Arch system."]="No se seleccionó ningún paquete de SynapseOS. Esto será un sistema Arch."
  ["SynapseOS packages installed"]="Paquetes de SynapseOS instalados"
  ["Component selection recorded in /etc/synapseos/components.conf"]="Selección de componentes registrada en /etc/synapseos/components.conf"
  ["Step 5 — Create User Account"]="Paso 5 — Crear la cuenta de usuario"
  ["  Create a user account for the installed system."]="  Cree una cuenta de usuario para el sistema instalado."
  ["Username [default: syn]:"]="Nombre de usuario [por defecto: syn]:"
  ["Full name (optional):"]="Nombre completo (opcional):"
  ["Password:"]="Contraseña:"
  ["Confirm password:"]="Confirme la contraseña:"
  ["Passwords do not match or are empty — try again"]="Las contraseñas no coinciden o están vacías — inténtelo de nuevo"
  ["Step 6 — Desktop Environment"]="Paso 6 — Entorno de escritorio"
  ["  Choose a desktop environment:"]="  Elija un entorno de escritorio:"
  ["  Installing KDE Plasma..."]="  Instalando KDE Plasma..."
  ["Some KDE packages failed to install"]="Algunos paquetes de KDE no se instalaron"
  ["KDE Plasma installed"]="KDE Plasma instalado"
  ["  Installing GNOME..."]="  Instalando GNOME..."
  ["Some GNOME packages failed to install"]="Algunos paquetes de GNOME no se instalaron"
  ["GNOME installed (session only — SynapseOS apps, not GNOME's)"]="GNOME instalado (solo la sesión — aplicaciones de SynapseOS, no las de GNOME)"
  ["  Installing greetd (login screen) + desktop extras..."]="  Instalando greetd (pantalla de acceso) + extras del escritorio..."
  ["greetd failed to install — boot falls back to getty login"]="greetd no se pudo instalar — el arranque cae al acceso por getty"
  ["SynapseUI selected (included)"]="SynapseUI elegido (incluido)"
  ["Installing Wine"]="Instalando Wine"
  ["Wine installed"]="Wine instalado"
  ["wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."]="wine no se pudo instalar — los .exe/.msi de Windows no funcionarán.
  Instálelo más tarde con 'sudo pacman -S wine wine-mono'."
  ["Configuring Video Driver"]="Configurando el controlador de vídeo"
  ["  Virtual machine — installing mesa (synui uses pixman here)..."]="  Máquina virtual — instalando mesa (aquí synui usa pixman)..."
  ["mesa failed to install"]="mesa no se pudo instalar"
  ["NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"]="La instalación del controlador NVIDIA falló — el sistema arrancaría con
  nouveau y el renderizador de synui nunca se iniciaría"
  ["NVIDIA sleep services enabled (VRAM save/restore)"]="Servicios de suspensión de NVIDIA activados (guardar/restaurar la VRAM)"
  ["Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"]="No se pudieron activar nvidia-{suspend,resume,hibernate} — la suspensión
  puede quedar en negro si más tarde se activa NVreg_PreserveVideoMemoryAllocations"
  ["  synapd can run inference on this GPU instead of the CPU.
  This downloads the CUDA runtime (~4.7 GiB installed)."]="  synapd puede hacer la inferencia en esta GPU en vez de en la CPU.
  Esto descarga el entorno CUDA (~4,7 GiB instalados)."
  ["Enable GPU inference? [Y/n]:"]="¿Activar la inferencia por GPU? [Y/n]:"
  ["Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda"]="Se mantiene la inferencia por CPU. Cámbielo luego con:
  sudo pacman -S synapse-llama-cuda"
  ["  Installing synapse-llama-cuda (this takes a while)..."]="  Instalando synapse-llama-cuda (esto tarda un rato)..."
  ["This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"]="Esta ISO no trae compilación de llama para GPU, así que synapd correrá en la CPU
  pese a la tarjeta NVIDIA. (La ISO debe construirse en un equipo con el toolkit
  de CUDA para que exista synapse-llama-cuda.)"
  ["Video driver install failed — synui may fall back to software rendering"]="La instalación del controlador de vídeo falló — synui podría caer al renderizado por software"
  ["Video drivers installed"]="Controladores de vídeo instalados"
  ["  Enabling GPU inference (synapse-llama-vulkan)..."]="  Activando la inferencia por GPU (synapse-llama-vulkan)..."
  ["This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"]="Esta ISO no trae compilación de llama para Vulkan, así que synapd correrá en la CPU
  pese a la GPU AMD/Intel. (Construya la ISO en un equipo con 'shaderc' +
  vulkan-headers para que exista synapse-llama-vulkan.)"
  ["Installing Steam and the game stack"]="Instalando Steam y el conjunto de juego"
  ["  Installing steam and the 32-bit runtime libraries..."]="  Instalando steam y las bibliotecas de 32 bits..."
  ["Steam installed (native multilib package)"]="Steam instalado (paquete multilib nativo)"
  ["steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."]="steam no se pudo instalar. El sistema está completo por lo demás —
  instálelo luego con 'sudo pacman -S steam' ([multilib] ya está
  activado en /etc/pacman.conf)."
  ["  Installing the game stack (overlay, governor, micro-compositor)..."]="  Instalando el conjunto de juego (superposición, regulador, microcompositor)..."
  ["Game stack installed (mangohud, gamemode, gamescope)"]="Conjunto de juego instalado (mangohud, gamemode, gamescope)"
  ["The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"]="El conjunto de juego no se pudo instalar. Steam sigue funcionando; la
  superposición de FPS, el regulador de CPU/GPU y 'synui-game-run --gamescope'
  no. Instálelo luego con:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
  ["Installing CachyOS Proton"]="Instalando CachyOS Proton"
  ["  Fetching the CachyOS keyring and mirrorlist..."]="  Obteniendo el llavero y la lista de réplicas de CachyOS..."
  ["  Trusting the CachyOS master key..."]="  Confiando en la clave maestra de CachyOS..."
  ["Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"]="No se pudo obtener la clave maestra de CachyOS desde keyserver.ubuntu.com.
  Sin ella no se puede instalar el llavero firmado, así que se omite CachyOS
  Proton. Añádalo luego con:  synpkg cachyos enable-repo"
  ["  Master key pinned as expected — trusting it..."]="  Clave maestra tal como se esperaba — se confía en ella..."
  ["[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."]="[cachyos] se añadió pero no lista ningún paquete — se quita de nuevo
  para que no bloquee una actualización posterior."
  ["The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."]="El llavero de CachyOS no lleva la clave maestra esperada.
  No se confía en él — el repositorio NO se añadió."
  ["  Installing proton-cachyos-slr (~340 MB download)..."]="  Instalando proton-cachyos-slr (~340 MB de descarga)..."
  ["CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."]="CachyOS Proton instalado — elíjalo por juego en Steam en Propiedades →
  Compatibilidad, listado como 'proton-cachyos-… (steam linux runtime)'.
  Steam solo lo busca al arrancar, reinícielo si ya estaba abierto."
  ["proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"]="proton-cachyos-slr no se pudo instalar. Steam y el Proton de Valve
  no se ven afectados. Instálelo luego con:
  sudo pacman -S proton-cachyos-slr"
  ["The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"]="El repositorio [cachyos] no se pudo activar, así que se omitió CachyOS
  Proton. Steam sigue funcionando con el Proton de Valve. Para añadirlo luego:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
  ["Enabling BlackArch"]="Activando BlackArch"
  ["  Fetching the BlackArch bootstrap..."]="  Obteniendo el bootstrap de BlackArch..."
  ["  Master key pinned as expected — running bootstrap..."]="  Clave maestra tal como se esperaba — ejecutando el bootstrap..."
  ["blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."]="blackarch-keyring no se instaló — las rotaciones de claves
  no llegarán a esta máquina. Arréglelo con 'sudo pacman -S blackarch-keyring'."
  ["The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."]="El strap.sh descargado no fija la clave maestra esperada de BlackArch.
  No se ejecuta — el repositorio NO se añadió."
  ["BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."]="BlackArch no se activó. El sistema está completo por lo demás;
  añádalo luego con 'sudo syn arsenal --enable-repo'."
  ["Installing software"]="Instalando software"
  ["That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."]="Esa transacción falló — se reintenta cada paquete por separado para que
  los que están bien lleguen igual, y el que no lo está quede nombrado."
  ["Software installed"]="Software instalado"
  ["Installing Flatpak apps"]="Instalando aplicaciones Flatpak"
  ["flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."]="flatpak no se pudo instalar — se omiten las aplicaciones Flatpak.
  Nada más se ve afectado."
  ["Could not add the flathub remote"]="No se pudo añadir el remoto flathub"
  ["Flatpak apps installed"]="Aplicaciones Flatpak instaladas"
  ["Configuring System"]="Configurando el sistema"
  ["  fstab generated"]="  fstab generada"
  ["Swap recorded in fstab"]="Intercambio registrado en la fstab"
  ["zram configured (compressed swap, half of RAM up to 8 GiB)"]="zram configurado (intercambio comprimido, la mitad de la RAM hasta 8 GiB)"
  ["zram-generator is not installed in the target — no compressed swap"]="zram-generator no está instalado en el destino — sin intercambio comprimido"
  ["  Hostname: synapse"]="  Nombre de equipo: synapse"
  ["Step 7 — Language & Region"]="Paso 7 — Idioma y región"
  ["   0) Other — enter a locale by hand"]="   0) Otro — introducir una locale a mano"
  ["Locale (e.g. sv_SE.UTF-8):"]="Locale (p. ej. sv_SE.UTF-8):"
  ["Console keymap (e.g. sv-latin1):"]="Mapa de teclado de consola (p. ej. sv-latin1):"
  ["Step 8 — Timezone"]="Paso 8 — Zona horaria"
  ["   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"]="   0) Otra — introducir cualquier nombre de tzdata (p. ej. Europe/Lisbon)"
  ["tzdata name (Region/City):"]="Nombre tzdata (Región/Ciudad):"
  ["  Did you mean:"]="  ¿Quiso decir:"
  ["  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"]="  Elija un número de la lista, o vea: ls /mnt/usr/share/zoneinfo"
  ["  os-release: copied from live system"]="  os-release: copiado del sistema en vivo"
  ["  issue: copied from live system"]="  issue: copiado del sistema en vivo"
  ["Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"]="El sistema de archivos destino ya no se puede escribir (¿errores de disco? mire 'dmesg') — abortando"
  ["sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"]="El conjunto de reglas de sudoers es inválido tras escribir los drop-ins — no se entrega un sistema que no pueda usar sudo"
  ["Could not relax pam_faillock in /etc/pam.d/system-auth (a tty-less sudo could still lock the account until reboot)."]="No se pudo relajar pam_faillock en /etc/pam.d/system-auth (un sudo sin tty todavía podría bloquear la cuenta hasta reiniciar)."
  ["could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"]="no se pudo crear de antemano /var/lib/synapse-src — el actualizador pedirá una contraseña la primera vez"
  ["  Desktop: KDE Plasma (SDDM login screen)"]="  Escritorio: KDE Plasma (pantalla de acceso SDDM)"
  ["  GDM: SynapseOS logo on the login screen"]="  GDM: logotipo de SynapseOS en la pantalla de acceso"
  ["  Desktop: GNOME (GDM login screen)"]="  Escritorio: GNOME (pantalla de acceso GDM)"
  ["  Desktop: TTY only"]="  Escritorio: solo TTY"
  ["  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"]="  Escritorio: SynapseUI (greeter de synui — el acceso refleja la pantalla de bloqueo)"
  ["  motd: written for this installation"]="  motd: escrito para esta instalación"
  ["  note: syn-rgb.path is not installed; RGB lights stay off"]="  nota: syn-rgb.path no está instalado; las luces RGB quedan apagadas"
  ["AI model"]="Modelo de IA"
  ["  AI model skipped — install one later with: syn model download"]="  Modelo de IA omitido — instale uno luego con: syn model download"
  ["AI model installed"]="Modelo de IA instalado"
  ["  the install, and everything else on the disk is already done."]="  la instalación, y todo lo demás del disco ya está hecho."
  ["syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."]="syn-model no está en el destino, así que no se descargó ningún modelo.
  Forma parte del conjunto básico; si se desmarcó, la IA queda inerte."
  ["Configuring Nix"]="Configurando Nix"
  ["Nix configured — /etc/synapseos/nix"]="Nix configurado — /etc/synapseos/nix"
  ["  That is the download — a few hundred MB before any packages
  you add to home.nix. 'syn nix edit' opens it."]="  Esa es la descarga — unos cientos de MB antes de cualquier paquete
  que añada a home.nix. 'syn nix edit' lo abre."
  ["nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."]="nix está instalado, pero el paquete 'syn' no está en el destino, así que
  el configurador no se preparó. Nix en sí funciona;
  la capa /etc/synapseos/nix necesita 'syn'."
  ["nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."]="nix no se pudo instalar — la capa declarativa no está disponible.
  Instálela luego con 'sudo pacman -S nix && sudo syn nix init'."
  ["  Generating initramfs..."]="  Generando el initramfs..."
  ["mkinitcpio failed — the installed system would not boot"]="mkinitcpio falló — el sistema instalado no arrancaría"
  ["initramfs missing after mkinitcpio — the installed system would not boot"]="falta el initramfs tras mkinitcpio — el sistema instalado no arrancaría"
  ["System configured"]="Sistema configurado"
  ["Installing Bootloader"]="Instalando el gestor de arranque"
  ["grub-install (UEFI) failed"]="grub-install (UEFI) falló"
  ["grub-install (BIOS) failed"]="grub-install (BIOS) falló"
  ["  Generating GRUB config..."]="  Generando la configuración de GRUB..."
  ["grub-mkconfig failed"]="grub-mkconfig falló"
  ["grub.cfg missing after install"]="falta grub.cfg tras la instalación"
  ["grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"]="grub.cfg lleva una contraseña de GRUB — queda solo para root, así que la aplicación de ajustes no puede informar de las entradas de arranque"
  ["  Installing systemd-boot..."]="  Instalando systemd-boot..."
  ["bootctl install failed"]="bootctl install falló"
  ["  Registering systemd-boot with the firmware..."]="  Registrando systemd-boot en el firmware..."
  ["efibootmgr entry not created — the removable-media path still applies"]="entrada de efibootmgr no creada — sigue aplicándose la ruta de medio extraíble"
  ["could not read the root filesystem UUID"]="no se pudo leer el UUID del sistema de archivos raíz"
  ["vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"]="vmlinuz-linux no está en la ESP — systemd-boot no encontraría nada que arrancar"
  ["initramfs is not on the ESP — systemd-boot would find nothing to boot"]="el initramfs no está en la ESP — systemd-boot no encontraría nada que arrancar"
  ["systemd-boot did not install its EFI binary"]="systemd-boot no instaló su binario EFI"
  ["  Installing limine..."]="  Instalando limine..."
  ["could not copy limine's EFI binary to the ESP"]="no se pudo copiar el binario EFI de limine a la ESP"
  ["limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"]="limine-mkinitcpio-hook no instalado — un núcleo instalado más tarde NO tendrá entrada de arranque"
  ["vmlinuz-linux is not on the ESP — limine would find nothing to boot"]="vmlinuz-linux no está en la ESP — limine no encontraría nada que arrancar"
  ["limine's EFI binary is not on the ESP"]="el binario EFI de limine no está en la ESP"
  ["limine.conf has no kernel entry"]="limine.conf no tiene ninguna entrada de núcleo"
  ["  Verifying the encrypted boot path..."]="  Verificando la ruta de arranque cifrada..."
  ["/boot is not a separate mount — an encrypted root needs a plain /boot"]="/boot no es un montaje aparte — una raíz cifrada necesita un /boot sin cifrar"
  ["/boot is missing from fstab — it would not be mounted after boot"]="/boot falta en la fstab — no se montaría tras el arranque"
  ["Encrypted boot path verified"]="Ruta de arranque cifrada verificada"
  ["Configuring snapshots"]="Configurando las instantáneas"
  ["snapper's config template is missing — snapshots cannot be configured"]="falta la plantilla de configuración de snapper — no se pueden configurar las instantáneas"
  ["could not write /etc/snapper/configs/root"]="no se pudo escribir /etc/snapper/configs/root"
  ["snapper does not see the 'root' config — snapshots would never be taken"]="snapper no ve la configuración 'root' — nunca se tomarían instantáneas"
  ["snapper's root config was not tuned — timeline snapshots would fill the disk"]="la configuración root de snapper no se ajustó — las instantáneas periódicas llenarían el disco"
  ["could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"]="no se pudo activar grub-btrfsd — las instantáneas no aparecerán solas en el menú de arranque"
  ["Snapshots enabled (snapper + snap-pac, bootable from GRUB)"]="Instantáneas activadas (snapper + snap-pac, arrancables desde GRUB)"
  ["could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"]="no se pudo activar limine-snapper-sync — las instantáneas no llegarán solas al menú de arranque"
  ["could not take the post-install snapshot"]="no se pudo tomar la instantánea posterior a la instalación"
  ["could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"]="no se pudo activar la sincronización de instantáneas del primer arranque — el menú se completa tras la primera actualización"
  ["Snapshots enabled (snapper + snap-pac, bootable from limine)"]="Instantáneas activadas (snapper + snap-pac, arrancables desde limine)"
  ["Bootloader installed"]="Gestor de arranque instalado"
  ["  The root account is locked (no root login / su).
  Note: 3 wrong password attempts lock the account for 10 minutes."]="  La cuenta root está bloqueada (sin acceso root / su).
  Nota: 3 contraseñas erróneas bloquean la cuenta 10 minutos."
  ["You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."]="Se le pedirá la frase de paso del cifrado en cada arranque,
  ANTES de la pantalla de acceso. No hay manera de recuperarla."
  ["    syn-crypt status                    is this disk encrypted, and how
    sudo syn-crypt change-key           replace the passphrase
    sudo syn-crypt add-key              add a second one
    sudo syn-crypt backup-header FILE   save the LUKS header"]="    syn-crypt status                    ¿está cifrado este disco, y cómo?
    sudo syn-crypt change-key           sustituir la frase de paso
    sudo syn-crypt add-key              añadir una segunda
    sudo syn-crypt backup-header ARCHIVO  guardar la cabecera LUKS"
  ["  means the data is unrecoverable even with the right passphrase."]="  significa que los datos son irrecuperables incluso con la frase de paso correcta."
  ["Remove installation media and press ENTER to reboot..."]="Retire el medio de instalación y pulse INTRO para reiniciar..."
  ["Install SynapseOS     — right here, in this terminal"]="Instalar SynapseOS      — aquí mismo, en esta terminal"
  ["Install graphically   — starts the desktop first"]="Instalar en gráfico     — arranca primero el escritorio"
  ["Try the live desktop  — look around; install later"]="Probar el escritorio    — echar un vistazo; instalar luego"
  ["Target:"]="Destino:"
  ["ALONGSIDE"]="AL LADO"
  ["ERASE"]="BORRAR"
  ["ADVANCED"]="AVANZADO"
  ["Encrypt this installation?"]="¿Cifrar esta instalación?"
  ["There is no recovery."]="No hay recuperación posible."
  ["Root filesystem"]="Sistema de archivos raíz"
  ["ext4   — the default. Boring, proven, repairable by anything."]="ext4   — el predeterminado. Aburrido, probado, reparable por cualquier cosa."
  ["btrfs  — snapshots + zstd compression. Roll back a bad update
                    from the boot menu. Uses more RAM and more CPU."]="btrfs  — instantáneas + compresión zstd. Deshacer una mala actualización
                    desde el menú de arranque. Más RAM y más CPU."
  ["xfs    — fast on large files. No snapshots, and it cannot be
                    SHRUNK once created."]="xfs    — rápido con archivos grandes. Sin instantáneas, y no se puede
                    REDUCIR una vez creado."
  ["f2fs   — built for flash. Good on SD cards and cheap SSDs;
                    unusual enough that fewer rescue tools know it."]="f2fs   — hecho para memoria flash. Bueno en tarjetas SD y SSD baratos;
                    lo bastante raro como para que pocas herramientas de rescate lo conozcan."
  ["Bootloader"]="Gestor de arranque"
  ["GRUB          — the default. Detects other operating systems,
                          and the only one here that can boot a btrfs
                          snapshot."]="GRUB          — el predeterminado. Detecta otros sistemas operativos,
                          y el único aquí capaz de arrancar una
                          instantánea btrfs."
  ["systemd-boot  — minimal. No OS detection, no snapshot menu."]="systemd-boot  — mínimo. Sin detección de SO, sin menú de instantáneas."
  ["limine        — modern and fast, and it CAN boot snapshots."]="limine        — moderno y rápido, y SÍ puede arrancar instantáneas."
  ["Automatic snapshots?"]="¿Instantáneas automáticas?"
  ["Review the plan — nothing has been written yet:"]="Revise el plan — todavía no se ha escrito nada:"
  ["nothing else is touched"]="no se toca nada más"
  ["not"]="no se"
  ["Partition"]="Particione"
  ["now."]="ahora."
  ["Partitions now on"]="Particiones ahora en"
  ["These partitions will be FORMATTED"]="Estas particiones serán FORMATEADAS"
  ["Full      — Standard + Steam + Nix + more software"]="Completa  — Estándar + Steam + Nix + más software"
  ["Standard  — the SynapseOS suite, Firefox, AI model,"]="Estándar  — la suite SynapseOS, Firefox, modelo de IA,"
  ["Minimal   — core daemons only: none of the above"]="Mínima    — solo los demonios básicos: nada de lo anterior"
  ["Custom    — tick every package yourself, ours and"]="A medida  — marcar cada paquete uno mismo, los nuestros y"
  ["Which AI model should this machine run?"]="¿Qué modelo de IA debería ejecutar esta máquina?"
  ["Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"]="Mistral 7B Instruct   ~4,1 GB   recomendado — con el que SynapseOS está ajustado"
  ["Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"]="Phi-3 Mini 4K         ~2,2 GB   la mitad de tamaño, y notablemente más flojo"
  ["Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"]="Qwen2 0.5B            ~0,4 GB   cabe en cualquier sitio, y responde acorde"
  ["None                            skip it — nothing else changes"]="Ninguno                         omitirlo — nada más cambia"
  ["Installing:"]="Instalando:"
  ["SynapseUI  — AI-native Wayland compositor  (default)"]="SynapseUI  — compositor Wayland nativo de IA  (por defecto)"
  ["SynapseUI  — NOT AVAILABLE: synui was not selected"]="SynapseUI  — NO DISPONIBLE: synui no fue seleccionado"
  ["KDE Plasma — Full-featured Wayland desktop"]="KDE Plasma — escritorio Wayland completo"
  ["GNOME      — Clean, modern Wayland desktop"]="GNOME      — escritorio Wayland limpio y moderno"
  ["TTY only   — No GUI (headless/server)"]="Solo TTY   — sin interfaz gráfica (sin pantalla/servidor)"
  ["Disk:"]="Disco:"
  ["Boot:"]="Arranque:"
  ["Encrypted:"]="Cifrado:"
  ["Desktop:"]="Escritorio:"
  ["User:"]="Usuario:"
  ["Hostname:"]="Nombre de equipo:"
  ["Back up the header to another machine."]="Haga copia de la cabecera en otra máquina."
  ["%s is mounted — unmount it first\\n"]="%s está montado — desmóntelo primero
"
  ["%s is %s MiB — %s needs at least %s MiB\\n"]="%s tiene %s MiB — %s necesita al menos %s MiB
"
  ["  Generating %s (a few seconds)...\\n"]="  Generando %s (unos segundos)...
"
  ["Language: %s  (%s, keyboard %s)\\n"]="Idioma: %s  (%s, teclado %s)
"
  ["  This disk already holds %s partition(s), an EFI System\\n  Partition (%s), and %s GiB of free space.\\n"]="  Este disco ya contiene %s partición(es), una partición de
  sistema EFI (%s) y %s GiB de espacio libre.
"
  ["    1) Install %s — use the free space, keep everything else\\n"]="    1) Instalar %s — usar el espacio libre, conservar todo lo demás
"
  ["    2) %s the whole disk — delete every partition and all data\\n"]="    2) %s el disco entero — borrar cada partición y todos los datos
"
  ["    3) %s — partition this disk yourself, then pick the partitions\\n"]="    3) %s — particionar este disco usted mismo, luego elegir las particiones
"
  ["    1) %s the whole disk — delete every partition and all data  (default)\\n"]="    1) %s el disco entero — borrar cada partición y todos los datos  (por defecto)
"
  ["    2) %s — partition this disk yourself, then pick the partitions\\n"]="    2) %s — particionar este disco usted mismo, luego elegir las particiones
"
  ["  %s If you forget the passphrase the data is\\n  gone — no password reset, no support call, nothing.\\n"]="  %s Si olvida la frase de paso los datos están
  perdidos — sin restablecerla, sin llamada al soporte, nada.
"
  ["  snapper takes a snapshot before and after every pacman\\n  transaction, and %s grows a menu to boot any of them. A\\n  bad upgrade becomes a reboot instead of a rescue USB.\\n"]="  snapper toma una instantánea antes y después de cada transacción
  de pacman, y %s obtiene un menú para arrancar cualquiera de ellas. Una
  mala actualización pasa a ser un reinicio en vez de un USB de rescate.
"
  ["    Disk          : %s\\n"]="    Disco         : %s
"
  ["    Firmware      : %s\\n"]="    Firmware      : %s
"
  ["    Filesystem    : %s\\n"]="    Sist. archivos: %s
"
  ["    Bootloader    : %s\\n"]="    Arranque      : %s
"
  ["    Separate /boot: %s\\n"]="    /boot aparte : %s
"
  ["    Encryption    : %s\\n"]="    Cifrado       : %s
"
  ["    Snapshots     : %s\\n"]="    Instantáneas  : %s
"
  ["  Encrypting %s (LUKS2)...\\n"]="  Cifrando %s (LUKS2)...
"
  ["  Formatting root partition (%s)...\\n"]="  Formateando la partición raíz (%s)...
"
  ["    • KEEP   all %s existing partition(s), including Windows\\n"]="    • CONSERVAR las %s partición(es) existentes, Windows incluido
"
  ["    • REUSE  %s as the EFI partition (mounted, %s formatted)\\n"]="    • REUTILIZAR %s como partición EFI (montada, %s formateada)
"
  ["    • CREATE a new ext4 root of ~%s GiB in the free space\\n"]="    • CREAR    una raíz ext4 nueva de ~%s GiB en el espacio libre
"
  ["  Creating root partition in free space (%sMiB–%sMiB)...\\n"]="  Creando la partición raíz en el espacio libre (%s MiB–%s MiB)...
"
  ["  Formatting new root (%s, ext4)...\\n"]="  Formateando la nueva raíz (%s, ext4)...
"
  ["  %s %s %s The installer will re-read the table when you exit.\\n"]="  %s %s %s El instalador releerá la tabla al salir.
"
  ["    • a root partition, at least %s GiB\\n"]="    • una partición raíz, de al menos %s GiB
"
  ["    • a separate /boot of ~1 GiB — %s with this layout cannot read the root\\n"]="    • un /boot aparte de ~1 GiB — %s con esta disposición no puede leer la raíz
"
  ["  Starting %s on %s — write your changes before quitting.\\n"]="  Iniciando %s en %s — escriba sus cambios antes de salir.
"
  ["    %s is already swap — another system may resume from it.\\n"]="    %s ya es intercambio — otro sistema podría reanudar desde él.
"
  ["  Everything else on %s is left untouched.\\n"]="  Todo lo demás en %s queda intacto.
"
  ["  Making swap on %s...\\n"]="  Creando el intercambio en %s...
"
  ["  Formatting EFI partition (%s MiB)...\\n"]="  Formateando la partición EFI (%s MiB)...
"
  ["  NVIDIA GPU detected — installing %s (builds the module, takes a while)...\\n"]="  GPU NVIDIA detectada — instalando %s (compila el módulo, tarda un rato)...
"
  ["  Installing video stack: %s %s...\\n"]="  Instalando la pila de vídeo: %s %s...
"
  ["  [cachyos] enabled (%s packages available)\\n"]="  [cachyos] activado (%s paquetes disponibles)
"
  ["  Language: %s  (chosen at boot)\\n"]="  Idioma: %s  (elegido en el arranque)
"
  ["  Locale:   %s   Keyboard: %s (console) / %s (desktop)\\n"]="  Locale:   %s   Teclado: %s (consola) / %s (escritorio)
"
  ["  Installing fonts (%s)...\\n"]="  Instalando tipografías (%s)...
"
  ["  Downloading the AI model (%s) — this is the long part of\\n"]="  Descargando el modelo de IA (%s) — esta es la parte larga de
"
  ["  Nothing is built yet. As %s, after the first boot:\\n"]="  Todavía no hay nada construido. Como %s, tras el primer arranque:
"
  ["  Adding the %s hook to mkinitcpio...\\n"]="  Añadiendo el hook %s a mkinitcpio...
"
  ["  Installing GRUB (%s)...\\n"]="  Instalando GRUB (%s)...
"
  ["yes — LUKS2 on %s"]="sí — LUKS2 en %s"
  ["  Admin: use %s with your user password.\\n"]="  Administración: use %s con su contraseña de usuario.
"
  ["  Manage it later with %s:\\n"]="  Gestiónelo luego con %s:
"
  ["  %s A damaged LUKS header\\n"]="  %s Una cabecera LUKS dañada
"
  ["%s is on the live/boot device — that is the installer's own media\\n"]="%s está en el dispositivo en vivo/de arranque — ese es el propio medio del instalador
"
  ["    %s is already FAT — it may hold another OS's bootloader.\\n"]="    %s ya es FAT — puede contener el gestor de arranque de otro sistema.
"
  ["  Creating user '%s'...\\n"]="  Creando el usuario '%s'...
"
  ["  User '%s' created (uid=%s)\\n"]="  Usuario '%s' creado (uid=%s)
"
  ["  Log in as '%s' after reboot.\\n"]="  Inicie sesión como '%s' tras reiniciar.
"
  ["  Type '%s' to get started.\\n"]="  Escriba '%s' para empezar.
"
  ["Install SynapseOS"]="Instalar SynapseOS"
  ["SynapseOS packages"]="Paquetes de SynapseOS"
  ["Everything the system is made of. What you cannot drop is what something else you kept depends on — those are turned back on and named before anything is installed."]="Todo aquello de lo que está hecho el sistema. Lo que no puedes quitar es de lo que depende algo más que has conservado — eso se vuelve a activar y se nombra antes de instalar nada."
  ["SYNAPSE UI — the Wayland desktop"]="SYNAPSE UI — el escritorio Wayland"
  ["synapd — the local AI daemon"]="synapd — el demonio de IA local"
  ["synsh — the AI-native shell"]="synsh — la shell nativa de IA"
  ["synguard + kernel module"]="synguard + módulo del núcleo"
  ["synnet — network policy"]="synnet — política de red"
  ["Software — the package manager"]="Software — el gestor de paquetes"
  ["Files — the file manager"]="Archivos — el gestor de archivos"
  ["Terminal (synui depends on it)"]="Terminal (synui lo necesita)"
  ["Settings"]="Ajustes"
  ["Disks"]="Discos"
  ["Editor"]="Editor"
  ["Calendar"]="Calendario"
  ["File Vault — a locked folder"]="Caja fuerte — una carpeta cerrada"
  ["Disk Cleanup — caches, and secure delete"]="Limpieza — cachés y borrado seguro"
  ["syn-update — how fixes arrive"]="syn-update — por donde llegan las correcciones"
  ["syn — the top-level CLI"]="syn — la línea de órdenes principal"
  ["syn-model — fetch AI models"]="syn-model — descargar modelos de IA"
  ["syn-confine — the sandbox"]="syn-confine — el aislamiento"
  ["fetch — the About OS readout"]="fetch — el resumen del sistema"
  ["Arcade — overlay, pads, big screen"]="Arcade — superposición, mandos, pantalla grande"
  ["cliamp — the music player"]="cliamp — el reproductor de música"
  ["Player — playlists, shuffle and history, on mpv"]="Player — listas, aleatorio e historial, sobre mpv"
  ["Studio — photo darkroom and video"]="Studio — laboratorio de fotos y vídeo"
  ["GeForce NOW — cloud gaming in a browser"]="GeForce NOW — juego en la nube desde el navegador"
  ["Arsenal — BlackArch browser"]="Arsenal — explorador de BlackArch"
  ["Chibi — voice companion"]="Chibi — acompañante por voz"
  ["Vibe — AI coding assistant"]="Vibe — asistente de programación con IA"
  ["Animated wallpapers (~317 MB)"]="Fondos animados (~317 MB)"
  ["Nexus Chat (pulls in Firefox)"]="Nexus Chat (arrastra Firefox)"
  ["TEPRIS (pulls in Firefox)"]="TEPRIS (arrastra Firefox)"
  ["Web and communication"]="Web y comunicación"
  ["None of this is ours; every name is in the Arch repositories. Firefox is on by default because an installed SynapseOS used to arrive with no browser at all."]="Nada de esto es nuestro; cada nombre está en los repositorios de Arch. Firefox viene activado porque un SynapseOS instalado solía llegar sin ningún navegador."
  ["Thunderbird — mail"]="Thunderbird — correo"
  ["KeePassXC — passwords"]="KeePassXC — contraseñas"
  ["Syncthing — file sync"]="Syncthing — sincronización de archivos"
  ["LocalSend — send to phone (Flatpak)"]="LocalSend — enviar al móvil (Flatpak)"
  ["Audio and video"]="Audio y vídeo"
  ["Office and graphics"]="Oficina y gráficos"
  ["Development and admin"]="Desarrollo y administración"
  ["VS Code (OSS build)"]="VS Code (compilación OSS)"
  ["7zip + unrar"]="7zip + unrar"
  ["Games, launchers and helpers"]="Juegos, lanzadores y utilidades"
  ["Steam is in the options below rather than here: it is the only one that turns on a second architecture and a third repository."]="Steam está en las opciones de abajo y no aquí: es el único que activa una segunda arquitectura y un tercer repositorio."
  ["Prism — Minecraft"]="Prism — Minecraft"
  ["Dolphin — GameCube/Wii"]="Dolphin — GameCube/Wii"
  ["PPSSPP — PSP"]="PPSSPP — PSP"
  ["Space Cadet Pinball (Flatpak)"]="Space Cadet Pinball (Flatpak)"
  ["GOverlay — MangoHud"]="GOverlay — MangoHud"
  ["AntiMicroX — pad remap"]="AntiMicroX — reasignar el mando"
  ["Welcome"]="Bienvenida"
  ["Disk"]="Disco"
  ["Software"]="Software"
  ["Account"]="Cuenta"
  ["Region"]="Región"
  ["Summary"]="Resumen"
  ["Install"]="Instalación"
  ["the installer's own media"]="el propio medio del instalador"
  ["%1 GiB — SynapseOS needs at least %2 GiB"]="%1 GiB — SynapseOS necesita al menos %2 GiB"
  ["No connection. SynapseOS downloads the base system while it installs, so this needs a working network before it can start."]="Sin conexión. SynapseOS descarga el sistema base mientras instala, así que hace falta una red que funcione antes de empezar."
  ["Choose a disk to install to."]="Elige un disco donde instalar."
  ["The encryption passphrase needs at least 8 characters."]="La frase de cifrado necesita al menos 8 caracteres."
  ["With neither the package manager nor the desktop, this install has no way to add either one back. Keep at least one."]="Sin el gestor de paquetes ni el escritorio, esta instalación no tiene forma de recuperar ninguno de los dos. Conserva al menos uno."
  ["A username is lower-case letters, digits, - and _, and cannot start with a digit."]="Un nombre de usuario lleva minúsculas, dígitos, - y _, y no puede empezar por un dígito."
  ["Set a password for the account."]="Pon una contraseña para la cuenta."
  ["The two passwords do not match."]="Las dos contraseñas no coinciden."
  ["A locale is needed, e.g. en_US.UTF-8."]="Hace falta una configuración regional, p. ej. es_ES.UTF-8."
  ["A timezone is needed, e.g. Europe/Lisbon."]="Hace falta una zona horaria, p. ej. Europe/Madrid."
  ["printing"]="impresión"
  ["%1 repo"]="repo de %1"
  ["Mode"]="Modo"
  ["Filesystem"]="Sistema de archivos"
  ["%1 on LUKS2"]="%1 sobre LUKS2"
  ["%1 + snapshots"]="%1 + instantáneas"
  ["none"]="ninguno"
  ["Desktop"]="Escritorio"
  ["Locale"]="Configuración regional"
  ["%1   keys %2 / %3"]="%1   teclados %2 / %3"
  ["Timezone"]="Zona horaria"
  ["%1 package(s) — WITHOUT %2"]="%1 paquete(s) — SIN %2"
  ["%1 package(s)"]="%1 paquete(s)"
  ["Options"]="Opciones"
  ["Could not write the install profile."]="No se pudo escribir el perfil de instalación."
  ["Installation complete."]="Instalación terminada."
  ["Installation failed — see the log."]="La instalación falló — mira el registro."
  ["No network connection"]="Sin conexión de red"
  ["The base system is downloaded while it installs, so this cannot start offline. Plug in a cable or join a network, then press Re-check — the answers on these pages are kept."]="El sistema base se descarga durante la instalación, así que no puede empezar sin conexión. Conecta un cable o únete a una red y pulsa Volver a comprobar — las respuestas de estas páginas se conservan."
  ["Checking…"]="Comprobando…"
  ["Re-check"]="Volver a comprobar"
  ["Wi-Fi settings"]="Ajustes de wifi"
  ["This asks for a disk, an account and a few preferences, then hands the answers to the same installer the text version runs. Nothing is written to any disk until the last page, and that page says exactly what it is about to do."]="Aquí se te piden un disco, una cuenta y unas cuantas preferencias, y luego las respuestas pasan al mismo instalador que ejecuta la versión de texto. No se escribe nada en ningún disco hasta la última página, y esa página dice exactamente lo que va a hacer."
  ["A disk is partitioned and formatted"]="Se particiona y formatea un disco"
  ["The base system and the SynapseOS packages are installed"]="Se instalan el sistema base y los paquetes de SynapseOS"
  ["An account and a desktop are set up"]="Se configuran una cuenta y un escritorio"
  ["A bootloader is written"]="Se escribe un gestor de arranque"
  ["Partitioning an existing layout by hand is the text installer's ADVANCED mode — quit this and run \`syn-install\` in a terminal for that."]="Particionar a mano una distribución existente es el modo ADVANCED del instalador de texto — cierra esto y ejecuta \`syn-install\` en una terminal para eso."
  ["Where should SynapseOS go?"]="¿Dónde va SynapseOS?"
  ["The installer's own media is listed and cannot be chosen."]="El medio del propio instalador aparece en la lista y no se puede elegir."
  ["No disks found."]="No se encontraron discos."
  ["Erase the disk"]="Borrar el disco"
  ["every partition and all data"]="todas las particiones y todos los datos"
  ["Install alongside"]="Instalar al lado"
  ["use free space, UEFI only"]="usar el espacio libre, solo UEFI"
  ["Snapshots"]="Instantáneas"
  ["btrfs + limine only"]="solo btrfs + limine"
  ["Encrypt the disk"]="Cifrar el disco"
  ["Passphrase"]="Frase de paso"
  ["8 characters or more"]="8 caracteres o más"
  ["What should be installed?"]="¿Qué se instala?"
  ["The SynapseOS core — the compositor, the daemons and the applications it is built on — is installed by every choice here."]="El núcleo de SynapseOS — el compositor, los demonios y las aplicaciones sobre las que se apoya — lo instala cualquiera de estas opciones."
  ["Full"]="Completa"
  ["Standard + Steam + Nix + more software"]="Estándar + Steam + Nix + más software"
  ["Standard"]="Estándar"
  ["the SynapseOS suite, Firefox, AI model, Bluetooth, printing, Wine, phone"]="la suite SynapseOS, Firefox, modelo de IA, Bluetooth, impresión, Wine, móvil"
  ["Minimal"]="Mínima"
  ["core daemons only — no apps, no software, no model"]="solo los demonios — sin aplicaciones, sin software, sin modelo"
  ["Custom"]="A medida"
  ["tick every package yourself, ours and the ordinary software"]="marcar cada paquete tú mismo, los nuestros y el software corriente"
  ["(required)"]="(obligatorio)"
  ["Not packages: a repository, an architecture or a service. Each is a decision with a consequence that does not fit on a checkbox above."]="No son paquetes: un repositorio, una arquitectura o un servicio. Cada uno es una decisión con una consecuencia que no cabe en una casilla de arriba."
  ["Printing (CUPS)"]="Impresión (CUPS)"
  ["Wine — run Windows .exe/.msi"]="Wine — ejecutar .exe/.msi de Windows"
  ["KDE Connect — pair a phone"]="KDE Connect — emparejar un móvil"
  ["Steam + game stack + Proton (~3.1 GB)"]="Steam + pila de juego + Proton (~3,1 GB)"
  ["BlackArch repo — ~5000 tools, none installed"]="Repo de BlackArch — ~5000 herramientas, ninguna instalada"
  ["Nix + Home Manager"]="Nix + Home Manager"
  ["syn-update is off: this machine will have no way to receive another SynapseOS package. Fixing that later means installing it by hand from the ISO, or reinstalling."]="syn-update está apagado: esta máquina no tendrá forma de recibir otro paquete de SynapseOS. Arreglarlo después significa instalarlo a mano desde la ISO, o reinstalar."
  ["synui is off: this will not be a SynapseOS desktop. The Desktop page offers KDE, GNOME or no GUI."]="synui está apagado: esto no será un escritorio SynapseOS. La página Escritorio ofrece KDE, GNOME o ninguna interfaz."
  ["AI model — downloaded during the install"]="Modelo de IA — se descarga durante la instalación"
  ["~4.1 GB — recommended"]="~4,1 GB — recomendado"
  ["~2.2 GB — weaker"]="~2,2 GB — más flojo"
  ["~0.4 GB — much weaker"]="~0,4 GB — mucho más flojo"
  ["None"]="Ninguno"
  ["AI stays inert"]="la IA se queda inactiva"
  ["NVIDIA GPU inference"]="Inferencia en GPU NVIDIA"
  ["the CUDA runtime, ~4.7 GiB"]="el entorno CUDA, ~4,7 GiB"
  ["Who is this machine for?"]="¿Para quién es esta máquina?"
  ["Username"]="Nombre de usuario"
  ["lower-case, no spaces"]="minúsculas, sin espacios"
  ["Full name (optional)"]="Nombre completo (opcional)"
  ["Password"]="Contraseña"
  ["Password again"]="Repite la contraseña"
  ["They do not match"]="No coinciden"
  ["the native compositor"]="el compositor propio"
  ["synui is not selected"]="synui no está seleccionado"
  ["headless"]="sin interfaz"
  ["Language, keyboard and time"]="Idioma, teclado y hora"
  ["Pick a language and the other three follow it. The console keymap and the desktop layout are separate on purpose — Swedish is 'sv-latin1' to the console and 'se' to the desktop — so they can be changed on their own afterwards."]="Elige un idioma y los otros tres lo siguen. El teclado de la consola y la distribución del escritorio están separados a propósito — el sueco es 'sv-latin1' para la consola y 'se' para el escritorio — para poder cambiarlos por separado después."
  ["Language"]="Idioma"
  ["sets the keyboard and the fonts too"]="también ajusta el teclado y las tipografías"
  ["typed by hand — fonts cover as much as possible"]="escrito a mano — las tipografías cubren lo que pueden"
  ["Sets the locale, both keyboard names and the font pack. Any locale glibc has can be typed instead."]="Ajusta la configuración regional, los dos nombres de teclado y el paquete de tipografías. También puedes escribir cualquier locale que tenga glibc."
  ["The common zones first, then every name tzdata ships."]="Primero las zonas habituales, luego todos los nombres que trae tzdata."
  ["Console keymap"]="Teclado de consola"
  ["loadkeys — the text console and the greeter"]="loadkeys — la consola de texto y la pantalla de inicio"
  ["Every keymap this image can load. This one names a file loadkeys has to find, which is why it is not the same list as the desktop layout."]="Todos los teclados que esta imagen puede cargar. Este nombra un archivo que loadkeys debe encontrar, por eso no es la misma lista que la del escritorio."
  ["Desktop layout"]="Distribución del escritorio"
  ["XKB — the compositor"]="XKB — el compositor"
  ["Desktop keyboard layout"]="Distribución de teclado del escritorio"
  ["The layouts xkbcommon can compile. 'uk' is a console keymap and not a layout here — the layout is 'gb'."]="Las distribuciones que xkbcommon sabe compilar. 'uk' es un teclado de consola y aquí no es una distribución — la distribución se llama 'gb'."
  ["Read this back"]="Léelo otra vez"
  ["Nothing has been written yet. The next button is the one that starts."]="Todavía no se ha escrito nada. El siguiente botón es el que empieza."
  ["EVERY PARTITION ON %1 WILL BE DELETED"]="TODAS LAS PARTICIONES DE %1 SE BORRARÁN"
  ["SynapseOS will be installed into the free space on %1"]="SynapseOS se instalará en el espacio libre de %1"
  ["SynapseOS is installed"]="SynapseOS está instalado"
  ["The install stopped"]="La instalación se detuvo"
  ["Installing SynapseOS"]="Instalando SynapseOS"
  ["Reboot and remove the installation media."]="Reinicia y retira el medio de instalación."
  ["The log below is the whole story — the last lines say why."]="El registro de abajo lo cuenta todo — las últimas líneas dicen por qué."
  ["This takes a while: the base system and the packages are downloaded, and an AI model is gigabytes on its own."]="Esto tarda: se descargan el sistema base y los paquetes, y un modelo de IA ya son varios gigabytes por sí solo."
  ["Back"]="Atrás"
  ["Next"]="Siguiente"
  ["Reboot"]="Reiniciar"
  ["Close"]="Cerrar"
  ["type to filter, or type a name that is not listed"]="escribe para filtrar, o escribe un nombre que no esté en la lista"
  ["Nothing to list on this image — type the name instead."]="Nada que listar en esta imagen — escribe el nombre."
  ["Nothing matches — the row below uses what you typed."]="Nada coincide — la fila de abajo usa lo que escribiste."
  ["Use “%1” as typed"]="Usar «%1» tal cual"
)
