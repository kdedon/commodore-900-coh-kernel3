#!/bin/sh
# pack-component.sh -- cut ONE component-kind package out of this repository.
#
#	sh os/hostbuild/pack-component.sh [-o OUTDIR] <component>-<kind>
#	sh os/hostbuild/pack-component.sh [-o OUTDIR] <component> <kind>
#	sh os/hostbuild/pack-component.sh components
#
#	  console-hr      the runtime files      -> c900-console-hr-bin-v<V>.tar.gz
#	  console-hr-src  the corresponding source
#	  console-hr-man  the Lexicon articles for it
#
# A component is an os/dist/lists/*.list carrying a `package' line -- here the
# three loadable console drivers, one list each -- and `components' prints them
# with the kinds each publishes.  A bare component name means its `bin' kind.
# OUTDIR defaults to build/packages.
#
# WHERE EACH KIND COMES FROM, all of it this repository's own record:
#
#   bin   the list's entries, resolved against build/drv; the stamp carries the
#         link id of the kernel the drivers are bound to (build/drv/.drvstamp),
#         because `ld -k' bakes that kernel's addresses into them
#   src   for every executable the list installs, the paths build/.ulsrcmap
#         names for it -- written by build-drivers.sh as it links, one row per
#         driver, a directory travelling whole.  A program with no row refuses
#         the package: the -src package is how the binaries' licence obligations
#         are discharged, and a guess would be a promise nobody checked.
#   man   the rows of man/man.index whose name is one of the component's
#         programs, or an article the list's `articles <program> <article>...'
#         line names for one: a driver is documented under the device it
#         provides (console, kb), not under its file name.  man.index is
#         generated with exactly the rows packed.  No page at all refuses.
#
# The archives meet the four component-kind declarations the userland publishes
# in its dist/packages/component-{bin,src,man,dev}.pkg -- the one home of the
# format every producer's component packages are judged against -- and
# commodore-900-dist's `os/dist/check-package.sh -d <userland>/dist/packages'
# judges them.  Every kind carries this repository's LICENSE, which is the text
# both regimes in os/dist/licences.tab resolve to.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)			# os/hostbuild
OS=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$OS/.." && pwd)
LISTS="$OS/dist/lists"
MANDIR="$OS/man"
DRV="$HERE/build/drv"
SRCMAP="$HERE/build/.ulsrcmap"
OUT="$HERE/build/packages"

die() { echo "pack-component.sh: $*" >&2; exit 1; }
# A list or table with its comments removed, so awk sees only records.
records() { sed 's/#.*//' "$1"; }

