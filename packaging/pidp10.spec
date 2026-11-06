# SPDX-License-Identifier: BSD-2-Clause
#
# RPM spec for the PiDP-10.
#
# Design notes for anyone comparing this to the old install.sh:
#
#   * Everything installable is BUILT here, by the top-level Makefile:
#     %%set_build_flags + %%make_build + %%make_install, which is also why every
#     binary comes out PIE with the distribution's hardening flags. No
#     prebuilt ELF is packaged at all. The AI Lab device sims the boot scripts
#     call (cbridge, tv11, tvcon, imlac, vt05/50/52, ...) have their own
#     upstreams, so they belong in their own packages on PATH rather than in
#     a tarball of checked-in binaries next to the sources.
#   * No setuid, no setcap. The old flow did `chmod +s bin/pdp10-ka` and
#     `setcap cap_sys_nice+ep`; capabilities come from the units instead
#     (AmbientCapabilities=CAP_SYS_NICE) and RT priority from logind's
#     DefaultLimitRTPRIO. A world-writable setuid simulator is how you end up
#     with a root hole on a kiosk.
#   * No network in scriptlets. The ~2 GB of ITS/TOPS/WAITS media is fetched
#     by `pidp10-fetch-images`, which the operator runs deliberately.
#   * Services are enabled by preset files, not by a post scriptlet; the
#     scriptlets here are the systemd macros and nothing else.
#   * Read-only data in /usr/share, writable state in /var/lib. This is what
#     makes ProtectSystem=strict usable and lets an upgrade not eat your ITS
#     disk image.
#   * Nothing under /usr is marked %%config. A file in /usr must be replaceable
#     by an upgrade; an operator who wants a different logind drop-in writes one
#     in /etc/systemd/logind.conf.d/.
#
# rpmlint results that are correct as they stand are filtered in
# packaging/rpmlint.toml (run them with `make rpmlint`), each with its
# reason there. In summary:
#
#   * spelling-error on gpiochip, blinkenlight, imlac, tvcon, teletype,
#     pdp: domain words, not typos.
#   * useless-provides group(pidp10): the versioned provide comes from the
#     sysusers fragment, the unversioned duplicate from the generators that
#     look at file owners and tmpfiles users. The package has to own the state
#     directories and the units have to name the account, so there is no spec
#     change that removes it. The units deliberately do not say Group=pidp10:
#     User=pidp10 already implies it, and writing it adds a requirement on a
#     group this package provides.
#   * non-standard-uid/gid, log-files-without-logrotate: the account is new
#     and not in rpmlint's users list yet; the state tree is documented in
#     packaging/README.md.
#   * no-manual-page-for-binary: the front doors have pages (man/pdp.1,
#     man/pdpcontrol.1); the engines and panel tools are driven through the
#     units and pdp, and their interface is documented there.
#
# Build:
#   packaging/build-rpm.sh                       # snapshot RPMs from the tree
#   rpmbuild -bb --with kiosk_default packaging/pidp10.spec
#       (the product build: presets enable the cabinet so it boots out of the box)

# Snapshot identification, passed by packaging/build-rpm.sh. Upstream has no
# releases, so a rebuilt tree has to upgrade cleanly and be attributable.
%{!?gitrev:  %global gitrev  unknown}
%{!?gitdate: %global gitdate 00000000}

%global srcname pidp10
%global appname PiDP-10
%global simh_version 4.1.0

%bcond_with     kiosk_default

Name:           %{srcname}
Version:        %{simh_version}
Release:        1.%{gitdate}git%{gitrev}%{?dist}
Summary:        PDP-10 simulator, front-panel emulation and AI Lab peripherals

