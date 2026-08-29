#!/usr/bin/env bash
# syn-settings — smoke tests.
#
# These check the CONTRACT, not the values: a machine with no NVIDIA, no
# localectl or no compositor must still produce a well-formed table, because
# the whole design rests on "a missing tool is a value, not a failure". A test
# that asserted "keymap is us" would pass here and fail on the ISO.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

BIN=${1:?usage: syn_settings_test.sh /path/to/syn-settings}
fails=0

ok()   { printf '  ok   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }

# Assert that a command REFUSES, with the status it should refuse with.
#
# ⚠ `|| rc=$?` IS THE WHOLE POINT. This suite runs under `set -euo pipefail`,
# and a refusal is a non-zero exit — the very thing being asserted. Written as a
# bare call it kills the run at the first check, silently: the script stops
# mid-file, prints no failure, and a `| tail` hands back tail's own status so it
# still looks green.
#
# ⚠ AND IT LIVES HERE, beside ok/bad, not beside its first caller. Defined
# further down it is simply "command not found" for everything above it — which
# under `set -e` is the same silent early exit wearing a different hat.
refuses() {   # <description> <expected-status> <args...>
    local desc=$1 want=$2 rc=0
    shift 2
    "$BIN" "$@" >/dev/null 2>&1 || rc=$?
    if [ "$rc" -eq "$want" ]; then ok "$desc"; else
        bad "$desc (exit $rc, wanted $want)"
    fi
}
# Named, not silent: a check that does not apply on this machine has to say so,
# or a suite that quietly stopped covering something still prints all green.
skip() { printf '  --   %s\n' "$1"; }

# `((n++))` evaluates to the OLD value, so it returns 1 the first time and
# kills the script under `set -e`. Hence fails=$((fails + 1)) above.

check_table() {
    local pane=$1 out ncols nrows line
    out=$("$BIN" --rec "$pane") || { bad "$pane: exited non-zero"; return; }

    [ -n "$out" ] || { bad "$pane: printed nothing"; return; }

    ncols=$(head -1 <<<"$out" | awk -F'\t' '{print NF}')
    [ "$ncols" -ge 2 ] || { bad "$pane: header has $ncols columns"; return; }
    ok "$pane: header has $ncols columns"

    # Every row must have exactly as many fields as the header. This is the
    # test that catches an unescaped tab in a value, which would silently
    # shift every column to its right rather than fail.
    nrows=0
    while IFS= read -r line; do
        nrows=$((nrows + 1))
        local n
        n=$(awk -F'\t' '{print NF}' <<<"$line")
        if [ "$n" -ne "$ncols" ]; then
            bad "$pane: row $nrows has $n fields, header has $ncols"
            return
        fi
    done < <(tail -n +2 <<<"$out")
    ok "$pane: $nrows rows, all $ncols fields wide"
}

echo "syn-settings smoke tests"

for pane in display region network bluetooth power kernel system apps time ai; do
    check_table "$pane"
done

# The `action` column is the contract between the C reader and the GUI: the
# window decides what is editable purely from it. A verb the QML does not know
# renders a dead button, and a malformed one renders a button that builds a
# command nobody validated — so the shape is checked here rather than
# discovered by clicking.
check_actions() {
    local pane=$1 out col line a
    out=$("$BIN" --rec "$pane") || { bad "$pane: exited non-zero"; return; }
    col=$(head -1 <<<"$out" | awk -F'\t' '{for(i=1;i<=NF;i++) if($i=="action") print i}')
    [ -n "$col" ] || { ok "$pane: no action column (read-only pane)"; return; }

    # An action cell is a SPACE-SEPARATED LIST — a row offering two things
    # genuinely offers both, and the GUI draws one button per token. Validating
    # the whole cell as one token would pass anything once a second appeared.
    local t
    while IFS= read -r line; do
        a=$(awk -F'\t' -v c="$col" '{print $c}' <<<"$line")
        [ "$a" = "-" ] && continue
        for t in $a; do
            case "$t" in
                set:*|toggle:*|unit:*|probe:*|mode:*|device:*|boot:*|install:*|remove:*|default:*|app:*|choice:*|enroll:*|forget:*|secret:*|unavailable:*) ;;
                *) bad "$pane: unknown action verb '$t'"; return ;;
            esac
            # A verb with an empty argument is the one that looks fine in a
            # table and builds `syn-settings set  <value>` when clicked.
            case "$t" in
                *:) bad "$pane: action '$t' has no argument"; return ;;
            esac
        done
    done < <(tail -n +2 <<<"$out")
    ok "$pane: every action is a known verb with an argument"
}

for pane in display region network bluetooth power kernel apps time ai speech fprint assistant; do
    check_actions "$pane"
done

# ── Default applications ────────────────────────────────────────────────────
#
# Every write here goes into a SANDBOX $HOME. The rest of this suite is safe
# because --dry-run is honoured; this part exercises the real writer, so it is
# safe only because $XDG_CONFIG_HOME points somewhere disposable. A test that
# ran this against the real one would rewrite the mimeapps.list of whoever ran
# `meson test`.
SBOX=$(mktemp -d)
trap 'rm -rf "$SBOX"' EXIT
mkdir -p "$SBOX/.config" "$SBOX/bin"

# A stand-in pkill, first on PATH: setting the terminal signals synui to
# reparse its config, and a test suite must not send signals to the running
# desktop of the person who typed `meson test`.
printf '#!/bin/sh\nexit 0\n' > "$SBOX/bin/pkill"
chmod +x "$SBOX/bin/pkill"

sbox() { env HOME="$SBOX" XDG_CONFIG_HOME="$SBOX/.config" \
             PATH="$SBOX/bin:$PATH" "$BIN" "$@"; }

# Every role the pane lists must be a role `apps` and `set app` accept. A row
# offering app:<role> that the writer then rejects is a button that fails only
# when clicked.
roles=$("$BIN" --rec apps | tail -n +2 | awk -F'\t' '{print $NF}' | sed 's/^app://')
bad_role=""
for r in $roles; do
    "$BIN" apps "$r" >/dev/null 2>&1 || bad_role="$r"
done
[ -z "$bad_role" ] && ok "every role the pane offers is one that apps accepts" \
                   || bad "role '$bad_role' is listed but not accepted"

# The candidate list is the same three-column shape `modes` uses.
if "$BIN" apps browser | awk -F'\t' 'NF != 3 { exit 1 }'; then
    ok "apps <role> lists three fields per candidate"
else
    bad "apps <role> produced a row that is not three fields"
fi

"$BIN" apps nonesuch >/dev/null 2>&1 \
    && bad "apps accepted an unknown role" \
    || ok "apps refuses an unknown role"

# The application must EXIST. A default naming something uninstalled reads like
# a setting and behaves like none: the spec skips such entries, so the old
# default keeps winning and the pane keeps showing it.
sbox set app browser definitely-not-installed.desktop >/dev/null 2>&1 \
    && bad "set app accepted an application that is not installed" \
    || ok "set app refuses an application that is not installed"

# A NAME, never a path — the same rule that stops synfiles running any Exec=
# on the system.
sbox set app browser ../../../etc/passwd >/dev/null 2>&1 \
    && bad "set app accepted a path" \
    || ok "set app refuses a path"

sbox set app potato firefox.desktop >/dev/null 2>&1 \
    && bad "set app accepted an unknown role" \
    || ok "set app refuses an unknown role"

