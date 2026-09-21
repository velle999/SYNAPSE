#!/usr/bin/env python3
"""gate_test.py — the confirmation gate in front of every tool that writes.

    python3 tests/gate_test.py      (from the vibe directory)

A write — bash, a file edit, a desktop action — runs only when the confirm
callback says yes. The case pinned here is the callback FAILING: it used to
count as a yes, so a confirmation dialog that broke ran what nobody agreed to.
docs/THREAT-MODEL.md.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""
import os
import sys
from types import SimpleNamespace

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from vibe import llm  # noqa: E402

npass = nfail = 0


def check(name, cond):
    global npass, nfail
    print(f"  {'ok  ' if cond else 'FAIL'}  {name}")
    if cond:
        npass += 1
    else:
        nfail += 1


def gate(callback, tool="bash"):
    fake = SimpleNamespace(_CONFIRM_TOOLS=llm.VibeModel._CONFIRM_TOOLS,
                           confirm_tool=callback)
    return llm.VibeModel._gate(fake, tool, {"command": "echo hi"})


def broken(name, args):
    raise RuntimeError("the dialog did not open")


check("a yes runs the tool", gate(lambda n, a: True) is True)
check("a no does not", gate(lambda n, a: False) is False)
check("a callback that RAISES does not run the tool", gate(broken) is False)
check("…for every writing tool",
      all(gate(broken, t) is False for t in llm.VibeModel._CONFIRM_TOOLS))
check("a read-only tool never asks", gate(broken, "read_file") is True)
check("no callback at all (non-interactive) proceeds", gate(None) is True)

print()
print(f"  {npass} passed, {nfail} failed")
sys.exit(1 if nfail else 0)
