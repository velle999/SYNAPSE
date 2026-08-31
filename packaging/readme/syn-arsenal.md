# syn-arsenal

Browse BlackArch's security tooling by category and install what you want,
instead of reading a package list of several thousand names.

```bash
syn-arsenal                 # the graphical browser
syn-arsenal --tui           # the same, in this terminal
syn-arsenal --enable-repo   # add the BlackArch repository (needs root)
```

The repository is not added for you at install time. `--enable-repo` is a
separate, deliberate step, because adding a repository changes what every
future `pacman -Syu` on the machine will consider.

## Notes

⚠ **One dead BlackArch mirror blocks every upgrade on the machine**, not just
BlackArch packages — pacman treats a database it cannot refresh as a failure
for the whole transaction. If upgrades start failing after enabling the
repository, that is where to look first.

Installation goes through pacman. The graphical browser needs `quickshell`;
the TUI needs nothing but the terminal.
