#!/bin/sh
# check-stamps.sh [image ...] -- verify kernel, drivers, and image have matching
# link ids. Drivers linked via ld -k are bound to a specific kernel.out; this
# gate catches stale pairings (mtime-independent, sha1 of bytes).
#
#   kobj/kernel.stamp   linkid=          written by link-kernel.sh
#   build/drv/.drvstamp kernel_linkid=   written by build-drivers.sh
#   build/<dist>.stamp  kernel_linkid=   written by build-image.sh
#
# With image argument, extracts /coherent back out to verify actual content.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/provenance.sh"

KSTAMP="$HERE/kobj/kernel.stamp"
DSTAMP="$HERE/build/drv/.drvstamp"
rc=0
say() { echo "$@"; }
bad() { echo "FAIL: $*" >&2; rc=1; }

[ -f "$HERE/kobj/kernel.out" ] || { bad "no kobj/kernel.out (run link-kernel.sh)"; exit 1; }
LIVEK="$HERE/kobj/kernel.out"
LIVE=$(prov_id "$LIVEK")
KID=$(prov_get "$KSTAMP" linkid)

# MISSING stamp (rc 2) is an inconsistency; dirty (rc 1) is reported but not fatal.
prov_header "kernel" "$KSTAMP"; [ $? = 2 ] && rc=1
say "== kernel.out on disk : $LIVE"
say "== kernel.stamp says  : ${KID:-<none>}"
[ -n "$KID" ] || bad "kobj/kernel.stamp missing or has no linkid"
[ -z "$KID" ] || [ "$KID" = "$LIVE" ] || \
	bad "kernel.out does not match its own stamp (published $KID, on disk $LIVE)"

DKID=$(prov_get "$DSTAMP" kernel_linkid)
say "== build/drv linked vs: ${DKID:-<none>}"
if [ -z "$DKID" ]; then
	bad "build/drv/.drvstamp missing -- drivers are bound to an unknown kernel"
elif [ "$DKID" != "$LIVE" ]; then
	bad "drivers were linked against $DKID, kernel is $LIVE -- run build-drivers.sh"
else
	say "== OK: drivers paired with the current kernel"
fi

for img in "$@"; do
	say "-- $img"
	[ -f "$img" ] || { bad "$img: no such image"; continue; }
	s="$img.stamp"
	prov_header "image" "$s"; [ $? = 2 ] && rc=1
	iid=$(prov_get "$s" imageid); now=$(prov_id "$img")
	[ -z "$iid" ] || [ "$iid" = "$now" ] || \
		bad "$img has been modified since it was packed ($iid -> $now)"
	ikid=$(prov_get "$s" kernel_linkid)
	[ "$ikid" = "$LIVE" ] || \
		say "!! $img was packed from kernel $ikid, the tree now has $LIVE"
	# Read the kernel back OUT of the filesystem and compare it.  This is the
	# only step that proves what the image actually carries; everything above
	# is bookkeeping about what was intended.  Root filesystem, /coherent --
	# the media descriptors' `kernel=' for the root partition.
	part=$(sed -n 's/^part[ \t]*root[ \t]*\([0-9]*\).*/\1/p' \
	       "$HERE/../dist/$(prov_get "$s" media)" 2>/dev/null)
	[ -n "$part" ] || part=136
	tmp=${TMPDIR:-/tmp}/ckstamp.$$.kern
	err=${TMPDIR:-/tmp}/ckstamp.$$.err
	# Extract kernel from image and compare byte-for-byte (not hash, since
	# dist.py patches rootdev + wd(4) table; patches are <16 bytes, relink moves thousands).
	if python3 "$HERE/fsread.py" "$img" cat /coherent --part "$part" > "$tmp" 2>"$err" &&
	   [ -s "$tmp" ]; then
		got=$(prov_id "$tmp")
		d=$(cmp -l "$LIVEK" "$tmp" 2>/dev/null | wc -l)
		if [ "$(wc -c < "$tmp")" != "$(wc -c < "$LIVEK")" ]; then
			bad "$img: /coherent is $(wc -c < "$tmp") bytes, the tree's"\
			    "kernel is $(wc -c < "$LIVEK") -- a different kernel"
		elif [ "$d" -le 16 ]; then
			say "== OK: /coherent in the image is this kernel ($got,"\
			    "$d byte(s) patched by dist.py)"
		else
			bad "$img: /coherent differs from the tree's kernel in $d"\
			    "bytes ($got vs $LIVE) -- it is a different kernel"
		fi
	else
		bad "$img: could not extract /coherent (part $part) -- the image was"
		say "      not read, so nothing above it has been checked against it."
		sed 's/^/      | /' "$err" >&2
	fi
	rm -f "$tmp" "$err"
done

[ $rc = 0 ] && say "== stamps consistent"
exit $rc
