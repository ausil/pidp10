#!/usr/bin/bash
# SPDX-License-Identifier: BSD-2-Clause
#
# Fedora package set for the PiDP-10. The Debian equivalents it replaces are
# the apt-get/apt lines in install/install.sh (which fail on Fedora with
# "apt: command not found" - and silently, because they were not guarded).
#
#   sudo install/fedora/packages.sh [--with-build] [--with-desktop]
#
# Groups:
#   runtime  - enough to RUN the shipped/prebuilt binaries
#   --with-build   compilers + -devel packages to BUILD src/pdp10 and friends
#   --with-desktop tilix/dconf/fonts for `pdp view`, sty, tvcon
#
# Notes on the non-obvious mappings:
#   libsdl2-*-dev        -> SDL2_*-devel        (Fedora's "SDL2" is
#                         sdl2-compat, which still provides
#                         libSDL2-2.0.so.0, so the soname the binaries want
#                         is there)
#   libvdeplug2/dev      -> vde2 / vde2-devel
#   libpcre3             -> pcre2 (the imlac simulator's regex dep)
#   libedit-dev          -> libedit-devel; ka10_pipanel.c includes
#                         <editline/readline.h>, so this is a *build* blocker
#                         for the front-panel build, not a nicety
#   telnet/telnetd       -> telnet / telnet-server; you almost certainly do
#                         NOT need telnet-server: every "telnet" in this
#                         project talks to a simh TMXR listener, not to the
#                         host's getty
#   raspi-config         -> does not exist on Fedora; see install-fedora.sh

set -euo pipefail

runtime=(
	screen            # still used by bin/*.expect and by hand-running sims
	expect            # bin/type340.expect
	telnet            # console client
	libpcap           # simh ethernet
	gzip unzip wget   # install.sh downloads disk images with these
)

build=(
	gcc gcc-c++ make cmake pkgconf-pkg-config git
	SDL2-devel
	SDL2_image-devel SDL2_net-devel SDL2_ttf-devel SDL2_mixer-devel
	readline-devel       # simh console line editing, AND the front panel's
	                     # vm_read(): ka10_pipanel.c uses the rl_callback_*
	                     # API, which GNU readline and libedit both provide.
	                     # libedit-devel is therefore optional (it is what
	                     # Raspberry Pi OS has, so it stays preferred there);
	                     # with neither installed the panel build still works
	                     # and the console falls back to plain line input.
	libpcap-devel
	vde2 vde2-devel      # extended ethernet (0readme_ethernet.txt)
	gtk3-devel           # simh display/340 windows
	libusbx-devel        # only for PANDA_LIGHTS builds
	pcre2-devel          # imlac
	libgpiod libgpiod-devel   # front panel via the GPIO char dev
	libgpiod-utils            # gpioinfo/gpioget, your best debugging friend
)

desktop=(
	tilix              # `pdp view` layout (install/view.json)
	dconf-cli dconf-editor
	xterm              # fallback terminal for libexec/pidp10-terminal
	fontconfig
	pulseaudio-utils   # pactl, to check sty's audio actually reaches PipeWire
)

pkgs=("${runtime[@]}")
for a in "$@"; do
	case "$a" in
		--with-build)   pkgs+=("${build[@]}") ;;
		--with-desktop) pkgs+=("${desktop[@]}") ;;
		--all)          pkgs+=("${build[@]}" "${desktop[@]}") ;;
		-h|--help)
			sed -n '3,40p' "$0"; exit 0 ;;
		*) echo "packages.sh: unknown option $a" >&2; exit 2 ;;
	esac
done

echo "# dnf install -y ${pkgs[*]}"
dnf install -y "${pkgs[@]}"

# Not in the default Fedora repos, but you may want them:
#   dnf install -y cockpit                 # web admin for the headless box
#   dnf install -y gnome-remote-desktop    # replaces "raspi-config do_vnc"
#   dnf copr enable ...                    # nothing needed so far
