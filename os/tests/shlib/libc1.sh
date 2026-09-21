#!/bin/sh
# tests/shlib/libc1.sh -- RAM cost and saving of the shared C library.
#
# Each phase holds clients resident while mem(1) reports free memory:
#
#   A  2*NCLIENT processes of two programs; only library data multiplies
#   B  NDIST different programs, a text segment each
#   C  two shells and two pipelines of six utilities
#
# Each phase is compared with the reading after its clients are reaped.  The
# first reading of a boot is discarded: the boot has not settled.
#
#	sh libc1.sh				on hostbuild/kobj
#	KERNEL=<kernel.out> DRV=<dir> sh libc1.sh
#
# Needs a toolchain checkout with `make libc1' done, and the test image.
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
NCLIENT=${NCLIENT:-6}			# of each kind, so 2*NCLIENT processes
NDIST=${NDIST:-10}			# different programs alive at once, phase B
CHOLD=${CHOLD:-25}			# seconds the session's pipelines block, phase C
# Built here both ways, so the two columns differ only in the library.
CCMDS="sleep sort wc cat uniq head"
for f in "$CCZ" "$B/libc1/libc.1" "$B/libc1/crt0sl.o" "$KERNEL" "$DRV/notty" \
	 "$IMG" "$COHFS" "$UL/hostbuild/emu-run.sh"; do
	[ -e "$f" ] || { echo "libc1: missing $f -- cannot run" >&2; exit 2; }
done

WORK=${WORK:-$(mktemp -d)}
mkdir -p "$WORK"
LOG=$WORK/build.log
: > "$LOG"
die() { echo "libc1: $1, see $LOG" >&2; exit 2; }
fail=0
say() { echo "  $1 $2"; [ "$1" = PASS ] || fail=1; }

# --- the clients ------------------------------------------------------------
# Shared clients stay unstripped: exec binds from their LI_ records.
for c in slsmall slmid; do
	"$CCZ" -i -s -o "$WORK/$c.st" "$HERE/$c.c" >>"$LOG" 2>&1 ||
		die "$c did not link statically"
	"$CCZ" -i -slibc -o "$WORK/$c.sl" "$HERE/$c.c" >>"$LOG" 2>&1 ||
		die "$c did not link against libc.1"
done
# Installed under C-prefixed names so nothing the image ships is replaced.
for c in $CCMDS; do
	D=""
	[ "$c" = sort ] && D="-D COHERENT=1"
	"$CCZ" -i -s $D -I "$UL/base/cmd" -o "$WORK/C$c.st" "$UL/base/cmd/$c.c" \
		>>"$LOG" 2>&1 || die "$c did not link statically"
	"$CCZ" -i -slibc $D -I "$UL/base/cmd" -o "$WORK/C$c.sl" "$UL/base/cmd/$c.c" \
		>>"$LOG" 2>&1 || die "$c did not link against libc.1"
done

# mem(1) needs the kernel namelist in /coherent and /dev/kmem.
"$CCZ" -i -s -I "$UL/base/cmd" -o "$WORK/mem" "$UL/base/cmd/mem.c" >>"$LOG" 2>&1 ||
	die "mem(1) did not build"

# Segments round up to a click, so print raw and rounded sizes.
echo "=== the images, and what the kernel will round them to"
python3 - "$B/libc1/libc.1" "$WORK/slsmall.st" "$WORK/slsmall.sl" \
	"$WORK/slmid.st" "$WORK/slmid.sl" <<'PY'
import sys
CLICK = 1024
SHRI, PRVI, BSSI, SHRD, PRVD, BSSD = range(6)
def sizes(p):
    b = open(p, 'rb').read()
    ln = lambda o: ((b[o] | b[o+1] << 8) << 16) | (b[o+2] | b[o+3] << 8)
    return len(b), [ln(8 + 4*i) for i in range(9)]
def prog(p):
    # A PROGRAM linked -n -i: text is SHRI+PRVI+BSSI, data SHRD+PRVD+BSSD.
    n, z = sizes(p)
    return n, z[SHRI] + z[PRVI] + z[BSSI], z[SHRD] + z[PRVD] + z[BSSD]
def lib_(p):
    # A LIBRARY: shared SHRI+SHRD, private PRVI+PRVD+BSSI+BSSD.
    n, z = sizes(p)
    return n, z[SHRI] + z[SHRD], z[PRVI] + z[PRVD] + z[BSSI] + z[BSSD]
def rnd(n):
    return (n + CLICK - 1) // CLICK * CLICK
