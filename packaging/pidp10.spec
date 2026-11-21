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
#     (AmbientCapabilities=CAP_SYS_NICE, LimitRTPRIO=99). A world-writable
#     setuid simulator is how you end up with a root hole on a kiosk.
#   * The engines are system units running as the pidp10 user in detached
#     screen sessions (pidp10, pidp6), like the PiDP-11's pidp11-simh. simh
#     reads its sim> prompt from stdin, so a bare unit with stdin on /dev/null
#     had no reachable prompt. Members of the pidp10 group attach through
#     /etc/sudoers.d/pidp10 and start/stop the units through a polkit rule.
#   * No network in scriptlets. The ~2 GB of ITS/TOPS/WAITS media is fetched
#     by `pidp10-fetch-images`, which the operator runs deliberately.
#   * Services are enabled by preset files, not by a post scriptlet; the
#     scriptlets here are the systemd macros and nothing else.
#   * Read-only data in /usr/share, writable state in /var/lib. This is what
#     makes ProtectSystem=strict usable and lets an upgrade not eat your ITS
#     disk image. The one exception is the system directories: a boot script
#     and the images it attaches are the same directory (simh resolves every
#     attach relative to the `cd systems/<name>` the scripts start with, and
#     pidp10-fetch-images unpacks the media into it), so they are installed to
#     /var/lib/pidp10/systems and are owned by the engine account. They are not
#     %%config (Fedora takes none outside /etc), so an upgrade replaces them;
#     a site-local boot copies the directory and points PIDP10_SELECTIONS at a
#     table of its own - see packaging/README.md.
#   * Nothing under /usr is marked %%config. A file in /usr must be replaceable
#     by an upgrade; an operator who wants a different polkit rule writes one
#     in /etc/polkit-1/rules.d/.
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

%global srcname pidp10
%global appname PiDP-10
%global simh_version 4.1.0
# Snapshot segment of the Release: bump srcdate when a release is cut so a
# rebuilt tree upgrades. The upstream commit a snapshot carries is recorded in
# %%changelog - nothing here is derived from git, which would be stale the
# moment it was committed and would leave a bare rpmbuild -bs looking for a
# tarball name nobody produced.
%global srcdate 20261106

%bcond_with     kiosk_default

Name:           %{srcname}
Version:        %{simh_version}
Release:        4.%{srcdate}%{?dist}
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
Source0:        %{srcname}-%{version}-%{srcdate}.tar.gz
Source1:        30-pidp10.conf
Source2:        pidp10.conf

BuildRequires:  gcc, gcc-c++, make, cmake, pkgconf-pkg-config
BuildRequires:  file
BuildRequires:  desktop-file-utils
BuildRequires:  systemd-rpm-macros
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
# The engines' consoles are screen sessions, attached through sudo, and the
# units are started through the polkit rule.
Requires:       screen
Requires:       sudo
Requires:       polkit
# pidp10-fetch-images downloads the media with wget and unpacks it with unzip
# (ITS, TOPS-10, TOPS-20) or tar (WAITS). Without them there is nothing to boot.
Requires:       wget
Requires:       unzip
Requires:       tar
# Lets the pidp10 user open the Type 340 / DD windows on the desktop
# (pidp10-xhost.service); without it the engine runs headless.
Recommends:     xhost
Requires:       libgpiod >= 2.0
Recommends:     libgpiod-utils
Recommends:     %{name}-panel%{?_isa} = %{version}-%{release}
# pdp con / pdp sty, and the pidp10-teletype unit, run sty.
Requires:       %{name}-clients%{?_isa} = %{version}-%{release}
# The operator side: pidp10-tv11.service (ITS says "TV 11 WENT DOWN" without
# it), the console/teletype/view user units, the .desktop files and icons.
# noarch, so no %%{?_isa}.
Requires:       %{name}-ui = %{version}-%{release}
# ITS's Chaosnet (pidp10-cbridge.service). Recommended, not required: the unit
# is skipped without /usr/bin/cbridge, and ITS boots without Chaosnet.
Recommends:     chaosnet-bridge
# simh dlopen()s libpcap for its Ethernet devices, so rpm finds no dependency
# on it. Upstream asks for the unversioned libpcap.so, which only
# libpcap-devel has; src/pdp10/sim_ether.c falls back to libpcap.so.1, the
# runtime package's. The IMP's default "at imp nat:" (systems/its/boot.*)
# works without it, but attaching to a host interface needs it, and without it
# every boot prints "libpcap.so failed to load, install libpcap".
Recommends:     libpcap
Suggests:       firewalld
Suggests:       vt05
Suggests:       tek4010
Suggests:       imlac
Suggests:       tv11
Suggests:       tvcon
# pdp gt40: open-simh-pdp11 (Fedora simh-pdp11 3.12 cannot run gt40.simh)
Suggests:       open-simh
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

