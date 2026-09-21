#!/bin/sh
# tests/shlib/run.sh -- shared-library tests on a booted system.
#
#	sh run.sh				on hostbuild/kobj
#	KERNEL=<kernel.out> DRV=<dir> sh run.sh	another kernel
#
# Needs a toolchain checkout (slgen, cc passes) matching the kernel's.
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
VAR=${VAR:-800000000800}
# As VAR plus VPIC (bit 43), so extern data go through far-pointer slots,
# and VREADONLY (bit 25).
VARP=${VARP:-800000020808}
CCZVARP=${CCZVARP:-800000020808}
PEEP=${PEEP:-0010}
for f in "$CCZ" "$B/slgen" "$B/ld-z8001" "$B/as-z8001" "$B/z8001/cc0-z8001" \
	 "$B/libc-z8001/crt0.o" "$B/libc-z8001/libc-z8001.a" \
	 "$KERNEL" "$DRV/notty" "$DRV/lrtty" "$DRV/hrtty" "$IMG" \
	 "$COHFS" "$UL/hostbuild/emu-run.sh"; do
	[ -e "$f" ] || { echo "shlib: missing $f -- cannot run" >&2; exit 2; }
done

WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
LOG=$WORK/build.log
: > "$LOG"
die() { echo "shlib: $1, see $LOG" >&2; exit 2; }

# --- the library ------------------------------------------------------------
# `SS' is the absolute stack references are emitted against.
cclib() {	# cclib <name> <source> [<variant>] -- one library object
	v=${3:-$VAR}
	"$B/z8001/cc0-z8001" $v "$2" "$WORK/$1.z0" >>"$LOG" 2>&1 &&
	"$B/z8001/cc1-z8001" $v "$WORK/$1.z0" "$WORK/$1.z1" >>"$LOG" 2>&1 &&
	"$B/z8001/cc2-z8001" $PEEP "$WORK/$1.z1" "$WORK/$1.o" "$WORK/$1.scr" 0 \
		>>"$LOG" 2>&1
}
mklib() {	# mklib <name> <exports> -- slgen one library from <name>.o
	"$B/slgen" -A "$B/as-z8001" -L "$B/ld-z8001" -T "$WORK" -e "$2" \
		-o "$WORK/link/lib$1.1" "$WORK/$1.o" "$WORK/ss.o" \
		>>"$LOG" 2>&1
}
printf '\t.globl\tSS\nSS = 0\n' > "$WORK/ss.s"
"$B/as-z8001" -o "$WORK/ss.o" "$WORK/ss.s" >>"$LOG" 2>&1 || die "SS"
mkdir -p "$WORK/link"
cclib toy "$HERE/toy.c" || die "the library did not compile"
mklib toy "$HERE/toy.exp" || die "slgen refused the library"
cclib toy2 "$HERE/toy2.c" || die "the second library did not compile"
mklib toy2 "$HERE/toy2.exp" || die "slgen refused the second library"
# Eight one-function libraries, one over each limit of seven.
i=1
while [ $i -le 8 ]; do
	printf 'int\ngen%s_add(a, b)\nint a, b;\n{\n\treturn (a + b + %s);\n}\n' \
		$i $i > "$WORK/gen$i.c"
	printf 'gen%s_add_\n' $i > "$WORK/gen$i.exp"
	cclib "gen$i" "$WORK/gen$i.c" || die "gen$i did not compile"
	mklib "gen$i" "$WORK/gen$i.exp" || die "slgen refused gen$i"
	i=$((i + 1))
done
# For the core-dump case: a marker in each half that must not reach a core.
cat > "$WORK/mark.c" <<'EOF'
char	mark_prv[] = "COREMARKPRV";

char *
mark_shr()
{
	return ("COREMARKSHR");
}

int
mark_add(a, b)
int a, b;
{
	return (a + b);
}
EOF
printf 'mark_add_\nmark_shr_\n' > "$WORK/mark.exp"
cclib mark "$WORK/mark.c" || die "the marker library did not compile"
mklib mark "$WORK/mark.exp" || die "slgen refused the marker library"
# Clients link against toy; the system gets a copy lacking toy_bump_.
cp "$WORK/link/libtoy.1" "$WORK/link/libskew.1"
"$B/slgen" -A "$B/as-z8001" -L "$B/ld-z8001" -T "$WORK" -e "$HERE/toycut.exp" \
	-o "$WORK/cut-libskew.1" "$WORK/toy.o" "$WORK/ss.o" >>"$LOG" 2>&1 ||
	die "slgen refused the cut library"
