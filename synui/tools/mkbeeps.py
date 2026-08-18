#!/usr/bin/env python3
"""
mkbeeps.py — regenerate Tuxagotchi's beeps.

    tools/mkbeeps.py quickshell/widgets/sounds

The pet's whole voice is eleven square-wave chirps, and they are generated
rather than recorded because that is what they ARE: a virtual pet from 1996 has
a piezo buzzer and one oscillator, and a sampled approximation of one would be
larger, less exact and impossible to adjust. This script IS the source; the
.wav files beside the widget are its build output, committed because a package
must not need python to make a noise.

8-bit unsigned mono at 22050 Hz on purpose — the format is part of the sound.
A 1.5 ms ramp on each edge is the one concession: a hard square edge at these
amplitudes clicks through a real speaker, and the click is louder than the note.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
https://github.com/velle999/SYNAPSE
"""
import math
import os
import struct
import sys
import wave

RATE = 22050
PEAK = 0.42          # loud enough to hear across a room, quiet enough at 2am
RAMP = 0.0015        # seconds of fade at each edge — the anti-click

# Note -> Hz. Named rather than numeric so the tunes below can be read as tunes.
N = {
    "A4": 440.0, "C5": 523.25, "D5": 587.33, "E5": 659.25, "F5": 698.46,
    "G5": 783.99, "A5": 880.0,  "B5": 987.77, "C6": 1046.5, "D6": 1174.7,
    "E6": 1318.5, "G6": 1568.0, "C7": 2093.0,
    "F3": 174.61, "A3": 220.0,  "C4": 261.63, "E4": 329.63, "G4": 392.0,
}


def square(freq, ms, level=1.0):
    """One note. freq of 0 is a rest, which is how the gaps are written."""
    n = int(RATE * ms / 1000.0)
    ramp = max(1, int(RATE * RAMP))
    out = []
    for i in range(n):
        if freq <= 0:
            out.append(0.0)
            continue
        # Square by sign of the sine: exact 50% duty, no phase bookkeeping.
        v = 1.0 if math.sin(2 * math.pi * freq * i / RATE) >= 0 else -1.0
        env = min(1.0, i / ramp, (n - i) / ramp)
        out.append(v * env * level)
    return out


def tune(*notes):
    """(note, ms) or (note, ms, level) pairs, played in order."""
    out = []
    for spec in notes:
        name, ms = spec[0], spec[1]
        level = spec[2] if len(spec) > 2 else 1.0
        out += square(N.get(name, 0.0) if name else 0.0, ms, level)
    return out


# The vocabulary. Every one of these is a MEANING, not a decoration: the widget
# never plays a sound to be pretty, so anything that is not one of these eleven
# events is silence.
BEEPS = {
    # THE sound. Three chirps, the pitch a pocket toy actually used, repeated
    # by the widget rather than made longer here — an alert you cannot ignore
    # once is an alert people switch off.
    "call":     tune(("G6", 70), (None, 55), ("G6", 70), (None, 55), ("G6", 70)),
    # Coming out of the egg: up, and up again.
    "hatch":    tune(("C5", 80), ("E5", 80), ("G5", 80), ("C6", 160)),
    # Two soft mid blips, played once per bite.
    "eat":      tune(("E5", 55, 0.8), (None, 40), ("C5", 70, 0.8)),
    # Winning the guessing game.
    "happy":    tune(("C6", 60), ("E6", 60), ("G6", 60), ("E6", 60), ("G6", 140)),
    # Losing it, or being told no.
    "sad":      tune(("E5", 90), ("C5", 90), ("A4", 180)),
    # A short downward sweep, written as steps because a piezo has no glide.
    "clean":    tune(("C6", 35), ("A5", 35), ("F5", 35), ("D5", 35), ("C5", 60)),
    # Medicine: two flat blips and a relieved one on top.
    "medicine": tune(("D5", 60), (None, 30), ("D5", 60), (None, 30), ("A5", 130)),
    # Lights out. Slow, low, and the only long sound here.
    "sleep":    tune(("G5", 140, 0.7), ("D5", 200, 0.6)),
    # Waking up.
    "wake":     tune(("D5", 90, 0.7), ("G5", 90, 0.7), ("B5", 140, 0.7)),
    # The end. Deliberately unlike everything else — slow, descending, quiet.
    "die":      tune(("G4", 220, 0.6), ("E4", 220, 0.6), ("C4", 260, 0.6),
                     ("A3", 300, 0.5), ("F3", 420, 0.45)),
    # A button. Short enough that a fast tapper hears texture, not a melody.
    "click":    tune(("C7", 14, 0.5)),
}


def write(path, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)                       # unsigned 8-bit
        w.setframerate(RATE)
        w.writeframes(b"".join(
            struct.pack("B", max(0, min(255, int(round(128 + s * PEAK * 127)))))
            for s in samples))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    for name, samples in sorted(BEEPS.items()):
        path = os.path.join(out, name + ".wav")
        write(path, samples)
        print("%-10s %5d ms  %6d bytes" % (
            name, 1000 * len(samples) // RATE, os.path.getsize(path)))


if __name__ == "__main__":
    main()
