#!/usr/bin/env bash
# music_errands.sh — every row of the music widget's drawer DOES something
#
# WHAT THIS EXISTS FOR (reported 2026-08-23): Spotify's row in the music
# widget's drawer invited a sign-in and did nothing when it was pressed, and the
# YouTube page — with a Firefox session already configured and being read
# correctly — offered Search…, Your playlists and Search inside cliamp… and
# acted on none of them.
#
# Both halves were the same bug wearing different clothes. `syn-arcade big
# music` answers, for this machine, what each row NEEDS — an `action` column on
# the source picker and a `kind` column on the YouTube page — and the widget
# drew both answers faithfully and then had nowhere to act on either:
#
#   · Spotify's row came back `action=setup`, note "press to sign in — needs
#     Spotify Premium". The note was drawn in the middle of an empty drawer and
#     the only thing to press was the source chip that had already been pressed.
#     There was no path in the shell that ran `big music setup` at all.
#   · The YouTube page came back with three `action` rows — Search…, Your
#     playlists, Search inside cliamp… — and chooseItem() refused every one of
#     them on the grounds that `yt find` and `yt login` read from stdin. That
#     was true when it was written and it stopped being true: both verbs ask
#     can_be_asked() first and re-run themselves inside a terminal when there is
#     none. So the reward for signing a browser in was three rows of grey text.
#
# ⚠ AND NOTHING COULD SEE IT. This is the second-roster trap that syn-arcade's
# own suite names: big.c can answer `setup` in the action column all day, and if
# the shell has no branch for it the row is inert — no warning at build time, no
# error at run time, no line in the journal. The only witness is a person on a
# sofa pressing a button that does nothing.
#
# A TEXT CHECK, and deliberately so. The widget is a quickshell surface on the
# running compositor and MusicLibrary shells out to a real `syn-arcade` against
# a real Plex server and a real YouTube session; loading it to press rows would
# drive velle's live desktop and start their music. What can be checked without
# running it is exactly the shape the bug had: an answer big.c gives that the
# shell has no branch for.
#
# ⚠ THE TELEVISION IS THE SPEC. syn-arcade's own shell already dispatched all of
# this correctly — `ytAction()` and `chooseSource()` in syn-arcade-big.qml — and
# the desktop widget is the surface that did not. So this asserts the widget
# reaches the same verbs, and NOT that it reaches them the same way: the
# television fires a source's errand the moment the source is chosen, which on a
# desktop would mean a click on a chip throwing up a terminal.
#
# Usage: music_errands.sh [path/to/MusicLibrary.qml] [path/to/MusicPlayer.qml]
#
# SynapseOS Project
# SPDX-License-Identifier: GPL-2.0-or-later
set -u

here=$(cd "$(dirname "$0")" && pwd)
lib=${1:-$here/../quickshell/MusicLibrary.qml}
widget=${2:-$here/../quickshell/widgets/MusicPlayer.qml}

[ -f "$lib" ]    || { echo "  ABORT no MusicLibrary.qml at $lib"; exit 1; }
[ -f "$widget" ] || { echo "  ABORT no MusicPlayer.qml at $widget"; exit 1; }

fails=0
say() { echo "  FAIL $*"; fails=$((fails + 1)); }

# ── 1. the three actions a SOURCE can come back with ────────────────────────
#
# `setup`, `install` and `browse` are what source_action() answers for a source
# that cannot play from here yet, and each one names a `big music` verb. All
# three, because the set moves: Spotify is `setup` today and becomes `browse`
# the moment somebody finishes signing in, and a widget that learned only the
# first of those would break at the exact moment it was fixed.
for act in setup install browse; do
    grep -q "sourceAction === \"$act\"" "$lib" ||
        say "nothing in MusicLibrary answers a source whose action is '$act'"
done

grep -q 'errand(\["setup"\])'                   "$lib" || say "the source errand never runs \`big music setup\` — this is Spotify's sign-in"
grep -q 'errand(\["install", root.sourceId\])'  "$lib" || say "the install errand does not name the source it is installing for"
grep -q 'errand(\["browse"\])'                  "$lib" || say "nothing opens cliamp for the sources only cliamp can reach"

# ⚠ THE LABEL IS A PROPERTY OF ITS OWN, and the button binds to it. A button
# whose label is a note fetched from the C side would read "press to sign in —
# needs Spotify Premium" across a 268px card; a button is its own label, and
# what makes that possible is the label being keyed on the action here.
grep -q 'property string sourceErrand' "$lib" ||
    say "there is no label for the source errand, so nothing can put it on a button"

# ── 2. and the widget has somewhere to press ────────────────────────────────
#
# The half that was actually missing. `sourceErrand` could be perfect and the
# drawer still have nothing in it but the sentence.
grep -q 'MusicLibrary.runSourceErrand()' "$widget" ||
    say "the drawer never calls runSourceErrand — the note is still the only thing on screen"
grep -q 'text: MusicLibrary.sourceErrand' "$widget" ||
    say "the button does not carry its own label"

# ⚠ IT MUST BE A TapHandler AND NOT THE CHIP. Pressing the source chip is
# `setSource`, which is a SETTING; wiring an errand onto it would mean glancing
# at Spotify in the picker opened a terminal.
errandblock=$(sed -n '/id: errandBtn/,/^            }$/p' "$widget")
printf '%s\n' "$errandblock" | grep -q 'TapHandler' ||
    say "the errand button has no TapHandler, so it is a label rather than a button"