cp "$WORK/link/libtoy.1" "$WORK/link/libgone.1"
# Only LIBPATH finds this one.
cp "$WORK/link/libtoy.1" "$WORK/link/libpath.1"

# --- the DATA library -------------------------------------------------------
cclib toyd  "$HERE/toyd.c"  "$VARP" || die "the data library did not compile"
cclib toydf "$HERE/toydf.c" "$VARP" || die "the data accessors did not compile"
"$B/slgen" -A "$B/as-z8001" -L "$B/ld-z8001" -T "$WORK" -e "$HERE/toyd.exp" \
	-o "$WORK/link/libtoyd.1" "$WORK/toyd.o" "$WORK/toydf.o" "$WORK/ss.o" \
	>>"$LOG" 2>&1 || die "slgen refused the data library"
# Cut to drop the struct export, and a copy only LIBPATH finds.
cp "$WORK/link/libtoyd.1" "$WORK/link/libdpath.1"
cp "$WORK/link/libtoyd.1" "$WORK/link/libdskew.1"
"$B/slgen" -A "$B/as-z8001" -L "$B/ld-z8001" -T "$WORK" -e "$HERE/toydcut.exp" \
	-o "$WORK/cut-libdskew.1" "$WORK/toyd.o" "$WORK/toydf.o" "$WORK/ss.o" \
	>>"$LOG" 2>&1 || die "slgen refused the cut data library"

# --- a library ON a library -------------------------------------------------
# -S lets a library appear in a -r link.  The dependency goes last, where ld
# treats it as an archive.
cclib toyup "$HERE/toyup.c" "$VARP" || die "the importing library did not compile"
"$B/slgen" -A "$B/as-z8001" -L "$B/ld-z8001" -T "$WORK" -e "$HERE/toyup.exp" \
	-o "$WORK/link/libup.1" "$WORK/toyup.o" "$WORK/ss.o" \
	"$WORK/link/libtoyd.1" >>"$LOG" 2>&1 ||
	die "slgen refused the importing library"

# --- the clients ------------------------------------------------------------
# Linked at segment 3, where exec loads them, so slot addresses stand.
link() {	# link <out> <src> <library>
	"$CCZ" -c -o "$WORK/$1.o" "$HERE/$2" >>"$LOG" 2>&1 || return 1
	"$B/ld-z8001" -n -i -o "$WORK/$1" \
		"$B/libc-z8001/crt0.o" "$WORK/$1.o" "$WORK/link/$3" \
		"$B/libc-z8001/libc-z8001.a" >>"$LOG" 2>&1
}
linkn() {	# linkn <out> <src> <library>... -- link against several
	out=$1; src=$2; shift 2
	libs=
	for l in "$@"; do libs="$libs $WORK/link/$l"; done
	"$CCZ" -c -o "$WORK/$out.o" "$src" >>"$LOG" 2>&1 || return 1
	"$B/ld-z8001" -n -i -o "$WORK/$out" \
		"$B/libc-z8001/crt0.o" "$WORK/$out.o" $libs \
		"$B/libc-z8001/libc-z8001.a" >>"$LOG" 2>&1
}
link shlibtest shlibtest.c libtoy.1 || die "the client did not link"
link shlibno   shlibno.c   libgone.1 || die "the no-library client"
link shlibbad  shlibbad.c  libskew.1 || die "the skewed client"
link shlibpath shlibtest.c libpath.1 || die "the LIBPATH client"
linkn shlibtwo "$HERE/shlibtwo.c" libtoy.1 libtoy2.1 ||
	die "the two-library client"
"$CCZ" -o "$WORK/hog" "$HERE/hog.c" >>"$LOG" 2>&1 || die "hog"

# shlibmany names eight; shlibhold holds gen2..gen8 while its child execs
# shlibover, which names gen1.
linkn shlibmany "$HERE/shlibmany.c" libgen1.1 libgen2.1 libgen3.1 libgen4.1 \
	libgen5.1 libgen6.1 libgen7.1 libgen8.1 || die "the eight-library client"
linkn shlibhold "$HERE/shlibhold.c" libgen2.1 libgen3.1 libgen4.1 libgen5.1 \
	libgen6.1 libgen7.1 libgen8.1 || die "the holding client"
