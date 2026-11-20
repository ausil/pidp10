# Packaging the PiDP-10

Everything here exists to get the project off `/opt/pidp10` and onto a normal
Linux system: binaries in `/usr/bin`, read-only data in `/usr/share`, writable
state in `/var/lib`, services managed by systemd, and no `chmod +s` anywhere.

## Where things live

| Path | Contents | Written at run time? |
|---|---|---|
| `/usr/bin/pdp10-{ka,ki,kl,ks}`, `/usr/bin/pdp6` | the simh engines | no |
| `/usr/bin/{sty,st}`, `/usr/share/pidp10/sounds`, `/usr/share/fonts/pidp10` | the Teletype terminal, the plain one it is built from, their recordings and font (`pidp10-clients`) | no |
| `/usr/bin/scansw10` | the maintenance-switch reader the engine unit calls | no |
| `/usr/bin/{pdp,pdpcontrol}` | user front doors | no |
| `/usr/share/man/man1/{pdp,pdpcontrol}.1` | their pages (the engines and panel tools are driven through the units; their interface is documented here) | no |
| `/usr/bin/pidp10-fetch-images` | fetches the OS/media images into the state tree (the package ships none) | no |
| `/usr/bin/{pinctrl,pidp-gpio-probe,pidp10-test}` | GPIO inspection, "is a cabinet attached?", cabinet bring-up test (`pidp10-panel`) | no |
| `/usr/bin/{cbridge,tv11,tvcon,imlac,vt05,vt50,vt52,dp3300,dm2500,gecon,tek4010,supdup,gw}` | the device sims the boot scripts call - **not from this package**: each has its own upstream and its own spec (see "The device sims") | no |
| `/usr/share/pidp10/{conf,view.json}` | cbridge.conf, chaos-hosts, the device-sim configs, browser panel | no |
| `/usr/libexec/pidp10/` | `pidp10-engine`, `pidp10-screen`, `pidp10-session`, `pidp10-stop`, `pidp10-teletype`, `pidp10-spawn`, `pidp10-terminal` | no |
| `/etc/pidp10/pidp10.conf` | operator configuration (`noreplace`) | yes, by you |
| `/etc/sudoers.d/pidp10` | lets the `pidp10` group attach to the engines' screen sessions (`noreplace`) | no |
| `/usr/share/polkit-1/rules.d/50-pidp10.rules` | lets the `pidp10` group start/stop/restart the engine units | no |
| `/var/lib/pidp10/` | **cwd of the simulator**: disk/tape images, logs | yes |
| `/var/lib/pidp10/systems/<name>/` | the boot scripts (`boot.pi` / `boot.pidp`), `selections`, **and the images they attach** | yes - the scripts ship with the package, the images are fetched |
| `/var/lib/pidp10/logs/`, `/var/log/pidp10/` | console logs (`pidp10-console.log`, `pidp6-console.log`, rotated by `/etc/logrotate.d/pidp10`) and cbridge logs | yes |

The split that matters: everything the simulator writes lives in
`/var/lib/pidp10`, and the engine `cd`s there before running a boot script, so
`ProtectSystem=strict` becomes possible and an upgrade can never overwrite your
ITS pack. The boot scripts are in that tree rather than in `/usr/share` for one
reason: they are not separable from the images. Each starts with
`cd systems/<name>`, simh resolves every `attach` relative to that directory,
and `pidp10-fetch-images` unpacks the media into it - so script and image are
one directory, and that directory has to be writable by the engine account.
Everything else the software only reads (`conf/`, `sounds/`, `view.json`, the
desktop entries) stays in `/usr/share`.

A consequence of putting the boot scripts in the state tree: they are package
files, so an upgrade replaces them (they are not `%config` - Fedora accepts no
`%config` outside `/etc`, and this package keeps `/usr` stateless). To customise
a boot durably, make a system of your own and point the selections table at it:

```sh
cp -a /var/lib/pidp10/systems/its /var/lib/pidp10/systems/myits
$EDITOR /var/lib/pidp10/systems/myits/boot.pi
# start from the shipped table - an unlisted number falls back to
# hills-blinky, so replacing the file wholesale loses the other systems
cp /var/lib/pidp10/systems/selections /etc/pidp10/selections
printf '0010\tmyits\n' >> /etc/pidp10/selections   # pdpcontrol start 8
```

