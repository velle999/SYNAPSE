# Maintainer: Velle Sinclair <brncomputerhelp@gmail.com>
#
# syn-settings — SynapseOS system settings.
pkgname=syn-settings
# pkgver stays 0.1.0 and releases move pkgrel, which is the convention every
# SynapseOS component follows — but here it is a HARD constraint, not a habit:
# build-all.sh writes "$name-0.1.0.tar.gz" and transforms paths to
# "$name-0.1.0/" for every component it builds. Bumping pkgver without changing
# that leaves makepkg looking for a tarball nothing creates.
pkgver=0.1.0
# 30: an AI pane, because AI was reachable from exactly one place — synui's
#   control panel. The installer offers KDE and GNOME, and on either of them
#   there was no way to see whether synapd was running, no way to turn it off,
#   and no way to find out which model it would load. The daemon was still
#   there holding GPU memory; only the switch was missing.
#   ⚠ IT DOES NOT REIMPLEMENT THE SWITCH. Turning the AI off correctly is four
#   closed resurrection paths deep — socket activation, the LAN bridge socket,
#   three separate Wants=, and a reboot — so `off` MASKS synapd.socket and
#   synapd.service and records the choice outside /run. All of that is
#   synui-ai-backend(1), covered by synui's tests/ai_backend_off.sh, and this
#   pane calls it. A second implementation of "off must hold" is a second thing
#   to get wrong, and the first took four attempts.
#   The pane names the SOCKETS as well as the service: a pane that showed only
#   synapd.service would read "inactive" on a machine where the next message
#   anybody sends starts it again.
#   ⚠ The acceleration row is enumerated from the ggml backend libraries in
#   /usr/lib, not from a guessed per-variant directory. The first draft looked
#   for /usr/lib/synapse-llama/cuda and reported "no build found" on a machine
#   running synapse-llama-cuda — the packages install into /usr/bin and
#   /usr/lib and no such directory has ever existed. Same trap power.c
#   documents for the sleep hooks.
# 31: the Network pane's firewall rows say what the firewall IS. They said
#   "ruleset: not read" before, with an honest note that `nft list` needs root
#   — honest and useless: the box HAS a default-drop input firewall and the
#   pane could not say so, which is the same silence that had it believed
#   missing. synnet publishes its state world-readable now, so the rows report
#   the PREFERENCE, the STATE and the SERVICE separately, and the row carries
#   `choice:firewall` — on/off through `pkexec synnet --firewall`, never by
#   writing synnet's files from here.
#   ⚠ ABSENT PREFERENCE MEANS ON, the same rule synnet applies. Reading a
#   missing file as "off" would report an unfiltered machine that is filtered.
#   ⚠ Nothing here reads the kernel: "what synnet believes" is not "what
#   nftables holds", and presenting the first as the second is the claim this
#   area already got wrong once.
# 32: a "container links" row in the Network pane, because the firewall's effect
#   on a container is INVISIBLE and does not look like a firewall. A Waydroid or
#   libvirt guest lives on 192.168.x, which the LAN-trust rule accepts — but the
#   DHCP request that gets it that address is sent from 0.0.0.0, matches no
#   accept, and the default-drop policy eats it. The guest comes up with no
#   network and nothing in any log says firewall, so people go looking at the
#   container, at DNS, at the bridge. The row lists what
#   /etc/synnet/trusted-ifaces names and warns when synnet has not loaded it.
#   ⚠ It reads the FILE, not the published count, so a bridge added but not
#   applied shows as itself rather than as an absence — that mismatch is exactly
#   the state worth reporting, since synnet only rebuilds a chain that has GONE
#   and one that is merely out of date looks healthy to it.
#   ⚠ And a mismatch while the firewall is OFF is not a warning: that is the
#   definition of off, not a fault.
#
# 33 THE MACHINE'S NAME, AND THE LIGHTS.
#
#   ⚠ EVERY SynapseOS install answers to `synapse`. Two of them on one network
#   and Avahi renames one `synapse-2.local`, with no say in which and no
#   promise the suffix survives a reboot — so the .local address stops being a
#   thing anybody can rely on. Renaming was a `hostnamectl` one-liner, which is
#   to say it was not in the settings app at all.
#
#   `set:hostname` on the System pane's identity row, `hostnamectl
#   set-hostname` underneath it. That keeps the design this file has had from
#   the first commit: the write goes through a systemd tool that does its OWN
#   polkit check, and this binary stays not-setuid with no policy of its own.
#
#   ⚠ Validated HERE and more narrowly than sane_value, which allows `/`
#   because most values here are paths. A hostname is letters, digits, hyphen
#   and dot, no leading or trailing punctuation, 63 characters — and refusing
#   it here says what was wrong with what was typed, where refusing it in
#   systemd says "hostnamectl exited 1".
#
#   AND THE RGB LIGHTS. syn-rgb(1) — synui 431 — puts the wallpaper's accent on
#   the RAM, the board and the keyboard. The switch is a row on this pane and a
#   row on synui's control panel, and BOTH run the same command: syn-rgb owns
#   the state file, the systemd path unit and the hardware, so neither door has
#   an opinion of its own to drift.
#
#   ⚠ The row reads rgb.state on every pane load rather than caching it, so
#   `syn-rgb on` typed in a terminal moves it. ⛔ And it is drawn whether
#   openrgb is installed or not — a control that vanishes with its optdepend is
#   a feature nobody finds out about — saying which package to install instead.
# 34: the speech pane — the second door onto the two switches an
#   accessibility user needs first.
#   synui's control panel got them in synui 530; this is the other door, and it
#   is the one that matters on a machine installed with KDE or GNOME, where
#   synui's panel is not there at all. Screen reader, "Answer to its name", the
#   voice and dictation engines this box actually has, speech rate and volume,
#   and the wake words.
#   ⚠ IT REIMPLEMENTS NOTHING. syn-speak(1) owns the announcer and vibe(1) owns
#   the listener; this reads their state files and runs their commands. The AI
#   pane's rule, for the same reason.
#   ⛔ THE WAKE ROW READS assistant.state, NOT `systemctl is-active`. A unit can
#   be active with its listener failing to open the device, and the question
#   this row answers is whether a MICROPHONE IS OPEN. synui's bar indicator and
#   its control panel row read the same file — one fact, three surfaces.
#   ⛔ AND A DEAD BUTTON WAS FOUND BY TESTING THE ROW'S OWN ADVICE. `wake-words`
#   is the one value in this app that legally contains commas, and set()'s
#   generic sane_value() gate rejects a comma before any key is looked at — so
#   the row offered a comma-separated list and refused every comma. Lifted
#   above the gate with a validator of its own (letters, commas, spaces);
#   loosening sane_value would have weakened it for the thirty keys that DO
#   exec. The test now sets each key to the value its own help text suggests,
#   which is the check that would have caught it.
# 42: the Speech pane shows the unit behind the reader, not just the switch.
#   ⛔ A SWITCH AND THE THING IT SWITCHES ARE TWO SEPARATE FACTS. This pane
#   listed vibe-wake.service — the listener — and nothing for the announcer,
#   which is precisely where the gap was: syn-speak.service was missing from the
#   synui package for four releases (synui 564 ships it), `syn-speak on` enabled
#   a unit that was not there, the failure was swallowed on purpose so that a
#   machine without systemd is not a broken install, and the Screen reader
#   switch here reported On with nothing behind it. A row reading "not
#   installed" beside a switch reading "on" is the sentence that would have said
#   so; both units are listed now, reader first, matching the switches above.
#   ⛔ AND "not-found" IS THE ANSWER THAT ACTUALLY HAPPENS. The old inline
#   lookup fell back to "not installed" only on EMPTY output — which comes from
#   an older systemd or from systemctl failing outright — while systemd prints
#   "not-found" for `is-enabled` on a unit it does not have. Both spellings are
#   translated now, in a shared user_unit_state() the wake row uses too; the
#   same trap ai.c documents in unit_absent(), and the reason this one is
#   --user: everything on this pane is a session service.
# 43: the window speaks thirteen languages.
#   The same bridge synui's bar carries: quickshell has no translator at all —
#   qsTr() compiles and returns its argument — so a .po becomes JSON at build
#   time and qml/I18n.qml reads it with a blocking FileView, because tr() is a
#   function and not a binding. 85 msgids, thirteen catalogs, installed beside
#   I18n.qml so the file locates its own directory and one byte-identical copy
#   serves every package.
#   ⛔ A PANE ID IS NOT A LABEL. panes[] carries both, and the id is matched
#   on in three places — SYNSETTINGS_PANE, the state file, and root.pane ===
#   — so translating it would move a German user to a pane that does not exist.
#   The gate asserts none of them is inside an I18n.tr(); the same trap as a
#   menu action name, and the reason the ids are spelled apart from the labels.
#   ⛔ AND AN ENGLISH GERUND WAS BUILT FROM A PROTOCOL WORD. The Bluetooth
#   rows took the verb the binary takes — "connect" or "disconnect" — and made
#   their status line by appending "ing" to it. That is unreachable by a
#   translator in any language and wrong in most; it is two whole sentences now,
#   chosen by the same value, and the value itself still reaches the binary
#   unchanged. Same for "%1 row"/"%1 rows", which no longer picks its form with
#   an English == 1.
# 47: ten drawn labels that only exist on a machine with a fingerprint reader.
#   syn-settings 46's i18n gate FAILED on velle's ThinkPad and passed here, and
#   `syn-update` aborted mid-build with every component after it unbuilt. The
#   check runs the program and reads the record, and this box has no reader — so
#   fprint.c's ten finger rows are never emitted here and their labels ("Right
#   index", "Left thumb", …) had never been marked, along with two sentences and
#   the enrolled/not-enrolled words beside them.
#   ⛔ THE GATE NOW FAKES THE READER. tests/i18n_test.sh puts an fprintd-list on
#   PATH that reports one device and two enrolled fingers, which works in both
#   directions: on a box without fprintd it ADDS the command, and on one with it
#   have_cmd() walks PATH in order and finds this first. Ten rows on every
#   machine, and one more assertion that they are really there — shadowing that
#   silently did nothing would put the check straight back to reading hardware.
#   ⚠ AND `enrolled` STAYS ENGLISH IN THE ROW. data/syn-settings.qml compares
#   `root.selValue === "enrolled"` to decide whether the button offers "Enrol
#   again…"; N_() puts the word in the catalog and leaves the record alone, and
#   the window translates the cell on its way to the screen.
#
# ── and the same class in six more places, found by a check that runs nothing ─
#
#   A second gate reads the SOURCE instead: every string literal handed to
#   rec_row() as an ARGUMENT is a cell somebody reads, so it is N_() or one of a
#   listed set of tokens a program matches — `current`, a unit name, a
#   `toggle:`/`choice:` action — and there is no third category. Machine-
#   independent, and it found what the runtime one cannot see from here.
#   ⛔ PROSE INSIDE A rec_row FORMAT STRING IS DRAWN AND REACHES NO TEMPLATE.
#   xgettext extracts the format whole, and the whole format is not the cell the
#   window looks up. Six of them: the AVRCP note, the unowned-kernel sentence,
#   the two firewall choices and the three AI-backend ones. All are arguments
#   now, and the choice list is translated at the draw site like every other
#   opportunistic cell.
#   ⚠ THE KERNEL SENTENCE PUT ITS RELEASE AT THE END. Whole-cell lookup cannot
#   match a sentence with a version in the middle of it — every machine composes
#   a different string — so the release trails and the marked sentence is a
#   PREFIX, which is the shape this pane already used and the gate accepts.
#   Six more drawn words marked with it: "not driven", "none this boot",
#   "listening", "24-hour"/"12-hour", and what synui-clock says when it is not
#   there. 326 msgids in each of the thirteen catalogs.
# 48: the same class again, in a table this time — and two gates that cannot
#   read this machine.
#   47's gate failed on velle's box a second time, on `Vulkan`. ai.c lists one
#   accelerator row per ggml backend library PRESENT, and of its four labels two
#   were marked and two were not — the two with no library here. Nothing could
#   see them: not the runtime check (the rows are never emitted on this box) and
#   not the rec_row check (the words are in a table, so the call site passes
#   `accel[i][1]` and not a literal).
#   ⛔ ALL FOUR ARE MARKED NOW, OR NONE WOULD BE. They are product names and
#   every translator will leave them alone; that is not the point. A label in a
#   drawn column is either in the catalog or it is not reachable, and "it
#   happens to be a proper noun" is not a property a check can read.
#   ⛔ AND THE LIBRARY DIRECTORY IS A SEAM, `SYN_SETTINGS_LIBDIR`, exactly as
#   SYN_DISKS_SYSFS is one. tests/i18n_test.sh points it at four stub files and
#   gets all four rows on any machine — the second half of the same fix the
#   fingerprint fixture was.
#   ⚠ AND A THIRD GATE READS THE STATIC TABLES. A table entry that is all
#   lower-case, digits, dots, colons or dashes is a token the record owns;
#   anything else in a table that can feed a drawn column is a label and must
#   be N_(). That is what would have caught these two here.
# 49: a drawn label no machine here can reach. The apps pane reports which
#   terminal synui would launch, and where NOTHING is set it reports the
#   built-in fallback in words rather than naming a file — a sentence, where
#   every other value of that cell is a path. It was unmarked, and could not be
#   caught here: every developer box has `terminal =` in synuirc, so the row
#   says "chosen" and a path. A clean build root has no config, hit the other
#   branch, and failed the build inside makepkg.
#   ⚠ THIRD TIME FOR THIS CLASS. The fingerprint rows needed a stubbed fprintd
#   and the accelerator rows a stubbed library directory, both because a pane
#   emits different rows on different machines. This one varies with the
#   CONFIG rather than the hardware, so the suite now collects the apps pane a
#   second time under an empty HOME and XDG_CONFIG_HOME — which is what a fresh
#   install looks like. Verified by unmarking the string again and watching the
#   suite fail on this box with the same message the build root printed.
# 50: A REMOTE DESKTOP PANE — and the SPEECH PANE, which had been unreachable.
#   syn-remote(1) is new, and a service with no row in this window is a service
#   reachable from a terminal and nowhere else. The pane reads
#   `syn-remote status --rec` and runs syn-remote's own commands; it owns no
#   part of the server, on the rule the speech and AI panes already follow.
#
#   ⛔ AND pane_speech() WAS ALREADY IN EXACTLY THAT STATE. It existed in C,
#   usage() named it, `set` had a handler for every row on it — and
#   data/syn-settings.qml's `panes` list did not mention it, so the screen
#   reader and the wake word were reachable from a prompt and from nowhere
#   else. Nothing failed: the binary answered, the window simply never asked.
#   The person who wants a screen reader is the person who cannot read the
#   terminal to start one, which is the sentence speech.c opens with. Both
#   panes are in the list now, and the suite reads BOTH sources and fails on a
#   pane either side does not have — the check that would have caught it.
#
#   ⚠ THE SCOPE ROW SAYS IT IN WORDS. synnet's default-drop input chain accepts
#   everything from 10/8, 172.16/12 and 192.168/16 (monitor.c), so "0.0.0.0"
#   does not mean "bound wide, still firewalled" — it means every device on the
#   network can reach this desktop and there is no second door left to unlock.
#   An IP tells that to somebody who already knows it.
#
# 52: THE ADDRESSES ARRIVE MASKED, and a button in the header reveals them. 51
#   put a MAC and an IP on screen for the first time, which is also the first
#   time this window has held a fact that identifies the machine rather than
#   describing it — and this is the window that ends up in a screenshot attached
#   to a question, or open behind a stream. Every `mac` and `ip` value, and the
#   Bluetooth adapter's and every paired device's address, now read
#   ••:••:••:••:••:•• until asked for.
#
#   ⛔ THE READER MASKS, NOT THE WINDOW. A GUI that hid what the binary printed
#   would leave `syn-settings --rec network` — the same answer, one pipe away —
#   putting the address in a terminal that is just as visible. So the mask is in
#   the record and `--reveal` is what lifts it; the button passes the flag and
#   RE-READS, which is why what is on screen is never something the window is
#   holding and not drawing.
#
#   ⛔ THE MASK KEEPS THE SHAPE AND NOT ONE CHARACTER. Every run of address
#   characters becomes two bullets whatever its real length, so the cell still
#   says how many groups there are — which the `kind` column already said — and
#   nothing about what is in them. Keeping the real widths would hand back the
#   difference between 10.0.0.5 and 192.168.1.100.
#
#   ⚠ AND IT MASKS AN ADDRESS, NOT A CELL. An interface with no hardware address
#   reports "none", and bullets there would be a machine hiding something it
#   does not have.
#
#   ⚠ A NEW ACTION TOKEN, `address:hidden` / `address:shown`. It is how the
#   window knows a pane has something to reveal, and which way the button
#   should read, without knowing which panes hold addresses — and it is NOT a
#   setting: rowSetAction() strips it, or a row that is only an address would
#   draw as a live control whose Apply button runs `syn-settings address
#   hidden`. The trap `unavailable:` sprang once already, one column over.
pkgrel=52
pkgdesc="SynapseOS settings: displays and resolution, keyboard and language, date and time, network addresses and interfaces, Bluetooth, power and sleep, kernels, and where configuration lives"
arch=('x86_64')
url="https://github.com/velle999/SYNAPSE"
license=('GPL-2.0-or-later')

