#!/bin/sh
# tests/lcmm/run.sh -- host check of the 8563 console back end.
#
# lcmm_check.c links the driver's sys/z8001/rec/lc.c and the kernel's font
# against vdc.c, a C model of the 8563, with plain cc.  It replays the
# terminal's scroll, insert and delete calls and compares the chip's planes
# with what the memory-mapped card would hold and with golden/*.txt (text,
# then attributes in hex, one line per row).
#
#	sh run.sh		check
#	UPDATE=1 sh run.sh	rewrite the golden files from this build
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
CC=${CC:-cc}

WORK=${WORK:-$HERE/work}
rm -rf "$WORK"
mkdir -p "$WORK"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15

if ! "$CC" -std=gnu89 -w -I "$OS/sys/z8001/h" -c -o "$WORK/lc.o" \
    "$OS/sys/z8001/rec/lc.c" > "$WORK/cc.log" 2>&1 ||
   ! "$CC" -std=gnu89 -w -c -o "$WORK/lcfont.o" \
    "$OS/sys/z8001/src/lcfont.c" >> "$WORK/cc.log" 2>&1 ||
   ! "$CC" -o "$WORK/check" "$HERE/lcmm_check.c" "$HERE/vdc.c" \
    "$WORK/lc.o" "$WORK/lcfont.o" >> "$WORK/cc.log" 2>&1; then
	echo "  FAIL lcmm_check did not compile:"
	cat "$WORK/cc.log"
	exit 1
fi
mkdir -p "$HERE/golden"
if "$WORK/check" "$HERE/golden" ${UPDATE:+update}; then
	echo "lcmm: the 8563 screen matches the lo-res card's"
	exit 0
fi
echo "lcmm: FAIL"
exit 1
