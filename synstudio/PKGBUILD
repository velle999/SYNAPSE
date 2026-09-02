# Maintainer: Velle Sinclair <brncomputerhelp@gmail.com>
#
# synstudio — the SynapseOS darkroom and edit suite.
#
# One colour engine, two faces. The develop stack in src/colour.c is the only
# place in this program where a pixel's colour is decided, and the video side
# does not reimplement it: a clip's grade is BAKED to an Iridas .cube and
# handed to ffmpeg's lut3d filter, so a still and a frame of video with the
# same settings come out the same. Measured, not asserted — tests/run.sh holds
# the engine against the LUT at 55.7 dB PSNR, which is about 0.4 of a code
# value, and 65 nodes measures WORSE than 33, so what is left is quantisation
# rather than interpolation error.
#
# Nothing here links ffmpeg or libraw. Decode, encode and camera raw are all
# subprocesses with an argv array, never a shell. That is a decision with a
# scar behind it: a shipped SynapseOS component that linked ffmpeg stopped
# launching the day ffmpeg bumped a SONAME, on an ordinary system upgrade,
# with no warning and nothing the application could do about it. A pipe has no
# ABI, and the cost is one process per file.
pkgname=synstudio
# pkgver stays 0.1.0 and releases move pkgrel. build-all.sh writes
# "$name-0.1.0.tar.gz" and transforms paths to "$name-0.1.0/" for every
# component, so bumping pkgver leaves makepkg looking for a tarball nothing
# creates.
pkgver=0.1.0