# The round trip, against whatever this machine actually has: pick the first
# candidate the binary itself offers, write it, and read it back as CHOSEN
# from the sandbox file. Skipped rather than failed on a machine with no
# browser, because "nothing is installed" is a legitimate state here.
first=$("$BIN" apps browser | head -1 | cut -f1)
if [ -n "$first" ]; then
    sbox set app browser "$first" >/dev/null
    state=$(sbox --rec apps | awk -F'\t' '$NF == "app:browser" { print $3 }')
    src=$(sbox --rec apps | awk -F'\t' '$NF == "app:browser" { print $5 }')
    [ "$state" = chosen ] && ok "a written default reads back as chosen" \
                          || bad "after writing, the browser role reads '$state'"
    case "$src" in
        "$SBOX"/*) ok "and names the file it was written to" ;;
        *) bad "the source reads '$src', not the file just written" ;;
    esac

    # EVERY type in the role, not just the one reported. A viewer that took
    # PNG and left JPEG behind is not a setting anybody meant to make.
    n=$(grep -c "=$first\$" "$SBOX/.config/mimeapps.list" || true)
    [ "$n" -ge 3 ] && ok "one choice writes every mime type in the role" \
                   || bad "the role wrote only $n type(s)"

    # Somebody else's data survives. This file has other editors.
    printf '\n[Added Associations]\nx-scheme-handler/zoom=zoom.desktop;\n' \
        >> "$SBOX/.config/mimeapps.list"
    sbox set app browser "$first" >/dev/null
    grep -q 'x-scheme-handler/zoom=zoom.desktop;' "$SBOX/.config/mimeapps.list" \
        && ok "an unrelated group survives a write" \
        || bad "a write dropped another group from the file"

    # Once, ever — or the second run overwrites the pre-syn-settings state
    # with a post-syn-settings one and the backup is worthless.
    [ -f "$SBOX/.config/mimeapps.list.pre-syn-settings" ] \
        && ok "the first write leaves a backup" \
        || bad "no backup was taken before the first write"
else
    skip "no browser installed — round trip not exercised"
fi

# The terminal is the one role no mimeapps.list decides: synui reads it from
# synuirc, so writing an x-scheme-handler would be a control that changes a
# file nothing on this desktop reads.
if "$BIN" apps terminal | grep -q .; then
    t=$("$BIN" apps terminal | head -1 | cut -f1)
    sbox set app terminal "$t" >/dev/null
    grep -q "^terminal = $t\$" "$SBOX/.config/synui/synuirc" \
        && ok "the terminal is written to synuirc, not to mimeapps.list" \
        || bad "synuirc has no terminal line after set app terminal"
    grep -q "terminal" "$SBOX/.config/mimeapps.list" \
        && bad "the terminal leaked into mimeapps.list" \
        || ok "and nowhere else"

    sbox set app terminal not-a-terminal >/dev/null 2>&1 \
        && bad "set app terminal accepted something unknown" \
        || ok "set app terminal refuses something it does not know"
else
    skip "no terminal emulator installed — terminal role not exercised"
fi

# An unknown pane must be refused, not silently empty.
if "$BIN" --rec nonesuch >/dev/null 2>&1; then
    bad "unknown pane was accepted"
else
    ok "unknown pane refused"
fi

# --help must work and must not need a display.
if "$BIN" --help | grep -q 'syn-settings'; then
    ok "--help prints usage"
else
    bad "--help printed nothing useful"
fi

# Writes must never run during a test. --dry-run is the guarantee, so it is
# itself tested: if this ever stopped being honoured the suite would start
# changing the machine it runs on.
if "$BIN" --dry-run set timezone UTC | grep -q '^would run: timedatectl set-timezone UTC$'; then
    ok "--dry-run shows the command and runs nothing"
else
    bad "--dry-run did not report the expected command"
fi

# Argument validation: a value that could be read as a flag must be refused
# before it reaches a tool that would honour it.
for bad_val in "--adjust" "a;b" 'x$(id)' "back\`tick\`"; do
    if "$BIN" --dry-run set timezone "$bad_val" >/dev/null 2>&1; then
        bad "accepted hostile value: $bad_val"
    else
        ok "refused: $bad_val"
    fi
done

# pkg is restricted to the kernels this pane manages. It must never become a
# general "install anything by name" endpoint reachable from a GUI row.
for evil in "firefox" "base" "sudo" "linux-lts-evil"; do
    if "$BIN" --dry-run pkg install "$evil" >/dev/null 2>&1; then
        bad "pkg accepted a package outside the kernel list: $evil"
    else
        ok "pkg refused: $evil"
    fi
done
if "$BIN" --dry-run pkg install linux-lts | grep -q 'synpkg .*install linux-lts linux-lts-headers'; then
    ok "pkg install pulls the matching headers"
else
    bad "pkg install did not include headers"
fi

# ⚠ --noconfirm is LOAD-BEARING, not cosmetic. synpkg's confirm() refuses when
# stdin is not a terminal, and this binary has no terminal — so without the
# flag every install authenticated through polkit and then declined itself,
# exiting 0 with nothing installed. That was the Kernel pane's install button
# for its whole life, and nothing failed loudly enough to catch it.
if "$BIN" --dry-run pkg install linux-zen | grep -q -- '--noconfirm'; then
    ok "pkg install passes --noconfirm"
else
    bad "pkg install dropped --noconfirm — it will silently install nothing"
fi

# --verbose is what makes the DOWNLOAD visible. synpkg names each file only
# once it has arrived and only when asked; a kernel is a couple of hundred
# megabytes, so without this the longest stretch of the operation reports
# nothing and the window looks hung — which is how this was reported.
if "$BIN" --dry-run pkg install linux-zen | grep -q -- '--verbose'; then
    ok "pkg install asks synpkg to say what it is doing"
else
    bad "pkg install dropped --verbose — the download will be silent"
fi

# A CachyOS kernel is not in any Arch repository, so the repo has to be added
# BEFORE the install, or pacman reports "not found" after the password prompt.
#
# ⚠ BOTH branches are pinned. A machine is only ever in one of them, and the
# interesting one — repo absent — stops being reachable the moment the repo is
# enabled, which is the state every machine ends up in. Read from the live
# answer this check would have quietly rotted into a no-op.
out=$(SYN_SETTINGS_CACHYOS_ENABLED=0 "$BIN" --dry-run pkg install linux-cachyos)
if printf '%s' "$out" | grep -q 'synpkg cachyos enable-repo'; then
    ok "repo absent: installing a Cachy kernel enables it first"
else
    bad "repo absent: installing a Cachy kernel did not enable the CachyOS repo"
fi
if printf '%s' "$out" | grep -q 'install linux-cachyos linux-cachyos-headers'; then
    ok "the Cachy kernel still pulls its headers"
else
    bad "the Cachy kernel did not pull its headers"
fi

# Already enabled: enable-repo needs root, so running it anyway would be a
# second password prompt before every install, to be told "already enabled".
out=$(SYN_SETTINGS_CACHYOS_ENABLED=1 "$BIN" --dry-run pkg install linux-cachyos)
if printf '%s' "$out" | grep -q 'enable-repo'; then
    bad "repo present: installing prompted for a redundant enable-repo"
else
    ok "repo present: installing does not re-enable the repo"
fi
if printf '%s' "$out" | grep -q 'install linux-cachyos linux-cachyos-headers'; then
    ok "repo present: the install still runs"
else
    bad "repo present: the install did not run"
fi

# ── Progress records ────────────────────────────────────────────────────────
#
# Installing a kernel takes minutes. It used to run with its output on
# /dev/null, so the window showed a dimmed button and a status line frozen at
# the moment of the click — reported, twice in one sitting, as a hung app.
#
# The fix is that everything the child says is re-emitted as "progress<TAB>…"
# on our stdout, flushed per line. This drives the whole path with a STUB
# synpkg first on PATH, so the contract is checked without installing a kernel
# on the machine running the tests.
#
# What is checked is the part that is easy to get wrong and impossible to see:
# that a '\r' redraw — which is how pacman-shaped progress arrives, with no
# newline from beginning to end — becomes one record per update rather than
# one enormous record at the end.
stubdir=$(mktemp -d)
trap 'rm -rf "$stubdir"' EXIT
cat > "$stubdir/synpkg" <<'STUB'
#!/bin/sh
printf ':: synchronising package databases\n' >&2
printf '\r(1/2) linux-lts   7%%' >&2
printf '\r(1/2) linux-lts 100%%' >&2
printf '\n' >&2
exit 0
STUB
chmod +x "$stubdir/synpkg"

stream=$(PATH="$stubdir:$PATH" "$BIN" pkg install linux-lts 2>/dev/null)
n=$(printf '%s\n' "$stream" | grep -c '^progress	' || true)
if [ "$n" -eq 3 ]; then
    ok "a write streams one progress record per line, carriage returns included"
else
    bad "expected 3 progress records from the stub, got $n"
fi
if printf '%s\n' "$stream" | grep -q '^progress	(1/2) linux-lts 100%$'; then
    ok "the record carries the percentage the GUI parses"
else
    bad "the percentage line did not survive as its own record"
fi
# An escape sequence reaching the status bar would print as literal "[0m".
printf '#!/bin/sh\nprintf "\\033[2m::\\033[0m coloured\\n" >&2\n' > "$stubdir/synpkg"
if PATH="$stubdir:$PATH" "$BIN" pkg install linux-lts 2>/dev/null \
     | grep -q '^progress	:: coloured$'; then
    ok "colour is stripped out of a progress record"
else
    bad "an ANSI escape survived into a progress record"
fi
rm -rf "$stubdir"
trap - EXIT

# An Arch kernel must NOT drag the CachyOS repo onto the machine.
if "$BIN" --dry-run pkg install linux-zen | grep -q 'cachyos'; then
    bad "installing an Arch kernel touched the CachyOS repo"
else
    ok "an Arch kernel does not enable the CachyOS repo"
fi

# Neither must REMOVE, in either direction. A button labelled Remove that
# configures a new repository would be a genuinely surprising thing.
if "$BIN" --dry-run pkg remove linux-cachyos | grep -q 'enable-repo'; then
    bad "removing a Cachy kernel enabled a repository"
else
    ok "remove never enables a repository"
fi

# A mode string reaches a command line; anything that is not WxH[@R] is refused
# here with a readable message rather than by wlr-randr with a usage dump.
#
# Checked by EXIT CODE, not merely non-zero. On a machine without wlr-randr
# every one of these fails whatever the argument is, so "it failed" would be a
# test that passes for the wrong reason and would keep passing if the
# validation were deleted. 2 is "refused the argument", 1 is "tool missing".
for m in "--output" "1920" "abcxdef" "1920x1080; reboot" ""; do
    # `|| rc=$?` and not a bare call: under `set -e` a non-zero exit that is
    # not part of a conditional kills the script, and every one of these is
    # SUPPOSED to be non-zero. The first version of this loop ended the suite
    # silently at the first case it was testing for.
    rc=0
    "$BIN" --dry-run mode DP-1 "$m" >/dev/null 2>&1 || rc=$?
    if [ "$rc" -eq 2 ]; then
        ok "mode refused (exit 2): ${m:-<empty>}"
    else
        bad "mode did not refuse '${m:-<empty>}' as a bad argument (exit $rc)"
    fi
done

# And the converse: a WELL-FORMED mode must get PAST validation. Without this
# the four checks above would still pass if sane_mode() rejected everything.
rc=0
"$BIN" --dry-run mode DP-1 1920x1080@60 >/dev/null 2>&1 || rc=$?
case "$rc" in
    0) ok "a valid mode passes validation (wlr-randr present)" ;;
    1) ok "a valid mode passes validation (stops at missing wlr-randr)" ;;
    *) bad "a valid mode was refused as a bad argument (exit $rc)" ;;
esac

# Loopback must be refused before nmcli ever sees it: taking lo down breaks
# every daemon on the machine that talks to itself over a UNIX socket.
if "$BIN" --dry-run device disconnect lo >/dev/null 2>&1; then
    bad "device accepted loopback"
else
    ok "device refused loopback"
fi
if "$BIN" --dry-run device disconnect enp0s1 | grep -q 'nmcli device disconnect enp0s1'; then
    ok "device builds the nmcli command"
else
    bad "device did not build the expected command"
fi
for bad_act in "up" "delete" "--help"; do
    if "$BIN" --dry-run device "$bad_act" eth0 >/dev/null 2>&1; then
        bad "device accepted action: $bad_act"
    else
        ok "device refused action: $bad_act"
    fi
done

if "$BIN" --dry-run unit enable not-a-unit >/dev/null 2>&1; then
    bad "accepted a name with no unit suffix"
else
    ok "refused a name with no unit suffix"
fi

# probe resolves a connector against the ones that ACTUALLY EXIST rather than
# building a path from the argument, so a traversal attempt cannot become a
# path at all. Checked with --dry-run so nothing is written even when this
# suite runs as root.
for bogus in "nope" "../../etc/passwd" "card1-DP-3/../../.."; do
    if "$BIN" --dry-run probe "$bogus" >/dev/null 2>&1; then
        bad "probe accepted a non-connector: $bogus"
    else
        ok "probe refused: $bogus"
    fi
done

# A real connector must resolve, whatever this machine happens to have. Skipped
# rather than failed on a box with no DRM at all (a VM, a container, the CI
# runner) — the test is that resolution WORKS, not that hardware is present.
real=$(ls /sys/class/drm 2>/dev/null | grep -m1 -- '-' || true)
if [ -n "$real" ]; then
    short=${real#*-}
    if "$BIN" --dry-run probe "$short" | grep -q "would write: detect"; then
        ok "probe resolves $short without writing"
    else
        bad "probe did not resolve the real connector $short"
    fi
else
    ok "no DRM connectors here; probe resolution not exercised"
fi

# ── Bootloaders ─────────────────────────────────────────────────────────────
#
# The reason these are fixture-driven rather than run against the real /boot:
# SynapseOS installs three bootloaders and every machine this project is
# developed on boots exactly one of them. The systemd-boot path shipped broken
# — the loop that was supposed to read loader/entries stopped one index short,
# and the "checked below" case was never written — and no amount of running it
# here would have caught that, because there is no systemd-boot here to run it
# against. SYN_SETTINGS_BOOT_ROOT exists so the suite can pose all three.
bootfx=$(mktemp -d)
trap 'rm -rf "$bootfx"' EXIT

mkdir -p "$bootfx/limine/boot" \
         "$bootfx/limbls/boot" \
         "$bootfx/limsnap/boot" \
         "$bootfx/grub/boot/grub" \
         "$bootfx/sdb/boot/loader/entries" \
         "$bootfx/bls/boot/loader/entries" \
         "$bootfx/multi/boot/grub" \
         "$bootfx/none/boot"

printf 'timeout: 5\n/+SynapseOS\n //SynapseOS\n kernel_path: boot():/vmlinuz-linux\n' \
    > "$bootfx/limine/boot/limine.conf"
printf 'menuentry "SynapseOS" {\n linux /vmlinuz-linux root=UUID=x\n}\n' \
    > "$bootfx/grub/boot/grub/grub.cfg"
printf 'title SynapseOS\nlinux /vmlinuz-linux\ninitrd /initramfs-linux.img\n' \
    > "$bootfx/sdb/boot/loader/entries/synapseos.conf"
printf 'timeout: 5\n' > "$bootfx/multi/boot/limine.conf"
printf 'menuentry x {}\n' > "$bootfx/multi/boot/grub/grub.cfg"

# The pure kernel-install layout. This entry names neither "vmlinuz" nor the
# package — only the kernel RELEASE — so matching on the image filename cannot
# find it at all. It is why the release is carried alongside.
# ⛔ THIS ASSIGNMENT USED TO KILL THE SUITE, SILENTLY, ON HALF THE MACHINES IT
# RAN ON. The loop's last command was `[ "$(cat pkgbase)" = linux ]`, so its
# exit status is the status of the LAST directory examined — and under
# `pipefail` a failing left-hand side fails the whole pipeline, which fails the
# command substitution, which under `set -e` ends the script on an assignment.
#
# It depends entirely on what /usr/lib/modules happens to be sorted like: with
# only `linux` installed the last iteration passes and nothing is wrong. Install
# linux-cachyos beside it — 7.1.8-1-cachyos sorts after 7.1.11-arch1-1 — and the
# last iteration is the one that does not match, the run stops HERE, and the
# ~120 checks below it simply never happen. No failure, no message: the suite
# prints everything above this line and exits 0 through a `| tail`.
#
# `continue` rather than `&&` so no iteration ends on a false test, and a
# trailing `|| true` because the answer being empty is a legitimate state.
run_rel=$(for d in /usr/lib/modules/*/; do
              [ -f "$d/pkgbase" ] || continue
              [ "$(cat "$d/pkgbase")" = linux ] || continue
              basename "$d"
          done | head -1) || true