%package clients
Summary:        Teletype Model 33 terminal emulator (sty) and st, from the PiDP-10
# sty33 and st are MIT; the Teletype recordings are CC-BY-SA-4.0. The two
# fonts carry no license statement in the tree. FIXME: unreviewed.
License:        MIT AND CC-BY-SA-4.0
# Both connect to a PDP-10's console with telnet (sty -e telnet HOST 1025).
Recommends:     telnet

%description clients
sty, a terminal that looks and sounds like a Teletype Model 33 (the PiDP-10's
"noisy and slow" console), and st, the plain suckless terminal it is built
from, with the Teletype font and the recorded Teletype sounds.

Nothing here needs the simulator: this is the package to install on a desktop
or laptop that uses a PiDP-10 elsewhere on the network, for example through
rpdp ("rpdp con" runs sty).

%package ui
Summary:        Desktop integration for the PiDP-10 terminals
BuildArch:      noarch
# Not %%{?_isa}: in a noarch package it expands to the architecture it was
# built on, and the package could then only be installed there.
Requires:       %{name} = %{version}-%{release}
Recommends:     tilix
Recommends:     lxterminal

%description ui
Icons, fonts, Tilix layout and .desktop files for the operator terminals
(pdp view, pdp con, tvcon, imlac, ...).

%prep
%autosetup -n %{srcname}-%{version}

%build
%set_build_flags
%make_build PREFIX=%{_prefix}

%install
%make_install PREFIX=%{_prefix}
install -Dm 0644 src/sty33/sounds/README.md \
    %{buildroot}%{_licensedir}/%{name}-clients/README.sounds

# The product ("kiosk") build boots the cabinet out of the box. It only edits
# preset files: a package must not run systemctl enable on someone's machine,
# and presets are the supported way to express what the defaults are (an admin
# gets the decision back with systemctl preset-all).
%if %{with kiosk_default}
sed -i 's|^disable pidp10-engine@.service$|enable pidp10-engine@.service default|' \
    %{buildroot}%{_presetdir}/pidp10.preset
sed -i \
    -e 's|^disable pidp10.target|enable pidp10.target|' \
    -e 's|^disable pidp10-xhost.service|enable pidp10-xhost.service|' \
    -e 's|^disable pidp10-console.service|enable pidp10-console.service|' \
    -e 's|^disable pidp10-teletype.service|enable pidp10-teletype.service|' \
    -e 's|^disable pidp10-tv11.service|enable pidp10-tv11.service|' \
    %{buildroot}%{_userpresetdir}/pidp10.preset
%endif

%check
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
for e in pdp10-ka pdp10-ki pdp10-kl pdp10-ks pdp6; do
    echo q | PIDP10_PANEL=sim timeout 60 build/$e >/dev/null || {
        echo "%check: $e did not boot"; exit 1; }
done
test "$(PIDP10_PANEL=sim build/pidp-gpio-probe --backend)" = \
     "sim (PIDP10_PANEL=sim)"
PIDP10_PANEL=sim build/pidp-gpio-probe --pins >/dev/null
test "$(PIDP10_PANEL=sim build/pidp-gpio-probe --list)" = \
     "pidp-gpio-probe: PIDP10_PANEL=sim: no chips enumerated (GPIO probing is skipped, not simulated)"