linkn shlibover "$HERE/shlibover.c" libgen1.1 || die "the overflow client"
cat > "$WORK/shlibcore.c" <<'EOF'
/* A client with a library mapped, killed by a signal that dumps core.  The
 * two markers in libmark.1 must NOT appear in the core file: library slots
 * are kept out of SRFDUMP so a core keeps the shape db(1) expects. */
#include <stdio.h>

extern int	mark_add();

main()
{
	printf("shlibcore[%d]: mark_add 1+2=%d\n", getpid(), mark_add(1, 2));
	fflush(stdout);
	kill(getpid(), 3);		/* SIGQUIT: dumps */
	printf("shlibcore: STILL ALIVE\n");
	fflush(stdout);
	return (0);
}
EOF
linkn shlibcore "$WORK/shlibcore.c" libmark.1 || die "the core-dump client"
# Data clients need -VPIC to link.
CCZ_VAR=$CCZVARP linkn shlibdata "$HERE/shlibdata.c" libtoyd.1 ||
	die "the data client"
CCZ_VAR=$CCZVARP linkn shlibdbad "$HERE/shlibdata.c" libdskew.1 ||
	die "the skewed data client"
CCZ_VAR=$CCZVARP linkn shlibdpath "$HERE/shlibdata.c" libdpath.1 ||
	die "the LIBPATH data client"
# shlibup gets libtoyd.1 only through libup.1; shlibupd names both and must
# see one object.
linkn shlibup "$HERE/shlibup.c" libup.1 || die "the chained client"
CCZ_VAR=$CCZVARP linkn shlibupd "$HERE/shlibupd.c" libup.1 libtoyd.1 ||
	die "the two-path client"

# --- the image --------------------------------------------------------------
cp -f "$IMG" "$WORK/gate.bin"
put() { "$COHFS" put -p "$ROOTPART" -m "$3" "$WORK/gate.bin" "$1" "$2" \
	>> "$WORK/put.log" 2>&1 ||
	{ echo "shlib: cannot put $2" >&2; exit 2; }; }
put "$KERNEL" /coherent 755
for d in notty lrtty hrtty; do put "$DRV/$d" "/drv/$d" 755; done
put "$WORK/shlibtest" /bin/shlibtest 755
put "$WORK/shlibno"   /bin/shlibno   755
put "$WORK/shlibbad"  /bin/shlibbad  755
put "$WORK/link/libtoy.1" /lib/libtoy.1 644
put "$WORK/cut-libskew.1" /lib/libskew.1 644
# A library exec'd as a program: refused.
put "$WORK/link/libtoy.1" /bin/libprog 755
# LIBPATH is ignored for setuid, so install the client plain and setuid.
put "$WORK/link/libpath.1" /usr/libpath.1 644
put "$WORK/shlibpath" /bin/shlibpath 755
put "$WORK/shlibpath" /bin/shlibsuid 4755
# libgone.1 is not installed.
put "$WORK/link/libtoy2.1" /lib/libtoy2.1 644
put "$WORK/link/libtoyd.1" /lib/libtoyd.1 644
put "$WORK/cut-libdskew.1" /lib/libdskew.1 644
put "$WORK/shlibdata" /bin/shlibdata 755
put "$WORK/shlibdbad" /bin/shlibdbad 755
# Only LIBPATH finds libdpath.1; its client goes in plain and setuid.
put "$WORK/link/libdpath.1" /usr/libdpath.1 644
put "$WORK/shlibdpath" /bin/shlibdpath 755
put "$WORK/shlibdpath" /bin/shlibdsuid 4755
put "$WORK/link/libmark.1" /lib/libmark.1 644
put "$WORK/link/libup.1" /lib/libup.1 644
put "$WORK/shlibup"  /bin/shlibup  755
put "$WORK/shlibupd" /bin/shlibupd 755
i=1
while [ $i -le 8 ]; do
	put "$WORK/link/libgen$i.1" "/lib/libgen$i.1" 644
	i=$((i + 1))
done
for c in shlibtwo hog shlibmany shlibhold shlibover shlibcore; do
	put "$WORK/$c" "/bin/$c" 755
done
# Typed input is paced on guest output, so start the hogs from a script
# or they would never overlap.
cat > "$WORK/hogall" <<'EOF'
i=1
while [ $i -le 24 ]
do
	/bin/hog $i 60 &
	i=`expr $i + 1`
