#!/bin/sh
# tests/shlib/libmgr1.sh -- does an MGR client on libmgr.1 and libc.1 run,
# and what does it cost?
#
# With TERM=mgr a client writes its protocol to the console, so static and
# shared builds are compared byte for byte in the transcript.  RAM phases hold
# clients while mem(1) reports free core: A is copies of one program, B is N
# different programs for the break-even curve.
#
#	sh libmgr1.sh				on hostbuild/kobj
#	KERNEL=<kernel.out> DRV=<dir> sh libmgr1.sh
#
# Needs the userland's mgr clients built static and CLMODE=shared.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/../.." && pwd)
UL=${C900_USERLAND:-$(cd "$OS/../.." && pwd)/commodore-900-coh-userland}
TCD=${C900_TOOLCHAIN:-$(cd "$OS/../.." && pwd)/commodore-900-toolchain}
B=${C900_TC_BUILD:-$TCD/host/build}
CCZ=$TCD/host/ccz
KERNEL=${KERNEL:-$OS/hostbuild/kobj/kernel.out}
DRV=${DRV:-$OS/hostbuild/build/drv}
IMG=${IMG:-$UL/hostbuild/build/test.bin}
COHFS=$(sh "$UL/mk/deps.sh" tools)/bin/cohfs
ROOTPART=${ROOTPART:-136}
LIBC1=$B/libc1/libc.1
LIBMGR1=$UL/mgr/build/lib/libmgr.1
CLST=$UL/mgr/build/bin/cl
CLSL=$UL/mgr/build/bin-sl/cl
NCOPY=${NCOPY:-8}			# phase A: copies of ONE program
NSET=${NSET:-"2 4 6 8 10"}		# phase B: how many DIFFERENT programs
SHOW="resetwin mgrmode"			# write-and-exit: the protocol comparison
MGRMODEARG="ABS WOB STANDOUT"		# mgrmode prints usage and exits with none
MGRDIR=$UL/mgr
INCS="-I$MGRDIR/include/c900 -I$MGRDIR/include -I$MGRDIR/include/mgr \
	-I$MGRDIR/src/libbitblit/hr -I$MGRDIR/build/gen"
DEFS="-DCOHERENT -DC900"

for f in "$CCZ" "$LIBC1" "$LIBMGR1" "$KERNEL" "$DRV/notty" "$IMG" "$COHFS" \
	 "$UL/hostbuild/emu-run.sh" "$CLST/resetwin" "$CLSL/resetwin" \
	 "$MGRDIR/build/lib/libmgrcl.a" "$MGRDIR/build/lib/libmgrcl-sl.a"; do
	[ -e "$f" ] || { echo "libmgr1: missing $f -- cannot run" >&2; exit 2; }
done

WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
LOG=$WORK/build.log
: > "$LOG"
die() { echo "libmgr1: $1, see $LOG" >&2; exit 2; }
fail=0
say() { echo "  $1 $2"; [ "$1" = PASS ] || fail=1; }

"$CCZ" -i -s -I "$UL/base/cmd" -o "$WORK/mem" "$UL/base/cmd/mem.c" >>"$LOG" 2>&1 ||
	die "mem(1) did not build"

# The shared build takes the -VPIC non-libmgr objects from the archive, then
# both libraries.  It stays unstripped: exec binds from its LI_ records.
( cd "$MGRDIR" && "$CCZ" -i -s -L $INCS $DEFS -o "$WORK/mgrhold.st" \
	"$HERE/mgrhold.c" build/lib/libmgrcl.a build/lib/libmgrcl.a ) >>"$LOG" 2>&1 ||
	die "mgrhold did not link statically"
( cd "$MGRDIR" && C900_LIBPATH="$MGRDIR/build/lib" "$CCZ" -i -L $INCS $DEFS \
	-o "$WORK/mgrhold.sl" "$HERE/mgrhold.c" build/lib/libmgrcl-sl.a \
	build/lib/libmgrcl-sl.a -lmgr.1 -lc.1 ) >>"$LOG" 2>&1 ||
	die "mgrhold did not link against libmgr.1 and libc.1"

echo "=== the two libraries, and what the kernel will round them to"
python3 - "$LIBC1" "$LIBMGR1" "$WORK/mgrhold.st" "$WORK/mgrhold.sl" <<'PY'
import sys
CLICK = 1024
SHRI, PRVI, BSSI, SHRD, PRVD, BSSD = range(6)
def sizes(p):
    b = open(p, 'rb').read()
    ln = lambda o: ((b[o] | b[o+1] << 8) << 16) | (b[o+2] | b[o+3] << 8)
    return len(b), [ln(8 + 4*i) for i in range(9)]