and set `PIDP10_SELECTIONS=/etc/pidp10/selections` in `/etc/pidp10/pidp10.conf`:
`pidp10-engine` and `pdpcontrol` both source that file and the units load it
through `EnvironmentFile=`, so one line covers `systemctl start pidp10-engine@1` and
`pdpcontrol start 1`. It is the one path the config file is allowed to name -
unlike `PIDP10_PREFIX`/`PIDP10_STATE` it is a file the operator owns, not the
layout the build was given.

## Where the paths come from

The top-level `Makefile`, and nowhere else:

```sh
make                           # everything into ./build
make install DESTDIR=/tmp/root PREFIX=/usr LIBEXECDIR=... DATADIR=...
```

It owns `PREFIX BINDIR LIBEXECDIR DATADIR SYSCONFDIR UNITDIR
SYSUSERSDIR TMPFILESDIR UDEVDIR STATEDIR SYSTEMSDIR ...` and rewrites them into
the units, the helpers and the front doors at install time (`SUBST`), the same
way the `-DPIDP10_SOUNDS_DIR=...` define puts the sample directory into the
Teletype at compile time.

There is deliberately **no runtime path-resolution library**. An earlier
revision shipped `pidp10-paths.sh`, sourced by every helper after a three-candidate
search with a hardcoded `/usr/libexec/pidp10` fallback; it duplicated the config
file's parser, inverted the documented precedence (the config file overwrote the
caller's environment), and its failure mode was a silently wrong tree rather
than an error. Build-time substitution is not cleverer, it is just earlier: what
`systemctl cat` and `pdp` show each other is decided when the package is built.

The consequences worth knowing:

* A helper that needs a default writes `: "${PIDP10_STATE:=/var/lib/pidp10}"`.
  Six lines of that duplication is cheaper than one file six things have to
  find, and it cannot be out of date. `PIDP10_SYSTEMS` defaults to
  `$PIDP10_STATE/systems` *derived from* `PIDP10_STATE`, so moving the state
  tree moves the systems with it.
* `/etc/pidp10/pidp10.conf` holds **policy only** - `PIDP10_BOOT`,
  `PIDP10_PANEL`, `PIDP10_CONSOLE_PORT`, and `PIDP10_SELECTIONS` (which
  selections table to read: a file the operator owns, so naming it is policy,
  not layout). The layout variables are not in it: they would outrank the
  package and send a foreign-prefix build looking in `/usr`. Move the state
  directory with a unit drop-in (`Environment=PIDP10_STATE=...`), which at
  least shows up in `systemctl cat`; `PIDP10_SYSTEMS` follows it, defaulting to
  `$PIDP10_STATE/systems`.
* `install` refuses to run on an incomplete `build/`. Nothing is installed from
  `src/`: the panel tools are built by `src/utils/pinctrl`'s cmake, the engines
  by simh's makefile and the terminals by their own makefiles, all into
  `./build`, and the committed ELFs that used to sit next to the sources are
  gone from git (an installer that falls back to whatever is lying in `src/`
  ships a stale one silently).
* Nothing is installed from `bin/` either, and nothing under it is tracked: it
  is scratch for whatever `make` dropped there and whatever prebuilt sim someone
  fetched to try. Installed data sits with what uses it - the Teletype
  recordings are `src/sty33/sounds/*.wav`, which is where `sounds.c` expects
  them to have come from - or under `install/fedora/conf` for config this
  project writes (`cbridge.conf`, `chaos-hosts`, `init.ini`, `gt40.simh`,
  `imlac.simh`, `ssv22.iml`, `bootvt.img`). Keeping that data in `bin/` had made
  it a *payload* of the prebuilt-ELF tarball, so a build without that tarball
  silently lost the sounds and the Chaosnet config; installs are unguarded now,
  so a missing data file fails the build instead. The committed engines
  (`pdp10-{ka,ki,kl,ks}`, `pdp6`), the `pdp10-*.2024` snapshots, `st`, `sty`,
  `scansw10`, `pidp10-test`, `binaries-for-X86-64.tar.gz`, `pidp10` and
  `pidp10.panama` are all untracked now: `pidp10` was a byte-identical copy of
  `pdp10-ka` under the name the old `pdpcontrol.sh` hardcodes, and
  `rcornwell/pidp10` was merged into `src/pdp10` at `02cb09a`, so there is one
  engine, not two. `pidp10.panama` was the panama5-era engine that build
  displaced on 2024-03-13 (md5-identical to `bin/pidp10` as of `1e81df0^`); its
  sources are this repository's own history at `569c61a^`, and they add nothing
  `src/pdp10/PDP10/` does not already have except the pre-`pidp_panel` panel
  code this branch removed.