if [ -n "${run_rel:-}" ]; then
    printf 'title SynapseOS\nlinux /b153/%s/linux\ninitrd /b153/%s/initrd\n' \
        "$run_rel" "$run_rel" > "$bootfx/bls/boot/loader/entries/b153-$run_rel.conf"
fi

# ⚠ The running release is PINNED to something no package can own.
#
# "running" takes precedence over the bootable check in the state ladder, so
# on a machine booted into plain `linux` — every box here — the bootable
# answer for `linux` is unobservable, and these assertions had to accept
# "running" as a pass. That made them vacuous exactly where they mattered: the
# prefix trap below could only ever be checked on a machine booted into
# something else, and on this one it FAILED and took the package build with
# it. Pinned, every assertion below means the same thing everywhere.
boot_state() {
    SYN_SETTINGS_BOOT_ROOT="$1" \
    SYN_SETTINGS_RUNNING_RELEASE="0.0.0-synsettings-test" \
        "$BIN" --rec kernel | awk -F'\t' '$1=="linux" {print $3}'
}

# All of it rests on the `linux` package being installed; where it is not,
# every state below is "not installed" and proves nothing. Skipped rather than
# failed, the same way the BLS fixture is.
if [ "$(boot_state "$bootfx/none")" = "not installed" ]; then
    linux_here=0
    ok "boot: no linux package here; bootloader matching not exercised"
else
    linux_here=1
    # The pin must actually have taken, or everything below silently reverts
    # to the coincidence it replaced.
    if [ "$(boot_state "$bootfx/none")" = "running" ]; then
        bad "boot: SYN_SETTINGS_RUNNING_RELEASE was ignored — the checks below are vacuous"
        linux_here=0
    fi
fi

# "Bootable" is now two states: an entry exists, and it is or is not the one
# the loader will pick. Both mean the entry was found, which is what these
# assert — the default itself is tested on its own below.
is_bootable() {
    case "$1" in
        "installed, bootable"|"installed, boots by default") return 0 ;;
        *) return 1 ;;
    esac
}

if [ "$linux_here" = 1 ]; then
    for fx in limine grub sdb; do
        if is_bootable "$(boot_state "$bootfx/$fx")"; then
            ok "boot: $fx entry naming vmlinuz-linux reads as bootable"
        else
            bad "boot: $fx did not see its own entry (got '$(boot_state "$bootfx/$fx")')"
        fi
    done
fi

# systemd-boot with ONLY a Boot Loader Spec entry. Skipped rather than failed
# where the linux package owns no module tree, since the fixture cannot then be
# written — the test is that release matching works, not that a kernel is here.
if [ -n "${run_rel:-}" ] && [ "$linux_here" = 1 ]; then
    if is_bootable "$(boot_state "$bootfx/bls")"; then
        ok "boot: a BLS entry naming only the release reads as bootable"
    else
        bad "boot: BLS entry missed (got '$(boot_state "$bootfx/bls")')"
    fi
else
    ok "boot: no linux module tree here; BLS release matching not exercised"
fi

# ── limine, as the GENERATOR actually writes it ─────────────────────────────
#
# The fixture above poses only the entry syn-install writes by hand. The entry
# limine-mkinitcpio-hook writes — the thing "Make bootable" installs — looks
# nothing like it: Boot Loader Spec paths, no "vmlinuz-" prefix, no release.
# So on 2026-08-12 the button installed the hook, the hook wrote the entry, and
# the pane still said NO BOOT ENTRY and offered to fix it again. Posing only
# the shape we already handled is the same hole the systemd-boot case had.
printf 'timeout: 5\n/+SynapseOS\ncomment: machine-id=b153\n  //linux\n  comment: Kernel version: %s\n  comment: kernel-id=linux \n  protocol: linux\n  module_path: boot():/b153/linux/initramfs#aaaa\n  path: boot():/b153/linux/vmlinuz#bbbb\n' \
    "${run_rel:-0-test}" > "$bootfx/limbls/boot/limine.conf"
if [ "$linux_here" = 1 ]; then
    if is_bootable "$(boot_state "$bootfx/limbls")"; then
        ok "boot: a limine entry written by limine-mkinitcpio-hook reads as bootable"
    else
        bad "boot: generated limine entry missed (got '$(boot_state "$bootfx/limbls")')"
    fi
fi

# THE SNAPSHOT COMMENT. With limine-snapper-sync installed, limine.conf also
# carries a snapshot list, and each snapshot's comment is the pacman command
# line that made it — "install linux linux-headers". That names the package
# while booting a DIFFERENT kernel, so any whole-file search for the package
# name calls the wrong thing bootable. Only an image line counts.
printf 'timeout: 5\n/+SynapseOS\n  //linux-lts\n  comment: kernel-id=linux-lts \n  path: boot():/b153/linux-lts/vmlinuz#cccc\n     //Snapshots\n     ///620 | 2026-08-12 18:04:06\n     comment: /usr/bin/synpkg --noconfirm install linux linux-headers\n     ////SynapseOS\n     kernel_path: boot():/b153/limine_history/vmlinuz-linux-lts_sha256_dddd\n' \
    > "$bootfx/limsnap/boot/limine.conf"
if [ "$linux_here" != 1 ]; then
    ok "boot: no linux package here; the snapshot-comment trap is not exercised"
elif [ "$(boot_state "$bootfx/limsnap")" = "installed, NO BOOT ENTRY" ]; then
    ok "boot: a snapper comment naming the package is not a boot entry"
else
    bad "boot: snapshot comment read as an entry (got '$(boot_state "$bootfx/limsnap")')"
fi

# THE PREFIX TRAP. "vmlinuz-linux" is a prefix of "vmlinuz-linux-lts", so an
# unanchored search reports the stock kernel bootable on the strength of an LTS
# entry — the pane would say "bootable" about the one kernel that is not.
printf 'menuentry "SynapseOS" {\n linux /vmlinuz-linux-lts root=UUID=x\n}\n' \
    > "$bootfx/grub/boot/grub/grub.cfg"
if [ "$linux_here" != 1 ]; then
    ok "boot: no linux package here; the prefix trap is not exercised"
elif [ "$(boot_state "$bootfx/grub")" = "installed, NO BOOT ENTRY" ]; then
    ok "boot: an LTS-only entry does not make plain linux look bootable"
else
    bad "boot: prefix match leaked (got '$(boot_state "$bootfx/grub")')"
fi

# Nothing configured is an ANSWER, not a crash.
rc=0; SYN_SETTINGS_BOOT_ROOT="$bootfx/none" "$BIN" --rec kernel >/dev/null 2>&1 || rc=$?
if [ "$rc" -eq 0 ]; then
    ok "boot: a machine with no bootloader config still renders the pane"
else
    bad "boot: no-bootloader machine failed the pane (exit $rc)"
fi

# ── Which kernel boots by DEFAULT ───────────────────────────────────────────
#
# Bootable was only half of it: this box had linux-cachyos installed, with an
# entry, and limine still picking the stock kernel every time. Each loader
# keeps the answer somewhere different, and for two of the three the place it
# is READ from is not the place their own tool WRITES it — so all three are
# posed from fixtures, including the systemd-boot EFI variable, which no
# machine here can produce.
mkdir -p "$bootfx/limdef/boot" "$bootfx/limidx/boot" \
         "$bootfx/sdbdef/boot/loader/entries" \
         "$bootfx/sdbvar/boot/loader/entries" \
         "$bootfx/sdbvar/sys/firmware/efi/efivars" \
         "$bootfx/grubdef/boot/grub" "$bootfx/grubdef/etc/default"

# limine: default_entry names an entry by PATH. Two entries, and the one the
# path names is NOT the first — an index-based reading would answer the other.
lim_two() {
    printf 'timeout: 5\n%s/+SynapseOS\n  //linux-lts\n  path: boot():/b1/linux-lts/vmlinuz#a\n  //linux\n  path: boot():/b1/linux/vmlinuz#b\n' "$1"
}
lim_two "default_entry: SynapseOS/linux
" > "$bootfx/limdef/boot/limine.conf"
lim_two "" > "$bootfx/limidx/boot/limine.conf"

if [ "$linux_here" = 1 ]; then
    if [ "$(boot_state "$bootfx/limdef")" = "installed, boots by default" ]; then
        ok "default: limine default_entry by path picks the named entry"
    else
        bad "default: limine path default missed (got '$(boot_state "$bootfx/limdef")')"
    fi

    # No default_entry at all is limine's documented "entry 1", and entry 1
    # here is linux-lts. Reading the file and finding nothing must mean THAT,
    # not "the kernel you asked about".
    if [ "$(boot_state "$bootfx/limidx")" = "installed, bootable" ]; then
        ok "default: no default_entry means entry 1, which is the other kernel"
    else
        bad "default: absent default_entry mis-read (got '$(boot_state "$bootfx/limidx")')"
    fi

    # ROUND TRIP. The write is the half that cannot be checked by reading, and
    # a default written in a form the reader does not accept is exactly the
    # bug this pane shipped in pkgrel 16.
    if SYN_SETTINGS_BOOT_ROOT="$bootfx/limidx" "$BIN" default linux --as-root >/dev/null 2>&1 &&
       [ "$(boot_state "$bootfx/limidx")" = "installed, boots by default" ]; then
        ok "default: limine write is read back as the default it set"
    else
        bad "default: limine round trip failed (got '$(boot_state "$bootfx/limidx")')"
    fi

    # ...and it stays ONE line, however many times it is set.
    lines=$(grep -c '^default_entry:' "$bootfx/limidx/boot/limine.conf")
    SYN_SETTINGS_BOOT_ROOT="$bootfx/limidx" "$BIN" default linux --as-root >/dev/null 2>&1
    lines2=$(grep -c '^default_entry:' "$bootfx/limidx/boot/limine.conf")
    if [ "$lines" = 1 ] && [ "$lines2" = 1 ]; then
        ok "default: setting it twice leaves exactly one default_entry"
    else
        bad "default: default_entry accumulated ($lines then $lines2)"
    fi

    # The mode survives, for the same reason it must in /etc/default/grub: the
    # file is REPLACED by a rename, so the temp file's mode becomes the boot
    # menu's, and a temp file's mode is the umask's. Run under umask 077, which
    # is the case that shows it.
    chmod 0644 "$bootfx/limidx/boot/limine.conf"
    ( umask 077
      SYN_SETTINGS_BOOT_ROOT="$bootfx/limidx" "$BIN" default linux-lts --as-root >/dev/null 2>&1 )
    mode=$(stat -c %a "$bootfx/limidx/boot/limine.conf")
    if [ "$mode" = 644 ]; then
        ok "default: limine.conf keeps its mode through the rewrite"
    else
        bad "default: limine.conf came back as $mode, not 644"
    fi
