# Português (pt) — syn-install's own words.
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
  ["render.nix is missing — the 'syn' package is not installed here."]="render.nix está faltando — o pacote 'syn' não está instalado aqui."
  ["locale-gen failed. The live session stays in English; the install is
  unaffected, because it generates the locale inside the target."]="locale-gen falhou. A sessão ao vivo continua em inglês; a instalação
  não é afetada, porque gera a locale dentro do sistema de destino."
  ["  The keyboard, the clock, the fonts and the shell all follow this.
  You can change any of it later."]="  O teclado, o relógio, as fontes e o shell seguem esta escolha.
  Tudo isso pode ser mudado depois."
  ["Toggle [numbers, 'all', 'none', Enter = accept]:"]="Alternar [números, 'all', 'none', Enter = aceitar]:"
  ["--config needs a file"]="--config precisa de um arquivo"
  ["syn-install must be run as root"]="syn-install precisa ser executado como root"
  ["  SynapseOS is running from the live image."]="  O SynapseOS está rodando a partir da imagem ao vivo."
  ["Starting the desktop — the installer opens with it."]="Iniciando a área de trabalho — o instalador abre junto."
  ["  This installer will:
    1. Partition a disk
    2. Install SynapseOS base system
    3. Install SynapseOS packages
    4. Create user account
    5. Choose desktop environment
    6. Configure system & bootloader"]="  Este instalador vai:
    1. Particionar um disco
    2. Instalar o sistema base do SynapseOS
    3. Instalar os pacotes do SynapseOS
    4. Criar uma conta de usuário
    5. Escolher um ambiente de área de trabalho
    6. Configurar o sistema e o gerenciador de inicialização"
  ["ALL DATA ON THE TARGET DISK WILL BE ERASED"]="TODOS OS DADOS DO DISCO DE DESTINO SERÃO APAGADOS"
  ["Press ENTER to continue or Ctrl+C to abort..."]="Pressione ENTER para continuar ou Ctrl+C para abortar..."
  ["Checking network"]="Verificando a rede"
  ["Network is up"]="A rede está ativa"
  ["  No network detected. Starting NetworkManager..."]="  Nenhuma rede detectada. Iniciando o NetworkManager..."
  ["  No connection — but this machine has Wi-Fi."]="  Sem conexão — mas esta máquina tem Wi-Fi."
  ["Open the Wi-Fi picker (nmtui)? [Y/n]:"]="Abrir o seletor de Wi-Fi (nmtui)? [Y/n]:"
  ["No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."]="Sem conexão de rede e sem dispositivo Wi-Fi para configurar.
  O SynapseOS baixa o sistema base durante a instalação, então conecte um
  cabo e execute de novo."
  ["Network connected"]="Rede conectada"
  ["Step 1 — Select Target Disk"]="Passo 1 — Escolher o disco de destino"
  ["  Available disks:"]="  Discos disponíveis:"
  ["Target disk (e.g. sda, vda, nvme0n1):"]="Disco de destino (ex.: sda, vda, nvme0n1):"
  ["Target disk is in use. Unmount its partitions and re-run."]="O disco de destino está em uso. Desmonte suas partições e execute de novo."
  ["Boot mode: UEFI"]="Modo de inicialização: UEFI"
  ["Boot mode: BIOS/Legacy"]="Modo de inicialização: BIOS/Legado"
  ["  Encrypts the root filesystem with LUKS2. You will be asked for the
  passphrase at every boot, before the system starts."]="  Criptografa o sistema de arquivos raiz com LUKS2. A frase secreta será
  pedida a cada inicialização, antes de o sistema começar."
  ["Encrypt the disk? [y/N]:"]="Criptografar o disco? [y/N]:"
  ["                          With encryption it is the BETTER choice: the
                          kernel lives on the EFI partition and only the
                          initramfs unlocks, so /boot needs no separate
                          unencrypted partition."]="                          Com criptografia é a MELHOR escolha: o kernel
                          fica na partição EFI e só o initramfs
                          destrava, então /boot não precisa de uma
                          partição separada sem criptografia."
  ["                          It copies each snapshot's kernel onto the EFI
                          partition, so that partition is made much
                          larger when snapshots are enabled."]="                          Ele copia o kernel de cada instantâneo para a
                          partição EFI, por isso essa partição é criada
                          bem maior quando há instantâneos."
  ["  Snapshots are cheap but not free: they hold the old copy of
  anything that changes, so a disk near full stays near full."]="  Instantâneos são baratos, mas não de graça: eles guardam a cópia antiga
  de tudo que muda, então um disco quase cheio continua quase cheio."
  ["Enable snapshots? [Y/n]:"]="Ativar instantâneos? [Y/n]:"
  ["mkfs.ext4 is missing from this installer image — /boot cannot be created"]="mkfs.ext4 está faltando nesta imagem do instalador — /boot não pode ser criado"
  ["btrfs is missing from this installer image — subvolumes cannot be created"]="btrfs está faltando nesta imagem do instalador — subvolumes não podem ser criados"
  ["Are these correct? [Y/n]:"]="Está correto? [Y/n]:"
  ["Starting the questions over — the disk has not been touched."]="Recomeçando as perguntas — o disco não foi tocado."
  ["cryptsetup is not available on this installer image"]="cryptsetup não está disponível nesta imagem do instalador"
  ["Encryption passphrase:"]="Frase secreta da criptografia:"
  ["Repeat passphrase:"]="Repita a frase secreta:"
  ["Empty passphrase — that would leave the disk unprotected."]="Frase secreta vazia — isso deixaria o disco sem proteção."
  ["Passphrases did not match — try again."]="As frases secretas não coincidem — tente de novo."
  ["Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."]="A frase secreta tem menos de 8 caracteres. Uma curta vale pouco
  contra alguém que está com o disco na mão."
  ["Use it anyway? [y/N]:"]="Usar mesmo assim? [y/N]:"
  ["Encryption enabled — root will be LUKS2"]="Criptografia ativada — a raiz será LUKS2"
  ["cryptsetup open failed — the passphrase did not take"]="cryptsetup open falhou — a frase secreta não foi aceita"
  ["Failed to mount root"]="Falha ao montar a raiz"
  ["  Creating btrfs subvolumes..."]="  Criando subvolumes btrfs..."
  ["btrfs: could not create @"]="btrfs: não foi possível criar @"
  ["btrfs: could not create @home"]="btrfs: não foi possível criar @home"
  ["btrfs: could not create @snapshots"]="btrfs: não foi possível criar @snapshots"
  ["btrfs: could not create @var_log"]="btrfs: não foi possível criar @var_log"
  ["btrfs: could not create @pkg"]="btrfs: não foi possível criar @pkg"
  ["could not remount the btrfs root onto @"]="não foi possível remontar a raiz btrfs em @"
  ["Failed to mount @"]="Falha ao montar @"
  ["Failed to mount @home"]="Falha ao montar @home"
  ["Failed to mount @snapshots"]="Falha ao montar @snapshots"
  ["Failed to mount @var_log"]="Falha ao montar @var_log"
  ["Failed to mount @pkg"]="Falha ao montar @pkg"
  ["This adds one partition in the free space. Back up anything irreplaceable first."]="Isso adiciona uma partição no espaço livre. Faça backup do que for insubstituível antes."
  ["Type 'yes' to install alongside:"]="Digite 'yes' para instalar ao lado:"
  ["Aborted"]="Abortado"
  ["Failed to create the root partition"]="Falha ao criar a partição raiz"
  ["Could not identify the new partition after creating it"]="Não foi possível identificar a nova partição depois de criá-la"
  ["Failed to format root partition"]="Falha ao formatar a partição raiz"
  ["Failed to mount the existing ESP"]="Falha ao montar a ESP existente"
  ["no partition editor on this image (cfdisk, fdisk and parted are all missing)"]="nenhum editor de partições nesta imagem (cfdisk, fdisk e parted estão todos faltando)"
  ["  What this install needs:"]="  O que esta instalação precisa:"
  ["    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"]="    • uma partição de sistema EFI (tipo EF00 / flag 'esp') — uma existente pode ser reaproveitada"
  ["  Skipping the partition editor (--config)."]="  Pulando o editor de partições (--config)."
  ["Format it? Everything on it is lost [y/N]:"]="Formatar? Tudo que estiver nela é perdido [y/N]:"
  ["Separate /boot partition:"]="Partição /boot separada:"
  ["Swap partition (blank for none):"]="Partição de swap (vazio para nenhuma):"
  ["Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"]="Refazer? O UUID muda e quebra a fstab daquele sistema [y/N]:"
  ["Type 'yes' to format these:"]="Digite 'yes' para formatar estas:"
  ["  Formatting EFI partition..."]="  Formatando a partição EFI..."
  ["  Formatting /boot partition..."]="  Formatando a partição /boot..."
  ["Failed to mount /boot"]="Falha ao montar /boot"
  ["Type 'yes' to confirm:"]="Digite 'yes' para confirmar:"
  ["  Creating GPT partition table..."]="  Criando a tabela de partições GPT..."
  ["Failed to format EFI partition"]="Falha ao formatar a partição EFI"
  ["Failed to format boot partition"]="Falha ao formatar a partição de inicialização"
  ["  Creating MBR partition table..."]="  Criando a tabela de partições MBR..."
  ["Disk partitioned and mounted at /mnt"]="Disco particionado e montado em /mnt"
  ["Step 3 — Installing Base System"]="Passo 3 — Instalando o sistema base"
  ["  Initializing pacman keyring..."]="  Inicializando o chaveiro do pacman..."
  ["  Running pacstrap (this may take several minutes)..."]="  Executando pacstrap (isso pode levar vários minutos)..."
  ["pacstrap failed — check network connection"]="pacstrap falhou — verifique a conexão de rede"
  ["grub-install not found in chroot — attempting recovery..."]="grub-install não encontrado no chroot — tentando recuperar..."
  ["Could not install grub into target — check network"]="Não foi possível instalar o grub no destino — verifique a rede"
  ["Base system installed"]="Sistema base instalado"
  ["Step 4 — Choose What to Install"]="Passo 4 — Escolher o que instalar"
  ["  What should be installed alongside the SynapseOS core?"]="  O que deve ser instalado junto com o núcleo do SynapseOS?"
  ["                   Bluetooth, printing, Wine, phone   (default)"]="                   Bluetooth, impressão, Wine, telefone   (padrão)"
  ["                   the ordinary software people install anyway"]="                   os programas comuns que as pessoas instalam de qualquer jeito"
  ["  Every preset except Minimal then asks WHICH AI model to download,
  and skipping it is one of the answers."]="  Todas as opções exceto Mínima perguntam depois QUAL modelo de IA
  baixar, e pular isso é uma das respostas."
  ["Full install selected"]="Instalação completa escolhida"
  ["Minimal install selected"]="Instalação mínima escolhida"
  ["  Two kinds of question. First the packages, as pages of
  checkboxes; then the handful of options that are a whole
  subsystem rather than a package."]="  Dois tipos de pergunta. Primeiro os pacotes, em páginas de
  caixas de seleção; depois o punhado de opções que são um
  subsistema inteiro em vez de um pacote."
  ["  And the software people install on the first evening anyway.
  All of it is in the Arch repositories; none of it is ours."]="  E os programas que as pessoas instalam na primeira noite de qualquer jeito.
  Tudo está nos repositórios do Arch; nada disso é nosso."
  ["  The rest is y/n. The default (shown in caps) is Standard."]="  O resto é s/n. O padrão (em maiúsculas) é Padrão."
  ["syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."]="syn-update desmarcado: esta máquina não terá como receber
  outro pacote do SynapseOS. Corrigir isso depois significa instalá-lo à mão
  a partir da ISO, ou reinstalar."
  ["Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."]="Nem a área de trabalho nem o daemon de IA foram mantidos. Isso é um sistema
  Arch com algumas ferramentas do SynapseOS, o que é uma resposta aceita —
  mas nada na documentação vai descrever a máquina que você recebe."
  ["Custom install configured"]="Instalação personalizada configurada"
  ["Standard install selected"]="Instalação padrão escolhida"
  ["  synapd loads one model and everything AI in SynapseOS talks to it:
  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,
  over this connection, onto the disk you are installing to."]="  O synapd carrega um modelo e tudo de IA no SynapseOS fala com ele:
  synsh, o painel de IA da área de trabalho, Chibi, Vibe. Ele é baixado agora,
  por esta conexão, para o disco em que você está instalando."
  ["  A smaller model is not just faster and lighter: it follows
  instructions worse. synsh mistakes what you asked for, Vibe's code
  needs more fixing, Chibi loses the thread. Take the default unless
  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."]="  Um modelo menor não é só mais rápido e leve: ele segue pior
  as instruções. O synsh entende errado o que você pediu, o código do Vibe
  precisa de mais conserto, o Chibi perde o fio. Fique com o padrão a não ser
  que o disco ou a RAM digam o contrário — 7B quer ~6 GB de RAM livre."
  ["  Whatever you pick, it can be changed later: 'syn model download',
  or Super+C ▸ System ▸ AI model on the desktop."]="  Seja qual for a escolha, dá para mudar depois: 'syn model download',
  ou Super+C ▸ Sistema ▸ Modelo de IA na área de trabalho."
  ["Install this selection? [Y/n]:"]="Instalar esta seleção? [Y/n]:"
  ["Choosing again — nothing has been installed yet."]="Escolhendo de novo — nada foi instalado ainda."
  ["Step 4b — Installing SynapseOS"]="Passo 4b — Instalando o SynapseOS"
  ["Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."]="Não foi possível ativar o ILoveCandy em /etc/pacman.conf (apenas estético)."
  ["  Enabling [multilib] (32-bit repo, needed by Steam)..."]="  Ativando [multilib] (repositório de 32 bits, necessário para o Steam)..."
  ["Could not sync the multilib database — Steam may fail to install"]="Não foi possível sincronizar a base multilib — o Steam pode falhar ao instalar"
  ["Could not enable [multilib]; Steam will be skipped."]="Não foi possível ativar [multilib]; o Steam será pulado."
  ["Some SynapseOS packages failed to install — verifying below"]="Alguns pacotes do SynapseOS falharam ao instalar — verificando abaixo"
  ["No SynapseOS packages were selected. This will be an Arch system."]="Nenhum pacote do SynapseOS foi selecionado. Isto será um sistema Arch."
  ["SynapseOS packages installed"]="Pacotes do SynapseOS instalados"
  ["Component selection recorded in /etc/synapseos/components.conf"]="Seleção de componentes registrada em /etc/synapseos/components.conf"
  ["Step 5 — Create User Account"]="Passo 5 — Criar a conta de usuário"
  ["  Create a user account for the installed system."]="  Crie uma conta de usuário para o sistema instalado."
  ["Username [default: syn]:"]="Nome de usuário [padrão: syn]:"
  ["Full name (optional):"]="Nome completo (opcional):"
  ["Password:"]="Senha:"
  ["Confirm password:"]="Confirme a senha:"
  ["Passwords do not match or are empty — try again"]="As senhas não coincidem ou estão vazias — tente de novo"
  ["Step 6 — Desktop Environment"]="Passo 6 — Ambiente de área de trabalho"
  ["  Choose a desktop environment:"]="  Escolha um ambiente de área de trabalho:"
  ["  Installing KDE Plasma..."]="  Instalando o KDE Plasma..."
  ["Some KDE packages failed to install"]="Alguns pacotes do KDE falharam ao instalar"
  ["KDE Plasma installed"]="KDE Plasma instalado"
  ["  Installing GNOME..."]="  Instalando o GNOME..."
  ["Some GNOME packages failed to install"]="Alguns pacotes do GNOME falharam ao instalar"
  ["GNOME installed (session only — SynapseOS apps, not GNOME's)"]="GNOME instalado (só a sessão — aplicativos do SynapseOS, não os do GNOME)"
  ["  Installing greetd (login screen) + desktop extras..."]="  Instalando greetd (tela de login) + extras da área de trabalho..."
  ["greetd failed to install — boot falls back to getty login"]="greetd falhou ao instalar — a inicialização volta ao login pelo getty"
  ["SynapseUI selected (included)"]="SynapseUI escolhido (incluído)"
  ["Installing Wine"]="Instalando o Wine"
  ["Wine installed"]="Wine instalado"
  ["wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."]="o wine falhou ao instalar — .exe/.msi do Windows não vão rodar.
  Instale depois com 'sudo pacman -S wine wine-mono'."
  ["Configuring Video Driver"]="Configurando o driver de vídeo"
  ["  Virtual machine — installing mesa (synui uses pixman here)..."]="  Máquina virtual — instalando mesa (aqui o synui usa pixman)..."
  ["mesa failed to install"]="mesa falhou ao instalar"
  ["NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"]="A instalação do driver NVIDIA falhou — o sistema iniciaria no
  nouveau e o renderizador do synui nunca começaria"
  ["NVIDIA sleep services enabled (VRAM save/restore)"]="Serviços de suspensão da NVIDIA ativados (salvar/restaurar a VRAM)"
  ["Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"]="Não foi possível ativar nvidia-{suspend,resume,hibernate} — a suspensão
  pode ficar em tela preta se NVreg_PreserveVideoMemoryAllocations for ativado depois"
  ["  synapd can run inference on this GPU instead of the CPU.
  This downloads the CUDA runtime (~4.7 GiB installed)."]="  O synapd pode fazer a inferência nesta GPU em vez da CPU.
  Isso baixa o ambiente CUDA (~4,7 GiB instalados)."
  ["Enable GPU inference? [Y/n]:"]="Ativar a inferência por GPU? [Y/n]:"
  ["Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda"]="Mantendo a inferência por CPU. Mude depois com:
  sudo pacman -S synapse-llama-cuda"
  ["  Installing synapse-llama-cuda (this takes a while)..."]="  Instalando synapse-llama-cuda (isso demora um pouco)..."
  ["This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"]="Esta ISO não traz compilação de llama para GPU, então o synapd vai rodar na CPU
  apesar da placa NVIDIA. (A ISO precisa ser construída num host com o toolkit
  CUDA para que synapse-llama-cuda exista.)"
  ["Video driver install failed — synui may fall back to software rendering"]="A instalação do driver de vídeo falhou — o synui pode cair na renderização por software"
  ["Video drivers installed"]="Drivers de vídeo instalados"
  ["  Enabling GPU inference (synapse-llama-vulkan)..."]="  Ativando a inferência por GPU (synapse-llama-vulkan)..."
  ["This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"]="Esta ISO não traz compilação de llama para Vulkan, então o synapd vai rodar na CPU
  apesar da GPU AMD/Intel. (Construa a ISO num host com 'shaderc' +
  vulkan-headers para que synapse-llama-vulkan exista.)"
  ["Installing Steam and the game stack"]="Instalando o Steam e o conjunto de jogos"
  ["  Installing steam and the 32-bit runtime libraries..."]="  Instalando steam e as bibliotecas de 32 bits..."
  ["Steam installed (native multilib package)"]="Steam instalado (pacote multilib nativo)"
  ["steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."]="o steam falhou ao instalar. O sistema está completo no resto —
  instale depois com 'sudo pacman -S steam' ([multilib] já está
  ativado em /etc/pacman.conf)."
  ["  Installing the game stack (overlay, governor, micro-compositor)..."]="  Instalando o conjunto de jogos (sobreposição, regulador, microcompositor)..."
  ["Game stack installed (mangohud, gamemode, gamescope)"]="Conjunto de jogos instalado (mangohud, gamemode, gamescope)"
  ["The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"]="O conjunto de jogos falhou ao instalar. O Steam continua funcionando; a
  sobreposição de FPS, o regulador de CPU/GPU e 'synui-game-run --gamescope'
  não. Instale depois com:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
  ["Installing CachyOS Proton"]="Instalando o CachyOS Proton"
  ["  Fetching the CachyOS keyring and mirrorlist..."]="  Buscando o chaveiro e a lista de espelhos do CachyOS..."
  ["  Trusting the CachyOS master key..."]="  Confiando na chave mestra do CachyOS..."
  ["Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"]="Não foi possível buscar a chave mestra do CachyOS em keyserver.ubuntu.com.
  Sem ela o chaveiro assinado não pode ser instalado, então o CachyOS Proton
  foi pulado. Adicione depois com:  synpkg cachyos enable-repo"
  ["  Master key pinned as expected — trusting it..."]="  Chave mestra conforme o esperado — confiando nela..."
  ["[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."]="[cachyos] foi adicionado mas não lista nenhum pacote — está sendo
  removido de novo para não travar uma atualização futura."
  ["The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."]="O chaveiro do CachyOS não traz a chave mestra esperada.
  Recusando confiar nele — o repositório NÃO foi adicionado."
  ["  Installing proton-cachyos-slr (~340 MB download)..."]="  Instalando proton-cachyos-slr (~340 MB de download)..."
  ["CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."]="CachyOS Proton instalado — escolha por jogo no Steam em Propriedades →
  Compatibilidade, listado como 'proton-cachyos-… (steam linux runtime)'.
  O Steam só procura por ele ao iniciar, então reinicie se já estiver aberto."
  ["proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"]="proton-cachyos-slr falhou ao instalar. O Steam e o Proton da Valve
  não são afetados. Instale depois com:
  sudo pacman -S proton-cachyos-slr"
  ["The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"]="O repositório [cachyos] não pôde ser ativado, então o CachyOS Proton
  foi pulado. O Steam continua funcionando com o Proton da Valve. Para adicionar depois:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
  ["Enabling BlackArch"]="Ativando o BlackArch"
  ["  Fetching the BlackArch bootstrap..."]="  Buscando o bootstrap do BlackArch..."
  ["  Master key pinned as expected — running bootstrap..."]="  Chave mestra conforme o esperado — executando o bootstrap..."
  ["blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."]="blackarch-keyring não foi instalado — as trocas de chave
  não vão chegar a esta máquina. Corrija com 'sudo pacman -S blackarch-keyring'."
  ["The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."]="O strap.sh baixado não fixa a chave mestra esperada do BlackArch.
  Recusando executá-lo — o repositório NÃO foi adicionado."
  ["BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."]="O BlackArch não foi ativado. O sistema está completo no resto;
  adicione depois com 'sudo syn arsenal --enable-repo'."
  ["Installing software"]="Instalando programas"
  ["That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."]="Essa transação falhou — cada pacote é tentado de novo sozinho, para que
  os que estão bem cheguem mesmo assim e o que não está seja nomeado."
  ["Software installed"]="Programas instalados"
  ["Installing Flatpak apps"]="Instalando aplicativos Flatpak"
  ["flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."]="o flatpak não pôde ser instalado — os aplicativos Flatpak foram pulados.
  Nada mais é afetado."
  ["Could not add the flathub remote"]="Não foi possível adicionar o remoto flathub"
  ["Flatpak apps installed"]="Aplicativos Flatpak instalados"
  ["Configuring System"]="Configurando o sistema"
  ["  fstab generated"]="  fstab gerada"
  ["Swap recorded in fstab"]="Swap registrado na fstab"
  ["zram configured (compressed swap, half of RAM up to 8 GiB)"]="zram configurado (swap comprimido, metade da RAM até 8 GiB)"
  ["zram-generator is not installed in the target — no compressed swap"]="zram-generator não está instalado no destino — sem swap comprimido"
  ["  Hostname: synapse"]="  Nome da máquina: synapse"
  ["Step 7 — Language & Region"]="Passo 7 — Idioma e região"
  ["   0) Other — enter a locale by hand"]="   0) Outro — digitar uma locale à mão"
  ["Locale (e.g. sv_SE.UTF-8):"]="Locale (ex.: sv_SE.UTF-8):"
  ["Console keymap (e.g. sv-latin1):"]="Mapa de teclado do console (ex.: sv-latin1):"
  ["Step 8 — Timezone"]="Passo 8 — Fuso horário"
  ["   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"]="   0) Outro — digitar qualquer nome do tzdata (ex.: Europe/Lisbon)"
  ["tzdata name (Region/City):"]="Nome do tzdata (Região/Cidade):"
  ["  Did you mean:"]="  Você quis dizer:"
  ["  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"]="  Escolha um número da lista, ou veja: ls /mnt/usr/share/zoneinfo"
  ["  os-release: copied from live system"]="  os-release: copiado do sistema ao vivo"
  ["  issue: copied from live system"]="  issue: copiado do sistema ao vivo"
  ["Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"]="O sistema de arquivos de destino não é mais gravável (erros de disco? veja 'dmesg') — abortando"
  ["sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"]="O conjunto de regras do sudoers ficou inválido depois de escrever os drop-ins — não vamos entregar um sistema que não consegue usar sudo"
  ["Could not relax pam_faillock in /etc/pam.d/system-auth (a tty-less sudo could still lock the account until reboot)."]="Não foi possível afrouxar o pam_faillock em /etc/pam.d/system-auth (um sudo sem tty ainda poderia travar a conta até reiniciar)."
  ["could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"]="não foi possível criar antecipadamente /var/lib/synapse-src — o atualizador vai pedir uma senha na primeira execução"
  ["  Desktop: KDE Plasma (SDDM login screen)"]="  Área de trabalho: KDE Plasma (tela de login SDDM)"
  ["  GDM: SynapseOS logo on the login screen"]="  GDM: logotipo do SynapseOS na tela de login"
  ["  Desktop: GNOME (GDM login screen)"]="  Área de trabalho: GNOME (tela de login GDM)"
  ["  Desktop: TTY only"]="  Área de trabalho: somente TTY"
  ["  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"]="  Área de trabalho: SynapseUI (greeter do synui — o login espelha a tela de bloqueio)"
  ["  motd: written for this installation"]="  motd: escrito para esta instalação"
  ["  note: syn-rgb.path is not installed; RGB lights stay off"]="  nota: syn-rgb.path não está instalado; as luzes RGB ficam apagadas"
  ["AI model"]="Modelo de IA"
  ["  AI model skipped — install one later with: syn model download"]="  Modelo de IA pulado — instale um depois com: syn model download"
  ["AI model installed"]="Modelo de IA instalado"
  ["  the install, and everything else on the disk is already done."]="  da instalação, e todo o resto no disco já está pronto."
  ["syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."]="syn-model não está no destino, então nenhum modelo foi baixado.
  Ele faz parte do conjunto básico; se foi desmarcado, a IA fica inerte."
  ["Configuring Nix"]="Configurando o Nix"
  ["Nix configured — /etc/synapseos/nix"]="Nix configurado — /etc/synapseos/nix"
  ["  That is the download — a few hundred MB before any packages
  you add to home.nix. 'syn nix edit' opens it."]="  Esse é o download — algumas centenas de MB antes de qualquer pacote
  que você adicionar ao home.nix. 'syn nix edit' abre o arquivo."
  ["nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."]="o nix está instalado, mas o pacote 'syn' não está no destino, então
  o configurador não foi preparado. O Nix em si funciona;
  a camada /etc/synapseos/nix precisa do 'syn'."
  ["nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."]="o nix falhou ao instalar — a camada declarativa não está disponível.
  Instale depois com 'sudo pacman -S nix && sudo syn nix init'."
  ["  Generating initramfs..."]="  Gerando o initramfs..."
  ["mkinitcpio failed — the installed system would not boot"]="mkinitcpio falhou — o sistema instalado não iniciaria"
  ["initramfs missing after mkinitcpio — the installed system would not boot"]="initramfs faltando depois do mkinitcpio — o sistema instalado não iniciaria"
  ["System configured"]="Sistema configurado"
  ["Installing Bootloader"]="Instalando o gerenciador de inicialização"
  ["grub-install (UEFI) failed"]="grub-install (UEFI) falhou"
  ["grub-install (BIOS) failed"]="grub-install (BIOS) falhou"
  ["  Generating GRUB config..."]="  Gerando a configuração do GRUB..."
  ["grub-mkconfig failed"]="grub-mkconfig falhou"
  ["grub.cfg missing after install"]="grub.cfg faltando depois da instalação"
  ["grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"]="grub.cfg contém uma senha do GRUB — fica só para o root, então o aplicativo de configurações não consegue informar sobre as entradas de inicialização"
  ["  Installing systemd-boot..."]="  Instalando o systemd-boot..."
  ["bootctl install failed"]="bootctl install falhou"
  ["  Registering systemd-boot with the firmware..."]="  Registrando o systemd-boot no firmware..."
  ["efibootmgr entry not created — the removable-media path still applies"]="entrada do efibootmgr não criada — o caminho de mídia removível continua valendo"
  ["could not read the root filesystem UUID"]="não foi possível ler o UUID do sistema de arquivos raiz"
  ["vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"]="vmlinuz-linux não está na ESP — o systemd-boot não acharia nada para iniciar"
  ["initramfs is not on the ESP — systemd-boot would find nothing to boot"]="o initramfs não está na ESP — o systemd-boot não acharia nada para iniciar"
  ["systemd-boot did not install its EFI binary"]="o systemd-boot não instalou seu binário EFI"
  ["  Installing limine..."]="  Instalando o limine..."
  ["could not copy limine's EFI binary to the ESP"]="não foi possível copiar o binário EFI do limine para a ESP"
  ["limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"]="limine-mkinitcpio-hook não instalado — um kernel instalado depois NÃO terá entrada de inicialização"
  ["vmlinuz-linux is not on the ESP — limine would find nothing to boot"]="vmlinuz-linux não está na ESP — o limine não acharia nada para iniciar"
  ["limine's EFI binary is not on the ESP"]="o binário EFI do limine não está na ESP"
  ["limine.conf has no kernel entry"]="limine.conf não tem nenhuma entrada de kernel"
  ["  Verifying the encrypted boot path..."]="  Verificando o caminho de inicialização criptografado..."
  ["/boot is not a separate mount — an encrypted root needs a plain /boot"]="/boot não é um ponto de montagem separado — uma raiz criptografada precisa de um /boot em claro"
  ["/boot is missing from fstab — it would not be mounted after boot"]="/boot está faltando na fstab — ele não seria montado depois da inicialização"
  ["Encrypted boot path verified"]="Caminho de inicialização criptografado verificado"
  ["Configuring snapshots"]="Configurando os instantâneos"
  ["snapper's config template is missing — snapshots cannot be configured"]="o modelo de configuração do snapper está faltando — os instantâneos não podem ser configurados"
  ["could not write /etc/snapper/configs/root"]="não foi possível escrever /etc/snapper/configs/root"
  ["snapper does not see the 'root' config — snapshots would never be taken"]="o snapper não vê a configuração 'root' — nenhum instantâneo seria tirado"
  ["snapper's root config was not tuned — timeline snapshots would fill the disk"]="a configuração root do snapper não foi ajustada — instantâneos periódicos encheriam o disco"
  ["could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"]="não foi possível ativar o grub-btrfsd — os instantâneos não aparecerão sozinhos no menu de inicialização"
  ["Snapshots enabled (snapper + snap-pac, bootable from GRUB)"]="Instantâneos ativados (snapper + snap-pac, inicializáveis pelo GRUB)"
  ["could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"]="não foi possível ativar o limine-snapper-sync — os instantâneos não chegarão sozinhos ao menu de inicialização"
  ["could not take the post-install snapshot"]="não foi possível tirar o instantâneo pós-instalação"
  ["could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"]="não foi possível ativar a sincronização de instantâneos da primeira inicialização — o menu se preenche depois da primeira atualização"
  ["Snapshots enabled (snapper + snap-pac, bootable from limine)"]="Instantâneos ativados (snapper + snap-pac, inicializáveis pelo limine)"
  ["Bootloader installed"]="Gerenciador de inicialização instalado"
  ["  The root account is locked (no root login / su).
  Note: 3 wrong password attempts lock the account for 10 minutes."]="  A conta root está travada (sem login root / su).
  Nota: 3 senhas erradas travam a conta por 10 minutos."
  ["You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."]="A frase secreta da criptografia será pedida a cada inicialização,
  ANTES da tela de login. Não há como recuperá-la."
  ["    syn-crypt status                    is this disk encrypted, and how
    sudo syn-crypt change-key           replace the passphrase
    sudo syn-crypt add-key              add a second one
    sudo syn-crypt backup-header FILE   save the LUKS header"]="    syn-crypt status                    este disco está criptografado, e como
    sudo syn-crypt change-key           trocar a frase secreta
    sudo syn-crypt add-key              adicionar uma segunda
    sudo syn-crypt backup-header ARQUIVO  salvar o cabeçalho LUKS"
  ["  means the data is unrecoverable even with the right passphrase."]="  significa que os dados são irrecuperáveis mesmo com a frase secreta certa."
  ["Remove installation media and press ENTER to reboot..."]="Remova a mídia de instalação e pressione ENTER para reiniciar..."
  ["Install SynapseOS     — right here, in this terminal"]="Instalar o SynapseOS     — aqui mesmo, neste terminal"
  ["Install graphically   — starts the desktop first"]="Instalar em modo gráfico — inicia a área de trabalho antes"
  ["Try the live desktop  — look around; install later"]="Testar a área de trabalho — dar uma olhada; instalar depois"
  ["Target:"]="Destino:"
  ["ALONGSIDE"]="AO LADO"
  ["ERASE"]="APAGAR"
  ["ADVANCED"]="AVANÇADO"
  ["Encrypt this installation?"]="Criptografar esta instalação?"
  ["There is no recovery."]="Não há recuperação."
  ["Root filesystem"]="Sistema de arquivos raiz"
  ["ext4   — the default. Boring, proven, repairable by anything."]="ext4   — o padrão. Chato, comprovado, reparável por qualquer coisa."
  ["btrfs  — snapshots + zstd compression. Roll back a bad update
                    from the boot menu. Uses more RAM and more CPU."]="btrfs  — instantâneos + compressão zstd. Desfaça uma atualização ruim
                    pelo menu de inicialização. Usa mais RAM e mais CPU."
  ["xfs    — fast on large files. No snapshots, and it cannot be
                    SHRUNK once created."]="xfs    — rápido com arquivos grandes. Sem instantâneos, e não pode ser
                    REDUZIDO depois de criado."
  ["f2fs   — built for flash. Good on SD cards and cheap SSDs;
                    unusual enough that fewer rescue tools know it."]="f2fs   — feito para flash. Bom em cartões SD e SSDs baratos;
                    incomum o bastante para que poucas ferramentas de resgate o conheçam."
  ["Bootloader"]="Gerenciador de inicialização"
  ["GRUB          — the default. Detects other operating systems,
                          and the only one here that can boot a btrfs
                          snapshot."]="GRUB          — o padrão. Detecta outros sistemas operacionais,
                          e é o único aqui capaz de iniciar um
                          instantâneo btrfs."
  ["systemd-boot  — minimal. No OS detection, no snapshot menu."]="systemd-boot  — mínimo. Sem detecção de SO, sem menu de instantâneos."
  ["limine        — modern and fast, and it CAN boot snapshots."]="limine        — moderno e rápido, e ELE CONSEGUE iniciar instantâneos."
  ["Automatic snapshots?"]="Instantâneos automáticos?"
  ["Review the plan — nothing has been written yet:"]="Confira o plano — nada foi escrito ainda:"
  ["nothing else is touched"]="nada mais é tocado"
  ["not"]="não será"
  ["Partition"]="Particione"
  ["now."]="agora."
  ["Partitions now on"]="Partições agora em"
  ["These partitions will be FORMATTED"]="Estas partições serão FORMATADAS"
  ["Full      — Standard + Steam + Nix + more software"]="Completa  — Padrão + Steam + Nix + mais programas"
  ["Standard  — the SynapseOS suite, Firefox, AI model,"]="Padrão    — a suíte SynapseOS, Firefox, modelo de IA,"
  ["Minimal   — core daemons only: none of the above"]="Mínima    — só os daemons básicos: nada do acima"
  ["Custom    — tick every package yourself, ours and"]="Custom    — marcar cada pacote você mesmo, os nossos e"
  ["Which AI model should this machine run?"]="Qual modelo de IA esta máquina deve rodar?"
  ["Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"]="Mistral 7B Instruct   ~4,1 GB   recomendado — é para ele que o SynapseOS foi ajustado"
  ["Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"]="Phi-3 Mini 4K         ~2,2 GB   metade do tamanho, e visivelmente mais fraco"
  ["Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"]="Qwen2 0.5B            ~0,4 GB   cabe em qualquer lugar, e responde de acordo"
  ["None                            skip it — nothing else changes"]="Nenhum                          pular — nada mais muda"
  ["Installing:"]="Instalando:"
  ["SynapseUI  — AI-native Wayland compositor  (default)"]="SynapseUI  — compositor Wayland nativo de IA  (padrão)"
  ["SynapseUI  — NOT AVAILABLE: synui was not selected"]="SynapseUI  — INDISPONÍVEL: o synui não foi selecionado"
  ["KDE Plasma — Full-featured Wayland desktop"]="KDE Plasma — área de trabalho Wayland completa"
  ["GNOME      — Clean, modern Wayland desktop"]="GNOME      — área de trabalho Wayland limpa e moderna"
  ["TTY only   — No GUI (headless/server)"]="Só TTY     — sem interface gráfica (headless/servidor)"
  ["Disk:"]="Disco:"
  ["Boot:"]="Inicialização:"
  ["Encrypted:"]="Criptografado:"
  ["Desktop:"]="Área de trabalho:"
  ["User:"]="Usuário:"
  ["Hostname:"]="Nome da máquina:"
  ["Back up the header to another machine."]="Faça backup do cabeçalho em outra máquina."
  ["%s is mounted — unmount it first\\n"]="%s está montado — desmonte primeiro
"
  ["%s is %s MiB — %s needs at least %s MiB\\n"]="%s tem %s MiB — %s precisa de pelo menos %s MiB
"
  ["  Generating %s (a few seconds)...\\n"]="  Gerando %s (alguns segundos)...
"
  ["Language: %s  (%s, keyboard %s)\\n"]="Idioma: %s  (%s, teclado %s)
"
  ["  This disk already holds %s partition(s), an EFI System\\n  Partition (%s), and %s GiB of free space.\\n"]="  Este disco já contém %s partição(ões), uma partição de
  sistema EFI (%s), e %s GiB de espaço livre.
"
  ["    1) Install %s — use the free space, keep everything else\\n"]="    1) Instalar %s — usar o espaço livre, manter todo o resto
"
  ["    2) %s the whole disk — delete every partition and all data\\n"]="    2) %s o disco inteiro — apagar toda partição e todos os dados
"
  ["    3) %s — partition this disk yourself, then pick the partitions\\n"]="    3) %s — particionar este disco você mesmo, depois escolher as partições
"
  ["    1) %s the whole disk — delete every partition and all data  (default)\\n"]="    1) %s o disco inteiro — apagar toda partição e todos os dados  (padrão)
"
  ["    2) %s — partition this disk yourself, then pick the partitions\\n"]="    2) %s — particionar este disco você mesmo, depois escolher as partições
"
  ["  %s If you forget the passphrase the data is\\n  gone — no password reset, no support call, nothing.\\n"]="  %s Se você esquecer a frase secreta os dados estão
  perdidos — sem redefinição, sem ligar para o suporte, nada.
"
  ["  snapper takes a snapshot before and after every pacman\\n  transaction, and %s grows a menu to boot any of them. A\\n  bad upgrade becomes a reboot instead of a rescue USB.\\n"]="  O snapper tira um instantâneo antes e depois de cada transação do
  pacman, e o %s ganha um menu para iniciar qualquer um deles. Uma
  atualização ruim vira um reinício em vez de um USB de resgate.
"
  ["    Disk          : %s\\n"]="    Disco         : %s
"
  ["    Firmware      : %s\\n"]="    Firmware      : %s
"
  ["    Filesystem    : %s\\n"]="    Sist. arquivos: %s
"
  ["    Bootloader    : %s\\n"]="    Inicialização : %s
"
  ["    Separate /boot: %s\\n"]="    /boot separado: %s
"
  ["    Encryption    : %s\\n"]="    Criptografia  : %s
"
  ["    Snapshots     : %s\\n"]="    Instantâneos  : %s
"
  ["  Encrypting %s (LUKS2)...\\n"]="  Criptografando %s (LUKS2)...
"
  ["  Formatting root partition (%s)...\\n"]="  Formatando a partição raiz (%s)...
"
  ["    • KEEP   all %s existing partition(s), including Windows\\n"]="    • MANTER   todas as %s partição(ões) existentes, Windows incluído
"
  ["    • REUSE  %s as the EFI partition (mounted, %s formatted)\\n"]="    • REUSAR   %s como partição EFI (montada, %s formatada)
"
  ["    • CREATE a new ext4 root of ~%s GiB in the free space\\n"]="    • CRIAR    uma nova raiz ext4 de ~%s GiB no espaço livre
"
  ["  Creating root partition in free space (%sMiB–%sMiB)...\\n"]="  Criando a partição raiz no espaço livre (%s MiB–%s MiB)...
"
  ["  Formatting new root (%s, ext4)...\\n"]="  Formatando a nova raiz (%s, ext4)...
"
  ["  %s %s %s The installer will re-read the table when you exit.\\n"]="  %s %s %s O instalador vai reler a tabela quando você sair.
"
  ["    • a root partition, at least %s GiB\\n"]="    • uma partição raiz, de pelo menos %s GiB
"
  ["    • a separate /boot of ~1 GiB — %s with this layout cannot read the root\\n"]="    • um /boot separado de ~1 GiB — o %s com este arranjo não consegue ler a raiz
"
  ["  Starting %s on %s — write your changes before quitting.\\n"]="  Iniciando %s em %s — grave suas mudanças antes de sair.
"
  ["    %s is already swap — another system may resume from it.\\n"]="    %s já é swap — outro sistema pode retomar a partir dele.
"
  ["  Everything else on %s is left untouched.\\n"]="  Todo o resto em %s fica intacto.
"
  ["  Making swap on %s...\\n"]="  Criando swap em %s...
"
  ["  Formatting EFI partition (%s MiB)...\\n"]="  Formatando a partição EFI (%s MiB)...
"
  ["  NVIDIA GPU detected — installing %s (builds the module, takes a while)...\\n"]="  GPU NVIDIA detectada — instalando %s (compila o módulo, demora um pouco)...
"
  ["  Installing video stack: %s %s...\\n"]="  Instalando a pilha de vídeo: %s %s...
"
  ["  [cachyos] enabled (%s packages available)\\n"]="  [cachyos] ativado (%s pacotes disponíveis)
"
  ["  Language: %s  (chosen at boot)\\n"]="  Idioma: %s  (escolhido na inicialização)
"
  ["  Locale:   %s   Keyboard: %s (console) / %s (desktop)\\n"]="  Locale:   %s   Teclado: %s (console) / %s (área de trabalho)
"
  ["  Installing fonts (%s)...\\n"]="  Instalando fontes (%s)...
"
  ["  Downloading the AI model (%s) — this is the long part of\\n"]="  Baixando o modelo de IA (%s) — esta é a parte longa da
"
  ["  Nothing is built yet. As %s, after the first boot:\\n"]="  Nada foi construído ainda. Como %s, depois da primeira inicialização:
"
  ["  Adding the %s hook to mkinitcpio...\\n"]="  Adicionando o hook %s ao mkinitcpio...
"
  ["  Installing GRUB (%s)...\\n"]="  Instalando o GRUB (%s)...
"
  ["yes — LUKS2 on %s"]="sim — LUKS2 em %s"
  ["  Admin: use %s with your user password.\\n"]="  Administração: use %s com a senha do seu usuário.
"
  ["  Manage it later with %s:\\n"]="  Gerencie depois com %s:
"
  ["  %s A damaged LUKS header\\n"]="  %s Um cabeçalho LUKS danificado
"
  ["%s is on the live/boot device — that is the installer's own media\\n"]="%s está no dispositivo ao vivo/de inicialização — essa é a própria mídia do instalador
"
  ["    %s is already FAT — it may hold another OS's bootloader.\\n"]="    %s já é FAT — pode conter o gerenciador de inicialização de outro sistema.
"
  ["  Creating user '%s'...\\n"]="  Criando o usuário '%s'...
"
  ["  User '%s' created (uid=%s)\\n"]="  Usuário '%s' criado (uid=%s)
"
  ["  Log in as '%s' after reboot.\\n"]="  Entre como '%s' depois de reiniciar.
"
  ["  Type '%s' to get started.\\n"]="  Digite '%s' para começar.
"
  ["Install SynapseOS"]="Instalar o SynapseOS"
  ["SynapseOS packages"]="Pacotes do SynapseOS"
  ["Everything the system is made of. What you cannot drop is what something else you kept depends on — those are turned back on and named before anything is installed."]="Tudo aquilo de que o sistema é feito. O que não pode ser desmarcado é aquilo de que depende outra coisa que manteve — esses são reativados e nomeados antes de se instalar seja o que for."
  ["SYNAPSE UI — the Wayland desktop"]="SYNAPSE UI — o ambiente Wayland"
  ["synapd — the local AI daemon"]="synapd — o serviço de IA local"
  ["synsh — the AI-native shell"]="synsh — a shell nativa de IA"
  ["synguard + kernel module"]="synguard + módulo do kernel"
  ["synnet — network policy"]="synnet — regras de rede"
  ["Software — the package manager"]="Software — o gestor de pacotes"
  ["Files — the file manager"]="Ficheiros — o gestor de ficheiros"
  ["Terminal (synui depends on it)"]="Terminal (o synui precisa dele)"
  ["Settings"]="Definições"
  ["Disks"]="Discos"
  ["Editor"]="Editor"
  ["Calendar"]="Calendário"
  ["File Vault — a locked folder"]="Cofre — uma pasta trancada"
  ["Disk Cleanup — caches, and secure delete"]="Limpeza de disco — caches e apagamento seguro"
  ["syn-update — how fixes arrive"]="syn-update — como chegam as correções"
  ["syn — the top-level CLI"]="syn — a linha de comandos principal"
  ["syn-model — fetch AI models"]="syn-model — obter modelos de IA"
  ["syn-confine — the sandbox"]="syn-confine — a sandbox"
  ["fetch — the About OS readout"]="fetch — o resumo do sistema"
  ["Arcade — overlay, pads, big screen"]="Arcade — overlay, comandos, ecrã grande"
  ["cliamp — the music player"]="cliamp — o leitor de música"
  ["Player — playlists, shuffle and history, on mpv"]="Player — listas, aleatório e histórico, sobre o mpv"
  ["Studio — photo darkroom and video"]="Studio — câmara escura e vídeo"
  ["GeForce NOW — cloud gaming in a browser"]="GeForce NOW — jogos na nuvem num navegador"
  ["Arsenal — BlackArch browser"]="Arsenal — navegador do BlackArch"
  ["Chibi — voice companion"]="Chibi — companheiro de voz"
  ["Vibe — AI coding assistant"]="Vibe — assistente de programação com IA"
  ["Animated wallpapers (~317 MB)"]="Fundos animados (~317 MB)"
  ["Nexus Chat (pulls in Firefox)"]="Nexus Chat (traz o Firefox)"
  ["TEPRIS (pulls in Firefox)"]="TEPRIS (traz o Firefox)"
  ["Web and communication"]="Web e comunicação"
  ["None of this is ours; every name is in the Arch repositories. Firefox is on by default because an installed SynapseOS used to arrive with no browser at all."]="Nada disto é nosso; todos os nomes estão nos repositórios do Arch. O Firefox vem ligado por omissão porque um SynapseOS instalado costumava chegar sem navegador nenhum."
  ["Thunderbird — mail"]="Thunderbird — correio"
  ["KeePassXC — passwords"]="KeePassXC — palavras-passe"
  ["Syncthing — file sync"]="Syncthing — sincronização de ficheiros"
  ["LocalSend — send to phone (Flatpak)"]="LocalSend — enviar para o telemóvel (Flatpak)"
  ["Audio and video"]="Áudio e vídeo"
  ["Office and graphics"]="Escritório e gráficos"
  ["Development and admin"]="Desenvolvimento e administração"
  ["VS Code (OSS build)"]="VS Code (compilação OSS)"
  ["7zip + unrar"]="7zip + unrar"
  ["Games, launchers and helpers"]="Jogos, lançadores e utilitários"
  ["Steam is in the options below rather than here: it is the only one that turns on a second architecture and a third repository."]="O Steam está nas opções abaixo em vez de aqui: é o único que liga uma segunda arquitetura e um terceiro repositório."
  ["Prism — Minecraft"]="Prism — Minecraft"
  ["Dolphin — GameCube/Wii"]="Dolphin — GameCube/Wii"
  ["PPSSPP — PSP"]="PPSSPP — PSP"
  ["Space Cadet Pinball (Flatpak)"]="Space Cadet Pinball (Flatpak)"
  ["GOverlay — MangoHud"]="GOverlay — MangoHud"
  ["AntiMicroX — pad remap"]="AntiMicroX — remapear comandos"
  ["Welcome"]="Boas-vindas"
  ["Disk"]="Disco"
  ["Software"]="Software"
  ["Account"]="Conta"
  ["Region"]="Região"
  ["Summary"]="Resumo"
  ["Install"]="Instalação"
  ["the installer's own media"]="o próprio suporte do instalador"
  ["%1 GiB — SynapseOS needs at least %2 GiB"]="%1 GiB — o SynapseOS precisa de pelo menos %2 GiB"
  ["No connection. SynapseOS downloads the base system while it installs, so this needs a working network before it can start."]="Sem ligação. O SynapseOS descarrega o sistema base enquanto instala, por isso precisa de uma rede a funcionar antes de poder começar."
  ["Choose a disk to install to."]="Escolha um disco para instalar."
  ["The encryption passphrase needs at least 8 characters."]="A palavra-passe de encriptação precisa de pelo menos 8 caracteres."
  ["With neither the package manager nor the desktop, this install has no way to add either one back. Keep at least one."]="Sem o gestor de pacotes nem o ambiente de trabalho, esta instalação não tem maneira de voltar a acrescentar nenhum dos dois. Mantenha pelo menos um."
  ["A username is lower-case letters, digits, - and _, and cannot start with a digit."]="Um nome de utilizador tem letras minúsculas, dígitos, - e _, e não pode começar por um dígito."
  ["Set a password for the account."]="Defina uma palavra-passe para a conta."
  ["The two passwords do not match."]="As duas palavras-passe não coincidem."
  ["A locale is needed, e.g. en_US.UTF-8."]="É precisa uma locale, p. ex. pt_PT.UTF-8."
  ["A timezone is needed, e.g. Europe/Lisbon."]="É preciso um fuso horário, p. ex. Europe/Lisbon."
  ["printing"]="impressão"
  ["%1 repo"]="repo %1"
  ["Mode"]="Modo"
  ["Filesystem"]="Sistema de ficheiros"
  ["%1 on LUKS2"]="%1 sobre LUKS2"
  ["%1 + snapshots"]="%1 + instantâneos"
  ["none"]="nenhum"
  ["Desktop"]="Ambiente de trabalho"
  ["Locale"]="Locale"
  ["%1   keys %2 / %3"]="%1   teclas %2 / %3"
  ["Timezone"]="Fuso horário"
  ["%1 package(s) — WITHOUT %2"]="%1 pacote(s) — SEM %2"
  ["%1 package(s)"]="%1 pacote(s)"
  ["Options"]="Opções"
  ["Could not write the install profile."]="Não foi possível escrever o perfil de instalação."
  ["Installation complete."]="Instalação concluída."
  ["Installation failed — see the log."]="A instalação falhou — veja o registo."
  ["No network connection"]="Sem ligação à rede"
  ["The base system is downloaded while it installs, so this cannot start offline. Plug in a cable or join a network, then press Re-check — the answers on these pages are kept."]="O sistema base é descarregado durante a instalação, por isso não pode começar offline. Ligue um cabo ou junte-se a uma rede e prima Verificar de novo — as respostas destas páginas são guardadas."
  ["Checking…"]="A verificar…"
  ["Re-check"]="Verificar de novo"
  ["Wi-Fi settings"]="Definições de Wi-Fi"
  ["This asks for a disk, an account and a few preferences, then hands the answers to the same installer the text version runs. Nothing is written to any disk until the last page, and that page says exactly what it is about to do."]="Isto pergunta por um disco, uma conta e algumas preferências, e depois entrega as respostas ao mesmo instalador que a versão de texto executa. Nada é escrito em disco algum até à última página, e essa página diz exatamente o que vai fazer."
  ["A disk is partitioned and formatted"]="Um disco é particionado e formatado"
  ["The base system and the SynapseOS packages are installed"]="O sistema base e os pacotes do SynapseOS são instalados"
  ["An account and a desktop are set up"]="Uma conta e um ambiente de trabalho são configurados"
  ["A bootloader is written"]="Um gestor de arranque é escrito"
  ["Partitioning an existing layout by hand is the text installer's ADVANCED mode — quit this and run \`syn-install\` in a terminal for that."]="Particionar à mão um esquema existente é o modo ADVANCED do instalador de texto — feche isto e execute \`syn-install\` num terminal para isso."
  ["Where should SynapseOS go?"]="Para onde vai o SynapseOS?"
  ["The installer's own media is listed and cannot be chosen."]="O próprio suporte do instalador é listado e não pode ser escolhido."
  ["No disks found."]="Nenhum disco encontrado."
  ["Erase the disk"]="Apagar o disco"
  ["every partition and all data"]="todas as partições e todos os dados"
  ["Install alongside"]="Instalar ao lado"
  ["use free space, UEFI only"]="usar espaço livre, só UEFI"
  ["Snapshots"]="Instantâneos"
  ["btrfs + limine only"]="só btrfs + limine"
  ["Encrypt the disk"]="Encriptar o disco"
  ["Passphrase"]="Palavra-passe"
  ["8 characters or more"]="8 caracteres ou mais"
  ["What should be installed?"]="O que deve ser instalado?"
  ["The SynapseOS core — the compositor, the daemons and the applications it is built on — is installed by every choice here."]="O núcleo do SynapseOS — o compositor, os serviços e as aplicações sobre as quais assenta — é instalado por qualquer escolha aqui."
  ["Full"]="Completa"
  ["Standard + Steam + Nix + more software"]="Padrão + Steam + Nix + mais software"
  ["Standard"]="Padrão"
  ["the SynapseOS suite, Firefox, AI model, Bluetooth, printing, Wine, phone"]="a suite SynapseOS, Firefox, modelo de IA, Bluetooth, impressão, Wine, telemóvel"
  ["Minimal"]="Mínima"
  ["core daemons only — no apps, no software, no model"]="só os serviços do núcleo — sem apps, sem software, sem modelo"
  ["Custom"]="Personalizada"
  ["tick every package yourself, ours and the ordinary software"]="marcar cada pacote você mesmo, os nossos e o software comum"
  ["(required)"]="(obrigatório)"
  ["Not packages: a repository, an architecture or a service. Each is a decision with a consequence that does not fit on a checkbox above."]="Não são pacotes: um repositório, uma arquitetura ou um serviço. Cada um é uma decisão com uma consequência que não cabe numa caixa acima."
  ["Printing (CUPS)"]="Impressão (CUPS)"
  ["Wine — run Windows .exe/.msi"]="Wine — executar .exe/.msi do Windows"
  ["KDE Connect — pair a phone"]="KDE Connect — emparelhar um telemóvel"
  ["Steam + game stack + Proton (~3.1 GB)"]="Steam + pilha de jogos + Proton (~3,1 GB)"
  ["BlackArch repo — ~5000 tools, none installed"]="Repo BlackArch — ~5000 ferramentas, nenhuma instalada"
  ["Nix + Home Manager"]="Nix + Home Manager"
  ["syn-update is off: this machine will have no way to receive another SynapseOS package. Fixing that later means installing it by hand from the ISO, or reinstalling."]="O syn-update está desligado: esta máquina não terá maneira de receber outro pacote do SynapseOS. Corrigir isso mais tarde significa instalá-lo à mão a partir da ISO, ou reinstalar."
  ["synui is off: this will not be a SynapseOS desktop. The Desktop page offers KDE, GNOME or no GUI."]="O synui está desligado: isto não será um ambiente SynapseOS. A página Ambiente de trabalho oferece KDE, GNOME ou nenhuma interface."
  ["AI model — downloaded during the install"]="Modelo de IA — descarregado durante a instalação"
  ["~4.1 GB — recommended"]="~4,1 GB — recomendado"
  ["~2.2 GB — weaker"]="~2,2 GB — mais fraco"
  ["~0.4 GB — much weaker"]="~0,4 GB — muito mais fraco"
  ["None"]="Nenhum"
  ["AI stays inert"]="a IA fica inerte"
  ["NVIDIA GPU inference"]="Inferência em GPU NVIDIA"
  ["the CUDA runtime, ~4.7 GiB"]="o runtime CUDA, ~4,7 GiB"
  ["Who is this machine for?"]="Para quem é esta máquina?"
  ["Username"]="Nome de utilizador"
  ["lower-case, no spaces"]="minúsculas, sem espaços"
  ["Full name (optional)"]="Nome completo (opcional)"
  ["Password"]="Palavra-passe"
  ["Password again"]="Palavra-passe outra vez"
  ["They do not match"]="Não coincidem"
  ["the native compositor"]="o compositor nativo"
  ["synui is not selected"]="o synui não está selecionado"
  ["headless"]="sem interface"
  ["Language, keyboard and time"]="Idioma, teclado e hora"
  ["Pick a language and the other three follow it. The console keymap and the desktop layout are separate on purpose — Swedish is 'sv-latin1' to the console and 'se' to the desktop — so they can be changed on their own afterwards."]="Escolha um idioma e os outros três seguem-no. O mapa de teclas da consola e o esquema do ambiente de trabalho são separados de propósito — o sueco é 'sv-latin1' para a consola e 'se' para o ambiente — para poderem ser alterados isoladamente depois."
  ["Language"]="Idioma"
  ["sets the keyboard and the fonts too"]="define também o teclado e as fontes"
  ["typed by hand — fonts cover as much as possible"]="escrito à mão — as fontes cobrem o máximo possível"
  ["Sets the locale, both keyboard names and the font pack. Any locale glibc has can be typed instead."]="Define a locale, ambos os nomes de teclado e o pacote de fontes. Em vez disso pode escrever-se qualquer locale que a glibc conheça."
  ["The common zones first, then every name tzdata ships."]="Primeiro as zonas comuns, depois todos os nomes que o tzdata traz."
  ["Console keymap"]="Mapa de teclas da consola"
  ["loadkeys — the text console and the greeter"]="loadkeys — a consola de texto e o ecrã de entrada"
  ["Every keymap this image can load. This one names a file loadkeys has to find, which is why it is not the same list as the desktop layout."]="Todos os mapas de teclas que esta imagem consegue carregar. Este nomeia um ficheiro que o loadkeys tem de encontrar, e por isso não é a mesma lista do esquema do ambiente de trabalho."
  ["Desktop layout"]="Esquema do ambiente de trabalho"
  ["XKB — the compositor"]="XKB — o compositor"
  ["Desktop keyboard layout"]="Esquema de teclado do ambiente de trabalho"
  ["The layouts xkbcommon can compile. 'uk' is a console keymap and not a layout here — the layout is 'gb'."]="Os esquemas que o xkbcommon consegue compilar. 'uk' é um mapa de teclas de consola e aqui não é um esquema — o esquema é 'gb'."
  ["Read this back"]="Reler tudo"
  ["Nothing has been written yet. The next button is the one that starts."]="Ainda não foi escrito nada. O próximo botão é o que começa."
  ["EVERY PARTITION ON %1 WILL BE DELETED"]="TODAS AS PARTIÇÕES EM %1 SERÃO APAGADAS"
  ["SynapseOS will be installed into the free space on %1"]="O SynapseOS será instalado no espaço livre em %1"
  ["SynapseOS is installed"]="O SynapseOS está instalado"
  ["The install stopped"]="A instalação parou"
  ["Installing SynapseOS"]="A instalar o SynapseOS"
  ["Reboot and remove the installation media."]="Reinicie e retire o suporte de instalação."
  ["The log below is the whole story — the last lines say why."]="O registo abaixo conta a história toda — as últimas linhas dizem porquê."
  ["This takes a while: the base system and the packages are downloaded, and an AI model is gigabytes on its own."]="Isto demora: o sistema base e os pacotes são descarregados, e um modelo de IA são gigabytes por si só."
  ["Back"]="Retroceder"
  ["Next"]="Seguinte"
  ["Reboot"]="Reiniciar"
  ["Close"]="Fechar"
  ["type to filter, or type a name that is not listed"]="escreva para filtrar, ou escreva um nome que não esteja listado"
  ["Nothing to list on this image — type the name instead."]="Nada a listar nesta imagem — escreva antes o nome."
  ["Nothing matches — the row below uses what you typed."]="Nada corresponde — a linha abaixo usa o que escreveu."
  ["Use “%1” as typed"]="Usar “%1” tal como escrito"
)
