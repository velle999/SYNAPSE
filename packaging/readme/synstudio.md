# synstudio

A darkroom and an edit suite: RAW development with local adjustments, and a
graded video timeline with a cutting room.

**Edits live in a sidecar** next to the file — `<file>.synstudio` — and the
original is never written. Everything below reads and writes that sidecar;
`render` is what produces a new file.

## Developing a photograph

```bash
synstudio probe FILE                    # what it is: size, codec, duration
synstudio keys                          # every develop key, with its range
synstudio set FILE exposure=0.4 contrast=12
synstudio get FILE                      # read the sidecar back
synstudio undo FILE / redo FILE
synstudio reset FILE                    # back to the picture as it arrived
synstudio render FILE --out out.jpg
```

Local adjustments are masks:

```bash
synstudio mask FILE add linear
synstudio mask FILE 0 geom=0,0,1,0.4 feather=0.2 exposure=-0.6
synstudio mask FILE list
```

## Video

The timeline is graded with the same keys as a photograph. `source` pulls one
frame of any file through its sidecar, which is what a source monitor shows,
and `logcurve` says what a camera's own curve means — a code value in, scene
light out — for S-Log3 and V-Log.

```bash
synstudio source clip.mov --at 12.5 --out frame.png
synstudio match shot.jpg --ref reference.jpg   # make this look like that
synstudio histogram FILE                       # 256 bins per channel
```

## The window

```bash
synstudio gui                # the darkroom and the cutting room
```

Playback in the timeline needs `qt6-multimedia`. Camera raw — Canon, Nikon,
Sony, Fuji, Olympus — needs `libraw`; without it the developed formats are
the ones ffmpeg already reads.
