#!/bin/sh
# tests/lcprobe/run.sh -- host check of the kernel's ID-byte video card probe.
#
# lcprobe_check.c links the kernel's sys/z8001/src/vcard.c against fake I/O
# ports and the 8568 model from ../lcmm, with plain cc: an empty or echoing
# bus, a wrong ID and an ID with no chip must all read as no card, and only
# the real card may be found.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
CC=${CC:-cc}

WORK=${WORK:-$HERE/work}
rm -rf "$WORK"
mkdir -p "$WORK"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15

if ! "$CC" -std=gnu89 -w -Dprintf=tprintf -Dputchar=tputchar \
    -I "$OS/sys/z8001/h" -c -o "$WORK/vcard.o" "$OS/sys/z8001/src/vcard.c" \
    > "$WORK/cc.log" 2>&1 ||
   ! "$CC" -I "$HERE/../lcmm" -o "$WORK/check" "$HERE/lcprobe_check.c" \
    "$HERE/../lcmm/vdc.c" "$WORK/vcard.o" >> "$WORK/cc.log" 2>&1; then
	echo "  FAIL lcprobe_check did not compile:"
	cat "$WORK/cc.log"
	exit 1
fi
if "$WORK/check"; then
	echo "lcprobe: only the LC card is found"
	exit 0
fi
echo "lcprobe: FAIL"
exit 1
