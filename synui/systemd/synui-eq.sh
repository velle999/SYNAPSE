#!/bin/bash
# synui-eq — the system equalizer: a 10-band PipeWire filter chain, its state
# file, and the one thing that starts and stops it.
#
#   synui-eq on | off | toggle      turn the equalizer on or off
#   synui-eq band <1-10> <dB>       set one band's gain   (-12 .. +12)
#   synui-eq preamp <dB>            set the pre-gain      (-12 .. +12)
#   synui-eq preset <name>          apply a named curve (see PRESETS)
#   synui-eq presets                list the preset names
#   synui-eq status                 key=value, for the control panel
#   synui-eq reset                  flat, preamp 0
#
# Single writer of ~/.config/synui/eq.state AND the thing that applies it, for
# the reason synui-sound is shaped the same way: "what is the equalizer set to"
# has to have exactly one answer, and a panel that wrote the file while a script
# applied it would be two.
#
# ── WHY A VIRTUAL SINK, WHICH IS THE ANNOYING PART ──────────────────────────
#
# The tidy way to do this is a "smart" filter that attaches to whatever the
# default sink is and never appears as a device. THIS PIPEWIRE CANNOT DO THAT:
# libpipewire-module-filter-chain here documents filter.graph, capture.props and
# playback.props and nothing else — there is no filter.smart. Checked against
# the module, not against the version number, because 1.6.8 is new enough that
# you would assume it were there.
#
# So the equalizer is a sink, exactly as PipeWire's own sink-eq6.conf example
# builds one, and turning it on means making it the default output. What keeps
# that from being a trap:
#
#   * playback.props sets node.passive = true, so the chain's OUTPUT follows
#     the real default sink instead of pinning one. Plugging in headphones
#     still moves the audio; the equalizer rides along.
#   * turning it OFF restores the sink that was default when it was turned on,
#     recorded in the state file. Without that, switching off would leave the
#     desktop pointing at a device that no longer exists and audio would stop
#     with nothing on screen to say why.
#
# ── AND WHY ITS OWN PROCESS ─────────────────────────────────────────────────
#
# `pipewire -c <conf>` runs a second, ordinary PipeWire client that hosts the
# filter chain. The alternative — a drop-in under ~/.config/pipewire/ — is read
# by the DAEMON, so every gain change would mean restarting the audio server
# and every application's stream with it. This way the only process that could
# ever need restarting owns nothing but the filter.
#
# In practice it does not need restarting either: a running chain is retuned in
# place through its Props parameters (see chain_tune), so a preset or a gain
# change never stops the audio. The restart path is only for a cold start.
#
# Being a standalone `pipewire -c` config rather than a drop-in has one sharp
# consequence, and it is the reason this never worked at all until 2026-08-07:
# context.modules REPLACES the defaults here instead of merging into them, so
# the config has to load the protocol and node modules itself. See conf_write.
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
# https://github.com/velle999/SYNAPSE
set -euo pipefail

STATE_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/synui"
STATE="$STATE_DIR/eq.state"
CONF="$STATE_DIR/eq.conf"

SINK_NAME="synui_eq"
# What the chain's capture side is called on the graph. pactl needs this to make
# it the default, and the panel needs it to know whether it is running.
NODE_NAME="effect_input.${SINK_NAME}"

