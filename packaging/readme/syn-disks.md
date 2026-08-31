# syn-disks

A disk utility: what drives are in the machine, what is on them, how healthy
they are, and the partitioning and formatting to change that.

## Looking

```bash
syn-disks list               # every drive in this machine
syn-disks parts /dev/sda     # partitions, and anything unlocked on top
syn-disks info /dev/sda2     # everything known about one
syn-disks smart /dev/sda     # drive health, from smartmontools
syn-disks table /dev/sda     # the table, and the free space between
syn-disks gui                # the window
```

`table` shows the gaps as well as the partitions, which is what you need
before making one, and says what is protecting each partition.

## Changing things

```bash
syn-disks mount /dev/sdb1    # through udisks2, no root needed
syn-disks unmount /dev/sdb1
syn-disks eject /dev/sdb     # flush, unmount everything, power it down
```

## Partitioning and formatting

Every destructive command takes `--yes` and `-n`. `-n` prints the exact
command it would run instead of running it, so you can read it first.

```bash
syn-disks mkpart /dev/sdb --size=32G --fs=ext4 --label=data -n
syn-disks format /dev/sdb1 --fs=btrfs --label=backup --yes
```

Formatting the disk the running system is on is refused rather than
confirmed — there is no flag for it.

Supported filesystems are ext4, btrfs, xfs, vfat, exfat and ntfs; each needs
its own userspace tools, which are pulled in as dependencies.

## Notes

SMART needs root; `--elevate` asks for authorisation through polkit rather
than requiring the whole command to be run as root. A mountpoint reported by
`lsblk` is not proof a filesystem is mounted where you think, so syn-disks
asks udisks2 rather than parsing that column.
