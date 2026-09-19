# syn-clean

Free disk space, and destroy files you mean to destroy. Caches, thumbnails,
package leftovers, trash — and a shredder for the files you want gone rather
than merely deleted.

## Freeing space

```bash
syn-clean list               # the category names
syn-clean scan               # what there is, and how big
syn-clean scan cache trash   # just those
syn-clean clean --all        # the caches, thumbnails, trash and /tmp
sudo syn-clean clean --all   # …and the package cache and the journal
syn-clean clean --dry-run cache
syn-clean gui                # the window
```

`--dry-run` says what would go and removes nothing. `--yes` skips the
confirmation. Nothing is removed without one of the two.

## The three that need root

`pkgcache`, `journal` and `orphans` live outside your home, and every row that
names them says so. Run the same command with `sudo` and it does them; run it
without and it tells you which command would. A scan's bottom line says how
much of the total that is, so the number you are looking at is one you can act
on.

`journal` is a trim, not a delete: `journalctl --vacuum-time` takes the
archived files older than a week and leaves the one the system is writing to.
The row measures exactly that, rather than the whole directory.

`orphans` uninstalls packages, so `--all` never includes it — name it and it
goes, the same rule cookies follow. `clean --dry-run orphans` says how many
would go first.

A clean counts what it actually removed. Files belonging to somebody else stay
where they are, and syn-clean says so rather than adding them to the total.

## Shredding

```bash
syn-clean shred ~/Documents/old.pdf
syn-clean shred --passes 7 ~/secret/
```

**Overwriting a file only destroys the old bytes if they are rewritten in
place**, and a copy-on-write filesystem does not do that — it writes
elsewhere and leaves the original blocks until something reuses them.
Snapshots keep whole copies, and an SSD controller remaps blocks out of any
program's reach.

syn-clean says which of these apply to the path you gave it *before* it
starts, rather than reporting success and letting the name imply something it
cannot deliver. Full-disk encryption is the thing that actually makes a
deleted file unreadable.

## Notes

`--rec` prints one record per line for a front end. `pacman` counts the
orphaned packages and, with root, removes them; `journalctl` does the journal
trim. With `synfiles` installed, "Destroy Permanently" appears in the file
manager's right-click menu.

The window cannot elevate, so it shows the three root rows greyed out with
"needs sudo" and leaves them to the command line.
