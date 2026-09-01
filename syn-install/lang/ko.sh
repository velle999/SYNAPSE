# 한국어 (ko) — syn-install's own words.
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
  ["render.nix is missing — the 'syn' package is not installed here."]="render.nix 가 없습니다 — 이 기기에는 'syn' 패키지가 설치되어 있지 않습니다."
  ["locale-gen failed. The live session stays in English; the install is
  unaffected, because it generates the locale inside the target."]="locale-gen 이 실패했습니다. 라이브 세션은 영어로 남지만 설치 자체에는
  영향이 없습니다. 로케일은 설치 대상 안에서 만들어집니다."
  ["  The keyboard, the clock, the fonts and the shell all follow this.
  You can change any of it later."]="  키보드, 시계, 글꼴, 셸이 모두 이 선택을 따릅니다.
  전부 나중에 바꿀 수 있습니다."
  ["Toggle [numbers, 'all', 'none', Enter = accept]:"]="전환 [번호, 'all', 'none', Enter = 확정]:"
  ["--config needs a file"]="--config 에는 파일이 필요합니다"
  ["syn-install must be run as root"]="syn-install 은 root 로 실행해야 합니다"
  ["  SynapseOS is running from the live image."]="  SynapseOS 가 라이브 이미지에서 실행 중입니다."
  ["Starting the desktop — the installer opens with it."]="데스크톱을 시작합니다 — 설치 프로그램도 함께 열립니다."
  ["  This installer will:
    1. Partition a disk
    2. Install SynapseOS base system
    3. Install SynapseOS packages
    4. Create user account
    5. Choose desktop environment
    6. Configure system & bootloader"]="  이 설치 프로그램은 다음을 합니다:
    1. 디스크를 분할합니다
    2. SynapseOS 기본 시스템을 설치합니다
    3. SynapseOS 패키지를 설치합니다
    4. 사용자 계정을 만듭니다
    5. 데스크톱 환경을 고릅니다
    6. 시스템과 부트로더를 설정합니다"
  ["ALL DATA ON THE TARGET DISK WILL BE ERASED"]="대상 디스크의 모든 데이터가 지워집니다"
  ["Press ENTER to continue or Ctrl+C to abort..."]="계속하려면 ENTER, 중단하려면 Ctrl+C 를 누르세요..."
  ["Checking network"]="네트워크를 확인하는 중"
  ["Network is up"]="네트워크가 연결되어 있습니다"
  ["  No network detected. Starting NetworkManager..."]="  네트워크가 없습니다. NetworkManager 를 시작합니다..."
  ["  No connection — but this machine has Wi-Fi."]="  연결이 없습니다 — 다만 이 기기에는 Wi-Fi 가 있습니다."
  ["Open the Wi-Fi picker (nmtui)? [Y/n]:"]="Wi-Fi 선택 화면(nmtui)을 열까요? [Y/n]:"
  ["No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."]="네트워크 연결도 없고 설정할 Wi-Fi 장치도 없습니다.
  SynapseOS 는 설치 중에 기본 시스템을 내려받으므로, 랜선을 꽂고
  다시 실행해 주세요."
  ["Network connected"]="네트워크 연결됨"
  ["Step 1 — Select Target Disk"]="1 단계 — 대상 디스크 고르기"
  ["  Available disks:"]="  사용할 수 있는 디스크:"
  ["Target disk (e.g. sda, vda, nvme0n1):"]="대상 디스크 (예: sda, vda, nvme0n1):"
  ["Target disk is in use. Unmount its partitions and re-run."]="대상 디스크가 사용 중입니다. 파티션을 마운트 해제하고 다시 실행하세요."
  ["Boot mode: UEFI"]="부팅 모드: UEFI"
  ["Boot mode: BIOS/Legacy"]="부팅 모드: BIOS/레거시"
  ["  Encrypts the root filesystem with LUKS2. You will be asked for the
  passphrase at every boot, before the system starts."]="  루트 파일 시스템을 LUKS2 로 암호화합니다. 부팅할 때마다, 시스템이
  시작되기 전에 암호문을 묻습니다."
  ["Encrypt the disk? [y/N]:"]="디스크를 암호화할까요? [y/N]:"
  ["                          With encryption it is the BETTER choice: the
                          kernel lives on the EFI partition and only the
                          initramfs unlocks, so /boot needs no separate
                          unencrypted partition."]="                          암호화할 때는 이쪽이 더 낫습니다: 커널이 EFI
                          파티션에 있고 initramfs 만 잠금을 풀기 때문에,
                          /boot 에 암호화하지 않은 별도 파티션이
                          필요하지 않습니다."
  ["                          It copies each snapshot's kernel onto the EFI
                          partition, so that partition is made much
                          larger when snapshots are enabled."]="                          스냅샷마다 커널을 EFI 파티션으로 복사하므로,
                          스냅샷을 켜면 그 파티션을 훨씬 크게 잡습니다."
  ["  Snapshots are cheap but not free: they hold the old copy of
  anything that changes, so a disk near full stays near full."]="  스냅샷은 싸지만 공짜는 아닙니다: 바뀐 것의 옛 사본을 그대로 들고
  있으므로, 거의 꽉 찬 디스크는 계속 거의 꽉 찬 채로 있습니다."
  ["Enable snapshots? [Y/n]:"]="스냅샷을 켤까요? [Y/n]:"
  ["mkfs.ext4 is missing from this installer image — /boot cannot be created"]="이 설치 이미지에 mkfs.ext4 가 없습니다 — /boot 를 만들 수 없습니다"
  ["btrfs is missing from this installer image — subvolumes cannot be created"]="이 설치 이미지에 btrfs 가 없습니다 — 서브볼륨을 만들 수 없습니다"
  ["Are these correct? [Y/n]:"]="이대로 맞습니까? [Y/n]:"
  ["Starting the questions over — the disk has not been touched."]="질문을 처음부터 다시 합니다 — 디스크는 건드리지 않았습니다."
  ["cryptsetup is not available on this installer image"]="이 설치 이미지에서는 cryptsetup 을 쓸 수 없습니다"
  ["Encryption passphrase:"]="암호화 암호문:"
  ["Repeat passphrase:"]="암호문을 한 번 더:"
  ["Empty passphrase — that would leave the disk unprotected."]="암호문이 비어 있습니다 — 그러면 디스크가 보호받지 못합니다."
  ["Passphrases did not match — try again."]="암호문이 서로 다릅니다 — 다시 해 주세요."
  ["Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."]="암호문이 8 자보다 짧습니다. 디스크를 손에 쥔 상대에게는
  짧은 암호문이 거의 소용없습니다."
  ["Use it anyway? [y/N]:"]="그래도 쓸까요? [y/N]:"
  ["Encryption enabled — root will be LUKS2"]="암호화를 켰습니다 — 루트는 LUKS2 가 됩니다"
  ["cryptsetup open failed — the passphrase did not take"]="cryptsetup open 이 실패했습니다 — 암호문이 통하지 않았습니다"
  ["Failed to mount root"]="루트를 마운트하지 못했습니다"
  ["  Creating btrfs subvolumes..."]="  btrfs 서브볼륨을 만드는 중..."
  ["btrfs: could not create @"]="btrfs: @ 를 만들지 못했습니다"
  ["btrfs: could not create @home"]="btrfs: @home 을 만들지 못했습니다"
  ["btrfs: could not create @snapshots"]="btrfs: @snapshots 를 만들지 못했습니다"
  ["btrfs: could not create @var_log"]="btrfs: @var_log 를 만들지 못했습니다"
  ["btrfs: could not create @pkg"]="btrfs: @pkg 를 만들지 못했습니다"
  ["could not remount the btrfs root onto @"]="btrfs 루트를 @ 로 다시 마운트하지 못했습니다"
  ["Failed to mount @"]="@ 를 마운트하지 못했습니다"
  ["Failed to mount @home"]="@home 을 마운트하지 못했습니다"
  ["Failed to mount @snapshots"]="@snapshots 를 마운트하지 못했습니다"
  ["Failed to mount @var_log"]="@var_log 를 마운트하지 못했습니다"
  ["Failed to mount @pkg"]="@pkg 를 마운트하지 못했습니다"
  ["This adds one partition in the free space. Back up anything irreplaceable first."]="빈 공간에 파티션 하나를 추가합니다. 대체할 수 없는 것은 먼저 백업하세요."
  ["Type 'yes' to install alongside:"]="나란히 설치하려면 'yes' 를 입력하세요:"
  ["Aborted"]="중단했습니다"
  ["Failed to create the root partition"]="루트 파티션을 만들지 못했습니다"
  ["Could not identify the new partition after creating it"]="새로 만든 파티션을 알아보지 못했습니다"
  ["Failed to format root partition"]="루트 파티션을 포맷하지 못했습니다"
  ["Failed to mount the existing ESP"]="기존 ESP 를 마운트하지 못했습니다"
  ["no partition editor on this image (cfdisk, fdisk and parted are all missing)"]="이 이미지에는 파티션 편집기가 없습니다 (cfdisk, fdisk, parted 모두 없음)"
  ["  What this install needs:"]="  이 설치에 필요한 것:"
  ["    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"]="    • EFI 시스템 파티션 (종류 EF00 / 'esp' 플래그) — 기존 것을 다시 쓸 수 있습니다"
  ["  Skipping the partition editor (--config)."]="  파티션 편집기를 건너뜁니다 (--config)."
  ["Format it? Everything on it is lost [y/N]:"]="포맷할까요? 안에 있는 것은 모두 사라집니다 [y/N]:"
  ["Separate /boot partition:"]="별도의 /boot 파티션:"
  ["Swap partition (blank for none):"]="스왑 파티션 (없으면 비워 두세요):"
  ["Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"]="다시 만들까요? UUID 가 바뀌어 그 시스템의 fstab 이 깨집니다 [y/N]:"
  ["Type 'yes' to format these:"]="이들을 포맷하려면 'yes' 를 입력하세요:"
  ["  Formatting EFI partition..."]="  EFI 파티션을 포맷하는 중..."
  ["  Formatting /boot partition..."]="  /boot 파티션을 포맷하는 중..."
  ["Failed to mount /boot"]="/boot 를 마운트하지 못했습니다"
  ["Type 'yes' to confirm:"]="확인하려면 'yes' 를 입력하세요:"
  ["  Creating GPT partition table..."]="  GPT 파티션 테이블을 만드는 중..."
  ["Failed to format EFI partition"]="EFI 파티션을 포맷하지 못했습니다"
  ["Failed to format boot partition"]="부팅 파티션을 포맷하지 못했습니다"
  ["  Creating MBR partition table..."]="  MBR 파티션 테이블을 만드는 중..."
  ["Disk partitioned and mounted at /mnt"]="디스크를 나누고 /mnt 에 마운트했습니다"
  ["Step 3 — Installing Base System"]="3 단계 — 기본 시스템 설치"
  ["  Initializing pacman keyring..."]="  pacman 키링을 준비하는 중..."
  ["  Running pacstrap (this may take several minutes)..."]="  pacstrap 실행 중 (몇 분 걸릴 수 있습니다)..."
  ["pacstrap failed — check network connection"]="pacstrap 이 실패했습니다 — 네트워크 연결을 확인하세요"
  ["grub-install not found in chroot — attempting recovery..."]="chroot 안에 grub-install 이 없습니다 — 복구를 시도합니다..."
  ["Could not install grub into target — check network"]="대상 시스템에 grub 을 설치하지 못했습니다 — 네트워크를 확인하세요"
  ["Base system installed"]="기본 시스템을 설치했습니다"
  ["Step 4 — Choose What to Install"]="4 단계 — 무엇을 설치할지 고르기"
  ["  What should be installed alongside the SynapseOS core?"]="  SynapseOS 핵심과 함께 무엇을 설치할까요?"
  ["                   Bluetooth, printing, Wine, phone   (default)"]="                   블루투스, 인쇄, Wine, 휴대전화 연동   (기본값)"
  ["                   the ordinary software people install anyway"]="                   어차피 설치하게 되는 흔한 소프트웨어"
  ["  Every preset except Minimal then asks WHICH AI model to download,
  and skipping it is one of the answers."]="  최소 구성을 뺀 모든 구성은 이어서 어떤 AI 모델을 내려받을지 묻고,
  내려받지 않는 것도 답 가운데 하나입니다."
  ["Full install selected"]="전체 설치를 골랐습니다"
  ["Minimal install selected"]="최소 설치를 골랐습니다"
  ["  Two kinds of question. First the packages, as pages of
  checkboxes; then the handful of options that are a whole
  subsystem rather than a package."]="  질문은 두 가지입니다. 먼저 패키지를 체크박스 페이지로,
  그다음에 패키지가 아니라 하나의 하위 시스템에 해당하는
  몇 가지 선택지를 묻습니다."
  ["  And the software people install on the first evening anyway.
  All of it is in the Arch repositories; none of it is ours."]="  그리고 첫날 저녁에 어차피 설치하게 되는 소프트웨어입니다.
  전부 Arch 저장소에 있고, 그중 우리 것은 하나도 없습니다."
  ["  The rest is y/n. The default (shown in caps) is Standard."]="  나머지는 y/n 입니다. 기본값(대문자로 표시)은 표준입니다."
  ["syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."]="syn-update 를 뺐습니다: 이 기기는 다음 SynapseOS 패키지를 받을
  방법이 없어집니다. 나중에 고치려면 ISO 에서 손으로 설치하거나
  다시 설치해야 합니다."
  ["Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."]="데스크톱도 AI 데몬도 남기지 않았습니다. 그것은 SynapseOS 도구 몇 개가
  올라간 Arch 시스템으로, 허용되는 답이기는 하지만 —
  그렇게 만들어진 기기를 설명하는 문서는 어디에도 없습니다."
  ["Custom install configured"]="사용자 지정 설치를 구성했습니다"
  ["Standard install selected"]="표준 설치를 골랐습니다"
  ["  synapd loads one model and everything AI in SynapseOS talks to it:
  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,
  over this connection, onto the disk you are installing to."]="  synapd 가 모델 하나를 올리고, SynapseOS 의 AI 관련 기능은 모두 그것과
  이야기합니다: synsh, 데스크톱의 AI 패널, Chibi, Vibe. 지금 이 연결로
  내려받아 설치 중인 디스크에 놓습니다."
  ["  A smaller model is not just faster and lighter: it follows
  instructions worse. synsh mistakes what you asked for, Vibe's code
  needs more fixing, Chibi loses the thread. Take the default unless
  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."]="  더 작은 모델은 빠르고 가볍기만 한 것이 아니라, 지시를 따르는 능력도
  떨어집니다. synsh 는 요청을 잘못 알아듣고, Vibe 의 코드는 손볼 곳이
  늘고, Chibi 는 맥락을 놓칩니다. 디스크나 메모리가 문제가 아니라면
  기본값을 쓰세요 — 7B 는 여유 메모리 6 GB 정도를 원합니다."
  ["  Whatever you pick, it can be changed later: 'syn model download',
  or Super+C ▸ System ▸ AI model on the desktop."]="  무엇을 고르든 나중에 바꿀 수 있습니다: 'syn model download',
  또는 데스크톱에서 Super+C ▸ 시스템 ▸ AI 모델."
  ["Install this selection? [Y/n]:"]="이 선택대로 설치할까요? [Y/n]:"
  ["Choosing again — nothing has been installed yet."]="다시 고릅니다 — 아직 아무것도 설치하지 않았습니다."
  ["Step 4b — Installing SynapseOS"]="4b 단계 — SynapseOS 설치"
  ["Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."]="/etc/pacman.conf 에서 ILoveCandy 를 켜지 못했습니다 (겉모습만 바뀌는 설정입니다)."
  ["  Enabling [multilib] (32-bit repo, needed by Steam)..."]="  [multilib] 을 켜는 중 (Steam 에 필요한 32 비트 저장소)..."
  ["Could not sync the multilib database — Steam may fail to install"]="multilib 데이터베이스를 동기화하지 못했습니다 — Steam 설치가 실패할 수 있습니다"
  ["Could not enable [multilib]; Steam will be skipped."]="[multilib] 을 켜지 못했습니다. Steam 은 건너뜁니다."
  ["Some SynapseOS packages failed to install — verifying below"]="일부 SynapseOS 패키지가 설치되지 않았습니다 — 아래에서 확인합니다"
  ["No SynapseOS packages were selected. This will be an Arch system."]="SynapseOS 패키지를 하나도 고르지 않았습니다. 이것은 Arch 시스템이 됩니다."
  ["SynapseOS packages installed"]="SynapseOS 패키지를 설치했습니다"
  ["Component selection recorded in /etc/synapseos/components.conf"]="고른 구성을 /etc/synapseos/components.conf 에 기록했습니다"
  ["Step 5 — Create User Account"]="5 단계 — 사용자 계정 만들기"
  ["  Create a user account for the installed system."]="  설치할 시스템에서 쓸 사용자 계정을 만드세요."
  ["Username [default: syn]:"]="사용자 이름 [기본값: syn]:"
  ["Full name (optional):"]="전체 이름 (선택):"
  ["Password:"]="비밀번호:"
  ["Confirm password:"]="비밀번호 확인:"
  ["Passwords do not match or are empty — try again"]="비밀번호가 서로 다르거나 비어 있습니다 — 다시 해 주세요"
  ["Step 6 — Desktop Environment"]="6 단계 — 데스크톱 환경"
  ["  Choose a desktop environment:"]="  데스크톱 환경을 고르세요:"
  ["  Installing KDE Plasma..."]="  KDE Plasma 를 설치하는 중..."
  ["Some KDE packages failed to install"]="일부 KDE 패키지가 설치되지 않았습니다"
  ["KDE Plasma installed"]="KDE Plasma 를 설치했습니다"
  ["  Installing GNOME..."]="  GNOME 을 설치하는 중..."
  ["Some GNOME packages failed to install"]="일부 GNOME 패키지가 설치되지 않았습니다"
  ["GNOME installed (session only — SynapseOS apps, not GNOME's)"]="GNOME 을 설치했습니다 (세션만 — 프로그램은 SynapseOS 의 것이고 GNOME 의 것이 아닙니다)"
  ["  Installing greetd (login screen) + desktop extras..."]="  greetd(로그인 화면)와 데스크톱 부가 요소를 설치하는 중..."
  ["greetd failed to install — boot falls back to getty login"]="greetd 가 설치되지 않았습니다 — 부팅은 getty 로그인으로 되돌아갑니다"
  ["SynapseUI selected (included)"]="SynapseUI 를 골랐습니다 (포함되어 있습니다)"
  ["Installing Wine"]="Wine 설치 중"
  ["Wine installed"]="Wine 을 설치했습니다"
  ["wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."]="wine 이 설치되지 않았습니다 — Windows 의 .exe/.msi 는 실행되지 않습니다.
  나중에 'sudo pacman -S wine wine-mono' 로 설치하세요."
  ["Configuring Video Driver"]="비디오 드라이버 설정 중"
  ["  Virtual machine — installing mesa (synui uses pixman here)..."]="  가상 머신입니다 — mesa 를 설치하는 중 (여기서는 synui 가 pixman 을 씁니다)..."
  ["mesa failed to install"]="mesa 가 설치되지 않았습니다"
  ["NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"]="NVIDIA 드라이버 설치가 실패했습니다 — 이대로면 시스템은 nouveau 로
  부팅되고 synui 의 렌더러는 아예 시작되지 않습니다"
  ["NVIDIA sleep services enabled (VRAM save/restore)"]="NVIDIA 절전 서비스를 켰습니다 (VRAM 저장/복원)"
  ["Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"]="nvidia-{suspend,resume,hibernate} 를 켜지 못했습니다 — 나중에
  NVreg_PreserveVideoMemoryAllocations 를 켜면 절전에서 화면이 검게 남을 수 있습니다"
  ["  synapd can run inference on this GPU instead of the CPU.
  This downloads the CUDA runtime (~4.7 GiB installed)."]="  synapd 는 CPU 대신 이 GPU 에서 추론할 수 있습니다.
  이를 위해 CUDA 환경을 내려받습니다 (설치 후 약 4.7 GiB)."
  ["Enable GPU inference? [Y/n]:"]="GPU 추론을 켤까요? [Y/n]:"
  ["Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda"]="CPU 추론을 그대로 씁니다. 나중에 바꾸려면:
  sudo pacman -S synapse-llama-cuda"
  ["  Installing synapse-llama-cuda (this takes a while)..."]="  synapse-llama-cuda 를 설치하는 중 (시간이 좀 걸립니다)..."
  ["This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"]="이 ISO 에는 llama 의 GPU 빌드가 없어서, NVIDIA 카드가 있어도 synapd 는
  CPU 에서 돕니다. (synapse-llama-cuda 가 있으려면 CUDA 툴킷이 있는
  호스트에서 ISO 를 빌드해야 합니다.)"
  ["Video driver install failed — synui may fall back to software rendering"]="비디오 드라이버 설치가 실패했습니다 — synui 가 소프트웨어 렌더링으로 내려갈 수 있습니다"
  ["Video drivers installed"]="비디오 드라이버를 설치했습니다"
  ["  Enabling GPU inference (synapse-llama-vulkan)..."]="  GPU 추론을 켜는 중 (synapse-llama-vulkan)..."
  ["This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"]="이 ISO 에는 llama 의 Vulkan 빌드가 없어서, AMD/Intel GPU 가 있어도 synapd 는
  CPU 에서 돕니다. (synapse-llama-vulkan 이 있으려면 'shaderc' 와
  vulkan-headers 가 있는 호스트에서 ISO 를 빌드하세요.)"
  ["Installing Steam and the game stack"]="Steam 과 게임 구성 요소 설치 중"
  ["  Installing steam and the 32-bit runtime libraries..."]="  steam 과 32 비트 런타임 라이브러리를 설치하는 중..."
  ["Steam installed (native multilib package)"]="Steam 을 설치했습니다 (네이티브 multilib 패키지)"
  ["steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."]="steam 이 설치되지 않았습니다. 그 밖에는 시스템이 온전합니다 —
  나중에 'sudo pacman -S steam' 으로 설치하세요 ([multilib] 은
  /etc/pacman.conf 에서 이미 켜져 있습니다)."
  ["  Installing the game stack (overlay, governor, micro-compositor)..."]="  게임 구성 요소를 설치하는 중 (오버레이, 조절기, 마이크로 컴포지터)..."
  ["Game stack installed (mangohud, gamemode, gamescope)"]="게임 구성 요소를 설치했습니다 (mangohud, gamemode, gamescope)"
  ["The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"]="게임 구성 요소가 설치되지 않았습니다. Steam 은 그대로 됩니다; FPS
  오버레이, CPU/GPU 조절기, 'synui-game-run --gamescope' 는 안 됩니다.
  나중에 설치하려면:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
  ["Installing CachyOS Proton"]="CachyOS Proton 설치 중"
  ["  Fetching the CachyOS keyring and mirrorlist..."]="  CachyOS 의 키링과 미러 목록을 받는 중..."
  ["  Trusting the CachyOS master key..."]="  CachyOS 마스터 키를 신뢰하는 중..."
  ["Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"]="keyserver.ubuntu.com 에서 CachyOS 마스터 키를 받지 못했습니다.
  그것이 없으면 서명된 키링을 설치할 수 없어 CachyOS Proton 은
  건너뜁니다. 나중에 추가하려면:  synpkg cachyos enable-repo"
  ["  Master key pinned as expected — trusting it..."]="  마스터 키가 예상과 같습니다 — 신뢰합니다..."
  ["[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."]="[cachyos] 가 추가되었지만 패키지가 하나도 없습니다 — 나중의
  업그레이드를 막지 않도록 다시 제거합니다."
  ["The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."]="CachyOS 키링에 예상한 마스터 키가 없습니다.
  신뢰를 거부했습니다 — 저장소를 추가하지 않았습니다."
  ["  Installing proton-cachyos-slr (~340 MB download)..."]="  proton-cachyos-slr 를 설치하는 중 (내려받기 약 340 MB)..."
  ["CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."]="CachyOS Proton 을 설치했습니다 — Steam 에서 게임마다 속성 →
  호환성에서 'proton-cachyos-… (steam linux runtime)' 로 고르세요.
  Steam 은 시작할 때만 찾으므로, 이미 켜져 있다면 다시 시작하세요."
  ["proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"]="proton-cachyos-slr 가 설치되지 않았습니다. Steam 과 Valve 자체 Proton 은
  영향을 받지 않습니다. 나중에 설치하려면:
  sudo pacman -S proton-cachyos-slr"
  ["The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"]="[cachyos] 저장소를 켜지 못해 CachyOS Proton 은 건너뛰었습니다.
  Steam 은 Valve 의 Proton 으로 그대로 됩니다. 나중에 추가하려면:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
  ["Enabling BlackArch"]="BlackArch 켜는 중"
  ["  Fetching the BlackArch bootstrap..."]="  BlackArch 부트스트랩을 받는 중..."
  ["  Master key pinned as expected — running bootstrap..."]="  마스터 키가 예상과 같습니다 — 부트스트랩을 실행합니다..."
  ["blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."]="blackarch-keyring 이 설치되지 않았습니다 — 키 교체가
  이 기기에 닿지 않습니다. 'sudo pacman -S blackarch-keyring' 으로 고치세요."
  ["The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."]="내려받은 strap.sh 가 BlackArch 의 예상 마스터 키를 고정하지 않습니다.
  실행을 거부했습니다 — 저장소를 추가하지 않았습니다."
  ["BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."]="BlackArch 를 켜지 않았습니다. 그 밖에는 시스템이 온전합니다;
  나중에 'sudo syn arsenal --enable-repo' 로 추가하세요."
  ["Installing software"]="소프트웨어 설치 중"
  ["That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."]="그 작업이 실패했습니다 — 멀쩡한 것은 그래도 들어가고 문제가 있는 것은
  이름이 드러나도록, 패키지를 하나씩 다시 시도합니다."
  ["Software installed"]="소프트웨어를 설치했습니다"
  ["Installing Flatpak apps"]="Flatpak 앱 설치 중"
  ["flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."]="flatpak 을 설치하지 못했습니다 — Flatpak 앱은 건너뜁니다.
  다른 것에는 영향이 없습니다."
  ["Could not add the flathub remote"]="flathub 원격 저장소를 추가하지 못했습니다"
  ["Flatpak apps installed"]="Flatpak 앱을 설치했습니다"
  ["Configuring System"]="시스템 설정 중"
  ["  fstab generated"]="  fstab 을 만들었습니다"
  ["Swap recorded in fstab"]="스왑을 fstab 에 기록했습니다"
  ["zram configured (compressed swap, half of RAM up to 8 GiB)"]="zram 을 설정했습니다 (압축 스왑, 메모리의 절반, 최대 8 GiB)"
  ["zram-generator is not installed in the target — no compressed swap"]="대상 시스템에 zram-generator 가 없습니다 — 압축 스왑을 쓰지 않습니다"
  ["  Hostname: synapse"]="  호스트 이름: synapse"
  ["Step 7 — Language & Region"]="7 단계 — 언어와 지역"
  ["   0) Other — enter a locale by hand"]="   0) 기타 — 로케일을 직접 입력"
  ["Locale (e.g. sv_SE.UTF-8):"]="로케일 (예: sv_SE.UTF-8):"
  ["Console keymap (e.g. sv-latin1):"]="콘솔 키맵 (예: sv-latin1):"
  ["Step 8 — Timezone"]="8 단계 — 시간대"
  ["   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"]="   0) 기타 — 아무 tzdata 이름이나 입력 (예: Europe/Lisbon)"
  ["tzdata name (Region/City):"]="tzdata 이름 (지역/도시):"
  ["  Did you mean:"]="  이것을 말씀하신 건가요:"
  ["  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"]="  목록에서 번호를 고르거나, 이것을 보세요: ls /mnt/usr/share/zoneinfo"
  ["  os-release: copied from live system"]="  os-release: 라이브 시스템에서 복사함"
  ["  issue: copied from live system"]="  issue: 라이브 시스템에서 복사함"
  ["Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"]="대상 파일 시스템에 더 이상 쓸 수 없습니다 (디스크 오류? 'dmesg' 를 보세요) — 중단합니다"
  ["sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"]="드롭인을 쓴 뒤 sudoers 규칙이 올바르지 않습니다 — sudo 를 못 쓰는 시스템은 넘기지 않습니다"
  ["Could not relax pam_faillock in /etc/pam.d/system-auth (a tty-less sudo could still lock the account until reboot)."]="/etc/pam.d/system-auth 의 pam_faillock 을 완화하지 못했습니다 (터미널 없는 sudo 가 재부팅 때까지 계정을 잠글 수 있습니다)."
  ["could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"]="/var/lib/synapse-src 를 미리 만들지 못했습니다 — 업데이트 도구가 처음 실행될 때 비밀번호를 묻습니다"
  ["  Desktop: KDE Plasma (SDDM login screen)"]="  데스크톱: KDE Plasma (SDDM 로그인 화면)"
  ["  GDM: SynapseOS logo on the login screen"]="  GDM: 로그인 화면의 SynapseOS 로고"
  ["  Desktop: GNOME (GDM login screen)"]="  데스크톱: GNOME (GDM 로그인 화면)"
  ["  Desktop: TTY only"]="  데스크톱: TTY 만"
  ["  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"]="  데스크톱: SynapseUI (synui 그리터 — 로그인 화면이 잠금 화면과 같습니다)"
  ["  motd: written for this installation"]="  motd: 이 설치에 맞게 썼습니다"
  ["  note: syn-rgb.path is not installed; RGB lights stay off"]="  참고: syn-rgb.path 가 설치되지 않아 RGB 조명은 꺼진 채로 둡니다"
  ["AI model"]="AI 모델"
  ["  AI model skipped — install one later with: syn model download"]="  AI 모델을 건너뛰었습니다 — 나중에 설치하려면: syn model download"
  ["AI model installed"]="AI 모델을 설치했습니다"
  ["  the install, and everything else on the disk is already done."]="  설치에서 가장 긴 부분이고, 디스크 위의 나머지는 이미 끝났습니다."
  ["syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."]="대상 시스템에 syn-model 이 없어 모델을 내려받지 않았습니다.
  핵심 구성에 속하며, 선택을 해제했다면 AI 는 계속 멈춰 있습니다."
  ["Configuring Nix"]="Nix 설정 중"
  ["Nix configured — /etc/synapseos/nix"]="Nix 를 설정했습니다 — /etc/synapseos/nix"
  ["  That is the download — a few hundred MB before any packages
  you add to home.nix. 'syn nix edit' opens it."]="  그것이 바로 내려받는 부분입니다 — home.nix 에 무엇을 더하기도 전에
  수백 MB 입니다. 'syn nix edit' 로 열 수 있습니다."
  ["nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."]="nix 는 설치되었지만 대상 시스템에 'syn' 패키지가 없어 설정 도구가
  준비되지 않았습니다. Nix 자체는 됩니다;
  /etc/synapseos/nix 계층에는 'syn' 이 필요합니다."
  ["nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."]="nix 가 설치되지 않았습니다 — 선언적 계층을 쓸 수 없습니다.
  나중에 'sudo pacman -S nix && sudo syn nix init' 로 설치하세요."
  ["  Generating initramfs..."]="  initramfs 를 만드는 중..."
  ["mkinitcpio failed — the installed system would not boot"]="mkinitcpio 가 실패했습니다 — 설치된 시스템은 부팅되지 않습니다"
  ["initramfs missing after mkinitcpio — the installed system would not boot"]="mkinitcpio 뒤에 initramfs 가 없습니다 — 설치된 시스템은 부팅되지 않습니다"
  ["System configured"]="시스템을 설정했습니다"
  ["Installing Bootloader"]="부트로더 설치 중"
  ["grub-install (UEFI) failed"]="grub-install (UEFI) 가 실패했습니다"
  ["grub-install (BIOS) failed"]="grub-install (BIOS) 가 실패했습니다"
  ["  Generating GRUB config..."]="  GRUB 설정을 만드는 중..."
  ["grub-mkconfig failed"]="grub-mkconfig 가 실패했습니다"
  ["grub.cfg missing after install"]="설치 후 grub.cfg 가 없습니다"
  ["grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"]="grub.cfg 에 GRUB 비밀번호가 들어 있습니다 — root 만 읽도록 두었으므로 설정 앱은 부팅 항목을 알려줄 수 없습니다"
  ["  Installing systemd-boot..."]="  systemd-boot 를 설치하는 중..."
  ["bootctl install failed"]="bootctl install 이 실패했습니다"
  ["  Registering systemd-boot with the firmware..."]="  systemd-boot 를 펌웨어에 등록하는 중..."
  ["efibootmgr entry not created — the removable-media path still applies"]="efibootmgr 항목을 만들지 못했습니다 — 이동식 매체 경로가 그대로 쓰입니다"
  ["could not read the root filesystem UUID"]="루트 파일 시스템의 UUID 를 읽지 못했습니다"
  ["vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"]="ESP 에 vmlinuz-linux 가 없습니다 — systemd-boot 가 부팅할 것을 찾지 못합니다"
  ["initramfs is not on the ESP — systemd-boot would find nothing to boot"]="ESP 에 initramfs 가 없습니다 — systemd-boot 가 부팅할 것을 찾지 못합니다"
  ["systemd-boot did not install its EFI binary"]="systemd-boot 가 EFI 실행 파일을 설치하지 않았습니다"
  ["  Installing limine..."]="  limine 을 설치하는 중..."
  ["could not copy limine's EFI binary to the ESP"]="limine 의 EFI 실행 파일을 ESP 로 복사하지 못했습니다"
  ["limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"]="limine-mkinitcpio-hook 이 설치되지 않았습니다 — 나중에 설치한 커널에는 부팅 항목이 생기지 않습니다"
  ["vmlinuz-linux is not on the ESP — limine would find nothing to boot"]="ESP 에 vmlinuz-linux 가 없습니다 — limine 이 부팅할 것을 찾지 못합니다"
  ["limine's EFI binary is not on the ESP"]="ESP 에 limine 의 EFI 실행 파일이 없습니다"
  ["limine.conf has no kernel entry"]="limine.conf 에 커널 항목이 없습니다"
  ["  Verifying the encrypted boot path..."]="  암호화된 부팅 경로를 확인하는 중..."
  ["/boot is not a separate mount — an encrypted root needs a plain /boot"]="/boot 가 별도의 마운트가 아닙니다 — 암호화된 루트에는 암호화하지 않은 /boot 가 필요합니다"
  ["/boot is missing from fstab — it would not be mounted after boot"]="fstab 에 /boot 가 없습니다 — 부팅 후 마운트되지 않습니다"
  ["Encrypted boot path verified"]="암호화된 부팅 경로를 확인했습니다"
  ["Configuring snapshots"]="스냅샷 설정 중"
  ["snapper's config template is missing — snapshots cannot be configured"]="snapper 의 설정 틀이 없습니다 — 스냅샷을 설정할 수 없습니다"
  ["could not write /etc/snapper/configs/root"]="/etc/snapper/configs/root 를 쓰지 못했습니다"
  ["snapper does not see the 'root' config — snapshots would never be taken"]="snapper 가 'root' 설정을 보지 못합니다 — 스냅샷이 한 번도 만들어지지 않습니다"
  ["snapper's root config was not tuned — timeline snapshots would fill the disk"]="snapper 의 root 설정을 다듬지 못했습니다 — 주기적 스냅샷이 디스크를 채웁니다"
  ["could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"]="grub-btrfsd 를 켜지 못했습니다 — 스냅샷이 부팅 메뉴에 저절로 나타나지 않습니다"
  ["Snapshots enabled (snapper + snap-pac, bootable from GRUB)"]="스냅샷을 켰습니다 (snapper + snap-pac, GRUB 에서 부팅 가능)"
  ["could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"]="limine-snapper-sync 를 켜지 못했습니다 — 스냅샷이 부팅 메뉴에 저절로 닿지 않습니다"
  ["could not take the post-install snapshot"]="설치 후 스냅샷을 만들지 못했습니다"
  ["could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"]="첫 부팅 때의 스냅샷 동기화를 켜지 못했습니다 — 대신 첫 업그레이드 뒤에 메뉴가 채워집니다"
  ["Snapshots enabled (snapper + snap-pac, bootable from limine)"]="스냅샷을 켰습니다 (snapper + snap-pac, limine 에서 부팅 가능)"
  ["Bootloader installed"]="부트로더를 설치했습니다"
  ["  The root account is locked (no root login / su).
  Note: 3 wrong password attempts lock the account for 10 minutes."]="  root 계정은 잠겨 있습니다 (root 로그인도 su 도 되지 않습니다).
  참고: 비밀번호를 3 번 틀리면 계정이 10 분 동안 잠깁니다."
  ["You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."]="부팅할 때마다, 로그인 화면보다 먼저 암호화 암호문을 묻습니다.
  되찾을 방법은 없습니다."
  ["    syn-crypt status                    is this disk encrypted, and how
    sudo syn-crypt change-key           replace the passphrase
    sudo syn-crypt add-key              add a second one
    sudo syn-crypt backup-header FILE   save the LUKS header"]="    syn-crypt status                    이 디스크가 암호화되어 있는지, 어떤 방식인지
    sudo syn-crypt change-key           암호문을 바꿉니다
    sudo syn-crypt add-key              두 번째를 더합니다
    sudo syn-crypt backup-header 파일   LUKS 헤더를 저장합니다"
  ["  means the data is unrecoverable even with the right passphrase."]="  이 되면 올바른 암호문이 있어도 데이터를 되살릴 수 없습니다."
  ["Remove installation media and press ENTER to reboot..."]="설치 매체를 빼고 ENTER 를 눌러 다시 시작하세요..."
  ["Install SynapseOS     — right here, in this terminal"]="SynapseOS 설치        — 바로 여기, 이 터미널에서"
  ["Install graphically   — starts the desktop first"]="그래픽으로 설치       — 먼저 데스크톱을 시작합니다"
  ["Try the live desktop  — look around; install later"]="라이브 데스크톱 체험  — 둘러보고, 설치는 나중에"
  ["Target:"]="대상:"
  ["ALONGSIDE"]="나란히"
  ["ERASE"]="지우기"
  ["ADVANCED"]="고급"
  ["Encrypt this installation?"]="이 설치를 암호화할까요?"
  ["There is no recovery."]="되살릴 방법이 없습니다."
  ["Root filesystem"]="루트 파일 시스템"
  ["ext4   — the default. Boring, proven, repairable by anything."]="ext4   — 기본값. 심심하고, 검증되었고, 어떤 도구로도 고칠 수 있습니다."
  ["btrfs  — snapshots + zstd compression. Roll back a bad update
                    from the boot menu. Uses more RAM and more CPU."]="btrfs  — 스냅샷과 zstd 압축. 잘못된 업데이트를 부팅 메뉴에서 되돌릴
                    수 있습니다. 메모리와 CPU 를 더 씁니다."
  ["xfs    — fast on large files. No snapshots, and it cannot be
                    SHRUNK once created."]="xfs    — 큰 파일에 빠릅니다. 스냅샷이 없고, 한 번 만들면 줄일 수
                    없습니다."
  ["f2fs   — built for flash. Good on SD cards and cheap SSDs;
                    unusual enough that fewer rescue tools know it."]="f2fs   — 플래시용으로 만들었습니다. SD 카드와 값싼 SSD 에 좋습니다;
                    드물어서 아는 복구 도구가 많지 않습니다."
  ["Bootloader"]="부트로더"
  ["GRUB          — the default. Detects other operating systems,
                          and the only one here that can boot a btrfs
                          snapshot."]="GRUB          — 기본값. 다른 운영체제를 찾아내고, 여기서 유일하게
                          btrfs 스냅샷을 부팅할 수 있습니다."
  ["systemd-boot  — minimal. No OS detection, no snapshot menu."]="systemd-boot  — 최소한. OS 탐지도, 스냅샷 메뉴도 없습니다."
  ["limine        — modern and fast, and it CAN boot snapshots."]="limine        — 새롭고 빠르며, 스냅샷도 부팅할 수 있습니다."
  ["Automatic snapshots?"]="스냅샷을 자동으로 만들까요?"
  ["Review the plan — nothing has been written yet:"]="계획을 확인하세요 — 아직 아무것도 쓰지 않았습니다:"
  ["nothing else is touched"]="그 밖에는 아무것도 건드리지 않습니다"
  ["not"]="하지 않음"
  ["Partition"]="지금 분할하세요"
  ["now."]="지금."
  ["Partitions now on"]="현재 파티션:"
  ["These partitions will be FORMATTED"]="이 파티션들은 포맷됩니다"
  ["Full      — Standard + Steam + Nix + more software"]="전체      — 표준 + Steam + Nix + 더 많은 소프트웨어"
  ["Standard  — the SynapseOS suite, Firefox, AI model,"]="표준      — SynapseOS 모음, Firefox, AI 모델,"
  ["Minimal   — core daemons only: none of the above"]="최소      — 핵심 데몬만: 위의 것은 아무것도 없음"
  ["Custom    — tick every package yourself, ours and"]="사용자 지정 — 우리 것과 그 밖의 패키지를 직접 골라서"
  ["Which AI model should this machine run?"]="이 기기에서 어떤 AI 모델을 돌릴까요?"
  ["Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"]="Mistral 7B Instruct   약 4.1 GB   권장 — SynapseOS 가 여기에 맞춰져 있습니다"
  ["Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"]="Phi-3 Mini 4K         약 2.2 GB   절반 크기이고, 눈에 띄게 약합니다"
  ["Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"]="Qwen2 0.5B            약 0.4 GB   어디든 들어가고, 대답도 그만큼입니다"
  ["None                            skip it — nothing else changes"]="없음                              건너뛰기 — 다른 것은 바뀌지 않습니다"
  ["Installing:"]="설치할 것:"
  ["SynapseUI  — AI-native Wayland compositor  (default)"]="SynapseUI  — AI 를 전제로 만든 Wayland 컴포지터  (기본값)"
  ["SynapseUI  — NOT AVAILABLE: synui was not selected"]="SynapseUI  — 쓸 수 없음: synui 를 고르지 않았습니다"
  ["KDE Plasma — Full-featured Wayland desktop"]="KDE Plasma — 기능이 다 갖춰진 Wayland 데스크톱"
  ["GNOME      — Clean, modern Wayland desktop"]="GNOME      — 깔끔하고 현대적인 Wayland 데스크톱"
  ["TTY only   — No GUI (headless/server)"]="TTY 만     — 그래픽 없음 (헤드리스/서버)"
  ["Disk:"]="디스크:"
  ["Boot:"]="부팅:"
  ["Encrypted:"]="암호화:"
  ["Desktop:"]="데스크톱:"
  ["User:"]="사용자:"
  ["Hostname:"]="호스트 이름:"
  ["Back up the header to another machine."]="헤더는 다른 기기에 백업해 두세요."
  ["%s is mounted — unmount it first\\n"]="%s 는 마운트되어 있습니다 — 먼저 마운트를 해제하세요
"
  ["%s is %s MiB — %s needs at least %s MiB\\n"]="%s 는 %s MiB 입니다 — %s 에는 최소 %s MiB 가 필요합니다
"
  ["  Generating %s (a few seconds)...\\n"]="  %s 를 만드는 중 (몇 초)...
"
  ["Language: %s  (%s, keyboard %s)\\n"]="언어: %s  (%s, 키보드 %s)
"
  ["  This disk already holds %s partition(s), an EFI System\\n  Partition (%s), and %s GiB of free space.\\n"]="  이 디스크에는 이미 %s 개의 파티션, EFI 시스템 파티션 (%s),
  그리고 %s GiB 의 빈 공간이 있습니다.
"
  ["    1) Install %s — use the free space, keep everything else\\n"]="    1) %s 설치 — 빈 공간을 쓰고, 나머지는 그대로 둡니다
"
  ["    2) %s the whole disk — delete every partition and all data\\n"]="    2) 디스크 전체 %s — 모든 파티션과 데이터를 지웁니다
"
  ["    3) %s — partition this disk yourself, then pick the partitions\\n"]="    3) %s — 이 디스크를 직접 나눈 뒤 파티션을 고릅니다
"
  ["    1) %s the whole disk — delete every partition and all data  (default)\\n"]="    1) 디스크 전체 %s — 모든 파티션과 데이터를 지웁니다  (기본값)
"
  ["    2) %s — partition this disk yourself, then pick the partitions\\n"]="    2) %s — 이 디스크를 직접 나눈 뒤 파티션을 고릅니다
"
  ["  %s If you forget the passphrase the data is\\n  gone — no password reset, no support call, nothing.\\n"]="  %s 암호문을 잊으면 데이터는 사라집니다 —
  재설정도, 지원 요청도, 아무것도 없습니다.
"
  ["  snapper takes a snapshot before and after every pacman\\n  transaction, and %s grows a menu to boot any of them. A\\n  bad upgrade becomes a reboot instead of a rescue USB.\\n"]="  snapper 는 pacman 작업 전후로 스냅샷을 만들고, %s 에는 그중
  아무거나 부팅할 수 있는 메뉴가 생깁니다. 잘못된 업그레이드가
  구조용 USB 대신 재부팅 한 번으로 끝납니다.
"
  ["    Disk          : %s\\n"]="    디스크        : %s
"
  ["    Firmware      : %s\\n"]="    펌웨어        : %s
"
  ["    Filesystem    : %s\\n"]="    파일 시스템   : %s
"
  ["    Bootloader    : %s\\n"]="    부트로더      : %s
"
  ["    Separate /boot: %s\\n"]="    별도의 /boot: %s
"
  ["    Encryption    : %s\\n"]="    암호화        : %s
"
  ["    Snapshots     : %s\\n"]="    스냅샷        : %s
"
  ["  Encrypting %s (LUKS2)...\\n"]="  %s 를 암호화하는 중 (LUKS2)...
"
  ["  Formatting root partition (%s)...\\n"]="  루트 파티션을 포맷하는 중 (%s)...
"
  ["    • KEEP   all %s existing partition(s), including Windows\\n"]="    • 유지  기존 %s 개의 파티션 전부, Windows 포함
"
  ["    • REUSE  %s as the EFI partition (mounted, %s formatted)\\n"]="    • 재사용 %s 를 EFI 파티션으로 (마운트만, %s 포맷)
"
  ["    • CREATE a new ext4 root of ~%s GiB in the free space\\n"]="    • 생성  빈 공간에 약 %s GiB 의 새 ext4 루트를
"
  ["  Creating root partition in free space (%sMiB–%sMiB)...\\n"]="  빈 공간에 루트 파티션을 만드는 중 (%s MiB–%s MiB)...
"
  ["  Formatting new root (%s, ext4)...\\n"]="  새 루트를 포맷하는 중 (%s, ext4)...
"
  ["  %s %s %s The installer will re-read the table when you exit.\\n"]="  %s %s %s 나가면 설치 프로그램이 테이블을 다시 읽습니다.
"
  ["    • a root partition, at least %s GiB\\n"]="    • 최소 %s GiB 의 루트 파티션
"
  ["    • a separate /boot of ~1 GiB — %s with this layout cannot read the root\\n"]="    • 약 1 GiB 의 별도 /boot — 이 구성에서는 %s 가 루트를 읽지 못합니다
"
  ["  Starting %s on %s — write your changes before quitting.\\n"]="  %s 에서 %s 를 시작합니다 — 나가기 전에 바뀐 내용을 쓰세요.
"
  ["    %s is already swap — another system may resume from it.\\n"]="    %s 는 이미 스왑입니다 — 다른 시스템이 거기서 깨어날 수 있습니다.
"
  ["  Everything else on %s is left untouched.\\n"]="  %s 의 나머지는 그대로 둡니다.
"
  ["  Making swap on %s...\\n"]="  %s 에 스왑을 만드는 중...
"
  ["  Formatting EFI partition (%s MiB)...\\n"]="  EFI 파티션을 포맷하는 중 (%s MiB)...
"
  ["  NVIDIA GPU detected — installing %s (builds the module, takes a while)...\\n"]="  NVIDIA GPU 를 찾았습니다 — %s 를 설치하는 중 (모듈을 빌드하므로 시간이 걸립니다)...
"
  ["  Installing video stack: %s %s...\\n"]="  영상 구성 요소를 설치하는 중: %s %s...
"
  ["  [cachyos] enabled (%s packages available)\\n"]="  [cachyos] 를 켰습니다 (쓸 수 있는 패키지 %s 개)
"
  ["  Language: %s  (chosen at boot)\\n"]="  언어: %s  (부팅할 때 고름)
"
  ["  Locale:   %s   Keyboard: %s (console) / %s (desktop)\\n"]="  로케일: %s   키보드: %s (콘솔) / %s (데스크톱)
"
  ["  Installing fonts (%s)...\\n"]="  글꼴을 설치하는 중 (%s)...
"
  ["  Downloading the AI model (%s) — this is the long part of\\n"]="  AI 모델을 내려받는 중 (%s) — 여기가
"
  ["  Nothing is built yet. As %s, after the first boot:\\n"]="  아직 아무것도 빌드하지 않았습니다. %s 로, 첫 부팅 뒤에:
"
  ["  Adding the %s hook to mkinitcpio...\\n"]="  mkinitcpio 에 %s 훅을 더하는 중...
"
  ["  Installing GRUB (%s)...\\n"]="  GRUB 을 설치하는 중 (%s)...
"
  ["yes — LUKS2 on %s"]="예 — %s 의 LUKS2"
  ["  Admin: use %s with your user password.\\n"]="  관리자 작업: 자기 사용자 비밀번호로 %s 를 쓰세요.
"
  ["  Manage it later with %s:\\n"]="  나중에 %s 로 관리하세요:
"
  ["  %s A damaged LUKS header\\n"]="  %s LUKS 헤더가 손상되면
"
  ["%s is on the live/boot device — that is the installer's own media\\n"]="%s 는 라이브/부팅 장치에 있습니다 — 설치 프로그램 자신의 매체입니다
"
  ["    %s is already FAT — it may hold another OS's bootloader.\\n"]="    %s 는 이미 FAT 입니다 — 다른 OS 의 부트로더가 들어 있을 수 있습니다.
"
  ["  Creating user '%s'...\\n"]="  사용자 '%s' 를 만드는 중...
"
  ["  User '%s' created (uid=%s)\\n"]="  사용자 '%s' 를 만들었습니다 (uid=%s)
"
  ["  Log in as '%s' after reboot.\\n"]="  다시 시작한 뒤 '%s' 로 로그인하세요.
"
  ["  Type '%s' to get started.\\n"]="  '%s' 라고 입력해 시작해 보세요.
"
)
