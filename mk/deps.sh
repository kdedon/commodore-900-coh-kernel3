#!/bin/sh
# deps.sh -- resolve dependencies (toolchain, kboot) or refuse by name.
#
#   sh mk/deps.sh <dep>              print the resolved path, or nothing
#   sh mk/deps.sh -k <dep>           print the SHAPE the path resolved to
#   sh mk/deps.sh -n <dep> [value]   refuse and exit 2 if value does not resolve
#
#   dep         variable          what it names
#   toolchain   C900_TOOLCHAIN    the Z8001 cross toolchain
#   kboot       C900_KBOOT        the kboot loader (include/bootinfo.h)
#
# Search order: the variable wins; then a sibling checkout (at most three parents),
# then repos/. Three parents reaches the enclosing workspace from <workspace>/repos/<repo>.

root=$(cd "$(dirname "$0")/.." && pwd)

# Generate the sibling search list for a repository name: three parents, then repos/.
siblings() {
	_d=$root
	_n=0
	while [ $_n -lt 3 ] && [ "$_d" != / ]; do
		_d=$(cd "$_d/.." && pwd)
		echo "$_d/$1"
		_n=$((_n + 1))
	done
	echo "$root/repos/$1"
}

# Per dep: VAR, WANT, LIST, ok(), fixup(), shape(), and HOW are set below.
# shape() is overridden for toolchain (two shapes: checkout or release X.Y.Z).
shape() { echo checkout; }
case "$1" in
-n) mode=need; dep=$2; given=$3 ;;
-k) mode=kind; dep=$2; given= ;;
*)  mode=find; dep=$1; given= ;;
esac

case "$dep" in
toolchain)
	VAR="C900_TOOLCHAIN"
	WANT="the Z8001 cross toolchain"
	LIST="$root/deps/commodore-900-toolchain $(siblings commodore-900-toolchain)"
	[ -n "$given" ] || given=${C900_TOOLCHAIN:-}
	fixup() { echo "$1"; }
	# Two shapes: source checkout or unpacked release (both have host/ccz).
	ok() { [ -f "$1/host/ccz" ]; }
	shape() {
		if [ -f "$1/host/build-cc.sh" ]; then
			echo checkout
		elif [ -f "$1/bin/ccz" ] && [ -f "$1/VERSION" ]; then
			echo "release $(sed -n 1p "$1/VERSION")"
		else
			echo unknown
		fi
	}
	HOW="  The toolchain is a repository of its own, and DEPS pins one of its
  RELEASES -- a checkout of it builds a compiler but not the five libc
  objects the kernel links by name, which come from the OS's libc:
      make deps DEP=toolchain
  or set C900_TOOLCHAIN= to a checkout or release archive (with host/ccz)."
	;;
kboot)
	VAR="C900_KBOOT"
	WANT="a kboot checkout"
	LIST=$(siblings commodore-900-kboot)
	[ -n "$given" ] || given=${C900_KBOOT:-}
	fixup() { echo "$1"; }
	# include/bootinfo.h is the loader->kernel handoff contract.
	ok() { [ -f "$1/include/bootinfo.h" ]; }
	HOW="  kboot is the multiboot loader, a repository of its own.  It owns
  <sys/bootinfo.h>, which the wd(4) driver compiles, so a KERNEL build needs
  the checkout (a userland or a stock image does not):
      git clone <...>/commodore-900-kboot
  or set C900_KBOOT= to a checkout."
	;;
*)
	echo "deps.sh: unknown dependency \`$dep' (toolchain, kboot)" >&2
	exit 2
	;;
esac

found=
if [ -n "$given" ]; then
	given=$(fixup "$given")
	ok "$given" && found=$given
else
	for c in $LIST; do
		c=$(fixup "$c")
		ok "$c" && { found=$c; break; }
	done
fi

if [ -n "$found" ]; then
	case "$mode" in
	find) echo "$found" ;;
	kind) shape "$found" ;;
	esac
	exit 0
fi

case "$mode" in find|kind) exit 0 ;; esac

{
	if [ -n "$given" ]; then
		echo "*** $WANT: nothing usable at $VAR=$given."
		echo "*** That is $VAR's own value, so nothing else was tried."
		echo "*** Unset it to search these instead:"
	else
		echo "*** $WANT: none found, and this target needs one."
		echo "*** $VAR is unset; the paths tried were:"
	fi
	for c in $LIST; do echo "***     $c"; done
	echo "$HOW" | sed 's/^/*** /'
} >&2
exit 2
