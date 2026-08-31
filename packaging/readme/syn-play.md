# syn-play

A front end for mpv: playlists you can see, a queue you can rearrange, quick
open, and a history of what you have watched and where you stopped.

mpv already decodes, keeps a playlist, shuffles it, resumes where it stopped
and reads m3u. What it has no place for is showing you any of that. So this
adds exactly those and delegates the rest — shuffle is mpv's `playlist-shuffle`,
resume is mpv's own watch-later files, and a playlist is an m3u8 that opens in
mpv, in VLC and on a phone.

## Playing

```bash
syn-play ~/Videos/film.mkv       # play it now
syn-play ~/Music/Album           # a folder becomes its files
syn-play add ~/Music/Other       # add to the end of the queue
syn-play open blade runner       # find the best match and play it
syn-play resume                  # the last thing played, where it stopped
```

Quick open searches the history first and never walks `$HOME`, so it answers
immediately for anything played before.

## Driving it

```bash
syn-play status
syn-play queue
syn-play next | prev | jump 4
syn-play pause | toggle
syn-play seek +30 | syn-play seek 12:05
syn-play shuffle | unshuffle
syn-play loop playlist
syn-play volume +10
```

`unshuffle` restores the order files were added in, because that is mpv's own
undo rather than a copy of the list kept here.

## Playlists and history

```bash
syn-play playlist save "Sunday"
syn-play playlist load "Sunday"
syn-play history 20
```

## The other two faces

```bash
syn-play tui                 # the queue and history, in this terminal
syn-play gui                 # the window
```

The window and the terminal UI are control surfaces beside mpv, not around
it: mpv keeps its own video window, and `syn-play next` from a keybind, a
script, the terminal UI and the window are the same line to the same socket.

## Requires

`mpv`, which is an optional dependency only because everything except playback
works without it. `quickshell` for the window, `yt-dlp` for playing a URL.