fi

# systemd-boot: loader.conf's `default`, and the EFI variable that OVERRIDES
# it. bootctl set-default writes the variable, so a pane that read only
# loader.conf would never see its own write.
printf 'title SynapseOS\nlinux /vmlinuz-linux\n' \
    > "$bootfx/sdbdef/boot/loader/entries/arch-linux.conf"
printf 'title LTS\nlinux /vmlinuz-linux-lts\n' \
    > "$bootfx/sdbdef/boot/loader/entries/arch-lts.conf"
printf 'timeout 4\ndefault arch-linux\n' > "$bootfx/sdbdef/boot/loader/loader.conf"

cp -r "$bootfx/sdbdef/boot/loader/entries/." "$bootfx/sdbvar/boot/loader/entries/"
mkdir -p "$bootfx/sdbvar/boot/loader"
printf 'timeout 4\ndefault arch-linux\n' > "$bootfx/sdbvar/boot/loader/loader.conf"
# 4 bytes of attributes then UTF-16LE, which is the on-disk shape of an EFI
# variable — written here rather than described, because "we assumed the
# encoding" is how a reader confidently reports the wrong entry.
printf '\007\000\000\000' > "$bootfx/sdbvar/sys/firmware/efi/efivars/LoaderEntryDefault-4a67b082-0a4c-41cf-b6c7-440b29bb8c4f"
printf 'arch-lts.conf' | sed 's/./&\x00/g' \
    >> "$bootfx/sdbvar/sys/firmware/efi/efivars/LoaderEntryDefault-4a67b082-0a4c-41cf-b6c7-440b29bb8c4f"

if [ "$linux_here" = 1 ]; then
    if [ "$(boot_state "$bootfx/sdbdef")" = "installed, boots by default" ]; then
        ok "default: systemd-boot reads loader.conf's default"
    else
        bad "default: loader.conf default missed (got '$(boot_state "$bootfx/sdbdef")')"
    fi
    # The variable says LTS, loader.conf says linux. The variable wins.
    if [ "$(boot_state "$bootfx/sdbvar")" = "installed, bootable" ]; then
        ok "default: LoaderEntryDefault overrides loader.conf"
    else
        bad "default: the EFI variable was ignored (got '$(boot_state "$bootfx/sdbvar")')"
    fi
fi

# grub: saved_entry in grubenv, which counts only when GRUB_DEFAULT=saved.
printf "menuentry 'SynapseOS' --class arch \$menuentry_id_option 'gnulinux-simple-x' {\n linux /vmlinuz-linux root=UUID=x\n}\nmenuentry 'LTS' \$menuentry_id_option 'gnulinux-lts-x' {\n linux /vmlinuz-linux-lts root=UUID=x\n}\n" \
    > "$bootfx/grubdef/boot/grub/grub.cfg"
printf 'saved_entry=gnulinux-simple-x\n' > "$bootfx/grubdef/boot/grub/grubenv"
printf 'GRUB_DEFAULT=saved\nGRUB_TIMEOUT=5\n' > "$bootfx/grubdef/etc/default/grub"

if [ "$linux_here" = 1 ]; then
    if [ "$(boot_state "$bootfx/grubdef")" = "installed, boots by default" ]; then
        ok "default: grub reads saved_entry when GRUB_DEFAULT=saved"
    else
        bad "default: grub saved_entry missed (got '$(boot_state "$bootfx/grubdef")')"
    fi

    # GRUB_DEFAULT=0 means grubenv is not consulted at all, so saved_entry
    # naming this kernel must NOT make it the default — that is a write that
    # would report success and change nothing that boots.
    printf 'GRUB_DEFAULT=1\n' > "$bootfx/grubdef/etc/default/grub"
    if [ "$(boot_state "$bootfx/grubdef")" = "installed, bootable" ]; then
        ok "default: grub ignores saved_entry unless GRUB_DEFAULT=saved"
    else
        bad "default: grub read grubenv it should not have (got '$(boot_state "$bootfx/grubdef")')"
    fi

fi

# ── grub, written the whole way through ─────────────────────────────────────
#
# Three privileged acts in an order that MATTERS, so they run in one child and
# are tested as one: GRUB_DEFAULT is read by grub-mkconfig at generation time,
# not by grub at boot, and the entry id has to come out of the grub.cfg that
# mkconfig has just written. Getting the order wrong produces a write that
# succeeds, reports success and changes nothing that boots.
#
# grub-mkconfig and grub-set-default are STUBS on PATH: no machine here boots
# GRUB, the real ones need root and probe every disk, and what is being tested
# is what this app does — the arguments it passes and the file it edits.
mkdir -p "$bootfx/grubw/boot/grub" "$bootfx/grubw/etc/default" "$bootfx/grubw/bin"
printf "menuentry 'SynapseOS' \$menuentry_id_option 'gnulinux-simple-x' {\n linux /vmlinuz-linux root=UUID=x\n}\nmenuentry 'Other' \$menuentry_id_option 'gnulinux-lts-x' {\n linux /vmlinuz-linux-lts root=UUID=x\n}\n" \
    > "$bootfx/grubw/boot/grub/grub.cfg"
printf '# hand-written, and it stays that way\nGRUB_DEFAULT=0\nGRUB_TIMEOUT=5\nGRUB_CMDLINE_LINUX_DEFAULT="quiet splash"\n' \
    > "$bootfx/grubw/etc/default/grub"
for stub in grub-mkconfig grub-set-default; do
    printf '#!/bin/sh\necho "%s $*" >> "$STUBLOG"\n' "$stub" > "$bootfx/grubw/bin/$stub"
    chmod +x "$bootfx/grubw/bin/$stub"
done

if [ "$linux_here" = 1 ]; then
    export STUBLOG="$bootfx/grubw/stub.log"
    : > "$STUBLOG"
    if SYN_SETTINGS_BOOT_ROOT="$bootfx/grubw" PATH="$bootfx/grubw/bin:$PATH" \
       "$BIN" default linux --as-root >/dev/null 2>&1; then
        ok "default: the grub write ran to completion"
    else
        bad "default: the grub write failed"
    fi

    if grep -q '^GRUB_DEFAULT=saved$' "$bootfx/grubw/etc/default/grub"; then
        ok "default: GRUB_DEFAULT=saved is written into /etc/default/grub"
    else
        bad "default: GRUB_DEFAULT was not set to saved"
    fi

    # The rest of that file is somebody's, and this app is a guest in it.
    if grep -q '^GRUB_TIMEOUT=5$' "$bootfx/grubw/etc/default/grub" &&
       grep -q 'quiet splash' "$bootfx/grubw/etc/default/grub" &&
       grep -q '^# hand-written' "$bootfx/grubw/etc/default/grub" &&
       [ "$(grep -c '^GRUB_DEFAULT=' "$bootfx/grubw/etc/default/grub")" = 1 ]; then
        ok "default: every other line of /etc/default/grub survives, exactly once"
    else
        bad "default: /etc/default/grub was mangled"
    fi

    # ORDER. mkconfig before set-default, or the saved entry is written into a
    # grub.cfg that has not been told to read it.
    if [ "$(sed -n 1p "$STUBLOG")" = "grub-mkconfig -o $bootfx/grubw/boot/grub/grub.cfg" ]; then
        ok "default: grub.cfg is regenerated FIRST, to the config that was found"
    else
        bad "default: wrong first step ($(sed -n 1p "$STUBLOG"))"
    fi
    if [ "$(sed -n 2p "$STUBLOG")" = "grub-set-default gnulinux-simple-x" ]; then
        ok "default: grub-set-default gets the id of the entry naming that kernel"
    else
        bad "default: wrong saved entry ($(sed -n 2p "$STUBLOG"))"
    fi

    # Idempotent: a second run must not append a second GRUB_DEFAULT.
    SYN_SETTINGS_BOOT_ROOT="$bootfx/grubw" PATH="$bootfx/grubw/bin:$PATH" \
        "$BIN" default linux --as-root >/dev/null 2>&1
    if [ "$(grep -c '^GRUB_DEFAULT=' "$bootfx/grubw/etc/default/grub")" = 1 ]; then
        ok "default: setting it twice leaves one GRUB_DEFAULT"
    else
        bad "default: GRUB_DEFAULT accumulated"
    fi

    # ── the file's MODE survives ────────────────────────────────────────────
    #
    # /etc/default/grub is replaced by a rename, so the TEMP file's mode
    # becomes the config's — and a temp file's mode comes from the umask of
    # whoever is running this, not from the file being replaced. Under a
    # restrictive umask that silently hands the config back at 0600.
    #
    # Run under `umask 077` on purpose: that is the case where getting it wrong
    # is visible, and a test at the developer's own 022 would pass either way.
    # The mode is copied fstat→fchmod, from the descriptor that was actually
    # read to the one about to be renamed over it — never stat(path) then
    # chmod(tmp), which is a time-of-check/time-of-use race (CWE-367): between
    # the two calls the name can come to mean a different file.
    printf 'GRUB_DEFAULT=0\nGRUB_TIMEOUT=5\n' > "$bootfx/grubw/etc/default/grub"
    chmod 0640 "$bootfx/grubw/etc/default/grub"
    ( umask 077
      SYN_SETTINGS_BOOT_ROOT="$bootfx/grubw" PATH="$bootfx/grubw/bin:$PATH" \
          "$BIN" default linux --as-root >/dev/null 2>&1 )
    mode=$(stat -c %a "$bootfx/grubw/etc/default/grub")
    if [ "$mode" = 640 ]; then
        ok "default: /etc/default/grub keeps its mode through the rewrite"
    else
        bad "default: /etc/default/grub came back as $mode, not 640"
    fi

    # A COMMENTED example is not a setting, and turning one into a live line is
    # a change nobody made.
    printf '#GRUB_DEFAULT=0\nGRUB_TIMEOUT=5\n' > "$bootfx/grubw/etc/default/grub"
    SYN_SETTINGS_BOOT_ROOT="$bootfx/grubw" PATH="$bootfx/grubw/bin:$PATH" \
        "$BIN" default linux --as-root >/dev/null 2>&1
    if grep -q '^#GRUB_DEFAULT=0$' "$bootfx/grubw/etc/default/grub" &&
       grep -q '^GRUB_DEFAULT=saved$' "$bootfx/grubw/etc/default/grub"; then
        ok "default: a commented GRUB_DEFAULT is left alone and a real one added"
    else
        bad "default: the commented example was rewritten"
    fi

    # The dialogue must SHOW those three acts. "pkexec syn-settings --as-root"
    # is a true description of what runs and says nothing about what it does.
    plan=$(SYN_SETTINGS_BOOT_ROOT="$bootfx/grubw" PATH="$bootfx/grubw/bin:$PATH" \
           "$BIN" -n default linux 2>/dev/null)
    if printf '%s' "$plan" | grep -q '^step1.*GRUB_DEFAULT=saved' &&
       printf '%s' "$plan" | grep -q '^step2.*grub-mkconfig' &&
       printf '%s' "$plan" | grep -q '^step3.*grub-set-default'; then
        ok "default: the confirmation lists all three privileged steps"
    else
        bad "default: the confirmation hid what the child would do"
    fi

    # ...and asking what it would do must not DO it.
    printf 'GRUB_DEFAULT=0\n' > "$bootfx/grubw/etc/default/grub"
    SYN_SETTINGS_BOOT_ROOT="$bootfx/grubw" PATH="$bootfx/grubw/bin:$PATH" \
        "$BIN" -n default linux >/dev/null 2>&1
    if grep -q '^GRUB_DEFAULT=0$' "$bootfx/grubw/etc/default/grub"; then
        ok "default: --dry-run changes nothing"
    else
        bad "default: --dry-run edited /etc/default/grub"
    fi
    unset STUBLOG