# The ten centre frequencies, ISO octave spacing. Fixed rather than
# configurable: a graphic equalizer with movable bands is a parametric
# equalizer, and that is a different control surface, not a bigger version of
# this one.
FREQS=(31 62 125 250 500 1000 2000 4000 8000 16000)
NBANDS=${#FREQS[@]}

# Gains are clamped to this, both here and in the panel. Wider than anyone
# should need and narrow enough that a slip cannot destroy the signal.
GAIN_MIN=-12
GAIN_MAX=12

# name:g1,g2,…,g10:preamp
#
# Preamp is negative wherever a preset boosts, because boosting ten bands and
# then sending it at full scale is how an equalizer turns into a clipping
# distortion unit. That is the whole reason a preamp control exists.
PRESETS=(
    "flat:0,0,0,0,0,0,0,0,0,0:0"
    "bass:6,5,4,2,0,0,0,0,0,0:-3"
    "treble:0,0,0,0,0,1,3,4,5,6:-3"
    "vocal:-2,-1,0,2,4,4,3,1,0,-1:-2"
    "rock:4,3,1,-1,-1,1,2,3,4,4:-3"
    "classical:3,2,0,0,0,0,-1,-1,-1,-2:-1"
    "loudness:6,4,0,0,-2,0,1,3,5,6:-4"
)

# ── State ───────────────────────────────────────────────────────────────────
#
# Defaults live here rather than in the file, so a state file that predates a
# new key still loads and simply takes the default for it.
enabled=off
preamp=0
preset=flat
prev_sink=
prev_pin=
declare -a gains
for ((i = 0; i < NBANDS; i++)); do gains[i]=0; done

state_load() {
    [[ -f $STATE ]] || return 0
    local k v
    while IFS='=' read -r k v; do
        case "$k" in
            enabled)   [[ $v == on ]] && enabled=on || enabled=off ;;
            preamp)    preamp=$v   ;;
            preset)    preset=$v   ;;
            prev_sink) prev_sink=$v ;;
            prev_pin)  prev_pin=$v ;;
            band*)     local n=${k#band}; [[ $n =~ ^[0-9]+$ ]] && ((n >= 1 && n <= NBANDS)) && gains[n-1]=$v ;;
        esac
    done < "$STATE"
}

state_save() {
    mkdir -p "$STATE_DIR"
    {
        echo "enabled=$enabled"
        echo "preset=$preset"
        echo "preamp=$preamp"
        echo "prev_sink=$prev_sink"
        echo "prev_pin=$prev_pin"
        # The centre frequencies go in the file so the PANEL can label its rows
        # from them. They are this script's table and nowhere else's; a second
        # copy compiled into eq.c would be free to drift the day a band moves,
        # and the symptom would be a slider labelled 500 Hz adjusting 250.
        local IFS=,
        echo "freqs=${FREQS[*]}"
        unset IFS
        for ((i = 0; i < NBANDS; i++)); do echo "band$((i+1))=${gains[i]}"; done
    } > "$STATE"
}

# Integers only, clamped. The panel sends whole dB and so does the CLI; a
# non-numeric argument becomes 0 rather than reaching the conf file, where it
# would be a syntax error inside a config PipeWire parses at load and the whole
# chain would silently fail to start.
clamp() {
    local v=${1:-0}
    [[ $v =~ ^-?[0-9]+$ ]] || v=0
    ((v < GAIN_MIN)) && v=$GAIN_MIN
    ((v > GAIN_MAX)) && v=$GAIN_MAX
    echo "$v"
}

# ── The filter chain ────────────────────────────────────────────────────────

# dB -> the linear node's plain multiplier. awk because the shell has no
# floating point. Shared by conf_write (cold start) and chain_tune (live), which
# must agree to the digit or a retune would move the preamp by a rounding error
# every time a band was touched.
preamp_mult() {
    awk -v d="$preamp" 'BEGIN { printf "%.6f", 10 ^ (d / 20) }'
}

