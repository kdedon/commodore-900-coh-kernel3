#!/bin/sh
# check-shared-headers.sh -- the headers this repository shares with the
# toolchain are one text, byte for byte.
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
# WHICH headers is not a list here.  It is every path that EXISTS IN BOTH
# trees -- so a header that becomes shared is compared from the moment it
# lands, and one that stops being shared drops out, with nothing to update by
# hand.  It also means an accidental copy is caught on the same terms as an
# intended one, which is the case a hand-written list would miss.
#
# When they disagree, neither copy is automatically right: decide which text
# is the contract, then make both that.  The header is the interface, and two
# spellings of an interface is one of them being wrong.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/.." && pwd)

# $TCINC.  Sourcing this is also the refusal when the toolchain is not
# resolved: an unresolved dependency must not read as agreement, and a gate
# that passes because it could not find the other side is worse than no gate --
# it reports the property it did not check.
. "$OS/hostbuild/toolchain.sh"

[ -d "$OS/include" ] || { echo "check-shared-headers: no $OS/include" >&2; exit 2; }

shared=0; bad=0
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

if [ "$bad" -ne 0 ]; then
	echo "*** $bad of $shared shared headers differ." >&2
	echo "*** kernel:    $OS/include" >&2
	echo "*** toolchain: $TCINC" >&2
	exit 1
fi
[ "$shared" -gt 0 ] ||
	{ echo "check-shared-headers: no header is in both trees -- nothing was compared, which is not a pass" >&2; exit 2; }
echo "check-shared-headers: $shared shared with the toolchain, all identical"
