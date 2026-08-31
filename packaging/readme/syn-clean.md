# syn-clean

Free disk space, and destroy files you mean to destroy. Caches, thumbnails,
package leftovers, trash — and a shredder for the files you want gone rather
than merely deleted.

## Freeing space

```bash
syn-clean list               # the category names
syn-clean scan               # what there is, and how big
syn-clean scan cache trash   # just those
syn-clean clean --all        # every category that does not need root
syn-clean clean --dry-run cache
syn-clean gui                # the window
```

`--dry-run` says what would go and removes nothing. `--yes` skips the
confirmation. Nothing is removed without one of the two.

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

`--rec` prints one record per line for a front end. `pacman` is used to count
orphaned packages when it is present; with `synfiles` installed, "Destroy
Permanently" appears in the file manager's right-click menu.