conf_write() {
    mkdir -p "$STATE_DIR"

    # Preamp is a plain gain stage in front of the bands rather than an offset
    # folded into each one: folding it in would change the SHAPE of the curve
    # at the clamp limits, so a +6 band with -3 preamp would not be the same
    # as +3, and the panel's numbers would stop meaning anything.
    {
        echo "# GENERATED by synui-eq. Edits are overwritten on the next change."
        echo "context.properties = { log.level = 0 }"
        # ── The four modules below are NOT optional, and leaving them out is
        # why this chain never started. ──────────────────────────────────────
        #
        # PipeWire's own sink-eq6.conf — the example this file is modelled on —
        # lists filter-chain and nothing else, because it is a DROP-IN for
        # ~/.config/pipewire/filter-chain.conf.d/, and a drop-in's
        # context.modules is MERGED into the base config's list.
        #
        # We do not run a drop-in. `pipewire -c $CONF` makes this the WHOLE
        # config, and then context.modules REPLACES the defaults rather than
        # adding to them. So a config carrying only filter-chain gets a client
        # with no protocol module, which cannot reach the daemon at all:
        #
        #   can't find protocol 'PipeWire:Protocol:Native': Operation not
        #   supported  →  could not load mandatory module
        #   "libpipewire-module-filter-chain"  →  failed to create context
        #
        # The filter GRAPH parses perfectly first — all ten bands load and every
        # link is made — so the log looks healthy right up to the last line, and
        # with log.level = 0 above it is silent. The only visible symptom is that
        # the sink never appears and chain_start times out.
        #
        # This is the module list from /usr/share/pipewire/filter-chain.conf,
        # which is upstream's base for exactly this `pipewire -c` use. Keep it in
        # step with that file rather than trimming it by experiment: rt is
        # optional (ifexists/nofail) but protocol-native, client-node and adapter
        # are all load-bearing, and adapter needs the spa-libs mapping too.
        echo "context.spa-libs = {"
        echo "  audio.convert.* = audioconvert/libspa-audioconvert"
        echo "  support.*       = support/libspa-support"
        echo "}"
        echo "context.modules = ["
        echo "  { name = libpipewire-module-rt"
        echo "    args = { }"
        echo "    flags = [ ifexists nofail ] }"
        echo "  { name = libpipewire-module-protocol-native }"
        echo "  { name = libpipewire-module-client-node }"
        echo "  { name = libpipewire-module-adapter }"
        echo "  { name = libpipewire-module-filter-chain"
        echo "    args = {"
        echo "      node.description = \"SynapseOS Equalizer\""
        echo "      media.name       = \"SynapseOS Equalizer\""
        echo "      filter.graph = {"
        echo "        nodes = ["
        # A `linear` node, NOT a shelf at 0 Hz. Both would work, but linear is
        # what a gain stage actually is, and its Mult is a plain multiplier —
        # so the value here is 10^(dB/20), converted with awk because the shell
        # has no floating point. A shelf would also make the preamp interact
        # with band 1, which is a lowshelf at the same end of the spectrum.
        #
        # "Mult"/"Add", NOT "Gain"/"Offset". The linear builtin's control ports
        # are Control, Mult and Add — this said Gain/Offset, which matches no
        # port, so THE PREAMP NEVER APPLIED. filter-chain does say so:
        #
        #   [W] parse_control() control 'Gain' can not be set:
        #       No such file or directory
        #
        # at WARN, which log.level = 0 above hides. The bands were unaffected
        # (bq_* really does call its port Gain), so the graph looked fine and
        # only the preamp silently did nothing. If a control ever stops taking,
        # raise log.level and read the `using port N ('Name') as control` lines —
        # those are the names that exist.
        echo "          { type = builtin name = preamp label = linear"
        echo "            control = { \"Mult\" = $(preamp_mult) \"Add\" = 0.0 } }"
        for ((i = 0; i < NBANDS; i++)); do
            # First and last are shelves so the extremes actually move the ends
            # of the spectrum; a peaking filter at 31 Hz with Q=1 barely touches
            # anything below it, which reads as "the bass slider does nothing".
            local label=bq_peaking
            ((i == 0))          && label=bq_lowshelf
            ((i == NBANDS - 1)) && label=bq_highshelf
            echo "          { type = builtin name = band$((i+1)) label = $label"
            echo "            control = { \"Freq\" = ${FREQS[i]}.0 \"Q\" = 1.0 \"Gain\" = ${gains[i]}.0 } }"
        done
        echo "        ]"
        echo "        links = ["
        echo "          { output = \"preamp:Out\" input = \"band1:In\" }"
        for ((i = 1; i < NBANDS; i++)); do
            echo "          { output = \"band$i:Out\" input = \"band$((i+1)):In\" }"
        done
        echo "        ]"
        echo "      }"
        echo "      audio.channels = 2"
        echo "      audio.position = [ FL FR ]"
        echo "      capture.props = {"
        echo "        node.name   = \"$NODE_NAME\""
        echo "        media.class = Audio/Sink"
        echo "      }"
        echo "      playback.props = {"
        echo "        node.name    = \"effect_output.${SINK_NAME}\""
        # The line that lets the output follow the real default sink; see the
        # header. Without it the chain pins whichever device was default when it
        # started and headphones stop working.
        echo "        node.passive = true"
        echo "      }"
        echo "    }"
        echo "  }"
        echo "]"
    } > "$CONF"
}

chain_running() {
    pgrep -f "pipewire -c $CONF" >/dev/null 2>&1
}