fi

# ── Bluetooth on a machine that has none ────────────────────────────────────
#
# THE HANG THAT STOPPED A BUILD. `bluetoothctl show` talks to BlueZ over D-Bus,
# and on a machine with no adapter — every VM, and plenty of desktops — the
# service is inactive and that call NEVER RETURNS. It wedged the Bluetooth
# pane, and through the pane this suite, and through the suite a package build:
# 934 seconds and rising, zero I/O, stuck on one read.
#
# Every box this project is developed on has Bluetooth, which is exactly why
# the fixture root exists: the case that breaks is the one the hardware here
# cannot show.
btfx=$(mktemp -d)
mkdir -p "$btfx/sys/class"          # no bluetooth subsystem at all
out=$(SYN_SETTINGS_SYS_ROOT="$btfx" "$BIN" --rec bluetooth 2>/dev/null)
if printf '%s' "$out" | grep -q 'no adapter'; then
    ok "bluetooth: no adapter is reported, not asked about"
else
    bad "bluetooth: a machine with no adapter did not say so"
fi

mkdir -p "$btfx/sys/class/bluetooth"   # subsystem present, still no hardware
out=$(SYN_SETTINGS_SYS_ROOT="$btfx" "$BIN" --rec bluetooth 2>/dev/null)
printf '%s' "$out" | grep -q 'no adapter' \
    && ok "bluetooth: an empty subsystem is no adapter either" \
    || bad "bluetooth: an empty /sys/class/bluetooth was treated as an adapter"

# And the belt to that pair of braces: a command that never answers must be
# GIVEN UP ON rather than waited for. Posed with an adapter present and a
# bluetoothctl that sleeps, at a budget short enough to test.
mkdir -p "$btfx/sys/class/bluetooth/hci0" "$btfx/bin"
printf '#!/bin/sh\nsleep 3600\n' > "$btfx/bin/bluetoothctl"
chmod +x "$btfx/bin/bluetoothctl"
start=$SECONDS
err=$(SYN_SETTINGS_SYS_ROOT="$btfx" SYN_SETTINGS_CMD_TIMEOUT_MS=400 \
      PATH="$btfx/bin:$PATH" "$BIN" --rec bluetooth 2>&1 >/dev/null)
took=$((SECONDS - start))
if printf '%s' "$err" | grep -q "did not answer" && [ "$took" -lt 10 ]; then
    ok "a command that never answers is given up on, and says so"
else
    bad "a hanging command was waited for (${took}s, stderr: ${err:-none})"
fi
rm -rf "$btfx"

# ── A config this pane may not READ ─────────────────────────────────────────
#
# THE BUG THESE EXIST FOR. grub-mkconfig writes grub.cfg inside an
# unconditional `umask 077`, and syn-install creates it fresh in the chroot, so
# /boot/grub/grub.cfg is 0600 root:root on every GRUB install — while the pane
# reads it as the user. fopen returned NULL, the scanners called that "no", and
# the pane reported NO BOOT ENTRY about the kernel the machine was RUNNING,
# offered Make bootable for ever, and never once offered Make default, which is
# gated on a definite yes.
#
# Every fixture in this file is mode 0644, which is exactly why the suite could
# not see it: a permission the tests never pose cannot fail.
mkdir -p "$bootfx/grubperm/boot/grub" "$bootfx/grubperm/etc/default" \
         "$bootfx/grubperm/bin"
printf "menuentry 'SynapseOS' \$menuentry_id_option 'gnulinux-simple-x' {\n linux /boot/vmlinuz-linux root=UUID=x\n}\n" \
    > "$bootfx/grubperm/boot/grub/grub.cfg"
printf 'GRUB_DEFAULT=0\n' > "$bootfx/grubperm/etc/default/grub"
for stub in grub-mkconfig grub-set-default; do
    printf '#!/bin/sh\nexit 0\n' > "$bootfx/grubperm/bin/$stub"
    chmod +x "$bootfx/grubperm/bin/$stub"
done

# Mode 000 is how an unprivileged reader experiences a root-only file. It is
# not how ROOT experiences one — root ignores the bits entirely — so the
# privileged half is posed separately, at 0600, further down.
if [ "$linux_here" = 1 ] && [ "$(id -u)" != 0 ]; then
    chmod 000 "$bootfx/grubperm/boot/grub/grub.cfg"
    st=$(boot_state "$bootfx/grubperm")
    if [ "$st" = "installed, ENTRY UNKNOWN" ]; then
        ok "boot: a config it may not read is not a config that says no"
    else
        bad "boot: unreadable config reported as '$st'"
    fi

    # And the row must still offer BOTH actions: on this very machine the
    # action is what makes the config readable, so withholding it until the
    # config can be read is a lock whose key is inside it.
    act=$(SYN_SETTINGS_BOOT_ROOT="$bootfx/grubperm" \
          SYN_SETTINGS_RUNNING_RELEASE="0.0.0-synsettings-test" \
          "$BIN" --rec kernel | awk -F'\t' '$1=="linux" {print $5}')
    if printf '%s' "$act" | grep -q 'boot:linux' &&
       printf '%s' "$act" | grep -q 'default:linux'; then
        ok "boot: a blind row still offers the actions that would fix it"
    else
        bad "boot: blind row offered '$act'"
    fi

    # The dialogue must NAME the mode change. It is a privileged act, and the
    # dry-run that fills the dialogue runs unprivileged against the one file it
    # cannot read — so the step is stated with its condition rather than
    # dropped from the list.
    if SYN_SETTINGS_BOOT_ROOT="$bootfx/grubperm" PATH="$bootfx/grubperm/bin:$PATH" \
       "$BIN" -n boot linux 2>/dev/null | grep -q '^step2	would write: mode 0644'; then
        ok "boot: the confirmation names the mode change too"
    else
        bad "boot: the mode change was hidden from the confirmation"
    fi
else
    ok "boot: skipped the unreadable-config checks (root, or no linux package)"
fi

# The repair, from the privileged half. 0600 is the faithful pose here: root
# can read such a file, and the question is whether it puts the mode right.
if [ "$linux_here" = 1 ]; then
    chmod 600 "$bootfx/grubperm/boot/grub/grub.cfg"
    SYN_SETTINGS_BOOT_ROOT="$bootfx/grubperm" PATH="$bootfx/grubperm/bin:$PATH" \
        "$BIN" boot linux --as-root >/dev/null 2>&1
    mode=$(stat -c %a "$bootfx/grubperm/boot/grub/grub.cfg")
    if [ "$mode" = 644 ]; then
        ok "boot: the privileged half makes grub.cfg readable"
    else
        bad "boot: grub.cfg left at $mode — the pane stays blind"
    fi

    # Once, and it STAYS: grub-mkconfig's last act truncates the existing file
    # in place, which keeps the mode it already has. Only a file it has to
    # CREATE comes back at 0600.
    printf "menuentry 'SynapseOS' \$menuentry_id_option 'gnulinux-simple-x' {\n linux /boot/vmlinuz-linux root=UUID=x\n}\n" \
        > "$bootfx/grubperm/boot/grub/grub.cfg.new"
    ( umask 077
      cat "$bootfx/grubperm/boot/grub/grub.cfg.new" \
        > "$bootfx/grubperm/boot/grub/grub.cfg" )
    rm -f "$bootfx/grubperm/boot/grub/grub.cfg.new"
    if [ "$(stat -c %a "$bootfx/grubperm/boot/grub/grub.cfg")" = 644 ]; then
        ok "boot: the widened mode survives a regeneration"
    else
        bad "boot: a regeneration took the mode back"
    fi

    # NOT DONE BLIND. A configured GRUB password lives in grub.cfg as a
    # password_pbkdf2 hash; publishing that to every local account is a real
    # loss for a pane's convenience.
    printf "menuentry 'x' \$menuentry_id_option 'y' {\n linux /boot/vmlinuz-linux\n}\npassword_pbkdf2 root grub.pbkdf2.sha512.10000.DEADBEEF\n" \
        > "$bootfx/grubperm/boot/grub/grub.cfg"
    chmod 600 "$bootfx/grubperm/boot/grub/grub.cfg"
    out=$(SYN_SETTINGS_BOOT_ROOT="$bootfx/grubperm" PATH="$bootfx/grubperm/bin:$PATH" \
          "$BIN" boot linux --as-root 2>&1)
    if [ "$(stat -c %a "$bootfx/grubperm/boot/grub/grub.cfg")" = 600 ] &&
       printf '%s' "$out" | grep -q 'GRUB password'; then
        ok "boot: a configured GRUB password keeps grub.cfg root-only, and says so"
    else
        bad "boot: published a grub.cfg carrying a password hash"
    fi
fi

# Each loader gets ITS OWN mechanism, and none of them is the other's.
for pair in "limdef limine default_entry" "sdbdef systemd-boot bootctl" ; do
    set -- $pair
    if SYN_SETTINGS_BOOT_ROOT="$bootfx/$1" "$BIN" -n default linux 2>/dev/null | grep -q "$3"; then
        ok "default: $2 is set through $3"
    else
        bad "default: $2 did not name $3"
    fi
done

# The confirmation gate is the C binary's, not the dialogue's.
rc=0; SYN_SETTINGS_BOOT_ROOT="$bootfx/limdef" "$BIN" default linux >/dev/null 2>&1 || rc=$?
if [ "$rc" -eq 2 ]; then
    ok "default: refuses to change the default without --confirm"
else
    bad "default: changed the default without --confirm (exit $rc)"
fi

# ── The actions a kernel row offers MATCH ITS STATE ─────────────────────────
#
# The pane emitted one token, `pkg:<name>`, for every row that was not asking
# to be made bootable, and the GUI answered it by drawing an Install button AND
# a Remove button — so a kernel that was not installed offered to remove
# itself, and an installed one offered to install itself again. Reported
# 2026-08-12: "the install and remove button disregard actual state".
#
# Read straight off the real pane: every row's state and its actions have to
# agree, whatever this machine happens to have installed.
kact_fail=""
while IFS=$'\t' read -r kname _ kstate _ kaction; do
    case "$kname" in -|kernel) continue ;; esac
    case "$kstate" in
        "not installed")
            case " $kaction " in
                *" remove:"*) kact_fail="$kname is not installed and offers remove" ;;
            esac
            case " $kaction " in
                *" install:$kname "*) ;;
                *) kact_fail="$kname is not installed and does not offer install" ;;
            esac ;;
        running)
            case " $kaction " in
                *" remove:"*) kact_fail="$kname is RUNNING and offers remove" ;;
            esac ;;
        installed*)
            case " $kaction " in
                *" install:"*) kact_fail="$kname is installed and offers install" ;;
            esac
            case " $kaction " in
                *" remove:$kname "*) ;;
                *) kact_fail="$kname is installed, not running, and does not offer remove" ;;
            esac ;;
    esac
done < <("$BIN" --rec kernel | awk -F'\t' 'NR>1 {print $1"\t"$2"\t"$3"\t"$4"\t "$5" "}')
if [ -z "$kact_fail" ]; then
    ok "kernel: every row's actions match its state"
else
    bad "kernel: $kact_fail"
fi

