#!/bin/sh
# tests/deepstack/run.sh -- the stack allowance gate, ON A BOOTED SYSTEM.
#
# deepstack(1) recurses in a child until the stack runs out and reports, from
# the record file the child left on the disk, how much stack it really had and
# how it died.  This drives it at four frame sizes on a booted COHERENT and
# fails unless every one of them reaches the allowance.
#
# WHY IT HAS TO BOOT.  `c900 --exec' runs a guest program against the HOST's
# stack, so the probe sees a stack that grows the way no Z8001 can and passes
# whatever the kernel does.  A --exec check here would be vacuous by
# construction, which is how this defect survived a fix, a claim and a
# comment.
#
# WHY FOUR SIZES.  The kernel has been wrong about the stack twice, and each
# time some frame sizes worked.  The MMU's write-warning region is the stack
# segment's lowest 256-byte page, so a demand-grown stack behaves completely
# differently either side of that: with 16-byte frames it ran 1087 levels
# while 512-byte frames stopped at 8.  16, 256, 512 and 4096 straddle it.
#
# `ld -k' bakes absolute kernel addresses into /drv/*, so the drivers must
# have been rebuilt against the kernel under test.  Phase 0 checks the two
# stamps agree and the image really carries it, rather than trusting that
# someone remembered.
#
#	sh run.sh				on hostbuild/kobj
#	KERNEL=<kernel.out> DRV=<dir> sh run.sh	another kernel
#	CONTROL=<kernel.out> sh run.sh		also prove the gate can fail
#
# CONTROL names a kernel that does NOT hand out the allowance; the gate must
# fail against it, or it is measuring nothing.  To build one, revert the fix
# and relink:
#	git stash push ../../sys/z8001/src/exec.c ../../sys/z8001/src/trap.c
#	make -C ../../.. drivers	   # relinks the kernel, rebuilds /drv
#	cp ../../hostbuild/kobj/kernel.out /tmp/unfixed.out
#	cp -r ../../hostbuild/build/drv /tmp/unfixed.drv
#	git stash pop; make -C ../../.. drivers
#	CONTROL=/tmp/unfixed.out CONTROLDRV=/tmp/unfixed.drv sh run.sh
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

# The allowance and the sizes.  ALLOW must agree with MADSIZE in
# include/sys/machz8001.h; it is repeated here only to be printed, since the
# probe is what asserts on it.  BIG is a raised RLIMIT_STACK, in KB, that the
# probe sets for itself at BIGSIZES; MAXKB is the largest stack the kernel
# maps (STKMAX).  argmax checks the argument space at ARGKB limits.
ALLOW=16384
SIZES="16 256 512 4096"
BIG=32
BIGSIZES="16 4096"
MAXKB=62
ARGKB="16 32 $MAXKB"

BAD=0
fail() { echo "  FAIL $*"; BAD=$((BAD + 1)); }
ok()   { echo "  ok   $*"; }

for f in "$CCZ" "$KERNEL" "$DRV/notty" "$DRV/lrtty" "$DRV/hrtty" "$IMG" \
	 "$COHFS" "$UL/hostbuild/emu-run.sh"; do
	[ -e "$f" ] || { echo "deepstack: missing $f -- cannot run" >&2; exit 2; }
done

WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"

# stage <tag> [kernel] [drvdir] -- a copy of the image carrying the probe and
# a kernel with its drivers.
stage() {
	tag=$1; kern=${2:-$KERNEL}; drvd=${3:-$DRV}
	cp -f "$IMG" "$WORK/$tag.bin"
	put() { "$COHFS" put -p "$ROOTPART" -m 755 "$WORK/$tag.bin" "$1" "$2" \
		>> "$WORK/put.$tag.log" 2>&1; }
	put "$WORK/deepstack" /bin/deepstack &&
	put "$WORK/argmax" /bin/argmax &&
	put "$kern" /coherent || return 1
	for d in notty lrtty hrtty; do
		[ -f "$drvd/$d" ] && { put "$drvd/$d" "/drv/$d" || return 1; }
	done
	return 0
}

# --------------------------------------------------------------- phase 0
# The drivers must be linked against this kernel, and the image must actually
# CARRY this kernel -- a stale image once booted a kernel that no longer
# existed.
echo "phase 0: the image carries the kernel the drivers were linked against"
klink=$(sed -n 's/^linkid=//p' "$(dirname "$KERNEL")/kernel.stamp" 2>/dev/null)
dlink=$(sed -n 's/^kernel_linkid=//p' "$DRV/.drvstamp" 2>/dev/null)
if [ -n "$klink" ] && [ "$klink" = "$dlink" ]; then
	ok "kernel link id $klink, drivers built against $dlink"
else
	fail "kernel link id '$klink' but drivers say '$dlink' -- rebuild drivers"
	echo "deepstack: cannot continue"; exit 1
fi

