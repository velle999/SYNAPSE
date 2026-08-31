# synfiles

A file browser with tabs, pinned places, recent files and a devices sidebar.
It draws its own file icons rather than hunting an icon theme for them, so a
folder looks the same on a machine with no theme installed.

A window, a terminal browser, and a command line that answers in records.

## Browsing

```bash
synfiles gui                 # the window
synfiles tui ~/Documents     # browse in this terminal
synfiles list -a --sort=size # entries in a directory
synfiles info FILE           # everything a properties pane shows
synfiles du ~/Videos         # recursive size, counting as it walks
```

## Finding things

```bash
synfiles find ~ --name='*.pdf' --limit=50
synfiles find ~/src --content='TODO' --max-depth=4
```

Searches never follow symlinks, so a loop in a tree cannot turn a search into
a hang.

## Opening things

```bash
synfiles actions FILE                    # what can open it, and service menus
synfiles action org.example.App -- FILE  # run one of them
```

Service menus are read from the desktop, so "Run with Wine", "Open in Disks"
and the rest appear when the thing that provides them is installed.

## The sidebar and volumes

```bash
synfiles places pin ~/Projects "Projects"
synfiles places list
```

Removable media mount through udisks2 without root. Network places — SMB,
SFTP and MTP — come from gvfs when it is installed, and shares announced over
mDNS or NetBIOS are found with avahi and smbclient.

`ffmpeg` gives video thumbnails and the resolution of Matroska, WebM and AVI
files; without it those files simply show their icon.
