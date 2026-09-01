# 日本語 (ja) — syn-install's own words.
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
  ["render.nix is missing — the 'syn' package is not installed here."]="render.nix がありません — このマシンには 'syn' パッケージが入っていません。"
  ["locale-gen failed. The live session stays in English; the install is
  unaffected, because it generates the locale inside the target."]="locale-gen に失敗しました。ライブセッションは英語のままですが、インストール
  自体には影響しません。ロケールはインストール先の中で生成されます。"
  ["  The keyboard, the clock, the fonts and the shell all follow this.
  You can change any of it later."]="  キーボード、時計、フォント、シェルはすべてこの選択に従います。
  どれも後から変更できます。"
  ["Toggle [numbers, 'all', 'none', Enter = accept]:"]="切り替え [番号, 'all', 'none', Enter で確定]:"
  ["--config needs a file"]="--config にはファイルが必要です"
  ["syn-install must be run as root"]="syn-install は root で実行してください"
  ["  SynapseOS is running from the live image."]="  SynapseOS はライブイメージから動いています。"
  ["Starting the desktop — the installer opens with it."]="デスクトップを起動します — インストーラーも一緒に開きます。"
  ["  This installer will:
    1. Partition a disk
    2. Install SynapseOS base system
    3. Install SynapseOS packages
    4. Create user account
    5. Choose desktop environment
    6. Configure system & bootloader"]="  このインストーラーは次のことを行います:
    1. ディスクをパーティション分割する
    2. SynapseOS の基本システムを入れる
    3. SynapseOS のパッケージを入れる
    4. ユーザーアカウントを作る
    5. デスクトップ環境を選ぶ
    6. システムとブートローダーを設定する"
  ["ALL DATA ON THE TARGET DISK WILL BE ERASED"]="対象ディスクのデータはすべて消去されます"
  ["Press ENTER to continue or Ctrl+C to abort..."]="続けるには ENTER、中止するには Ctrl+C を押してください..."
  ["Checking network"]="ネットワークを確認しています"
  ["Network is up"]="ネットワークは使えます"
  ["  No network detected. Starting NetworkManager..."]="  ネットワークが見つかりません。NetworkManager を起動します..."
  ["  No connection — but this machine has Wi-Fi."]="  接続がありません — ただしこのマシンには Wi-Fi があります。"
  ["Open the Wi-Fi picker (nmtui)? [Y/n]:"]="Wi-Fi の選択画面 (nmtui) を開きますか? [Y/n]:"
  ["No network connection, and no Wi-Fi device to configure.
  SynapseOS downloads the base system during install, so connect a cable
  and re-run."]="ネットワーク接続がなく、設定できる Wi-Fi デバイスもありません。
  SynapseOS はインストール中に基本システムをダウンロードするので、ケーブルを
  つないでからやり直してください。"
  ["Network connected"]="ネットワークに接続しました"
  ["Step 1 — Select Target Disk"]="ステップ 1 — インストール先のディスクを選ぶ"
  ["  Available disks:"]="  使えるディスク:"
  ["Target disk (e.g. sda, vda, nvme0n1):"]="インストール先のディスク (例: sda, vda, nvme0n1):"
  ["Target disk is in use. Unmount its partitions and re-run."]="インストール先のディスクは使用中です。パーティションをアンマウントしてからやり直してください。"
  ["Boot mode: UEFI"]="起動モード: UEFI"
  ["Boot mode: BIOS/Legacy"]="起動モード: BIOS/レガシー"
  ["  Encrypts the root filesystem with LUKS2. You will be asked for the
  passphrase at every boot, before the system starts."]="  ルートファイルシステムを LUKS2 で暗号化します。起動のたびに、システムが
  立ち上がる前にパスフレーズを聞かれます。"
  ["Encrypt the disk? [y/N]:"]="ディスクを暗号化しますか? [y/N]:"
  ["                          With encryption it is the BETTER choice: the
                          kernel lives on the EFI partition and only the
                          initramfs unlocks, so /boot needs no separate
                          unencrypted partition."]="                          暗号化するならこちらの方が良い選択です。カーネル
                          は EFI パーティションに置かれ、解錠するのは
                          initramfs だけなので、/boot に暗号化されていない
                          別パーティションを用意する必要がありません。"
  ["                          It copies each snapshot's kernel onto the EFI
                          partition, so that partition is made much
                          larger when snapshots are enabled."]="                          各スナップショットのカーネルを EFI パーティション
                          にコピーするため、スナップショットを有効にすると
                          そのパーティションはかなり大きく作られます。"
  ["  Snapshots are cheap but not free: they hold the old copy of
  anything that changes, so a disk near full stays near full."]="  スナップショットは安価ですが無料ではありません。変わったものの古い写しを
  保持するので、ほぼ満杯のディスクはほぼ満杯のままです。"
  ["Enable snapshots? [Y/n]:"]="スナップショットを有効にしますか? [Y/n]:"
  ["mkfs.ext4 is missing from this installer image — /boot cannot be created"]="このインストーライメージには mkfs.ext4 がありません — /boot を作成できません"
  ["btrfs is missing from this installer image — subvolumes cannot be created"]="このインストーライメージには btrfs がありません — サブボリュームを作成できません"
  ["Are these correct? [Y/n]:"]="これで正しいですか? [Y/n]:"
  ["Starting the questions over — the disk has not been touched."]="質問をやり直します — ディスクには手を付けていません。"
  ["cryptsetup is not available on this installer image"]="このインストーライメージでは cryptsetup が使えません"
  ["Encryption passphrase:"]="暗号化のパスフレーズ:"
  ["Repeat passphrase:"]="パスフレーズをもう一度:"
  ["Empty passphrase — that would leave the disk unprotected."]="パスフレーズが空です — それではディスクが無防備になります。"
  ["Passphrases did not match — try again."]="パスフレーズが一致しません — もう一度どうぞ。"
  ["Passphrase is under 8 characters. A short one is worth little
  against an attacker who has the disk in hand."]="パスフレーズが 8 文字未満です。短いものは、ディスクを手にしている
  相手にはほとんど役に立ちません。"
  ["Use it anyway? [y/N]:"]="それでも使いますか? [y/N]:"
  ["Encryption enabled — root will be LUKS2"]="暗号化を有効にしました — ルートは LUKS2 になります"
  ["cryptsetup open failed — the passphrase did not take"]="cryptsetup open に失敗しました — パスフレーズが通りませんでした"
  ["Failed to mount root"]="ルートのマウントに失敗しました"
  ["  Creating btrfs subvolumes..."]="  btrfs のサブボリュームを作成しています..."
  ["btrfs: could not create @"]="btrfs: @ を作成できませんでした"
  ["btrfs: could not create @home"]="btrfs: @home を作成できませんでした"
  ["btrfs: could not create @snapshots"]="btrfs: @snapshots を作成できませんでした"
  ["btrfs: could not create @var_log"]="btrfs: @var_log を作成できませんでした"
  ["btrfs: could not create @pkg"]="btrfs: @pkg を作成できませんでした"
  ["could not remount the btrfs root onto @"]="btrfs のルートを @ に再マウントできませんでした"
  ["Failed to mount @"]="@ のマウントに失敗しました"
  ["Failed to mount @home"]="@home のマウントに失敗しました"
  ["Failed to mount @snapshots"]="@snapshots のマウントに失敗しました"
  ["Failed to mount @var_log"]="@var_log のマウントに失敗しました"
  ["Failed to mount @pkg"]="@pkg のマウントに失敗しました"
  ["This adds one partition in the free space. Back up anything irreplaceable first."]="空き領域にパーティションを 1 つ追加します。かけがえのないものは先にバックアップしてください。"
  ["Type 'yes' to install alongside:"]="横に並べてインストールするには 'yes' と入力してください:"
  ["Aborted"]="中止しました"
  ["Failed to create the root partition"]="ルートパーティションの作成に失敗しました"
  ["Could not identify the new partition after creating it"]="作成した新しいパーティションを識別できませんでした"
  ["Failed to format root partition"]="ルートパーティションのフォーマットに失敗しました"
  ["Failed to mount the existing ESP"]="既存の ESP のマウントに失敗しました"
  ["no partition editor on this image (cfdisk, fdisk and parted are all missing)"]="このイメージにはパーティションエディタがありません (cfdisk も fdisk も parted も入っていません)"
  ["  What this install needs:"]="  このインストールに必要なもの:"
  ["    • an EFI System Partition (type EF00 / 'esp' flag) — an existing one can be reused"]="    • EFI システムパーティション (種別 EF00 / 'esp' フラグ) — 既存のものを再利用できます"
  ["  Skipping the partition editor (--config)."]="  パーティションエディタを飛ばします (--config)。"
  ["Format it? Everything on it is lost [y/N]:"]="フォーマットしますか? 中身はすべて失われます [y/N]:"
  ["Separate /boot partition:"]="独立した /boot パーティション:"
  ["Swap partition (blank for none):"]="スワップパーティション (なしなら空欄):"
  ["Re-make it? Its UUID changes, breaking that system's fstab [y/N]:"]="作り直しますか? UUID が変わり、そのシステムの fstab が壊れます [y/N]:"
  ["Type 'yes' to format these:"]="これらをフォーマットするには 'yes' と入力してください:"
  ["  Formatting EFI partition..."]="  EFI パーティションをフォーマットしています..."
  ["  Formatting /boot partition..."]="  /boot パーティションをフォーマットしています..."
  ["Failed to mount /boot"]="/boot のマウントに失敗しました"
  ["Type 'yes' to confirm:"]="確認のため 'yes' と入力してください:"
  ["  Creating GPT partition table..."]="  GPT パーティションテーブルを作成しています..."
  ["Failed to format EFI partition"]="EFI パーティションのフォーマットに失敗しました"
  ["Failed to format boot partition"]="ブートパーティションのフォーマットに失敗しました"
  ["  Creating MBR partition table..."]="  MBR パーティションテーブルを作成しています..."
  ["Disk partitioned and mounted at /mnt"]="ディスクを分割し、/mnt にマウントしました"
  ["Step 3 — Installing Base System"]="ステップ 3 — 基本システムのインストール"
  ["  Initializing pacman keyring..."]="  pacman のキーリングを準備しています..."
  ["  Running pacstrap (this may take several minutes)..."]="  pacstrap を実行中 (数分かかることがあります)..."
  ["pacstrap failed — check network connection"]="pacstrap に失敗しました — ネットワーク接続を確認してください"
  ["grub-install not found in chroot — attempting recovery..."]="chroot 内に grub-install が見つかりません — 復旧を試みます..."
  ["Could not install grub into target — check network"]="インストール先に grub を入れられませんでした — ネットワークを確認してください"
  ["Base system installed"]="基本システムをインストールしました"
  ["Step 4 — Choose What to Install"]="ステップ 4 — 何を入れるか選ぶ"
  ["  What should be installed alongside the SynapseOS core?"]="  SynapseOS の中核と一緒に何を入れますか?"
  ["                   Bluetooth, printing, Wine, phone   (default)"]="                   Bluetooth、印刷、Wine、スマートフォン連携   (既定)"
  ["                   the ordinary software people install anyway"]="                   どのみち入れることになる普通のソフト"
  ["  Every preset except Minimal then asks WHICH AI model to download,
  and skipping it is one of the answers."]="  最小構成以外はこのあと、どの AI モデルをダウンロードするか尋ねます。
  ダウンロードしないことも答えのひとつです。"
  ["Full install selected"]="フル構成を選びました"
  ["Minimal install selected"]="最小構成を選びました"
  ["  Two kinds of question. First the packages, as pages of
  checkboxes; then the handful of options that are a whole
  subsystem rather than a package."]="  質問は 2 種類あります。まずパッケージをチェックボックスの
  ページで、そのあとパッケージではなく仕組みそのものにあたる
  いくつかの選択肢を。"
  ["  And the software people install on the first evening anyway.
  All of it is in the Arch repositories; none of it is ours."]="  そして、どのみち初日の夜に入れることになるソフトです。
  すべて Arch のリポジトリにあり、どれも私たちのものではありません。"
  ["  The rest is y/n. The default (shown in caps) is Standard."]="  残りは y/n です。既定 (大文字) は標準構成です。"
  ["syn-update deselected: this machine will have no way to receive
  another SynapseOS package. Fixing that later means installing it by hand
  from the ISO, or reinstalling."]="syn-update を外しました: このマシンには次の SynapseOS パッケージを
  受け取る手段がなくなります。あとで直すには ISO から手作業で入れるか、
  入れ直すことになります。"
  ["Neither the desktop nor the AI daemon was kept. That is an Arch
  system with some SynapseOS tools on it, which is a supported answer —
  but nothing in the documentation will describe the machine you get."]="デスクトップも AI デーモンも残していません。それは SynapseOS の道具が
  いくつか載った Arch システムで、答えとしては認められていますが —
  そうしてできあがるマシンについては、どの文書にも説明がありません。"
  ["Custom install configured"]="カスタム構成を設定しました"
  ["Standard install selected"]="標準構成を選びました"
  ["  synapd loads one model and everything AI in SynapseOS talks to it:
  synsh, the desktop's AI panel, Chibi, Vibe. It is downloaded now,
  over this connection, onto the disk you are installing to."]="  synapd はモデルを 1 つ読み込み、SynapseOS の AI はすべてそこに話しかけます:
  synsh、デスクトップの AI パネル、Chibi、Vibe。この接続を使って今ダウンロード
  し、インストール先のディスクに置きます。"
  ["  A smaller model is not just faster and lighter: it follows
  instructions worse. synsh mistakes what you asked for, Vibe's code
  needs more fixing, Chibi loses the thread. Take the default unless
  the disk or the RAM says otherwise — 7B wants ~6 GB of RAM free."]="  小さいモデルは速くて軽いだけではなく、指示に従う力も落ちます。synsh は
  頼んだ内容を取り違え、Vibe のコードは手直しが増え、Chibi は話を見失います。
  ディスクやメモリの都合がなければ既定のままにしてください —
  7B は空きメモリ 6 GB ほどを求めます。"
  ["  Whatever you pick, it can be changed later: 'syn model download',
  or Super+C ▸ System ▸ AI model on the desktop."]="  どれを選んでも後から変えられます: 'syn model download'、
  あるいはデスクトップの Super+C ▸ システム ▸ AI モデルで。"
  ["Install this selection? [Y/n]:"]="この内容でインストールしますか? [Y/n]:"
  ["Choosing again — nothing has been installed yet."]="選び直します — まだ何もインストールしていません。"
  ["Step 4b — Installing SynapseOS"]="ステップ 4b — SynapseOS のインストール"
  ["Could not enable ILoveCandy in /etc/pacman.conf (cosmetic only)."]="/etc/pacman.conf の ILoveCandy を有効にできませんでした (見た目だけの設定です)。"
  ["  Enabling [multilib] (32-bit repo, needed by Steam)..."]="  [multilib] を有効にしています (Steam が必要とする 32 ビットのリポジトリ)..."
  ["Could not sync the multilib database — Steam may fail to install"]="multilib のデータベースを同期できませんでした — Steam の導入に失敗するかもしれません"
  ["Could not enable [multilib]; Steam will be skipped."]="[multilib] を有効にできませんでした。Steam は飛ばします。"
  ["Some SynapseOS packages failed to install — verifying below"]="一部の SynapseOS パッケージが入りませんでした — 下で確認します"
  ["No SynapseOS packages were selected. This will be an Arch system."]="SynapseOS のパッケージが 1 つも選ばれていません。これは Arch システムになります。"
  ["SynapseOS packages installed"]="SynapseOS のパッケージを入れました"
  ["Component selection recorded in /etc/synapseos/components.conf"]="選んだ構成を /etc/synapseos/components.conf に記録しました"
  ["Step 5 — Create User Account"]="ステップ 5 — ユーザーアカウントの作成"
  ["  Create a user account for the installed system."]="  インストールするシステム用のユーザーアカウントを作ります。"
  ["Username [default: syn]:"]="ユーザー名 [既定: syn]:"
  ["Full name (optional):"]="氏名 (任意):"
  ["Password:"]="パスワード:"
  ["Confirm password:"]="パスワードの確認:"
  ["Passwords do not match or are empty — try again"]="パスワードが一致しないか空です — もう一度どうぞ"
  ["Step 6 — Desktop Environment"]="ステップ 6 — デスクトップ環境"
  ["  Choose a desktop environment:"]="  デスクトップ環境を選んでください:"
  ["  Installing KDE Plasma..."]="  KDE Plasma を入れています..."
  ["Some KDE packages failed to install"]="一部の KDE パッケージが入りませんでした"
  ["KDE Plasma installed"]="KDE Plasma を入れました"
  ["  Installing GNOME..."]="  GNOME を入れています..."
  ["Some GNOME packages failed to install"]="一部の GNOME パッケージが入りませんでした"
  ["GNOME installed (session only — SynapseOS apps, not GNOME's)"]="GNOME を入れました (セッションのみ — アプリは SynapseOS のもので、GNOME のものではありません)"
  ["  Installing greetd (login screen) + desktop extras..."]="  greetd (ログイン画面) とデスクトップの追加物を入れています..."
  ["greetd failed to install — boot falls back to getty login"]="greetd が入りませんでした — 起動は getty のログインに戻ります"
  ["SynapseUI selected (included)"]="SynapseUI を選びました (同梱)"
  ["Installing Wine"]="Wine を入れています"
  ["Wine installed"]="Wine を入れました"
  ["wine failed to install — Windows .exe/.msi will not run.
  Install it later with 'sudo pacman -S wine wine-mono'."]="wine が入りませんでした — Windows の .exe/.msi は動きません。
  あとで 'sudo pacman -S wine wine-mono' で入れてください。"
  ["Configuring Video Driver"]="ビデオドライバーの設定"
  ["  Virtual machine — installing mesa (synui uses pixman here)..."]="  仮想マシンです — mesa を入れています (ここでは synui は pixman を使います)..."
  ["mesa failed to install"]="mesa が入りませんでした"
  ["NVIDIA driver install failed — the system would boot on
  nouveau and synui's renderer would never start"]="NVIDIA ドライバーの導入に失敗しました — このままでは nouveau で起動し、
  synui のレンダラーは一度も立ち上がりません"
  ["NVIDIA sleep services enabled (VRAM save/restore)"]="NVIDIA のスリープ用サービスを有効にしました (VRAM の保存と復元)"
  ["Could not enable nvidia-{suspend,resume,hibernate} — suspend
  may black-screen if NVreg_PreserveVideoMemoryAllocations is set later"]="nvidia-{suspend,resume,hibernate} を有効にできませんでした — あとで
  NVreg_PreserveVideoMemoryAllocations を有効にすると、復帰が黒画面になることがあります"
  ["  synapd can run inference on this GPU instead of the CPU.
  This downloads the CUDA runtime (~4.7 GiB installed)."]="  synapd は CPU ではなくこの GPU で推論できます。
  そのために CUDA 環境をダウンロードします (導入後 約 4.7 GiB)。"
  ["Enable GPU inference? [Y/n]:"]="GPU での推論を有効にしますか? [Y/n]:"
  ["Keeping CPU inference. Switch later with:
  sudo pacman -S synapse-llama-cuda"]="CPU での推論のままにします。あとで切り替えるには:
  sudo pacman -S synapse-llama-cuda"
  ["  Installing synapse-llama-cuda (this takes a while)..."]="  synapse-llama-cuda を入れています (少し時間がかかります)..."
  ["This ISO ships no GPU build of llama, so synapd will run on the CPU
  despite the NVIDIA card. (The ISO must be built on a host with the CUDA
  toolkit for synapse-llama-cuda to exist.)"]="この ISO には llama の GPU ビルドが入っていないため、NVIDIA カードがあっても
  synapd は CPU で動きます。(synapse-llama-cuda を作るには、CUDA ツールキットの
  あるホストで ISO をビルドする必要があります。)"
  ["Video driver install failed — synui may fall back to software rendering"]="ビデオドライバーの導入に失敗しました — synui がソフトウェア描画に落ちるかもしれません"
  ["Video drivers installed"]="ビデオドライバーを入れました"
  ["  Enabling GPU inference (synapse-llama-vulkan)..."]="  GPU での推論を有効にしています (synapse-llama-vulkan)..."
  ["This ISO ships no Vulkan build of llama, so synapd will run on the CPU
  despite the AMD/Intel GPU. (Build the ISO on a host with 'shaderc' +
  vulkan-headers for synapse-llama-vulkan to exist.)"]="この ISO には llama の Vulkan ビルドが入っていないため、AMD/Intel の GPU があっても
  synapd は CPU で動きます。(synapse-llama-vulkan を作るには 'shaderc' と
  vulkan-headers のあるホストで ISO をビルドしてください。)"
  ["Installing Steam and the game stack"]="Steam とゲーム関連一式のインストール"
  ["  Installing steam and the 32-bit runtime libraries..."]="  steam と 32 ビットのランタイムライブラリを入れています..."
  ["Steam installed (native multilib package)"]="Steam を入れました (multilib のネイティブパッケージ)"
  ["steam failed to install. The system is otherwise complete —
  install it later with 'sudo pacman -S steam' ([multilib] is already
  enabled in /etc/pacman.conf)."]="steam が入りませんでした。ほかは問題なく仕上がっています —
  あとで 'sudo pacman -S steam' で入れてください ([multilib] は
  /etc/pacman.conf ですでに有効です)。"
  ["  Installing the game stack (overlay, governor, micro-compositor)..."]="  ゲーム関連一式を入れています (オーバーレイ、ガバナー、マイクロコンポジタ)..."
  ["Game stack installed (mangohud, gamemode, gamescope)"]="ゲーム関連一式を入れました (mangohud, gamemode, gamescope)"
  ["The game stack failed to install. Steam still works; the FPS
  overlay, the CPU/GPU governor and 'synui-game-run --gamescope' will
  not. Install later with:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"]="ゲーム関連一式が入りませんでした。Steam は問題なく動きますが、FPS
  オーバーレイ、CPU/GPU ガバナー、'synui-game-run --gamescope' は使えません。
  あとで入れるには:
  sudo pacman -S mangohud lib32-mangohud gamemode lib32-gamemode gamescope"
  ["Installing CachyOS Proton"]="CachyOS Proton のインストール"
  ["  Fetching the CachyOS keyring and mirrorlist..."]="  CachyOS のキーリングとミラーリストを取得しています..."
  ["  Trusting the CachyOS master key..."]="  CachyOS のマスターキーを信頼します..."
  ["Could not fetch the CachyOS master key from keyserver.ubuntu.com.
  The signed keyring cannot be installed without it, so CachyOS Proton is
  skipped. Add it later with:  synpkg cachyos enable-repo"]="keyserver.ubuntu.com から CachyOS のマスターキーを取得できませんでした。
  それがないと署名済みキーリングを入れられないため、CachyOS Proton は
  飛ばします。あとで追加するには:  synpkg cachyos enable-repo"
  ["  Master key pinned as expected — trusting it..."]="  マスターキーは想定どおりです — 信頼します..."
  ["[cachyos] was added but lists no packages — removing it
  again so it cannot block a later upgrade."]="[cachyos] は追加されましたがパッケージが 1 つもありません — あとの
  アップグレードを妨げないよう、元に戻して削除します。"
  ["The CachyOS keyring does not carry the expected master key.
  Refusing to trust it — the repository was NOT added."]="CachyOS のキーリングに想定したマスターキーが入っていません。
  信頼を拒否しました — リポジトリは追加していません。"
  ["  Installing proton-cachyos-slr (~340 MB download)..."]="  proton-cachyos-slr を入れています (ダウンロード 約 340 MB)..."
  ["CachyOS Proton installed — pick it per game in Steam under
  Properties → Compatibility, listed as 'proton-cachyos-… (steam linux runtime)'.
  Steam only scans for it at startup, so restart Steam if it is already running."]="CachyOS Proton を入れました — Steam のゲームごとのプロパティ →
  互換性で 'proton-cachyos-… (steam linux runtime)' として選べます。
  Steam は起動時にしか探さないので、すでに動いていれば再起動してください。"
  ["proton-cachyos-slr failed to install. Steam and Valve's own
  Proton are unaffected. Install later with:
  sudo pacman -S proton-cachyos-slr"]="proton-cachyos-slr が入りませんでした。Steam と Valve 自身の Proton には
  影響ありません。あとで入れるには:
  sudo pacman -S proton-cachyos-slr"
  ["The [cachyos] repository could not be enabled, so CachyOS Proton
  was skipped. Steam still works with Valve's Proton. To add it later:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"]="[cachyos] リポジトリを有効にできなかったため、CachyOS Proton は
  飛ばしました。Steam は Valve の Proton で問題なく動きます。あとで追加するには:
      synpkg cachyos enable-repo
      synpkg install proton-cachyos-slr"
  ["Enabling BlackArch"]="BlackArch の有効化"
  ["  Fetching the BlackArch bootstrap..."]="  BlackArch のブートストラップを取得しています..."
  ["  Master key pinned as expected — running bootstrap..."]="  マスターキーは想定どおりです — ブートストラップを実行します..."
  ["blackarch-keyring did not install — key rotations
  will not reach this machine. Fix with 'sudo pacman -S blackarch-keyring'."]="blackarch-keyring が入りませんでした — 鍵の更新が
  このマシンに届きません。'sudo pacman -S blackarch-keyring' で直せます。"
  ["The downloaded strap.sh does not pin BlackArch's expected master
  key. Refusing to run it — the repository was NOT added."]="ダウンロードした strap.sh が BlackArch の想定したマスターキーを固定して
  いません。実行を拒否しました — リポジトリは追加していません。"
  ["BlackArch was not enabled. The system is otherwise complete;
  add it later with 'sudo syn arsenal --enable-repo'."]="BlackArch は有効になりませんでした。ほかは問題なく仕上がっています。
  あとで 'sudo syn arsenal --enable-repo' で追加してください。"
  ["Installing software"]="ソフトウェアのインストール"
  ["That transaction failed — retrying each package on its own so the
  ones that are fine still land, and the one that is not gets named."]="その処理は失敗しました — 問題のないものはそれでも入るように、そして
  問題のあるものが名指しされるように、パッケージごとに入れ直します。"
  ["Software installed"]="ソフトウェアを入れました"
  ["Installing Flatpak apps"]="Flatpak アプリのインストール"
  ["flatpak could not be installed — skipping the Flatpak apps.
  Nothing else is affected."]="flatpak を入れられませんでした — Flatpak アプリは飛ばします。
  ほかには影響ありません。"
  ["Could not add the flathub remote"]="flathub リモートを追加できませんでした"
  ["Flatpak apps installed"]="Flatpak アプリを入れました"
  ["Configuring System"]="システムの設定"
  ["  fstab generated"]="  fstab を生成しました"
  ["Swap recorded in fstab"]="スワップを fstab に記録しました"
  ["zram configured (compressed swap, half of RAM up to 8 GiB)"]="zram を設定しました (圧縮スワップ、メモリの半分、上限 8 GiB)"
  ["zram-generator is not installed in the target — no compressed swap"]="インストール先に zram-generator が入っていません — 圧縮スワップは使えません"
  ["  Hostname: synapse"]="  ホスト名: synapse"
  ["Step 7 — Language & Region"]="ステップ 7 — 言語と地域"
  ["   0) Other — enter a locale by hand"]="   0) その他 — ロケールを手で入力する"
  ["Locale (e.g. sv_SE.UTF-8):"]="ロケール (例: sv_SE.UTF-8):"
  ["Console keymap (e.g. sv-latin1):"]="コンソールのキーマップ (例: sv-latin1):"
  ["Step 8 — Timezone"]="ステップ 8 — タイムゾーン"
  ["   0) Other — enter any tzdata name (e.g. Europe/Lisbon)"]="   0) その他 — tzdata の名前を自由に入力する (例: Europe/Lisbon)"
  ["tzdata name (Region/City):"]="tzdata の名前 (地域/都市):"
  ["  Did you mean:"]="  こちらのことでしょうか:"
  ["  Pick a number from the list, or see: ls /mnt/usr/share/zoneinfo"]="  一覧から番号を選ぶか、ls /mnt/usr/share/zoneinfo をご覧ください"
  ["  os-release: copied from live system"]="  os-release: ライブシステムからコピーしました"
  ["  issue: copied from live system"]="  issue: ライブシステムからコピーしました"
  ["Target filesystem is no longer writable (disk errors? check 'dmesg') — aborting"]="インストール先のファイルシステムに書き込めなくなりました (ディスクの異常? 'dmesg' を確認) — 中止します"
  ["sudoers ruleset is invalid after writing the drop-ins — refusing to ship a system that cannot sudo"]="ドロップインを書いたあとの sudoers 規則が不正です — sudo できないシステムは出荷しません"
  ["Could not relax pam_faillock in /etc/pam.d/system-auth (a tty-less sudo could still lock the account until reboot)."]="/etc/pam.d/system-auth の pam_faillock を緩められませんでした (端末なしの sudo が、再起動までアカウントを閉じてしまう恐れが残ります)。"
  ["could not pre-create /var/lib/synapse-src — the updater will ask for a password on first run"]="/var/lib/synapse-src をあらかじめ作れませんでした — 更新ツールが初回にパスワードを尋ねます"
  ["  Desktop: KDE Plasma (SDDM login screen)"]="  デスクトップ: KDE Plasma (SDDM のログイン画面)"
  ["  GDM: SynapseOS logo on the login screen"]="  GDM: ログイン画面に SynapseOS のロゴ"
  ["  Desktop: GNOME (GDM login screen)"]="  デスクトップ: GNOME (GDM のログイン画面)"
  ["  Desktop: TTY only"]="  デスクトップ: TTY のみ"
  ["  Desktop: SynapseUI (synui greeter — login mirrors the lock screen)"]="  デスクトップ: SynapseUI (synui のグリーター — ログインはロック画面と同じ見た目)"
  ["  motd: written for this installation"]="  motd: このインストール向けに書きました"
  ["  note: syn-rgb.path is not installed; RGB lights stay off"]="  注意: syn-rgb.path が入っていないため、RGB ライトは消えたままです"
  ["AI model"]="AI モデル"
  ["  AI model skipped — install one later with: syn model download"]="  AI モデルを飛ばしました — あとで入れるには: syn model download"
  ["AI model installed"]="AI モデルを入れました"
  ["  the install, and everything else on the disk is already done."]="  インストールのうちの長い部分で、ディスク上のほかはもう終わっています。"
  ["syn-model is not on the target, so no model was downloaded.
  It is part of the core set; if it was deselected, the AI stays inert."]="インストール先に syn-model がないため、モデルはダウンロードしていません。
  中核の一部です。外していた場合、AI は動かないままになります。"
  ["Configuring Nix"]="Nix の設定"
  ["Nix configured — /etc/synapseos/nix"]="Nix を設定しました — /etc/synapseos/nix"
  ["  That is the download — a few hundred MB before any packages
  you add to home.nix. 'syn nix edit' opens it."]="  これがそのダウンロードです — home.nix に足すパッケージより前に、
  数百 MB あります。'syn nix edit' で開けます。"
  ["nix installed, but the 'syn' package is not on the target, so
  the configurator was not set up. Nix itself works; the
  /etc/synapseos/nix layer needs 'syn'."]="nix は入りましたが、インストール先に 'syn' パッケージがないため、
  設定ツールは用意されていません。Nix 自体は動きます。
  /etc/synapseos/nix の層には 'syn' が必要です。"
  ["nix failed to install — the declarative layer is not available.
  Install it later with 'sudo pacman -S nix && sudo syn nix init'."]="nix が入りませんでした — 宣言的な層は使えません。
  あとで 'sudo pacman -S nix && sudo syn nix init' で入れてください。"
  ["  Generating initramfs..."]="  initramfs を生成しています..."
  ["mkinitcpio failed — the installed system would not boot"]="mkinitcpio に失敗しました — このままではインストールしたシステムは起動しません"
  ["initramfs missing after mkinitcpio — the installed system would not boot"]="mkinitcpio のあとに initramfs がありません — このままでは起動しません"
  ["System configured"]="システムを設定しました"
  ["Installing Bootloader"]="ブートローダーのインストール"
  ["grub-install (UEFI) failed"]="grub-install (UEFI) に失敗しました"
  ["grub-install (BIOS) failed"]="grub-install (BIOS) に失敗しました"
  ["  Generating GRUB config..."]="  GRUB の設定を生成しています..."
  ["grub-mkconfig failed"]="grub-mkconfig に失敗しました"
  ["grub.cfg missing after install"]="インストール後に grub.cfg がありません"
  ["grub.cfg carries a GRUB password — left root-only, so the settings app cannot report on boot entries"]="grub.cfg に GRUB のパスワードが入っています — root だけが読める状態のままなので、設定アプリは起動項目について何も報告できません"
  ["  Installing systemd-boot..."]="  systemd-boot を入れています..."
  ["bootctl install failed"]="bootctl install に失敗しました"
  ["  Registering systemd-boot with the firmware..."]="  systemd-boot をファームウェアに登録しています..."
  ["efibootmgr entry not created — the removable-media path still applies"]="efibootmgr の項目を作れませんでした — リムーバブルメディア用の経路のままです"
  ["could not read the root filesystem UUID"]="ルートファイルシステムの UUID を読めませんでした"
  ["vmlinuz-linux is not on the ESP — systemd-boot would find nothing to boot"]="ESP に vmlinuz-linux がありません — systemd-boot は起動するものを見つけられません"
  ["initramfs is not on the ESP — systemd-boot would find nothing to boot"]="ESP に initramfs がありません — systemd-boot は起動するものを見つけられません"
  ["systemd-boot did not install its EFI binary"]="systemd-boot が EFI バイナリを入れませんでした"
  ["  Installing limine..."]="  limine を入れています..."
  ["could not copy limine's EFI binary to the ESP"]="limine の EFI バイナリを ESP にコピーできませんでした"
  ["limine-mkinitcpio-hook not installed — a kernel installed later will NOT get a boot entry"]="limine-mkinitcpio-hook が入っていません — あとで入れたカーネルには起動項目が作られません"
  ["vmlinuz-linux is not on the ESP — limine would find nothing to boot"]="ESP に vmlinuz-linux がありません — limine は起動するものを見つけられません"
  ["limine's EFI binary is not on the ESP"]="ESP に limine の EFI バイナリがありません"
  ["limine.conf has no kernel entry"]="limine.conf にカーネルの項目がありません"
  ["  Verifying the encrypted boot path..."]="  暗号化された起動経路を確認しています..."
  ["/boot is not a separate mount — an encrypted root needs a plain /boot"]="/boot が独立したマウントではありません — 暗号化したルートには暗号化していない /boot が必要です"
  ["/boot is missing from fstab — it would not be mounted after boot"]="fstab に /boot がありません — 起動後にマウントされません"
  ["Encrypted boot path verified"]="暗号化された起動経路を確認しました"
  ["Configuring snapshots"]="スナップショットの設定"
  ["snapper's config template is missing — snapshots cannot be configured"]="snapper の設定テンプレートがありません — スナップショットを設定できません"
  ["could not write /etc/snapper/configs/root"]="/etc/snapper/configs/root を書けませんでした"
  ["snapper does not see the 'root' config — snapshots would never be taken"]="snapper が 'root' の設定を認識していません — スナップショットは一度も作られません"
  ["snapper's root config was not tuned — timeline snapshots would fill the disk"]="snapper の root 設定を調整できませんでした — 定期スナップショットがディスクを埋めてしまいます"
  ["could not enable grub-btrfsd — snapshots will not appear in the boot menu automatically"]="grub-btrfsd を有効にできませんでした — スナップショットが起動メニューに自動では出ません"
  ["Snapshots enabled (snapper + snap-pac, bootable from GRUB)"]="スナップショットを有効にしました (snapper + snap-pac、GRUB から起動できます)"
  ["could not enable limine-snapper-sync — snapshots will not reach the boot menu automatically"]="limine-snapper-sync を有効にできませんでした — スナップショットが起動メニューに自動では届きません"
  ["could not take the post-install snapshot"]="インストール後のスナップショットを作れませんでした"
  ["could not enable the first-boot snapshot sync — the menu fills in after the first upgrade instead"]="初回起動時のスナップショット同期を有効にできませんでした — 代わりに最初のアップグレード後にメニューが埋まります"
  ["Snapshots enabled (snapper + snap-pac, bootable from limine)"]="スナップショットを有効にしました (snapper + snap-pac、limine から起動できます)"
  ["Bootloader installed"]="ブートローダーを入れました"
  ["  The root account is locked (no root login / su).
  Note: 3 wrong password attempts lock the account for 10 minutes."]="  root アカウントは無効にしてあります (root でのログインも su もできません)。
  注意: パスワードを 3 回間違えると、アカウントが 10 分間ロックされます。"
  ["You will be asked for the encryption passphrase at every boot,
  BEFORE the login screen. There is no way to recover it."]="起動のたびに、ログイン画面より前に暗号化のパスフレーズを聞かれます。
  取り戻す方法はありません。"
  ["    syn-crypt status                    is this disk encrypted, and how
    sudo syn-crypt change-key           replace the passphrase
    sudo syn-crypt add-key              add a second one
    sudo syn-crypt backup-header FILE   save the LUKS header"]="    syn-crypt status                    このディスクは暗号化されているか、その方式
    sudo syn-crypt change-key           パスフレーズを差し替える
    sudo syn-crypt add-key              2 つめを追加する
    sudo syn-crypt backup-header ファイル  LUKS ヘッダーを保存する"
  ["  means the data is unrecoverable even with the right passphrase."]="  になると、正しいパスフレーズがあってもデータは取り戻せません。"
  ["Remove installation media and press ENTER to reboot..."]="インストールメディアを取り外し、ENTER で再起動してください..."
  ["Install SynapseOS     — right here, in this terminal"]="SynapseOS を入れる     — この端末で、いますぐ"
  ["Install graphically   — starts the desktop first"]="グラフィカルに入れる   — 先にデスクトップを起動します"
  ["Try the live desktop  — look around; install later"]="ライブデスクトップを試す — 見て回る。インストールは後で"
  ["Target:"]="対象:"
  ["ALONGSIDE"]="横に並べる"
  ["ERASE"]="消去"
  ["ADVANCED"]="詳細設定"
  ["Encrypt this installation?"]="このインストールを暗号化しますか?"
  ["There is no recovery."]="取り戻す方法はありません。"
  ["Root filesystem"]="ルートファイルシステム"
  ["ext4   — the default. Boring, proven, repairable by anything."]="ext4   — 既定。地味で、実績があり、たいていの道具で直せます。"
  ["btrfs  — snapshots + zstd compression. Roll back a bad update
                    from the boot menu. Uses more RAM and more CPU."]="btrfs  — スナップショットと zstd 圧縮。まずい更新を起動メニューから
                    巻き戻せます。メモリと CPU をより多く使います。"
  ["xfs    — fast on large files. No snapshots, and it cannot be
                    SHRUNK once created."]="xfs    — 大きなファイルに強い。スナップショットはなく、いちど作ると
                    縮小できません。"
  ["f2fs   — built for flash. Good on SD cards and cheap SSDs;
                    unusual enough that fewer rescue tools know it."]="f2fs   — フラッシュ向け。SD カードや安価な SSD に向きます。
                    珍しいので、対応している救出ツールは多くありません。"
  ["Bootloader"]="ブートローダー"
  ["GRUB          — the default. Detects other operating systems,
                          and the only one here that can boot a btrfs
                          snapshot."]="GRUB          — 既定。ほかの OS を見つけ、ここで唯一 btrfs の
                          スナップショットを起動できます。"
  ["systemd-boot  — minimal. No OS detection, no snapshot menu."]="systemd-boot  — 最小限。OS の検出もスナップショットのメニューもありません。"
  ["limine        — modern and fast, and it CAN boot snapshots."]="limine        — 新しくて速く、しかもスナップショットを起動できます。"
  ["Automatic snapshots?"]="スナップショットを自動で取りますか?"
  ["Review the plan — nothing has been written yet:"]="計画を確認してください — まだ何も書き込んでいません:"
  ["nothing else is touched"]="ほかには何も触りません"
  ["not"]="しません"
  ["Partition"]="パーティション分割を"
  ["now."]="してください。"
  ["Partitions now on"]="現在のパーティション:"
  ["These partitions will be FORMATTED"]="これらのパーティションはフォーマットされます"
  ["Full      — Standard + Steam + Nix + more software"]="フル      — 標準 + Steam + Nix + さらに多くのソフト"
  ["Standard  — the SynapseOS suite, Firefox, AI model,"]="標準      — SynapseOS 一式、Firefox、AI モデル、"
  ["Minimal   — core daemons only: none of the above"]="最小      — 中核のデーモンのみ: 上のものは何も入りません"
  ["Custom    — tick every package yourself, ours and"]="カスタム  — パッケージを自分で選ぶ。私たちのものも"
  ["Which AI model should this machine run?"]="このマシンではどの AI モデルを動かしますか?"
  ["Mistral 7B Instruct   ~4.1 GB   recommended — what SynapseOS is tuned against"]="Mistral 7B Instruct   約 4.1 GB   推奨 — SynapseOS はこれに合わせて調整されています"
  ["Phi-3 Mini 4K         ~2.2 GB   half the size, and noticeably weaker"]="Phi-3 Mini 4K         約 2.2 GB   半分の大きさで、はっきり弱くなります"
  ["Qwen2 0.5B            ~0.4 GB   fits anywhere, answers like it"]="Qwen2 0.5B            約 0.4 GB   どこにでも入りますが、答えもそれなりです"
  ["None                            skip it — nothing else changes"]="なし                              入れない — ほかは何も変わりません"
  ["Installing:"]="インストールするもの:"
  ["SynapseUI  — AI-native Wayland compositor  (default)"]="SynapseUI  — AI を前提にした Wayland コンポジタ  (既定)"
  ["SynapseUI  — NOT AVAILABLE: synui was not selected"]="SynapseUI  — 使えません: synui が選ばれていません"
  ["KDE Plasma — Full-featured Wayland desktop"]="KDE Plasma — 機能のそろった Wayland デスクトップ"
  ["GNOME      — Clean, modern Wayland desktop"]="GNOME      — すっきりした現代的な Wayland デスクトップ"
  ["TTY only   — No GUI (headless/server)"]="TTY のみ   — 画面なし (ヘッドレス/サーバー)"
  ["Disk:"]="ディスク:"
  ["Boot:"]="起動:"
  ["Encrypted:"]="暗号化:"
  ["Desktop:"]="デスクトップ:"
  ["User:"]="ユーザー:"
  ["Hostname:"]="ホスト名:"
  ["Back up the header to another machine."]="ヘッダーは別のマシンにバックアップしてください。"
  ["%s is mounted — unmount it first\\n"]="%s はマウントされています — 先にアンマウントしてください
"
  ["%s is %s MiB — %s needs at least %s MiB\\n"]="%s は %s MiB です — %s には少なくとも %s MiB 必要です
"
  ["  Generating %s (a few seconds)...\\n"]="  %s を生成しています (数秒)...
"
  ["Language: %s  (%s, keyboard %s)\\n"]="言語: %s  (%s、キーボード %s)
"
  ["  This disk already holds %s partition(s), an EFI System\\n  Partition (%s), and %s GiB of free space.\\n"]="  このディスクにはすでに %s 個のパーティション、EFI システム
  パーティション (%s)、そして %s GiB の空き領域があります。
"
  ["    1) Install %s — use the free space, keep everything else\\n"]="    1) %s に入れる — 空き領域を使い、ほかはそのまま残す
"
  ["    2) %s the whole disk — delete every partition and all data\\n"]="    2) ディスク全体を%s — すべてのパーティションとデータを削除する
"
  ["    3) %s — partition this disk yourself, then pick the partitions\\n"]="    3) %s — このディスクを自分で分割し、そのあとパーティションを選ぶ
"
  ["    1) %s the whole disk — delete every partition and all data  (default)\\n"]="    1) ディスク全体を%s — すべてのパーティションとデータを削除する  (既定)
"
  ["    2) %s — partition this disk yourself, then pick the partitions\\n"]="    2) %s — このディスクを自分で分割し、そのあとパーティションを選ぶ
"
  ["  %s If you forget the passphrase the data is\\n  gone — no password reset, no support call, nothing.\\n"]="  %s パスフレーズを忘れたらデータは失われます —
  再設定も、サポートへの連絡も、何もできません。
"
  ["  snapper takes a snapshot before and after every pacman\\n  transaction, and %s grows a menu to boot any of them. A\\n  bad upgrade becomes a reboot instead of a rescue USB.\\n"]="  snapper は pacman の処理の前後にスナップショットを取り、%s には
  そのどれからでも起動できるメニューができます。まずい更新が、救出用 USB
  ではなく再起動で済むようになります。
"
  ["    Disk          : %s\\n"]="    ディスク      : %s
"
  ["    Firmware      : %s\\n"]="    ファームウェア: %s
"
  ["    Filesystem    : %s\\n"]="    ファイルシステム: %s
"
  ["    Bootloader    : %s\\n"]="    ブートローダー: %s
"
  ["    Separate /boot: %s\\n"]="    独立した /boot: %s
"
  ["    Encryption    : %s\\n"]="    暗号化        : %s
"
  ["    Snapshots     : %s\\n"]="    スナップショット: %s
"
  ["  Encrypting %s (LUKS2)...\\n"]="  %s を暗号化しています (LUKS2)...
"
  ["  Formatting root partition (%s)...\\n"]="  ルートパーティションをフォーマットしています (%s)...
"
  ["    • KEEP   all %s existing partition(s), including Windows\\n"]="    • 残す   既存の %s 個のパーティションすべて (Windows を含む)
"
  ["    • REUSE  %s as the EFI partition (mounted, %s formatted)\\n"]="    • 再利用 %s を EFI パーティションとして (マウントのみ、%s フォーマット)
"
  ["    • CREATE a new ext4 root of ~%s GiB in the free space\\n"]="    • 作成   空き領域に約 %s GiB の新しい ext4 ルートを
"
  ["  Creating root partition in free space (%sMiB–%sMiB)...\\n"]="  空き領域にルートパーティションを作成しています (%s MiB–%s MiB)...
"
  ["  Formatting new root (%s, ext4)...\\n"]="  新しいルートをフォーマットしています (%s, ext4)...
"
  ["  %s %s %s The installer will re-read the table when you exit.\\n"]="  %s %s %s 終了するとインストーラーがテーブルを読み直します。
"
  ["    • a root partition, at least %s GiB\\n"]="    • 少なくとも %s GiB のルートパーティション
"
  ["    • a separate /boot of ~1 GiB — %s with this layout cannot read the root\\n"]="    • 約 1 GiB の独立した /boot — この構成では %s がルートを読めません
"
  ["  Starting %s on %s — write your changes before quitting.\\n"]="  %s を %s で起動します — 終了する前に変更を書き込んでください。
"
  ["    %s is already swap — another system may resume from it.\\n"]="    %s はすでにスワップです — 別のシステムがそこから復帰するかもしれません。
"
  ["  Everything else on %s is left untouched.\\n"]="  %s の上のほかのものはそのまま残ります。
"
  ["  Making swap on %s...\\n"]="  %s にスワップを作成しています...
"
  ["  Formatting EFI partition (%s MiB)...\\n"]="  EFI パーティションをフォーマットしています (%s MiB)...
"
  ["  NVIDIA GPU detected — installing %s (builds the module, takes a while)...\\n"]="  NVIDIA GPU を検出しました — %s を入れています (モジュールをビルドするため時間がかかります)...
"
  ["  Installing video stack: %s %s...\\n"]="  映像まわりを入れています: %s %s...
"
  ["  [cachyos] enabled (%s packages available)\\n"]="  [cachyos] を有効にしました (利用できるパッケージ %s 個)
"
  ["  Language: %s  (chosen at boot)\\n"]="  言語: %s  (起動時に選択)
"
  ["  Locale:   %s   Keyboard: %s (console) / %s (desktop)\\n"]="  ロケール: %s   キーボード: %s (コンソール) / %s (デスクトップ)
"
  ["  Installing fonts (%s)...\\n"]="  フォントを入れています (%s)...
"
  ["  Downloading the AI model (%s) — this is the long part of\\n"]="  AI モデルをダウンロードしています (%s) — ここが
"
  ["  Nothing is built yet. As %s, after the first boot:\\n"]="  まだ何もビルドしていません。%s として、初回起動のあとに:
"
  ["  Adding the %s hook to mkinitcpio...\\n"]="  mkinitcpio に %s フックを追加しています...
"
  ["  Installing GRUB (%s)...\\n"]="  GRUB を入れています (%s)...
"
  ["yes — LUKS2 on %s"]="はい — %s 上の LUKS2"
  ["  Admin: use %s with your user password.\\n"]="  管理者権限: 自分のユーザーパスワードで %s を使ってください。
"
  ["  Manage it later with %s:\\n"]="  あとで管理するには %s:
"
  ["  %s A damaged LUKS header\\n"]="  %s LUKS ヘッダーが壊れると
"
  ["%s is on the live/boot device — that is the installer's own media\\n"]="%s はライブ/起動デバイス上にあります — インストーラー自身のメディアです
"
  ["    %s is already FAT — it may hold another OS's bootloader.\\n"]="    %s はすでに FAT です — 別の OS のブートローダーが入っているかもしれません。
"
  ["  Creating user '%s'...\\n"]="  ユーザー '%s' を作成しています...
"
  ["  User '%s' created (uid=%s)\\n"]="  ユーザー '%s' を作成しました (uid=%s)
"
  ["  Log in as '%s' after reboot.\\n"]="  再起動後は '%s' でログインしてください。
"
  ["  Type '%s' to get started.\\n"]="  まずは '%s' と入力してみてください。
"
)
