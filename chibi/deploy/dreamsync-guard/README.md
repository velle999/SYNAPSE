# chibi dream-sync guard — site-specific, opt-in, NOT installed by any package

Dream sync keeps the dream journal in step between two chibi instances (here:
the Pi and the PC). It is peer-to-peer union-merge — each side serves its own
entries over HTTP and pulls the other's on an interval — so **both machines
bind `0.0.0.0:8077`**.

These files fence that port to the paired peer. `chibi` itself does not install
them: opening a port to the LAN is an administrator's decision, and the peer
address is specific to this network.

| file | installed to |
|---|---|
| `chibi-dreamsync.nft` | `/etc/nftables.d/` |
| `chibi-dreamsync-guard.service` | `/etc/systemd/system/` |

```sh
sudo $EDITOR chibi-dreamsync.nft   # set YOUR peer address
./install.sh
```

## The token is the real control

`nft` narrows *who may ask*. What decides whether an asker is answered is
`dream_sync_token`, compared against an `X-Sync-Token` header:

```python
return not token or self.headers.get("X-Sync-Token") == token
```

`not token` short-circuits — **an empty token authenticates everyone.** The
committed default is the placeholder `"change-this-shared-secret"`, so a config
that never overrides it is using a secret that is in the public repository. Set
a real one on both machines. This guard exists so that a token mistake is not
also a LAN exposure.

## This guard is best-effort, and `synapd-bridge.socket` is not

The synapd bridge is fail-closed: its socket unit has `Requires=` on its guard,
so a guard that fails takes the port with it. **Nothing equivalent is possible
here.** chibi is a session application that binds 8077 itself; if this table
fails to load, chibi still listens and is simply unfenced, with no failed unit
to notice.

So do not read "the file is deployed" as "the port is fenced". Check the table:

```sh
sudo nft list table inet chibi_dreamsync_guard
```

That distinction is the same one written up in `synapd/bridge/README.md` under
the `Wants=` vs `Requires=` heading — a control that cannot gate the thing it
protects has to be verified rather than assumed.
