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
# comment.  Same for the host mutation gate next door (tests/hostcheck): that
# one decides the probe's arithmetic is right, not the kernel's behaviour.
#
# WHY FOUR SIZES.  The kernel has been wrong about the stack twice, and each
# time some frame sizes worked.  The MMU's write-warning region is the stack
# segment's lowest 256-byte page, so a demand-grown stack behaves completely
# differently either side of that: with 16-byte frames it ran 1087 levels
# while 512-byte frames stopped at 8.  16, 256, 512 and 4096 straddle it.
#
# The image under test carries the kernel in hostbuild/kobj -- run
# `make -C ../../hostbuild dist DIST=$DIST' first, and note that `ld -k' bakes
# absolute kernel addresses into /drv/*, so the drivers must have been rebuilt
# against that same kernel.  Phase 0 checks the two stamps agree and the image
# really carries it, rather than trusting that someone remembered.
#
#	sh run.sh			the gate
#	CONTROL=<kernel.out> sh run.sh	also prove the gate can fail
#	KEEP=1 sh run.sh		leave the work directory
#
# CONTROL names a kernel that does NOT hand out the allowance; the gate must
# fail against it, or it is measuring nothing.  To build one, revert the fix
# and relink:
#	git stash push ../../sys/z8001/src/exec.c ../../sys/z8001/src/trap.c
#	make -C ../../hostbuild drivers	   # relinks the kernel, rebuilds /drv
#	cp ../../hostbuild/kobj/kernel.out /tmp/unfixed.out
#	cp -r ../../hostbuild/build/drv /tmp/unfixed.drv
#	git stash pop; make -C ../../hostbuild drivers
#	CONTROL=/tmp/unfixed.out CONTROLDRV=/tmp/unfixed.drv sh run.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
HB=$(cd "$HERE/../../hostbuild" && pwd)
OS=$(cd "$HERE/../.." && pwd)
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
CCZ="$TC/ccz"
DIST=${DIST:-extended-both}
IMG=$HB/build/$DIST.bin
ROOTPART=${ROOTPART:-136}
INJECT=$OS/tests/rawalign/inject.py
C900_ROOT=$(cd "$OS/.." && pwd)
. "$C900_ROOT/mk/emulator.sh"
emu_need "boot the target and run this gate"

# The allowance and the sizes.  ALLOW must agree with MADSIZE in
# include/sys/machz8001.h and with ALLOWANCE in deepstack.c; it is repeated
# here only to be printed, since the probe is what asserts on it.
ALLOW=32768
SIZES="16 256 512 4096"

BAD=0
fail() { echo "  FAIL $*"; BAD=$((BAD + 1)); }
ok()   { echo "  ok   $*"; }

for f in "$IMG" "$CCZ" "$HB/kobj/kernel.out" "$INJECT"; do
	[ -e "$f" ] || { echo "deepstack: missing $f -- cannot run" >&2; exit 2; }
done

WORK=${WORK:-$HERE/work}
rm -rf "$WORK"
mkdir -p "$WORK"
[ "${KEEP:-0}" = 1 ] || trap 'rm -rf "$WORK"' 0 1 2 15

# --------------------------------------------------------------- phase 0
# Three separate things have to agree, and each has faked a result here
# before: the drivers must be linked against this kernel, and the image must
# actually CARRY this kernel -- a stale image once booted a kernel that no
# longer existed.  dist.py patches a few words of the staged kernel (rootdev,
# the wd table), so the two differ in a handful of bytes by design; anything
# more is a different kernel.
echo "phase 0: the image carries the kernel the drivers were linked against"
klink=$(sed -n 's/^linkid=//p' "$HB/kobj/kernel.stamp" 2>/dev/null)
dlink=$(sed -n 's/^kernel_linkid=//p' "$HB/build/drv/.drvstamp" 2>/dev/null)
if [ -n "$klink" ] && [ "$klink" = "$dlink" ]; then
	ok "kernel link id $klink, drivers built against $dlink"
else
	fail "kernel link id '$klink' but drivers say '$dlink' -- rebuild drivers"
	echo "deepstack: cannot continue"; exit 1
