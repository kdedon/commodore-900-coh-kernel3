#!/bin/sh
# link-kernel.sh - compile and link the C900 Coherent kernel.
#
# Compiles the kernel object set (3.2 MI core + Z8001 MD + drivers + the
# wdcon root configuration + md.s) and links with ld-z8001.  Objects mirror
# the 3.2 us/Makefile DOTDOT list with the Z8001 MD standing in for the i286
# files; swap.o is excluded (not linked in 3.2 -- /dev/swap is /dev/null) and
# the diag/rec subtrees are standalone programs, not kernel.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/.." && pwd)
. "$OS/hostbuild/toolchain.sh"	# sets $TC: the Z8001 toolchain checkout
. "$OS/hostbuild/kboot.sh"	# sets $KB: the loader checkout, for bistage below
VERSION=$(sh "$HERE/version.sh") || exit 1		# single source of the release string (also feeds /etc/motd)
CC0="$TCB/z8001/cc0-z8001"; CC1="$TCB/z8001/cc1-z8001"
CC2="$TCB/z8001/cc2-z8001"; AS="$TCB/as-z8001"; LD="$TCB/ld-z8001"
VAR="${CCZ_VAR:-800000020800}"
. "$HERE/provenance.sh"

# Build private, publish by atomic rename (drivers and image build against live kernel.out).
# ld writes kernel.out even with unresolved symbols; publish only on success.
PUB="$HERE/kobj"
OBJ="$HERE/kobj.$$"
trap 'rm -rf "$OBJ"' EXIT INT TERM
rm -rf "$OBJ"; mkdir -p "$OBJ" "$PUB"
# Clean up stale work dirs from previous runs (only >1 day old to avoid concurrent builds).
find "$HERE" -maxdepth 1 -mtime +0 -name 'kobj.[0-9]*' -exec rm -rf {} + 2>/dev/null || true
LOG="$HERE/logs/kernel-link.log"; mkdir -p "$HERE/logs"; : > "$LOG"
# Record which compiler builds the kernel and what source it came from; a
# toolchain built from a dirty tree says so here.
prov_header "toolchain" "$TCB/z8001/.provenance" || true

# build/gen: generated headers (wd(4) table and loader handoff).
# KMEDIA picks the built-in partition table (hd21 = real hardware, fallback for no-loader boots).
# dist.py patches wdbtab_ for the actual media; only the loader's bootinfo.h is shared.
KMEDIA="${KMEDIA:-hd21}"
mkdir -p "$HERE/build/gen"
if [ "$KMEDIA" = hd21 ] && [ -r "$HERE/wdbtab-hd21.h" ]; then
	cp "$HERE/wdbtab-hd21.h" "$HERE/build/gen/wdbtab.h"
else
	echo "wdbtab.h: no built-in table for KMEDIA=$KMEDIA in this repository." >&2
	echo "  Only hd21 (real hardware's layout) ships built in; see the note" >&2
	echo "  above link-kernel.sh's KMEDIA=... line." >&2
	exit 1
fi
bistage "$HERE/build/gen" ||
	{ echo "cannot stage bootinfo.h from $KB"; exit 1; }

IMI="-I$HERE/build/gen -I$OS/include -I$OS/include/sys -I$TCINC -I$TCINC/sys -I$OS/sys/z8001/h"
IMD="-I$HERE/build/gen -I$OS/sys/z8001/h -I$OS/sys/h -I$OS/include -I$OS/include/sys -I$TCINC -I$TCINC/sys"

# Size-reduction defines: NOMONITOR (drop debug printfs), TINY (stub ptrace),
# READAHEAD=0 (drop read-ahead). KDDT=1 links the in-kernel debugger (~3.5K).
KDEFS="-DNOMONITOR=1 ${KDDT:+-DDDT=1} ${EXTRA_KDEFS:-}"

