# A SynapseOS install profile — every question syn-install can ask.
#
#   syn-install --config this-file.nix          (needs nix on the install medium)
#   syn nix profile this-file.nix > install.conf   then --config install.conf
#
# EVERY KEY IS OPTIONAL. Whatever you leave out is still asked at the machine,
# so a profile that pins only the disk layout and the package set is a perfectly
# good profile. That is also why nothing here is "required": there is no schema
# to satisfy, only questions you have chosen to pre-answer.
#
# Booleans render to yes/no and integers to themselves, so write `true`, not
# `"yes"`. A nested `want = { steam = true; }` flattens to want_steam.
#
# A key that answers no question is REPORTED at the end of the install rather
# than ignored — a misspelling is the one preseed failure that otherwise only
# shows up after the reboot.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
{
  # ── Disk ────────────────────────────────────────────────────────────────
  # Bare name or /dev/ path, either works.
  disk = "vda";

  # alongside — use the free space, keep the other OS (UEFI only)
  # erase     — the whole disk
  # manual    — use partitions that already exist (see part_* below)
  install_mode = "erase";

  # THE DESTRUCTIVE CONFIRMATIONS ARE NOT OPTIONAL IN SPIRIT. Each one is a
  # separate key on purpose: an unattended install that erases a disk should
  # have said so in writing. Omit them and the installer stops and asks, which
  # is the right outcome for a profile that was not meant to be unattended.
  confirm_erase = true;        # "type yes to erase"      (install_mode = erase)
  # confirm_alongside = true;  # (install_mode = alongside)
  # confirm_format   = true;   # (install_mode = manual)

  filesystem = "btrfs";        # ext4 | btrfs | xfs | f2fs
  bootloader = "limine";       # grub | systemd-boot | limine
  snapshots  = true;           # btrfs + limine only; ignored elsewhere
  disk_plan_ok = true;         # the read-back of the four answers above

  encrypt = false;             # LUKS2
  # luks_passphrase = "…";     # only read when encrypt = true
  # short_passphrase_ok = true;  # only asked if it is under 8 characters

  # ── Existing partitions (install_mode = "manual" only) ──────────────────
  # `skip` is what stops the full-screen partition editor from launching at an
  # install nobody is sitting in front of.
  # partition_editor = "skip";
  # part_root  = "/dev/vda2";
  # part_efi   = "/dev/vda1";
  # format_esp = false;        # false keeps another OS's bootloader
  # part_boot  = "/dev/vda3";  # only when the layout needs a separate /boot
  # part_swap  = "";           # empty means none
  # remake_swap = false;       # re-mkswap an existing one? changes its UUID

  # ── What to install ─────────────────────────────────────────────────────
  # full | standard | minimal | custom
  preset = "custom";

  # Which AI model synapd gets, downloaded during the install (the ISO no
  # longer carries one). Asked on every preset except minimal.
  #
  #   mistral-7b   Mistral 7B Instruct Q4_K_M  ~4.1 GB   recommended
  #   phi3         Phi-3 Mini 4K Instruct Q4   ~2.2 GB   weaker answers
  #   tiny         Qwen2 0.5B Instruct Q4_K_M  ~0.4 GB   much weaker answers
  #   none         no model — every AI feature stays inert until
  #                `syn model download` is run on the installed system
  #
  # A smaller model is not just a smaller download: synsh, Chibi, Vibe and the
  # desktop's AI panel all get worse with it.
  ai_model = "mistral-7b";

  # Read only when preset = "custom". On any other preset these answer nothing
  # and will be listed as unused, which is accurate rather than a warning to
  # silence.
  want = {
    model     = false;   # legacy. In a profile written before the picker,
                         # false still means "no model at all"; where ai_model
                         # is also set, ai_model wins. New profiles want that
                         # key, not this one.
    bluetooth = true;
    printing  = false;
    wine      = false;
    phone     = false;   # kdeconnect
    steam     = false;   # + the 32-bit stack and CachyOS Proton;
                         #   enables multilib and [cachyos]
    blackarch = true;    # the repo and keyring, no tools
    nix       = true;    # nix + Home Manager (`syn nix`)
  };

  # ── Which SynapseOS packages ────────────────────────────────────────────
  #
  # EVERY one of them, including the ones that used to be installed whatever
  # you answered. Read only when preset = "custom"; on any other preset these
  # answer nothing and are listed as unused, which is accurate.
  #
  # What you CANNOT do is drop something a package you kept depends on. Those
  # are turned back on and named on screen rather than silently obeyed:
  #   synui → syntty · synnet → synapd · vibe → synapd + syn-confine ·
  #   firstboot → syn-model · any ai_model → syn-model
  comp = {
    synui      = true;   # the compositor. Without it, desktop = must not be synui
    synapd     = true;   # the local LLM daemon
    synsh      = true;
    synguard   = true;   # synguard + the kernel module, together
    synnet     = true;
    synpkg     = true;   # the package manager
    synfiles   = true;   # the file manager
    syntty     = true;   # the terminal
    synsettings = true;
    syndisks   = true;
    synedit    = true;
    synupdate  = true;   # WITHOUT THIS the machine can never receive a fix
    syn        = true;   # the top-level `syn` CLI
    synmodel   = true;   # `syn model download`
    synfirstboot = true;
    synconfine = true;   # the Landlock sandbox vibe's shell tool runs in
    fetch      = true;   # the About OS readout

    arcade   = true;     # syn-arcade: overlay, pads, big screen mode
    cliamp   = true;     # the player big screen mode drives
    synstudio = true;    # the darkroom: RAW develop, masks, graded video
    arsenal  = true;
    chibi    = true;
    vibe     = true;
    wpengine = true;     # the SynapseOS animated wallpapers + their renderer
                         #   (~317 MB); no Steam needed. Off leaves synui's
                         #   wallpaper picker with no Wallpaper Engine rows.
    nexus    = false;    # both of these pull in Firefox as a dependency
    tepris   = false;
  };

  # ── Ordinary software ───────────────────────────────────────────────────
  #
  # Nothing here is ours; every name is in core/extra. Also custom-only.
  # Omitted keys are the Standard default, which is Firefox and nothing else —
  # a machine with no browser at all was the gap this closes.
  # One key per line, and that is not house style: the drift check in
  # tests/config_test.sh reads this file a line at a time and takes the FIRST
  # `name =` on it, so two on one line would document one and quietly leave the
  # other looking undocumented — or worse, documented and unread.
  sw = {
    # Web and communication
    firefox     = true;
    chromium    = false;
    vivaldi     = false;
    thunderbird = false;
    discord     = false;
    telegram    = false;
    signal      = false;
    keepassxc   = false;
    qbittorrent = false;
    syncthing   = false;
    localsend   = false;   # flatpak from flathub, not a pacman package
    # Audio and video
    vlc       = false;
    mpv       = false;
    obs       = false;
    audacity  = false;
    kdenlive  = false;
    handbrake = false;
    spotify   = false;
    # Office and graphics
    libreoffice = false;
    gimp        = false;
    inkscape    = false;
    krita       = false;
    blender     = false;
    calibre     = false;
    # Development and admin
    code        = false;
    neovim      = false;
    gitlfs      = false;
    docker      = false;
    virtmanager = false;
    gparted     = false;
    btop        = false;
    filezilla   = false;
    remmina     = false;
    archivers   = false;   # 7zip + unrar
    # Games, launchers and helpers. Steam is want.steam above, not here: it is
    # the only one that turns on a second architecture and a third repository.
    lutris       = false;
    prism        = false;
    retroarch    = false;
    dolphinemu   = false;
    ppsspp       = false;
    scummvm      = false;
    pinball      = false;  # Space Cadet; flatpak, fetches its own game data
    dosbox       = false;
    mame         = false;
    protontricks = false;
    winetricks   = false;
    goverlay     = false;
    antimicrox   = false;
    openrgb      = false;
    corectrl     = false;
  };

  selection_ok = true;         # the read-back of everything above

  # ── User ────────────────────────────────────────────────────────────────
  username = "syn";
  fullname = "";
  password = "changeme";       # chmod 600 this file, or leave it out and be asked

  # ── Desktop ─────────────────────────────────────────────────────────────
  desktop = "synui";           # synui | kde | gnome | tty

  # ── Locale and time ─────────────────────────────────────────────────────
  # `language` also accepts a menu number, but do not use one in a profile:
  # the number means whatever that row is on the day you install. "other" plus
  # the three explicit keys below says the same thing and keeps saying it.
  language   = "other";
  locale     = "en_US.UTF-8";
  keymap     = "us";           # console
  xkb_layout = "us";           # desktop

  # The font pack for that locale, read only when language = "other" — which is
  # what a profile always is. Latin/Greek/Cyrillic need nothing here
  # (noto-fonts is installed either way); a CJK locale needs noto-fonts-cjk and
  # an Indic/Arabic/Hebrew one noto-fonts-extra, or the locale is set correctly
  # and every glyph on the machine is a box. Left out, it is noto-fonts-extra:
  # the widest cover for a locale nothing here can identify.
  # lang_fonts = "noto-fonts-cjk";

  # A tzdata name, an abbreviation like CST, or "other" + timezone_name.
  timezone = "America/Chicago";
  # timezone_name = "Europe/Lisbon";

  # ── Odds and ends ───────────────────────────────────────────────────────
  wifi_picker   = false;       # only asked when there is no connection
  gpu_inference = true;        # NVIDIA only: install the ~4.7 GiB CUDA runtime

  # press_enter_start / press_enter_reboot are the two bare acknowledgements.
  # Supplying a profile at all is taken as answering them, so they need no key.
}
