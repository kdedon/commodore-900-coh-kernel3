#!/bin/sh
# tests/memmap/run.sh -- static gate on the memory map: which 64 KB segments
# hold memory a system may allocate, and what a loader's map is allowed to say.
#
# memmap_check.c links against the kernel's own sys/z8001/src/memmap.c and runs
# on the HOST (plain cc, no Z8001 cross toolchain): the map is segment
# arithmetic over a table, with no target dependency, so this is the check that
# catches a framebuffer handed out as memory, or a boot block trusted when it
# should not be, without a boot or a screen.
#
# The loader's headers are the ones the kernel compiles against, resolved the
# same way the kernel build resolves them.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
ROOT=$(cd "$OS/.." && pwd)
CC=${CC:-cc}

KB=$(C900_KBOOT="${C900_KBOOT:-}" sh "$ROOT/mk/deps.sh" kboot)
[ -n "$KB" ] || {
	C900_KBOOT="${C900_KBOOT:-}" sh "$ROOT/mk/deps.sh" -n kboot "${C900_KBOOT:-}"
	exit 2
}

WORK=${WORK:-$HERE/work}
rm -rf "$WORK"
mkdir -p "$WORK/sys"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15
cp "$KB/include/bootinfo.h" "$WORK/sys/bootinfo.h"
cp "$KB/include/memmap.h" "$WORK/sys/memmap.h"

if ! "$CC" -I "$WORK" -o "$WORK/check" "$HERE/memmap_check.c" \
    "$OS/sys/z8001/src/memmap.c" > "$WORK/cc.log" 2>&1; then
	echo "  FAIL memmap_check did not compile:"
	cat "$WORK/cc.log"
	exit 1
fi
"$WORK/check" || exit 1