# 1: first release. The photo darkroom is the face that ships — RAW and stills
#   in, a 64-setting non-destructive develop stack, masks, and an export. The
#   video timeline is real and tested (it cuts, grades, fades and encodes) but
#   is reached from the command line; it has no page in the window yet.
# 2: the video side gets its face. The window has two pages now: the darkroom,
#   and an edit page with a program monitor, draggable clips, a razor, ripple
#   delete and an inspector — whose Grade section is the SAME develop table the
#   darkroom draws, on a clip, baked to a .cube. Underneath: clip transforms
#   (pan, zoom and the Ken Burns move a still needs to earn its place in a
#   cut), transitions as alpha ramps on the incoming clip, generated title and
#   colour clips, move/trim/split/delete/ripple, and `timeline frame`, which
#   composites ONE frame from only the clips under the playhead so a scrub
#   costs a seek rather than a decode from zero.
#
#   Three of those fixed things that were quietly broken: a still contributed a
#   single frame to an export that expected seconds of them (no -loop, no
#   error); a caption containing a percent sign failed the whole filter graph
#   at export time; and the property table wrote a float through a double's
#   offset, so fade lengths and transition lengths silently would not stick.
# 3: it plays.
#   Compositing twenty-five frames a second from a process per frame is not
#   something this architecture can do, so playback is the EXPORT, played:
#   `timeline export --preview` runs the SAME graph at 960 wide with the
#   encoder set to ultrafast, cached against a revision counter and
#   invalidated by any edit. What you watch is what you will ship, only
#   rougher — rather than a second, cheaper renderer that disagrees with the
#   real one about exactly the things a preview exists to check. The file is
#   fragmented mp4 so a player can open it while ffmpeg is still writing.
#
#   Also: the monitor no longer blinks. Both pages show a PNG at a path that
#   never changes, and a plain Image goes BLANK for the whole time the next
#   one is decoding — every scrub step, every slider release. Two images now,
#   swapping only once the new one is Ready, so the last good frame stays up.
# 4: the window at a size somebody actually uses, and a playhead that renders
#   while it is dragged.
#
#   The top strip anchored a button row to the left, a filename to the CENTRE
#   and a size readout to the right, with nothing arranging them — so on a
#   narrow window all three were drawn on top of each other, and the same
#   collision happened on the transport bar. The toolbar is a Flow now and
#   wraps rather than running off the edge (Export and Ripple delete were
#   unreachable, not merely cramped), the filename lives in the title bar
#   where it already was, the readout moved to the status line, and the side
#   panels give ground below about 1060 wide instead of taking half the
#   window from the picture.
#
#   Dragging the playhead rendered NOTHING until it was released: the frame
#   request restarted a debounce on every mouse move, and a debounce that
#   restarts while the hand is moving never fires. Measured 0 frames over 3.6
#   seconds of dragging; now 35. The darkroom sliders had the same bug.
#
#   It also no longer opens straight into a photo picker. It edits photographs
#   AND cuts video, and choosing the darkroom for somebody is the wrong half
#   half the time — there is a start screen with the three ways in, and
#   `browse` lists .syntl projects so one of them can be opened at all.
# 5: waveforms on the clips, and audio as a first-class source.
#
#   `synstudio peaks` reads an envelope — peak and RMS per bucket — and the
#   timeline draws it on every clip that has sound. Verified against ffmpeg's
#   OWN volumedetect rather than a number written down here, and a sine's
#   peak/RMS ratio is checked against sqrt(2), which is what catches a byte
#   order or sign mistake in reading s16le.
#
#   The first attempt asked ffmpeg to resample down to a few samples per
#   bucket. Resampling LOW-PASSES: at 200Hz a 440Hz tone is not quieter, it is
#   GONE, and the test file read as digital silence. The envelope is reduced
#   by streaming the decode past a bucket accumulator instead, so it stays at
#   a rate that still contains the signal and costs one chunk of memory
#   regardless of how long the clip is.
#
#   Audio-only files worked nowhere, because ss_probe_file is about the
#   PICTURE and fails outright without a video stream. A music bed reported
#   "no audio" to peaks, arrived on a track as a five second clip whatever its
#   real length, and could not be found in the picker at all — browse listed
#   what the engine can DECODE and no audio extension was on the list. All
#   three are fixed: durations come from ss_media_duration, and mp3/wav/flac
#   and friends are their own row kind.
# 6: keyframes on the grade, and four playback bugs.
#
#   A clip can hold up to eight keyframes, each a whole develop stack pinned
#   to an instant, and the grade moves between them. A 3D LUT is a static
#   table and ffmpeg cannot fade between two of them, so an animated grade
#   renders as a run of cubes each gated to its own span — and the monitor
#   quantises identically, so a scrub and an export cannot disagree.
#
#   The bugs, all found by measuring rather than reading:
#   - `between(t,a,b)` is inclusive at BOTH ends, so a frame landing exactly
#     on a step boundary satisfied two gates and got the grade applied TWICE.
#     One frame a second of roughly double the grade.
#   - a video clip's own audio was never in the export. Audio was emitted per
#     TRACK, so footage with dialogue on it played and exported SILENT.
#   - dragging a clip measured the pointer inside the clip, which moves as it
#     is dragged, so the movement cancelled itself: stuttering right, and
#     nothing at all to the left.
#   - the monitor named every cube of an animated grade while the baker wrote
#     only the one it needed, so the frame failed and the window kept showing
#     the previous one. A stale picture is the worst failure a monitor has.
#
#   Previews are no longer fragmented mp4 (nothing plays a half-written file,
#   and empty_moov can report a wrong duration and stop early), and one is
#   rendered in the background after an edit so a clip is playable as soon as
#   it is added rather than at the first press of play.
# 7: play starts where the playhead is, and a sound lands on a sound track.
#
#   `pl.source !== url` compared a `url` property — which QML hands to
#   JavaScript as an OBJECT — against a string, so it was ALWAYS true, and the
#   two print identically. Every press of play therefore took the change-the-
#   source path and armed a seek for a load that never came, because assigning
#   a url the value it already holds notifies nothing. Playback carried on
#   from wherever it had been paused and the playhead froze where it had been
#   scrubbed to. Rendering the preview in the background at pkgrel 6 is what
#   exposed it: before that every press rendered a file under a new name, so
#   the source really did change.
#
#   Add media chose the SELECTED track, so a music file dropped onto V1 —
#   which is what the selection usually is. It was legal in the document and
#   exported correctly, and on screen it read as the audio having vanished,
#   because the only sign of it was a waveform inside a video clip's bar. The
#   destination now comes from what the file IS: a sound goes to an audio
#   track, a picture to a video track, one is laid down if the project has
#   none, and Add media no longer needs a track selected first.
# 8: things that would not drag, tracks that could not be seen, and an export
#    that said nothing.
#
#   Every draggable control in this window sits inside a Flickable, and a
#   Flickable takes the grab once a drag passes the threshold — measured at 2
#   moves out of 10 reaching the MouseArea, 10 out of 10 with preventStealing.
#   It only steals when the content can actually scroll that way, which is why
#   the playhead dragged fine MAXIMIZED and fought back in a smaller window,
#   and why it read as "hard" rather than dead. The ruler, clip moves and
#   trims, and all three kinds of slider now hold their own drags.
#
#   Tracks past the bottom of the timeline strip were simply not there: added,
#   in the document, exported, invisible. The lanes scroll vertically now, the
#   headers ride the same contentY so a name never drifts off its lane, and the
#   ruler stays put by riding it the other way.
#
#   Export gave no sign of life — and a second press could not help, because
#   `running = true` on a Process already running is a silent no-op. The button
#   goes busy, and ffmpeg's own `time=` (written on a CARRIAGE RETURN, which is
#   why a StdioCollector never showed a word of it) drives a percentage in the
#   status bar.
# 9: what a file IS, what it comes out as, and sliders that finally slide.
#
#   THE SLIDERS. preventStealing at pkgrel 8 was necessary and not sufficient:
#   nothing was stealing the drag, something was DESTROYING it. The develop
#   values lived in the same array the panel was built from, so every tick
#   rebuilt it — and a Repeater whose JS-array model is reassigned rebuilds its
#   delegates, taking the MouseArea that holds the mouse grab with them.
#   Measured: one of ten mouse moves arrived, and the delegate was destroyed on
#   it. The values moved into a map beside the rows, which nothing is modelled
#   on. The -10 halo around each track also had its sign backwards, so every
#   value landed twenty pixels to the right of the pointer.
#
#   DROP FILES ON IT. A drop asks the engine what each file is, one file at a
#   time, rather than guessing from the name — so a sound lands on an audio
#   track, a picture on a video track, a .syntl opens as a project, and a
#   photograph dropped on the darkroom opens there.
#
#   `kind FILE` is that question: image|video|audio|project|none, from ffmpeg
#   and not from the extension, so a format nobody thought to list still works.
#   Cover art is a video stream and used to read as a movie, which is how an
#   album ended up on the video track. The extension tables that `browse` uses
#   for whole directories grew a lot at the same time, and any `<x>_pipe`
#   demuxer now counts as a still — before, only png and jpeg did, so every
#   other single-image format was treated as a movie.
#
#   EXPORT AS. A name and a format, both chosen. `timeline formats` and
#   `formats` print what the engine can write — mp4, mkv, mov, HEVC, WebM,
#   ProRes for the cut; JPEG, PNG, TIFF, WebP, BMP for a photograph — and the
#   window builds its picker from that, the way the develop panel is built from
#   `keys`, so it can never offer one the engine has no encoder for.
#   `timeline export --format F`, or inferred from the output's extension.
#
#   193 assertions.
# 10: a mixer, and audio you can see.
#
#   Per-clip gain and fades were already in the document and in the export.
#   What was missing was every way to SEE a level and every way to set one that
#   was not a single clip: track faders and pan, a master, solo, meters,
#   monitoring volume, and normalise.
#
#   All of it measured rather than read. A track fader moves the exported file
#   by exactly its dB — six is six, on ProRes, because a low-level sine comes
#   back 3dB hot through AAC and would make every number in the suite noise.
#
#   `mute` and `hide` used to be ONE condition, so muting a video track took
#   its picture away and hiding one took the dialogue with it. They are now
#   what their names say: hide is the picture, mute is the sound, and the four
#   combinations are each asserted against a real export — mean luma of a frame
#   for the picture, a meter for the sound.
#
#   Panning a MONO clip through `aformat=channel_layouts=stereo` costs 3dB, so
#   hard left measured quieter than centre — the one thing a pan can never do.
#   The pan is built from the source's own channel count instead, and constant
#   power, so a sound in the middle is not louder than the same sound at the
#   side.
#
#   Solo is a property of the whole timeline rather than of the track holding
#   the flag; the mix is limited at 0.99 after the master, because summing
#   tracks with normalize=0 can pass full scale and what that sounds like is a
#   crackle nobody can trace back to the fader that caused it.
#
#   `loudness FILE` and `timeline normalise` are ebur128 — the meter the
#   broadcast standards are written against. The engine measures AND decides,
#   so "how loud should this be" is answered in one place.
#
#   The meters read the ENVELOPE at the playhead, not the sound card: nothing
#   in the window can see what the player is pushing, so they are computed from
#   the same per-clip peaks that draw the waveforms with the clip gain, the
#   track fader and the master applied exactly as the graph applies them.
#
#   212 assertions.
# 11: voiceover.
#
#   `devices` asks ffmpeg what can capture and marks the monitors — a monitor
#   records what the machine is PLAYING, which is a real thing to want and
#   never what somebody asking for a voiceover meant. `record` takes it, with a
#   live meter, and stops on a signal: SIGINT or SIGTERM finishes the file
#   rather than killing the process, so the window's Stop button leaves a WAV
#   with its real length in the header.
#
#   In the window: a countdown, the timeline rolling under the take, and
#   monitoring muted for the duration unless somebody says they are on
#   headphones — then put back exactly as it was, including after a failed
#   take. The take lands beside the project, stamped, and punches in where the
#   playhead was when it STARTED rather than where playback has since moved it.
#
#   ⚠ `ametadata=print` writes through avio's 4KB buffer. Without `direct=1` a
#   take under about eight seconds delivered NOTHING until ffmpeg exited and
#   then everything at once — on screen indistinguishable from a microphone
#   that was never live. The test asserts the FIRST level line arrives seconds
#   before the last one, which is the only way to tell those two apart.
#
#   The recorder also sets PR_SET_PDEATHSIG. A killed parent used to leave
#   ffmpeg holding the microphone open for the rest of the session; that was
#   found as an hour-old orphan from a test.
#
#   No microphone is needed to test any of it: `--format lavfi` records a
#   generated signal through the same path, and `-re` is what makes a generated
#   source stand in for a device rather than producing an hour in six seconds.
#
#   225 assertions.
# 12: undo, markers, snapping, more than one clip — and a dialog that fits.
#
#   UNDO is a stack of whole DOCUMENTS in `<project>.undo/`, not of inverse
#   operations: a .syntl is a few kilobytes of text and every verb is a
#   separate process that loads, changes and saves, so there is no session to
#   keep a stack in — and an inverse per verb would be twenty more things that
#   can be wrong in one direction only. On disk, so it survives the window
#   closing and an edit made from the command line in between. `timeline undo|
#   redo|history`.
#
#   MARKERS, `timeline mark|unmark`: a note pinned to an instant, on the ruler,
#   click to go there and right-click to remove. Nothing renders differently
#   for one, which is the point — it is the only way to put something where a
#   problem is without changing the cut to say so.
#
#   SNAPPING now covers trims and the playhead, not just clip moves, and takes
#   markers as targets. Eight PIXELS rather than frames, so the tolerance
#   shrinks as the zoom grows; a Snap button turns it off for the one edit that
#   has to land between two things.
#
#   MORE THAN ONE CLIP: shift-click adds to the selection, and Delete says how
#   many it is about to take. Deletes are ordered HIGHEST INDEX FIRST, because
#   removing clip 2 renumbers everything above it. Edits also QUEUE now instead
#   of being dropped when the engine is busy — `running = true` on a busy
#   Process is a silent no-op, so asking for six deletes used to do one.
#
#   Also: the Export dialog computed its height from a row count plus a guess
#   at the chrome, and the twenty pixels it was short were the Cancel and
#   Export buttons. It is sized from its content now.
#
#   And a photograph can be dragged from the darkroom onto the Video tab to
#   land in the cut — the two pages are never on screen together, so the tab is
#   the only target there is.
#
#   240 assertions.
# 13: a key on anything that is not colour — and an animated pan that was
#     coming out MIRRORED.
#
#   A grade key carries a whole develop stack because colour has to be baked to
#   a cube, which is why an animated grade costs forty-eight files on disk.
#   Everything else about a clip is ONE NUMBER, and ffmpeg takes an expression
#   for nearly all of them — so a PARAMETER KEY is a property name, a time and
#   a value, and an animated zoom is a string. `timeline anim PROJ T C
#   add|list|set|remove|clear|at`, with five eases: linear, in, out, inout and
#   hold. Which properties can be keyed is a column in the clip property table,
#   so `timeline keys` says, and the inspector's diamond appears on exactly
#   those rows.
#
#   ss_clip_prop_at is the only place a keyed property becomes a number — the
#   monitor calls it, the export generates its filter expressions from the same
#   key list, and zoompan, rotate and volume each take one. OPACITY is the
#   exception, because nothing in ffmpeg multiplies alpha by an expression: it
#   is sendcmd stepping a named colorchannelmixer, with the steps placed where
#   the value crosses a CODE VALUE and the evaluator rounding down the same way,
#   so the monitor is EQUAL to the export rather than close to it.
#
#   ⚠ And the bug that fell out of measuring it. The export positioned an
#   animated clip by sliding zoompan's CROP WINDOW — and a window sliding right
#   shows what is to the right of it, so the picture went LEFT while the
#   monitor, which has no zoompan, moved it RIGHT. Every animated pan has been
#   exporting MIRRORED since transforms existed. The test that measures the
#   picture only ever measured the zoom, which is symmetric. Position is the
#   overlay's job in both paths now, and zoompan does the zoom and nothing else.
#
#   ⚠ zoompan only ever zooms IN, so a scale under 1 — the picture smaller than
#   the frame — used to clamp to 1 in the export while the monitor obeyed it.
#   The zoom canvas is padded out for the dip now.
#
#   `timeline get --at S` reports what a clip looks like S seconds into itself,
#   which is what an inspector parked on a moving clip has to show; the window
#   follows the playhead only when the selected clip has keys, so a clip with
#   none costs nothing.
#
#   282 assertions, still clean under ASan+UBSan+LSan.
# 14: sixty transitions, a dip through a colour, and the sound crossing with
#     the picture.
#
#   Every transition is ffmpeg's `xfade` now — one filter, sixty looks: hard
#   and soft wipes, slides, covers, reveals, slices, wind, diagonals, corners,
#   circles, curtains, radial, pixelize, distance, blur, squeeze, zoom, and the
#   fades through black, white and grey. ONE table in timeline.c;
#   `timeline transitions` prints it and the window builds its picker from
#   that, so a list can never offer one the renderer does not have.
#
#   ⚠ DIRECTION. Ours names where the incoming picture comes FROM; xfade's
#   names which way the boundary TRAVELS. Measured at half progress across
#   every directional family, they are exact opposites — so every mapping in
#   the table is MIRRORED, and the four original wipes keep both their
#   direction and their soft edge by mapping to xfade's `smooth*`.
#
#   A transition is a LAYER now rather than an alpha ramp on the incoming
#   clip. The ramp bought every transition for free and could only ever be a
#   dissolve: the wipes were a geq in the export and a plain uniform fade in
#   the monitor, so a scrub and a render showed DIFFERENT PICTURES of the same
#   moment. Both build the same xfade now — the monitor buys its progress with
#   a second frame, because xfade takes its progress from a frame's timestamp
#   against the first one it saw, and one frame is always progress zero.
#
#   DIP TO A COLOUR is the one kind that is not an xfade of the two clips: it
#   is two dissolves THROUGH a colour. Both halves are xfades all the same —
#   `fade` steps by frame index while an alpha worked out from the time is a
#   few code values away from it, near enough to look right and not near
#   enough to be the same picture.
#
#   ⚠ THE SOUND. Two clips overlapping used to ADD, so a dissolve got LOUDER
#   for its length. The two ends now cross with `qsin` curves, which hold the
#   power constant; two linear fades dip about 3dB in the middle, audible on
#   anything continuous as a hole exactly where the cut is.
#
#   `timeline transition PROJ T [C] [--at S] [--kind K] [--dur D]` does the
#   whole thing in one command, including the OVERLAP, which is an edit and
#   not a property: out of the outgoing clip's handles when it has them —
#   nothing else on the timeline moves — and by rippling what follows when it
#   does not. It says which it did.
#
#   Also: a transition length set while the kind was still `none` used to
#   vanish with the line it would have been written on, so choosing the length
#   first and the kind second silently gave a transition of zero.
#
#   308 assertions, still clean under ASan+UBSan+LSan.
# 15: effects — and a format so they can be somebody else's.
#
#   Resolve takes third-party work three ways: OpenFX (compiled C++), DCTL
#   (GPU shader source) and LUTs (data). Only the third fits a program that
#   never links anything, and this is a fourth that fits it just as well: an
#   effect is a TEXT MANIFEST naming an ffmpeg filter chain and the knobs on
#   it. Somebody writes one in an editor, drops it in a folder, and it works —
#   no compiler, no ABI, nothing to rebuild when ffmpeg bumps a SONAME.
#
#       name    halation
#       param   strength  0.4  0  1   Strength
#       filter  [$in]split[a][b];[b]lumakey=0.6,gblur=sigma=$radius...[$out]
#
#   Twenty-seven ship: blur, sharpen, soft focus, glow, bloom, halation,
#   pixelate, posterise, invert, desaturate, sepia, duotone, temperature,
#   thermal, vignette, aberration, lens distortion, edges, deband, scanlines,
#   glitch, flip, flop, green screen, blue screen, luma key and despill. A
#   clip carries a STACK of them, in order, after the grade —
#   `timeline fx add|list|set|remove|move`, and an Effects panel in the window
#   built entirely from `fx list` and `fx params`, so an effect written this
#   morning appears with its own sliders and the QML never learns its name.
#
#   ⚠ A FILTER STRING CAN DO ANYTHING FFMPEG CAN, INCLUDING READ FILES. That
#   is the whole risk of shipping effects as text, and there are four walls
#   around it: a WHITELIST of filter names, a refusal of any argument that
#   names a file, nothing interpolated that was not declared as a parameter,
#   and every parameter a NUMBER — clamped to the recipe's own range and
#   printed by the engine, never carried through from the document as text. A
#   recipe that fails any of them is not loaded, so it cannot be picked.
#
#   The whitelist is also what keeps the MONITOR honest: everything on it is
#   one frame in, one frame out, same size, same answer every time. A filter
#   needing a WINDOW of frames (tmix, deflicker) would render differently on a
#   one-frame monitor than in an export; one changing the GEOMETRY (crop,
#   scale, rotate) would move the picture out from under the transform that
#   owns it; a RANDOM one would disagree with itself. None of them can be in a
#   recipe — which is why film grain stays a develop setting.
#
#   And it is RENDERED THROUGH before it is trusted: `fx check` puts one frame
#   of 64x64 grey through the chain, and so does `timeline fx add`, because a
#   recipe can name only allowed filters and still be nonsense — a misspelled
#   option, a label going nowhere — and ffmpeg does not find out until it
#   builds the graph.
#
#   ⚠ An effect this machine has not got is KEPT, whole, and written back
#   exactly as it was read. Dropping it would delete a colleague's work from
#   their project the first time it was opened on the wrong machine. It lists
#   as missing, renders as nothing, and the export says so once by name.
#
#   340 assertions, still clean under ASan+UBSan+LSan.
#
# 16 THE WINDOW WOULD NOT OPEN ON AN AMD LAPTOP.
#
#   quickshell SEGFAULTED before it drew a frame, with its own crash dialog
#   and a dump. The stack is not this program's code at all:
#
#       #2  libMangoHud.so
#       #5  vkCreateDevice              (libvulkan)
#       #8  av_hwdevice_ctx_create      (libavutil)
#       #9+ libffmpegmediaplugin.so
#       #15 QMediaPlayer::QMediaPlayer
#
#   The session exports MANGOHUD=1 so a game gets the overlay without a
#   wrapper, and MangoHud's Vulkan manifest declares
#   "enable_environment": { "MANGOHUD": "1" } — one variable that loads its
#   layer into EVERY Vulkan client. This window became one WITHOUT ASKING TO
#   BE: constructing a QML MediaPlayer constructs a QMediaPlayer, and its
#   ffmpeg backend asks libavutil for a Vulkan hardware device on
#   construction, before it has been given anything to play.
#
#   ⚠ NVIDIA NEVER SEES IT. A different hardware device is chosen there,
#   vkCreateDevice is never called, and the hook is never entered — so the
#   machine this is developed on opens the editor happily while every AMD
#   laptop gets a crash dialog. Same shape, same laptop and same fix as the
#   wallpaper engine's MangoHud crash at synui 409.
#
#   ⚠ And keeping `import QtMultimedia` out of the main QML file did NOT
#   contain it. That protects the window from a MISSING import — a failed
#   import fails the whole FILE — but this is a segfault inside the process,
#   which takes the window with it however it was reached.
#
#   `gui` now sets DISABLE_MANGOHUD=1 (the manifest's own disable_environment,
#   which beats the enable) and MANGOHUD=0 with it, before exec'ing
#   quickshell. Asserted by running `gui` with a stub named quickshell first
#   on PATH that prints the environment it was handed. ⚠ the negative
#   assertion has to be ANCHORED: "MANGOHUD=1" is a substring of
#   "DISABLE_MANGOHUD=1", so the obvious grep fails on the very environment
#   that proves the fix.
#
#   344 assertions.
#
# 17 SOMEBODY ELSE'S COLOUR, COMING IN — roadmap item 7 of 10.
#
#   `lut` is a develop SETTING now: a catalogue name or a path to a .cube,
#   with `lut.amount` beside it. Being a setting is most of the feature — it
#   rides the sidecar, the clip grade, undo, the keyframes and the GUI panels
#   because all of those are built from the one table in develop.c.
#
#   ⭐ AND THE LUT BRIDGE COMPOSES. An imported look is applied at the END of
#   the pointwise chain, in the display encoding, which is the domain a .cube
#   is defined in. ss_lut_write bakes a clip's grade by WALKING THAT SAME
#   CHAIN — so an imported look comes out inside the baked cube. The export
#   needs no second lut3d, no new graph builder and no new way for a still and
#   a frame to disagree. Measured at 55.15 dB against the still renderer,
#   against 55.7 for the pure grade: composing two lattices costs nothing
#   above the quantisation that was there already.
#
#   3D and 1D, DOMAIN_MIN/MAX honoured, the row count checked. ⚠ A TRUNCATED
#   LUT IS THE ONE THAT MATTERS: a download that stopped parses perfectly row
#   by row, and only counting them catches it — without that the missing rows
#   read as black and the top of every picture dies. ⚠ DOMAIN_MIN and
#   DOMAIN_MAX share every character through the M, so telling them apart on
#   s[7] silently sends the floor into the ceiling; it is s[8]. ⚠ Rows vary
#   RED FASTEST, and the only test that catches the other order is a cube that
#   MOVES a channel — a ramp or a gamma cannot see it.
#
#   ⚠ A LUT this machine has not got is KEPT and renders as nothing, exactly
#   as a missing effect is, and says so once by name rather than once per
#   pixel. ⚠ And NO .cube SHIPS: a LUT is somebody's licensed work far more
#   often than a slider position is. The reader, the catalogue and
#   ~/.config/synstudio/luts are all here; the tables are the user's to bring.
#
#   And a LOOK is the other half: `.synlook`, the develop stack as
#   tab-separated text, carrying only the fields it moves. Applying one SETS
#   those fields and leaves the rest — so it lands on top of the exposure and
#   white balance a photograph needed rather than throwing them away, and
#   every slider it moved is still a slider afterwards. Geometry is never in
#   one, decided by asking the table which GROUP a key is in so a control
#   added later is excluded by being put in the right place. Twelve ship;
#   ~/.config/synstudio/looks wins on a name.
#
#   `look list|show|save|apply|remove`, `luts`, `lut show`, and
#   `timeline grade --look`. `browse` lists a .cube now — without that a LUT
#   was reachable by typing a path and no other way, which is the hole a
#   project had before the picker listed one.
#
#   ⚠ A string row in a slider panel is not a slider: the Loader that draws
#   the LUT name has to HIDE the track under it, or the handle sits at zero
#   beneath the name and is draggable through the gap. And the window's grade
#   panel offers only the pointwise groups — LUT is on that list by the same
#   test as the rest, because it bakes into the cube.
#
#   379 assertions, clean under ASan+UBSan+LSan, and the suite now loads the
#   QML offscreen: a duplicate id or a bad property fails the WHOLE FILE, and
#   this repo has lost a window to exactly that twice.
#
# 18 TITLES WORTH USING, AND SUBTITLES — roadmap item 8 of 10.
#
#   A caption had words, a size, a colour and one of nine placements. It now
#   has a FACE: a family and a weight resolved through fc-match, an outline, a
#   drop shadow, a plate, line spacing, more than one line, and — for the end
#   of the film — a climb. Every one of them is a fraction of the FONT SIZE
#   rather than a pixel count, so a title styled on a 1080 timeline is the
#   same title delivered at 4K, which is the rule text_size already followed.
#
#   ⚠ A FAMILY IS RESOLVED TO A FILE, HERE, ONCE. drawtext's `font=Sans` needs
#   an ffmpeg built against fontconfig and fails the whole GRAPH when it is
#   not — at export time, after the edit — so fc-match answers with a path and
#   the path is checked before ffmpeg is handed it. ⚠ fc-match ALWAYS answers:
#   asked for a family this machine has not got it substitutes one and exits
#   0, so `did it run` is not `was it found`, and the checkbox question is
#   asked a different way (compare the family it settled ON). ⚠ A weight with
#   no family named still has to mean something — `sans-serif:weight=bold`,
#   not the default face with the tick quietly dropped.
#
#   ⚠ A LINE BREAK IS TWO BYTES EVERYWHERE. The project file is one record per
#   line, so a real newline in a caption would end the record halfway through
#   and the rest would read as a fresh clip; it travels as \n, and `set` and
#   `get` speak the same escape so the window can show and edit it in a
#   one-line field. ⚠ ss_clip_get used to print the caption RAW, which is the
#   same bug on the way out. ⚠ CO_TEXT wrote with `sizeof c->text` whatever
#   field it was handed — correct while `text` was the only one of them, and a
#   64-byte overflow the moment `text.font` arrived; the table carries each
#   member's own size now.
#
#   Five styles set those fields together — plain, lower third, subtitle,
#   heading, credit roll — and then get out of the way: every field is still a
#   slider afterwards, the bargain a look strikes with a grade. Built in
#   rather than a file format, because there is nothing in one a third party
#   could not say in four `timeline set` commands.
#
#   ⚠ THE ROLL IS THE ONE TITLE THAT MOVES, so it is generated twice: an
#   expression in the export, where a clip's `t` is running, and a NUMBER in
#   the monitor, which holds ONE frame at t=0 and would otherwise draw every
#   roll at its starting position while the export scrolled it. Same bargain
#   chain_grade_at strikes, same reason.
#
#   SUBTITLES: a cue is a TITLE CLIP. Not a fourth clip kind and not a track
#   type of its own — so an imported caption takes the font, the plate, the
#   placement, the fades, the transform and the grade that a typed one takes,
#   is edited by the commands that already exist, and burning it in is free
#   because that is what a title does. `.srt` in and back out, CRLF and the
#   full-stop separator both, markup dropped and words kept, cue numbers
#   ignored because they are not what separates one cue from the next.
#   Shipping them SOFT instead is `timeline export --subs FILE`: mov_text,
#   srt or WebVTT by container, and ⚠ the input goes in LAST, because every
#   label in the graph names an input by NUMBER and a file inserted anywhere
#   else renumbers the clips and hands the timeline the wrong pictures.
#
#   ⚠ text_align is ASKED FOR, not assumed. It reached drawtext in 2024 and an
#   option this ffmpeg does not know fails the graph to parse rather than
#   being ignored, so it is probed once and the layout without it is what it
#   always was.
#
#   416 assertions, clean under ASan+UBSan+LSan. ⚠ The test that proves a line
#   break reached the PICTURE cannot measure the frame's average: the same
#   words on two lines carry the same ink (17.0603 against 17.061). It
#   measures a band the one-line caption cannot reach.
#
# 19 RETIME AND THE STABILISER — roadmap item 9 of 10.
#
#   ⭐ AND A BUG THAT FAILED EVERY SLOW-MOTION EXPORT. atempo's range starts at
#   0.5 and the speed property's at 0.1, so any clip under half speed WITH A
#   SOUND TRACK ON IT died at the end of the render — "Value 0.200000 for
#   parameter 'tempo' out of range", after the encode had been running for
#   minutes. Halvings multiply, so a chain of them reaches any slowdown: 0.2
#   is 0.5 x 0.5 x 0.8.
#
#   A clip can run backwards, hold one frame, or RAMP. A ramp is keys on
#   `speed`, and it moves the TIMEBASE rather than a number inside it: the
#   clip's length becomes the integral of 1/speed over the source. So the
#   curve is sampled ONCE into constant-speed segments, and the length, the
#   frame the monitor seeks to, the export's piecewise setpts and the tempo
#   the sound runs at all read that one table. ⚠ A ramp's keys are in SOURCE
#   seconds, the only keyed property that is — a ramp says "at this point in
#   the shot", and on the output axis the length is an equation to solve
#   rather than an integral to take.
#
#   ⚠ THE MONITOR-EQUALS-EXPORT TEST CAUGHT TWO OF THESE. A freeze inherited
#   the input seek every other clip uses, so it held the clip's IN POINT
#   rather than the instant asked for — convincingly, which is why it needed
#   measuring against the source and not looking at. And a reverse was one
#   frame out: `reverse` hands out frame N-1 first, whose instant is one frame
#   before the end of the span, so mirroring the TIME alone lands next door.
#   A one-frame miss measures 19 dB here; the same frame through two encoders
#   measures 39.
#
#   ⚠ A ramp outside 0.5-2x drops that clip's sound and says so by name. One
#   atempo can be driven by sendcmd; a CHAIN of them cannot be commanded as a
#   unit, and the alternatives are a graph that fails or a sound that drifts
#   out of sync with its own picture. ⚠ And the commands are timed on the
#   SOURCE axis: asendcmd sits BEFORE atempo, so the frames it is timing have
#   not been stretched yet — on the output axis the sound came out 2.579s
#   against the picture's 2.760s.
#
#   Optical flow and frame blending are `retime=flow|blend`, minterpolate
#   either way, and only where the timebase actually moved: at 1x every output
#   frame IS an input frame.
#
#   THE STABILISER is two passes and the first cannot be part of a graph.
#   `timeline stabilise` runs vidstabdetect over the clip's source range and
#   writes a .trf into `<project>.stab`; the graph reads it. ⚠ At the SOURCE's
#   own size, because what it writes is PIXELS — which is why the transform
#   goes in before anything scales the picture. ⚠ `--off` keeps the analysis:
#   it took as long as a render to make. Measured steadier — frame-to-frame
#   difference 7.08 before, 4.13 after.
#
#   446 assertions, clean under ASan+UBSan+LSan. Two of them asserted that
#   `speed` could NOT be keyed; they were right until this release and now
#   name a property that still cannot.
#
# 20 THE QML TEST FAILED THE BUILD ON A SLOWER MACHINE.
#
#   ⚠ A test whose PASS condition is "this finished starting in 25 seconds"
#   is a test that fails on whatever is slower than the machine it was written
#   on. It did: a ThinkPad running syn-update could not build synstudio at all
#   because quickshell had not reached "Configuration Loaded" inside the
#   timeout — while compiling this same package. No QML error was reported;
#   there was nothing wrong with the window.
#
#   ⚠ AND THE ASSERTION THAT WAS SUPPOSED TO CATCH A BROKEN WINDOW COULD NEVER
#   HAVE FIRED. It looked for `Error:`. quickshell prints `ERROR: Failed to
#   load configuration` and then the reason — so the needle matched neither
#   the marker nor the message, and a shell that failed to load would have
#   passed both halves. Found by breaking a QML file ON PURPOSE and reading
#   what the tool actually says, which is the only way to know.
#
#   So the FAILURE condition is an ERROR line, the pass condition is a loaded
#   configuration, and a shell that never started says so and asserts nothing.
#
# 21 AND THE FIX FOR THAT MADE THE SUITE TAKE 96 SECONDS.
#
#   ⚠ A shell that loads SUCCESSFULLY runs forever, so `timeout N quickshell`
#   always costs the full N. Raising it from 25 to 60 seconds to help a slow
#   machine therefore added 35 seconds of pure waiting to every build on every
#   machine — and the suite renders real video, so it was already the longest
#   thing in the package. 96 seconds here is minutes on a laptop, and meson
#   kills a test at its timeout and FAILS THE BUILD.
#
#   Polled instead: watch the log, stop the moment either answer appears. 96
#   seconds back down to 38. And meson's own timeout goes to 900, because this
#   suite renders a slow-motion export and a stabiliser analysis and 300 was
#   set when it was 344 assertions.
#
#   ⚠ Also hardened the bold-face assertion, which was environmental in the
#   same way: it asserted that fc-match answers with a path containing "Bold",
#   and a machine with no bold face installed would have failed the BUILD for
#   having different fonts. It compares the bold answer to the regular one and
#   skips when they are the same.
#
# 22 THE QML TEST CRASHED QUICKSHELL ON AN AMD LAPTOP.
#
#   The real reason the ThinkPad could not build this package, and it was
#   never the timeout: `ERROR: Quickshell has crashed under pid ...`.
#
#   ⚠ THE TEST RAN QUICKSHELL DIRECTLY, SO IT BYPASSED THE LAUNCHER — and the
#   launcher is what sets DISABLE_MANGOHUD=1. The session exports MANGOHUD=1,
#   which loads MangoHud's Vulkan layer into every Vulkan client; a QML
#   MediaPlayer constructs a QMediaPlayer, whose ffmpeg backend calls
#   av_hwdevice_ctx_create on construction; on AMD that segfaults inside
#   MangoHud's own vkCreateDevice hook and takes the shell with it. NVIDIA
#   never reproduces it, so it passed on the development desktop every time.
#
#   That is EXACTLY the bug pkgrel 16 fixed, re-run by a test written after
#   it. A test that loads the QML file directly has to supply the environment
#   the launcher would, or it is not testing the window — it is reproducing a
#   fixed crash on whichever machines still have the hardware for it. There is
#   a separate test asserting the launcher sets the variable; this one now
#   sets it too.
#
#   Verified both ways on the desktop: without the guard MangoHud loads into
#   quickshell, with it the layer is absent and the window still loads.
#
# 23 SCOPES — roadmap item 10 of 10, first half.
#
#   ⚠ AND AN UNBREAKING. pkgrel 21 staged meson.build for a timeout change and
#   swept in a line naming `src/scope.c`, which was not committed — so 21 and
#   22 could not CONFIGURE on any machine but this one, where the file exists
#   untracked. `git add <file>` takes the whole file, not the hunk you meant.
#
#   A waveform, an RGB parade and a vectorscope, computed HERE rather than by
#   an ffmpeg filter — the same bargain the histogram strikes. A scope is read
#   to decide whether a shot is legal and whether two shots match, and an
#   answer from a different renderer than the picture is an answer about
#   something else. `synstudio scope FILE` measures a photograph through its
#   own develop stack; `timeline scope PROJ --at T` composites the frame first,
#   so it describes the picture that will be delivered.
#
#   Measured in the DISPLAY encoding, like the histogram: a waveform in linear
#   light puts middle grey at 18% and nobody reads one there.
#
#   ⚠ NOT normalised by the busiest cell. The first version was, and a
#   vectorscope of colour bars came out almost black — the greys pile into a
#   few cells at the centre, that peak is enormous, and every hue around it
#   divides down to nothing. Referenced to the mean of the OCCUPIED cells
#   through a curve that saturates instead, which is scale-invariant: the same
#   picture reads the same at any scope size and any resolution.
#
# 24 DELIVERY — roadmap item 10 of 10, and the build order is finished.
#
#   A RENDER RANGE, in the document because it is set while looking at the
#   cut. Trimmed at the END of the graph rather than by seeking the inputs:
#   every clip is placed at its own tl_in and composited onto a base the
#   length of the whole timeline, so a range is a WINDOW onto the finished
#   picture and not a different edit. ⚠ setpts=PTS-STARTPTS after the trim, or
#   a render of minutes nine to ten arrives as a file with nine minutes of
#   nothing at the front of it.
#
#   SEVEN PRESETS, named for where they are going. Applied by rendering the
#   whole composite at that size — every clip, the base, the titles and the
#   transitions are built from the project's own dimensions, so changing those
#   changes all of them together and nothing is scaled afterwards.
#
#   BURN-IN (timecode, name, both) on the delivery arguments and never in the
#   document: it is for a review copy and must not survive into a master.
#   ⚠ The timecode starts at the RANGE — the trim resets timestamps, and
#   00:00:00 on a render that starts nine minutes in is a wrong answer to the
#   exact question a burn-in was added to answer. ⚠ Bottom RIGHT, because a
#   subtitle is bottom centre and the two landed on each other on the first
#   review copy this made.
#
#   IMAGE SEQUENCES: png and exr rows with `--out dir/f_%04d.png`. ⚠ A format
#   with no audio codec has nowhere to put the sound, and mapping the mix into
#   one fails the whole render after the encode has started.
#
#   A RENDER QUEUE that is a FILE OF COMMANDS, not a daemon. One job per line:
#   the arguments of a `timeline export`, run by re-invoking this binary. A
#   job somebody typed and a job the window queued are the same object, and
#   there is no second code path that renders things. ⚠ The queue is kept
#   after a run: a job that failed is a job to look at.
#
#   483 assertions, clean under ASan+UBSan+LSan.
#
#   ⚠ TWO TEST BUGS FOUND WHILE WRITING THIS, BOTH THE SAME SHAPE. `psnr` of
#   two IDENTICAL pictures is `inf`, and `$2+0` on that is ZERO — so an
#   assertion that two frames match failed precisely when they matched. And
#   the fix for THAT, `$2 ~ /inf/`, matched the `psnr_g:inf` sitting further
#   along a line whose average was 4.86, so two completely different pictures
#   read as identical. Test the FIRST TOKEN.
#
# 25 SHOT MATCH — and ⭐ A WHITE PIXEL THAT RENDERED BLACK.
#
#   ⭐ THE BUG FIRST, because it was here long before the matcher found it.
#   apply_contrast is the cubic -4v^3+6v^2-2v, which is a contrast curve on
#   [0,1] and a cliff outside it. Anything that lifts a highlight past white
#   leaves an encoded value above 1 in the float pipeline; at 5.55 that cubic
#   returns MINUS 510, so `gain = nv / v` comes out NEGATIVE and every channel
#   of the pixel is multiplied by a negative number:
#
#       exposure=8 contrast=0  ->  255 255 255
#       exposure=8 contrast=5  ->    0   0   0
#
#   Five points of contrast turned white into black. The tone-region block
#   immediately above it had always clamped its input; this one never did. The
#   fix applies the curve to the clamped value and keeps the excess, which is
#   a NO-OP everywhere inside the display range — sc(0) and sc(1) are both
#   zero — and leaves an over-range highlight over-range, which is what it is.
#
#   SHOT MATCH is fitted, not solved. Every control has a transfer function of
#   its own, and solving one in closed form means writing a second model of
#   what colour.c does — a model that drifts the first time colour.c is
#   improved. So each control is set, rendered THROUGH THE REAL ENGINE,
#   measured and bisected: correct by construction, and it never needs to know
#   what `contrast` means. Brightness, contrast and white balance; not a
#   three-way grade, because there are no per-channel lift/gamma/gain controls
#   here. 22.5 dB apart before, 33.1 dB after, on a realistic pair.
#
#   ⚠ THE AXES ARE CHOSEN TO MATCH THE CONTROLS. Warm against cool is red over
#   blue, which is what a temperature does; green against magenta is green
#   over the other two, which is what a tint does. Measuring R/G and B/G
#   instead gives two numbers that BOTH controls move, and the descent chases
#   its own tail — the first version did, and pinned tint at its limit.
#
#   ⚠ RANGES COME FROM THE TABLE, and it is the UI range. `temp` is KELVIN
#   (2000..12000), not a -100..100 slider — hardcoding that made every `set`
#   fail as out of range, silently, so the fit left the control alone. And the
#   ACCEPT range is 0..50000, where 0 means "as shot": bisecting THAT walks
#   out of the sensible part of the control.
#
#   ⚠ ss_develop_describe returns ZERO on success and -1 past the end, while
#   ss_clip_describe returns 1 on success and 0 past the end. A plain truth
#   test on the first stopped the lookup loop at row zero, every range missed,
#   and the matcher reported a perfectly successful match that had changed
#   nothing at all.
#
#   ⚠ And the statistics CLAMP, the way the histogram clamps. What is matched
#   is a DISPLAYED picture and a value above white is white; unclamped, a shot
#   pushed eight stops reported a mean of 5.0 rather than 1.0, so the fit
#   believed brightness climbs forever and pinned exposure at MINUS eight.
#
#   497 assertions, clean under ASan+UBSan+LSan.
#
# 26 THE SOUND CHAIN — the audio section, closed out.
#
#   Noise reduction, a gate, six bands of EQ, a compressor, a de-esser, fade
#   SHAPES, delivery loudness and ducking. The roadmap's own table called
#   audio the weakest area in the program; it is not any more.
#
#   THE ORDER IS THE DESIGN: clean it, shape it, control it. A gate AFTER a
#   compressor gates a signal whose quiet parts have already been lifted, and
#   a de-esser BEFORE an EQ chases sibilance the EQ is about to move. The test
#   asserts the order as positions in the graph string, so a reshuffle that
#   still contains every filter fails it.
#
#   ⚠ Zero means the filter is NOT IN THE GRAPH, not in it doing nothing. A
#   six-band EQ that always emits six biquads is six passes over the samples
#   to do nothing four times, and afftdn is not cheap.
#
#   ⚠ A CHAIN OF `equalizer`, NOT ONE `anequalizer`. anequalizer is PER
#   CHANNEL: a stereo clip needs every band written twice and a mono one
#   written once, which is a shape that gets out of step with the source the
#   first time somebody swaps a take.
#
#   ⚠ DUCKING SPLITS THE KEY TRACK. Every clip on it has already been spent on
#   the main mix, and naming a stream twice fails the whole graph — so the key
#   is mixed to one stream, split once per clip it keys plus once for the mix
#   itself, and the main amix's input count follows what was actually named.
#   Measured: identical to un-ducked where the key is silent, 2.6 dB down
#   where it is not.
#
#   ⚠ SINGLE-pass loudnorm, deliberately. Two-pass measures the whole
#   programme and then encodes it, which is rendering the timeline twice.
#   `loudness FILE` measures what came out — -14.00 against a -14 target.
#
#   523 assertions, clean under ASan+UBSan+LSan.
#
# 27 COPY, PASTE, DUPLICATE — and a grade onto a whole scene.
#
#   ⚠ THE CLIPBOARD IS A ONE-CLIP DOCUMENT, written and read by the same two
#   functions the project file uses. Not a struct dumped to disk: an ss_clip
#   carries four curve tables and a develop stack, its layout changes whenever
#   a control is added, and a binary clipboard would then be a file that
#   silently means something different after an update. The text format
#   already has to survive that — it is what every project file is — so the
#   clipboard gets it for free, including the grade, the parameter keys, the
#   effect stack, the sound chain and the retime.
#
#   `paste --grade` is the half that saves an afternoon: ONLY the develop
#   stack, leaving the target's timing, framing and sound alone, and with
#   `--all` onto every clip on the track. ⚠ It clears the target's grade KEYS,
#   because a key holds a whole develop stack — leaving them would leave the
#   clip being DRIVEN by the grade it had while claiming to wear the new one.
#
#   537 assertions, clean under ASan+UBSan+LSan.
#
# 28 VERSIONS AND A WATERMARK.
#
#   Undo was already the auto-save half — every save records the state it left
#   — but it is a RING of a hundred states and the oldest falls off the end. A
#   version is a document somebody decided to KEEP, named, in
#   `<project>.versions/`, that nothing expires and no edit disturbs.
#
#   ⚠ A RESTORE GOES THROUGH THE ORDINARY SAVE PATH, so it is itself undoable.
#   A restore that could not be undone would be the one operation in this
#   program capable of losing work.
#
#   ⚠ A version NAME BECOMES A FILE. A slash or a leading dot is refused
#   rather than sanitised: quietly turning `a/b` into `a_b` means a later
#   `restore a/b` cannot find what it just saved.
#
#   The watermark is a PICTURE, so unlike the burn-in it cannot be a filter on
#   the end of the chain — it is another input, and it goes in LAST for the
#   same reason the subtitle input does: every label in the graph names an
#   input by NUMBER. Sized as a fraction of the frame, so one file marks a
#   1080 delivery and a 4K one identically.
#
#   ⚠ A clean build showed a truncation warning the incremental one had
#   cached away. Configure a fresh build directory before believing a tree is
#   warning-free.
#
#   551 assertions, clean under ASan+UBSan+LSan.
#
# 29 LINKED AUDIO AND VIDEO.
#
#   Routing separated the picture from the sound — a video clip's dialogue
#   plays on whatever track it sits on — which is exactly what makes a link
#   necessary: without one, moving a shot leaves its sound where it was.
#
#   A GROUP ID, not a pointer to a partner. A link is not necessarily a pair
#   (a shot, its dialogue and its room tone is three), a pointer would not
#   survive being written to a text file, and an index would not survive the
#   clip beside it being deleted.
#
#   Move, trim and delete apply to the group IN THE ENGINE and not in the CLI,
#   so a drag in the window and a `timeline move` from a script behave the
#   same way.
#
#   ⚠ A MOVE CARRIES THE DELTA, NOT THE DESTINATION. Moving every linked clip
#   TO the same instant would stack a shot's dialogue on top of it instead of
#   keeping the offset it was cut with.
#
#   ⚠ AND A TRIM AGREES ONE DELTA ACROSS THE GROUP FIRST. A head trim CLAMPS
#   to what each clip's source allows, so two linked clips with different in
#   points, asked for more than either has, clamp to DIFFERENT amounts and
#   come out of sync by the difference — silently. The test builds that case
#   on purpose (a second of handle against two tenths) where an unclamped
#   group drifts 0.8s. A member can still refuse outright, and then none of
#   them move.
#
#   564 assertions, clean under ASan+UBSan+LSan.
#
# 30 TRACK AUTOMATION — a fader ridden against the picture.
#
#   The same shape as a clip's parameter keys, sharing their interpolation and
#   their eases: the expression generator was split so a track's curve and a
#   clip's produce the identical form rather than two implementations of one
#   interpolation.
#
#   ⚠ ITS KEYS ARE IN TIMELINE SECONDS. A clip's are relative to the clip,
#   because a clip can be MOVED and its keys have to move with it; a track
#   cannot be moved, and its fader is set against what is on screen. That
#   difference is the whole reason this is a separate list and not a clip
#   property applied to a track.
#
#   ⚠ AND SO THE EXPORTED EXPRESSION'S TIME VARIABLE IS SHIFTED by where each
#   clip starts — the chain it is spliced into runs in CLIP seconds, the trim
#   and the speed setpts being behind it. Without the shift every clip on the
#   track rides the automation from the top of the programme, which for a clip
#   four seconds in is four seconds of the wrong curve. Measured: a bed under
#   a fader at -24 dB from four seconds on renders 24 dB down where it plays,
#   against the same bed with no automation.
#
#   574 assertions, clean under ASan+UBSan+LSan.
#
# 31 LOG INPUT TRANSFORMS — what the file's numbers MEAN.
#
#   A camera shooting log records scene light through a curve of its own, and
#   an image loader decodes it as sRGB because that is what image loaders do.
#   The picture comes out flat and washed and no amount of contrast puts it
#   right, because the numbers were never sRGB in the first place.
#
#   `log = none|slog3|vlog`, applied FIRST — before white balance, before
#   anything else reads a pixel. It UNDOES the loader's sRGB decode and
#   applies the camera's curve instead, landing in the linear scene light the
#   rest of the stack expects. Undoing rather than skipping: the decode has
#   already happened by the time a develop stack sees a pixel, and re-plumbing
#   every loader to ask a develop setting would put this decision in a dozen
#   places instead of one.
#
#   ⚠ ONLY CURVES WITH A CHECKABLE ANCHOR. Each maps 18% grey to a code value
#   the manufacturer publishes, and `synstudio logcurve` exposes the transform
#   so the suite can assert it IN FLOATING POINT. An 8-bit render cannot tell
#   a subtly wrong constant from a right one — this pipeline's own round trip
#   loses a code either way (an identity render of 150 comes back 149) — and a
#   transform whose constants are slightly off makes a PLAUSIBLE picture,
#   which is the worst kind of wrong.
#
#   ⚠ AND THE TEST CAUGHT THE DOCUMENTATION, NOT THE CODE. V-Log's 0.599 is
#   100% reflectance, not 90%: solving the curve for 0.90 gives 0.588167. The
#   spec's table lists 0.599 beside 90% and it is easy to read as the anchor.
#   The formula was right and the comment above it was wrong, which is the
#   version of this mistake that survives review.
#
#   586 assertions, clean under ASan+UBSan+LSan.
#
# 32 A TITLE STYLE YOU CAN ACTUALLY REACH.
#
#   Five title styles shipped at pkgrel 18 and the only door into them was
#   `--style` on `timeline title` — at CREATION, from a shell. Every title
#   that arrived any other way could never use one: the window's Title button
#   passes no style, a subtitle import makes a hundred of them and then says
#   "the style is something to change if the picture underneath wants it",
#   and there was nothing to change it with. A feature with no way in.
#
#   `timeline style PROJ TRACK CLIP NAME` restyles a title that is already
#   there, and the cutting room's Title group lists the styles above the
#   controls they move — the same shape, and the same place, as the
#   darkroom's Looks list.
#
#   ⚠ A VERB, NOT A ROW IN THE CLIP TABLE. Everything else in the inspector
#   is a property, and a `style` property is the obvious way to do this. It
#   is also wrong twice over. A style is a STARTING POINT — it sets seven
#   fields and every one of them is still a control afterwards, exactly as a
#   look does to a grade, so there is nothing to read back and a picker
#   claiming to show the style in force would be inventing it. And
#   `ss_clip_set` is shared with the project READER: a property whose set
#   applied a preset would restyle the title, over numbers someone had since
#   tuned by hand, every time the file was opened. That is the same bug
#   `ss_develop_set` had when a `crop.*` key quietly enabled cropping.
#
#   Refused on a clip that is not a title, because a style is only ever text
#   fields — on a background it would report success and change nothing
#   anybody can see.
#
#   593 assertions, clean under ASan+UBSan+LSan.
#
# 33 A FONT PICKER, INSTEAD OF A FIELD TO TYPE INTO.
#
#   `text.font` is a TEXT row in the clip table, so lettering a title in
#   anything but the default meant TYPING a family name — no list, no spelling
#   to check against, and no way to find out what the machine had. `synstudio
#   fonts` has existed the whole time to answer exactly that and nothing ever
#   called it. A typo did not fail: fc-match resolves it to something else and
#   the title comes out lettered in a face nobody chose, which reads as
#   deliberate.
#
#   The Title group's Font row now carries a ▾ with the family COUNT on it,
#   and opens a filtered list read once from the engine at startup. Each row
#   is DRAWN IN ITS OWN FACE — a list of family names all set in the same font
#   tells you their spelling and nothing else, and what one looks like is the
#   whole question. The field stays, so a family this machine has not got can
#   still be typed for a render happening elsewhere, and its border goes red
#   when the name is not one fontconfig knows.
#
#   ⚠ OPENED BY A BUTTON, NOT BY FOCUS. Focus is the obvious trigger and it
#   does not work: clicking a row takes focus off the field, which closes the
#   list out from under the click, and the row never fires.
#
#   ⚠ AND THE FILTER IS ITS OWN FIELD. Filtering with the value field would
#   commit what was typed — type "jet" to narrow, click JetBrains Mono, and
#   the field loses focus FIRST, so the title is lettered in a family called
#   "jet" for as long as the click takes to land. Two fields, no race.
#
#   Proven by an offscreen probe on a copy of the window, not by the file
#   loading: 255 families reached the window, the list delegates instantiated,
#   and they rendered in 24 distinct faces. "Configuration Loaded" would have
#   said yes to an empty picker.
#
#   600 assertions, clean under ASan+UBSan+LSan.
#
# 34 A PROJECT CAN BE GIVEN A NAME — AND A DROPPED FILE STARTS ONE.
#
#   There was no way to save a project, and there did not need to be one:
#   every verb in the engine ends in a write, so the .syntl on disk is the cut
#   as it stands after each edit and closing the window has never lost a
#   frame. What was missing was a NAME. Both doors into a new
#   project — the start screen and the toolbar — wrote the same fixed path,
#   `~/synstudio-project.syntl`, so starting a second project SILENTLY
#   DESTROYED the first, and there was no way to say "this one is Holiday".
#
#   `timeline saveas PROJ --out PATH [--force]` writes the document somewhere
#   else and prints where; the window then edits the copy, because a Save as
#   that leaves you editing the old file has made a copy, not a save. New
#   project and Save as are one sheet with a name field in it.
#
#   ⚠ NEITHER WRITES OVER A FILE ON THE FIRST PRESS. `new --no-clobber` and
#   `saveas` answer exit 3 for "that name is taken", which is an ANSWER — the
#   sheet turns it into a Replace button rather than an error, the way `peaks`
#   answers 100 for "no audio". Plain `new` still clobbers: every script and
#   most of the suite already asks it for exactly that, and changing it
#   underneath them would be a silent regression of its own. `new --unique`
#   takes the next free name and PRINTS it — that is what the start screen
#   uses now, so a new project can never land on another one.
#
#   ⚠ AND A SAVE UNDER A NEW NAME CARRIES THE STABILISER'S WORK. Those
#   measurements live in `<project>.stab`, keyed to the project's NAME, so the
#   copy would look right, open right, and render every stabilised clip
#   UNSTEADY. No error and no message: the shake would simply be back.
#
#   Dropping a photograph on the Video tab with nothing open used to say
#   "start a project first" — an instruction to go and do the thing the drop
#   had already asked for. It starts one and takes the drop.
#
#   And copy/paste, which were engine verbs with no door into them from the
#   window: Copy and Paste in the cutting room, pasting at the PLAYHEAD.
#   `timeline clipboard` says what is on it without pasting to find out, so
#   Paste is inactive with nothing there rather than pressed to discover it —
#   the clipboard is a file that outlives the window, so that is a question
#   only the engine can answer. ⚠ Re-read when the copy has RUN, not when it
#   is queued: edits queue, and asking early reports the PREVIOUS copy.
#
#   ⚠ `--at 0` is a POSITION. `o.at > 0` cannot tell "not given" from "the
#   head of the cut", so a paste at 0 put the clip back where it was copied
#   from — at the one place somebody pasting at 0 is looking.
#
#   622 assertions, clean under ASan+UBSan+LSan.
#
# 35 KEYS, UNDO IN THE DARKROOM, AND A PHOTOGRAPH THAT ARRIVES DEVELOPED.
#
#   The window bound NOTHING. Every action was a button, the transport
#   included, and a cutting room whose play, step and split are mouse-only is
#   one nobody can work quickly in.
#
#   Space plays. L plays and doubles on each press — the PLAYER's rate on the
#   rendered preview, so a fast pass is still the export, played. K stops.
#   Arrows step a frame, with Shift a second; Home and End; S splits, T makes
#   a transition, M marks, Del and Shift+Del delete and ripple delete;
#   Ctrl+C/Ctrl+V copy and paste at the playhead; Ctrl+Z and Ctrl+Shift+Z;
#   Ctrl+S saves the cut under a name (and exports the photograph, which is
#   what saving a still means here); Ctrl+O, Ctrl+E, Ctrl+N. `?` lists them,
#   and so does a Keys button — a binding nobody can find is half a feature.
#
#   ⚠ J CANNOT BE WHAT L IS. Nothing plays an encoded preview backwards, and
#   rendering the timeline in reverse to watch it would be a second renderer
#   with its own opinion of the cut — the thing this program has refused since
#   the first commit. J shuttles the frame monitor back at 1, 2 or 4 frames a
#   step, and the status line and the key sheet both say so rather than
#   letting the speed imply something the picture cannot do.
#
#   ⚠ A FOCUS ITEM, NOT `Shortcut` OBJECTS. Qt matches a shortcut BEFORE the
#   key is delivered to whatever has focus, so Ctrl+C over the project-name
#   field would copy a clip instead of the word under the cursor, and J would
#   shuttle the cut while somebody typed "Jan" into a name. A focused
#   TextInput swallows its own keys and the catcher never sees them; focus
#   comes back to it whenever a sheet closes.
#
#   THE DARKROOM HAS UNDO NOW — `undo FILE`, `redo FILE`, `history FILE`, off
#   the same machinery the cutting room uses, because a photograph's document
#   is its sidecar and history is a property of a file. Every verb that writes
#   a sidecar ends in dev_save, exactly as every timeline verb ends in
#   tl_save.
#
#   ⚠ THE UNTOUCHED PHOTOGRAPH HAS TO BE REACHABLE. ss_history_seed can only
#   snapshot a file that exists, so a first edit writes the defaults FIRST and
#   seeds from those — otherwise the oldest state undo knows is the one the
#   first slider produced, and the picture as it arrived is gone the moment
#   anything moves.
#
#   ⚠ RESET IS AN EDIT, NOT A DELETION. Unlinking the sidecar put the work it
#   threw away outside the history entirely — nothing on disk left to
#   snapshot, and no way back from a mis-click. It writes the defaults now; a
#   sidecar of defaults renders identically to none, which is why `get` works
#   on a photograph nobody has touched.
#
#   ⚠ AND A DRAG IS ONE STEP BACK, NOT A HUNDRED. A slider is one `set` per
#   tick; recording each would fill a hundred-deep ring with one slider's
#   journey and Ctrl+Z would walk back through it a hundredth of a stop at a
#   time. Ticks carry --no-history and the release commits once.
#
#   AND A PHOTOGRAPH ARRIVES IN THE CUT DEVELOPED. A still dragged from the
#   darkroom onto the Video tab landed with a default grade, so the shot in
#   the cut was the picture as the camera left it — worse than an error,
#   because the only clue is that it looks exactly original. A clip's grade IS
#   an ss_develop, the same table the sidecar holds, so the stack travels by
#   assignment and every setting is still a row in the inspector. `--flat`
#   takes the file as it is. ⚠ Masks do not travel: a mask is a local
#   adjustment on the ss_edit and a clip has no such list. ⚠ An identity stack
#   is NOT marked as a grade — a clip claiming one it has not got means
#   something different to every reader downstream.
#
#   ⚠ Proven by the picture, not the flag: the developed frame measures 59
#   YAVG against the flat one's 36. A grade copied into the clip and then not
#   read by the frame renderer would pass every other assertion here.
#
#   645 assertions, clean under ASan+UBSan+LSan.
#
# 36 A SOURCE MONITOR, AND THE TWO EDITS THAT COME OFF ONE.
#
#   The cut had one viewer and one way in: a whole file landed at the end of a
#   track and was trimmed afterwards. What an editor does is decide the in and
#   the out ON THE FOOTAGE first, put the playhead where it goes, and let the
#   third point follow from the other two.
#
#   `synstudio source FILE --at S --out F.png` is one frame of a file that is
#   not in a project yet — the other half of `timeline frame`, which
#   composites one that is. `timeline insert` and `timeline overwrite` are the
#   two ways a marked range lands.
#
#   ⚠ AN INSERT RIPPLES EVERY TRACK. That is what an insert edit IS: moving
#   one track would slide a shot off its own dialogue, and this program has
#   linked clips precisely because those belong together. A clip the point
#   lands inside is split first, so its tail travels with everything else.
#
#   ⚠ AN OVERWRITE CUTS A HOLE ITS OWN LENGTH, splitting at BOTH ends before
#   removing what lies entirely inside. Splitting is what keeps the far side's
#   SOURCE position: a hole cut by deleting and re-adding would restart the
#   remaining half of the shot it cut into, which looks like a jump nobody
#   made. Clips are removed highest index first — the same rule the window's
#   multi-delete had to learn, because removing one renumbers the rest.
#
#   ⚠ `ss_timeline_push` is a SEPARATE function, not a negative length to
#   ss_timeline_ripple: that one clamps at the point so a gap can never be
#   over-closed, and the clamp is exactly wrong here — it would pile every
#   pushed clip onto the insert point.
#
#   ⚠ THE VIEWER RENDERS THROUGH THE FILE'S SIDECAR. An insert brings a
#   photograph's develop with it (pkgrel 35), so a flat source viewer beside a
#   graded timeline is a disagreement the eye catches at once and cannot
#   explain. And a still is decoded at 0 whatever time is asked for: -ss past
#   the end of a one-frame input yields nothing at all.
#
#   ONE VIEWER, SWITCHED, not two side by side: on a 1400-wide window a pair
#   leaves neither big enough to judge a shot on, and judging the shot is what
#   a source monitor is for. I and O mark, comma and period insert and
#   overwrite — what a hand that knows another NLE will already try.
#
#   662 assertions, clean under ASan+UBSan+LSan.
#
# 37 THE PHOTO DRAG NEVER WORKED, AND arnndn's MODEL.
#
#   ⛔ DRAGGING A PHOTOGRAPH ONTO THE VIDEO TAB FLASHED AN OVERLAY AND DID
#   NOTHING — and had done since the gesture shipped at pkgrel 12, because the
#   only thing ever tested was `dropUrls()` called directly. TWO faults, both
#   invisible in the file:
#
#   1. `fileDrop` fills the window and is declared LAST, so it is the topmost
#      target for every drag — including the window's own. It took the photo
#      drag, found no urls on it, refused, and lit its own "drop a
#      photograph" overlay on the way past. The tab never saw the drag. That
#      flash IS the bug report. A DropArea whose `keys` do not match is not
#      entered and delivery carries on underneath, so the drag names itself
#      (`Drag.keys: ["synstudio.photo"]`) and the window-wide area takes
#      files only.
#
#   2. `drag.target` moves an item by the mouse DELTA, and the proxy was reset
#      to the picture pane's TOP-LEFT CORNER on every press — so the point
#      being hit-tested was as far up and left of the pointer as the press was
#      down and right of the corner. Pressing in the middle of the photograph
#      and dragging to the tab put the proxy off the window entirely. It
#      starts under the pointer now.
#
#   ⚠ TESTED BY DRIVING THE REAL DRAG, in a real window, offscreen: press in
#   the MIDDLE of the picture, move the proxy to the tab, assert the tab sees
#   it, that the drop returns CopyAction, and that a clip lands. No grep over
#   the QML could have caught either fault, and both survived a feature that
#   looked finished.
#
#   AND `arnndn` — the last named gap in the audio chain. A trained denoiser
#   is nothing without its model file, which is somebody else's licensed work,
#   so none ship: `nr.model` is a catalogue name or a path, found the way a
#   LUT is (installed, then ~/.config/synstudio/rnn, then SYNSTUDIO_RNN), and
#   `nr` becomes its mix. One knob, two answers to the same question.
#
#   ⚠ A LISTED MODEL IS ONE FFMPEG ITSELF ACCEPTED. There is no parser for the
#   format here, and the alternative to asking is listing a file that fails in
#   the middle of a delivery render — the same bargain `fx check` strikes with
#   an effect recipe. A bogus .rnnn is simply not in the catalogue.
#
#   ⛔ A MODEL THIS MACHINE HAS NOT GOT LEAVES NO DENOISER AT ALL, and the
#   name is kept. Falling back to afftdn would be substituting a different
#   denoiser for the one that was approved, silently. `timeline get` answers
#   `nr.model.found`, and the inspector's row goes red — the same way the font
#   field says fontconfig has never heard of a family.
#
#   674 assertions, clean under ASan+UBSan+LSan.
#
# 38 THE CURVE, OVER TIME.
#
#   The darkroom has had a curve widget over TONE since the beginning. This is
#   the same idea over TIME, and it is the last thing keyframes were missing:
#   a key could be dropped and nudged, but a move over four seconds was a list
#   of numbers rather than a shape, and the ease on it was a word.
#
#   The ∿ opens on any animatable row with more than one key: x is time inside
#   the clip, y is the property's own range, the line is the curve and the
#   squares are the keys. Drag one, click the empty space to put one there,
#   double-click to take it away, and the five eases below set how the picked
#   key LEAVES.
#
#   ⛔ THE CURVE IS SAMPLED BY THE ENGINE — `timeline anim ... curve PROP`.
#   `ss_clip_prop_at` is the one place a keyed property becomes a number, and
#   it exists because the monitor and the export have to agree about one frame
#   by frame. Five eases re-implemented in QML would be a picture of something
#   nothing renders, right up until the day one of the two changed. The test
#   asserts a sample IS what `anim at` answers at the same instant.
#
#   ⚠ A DRAG IS ONE EDIT. `anim move PROP N [--at] [--value] [--ease]` moves a
#   key in place; remove-then-add from the window would be two processes, two
#   writes and two steps of undo for one gesture — and two chances for the
#   second half to be dropped, which reads as the editor eating the key.
#   Fields not named KEEP what they were, so a drag along the time axis does
#   not reset an ease somebody chose, and a failed move puts the key back.
#
#   ⚠ It prints the index it LANDED at, which is not the one it left: keys are
#   held in time order, so a drag past a neighbour renumbers both.
#
#   ⚠ `ss_clip_prop_key` returns 1 on SUCCESS, unlike almost everything else
#   in the header — `!= 0` read every key that was there as one that was not,
#   and every move failed with "has no key N".
#
#   ⚠ The Canvas repaints from a SERIAL, not from the array: assigning a new
#   array of the same length changes nothing QML can see, and the line would
#   stay on the old shape until something else happened to repaint it.
#
#   689 assertions, clean under ASan+UBSan+LSan.
#
# 39 THE SMOOTH CUT — THE LAST NAMED GAP.
#
#   A morph across a jump cut: the outgoing picture at the START of the
#   overlap, the incoming picture at its END, and every frame between them
#   INVENTED by motion estimation. It is what makes a cut inside one shot look
#   like the shot never stopped, and like every editor that offers one it
#   wants a SHORT duration — a morph over a large movement is mush.
#
#   ⚠ minterpolate CANNOT BE HANDED TWO FRAMES. Fed a two-frame stream it
#   emits NOTHING AT ALL — measured on a real file, not guessed — because its
#   pipeline needs a frame either side of the pair it is inventing between.
#   Each side is doubled with `loop`, and the pads sit ONE FRAME from their
#   own side rather than a whole duration away: spacing them by `dur` made it
#   invent a second identical span that is thrown away, at twice the cost.
#
#   ⚠ AND MINTERPOLATE HAS NO ALPHA. Its formats are YUV, so the picture comes
#   back opaque and a transition on an upper track would black out everything
#   under it for its whole length. The matte is carried around the morph —
#   both sides' alpha, dissolved, merged back on.
#
#   ⚠ extractplanes, NOT alphaextract. The two do the same thing and
#   `alphaextract` cannot negotiate a format in this graph at all: "the
#   following filters could not choose their formats", and the whole render
#   dies. Nothing in the documentation says so.
#
#   ⛔ THE MONITOR BUILDS THE SAME FOUR FRAMES AND SELECTS ONE. It is not a
#   second way of working out what a morph looks like — it is the same graph
#   asked for one frame, which is the only way the two can agree. That needed
#   the SEEK to move too: both sides are seeked to the fixed instants the
#   export morphs between, not to the moment on screen, and the transform is
#   evaluated at those same instants because the export bakes it into the two
#   frames. Verified frame-exact against the exported file.
#
#   ⚠ THE MORPH RUNS AT 960 WIDE, in BOTH builders. Motion estimation is the
#   most expensive thing this program asks ffmpeg for: at 1920 it costs about
#   half a second per invented frame, so a one-second morph was NINETEEN
#   SECONDS for one monitor frame — on every scrub step. Bounded, it is a
#   couple of seconds, and the softness that costs is invisible against the
#   softness a morph already has. A bound both builders share is not a
#   disagreement; a faster monitor would have been.
#
#   697 assertions, clean under ASan+UBSan+LSan.
#
# 40 THE PHOTO DRAG, FOR REAL THIS TIME.
#
#   pkgrel 37 fixed two of the three things wrong with dragging a photograph
#   onto the Video tab. The one it missed is the one that mattered: the drag
#   started and never arrived.
#
#   ⛔ `drag.target` MOVES AN ITEM BY THE MOUSE DELTA FROM A START POSITION
#   MOUSEAREA CAPTURED AT PRESS — before the `pressed` handler runs. So
#   putting the proxy under the pointer on press, which is what 37 did, is
#   OVERWRITTEN by the first move: Qt writes start + delta, where start is
#   whatever the last drag left on the item (0,0 the first time). The
#   hit-tested point therefore tracked the pointer's MOVEMENT measured from
#   the picture pane's corner, not the pointer — so it left the window on the
#   way to a tab near the top left, and the drag went nowhere.
#
#   The position is written from the MOVE handler now, after Qt's own update,
#   which is the write that lands.
#
#   ⚠ AND A DROP FOUR PIXELS SHORT OF A 56-PIXEL TAB DID NOTHING. That is the
#   common case, not the edge case. The window-wide drop area catches the
#   photograph anywhere the hand actually went — the toolbar, the panels, the
#   timeline — and the tab stays lit for the whole gesture rather than only
#   once the pointer is already on it. ⛔ Except back onto the PICTURE, which
#   is a mis-click: adding a clip and swapping the page under the hand is
#   worse than doing nothing.
#
#   ⚠ The test drives the drag THE WAY QT DOES — stale start position, Qt's
#   write, the window's write, then the drop — and asserts all three answers.
#   The old one set the position itself, which is exactly why it passed while
#   the gesture was broken.
#
#   700 assertions, clean under ASan+UBSan+LSan.
#
# 41 THE PHOTOGRAPH IS CARRIED BY THE POINTER NOW, NOT BY Qt's DRAG.
#
#   Three releases fixed three real faults in this one gesture — a
#   window-wide DropArea that ate the event, a drag item positioned from a
#   start captured at press, a tab too small to hit — and it still did not
#   work. At that point the machinery is the problem, not the settings.
#
#   There is no `Drag` attached property and no DropArea in it any more. A
#   press, a twelve-pixel threshold, a pointer position on every move, and a
#   release: all in the picture pane's own coordinates, all in this file. The
#   hit test is one rectangle check and the drop is a direct call to the same
#   handler a file dropped from a file manager goes through.
#
#   Nothing is lost by leaving Qt's drag behind — this gesture never left the
#   window. Files dragged IN from elsewhere are a different path and are still
#   a DropArea.
#
#   ⚠ AND THE HARNESS NO LONGER SETS THE THING UNDER TEST. Every previous
#   version of this test placed the drag item itself, which is precisely why
#   it passed while the gesture was broken twice over. What is driven now is
#   what the mouse drives: `photoOverTab` and `photoDropAt`, with the tab's
#   position asked of the window rather than assumed.
#
#   ⚠ A drop anywhere OFF the picture is the gesture, because a 56-pixel tab
#   is not a fair target; back onto the picture is a mis-click and says so
#   rather than adding a clip and swapping the page under the hand. And the
#   thing being carried is VISIBLE at last — a label follows the pointer with
#   the file's name on it, which is why "is it even dragging?" was a question.
#
#   702 assertions, clean under ASan+UBSan+LSan.
#
# 42 THE THUMBNAIL MAKER.
#
#   A thumbnail is a SECOND picture made from a photograph, and it is about
#   the thumbnail rather than about the photograph: a fixed canvas somebody
#   else's page will show whatever happens, the developed frame framed into
#   it, and a few words big enough to read at the size one is actually seen.
#
#   Canvases are listed by DESTINATION and not by aspect ratio — nobody has
#   ever wanted "16:9", they have wanted the one YouTube takes. Fill crops to
#   the canvas, fit pads it with a colour. Three captions, each with the nine
#   anchors a title has, plus a nudge, an outline, a shadow and a plate.
#
#   ⚠ IT RIDES IN THE SIDECAR, beside the develop stack, because it is a
#   decision about the same file — so it comes back when the photograph is
#   reopened rather than belonging to whoever last exported one. Written only
#   when it is not the default, so every sidecar made before this reads back
#   byte for byte.
#
#   ⚠ THE WORDS ARE LETTERED BY FFMPEG, the same `drawtext` a timeline's
#   titles use. Nothing here is linked to a font rasteriser and nothing is
#   going to be — so this program develops the picture, writes it once, and
#   one ffmpeg pass frames it and letters it. Colour never leaves colour.c.
#
#   ⚠ AND THE CAPTIONS GO THROUGH A FILE. drawtext's argument is parsed twice
#   — once by the filtergraph splitter and once by drawtext itself — so a
#   caption with a colon, a comma or a quote in it fails the whole render at
#   the moment somebody exports, long after it was typed. `expansion=none`
#   for the same reason with a percent sign.
#
#   ⚠ The temporaries live BESIDE THE OUTPUT, not in /tmp: the output is
#   somewhere writable by definition, /tmp may be another filesystem, and a
#   rename across one is a copy. They are removed even when the render fails.
#
#   One table in src/thumb.c drives the CLI, the sidecar and the panel, so a
#   control the renderer has not got cannot appear in the window. The panel's
#   preview is the SAME command the export runs, at a smaller size.
#
#   728 assertions, clean under ASan+UBSan+LSan.
# 43: THE DARKROOM IGNORED THE DESKTOP FONT. Not one Text named a family and
#   all hundred-and-seven pixel sizes were literals, so it kept whatever face
#   and size Qt resolved at startup while every other window in the suite
#   followed ~/.config/synui/font.state.
#   ⚠ BOTH HALVES HAVE TO BE BINDINGS. Qt resolves an application's default
#   font ONCE at startup and QML cannot change it afterwards, so the family has
#   to be named on every Text and the size has to go through ui(). Doing one
#   and not the other gives a window that follows the desktop until somebody
#   changes it. Ten families are NOT the desktop's and stay: the literal
#   "monospace" ones are values to read and type, and the font PICKER draws
#   each row in the face it names, which is the whole point of the list.
#   ⚠ AND THE START SCREEN'S BUTTONS HAD TO GROW WITH THEIR TEXT. `Door` was a
#   fixed 340x56 holding two lines; at 150% the title and the subtitle
#   overlapped inside it and the subtitle ran out past the right edge — the
#   only control on that screen, unusable. The suite's rule is that ui() scales
#   pixelSize and nothing else, and a height that exists ONLY to hold N lines
#   of text is the documented exception: it is not really a size, it is a line
#   count. The width follows the widest label rather than a second guess at it,
#   so no scale can clip it.
#   Verified in a nested headless synui against a font.state carrying a face
#   and a scale nothing resolves to by accident (DejaVu Serif at 150%), so
#   "it followed the file" and "it kept its defaults" cannot be confused.
# 47: the window speaks thirteen languages.
#   191 msgids through the same JSON bridge the bar and the other five
#   quickshell apps carry — one byte-identical data/qml/I18n.qml with its
#   catalogs generated beside it and read by a blocking FileView.
#   ⛔ THE PANEL HEADINGS ARRIVE FROM THE ENGINE AND ARE MATCHED ON. `groups`,
#   `clipGroups` and `thumbGroups` are field 5 of the keys records, spelled by
#   C tables in develop.c, timeline.c and thumb.c — and this window compares
#   them (`modelData === "Basic"`, `=== "Title"`, `rowsIn(group)`) as well as
#   drawing them. groupLabel() is the one place a group becomes a word; the
#   value compared stays the engine's. tests/i18n_test.sh reads the three C
#   tables and fails when the mapper's set and theirs disagree EITHER WAY —
#   which immediately found a `case "Speed"` that no group ever matches
#   (it is a row LABEL inside Levels), a msgid no translator would ever see used.
#   ⚠ AND THE ROW LABELS UNDER THOSE HEADINGS STAY ENGLISH. "Temperature",
#   "Opacity" and the other ~70 come from the same C tables over the record
#   protocol, so they need a catalog on the C side; the .po header says so
#   rather than leaving the gap to be discovered.
#   ⛔ AN UNRESOLVABLE QML IMPORT IS A **WARNING**, AND THAT COST THREE
#   ASSERTIONS. tests/run.sh copies synstudio.qml to a scratch directory to
#   drive the photo-drag gesture, and did not copy the new qml/ module beside
#   it. quickshell logs "Ignoring unresolvable import", brings the window up
#   anyway, and every I18n.tr() then throws "ReferenceError: I18n is not
#   defined" AT THE POINT OF USE — aborting whatever function it was in. Here
#   that was photoDropAt(), so the drop silently stopped adding a clip on a
#   gesture that works perfectly. The harness copies the module now, and greps
#   ReferenceError|TypeError — the throw is logged at WARN, so an ERROR grep
#   walks straight past it.
#   ⚠ The export filename keeps Qt.formatDateTime(…, "yyyyMMdd-hhmmss") on the
#   C locale ON PURPOSE, and the suite asserts it in both directions: that call
#   builds a PATH, and under ar_EG a locale-formatted one carries Arabic-Indic
#   digits into it.
# 48: the panels stop being English under translated headings.
#   47 translated the WINDOW; the develop, clip and thumbnail panels are built
#   from TABLES IN C, so "Temperature", "Opacity" and about 150 others still
#   drew in English underneath. They are marked N_() now — src/develop.c,
#   src/timeline.c, src/thumb.c — and po/pot.sh runs REAL xgettext over them and
#   msgcat's the result into the same template the QML extractor writes. One
#   .po per language, one JSON, two source languages. 326 msgids, 325/325 in
#   all thirteen.
#   ⛔ AND THE RECORD STAYS ENGLISH, which is the whole point. The group and the
#   label are KEYS as well as words — the window matches on the group, the CLI
#   and every test parse the same records, and a translated record makes output
#   depend on the locale, which is the bug `pacman -Qi` taught this project
#   twice. src/i18n.h therefore has N_() and NO _(): the C marks, the window
#   looks up. Verified: `synstudio keys` is byte-identical under de_DE.
#   ⛔ WHICH MAKES THE WINDOW'S LOOKUP DYNAMIC — `I18n.tr(row.label)` — and
#   tools/qml-xgettext.py refuses a non-literal argument for good reason. The
#   refusal stands; the exemption is per call site and has to be written down as
#   `// i18n-dynamic: <where the msgids come from>`. There are exactly four, the
#   suite counts them, and it also asserts every N_() label in the three C
#   tables is a msgid — so the dynamic lookup can never be handed a string the
#   catalog has not got.
#   ⚠ THIS DELETED THE groupLabel() SWITCH and its set-equality check. The
#   mapper was a hand-written list that could drift from the C; the group names
#   are now extracted from the C by construction, so groupLabel() is one line.
#   ⚠ tools/qml-xgettext.py in THIS component therefore differs from the copies
#   in synui/, synfiles/ and synpkg/ — which already differ from each other. It
#   is a build-time tool and ships to nobody; I18n.qml is the file kept
#   byte-identical, and still is.
pkgrel=48