* `PIDP10_HOME=<checkout>` remains a developer shortcut: it is used as a last
  resort when looking for the engines and for a locally built device sim, so
  `PIDP10_HOME=$PWD install/fedora/libexec/pidp10-engine 1` works with nothing
  installed. It is not a packaging knob.

## Building the RPM

```sh
make rpm                                     # RPMs + SRPM into ./dist-rpm
make srpm                                    # just the SRPM into ./dist-rpm (COPR, koji, mock)
make rpmlint                                 # build them first, then lint
make rpmlint WORKTREE=yes                    # ... from the tree as it stands

packaging/build-rpm.sh                       # snapshot RPMs from HEAD
packaging/build-rpm.sh --worktree            # package the tree as it stands
packaging/build-rpm.sh --srpm-only           # just the SRPM (what COPR runs)
```

The layout: `dist/` holds `Source0`/`Source1`, `dist-rpm/` holds nothing but
the finished RPMs (`_sourcedir`/`_rpmdir`/`_srcrpmdir` overrides,
`_rpmfilename` without the arch subdirectory), and the rpmbuild scratch stays
in `~/rpmbuild` - this tree gets no `BUILD/` or `SOURCES/` of its own.

By default the tarball is `git archive` of HEAD: what ships is what is
committed, the only state of the project anyone else can rebuild. A dirty work
tree is warned about rather than silently packaged; `--worktree` says explicitly
that you want the uncommitted version (it uses `git ls-files`, so `.gitignore`
still decides what is in - `build/`, `dist/` and `dist-rpm/` are not).

`Source0` is the tree, and that is the whole story: nothing prebuilt is
packaged, so there is no second tarball of ELFs, no `--with` switch to opt into
shipping someone's binary, and no architecture special case to keep it honest.
The device sims the cabinet wants are packages of their own (below), built by
whoever maintains them.

Nothing in the NVR is derived from git. `%global srcdate` in the spec is the
snapshot segment (`Release: 1.%{srcdate}%{?dist}`), bumped when a release is
cut, and the upstream commit a build snapshots is recorded in `%changelog`.
A git-derived version is stale the moment it is committed, and a bare
`rpmbuild -bs` would go looking for a tarball name nobody produced — so
`packaging/build-rpm.sh` reads the `Source0` name from the spec
(`rpmspec -P`) instead of inventing one, and passes no `--define` that the
spec cannot resolve by itself.

Subpackages: `pidp10` (engines, helpers, units, config), `pidp10-panel`
(pinctrl, pidp-gpio-probe, pidp10-test, the `/dev/gpiochip*` udev rule),
`pidp10-clients` (`sty`, `st`, the Teletype sounds and fonts), `pidp10-ui`
(icons, `.desktop` files, tilix layout). `pidp10` Recommends `pidp10-panel`,
because the engine unit asks the probe whether a cabinet is attached; both
helpers degrade without it. `pidp10` Requires `pidp10-clients`, which needs
nothing else from this package: it is what a desktop or laptop installs to
reach a PiDP-10 over the network with rpdp (`rpdp con` runs `sty`), without
pulling in the engines, units, sudoers and system user.

One switch:

* `--with kiosk_default` flips the systemd presets to enable the cabinet
  service at install, for a machine whose only purpose is blinkenlights.
  Off by default: a package that starts a PDP-10 on someone's laptop unasked
  is a package people remove.

Services are enabled through preset files rather than `systemctl enable` in a
scriptlet, so `systemctl preset-all` is what decides, and the scriptlets stay
offline (no network — the ~2 GB of ITS/TOPS/WAITS media is fetched deliberately
by `pidp10-fetch-images`, which verifies checksums). `%check` boots every
engine with `PIDP10_PANEL=sim`, runs the panel probe's three non-destructive
modes, and validates the desktop files.

