# syn-settings

System settings: displays and resolution, keyboard and language, date and
time, network addresses, Bluetooth, power and sleep, kernels, the AI backend,
the screen reader, default applications, and where configuration actually
lives.

A window with panes, and a command line that answers each pane as records.

## The window

```bash
syn-settings gui             # open it
syn-settings gui network     # open it on one pane
```

Panes: `display`, `region`, `time`, `network`, `bluetooth`, `power`, `apps`,
`kernel`, `ai`, `assistant`, `fprint`, `speech`, `remote`, `system`.

## Reading the same answers from a script

```bash
syn-settings --rec display   # connectors: the kernel's state beside what
                             # the compositor is actually driving
syn-settings --rec time      # the clock, and how the desktop writes it
syn-settings --rec apps      # the default app per role, and WHICH file
                             # decided it
syn-settings --rec kernel    # every kernel on offer, installed, running
syn-settings --rec fprint    # the reader, and which fingers are enrolled
syn-settings --rec network   # interfaces, the MAC and addresses of each,
                             # the gateway, nameservers, and the firewall
```

The `network` pane reports a hardware address per interface, which is what
another machine needs in order to wake this one: a magic packet is addressed to
a MAC, not an IP. Only a wired card can be woken that way, and the Wi-Fi row
says so rather than leaving you to arm something that cannot work.

The `apps` pane reports which file made each decision, because a deliberate
choice and a fallback look identical everywhere else — that is the difference
between "nothing is set" and "something set it and you did not".

## What it drives

`systemd` for `localectl`, `timedatectl` and `systemctl`; `networkmanager`
for the network pane; `bluez-utils` for Bluetooth; `wlr-randr` and `synctl`
for displays; `synpkg` and `pacman` for kernels; `fprintd` for fingerprints.
Each pane says what is missing rather than showing an empty box, and changes
that need authorisation go through polkit instead of asking for a root shell.
