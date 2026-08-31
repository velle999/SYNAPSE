# syn-gfn

GeForce NOW as a dedicated web app, in its own browser profile.

```bash
syn-gfn                      # launch it
syn-gfn --list-browsers      # which browsers are installed, and which can stream
syn-gfn --browser=chromium
syn-gfn --url=https://example.com -- --some-browser-flag
```

## Why a launcher and not just a bookmark

Cloud gaming needs two things an ordinary browser window does not give you:
full-screen without the browser's own chrome, and the **Keyboard Lock API**,
so that Escape reaches the game instead of leaving full screen.

⚠ **Firefox cannot do it on Linux.** GeForce NOW has officially supported
Firefox since 2026-08-19 — for Windows builds only. On Linux, Firefox loads
the site, lists your library, and will not start a game; it also has no
Keyboard Lock API at all, so even if a stream did start, Escape would drop
out of full screen rather than open the in-game menu. `--browser=firefox`
opens one anyway, for browsing the catalogue.

So the launcher picks a Chromium-family browser, and `--list-browsers` says
which of the ones you have can actually stream.

## Its own profile, never your browsing one

The default profile is `~/.local/share/syn-gfn`. A stream is a full-screen
session with a pointer lock and a keyboard lock — mixing that into the profile
you browse with means your extensions, your cookies and your open tabs are all
in the session too.
