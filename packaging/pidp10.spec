# SPDX-License-Identifier: BSD-2-Clause
#
# RPM spec for the PiDP-10.
#
# Design notes for anyone comparing this to the old install.sh:
#
#   * Everything installable is BUILT here, by the top-level Makefile:
#     %%set_build_flags + %%make_build + %%make_install, which is also why every
#     binary comes out PIE with the distribution's hardening flags. The 31
#     prebuilt ELF binaries under bin/ (AI Lab peripherals with no source in
#     this tree) are not in Source0 at all: they arrive in their own tarball
#     (Source2) and only for --with vendored, into their own subpackage, so the
#     tarball a distribution builds from contains no unreviewed blob.
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
# rpmlint results that are correct as they stand, recorded so nobody chases them:
#
#   * spelling-error on gpiochip, blinkenlight, imlac, tvcon, teletype, vendored,
#     pdp: domain words, not typos.
#   * useless-provides group(pidp10): the versioned provide comes from the
#     sysusers fragment, the unversioned duplicate from the generators that
#     look at file owners and tmpfiles users. The package has to own the state
#     directories and the units have to name the account, so there is no spec
#     change that removes it. The units deliberately do not say Group=pidp10:
#     User=pidp10 already implies it, and writing it adds a requirement on a
#     group this package provides.
#   * non-standard-uid/gid, no-manual-page-for-binary, log-files-without-
#     logrotate: the first three binaries upstream shipped had no man pages
#     either; the state tree is documented in packaging/README.md.
#
# Build:
#   packaging/build-rpm.sh                       # snapshot RPMs from the tree
#   rpmbuild -bb --with vendored packaging/pidp10.spec
#   rpmbuild -bb --with vendored --with kiosk_default packaging/pidp10.spec
#       (the product build: presets enable the cabinet so it boots out of the box)

# Snapshot identification, passed by packaging/build-rpm.sh. Upstream has no
# releases, so a rebuilt tree has to upgrade cleanly and be attributable.
%{!?gitrev:  %global gitrev  unknown}
%{!?gitdate: %global gitdate 00000000}

%global srcname pidp10
%global appname PiDP-10
%global simh_version 4.1.0

%bcond_with     vendored
%bcond_with     kiosk_default

# The prebuilt peripherals in bin/ are aarch64 ELF. An x86-64 build that
# installed them would ship a package of binaries that cannot execute, which is
# what --with vendored did here until anyone checked, so the arch decides what
# can be packaged and --with vendored only opts in on top of that; the request
# is reported in %build rather than silently dropped. Defined before the Source
# declarations because Source2 is conditional on it: an SRPM built without
# vendored then carries no blob at all, rather than shipping one it never uses.
%global build_vendored %{with vendored}
%ifnarch aarch64
%global build_vendored 0
%endif

Name:           %{srcname}
Version:        %{simh_version}
Release:        1.%{gitdate}git%{gitrev}%{?dist}
Summary:        PDP-10 simulator, front-panel emulation and AI Lab peripherals

# simh is the MIT-like "Historical Permission Notice"; st/sty33 are MIT;
# pinctrl and the PiDP-10 glue are BSD-2-Clause. There is no per-file license
# scan in the tree, and the provenance of the Teletype .wav samples is not
# recorded, so read this as "the union of what the sources claim" rather than
# as a cleared license. FIXME: unreviewed.
License:        BSD-2-Clause AND MIT AND HPND
URL:            https://obsolescence.dev/pdp10
# packaging/build-rpm.sh makes both tarballs with `git archive` (committed state
# plus uncommitted changes to tracked files). Source0 has no binary blobs in it;
# the prebuilt peripherals are Source2, declared only when they will be used.
Source0:        %{srcname}-%{version}.tar.gz
%if %{build_vendored}
Source2:        %{srcname}-vendored-%{version}.tar.gz
%endif
# Same content as install/fedora/sysusers.d/30-pidp10.conf, shipped separately
# because %%sysusers_create_package has to read it in %%pre, before the files it
# describes have been unpacked.
Source1:        30-pidp10.conf

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
%{?systemd_requires}

%description
A PDP-10 (KA10/KI10/KL10/KS10) simulator with the PiDP-10 front panel:
blinkenlights and buttons driven over GPIO, plus the AI Lab peripherals
(Knight TV, Teletype Model 33, VT05/VT50/VT52, IMLAC, Tektronix 4010,
Digitronics 3300, Type-340 display) and Chaosnet.

The engines, the panel driver and the GPIO tools in this package are built
from source.
%if %{build_vendored}
The device simulators that upstream only publishes as binaries live in
pidp10-vendored.
%else
The prebuilt device simulators are not packaged here: what upstream publishes
of them is aarch64 binaries, and installing those on this architecture would
only put files on disk that cannot execute.
%endif

%package panel
Summary:        Front-panel GPIO tools for the PiDP-10

%description panel
pinctrl and pidp-gpio-probe: inspect the GPIO controller, answer "is a front
panel actually attached" (pidp-gpio-probe --check), drive the blinkenlight
multiplexing loop as a self-test, and run the cabinet bring-up test
(pidp10-test). The udev rule that puts the Raspberry Pi header's /dev/gpiochip
node in the pidp10 group ships here.

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