pkgdesc="SynapseOS darkroom and edit suite: RAW develop, masks, and a graded video timeline with a cutting room"
arch=('x86_64')
url="https://github.com/velle999/SYNAPSE"
license=('GPL-2.0-or-later')

# libc and libm. Everything else is a subprocess, and the ones that are not
# strictly required are optdepends so that an install without them degrades
# by feature rather than refusing to start.
depends=('glibc')

# ffmpeg is NOT optional, unlike in synfiles where it only supplies a
# resolution row. It is how this program reads and writes every file: without
# it synstudio can compute colour and can open nothing, which is not a
# degraded editor, it is a calculator.
depends+=('ffmpeg')

makedepends=('meson' 'ninja' 'gcc')

# qt6-multimedia is optional and MEANS it: playback lives in its own QML file
# behind a Loader, so without the module the play button says why and
# everything else — the darkroom, the timeline, the monitor, the export —
# still works. That indirection is the whole reason it can be an optdepend.
# An `import QtMultimedia` in the main file would fail the WHOLE file, turning
# a missing optional module into a window that does not open at all, and
# quickshell does not pull qt6-multimedia in.
optdepends=('quickshell: the window — darkroom and cutting room (synstudio gui)'
            'qt6-multimedia: playing the timeline back (the ▶ button)'
            # dcraw_emu, which is libraw's own tool. ffmpeg reads DNG and a
            # few others, but for most camera raw it fails by producing a
            # THUMBNAIL-sized preview rather than an error — far worse than
            # refusing — so those extensions are routed to libraw instead and
            # simply do not open without it.
            'libraw: Canon, Nikon, Sony, Fuji and Olympus camera raw'
            # For `synstudio timeline export`. Same binary as depends, named
            # again here only in the comment: the timeline builds one filter
            # graph and hands it over whole.
            )

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
    cd "$srcdir/synstudio-0.1.0"
    meson setup build --prefix=/usr --buildtype=release
    meson compile -C build
}

check() {
    cd "$srcdir/synstudio-0.1.0"
    # The whole suite is headless: no display, no compositor, no GPU. That is
    # possible because the window is a renderer over this same command line,
    # so a passing suite is evidence about the application and not merely
    # about a library underneath it.
    #
    # Every path is inside a mktemp -d that the EXIT trap removes, and the
    # sidecar tests md5sum the original photograph before and after to prove
    # this program never writes to it. An editor whose tests could modify a
    # real photograph would be the most dangerous file in this repository.
    meson test -C build --print-errorlogs
}

package() {
    cd "$srcdir/synstudio-0.1.0"
    meson install -C build --destdir="$pkgdir"
}
