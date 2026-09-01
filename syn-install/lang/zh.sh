# 中文 (zh) — syn-install's own words.
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
  ["render.nix is missing — the 'syn' package is not installed here."]="找不到 render.nix — 这台机器没有安装 'syn' 软件包。"
  ["locale-gen failed. The live session stays in English; the install is
  unaffected, because it generates the locale inside the target."]="locale-gen 失败。实时会话仍是英文；安装本身不受影响，
  因为它会在目标系统里生成语言环境。"
  ["  The keyboard, the clock, the fonts and the shell all follow this.
  You can change any of it later."]="  键盘、时钟、字体和 shell 都跟着这个选择走。
  这些以后都可以改。"
  ["Toggle [numbers, 'all', 'none', Enter = accept]:"]="切换 [编号, 'all', 'none', 回车 = 确认]:"
  ["--config needs a file"]="--config 需要一个文件"
  ["syn-install must be run as root"]="syn-install 必须以 root 身份运行"
  ["  SynapseOS is running from the live image."]="  SynapseOS 正从实时镜像运行。"
  ["Starting the desktop — the installer opens with it."]="正在启动桌面 — 安装程序会随它一起打开。"
  ["  This installer will:
    1. Partition a disk
    2. Install SynapseOS base system
    3. Install SynapseOS packages
    4. Create user account
    5. Choose desktop environment
    6. Configure system & bootloader"]="  这个安装程序会:
    1. 给硬盘分区
    2. 安装 SynapseOS 基本系统
    3. 安装 SynapseOS 软件包
    4. 创建用户账户
    5. 选择桌面环境
    6. 配置系统和引导程序"
  ["ALL DATA ON THE TARGET DISK WILL BE ERASED"]="目标硬盘上的所有数据都将被清除"
  ["Press ENTER to continue or Ctrl+C to abort..."]="按回车继续，或按 Ctrl+C 中止..."
  ["Checking network"]="正在检查网络"
  ["Network is up"]="网络已就绪"
  ["  No network detected. Starting NetworkManager..."]="  未检测到网络。正在启动 NetworkManager..."
  ["  No connection — but this machine has Wi-Fi."]="  没有连接 — 但这台机器有 Wi-Fi。"
  ["Open the Wi-Fi picker (nmtui)? [Y/n]:"]="要打开 Wi-Fi 选择器 (nmtui) 吗? [Y/n]:"
  ["No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."]="没有网络连接，也没有可配置的 Wi-Fi 设备。
  SynapseOS 在安装过程中要下载基本系统，请插上网线后重新运行。"
  ["Network connected"]="网络已连接"
  ["Step 1 — Select Target Disk"]="第 1 步 — 选择目标硬盘"
  ["  Available disks:"]="  可用的硬盘:"
  ["Target disk (e.g. sda, vda, nvme0n1):"]="目标硬盘 (例如 sda、vda、nvme0n1):"
  ["Target disk is in use. Unmount its partitions and re-run."]="目标硬盘正在使用中。请卸载它的分区后重新运行。"
  ["Boot mode: UEFI"]="引导模式: UEFI"
  ["Boot mode: BIOS/Legacy"]="引导模式: BIOS/传统"
  ["  Encrypts the root filesystem with LUKS2. You will be asked for the
  passphrase at every boot, before the system starts."]="  用 LUKS2 加密根文件系统。每次开机、在系统启动之前
  都会要求输入密码短语。"
  ["Encrypt the disk? [y/N]:"]="要加密硬盘吗? [y/N]:"
  ["                          With encryption it is the BETTER choice: the
                          kernel lives on the EFI partition and only the
                          initramfs unlocks, so /boot needs no separate
                          unencrypted partition."]="                          启用加密时它是更好的选择: 内核放在 EFI 分区上，
                          只有 initramfs 负责解锁，因此 /boot 不需要
                          单独的未加密分区。"
  ["                          It copies each snapshot's kernel onto the EFI
                          partition, so that partition is made much
                          larger when snapshots are enabled."]="                          它会把每个快照的内核复制到 EFI 分区，所以
                          启用快照时那个分区会创建得大得多。"
  ["  Snapshots are cheap but not free: they hold the old copy of
  anything that changes, so a disk near full stays near full."]="  快照便宜但不是免费的: 它们保留所有改动过的旧副本，
  所以快满的硬盘还是会一直快满。"
  ["Enable snapshots? [Y/n]:"]="要启用快照吗? [Y/n]:"
  ["mkfs.ext4 is missing from this installer image — /boot cannot be created"]="这个安装镜像里没有 mkfs.ext4 — 无法创建 /boot"
  ["btrfs is missing from this installer image — subvolumes cannot be created"]="这个安装镜像里没有 btrfs — 无法创建子卷"
  ["Are these correct? [Y/n]:"]="这样对吗? [Y/n]:"
  ["Starting the questions over — the disk has not been touched."]="重新开始提问 — 硬盘还没有被动过。"
  ["cryptsetup is not available on this installer image"]="这个安装镜像上没有 cryptsetup"
  ["Encryption passphrase:"]="加密密码短语:"
  ["Repeat passphrase:"]="再输入一次密码短语:"
  ["Empty passphrase — that would leave the disk unprotected."]="密码短语为空 — 那样硬盘就毫无保护。"
  ["Passphrases did not match — try again."]="两次密码短语不一致 — 请再试一次。"
  ["Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."]="密码短语不到 8 个字符。对拿着硬盘的人来说，太短的
  几乎起不到作用。"
  ["Use it anyway? [y/N]:"]="仍然使用吗? [y/N]:"
  ["Encryption enabled — root will be LUKS2"]="已启用加密 — 根分区将使用 LUKS2"
  ["cryptsetup open failed — the passphrase did not take"]="cryptsetup open 失败 — 密码短语没有通过"
  ["Failed to mount root"]="挂载根分区失败"
  ["  Creating btrfs subvolumes..."]="  正在创建 btrfs 子卷..."
  ["btrfs: could not create @"]="btrfs: 无法创建 @"
  ["btrfs: could not create @home"]="btrfs: 无法创建 @home"
  ["btrfs: could not create @snapshots"]="btrfs: 无法创建 @snapshots"
  ["btrfs: could not create @var_log"]="btrfs: 无法创建 @var_log"
  ["btrfs: could not create @pkg"]="btrfs: 无法创建 @pkg"
  ["could not remount the btrfs root onto @"]="无法把 btrfs 根重新挂载到 @"
  ["Failed to mount @"]="挂载 @ 失败"
  ["Failed to mount @home"]="挂载 @home 失败"
  ["Failed to mount @snapshots"]="挂载 @snapshots 失败"
  ["Failed to mount @var_log"]="挂载 @var_log 失败"
  ["Failed to mount @pkg"]="挂载 @pkg 失败"
  ["This adds one partition in the free space. Back up anything irreplaceable first."]="这会在空闲空间里新增一个分区。请先备份任何无法替代的东西。"
  ["Type 'yes' to install alongside:"]="输入 'yes' 以并排安装:"
  ["Aborted"]="已中止"
  ["Failed to create the root partition"]="创建根分区失败"
  ["Could not identify the new partition after creating it"]="创建之后无法识别新分区"
  ["Failed to format root partition"]="格式化根分区失败"
  ["Failed to mount the existing ESP"]="挂载现有 ESP 失败"
  ["no partition editor on this image (cfdisk, fdisk and parted are all missing)"]="这个镜像上没有分区编辑器 (cfdisk、fdisk 和 parted 都没有)"
  ["  What this install needs:"]="  这次安装需要:"
  ["    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"]="    • 一个 EFI 系统分区 (类型 EF00 / 'esp' 标志) — 已有的可以复用"
  ["  Skipping the partition editor (--config)."]="  跳过分区编辑器 (--config)。"
  ["Format it? Everything on it is lost [y/N]:"]="要格式化吗? 上面的一切都会丢失 [y/N]:"
  ["Separate /boot partition:"]="单独的 /boot 分区:"
  ["Swap partition (blank for none):"]="交换分区 (留空表示不用):"
  ["Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"]="要重新创建吗? 它的 UUID 会改变，从而破坏那个系统的 fstab [y/N]:"
  ["Type 'yes' to format these:"]="输入 'yes' 以格式化这些分区:"
  ["  Formatting EFI partition..."]="  正在格式化 EFI 分区..."
  ["  Formatting /boot partition..."]="  正在格式化 /boot 分区..."
  ["Failed to mount /boot"]="挂载 /boot 失败"
  ["Type 'yes' to confirm:"]="输入 'yes' 以确认:"
  ["  Creating GPT partition table..."]="  正在创建 GPT 分区表..."
  ["Failed to format EFI partition"]="格式化 EFI 分区失败"
  ["Failed to format boot partition"]="格式化引导分区失败"
  ["  Creating MBR partition table..."]="  正在创建 MBR 分区表..."
  ["Disk partitioned and mounted at /mnt"]="硬盘已分区并挂载到 /mnt"
  ["Step 3 — Installing Base System"]="第 3 步 — 安装基本系统"
  ["  Initializing pacman keyring..."]="  正在初始化 pacman 密钥环..."
  ["  Running pacstrap (this may take several minutes)..."]="  正在运行 pacstrap (可能需要几分钟)..."
  ["pacstrap failed — check network connection"]="pacstrap 失败 — 请检查网络连接"
  ["grub-install not found in chroot — attempting recovery..."]="在 chroot 中找不到 grub-install — 正在尝试补救..."
  ["Could not install grub into target — check network"]="无法把 grub 安装到目标系统 — 请检查网络"
  ["Base system installed"]="基本系统已安装"
  ["Step 4 — Choose What to Install"]="第 4 步 — 选择要安装什么"
  ["  What should be installed alongside the SynapseOS core?"]="  除了 SynapseOS 的核心之外还要装什么?"
  ["                   Bluetooth, printing, Wine, phone   (default)"]="                   蓝牙、打印、Wine、手机联动   (默认)"
  ["                   the ordinary software people install anyway"]="                   反正大家都会装的常用软件"
  ["  Every preset except Minimal then asks WHICH AI model to download,
  and skipping it is one of the answers."]="  除“最小”之外的每种预设，接下来都会问要下载哪个 AI 模型，
  而“不下载”也是其中一个答案。"
  ["Full install selected"]="已选择完整安装"
  ["Minimal install selected"]="已选择最小安装"
  ["  Two kinds of question. First the packages, as pages of
  checkboxes; then the handful of options that are a whole
  subsystem rather than a package."]="  有两类问题。先是软件包，以一页页复选框的形式;
  然后是那几个不是软件包、而是整个子系统的选项。"
  ["  And the software people install on the first evening anyway.
  All of it is in the Arch repositories; none of it is ours."]="  还有大家第一天晚上反正都会装的软件。
  它们全在 Arch 的仓库里，没有一个是我们的。"
  ["  The rest is y/n. The default (shown in caps) is Standard."]="  其余是 y/n。默认值 (大写显示) 是标准。"
  ["syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."]="取消了 syn-update: 这台机器将没有任何办法收到下一个
  SynapseOS 软件包。以后要补救，只能从 ISO 手动安装，或者重装。"
  ["Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."]="桌面和 AI 守护进程都没有保留。那就是一台装了几个 SynapseOS
  工具的 Arch 系统 — 这是允许的答案，但文档里不会有任何一处
  描述你最后得到的这台机器。"
  ["Custom install configured"]="自定义安装已配置"
  ["Standard install selected"]="已选择标准安装"
  ["  synapd loads one model and everything AI in SynapseOS talks to it:
  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,
  over this connection, onto the disk you are installing to."]="  synapd 加载一个模型，SynapseOS 里所有跟 AI 有关的东西都和它对话:
  synsh、桌面的 AI 面板、Chibi、Vibe。现在就通过这条连接下载，
  放到你正在安装的这块硬盘上。"
  ["  A smaller model is not just faster and lighter: it follows
  instructions worse. synsh mistakes what you asked for, Vibe's code
  needs more fixing, Chibi loses the thread. Take the default unless
  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."]="  更小的模型不只是更快更轻: 它遵循指令的能力也更差。synsh 会误解
  你的要求，Vibe 写的代码要多改几遍，Chibi 会跟丢话题。除非硬盘或
  内存不允许，否则请用默认值 — 7B 需要约 6 GB 空闲内存。"
  ["  Whatever you pick, it can be changed later: 'syn model download',
  or Super+C ▸ System ▸ AI model on the desktop."]="  无论选哪个，以后都能改: 'syn model download'，
  或者在桌面上按 Super+C ▸ 系统 ▸ AI 模型。"
  ["Install this selection? [Y/n]:"]="要安装这些选择吗? [Y/n]:"
  ["Choosing again — nothing has been installed yet."]="重新选择 — 目前还什么都没有装。"
  ["Step 4b — Installing SynapseOS"]="第 4b 步 — 正在安装 SynapseOS"
  ["Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."]="无法在 /etc/pacman.conf 中启用 ILoveCandy (只影响外观)。"
  ["  Enabling [multilib] (32-bit repo, needed by Steam)..."]="  正在启用 [multilib] (32 位仓库，Steam 需要它)..."
  ["Could not sync the multilib database — Steam may fail to install"]="无法同步 multilib 数据库 — Steam 可能装不上"
  ["Could not enable [multilib]; Steam will be skipped."]="无法启用 [multilib]；将跳过 Steam。"
  ["Some SynapseOS packages failed to install — verifying below"]="有些 SynapseOS 软件包没装上 — 下面会逐个核对"
  ["No SynapseOS packages were selected. This will be an Arch system."]="没有选择任何 SynapseOS 软件包。这将是一台 Arch 系统。"
  ["SynapseOS packages installed"]="SynapseOS 软件包已安装"
  ["Component selection recorded in /etc/synapseos/components.conf"]="组件选择已记录到 /etc/synapseos/components.conf"
  ["Step 5 — Create User Account"]="第 5 步 — 创建用户账户"
  ["  Create a user account for the installed system."]="  为安装好的系统创建一个用户账户。"
  ["Username [default: syn]:"]="用户名 [默认: syn]:"
  ["Full name (optional):"]="全名 (可选):"
  ["Password:"]="密码:"
  ["Confirm password:"]="确认密码:"
  ["Passwords do not match or are empty — try again"]="两次密码不一致或为空 — 请再试一次"
  ["Step 6 — Desktop Environment"]="第 6 步 — 桌面环境"
  ["  Choose a desktop environment:"]="  请选择一个桌面环境:"
  ["  Installing KDE Plasma..."]="  正在安装 KDE Plasma..."
  ["Some KDE packages failed to install"]="有些 KDE 软件包没装上"
  ["KDE Plasma installed"]="KDE Plasma 已安装"
  ["  Installing GNOME..."]="  正在安装 GNOME..."
  ["Some GNOME packages failed to install"]="有些 GNOME 软件包没装上"
  ["GNOME installed (session only — SynapseOS apps, not GNOME's)"]="GNOME 已安装 (只有会话 — 应用是 SynapseOS 的，不是 GNOME 的)"
  ["  Installing greetd (login screen) + desktop extras..."]="  正在安装 greetd (登录界面) 和桌面附加组件..."
  ["greetd failed to install — boot falls back to getty login"]="greetd 没装上 — 开机会退回到 getty 登录"
  ["SynapseUI selected (included)"]="已选择 SynapseUI (已包含)"
  ["Installing Wine"]="正在安装 Wine"
  ["Wine installed"]="Wine 已安装"
  ["wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."]="wine 没装上 — Windows 的 .exe/.msi 无法运行。
  以后用 'sudo pacman -S wine wine-mono' 安装。"
  ["Configuring Video Driver"]="正在配置显卡驱动"
  ["  Virtual machine — installing mesa (synui uses pixman here)..."]="  虚拟机 — 正在安装 mesa (synui 在这里用 pixman)..."
  ["mesa failed to install"]="mesa 没装上"
  ["NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"]="NVIDIA 驱动安装失败 — 这样系统会以 nouveau 启动，
  synui 的渲染器根本起不来"
  ["NVIDIA sleep services enabled (VRAM save/restore)"]="已启用 NVIDIA 睡眠服务 (保存/恢复显存)"
  ["Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"]="无法启用 nvidia-{suspend,resume,hibernate} — 如果以后打开
  NVreg_PreserveVideoMemoryAllocations，睡眠唤醒可能是黑屏"
  ["  synapd can run inference on this GPU instead of the CPU.
  This downloads the CUDA runtime (~4.7 GiB installed)."]="  synapd 可以用这块 GPU 而不是 CPU 来推理。
  这会下载 CUDA 运行环境 (安装后约 4.7 GiB)。"
  ["Enable GPU inference? [Y/n]:"]="要启用 GPU 推理吗? [Y/n]:"
  ["Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda"]="保持用 CPU 推理。以后切换:
  sudo pacman -S synapse-llama-cuda"
  ["  Installing synapse-llama-cuda (this takes a while)..."]="  正在安装 synapse-llama-cuda (这会花一些时间)..."
  ["This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"]="这个 ISO 没有 llama 的 GPU 版本，所以即使有 NVIDIA 显卡，synapd 也会
  在 CPU 上运行。(要有 synapse-llama-cuda，必须在装有 CUDA 工具包的
  主机上构建 ISO。)"
  ["Video driver install failed — synui may fall back to software rendering"]="显卡驱动安装失败 — synui 可能退回到软件渲染"
  ["Video drivers installed"]="显卡驱动已安装"
  ["  Enabling GPU inference (synapse-llama-vulkan)..."]="  正在启用 GPU 推理 (synapse-llama-vulkan)..."
  ["This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"]="这个 ISO 没有 llama 的 Vulkan 版本，所以即使有 AMD/Intel GPU，synapd 也会
  在 CPU 上运行。(请在装有 'shaderc' 和 vulkan-headers 的主机上构建 ISO，
  才会有 synapse-llama-vulkan。)"
  ["Installing Steam and the game stack"]="正在安装 Steam 和游戏组件"
  ["  Installing steam and the 32-bit runtime libraries..."]="  正在安装 steam 和 32 位运行库..."
  ["Steam installed (native multilib package)"]="Steam 已安装 (原生 multilib 软件包)"
  ["steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."]="steam 没装上。系统其余部分都已完成 —
  以后用 'sudo pacman -S steam' 安装 ([multilib] 已经在
  /etc/pacman.conf 中启用)。"
  ["  Installing the game stack (overlay, governor, micro-compositor)..."]="  正在安装游戏组件 (叠加层、调频器、微合成器)..."
  ["Game stack installed (mangohud, gamemode, gamescope)"]="游戏组件已安装 (mangohud、gamemode、gamescope)"
  ["The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"]="游戏组件没装上。Steam 仍然可用；但 FPS 叠加层、CPU/GPU 调频器和
  'synui-game-run --gamescope' 用不了。以后安装:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
  ["Installing CachyOS Proton"]="正在安装 CachyOS Proton"
  ["  Fetching the CachyOS keyring and mirrorlist..."]="  正在获取 CachyOS 的密钥环和镜像列表..."
  ["  Trusting the CachyOS master key..."]="  正在信任 CachyOS 的主密钥..."
  ["Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"]="无法从 keyserver.ubuntu.com 获取 CachyOS 的主密钥。
  没有它就装不了已签名的密钥环，因此跳过 CachyOS Proton。
  以后添加:  synpkg cachyos enable-repo"
  ["  Master key pinned as expected — trusting it..."]="  主密钥与预期一致 — 正在信任它..."
  ["[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."]="[cachyos] 已添加但没有列出任何软件包 — 把它再移除，
  以免以后升级时被它挡住。"
  ["The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."]="CachyOS 的密钥环里没有预期的主密钥。
  拒绝信任 — 该仓库并未添加。"
  ["  Installing proton-cachyos-slr (~340 MB download)..."]="  正在安装 proton-cachyos-slr (下载约 340 MB)..."
  ["CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."]="CachyOS Proton 已安装 — 在 Steam 中按游戏进入 属性 → 兼容性 选择，
  列表中显示为 'proton-cachyos-… (steam linux runtime)'。
  Steam 只在启动时扫描，若它已在运行请重启 Steam。"
  ["proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"]="proton-cachyos-slr 没装上。Steam 和 Valve 自己的 Proton 不受影响。
  以后安装:
  sudo pacman -S proton-cachyos-slr"
  ["The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"]="无法启用 [cachyos] 仓库，因此跳过了 CachyOS Proton。
  Steam 用 Valve 的 Proton 依然可用。以后添加:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
  ["Enabling BlackArch"]="正在启用 BlackArch"
  ["  Fetching the BlackArch bootstrap..."]="  正在获取 BlackArch 的引导脚本..."
  ["  Master key pinned as expected — running bootstrap..."]="  主密钥与预期一致 — 正在运行引导脚本..."
  ["blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."]="blackarch-keyring 没装上 — 密钥轮换将到不了这台机器。
  用 'sudo pacman -S blackarch-keyring' 修复。"
  ["The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."]="下载到的 strap.sh 没有固定 BlackArch 预期的主密钥。
  拒绝运行 — 该仓库并未添加。"
  ["BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."]="BlackArch 没有启用。系统其余部分都已完成;
  以后用 'sudo syn arsenal --enable-repo' 添加。"
  ["Installing software"]="正在安装软件"
  ["That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."]="那次事务失败了 — 现在逐个重试每个软件包，好让正常的那些照样装上，
  也让出问题的那个被点名。"
  ["Software installed"]="软件已安装"
  ["Installing Flatpak apps"]="正在安装 Flatpak 应用"
  ["flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."]="无法安装 flatpak — 跳过 Flatpak 应用。
  其他一切不受影响。"
  ["Could not add the flathub remote"]="无法添加 flathub 远程源"
  ["Flatpak apps installed"]="Flatpak 应用已安装"
  ["Configuring System"]="正在配置系统"
  ["  fstab generated"]="  已生成 fstab"
  ["Swap recorded in fstab"]="交换分区已记入 fstab"
  ["zram configured (compressed swap, half of RAM up to 8 GiB)"]="已配置 zram (压缩交换，内存的一半，最多 8 GiB)"
  ["zram-generator is not installed in the target — no compressed swap"]="目标系统里没有 zram-generator — 没有压缩交换"
  ["  Hostname: synapse"]="  主机名: synapse"
  ["Step 7 — Language & Region"]="第 7 步 — 语言与地区"
  ["   0) Other — enter a locale by hand"]="   0) 其他 — 手动输入一个语言环境"
  ["Locale (e.g. sv_SE.UTF-8):"]="语言环境 (例如 sv_SE.UTF-8):"
  ["Console keymap (e.g. sv-latin1):"]="控制台键盘布局 (例如 sv-latin1):"
  ["Step 8 — Timezone"]="第 8 步 — 时区"
  ["   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"]="   0) 其他 — 输入任意 tzdata 名称 (例如 Europe/Lisbon)"
  ["tzdata name (Region/City):"]="tzdata 名称 (地区/城市):"
  ["  Did you mean:"]="  你是想输入:"
  ["  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"]="  从列表里选一个编号，或查看: ls /mnt/usr/share/zoneinfo"
  ["  os-release: copied from live system"]="  os-release: 从实时系统复制"
  ["  issue: copied from live system"]="  issue: 从实时系统复制"
  ["Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"]="目标文件系统已经无法写入 (硬盘错误? 看看 'dmesg') — 中止"
  ["sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"]="写入 drop-in 之后 sudoers 规则不合法 — 不会交付一个用不了 sudo 的系统"
  ["Could not relax pam_faillock in /etc/pam.d/system-auth (a tty-less sudo could still lock the account until reboot)."]="无法放宽 /etc/pam.d/system-auth 里的 pam_faillock (没有终端的 sudo 仍可能把账户锁到重启为止)。"
  ["could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"]="无法预先创建 /var/lib/synapse-src — 更新工具首次运行时会要求输入密码"
  ["  Desktop: KDE Plasma (SDDM login screen)"]="  桌面: KDE Plasma (SDDM 登录界面)"
  ["  GDM: SynapseOS logo on the login screen"]="  GDM: 登录界面上的 SynapseOS 标志"
  ["  Desktop: GNOME (GDM login screen)"]="  桌面: GNOME (GDM 登录界面)"
  ["  Desktop: TTY only"]="  桌面: 仅 TTY"
  ["  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"]="  桌面: SynapseUI (synui 迎宾程序 — 登录界面与锁屏一致)"
  ["  motd: written for this installation"]="  motd: 已为这次安装写好"
  ["  note: syn-rgb.path is not installed; RGB lights stay off"]="  注意: 未安装 syn-rgb.path；RGB 灯保持关闭"
  ["AI model"]="AI 模型"
  ["  AI model skipped — install one later with: syn model download"]="  已跳过 AI 模型 — 以后用这个安装: syn model download"
  ["AI model installed"]="AI 模型已安装"
  ["  the install, and everything else on the disk is already done."]="  安装中最长的一步，硬盘上其他的事都已经做完了。"
  ["syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."]="目标系统里没有 syn-model，所以没有下载模型。
  它属于核心组件；如果被取消勾选，AI 就一直不会动。"
  ["Configuring Nix"]="正在配置 Nix"
  ["Nix configured — /etc/synapseos/nix"]="Nix 已配置 — /etc/synapseos/nix"
  ["  That is the download — a few hundred MB before any packages
  you add to home.nix. 'syn nix edit' opens it."]="  那就是要下载的部分 — 在你往 home.nix 里加任何软件包之前，
  就有几百 MB。用 'syn nix edit' 打开它。"
  ["nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."]="nix 装上了，但目标系统里没有 'syn' 软件包，所以配置工具没有准备好。
  Nix 本身可用; /etc/synapseos/nix 这一层需要 'syn'。"
  ["nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."]="nix 没装上 — 声明式这一层用不了。
  以后用 'sudo pacman -S nix && sudo syn nix init' 安装。"
  ["  Generating initramfs..."]="  正在生成 initramfs..."
  ["mkinitcpio failed — the installed system would not boot"]="mkinitcpio 失败 — 装好的系统将无法启动"
  ["initramfs missing after mkinitcpio — the installed system would not boot"]="mkinitcpio 之后找不到 initramfs — 装好的系统将无法启动"
  ["System configured"]="系统已配置"
  ["Installing Bootloader"]="正在安装引导程序"
  ["grub-install (UEFI) failed"]="grub-install (UEFI) 失败"
  ["grub-install (BIOS) failed"]="grub-install (BIOS) 失败"
  ["  Generating GRUB config..."]="  正在生成 GRUB 配置..."
  ["grub-mkconfig failed"]="grub-mkconfig 失败"
  ["grub.cfg missing after install"]="安装后找不到 grub.cfg"
  ["grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"]="grub.cfg 里带有 GRUB 密码 — 文件只保留给 root 读取，所以设置程序无法报告启动项"
  ["  Installing systemd-boot..."]="  正在安装 systemd-boot..."
  ["bootctl install failed"]="bootctl install 失败"
  ["  Registering systemd-boot with the firmware..."]="  正在向固件登记 systemd-boot..."
  ["efibootmgr entry not created — the removable-media path still applies"]="没有创建 efibootmgr 项 — 仍然走可移动介质的路径"
  ["could not read the root filesystem UUID"]="无法读取根文件系统的 UUID"
  ["vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"]="ESP 上没有 vmlinuz-linux — systemd-boot 找不到可启动的东西"
  ["initramfs is not on the ESP — systemd-boot would find nothing to boot"]="ESP 上没有 initramfs — systemd-boot 找不到可启动的东西"
  ["systemd-boot did not install its EFI binary"]="systemd-boot 没有安装它的 EFI 程序"
  ["  Installing limine..."]="  正在安装 limine..."
  ["could not copy limine's EFI binary to the ESP"]="无法把 limine 的 EFI 程序复制到 ESP"
  ["limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"]="未安装 limine-mkinitcpio-hook — 以后安装的内核将不会有启动项"
  ["vmlinuz-linux is not on the ESP — limine would find nothing to boot"]="ESP 上没有 vmlinuz-linux — limine 找不到可启动的东西"
  ["limine's EFI binary is not on the ESP"]="ESP 上没有 limine 的 EFI 程序"
  ["limine.conf has no kernel entry"]="limine.conf 里没有任何内核项"
  ["  Verifying the encrypted boot path..."]="  正在核对加密启动路径..."
  ["/boot is not a separate mount — an encrypted root needs a plain /boot"]="/boot 不是单独的挂载点 — 加密的根需要一个未加密的 /boot"
  ["/boot is missing from fstab — it would not be mounted after boot"]="fstab 里没有 /boot — 开机后它不会被挂载"
  ["Encrypted boot path verified"]="加密启动路径已核对"
  ["Configuring snapshots"]="正在配置快照"
  ["snapper's config template is missing — snapshots cannot be configured"]="找不到 snapper 的配置模板 — 无法配置快照"
  ["could not write /etc/snapper/configs/root"]="无法写入 /etc/snapper/configs/root"
  ["snapper does not see the 'root' config — snapshots would never be taken"]="snapper 看不到 'root' 配置 — 永远不会生成快照"
  ["snapper's root config was not tuned — timeline snapshots would fill the disk"]="snapper 的 root 配置没有调整过 — 定时快照会把硬盘塞满"
  ["could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"]="无法启用 grub-btrfsd — 快照不会自动出现在启动菜单里"
  ["Snapshots enabled (snapper + snap-pac, bootable from GRUB)"]="已启用快照 (snapper + snap-pac，可从 GRUB 启动)"
  ["could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"]="无法启用 limine-snapper-sync — 快照不会自动出现在启动菜单里"
  ["could not take the post-install snapshot"]="无法创建安装后的快照"
  ["could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"]="无法启用首次开机的快照同步 — 菜单会在第一次升级之后才补上"
  ["Snapshots enabled (snapper + snap-pac, bootable from limine)"]="已启用快照 (snapper + snap-pac，可从 limine 启动)"
  ["Bootloader installed"]="引导程序已安装"
  ["  The root account is locked (no root login / su).
  Note: 3 wrong password attempts lock the account for 10 minutes."]="  root 账户已锁定 (不能用 root 登录，也不能 su)。
  注意: 密码输错 3 次，账户会锁定 10 分钟。"
  ["You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."]="每次开机、在登录界面之前都会要求输入加密密码短语。
  它没有任何找回的办法。"
  ["    syn-crypt status                    is this disk encrypted, and how
    sudo syn-crypt change-key           replace the passphrase
    sudo syn-crypt add-key              add a second one
    sudo syn-crypt backup-header FILE   save the LUKS header"]="    syn-crypt status                    这块硬盘是否加密，以及用的什么方式
    sudo syn-crypt change-key           更换密码短语
    sudo syn-crypt add-key              再加一个
    sudo syn-crypt backup-header 文件   保存 LUKS 头"
  ["  means the data is unrecoverable even with the right passphrase."]="  就意味着即使密码短语正确，数据也再也取不回来。"
  ["Remove installation media and press ENTER to reboot..."]="请取出安装介质，然后按回车重启..."
  ["Install SynapseOS     — right here, in this terminal"]="安装 SynapseOS   — 就在这个终端里"
  ["Install graphically   — starts the desktop first"]="图形化安装       — 先启动桌面"
  ["Try the live desktop  — look around; install later"]="试用实时桌面     — 先看看；以后再装"
  ["Target:"]="目标:"
  ["ALONGSIDE"]="并排"
  ["ERASE"]="清除"
  ["ADVANCED"]="高级"
  ["Encrypt this installation?"]="要加密这次安装吗?"
  ["There is no recovery."]="没有任何找回的办法。"
  ["Root filesystem"]="根文件系统"
  ["ext4   — the default. Boring, proven, repairable by anything."]="ext4   — 默认。朴素、久经考验，什么工具都能修。"
  ["btrfs  — snapshots + zstd compression. Roll back a bad update
                    from the boot menu. Uses more RAM and more CPU."]="btrfs  — 快照 + zstd 压缩。可以从启动菜单回退一次糟糕的更新。
                    占用更多内存和 CPU。"
  ["xfs    — fast on large files. No snapshots, and it cannot be
                    SHRUNK once created."]="xfs    — 处理大文件很快。没有快照，而且创建之后无法缩小。"
  ["f2fs   — built for flash. Good on SD cards and cheap SSDs;
                    unusual enough that fewer rescue tools know it."]="f2fs   — 为闪存而生。适合 SD 卡和便宜的固态硬盘;
                    少见到几乎没有救援工具认识它。"
  ["Bootloader"]="引导程序"
  ["GRUB          — the default. Detects other operating systems,
                          and the only one here that can boot a btrfs
                          snapshot."]="GRUB          — 默认。能识别其他操作系统，也是这里唯一能启动
                          btrfs 快照的一个。"
  ["systemd-boot  — minimal. No OS detection, no snapshot menu."]="systemd-boot  — 极简。不识别其他系统，也没有快照菜单。"
  ["limine        — modern and fast, and it CAN boot snapshots."]="limine        — 又新又快，而且它能启动快照。"
  ["Automatic snapshots?"]="要自动创建快照吗?"
  ["Review the plan — nothing has been written yet:"]="请核对这份方案 — 目前还什么都没有写入:"
  ["nothing else is touched"]="其他一概不动"
  ["not"]="不会"
  ["Partition"]="请分区"
  ["now."]="现在。"
  ["Partitions now on"]="当前分区所在:"
  ["These partitions will be FORMATTED"]="这些分区将被格式化"
  ["Full      — Standard + Steam + Nix + more software"]="完整      — 标准 + Steam + Nix + 更多软件"
  ["Standard  — the SynapseOS suite, Firefox, AI model,"]="标准      — SynapseOS 套件、Firefox、AI 模型、"
  ["Minimal   — core daemons only: none of the above"]="最小      — 只有核心守护进程: 以上都不装"
  ["Custom    — tick every package yourself, ours and"]="自定义    — 每个软件包自己勾选，我们的和"
  ["Which AI model should this machine run?"]="这台机器该运行哪个 AI 模型?"
  ["Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"]="Mistral 7B Instruct   约 4.1 GB   推荐 — SynapseOS 就是照着它调的"
  ["Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"]="Phi-3 Mini 4K         约 2.2 GB   只有一半大，而且明显更弱"
  ["Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"]="Qwen2 0.5B            约 0.4 GB   哪儿都装得下，回答也就那样"
  ["None                            skip it — nothing else changes"]="不装                              跳过 — 其他什么都不变"
  ["Installing:"]="将安装:"
  ["SynapseUI  — AI-native Wayland compositor  (default)"]="SynapseUI  — 为 AI 而生的 Wayland 合成器  (默认)"
  ["SynapseUI  — NOT AVAILABLE: synui was not selected"]="SynapseUI  — 不可用: 没有选择 synui"
  ["KDE Plasma — Full-featured Wayland desktop"]="KDE Plasma — 功能齐全的 Wayland 桌面"
  ["GNOME      — Clean, modern Wayland desktop"]="GNOME      — 简洁现代的 Wayland 桌面"
  ["TTY only   — No GUI (headless/server)"]="仅 TTY     — 没有图形界面 (无头/服务器)"
  ["Disk:"]="硬盘:"
  ["Boot:"]="引导:"
  ["Encrypted:"]="已加密:"
  ["Desktop:"]="桌面:"
  ["User:"]="用户:"
  ["Hostname:"]="主机名:"
  ["Back up the header to another machine."]="请把这个头备份到另一台机器上。"
  ["%s is mounted — unmount it first\\n"]="%s 已挂载 — 请先卸载它
"
  ["%s is %s MiB — %s needs at least %s MiB\\n"]="%s 是 %s MiB — %s 至少需要 %s MiB
"
  ["  Generating %s (a few seconds)...\\n"]="  正在生成 %s (几秒钟)...
"
  ["Language: %s  (%s, keyboard %s)\\n"]="语言: %s  (%s，键盘 %s)
"
  ["  This disk already holds %s partition(s), an EFI System\\n  Partition (%s), and %s GiB of free space.\\n"]="  这块硬盘上已经有 %s 个分区、一个 EFI 系统分区 (%s)，
  以及 %s GiB 的空闲空间。
"
  ["    1) Install %s — use the free space, keep everything else\\n"]="    1) %s 安装 — 使用空闲空间，其他一概保留
"
  ["    2) %s the whole disk — delete every partition and all data\\n"]="    2) %s 整块硬盘 — 删除每一个分区和所有数据
"
  ["    3) %s — partition this disk yourself, then pick the partitions\\n"]="    3) %s — 自己给这块硬盘分区，然后挑选分区
"
  ["    1) %s the whole disk — delete every partition and all data  (default)\\n"]="    1) %s 整块硬盘 — 删除每一个分区和所有数据  (默认)
"
  ["    2) %s — partition this disk yourself, then pick the partitions\\n"]="    2) %s — 自己给这块硬盘分区，然后挑选分区
"
  ["  %s If you forget the passphrase the data is\\n  gone — no password reset, no support call, nothing.\\n"]="  %s 一旦忘记密码短语，数据就没了 —
  不能重置，找支持也没用，什么办法都没有。
"
  ["  snapper takes a snapshot before and after every pacman\\n  transaction, and %s grows a menu to boot any of them. A\\n  bad upgrade becomes a reboot instead of a rescue USB.\\n"]="  snapper 会在每次 pacman 事务前后各创建一个快照，%s 会多出一个菜单，
  可以从其中任何一个启动。糟糕的升级于是变成重启一次，
  而不是找救援 U 盘。
"
  ["    Disk          : %s\\n"]="    硬盘          : %s
"
  ["    Firmware      : %s\\n"]="    固件          : %s
"
  ["    Filesystem    : %s\\n"]="    文件系统      : %s
"
  ["    Bootloader    : %s\\n"]="    引导程序      : %s
"
  ["    Separate /boot: %s\\n"]="    单独的 /boot: %s
"
  ["    Encryption    : %s\\n"]="    加密          : %s
"
  ["    Snapshots     : %s\\n"]="    快照          : %s
"
  ["  Encrypting %s (LUKS2)...\\n"]="  正在加密 %s (LUKS2)...
"
  ["  Formatting root partition (%s)...\\n"]="  正在格式化根分区 (%s)...
"
  ["    • KEEP   all %s existing partition(s), including Windows\\n"]="    • 保留 全部 %s 个现有分区，包括 Windows
"
  ["    • REUSE  %s as the EFI partition (mounted, %s formatted)\\n"]="    • 复用 %s 作为 EFI 分区 (挂载，%s 格式化)
"
  ["    • CREATE a new ext4 root of ~%s GiB in the free space\\n"]="    • 新建 在空闲空间里创建约 %s GiB 的新 ext4 根分区
"
  ["  Creating root partition in free space (%sMiB–%sMiB)...\\n"]="  正在空闲空间里创建根分区 (%s MiB–%s MiB)...
"
  ["  Formatting new root (%s, ext4)...\\n"]="  正在格式化新的根分区 (%s, ext4)...
"
  ["  %s %s %s The installer will re-read the table when you exit.\\n"]="  %s %s %s 你退出后安装程序会重新读取分区表。
"
  ["    • a root partition, at least %s GiB\\n"]="    • 一个根分区，至少 %s GiB
"
  ["    • a separate /boot of ~1 GiB — %s with this layout cannot read the root\\n"]="    • 一个约 1 GiB 的单独 /boot — 这种布局下 %s 读不了根分区
"
  ["  Starting %s on %s — write your changes before quitting.\\n"]="  正在 %s 上启动 %s — 退出前请写入你的更改。
"
  ["    %s is already swap — another system may resume from it.\\n"]="    %s 已经是交换分区 — 另一个系统可能要从它恢复。
"
  ["  Everything else on %s is left untouched.\\n"]="  %s 上的其他一切都保持不动。
"
  ["  Making swap on %s...\\n"]="  正在 %s 上创建交换空间...
"
  ["  Formatting EFI partition (%s MiB)...\\n"]="  正在格式化 EFI 分区 (%s MiB)...
"
  ["  NVIDIA GPU detected — installing %s (builds the module, takes a while)...\\n"]="  检测到 NVIDIA GPU — 正在安装 %s (要编译模块，会花一些时间)...
"
  ["  Installing video stack: %s %s...\\n"]="  正在安装显示相关组件: %s %s...
"
  ["  [cachyos] enabled (%s packages available)\\n"]="  已启用 [cachyos] (可用软件包 %s 个)
"
  ["  Language: %s  (chosen at boot)\\n"]="  语言: %s  (开机时选择)
"
  ["  Locale:   %s   Keyboard: %s (console) / %s (desktop)\\n"]="  语言环境: %s   键盘: %s (控制台) / %s (桌面)
"
  ["  Installing fonts (%s)...\\n"]="  正在安装字体 (%s)...
"
  ["  Downloading the AI model (%s) — this is the long part of\\n"]="  正在下载 AI 模型 (%s) — 这是整个过程里最长的
"
  ["  Nothing is built yet. As %s, after the first boot:\\n"]="  目前还什么都没构建。以 %s 的身份，第一次开机之后:
"
  ["  Adding the %s hook to mkinitcpio...\\n"]="  正在把 %s 钩子加入 mkinitcpio...
"
  ["  Installing GRUB (%s)...\\n"]="  正在安装 GRUB (%s)...
"
  ["yes — LUKS2 on %s"]="是 — %s 上的 LUKS2"
  ["  Admin: use %s with your user password.\\n"]="  管理员操作: 用 %s 加上你的用户密码。
"
  ["  Manage it later with %s:\\n"]="  以后用 %s 管理它:
"
  ["  %s A damaged LUKS header\\n"]="  %s LUKS 头一旦损坏
"
  ["%s is on the live/boot device — that is the installer's own media\\n"]="%s 位于实时/启动设备上 — 那是安装程序自己的介质
"
  ["    %s is already FAT — it may hold another OS's bootloader.\\n"]="    %s 已经是 FAT — 上面可能有另一个系统的引导程序。
"
  ["  Creating user '%s'...\\n"]="  正在创建用户 '%s'...
"
  ["  User '%s' created (uid=%s)\\n"]="  已创建用户 '%s' (uid=%s)
"
  ["  Log in as '%s' after reboot.\\n"]="  重启后请以 '%s' 登录。
"
  ["  Type '%s' to get started.\\n"]="  输入 '%s' 开始上手。
"
)
