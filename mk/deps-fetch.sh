#!/bin/sh
# deps-fetch.sh -- acquire dependencies listed in DEPS (name kind url ref ...).
#
#   sh <dir>/deps-fetch.sh            place every dependency in DEPS
#   sh <dir>/deps-fetch.sh <name>     just that one
#
# DEPS format: `name kind url ref [asset] [dir]', one line per edge.
# kind=git: clones to ../<basename of url> on branch <ref>.
# kind=release: downloads a binary tag release, unpacked to deps/<dir>/.
# Assets use @REF@ for the tag and @HOST@ for the platform suffix (linux-x86_64.tar.gz or windows-x86_64.zip).
# <dir> defaults to the basename of the url.
#
# Idempotent: if a dependency already resolves (by variable, deps/, or path), it is left alone.
set -e

root=$(cd "$(dirname "$0")/.." && pwd)
here=$(cd "$(dirname "$0")" && pwd)
deps=$root/DEPS

[ -f "$deps" ] || { echo "deps-fetch.sh: no DEPS file at $deps" >&2; exit 2; }

only=${1:-}

# Consult the resolver (mk/deps.sh) to check if a dependency is already resolvable.
resolve() {
	[ -f "$here/deps.sh" ] || return 1
	_r=$(sh "$here/deps.sh" "$1" 2>/dev/null) || return 1
	[ -n "$_r" ] || return 1
	echo "$_r"
}

fetch_git() {
	# $1 name  $2 url  $3 ref  $4 dest
	if git -C "$4" rev-parse --git-dir >/dev/null 2>&1; then
		echo "$1: checkout already at $4 -- left alone"
		return 0
	fi
	if [ -e "$4" ]; then
		echo "$1: $4 exists and is not a git checkout -- left alone" >&2
		return 1
	fi
	echo "$1: cloning $2 ($3) -> $4"
	git clone --branch "$3" "$2" "$4" || return 1
}

fetch_release() {
	# $1 name  $2 url  $3 ref  $4 dest  $5 asset
	if [ -d "$4" ]; then
		echo "$1: $3 already unpacked at $4 -- left alone"
		return 0
	fi
	[ -n "$5" ] || { echo "$1: a release line needs an asset name" >&2; return 1; }
	# Resolve @HOST@ only for assets that use it.
	case "$5" in
	*@HOST@*)
		case $(uname -s) in
		Linux)			host=linux-x86_64.tar.gz ;;
		MINGW*|MSYS*|CYGWIN*)	host=windows-x86_64.zip ;;
		*)	echo "$1: the asset name is per-host (@HOST@) and none is" >&2
			echo "  published for $(uname -s);" >&2
			echo "  build the dependency and name it by variable." >&2
			return 1 ;;
		esac ;;
	*)	host= ;;
	esac
	asset=$(echo "$5" | sed "s/@REF@/$3/g; s/@HOST@/$host/g")
	from=$2/releases/download/$3/$asset
	tmp=$4.tmp.$$
	rm -rf "$tmp"
	mkdir -p "$tmp"
	echo "$1: downloading $from"
	if ! curl -fL --retry 2 -o "$tmp/$asset" "$from"; then
		rm -rf "$tmp"
		echo "$1: no release asset at $from" >&2
		echo "  The tag in DEPS is the pin: it is deliberate and bumped by hand," >&2
		echo "  so a missing one means that release has not been published yet." >&2
		echo "  Until it is, build the dependency yourself and name it by variable;" >&2
		echo "  the resolver's refusal says which variable." >&2
		return 1
	fi
	case "$asset" in
	*.tar.gz|*.tgz) tar xzf "$tmp/$asset" -C "$tmp" ;;
	*.zip)          unzip -q "$tmp/$asset" -d "$tmp" ;;
	*) echo "$1: don't know how to unpack $asset" >&2; rm -rf "$tmp"; return 1 ;;
	esac
	rm -f "$tmp/$asset"
	# The asset carries one top directory (bin/, rom/, disk/ inside it); it is
	# stripped so deps/<name>/bin/c900 is the path the resolvers search for.
	inner=
	for d in "$tmp"/*; do
		[ -d "$d" ] || { inner=; break; }
		[ -z "$inner" ] || { inner=; break; }
		inner=$d
	done
	mkdir -p "$(dirname "$4")"
	if [ -n "$inner" ]; then mv "$inner" "$4"; rm -rf "$tmp"; else mv "$tmp" "$4"; fi
	echo "$1: unpacked $3 -> $4"
}

rc=0
# Read DEPS on fd 3 so git/curl inherit stdin without consuming the rest of the file.
while read -r name kind url ref asset dir <&3; do
	case "$name" in ''|\#*) continue ;; esac
	[ -z "$only" ] || [ "$only" = "$name" ] || continue
	got=$(resolve "$name") || got=
	if [ -n "$got" ]; then
		echo "$name: already resolves to $got"
		continue
	fi
	[ -n "$dir" ] || dir=$(basename "$url" .git)
	case "$dir" in
	*/*|..|.)	echo "$name: dir \`$dir' in DEPS is a path, not a name" >&2
			rc=1; continue ;;
	esac
	case "$kind" in
	git)     fetch_git "$name" "$url" "$ref" "$(cd "$root/.." && pwd)/$dir" || rc=1 ;;
	release) fetch_release "$name" "$url" "$ref" "$root/deps/$dir" "$asset" || rc=1 ;;
	*)       echo "$name: unknown kind \`$kind' in DEPS" >&2; rc=1 ;;
	esac
	got=$(resolve "$name") || got=
	[ -n "$got" ] && echo "$name: resolves to $got" || :
done 3< "$deps"

exit $rc
