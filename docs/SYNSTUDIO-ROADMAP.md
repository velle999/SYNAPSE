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
| colour    | strongest — 64 settings, curves, HSL, masks, LUT bridge, undo |
| cutting   | undo, markers, snapping, keys, copy/paste, and Save as        |
| retime    | ramps, reverse, freeze, optical flow, and a stabiliser         |
| audio     | EQ, dynamics, noise reduction, ducking, delivery loudness      |
| effects   | sixty transitions, twenty-seven effects, and a format for more |
| looks     | .cube in and out, twelve looks, and a format for those too   |
| titles    | a face, a plate, more than one line, five styles, .srt both ways |
| delivery  | range, presets, burn-in, sequences and a queue                 |
| scopes    | histogram, waveform, parade and vectorscope — all measured here |
| media     | one project, named and saved as — no pool, no proxies, no relink |

## 1 · Audio  — **shipped in 0.1.0-10**

- [x] clip gain (`gain`, −60…+24 dB, reaches the export as `volume=NdB`)
- [x] clip fades in/out (`afade`)
- [x] **fade shapes** — six of afade's curves, by name. `linear` is afade's
      own `tri`, and `qsin` is the equal-power one a crossfade wants
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
- [x] **loudness on delivery** — `timeline loudness --value -23|-14`, after
      the mix, the master fader and the limiter, because a broadcast target is
      a statement about the FILE. ⚠ SINGLE-pass `loudnorm`, deliberately:
      two-pass measures the whole programme and then encodes it, which is
      rendering the timeline twice. `loudness FILE` measures what actually
      came out, so the answer is checkable rather than promised — measured at
      -14.00 against a -14 target
- [x] voiceover recording — `devices` + `record`, punch in at the playhead,
      countdown, monitoring muted while live, a hard `--limit`, the take beside
      the project. ⚠ `ametadata=print` needs `direct=1` or the meter arrives
      all at once when it is already too late
- [x] **clip EQ** — six bands (60, 200, 600, 2k, 6k, 12k), one row each. ⚠ A
      chain of `equalizer` biquads rather than one `anequalizer`: that filter
      is PER CHANNEL, so a stereo clip needs every band written twice and a
      mono one written once — a shape that gets out of step with the source
      the first time somebody swaps a take. Only the bands that were set
      appear in the graph
- [x] **dynamics** — `agate`, `acompressor`, `deesser`, one knob each driving
      the parameters that matter; `alimiter` was already the master bus. The
      ORDER is the design: clean it, shape it, control it. A gate after a
      compressor gates a signal whose quiet parts have already been lifted
- [x] **noise reduction** — `afftdn`, first in the chain, because everything
      downstream is deciding what to do about a signal and the noise is not
      part of it
- [ ] `arnndn` — needs a model file, so it is also the first importable effect
- [x] **ducking** — `timeline track N --duck K`, `sidechaincompress` keyed off
      the nominated track. A TRACK and not a clip, because that is what ducking
      is: a relationship between two layers of a mix. ⚠ The key track's clips
      have already been spent on the main mix, so each is SPLIT rather than
      read twice — naming a stream twice fails the whole graph. Measured:
      identical to un-ducked where the key is silent, 2.6 dB down where it is
      not


## 2 · Effects, and other people's effects

Resolve accepts other people's work three ways: OpenFX (compiled C++), DCTL
(GPU shader source), and LUTs and stills (data). **Only the third fits a
program that never links anything** — and it is already the currency here,
because a grade is baked to an Iridas `.cube` before it reaches ffmpeg.

- [x] **`.cube`, both ways** — **shipped in 0.1.0-17.** `lut` is a develop
      SETTING now (a catalogue name or a path) with a `lut.amount` beside it,
      so a LUT rides the sidecar, the clip grade, undo and the keyframes like
      everything else. 3D and 1D, DOMAIN honoured, the row count checked —
      a truncated download parses row by row and only the count catches it.

      It applies at the END of the pointwise chain, in the display encoding,
      which is what makes the LUT BRIDGE COMPOSE: `ss_lut_write` bakes by
      walking that same chain, so an imported look comes out INSIDE the baked
      cube. The export needs no second `lut3d`, no new graph and no new way
      for a still and a frame to disagree. Measured at 55.15 dB against the
      still renderer — the same as the pure grade's 55.7, so composing two
      lattices costs nothing above the quantisation that was already there.

      ⚠ A LUT this machine has not got is KEPT and renders as nothing, the way
      a missing effect is. ⚠ And no `.cube` ships with SynapseOS: a LUT is
      somebody's licensed work far more often than a slider position is.
