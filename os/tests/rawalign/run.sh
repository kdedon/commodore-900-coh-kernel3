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
# Controls: offset 0 must pass whatever the driver does, and with CONTROL
# naming a kernel that refuses the transfer, phase 2 runs the identical
# commands against it, requiring the misaligned cases to FAIL there.
#
# `ld -k' bakes absolute kernel addresses into the drivers, so the script
# checks the kernel and driver stamps agree before booting.  The kernel and
# the test program are put into COPIES of the test image.
#
#	sh run.sh					on hostbuild/kobj
#	KERNEL=<kernel.out> DRV=<dir> sh run.sh		another kernel
#	CONTROL=<kernel.out> CONTROLDRV=<dir> sh run.sh	with the negative control
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

BAD=0
fail() { echo "  FAIL $*"; BAD=$((BAD + 1)); }
ok()   { echo "  ok   $*"; }

for f in "$CCZ" "$KERNEL" "$DRV/notty" "$DRV/lrtty" "$DRV/hrtty" "$IMG" \
	 "$COHFS" "$UL/hostbuild/emu-run.sh"; do
	[ -e "$f" ] || { echo "rawalign: missing $f -- cannot run" >&2; exit 2; }
done

WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"

# ---------------------------------------------------------------- phase 0
# The drivers are linked against the kernel's absolute addresses.  A kernel
# newer than the drivers boots and then calls the console driver at addresses
# that moved, which does not look like a stale build -- it looks like this
# test's own I/O hanging.
echo "phase 0: kernel and drivers were linked against each other"
klink=$(sed -n 's/^linkid=//p' "$(dirname "$KERNEL")/kernel.stamp" 2>/dev/null)
dlink=$(sed -n 's/^kernel_linkid=//p' "$DRV/.drvstamp" 2>/dev/null)
if [ -n "$klink" ] && [ "$klink" = "$dlink" ]; then
	ok "kernel link id $klink, drivers built against $dlink"
else
	fail "kernel link id '$klink' but drivers say '$dlink' -- rebuild drivers"
	echo "rawalign: cannot continue"; exit 1
fi

# ---------------------------------------------------------------- phase 1
echo "phase 1: build rawalign and stage the images"
if "$CCZ" -s -i -o "$WORK/rawalign" "$HERE/rawalign.c" > "$WORK/cc.log" 2>&1; then
	ok "rawalign: $(wc -c < "$WORK/rawalign") B"
else
	fail "rawalign did not compile: $(tail -1 "$WORK/cc.log")"
	echo "rawalign: cannot continue"; exit 1
fi

# stage <tag> <kernel> <drvdir> -- a copy of the image carrying the probe, a
# kernel and its drivers.
stage() {
	cp -f "$IMG" "$WORK/$1.bin"
	put() { "$COHFS" put -p "$ROOTPART" -m 755 "$WORK/$tag.bin" "$1" "$2" \
		>> "$WORK/put.$tag.log" 2>&1; }
	tag=$1
	put "$WORK/rawalign" /bin/rawalign && put "$2" /coherent || return 1
	for d in notty lrtty hrtty; do
		[ -f "$3/$d" ] && { put "$3/$d" "/drv/$d" || return 1; }
	done
	return 0
}
stage new "$KERNEL" "$DRV" ||
	{ fail "cannot stage the test image, see $WORK/put.new.log"; exit 1; }
if [ -n "${CONTROL:-}" ]; then
	stage ctl "$CONTROL" "${CONTROLDRV:-$DRV}" ||
		{ fail "cannot stage the control image"; exit 1; }
	ok "control = $CONTROL, test = $KERNEL + drv"
else
	ok "test = $KERNEL + drv; no CONTROL kernel, so no negative control"
fi

# ---------------------------------------------------------------- phase 2
# hd4 is the root filesystem and hd3 is /tmp+swap.  /tmp is unmounted first
# so that the write direction may land on hd3; reading hd4 raw while it is
# mounted is fine, writing it would not be.
cat > "$WORK/cmds" <<'EOF'
/etc/umount /dev/hd3 && echo rawalign: umounted
/bin/rawalign r /dev/rhd4 /dev/hd4 1 0
/bin/rawalign r /dev/rhd4 /dev/hd4 4 1
/bin/rawalign r /dev/rhd4 /dev/hd4 8 256
/bin/rawalign w /dev/rhd3 /dev/hd3 4 3 77
/bin/rawalign w /dev/rhd3 /dev/hd3 1 0 11
EOF
# Both writes start at block 0, so the order is load-bearing: the misaligned
# one covers blocks 0..3 and the aligned one then takes block 0 back.  Phase 4
# reads block 0 as the aligned write's and blocks 1..3 as the misaligned one's,
# which is only true in this order.

