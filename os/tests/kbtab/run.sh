#!/bin/sh
# tests/kbtab/run.sh -- static gate on the keyboard scancode-to-ASCII
# tables, no boot and no display required.
#
# kbtab_check.c is data-only: it #includes kbtab.h, links against exactly
# one of the four ktab[] table sources shipped in this tree, and asserts
# the ASCII byte every scan code in the shared alphanumeric/punctuation
# block must decode to. It runs on the HOST (plain cc, no Z8001 cross
# toolchain) because the tables are plain C data with no target
# dependency -- this is the check that would have caught a keymap byte
# swap (e.g. apostrophe's entry holding a backtick) without ever needing
# the simulator or a screen.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
CC=${CC:-cc}

BAD=0
fail() { echo "  FAIL $*"; BAD=$((BAD + 1)); }
ok()   { echo "  ok   $*"; }

WORK=${WORK:-$HERE/work}
rm -rf "$WORK"
mkdir -p "$WORK"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15

# name : directory holding kbtab.h/kbchar.h : table source file
check() {
	name=$1 incdir=$2 tabsrc=$3
	bin="$WORK/check_$name"
	log="$WORK/$name.log"
	if ! "$CC" -I "$incdir" -o "$bin" "$HERE/kbtab_check.c" "$tabsrc" \
	    > "$log" 2>&1; then
		fail "$name: did not compile ($(tail -1 "$log"))"
		return
	fi
	if out=$("$bin" 2>&1); then
		ok "$name: $out"
	else
		fail "$name:"
		echo "$out" | sed 's/^/       /'
	fi
}

echo "checking every ktab[] this tree ships (built and unused alike)"
check rec_kbtab     "$OS/sys/z8001/rec"  "$OS/sys/z8001/rec/kbtab.c"
check rec_kbibmtab  "$OS/sys/z8001/rec"  "$OS/sys/z8001/rec/kbibmtab.c"
check hrtty_kbtab    "$OS/hrtty/src"      "$OS/hrtty/src/kbtab.c"
check hrtty_kbibmtab "$OS/hrtty/src"      "$OS/hrtty/src/kbibmtab.c"

if [ "$BAD" -eq 0 ]; then
	echo "kbtab: all tables agree with the US ASCII layout"
	exit 0
fi
echo "kbtab: $BAD failure(s)"
exit 1