done
wait
EOF
put "$WORK/hogall" /bin/hogall 755

cat > "$WORK/cmds" <<'EOF'
/bin/shlibtest
/bin/shlibtest p 2 &
/bin/shlibtest p 5
/bin/shlibtest f
/bin/shlibno
/bin/shlibbad
/bin/libprog
LIBPATH=/usr
export LIBPATH
/bin/shlibpath
/bin/shlibsuid
echo alive
/bin/shlibtwo
/bin/shlibtest s 100 &
/bin/shlibdata s 100 &
sleep 1
sh /bin/hogall
wait
echo pressure over
echo mark limits
/bin/shlibmany
echo mark hold
/bin/shlibhold
echo held no longer
/bin/shlibover
cd /usr
/bin/shlibcore
cd /
echo still alive
echo mark data
/bin/shlibdata
/bin/shlibdata p 3 &
/bin/shlibdata p 9
/bin/shlibdata f
/bin/shlibdpath
/bin/shlibdsuid
/bin/shlibdata c
/bin/shlibdbad
echo data done
echo mark chain
/bin/shlibup
/bin/shlibup b 2 &
/bin/shlibup b 5
/bin/shlibupd
echo chain done
EOF
W=$WORK
WORK="$W/run.bin" OUT="$W/out" ERR="$W/err" \
	sh "$UL/hostbuild/emu-run.sh" "$W/cmds" "$W/gate.bin" \
	> "$W/harness" 2>&1
grep -a 'stop channel' "$W/harness"

fail=0
say() { echo "  $1 $2"; [ "$1" = PASS ] || fail=1; }
have() { grep -a -q -- "$1" "$W/out"; }
n_loaded=$(grep -a -c 'shlib: loaded libtoy.1' "$W/out" || true)

echo "=== 1 the client runs"
have 'name "toy library v1"' && say PASS "the string constant reads back" \
	|| say FAIL "the library's string constant"
have 'add 2+3=5 40+2=42' && say PASS "calls through the bound slots return" \
	|| say FAIL "a call through a slot"
have 'bump 3' && say PASS "the library's private counter counts" \
	|| say FAIL "the private counter"

echo "=== 2 one shared copy, 3 private copies"
[ "$n_loaded" = 1 ] && say PASS "the library was loaded exactly once" \
	|| say FAIL "the library was loaded $n_loaded times"
# The two `p' clients overlap: each sleeps 4 seconds after reporting.
grep -a 'counter 2 after 2 bumps' "$W/out" > /dev/null &&
grep -a 'counter 5 after 5 bumps' "$W/out" > /dev/null &&
	say PASS "two clients at once, each with its own count" \
	|| say FAIL "the two concurrent counts"
grep -a 'counter 2 still' "$W/out" > /dev/null &&
grep -a 'counter 5 still' "$W/out" > /dev/null &&
	say PASS "neither client saw the other's bumps" \
	|| say FAIL "the counts after the overlap"
# Every libtoy.1 client must see the library's code at the same address.
addrs=$(sed '/loaded libpath.1/,$d' "$W/out" |
	grep -a -o 'shared code at [0-9a-f]*' |
	awk '{print $NF}' | sort -u | wc -l)
[ "$addrs" = 1 ] && say PASS "every client maps the shared segment alike" \
	|| say FAIL "$addrs different addresses for the shared segment"

echo "=== 4 fork"
have 'parent counter 10 before fork' &&
have 'child counter 15' &&
have 'parent counter 10 after fork' &&
	say PASS "the child got a copy of the library's private data" \
	|| say FAIL "fork across the library boundary"

echo "=== 5 LIBPATH, and D10's setuid rule"
have 'shlib: loaded libpath.1' && say PASS "LIBPATH found a library" \
	|| say FAIL "LIBPATH"
have 'shlib: libpath.1: not found' &&
	say PASS "LIBPATH is ignored for a setuid exec" \
	|| say FAIL "the setuid rule"

echo "=== 6 refusals"
have 'shlib: libgone.1: not found' && ! have 'shlibno: should never run' &&
	say PASS "a missing library refuses the exec, named" \
	|| say FAIL "the missing-library refusal"
have 'symbol not exported: toy_bump_' && ! have 'shlibbad: should never run' &&
	say PASS "a missing symbol refuses the exec, named" \
	|| say FAIL "the missing-symbol refusal"
