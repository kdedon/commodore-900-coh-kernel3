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
# `ld -k' bakes absolute kernel addresses into the drivers, so the script
# checks the kernel and driver stamps agree before booting.  The kernel and
# the test program are put into a COPY of the test image.
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

BAD=0
fail() { echo "  FAIL $*"; BAD=$((BAD + 1)); }
ok()   { echo "  ok   $*"; }

for f in "$CCZ" "$KERNEL" "$DRV/notty" "$DRV/lrtty" "$DRV/hrtty" "$IMG" \
	 "$COHFS" "$UL/hostbuild/emu-run.sh"; do
	[ -e "$f" ] || { echo "loadavg: missing $f -- cannot run" >&2; exit 2; }
done

WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"

# ---------------------------------------------------------------- phase 0
echo "phase 0: kernel and drivers were linked against each other"
klink=$(sed -n 's/^linkid=//p' "$(dirname "$KERNEL")/kernel.stamp" 2>/dev/null)
dlink=$(sed -n 's/^kernel_linkid=//p' "$DRV/.drvstamp" 2>/dev/null)
if [ -n "$klink" ] && [ "$klink" = "$dlink" ]; then
	ok "kernel link id $klink, drivers built against $dlink"
else
	fail "kernel link id '$klink' but drivers say '$dlink' -- rebuild drivers"
	echo "loadavg: cannot continue"; exit 1
fi

# ---------------------------------------------------------------- phase 1
echo "phase 1: build loadavg and stage an image"
if "$CCZ" -s -i -o "$WORK/loadavg" "$HERE/loadavg.c" > "$WORK/cc.log" 2>&1; then
	ok "loadavg: $(wc -c < "$WORK/loadavg") B"
else
	fail "loadavg did not compile: $(tail -1 "$WORK/cc.log")"
	echo "loadavg: cannot continue"; exit 1
fi
cp -f "$IMG" "$WORK/lav.bin"
put() { "$COHFS" put -p "$ROOTPART" -m 755 "$WORK/lav.bin" "$1" "$2" \
	>> "$WORK/put.log" 2>&1 ||
	{ fail "cannot put $2"; echo "loadavg: cannot continue"; exit 1; }; }
put "$KERNEL" /coherent
for d in notty lrtty hrtty; do put "$DRV/$d" "/drv/$d"; done
put "$WORK/loadavg" /bin/loadavg
ok "kernel, drivers and loadavg staged"

# ---------------------------------------------------------------- phase 2
echo "phase 2: 48 samples -- idle, 3 spinners for 140 s, idle again"
# The multi-user boot leaves about 0.07 on the one-minute average; five
# minutes decays it to zero.
printf 'sleep 300\n/bin/loadavg 5 48 3 4 32\n' > "$WORK/cmds"
# Every path is expanded into its own variable first: assignments in one
# command prefix ARE visible to the later ones in the same prefix, so writing
# WORK=$WORK/... ahead of OUT=$WORK/... would put the transcript inside the
# image file.
rwork=$WORK/work.bin; rout=$WORK/out; rerr=$WORK/err
WORK=$rwork OUT=$rout ERR=$rerr \
	sh "$UL/hostbuild/emu-run.sh" "$WORK/cmds" "$WORK/lav.bin" \
	> "$WORK/harness" 2>&1
if [ -s "$WORK/out" ]; then
	ok "the run produced a transcript"
else
	fail "no transcript"; echo "loadavg: cannot continue"; exit 1
fi
grep -a '^loadavg:' "$WORK/out" | sed 's/^/       | /'

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
echo "loadavg: FAIL ($BAD)  ($WORK)"
exit 1
