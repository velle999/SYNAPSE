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
synfiles chmod 755 FILE      # set permissions; never through a symlink
```

`info` also names the disk a file is on — where it is mounted, the
filesystem, the device, and its capacity, used and free space, counted the same
way as the sidebar's meters. The Properties window shows it on its General tab,
beside Permissions (a read/write/execute grid that changes the file), Checksums
(MD5, SHA-1, SHA-256 and SHA-512, calculated when asked; paste one from a
download page and it says whether the file matches) and Details (every record
`info` prints).

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

Removable media mount through udisks2 without root. A disc image mounted from
the right-click menu appears under Removable Devices, and Eject unmounts it and
releases the loop device behind it. Network places — SMB,
SFTP and MTP — come from gvfs when it is installed, and shares announced over
mDNS or NetBIOS are found with avahi and smbclient.

The devices list keeps itself current. Plug a stick in, put a disc in a drive,
or mount something from another program or a terminal, and the sidebar changes
without a refresh. `synfiles volumes --watch` is the same thing on the command
line: it blocks, and prints a line whenever the list would change.

`ffmpeg` gives video thumbnails and the resolution of Matroska, WebM and AVI
files; without it those files simply show their icon.