- [x] **look files** — **shipped in 0.1.0-17.** `.synlook` is the develop
      stack as tab-separated text, carrying only the fields it moves. Applying
      one SETS those fields and leaves the rest, so a look lands on top of the
      exposure and white balance a photograph needed rather than throwing them
      away — and every slider it moved is still a slider. Geometry is never in
      one: a crop belongs to one photograph, a look is meant to travel.
      `look list|show|save|apply|remove`, `timeline grade --look`, twelve
      shipped, a browser in both panels, and a user's own in
      `~/.config/synstudio/looks` winning on a name
- [x] **effect recipes** — **shipped in 0.1.0-15.** A text manifest naming a
      filter chain and its parameters, so a third party ships an effect with
      no compiler:

      name    Halation
      param   strength  0.4   0  1   float  Strength
      param   radius    12    1  64  float  Radius
      filter  [$in]split[a][b];[b]lumakey=0.6,gblur=sigma=$radius,...[$out]

      Validated by rendering ONE frame through it on arrival — when it is
      checked, and again when it first lands on a clip — so a broken effect
      fails then rather than at the end of an export.

      ⚠ A filter string can do anything ffmpeg can, INCLUDING READ FILES.
      Four walls: a whitelist of filter names, a refusal of any argument that
      names a file, nothing interpolated that was not declared, and every
      parameter a NUMBER clamped to the recipe's own range and printed by the
      engine. The whitelist is also what keeps the monitor honest — everything
      on it is one frame in, one frame out, same size, same answer every time,
      so nothing needing a WINDOW of frames (tmix, deflicker), changing the
      GEOMETRY (crop, scale, rotate) or RANDOM (noise) can be in a recipe.

      Installed to `$datadir/synstudio/effects`, a user's own to
      `~/.config/synstudio/effects` (which wins on a name), and a bundle
      anywhere via `SYNSTUDIO_EFFECTS`. `fx list|params|show|check`,
      `timeline fx add|list|set|remove|move`.
- [ ] title templates — the same manifest with `drawtext` behind it. Not the
      effect whitelist: drawtext reads a font and a textfile, so a title
      recipe needs its own rules about what a caption may contain
- [ ] keyframed effect parameters — the keys are in place for clip properties,
      but an effect's knobs are a dynamic table and most of the filters behind
      them take a fixed value, not an expression
- [x] **twenty-seven built-in effects**, all of them recipes like anybody
      else's: blur, sharpen, soft focus, glow, bloom, halation, pixelate,
      posterise, invert, desaturate, sepia, duotone, colour temperature,
      thermal, vignette, chromatic aberration, lens distortion, edge detect,
      deband, scanlines, glitch, flip, flop, green screen, blue screen, luma
      key, despill
- [no] film damage and grain as effects — both want randomness, and a random
       filter renders a different frame in the monitor than in the export.
       Grain is a develop setting, where it is deterministic
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
- [x] **track automation** — shipped in 0.1.0-30. `timeline auto`, the same
      shape as a clip's parameter keys and sharing their interpolation and
      eases. ⚠ Its keys are in TIMELINE seconds, unlike a clip's, which are
      relative to the clip — a clip can be moved and its keys have to move
      with it, a track cannot, and its fader is ridden against what is on
      screen. ⚠ Which means the exported expression's time variable is SHIFTED
      by where each clip starts: without it, every clip on the track would
      ride the automation from the top of the programme
- [ ] keys on a title's size and colour — drawtext takes no expression for
      either, so they need the sendcmd path opacity uses

Doing this one unblocked speed ramps, animated titles, animated effects and
Fairlight-style automation: four features for one piece of work.

## 4 · Transitions — **shipped in 0.1.0-14**

- [x] **sixty kinds**, all of them `xfade`: dissolve, hard and soft wipes,
      slides, covers, reveals, slices, wind, diagonals, corners, circles,
      curtains, radial, pixelize, distance, blur, squeeze, zoom, and the fades
      through black, white and grey. ONE table in `timeline.c`; `timeline
      transitions` prints it and the window builds its picker from that
