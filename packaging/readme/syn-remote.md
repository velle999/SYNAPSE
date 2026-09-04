# syn-remote

The desktop, from somewhere else. A thin wrapper over **wayvnc**, plus the
parts a wrapper has to add.

```bash
syn-remote on                # serve this desktop, now and at every login
syn-remote status            # what it is doing
syn-remote address           # how to reach it
syn-remote listen lan        # the network, rather than this machine only
syn-remote wakeable on       # let a magic packet wake this machine while it sleeps

syn-remote add desk 192.168.1.20 velle   # a machine to reach
syn-remote trust desk        # check its certificate, once, before the first connection
syn-remote connect desk      # open it — waking it first if it is asleep
syn-remote gui | tui         # the same list, in a window or in the terminal
```

## "Wayland cannot do remote desktop" does not apply here

All three reasons that is said are about other stacks. GNOME and KDE gate
capture behind a portal that asks a human per session. `xdg-desktop-portal-wlr`
implements ScreenCast but **not** RemoteDesktop, so portal-based tools can watch
a wlroots desktop and cannot touch it. And nothing exists to connect to before
somebody logs in — which is true here too, and `syn-remote status` says so
rather than failing obscurely.

wayvnc captures through `zwlr_screencopy_manager_v1` and drives the seat through
`zwp_virtual_pointer_manager_v1` and `zwp_virtual_keyboard_manager_v1`. synui
implements all three for any native client, so no portal is involved and nothing
prompts.

## What the wrapper adds

**A blanked output cannot be captured at all.** Once the idle blank stage has
fired, screencopy answers *failed to copy output* — a viewer gets nothing, and
there is no frame to click on to get out of it. A connection turns the outputs
back on and holds a real idle inhibitor until the last viewer leaves.

**It binds to loopback.** A port on `0.0.0.0` is reachable by every device on
the network; `syn-remote listen lan` is the deliberate way to change that, and
says what it means at the time. Either way the connection carries TLS and a
password — wayvnc's `enable_auth` needs a certificate, a key and a password
together, so there is no password without encryption.

**The certificate is checked in front of a person.** wayvnc's certificate is
self-signed, so the first connection is trust-on-first-use: `syn-remote trust`
prints the fingerprint and waits for a yes. Until then the TLS session comes up
and is dropped with no error on the machine you are sitting at — the only trace
is *Client handshake timed out* in the server's journal, which reads like a
firewall and is not one.

**And a machine that is asleep answers nothing at all.** `syn-remote wakeable`
arms the wired card for a magic packet and reports what is armed **now** as well
as what will still be armed after a reboot — two different questions, and a
machine that is armed today and forgotten at the next boot reads as working
right up until the reboot nobody connects to it after. `syn-remote wake <name>`
sends the packet; `connect` sends one by itself when the machine it is opening
is not answering. ⚠ A magic packet is a broadcast, so the machine sending it has
to be on the same network as the machine being woken.

## Requires

wayvnc, wlopm, gtk-vnc, openssl, python. NetworkManager is optional and
load-bearing for one thing: without it the wake flag is set until the next
reboot only, and `wakeable` says so.