lib = sys.argv[1]
n, t, d = lib_(lib)
print("  libc.1           file %6d  shared %5d -> %5d resident ONCE" % (n, t, rnd(t)))
print("                                 private %5d -> %5d resident PER CLIENT"
      % (d, rnd(d)))
for p in sys.argv[2:]:
    n, t, d = prog(p)
    print("  %-16s file %6d  text %5d -> %5d once   data %5d -> %5d per process"
          % (p.split('/')[-1], n, t, rnd(t), d, rnd(d)))
PY

# --- the image --------------------------------------------------------------
cp -f "$IMG" "$WORK/libc1.bin"
put() { "$COHFS" put -p "$ROOTPART" -m "$3" "$WORK/libc1.bin" "$1" "$2" \
	>> "$WORK/put.log" 2>&1 || { echo "libc1: cannot put $2" >&2; exit 2; }; }
put "$KERNEL" /coherent 755
for d in notty lrtty hrtty; do put "$DRV/$d" "/drv/$d" 755; done
put "$B/libc1/libc.1" /lib/libc.1 644
for c in slsmall.st slsmall.sl slmid.st slmid.sl mem; do
	put "$WORK/$c" "/bin/$c" 755
done
# slmid under NDIST names: NDIST separate text segments.
i=1
while [ $i -le $NDIST ]; do
	put "$WORK/slmid.st" "/bin/d$i.st" 755
	put "$WORK/slmid.sl" "/bin/d$i.sl" 755
	i=$((i + 1))
done
for c in $CCMDS; do
	put "$WORK/C$c.st" "/bin/C$c.st" 755
	put "$WORK/C$c.sl" "/bin/C$c.sl" 755
done

