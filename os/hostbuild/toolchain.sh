# toolchain.sh -- resolve the Z8001 cross toolchain. SOURCE only; do not exec.
# Sets $TC, $TCB, $C900_TC_SHAPE, exports $COHERENT_OS.
# Two shapes: checkout (full, with build harnesses) or release (binary archive).
# mk/deps.sh does the search (DEPS pinned); $C900_TOOLCHAIN unset = default search.
# Requires $OS to name the os/ tree.
if [ -z "${OS:-}" ] || [ ! -d "$OS/include" ]; then
	echo "toolchain.sh: \$OS must name the os/ tree before sourcing" >&2
	exit 2
fi
_c9root=$(cd "$OS/.." && pwd)
_c9deps="$_c9root/mk/deps.sh"
_c9tc=$(C900_TOOLCHAIN="${C900_TOOLCHAIN:-}" sh "$_c9deps" toolchain)
if [ -z "$_c9tc" ]; then
	C900_TOOLCHAIN="${C900_TOOLCHAIN:-}" sh "$_c9deps" -n toolchain "${C900_TOOLCHAIN:-}"
	exit 2
fi
C900_TOOLCHAIN=$(cd "$_c9tc" && pwd)
C900_TC_SHAPE=$(C900_TOOLCHAIN="$C900_TOOLCHAIN" sh "$_c9deps" -k toolchain)
TC="$C900_TOOLCHAIN/host"
TCB="${C900_TC_BUILD:-$TC/build}"
COHERENT_OS=$(cd "$OS" && pwd)
# Report once per build; guard also carries toolchain identity check.
if [ -z "${C900_TC_REPORTED:-}" ]; then
	echo "toolchain: $C900_TC_SHAPE at C900_TOOLCHAIN=$C900_TOOLCHAIN" >&2
	[ -n "${C900_TC_BUILD:-}" ] &&
		echo "toolchain: build dir C900_TC_BUILD=$TCB (not the shared $TC/build)" >&2
	. "$OS/hostbuild/provenance.sh"
	prov_tc_check "$TCB" "$COHERENT_OS/hostbuild/build/.tcstamp" "$C900_TC_SHAPE" || exit 2
	C900_TC_REPORTED=1
fi
# c900_buildlog, for the harnesses that drive cc0/cc1/cc2 themselves rather
# than through ccz (which sources the same file).  It belongs to the toolchain
# because both repositories' scripts compile with it and the record has to read
# the same either way; a release older than the file, or the release host/ view
# that does not carry it, gets the no-op and behaves as before.
if [ -r "$TC/buildlog.sh" ]; then
	. "$TC/buildlog.sh"
else
	c900_buildlog() { :; }
fi
export C900_TOOLCHAIN COHERENT_OS C900_TC_SHAPE C900_TC_REPORTED C900_TC_BUILD
unset _c9root _c9deps _c9tc
# end of toolchain.sh
