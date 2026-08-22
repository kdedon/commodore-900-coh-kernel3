#!/bin/sh
# pack-kernel.sh - package the kernel deliverable (kernel + drivers + headers + contracts).
#
#	sh hostbuild/pack-kernel.sh [VERSION] [DESTDIR]
#
# VERSION defaults to hostbuild/VERSION, DESTDIR to hostbuild/build/dist.
# Writes: c900-kernel-v<V>.tar.gz
# Contents: drivers linked ld -k against this kernel (content-identified), symbol table,
# headers, syscalls.tab, and check-stamps.sh (pairing gate for consumers).
# Prereqs: make kernel drivers.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$OS/.." && pwd)
. "$HERE/provenance.sh"

V=${1:-}
[ -n "$V" ] || V=$(sh "$HERE/version.sh" 2>/dev/null || true)
V=${V#v}
[ -n "$V" ] || { echo "pack-kernel.sh: no version (hostbuild/VERSION is empty)" >&2; exit 2; }
DEST=${2:-$HERE/build/dist}

KOBJ="$HERE/kobj"
DRV="$HERE/build/drv"
for p in "$KOBJ/kernel.out:make kernel" \
	 "$KOBJ/kernel.stamp:make kernel" \
	 "$DRV/.drvstamp:make drivers" \
	 "$DRV/.kdrv.conf:make drivers"; do
	f=${p%:*}; t=${p#*:}
	[ -e "$f" ] || { echo "pack-kernel.sh: $f is missing -- run \`$t'" >&2; exit 1; }
done

# Gate: drivers and kernel must be paired before packing.
sh "$HERE/check-stamps.sh" >/dev/null || {
	echo "pack-kernel.sh: kernel and drivers disagree about which link they came from." >&2
	sh "$HERE/check-stamps.sh" >&2 || true
	echo "  Run \`make kernel drivers'.  Nothing has been packed." >&2
	exit 1
}

LINKID=$(prov_get "$KOBJ/kernel.stamp" linkid)
name=c900-kernel-v$V
W=$(mktemp -d "${TMPDIR:-/tmp}/packkern.XXXXXX")
trap 'rm -rf "$W"' EXIT INT TERM
A="$W/$name"
mkdir -p "$A/boot" "$A/drv" "$A/include/sys" "$A/hostbuild/kobj" "$A/hostbuild/build"

# ---- the kernel, and the drivers bound to it ----
cp "$KOBJ/kernel.out" "$KOBJ/kernel.stamp" "$A/boot/"
for f in "$DRV"/*; do
	case $(basename "$f") in .*) continue;; esac
	cp "$f" "$A/drv/"
done
cp "$DRV/.drvstamp" "$DRV/.kdrv.conf" "$A/drv/"

# ---- the headers ----
# Computed by kheaders.py (resolves include closure automatically; no stale lists).
python3 "$HERE/kheaders.py" pairs > "$W/headers" ||
	{ echo "pack-kernel.sh: kheaders.py failed" >&2; exit 1; }
n=0
while IFS="	" read -r h src; do
	[ -n "$h" ] || continue
	mkdir -p "$A/$(dirname "$h")"
	cp "$src" "$A/$h"
	n=$((n + 1))
done < "$W/headers"
# A count of zero is an answer, not a failure: the kernel ships the headers it
# produces, and every interface it shares with the C library and the userland is
# published by the toolchain instead.  kheaders.py failing IS a failure, which
# is why its exit status is checked above and not this number.
echo "kernel headers: $n"

# ---- the syscall dispatch table, as data ----
# Contract with C library stubs (in toolchain); extracted from trailing comments in tab.c.
awk '/^struct[ \t]+systab[ \t]+sysitab/ { on = 1; next }
     on && /^};/ { on = 0 }
     on && match($0, /\/\*[ \t]*[0-9]+[ \t]*=/) {
	     s = substr($0, RSTART)
	     gsub(/^\/\*[ \t]*/, "", s); gsub(/[ \t]*\*\/[ \t]*$/, "", s)
	     nn = s; sub(/[ \t]*=.*/, "", nn)
	     nm = s; sub(/^[0-9]+[ \t]*=[ \t]*/, "", nm)
	     # The comments carry prose after the name -- poll is followed by
	     # a note that npoll is a LONG.  A consumer comparing against the
	     # sys NN literal in a library stub wants the name alone, so the
	     # first word is what is emitted.
	     sub(/[ \t].*/, "", nm)
	     print nn, nm
     }' "$OS/sys/z8001/src/tab.c" > "$A/syscalls.tab"
[ -s "$A/syscalls.tab" ] || { echo "pack-kernel.sh: extracted no syscall table from tab.c" >&2; exit 1; }

# ---- the driver-build contract ----
# No linker script; layout decided by flags. cc2 mode selects kernel frame convention.
{
	cat <<EOF
# kdrv.conf -- what a loadable driver must be built with to load into this
# kernel.  Read as key=value; see build-drivers.sh for the invocations.
#
# A driver is a NON-SEPARATED l.out linked at base 0: pload() allocates one
# segment of si+pi+sd+pd and maps it through the OS transient window, so text and
# data share the address space.  It keeps its symbol table -- that is how
# /etc/load finds the *con_ configuration table -- and it must be executable or
# exlopen() refuses it with EACCES.
#
# The compiler keys below are drv/.kdrv.conf verbatim: the values the drivers
# in this package were actually built with, written by the build that built
# them.  A consumer reading them from a checkout finds the same file at
# hostbuild/build/drv/.kdrv.conf.
kernel=boot/kernel.out
version=$V
incdirs=include include/sys
EOF
	cat "$DRV/.kdrv.conf"
} > "$A/kdrv.conf"

# ---- the pairing gate, shipped with the thing it judges ----
# check-stamps.sh resolves kobj/ and build/drv relative to its own directory (below).
cp "$HERE/check-stamps.sh" "$HERE/provenance.sh" "$A/hostbuild/"

# ---- the checkout-shaped view ----
# Consumers use C900_KERNEL with same paths whether from checkout or release.
# Relative symlinks so tree is portable.
ln -s ../../boot/kernel.out "$A/hostbuild/kobj/kernel.out"
ln -s ../../boot/kernel.stamp "$A/hostbuild/kobj/kernel.stamp"
ln -s ../../drv "$A/hostbuild/build/drv"

echo "$V" > "$A/VERSION"
[ -f "$ROOT/LICENSE" ] && cp "$ROOT/LICENSE" "$A/"

# The stamp: kernel's own, plus version and pack time (composed, not appended).
{
	cat "$KOBJ/kernel.stamp"
	echo "version=$V"
	echo "packed=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "drivers=$(prov_get "$DRV/.drvstamp" drivers)"
	echo "headers=$n"
} > "$A/.provenance"

mkdir -p "$DEST"
(cd "$W" && tar czf "$name.tar.gz" "$name")
mv "$W/$name.tar.gz" "$DEST/"
echo "packed: $DEST/$name.tar.gz"
echo "  kernel link id $LINKID, $n headers, $(awk 'END{print NR}' "$A/syscalls.tab") syscalls"
prov_header "kernel" "$KOBJ/kernel.stamp" || true
