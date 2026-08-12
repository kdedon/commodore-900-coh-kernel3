#!/bin/sh
# build-hostfs.sh [check] -- build hostfs pass-through (dev dists):
# build/drv/hostfs (loadable driver, linked via ld -k against kernel.out)
# build/bin/hostfs (mount/sync/status tool)
# Prereq: link-kernel.sh (kobj/kernel.out), C900_TOOLCHAIN/host (compiler).
# Note: ld -k bakes kernel addresses; relink after kernel relink or remove stale driver.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/.." && pwd)
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CC0="$TCB/z8001/cc0-z8001"; CC1="$TCB/z8001/cc1-z8001"
CC2="$TCB/z8001/cc2-z8001"; LD="$TCB/ld-z8001"
CCZ="$TC/ccz"
VAR="${CCZ_VAR:-800000020800}"
KSYM="$HERE/kobj/kernel.out"
OBJ="$HERE/build/hostfsobj"; DRV="$HERE/build/drv"; BIN="$HERE/build/bin"
LOG="$HERE/logs/hostfs.log"
mkdir -p "$HERE/logs" "$OBJ" "$DRV" "$BIN"; : > "$LOG"

[ -f "$KSYM" ] || { echo "missing $KSYM (run link-kernel.sh first)" >&2; exit 1; }

# Check mode: gate on driver staleness (present and newer than kernel).
if [ "${1:-}" = "check" ]; then
	if [ ! -f "$DRV/hostfs" ]; then
		echo "hostfs driver NOT BUILT ($DRV/hostfs missing)" >&2; exit 1
	fi
	if [ "$KSYM" -nt "$DRV/hostfs" ]; then
		echo "hostfs driver STALE: kernel.out is newer than $DRV/hostfs" >&2
		echo "(ld -k bakes kernel addresses -- rerun build-hostfs.sh)" >&2
		exit 1
	fi
	echo "hostfs driver up to date with kernel.out"
	exit 0
fi

fail=0

# Driver: cc2 mode 0012 matches kernel frame convention.
IMI="-I$OS/include -I$OS/include/sys -I$OS/sys/z8001/h"
c900_buildlog "$OS/sys/drv/hostfs.c"
if "$CC0" $VAR "$OS/sys/drv/hostfs.c" "$OBJ/hostfs.z0" $IMI -DNOMONITOR=1 >>"$LOG" 2>&1 &&
   "$CC1" $VAR "$OBJ/hostfs.z0" "$OBJ/hostfs.z1" >>"$LOG" 2>&1 &&
   "$CC2" 0012 "$OBJ/hostfs.z1" "$OBJ/hostfs.o" "$OBJ/hostfs.scr" 0 >>"$LOG" 2>&1 &&
   "$LD" -X -o "$DRV/.hostfs.new" "$OBJ/hostfs.o" -k"$KSYM" >>"$LOG" 2>&1
then
	mv -f "$DRV/.hostfs.new" "$DRV/hostfs"
	chmod +x "$DRV/hostfs"
	echo "  drv/hostfs: OK ($(wc -c < "$DRV/hostfs") B)"
else
	fail=$((fail+1)); rm -f "$DRV/.hostfs.new"
	echo "  drv/hostfs: FAIL -- $(grep -iE 'error|undefined|no match|Internal' "$LOG" | grep -v 'Strict\|Warning' | tail -1)"
fi

# Tool: ordinary userland, separated-I/D large model like build-cmd.sh.
if CCZ_VAR="$VAR" "$CCZ" -s -i -L -I "$OS/include" -I "$OS/include/sys" \
	-o "$BIN/.hostfs.new" "$OS/cmd/hostfs/hostfs.c" >>"$LOG" 2>&1
then
	mv -f "$BIN/.hostfs.new" "$BIN/hostfs"
	echo "  bin/hostfs: OK ($(wc -c < "$BIN/hostfs") B)"
else
	fail=$((fail+1)); rm -f "$BIN/.hostfs.new"
	echo "  bin/hostfs: FAIL -- $(grep -iE 'error|undefined|no match|Internal' "$LOG" | grep -v 'Strict\|Warning' | tail -1)"
fi

# Host daemon (optional; needs Go + the simulator in c900oses/gotools).
if command -v go >/dev/null 2>&1; then
	if [ -z "${C900_GOTOOLS:-}" ]; then
		for _try in "$OS/../../../gotools" "$OS/../../c900oses/gotools" \
			    "$OS/../../../c900oses/gotools"; do
			[ -f "$_try/Makefile" ] || continue
			C900_GOTOOLS=$(cd "$_try" && pwd); break
		done
	fi
	if [ ! -f "${C900_GOTOOLS:-}/Makefile" ]; then
		fail=$((fail+1))
		echo "  hostfsd: FAIL -- no gotools tree at C900_GOTOOLS=${C900_GOTOOLS:-}" \
		     "(hostfsd moved to c900oses/gotools; set \$C900_GOTOOLS to it)"
	elif make -s -C "$C900_GOTOOLS" hostfsd BIN="$HERE/build" >>"$LOG" 2>&1; then
		echo "  hostfsd: OK ($(wc -c < "$HERE/build/hostfsd") B)"
	else
		fail=$((fail+1))
		echo "  hostfsd: FAIL -- $(tail -3 "$LOG" | head -1)"
	fi
else
	echo "  hostfsd: skipped (no go on PATH)"
fi

[ "$fail" = 0 ]
