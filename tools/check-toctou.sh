#!/usr/bin/env bash
#
# check-toctou.sh — refuse a check-then-use on the same path NAME.
#
# The bug this exists to stop, in its general shape:
#
#     if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode))
#             fd = open(path, O_RDONLY);        /* a different file, maybe */
#
# Two resolutions of one name, with a window in between. Anyone who can write
# the containing directory can swap what `path` means inside that window, and
# the second call — the one that actually DOES something — answers about the
# replacement. It is worse than it looks in this repo specifically, because the
# check and the use disagree about symlinks by default: lstat() does not follow
# one, open() does. So the check says "an ordinary directory I own" and the
# open lands wherever the attacker's symlink points. That is CVE-shaped in
# synfiles, syn-disks and synguard, all of which walk trees the user does not
# necessarily control, and two of which run privileged.
#
# The fix is never a smaller window. It is to resolve the name ONCE and then
# talk to the descriptor:
#
#     fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
#     if (fd >= 0 && fstat(fd, &st) == 0)  ...
#
# O_DIRECTORY is the is-it-a-directory test, O_NOFOLLOW is the refuse-a-symlink
# test, and fstat() on the fd cannot be pointed at anything else. Below a
# directory, the same shape is fstatat()/openat() relative to the parent's fd —
# which is why every *at() call is invisible to this script: they take a
# descriptor, not a name, and are the answer rather than the problem.
#
# Written after CodeQL cpp/toctou-race-condition #13 landed on
# synfiles/src/listing.c — a real one, already fixed by the commit the alert
# fired on, and worth catching here rather than on push.
#
# Usage:
#   tools/check-toctou.sh            # every tracked C source
#   tools/check-toctou.sh a.c b.c    # just these
#
# Exit 0 clean, 1 with findings.
#
# Suppressing a finding: put `toctou-ok: <why>` in a comment on the checking
# line. A reason is required — a bare marker is itself a finding. Legitimate
# uses are narrow, and they all look like "this stat is printed and then
# dropped; nothing opens the name afterwards". If you are writing "the window
# is tiny" or "only root can do that", the suppression is wrong: root is
# exactly who this hurts, and the window is as long as the attacker wants —
# they can stall it with a slow filesystem.
#
# SynapseOS Project — GPL-2.0-or-later
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

cd "$(dirname "$0")/.."

# Vendored and generated trees. llama-staging*/ and scenefx*/ are upstream
# code, and */src/<name>-0.1.0/ is makepkg's extraction of our own tarball —
# fixing a finding there edits a copy that the next build overwrites.
skip_re='^(llama-staging|scenefx|archiso/build/)|/src/[A-Za-z0-9_-]+-[0-9]+\.[0-9]+\.[0-9]+/|(^|/)pkg/'

selftest=0
[ "${1:-}" = "--self-test" ] && { selftest=1; shift; }

if [ "$#" -gt 0 ]; then
	files=("$@")
elif [ "$selftest" -eq 1 ]; then
	files=()
else
	mapfile -t files < <(git ls-files '*.c' '*.h' | grep -Ev "$skip_re" || true)
fi

[ "$selftest" -eq 1 ] || [ "${#files[@]}" -gt 0 ] || { echo "ok: no C sources to scan"; exit 0; }

# GitHub Actions renders ::error file=…,line=… as an annotation on the diff;
# outside CI that prefix is just noise, so it is switched off.
fmt=plain
[ -n "${GITHUB_ACTIONS:-}" ] && fmt=gha

scan() { awk -v fmt="$fmt" "$AWK" "$@"; }

