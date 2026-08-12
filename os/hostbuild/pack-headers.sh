#!/bin/sh
# pack-headers.sh -- package the kernel header set on its own.
#
#	sh hostbuild/pack-headers.sh [VERSION] [DESTDIR]
#
# Writes c900-kernel-headers-v<V>.tar.gz.  VERSION defaults to the git tag
# (version.sh), DESTDIR to hostbuild/build/dist.
#
# A consumer that COMPILES against the kernel -- a driver, a program reading
# <sys/proc.h>, anything using the syscall numbers -- needs the headers and
# nothing else.  It has no use for a kernel image it cannot boot and must not
# have to unpack one to get a header.  So this is a package of its own; the
# kernel package carries the same headers for the operator installing a system.
#
# WHICH headers is computed, not listed: kheaders.py resolves every #include in
# the kernel and driver sources transitively.  A header added to a kernel source
# joins the package the next time it is cut.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$HERE/.." && pwd)

V=${1:-}
[ -n "$V" ] || V=$(sh "$HERE/version.sh") || exit 1
V=${V#v}
DEST=${2:-$HERE/build/dist}

name="c900-kernel-headers-v$V"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
A="$W/$name"
mkdir -p "$A"

n=0
python3 "$HERE/kheaders.py" list | while read -r h; do
	[ -f "$OS/$h" ] || { echo "pack-headers.sh: $h is missing" >&2; exit 1; }
	mkdir -p "$A/$(dirname "$h")"
	cp "$OS/$h" "$A/$h"
done
n=$(python3 "$HERE/kheaders.py" list | wc -l)
[ "$n" -gt 0 ] || { echo "pack-headers.sh: kheaders.py named no headers" >&2; exit 1; }

echo "$V" > "$A/VERSION"
mkdir -p "$DEST"
(cd "$W" && tar czf "$name.tar.gz" "$name")
mv "$W/$name.tar.gz" "$DEST/"
echo "packed: $DEST/$name.tar.gz ($n headers)"