# --------------------------------------------------------------- phase 1
echo "phase 1: build the probe"
if "$CCZ" -s -i -o "$WORK/deepstack" "$HERE/deepstack.c" \
	> "$WORK/cc.log" 2>&1; then
	ok "deepstack: $(wc -c < "$WORK/deepstack") B"
else
	fail "deepstack did not compile: $(tail -1 "$WORK/cc.log")"
	echo "deepstack: cannot continue"; exit 1
fi
if "$CCZ" -s -i -o "$WORK/argmax" "$HERE/argmax.c" >> "$WORK/cc.log" 2>&1; then
	ok "argmax: $(wc -c < "$WORK/argmax") B"
else
	fail "argmax did not compile: $(tail -1 "$WORK/cc.log")"
	echo "deepstack: cannot continue"; exit 1
fi
if ! stage new; then
	fail "cannot stage the image, see $WORK/put.new.log"
	echo "deepstack: cannot continue"; exit 1
fi
if "$COHFS" cat -p "$ROOTPART" "$WORK/new.bin" /coherent \
	> "$WORK/img.kernel" 2> "$WORK/cat.err" &&
	cmp -s "$KERNEL" "$WORK/img.kernel"; then
	ok "/coherent in the image is this kernel"
else
	fail "/coherent in the image is not $KERNEL"
	echo "deepstack: cannot continue"; exit 1
fi

# run <tag> -- boot it once and run the probe at every frame size.  One boot
# for all four: each invocation is its own process, and the record file is
# rewritten each time.
run() {
	tag=$1
	: > "$WORK/$tag.cmds"
	for s in $SIZES; do
		echo "/bin/deepstack $s" >> "$WORK/$tag.cmds"
	done
	for s in $BIGSIZES; do
		echo "/bin/deepstack $s $BIG" >> "$WORK/$tag.cmds"
	done
	echo "/bin/deepstack 16 $MAXKB" >> "$WORK/$tag.cmds"
	for k in $ARGKB; do
		echo "/bin/argmax $k" >> "$WORK/$tag.cmds"
	done
	rwork=$WORK/work.$tag.bin; rout=$WORK/$tag.out; rerr=$WORK/$tag.err
	rcmds=$WORK/$tag.cmds; rharn=$WORK/$tag.harness
	WORK=$rwork OUT=$rout ERR=$rerr \
		sh "$UL/hostbuild/emu-run.sh" "$rcmds" "$WORK/$tag.bin" \
		> "$rharn" 2>&1
	[ -s "$WORK/$tag.out" ]
}

# verdicts <tag> -- one line per frame size, in the order they ran.
verdicts() { grep -a -c -E '^(deepstack|argmax): PASS' "$WORK/$1.out"; }

# report <tag> -- the numbers, whatever they were.  A run that fails is only
# useful if it says how far it got.
report() {
	grep -a -E 'recursing in a child|bytes of records|level [0-9]+ at|argmax:|FAIL|PASS|CAPPED' \
		"$WORK/$1.out" | sed 's/^/       | /'
}

# --------------------------------------------------------------- phase 2
echo "phase 2: every frame size gets the allowance ($ALLOW bytes, or $BIG KB when set),"
echo "         and exec takes arguments up to the limit less 4 KB"
n=$(set -- $SIZES $BIGSIZES $MAXKB $ARGKB; echo $#)
if run new; then
	p=$(verdicts new)
	if [ "$p" = "$n" ]; then
		ok "$p of $n frame sizes reached the allowance and faulted cleanly"
		report new
	else
		fail "only $p of $n frame sizes passed"
		report new
	fi
else
	fail "the run produced no transcript"
fi

# --------------------------------------------------------------- phase 3
if [ -n "${CONTROL:-}" ]; then
	echo "phase 3: the same checks FAIL against a kernel that does not"
	if [ ! -f "$CONTROL" ]; then
		fail "CONTROL=$CONTROL is not a file"
	elif ! stage ctl "$CONTROL" "${CONTROLDRV:-$DRV}"; then
		fail "cannot stage the control image, see $WORK/put.ctl.log"
	elif run ctl; then
		p=$(verdicts ctl)
		if [ "$p" -lt "$n" ]; then
			ok "$p of $n passed on the control -- the gate discriminates"
			report ctl
		else
			fail "all $n passed on the control kernel too: this gate"
			fail "  cannot fail, so phase 2 decided nothing"
			report ctl
		fi
	else
		fail "the control run produced no transcript"
	fi
fi

echo
if [ "$BAD" = 0 ]; then
	echo "deepstack: PASS -- a process gets the whole $ALLOW-byte stack"
	echo "           allowance whatever size its frames are, and dies of"
	echo "           SIGSEGV, alone, when it runs out  ($WORK)"
	exit 0
fi
echo "deepstack: FAIL ($BAD)  ($WORK)"
exit 1
