# synstudio — the road to a mature edit suite

Measured against DaVinci Resolve 20. The readable version, with the reasoning
and the ffmpeg filter behind every line:

  https://claude.ai/code/artifact/a9a0f696-cc30-48d0-b738-3552030f4c0c

Everything here is judged against the one constraint that has shaped this
program from the first commit: colour is decided once, in `src/colour.c`, and
everything that moves is a filter graph handed to an **ffmpeg subprocess**.
Nothing is linked. That rules parts of Resolve out entirely and makes other
parts nearly free — one ffmpeg filter is worth fifty transitions.

Status: `[x]` shipped · `[~]` partial · `[ ]` absent · `[no]` decided against.

## Where it stands

| area      | state                                                        |
|-----------|--------------------------------------------------------------|
| colour    | strongest — 64 settings, curves, HSL, masks, the LUT bridge   |
| cutting   | usable — undo, markers, snapping, keys; no copy/paste yet     |
| audio     | mixed, metered, recordable; no EQ, dynamics or noise reduction |
| effects   | not started — the grade is the only effect                    |
| delivery  | works, thin — six formats, no range, no presets, no queue      |
| media     | one project at a time — no pool, no proxies, no relink         |

## 1 · Audio  — **shipped in 0.1.0-10**

- [x] clip gain (`gain`, −60…+24 dB, reaches the export as `volume=NdB`)
- [x] clip fades in/out (`afade`)
- [~] fade shapes — linear only; `afade` has twenty curves
- [x] track volume and pan — `timeline track N --gain --pan`, constant power,
      built from the source's channel count (an upmix costs a mono clip 3dB)
- [x] master fader — `timeline master --gain`, then a limiter at 0.99
- [x] solo — a property of the whole timeline, not of the track holding it
- [x] mute/hide split — hide is the PICTURE, mute is the SOUND; they were one
      condition, so muting a video track took its picture away
- [x] level meters, per track and master, from the same `peaks` engine
- [x] monitoring volume + mute on the transport
- [x] normalise a clip — `timeline normalise`, ebur128, engine decides
- [x] `loudness FILE` — integrated LUFS, true peak, range
- [ ] loudness on delivery — two-pass `loudnorm`, −23 LUFS / −14 presets
- [x] voiceover recording — `devices` + `record`, punch in at the playhead,
      countdown, monitoring muted while live, a hard `--limit`, the take beside
      the project. ⚠ `ametadata=print` needs `direct=1` or the meter arrives
      all at once when it is already too late
- [ ] clip EQ — `anequalizer`, six bands, as rows in the clip table
- [ ] dynamics — `acompressor`, `alimiter`, `agate`, `deesser`
- [ ] noise reduction — `afftdn`; `arnndn` needs a model, so it is also the
      first importable effect
- [ ] ducking — `sidechaincompress` keyed off a nominated dialogue track
- [ ] solo (the inverse of the mute flag that already exists)

## 2 · Effects, and other people's effects

Resolve accepts other people's work three ways: OpenFX (compiled C++), DCTL
(GPU shader source), and LUTs and stills (data). **Only the third fits a
program that never links anything** — and it is already the currency here,
because a grade is baked to an Iridas `.cube` before it reaches ffmpeg.

- [~] `.cube` — the engine writes and reads it; importing one as a clip look is
      the missing half
- [ ] look files — a develop stack is already tab-separated text; name it
      `.synlook`, give it a browser, and a look becomes shareable
- [ ] **effect recipes** — a text manifest naming a filter chain and its
      parameters, so a third party ships an effect with no compiler:

      name    Halation
      param   strength  0.4   0  1   float  Strength
      param   radius    12    1  64  float  Radius
      filter  [$in]split[a][b];[b]lumakey=0.6,gblur=sigma=$radius,...[$out]

      Validated by rendering ONE frame through it on arrival, so a broken
      effect fails when it lands rather than at the end of an export.
      ⚠ a filter string can do anything ffmpeg can, including read files —
      needs a filter whitelist and no interpolation of anything undeclared.
- [ ] title templates — the same manifest with `drawtext` behind it
- [ ] two dozen built-in effects, all filters that exist today: blur, glow,
      bloom, halation, pixelate, chroma/luma key, lens distortion, chromatic
      aberration, mirror, edge detect, posterise, deband, deflicker, film
      damage, light rays, scanlines/CRT, glitch
- [no] an OpenFX host — third-party compiled plug-ins in-process is the
       opposite of the rule that survived an ffmpeg SONAME bump
- [no] DCTL — needs a realtime GPU pipeline that does not exist here

## 3 · Keyframes  — **shipped in 0.1.0-13**

- [x] the grade — eight per clip, interpolated through the setters' own table,
      quantised to 48 baked cubes so a scrub and an export agree
- [~] eight is a ceiling (a fixed array; the 147MB stack object is why)
- [x] **any clip property at t** — `timeline anim`, 64 keys a clip across all
      properties. Opacity, gain, scale, position and angle today; WHICH ones
      can be keyed is a column in the clip property table, so the inspector's
      diamond and the renderer cannot disagree about it. No cubes are involved:
      zoompan, rotate and volume each take an expression, and opacity — the one
      of them ffmpeg will not take an expression for — is sendcmd stepping a
      colorchannelmixer at code-value boundaries, which the evaluator rounds to
      as well so the monitor is equal to the export and not merely close