have 'a shared library is not a program' &&
	say PASS "an LF_SLIB image is refused as a program" \
	|| say FAIL "the LF_SLIB refusal"
have 'alive' && say PASS "the shell survived every refusal" \
	|| say FAIL "the shell did not survive"

echo "=== 7 two libraries in one client"
have 'shlibtwo.*add 3+4=7 mul 3\*4=12' &&
	say PASS "both libraries answer their own calls" \
	|| say FAIL "a call into one of two libraries"
have 'shlibtwo.*toy "toy library v1" toy2 "toy2 library v1"' &&
	say PASS "each library's string constant is its own" \
	|| say FAIL "the two string constants"
have 'shlibtwo.*counters 7 9' &&
	say PASS "each library's private counter is its own" \
	|| say FAIL "the two private counters"
! have 'SAME SEGMENT for both libraries' && have 'shlibtwo.*two segments,' &&
	say PASS "the two libraries landed on different pairs" \
	|| say FAIL "both libraries on one pair"
have 'shlib: loaded libtoy2.1' && say PASS "the second library was loaded" \
	|| say FAIL "the second library"

echo "=== 8 swap and krunch with libraries mapped (K7)"
nhog=$(grep -a -c 'hog [0-9]*: ok' "$W/out" || true)
ncorrupt=$(grep -a -c 'hog [0-9]*: CORRUPT' "$W/out" || true)
nbad=$(grep -a -c 'hog [0-9]*: BAD BEFORE\|hog [0-9]*: no memory' "$W/out" || true)
# Count hogs holding memory before the first exits: 22 is 880K on a
# machine with 860K free, so the swapper ran.
first_ok=$(grep -a -n -m1 'hog [0-9]*: ok' "$W/out" | cut -d: -f1)
[ -n "$first_ok" ] || first_ok=0
peak=$(sed -n "1,${first_ok}p" "$W/out" | grep -a -c '40000 bytes at' || true)
[ "$nhog" = 24 ] && [ "$ncorrupt" = 0 ] && [ "$peak" -ge 22 ] &&
	say PASS "$peak hogs held $((peak * 40))K at once on a 860K machine: swap ran" \
	|| say FAIL "$nhog hogs of 24 came back, $ncorrupt corrupted, $nbad bad before the sleep, only $peak held memory at once"
before=$(grep -a -o 'before pressure counter 7 code [0-9a-f]*' "$W/out" |
	awk '{print $NF}')
after=$(grep -a -o 'after pressure counter 7 code [0-9a-f]*' "$W/out" |
	awk '{print $NF}')
[ -n "$before" ] && [ "$before" = "$after" ] &&
	say PASS "the client kept its count and its library address across it" \
	|| say FAIL "the client across the pressure ($before vs $after)"
have 'after pressure .* name "toy library v1"' &&
	say PASS "the library's shared string still reads back" \
	|| say FAIL "the shared string after the pressure"
! have 'the string MOVED' && say PASS "the shared segment did not move" \
	|| say FAIL "the shared segment moved under a client"
have 'pressure over' && say PASS "the system survived the pressure" \
	|| say FAIL "the system did not come back from the pressure"

echo "=== 9 the per-process and system limits"
sed -n '/mark limits/,/mark hold/p' "$W/out" |
	grep -a -q 'too many libraries for one process' &&
	say PASS "an eighth library in one client is refused, named" \
	|| say FAIL "the per-process NSLIB limit"
! have 'shlibmany.*sum' && say PASS "and shlibmany did not run" \
	|| say FAIL "shlibmany ran with eight libraries"
have 'shlibhold.*sum 49' && have 'shlibhold.*still holding 4' &&
	say PASS "seven libraries in one client is allowed, and stay mapped" \
	|| say FAIL "seven libraries in one client"
# While shlibhold holds seven, the table and the pairs are both full.
sed -n '/shlibhold.*sum 49/,/held no longer/p' "$W/out" |
	grep -a -q 'no free library table entry\|no free segment pair' &&
	have 'the eighth exec was refused' &&
	say PASS "an eighth library system-wide is refused, named" \
	|| say FAIL "the system-wide limit"
have 'still alive' && say PASS "the shell survived both refusals" \
	|| say FAIL "the shell did not survive the limits"

