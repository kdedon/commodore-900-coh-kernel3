# toolchain.sh -- resolve the Z8001 cross toolchain. SOURCE only; do not exec.
# Sets $TC, $TCB, $TCINC, $TCID, $C900_TC_SHAPE, exports $COHERENT_OS.
# Two shapes: checkout (full, with build harnesses) or release (binary archive).
# mk/deps.sh does the search (DEPS pinned); $C900_TOOLCHAIN unset = default search.
# Requires $OS to name the os/ tree.
if [ -z "${OS:-}" ] || [ ! -d "$OS/hostbuild" ]; then
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
# $TCINC: the toolchain's system headers -- the C library's, the object
# formats', and the COHERENT interfaces the kernel and the userland compile
# against alike.  A checkout spells them src/include, an unpacked release
# usr/include; both shapes answer here so a build never learns which it got.
# On the kernel's -I path this comes AFTER $OS/include, so a kernel-produced
# header wins, and BEFORE $OS/sys/z8001/h, whose copies are 0.7.3-era and are
# meant to stay unread.
for _c9i in "$C900_TOOLCHAIN/src/include" "$C900_TOOLCHAIN/usr/include"; do
	[ -d "$_c9i" ] && { TCINC="$_c9i"; break; }
done
if [ -z "${TCINC:-}" ]; then
	echo "toolchain.sh: no system headers in $C900_TOOLCHAIN" >&2
	echo "  looked for src/include (checkout) and usr/include (release)." >&2
	exit 2
fi
unset _c9i
COHERENT_OS=$(cd "$OS" && pwd)
# $(TCID): the compiler's source id, alone in a file, for the makefiles to
# depend on -- the shell equivalent of toolchain.mk's, naming the same path.
TCID="$COHERENT_OS/hostbuild/build/.tcid"
# Report once per build; the same guard records which compiler is in use.
if [ -z "${C900_TC_REPORTED:-}" ]; then
	echo "toolchain: $C900_TC_SHAPE at C900_TOOLCHAIN=$C900_TOOLCHAIN" >&2
	[ -n "${C900_TC_BUILD:-}" ] &&
		echo "toolchain: build dir C900_TC_BUILD=$TCB (not the shared $TC/build)" >&2
	. "$OS/hostbuild/provenance.sh"
	prov_tc_record "$TCB" "$COHERENT_OS/hostbuild/build/.tcstamp" "$TCID" "$C900_TC_SHAPE"
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
export TCINC TCID
export C900_TOOLCHAIN COHERENT_OS C900_TC_SHAPE C900_TC_REPORTED C900_TC_BUILD
unset _c9root _c9deps _c9tc
# end of toolchain.sh
