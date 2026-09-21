#!/bin/sh
# tests/sbrkgap/run.sh -- boot the kernel and run sbrkgap(1).
#
#	sh run.sh				on hostbuild/kobj
#	KERNEL=<kernel.out> DRV=<dir> sh run.sh	another kernel
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
UL=${C900_USERLAND:-$(cd "$OS/../.." && pwd)/commodore-900-coh-userland}
TCD=${C900_TOOLCHAIN:-$(cd "$OS/../.." && pwd)/commodore-900-toolchain}
CCZ=$TCD/host/ccz
KERNEL=${KERNEL:-$OS/hostbuild/kobj/kernel.out}
DRV=${DRV:-$OS/hostbuild/build/drv}
IMG=${IMG:-$UL/hostbuild/build/test.bin}
COHFS=$(sh "$UL/mk/deps.sh" tools)/bin/cohfs
ROOTPART=${ROOTPART:-136}
for f in "$CCZ" "$KERNEL" "$DRV/notty" "$DRV/lrtty" "$DRV/hrtty" "$IMG" \
	 "$COHFS" "$UL/hostbuild/emu-run.sh"; do
	[ -e "$f" ] || { echo "sbrkgap: missing $f -- cannot run" >&2; exit 2; }
done

WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
"$CCZ" -s -i -o "$WORK/sbrkgap" "$HERE/sbrkgap.c" > "$WORK/cc.log" 2>&1 ||
	{ echo "sbrkgap: probe did not compile, see $WORK/cc.log" >&2; exit 2; }
cp -f "$IMG" "$WORK/gate.bin"
put() { "$COHFS" put -p "$ROOTPART" -m 755 "$WORK/gate.bin" "$1" "$2" \
	>> "$WORK/put.log" 2>&1 ||
	{ echo "sbrkgap: cannot put $2" >&2; exit 2; }; }
put "$KERNEL" /coherent
for d in notty lrtty hrtty; do put "$DRV/$d" "/drv/$d"; done
put "$WORK/sbrkgap" /bin/sbrkgap

cat > "$WORK/cmds" <<'EOF'
cd /tmp
/bin/sbrkgap
echo alive
EOF
W=$WORK
WORK="$W/run.bin" OUT="$W/out" ERR="$W/err" \
	sh "$UL/hostbuild/emu-run.sh" "$W/cmds" "$W/gate.bin" \
	> "$W/harness" 2>&1
grep -a 'stop channel' "$W/harness"
grep -a -E '^sbrkgap:' "$W/out"
if grep -a -q '^sbrkgap: PASS' "$W/out"; then
	echo "gate: PASS  ($W)"; exit 0
fi
echo "gate: FAIL  ($W)"; exit 1
