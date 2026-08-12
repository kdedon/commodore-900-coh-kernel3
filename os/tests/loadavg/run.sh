#!/bin/sh
# tests/loadavg/run.sh -- the kernel's load average, watched over time.
#
# A load average that is merely non-zero has not been shown to be a load
# average.  This runs one timeline under the instruction-level emulator --
# idle, three spinning processes for 140 of the guest's own seconds, idle
# again for 75 -- and checks the SHAPE: it starts at zero, rises toward the
# number of processes wanting the processor, orders itself one > five >
# fifteen minutes while it is rising, and decays monotonically afterwards.
#
# The numbers are compared against the exponential the constants describe
# (sched.h): after 140 s of three spinners the one-minute average is
# 3*(1-exp(-140/60)) = 2.71, and 75 s later 2.71*exp(-75/60) = 0.78.  The
# bounds below are those, loosened by the sampling jitter of a 5-second
# sample taken by a process that has to be woken to take it.
#
# Needs a relinked kernel in hostbuild/kobj/kernel.out and drivers in
# hostbuild/build/drv rebuilt against it (ld -k bakes absolute kernel
# addresses); the script checks the two stamps agree before booting.  The
# kernel and the test program are injected into a COPY of the shipped image.
#
#	sh run.sh		the gate
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
C900_ROOT=$(cd "$OS/.." && pwd)
. "$C900_ROOT/mk/emulator.sh"
emu_need "boot the target and run this gate"

BAD=0
fail() { echo "  FAIL $*"; BAD=$((BAD + 1)); }
ok()   { echo "  ok   $*"; }

for f in "$IMG" "$CCZ" "$HB/kobj/kernel.out"; do
	[ -e "$f" ] || { echo "loadavg: missing $f -- cannot run" >&2; exit 2; }
done

WORK=${WORK:-$HERE/work}
rm -rf "$WORK"; mkdir -p "$WORK"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15

# ---------------------------------------------------------------- phase 0
echo "phase 0: kernel and drivers were linked against each other"
klink=$(sed -n 's/^linkid=//p' "$HB/kobj/kernel.stamp" 2>/dev/null)
dlink=$(sed -n 's/^kernel_linkid=//p' "$HB/build/drv/.drvstamp" 2>/dev/null)
if [ -n "$klink" ] && [ "$klink" = "$dlink" ]; then
	ok "kernel link id $klink, drivers built against $dlink"
else
	fail "kernel link id '$klink' but drivers say '$dlink' -- rebuild drivers"
	echo "loadavg: cannot continue"; exit 1
fi

# ---------------------------------------------------------------- phase 1
echo "phase 1: build loadavg and stage an image"
if "$CCZ" -s -i -I "$OS/include" -I "$OS/include/sys" \
	-o "$WORK/loadavg" "$HERE/loadavg.c" > "$WORK/cc.log" 2>&1; then
	ok "loadavg: $(wc -c < "$WORK/loadavg") B"
else
	fail "loadavg did not compile: $(tail -1 "$WORK/cc.log")"
	echo "loadavg: cannot continue"; exit 1
fi
# Over /bin/banner: no new inode, no directory surgery, and banner is on no
# path this test walks.
cp --reflink=auto -f "$IMG" "$WORK/lav.bin" 2>/dev/null || cp -f "$IMG" "$WORK/lav.bin"
set -- "$WORK/loadavg" /bin/banner "$HB/kobj/kernel.out" /coherent
for d in notty lrtty hrtty; do
	[ -f "$HB/build/drv/$d" ] && set -- "$@" "$HB/build/drv/$d" "/drv/$d"
done
python3 "$HERE/../rawalign/inject.py" "$WORK/lav.bin" "$ROOTPART" "$@" \
	> "$WORK/inj.log" 2>&1 || { fail "inject into the test image"; exit 1; }
ok "kernel, drivers and loadavg staged"