- [x] the transition is a LAYER now, not an alpha ramp on the incoming clip.
      The ramp bought every transition for free but it could only ever be a
      dissolve — the wipes were a `geq` in the export and a plain uniform fade
      in the monitor, which is not the same picture
- [x] dip to colour — two dissolves THROUGH a colour, per clip
- [x] audio crossfade paired with the video transition — `qsin` both ways, so
      the power holds across the overlap instead of dipping 3dB in the middle
      of it. Two clips overlapping used to ADD
- [x] a default transition on the cut under the playhead in one command —
      `timeline transition`, which makes the OVERLAP too: out of the outgoing
      clip's handles when it has them, by rippling what follows when it does
      not
- [ ] smooth cut — `minterpolate` across a jump cut. Not an xfade and not a
      blend: it needs frames that were never shot, which is a different piece
      of work from everything above

## 5 · Titles — **shipped in 0.1.0-18**

- [x] text, size, colour, nine placements (`drawtext`)
- [x] **font family and weight** — resolved through `fc-match` to a FILE, and
      the file checked. `font=Sans` needs an ffmpeg built against fontconfig
      and fails the whole graph where it is not — at export time, after the
      edit. `synstudio fonts` lists what this machine has; `fonts have NAME`
      answers for one. ⚠ fc-match always answers, substituting silently, so
      "did it run" is not "was it found"; a weight with no family named
      resolves through `sans-serif` rather than dropping the tick
- [x] **outline, shadow, plate** — `borderw`, `shadowx/y`, `box`, each a
      fraction of the FONT SIZE so a title styled at 1080 is the same title
      delivered at 4K. Zero means off, and emits nothing: the old code had a
      one-pixel floor baked into the arithmetic
- [x] **multi-line and line spacing** — `\n` in a caption is a line break, in
      the project file, in `set`, in `get` and at the shell. `text_align`
      centres the lines inside the block where this ffmpeg has it, and is
      probed for rather than assumed
- [no] letter spacing — drawtext has no tracking option at any version, and a
       second text renderer to gain one is the trade this program does not
       make
- [~] animate on and off — a title's position and opacity move now; its size
      and colour cannot, because drawtext takes no expression for either
- [x] **lower thirds and credit rolls as presets** — five styles (plain,
      lower third, subtitle, heading, credit roll) that SET the fields above
      and then get out of the way, so every one is still a slider afterwards.
      A roll is the one title that MOVES, so it is generated twice: an
      expression in the export, a number in the monitor, which holds one frame
      at t=0 and would otherwise draw every roll at its start
- [x] **subtitles** — `.srt` in and back out, and a cue is a TITLE CLIP. Not a
      fourth clip kind and not a track type of its own, so an imported caption
      takes the font, plate, placement, fades, transform and grade a typed one
      takes and is edited by the commands that already exist — which makes
      burning in free, because that is what a title does. Shipping them soft
      instead is `timeline export --subs FILE`: mov_text, srt or WebVTT by
      container, with the input added LAST so no clip is renumbered

## 6 · Retime and the rest of the cutting room

- [x] **speed** — constant 0.1×…10×, and ⚠ the slow half of that range
      FAILED EVERY EXPORT it was used on: atempo's own range starts at 0.5, so
      a clip under half speed with a sound track on it died at the end of the
      render. Halvings multiply; it chains them now
- [x] **undo and redo** — whole documents in `<project>.undo/`, on disk, so it
      survives the window closing. `timeline undo|redo|history`
- [x] **undo in the DARKROOM too** — shipped in 0.1.0-35. `undo|redo|history
      FILE`, off the same machinery: a photograph's document is its sidecar,
      history is a property of a file, and the engine moves whole documents
      either way. ⚠ Reset WRITES the defaults now rather than unlinking the
      sidecar, so it is an edit like any other and can be taken back — the
      deletion put an afternoon's work outside the history entirely, because
      there was nothing left on disk to snapshot. ⚠ And a slider drag is one
      step, not a hundred: the ticks carry `--no-history` and the release
      commits once, or a hundred-deep ring would hold nothing but one
      slider's journey