## Services

System scope, all running as the `pidp10` user:

* `pidp10-engine@<boot>.service`: the PDP-10, in a detached screen session
  named `pidp10`. The instance is the octal boot number, or `default` for
  `PIDP10_BOOT` / the panel switches (`systemctl enable
  pidp10-engine@default.service` boots it with the machine).
* `pidp10-pdp6.service`: the PDP-6, in screen session `pidp6`. It shares memory
  with ITS on the KA10 (boot 1) through the KA10's `auxcpu` listener on port
  10006; its console is the session itself.
* `pidp10-cbridge.service`: Chaosnet.

This is the PiDP-11's layout (`pidp11-simh.service`), for the same reason:
simh reads its `sim>` prompt from stdin, and the telnet listener on port 1025
is the PDP-10's terminal, not that prompt. As a bare unit with stdin on
`/dev/null` there was no way to reach `sim>`, and a ^E on the console halted
the machine for good. Now:

```sh
sudo usermod -aG pidp10 $USER   # once, then log in again
pdpcontrol start 1              # ITS; no password, via the polkit rule
pdp                             # attach to sim> (^E stops, "c" continues, Ctrl-A d detaches)
pdp con                         # the PDP-10's own terminal (telnet 1025)
systemctl start pidp10-pdp6.service
pdp pdp6                        # the PDP-6 console
```

Attaching runs `sudo -u pidp10 screen -x <session>`, which
`/etc/sudoers.d/pidp10` allows for the `pidp10` group. Treat that group as
trusted: an attached user can type simh `!` shell escapes, which run as
`pidp10`. Sessions are logged to `/var/log/pidp10/<session>-console.log`.

User scope (the desktop): `pidp10.target` with `pidp10-xhost` (runs
`xhost +SI:localuser:pidp10` so the engine can open its Type 340 / DD windows
on this display), `pidp10-teletype`, `pidp10-tv11`, `pidp10-view`,
`pidp10-console`. Without an X server the engine uses SDL's dummy driver.

The engines get `AmbientCapabilities=CAP_SYS_NICE` and `LimitRTPRIO=99` from
their units instead of setuid/setcap. screen runs through
`/usr/libexec/pidp10/pidp10-screen` (SELinux will not let systemd exec it
directly), and its sockets live in a private `SCREENDIR=/run/pidp10/screen`
(0700, `pidp10`): `NoNewPrivileges=` turns off screen's setgid bit, and without
it screen refuses the shared `/run/screen` unless that is mode 777. To look at
the sessions by hand: `sudo -u pidp10 SCREENDIR=/run/pidp10/screen screen -ls`.

Boot numbers are the octal switch-register values (`0200` = TOPS-20), and
`/var/lib/pidp10/systems/selections` maps them to a system directory.

## The device sims

The boot scripts and two units call programs this package does not build and
must not ship: `cbridge` (Chaosnet over UDP), `tv11`/`tvcon` (Knight TV), the
`vt05`/`vt50`/`vt52`/`dp3300`/`dm2500`/`gecon` terminals, `tek4010`, `imlac`,
`supdup`, and the Chaosnet `gw`/`mlftp`/`shutdown`/`gateway.sh`. They were
committed here as aarch64 ELFs with no source, which meant a distribution build
either shipped unreviewed binaries or lost the cabinet; both are worse than
saying they are someone else's programs.

They are looked up on `PATH`, and the units that need one gate on it
(`ConditionPathExists=/usr/bin/cbridge`), so an absent sim means a skipped unit
rather than a failed service, and `pdp` says which package to install. The
spec names them as `Suggests:` (`vt05`, `tek4010`, `imlac`, `tv11`, `tvcon`,
`cbridge`) so `dnf` offers them when it offers this package - suggested, not
required, because a headless box or a Teletype-only cabinet needs none of them
and an unresolvable suggestion is silently skipped by dnf, which is what lets
`imlac`/`tvcon`/`cbridge` be named before their specs join the COPR. Each
belongs in its own repo and spec - they are DEC, Datapoint and IMLAC emulators
that other PDP projects use too, and they have licenses of their own:

