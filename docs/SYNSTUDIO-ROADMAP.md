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
| cutting   | usable, not trustworthy — no undo, no markers, no copy/paste  |
| audio     | the largest gap — no fader, no meter, no track level, no record |
| effects   | not started — the grade is the only effect                    |
| delivery  | works, thin — six formats, no range, no presets, no queue      |
| media     | one project at a time — no pool, no proxies, no relink         |

## 1 · Audio

- [x] clip gain (`gain`, −60…+24 dB, reaches the export as `volume=NdB`)
- [x] clip fades in/out (`afade`)
- [~] fade shapes — linear only; `afade` has twenty curves
- [ ] track volume and pan — beside the existing mute/hide flags, before `amix`
- [ ] level meters, per track and master — from the same `peaks` engine
- [ ] monitoring volume + mute on the transport
- [ ] normalise a clip — measure `ebur128`, write the gain
- [ ] loudness on delivery — two-pass `loudnorm`, −23 LUFS / −14 presets
- [ ] voiceover recording — `devices` + `record`, armed track, punch in at the
      playhead. ⚠ output muted while live or it feeds back; countdown and
      pre-roll; a hard `--limit`; the take lands beside the project
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

## 3 · Keyframes

- [x] the grade — eight per clip, interpolated through the setters' own table,
      quantised to 48 baked cubes so a scrub and an export agree
- [~] eight is a ceiling (a fixed array; the 147MB stack object is why)
- [ ] **any clip property at t**, not just the develop stack — non-colour
      parameters need no cubes, ffmpeg takes an expression
- [ ] easing per key; every interpolation is linear today
- [ ] a curve editor — the darkroom's curve widget, over time instead of tone
- [ ] track automation (keyframes on a track's volume)

Doing this one unblocks speed ramps, animated titles, animated effects and
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
- [ ] animate on and off (needs general keyframes)
- [ ] lower thirds and credit rolls as presets over the above
- [ ] **subtitles** — import `.srt`, edit as a track, burn in or ship as a
      stream

## 6 · Retime and the rest of the cutting room

- [~] speed — constant, 0.1×…10×
- [ ] **undo and redo** — the document is small text and every edit goes
      through one function. The most conspicuous absence in the program
- [ ] copy/paste/duplicate clips, and paste a grade to many clips
- [ ] markers, on the timeline and on clips
- [ ] snapping to cuts, markers and the playhead
- [ ] multi-select
- [ ] J K L, and keys at all — the window binds none
- [ ] source monitor + three-point editing
- [ ] linked audio and video (routing now separates them, which makes the link
      necessary)
- [ ] speed ramps, reverse, freeze frame
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

1. **Audio you can see and set** — 2–3 days. Track volume/pan, master fader,
   meters, monitoring volume, normalise, solo.
2. **Voiceover** — 2 days.
3. **Undo, markers, snapping, multi-select** — 2 days. Undo first.
4. **Keyframes on everything** — 3 days. Unblocks four other features.
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