- [x] **copy/paste/duplicate clips, and paste a grade to many clips** —
      shipped in 0.1.0-27. The clipboard is a one-clip DOCUMENT, written and
      read by the same two functions the project file uses, so everything the
      format knows about a clip travels: the grade, the parameter keys, the
      effect stack, the sound chain and the retime. `paste --grade` takes only
      the develop stack and leaves the target's timing, framing and sound
      alone; `--all` does that to every clip on a track, which is what grading
      a scene actually looks like. ⚠ A pasted grade clears the target's grade
      KEYS — they hold whole develop stacks, so leaving them would leave the
      clip driven by the grade it had while claiming to wear the new one
- [x] **a project has a NAME** — shipped in 0.1.0-34. `timeline saveas PROJ
      --out PATH [--force]`, and a New project that asks. Nothing here is
      about persistence: every verb ends in a write, so the cut on disk is
      always current and there is no Save to press. What was missing was that
      both doors into a new project wrote the same fixed path, so a second
      project silently took the first one's place. ⚠ Neither writes over a
      file on the first press — exit 3 means "that name is taken", which the
      window turns into a Replace button; `new --unique` takes the next free
      name and prints it. ⚠ And the copy carries `<project>.stab`, or every
      stabilised clip renders unsteady in it with no error and no message
- [x] **copy and paste in the WINDOW** — the verbs shipped at 0.1.0-27 with no
      door into them. Paste lands at the playhead, and is offered only when
      `timeline clipboard` says there is something on it: the clipboard is a
      file that outlives the window, so what is on it is not something this
      window can remember for itself
- [x] markers on the timeline — `timeline mark|unmark`, on the ruler, click to
      go there. ⚠ still nothing on a CLIP
- [x] snapping to cuts, markers and the playhead, on moves, trims and scrubs
- [x] multi-select — shift-click; deletes run highest index first
- [x] **J K L, and keys at all** — shipped in 0.1.0-35. The window bound
      nothing until then: every action was a button, the transport included.
      Space plays, L plays and doubles on each press (the PLAYER's rate on the
      rendered preview, so a fast pass is still the export, played), K stops,
      arrows step a frame and a second, Home/End, S splits, T transitions, M
      marks, Del and Shift+Del delete, Ctrl+C/V copy and paste at the
      playhead, Ctrl+Z/Ctrl+Shift+Z, Ctrl+S saves as, and `?` lists them all.
      ⚠ J cannot be what L is: nothing plays an encoded preview backwards, and
      rendering the timeline in reverse to watch it would be a second renderer
      with its own opinion of the cut. J shuttles the frame monitor back at
      1/2/4 frames a step, and both the status line and the key sheet say that
      is what it is. ⚠ A FOCUS ITEM, not Shortcut objects — Qt matches a
      shortcut before the key reaches whatever has focus, so Ctrl+C over a
      name field would copy a clip and J would shuttle while somebody typed
- [ ] source monitor + three-point editing
- [x] **linked audio and video** — shipped in 0.1.0-29. A link is a GROUP id,
      not a pointer to a partner: a link is not necessarily a pair (a shot,
      its dialogue and its room tone is three), a pointer would not survive
      being written to a text file, and an index would not survive the clip
      beside it being deleted. Move, trim and delete apply to the group in the
      ENGINE rather than in the CLI, so a drag in the window and a `timeline
      move` from a script behave the same. ⚠ A move carries the DELTA, not the
      destination — moving every linked clip to the same instant would stack a
      shot's dialogue on top of it. ⚠ And a trim agrees ONE delta across the
      group first: a head trim clamps to what each clip's source allows, so
      two linked clips with different in points would otherwise clamp by
      different amounts and drift out of sync silently
- [x] **speed ramps, reverse, freeze frame** — **shipped in 0.1.0-19.** A ramp
      is keys on `speed`, and it does move the timebase rather than a number
      inside it: the clip's LENGTH becomes the integral of 1/speed over the
      source. So the curve is sampled once, into a table of constant-speed
      segments, and the length, the frame the monitor seeks to, the export's
      piecewise `setpts` and the tempo the sound runs at are all read from
      that one table. ⚠ Its keys are in SOURCE seconds — the only keyed
      property that is — because a ramp says "at this point in the shot", and
      the alternative is an equation to solve rather than an integral to take.
      ⚠ A ramp outside 0.5–2× drops the clip's sound and says so: one atempo
      can be commanded, a chain of them cannot, and the alternative is a graph
      that fails or a sync that drifts