# simh is the MIT-like "Historical Permission Notice"; st/sty33 are MIT;
# pinctrl and the PiDP-10 glue are BSD-2-Clause. The Teletype .wav samples are
# CC-BY-SA-4.0 (recorded in src/sty33/sounds/README.md, which ships as
# %%license below so Hugh Pyle's attribution and share-alike terms travel with
# the files they cover). There is still no per-file license scan in the tree,
# so read this as "the union of what the sources claim" rather than as a
# cleared license. FIXME: unreviewed.
License:        BSD-2-Clause AND MIT AND HPND AND CC-BY-SA-4.0
URL:            https://obsolescence.dev/pdp10
# packaging/build-rpm.sh makes both tarballs with `git archive` (committed state
# plus uncommitted changes to tracked files). Nothing under bin/ is in it: the
# tree's prebuilt ELFs are not built or installed by this spec at all.
Source0:        %{srcname}-%{version}.tar.gz
# Same content as install/fedora/sysusers.d/30-pidp10.conf, shipped separately
# because %%sysusers_create_package has to read it in %%pre, before the files it
# describes have been unpacked.
Source1:        30-pidp10.conf
# Same content as install/fedora/tmpfiles.d/pidp10.conf, shipped separately for
# the same reason: %%tmpfiles_create_package cats %%{SOURCE2} when the spec is
# *parsed*, into the scriptlet's stdin - a path inside the buildroot would not
# exist yet then, and the generated scriptlet would silently carry an empty
# config (the scriptlet ends in "|| :" - nothing fails, nothing is created).
Source2:        pidp10.conf

BuildRequires:  gcc, gcc-c++, make, cmake, pkgconf-pkg-config
BuildRequires:  file
BuildRequires:  desktop-file-utils
BuildRequires:  systemd-rpm-macros
# The panel backends: libgpiod is not optional any more. Without it the build
# still succeeds but the engines can only use gpiomem, which a distribution
# kernel does not provide - the failure then shows up on the cabinet, not in
# the build log.
BuildRequires:  libgpiod-devel >= 2.0
BuildRequires:  pkgconfig(libgpiod)
BuildRequires:  readline-devel
BuildRequires:  libpcap-devel
BuildRequires:  zlib-devel
BuildRequires:  libpng-devel
BuildRequires:  SDL2-devel
BuildRequires:  SDL2_image-devel
BuildRequires:  SDL2_ttf-devel
BuildRequires:  SDL2_mixer-devel
BuildRequires:  libX11-devel
BuildRequires:  libXft-devel
BuildRequires:  fontconfig-devel
BuildRequires:  freetype-devel

Requires:       systemd
Requires:       libgpiod >= 2.0
Recommends:     libgpiod-utils
# The engine unit asks pidp-gpio-probe whether a cabinet is attached and
# (with the panel live) scansw10 which system to boot; both degrade gracefully
# when pidp10-panel is absent, but a machine with a front panel wants it.
Recommends:     %{name}-panel%{?_isa} = %{version}-%{release}
Suggests:       firewalld
# The AI Lab terminals `pdp` opens and the desktop entries launch, plus the
# Chaosnet bridge the cbridge unit and the boot scripts call. Each is its own
# upstream with its own spec (the table in packaging/README.md); suggested,
# not required: a headless box, or an operator who only ever uses the
# Teletype, must not be pulled into installing graphics emulators. The names
# are the upstream package names: the vt05 package provides all six
# DEC/Datapoint terminals (vt05/vt50/vt52/dp3300/dm2500/gecon), tek4010 the
# storage tube. imlac, tv11/tvcon and cbridge are named ahead of their specs
# landing in the same COPR: dnf silently skips a suggested package no
# enabled repo provides, and tv11/tvcon are one upstream, so either name
# covers the pair. Absent cbridge the cbridge unit skips by its
# ConditionPathExists=, which is the honest degraded mode.
Suggests:       vt05
Suggests:       tek4010
Suggests:       imlac
Suggests:       tv11
Suggests:       tvcon
Suggests:       cbridge
%{?systemd_requires}

%description
A PDP-10 (KA10/KI10/KL10/KS10) simulator with the PiDP-10 front panel:
blinkenlights and buttons driven over GPIO, plus the AI Lab peripherals
(Knight TV, Teletype Model 33, VT05/VT50/VT52, IMLAC, Tektronix 4010,
Digitronics 3300, Type-340 display) and Chaosnet.

