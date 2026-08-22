#!/bin/sh
# build-drivers.sh -- build loadable device drivers, output to build/drv/<name>.
# Drivers are linked via `ld -k' against kernel symbols and must be executable.
# Prereq: link-kernel.sh (kobj/kernel.out is the -k symbol source).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)

# Lock to prevent concurrent builds from clobbering build/drvobj intermediates.
if [ -z "${DRVLOCKED:-}" ] && command -v flock >/dev/null 2>&1; then
	mkdir -p "$HERE/build"
	DRVLOCKED=1; export DRVLOCKED
	exec flock "$HERE/build/.drivers.lock" /bin/sh "$0" "$@"
fi

OS=$(cd "$HERE/.." && pwd)
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CC0="$TCB/z8001/cc0-z8001"; CC1="$TCB/z8001/cc1-z8001"
CC2="$TCB/z8001/cc2-z8001"; LD="$TCB/ld-z8001"; AS="$TCB/as-z8001"
VAR="${CCZ_VAR:-800000020800}"
KSYM="$HERE/kobj/kernel.out"
OBJ="$HERE/build/drvobj"; OUT="$HERE/build/drv"
LOG="$HERE/logs/drivers.log"; mkdir -p "$HERE/logs" "$OBJ" "$OUT"; : > "$LOG"

[ -f "$KSYM" ] || { echo "missing $KSYM (run link-kernel.sh first)" >&2; exit 1; }

. "$HERE/provenance.sh"
prov_header "toolchain" "$TCB/z8001/.provenance" || true
# Drivers linked via ld -k bake absolute kernel addresses, so track the kernel link ID.
# This pairing must be recorded (not mtime-inferred) to detect relinks to byte-identical output.
KLINKID=$(prov_id "$KSYM")
echo "== linking drivers against kernel link id $KLINKID"

# cc2 mode 0012 (VPEEP + VKERN) matches the kernel; drivers run on kernel frame convention.
IMI="-I$OS/include -I$OS/include/sys -I$TCINC -I$TCINC/sys -I$OS/sys/z8001/h"
KDEFS="-DNOMONITOR=1"
CC2MODE=0012
DRVLDFLAGS=-X

ok=0; fail=0

cc_one() {	# cc_one <src> [extra-cpp-defines...] -> $OBJ/<stem>.o
	src=$1; shift; stem=$(basename "$src" .c)
	c900_buildlog "$src"
	"$CC0" $VAR "$src" "$OBJ/$stem.z0" $IMI $KDEFS "$@" >>"$LOG" 2>&1 &&
	"$CC1" $VAR "$OBJ/$stem.z0" "$OBJ/$stem.z1" >>"$LOG" 2>&1 &&
	"$CC2" "$CC2MODE" "$OBJ/$stem.z1" "$OBJ/$stem.o" "$OBJ/$stem.scr" 0 >>"$LOG" 2>&1
}

as_one() {	# as_one <src.s> [extra-cpp-defines...] -> $OBJ/<stem>.o
	src=$1; shift; stem=$(basename "$src" .s)
	c900_buildlog "$src"
	cpp -traditional-cpp -P "$@" $IMI "$src" > "$OBJ/$stem.i" 2>>"$LOG" &&
	"$AS" -g -o "$OBJ/$stem.o" "$OBJ/$stem.i" >>"$LOG" 2>&1
}

# Link to a side name and rename into place to ensure atomicity (build/drv is
# read concurrently by dist.py).
link_drv() {	# link_drv <name> <obj...>
	name=$1; shift
	if "$LD" $DRVLDFLAGS -o "$OUT/.$name.new" "$@" -k"$KSYM" >>"$LOG" 2>&1; then
		chmod +x "$OUT/.$name.new"
		mv -f "$OUT/.$name.new" "$OUT/$name"
		ok=$((ok+1)); echo "  $name: OK ($(wc -c < "$OUT/$name") B)"
	else
		rm -f "$OUT/.$name.new"
		fail=$((fail+1))
		echo "  $name: FAIL -- $(grep -iE 'error|undefined|no match|Internal' "$LOG" | grep -v 'Strict\|Warning' | tail -1)"
	fi
}