- [x] **optical flow / frame blending** — `minterpolate`, as `retime=blend` or
      `retime=flow`, and only where the timebase actually moved
- [x] **stabilisation** — two-pass `vidstab`. The first pass is not part of
      any graph and could not be: `timeline stabilise` watches the clip and
      writes a `.trf` beside the project, and the graph reads it. ⚠ It runs at
      the SOURCE's own size, because the numbers in it are pixels — which is
      why the transform goes in before anything scales the picture. Measured:
      frame-to-frame difference 7.08 before, 4.13 after. `--off` keeps the
      analysis, which took as long as a render to make
- [x] **auto-save and versions** — shipped in 0.1.0-28. Undo was already the
      auto-save half: every save records the state it left, so nothing is lost
      between saves. What it does not do is keep anything for long — it is a
      ring of a hundred states. A VERSION is a document somebody decided to
      keep, with a name they chose, in `<project>.versions/`, that nothing
      expires and no edit disturbs. ⚠ A restore goes through the ordinary save
      path, so it is itself undoable: a restore that could not be undone would
      be the one operation in this program able to lose work. ⚠ And a name
      becomes a FILE, so a slash or a leading dot is refused rather than
      sanitised — quietly rewriting it means a later `restore` cannot find
      what it just saved

## 7 · Scopes, colour management, delivery

- [x] histogram (stills)
- [x] six delivery formats with a name and format picker
- [~] masks — darkroom only; a clip's grade is pointwise so it can ride the
      LUT, and a mask is spatial, so it needs the filter path
- [x] **waveform, RGB parade, vectorscope** — **shipped in 0.1.0-23/24.**
      Computed HERE rather than by an ffmpeg filter, which is the bargain the
      histogram already strikes: a scope is read to decide whether a shot is
      legal and whether two shots match, and an answer from a different
      renderer than the picture is an answer about something else. `synstudio
      scope FILE` measures a photograph through its own develop stack;
      `timeline scope PROJ --at T` composites the frame first. ⚠ Not
      normalised by the busiest cell — the greys of a colour-bar frame pile
      into a few cells at the centre of a vectorscope and every hue around
      them divides down to nothing. Referenced to the mean of the OCCUPIED
      cells through a curve that saturates, which is scale-invariant
- [x] **colour management, the input half** — **shipped in 0.1.0-31.** A
      `.cube` on the way in has been a develop setting since 0.1.0-17; named
      LOG transforms join it now: `log = none|slog3|vlog`, applied FIRST,
      before white balance and before anything else reads the numbers. The
      loader decoded the file as sRGB because that is what loaders do, so the
      transform undoes that and applies the camera's own curve instead.
      ⚠ Only curves whose published formula carries a checkable ANCHOR are
      here, and the suite asserts each one in floating point through
      `synstudio logcurve` — an 8-bit render cannot tell a subtly wrong
      constant from a right one, and this pipeline's own round trip loses a
      code either way. ⚠ V-Log's 0.599 is 100% reflectance, NOT 90% (that is
      0.588); the spec's table is easy to misread and the anchor test is what
      caught it
- [ ] the OUTPUT transform — a Rec.709 display curve distinct from sRGB, and
      the colour tags to say so on a delivery
- [x] **shot match** — **shipped in 0.1.0-25.** `synstudio match FILE --ref
      REF` for two photographs, `timeline match PROJ T C REFT REFC` for two
      clips. FITTED, not solved: every control has a transfer function of its
      own, and solving one in closed form means writing a second model of what
      `colour.c` does — which drifts the first time colour.c is improved. Each
      control is set, rendered THROUGH THE REAL ENGINE, measured and bisected,
      so it never needs to know what `contrast` means. Brightness, contrast
      and white balance; not a three-way grade, because there are no
      per-channel lift/gamma/gain controls here and inventing them to have
      something to solve for would be the tail wagging the dog. Measured on a
      realistic pair: 22.5 dB apart before, 33.1 dB after
- [x] **render range** — `timeline range`, in the document because it is set
      while looking at the cut. Trimmed at the END of the graph rather than by
      seeking the inputs: a range is a WINDOW onto the finished picture, not a
      different edit. ⚠ `setpts=PTS-STARTPTS` after the trim, or a render of
      minutes nine to ten arrives with nine minutes of nothing at the front
