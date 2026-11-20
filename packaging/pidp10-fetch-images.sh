#!/usr/bin/bash
# SPDX-License-Identifier: BSD-2-Clause
#
# pidp10-fetch-images - bring down the OS/media images into the state tree.
#
# WHY THIS IS A SEPARATE TOOL AND NOT PART OF THE PACKAGE
#   The old install.sh wget'ed ~2 GB of ITS/TOPS/WAITS media as part of
#   "installing the software", with no checksums and no way to do it again
#   later. That is unpackageable: distributions forbid network access in
#   scriptlets, the media are licence-encumbered, and a 2 GB download must not
#   be a side effect of `dnf install`. So the package ships the software and
#   this tool fetches the media, explicitly, repeatably, and verifiably.
#
# usage:
#   pidp10-fetch-images --list
#   pidp10-fetch-images --all
#   pidp10-fetch-images its tops20
#   pidp10-fetch-images --state /var/lib/pidp10 --only its
#
# Images land in $PIDP10_STATE/systems/<name>/, which is the directory the boot
# scripts of that name are installed into: the units chdir to the state tree,
# each script opens with `cd systems/<name>`, and every attach after that is
# relative to it. Script and image are one directory - which is why the scripts
# ship there too (SYSTEMSDIR in the top-level Makefile), why the directory is
# owned by the engine account, and why a root-owned one makes this tool fail.
#
# Run it as the user that owns the state dir - normally pidp10:
#   sudo -u pidp10 pidp10-fetch-images --all

set -euo pipefail

# Operator settings first (the same file the units load through EnvironmentFile=),
# then the layout defaults. The conf file is authoritative for policy - what to
# boot, which panel driver, which console port - and the layout is not in it, so
# a PIDP10_STATE set on the command line still picks a scratch tree.
[ -r /etc/pidp10/pidp10.conf ] && . /etc/pidp10/pidp10.conf
: "${PIDP10_STATE:=/var/lib/pidp10}"
base=${PIDP10_IMAGE_BASE:-https://obsolescence.dev/pidp10-sw}
cache=${PIDP10_IMAGE_CACHE:-$PIDP10_STATE/downloads}
force=0

# name | archive | kind | destination (relative to the state dir)
manifest=(
	"its|its-system.zip|zip|systems/its"
	"tops10|tops603ka.zip|zip|systems/tops10-603"
	"tops20|tops20-panda.zip|zip|systems/tops20"
	"waits|waits.tar.gz|tar|."
)

# Checksums, when we have them. An empty field means "nobody has recorded a
# sha256 for this yet", which the fetcher shouts about rather than swallowing:
# silently pulling 2 GB of ancient binaries off a random host and running them
# is not how this should work. Optional overrides live in
# /etc/pidp10/images.sha256 so a site can pin its own copies.
declare -A sha256=(
	[its-system.zip]=""
	[tops603ka.zip]=""
	[tops20-panda.zip]=""
	[waits.tar.gz]=""
)
[ -r /etc/pidp10/images.sha256 ] && . /etc/pidp10/images.sha256

die() { echo "pidp10-fetch-images: $*" >&2; exit 1; }

list() {
	printf '%-8s %-20s %-4s %s\n' NAME ARCHIVE KIND DESTINATION
	for m in "${manifest[@]}"; do
		IFS='|' read -r n a k d <<<"$m"
		printf '%-8s %-20s %-4s %s\n' "$n" "$a" "$k" "$PIDP10_STATE/$d"
	done
}

verify() {
	local archive=$1 sum=$2
	if [ -z "$sum" ]; then
		echo "pidp10-fetch-images: WARNING: no sha256 recorded for $archive;" >&2
		echo "pidp10-fetch-images:          integrity not verified. Pin one in" >&2
		echo "pidp10-fetch-images:          /etc/pidp10/images.sha256 as  sha256[\"$archive\"]=<hex>" >&2
		return 0
	fi
	echo "$sum  $cache/$archive" | sha256sum --check --status ||
		die "checksum mismatch for $archive (refusing to unpack)"
	echo "pidp10-fetch-images: $archive sha256 ok"
}

fetch_one() {
	local name=$1 m n archive kind dest
	for m in "${manifest[@]}"; do
		IFS='|' read -r n a k d <<<"$m"
		[ "$n" = "$name" ] && { archive=$a; kind=$k; dest=$d; break; }
	done
	[ -n "${archive:-}" ] || die "unknown image '$name' (try --list)"

	install -d -m 0755 "$cache" "$PIDP10_STATE/$dest"
	local file=$cache/$archive
	if [ -f "$file" ] && [ "$force" != 1 ]; then
		echo "pidp10-fetch-images: using cached $file"
	else
		echo "pidp10-fetch-images: downloading $base/$archive"
		# -C - : resume. Temp name so an interrupted download is never
		# mistaken for a complete one.
		wget -c -O "$file.part" "$base/$archive" || die "download failed"
		mv -f "$file.part" "$file"
	fi
	verify "$archive" "${sha256[$archive]:-}"

	case "$kind" in
	zip) unzip -o -q "$file" -d "$PIDP10_STATE/$dest" ;;
	tar) tar -xzf "$file" -C "$PIDP10_STATE/$dest" ;;
	*)   die "unknown archive kind $kind" ;;
	esac
	echo "pidp10-fetch-images: $name unpacked into $PIDP10_STATE/$dest"
}

want=()
while [ $# -gt 0 ]; do
	case "$1" in
	--list)  list; exit 0 ;;
	--all)   want=(); for m in "${manifest[@]}"; do IFS='|' read -r n _x _y _z <<<"$m"; want+=("$n"); done; shift ;;
	--state) PIDP10_STATE=$2; shift 2 ;;
	--base)  base=$2; shift 2 ;;
	--force) force=1; shift ;;
	--)      shift; while [ $# -gt 0 ]; do want+=("$1"); shift; done ;;
	-*)      die "unknown option $1" ;;
	*)       want+=("$1"); shift ;;
	esac
done

[ ${#want[@]} -gt 0 ] || { list; echo; echo "usage: pidp10-fetch-images [--all | NAME...]"; exit 2; }

[ -d "$PIDP10_STATE" ] || die "$PIDP10_STATE does not exist (run: systemd-tmpfiles --create pidp10.conf)"
[ -w "$PIDP10_STATE" ] || die "$PIDP10_STATE is not writable by $(id -un) (chown it to pidp10, or run with sudo -u pidp10)"

for n in "${want[@]}"; do fetch_one "$n"; done
echo "pidp10-fetch-images: done. Boot one with: systemctl start pidp10-engine@1.service"
