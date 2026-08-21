#!/bin/sh
# check-shared-headers.sh -- the headers this repository shares with the
# toolchain are one text, byte for byte -- and every place that sharing
# happens gets walked, not just the convenient one.
#
#	sh hostbuild/check-shared-headers.sh
#
# A few headers are the CONTRACT between the kernel and the C library: the
# syscall interface (errno, signal, stat, times, types), the terminal
# interface both sides program (sgtty, termio, canon), the on-disk shapes the
# kernel writes and the file-system tools read (dir, ino, filsys, fblk), and
# the object format the kernel's exec loader reads out of what ld wrote
# (l.out, n.out, mtype).  Each is compiled by BOTH sides, so each is kept in
# BOTH trees, deliberately.
#
# Deliberate duplication is safe only while something compares it.  Two copies
# with nothing checking them is how <ctype.h> came to assign _N, _S, _P and _C
# different bits from the table in the C library that every program indexes
# with them: isdigit('7') was false and isspace(' ') was false, in every
# binary, for as long as nobody looked.  That is the failure this refuses.
#
# TWO CHECKS, not one:
#
#  1. os/include vs the toolchain.  WHICH headers is not a list here.  It is
#     every path that EXISTS IN BOTH trees -- so a header that becomes shared
#     is compared from the moment it lands, and one that stops being shared
#     drops out, with nothing to update by hand.  Every path here is one this
#     kernel actually ships and the toolchain actually publishes, so identity
#     is not negotiable: a difference is always a bug.
#
#  2. os/sys/h and os/sys/z8001/h vs the toolchain, resolved.  These two
#     directories are 0.7.3-donor snapshots that predate the 3.2 forward-port
#     (see kheaders.py's header comment): most of what is in them the
#     kernel's own build never reads -- link-kernel.sh's IMI (what
#     kheaders.py's IPATH mirrors) puts os/include and the toolchain ahead of
#     os/sys/z8001/h and omits os/sys/h outright, so the toolchain's or
#     os/include's copy wins for any name both trees carry.  IMD (used only
#     for sys/z8001/src/ddt.c, the KDDT=1 debugger) puts os/sys/z8001/h
#     first, so a name ddt.c includes IS read from there.  Divergence here is
#     therefore not automatically a bug the way case 1's is -- some of it is
#     donor rot nothing reads, some of it (proc.h, seg.h) is the Z8001
#     layer's own authoritative shape.  Nothing distinguished the two before
#     this script existed, which is the hole: a header that both trees carry
#     but disagree on read as UNCOMPARED, not as agreeing, and the walk that
#     found "N shared, all identical" never went looking here at all.
#
#     shared-header-exceptions.tsv carries the judgment: a (dir, basename)
#     pair with a toolchain counterpart that differs must be listed there
#     WITH A REASON, or the gate fails naming the file.  A listed pair that
#     stops differing is reported too -- the list is not permitted to
#     accumulate entries nobody re-checks.
#
# When they disagree, neither copy is automatically right: decide which text
# is the contract, then make both that (case 1), or write down why not
# (case 2).  The header is the interface, and two spellings of an interface
# is one of them being wrong -- unless something on record says otherwise.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/.." && pwd)
EXC="$HERE/shared-header-exceptions.tsv"

# $TCINC.  Sourcing this is also the refusal when the toolchain is not
# resolved: an unresolved dependency must not read as agreement, and a gate
# that passes because it could not find the other side is worse than no gate --
# it reports the property it did not check.
. "$OS/hostbuild/toolchain.sh"

[ -d "$OS/include" ] || { echo "check-shared-headers: no $OS/include" >&2; exit 2; }
[ -r "$EXC" ] || { echo "check-shared-headers: no $EXC" >&2; exit 2; }

bad=0