fi
if python3 "$HB/fsread.py" "$IMG" cat /coherent --part "$ROOTPART" \
	> "$WORK/img.kernel" 2>"$WORK/fsread.err" && [ -s "$WORK/img.kernel" ]; then
	d=$(cmp -l "$HB/kobj/kernel.out" "$WORK/img.kernel" 2>/dev/null | wc -l)
	if [ "$d" -le 16 ]; then
		ok "/coherent in $DIST.bin is this kernel ($d patched bytes)"
	else
		fail "/coherent in $DIST.bin differs in $d bytes -- it is a"
		fail "  different kernel; repack with 'make dist DIST=$DIST'"
		echo "deepstack: cannot continue"; exit 1
	fi
else
	fail "could not read /coherent out of $DIST.bin (part $ROOTPART)"
	sed 's/^/       | /' "$WORK/fsread.err" >&2
	echo "deepstack: cannot continue"; exit 1
fi

# --------------------------------------------------------------- phase 1
echo "phase 1: build the probe"
if "$CCZ" -s -i -I "$OS/include" -I "$OS/include/sys" \
	-o "$WORK/deepstack" "$HERE/deepstack.c" > "$WORK/cc.log" 2>&1; then
	ok "deepstack: $(wc -c < "$WORK/deepstack") B"
else
	fail "deepstack did not compile: $(tail -1 "$WORK/cc.log")"
	echo "deepstack: cannot continue"; exit 1
fi

# stage <tag> [kernel] [drvdir] -- a copy of the image carrying the probe just
# built, and optionally a different kernel and its drivers.  The freshly built
# probe goes in whatever the image was packed with, so the source in this
# directory is what runs.
stage() {
	tag=$1; kern=${2:-}; drvd=${3:-}
	cp --reflink=auto -f "$IMG" "$WORK/$tag.bin" 2>/dev/null || \
		cp -f "$IMG" "$WORK/$tag.bin"
	set -- "$WORK/deepstack" /bin/deepstack
	[ -n "$kern" ] && set -- "$@" "$kern" /coherent
	if [ -n "$drvd" ]; then
		for d in notty lrtty hrtty; do
			[ -f "$drvd/$d" ] && set -- "$@" "$drvd/$d" "/drv/$d"
		done
	fi
	python3 "$INJECT" "$WORK/$tag.bin" "$ROOTPART" "$@" \
		> "$WORK/inj.$tag.log" 2>&1
}

# run <tag> -- boot it once and run the probe at every frame size.  One boot
# for all four: each invocation is its own process, and the record file is
# rewritten each time.
run() {
	tag=$1
	: > "$WORK/$tag.cmds"
	for s in $SIZES; do
		echo "/bin/deepstack $s" >> "$WORK/$tag.cmds"
	done
	cp -f "$WORK/$tag.bin" "$HB/build/deepstack-$tag.bin"
	rwork=$WORK/work.$tag.bin; rout=$WORK/$tag.out; rerr=$WORK/$tag.err
	rcmds=$WORK/$tag.cmds; rharn=$WORK/$tag.harness
	WORK=$rwork OUT=$rout ERR=$rerr EMUWAIT=${EMUWAIT:-600} \
		sh "$HB/emu-run.sh" "$rcmds" "deepstack-$tag" > "$rharn" 2>&1
	rm -f "$HB/build/deepstack-$tag.bin"
	[ -s "$WORK/$tag.out" ]
}

# verdicts <tag> -- one line per frame size, in the order they ran.
verdicts() { grep -c 'deepstack: PASS' "$WORK/$1.out"; }

# report <tag> -- the numbers, whatever they were.  A run that fails is only
# useful if it says how far it got.
report() {
	grep -E 'recursing in a child|bytes of records|level [0-9]+ at|FAIL|PASS|CAPPED' \
		"$WORK/$1.out" | sed 's/^/       | /'
}

# --------------------------------------------------------------- phase 2
echo "phase 2: every frame size gets the allowance ($ALLOW bytes)"
n=$(set -- $SIZES; echo $#)
stage new
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
	else
		stage ctl "$CONTROL" "${CONTROLDRV:-}"
		if run ctl; then
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
fi

echo
if [ "$BAD" = 0 ]; then
	echo "deepstack: PASS -- a process gets the whole $ALLOW-byte stack"
	echo "           allowance whatever size its frames are, and dies of"
	echo "           SIGSEGV, alone, when it runs out"
	exit 0
fi
echo "deepstack: FAIL ($BAD)"
exit 1
