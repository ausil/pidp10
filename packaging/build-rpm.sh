#!/usr/bin/bash
# SPDX-License-Identifier: BSD-2-Clause
#
# build-rpm.sh - build the RPMs from this tree.
#
#   packaging/build-rpm.sh [topdir]                    default topdir: ./dist-rpm
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
# Source0 is the whole tree. Nothing in it is a prebuilt binary: the AI Lab
# device simulators the boot scripts call (cbridge, tv11, tvcon, imlac, ...) are
# separate programs with their own upstreams and their own packages, picked up
# off PATH, so there is no blob tarball, no second Source and no --with switch
# to opt into shipping someone's ELF.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
cd "$root"

worktree=0
srpm_only=0
while [ "${1:-}" = "--worktree" ] || [ "${1:-}" = "--srpm-only" ]; do
	case "$1" in
	    --worktree)  worktree=1 ;;
	    --srpm-only) srpm_only=1 ;;
	esac
	shift
done
top=$(realpath -m "${1:-dist-rpm}")

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
		# git ls-files keeps .gitignore in force, so build/, dist-rpm/ and
		# the stray objects under src/ stay out the same way they do for git.
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

# Two directories: the sources go to dist/ (the _sourcedir), the products
# land flat in $top. No _topdir is set, so no tree of SOURCES/BUILD/BUILDROOT
# appears in either - the rpmbuild scratch stays in ~/rpmbuild, where it
# belongs.
# _rpmfilename keeps the arch out of the path (its default is
# "%{ARCH}/%{NAME}-..." - the arch subdirectory is the filename, so overriding
# that is what flattens). The %% are load-bearing: with a single %, NAME is
# expanded when the --define is processed - the spec's Name:, once, for every
# subpackage - and pidp10-panel, -debuginfo and -debugsource are all written
# over the same pidp10-...rpm; %% defers it to packaging time, where rpm
# rebinds NAME per package. Same reason rpm's own macros file spells it %%.
dist=${DIST_DIR:-$root/dist}
mkdir -p "$top" "$dist"

# Source0: the tree. The prefix must match %{name}-%{version} or %autosetup -n
# fails.
make_tarball "$dist/$topdir_prefix.tar.gz" "$topdir_prefix" .

cp install/fedora/sysusers.d/30-pidp10.conf "$dist/30-pidp10.conf"
# Source2 for the same reason as Source1: the tmpfiles scriptlet is generated
# at spec-parse time, and %(cat %SOURCE2) has to find the file before %install
# puts anything anywhere.
cp install/fedora/tmpfiles.d/pidp10.conf "$dist/pidp10.conf"

rpmbuild -b$([ "$srpm_only" = 1 ] && echo s || echo a) \
    --define "_sourcedir $dist" \
    --define "_rpmdir $top" --define "_srcrpmdir $top" \
    --define "_rpmfilename %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm" \
    --define "gitrev $rev" --define "gitdate $date" \
    "$here/pidp10.spec"

if [ "$srpm_only" = 1 ]; then
	ls -1 "$top"/*.src.rpm
else
	echo "RPMs: $top"
fi
