# kboot.sh -- resolve kboot (multiboot loader). SOURCE this file; do not exec.
# kboot.mk is the make equivalent, searching the same paths.
#
# Sets $KB and defines bistage(). Requires $OS to name the os/ tree.
# $C900_KBOOT: the checkout; defaults to sibling or repos/ directory.
# include/bootinfo.h (loader->kernel handoff) is the kernel's compile dependency.
if [ -z "${OS:-}" ] || [ ! -d "$OS/include" ]; then
	echo "kboot.sh: \$OS must name the os/ tree before sourcing" >&2
	exit 2
fi
_kbroot=$(cd "$OS/.." && pwd)
_kbsearch="$_kbroot/../commodore-900-kboot $_kbroot/repos/commodore-900-kboot"
if [ -z "${C900_KBOOT:-}" ]; then
	for _kbtry in $_kbsearch; do
		[ -f "$_kbtry/include/bootinfo.h" ] || continue
		C900_KBOOT=$_kbtry
		break
	done
fi
: "${C900_KBOOT:=}"
if [ ! -f "$C900_KBOOT/include/bootinfo.h" ]; then
	echo "$(basename "$0"): no kboot checkout at C900_KBOOT=$C900_KBOOT" \
	     "(no include/bootinfo.h there).  Clone commodore-900-kboot to one" \
	     "of: $_kbsearch -- or set C900_KBOOT to a checkout.  It owns" \
	     "<sys/bootinfo.h>, which the wd(4) driver compiles." >&2
	exit 2
fi
C900_KBOOT=$(cd "$C900_KBOOT" && pwd)
KB="$C900_KBOOT"
export C900_KBOOT
unset _kbroot _kbsearch _kbtry

# bistage <dir> -- stage bootinfo.h to <dir>/sys/bootinfo.h (to avoid shadowing).
bistage() {
	mkdir -p "$1/sys" &&
	cp -f "$KB/include/bootinfo.h" "$1/sys/bootinfo.h"
}
# end of kboot.sh