# ── Retuning WITHOUT a restart ──────────────────────────────────────────────
#
# filter-chain publishes every graph control on the sink node as a Props
# parameter named "<node>:<control>" — "band1:Gain", "preamp:Mult" — and they
# are writable while the chain runs. So a gain change is one set-param, not a
# restart, and the audio never stops.
#
# This is what eq_reapply used to do the hard way. Restarting meant pkill'ing
# the chain WHILE IT WAS THE DEFAULT SINK — the exact thing eq_off's comment
# warns about — so every preset, band and preamp change cost an audible dropout
# while wireplumber reassigned the default, plus a second one when the new chain
# came up and the default was pushed back. Every stream in the session was moved
# twice for a one-decibel change.
#
# The node id, not the name: set-param takes an object id. Resolved by walking
# `pw-cli ls Node` for our node.name, since the id is assigned at runtime and
# changes on every cold start.
chain_node_id() {
    pw-cli ls Node 2>/dev/null | awk -v want="$NODE_NAME" '
        /^[[:space:]]*id [0-9]+,/ { id = $2; sub(",", "", id) }
        $0 ~ "node.name = \"" want "\"" { print id; exit }'
}

# Push the whole curve at the running chain. All of it in ONE set-param rather
# than one per control: eleven calls would be eleven round trips and eleven
# separate graph updates, and a preset that arrived a band at a time would be
# audible as a sweep.
#
# Returns non-zero if the chain could not be retuned, which is the caller's cue
# to fall back to a cold start rather than leave the audio on a stale curve.
chain_tune() {
    local id params i
    id=$(chain_node_id)
    [[ -n $id ]] || return 1

    params="\"preamp:Mult\" $(preamp_mult)"
    for ((i = 0; i < NBANDS; i++)); do
        params+=" \"band$((i+1)):Gain\" ${gains[i]}.0"
    done

    pw-cli set-param "$id" Props "{ params = [ $params ] }" >/dev/null 2>&1 || return 1
}

chain_stop() {
    # -f against the full command line: a bare `pkill pipewire` would take down
    # the audio SERVER, which is the one process on this box that must not be
    # killed to change an equalizer setting.
    pkill -f "pipewire -c $CONF" 2>/dev/null || true
    # Give it a moment to drop its nodes, or the restart races the old process
    # and PipeWire refuses the duplicate node name.
    # n=$((n+1)), never ((n++)). Post-increment evaluates to the value BEFORE
    # incrementing, so the first pass through this loop is ((0)) — an arithmetic
    # command that evaluates to zero returns exit status 1, and `set -e` at the
    # top of this file then kills the script mid-function. It did exactly that
    # here: pkill had already taken the chain down and chain_start's restart line
    # was never reached, so every preset/band/preamp change silently turned the
    # equalizer off. An assignment always returns 0.
    local n=0
    while chain_running && ((n < 20)); do sleep 0.05; n=$((n + 1)); done
}

chain_start() {
    conf_write
    chain_stop
    setsid pipewire -c "$CONF" >/dev/null 2>&1 &
    # Wait for the sink to actually appear before anything tries to make it the
    # default — pactl against a node that is not there yet fails silently and
    # the desktop is left pointing at the old device with the equalizer "on".
    local n=0
    while ((n < 40)); do
        if pactl list short sinks 2>/dev/null | grep -q "$NODE_NAME"; then return 0; fi
        sleep 0.05; n=$((n + 1))    # not ((n++)) — see chain_stop
    done
    echo "synui-eq: the filter chain did not come up" >&2
    return 1
}

current_default_sink() {
    pactl get-default-sink 2>/dev/null || true
}