- [x] **render presets** — seven, named for where they are going. Applied by
      rendering the whole COMPOSITE at that size: every clip, the base and the
      titles are built from the project's own dimensions, so changing those
      changes all of them together and nothing is scaled afterwards
- [x] **image sequences** — `--format png|exr` with `--out dir/f_%04d.png`.
      ⚠ A format with no audio codec has nowhere to put the sound, and mapping
      the mix into one fails the render after the encode has started
- [x] **burn-in** — `--burn timecode|name|both`, on the delivery arguments and
      never in the document: it is for a review copy and must not survive into
      a master. ⚠ The timecode starts at the RANGE, because the trim resets
      timestamps and 00:00:00 on a render that starts nine minutes in is a
      wrong answer to the question it was added to answer
- [x] **render queue** — a FILE of commands, not a daemon. One job per line:
      the arguments of a `timeline export`. Running the queue is running those
      commands, so a job somebody typed and a job the window queued are the
      same object and there is no second code path that renders things. ⚠ The
      queue is kept after a run — a job that failed is a job to look at
- [x] **a watermark image** — `--watermark F.png`, over the delivered frame
      after the range and the burn-in. A picture, so unlike the burn-in it
      cannot be a filter on the end of the chain: it is another input, and it
      goes in LAST for the same reason the subtitles do. Sized as a fraction
      of the frame, so one file marks a 1080 delivery and a 4K one identically

## Build order

1. ~~**Audio you can see and set**~~ — **done, 0.1.0-10.** Track volume/pan,
   master fader, meters, monitoring volume, normalise, solo — and a mute/hide
   split that was quietly wrong.
2. ~~**Voiceover**~~ — **done, 0.1.0-11.**
3. ~~**Undo, markers, snapping, multi-select**~~ — **done, 0.1.0-12.**
4. ~~**Keyframes on everything**~~ — **done, 0.1.0-13.** Any clip property at
   t, five eases — and an animated pan that had been exporting MIRRORED for as
   long as transforms have existed.
5. ~~**xfade transitions + audio crossfades**~~ — **done, 0.1.0-14.** Sixty
   kinds, dip to colour, the sound crossing with the picture, and one command
   that makes the overlap as well as the transition.
6. ~~**The effect recipe format**~~ — **done, 0.1.0-15.** A recipe is a text
   file naming a filter chain, checked against a whitelist and rendered
   through before it is trusted; twenty-seven of them ship.
7. ~~**LUT and look import**~~ — **done, 0.1.0-17.** A .cube reads in as a
   develop setting and composes into the baked cube for free; a look is the
   develop stack as a file. Twelve looks ship, no LUTs do.
8. ~~**Titles worth using**, then `.srt`~~ — **done, 0.1.0-18.** A caption
   with a face, a plate, more than one line and five styles; subtitles in and
   out, burnt in or shipped as a stream.
9. ~~**Retime and stabilisation**~~ — **done, 0.1.0-19.** Ramps, reverse,
   freeze, optical flow and a two-pass stabiliser — and a slow-motion export
   that had been failing outright for every clip under half speed.
10. ~~**Scopes and delivery**~~ — **done, 0.1.0-23/24.** Waveform, parade and
   vectorscope measured by this program rather than by a filter; a render
   range, seven presets, burn-in, image sequences and a queue.

**The build order is finished.** What is left in the sections above is a
short list of named gaps rather than a plan: the named colour transforms, a
watermark image, a source monitor, track automation, a curve editor over time,
and smooth cut. Auto-save was never a gap and is not a feature
here: every verb writes, so the file on disk is the cut as it stands — what
that was missing, until 0.1.0-34, was a way to give it a name.

## Not doing

An **OpenFX host** — loading third-party compiled plug-ins in-process is the
opposite of the rule that kept this program alive through an ffmpeg SONAME
bump. **DCTL and GPU shaders** — building a realtime GPU pipeline means
becoming a different program. **A node compositor** — Fusion is an application
inside an application. **Realtime multi-stream playback** — playback is the
export, played, which is what guarantees that what you watch is what you ship;
anything faster is a second renderer that will eventually disagree with the
first.