# wlr-randr is a hard dependency, not an optional one: it is the only way this
# app can change a resolution, and "Settings cannot change your resolution
# until you install something" is not a settings app. It is 28 KB, and on a
# headless box it simply never runs.
#
# Everything else stays optional. Each answer comes from a tool that already
# owns it, and each pane degrades to "not installed" rather than failing, so
# this still works over SSH and on the ISO.
depends=('glibc' 'wlr-randr')
makedepends=('meson' 'ninja' 'gcc')

optdepends=('quickshell: the settings window (syn-settings gui)'
            'systemd: localectl, timedatectl and systemctl — the Region and Power panes'
            'synui: synctl, for what the compositor is actually driving'
            'networkmanager: nmcli — the Network pane'
            'bluez-utils: bluetoothctl — the Bluetooth pane'
            'util-linux: rfkill — soft and hard radio blocks'
            'synpkg: install and remove kernels from the Kernel pane'
            'pacman: which kernels are installed'
            # The Fingerprint pane reads what it can without this — it says
            # "not installed", which is the honest answer and the one every
            # desktop without a reader gets. Enrolling needs the CLIs.
            'fprintd: the Fingerprint pane — enrol a finger for the lock screen'
            'polkit: change settings without dropping to a root shell')

# ── Where the source comes from, here and everywhere else ──────────────────
#
# ⛔ ONE source LINE SERVES BOTH, AND THAT IS DELIBERATE. build-all.sh runs
# tools/collect-source.sh, which drops $pkgname-$pkgver.tar.gz beside this file;
# makepkg finds it (`-> Found ...`) and never touches the URL. Anybody WITHOUT
# this checkout has no such file, so makepkg fetches the identical tarball from
# the release that carries this exact pkgver-pkgrel. A second PKGBUILD for
# outside use would be a second set of depends and install rules, free to drift
# from this one — and the person it broke for could not see this file at all.
#
# ⚠ ITS OWN REPOSITORY, NOT THIS ONE. The source release lives at
# github.com/velle999/$pkgname — which is also where the PKGBUILD is published
# as a clonable package repo — because putting them on SYNAPSE's releases page
# buried the ISO downloads under a component tarball per bump, and made the
# newest of those GitHub's "Latest release" for the whole project.
#
# ⚠ THE TAG CARRIES THE pkgrel, so the URL cannot point at the wrong source.
# preflight.sh already refuses a source edit that does not bump pkgrel, which
# means every change to what gets built moves this URL with it.
#
# ⛔ AND sha256sums STAYS 'SKIP'. A real checksum would break every LOCAL build
# the moment somebody edited a source file, because the tarball beside this file
# is regenerated from the working tree and would no longer match. The published
# asset is reproducible instead — collect-source.sh sorts and zeroes the
# timestamps, so `tools/collect-source.sh <name>` at the tagged commit
# re-derives it byte for byte. packaging/README.md has the whole of it.
source=("$pkgname-$pkgver.tar.gz::https://github.com/velle999/$pkgname/releases/download/$pkgver-$pkgrel/$pkgname-$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "$srcdir/syn-settings-0.1.0"
    meson setup build --prefix=/usr --buildtype=release
    meson compile -C build
}

check() {
    cd "$srcdir/syn-settings-0.1.0"
    # Read-only, and the write paths are exercised only through --dry-run. A
    # package that changed the build machine's timezone while being built would
    # be a memorable bug.
    meson test -C build --print-errorlogs
}

package() {
    cd "$srcdir/syn-settings-0.1.0"
    DESTDIR="$pkgdir" meson install -C build
}
