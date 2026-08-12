"""Does vibe's bash tool actually run confined?

Points tools._SYN_CONFINE at a build rather than the installed path — the
constant is deliberately NOT overridable at runtime (an env-settable sandbox
path is a bypass: set it to /bin/sh and the confinement is gone), so a test
patches the module instead.
"""
import os, sys, tempfile, pathlib
sys.path.insert(0, "vibe")
import tools

BUILD = os.path.abspath(sys.argv[1])
tools._SYN_CONFINE = BUILD

npass = nfail = 0
def check(name, cond):
    global npass, nfail
    print(("  ok    " if cond else "  FAIL  ") + name)
    if cond: npass += 1
    else: nfail += 1

work = tempfile.mkdtemp()
os.chdir(work)

out = tools.bash("echo hello")
check("an ordinary command still works", "hello" in out)

out = tools.bash("cat ~/.ssh/id_ed25519")
check("CANARY: the agent cannot read ~/.ssh", "denied" in out.lower() or "no such" in out.lower())

out = tools.bash("ls ~")
check("the agent cannot list $HOME", "denied" in out.lower())

out = tools.bash("echo written > f.txt && cat f.txt")
check("the workspace is writable", "written" in out)

# The bypasses the old regex denylist waved through, now stopped by the kernel.
out = tools.bash("python3 -c \"print(open('/etc/shadow').read())\"")
check("an interpreter cannot read around the sandbox", "denied" in out.lower())

out = tools.bash("sh -c \"sh -c 'cat ~/.ssh/id_ed25519'\"")
check("nesting shells does not escape it", "denied" in out.lower() or "no such" in out.lower())

out = tools.bash("curl -s -m 5 https://example.com")
check("the network is closed by default", "hello" not in out.lower() and "<html" not in out.lower())

# Fail closed.
tools._SYN_CONFINE = "/definitely/not/here"
out = tools.bash("echo hello")
check("a missing sandbox REFUSES rather than running unconfined",
      "BLOCKED" in out and "hello" not in out)
tools._SYN_CONFINE = BUILD

# The workspace guard.
os.chdir(str(pathlib.Path.home()))
out = tools.bash("echo hello")
check("running from $HOME is refused (it would re-expose everything)",
      "BLOCKED" in out and "hello" not in out)
os.chdir(work)

print()
print(f"  {npass} passed, {nfail} failed")
sys.exit(1 if nfail else 0)