# ---------------------------------------------------------------- phase 2
echo "phase 2: 48 samples -- idle, 3 spinners for 140 s, idle again"
echo "/bin/banner 5 48 3 4 32" > "$WORK/cmds"
cp -f "$WORK/lav.bin" "$HB/build/loadavg-t.bin"
# Every path is expanded into its own variable first: assignments in one
# command prefix ARE visible to the later ones in the same prefix, so writing
# WORK=$WORK/... ahead of OUT=$WORK/... would put the transcript inside the
# image file.
rwork=$WORK/work.bin; rout=$WORK/out; rerr=$WORK/err
WORK=$rwork OUT=$rout ERR=$rerr \
	sh "$HB/emu-run.sh" "$WORK/cmds" "loadavg-t" > "$WORK/harness" 2>&1
rm -f "$HB/build/loadavg-t.bin"
if [ -s "$WORK/out" ]; then
	ok "the run produced a transcript"
else
	fail "no transcript"; echo "loadavg: cannot continue"; exit 1
fi
grep '^loadavg:' "$WORK/out" | sed 's/^/       | /'

# ---------------------------------------------------------------- phase 3
# The samples, checked against the exponential the constants describe.
echo "phase 3: the shape"
python3 - "$WORK/out" <<'PY' || BAD=$((BAD + 1))
import re, sys

FSCALE = 2048.0
rows = []
for line in open(sys.argv[1], errors='replace'):
    m = re.match(r'loadavg:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s', line)
    if m:
        t, a1, a5, a15 = (int(x) for x in m.groups())
        rows.append((t, a1 / FSCALE, a5 / FSCALE, a15 / FSCALE))

bad = 0
def fail(m):
    global bad
    print("  FAIL %s" % m); bad += 1
def ok(m):
    print("  ok   %s" % m)

if len(rows) < 40:
    fail("only %d samples -- the run did not finish" % len(rows))
    sys.exit(1)

if rows[0][1] == 0.0 and rows[1][1] == 0.0:
    ok("an idle machine reads 0.00")
else:
    fail("idle machine read %.2f then %.2f" % (rows[0][1], rows[1][1]))

# Sample 32 is where the spinners are killed; 3*(1-exp(-140/60)) = 2.71.
peak = max(r[1] for r in rows)
pi = max(range(len(rows)), key=lambda i: rows[i][1])
if 2.40 <= peak <= 3.00:
    ok("three spinners for 140 s carried it to %.2f (model 2.71)" % peak)
else:
    fail("peak %.2f, expected 2.40..3.00 (model 2.71)" % peak)

rise = [r[1] for r in rows[2:pi + 1]]
if all(b >= a for a, b in zip(rise, rise[1:])):
    ok("it rose without going backwards (%d samples)" % len(rise))
else:
    fail("the rise was not monotone: %s" % rise)

# While it rises, a shorter average has to be ahead of a longer one.
mid = rows[pi]
if mid[1] > mid[2] > mid[3]:
    ok("at the peak 1 > 5 > 15 min (%.2f > %.2f > %.2f)" % mid[1:])
else:
    fail("averages out of order at the peak: %.2f %.2f %.2f" % mid[1:])

# 2.71*exp(-75/60) = 0.78 at the last sample.
tail = [r[1] for r in rows[pi + 1:]]
if tail and all(b <= a for a, b in zip(tail, tail[1:])):
    ok("it decayed without going backwards (%d samples)" % len(tail))
else:
    fail("the decay was not monotone: %s" % tail)
last = rows[-1][1]
if 0.50 <= last <= 1.15:
    ok("75 s after the load stopped it was %.2f (model 0.78)" % last)
else:
    fail("%.2f at the end, expected 0.50..1.15 (model 0.78)" % last)

sys.exit(1 if bad else 0)
PY

echo
if [ "$BAD" = 0 ]; then
	echo "loadavg: PASS -- the kernel's load average starts at zero, rises"
	echo "         toward the number of processes wanting the processor on"
	echo "         its stated time constant, and decays on the same one"
	exit 0
fi
echo "loadavg: FAIL ($BAD)"
exit 1