# ── A default is not the same thing as a PIN ────────────────────────────────
#
# `pactl set-default-sink` does not merely point the audio somewhere; it writes
# WirePlumber's `default.configured.audio.sink`, which means "the user CHOSE
# this device". find-selected-default-node.lua then hands that node
# priority.session + 30000, so it outranks everything for as long as it exists.
#
# That is fatal for auto-switching. A bluetooth headset only wins the default
# by being slightly higher priority than the speakers — 1010 vs 1009 on velle's
# desktop — and no such margin survives +30000. Hit 2026-08-08: the equalizer
# was switched off at 12:26, eq_off pinned the speakers on the way out, and
# from then on connecting the P20i earbuds left the audio on the speakers with
# nothing in any log to say why.
#
# So the pin has to be restored as what it WAS, and "there wasn't one" is a
# value like any other — it cannot be represented by writing a sink name.
default_sink_pin() {
    command -v pw-metadata >/dev/null 2>&1 || return 0
    # The JSON is not written in one style: pactl emits `{ "name": "x" }` and
    # WirePlumber's own state file the compact `{"name":"x"}`, so the spaces
    # have to be optional or this reads empty for half its callers — which
    # looks exactly like "there was no pin" and silently unpins a real choice.
    pw-metadata 0 default.configured.audio.sink 2>/dev/null |
        sed -n "/key:'default.configured.audio.sink'/s/.*\"name\"[[:space:]]*:[[:space:]]*\"\([^\"]*\)\".*/\1/p"
}

default_sink_unpin() {
    command -v pw-metadata >/dev/null 2>&1 || return 0
    pw-metadata -d 0 default.configured.audio.sink >/dev/null 2>&1 || true
}

# ── prev_sink GOES STALE, SO ASK THE GRAPH ──────────────────────────────────
#
# The device the chain is feeding RIGHT NOW, read from the link. This is not
# the same thing as prev_sink, and the difference is a bug velle hit twice:
# prev_sink is the default as it was when the equalizer was switched ON and is
# never updated afterwards, but the chain's output is passive, so WirePlumber
# re-links it whenever the default device changes. Switch the equalizer on with
# the headset asleep, connect the headset, and the audio moves to it while
# eq.state still says speakers. Turning the equalizer off then "restores" a
# device that was abandoned ten minutes ago and the music jumps to the
# speakers with the headset still on your head.
#
# Captured on 2026-08-08 by a watcher across a logout: four consecutive
# eq_off's, every one of them landing on alsa_output while acl=yes,
# profile=a2dp-sink.
chain_target_sink() {
    pw-link -o -l "effect_output.${SINK_NAME}" 2>/dev/null |
        sed -n 's/^[[:space:]]*|-> \([^:]*\):.*/\1/p' | head -1
}

# ── EXACTLY ONE STAGE MAY OWN THE VOLUME ────────────────────────────────────
#
# The chain is a sink that FEEDS another sink, so both apply their own volume
# and the signal is attenuated TWICE. Until 2026-08-08 this script copied the
# device's level onto the chain (to stop the loudness jumping when the two
# nodes' remembered volumes differed), which made the double stage exact:
#
#     bluetooth at 29%  ->  0.29^3 = 0.0244 amplitude, -32 dB
#     chain also at 29% ->  0.0244 * 0.0244 = 0.000595, -64.5 dB   SILENT
#
# velle reported it as "turning eq on mutes my music through bluetooth", and
# the reason it looked like a bluetooth-only bug is that the speakers sit at
# 50%: 0.125 * 0.125 is -36 dB, quiet but still audible. Same defect, one
# device just had enough level left to survive it.
#
# So the DEVICE owns the volume and the chain is pinned at unity, forever.
# Nothing here ever writes a device's volume — in particular it never raises
# one, because on a bluetooth headset that is an AVRCP absolute-volume command
# to hardware in someone's ears (see the P20i's connect-at-100% quirk), and a
# chain that later dies would leave the next sound playing at hardware max.
#
# The volume KEYS follow the device instead of the default sink, so turning the
# equalizer on no longer redirects them onto the chain. See input.c.
#
# WirePlumber still remembers a level against the chain node
# (`Audio/Sink:media.name:SynapseOS\sEqualizer` in stream-properties) and
# restores it as the node appears — velle's was saved at 0.024387 by the old
# code — so unity has to be ASSERTED against that restore, not assumed.
#
# Percentages rather than raw values because both sides of the round trip go
# through pactl's cubic scale, so what is read is what is written.
sink_volume_pct() {
    pactl get-sink-volume "$1" 2>/dev/null |
        grep -oE '[0-9]+%' | head -1 | tr -d '%'
}