rnd = lambda n: (n + CLICK - 1) // CLICK * CLICK
for p in sys.argv[1:3]:
    n, z = sizes(p)
    t = z[SHRI] + z[SHRD]
    d = z[PRVI] + z[PRVD] + z[BSSI] + z[BSSD]
    print("  %-12s file %6d  shared %6d -> %6d ONCE   private %5d -> %5d PER CLIENT"
          % (p.split('/')[-1], n, t, rnd(t), d, rnd(d)))
for p in sys.argv[3:]:
    n, z = sizes(p)
    t = z[SHRI] + z[PRVI] + z[BSSI]
    d = z[SHRD] + z[PRVD] + z[BSSD]
    print("  %-12s file %6d  text   %6d -> %6d once   data  %5d -> %5d per process"
          % (p.split('/')[-1], n, t, rnd(t), d, rnd(d)))
PY

# --- the image --------------------------------------------------------------
cp -f "$IMG" "$WORK/libmgr1.bin"
put() { "$COHFS" put -p "$ROOTPART" -m "$3" "$WORK/libmgr1.bin" "$1" "$2" \
	>> "$WORK/put.log" 2>&1 || { echo "libmgr1: cannot put $2" >&2; exit 2; }; }
put "$KERNEL" /coherent 755
for d in notty lrtty hrtty; do put "$DRV/$d" "/drv/$d" 755; done
put "$LIBC1" /lib/libc.1 644
put "$LIBMGR1" /lib/libmgr.1 644
put "$WORK/mem" /bin/mem 755
for c in $SHOW; do
	[ -f "$CLST/$c" ] && [ -f "$CLSL/$c" ] || die "$c is not built both ways"
	# Directory entries are 14 characters, hence short prefixes.
	put "$CLST/$c" "/bin/s.$c" 755
	put "$CLSL/$c" "/bin/h.$c" 755
done
# The same image under N names: N separate text segments.
put "$WORK/mgrhold.st" /bin/s.hold 755
put "$WORK/mgrhold.sl" /bin/h.hold 755
i=1
while [ $i -le 10 ]; do
	put "$WORK/mgrhold.st" "/bin/s.d$i" 755
	put "$WORK/mgrhold.sl" "/bin/h.d$i" 755
	i=$((i + 1))
done
# One typed line per phase, so the clients overlap.
mkhold() {	# mkhold <script> <name>... -- hold them, mem(1), kill, mem(1)
	f=$1; shift
	{
		echo 'pids='
		for c in "$@"; do
			echo "/bin/$c 120 </dev/null >/dev/null 2>&1 &"
			echo 'pids="$pids $!"'
		done
		echo 'sleep 4'
		echo '/bin/mem'
		echo 'kill $pids 2>/dev/null'
		echo 'wait'
		echo '/bin/mem'
	} > "$WORK/$f"
	put "$WORK/$f" "/bin/$f" 755
}
i=1; cst=""; csl=""
while [ $i -le $NCOPY ]; do
	cst="$cst s.hold"; csl="$csl h.hold"; i=$((i + 1))
done
mkhold same.st $cst
mkhold same.sl $csl
for n in $NSET; do
	i=1; dst=""; dsl=""
	while [ $i -le $n ]; do
		dst="$dst s.d$i"; dsl="$dsl h.d$i"; i=$((i + 1))
	done
	mkhold "d$n.st" $dst
	mkhold "d$n.sl" $dsl
done

{
	# Held clients block on /dev/tty, so the guest looks hung until killed.
	echo '#% stop mark'
	echo 'sleep 10'
	echo 'TERM=mgr'
	echo 'export TERM'
	echo '/bin/mem'
	echo 'echo mark proto'
	# The escape stream reaches the console whatever the redirection.
	for c in $SHOW; do
		a=
		[ "$c" = mgrmode ] && a=" $MGRMODEARG"
		for k in s h; do
			echo "/bin/$k.$c$a"
			echo "echo END.$k.$c"
		done
	done
	echo 'echo proto done'
	echo 'sh /bin/same.st'
	echo 'sh /bin/same.sl'
	for n in $NSET; do
		echo "echo mark d$n"
		echo "sh /bin/d$n.st"
		echo "sh /bin/d$n.sl"
	done
	echo 'echo mark over'
} > "$WORK/cmds"