AWK='
# ── what counts as a CHECK: resolves a NAME, answers a question, opens nothing
function is_check(fn) {
	return fn ~ /^(stat|lstat|statx|access|euidaccess|eaccess|faccess)$/
}
# ── what counts as a USE: resolves a NAME again and acts on the result
function is_use(fn) {
	return fn ~ /^(open|open64|fopen|fopen64|freopen|creat|opendir|scandir|truncate|truncate64|unlink|rmdir|remove|rename|link|symlink|chmod|chown|lchown|utimes|utime|mkfifo|mknod|mkdir|execl|execle|execlp|execv|execve|execvp|chdir|chroot|mount|umount)$/
}
# first argument of a call, normalised: `foo(bar, ...)` -> `bar`
function first_arg(rest,   a) {
	sub(/^[ \t]*/, "", rest)
	a = rest
	sub(/[,)].*$/, "", a)
	gsub(/[ \t]/, "", a)
	return a
}
# Mask string literals, keeping them DISTINCT from one another.
#
# Masking at all is needed so a "//" or "/*" inside a literal is not read as the
# start of a comment. But collapsing every literal to the SAME token makes two
# calls that name two different files look like one name checked and then
# opened:
#
#     access("data/syn-arcade.qml", R_OK)      ->  access("")
#     execlp("quickshell", "quickshell", ...)  ->  execlp("")   /* "same" name */
#
# That is not a near miss — nothing opens the checked path and the two literals
# have no characters in common. It reported syn-arcade/src/main.c on the commit
# that introduced the component and every commit after it, which is a scanner
# that cries wolf on a shape this repo uses in five binaries.
#
# So the literal keeps its own characters, with everything but alphanumerics
# folded to "_": distinct bodies stay distinct, and nothing left inside the
# token can open a comment, a string or a block.
function maskstrings(s,   out, i, j, body) {
	out = ""
	while ((i = index(s, "\"")) > 0) {
		out = out substr(s, 1, i - 1)
		s = substr(s, i + 1)
		j = index(s, "\"")
		# No closing quote on this line. Leave the remainder exactly as
		# the old regex did (it simply failed to match) rather than
		# swallowing calls that may follow it.
		if (j == 0)
			return out s
		body = substr(s, 1, j - 1)
		s = substr(s, j + 1)
		gsub(/[^A-Za-z0-9]/, "_", body)
		out = out "\"STR_" body "\""
	}
	return out s
}
# Drop comments, carrying /* */ across lines. Not fussiness: this file is a
# repo where the fix for a finding is usually a comment EXPLAINING the finding,
# so a scanner that reads comments reports every explanation of itself as a new
# bug — and braces in prose would wreck the scope tracking too.
function decomment(s,   out, i, j) {
	out = ""
	while (length(s)) {
		if (incomment) {
			i = index(s, "*/")
			if (i == 0) return out
			s = substr(s, i + 2)
			incomment = 0
			continue
		}
		i = index(s, "/*")
		j = index(s, "//")
		if (j > 0 && (i == 0 || j < i))
			return out substr(s, 1, j - 1)
		if (i == 0)
			return out s
		out = out substr(s, 1, i - 1)
		s = substr(s, i + 2)
		incomment = 1
	}
	return out
}

FNR == 1 { delete checked; delete checkline; delete checkfn; delete checkdepth; depth = 0; incomment = 0 }

# A closing brace in column 0 ends a function. Checks do not survive it: two
# functions touching the same-named variable are not the same window.
/^}/ { delete checked; delete checkline; delete checkfn; delete checkdepth; depth = 0; next }