run() {
	tag=$1
	# Expand every path here, into its own variable: assignments in one
	# command prefix ARE visible to the later ones in the same prefix, so
	# writing WORK=$WORK/... ahead of OUT=$WORK/... would make OUT a path
	# under the image file.
	rwork=$WORK/work.$tag.bin; rout=$WORK/$tag.out; rerr=$WORK/$tag.err
	rcmds=$WORK/cmds; rharn=$WORK/$tag.harness
	WORK=$rwork OUT=$rout ERR=$rerr \
		sh "$UL/hostbuild/emu-run.sh" "$rcmds" "$WORK/$tag.bin" \
		> "$rharn" 2>&1
	[ -s "$WORK/$tag.out" ] || return 1
	grep -a -q 'rawalign: umounted' "$WORK/$tag.out" ||
		{ fail "$tag: /tmp would not unmount"; return 1; }
}

# matches <tag> -- how many of the five invocations reported a byte-for-byte
# match.  The two aligned ones are expected to match on both images, so a run
# that boots at all scores at least 2.
matches() { grep -a -c 'rawalign: MATCH' "$WORK/$1.out"; }

if [ -n "${CONTROL:-}" ]; then
	echo "phase 2: the control kernel refuses the misaligned transfers"
	if run ctl; then
		m=$(matches ctl)
		if [ "$m" = 2 ]; then
			ok "only the two aligned transfers succeeded"
		else
			fail "$m of 5 matched on the control -- expected the 2 aligned ones"
			grep -a rawalign "$WORK/ctl.out" | sed 's/^/       | /'
		fi
		if grep -a -q 'non-aligned dma in RAW mode' "$WORK/ctl.out"; then
			ok "the driver said so: non-aligned dma in RAW mode"
		else
			fail "the control did not refuse -- these assertions measure nothing"
		fi
	else
		fail "the control run produced no usable transcript"
	fi
fi

# ---------------------------------------------------------------- phase 3
echo "phase 3: the kernel under test bounces them, and the bytes agree"
if run new; then
	m=$(matches new)
	if [ "$m" = 5 ]; then
		ok "all 5 transfers matched byte for byte (1, 4 and 8 sectors)"
	else
		fail "only $m of 5 matched"
		grep -a rawalign "$WORK/new.out" | sed 's/^/       | /'
	fi
	if grep -a -q 'non-aligned dma in RAW mode' "$WORK/new.out"; then
		fail "the driver still refused a transfer"
	else
		ok "no transfer was refused"
	fi
else
	fail "the test run produced no usable transcript"
fi

# ---------------------------------------------------------------- phase 4
# The guest compared what it wrote against what the block device read back,
# and both of those go through the same kernel.  This reads the image on the
# HOST and predicts every byte of the pattern from the seed alone, so it does
# not take the guest's word for anything.
echo "phase 4: the pattern is on the disk, checked on the host"
set -- "$WORK/work.new.bin"
want=11
if [ -n "${CONTROL:-}" ]; then
	set -- "$@" "$WORK/work.ctl.bin"
	want=1110
fi
if [ -s "$1" ]; then
	seen=$(python3 - "$@" <<'PY'
import sys
BASE = 31008 * 512      # /dev/hd3 on the test image
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
	if [ "$seen" = "$want" ]; then
		ok "the misaligned pattern is on the disk"
		[ -n "${CONTROL:-}" ] &&
			ok "  the fixed kernel wrote; the control image does not carry it"
	else
		fail "host check of /dev/hd3 gave $seen, expected $want"
		fail "  (new aligned, new misaligned[, ctl aligned, ctl misaligned])"
	fi
else
	fail "phase 3 left no image to inspect"
fi

echo
if [ "$BAD" = 0 ]; then
	echo "rawalign: PASS -- misaligned raw reads and writes transfer the right"
	echo "          bytes  ($WORK)"
	exit 0
fi
echo "rawalign: FAIL ($BAD)  ($WORK)"
exit 1
