# syn-confine

Run a command inside a kernel-enforced allowlist. Everything not granted is
denied, and the restriction is Landlock — so it is applied by the kernel,
inherited across `execve`, and cannot be dropped by the program being confined.

```bash
syn-confine --ro /usr --rw ./build -- make
syn-confine --rx /usr/bin --ro /etc --tcp 443 -- ./fetch-things
syn-confine --isolate-net -- ./untrusted-script
syn-confine --ro /home/me/project --print -- true   # the policy, without running
```

## Paths

`--rw`, `--ro` and `--rx` are repeatable and each covers everything beneath the
path it names — write, read-only, and read-and-execute respectively. A base
profile for `/usr`, `/etc`, `/proc` and friends is applied unless `--no-base`
says otherwise.

## Network

Outbound TCP is refused by default. `--tcp PORT` opens one port, `--net` opens
all of them, and `--isolate-net` puts the command in an empty network
namespace — no TCP, no UDP, no DNS, no loopback.

**`--isolate-net` is the only one that stops DNS exfiltration.** A policy that
blocks TCP but leaves the resolver reachable still lets a program spell data
out in hostnames.

## Requires

A kernel with Landlock (5.13 and later; the ruleset ABI in use decides which
of the path rights are enforceable). `--print` says what the policy resolves
to on the machine it runs on, which is the way to find out rather than
guessing from a version number.
