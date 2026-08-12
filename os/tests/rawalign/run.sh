#!/bin/sh
# tests/rawalign/run.sh -- raw disk I/O out of a buffer that is not on a
# 512-byte boundary, on the target, checked byte for byte.
#
# The C900's DMA address counter is four TTL chips on the motherboard, and
# only the low byte (U82/U83, 74LS193) counts: the carry reaches neither U80
# (address mid) nor U81 (address high), so a transfer wraps after 256 words
# and stays inside the 512-byte-aligned window its address fell in.  wd(4)
# therefore runs any transfer whose buffer is not 512-aligned one sector at a
# time through an aligned kernel buffer.  Raw (character-device) I/O DMAs
# straight into the caller's buffer, so it is the case that needs the bounce;
# block I/O stages through the aligned buffer cache.
#
# A bounce that quietly does nothing returns the full count, so nothing here
# asserts on a return code alone: rawalign reads the same blocks through the
# raw and block devices into buffers at the SAME misalignment and compares
# every byte, and the write direction writes a seeded pattern raw and reads
# it back through the block device (the reference, since it cannot be wrong
# in the way under test).
#
# Controls: offset 0 must pass whatever the driver does, and phase 2 runs the
# identical commands against the SHIPPED image, whose kernel refuses the
# transfer, requiring the misaligned cases to FAIL there.
#
# Needs a relinked kernel in hostbuild/kobj/kernel.out and drivers in
# hostbuild/build/drv rebuilt against it (ld -k bakes absolute kernel
# addresses); the script checks the two stamps agree before booting.  The
# kernel and the test program are injected into COPIES of the shipped image.
#
#	sh run.sh		positive + negative control
#	KEEP=1 sh run.sh	leave the work directory
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
HB=$(cd "$HERE/../../hostbuild" && pwd)
OS=$(cd "$HERE/../.." && pwd)
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
DIST=${DIST:-extended-both}
IMG=$HB/build/$DIST.bin
ROOTPART=${ROOTPART:-136}
# The emulator is resolved by mk/emulator.sh, the same search list that
# mk/compiler.mk and the hostbuild harnesses use; $EMUBIN names one
# explicitly.
C900_ROOT=$(cd "$OS/.." && pwd)
. "$C900_ROOT/mk/emulator.sh"
emu_need "boot the target and run this gate"
EMUBIN=$C900_EMU

BAD=0
fail() { echo "  FAIL $*"; BAD=$((BAD + 1)); }
ok()   { echo "  ok   $*"; }

for f in "$IMG" "$CCZ" "$HB/kobj/kernel.out"; do
	[ -e "$f" ] || { echo "rawalign: missing $f -- cannot run" >&2; exit 2; }
done

WORK=${WORK:-$HERE/work}
rm -rf "$WORK"
mkdir -p "$WORK"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15

# ---------------------------------------------------------------- phase 0
# The drivers are linked against the kernel's absolute addresses.  A kernel
# newer than the drivers boots and then calls the console driver at addresses
# that moved, which does not look like a stale build -- it looks like this
# test's own I/O hanging.
echo "phase 0: kernel and drivers were linked against each other"
klink=$(sed -n 's/^linkid=//p' "$HB/kobj/kernel.stamp" 2>/dev/null)
dlink=$(sed -n 's/^kernel_linkid=//p' "$HB/build/drv/.drvstamp" 2>/dev/null)
if [ -n "$klink" ] && [ "$klink" = "$dlink" ]; then
	ok "kernel link id $klink, drivers built against $dlink"
else
	fail "kernel link id '$klink' but drivers say '$dlink' -- rebuild drivers"
	echo "rawalign: cannot continue"; exit 1
fi

# ---------------------------------------------------------------- phase 1
echo "phase 1: build rawalign and stage two images"
if "$CCZ" -s -i -I "$OS/include" -I "$OS/include/sys" \
	-o "$WORK/rawalign" "$HERE/rawalign.c" > "$WORK/cc.log" 2>&1; then
	ok "rawalign: $(wc -c < "$WORK/rawalign") B"
else
	fail "rawalign did not compile: $(tail -1 "$WORK/cc.log")"
	echo "rawalign: cannot continue"; exit 1
fi
# The test program goes in over /bin/banner: this needs no new inode and no
# directory surgery, and banner is on no path this test walks.
cp --reflink=auto -f "$IMG" "$WORK/ctl.bin" 2>/dev/null || cp -f "$IMG" "$WORK/ctl.bin"
cp --reflink=auto -f "$IMG" "$WORK/new.bin" 2>/dev/null || cp -f "$IMG" "$WORK/new.bin"
python3 "$HERE/inject.py" "$WORK/ctl.bin" "$ROOTPART" \
	"$WORK/rawalign" /bin/banner > "$WORK/inj.ctl.log" 2>&1 || {
	fail "inject into the control image"; exit 1; }
set -- "$WORK/rawalign" /bin/banner "$HB/kobj/kernel.out" /coherent
for d in notty lrtty hrtty; do
	[ -f "$HB/build/drv/$d" ] && set -- "$@" "$HB/build/drv/$d" "/drv/$d"
done
python3 "$HERE/inject.py" "$WORK/new.bin" "$ROOTPART" "$@" \
	> "$WORK/inj.new.log" 2>&1 || { fail "inject into the test image"; exit 1; }
