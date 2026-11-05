#!/usr/bin/bash
# SPDX-License-Identifier: BSD-2-Clause
#
# build-rpm.sh - build the RPMs from this tree.
#
#   packaging/build-rpm.sh [topdir]                    default topdir: ./rpmbuild
#   packaging/build-rpm.sh --with-vendored [topdir]    also build pidp10-vendored
#   packaging/build-rpm.sh --worktree [topdir]         package the work tree
#   packaging/build-rpm.sh --srpm-only [topdir]        just the SRPM (for COPR)
#
# By default the source tarballs are `git archive` of HEAD: what ships is what
# is committed, which is the only version of the project that is reproducible
# from the repository alone. If the work tree is dirty that is said out loud,
# because the alternative - silently building an RPM nobody can rebuild - is how
# a package ends up containing a fix that exists in one person's checkout.
#
# --worktree packages the tree as it stands instead (tracked modifications and
# untracked files, minus what .gitignore excludes). For testing a change before
# committing it. Not for anything that leaves the machine.
#
# Source0 never contains bin/, the 46 MB of prebuilt AI Lab peripheral
# simulators that have no source in this tree, so the tarball a distribution
# builds from has no unreviewed ELF in it. --with-vendored additionally
# produces pidp10-vendored-<version>.tar.gz (Source2) and builds the subpackage
# that installs them, for a cabinet.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
cd "$root"

vendored=0
worktree=0
srpm_only=0
while [ "${1:-}" = "--with-vendored" ] || [ "${1:-}" = "--worktree" ] || \
      [ "${1:-}" = "--srpm-only" ]; do
	case "$1" in
	    --with-vendored) vendored=1 ;;
	    --worktree)      worktree=1 ;;
	    --srpm-only)     srpm_only=1 ;;
	esac
	shift
done
top=$(realpath -m "${1:-rpmbuild}")

version=$(sed -n 's/^%global simh_version \([0-9.]*\).*/\1/p' "$here/pidp10.spec" | head -1)
[ -n "$version" ] || { echo "build-rpm: cannot read simh_version from the spec" >&2; exit 1; }
name=pidp10
topdir_prefix=$name-$version

rev=$(git rev-parse --short HEAD)
date=$(git show -s --format=%cd --date=format:%Y%m%d HEAD)

# make_tarball DEST PREFIX PATHSPEC...
#   PREFIX "" means the archive holds bare relative paths (%prep unpacks those
#   into an existing directory rather than creating a source directory).
make_tarball()
{
	local dest=$1 prefix=$2; shift 2

	if [ "$worktree" = 1 ]; then
		# git ls-files keeps .gitignore in force, so build/, rpmbuild/ and the
		# stray objects under src/ stay out the same way they do for git.
		# (tar has no --prefix; --transform with the S flag is what rewrites
		# the name of a file *and* of what a symlink points at.)
		git ls-files -z --cached --others --exclude-standard -- "$@" |
		    tar --format=gnu --null ${prefix:+--transform="s|^|$prefix/|S"} -cf - -T - |
		    gzip > "$dest"
	else
		git archive --format=tar ${prefix:+--prefix="$prefix/"} HEAD -- "$@" |
		    gzip > "$dest"
	fi
}

if [ "$worktree" = 1 ]; then
	echo "build-rpm: packaging the WORK TREE (uncommitted changes included;"
	echo "build-rpm: nobody but you can rebuild this)" >&2
elif [ -n "$(git status --porcelain)" ]; then
	echo "build-rpm: WARNING: work tree is dirty; HEAD is being packaged and" >&2
	echo "build-rpm: your uncommitted work is NOT in it. --worktree to include it." >&2
	git status --short | head -20 >&2
fi

mkdir -p "$top"/{SOURCES,BUILD,RPMS,SRPMS}

# Source0: everything except the prebuilt peripherals. The prefix must match
# %{name}-%{version} or %autosetup -n fails.
make_tarball "$top/SOURCES/$topdir_prefix.tar.gz" "$topdir_prefix" . ':!bin'

if [ "$vendored" = 1 ]; then
	# No prefix: %prep unpacks this straight into bin/ of the source tree, so
	# the Makefile's WITH_VENDORED path needs no extra variable.
	make_tarball "$top/SOURCES/$name-vendored-$version.tar.gz" "" bin
	with=--with=vendored
else
	with=--without=vendored
fi
cp install/fedora/sysusers.d/30-pidp10.conf "$top/SOURCES/30-pidp10.conf"

rpmbuild -b$([ "$srpm_only" = 1 ] && echo s || echo a) --define "_topdir $top" \
    --define "gitrev $rev" --define "gitdate $date" \
    "$with" "$here/pidp10.spec"

if [ "$srpm_only" = 1 ]; then
	ls -1 "$top"/SRPMS/*.src.rpm
else
	echo "RPMs: $top/RPMS"
fi