- [x] easing per key — linear, in, out, inout, hold. Polynomials, because the
      export has to evaluate the same shape in ffmpeg's expression language
- [ ] a curve editor — the darkroom's curve widget, over time instead of tone
- [ ] track automation (keyframes on a track's volume)
- [ ] keys on a title's size and colour — drawtext takes no expression for
      either, so they need the sendcmd path opacity uses

Doing this one unblocked speed ramps, animated titles, animated effects and
Fairlight-style automation: four features for one piece of work.

## 4 · Transitions

- [x] dissolve + four wipes, as an alpha ramp on the incoming clip
- [ ] `xfade` — fifty more in one filter: slides, circles, radial, pixelize,
      distance, squeeze, zoom, wind
- [ ] dip to colour (the solid clip already exists as a source)
- [ ] audio crossfade paired with the video transition — today the picture
      dissolves and the sound cuts
- [ ] a default transition applied to the cut under the playhead with one key
- [ ] smooth cut — `minterpolate` across a jump cut

## 5 · Titles

- [x] text, size, colour, nine placements (`drawtext`)
- [ ] font family and weight, from `fc-list`
- [ ] outline, shadow, plate — `borderw`, `shadowx/y`, `box`; white text stops
      disappearing on a bright shot
- [ ] multi-line, line spacing, letter spacing
- [~] animate on and off — a title's position and opacity move now; its size
      and colour cannot, because drawtext takes no expression for either
- [ ] lower thirds and credit rolls as presets over the above
- [ ] **subtitles** — import `.srt`, edit as a track, burn in or ship as a
      stream

## 6 · Retime and the rest of the cutting room

- [~] speed — constant, 0.1×…10×
- [x] **undo and redo** — whole documents in `<project>.undo/`, on disk, so it
      survives the window closing. `timeline undo|redo|history`
- [ ] copy/paste/duplicate clips, and paste a grade to many clips
- [x] markers on the timeline — `timeline mark|unmark`, on the ruler, click to
      go there. ⚠ still nothing on a CLIP
- [x] snapping to cuts, markers and the playhead, on moves, trims and scrubs
- [x] multi-select — shift-click; deletes run highest index first
- [ ] J K L, and keys at all — the window binds none
- [ ] source monitor + three-point editing
- [ ] linked audio and video (routing now separates them, which makes the link
      necessary)
- [ ] speed ramps, reverse, freeze frame — the keys exist now, but a ramp is
      a keyed SPEED, which moves the timebase rather than a number inside it
- [ ] optical flow / frame blending — `minterpolate`
- [ ] stabilisation — two-pass `vidstab`, which fits the subprocess model
- [ ] auto-save and versions

## 7 · Scopes, colour management, delivery

- [x] histogram (stills)
- [x] six delivery formats with a name and format picker
- [~] masks — darkroom only; a clip's grade is pointwise so it can ride the
      LUT, and a mask is spatial, so it needs the filter path
- [ ] waveform, RGB parade, vectorscope on the monitor
- [ ] colour management — input/output transforms; a `.cube` on the way in is
      most of it
- [ ] shot match
- [ ] render range (in/out on the timeline)
- [ ] render presets — resolution, frame rate, bitrate; "YouTube 1080p" as one
      row
- [ ] image sequences (`ss_save` already writes PNG and EXR)
- [ ] burn-in — timecode, filename, watermark
- [ ] render queue, in the background

## Build order

1. ~~**Audio you can see and set**~~ — **done, 0.1.0-10.** Track volume/pan,
   master fader, meters, monitoring volume, normalise, solo — and a mute/hide
   split that was quietly wrong.
2. ~~**Voiceover**~~ — **done, 0.1.0-11.**
3. ~~**Undo, markers, snapping, multi-select**~~ — **done, 0.1.0-12.**
4. ~~**Keyframes on everything**~~ — **done, 0.1.0-13.** Any clip property at
   t, five eases — and an animated pan that had been exporting MIRRORED for as
   long as transforms have existed.
5. **xfade transitions + audio crossfades** — 1 day. The cheapest large win.
6. **The effect recipe format** — 3–4 days, plus two dozen effects in it.
7. **LUT and look import** — 1–2 days.
8. **Titles worth using**, then `.srt` — 2 days.
9. **Retime and stabilisation** — 2 days.
10. **Scopes and delivery** — 2–3 days.

## Not doing

An **OpenFX host** — loading third-party compiled plug-ins in-process is the
opposite of the rule that kept this program alive through an ffmpeg SONAME
bump. **DCTL and GPU shaders** — building a realtime GPU pipeline means
becoming a different program. **A node compositor** — Fusion is an application
inside an application. **Realtime multi-stream playback** — playback is the
export, played, which is what guarantees that what you watch is what you ship;
anything faster is a second renderer that will eventually disagree with the
first.