cc_one() { # cc_one <src> <incs> -> $OBJ/<stem>.o ; return nonzero on failure
	src=$1; incs=$2; stem=$(basename "$src" .c)
	c900_buildlog "$src"
	xdef=""
	case "$stem" in
	fs3)  xdef="-DTINY=1";;
	bio)  xdef="-DREADAHEAD=0";;
	# COPYYEAR from date(1) on the build host: the target has no RTC
	# (mdstub.c read_cmos returns 0).
	main) xdef="-DRELEASE=\"$VERSION\" -DCOPYYEAR=\"$(date +%Y)\"";;
	esac
	"$CC0" $VAR "$src" "$OBJ/$stem.z0" $incs $KDEFS $xdef >>"$LOG" 2>&1 &&
	"$CC1" $VAR "$OBJ/$stem.z0" "$OBJ/$stem.z1" >>"$LOG" 2>&1 &&
	"$CC2" 0012 "$OBJ/$stem.z1" "$OBJ/$stem.o" "$OBJ/$stem.scr" 0 >>"$LOG" 2>&1	# 0012 = VPEEP + VKERN (frame refs in SS=0x3F)
}

fail=0
# --- 3.2 MI core (all of sys/coh except the ATTIC) ---
# i386 boot machinery (fifo_*.c, arg_exist.c) omitted; poll.c linked (inet/FIFO IPC).
for f in "$OS"/sys/coh/*.c; do
	case "$(basename "$f")" in
	fifo_close.c|fifo_len.c|fifo_open.c|fifo_read.c|fifo_rewind.c|fifo_write.c|arg_exist.c) continue;;
	esac
	cc_one "$f" "$IMI" || { echo "MI compile FAIL: $f" | tee -a "$LOG"; fail=1; }
done
# --- MI leftovers hosted with the 0.7.3 headers ---
# (conf.c/mdstub.c define 3.2-contract items -> 3.2 headers)
for f in "$OS"/sys/z8001/src/conf.c "$OS"/sys/z8001/src/mdstub.c "$OS"/sys/z8001/src/pcopy.c "$OS"/sys/z8001/src/krunch.c "$OS"/sys/z8001/src/exec.c; do
	cc_one "$f" "$IMI" || { echo "conf compile FAIL: $f" | tee -a "$LOG"; fail=1; }
done
# KTTY=termio (default, 4.x with VMIN/VTIME) or sgtty (0.7.3, no pty support).
# pty.c depends on termio, so switch both together.
TTYSRC="$OS/sys/drv/tty.c"
[ "${KTTY:-termio}" = sgtty ] && TTYSRC="$OS/sys/drv/tty-sgtty.c"
PTYSRC="$OS/sys/drv/pty.c"
KDEFS="$KDEFS -DKPTY=1"
[ "${KTTY:-termio}" = sgtty ] && { PTYSRC=""; KDEFS="${KDEFS% -DKPTY=1}"; }
# KMOUSE=1 (default) links the HR card's mouse driver (resident, not loadable; uses timeout()).
MOUSESRC="$OS/sys/drv/mouse.c"
KMOUSE="${KMOUSE-1}"
[ -n "$KMOUSE" ] && KDEFS="$KDEFS -DKMOUSE=1" || MOUSESRC=""
for f in "$OS"/sys/drv/ct.c "$TTYSRC" $PTYSRC $MOUSESRC "$OS"/sys/ker/elog.c; do
	cc_one "$f" "$IMI" || { echo "MD-host compile FAIL: $f" | tee -a "$LOG"; fail=1; }
done
# --- Z8001 MD: src + drivers + console (rec/) + root device config (wdcon:
# the WD disk root, whose CON tables we compile: nl/ct/al/wd/lp/kv) ---
[ -n "${KDDT:-}" ] && { cc_one "$OS/sys/z8001/src/ddt.c" "$IMD" ||
	{ echo "ddt compile FAIL" | tee -a "$LOG"; fail=1; }; }
for f in "$OS"/sys/z8001/src/commodore.c "$OS"/sys/z8001/src/console.c \
	 "$OS"/sys/z8001/src/trap.c \
	 "$OS"/sys/z8001/src/tab.c \
	 "$OS"/sys/z8001/drv/wd.c \
	 "$OS"/sys/z8001/drv/al.c \
	 "$OS"/sys/z8001/drv/lp.c \
	 "$OS"/sys/z8001/drv/snd.c \
	 "$OS"/sys/z8001/con/wdcon.c; do
	cc_one "$f" "$IMI" || { echo "MD compile FAIL: $f" | tee -a "$LOG"; fail=1; }
done
# --- kernel support library: the five libc routines the 3.2 MI calls ---
# Linked from toolchain/kobj, not compiled here (model-neutral w/ SS relocation).
# Named explicitly to avoid picking up conflicting userland members from libc-z8001.a.
# only the five routines asked for.
KOBJ="$TCB/libc-z8001/kobj"
for b in l3tol ltol3 ltoc strcmp canon; do
	if [ -r "$KOBJ/$b.o" ]; then
		cp "$KOBJ/$b.o" "$OBJ/$b.o"
	else
		echo "klib FAIL: no $KOBJ/$b.o (toolchain: build libc, or unpack a release carrying native/kobj)" | tee -a "$LOG"
		fail=1
	fi
done
# --- md.s (cpp-preprocessed MD assembly) ---
# The video console stack (rec/{kv,kb,v0,mm,kbtab} + mmas.s) is not linked --
# it ships as the loadable /drv/lrtty (build-drivers.sh), which vidsel picks
# at boot.  ddt rides on KDDT above.
if [ -n "${KDDT:-}" ]; then ddtdef="-DDDT=1"; else ddtdef="-UDDT"; fi
for f in "$OS/sys/z8001/src/md.s"; do
	b=$(basename "$f" .s)
	c900_buildlog "$f"
	# -o, not `>': gcc's cpp reads `cpp IN OUT' and removes that output
	# on error, so a stray argument taken for the output file deletes a
	# source.
	cpp -traditional-cpp -P -DPARANOID -DNLD $ddtdef -o "$OBJ/$b.i" "$f" 2>>"$LOG" &&
	"$AS" -g -o "$OBJ/$b.o" "$OBJ/$b.i" >>"$LOG" 2>&1 || { echo "$b.s FAIL" | tee -a "$LOG"; fail=1; }
done

n=$(ls "$OBJ"/*.o | wc -l)
echo "== objects built: $n (compile failures: $fail)"

# --- the link ---
# Link layout = the ROM contract: text at segment 0x30 offset 0 (-R takes the
# VADDR form seg<<24; ld vtop's it internally), -L bumps data to the next
# segment boundary = 0x31:0000 (the kernel's DS).  `-e start' sets l_entry to
# md.s `start' so the ROM jumps LDSEG:start directly: the default offset 0
# lands on the cmdblk_ trampoline, which is the disk command block and gets
# corrupted by the ROM's staging copy-down for kernels near the 64K text
# ceiling.  start rebuilds all its own registers, so it needs no trampoline.
"$LD" -i -L -e start -R 0x30000000 -o "$OBJ/kernel.out" "$OBJ/md.o" $(ls "$OBJ"/*.o | grep -v '/md\.o$') > "$OBJ/link.txt" 2>&1
rc=$?
echo "== ld exit: $rc"
# ld prints undefined symbols; dedupe into the gap list
grep -i 'undef' "$OBJ/link.txt" | sort -u > "$OBJ/undefined.txt" || true
awk '{print $NF}' "$OBJ/undefined.txt" 2>/dev/null | sort -u > "$OBJ/gap-symbols.txt" || true
nundef=$(wc -l < "$OBJ/gap-symbols.txt")
echo "== undefined symbols: $nundef"
head -60 "$OBJ/gap-symbols.txt"
# ld still writes kernel.out when it leaves symbols unresolved (they become
# zero), so a failed link must not publish.
if [ "$rc" -ne 0 ] || [ "$nundef" -ne 0 ] || [ "$fail" -ne 0 ]; then
	echo "== FATAL: link failed (ld exit $rc, $nundef undefined, $fail compile failures)"
	echo "== nothing published: $PUB/kernel.out is unchanged"
	[ -f "$PUB/kernel.out" ] &&
		echo "==   it is now STALE (link id $(prov_id "$PUB/kernel.out"), $(prov_get "$PUB/kernel.stamp" built)) -- older than the source that failed"
	exit 1
fi
[ -f "$OBJ/kernel.out" ] && echo "== kernel.out: $(wc -c < "$OBJ/kernel.out") bytes"
# Load-size check.  Text may span segments -- ld gives it consecutive ones and
# puts the data in the next (main.c newpage), and kboot stages and maps that
# many (bmain.c NTSEG, crt.s launch stub).  Data still has to fit one segment,
# and the whole image the segments md.s describes (machz8001.h NKSEG).  The
# ROM's vread() walks only 10 direct + 128 indirect blocks and then silently
# hands back zero-filled buffers, so a kernel past 138 blocks requires kboot,
# which walks the inode itself (kopen/kvread, double-indirect included).
python3 - "$OBJ/kernel.out" <<'PYEOF'
import sys
b=open(sys.argv[1],'rb').read(48)
u16=lambda o: b[o]|b[o+1]<<8
pdp=lambda o: (u16(o)<<16)|u16(o+2)
text=pdp(8)+pdp(12); data=pdp(20)+pdp(24); bss=pdp(28)
LIM=65536
NTSEG=2				# bmain.c: text segments kboot can stage
nts=(text+LIM-1)//LIM or 1
print("== load image: text %d (%d segment%s of %d) + data %d + bss %d (%d/%d)"
      % (text,nts,"" if nts==1 else "s",LIM,data,bss,data+bss,LIM))
bad=0
if nts>NTSEG:
    print("== FATAL: text needs %d segments; kboot stages %d (bmain.c NTSEG)"%(nts,NTSEG)); bad=1
if data+bss>=LIM:
    print("== FATAL: data+bss is %d bytes; it must fit one 64K segment"%(data+bss)); bad=1
if bad: sys.exit(1)
if 48+text+data > 70656:
    print("== NOTE: %d bytes = %d blocks, past the ROM vread()'s 138 -- needs the"
          " current kboot (its own inode walk)" % (48+text+data, (48+text+data+511)//512))
print("== OK: text fits %d segment%s, data fits one" % (nts,"" if nts==1 else "s"))
PYEOF
[ $? -eq 0 ] || { echo "== nothing published: $PUB/kernel.out is unchanged"; exit 1; }

# --- publish ----------------------------------------------------------------
# Atomic rename(2) ensures readers see either whole old or whole new kernel.
# Link id (sha1) is the stamp identity: drivers linked ld -k carry absolute addresses
# from this exact image, so pairing is content-based, not time-based.
LINKID=$(prov_id "$OBJ/kernel.out")
prov_write "$OBJ/kernel.stamp" kernel \
	os/sys os/include os/hrtty \
	os/hostbuild/link-kernel.sh os/hostbuild/wdbtab-hd21.h \
	-- "linkid=$LINKID" "version=$VERSION" "ktty=${KTTY:-termio}" \
	   "kddt=${KDDT:-}" "kmedia=$KMEDIA" "kmouse=$KMOUSE" \
	   "toolchain=$(prov_get "$TCB/z8001/.provenance" commit)" \
	   "toolchain_dirtysrc=$(prov_get "$TCB/z8001/.provenance" dirtysrc)"
for f in "$OBJ"/*; do
	case "$f" in */kernel.out) continue;; esac
	mv -f "$f" "$PUB/" || exit 1
done
mv -f "$OBJ/kernel.out" "$PUB/kernel.out" || exit 1
echo "== published $PUB/kernel.out  link id $LINKID"
echo "==   drivers must now be relinked against it (ld -k bakes absolute addresses)"
prov_header "kernel" "$PUB/kernel.stamp" || true
