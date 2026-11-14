#!/bin/sh
# tests/hrc/run.sh -- host check of the colour console's back end.
#
# hrc_check.c links the driver's rec/hrc.c and rec/hrcfont.c, with plain cc,
# against C versions of its two assembler loops, and compares the bitmap after
# each batch of the terminal's calls with a screen drawn from scratch.  h/
# stands in for the kernel headers.
#
#	sh run.sh		check
#	UPDATE=1 sh run.sh	rewrite golden/screen.crc from this build
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
CC=${CC:-cc}

WORK=${WORK:-$HERE/work}
rm -rf "$WORK"
mkdir -p "$WORK"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15

if ! "$CC" -std=gnu89 -w -I "$HERE/h" -c -o "$WORK/hrc.o" \
    "$OS/sys/z8001/rec/hrc.c" > "$WORK/cc.log" 2>&1 ||
   ! "$CC" -std=gnu89 -w -c -o "$WORK/hrcfont.o" \
    "$OS/sys/z8001/rec/hrcfont.c" >> "$WORK/cc.log" 2>&1 ||
   ! "$CC" -o "$WORK/check" "$HERE/hrc_check.c" "$WORK/hrc.o" \
    "$WORK/hrcfont.o" >> "$WORK/cc.log" 2>&1; then
	echo "  FAIL hrc_check did not compile:"
	cat "$WORK/cc.log"
	exit 1
fi
mkdir -p "$HERE/golden"
if "$WORK/check" "$HERE/golden" "$WORK" ${UPDATE:+update}; then
	echo "hrc: the colour bitmap matches the cells"
	exit 0
fi
echo "hrc: FAIL"
exit 1