The engines, the panel driver and the GPIO tools are built from source. The
device simulators the boot scripts call (cbridge, tv11, tvcon, imlac,
vt05/vt50/vt52, dp3300, dm2500, gecon, tek4010, supdup, gw) are separate
programs with their own upstreams and their own packages; install them and
they are picked up off PATH.

%package panel
Summary:        Front-panel GPIO tools for the PiDP-10

%description panel
pinctrl and pidp-gpio-probe: inspect the GPIO controller, answer "is a front
panel actually attached" (pidp-gpio-probe --check), drive the blinkenlight
multiplexing loop as a self-test, and run the cabinet bring-up test
(pidp10-test). The udev rule that adds the /dev/gpiochip node of the
Raspberry Pi expansion header to the pidp10 group ships here.

%package ui
Summary:        Desktop integration for the PiDP-10 terminals
BuildArch:      noarch
Requires:       %{name}%{?_isa} = %{version}-%{release}
# `pdp view` runs inside tilix (its layout file is a tilix profile) and
# `pdp con`/`telcon`/`tvcon` open an lxterminal; without them the desktop
# entries launch nothing.
Recommends:     tilix
Recommends:     lxterminal

%description ui
Icons, fonts, Tilix layout and .desktop files for the operator terminals
(pdp view, pdp con, tvcon, imlac, ...).

%prep
%autosetup -n %{srcname}-%{version}

%build
# The top-level Makefile reads CFLAGS/LDFLAGS from the environment and knows
# how to hand them to simh's own makefile (which otherwise assembles its own
# compile line and drops -lm -lpng -lz if it is handed an LDFLAGS=).
%set_build_flags
%make_build PREFIX=%{_prefix}

%install
%make_install PREFIX=%{_prefix}
# The .wav samples' own licence text (CC-BY-SA-4.0, attribution to Hugh
# Pyle). Its basename is README.md like the top-level one, so it goes into
# the licensedir under a distinguishing name or the two collide.
install -Dm 0644 src/sty33/sounds/README.md \
    %{buildroot}%{_licensedir}/%{name}/README.sounds

# The product ("kiosk") build boots the cabinet out of the box. It only edits
# preset files: a package must not run systemctl enable on someone's machine,
# and presets are the supported way to express what the defaults are (an admin
# gets the decision back with systemctl preset-all).
%if %{with kiosk_default}
sed -i 's|^disable pidp10-engine@0001.service|enable pidp10-engine@0001.service|' \
    %{buildroot}%{_presetdir}/pidp10.preset
sed -i \
    -e 's|^disable pidp10.target|enable pidp10.target|' \
    -e 's|^disable pidp10.service|enable pidp10.service|' \
    -e 's|^disable pidp10-console.service|enable pidp10-console.service|' \
    -e 's|^disable pidp10-teletype.service|enable pidp10-teletype.service|' \
    -e 's|^disable pidp10-tv11.service|enable pidp10-tv11.service|' \
    %{buildroot}%{_userpresetdir}/pidp10.preset
%endif

%check
# The engines run simh's register sanity check when they are linked; this is
# the next level up: every engine has to boot, take a command and exit. Q goes
# in on stdin rather than via -e/-c, which both leave simh at a sim> prompt
# with no input and hang the build until %%check's timeout. PIDP10_PANEL=sim is
# what lets this run at all: the builders have no GPIO hardware, so nothing here
# may open a /dev/gpiochipN - not as a failure and not even as a look. The
# sim backend exists for exactly this, and the panel tools honour the same
# declaration by opening nothing rather than by probing and coping.
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
for e in pdp10-ka pdp10-ki pdp10-kl pdp10-ks pdp6; do
    echo q | PIDP10_PANEL=sim timeout 60 build/$e >/dev/null || {
        echo "%check: $e did not boot"; exit 1; }