{
	# The marker is read from the raw line, because it lives in a comment.
	ok = ($0 ~ /toctou-ok:[ \t]*[^ \t]/)
	bare = ($0 ~ /toctou-ok/) && !ok

	line = $0
	# strings first, so a "//" inside one is not read as a comment
	line = maskstrings(line)
	line = decomment(line)
	if (bare) {
		emit(FILENAME, FNR, "toctou-ok with no reason given")
		next
	}

	rest = line
	while (match(rest, /(^|[^A-Za-z0-9_])[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/)) {
		call = substr(rest, RSTART, RLENGTH)
		rest = substr(rest, RSTART + RLENGTH)
		fn = call
		sub(/^[^A-Za-z_]*/, "", fn)
		sub(/[ \t]*\($/, "", fn)
		arg = first_arg(rest)
		if (arg == "")
			continue

		if (is_check(fn)) {
			if (!ok && !(arg in checked)) {
				checked[arg]    = 1
				checkline[arg]  = FNR
				checkfn[arg]    = fn
				checkdepth[arg] = depth
			}
		} else if (is_use(fn) && (arg in checked)) {
			emit(FILENAME, FNR, \
			     fn "(" arg ") re-resolves a name already checked by " \
			     checkfn[arg] "() at line " checkline[arg] \
			     " — open once, then use the descriptor")
			delete checked[arg]   # one finding per pair, not per use
		}
	}

	# ── block scoping. A check inside a branch that returns does not reach the
	# code after that branch — `if (dry_run) { access(p); print; return; } ...
	# open(p);` is two different runs of the program, not a window. So a check
	# is forgotten once the block holding it closes.
	#
	# Only a NET drop counts, never a raw `}`. A line that opens and closes in
	# equal measure has not left any block, and the commonest such line is a
	# struct initializer:
	#
	#     struct du_acc a = { 0, 0, 0, 0, seen, time(NULL), false };
	#
	# Counting its `}` as a block exit forgot the check sitting above it — and
	# that one line is exactly what sat between the lstat() and the open() in
	# the finding this script was written for, so the first version of it
	# scanned the very bug it exists to catch and reported the file clean.
	# The self-test below plants that shape deliberately.
	closers = gsub(/}/, "}", line)
	openers = gsub(/{/, "{", line)
	net = openers - closers
	if (net < 0) {
		limit = depth + net
		for (a in checked)
			if (checkdepth[a] > limit)
				delete checked[a]
	}
	depth += net
	if (depth < 0) depth = 0
}

function emit(f, l, msg) {
	bad++
	if (fmt == "gha")
		printf "::error file=%s,line=%d::%s\n", f, l, msg
	else
		printf "%s:%d: %s\n", f, l, msg
}

END {
	if (bad) {
		printf "\n%d check-then-use finding(s). Resolve the path once:\n", bad
		print  "  fd = open(p, O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);"
		print  "  fstat(fd, &st);            /* not stat(p, &st) */"
		print  "  ...at()/openat() below it, relative to fd."
		print  "If the check really is printed and then dropped, say so:"
		print  "  toctou-ok: <why nothing opens this name afterwards>"
		exit 1
	}
	print "ok: no check-then-use on a path name"
}
'

# ── Does the scanner still SEE anything? ─────────────────────
# A pattern-matcher that has stopped matching passes every file in the repo and
# reports success, which is indistinguishable from the repo being clean. So it
# is made to find a planted bug, and to leave the fixed, scoped, descriptor-
# relative and suppressed forms of the same code alone, before its opinion on
# real sources is worth anything.
if [ "$selftest" -eq 1 ]; then
	# Plain output, even under Actions. The assertions below read the "file:line:"
	# form, and an ::error annotation hung on a temp fixture would land on a file
	# no one can open anyway — it would just decorate the log with two failures
	# that are the test WORKING.
	fmt=plain

	fixture=$(mktemp -t toctou-fixture.XXXXXX.c)
	trap 'rm -f "$fixture"' EXIT
	cat >"$fixture" <<-'EOF'
	void bad(const char *p) {                       /* 1 */
	    struct stat st;
	    if (lstat(p, &st) != 0) return;
	    int fd = open(p, O_RDONLY);                 /* 4: FINDING */
	}
	void initializer_between(const char *p) {
	    struct stat st;
	    if (lstat(p, &st) != 0) return;
	    struct acc a = { 0, 0, 0 };                 /* not a block exit */
	    int fd = open(p, O_RDONLY);                 /* 10: FINDING */
	}
	void fixed(const char *p) {
	    int fd = open(p, O_RDONLY | O_NOFOLLOW);
	    struct stat st;
	    fstat(fd, &st);
	}
	void scoped(const char *p) {
	    if (dry) {
	        access(p, F_OK);
	        return;
	    }
	    int fd = open(p, O_RDONLY);
	}
	void relative(int dirfd, const char *name) {
	    struct stat st;
	    fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW);
	    int fd = openat(dirfd, name, O_RDONLY);
	}
	void allowed(const char *p) {
	    struct stat st;
	    lstat(p, &st);          /* toctou-ok: printed, nothing opens it */
	    printf("%lld\n", (long long)st.st_size);
	    unlink(p);
	}
	void in_prose(const char *p) {
	    /* Do not write lstat(p) and then open(p) here. */
	    int fd = open(p, O_RDONLY | O_NOFOLLOW);
	}
	void two_literals(void) {
	    if (access("/etc/one", R_OK) != 0) return;
	    execlp("some-program", "some-program", (char *)NULL);
	}
	void same_literal(void) {
	    if (access("/etc/one", R_OK) != 0) return;
	    int fd = open("/etc/one", O_RDONLY);        /* 45: FINDING */
	}
	EOF

	# The output goes to a FILE and the assertions grep the file. Not style:
	# `printf ... | grep -q` under `set -o pipefail` reports 141 the moment
	# grep exits on its first match and printf takes the SIGPIPE, so a
	# MATCHING assertion fails — and only once the output is long enough not
	# to fit the pipe buffer, which is how it hid here.
	out=$(mktemp -t toctou-out.XXXXXX)
	trap 'rm -f "$fixture" "$out"' EXIT
	scan "$fixture" >"$out" 2>&1 || true

	fails=0
	got=$(grep -c ':[0-9]*: ' "$out" || true)
	[ "$got" -eq 3 ] || { echo "SELF-TEST: expected 3 findings, got $got"; cat "$out"; fails=1; }

	# Two planted bugs on a VARIABLE, and one on a literal path checked and
	# then opened by the same name.
	for want in '4:open(p)' '10:open(p)' '45:open("STR__etc_one")'; do
		grep -q ":${want%%:*}: ${want#*:}" "$out" \
			|| { echo "SELF-TEST: the planted bug on line ${want%%:*} was not found"; fails=1; }
	done

	# And the false positive this scanner shipped with: two DIFFERENT string
	# literals are two different names. Without this the fix is one gsub away
	# from being tidied back out, and the failure is a red build on every
	# component that checks for a data file and then execs a program.
	if grep -q 'execlp("STR_some_program")' "$out"; then
		echo "SELF-TEST: two different literals were read as one name"
		fails=1
	fi

	[ "$fails" -eq 0 ] || exit 1
	echo "ok: self-test — finds all three planted bugs, and nothing else"
	exit 0
fi

scan "${files[@]}"
