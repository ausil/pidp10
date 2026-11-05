#!/usr/bin/bash
# SPDX-License-Identifier: BSD-2-Clause
#
# build-rpm.sh - build the RPMs from the current git tree.
#
#   packaging/build-rpm.sh [topdir]                default topdir: ./rpmbuild
#   packaging/build-rpm.sh --with-vendored [topdir]
#
# Uses `git archive` on a stashed view of the work tree, so the tarball is the
# committed state plus any uncommitted changes to tracked files. Untracked new
# files are NOT included - if `git status` says untracked, the build will not
# see it, and that is deliberate: a package built from a tarball must be
# reproducible from the repository alone.
#
# The default Source0 excludes bin/, the 46 MB of prebuilt AI Lab peripheral
# simulators that have no source in this tree: a distribution build that forbids
# binary blobs then gets a clean tarball, and --with-vendored produces the one
# that also builds pidp10-vendored.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
cd "$root"

vendored=0
if [ "${1:-}" = "--with-vendored" ]; then
	vendored=1
	shift
fi
top=$(realpath -m "${1:-rpmbuild}")

version=$(sed -n 's/^%global simh_version \([0-9.]*\).*/\1/p' "$here/pidp10.spec" | head -1)
[ -n "$version" ] || { echo "build-rpm: cannot read simh_version from the spec" >&2; exit 1; }
name=pidp10
topdir_prefix=$name-$version

rev=$(git rev-parse --short HEAD)
date=$(git show -s --format=%cd --date=format:%Y%m%d HEAD)

mkdir -p "$top"/{SOURCES,BUILD,RPMS,SRPMS}

# --exclude the vendored set unless asked for; the prefix must match %{name}-%{version}
# or %autosetup -n fails.
if [ "$vendored" = 1 ]; then
	git archive --format=tar --prefix="$topdir_prefix/" HEAD | gzip \
	    > "$top/SOURCES/$topdir_prefix.tar.gz"
	with=--with=vendored
else
	git archive --format=tar --prefix="$topdir_prefix/" HEAD -- . ':!bin' | gzip \
	    > "$top/SOURCES/$topdir_prefix.tar.gz"
	with=--without=vendored
fi
cp install/fedora/sysusers.d/30-pidp10.conf "$top/SOURCES/30-pidp10.conf"

rpmbuild -ba --define "_topdir $top" \
    --define "gitrev $rev" --define "gitdate $date" \
    "$with" "$here/pidp10.spec"

echo "RPMs: $top/RPMS"