# One typed line per phase, so the clients overlap on the guest.
mksame() {	# mksame <name> <suffix> -- NCLIENT copies of each of two programs
	cat > "$WORK/$1" <<EOF
pids=
i=1
while [ \$i -le $NCLIENT ]
do
	/bin/slsmall.$2 300 &
	pids="\$pids \$!"
	/bin/slmid.$2 300 &
	pids="\$pids \$!"
	i=\`expr \$i + 1\`
done
sleep 3
/bin/mem
kill \$pids
wait
EOF
	put "$WORK/$1" "/bin/$1" 755
}
mkdist() {	# mkdist <name> <suffix> -- one copy of each of NDIST programs
	cat > "$WORK/$1" <<EOF
pids=
i=1
while [ \$i -le $NDIST ]
do
	/bin/d\$i.$2 300 &
	pids="\$pids \$!"
	i=\`expr \$i + 1\`
done
sleep 3
/bin/mem
kill \$pids
wait
EOF
	put "$WORK/$1" "/bin/$1" 755
}
mksess() {	# mksess <name> <suffix> -- two shells, two pipelines, six commands
	# Each pipeline blocks on a sleep, so all stages are resident.
	cat > "$WORK/$1" <<EOF
( /bin/Csleep.$2 $CHOLD ; echo one ) | /bin/Csort.$2 | /bin/Cwc.$2 &
( /bin/Csleep.$2 $CHOLD ; echo two ) | /bin/Ccat.$2 | /bin/Cuniq.$2 &
sleep 4
/bin/Chead.$2 -1 /etc/motd
/bin/mem
wait
sleep 8
EOF
	put "$WORK/$1" "/bin/$1" 755
}
mksame runsame.st st
mksame runsame.sl sl
mkdist rundist.st st
mkdist rundist.sl sl
mksess runsess.st st
mksess runsess.sl sl

cat > "$WORK/cmds" <<'EOF'
sleep 10
/bin/mem
sh /bin/runsame.st
/bin/mem
sh /bin/runsame.sl
/bin/mem
sh /bin/rundist.st
/bin/mem
sh /bin/rundist.sl
/bin/mem
sh /bin/runsess.st
/bin/mem
sh /bin/runsess.sl
/bin/mem
echo mark over
EOF
W=$WORK
WORK="$W/run.bin" OUT="$W/out" ERR="$W/err" \
	sh "$UL/hostbuild/emu-run.sh" "$W/cmds" "$W/libc1.bin" \
	> "$W/harness" 2>&1
grep -a 'stop channel' "$W/harness"

# --- what the transcript says -----------------------------------------------
probe() {	# probe <n> -- the nth mem(1) free figure, in Kb
	grep -a '^free  *[0-9]*K' "$W/out" | sed -n "$1s/^free  *\([0-9]*\)K.*/\1/p"
}
b1=$(probe 1); same_st=$(probe 2)
b2=$(probe 3); same_sl=$(probe 4)
b3=$(probe 5); dist_st=$(probe 6)
b4=$(probe 7); dist_sl=$(probe 8)
b5=$(probe 9); sess_st=$(probe 10)
b6=$(probe 11); sess_sl=$(probe 12)
b7=$(probe 13)

echo ""
echo "=== the clients ran"
n_small=$(grep -a -c 'slsmall\[' "$W/out" || true)
n_mid=$(grep -a -c 'slmid\[' "$W/out" || true)
n_load=$(grep -a -c 'shlib: loaded libc.1' "$W/out" || true)
want_small=$((NCLIENT*2))
want_mid=$((NCLIENT*2 + NDIST*2))
[ "$n_small" = "$want_small" ] &&
	say PASS "$n_small slsmall runs" ||
	say FAIL "$n_small slsmall runs, expected $want_small"
[ "$n_mid" = "$want_mid" ] &&
	say PASS "$n_mid slmid runs (including the $NDIST-way distinct phase, twice)" ||
	say FAIL "$n_mid slmid runs, expected $want_mid"
# Static and shared slmid must print the same payload.
npay=$(grep -a 'slmid\[' "$W/out" | sed 's/.*alive //' | sort -u | wc -l)
[ "$npay" = 1 ] &&
	say PASS "all $n_mid slmid runs printed the same answers, shared and static" ||
	say FAIL "$npay different answers from slmid -- the shared clients do not agree with the static ones"
[ "$n_load" -ge 1 ] && [ "$n_load" -le 3 ] &&
	say PASS "libc.1 loaded $n_load time(s) for three whole phases of clients" ||
	say FAIL "libc.1 was loaded $n_load times"
# Phase C's answers must match too.
for want in '1  *1  *4' 'two'; do
	n=$(grep -a -c "$want" "$W/out" || true)
	[ "$n" = 2 ] &&
		say PASS "the session pipelines agree, static and shared ($want)" ||
		say FAIL "\`$want' appeared $n times, wanted 2 (static and shared)"
done

echo ""
echo "=== FREE MEMORY, from mem(1) through /dev/kmem, in Kb"
row() {		# row <label> <baseline> <with clients>
	printf "  %-40s %6sK -> %6sK   cost %5sK\n" "$1" "$2" "$3" "$(($2 - $3))"
}
# The first reading is printed, not used, so the settling shows.
echo "  first reading, boot still settling: ${b1:-?}K (not used)"
echo "  A: $((NCLIENT*2)) processes, TWO programs (text shared among the copies)"
row "    static" "${b2:-0}" "${same_st:-0}"
row "    shared (libc.1)" "${b3:-0}" "${same_sl:-0}"
echo "  B: $NDIST processes, $NDIST DIFFERENT programs (a text segment each)"
row "    static" "${b4:-0}" "${dist_st:-0}"
row "    shared (libc.1)" "${b5:-0}" "${dist_sl:-0}"
echo "  C: a session -- 2 shells, 2 pipelines, 6 different real commands"
row "    static" "${b6:-0}" "${sess_st:-0}"
row "    shared (libc.1)" "${b7:-0}" "${sess_sl:-0}"
echo "  settled baselines: $b2 $b3 $b4 $b5 $b6 $b7"
echo ""
da=$(( (b2 - same_st) - (b3 - same_sl) ))
db=$(( (b4 - dist_st) - (b5 - dist_sl) ))
dc=$(( (b6 - sess_st) - (b7 - sess_sl) ))
echo "  A: the shared clients cost $((-da))K MORE than the static ones"
echo "  B: the shared clients cost $((-db))K MORE than the static ones"
echo "  C: the shared clients cost $((-dc))K MORE than the static ones"
echo "  (a negative number there means the shared library WON)"

# A phase whose two baselines differ as much as its answer decides nothing.
verdict() {	# verdict <phase> <baseline> <baseline> <difference>
	w=$(($2 - $3)); [ $w -lt 0 ] && w=$((-w))
	m=$4; [ $m -lt 0 ] && m=$((-m))
	[ "$w" -lt "$m" ] &&
		say PASS "$1: its two baselines agree to ${w}K, inside the ${m}K it measured" ||
		say FAIL "$1: its baselines differ by ${w}K, as much as the ${m}K measured -- this phase decides nothing"
}
verdict A "$b2" "$b3" "$da"
verdict B "$b4" "$b5" "$db"
verdict C "$b6" "$b7" "$dc"

echo ""
[ "$fail" = 0 ] && echo "libc1: measurement complete" || echo "libc1: FAILED"
echo "  transcript $W/out"
exit $fail