ok "control = shipped kernel, test = kobj/kernel.out + drv"

# ---------------------------------------------------------------- phase 2
# hd4 is the root filesystem and hd3 is /tmp+swap, which single user has not
# mounted -- so hd3 is where the write direction may land.  Reading hd4 raw
# while it is mounted is fine; writing it would not be.
cat > "$WORK/cmds" <<'EOF'
/bin/banner r /dev/rhd4 /dev/hd4 1 0
/bin/banner r /dev/rhd4 /dev/hd4 4 1
/bin/banner r /dev/rhd4 /dev/hd4 8 256
/bin/banner w /dev/rhd3 /dev/hd3 4 3 77
/bin/banner w /dev/rhd3 /dev/hd3 1 0 11
EOF
# Both writes start at block 0, so the order is load-bearing: the misaligned
# one covers blocks 0..3 and the aligned one then takes block 0 back.  Phase 4
# reads block 0 as the aligned write's and blocks 1..3 as the misaligned one's,
# which is only true in this order.

run() {
	tag=$1
	cp -f "$WORK/$tag.bin" "$HB/build/rawalign-$tag.bin"
	# Expand every path here, into its own variable: assignments in one
	# command prefix ARE visible to the later ones in the same prefix, so
	# writing WORK=$WORK/... ahead of OUT=$WORK/... would make OUT a path
	# under the image file.
	rwork=$WORK/work.$tag.bin; rout=$WORK/$tag.out; rerr=$WORK/$tag.err
	rcmds=$WORK/cmds; rharn=$WORK/$tag.harness
	WORK=$rwork OUT=$rout ERR=$rerr \
		sh "$HB/emu-run.sh" "$rcmds" "rawalign-$tag" > "$rharn" 2>&1
	rm -f "$HB/build/rawalign-$tag.bin"
	[ -s "$WORK/$tag.out" ]
}

# matches <tag> -- how many of the five invocations reported a byte-for-byte
# match.  The two aligned ones are expected to match on both images, so a run
# that boots at all scores at least 2.
matches() { grep -c 'rawalign: MATCH' "$WORK/$1.out"; }

echo "phase 2: the shipped kernel refuses the misaligned transfers"
if run ctl; then
	m=$(matches ctl)
	if [ "$m" = 2 ]; then
		ok "only the two aligned transfers succeeded"
	else
		fail "$m of 5 matched on the control -- expected the 2 aligned ones"
		grep rawalign "$WORK/ctl.out" | sed 's/^/       | /'
	fi
	if grep -q 'non-aligned dma in RAW mode' "$WORK/ctl.out"; then
		ok "the driver said so: non-aligned dma in RAW mode"
	else
		fail "the control did not refuse -- these assertions measure nothing"
	fi
else
	fail "the control run produced no transcript"
fi

# ---------------------------------------------------------------- phase 3
echo "phase 3: the relinked kernel bounces them, and the bytes agree"
if run new; then
	m=$(matches new)
	if [ "$m" = 5 ]; then
		ok "all 5 transfers matched byte for byte (1, 4 and 8 sectors)"
	else
		fail "only $m of 5 matched"
		grep rawalign "$WORK/new.out" | sed 's/^/       | /'
	fi
	if grep -q 'non-aligned dma in RAW mode' "$WORK/new.out"; then
		fail "the driver still refused a transfer"
	else
		ok "no transfer was refused"
	fi
else
	fail "the test run produced no transcript"
fi

# ---------------------------------------------------------------- phase 4
# The guest compared what it wrote against what the block device read back,
# and both of those go through the same kernel.  This reads the image on the
# HOST and predicts every byte of the pattern from the seed alone, so it does
# not take the guest's word for anything.
echo "phase 4: the pattern is on the disk, checked on the host"
if [ -s "$WORK/work.new.bin" ] && [ -s "$WORK/work.ctl.bin" ]; then
	seen=$(python3 - "$WORK/work.new.bin" "$WORK/work.ctl.bin" <<'PY'
import sys
BASE = 31008 * 512      # /dev/hd3 on hd42-coh.media
seed11 = bytes((11 + i * 7) & 0xFF for i in range(512))
seed77 = bytes((77 + i * 7) & 0xFF for i in range(2048))
out = []
for p in sys.argv[1:]:
    d = open(p, 'rb').read()[BASE:BASE + 2048]
    out.append('1' if d[:512] == seed11 else '0')
    out.append('1' if d[512:2048] == seed77[512:2048] else '0')
print("".join(out))
PY
)
	# new: aligned landed, misaligned landed.  ctl: aligned landed, misaligned
	# refused and so absent -- which is what licenses reading the other three.
	case "$seen" in
	1110)	ok "the misaligned pattern is on the disk, and only on the disk"
		ok "  the fixed kernel wrote; the control image does not carry it"
		;;
	*)	fail "host check of /dev/hd3 gave $seen, expected 1110"
		fail "  (new aligned, new misaligned, ctl aligned, ctl misaligned)"
		;;
	esac
else
	fail "phase 3 left no image to inspect"
fi

echo
if [ "$BAD" = 0 ]; then
	echo "rawalign: PASS -- misaligned raw reads and writes transfer the right"
	echo "          bytes, and the same checks demonstrably fail on the kernel"
	echo "          that refuses them"
	exit 0
fi
echo "rawalign: FAIL ($BAD)"
exit 1