W=$WORK
WORK="$W/run.bin" OUT="$W/out" ERR="$W/err" \
	sh "$UL/hostbuild/emu-run.sh" "$W/cmds" "$W/libmgr1.bin" \
	> "$W/harness" 2>&1
grep -a 'stop channel' "$W/harness"

# --- what the transcript says -----------------------------------------------
have() { grep -a -q -- "$1" "$W/out"; }
probe() {	# probe <n> -- the nth mem(1) free figure, in Kb
	grep -a '^free  *[0-9]*K' "$W/out" |
		sed -n "$1s/^free  *\([0-9]*\)K.*/\1/p"
}
echo ""
echo "=== the libraries loaded"
for l in libc.1 libmgr.1; do
	n=$(grep -a -c "shlib: loaded $l" "$W/out" || true)
	r=$(grep -a -c "shlib: reclaiming" "$W/out" || true)
	# Allow one reload per reclaim, not one per client.
	if [ "$n" -ge 1 ] && [ "$n" -le $((r + 1)) ]; then
		say PASS "$l was loaded $n time(s), against $r reclaims"
	else
		say FAIL "$l was loaded $n times, against $r reclaims"
	fi
done
have 'symbol not exported' && say FAIL "a symbol was refused at exec" \
	|| say PASS "no import was refused at exec"

echo ""
echo "=== the protocol: the same client, both ways, byte for byte"
for c in $SHOW; do
	a=""
	[ "$c" = mgrmode ] && a=" $MGRMODEARG"
	r=$(python3 "$HERE/mgrproto.py" "$W/out" "$c" "$a")
	case "$r" in
	same*)	n=${r#same }
		if [ "$n" -lt 8 ]; then
			say FAIL "$c wrote only $n bytes of protocol"
		else
			say PASS "$c: $n bytes of escape stream, identical shared and static"
		fi;;
	differ*) say FAIL "$c: the shared client wrote a different stream ($r)";;
	*)	say FAIL "$c: no protocol in the transcript";;
	esac
done

echo ""
echo "=== RAM, free core in Kb (each phase against the baseline after it)"
b0=$(probe 1)
echo "  baseline after boot            $b0"
i=2
same_st=$(probe $i); i=$((i+1)); b_st=$(probe $i); i=$((i+1))
same_sl=$(probe $i); i=$((i+1)); b_sl=$(probe $i); i=$((i+1))
printf "  A %2d copies of ONE client      static %4s -> %4s  cost %4s\n" \
	"$NCOPY" "$b_st" "$same_st" "$((b_st - same_st))"
printf "                                 shared %4s -> %4s  cost %4s\n" \
	"$b_sl" "$same_sl" "$((b_sl - same_sl))"
best=""; worst=""
# The libraries' shared segments, paid once; break-even is where the
# per-client saving covers them.
once=$(( (48128 + 15360 + 1023) / 1024 ))
for n in $NSET; do
	d_st=$(probe $i); i=$((i+1)); r_st=$(probe $i); i=$((i+1))
	d_sl=$(probe $i); i=$((i+1)); r_sl=$(probe $i); i=$((i+1))
	c_st=$((r_st - d_st)); c_sl=$((r_sl - d_sl))
	per=$(( (c_st - c_sl) / n ))
	printf "  B %2d different clients         static %4s -> %4s  cost %4s\n" \
		"$n" "$r_st" "$d_st" "$c_st"
	printf "                                 shared %4s -> %4s  cost %4s   %+d" \
		"$r_sl" "$d_sl" "$c_sl" "$((c_sl - c_st))"
	if [ "$per" -gt 0 ]; then
		be=$(( (once + per - 1) / per ))
		printf "   %dK/client, break-even %d\n" "$per" "$be"
		[ -z "$best" ] && best=$be
		[ "$be" -lt "$best" ] && best=$be
		[ -z "$worst" ] && worst=$be
		[ "$be" -gt "$worst" ] && worst=$be
	else
		echo ""
	fi
done

echo ""
echo "  the two shared segments cost ${once}K once"
if [ -n "$best" ]; then
	say PASS "break-even between $best and $worst distinct clients"
else
	say FAIL "shared never cost less RAM over $NSET clients"
fi
have 'mark over' && say PASS "the system came back from every phase" \
	|| say FAIL "the run did not reach the end"

echo ""
[ "$fail" = 0 ] && echo "libmgr1: PASS  ($WORK)" || echo "libmgr1: FAIL  ($WORK)"
exit $fail