# ── The confirmation gate ───────────────────────────────────────────────────
#
# The GUI shows a dialogue, but the binary is the boundary. A confirmation that
# only exists in QML is one that anything else can skip, so it is enforced here
# and tested here. Without --confirm nothing may run, and the exit must be 2
# (refused) rather than 0 with a silent no-op.
rc=0
SYN_SETTINGS_BOOT_ROOT="$bootfx/grub" "$BIN" boot linux >/dev/null 2>&1 || rc=$?
if [ "$rc" -eq 2 ]; then
    ok "boot: refuses to change anything without --confirm"
else
    bad "boot: ran without --confirm (exit $rc)"
fi

# And the refusal must SAY what it would have done, or the GUI has nothing to
# put in the dialogue and the user has nothing to approve.
#
# Captured into a variable rather than piped into grep: this command exits 2 on
# purpose, `set -o pipefail` is on, and a pipeline reports the FIRST non-zero
# status — so `"$BIN" boot linux | grep -q x` returns 2 even when grep matched,
# and the check fails for a reason that has nothing to do with the output.
refusal=$(SYN_SETTINGS_BOOT_ROOT="$bootfx/grub" "$BIN" boot linux 2>&1 || true)
if grep -qF 'grub-mkconfig' <<<"$refusal"; then
    ok "boot: the refusal names the command it would run"
else
    bad "boot: the refusal did not name the command"
fi

# Each bootloader gets ITS OWN mechanism. Getting this wrong means running
# grub-mkconfig on a limine machine, which succeeds and changes nothing.
#
# grub goes through this binary under pkexec rather than calling grub-mkconfig
# directly, because generating the config is only half of the act — see the
# mode work below — so what the dialogue must show is the STEP, not the
# command. The step is produced by the code that performs it.
if SYN_SETTINGS_BOOT_ROOT="$bootfx/grub" "$BIN" -n boot linux \
     | grep -q '^step1	would run: grub-mkconfig -o '; then
    ok "boot: grub gets grub-mkconfig"
else
    bad "boot: wrong mechanism for grub"
fi
if [ -n "${run_rel:-}" ]; then
    if SYN_SETTINGS_BOOT_ROOT="$bootfx/sdb" "$BIN" -n boot linux \
         | grep -q '^command	pkexec kernel-install add '; then
        ok "boot: systemd-boot gets kernel-install"
    else
        bad "boot: wrong mechanism for systemd-boot"
    fi
fi
if SYN_SETTINGS_BOOT_ROOT="$bootfx/limine" "$BIN" -n boot linux \
     | grep -qE '^command	(pkexec limine-update|synpkg( --[a-z]+)* install limine-mkinitcpio-hook)'; then
    ok "boot: limine gets limine-update or the hook that provides it"
else
    bad "boot: wrong mechanism for limine"
fi

# ⚠ --noconfirm is LOAD-BEARING here for the same reason it is on `pkg install`
# — and this is the check that was missing when it was. The pattern above
# tolerates any flags, so it passed happily while "Make bootable" authenticated
# through polkit three times on 2026-08-12 and installed nothing: synpkg's
# confirm() refuses with no terminal to ask in, and a declined transaction was
# a zero exit. Only assert it on the branch that actually invokes synpkg — a
# machine with limine-update already installed builds a pkexec command instead.
bootcmd=$(SYN_SETTINGS_BOOT_ROOT="$bootfx/limine" "$BIN" -n boot linux | grep '^command	')
case "$bootcmd" in
*synpkg*)
    case "$bootcmd" in
    *"--noconfirm"*) ok "boot: the synpkg install passes --noconfirm" ;;
    *) bad "boot: synpkg install dropped --noconfirm — it will install nothing" ;;
    esac
    # Globals must precede the verb; synpkg stops parsing them at the first
    # non-option argument, so "install --noconfirm" is a flag it never sees.
    case "$bootcmd" in
    *"--noconfirm"*install*) ok "boot: --noconfirm comes before the verb" ;;
    *) bad "boot: --noconfirm lands after 'install', where synpkg ignores it" ;;
    esac
    ;;
*)
    skip "boot: limine-update is installed, so no synpkg install to check"
    ;;
esac

# With several bootloaders configured, picking one for the user means picking
# which config file to rewrite. Being wrong there is the failure this pane
# exists to prevent, so it must refuse rather than guess.
rc=0
SYN_SETTINGS_BOOT_ROOT="$bootfx/multi" "$BIN" boot linux --confirm >/dev/null 2>&1 || rc=$?
if [ "$rc" -eq 2 ]; then
    ok "boot: refuses to guess between two configured bootloaders"
else
    bad "boot: picked a bootloader on its own (exit $rc)"
fi
if SYN_SETTINGS_BOOT_ROOT="$bootfx/multi" "$BIN" -n boot linux --loader grub \
     | grep -q '^loader	grub'; then
    ok "boot: --loader resolves the ambiguity"
else
    bad "boot: --loader did not select the named bootloader"
fi

# boot is restricted to the kernels this pane manages, exactly as pkg is. A GUI
# row must never become a way to run a boot-config generator against an
# arbitrary name.
for evil in "firefox" "base" "../../etc/passwd" "linux-lts-evil"; do
    rc=0
    SYN_SETTINGS_BOOT_ROOT="$bootfx/grub" "$BIN" boot "$evil" --confirm >/dev/null 2>&1 || rc=$?
    if [ "$rc" -eq 2 ]; then
        ok "boot refused: $evil"
    else
        bad "boot accepted a name outside the kernel list: $evil (exit $rc)"
    fi
done

# ── settings this session cannot use ────────────────────────────────────────
#
# A handful of settings here are synui's alone — the clock format, the terminal
# synui launches — because they are written into synui's own configuration. On
# a SynapseOS box running KDE or GNOME the write succeeds and NOTHING changes:
# no part of that session reads the file. That is the worst failure a settings
# app has, and it is invisible, so it is tested rather than trusted.
#
# The environment is forced both ways. Testing only the current session would
# pass on any developer box and cover nothing: this machine is synui, so the
# blocked branch would never run.
#
# XDG_RUNTIME_DIR goes too. Without it the socket probe would still find a live
# synui through $XDG_RUNTIME_DIR/synui-0.sock and report "synui" no matter what
# XDG_CURRENT_DESKTOP says — the check would pass while testing nothing.
as_desktop() {
    env -u SYNUI_SOCKET -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR \
        XDG_CURRENT_DESKTOP="$1" "$BIN" "${@:2}"
}
cell() { awk -F'\t' -v k="$2" -v c="$3" '$1 == k { print $c }' <<<"$1"; }

for de in GNOME KDE; do
    t=$(as_desktop "$de" --rec time)
    n=0
    for key in time-format time-seconds date-format; do
        [ "$(cell "$t" "$key" 4)" = "unavailable:$de" ] && n=$((n + 1))
    done
    [ "$n" = 3 ] && ok "$de: all three clock knobs are marked unavailable" \
                 || bad "$de: only $n/3 clock knobs are marked unavailable"

    # The reason has to NAME the desktop. "synui only" alone leaves somebody
    # looking for a synui they are already running.
    case "$(cell "$t" time-format 3)" in
        *"$de"*) ok "$de: the reason names the desktop in the way" ;;
        *)       bad "$de: the detail column does not name $de" ;;
    esac

    a=$(as_desktop "$de" --rec apps)
    [ "$(cell "$a" Terminal 6)" = "unavailable:$de" ] \
        && ok "$de: the terminal role is marked unavailable" \
        || bad "$de: the terminal role still offers a write"

    # And ONLY that one. Every other role is decided by mimeapps.list, which
    # every desktop reads — greying those would be a regression that took a
    # working setting away from KDE and GNOME users.
    blocked=$(tail -n +2 <<<"$a" | awk -F'\t' '$6 ~ /^unavailable:/ { print $1 }' | tr '\n' ' ')
    [ "$blocked" = "Terminal " ] \
        && ok "$de: no other application role was greyed with it" \
        || bad "$de: greyed roles are '$blocked', expected only Terminal"

    # "not driven" claims the head is dark. Under another compositor that is
    # false — it is scanning out perfectly well, this just cannot see it — and
    # the protocol already has a value for "cannot be answered here": "-".
    d=$(as_desktop "$de" --rec display)
    if grep -q 'not driven' <<<"$d"; then
        bad "$de: the display pane still says 'not driven' for another compositor"
    else
        ok "$de: the compositor columns read '-' rather than claiming a dark head"
    fi
done

# Both names synui logs in under. greetd exports "synui"; a display manager
# reading the session file exports "SynapseOS". Matching one of them would grey
# every synui-only row on half of the login paths — and the half that broke
# would be whichever one the developer does not use.
for name in synui SynapseOS synui:wlroots ubuntu:SynapseOS; do
    v=$(cell "$(as_desktop "$name" --rec time)" time-format 4)
    [ "$v" = "choice:time-format" ] \
        && ok "XDG_CURRENT_DESKTOP=$name is recognised as synui" \
        || bad "XDG_CURRENT_DESKTOP=$name greyed a synui row ($v)"
done

# The distributions that prepend to the list prepend their OWN brand, so the
# desktop is the LAST component. Reading the first names "ubuntu" as the thing
# in the way, which is not a desktop anybody can go and change.
v=$(cell "$(as_desktop ubuntu:GNOME --rec time)" time-format 4)
[ "$v" = "unavailable:GNOME" ] \
    && ok "ubuntu:GNOME is named as GNOME, not as ubuntu" \
    || bad "ubuntu:GNOME resolved to '$v'"

# No graphical session at all is deliberately NOT a refusal: over SSH or on a
# TTY the write still lands in synui's config and applies at the next synui
# login, which is a reasonable thing to do. Greying it there would take the CLI
# away from the case a settings tool is most often needed in.
v=$(env -u SYNUI_SOCKET -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR \
        -u XDG_CURRENT_DESKTOP "$BIN" --rec time | awk -F'\t' '$1=="time-format"{print $4}')
[ "$v" = "choice:time-format" ] \
    && ok "a TTY with no desktop is not a refusal" \
    || bad "no desktop greyed a synui row ($v)"

# The CLI has no grey. It must SAY that the write it just made is not the one
# that changes the screen, or it reports success and does nothing visible.
out=$(as_desktop GNOME --dry-run set time-format 24 2>&1 || true)
case "$out" in
    *would\ write*) ok "--dry-run still changes nothing under another desktop" ;;
    *)              bad "dry-run set time-format printed: $out" ;;
esac

# ── the window follows the desktop font ─────────────────────────────────────
# This pane is where the font gets PICKED, so it is the one window that must
# never drift from font.state. The file carries the desktop's family AND its
# text scale; it is deliberately not a key in theme.json, because the font
# outlives a theme switch.
#
# Qt resolves an application's default font ONCE at startup, so both have to be
# BINDINGS on every Text. A bare `font.pixelSize: 13` or a literal family is the
# regression, and it fails silently — the window just stops moving with the
# desktop, which is how syn-arsenal and synpkg behaved until 2026-08-11.
# ── the assistant's backend and its API key ─────────────────────────────────
#
# `vibe provider` and `vibe key` have worked from a terminal for a while; this
# is the half that makes them reachable. velle, 2026-08-28: "add gui setup for
# cloud backends".
QMLA="$(dirname "$0")/../data/syn-settings.qml"
if [ -f "$QMLA" ]; then
    grep -q 'actionHas(root.selAction, "secret")' "$QMLA" \
        || bad "the QML has no button for the secret verb — the key row would
       highlight and nothing would happen"

    # ⛔ THE KEY MUST NOT REACH argv. /proc/<pid>/cmdline is world-readable;
    # /proc/<pid>/environ is 0400. A `set`-style write would put a live API key
    # where every account on the machine can read it.
    grep -q 'SYN_SETTINGS_SECRET' "$QMLA" \
        || bad "the key is not passed in the environment — if it went through
       runWrite() it would be an argv element, and world-readable while it ran"
    grep -q 'runSecretWrite(\["assistant-key"' "$QMLA" \
        || bad "the key button does not use runSecretWrite()"

    # ⛔ AND IT MUST NOT BE ON SCREEN, in a window people screenshot to ask for
    # help with it.
    grep -q 'TextInput.Password' "$QMLA" \
        || bad "the key field is not masked"

    # ⛔ NOR PREFILLED. A key row's `value` is where the key LIVES — "keyring",
    # "file" — never the key. Seeded into the box, the next Save stores that
    # word AS the key.
    grep -q 'actionHas(a, "secret") ? "" : root.selValue' "$QMLA" \
        || bad "the secret field is prefilled from the row's value — which is
       the word 'keyring', and Save would store it as the key"

    grep -qE '"enroll", *"forget", *"secret"' "$QMLA" \
        || bad "secret is not in applyBtn's exclusion list — the row would draw
       a meaningless second Apply beside Save key"
    ok "the key field is masked, empty, and never passed in argv"