assert_sink_pct() {
    local to=$1 pct=$2 got i
    [[ -n $to && -n $pct ]] || return 0

    # Set it, then CHECK it, because we are racing WirePlumber's own restore.
    # The remembered level is applied as the node appears, and whether that
    # lands before or after this write is not something the script gets to
    # decide — so assert it until it sticks rather than assuming an ordering.
    # Three tries over ~0.3s: enough to lose the race twice and still win,
    # short enough that nobody hears it. If it never takes, leave it — a loop
    # that fought WirePlumber forever would be worse than a wrong volume.
    for i in 1 2 3; do
        pactl set-sink-volume "$to" "${pct}%" 2>/dev/null || true
        got=$(sink_volume_pct "$to")
        [[ $got == "$pct" ]] && return 0
        sleep 0.1
    done
}

# ── Turning it on and off ───────────────────────────────────────────────────

eq_on() {
    # Record where the audio was going BEFORE we redirect it, but never record
    # our own sink — turning it on twice would otherwise make "restore" mean
    # "go back to the equalizer", and off would leave it on forever.
    local cur
    cur=$(current_default_sink)
    if [[ -n $cur && $cur != "$NODE_NAME" ]]; then
        prev_sink=$cur
        # Recorded in the SAME branch as prev_sink, and for the same reason: on
        # a second eq_on the pin is already ours, and saving that would make
        # "restore" mean "pin the equalizer", which outlives the chain itself.
        # Empty is the common case and is meaningful — see default_sink_pin.
        prev_pin=$(default_sink_pin)
    fi

    chain_start || return 1

    # BEFORE the streams move, so nothing is ever audible at the chain's own
    # remembered level — which is the whole level bug: WirePlumber restores
    # whatever was last saved against this node, and a chain at anything but
    # unity attenuates a second time on top of the device. See assert_sink_pct.
    assert_sink_pct "$NODE_NAME" 100

    pactl set-default-sink "$NODE_NAME" 2>/dev/null || true

    # Move streams that are already playing, or the equalizer only applies to
    # applications started after it — which reads as "it did nothing".
    local id
    while read -r id _; do
        [[ -n $id ]] && pactl move-sink-input "$id" "$NODE_NAME" 2>/dev/null || true
    done < <(pactl list short sink-inputs 2>/dev/null)

    enabled=on
    state_save
}

eq_off() {
    # WHERE the audio actually is beats where it was when this started. Only
    # fall back to prev_sink when the link cannot be read — a stale answer is
    # still better than none, because leaving no default at all is silence.
    local restore_to
    restore_to=$(chain_target_sink)
    [[ -n $restore_to ]] || restore_to=$prev_sink

    # Put the default back FIRST. Killing the chain while it is still the
    # default sink leaves PipeWire with no default at all until wireplumber
    # notices, and that gap is audible as a dropout.
    if [[ -n $restore_to ]] && pactl list short sinks 2>/dev/null | grep -q "$restore_to"; then
        # No volume is carried back. The device kept its own level the whole
        # time the equalizer was on — that is the point of pinning the chain at
        # unity — so it is already correct and writing to it could only make it
        # wrong. This is where the old code raised a device, and on a bluetooth
        # headset raising a device is a command to hardware.
        pactl set-default-sink "$restore_to" 2>/dev/null || true
        local id
        while read -r id _; do
            [[ -n $id ]] && pactl move-sink-input "$id" "$restore_to" 2>/dev/null || true
        done < <(pactl list short sink-inputs 2>/dev/null)
    fi

    chain_stop

    # The restore above had to go through set-default-sink — anything softer
    # leaves the audible gap this function opens with — so it has necessarily
    # just written a pin. What happens to it depends on what was there before:
    #
    #   no pin before      -> unpin, so auto-switching works again
    #   pin, still current -> put it back; the user did choose that device
    #   pin, now stale     -> unpin
    #
    # That last case is the one that bites. prev_pin is recorded at eq_on and
    # ages exactly like prev_sink, so restoring it blindly re-pins a device the
    # audio has long since left — and a pin outranks a connecting headset by
    # +30000, which is precisely the bug that started all of this. An abandoned
    # preference is not a preference; when it no longer matches where the sound
    # is, auto-switching is the safer thing to leave behind.
    if [[ -z $prev_pin || $prev_pin != "$restore_to" ]]; then
        default_sink_unpin
    fi

    enabled=off
    state_save
}