# ── 3. every row of the YOUTUBE page ────────────────────────────────────────
#
# yt_stations() emits exactly four ids with kind=action, and which of them exist
# is big.c's answer about this machine. All four must land somewhere.
# ⚠ SCOPED TO chooseItem, not to the file. Every one of these ids is named in
# the prose above it — this file explains at length what each row is for — so a
# whole-file grep is satisfied by the comment describing the branch that is
# missing. The dispatch is what must mention them.
choose=$(sed -n '/function chooseItem/,/^    }$/p' "$lib")
if [ -z "$choose" ]; then
    echo "  ABORT could not find chooseItem in $lib — every check below is vacuous"
    exit 1
fi
for id in find login setup mine; do
    printf '%s\n' "$choose" | grep -q "it.id === \"$id\"" ||
        say "chooseItem has no branch for the YouTube errand row '$id'"
done

# `find` and `login` are the two that end in typing, and they go through the
# SAME verb the id came from — no second table of what each row runs.
printf '%s\n' "$choose" | grep -q 'errand(\["yt", it.id\])' ||
    say "Search… and Sign in… do not run through the verb their id names"

# ⚠ `setup` IS CLIAMP'S WIZARD AND NOT A YT VERB. `big music yt setup` is not a
# command; sending it there would be a row that reports success and does
# nothing. It is the same verb the Spotify source uses.
grep -q '"yt", "setup"' "$lib" &&
    say "the OAuth row is sent to \`big music yt setup\`, which is not a verb"

# ── 4. Your playlists is a LIST, not an errand ──────────────────────────────
#
# The whole payoff of `yt login`. It cannot be shown by opening a terminal, so
# the drawer has to go one level down and come back — which is what the
# television does with a menu page and what `drill` does here.
grep -q '"yt", "mine", "--rec"' "$lib" ||
    say "Your playlists never asks big.c for the playlists"
grep -q 'property string drill' "$lib" ||
    say "there is no way into a playlist list, so \`mine\` has nowhere to go"
grep -q 'kind: "back"' "$lib" ||
    say "there is no way back out of the playlists — a one-way drawer"

# ⚠ AND THE DRILL IS CLEARED WHEN THE SOURCE CHANGES. Left set, the next fetch
# goes to `yt mine` and draws a YouTube library under the word Plex.
sed -n '/function setSource/,/^    }$/p' "$lib" | grep -q 'drill = ""' ||
    say "switching source does not clear the drill — Plex would draw YouTube playlists"

# ⚠ AND A FETCH IS STAMPED WITH WHAT IT WAS ASKED ABOUT. `itemsFor` was written
# from sourceId when the answer ARRIVED, which is a different question: switch
# source while a fetch is in flight and the old source's rows are stamped with
# the new source's name, passing the very gate that exists to catch it — and
# loadItems bows out while the Process is busy, so the switch fetched nothing of
# its own. `yt mine` is a round trip through yt-dlp, which is what turns a
# latent race into a reachable one.
grep -q 'property string forSource' "$lib" ||
    say "a fetch is not stamped with the source it was asked about"
grep -q 'root.itemsFor = itemsJob.forSource' "$lib" ||
    say "the arrival is recorded against the source it FINISHED on, not the one it asked about"
sed -n '/onStreamFinished/,/const rows/p' "$lib" | grep -q 'root.loadItems()' ||
    say "a dropped stale fetch does not start the one it displaced"

# ── 5. the rows that CAN be pressed are drawn as such ───────────────────────
#
# ⚠ ONE ANSWER, ONE PLACE. The delegate used to decide this itself with
# `kind !== "action"`, which was right while no action row worked and is the
# whole of what velle saw: three grey rows. A second opinion about which rows
# are live is a second thing to keep in step with the dispatch above.
grep -q 'function pressable' "$lib" ||
    say "MusicLibrary does not answer which rows are pressable"
grep -q 'MusicLibrary.pressable(modelData)' "$widget" ||
    say "the row delegate decides for itself whether a row is live"
grep -q 'modelData.kind !== "action"' "$widget" &&
    say "the delegate still greys out every action row — the rows work but look dead"

# ── 6. an errand is not on the transport pool ───────────────────────────────
#
# Every one of these blocks for as long as the terminal it opened is on screen.
# Put through actPool it holds one of three slots for however long somebody
# takes to sign in, and `running = true` on a busy Process is a SILENT no-op —
# three of them and every skip is dropped with nothing said.
grep -q 'property Process errandProc' "$lib" ||
    say "the errands share the transport pool, where they would swallow skips"
sed -n '/function errand(/,/^    }$/p' "$lib" | grep -q 'errandProc.running' ||
    say "nothing guards a second errand over the first"

# And the picker is asked again afterwards: every errand exists to change the
# answer, and a row still saying "press to sign in" after a sign-in is the fix
# looking like a failure.
sed -n '/property Process errandProc/,/^    }$/p' "$lib" | grep -q 'refreshSources' ||
    say "the picker is not refreshed when an errand finishes"

if [ "$fails" -eq 0 ]; then
    echo "  ok  every row in the music drawer reaches the verb it names"
    exit 0
fi
exit 1