fi

# The provider list is vibe's, and a name it does not know is a dead button.
refuses "assistant-backend refuses a provider vibe does not know" 2 \
        set assistant-backend not-a-provider
refuses "assistant-key refuses a provider that takes no key" 2 \
        assistant-key synapd
refuses "assistant-key with no provider is refused" 2 assistant-key

# ── the fingerprint pane ────────────────────────────────────────────────────
#
# ⛔ THIS BOX HAS NO READER AND NO fprintd, which is exactly why these are here.
# The generic "every verb the reader emits appears in the QML" check can only
# see what this machine's pane actually printed — one `unavailable:` row — so it
# proves nothing about `enroll:` and `forget:`, the two verbs that only appear
# in front of hardware. A dead button would ship unnoticed on the developer's
# desktop and be found by the one person with a ThinkPad.
QML="$(dirname "$0")/../data/syn-settings.qml"
if [ -f "$QML" ]; then
    grep -q 'actionHas(root.selAction, "enroll")' "$QML" \
        || bad "the QML has no button for the enroll verb — the row would
       highlight, the strip would open, and nothing would happen"
    grep -q 'actionHas(root.selAction, "forget")' "$QML" \
        || bad "the QML has no button for the forget verb"
    # ⚠ THE SECOND EDIT PEOPLE FORGET. A verb missing from this list draws
    # "Apply" as well as its own button, and Apply builds a command for a verb
    # it does not understand.
    grep -qE '"enroll", *"forget"' "$QML" \
        || bad "enroll/forget are not in applyBtn's exclusion list — the row
       would draw a second, meaningless Apply button beside the real one"
    ok "the fingerprint verbs have buttons and are excluded from Apply"
fi

# The argument is an allowlist, not a passthrough: it becomes an argv element.
#
refuses "enroll refuses a finger fprintd does not know" 2 enroll not-a-finger
refuses "enroll with no finger is refused"              2 enroll ""

# ⛔ `forget <finger>` MUST NOT LOOK LIKE IT WORKS. fprintd removes a user's
# prints together, so a per-finger spelling would be a command that silently
# destroys nine more than it names.
refuses "forget refuses a single finger, which it cannot do" 2 \
        forget right-index-finger

QML="$(dirname "$0")/../data/syn-settings.qml"
if [ -f "$QML" ]; then
    grep -q 'config/synui/font.state' "$QML" \
        && ok "the desktop font file is watched" \
        || bad "syn-settings.qml does not read font.state"
    grep -q 'root.textScale = s' "$QML" \
        && ok "the scale is read from the same file" \
        || bad "syn-settings.qml reads the family but not the scale"

    # awk rather than `grep -c ... | grep -vc ...`: grep exits 1 on no matches,
    # and under pipefail a correct zero would be read as a failed check.
    n=$(awk '/pixelSize: *[0-9]/ { n++ } END { print n + 0 }' "$QML")
    [ "$n" = 0 ] && ok "no pixel size bypasses ui()" \
                 || bad "$n pixel size(s) bypass ui()"

    n=$(awk '/family: *"/ && !/family: *"monospace"/ { n++ } END { print n + 0 }' "$QML")
    [ "$n" = 0 ] && ok "every literal family is the deliberate monospace" \
                 || bad "$n literal font family/families are not monospace"

    # Qt.application.font.family is the STARTUP font: as a fallback it freezes
    # the very thing this is fixing.
    # The comment at the top of check_actions says a verb the QML does not know
    # renders a dead button. This makes that a test rather than a warning: the
    # reader and the window are two files, and adding a verb to one of them is
    # exactly the kind of half-change that looks finished and clicks dead.
    verbs=$(
        for pane in display region network bluetooth power kernel apps time ai; do
            pout=$("$BIN" --rec "$pane") || continue
            pcol=$(head -1 <<<"$pout" | awk -F'\t' '{for(i=1;i<=NF;i++) if($i=="action") print i}')
            [ -n "$pcol" ] || continue
            tail -n +2 <<<"$pout" | awk -F'\t' -v c="$pcol" '{print $c}'
        done | tr ' ' '\n' | grep -E '^[a-z]+:' | cut -d: -f1 | sort -u)
    missing=""
    for v in $verbs; do
        grep -q "\"$v\"" "$QML" || missing="$missing $v"
    done
    [ -z "$missing" ] && ok "every action verb the reader emits appears in the QML" \
                      || bad "verb(s) the QML never mentions:$missing"

    # ── The firewall rows ───────────────────────────────────────────────────
    #
    # These replaced a row that said "ruleset: not read". That was honest —
    # `nft list ruleset` needs root — and useless: the box HAS a firewall,
    # synnet applies a default-drop input chain at every start, and the pane
    # could not say so. The rows read what synnet publishes instead.
    #
    # Every state is driven through the two file paths, because none of them
    # can be produced on demand on the machine running this: a failed apply, a
    # firewall rebuilt nine times, a daemon that has not published yet.
    fwdir=$(mktemp -d)
    fwstate="$fwdir/state"; fwpref="$fwdir/pref"
    fwrow() { SYNNET_FW_STATE_FILE="$fwstate" SYNNET_FW_PREF_FILE="$fwpref" \
              "$BIN" --rec network | grep "^firewall	input filtering"; }

    # ⚠ ABSENT MEANS ON. A pane that read a missing preference file as "off"
    # would report an unfiltered machine that is in fact filtered — the same
    # class of wrong answer these rows exist to remove. No preference file is
    # the normal state of every box that has never touched the setting.
    rm -f "$fwpref"
    printf 'state=active\nreasserts=0\n' > "$fwstate"
    case "$(fwrow | cut -f3)" in
        on) ok "an asserted firewall with no preference file reads as ON" ;;
        *)  bad "a firewall with no preference file read as '$(fwrow | cut -f3)'" ;;
    esac

    printf 'off\n' > "$fwpref"
    case "$(fwrow | cut -f3)" in
        off) ok "a preference of off reads as OFF even while the state says active" ;;
        *)   bad "the preference did not override the published state" ;;
    esac
    rm -f "$fwpref"

    printf 'state=failed\n' > "$fwstate"
    case "$(fwrow | cut -f3)" in
        failed) ok "a firewall that could not be applied reads as FAILED" ;;
        *)      bad "a failed apply read as '$(fwrow | cut -f3)'" ;;
    esac
    # ⚠ It must not read as "on" — that is the one wrong answer that matters,
    # and the difference between the two is a machine that is filtered and one
    # that only believes it is.
    case "$(fwrow | cut -f3)" in
        on) bad "a FAILED firewall reported itself as on" ;;
        *)  ok "…and never as on" ;;
    esac

    rm -f "$fwstate"
    case "$(fwrow | cut -f3)" in
        unknown) ok "no published state reads as unknown, not as on or off" ;;
        *)       bad "an unpublished state read as '$(fwrow | cut -f3)'" ;;
    esac

    # The rebuild counter is a row only when it has happened. A zero would be a
    # row about nothing.
    printf 'state=active\nreasserts=0\n' > "$fwstate"
    SYNNET_FW_STATE_FILE="$fwstate" SYNNET_FW_PREF_FILE="$fwpref" \
        "$BIN" --rec network | grep -q "^firewall	rebuilt" \
        && bad "a zero rebuild count still drew a row" \
        || ok "no rebuild row when the firewall has not been rebuilt"

    printf 'state=active\nreasserts=9\n' > "$fwstate"
    SYNNET_FW_STATE_FILE="$fwstate" SYNNET_FW_PREF_FILE="$fwpref" \
        "$BIN" --rec network | grep -q "^firewall	rebuilt	9" \
        && ok "a firewall that keeps vanishing gets a row with the count" \
        || bad "the rebuild count was not reported"

    # The row has to be actionable, or it is a status display wearing a
    # settings pane's clothes. `choice:` is the generic verb the QML already
    # knows, so no dead button — the verb sweep above covers that.
    printf 'state=active\nreasserts=0\n' > "$fwstate"
    case "$(fwrow | cut -f6)" in
        *choice:firewall*) ok "the firewall row offers the on/off choice" ;;
        *) bad "the firewall row carries no action: $(fwrow | cut -f6)" ;;
    esac

    # …and the choice has to have both options with exactly one current.
    cout=$(SYNNET_FW_PREF_FILE="$fwpref" "$BIN" choices firewall)
    n=$(grep -c 'current' <<<"$cout")
    [ "$n" = 1 ] && ok "exactly one firewall choice is marked current" \
                 || bad "$n firewall choices are marked current"
    grep -q '^on	' <<<"$cout" && grep -q '^off	' <<<"$cout" \
        && ok "both on and off are offered" \
        || bad "the firewall choice is missing an option"

    # ⚠ The labels say what HAPPENS. "Off" alone does not tell somebody their
    # laptop will start answering strangers; this is the one setting in the app
    # that makes the machine less safe than it shipped.
    grep '^off	' <<<"$cout" | grep -qi 'answer anything' \
        && ok "the off label says what turning it off does" \
        || bad "the off label does not describe the consequence"

    # ── The container / VM links row ────────────────────────────────────────
    #
    # THE BUG (2026-08-22): a Waydroid guest came up with no network. Its bridge
    # is 192.168.240.0/24, which the LAN-trust rule accepts — but the DHCP
    # request that gets it that address is sent from 0.0.0.0, matches nothing,
    # and the drop policy eats it. Nothing in any log mentions the firewall, so
    # this row is where somebody with a dead container network can see whether
    # the bridge has been named in /etc/synnet/trusted-ifaces.
    fwifaces="$fwdir/ifaces"
    fwlinks() { SYNNET_FW_STATE_FILE="$fwstate" SYNNET_FW_PREF_FILE="$fwpref" \
                SYNNET_FW_IFACES_FILE="$fwifaces" \
                "$BIN" --rec network | grep "^firewall	container links"; }

    printf 'state=active\nlinks=2\nreasserts=0\n' > "$fwstate"
    printf '# comment\nwaydroid0\n\n  virbr0  # trailing\n' > "$fwifaces"
    case "$(fwlinks | cut -f3)" in
        *waydroid0*virbr0*) ok "the links row lists the trusted bridges" ;;
        *) bad "the links row read '$(fwlinks | cut -f3)'" ;;
    esac
    # Comments and blanks are entries in the file, not interfaces.
    case "$(fwlinks | cut -f3)" in
        *comment*|*trailing*) bad "a comment was read as an interface name" ;;
        *) ok "…and reads the entries, not the comments around them" ;;
    esac

    rm -f "$fwifaces"
    case "$(fwlinks | cut -f3)" in
        none) ok "no file reads as none — the normal state of a box with no containers" ;;
        *)    bad "an absent list read as '$(fwlinks | cut -f3)'" ;;
    esac

    # ⚠ THE STALE CASE IS THE POINT OF THE WARN STATE. The daemon only rebuilds
    # a chain that has GONE; one that is merely out of date looks healthy to it,
    # so a bridge added by hand to the file is not in the kernel and the
    # container's DHCP is still being dropped. The row has to say so.
    printf 'waydroid0\nvirbr0\n' > "$fwifaces"
    printf 'state=active\nlinks=0\nreasserts=0\n' > "$fwstate"
    [ "$(fwlinks | cut -f4)" = warn ] \
        && ok "a list the firewall has not loaded yet is flagged" \
        || bad "a stale link list did not warn"

    printf 'state=active\nlinks=2\nreasserts=0\n' > "$fwstate"
    [ "$(fwlinks | cut -f4)" = warn ] \
        && bad "an up-to-date link list warned anyway" \
        || ok "…and an up-to-date one does not"

    # ⚠ A mismatch on a firewall that is OFF is not news, it is the definition
    # of off. Warning there sends somebody chasing a fault they created on
    # purpose.
    printf 'off\n' > "$fwpref"
    printf 'state=active\nlinks=0\nreasserts=0\n' > "$fwstate"
    [ "$(fwlinks | cut -f4)" = warn ] \
        && bad "a switched-off firewall warned about its link list" \
        || ok "…and a switched-off firewall does not warn about the list at all"
    rm -f "$fwpref"

    rm -rf "$fwdir"

    # `unavailable` is the one verb the loop above cannot catch on its own: it
    # is only emitted when the session is NOT synui, and the machine running
    # this suite is. So it is checked directly — and checked for the part that
    # actually greys the row, not merely for the word appearing somewhere.
    #
    # The trap this guards is specific. `actionable` is what draws the accent
    # edge, the pointing cursor and the editor strip, and it was
    # `rowAction(...) !== "-"`. An `unavailable:GNOME` cell is not "-", so
    # without the second half of that condition every blocked row would render
    # as a live control whose Apply button builds `syn-settings unavailable
    # GNOME` — a dead button, which is exactly what this column exists to
    # prevent.
    grep -q 'function rowBlocked' "$QML" \
        && ok "the window knows what a blocked row is" \
        || bad "syn-settings.qml has no rowBlocked()"
    grep -q '!== "-" && !dataRow.blocked' "$QML" \
        && ok "a blocked row is not actionable" \
        || bad "actionable does not exclude blocked rows — they render as live controls"

    n=$(grep -c 'Qt.application.font' "$QML" || true)
    [ "$n" = 0 ] && ok "no fallback pins the startup font" \
                 || bad "$n use(s) of Qt.application.font"