done
# The three probe modes %%check runs must answer from the declaration alone.
# That is the property the units rely on too: --check runs at every engine
# start on a machine that may have no panel, no group membership and no
# business opening someone else's chip. --list is the one mode that would
# enumerate /dev/gpiochip0..63 - opening a chip is the probe - so under sim it
# says so and opens nothing, which is also what this line is asserting.
test "$(PIDP10_PANEL=sim build/pidp-gpio-probe --backend)" = \
     "sim (PIDP10_PANEL=sim)"
PIDP10_PANEL=sim build/pidp-gpio-probe --pins >/dev/null
test "$(PIDP10_PANEL=sim build/pidp-gpio-probe --list)" = \
     "pidp-gpio-probe: PIDP10_PANEL=sim: no chips enumerated (GPIO probing is skipped, not simulated)"
desktop-file-validate %{buildroot}%{_datadir}/applications/*.desktop

%pre
%sysusers_create_package %{name} %SOURCE1

%post
%tmpfiles_create_package %{name} %SOURCE2
%systemd_post pidp10-cbridge.service pidp10-engine@.service
%systemd_user_post pidp10.service pidp10@.service pidp10.target

%preun
%systemd_preun pidp10-cbridge.service pidp10-engine@.service
%systemd_user_preun pidp10.service pidp10@.service pidp10.target

%postun
%systemd_postun_with_restart pidp10-cbridge.service
%systemd_user_postun_with_restart pidp10.service

%post ui
%systemd_user_post pidp10-console.service pidp10-teletype.service \
    pidp10-tv11.service pidp10-view.service

%preun ui
%systemd_user_preun pidp10-console.service pidp10-teletype.service \
    pidp10-tv11.service pidp10-view.service

%postun ui
%systemd_user_postun_with_restart pidp10-console.service \
    pidp10-teletype.service pidp10-tv11.service pidp10-view.service

# No gtk-update-icon-cache scriptlet: the payload is plain PNG/SVG under
# hicolor, which gtk-update-icon-cache's file triggers already maintain.

%files
%license README.md
# The .wav samples' CC-BY-SA-4.0 text; placed under this name in %%install
# so it does not collide with the top-level README.md.
%license %{_licensedir}/%{srcname}/README.sounds
%doc packaging/README.md
%dir %{_datadir}/%{srcname}
%{_datadir}/%{srcname}/systems
%dir %{_datadir}/%{srcname}/conf
%{_datadir}/%{srcname}/conf/*
%dir %{_datadir}/%{srcname}/sounds
%{_datadir}/%{srcname}/sounds/*
%{_bindir}/pdp10-ka
%{_bindir}/pdp10-ki
%{_bindir}/pdp10-kl
%{_bindir}/pdp10-ks
%{_bindir}/pdp6
%{_bindir}/pdp
%{_bindir}/pdpcontrol
# brp-compress gzips man pages, so match both spellings.
%{_mandir}/man1/pdp.1*
%{_mandir}/man1/pdpcontrol.1*
%{_mandir}/man1/st.1*
# Built from src/: the Teletype and the DECwriter-style terminal, plus the
# switch reader the engine unit calls. The panel diagnostics (pidp-gpio-probe,
# pinctrl, pidp10-test) are in pidp10-panel.
%{_bindir}/sty
%{_bindir}/st
%{_bindir}/scansw10
%{_bindir}/pidp10-fetch-images
%dir %{_libexecdir}/%{srcname}
%{_libexecdir}/%{srcname}/*
%{_unitdir}/pidp10-cbridge.service
%{_unitdir}/pidp10-engine@.service
# User-scope engine units: pdpcontrol user drives these even on a headless
# box. The desktop-session units (console, teletype, tv11, view) ship with
# pidp10-ui, which owns the fonts, icons, desktop entries and terminal
# Recommends they need; Wants= across the package boundary is a no-op when
# ui is absent, and the user preset naming a unit that is not installed is
# silent. The scriptlets above name exactly this payload, and ui's name
# its own - scriptlets and files must not live in different packages.
%{_userunitdir}/pidp10.service
%{_userunitdir}/pidp10@.service
%{_userunitdir}/pidp10.target
%{_presetdir}/pidp10.preset
%{_userpresetdir}/pidp10.preset
%{_sysusersdir}/30-pidp10.conf
%{_tmpfilesdir}/%{srcname}.conf
# Created by systemd-tmpfiles from the fragment above; %%ghost so the package
# owns the ownership and mode without owning contents an upgrade must not
# touch.
%ghost %dir %attr(0755,pidp10,pidp10) %{_sharedstatedir}/%{srcname}
%ghost %dir %attr(0755,pidp10,pidp10) %{_sharedstatedir}/%{srcname}/systems
%ghost %dir %attr(0755,pidp10,pidp10) /var/log/%{srcname}
%ghost %dir %attr(0755,pidp10,pidp10) /run/%{srcname}
%dir %{_sysconfdir}/%{srcname}
%config(noreplace) %{_sysconfdir}/%{srcname}/pidp10.conf
%{_prefix}/lib/firewalld/services/%{srcname}.xml
# Not %%config: /usr must be stateless, and an operator override belongs in
# /etc/systemd/logind.conf.d/.
%{_prefix}/lib/systemd/logind.conf.d/60-pidp10-rt.conf

%files panel
%{_bindir}/pinctrl
%{_bindir}/pidp-gpio-probe
# Cabinet bring-up test: random LED patterns plus a report on which switches
# answer. It needs no privileges, so it belongs with the other panel tools
# rather than in the package a headless box installs.
%{_bindir}/pidp10-test
%{_datadir}/bash-completion/completions/pinctrl
%{_udevrulesdir}/99-pidp10.rules
# The upstream pinctrl README (RPi-specific but the only description of what
# the tool's modes mean); answers no-documentation with real text.
%doc src/utils/pinctrl/README.md

%files ui
# The desktop-session user units; the scriptlets that manage them are above
# (the note in the main %%files explains the split).
%{_userunitdir}/pidp10-console.service
%{_userunitdir}/pidp10-teletype.service
%{_userunitdir}/pidp10-tv11.service
%{_userunitdir}/pidp10-view.service
%{_datadir}/applications/*.desktop
# Named per install, not a *.png glob: a glob forgives a wrong file name
# (con-48.png sat under 48x48/apps unnoticed for a whole packaging
# revision, and the hicolor theme never shows a layer it cannot name).
%{_datadir}/icons/hicolor/48x48/apps/con.png
%{_datadir}/icons/hicolor/48x48/apps/imlac.png
%{_datadir}/icons/hicolor/48x48/apps/knight.png
%{_datadir}/icons/hicolor/48x48/apps/pdp10.png
%{_datadir}/icons/hicolor/48x48/apps/pdp11.png
%{_datadir}/icons/hicolor/48x48/apps/telcon.png
%{_datadir}/icons/hicolor/48x48/apps/vt52.png
%{_datadir}/icons/hicolor/scalable/apps/*.svg
%{_datadir}/fonts/%{srcname}
%{_datadir}/%{srcname}/view.json
%{_datadir}/%{srcname}/pidp10tilix.conf

# The entries carry the plain 4.1.0-1 version: the snapshot identity lives in
# Release and cannot be known by a hand-written entry (rpmlint's
# incoherent-version-in-changelog is filtered for exactly this; the
# alternative would be editing this file on every build).
%changelog
* Wed Nov 04 2026 Dennis Gilmore <dennis@ausil.us> - 4.1.0-1
- %%check boots every engine and runs the panel probe; scriptlets are the
  systemd macros; state dirs are %%ghost; no %%config under /usr; snapshot
  Release from git.
- First packaged form: FHS layout, services via presets, no setuid/setcap,
  media images fetched explicitly by pidp10-fetch-images.