%if %{build_vendored}
%package vendored
Summary:        Prebuilt device simulators with no source in this tree
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description vendored
AI Lab peripheral simulators (cbridge, tv11, tvcon, imlac, vt05/vt50/vt52,
tek4010, dp3300, dm2500, ...) that upstream publishes only as ELF binaries,
checked into the repository under bin/. They are architecture-specific and are
not built from source here, which is why they arrive in their own source
tarball and their own package: install it for a cabinet, leave it out (and
build from Source0, which has no blobs in it) for a distribution build.
%endif

%prep
%autosetup -n %{srcname}-%{version}
%if %{build_vendored}
# An extra payload rather than a patch series, so it is unpacked by hand:
# %autosetup owns Source0 and these are binaries that must not be in it. The
# archive carries bare relative paths, so they land in the bin/ that the
# Makefile's WITH_VENDORED path reads.
%{__tar} -x -f %{SOURCE2}
%endif

%build
%if %{with vendored}
%ifnarch aarch64
echo "NOTE: --with vendored ignored on %_target_cpu: bin/ holds aarch64 binaries" >&2
%endif
%endif
# The top-level Makefile reads CFLAGS/LDFLAGS from the environment and knows
# how to hand them to simh's own makefile (which otherwise assembles its own
# compile line and drops -lm -lpng -lz if it is handed an LDFLAGS=).
%set_build_flags
%make_build PREFIX=%{_prefix}

%install
%make_install PREFIX=%{_prefix} VENDORBINDIR=%{_libdir}/pidp10/bin \
    %{?build_vendored:WITH_VENDORED=yes}

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
# with no input and hang the build until %%check's timeout. PIDP10_PANEL=sim so
# a build host with GPIO hardware is not required and the panel thread does not
# try to own a gpiochip during %%check.
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
for e in pdp10-ka pdp10-ki pdp10-kl pdp10-ks pdp6; do
    echo q | PIDP10_PANEL=sim timeout 60 build/$e >/dev/null || {
        echo "%check: $e did not boot"; exit 1; }
done
# The probe answers without hardware, which is the property the units rely on.
test "$(PIDP10_PANEL=sim build/pidp-gpio-probe --backend)" = \
     "sim (PIDP10_PANEL=sim)"
PIDP10_PANEL=sim build/pidp-gpio-probe --pins >/dev/null
PIDP10_PANEL=sim build/pidp-gpio-probe --list >/dev/null
desktop-file-validate %{buildroot}%{_datadir}/applications/*.desktop

%pre
%sysusers_create_package %{name} %SOURCE1

%post
%tmpfiles_create_package %{name} %{_tmpfilesdir}/%{name}.conf
%systemd_post pidp10-cbridge.service pidp10-engine@.service

%preun
%systemd_preun pidp10-cbridge.service pidp10-engine@.service

%postun
%systemd_postun_with_restart pidp10-cbridge.service

%post ui
%systemd_user_post pidp10.service pidp10@.service pidp10-console.service \
    pidp10-teletype.service pidp10-tv11.service pidp10-view.service pidp10.target

%preun ui
%systemd_user_preun pidp10.service pidp10@.service pidp10-console.service \
    pidp10-teletype.service pidp10-tv11.service pidp10-view.service pidp10.target

%postun ui
%systemd_user_postun_with_restart pidp10.service
gtk-update-icon-cache -q -t -f %{_datadir}/icons/hicolor 2>/dev/null || :

%files
%license README.md
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
%{_userunitdir}/pidp10*.service
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

%files ui
%{_datadir}/applications/*.desktop
%{_datadir}/icons/hicolor/48x48/apps/*.png
%{_datadir}/icons/hicolor/scalable/apps/*.svg
%{_datadir}/fonts/%{srcname}
%{_datadir}/%{srcname}/view.json
%{_datadir}/%{srcname}/pidp10tilix.conf

%if %{build_vendored}
%files vendored
%defattr(-,root,root,-)
%dir %{_libdir}/%{srcname}
%dir %{_libdir}/%{srcname}/bin
%{_libdir}/%{srcname}/bin/*
%endif

%changelog
* Tue Nov 04 2025 Dennis Gilmore <dennis@ausil.us> - 4.1.0-1
- Top-level Makefile owns the layout (PREFIX/LIBEXECDIR/DATADIR/VENDORBINDIR),
  builds out of tree and honours CFLAGS/LDFLAGS, so every binary is PIE with
  the distribution's hardening flags; the spec is %set_build_flags +
  %make_build + %make_install and installs only from the build directory.
- The runtime path-resolution shell library is gone: paths are compiled in or
  substituted at install time, and /etc/pidp10/pidp10.conf carries policy only.
- %check boots every engine and runs the panel probe; scriptlets are the
  systemd macros; state dirs are %ghost; no %config under /usr; the vendored
  device sims move to %%{_libdir}; snapshot Release from git.

* Mon Nov 03 2025 Dennis Gilmore <dennis@ausil.us> - 4.1.0-1
- First packaged form: FHS layout, services via presets, no setuid/setcap,
  media images fetched explicitly by pidp10-fetch-images.