else
    bad "syn-settings.qml not found beside the tests: $QML"
fi

# ── the wallpaper's accent reaches this window ──────────────────────────────
#
# 387 gave the BAR the colour synui measures off the wallpaper, and only the
# bar: every app window beside it kept the preset's accent, so a desktop with
# the switch on wore two colours at once — the picture's on the bar, the
# theme's on Files, Software, Disks and the rest. These windows read
# ~/.config/synui/palette.state now, and this is the check that they do.
#
# ⚠ IT LOADS THE FILE IN A REAL ENGINE rather than grepping for the property.
# A duplicate property name is the trap this feature has sprung before: the
# file PARSES, qmllint is happy, and the type then refuses to LOAD, naming a
# line that is not the one at fault. Only running it can tell.
#
# Three cases, because two of them are the ones already got wrong once:
#   use=yes  the MEASURED colour;
#   use=no   the theme's own, because `use` is the SETTING and synui writes
#            the file whichever way it is set — reading the colour without
#            checking it is how the bar came to wear a wallpaper on themes
#            that never asked for one (386);
#   ok=no    the theme's own, the picture having no usable hue to give.
if [ -f "$QML" ] && command -v quickshell >/dev/null 2>&1; then
    WPT=$(mktemp -d)
    mkdir -p "$WPT/home/.config/synui" "$WPT/run"
    # A preset accent that is nothing like the measured one, so "it took the
    # wallpaper's" and "it kept the theme's" cannot be confused for each other.
    cat > "$WPT/home/.config/synui/theme.json" <<'WPJSON'
{ "scheme": "dark", "accent": [0,214,229], "glyph": [0,214,229],
  "bar": [25,28,35], "popup": [17,21,28], "fg": "#c8e3ee" }
WPJSON
    # A COPY with a probe timer appended INSIDE the root object — outside its
    # final brace the file is a syntax error and this would "fail" on a QML
    # that is perfectly good.
    awk 'BEGIN{RS="\0"} {
            n = match($0, /}[ \t\r\n]*$/)
            printf "%s\n    Timer { running: true; interval: 1200; repeat: false;\n             onTriggered: { console.log(\"WPACCENT=\" + root.cAccent); Qt.quit() } }\n%s", substr($0,1,n-1), substr($0,n)
         }' "$QML" > "$WPT/probe.qml"
    # ⚠ QT_ASSUME_STDERR_HAS_CONSOLE=1, or console.log() prints NOTHING at all
    # and every case below reads as an empty accent — a green suite that tested
    # the engine's silence. GSETTINGS_BACKEND=memory because the fake HOME has
    # no dconf for Qt's platform theme to find.
    wp_accent() {  # wp_accent <use> <ok> -> the colour the window resolves
        printf 'use=%s\nok=%s\naccent=#6479FF\naccent_dim=#37438C\nsecondary=#C68F14\n' \
               "$1" "$2" > "$WPT/home/.config/synui/palette.state"
        HOME="$WPT/home" XDG_RUNTIME_DIR="$WPT/run" QT_QPA_PLATFORM=offscreen \
        GSETTINGS_BACKEND=memory QT_ASSUME_STDERR_HAS_CONSOLE=1 \
        timeout 30 quickshell -p "$WPT/probe.qml" 2>&1 |
            sed -n 's/.*WPACCENT=\(#[0-9a-fA-F]*\).*/\1/p' | head -1
    }
    [ "$(wp_accent yes yes)" = "#6479ff" ] \
        && ok "the measured wallpaper accent reaches the window" \
        || bad "the window ignores palette.state and stays on the preset accent"
    [ "$(wp_accent no yes)" = "#00d6e5" ] \
        && ok "wallpaper_accent off leaves the theme's accent alone" \
        || bad "the window wears the wallpaper with use=no in palette.state"
    [ "$(wp_accent yes no)" = "#00d6e5" ] \
        && ok "a wallpaper with no usable hue falls back to the theme" \
        || bad "the window took a colour out of a palette.state saying ok=no"
    rm -rf "$WPT"
else
    echo "  skip  quickshell not installed, cannot check the wallpaper accent"
fi

# ── The machine's name ──────────────────────────────────────────────────────
#
# ⚠ Every SynapseOS install ships as `synapse`, so two on one network means
# Avahi renames one of them and the .local address stops being stable. The
# System pane offers the rename; hostnamectl does the polkit check.
#
# ⛔ DRY RUN ONLY. A test that renamed the machine running it would be a test
# nobody runs twice, and the interesting half is the VALIDATION anyway.
out=$("$BIN" --rec system)
if grep -q 'set:hostname' <<<"$out"; then
    ok "the System pane offers the hostname"
else
    bad "the System pane has no settable hostname"
fi

out=$("$BIN" -n set hostname studio-01 2>&1)
case $out in
    *"hostnamectl set-hostname studio-01"*) ok "a good name reaches hostnamectl" ;;
    *) bad "setting the hostname ran [$out]" ;;
esac

# ⚠ Narrower than the general value filter: a hostname is not a filename, and
# `a/b` passes that filter. Rejected HERE rather than by systemd, because
# "hostnamectl exited 1" does not tell anybody what was wrong with what they
# typed.
for bad_name in 'a/b' 'ends-' '.starts' 'has_underscore'; do
    if "$BIN" -n set hostname "$bad_name" >/dev/null 2>&1; then
        bad "'$bad_name' was accepted as a hostname"
    else
        ok "'$bad_name' is refused"
    fi
done
long=$(printf 'a%.0s' $(seq 1 70))
if "$BIN" -n set hostname "$long" >/dev/null 2>&1; then
    bad "a 70-character hostname was accepted"
else
    ok "a name past 63 characters is refused"
fi

# ── The lights ──────────────────────────────────────────────────────────────
#
# One switch, two doors: this row and the control panel's both run `syn-rgb`,
# which owns the state and the hardware. The row is drawn whether openrgb is
# installed or not — a control that vanishes is a feature nobody finds out
# about — so what is asserted is that it is THERE and that it is a toggle.
if "$BIN" --rec system | grep -q 'toggle:rgb'; then
    ok "the System pane offers the RGB lights"
else
    bad "no RGB row on the System pane"
fi
out=$("$BIN" -n set rgb on 2>&1)
case $out in
    *"syn-rgb on"*) ok "switching them on runs syn-rgb" ;;
    *) bad "the RGB row ran [$out]" ;;
esac
if "$BIN" -n set rgb maybe >/dev/null 2>&1; then
    bad "'maybe' was accepted for a switch"
else
    ok "anything but on or off is refused"
fi

# ── Speech ──────────────────────────────────────────────────────────────────
#
# ⛔ EVERY ACTION THIS PANE EMITS MUST HAVE A VERB, and the check above only
# proves the verb NAME is one the GUI knows. It cannot tell whether `set
# <that key>` actually works — and the first version of this pane failed
# exactly there in a way no vocabulary check could see: `wake-words` is the one
# value in this app that legally contains commas, and set()'s generic
# sane_value() gate rejects a comma before any key is looked at. The row
# offered a comma-separated list and refused every comma. So each key is
# exercised with the value its own help text suggests.
out=$("$BIN" --rec speech)
case $out in
    *"Screen reader"*) ok "the Speech pane offers the screen reader" ;;
    *) bad "no screen reader row" ;;
esac
case $out in
    *"Answer to its name"*) ok "…and the wake word" ;;
    *) bad "no wake word row" ;;
esac
# ⚠ The disclosure is part of the row, not a nicety: this is the one switch in
# the app whose "on" leaves a device open.
case $out in
    *"MICROPHONE OPEN"*) ok "…and says that it holds the microphone open" ;;
    *) bad "the wake row does not disclose the microphone" ;;
esac

for pair in "screen-reader:maybe" "wake-word:maybe" "speech-rate:9999" \
            "speech-rate:abc" "speech-volume:200" "wake-words:,,,"; do
    k=${pair%%:*}; v=${pair#*:}
    if "$BIN" -n set "$k" "$v" >/dev/null 2>&1; then
        bad "set $k $v was accepted"
    else
        ok "set $k $v is refused"
    fi
done

# …and the values the rows themselves suggest are ACCEPTED. This is the half
# that catches a gate refusing its own advertised input.
tmpcfg=$(mktemp -d)
for pair in "speech-rate:175" "speech-volume:100" "wake-words:synapse,computer"; do
    k=${pair%%:*}; v=${pair#*:}
    if XDG_CONFIG_HOME="$tmpcfg" "$BIN" -n set "$k" "$v" >/dev/null 2>&1; then
        ok "set $k $v is accepted"
    else
        bad "set $k $v was REFUSED — the row advertises a value its verb rejects"
    fi
done
rm -rf "$tmpcfg"

if [ "$fails" -gt 0 ]; then
    printf '\n%d test(s) failed\n' "$fails"
    exit 1
fi
printf '\nall tests passed\n'
