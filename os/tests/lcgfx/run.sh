#!/bin/sh
# tests/lcgfx/run.sh -- host check of the LC graphics minor.
#
# lcgfx_check.c links the driver's rec/lcgfx.c and rec/lc.c and the kernel's
# font against the 8563 model of tests/lcmm, with plain cc; h/ stands in for
# the kernel headers.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
CC=${CC:-cc}

WORK=${WORK:-$HERE/work}
rm -rf "$WORK"
mkdir -p "$WORK"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15

if ! "$CC" -std=gnu89 -w -I "$HERE/h" -I "$OS/sys/z8001/h" -I "$OS/include" \
    -DSHADOW=shadowbuf -DGDISP=gdtest -c -o "$WORK/lcgfx.o" \
    "$OS/sys/z8001/rec/lcgfx.c" > "$WORK/cc.log" 2>&1 ||
   ! "$CC" -std=gnu89 -w -I "$OS/sys/z8001/h" -c -o "$WORK/lc.o" \
    "$OS/sys/z8001/rec/lc.c" >> "$WORK/cc.log" 2>&1 ||
   ! "$CC" -std=gnu89 -w -c -o "$WORK/lcfont.o" \
    "$OS/sys/z8001/src/lcfont.c" >> "$WORK/cc.log" 2>&1 ||
   ! "$CC" -std=gnu89 -w -I "$HERE/h" -o "$WORK/check" "$HERE/lcgfx_check.c" \
    "$OS/tests/lcmm/vdc.c" "$WORK/lcgfx.o" "$WORK/lc.o" "$WORK/lcfont.o" \
    >> "$WORK/cc.log" 2>&1; then
	echo "  FAIL lcgfx_check did not compile:"
	cat "$WORK/cc.log"
	exit 1
fi
"$WORK/check" && exit 0
echo "lcgfx: FAIL"
exit 1