# Re-apply the curve without the on/off dance — what a gain change needs.
# A no-op when the equalizer is off, so dragging a slider with it switched off
# writes the state and starts nothing.
eq_reapply() {
    state_save
    [[ $enabled == on ]] || return 0

    # The chain is already up: retune it in place. No restart, no dropout, and
    # the default sink is never disturbed — so nothing has to be re-asserted and
    # no stream is moved. conf_write still runs, because the conf is what the
    # NEXT cold start reads and it would otherwise hold the previous curve.
    if chain_running && chain_tune; then
        conf_write
        return 0
    fi

    # Not running, or the live path failed — build it from scratch.
    chain_start || return 1
    # A cold start is a NEW node, so WirePlumber restores its remembered level
    # onto it exactly as it does in eq_on, and unity has to be asserted here
    # too. Missing this would make the equalizer mute itself on the first
    # preset change after a chain crash, which is indistinguishable from the
    # preset being at fault — the same misattribution the chain-start bug had.
    assert_sink_pct "$NODE_NAME" 100
    # The sink is new, so the default has to be asserted and the streams moved
    # across. Only on this path: the retune above leaves both alone.
    pactl set-default-sink "$NODE_NAME" 2>/dev/null || true
    local id
    while read -r id _; do
        [[ -n $id ]] && pactl move-sink-input "$id" "$NODE_NAME" 2>/dev/null || true
    done < <(pactl list short sink-inputs 2>/dev/null)
}

preset_apply() {
    local want=$1 entry name curve pre
    for entry in "${PRESETS[@]}"; do
        IFS=: read -r name curve pre <<< "$entry"
        if [[ $name == "$want" ]]; then
            IFS=, read -r -a gains <<< "$curve"
            local i
            for ((i = 0; i < NBANDS; i++)); do gains[i]=$(clamp "${gains[i]:-0}"); done
            preamp=$(clamp "$pre")
            preset=$name
            return 0
        fi
    done
    echo "synui-eq: no such preset '$want'" >&2
    return 2
}

# ── Entry point ─────────────────────────────────────────────────────────────

state_load

case "${1:-status}" in
on)     eq_on ;;
off)    eq_off ;;
toggle) if [[ $enabled == on ]]; then eq_off; else eq_on; fi ;;

band)
    n=${2:-0}
    [[ $n =~ ^[0-9]+$ ]] && ((n >= 1 && n <= NBANDS)) || {
        echo "synui-eq: band must be 1..$NBANDS" >&2; exit 2; }
    gains[n-1]=$(clamp "${3:-0}")
    # Any hand-set band means the curve is no longer the preset it came from.
    # Saying so is the difference between a label and a lie.
    preset=custom
    eq_reapply
    ;;

preamp)
    preamp=$(clamp "${2:-0}")
    eq_reapply
    ;;

preset)
    preset_apply "${2:-flat}" && eq_reapply
    ;;

presets)
    for entry in "${PRESETS[@]}"; do echo "${entry%%:*}"; done
    ;;

reset)
    preset_apply flat
    eq_reapply
    ;;

status)
    # key=value, one per line — the panel parses this, so the shape is a
    # contract. `running` is the PROCESS, not the state file: the two disagree
    # after a crash or a logout, and the panel must show what is true.
    echo "enabled=$enabled"
    echo "running=$(chain_running && echo yes || echo no)"
    echo "preset=$preset"
    echo "preamp=$preamp"
    for ((i = 0; i < NBANDS; i++)); do
        echo "band$((i+1))=${gains[i]} freq=${FREQS[i]}"
    done
    ;;

bands)
    # Just the frequencies, so the panel can label its rows without carrying a
    # second copy of the table that could drift from this one.
    printf '%s\n' "${FREQS[@]}"
    ;;

conf)
    # Write the filter-chain config from the current state and print its path,
    # WITHOUT starting anything or touching the default sink.
    #
    # This is the only way to check the generated config without inserting an
    # equalizer into someone's live audio: PipeWire parses this file at load and
    # a syntax error makes the chain fail to start with no useful message, so
    # `synui-eq conf | xargs spa-json-dump` is the test.
    conf_write
    echo "$CONF"
    ;;

*)
    echo "synui-eq: unknown command '${1:-}'" >&2
    echo "usage: synui-eq {on|off|toggle|band N dB|preamp dB|preset NAME|presets|reset|status|bands}" >&2
    exit 2
    ;;
esac