echo "=== 10 slreclaim, under pressure and not"
# Unused libraries stay resident, so shlibhold's gen8 forces the reclaim
# of gen1.
sed -n '/mark hold/,/shlibhold.*sum 49/p' "$W/out" |
	grep -a -q 'reclaiming segments' &&
	say PASS "the table filling forced a reclaim, and the load went on" \
	|| say FAIL "no reclaim when the table was full"
n=$(sed -n '/shlibhold.*sum 49/,/held no longer/p' "$W/out" |
	grep -a -c 'reclaiming segments' || true)
[ "$n" = 0 ] &&
	say PASS "no library in use was reclaimed, even with the table full" \
	|| say FAIL "$n libraries were reclaimed while in use"
# Once it exits, the same exec succeeds.
sed -n '/held no longer/,$p' "$W/out" | grep -a -q 'shlibover.*sum 3' &&
	say PASS "the same exec succeeds once the holder has gone" \
	|| say FAIL "the load after the holder exited"

echo "=== 11 a core dump from a client with a library mapped"
have 'shlibcore.*mark_add 1+2=3' && ! have 'shlibcore: STILL ALIVE' &&
	say PASS "the client ran and was killed by the dumping signal" \
	|| say FAIL "the core-dump client"
# The core is in the guest's copy of the image.
if "$COHFS" cat -p "$ROOTPART" "$W/run.bin" /usr/core > "$W/core" 2>/dev/null &&
   [ -s "$W/core" ]; then
	say PASS "a core file was written ($(wc -c < "$W/core") bytes)"
	if grep -a -q 'COREMARKSHR\|COREMARKPRV' "$W/core"; then
		say FAIL "the core file holds a library segment"
	else
		say PASS "neither half of the library is in the core file"
	fi
else
	say FAIL "no core file"
fi

echo "=== 12 data imports: one object, seen from both sides"
D() { sed -n '/mark data/,/data done/p' "$W/out"; }
D | grep -a -q 'init 11 22' &&
	say PASS "the client reads the library's own initializer" \
	|| say FAIL "the initialized data did not reach the client"
! D | grep -a -q 'TWO OBJECTS' &&
	say PASS "the client and the library agree on the object's address" \
	|| say FAIL "the two sides see different objects"
# The client writes, the library adds it up: 3 + 22 + 4 and 9 + 22 + 10.
D | grep -a -q 'library sums 29' && D | grep -a -q 'library sums 41' &&
	say PASS "the library reads what the client wrote" \
	|| say FAIL "the library did not see the client's writes"
# Then toyd_set() writes from inside the library and the client reads it back.
D | grep -a -q 'client reads 6 7' && D | grep -a -q 'client reads 18 19' &&
	say PASS "the client reads what the library wrote" \
	|| say FAIL "the client did not see the library's writes"
# Overlapping clients must keep their own data.  `still 3 6 4' also shows
# the addend was applied.
D | grep -a -q 'still 3 6 4' && D | grep -a -q 'still 9 18 10' &&
	say PASS "two clients at once, each with its own copy of the data" \
	|| say FAIL "the two concurrent copies"
D | grep -a -q 'symbol not exported: toyd_box_' &&
	! D | grep -a -q 'shlibdbad.*init' &&
	say PASS "a data export the library has lost refuses the exec, named" \
	|| say FAIL "the missing-data-export refusal"
D | grep -a -q 'data done' && say PASS "the shell survived that refusal too" \
	|| say FAIL "the shell did not survive the data refusal"

echo "=== 13 a readonly table, bound to the SHARED segment (SE_SHRD)"
# The table's slot must carry the shared segment, not the private one.
D | grep -a -q 'tab 2 7 19 sums 77 77' &&
	say PASS "the table reads back whole, from both sides" \
	|| say FAIL "the shared table did not read back"
! D | grep -a -q 'TWO TABLES' &&
	say PASS "the client and the library agree on the table's address" \
	|| say FAIL "the two sides see different tables"
! D | grep -a -q 'ONE SEGMENT' &&
	say PASS "it is one segment BELOW the private objects: the shared half" \
	|| say FAIL "the shared table was bound to the private segment"

echo "=== 14 fork with data imports bound"
# 30 + 22 + 1 = 53 in the parent; the child writes 70 (93); the parent is
# unchanged.
D | grep -a -q 'parent sums 53 before fork' &&
D | grep -a -q 'child sums 53 at' &&
D | grep -a -q 'child sums 93 after its write' &&
D | grep -a -q 'parent sums 53 after fork' &&
	say PASS "the child reached its own copy through the slots it inherited" \
	|| say FAIL "fork with data imports bound"

