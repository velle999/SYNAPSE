# synnet

A network policy daemon: a base firewall it keeps applied, a blocklist it
maintains, and per-interface exceptions that survive a reboot.

```bash
synnet --status              # the firewall, the live ruleset and the blocklist
synnet --firewall on         # apply it now, and switch it on for good (root)
synnet --dry-run             # watch and decide, block nothing
synnet --allow 10.0.0.5
synnet --foreground --debug
```

## The firewall is remembered, not just applied

`--firewall off` is recorded, not merely executed. The daemon re-applies its
policy about once a minute, so a rule taken down by hand comes straight back —
switching it off has to be a decision the daemon knows about, or it looks like
the change did not work.

## Bridges you are the gateway for

```bash
synnet --trust-if virbr0
synnet --untrust-if virbr0
```

A container or VM bridge needs DHCP and DNS accepted or the guest's network
never comes up at all. **The guest's first DHCP packet comes from 0.0.0.0**,
which the base policy drops — so a guest that "has no network" on an otherwise
working bridge is nearly always this. Trusting the interface is applied
immediately and remembered.

## Notes

Rules are nftables. ⚠ The match on an incoming interface is `iifname`, not
`iif` — a rule written with the wrong one loads without complaint and matches
nothing, so `--status` and the journal are the way to tell whether a rule is
doing anything.

Requires `synapd` for the classification half; the firewall and blocklist work
without it.