fail_drv() { name=$1
	fail=$((fail+1))
	echo "  $name: FAIL (compile) -- $(grep -iE 'error|no match|Internal' "$LOG" | grep -v 'Strict\|Warning' | tail -1)"
}

# notty: serial console (major 8); alternatives are lrtty or hrtty for displays.
if cc_one "$OS/sys/z8001/drv/notty.c"; then
	link_drv notty "$OBJ/notty.o"
else
	fail_drv notty
fi

# lrtty: keyboard + LO-RES TEXT console at major 8 (rec/ is the 6845 text card driver).
# kbtab.c is the Commodore extended keyboard table; mmas.s preprocessed without DDT.
if cc_one "$OS/sys/z8001/rec/kv.c" &&
   cc_one "$OS/sys/z8001/rec/v0.c" &&
   cc_one "$OS/sys/z8001/rec/mm.c" &&
   cc_one "$OS/sys/z8001/rec/kb.c" &&
   cc_one "$OS/sys/z8001/rec/kbtab.c" &&
   as_one "$OS/sys/z8001/rec/mmas.s" -UDDT
then
	link_drv lrtty "$OBJ/kv.o" "$OBJ/v0.o" "$OBJ/mm.o" "$OBJ/mmas.o" \
		       "$OBJ/kb.o" "$OBJ/kbtab.o"
else
	fail_drv lrtty
fi

# hrtty: keyboard + HI-RES BITMAP console at major 8; sources use os/hrtty/h on include path.
HRI="-I$OS/hrtty/h -I$OS/hrtty/src"
if cc_one "$OS/hrtty/src/hrterm1.c" $HRI &&
   cc_one "$OS/hrtty/src/hrterm2.c" $HRI &&
   cc_one "$OS/hrtty/src/gall.c" $HRI &&
   cc_one "$OS/hrtty/src/scrollu.c" $HRI &&
   cc_one "$OS/hrtty/src/subr.c" $HRI &&
   cc_one "$OS/hrtty/src/kb.c" $HRI &&
   cc_one "$OS/hrtty/src/kv.c" $HRI &&
   cc_one "$OS/hrtty/src/kbtab.c" $HRI &&
   as_one "$OS/hrtty/src/scroll.s" -UDDT
then
	link_drv hrtty "$OBJ/hrterm1.o" "$OBJ/hrterm2.o" "$OBJ/gall.o" \
		       "$OBJ/scrollu.o" "$OBJ/subr.o" \
		       "$OBJ/kb.o" "$OBJ/kv.o" "$OBJ/kbtab.o" "$OBJ/scroll.o"
else
	fail_drv hrtty
fi

# hostfs is development-only; relink if present (ld -k bakes kernel addresses).
[ -x "$HERE/build-hostfs.sh" ] && sh "$HERE/build-hostfs.sh" >/dev/null 2>&1

echo "== drivers: $ok built, $fail failed"
# Write pairing record only if all drivers linked (half-built build/drv is not paired).
if [ "$fail" = 0 ]; then
	prov_write "$OUT/.drvstamp" drivers \
		os/sys/z8001/drv os/sys/z8001/rec \
		os/hrtty os/hostbuild/build-drivers.sh \
		-- "kernel_linkid=$KLINKID" \
		   "drivers=$(cd "$OUT" && ls | grep -v '^\.' | tr '\n' ' ')"
	echo "== drv stamp: kernel link id $KLINKID"
	# The compiler contract, published beside the drivers it produced, so a
	# loadable driver whose source lives in another repository is built with
	# the settings THIS kernel was built with rather than with a copy of them
	# kept by the consumer.  pack-kernel.sh composes the release's kdrv.conf
	# from this file; a dot name keeps it out of the driver list above.
	cat > "$OUT/.kdrv.conf" <<EOF
kernel_linkid=$KLINKID
ccvar=$VAR
cc2mode=$CC2MODE
cdefs=$KDEFS
ldflags=$DRVLDFLAGS
EOF
else
	rm -f "$OUT/.drvstamp" "$OUT/.kdrv.conf"
fi
[ "$fail" = 0 ]