echo "=== 15 a bound data slot across the pressure of case 8"
dbefore=$(grep -a -o 'before pressure sums 76 tab 77 at [0-9a-f]*' "$W/out" |
	awk '{print $NF}')
dafter=$(grep -a -o 'after pressure sums 76 tab 77 at [0-9a-f]*' "$W/out" |
	awk '{print $NF}')
[ -n "$dbefore" ] && [ "$dbefore" = "$dafter" ] &&
	say PASS "the private copy and the shared table survived swap and krunch" \
	|| say FAIL "the data client across the pressure ($dbefore vs $dafter)"

echo "=== 16 a core dump from a client with data imports bound"
D | grep -a -q 'dumping, sums 82' && ! D | grep -a -q 'shlibdata.*STILL ALIVE' &&
	say PASS "the data client ran and was killed by the dumping signal" \
	|| say FAIL "the data core-dump client"
if "$COHFS" cat -p "$ROOTPART" "$W/run.bin" /core > "$W/dcore" 2>/dev/null &&
   [ -s "$W/dcore" ]; then
	say PASS "a core file was written ($(wc -c < "$W/dcore") bytes)"
	# Primes are the shared table; 60 and 22 the private copy.
	if python3 - "$W/dcore" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
sys.exit(0 if bytes([2,3,5,7,11,13,17,19]) in d else 1)
PY
	then
		say FAIL "the core file holds the library's shared table"
	else
		say PASS "neither the shared table nor the private copy is in it"
	fi
else
	say FAIL "no core file from the data client"
fi

echo "=== 17 LIBPATH and the setuid rule, for a library of objects"
have 'shlib: loaded libdpath.1' &&
	say PASS "LIBPATH found the data library" \
	|| say FAIL "LIBPATH with a data library"
have 'shlib: libdpath.1: not found' &&
	say PASS "and is ignored for a setuid exec of its client" \
	|| say FAIL "the setuid rule with a data library"

echo "=== 18 a library that imports from another library (design 3.3)"
C() { sed -n '/mark chain/,/chain done/p' "$W/out"; }
C | grep -a -q 'up_sum 133 tab3 7' &&
	say PASS "the importing library's call, data and shared table all bound" \
	|| say FAIL "the chained library's imports"
n_chain=$(grep -a -c 'shlib: loaded libup.1' "$W/out" || true)
[ "$n_chain" = 1 ] &&
	say PASS "the importing library itself was loaded once" \
	|| say FAIL "libup.1 was loaded $n_chain times"
C | grep -a -q 'shlibup.*bump 3' && C | grep -a -q 'shlibup.*bump 6' &&
	say PASS "the importing library keeps its own private data per client" \
	|| say FAIL "the chained library's private counter"
up_w=$(C | sed -n 's/.*shlibupd.*where \([0-9a-f]*\) \([0-9a-f]*\).*/\1 \2/p')
[ -n "$up_w" ] && [ "${up_w% *}" = "${up_w#* }" ] &&
	say PASS "the library and the client reach ONE copy of the object ($up_w)" \
	|| say FAIL "two addresses for the dependency's object: $up_w"
C | grep -a -q 'after set b 7 up_sum 118' &&
	say PASS "a write by the client is read by the library through its slot" \
	|| say FAIL "the client's write was not seen through the library"

echo "=== the RAM cost of section 3.7, measured on libtoy.1"
python3 - "$W/link/libtoy.1" <<'PY' || true
import sys
b = open(sys.argv[1], 'rb').read()
canl = lambda o: ((b[o] | b[o+1] << 8) << 16) | (b[o+2] | b[o+3] << 8)
n = "SHRI PRVI BSSI SHRD PRVD BSSD".split()
z = [canl(8 + 4*i) for i in range(6)]
print("  shared, once for the system: SHRI %d + SHRD %d = %d bytes"
      % (z[0], z[3], z[0] + z[3]))
print("  private, once per client:    PRVI %d + PRVD %d + bss %d = %d bytes"
      % (z[1], z[4], z[2] + z[5], z[1] + z[4] + z[2] + z[5]))
PY

[ "$fail" = 0 ] && { echo "gate: PASS  ($W)"; exit 0; }
echo "gate: FAIL  ($W)"; exit 1