desktop-file-validate %{buildroot}%{_datadir}/applications/*.desktop

%pre
%sysusers_create_package %{name} %SOURCE1

%post
# The systemd_* macros take their arguments up to the end of the line: a
# backslash continuation splits the generated command and the shell runs the
# second line as a command of its own. Keep each unit list on one line.
%tmpfiles_create_package %{name} %SOURCE2
%systemd_post pidp10-cbridge.service pidp10-engine@.service pidp10-pdp6.service
%systemd_user_post pidp10.target pidp10-xhost.service

%preun
%systemd_preun pidp10-cbridge.service pidp10-engine@.service pidp10-pdp6.service
%systemd_user_preun pidp10.target pidp10-xhost.service

%postun
%systemd_postun_with_restart pidp10-cbridge.service

%post ui
%systemd_user_post pidp10-console.service pidp10-teletype.service pidp10-tv11.service pidp10-view.service

%preun ui
%systemd_user_preun pidp10-console.service pidp10-teletype.service pidp10-tv11.service pidp10-view.service

%postun ui
%systemd_user_postun_with_restart pidp10-console.service pidp10-teletype.service pidp10-tv11.service pidp10-view.service


%files
%license README.md
%doc packaging/README.md
%dir %{_datadir}/%{srcname}
%dir %{_datadir}/%{srcname}/conf
%{_datadir}/%{srcname}/conf/*
# The GT40 boot ROM is in -clients, for the GT40 terminal on other machines.
%exclude %{_datadir}/%{srcname}/conf/bootvt.img
%{_bindir}/pdp10-ka
%{_bindir}/pdp10-ki
%{_bindir}/pdp10-kl
%{_bindir}/pdp10-ks
%{_bindir}/pdp6
%{_bindir}/pdp
%{_bindir}/pdpcontrol
%{_mandir}/man1/pdp.1*
%{_mandir}/man1/pdpcontrol.1*
%{_bindir}/scansw10
%{_bindir}/pidp10-fetch-images
%dir %{_libexecdir}/%{srcname}
%{_libexecdir}/%{srcname}/*
%{_unitdir}/pidp10-cbridge.service
%{_unitdir}/pidp10-engine@.service
%{_unitdir}/pidp10-pdp6.service
%{_userunitdir}/pidp10.target
%{_userunitdir}/pidp10-xhost.service
%{_presetdir}/pidp10.preset
%{_userpresetdir}/pidp10.preset
%{_sysusersdir}/30-pidp10.conf
%{_tmpfilesdir}/%{srcname}.conf
%ghost %dir %attr(0755,pidp10,pidp10) %{_sharedstatedir}/%{srcname}
%dir %attr(0755,pidp10,pidp10) %{_sharedstatedir}/%{srcname}/systems
%attr(0644,pidp10,pidp10) %{_sharedstatedir}/%{srcname}/systems/selections
%dir %attr(0755,pidp10,pidp10) %{_sharedstatedir}/%{srcname}/systems/hills-blinky
%attr(0644,pidp10,pidp10) %{_sharedstatedir}/%{srcname}/systems/hills-blinky/*
%dir %attr(0755,pidp10,pidp10) %{_sharedstatedir}/%{srcname}/systems/its
%attr(0644,pidp10,pidp10) %{_sharedstatedir}/%{srcname}/systems/its/*
%dir %attr(0755,pidp10,pidp10) %{_sharedstatedir}/%{srcname}/systems/tops10-603
%attr(0644,pidp10,pidp10) %{_sharedstatedir}/%{srcname}/systems/tops10-603/*
%dir %attr(0755,pidp10,pidp10) %{_sharedstatedir}/%{srcname}/systems/tops20
%attr(0644,pidp10,pidp10) %{_sharedstatedir}/%{srcname}/systems/tops20/*
%ghost %dir %attr(0755,pidp10,pidp10) /var/log/%{srcname}
%ghost %dir %attr(0755,pidp10,pidp10) /run/%{srcname}
%dir %{_sysconfdir}/%{srcname}
%config(noreplace) %{_sysconfdir}/%{srcname}/pidp10.conf
%{_prefix}/lib/firewalld/services/%{srcname}.xml
%config(noreplace) %attr(0440,root,root) %{_sysconfdir}/sudoers.d/pidp10
%{_datadir}/polkit-1/rules.d/50-pidp10.rules
%config(noreplace) %{_sysconfdir}/logrotate.d/pidp10

%files panel
%{_bindir}/pinctrl
%{_bindir}/pidp-gpio-probe
%{_bindir}/pidp10-test
%{_datadir}/bash-completion/completions/pinctrl
%{_udevrulesdir}/99-pidp10.rules
%doc src/utils/pinctrl/README.md

%files clients
%license src/sty33/LICENSE
# The .wav samples' CC-BY-SA-4.0 text; placed under this name in %%install
# so it does not collide with the top-level README.md.
%license %{_licensedir}/%{name}-clients/README.sounds
%{_bindir}/sty
%{_bindir}/st
%{_mandir}/man1/st.1*
%dir %{_datadir}/%{srcname}
# The GT40's VT11 boot ROM: the open-simh GT40 terminal (rpdp gt40, and
# pdp gt40 through conf/gt40.simh) boots from it.
%dir %{_datadir}/%{srcname}/conf
%{_datadir}/%{srcname}/conf/bootvt.img
%dir %{_datadir}/%{srcname}/sounds
%{_datadir}/%{srcname}/sounds/*
# sty and st draw in the TELETYPE 1945-1985 font.
%{_datadir}/fonts/%{srcname}

%files ui
%{_userunitdir}/pidp10-console.service
%{_userunitdir}/pidp10-teletype.service
%{_userunitdir}/pidp10-tv11.service
%{_userunitdir}/pidp10-view.service
%{_datadir}/applications/*.desktop
%{_datadir}/icons/hicolor/48x48/apps/con.png
%{_datadir}/icons/hicolor/48x48/apps/imlac.png
%{_datadir}/icons/hicolor/48x48/apps/knight.png
%{_datadir}/icons/hicolor/48x48/apps/pdp10.png
%{_datadir}/icons/hicolor/48x48/apps/pdp11.png
%{_datadir}/icons/hicolor/48x48/apps/telcon.png
%{_datadir}/icons/hicolor/48x48/apps/vt52.png
%{_datadir}/icons/hicolor/scalable/apps/*.svg
%{_datadir}/%{srcname}/view.json
%{_datadir}/%{srcname}/pidp10tilix.conf

%changelog
* Fri Nov 20 2026 Dennis Gilmore <dennis@ausil.us> - 4.1.0-4.20261106
- Load libpcap by its soname (libpcap.so.1) when the unversioned libpcap.so
  from libpcap-devel is not there; Recommend libpcap
- Require wget, unzip and tar for pidp10-fetch-images, which now checks for
  its tools before downloading
- Engine: refuse to boot a system whose disk or tape images are missing or
  empty, and say which pidp10-fetch-images to run; the boot scripts attach
  their media with "at -e" so simh no longer creates empty images
- Require pidp10-ui, which carries pidp10-tv11.service; drop %%{?_isa} from
  the noarch -ui package's requirement on pidp10

* Fri Nov 20 2026 Dennis Gilmore <dennis@ausil.us> - 4.1.0-3.20261106
- Recommend chaosnet-bridge (was a Suggests on cbridge, which nothing provides)
- pidp10-cbridge.service: make /tmp writable instead of the not yet existing
  /tmp/chaos_stream, which failed every start with 226/NAMESPACE; read the
  packaged chaos-hosts instead of /opt/pidp10/bin/chaos-hosts
- Engine: put libexec on PATH for the boot scripts' pidp10-spawn; attach the
  auxcpu port with -u so a restart does not hit TIME_WAIT; make /tmp writable;
  fix the X socket check that made every boot display=none; use SDL's dummy
  audio driver so opening the Type 340 window no longer deadlocks

* Fri Nov 20 2026 Dennis Gilmore <dennis@ausil.us> - 4.1.0-2.20261106
- split the bits needed for clients out to a -clients package
- make sure pdp pdp6 and pdp gt40 work

* Wed Nov 04 2026 Dennis Gilmore <dennis@ausil.us> - 4.1.0-1.20261106
- %%check boots every engine and runs the panel probe; scriptlets are the
  systemd macros; the runtime-only state dirs are %%ghost while the packaged
  system directories are real files owned by pidp10; no %%config under /usr;
  snapshot Release from git.
- The system directories (boot scripts plus the images they attach) install to
  /var/lib/pidp10/systems and are owned by pidp10, not to /usr/share: simh
  resolves every attach relative to the `cd systems/<name>` each boot script
  starts with, so script and image cannot live in two trees.
- First packaged form: FHS layout, services via presets, no setuid/setcap,
  media images fetched explicitly by pidp10-fetch-images.