# ---- the components: every list with a `package' line ----
# Columns: component, kinds (`bin' first, then the `kinds' line), role, list.
components() {
	for l in "$LISTS"/*.list; do
		[ -f "$l" ] || continue
		records "$l" | awk -v list="lists/$(basename "$l")" '
			$1=="package" { p=$2 }
			$1=="role"    { r=$2 }
			$1=="kinds"   { for (i=2; i<=NF; i++) k=k","$i }
			END { if (p != "") printf "%-16s %-14s %-10s %s\n", p, "bin"k, r, list }'
	done
	echo "-- $(components_count) component(s)"
}
components_count() {
	n=0
	for l in "$LISTS"/*.list; do
		[ -f "$l" ] || continue
		records "$l" | awk '$1=="package"{f=1} END{exit !f}' && n=$((n+1))
	done
	echo $n
}

while [ $# -gt 0 ]; do
	case "$1" in
	-o) OUT=$2; shift 2 ;;
	-h|--help) sed -n '2,14p' "$0"; exit 0 ;;
	--) shift; break ;;
	-*) die "unknown option $1" ;;
	*) break ;;
	esac
done
[ $# -ge 1 ] || { sed -n '2,14p' "$0" >&2; exit 2; }
if [ "$1" = components ]; then components; exit 0; fi

if [ $# -ge 2 ]; then
	COMP=$1; KIND=$2
else
	# `console-hr-src' splits at the LAST hyphen, and only when the tail is a
	# kind; `console-hr' itself is a component and means its bin kind.
	case "$1" in
	*-bin) COMP=${1%-bin}; KIND=bin ;;
	*-src) COMP=${1%-src}; KIND=src ;;
	*-man) COMP=${1%-man}; KIND=man ;;
	*)     COMP=$1;        KIND=bin ;;
	esac
fi
case "$KIND" in bin|src|man) ;; *) die "no kind \`$KIND' (bin|src|man)" ;; esac

# ---- the list, and what its head declares ----
LIST=
for l in "$LISTS"/*.list; do
	[ -f "$l" ] || continue
	if records "$l" | awk -v c="$COMP" '$1=="package" && $2==c {f=1} END{exit !f}'; then
		LIST=$l; break
	fi
done
[ -n "$LIST" ] || die "no component \`$COMP' (pack-component.sh components lists them)"
KINDS="bin $(records "$LIST" | awk '$1=="kinds" {for (i=2; i<=NF; i++) printf "%s ", $i}')"
case " $KINDS " in
*" $KIND "*) ;;
*) die "component \`$COMP' does not publish a \`$KIND' package.  Its kinds are
  $KINDS; add \`$KIND' to the \`kinds' line of $(basename "$LIST") to change that." ;;
esac

# The entries.  This repository's components install files and nothing else,
# so the only entry type read is `f'; anything else in a list here is a list
# this packer was not written for, and is refused rather than skipped.
ENTRIES=$(records "$LIST" | awk -v list="$(basename "$LIST")" '
	$1=="f" { print $2, $3, $4, $5; next }
	$1=="d"||$1=="e"||$1=="l"||$1=="t"||$1=="include"||$1=="require" {
		print "pack-component.sh: " list ": `" $1 "\047 entries are not handled here" > "/dev/stderr"
		bad=1 }
	END { exit bad }') || exit 1
[ -n "$ENTRIES" ] || die "$(basename "$LIST") installs no file"

# mode_x <octal mode>: does the mode carry an execute bit?  (Every octal digit
# with the x bit is odd.)
mode_x() { case "$1" in *[1357]*) return 0 ;; *) return 1 ;; esac; }

[ -f "$DRV/.drvstamp" ] || die "no $DRV/.drvstamp -- the drivers are not built, or not all of them.
  Run \`make drivers'.  A package cut from a half-built build/drv would name a
  kernel link id its bytes do not all come from."
. "$HERE/provenance.sh"
LINKID=$(prov_get "$DRV/.drvstamp" kernel_linkid)
[ -n "$LINKID" ] || die "$DRV/.drvstamp carries no kernel_linkid"

V=$(sh "$HERE/version.sh")
[ -n "$V" ] || die "no version -- see hostbuild/version.sh"
NAME="c900-$COMP-$KIND-v$V"

W=$(mktemp -d "${TMPDIR:-/tmp}/packcomp.XXXXXX")
trap 'rm -rf "$W"' EXIT INT TERM
T="$W/$NAME"
mkdir -p "$T/files"
: > "$T/manifest.tab"
nf=0

# put <path> <mode> <source file>: one manifest row and its payload under
# files/.  manifest.tab is the installation instruction -- `<type> <path>
# <mode> <uid> <gid>', the list format's own spelling -- because a tar archive
# cannot carry a COHERENT uid/gid through an unpack on somebody's Linux box.  A
# bin package's paths are the TARGET paths (/drv/hrtty), because that is what
# installing it means; a man package's are `<section>/<page>' as man.index
# spells them; a src package's are relative to os/.
#
# The staged payload carries the row's mode, and the archive the row's owner, so
# what is unpacked is what the manifest says -- and the cut is judged on exactly
# that before it leaves (see the end of this script).
OWN_UID=0
OWN_GID=1
put() {
	printf 'f %s %s %s %s\n' "$1" "$2" "$OWN_UID" "$OWN_GID" >> "$T/manifest.tab"
	mkdir -p "$T/files/$(dirname "${1#/}")"
	cp -p "$3" "$T/files/${1#/}"
	chmod "$2" "$T/files/${1#/}"
	nf=$((nf + 1))
}

# ---- the programs: every entry with an execute bit, by the name it installs ----
PROGS=
while read -r dest mode uid gid; do
	[ -n "$dest" ] || continue
	name=$(basename "$dest")
	src="$DRV/$name"
	[ -f "$src" ] || die "$COMP: $dest resolves to nothing ($src) -- run \`make drivers'"
	[ -n "$mode" ] || { if [ -x "$src" ]; then mode=755; else mode=644; fi; }
	mode_x "$mode" && PROGS="$PROGS $name"
	[ "$KIND" = bin ] && put "$dest" "$mode" "$src"
done <<EOF2
$ENTRIES
EOF2
NPROG=$(echo $PROGS | wc -w)

case "$KIND" in
src)
	[ -f "$SRCMAP" ] || die "$COMP-src: no source map at $SRCMAP.
  build-drivers.sh writes it as it links the drivers (\`make drivers'); a -src
  package may not be assembled from a guess about which sources built what."
	RELS=; MISSING=; NMAP=0
	for p in $PROGS; do
		rows=$(records "$SRCMAP" | awk -v p="$p" '$1==p {for (i=2; i<=NF; i++) print $i}')
		[ -n "$rows" ] || { MISSING="$MISSING $p"; continue; }
		NMAP=$((NMAP + 1))
		for r in $rows; do
			case " $RELS " in *" $r "*) ;; *) RELS="$RELS $r" ;; esac
		done
	done
	[ -z "$MISSING" ] || die "$COMP-src: not in $SRCMAP:$MISSING
  Every executable this component installs must have its corresponding source
  named, or the package understates what was shipped."
	for r in $RELS; do
		s="$OS/$r"
		[ -e "$s" ] || die "$COMP-src: $SRCMAP names $r and os/ has no such path"
		if [ -d "$s" ]; then
			# A source ROOT travels whole: licence text and makefile with it.
			for f in $(cd "$OS" && find "$r" -type f | LC_ALL=C sort); do
				if [ -x "$OS/$f" ]; then m=755; else m=644; fi
				put "$f" "$m" "$OS/$f"
			done
		else
			if [ -x "$s" ]; then m=755; else m=644; fi
			put "$r" "$m" "$s"
		fi
	done
	;;
man)
	IDX="$MANDIR/man.index"
	[ -f "$IDX" ] || die "$COMP-man: no $IDX"
	# The pages: a program's own row, and the rows of the articles declared
	# for it.  An `articles' line naming an article the index has no row for
	# is a claim about the manual that the manual does not make.
	PAGES=; NDOC=0; ARTS=
	for p in $PROGS; do
		arts=$(records "$LIST" | awk -v p="$p" '$1=="articles" && $2==p {for (i=3; i<=NF; i++) print $i}')
		found=
		for n in $p $arts; do
			rows=$(awk -F'\t' -v n="$n" '$2==n {print $1}' "$IDX")
			if [ -z "$rows" ]; then
				[ "$n" = "$p" ] && continue
				die "$COMP-man: $(basename "$LIST") says $p is documented by \`$n' and $IDX has no such article"
			fi
			found=1
			for pg in $rows; do
				case " $PAGES " in *" $pg "*) ;; *) PAGES="$PAGES $pg" ;; esac
			done
		done
		[ -n "$found" ] && NDOC=$((NDOC + 1))
		for a in $arts; do
			case ",$ARTS," in *",$a,"*) ;; *) ARTS="${ARTS:+$ARTS,}$a" ;; esac
		done
	done
	[ -n "$PAGES" ] || die "$COMP-man: not one of this component's $NPROG program(s) has a page,
  so there is no manual package to cut -- an empty archive that unpacked and
  passed would read as coverage.  Owed:$PROGS"
	for pg in $PAGES; do
		[ -f "$MANDIR/$pg" ] || die "$COMP-man: man.index names $pg and man/ has no such page"
		put "$pg" 644 "$MANDIR/$pg"
	done
	# man.index: GENERATED with exactly the rows for the pages packed.  /bin/man
	# scans it a line at a time and cannot reach a page that has no row.
	printf 'f man.index 644 %s %s\n' "$OWN_UID" "$OWN_GID" >> "$T/manifest.tab"
	awk 'NR==FNR { if ($1=="f") want[$2]=1; next }
	         { split($0, r, "\t"); if (r[1] in want) print }' \
	    "$T/manifest.tab" "$IDX" > "$T/files/man.index"
	[ -s "$T/files/man.index" ] || die "$COMP-man: generated man.index is empty"
	chmod 644 "$T/files/man.index"
	nf=$((nf + 1))
	;;
esac

# ---- the package's own furniture ----
echo "$V" > "$T/VERSION"
cp "$ROOT/LICENSE" "$T/LICENSE"

EXTRA="version=$V package=$COMP-$KIND component=$COMP pkgkind=$KIND entries=$nf"
case "$KIND" in
# WHICH KERNEL THESE BYTES ARE BOUND TO.  `ld -k' bakes the kernel's absolute
# addresses into a driver, so the kernel link id is the set a driver package
# was selected from, as `ulid' is for a package of the userland's sweep.
bin) EXTRA="$EXTRA kernel_linkid=$LINKID" ;;
src) EXTRA="$EXTRA programs=$NPROG mapped=$NMAP" ;;
man) EXTRA="$EXTRA programs=$NPROG documented=$NDOC libraries=- articles=${ARTS:--}" ;;
esac
# shellcheck disable=SC2086
prov_write "$T/.provenance" component os/dist os/hostbuild/build-drivers.sh \
	-- $EXTRA "contentid=pending"

# ---- which bytes.  .contents over every other file, contentid its sha1. ----
( cd "$T" && find . -type f ! -path ./.contents ! -path ./.provenance -print |
  LC_ALL=C sort | sed 's|^\./||' | xargs md5sum ) > "$T/.contents"
CID=$(sha1sum "$T/.contents" | cut -c1-12)
sed "s|^contentid=.*|contentid=$CID|" "$T/.provenance" > "$T/.provenance.new"
mv -f "$T/.provenance.new" "$T/.provenance"

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
# Sorted members, numeric owners, gzip -n for a deterministic checksum.
( cd "$W" && find "$NAME" -print | LC_ALL=C sort |
  tar czf "$OUT/$NAME.tar.gz" --numeric-owner --owner="$OWN_UID" --group="$OWN_GID" \
      --mtime="@0" --no-recursion -T - ) 2>/dev/null ||
( cd "$W" && tar czf "$OUT/$NAME.tar.gz" "$NAME" )

# ---- the cut is judged before it leaves ----
# The archive just written is unpacked and read back against what it says it
# carries: the format every component-kind declaration asserts (one top-level
# directory named for the archive, VERSION, the .provenance keys, no link that
# dangles or leaves the package, contentid over .contents), .contents against
# every file present in both directions, and manifest.tab against files/ -- each
# row staged, at its mode and owner as the archive records them, and every staged
# file with a row.  A failure removes the archive, so a package that does not
# match its own manifest never leaves this repository.
nbad=0
refuse() { echo "pack-component.sh: $NAME: $*" >&2; nbad=$((nbad + 1)); }
CUT="$W/cut"
mkdir -p "$CUT"
tar xzf "$OUT/$NAME.tar.gz" -C "$CUT" ||
	{ rm -f "$OUT/$NAME.tar.gz"; die "$NAME: archive.unpack: the archive just written does not unpack"; }
tar tvzf "$OUT/$NAME.tar.gz" --numeric-owner > "$W/members"
tops=$(ls -A "$CUT")
[ "$tops" = "$NAME" ] && [ -d "$CUT/$NAME" ] ||
	{ rm -f "$OUT/$NAME.tar.gz"; die "$NAME: archive.toplevel: holds \`$(echo $tops)', not the one directory $NAME"; }
U="$CUT/$NAME"
UA=$(cd "$U" && pwd -P)

[ "$(sed -n 1p "$U/VERSION" 2>/dev/null)" = "$V" ] ||
	refuse "version.match: VERSION says \`$(sed -n 1p "$U/VERSION" 2>/dev/null)', the name says \`$V'"
KEYS="kind commit version package component pkgkind entries contentid"
case "$KIND" in
bin) KEYS="$KEYS kernel_linkid" ;;
src) KEYS="$KEYS programs mapped" ;;
man) KEYS="$KEYS programs documented libraries" ;;
esac
for k in $KEYS; do
	[ -n "$(prov_get "$U/.provenance" "$k")" ] || refuse "stamp.keys: .provenance carries no $k"
done
for kv in "package=$COMP-$KIND" "pkgkind=$KIND" "component=$COMP"; do
	grep -qx "$kv" "$U/.provenance" || refuse "stamp.text: .provenance has no line $kv"
done
[ "$KIND" != src ] || [ "$(prov_get "$U/.provenance" programs)" = "$(prov_get "$U/.provenance" mapped)" ] ||
	refuse "stamp.pair: .provenance programs and mapped disagree"
[ -s "$U/LICENSE" ] || refuse "file.present: LICENSE is missing or empty"
[ -s "$U/manifest.tab" ] || refuse "file.present: manifest.tab is missing or empty"
[ -n "$(ls -A "$U/files" 2>/dev/null)" ] || refuse "dir.present: files/ is missing or empty"
case "$KIND" in
bin)	grep -q '^f /' "$U/manifest.tab" || refuse "text.match: manifest.tab installs no absolute path" ;;
man)	[ -s "$U/files/man.index" ] || refuse "file.present: files/man.index is missing or empty"
	grep -q '^COHERENT[.]' "$U/files/man.index" 2>/dev/null ||
		refuse "text.match: files/man.index has no COHERENT. row"
	grep -q '^f COHERENT[.]' "$U/manifest.tab" || refuse "text.match: manifest.tab carries no page" ;;
esac

for l in $(cd "$U" && find . -type l | LC_ALL=C sort); do
	t=$(readlink "$U/$l")
	case "$t" in
	/*) refuse "path.escape: $l -> $t is absolute" ;;
	*)  r=$(cd "$U/$(dirname "$l")" && realpath -m "$t")
	    case "$r" in "$UA"|"$UA"/*) ;; *) refuse "path.escape: $l -> $t leaves the package" ;; esac ;;
	esac
	[ -e "$U/$l" ] || refuse "link.dangling: $l -> $t resolves to nothing"
done

[ "$(sha1sum "$U/.contents" | cut -c1-12)" = "$(prov_get "$U/.provenance" contentid)" ] ||
	refuse "content.id: .contents is not the file .provenance:contentid names"
(cd "$U" && md5sum --quiet -c .contents) > "$W/md5" 2>&1 ||
	refuse "contents.md5: $(grep -v '^md5sum:' "$W/md5" | head -1)"
sed 's/^[0-9a-f]*  //' "$U/.contents" | LC_ALL=C sort > "$W/listed"
(cd "$U" && find . -type f ! -path ./.contents ! -path ./.provenance | sed 's|^\./||' |
	LC_ALL=C sort) > "$W/present"
for f in $(LC_ALL=C comm -13 "$W/listed" "$W/present"); do
	refuse "contents.complete: $f is in the package and .contents does not list it"
done
for f in $(LC_ALL=C comm -23 "$W/listed" "$W/present"); do
	refuse "contents.complete: .contents lists $f and the package does not carry it"
done

# The archive's own record of each member's mode and owner, as a consumer's tar
# will read it: `<octal mode> <uid>/<gid> <path under files/>'.
awk -v top="$NAME/files/" '
	function bits(s, r, w, x) { return (r=="r")*4 + (w=="w")*2 + (x ~ /[xst]/) }
	function spec(s) { return (substr(s,4,1) ~ /[sS]/)*4 + (substr(s,7,1) ~ /[sS]/)*2 + (substr(s,10,1) ~ /[tT]/) }
	substr($1,1,1)=="-" && index($6, top)==1 {
		m = bits(substr($1,2,3), substr($1,2,1), substr($1,3,1), substr($1,4,1)) "" \
		    bits(substr($1,5,3), substr($1,5,1), substr($1,6,1), substr($1,7,1)) "" \
		    bits(substr($1,8,3), substr($1,8,1), substr($1,9,1), substr($1,10,1))
		if (spec($1)) m = spec($1) m
		print m, $2, substr($6, length(top)+1)
	}' "$W/members" > "$W/modes"
: > "$W/rows"
while read -r typ path mode uid gid tgt; do
	[ -n "$typ" ] || continue
	case "$typ" in
	f)	p=${path#/}
		echo "$p" >> "$W/rows"
		if [ ! -f "$U/files/$p" ]; then
			refuse "manifest.staged: manifest.tab declares $path and files/$p is not in the archive"
			continue
		fi
		want=$(echo "$mode" | sed 's/^0*//')
		got=$(awk -v p="$p" '$3==p {print $1, $2}' "$W/modes")
		[ "${got% *}" = "$want" ] ||
			refuse "manifest.mode: $path is mode ${got% *} in the archive, manifest.tab says $mode"
		[ "${got#* }" = "$uid/$gid" ] ||
			refuse "manifest.owner: $path is owned ${got#* } in the archive, manifest.tab says $uid/$gid"
		;;
	*)	refuse "manifest.type: row \`$typ $path' is not a type this packer cuts" ;;
	esac
done < "$U/manifest.tab"
LC_ALL=C sort -o "$W/rows" "$W/rows"
(cd "$U/files" && find . -type f | sed 's|^\./||' | LC_ALL=C sort) > "$W/staged"
for f in $(LC_ALL=C comm -13 "$W/rows" "$W/staged"); do
	refuse "manifest.unlisted: files/$f is staged and manifest.tab has no row for it"
done

if [ "$nbad" -gt 0 ]; then
	rm -f "$OUT/$NAME.tar.gz"
	die "$NAME: $nbad assertion(s) failed against its own manifest; the archive was removed"
fi

echo "packed $OUT/$NAME.tar.gz -- $nf file(s), contentid $CID, judged against its manifest"
# end of pack-component.sh
