"""
secrets.py — where a provider's API key lives.

Three places, in this order, and the first one that answers wins:

  the environment   ANTHROPIC_API_KEY / OPENAI_API_KEY — a shell session
                    handing one in for this run only.
  the keyring       libsecret, through secret-tool(1). Encrypted at rest and
                    unlocked with the login, which is the best a desktop can do.
  a file            ~/.config/synui/ai/<provider>.key, mode 0600.

⛔ SECRET-TOOL EXITS 0 WHEN THERE IS NO KEYRING. It prints "The name is not
activatable" to stderr and returns SUCCESS — so a store that went nowhere looks
exactly like one that worked. Trusting that status would mean telling somebody
their key was saved and then having no key at all, which for a credential store
is the worst failure available. **Every write is verified by reading it back**,
and only a value that comes back byte for byte counts as stored.

## Why the file is still here, and why that is not the alert it looks like

CodeQL flags the file as clear-text storage of sensitive information, and it is
right that the bytes are not encrypted. It is worth being precise about what
that buys an attacker: the file is mode 0600 under the user's own home, so
reading it requires already being that user or root — and anybody who is can
also read the process's memory, ptrace it, or simply run `vibe` and let it use
the key. Encrypting it under a second key stored beside it moves the problem
without shrinking it.

That is why every comparable tool does exactly this: ~/.aws/credentials,
~/.docker/config.json, ~/.netrc, ~/.config/gh/hosts.yml and an unencrypted
~/.ssh/id_ed25519 are all plaintext at 0600.

The keyring IS a real improvement, because its key is not on disk beside the
secret — so it is preferred whenever a desktop has one. The file is what is left
for a machine with no keyring running, which includes every headless one, and
the alternative there is not a safer file, it is no assistant.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""

import os
import subprocess

import vibe.config as cfg

_ENV = {"anthropic": "ANTHROPIC_API_KEY", "openai": "OPENAI_API_KEY"}
_LABEL = "SynapseOS assistant"
_TIMEOUT = 10


def _attrs(provider: str) -> list:
    return ["service", "synapse-assistant", "provider", provider]


def _have_secret_tool() -> bool:
    from shutil import which
    return which("secret-tool") is not None


def keyring_get(provider: str) -> str:
    """The key the keyring holds, or "". Never raises."""
    if not _have_secret_tool():
        return ""
    try:
        r = subprocess.run(["secret-tool", "lookup"] + _attrs(provider),
                           capture_output=True, text=True, timeout=_TIMEOUT)
    except Exception:
        return ""
    # ⚠ NOT `r.returncode == 0`. See the module docstring: with no keyring
    # running, secret-tool prints its complaint to stderr and exits 0 with
    # nothing on stdout. The OUTPUT is the answer; the status is not.
    return (r.stdout or "").strip()


def keyring_put(provider: str, key: str) -> bool:
    """Store it, and prove it. True only if it reads back byte for byte."""
    if not _have_secret_tool():
        return False
    try:
        subprocess.run(
            ["secret-tool", "store", "--label", f"{_LABEL} ({provider})"]
            + _attrs(provider),
            # ⚠ THROUGH STDIN. `secret-tool store` takes the secret on stdin
            # precisely so it never appears in the process table; passing it as
            # an argument would put the key in `ps` for every user on the box.
            input=key + "\n", capture_output=True, text=True, timeout=_TIMEOUT)
    except Exception:
        return False
    return keyring_get(provider) == key


def keyring_clear(provider: str) -> None:
    if not _have_secret_tool():
        return
    try:
        subprocess.run(["secret-tool", "clear"] + _attrs(provider),
                       capture_output=True, text=True, timeout=_TIMEOUT)
    except Exception:
        pass


def file_get(provider: str) -> str:
    try:
        return cfg.key_path(provider).read_text(encoding="utf-8").strip()
    except OSError:
        return ""


def file_put(provider: str, key: str) -> None:
    """Write it 0600, atomically, never through a wider-mode file.

    ⛔ NOT `write_text()` ONTO THE PATH. Writing to a file that already exists
    does not change its mode — so a key.state left 0644 by an older version, a
    restored backup or a `cp` would take the new key in the clear and only be
    chmod'd a moment later. Small window, real, and free to close: the bytes go
    into a fresh file that is 0600 from the instant it exists, and a rename puts
    it in place.

    ⚠ AND NO umask() CALL. The previous version set the process umask to 0o077
    to get the mode right and never put it back — harmless in a command that
    exits immediately, and a landmine the moment this is called from anywhere
    that keeps running. O_CREAT|O_EXCL with an explicit mode needs no such
    global.
    """
    path = cfg.key_path(provider)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + f".tmp.{os.getpid()}")
    fd = os.open(str(tmp), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(key + "\n")
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def file_clear(provider: str) -> None:
    try:
        cfg.key_path(provider).unlink()
    except OSError:
        pass


# ── what the rest of the program uses ───────────────────────────────────────

def get(provider: str) -> str:
    """The key for `provider`, or "". Environment, then keyring, then file."""
    v = os.environ.get(_ENV.get(provider, ""), "").strip()
    if v:
        return v
    v = keyring_get(provider)
    if v:
        return v
    return file_get(provider)


def put(provider: str, key: str) -> str:
    """Store it in the best place this machine has. Returns where it went."""
    if keyring_put(provider, key):
        # ⚠ AND THE FILE COPY GOES. Leaving one behind would mean a key changed
        # in the keyring and a stale one still readable on disk — two answers to
        # one question, and the plaintext one is the one that outlives it.
        file_clear(provider)
        return "keyring"
    file_put(provider, key)
    return "file"


def clear(provider: str) -> None:
    keyring_clear(provider)
    file_clear(provider)


def where(provider: str) -> str:
    """Which of the three is answering, in a word."""
    if os.environ.get(_ENV.get(provider, ""), "").strip():
        return "environment"
    if keyring_get(provider):
        return "keyring"
    if file_get(provider):
        return "file"
    return "not set"