| programs | upstream | licence |
|---|---|---|
| `vt05 vt50 vt52 dp3300 dm2500 gecon` | `aap/vt05` | MIT |
| `tv11 tvcon` | `aap/pdp11` | BSD-2 |
| `tek4010` | `rricharz/Tek4010` | GPL-3.0 |
| `cbridge` | `bictorv/chaosnet-bridge` | Apache-2.0 |
| `supdup` | `PDP-10/supdup` | to confirm |
| `gw mlftp shutdown gateway.sh` | `Chaosnet/chaosnet-tools` | to confirm |
| `mkpty33` | `aap/pdp6` (`tools/mkpty33.c`) | MIT |
| `imlac` | `jdersch/sImlac` | to confirm |

`PDP-10/its`'s `.gitmodules` is the map these came from (`tools/vt05`,
`tools/tek4010`, `tools/tv11`, `tools/cbridge`, `tools/supdup`, ...), which is
also how the old `install.sh` fetched them. Until those specs exist, build one
from its upstream and drop the binary in `bin/` (ignored, found via
`PIDP10_BIN`/`PIDP10_HOME`) - that is what `bin/` is for now.

## COPR

`.copr/Makefile` implements the `make srpm` step that COPR's "build from git"
method runs, by delegating to `packaging/build-rpm.sh --srpm-only` - the
snapshot `Release:` and the blob-free `Source0` are that script's business, and
a second implementation here is how a COPR build ends up shipping something a
local build cannot reproduce.

```sh
copr-cli create pdp10 --chroot fedora-45-aarch64 --chroot fedora-45-x86_64 \
    --scm-type git --scm-metadata https://github.com/<you>/pidp10
copr-cli build pdp10 --nowait                      # the clean build
dnf install -y dnf-plugins-core && dnf copr enable <you>/pdp10 && dnf install pidp10
```

Enable aarch64 *and* x86-64: the engines are architecture-neutral (everything
is compiled, `%check` boots all five), and the x86-64 chroot is what catches
architecture assumptions early. Multi-spec projects (`:multi-spec: yes`) are the
next step once the terminal packages have specs of their own - one project so
the `Suggests:` above (`vt05 tek4010 imlac tv11 tvcon cbridge`) resolve in the
repo the user just enabled, separate SRPMs so each keeps its own version and
licence.

## Debian

No `debian/` directory yet, deliberately: this development box has no dpkg
tooling, and shipping an untested `debian/rules` is worse than shipping none.
The work is small now that the layout lives in the Makefile — `debian/rules`
should be

```make
override_dh_auto_install:
        $(MAKE) install DESTDIR=$(CURDIR)/debian/pidp10 PREFIX=/usr
```

and the packaging split mirrors the `%files` sections of `pidp10.spec`. On a
Debian-capable machine, verify with `lintian` before trusting any of it.

## Verifying a build

```sh
pidp-gpio-probe --check      # exit 0 only if a real Broadcom panel is there
pidp-gpio-probe --selftest   # drives the mux loop on the sim backend
pidp10-test 10               # lamps + switch echo, works on the sim backend too
pdpcontrol stat
```

`pidp-gpio-probe --check` is what the engine unit asks instead of the old
`gpioinfo | grep gpio`, which was true on any machine with GPIO at all -
including a laptop whose eleven gpiochips are PMIC power rails.

Package-level checks used while developing this:

```sh
make rpmlint WORKTREE=yes RPM_DIR=/tmp/rpb  # clean; exceptions live in packaging/rpmlint.toml
systemd-analyze verify ~/rpmbuild/BUILD/pidp10-*/BUILDROOT/usr/lib/systemd/system/*
udevadm verify install/fedora/udev.d/99-pidp10.rules
```

Two checks worth running whenever data or units move, because both failures are
silent otherwise:

```sh
rpm -qlp /tmp/rpb/pidp10-*.aarch64.rpm | grep -c 'sounds/.*\.wav'   # 40
rpm -qlp /tmp/rpb/pidp10-*.aarch64.rpm | grep 'conf/'               # 7 files
grep -o '"[a-z0-9-]*\.wav"' src/sty33/sounds.c | sort -u | wc -l                 # also 40
```

The Teletype degrades to silence rather than an error when its samples are
missing, and `pidp10-cbridge.service` / `pidp10-tv11.service` are skipped by
their `ConditionPathExists=` when the device sims are not installed - which is
the intended behaviour, but only if it is deliberate.
