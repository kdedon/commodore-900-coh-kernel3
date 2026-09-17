# kboot.sh -- resolve what a kernel build consumes from kboot. SOURCE this
# file; do not exec. kboot.mk is the make equivalent, searching the same path.
#
# Sets $KB and defines bistage(). Requires $OS to name the os/ tree.
# mk/deps.sh does the search (DEPS pinned): deps/ for the release, then a
# checkout beside this repository, then repos/.  $C900_KBOOT unset = default
# search; a value that does not resolve is refused as itself.
# include/bootinfo.h (loader->kernel handoff) is the kernel's only compile
# dependency on kboot -- the whole of what a release asset needs to carry.
if [ -z "${OS:-}" ] || [ ! -d "$OS/include" ]; then
	echo "kboot.sh: \$OS must name the os/ tree before sourcing" >&2
	exit 2
fi
_kbroot=$(cd "$OS/.." && pwd)
_kbdeps="$_kbroot/mk/deps.sh"
_kbfound=$(C900_KBOOT="${C900_KBOOT:-}" sh "$_kbdeps" kboot)
if [ -z "$_kbfound" ]; then
	C900_KBOOT="${C900_KBOOT:-}" sh "$_kbdeps" -n kboot "${C900_KBOOT:-}"
	exit 2
fi
C900_KBOOT=$(cd "$_kbfound" && pwd)
KB="$C900_KBOOT"
export C900_KBOOT
C900_KB_SHAPE=$(C900_KBOOT="$C900_KBOOT" sh "$_kbdeps" -k kboot)
if [ -z "${C900_KB_REPORTED:-}" ]; then
	echo "kboot: $C900_KB_SHAPE at C900_KBOOT=$C900_KBOOT" >&2
	C900_KB_REPORTED=1
fi
export C900_KB_SHAPE C900_KB_REPORTED
unset _kbroot _kbdeps _kbfound

# bistage <dir> -- stage bootinfo.h to <dir>/sys/bootinfo.h (to avoid shadowing).
bistage() {
	mkdir -p "$1/sys" &&
	cp -f "$KB/include/bootinfo.h" "$1/sys/bootinfo.h"
}
# end of kboot.sh