# --- check 1: os/include vs $TCINC, by identical relative path -----------
shared=0
for f in $(cd "$OS/include" && find . -name '*.h' | sed 's|^\./||' | sort); do
	[ -f "$TCINC/$f" ] || continue
	shared=$((shared + 1))
	cmp -s "$OS/include/$f" "$TCINC/$f" && continue
	if [ "$bad" -eq 0 ]; then
		echo "*** check-shared-headers: these are kept in both trees and no" >&2
		echo "*** longer agree.  Both sides compile them, so the two copies" >&2
		echo "*** are two different interfaces:" >&2
	fi
	bad=$((bad + 1))
	echo "***   $f" >&2
	diff -u "$OS/include/$f" "$TCINC/$f" | sed 's/^/***     /' >&2 || true
done

# --- check 2: os/sys/h and os/sys/z8001/h vs the toolchain, judged --------
# tc_find NAME -> path, searching $TCINC then $TCINC/sys: the same two
# entries IMI adds to the kernel's own -I path (in that order) beyond
# os/include and os/include/sys.
tc_find() {
	if [ -f "$TCINC/$1" ]; then
		printf '%s\n' "$TCINC/$1"
	elif [ -f "$TCINC/sys/$1" ]; then
		printf '%s\n' "$TCINC/sys/$1"
	fi
}

# exc_reason DIR BASENAME -> the reason column, or empty if unlisted.
exc_reason() {
	awk -F '\t' -v d="$1" -v b="$2" \
		'$0 !~ /^#/ && NF >= 3 && $1 == d && $2 == b { $1=""; $2=""; sub(/^\t\t/,""); print; exit }' \
		"$EXC"
}

judged=0; unjudged=0; stale=0; resolved=0
for dir in sys/h sys/z8001/h; do
	[ -d "$OS/$dir" ] || continue
	for f in $(cd "$OS/$dir" && find . -maxdepth 1 -name '*.h' | sed 's|^\./||' | sort); do
		tc=$(tc_find "$f")
		[ -n "$tc" ] || continue
		resolved=$((resolved + 1))
		reason=$(exc_reason "$dir" "$f")
		if cmp -s "$OS/$dir/$f" "$tc"; then
			if [ -n "$reason" ]; then
				echo "*** check-shared-headers: stale exception -- $dir/$f no longer" >&2
				echo "*** differs from $tc but is still listed in" >&2
				echo "*** $(basename "$EXC"); remove the row or the list stops meaning" >&2
				echo "*** anything." >&2
				bad=$((bad + 1))
				stale=$((stale + 1))
			fi
			continue
		fi
		if [ -z "$reason" ]; then
			if [ "$unjudged" -eq 0 ]; then
				echo "*** check-shared-headers: these exist in both a donor header" >&2
				echo "*** directory and the toolchain, disagree, and are on no" >&2
				echo "*** allowed-to-differ list -- an unjudged divergence:" >&2
			fi
			bad=$((bad + 1))
			unjudged=$((unjudged + 1))
			echo "***   $dir/$f" >&2
			echo "***     vs $tc" >&2
			diff -u "$OS/$dir/$f" "$tc" | sed 's/^/***     /' >&2 || true
		else
			judged=$((judged + 1))
		fi
	done
done

if [ "$bad" -ne 0 ]; then
	echo "***" >&2
	[ "$unjudged" -gt 0 ] && echo "*** $unjudged divergence(s) have no entry in $(basename "$EXC")." >&2
	[ "$stale" -gt 0 ] && echo "*** $stale exception row(s) no longer diverge from the toolchain." >&2
	echo "*** kernel:    $OS/include, $OS/sys/h, $OS/sys/z8001/h" >&2
	echo "*** toolchain: $TCINC" >&2
	exit 1
fi
[ "$shared" -gt 0 ] || [ "$resolved" -gt 0 ] ||
	{ echo "check-shared-headers: no header is in both trees -- nothing was compared, which is not a pass" >&2; exit 2; }
echo "check-shared-headers: $shared shared with the toolchain, all identical; $resolved resolved from os/sys/h+os/sys/z8001/h ($judged judged, rest identical)"
