#!/bin/sh
# version.sh -- print the release version.
#
# The git tag is the source.  A release is tagged `v<major>.<minor>.<patch>':
#
#	3.5.0			built at the tag
#	3.5.0-4-g1a2b3c4	four commits past it
#	3.5.0-4-g1a2b3c4-dirty	...with the tree modified
#
# So an image built off a working tree says so in its own banner, and cannot
# be mistaken for the release it was built near.
#
# Outside a checkout -- an unpacked source archive -- `git archive' has already
# substituted the tag into ARCHIVE below (.gitattributes marks this file
# export-subst), so the answer travels with the archive.
#
# Exits 1 printing nothing when neither is available: a build that cannot say
# what it is must not stamp an image with a guess.

ARCHIVE='$Format:%(describe:tags=v*)$'

here=$(dirname "$0")
v=$(git -C "$here" describe --tags --match 'v*' --dirty 2>/dev/null) || v=

if [ -z "$v" ]; then
	case $ARCHIVE in
	*Format:*)	;;			# unsubstituted: not an archive
	*)		v=$ARCHIVE ;;
	esac
fi

if [ -z "$v" ]; then
	echo "version.sh: no version -- this is not a git checkout with a" >&2
	echo "  v<major>.<minor>.<patch> tag, and not an exported archive." >&2
	echo "  Tag the release (git tag -a v3.5.0 -m 3.5.0) and rebuild." >&2
	exit 1
fi

echo "${v#v}"
