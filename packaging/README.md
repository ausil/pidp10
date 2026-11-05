# Packaging the PiDP-10

Everything here exists to get the project off `/opt/pidp10` and onto a normal
Linux system: binaries in `/usr/bin`, read-only data in `/usr/share`, writable
state in `/var/lib`, services managed by systemd, and no `chmod +s` anywhere.

## Where things live

| Path | Contents | Written at run time? |
|---|---|---|
| `/usr/bin/pdp10-{ka,ki,kl,ks}`, `/usr/bin/pdp6` | the simh engines | no |
| `/usr/bin/{sty,st,scansw10}` | terminals and the maintenance-switch reader the engine unit calls | no |
| `/usr/bin/{pdp,pdpcontrol}` | user front doors | no |
| `/usr/bin/pidp10-fetch-images` | fetches the OS/media images into the state tree (the package ships none) | no |
| `/usr/bin/{pinctrl,pidp-gpio-probe,pidp10-test}` | GPIO inspection, "is a cabinet attached?", cabinet bring-up test (`pidp10-panel`) | no |
| `%{_libdir}/pidp10/bin/` | prebuilt device simulators with no source in this tree (`pidp10-vendored`: cbridge, tv11, tvcon, imlac, vt05/50/52, ...) | no |
| `/usr/share/pidp10/systems/` | `boot.pi` / `boot.pidp` scripts and read-only system data | no |
| `/usr/share/pidp10/{conf,sounds,view.json}` | cbridge.conf, chaos-hosts, `.wav` samples, browser panel | no |
| `/usr/libexec/pidp10/` | `pidp10-engine`, `pidp10-teletype`, `pidp10-spawn`, `pidp10-stop`, `pidp10-terminal` | no |
| `/etc/pidp10/pidp10.conf` | operator configuration (`noreplace`) | yes, by you |
| `/var/lib/pidp10/` | **cwd of the simulator**: disk/tape images, `systems/<name>/` | yes |
| `/var/lib/pidp10/logs/`, `/var/log/pidp10/` | console and cbridge logs | yes |

The split that matters: the boot scripts are read-only data and live in
`/usr/share`, but the engine `cd`s to `/var/lib/pidp10` before running them, so
every `attach ./rp03.2` lands in the state tree. An upgrade can never overwrite
your ITS pack, and `ProtectSystem=strict` becomes possible.

## Where the paths come from

The top-level `Makefile`, and nowhere else:

```sh
make                           # everything into ./build
make install DESTDIR=/tmp/root PREFIX=/usr LIBEXECDIR=... VENDORBINDIR=...
```

It owns `PREFIX BINDIR LIBEXECDIR DATADIR VENDORBINDIR SYSCONFDIR UNITDIR
SYSUSERSDIR TMPFILESDIR UDEVDIR STATEDIR ...` and rewrites them into the units,
the helpers and the front doors at install time (`SUBST`), the same way the
`-DPIDP10_SOUNDS_DIR=...` define puts the sample directory into the Teletype at
compile time.

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
  find, and it cannot be out of date.
* `/etc/pidp10/pidp10.conf` holds **policy only** - `PIDP10_BOOT`,
  `PIDP10_PANEL`, `PIDP10_CONSOLE_PORT`. Layout variables in it would outrank
  the package and send a foreign-prefix build looking in `/usr`. Move the state
  directory with a unit drop-in (`Environment=PIDP10_STATE=...`), which at
  least shows up in `systemctl cat`.
* `install` refuses to run on an incomplete `build/`. Nothing is installed from
  `src/`: the panel tools are built by `src/utils/pinctrl`'s cmake, the engines
  by simh's makefile and the terminals by their own makefiles, all into
  `./build`, and the committed ELFs that used to sit next to the sources are
  gone from git (an installer that falls back to whatever is lying in `src/`
  ships a stale one silently).
* The same applies to `bin/`. What remains tracked there is the set with no
  source in this tree, plus `pidp10.panama`. The committed engines
  (`pdp10-{ka,ki,kl,ks}`, `pdp6`), the `pdp10-*.2024` snapshots, `st`, `sty`,
  `scansw10`, `pidp10-test`, `binaries-for-X86-64.tar.gz` and `pidp10` are
  untracked - `pidp10` was a byte-identical copy of `pdp10-ka` under the name
  `bin/pdpcontrol.sh` hardcodes, and `rcornwell/pidp10` was merged into
  `src/pdp10` at `02cb09a`, so there is one engine, not two.
* `PIDP10_HOME=<checkout>` remains a developer shortcut: it is used as a last
  resort when looking for the engines and the vendored sims, so
  `PIDP10_HOME=$PWD install/fedora/libexec/pidp10-engine 1` works with nothing
  installed. It is not a packaging knob.

## Building the RPM

```sh
packaging/build-rpm.sh                       # clean tarball, no binary blobs
packaging/build-rpm.sh --with-vendored       # also builds pidp10-vendored
packaging/build-rpm.sh --worktree            # package the tree as it stands
```

By default the tarballs are `git archive` of HEAD: what ships is what is
committed, the only state of the project anyone else can rebuild. A dirty work
tree is warned about rather than silently packaged; `--worktree` says explicitly
that you want the uncommitted version (it uses `git ls-files`, so `.gitignore`
still decides what is in - `build/` and `rpmbuild/` are not).

`Source0` never contains `bin/`. The 46 MB of prebuilt AI Lab peripherals -
ELF with no source in this tree - come in `Source2` and are unpacked only for
`--with vendored`, so the tarball a distribution verifies against a checksum
contains no unreviewed blob.

The `Release:` is `1.<gitdate>git<gitrev>` so a rebuilt snapshot upgrades
cleanly and is attributable.

Subpackages: `pidp10` (engines, helpers, units, config), `pidp10-panel`
(pinctrl, pidp-gpio-probe, pidp10-test, the `/dev/gpiochip*` udev rule),
`pidp10-ui` (fonts, icons, `.desktop` files, tilix layout), `pidp10-vendored`
(the ELF-only peripherals in `%{_libdir}/pidp10/bin`). `pidp10` Recommends
`pidp10-panel`, because the engine unit asks the probe whether a cabinet is
attached; both helpers degrade without it.

Two switches:

* `--with vendored` installs `bin/`'s prebuilt ELFs into `pidp10-vendored`
  (`cbridge`, `tv11`, `tvcon`, `imlac`, `vt05/50/52`, ...). Off by default, so a
  build that forbids binary blobs still produces a working simulator (you lose
  cbridge/tv11/the exotic terminals), and **also off on any architecture but
  aarch64**: what upstream publishes there is aarch64 ELF, so `rpmbuild
  --with vendored` on x86-64 says so in `%build` and packages nothing rather
  than installing binaries that cannot execute. `Source2` is declared only when
  it will be used, so an SRPM built without vendored carries no blob at all.

  Most of that set does have an upstream and should be built from it instead -
  `aap/vt05` (vt05 vt50 vt52 dp3300 dm2500 gecon, MIT), `aap/pdp11` (tv11
  tvcon), `rricharz/Tek4010` (GPL-3.0), `bictorv/chaosnet-bridge` (Apache-2.0),
  `PDP-10/supdup`, `Chaosnet/chaosnet-tools` (gw mlftp shutdown gateway.sh),
  `aap/pdp6` (mkpty33) and `jdersch/sImlac` (imlac). Each belongs in its own
  repo and spec, because they are DEC/Datapoint/IMLAC emulators that other PDP
  projects use too; `install.sh` already fetches most of them as `PDP-10/its`
  submodules.
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

System scope: `pidp10-cbridge.service` (chaosnet) and
`pidp10-engine@<octal-boot>.service`.
User scope: `pidp10.target` plus `pidp10.service` / `pidp10@<boot>.service`,
`pidp10-teletype`, `pidp10-tv11`, `pidp10-view`, `pidp10-console`.

Engines run as an unprivileged user with `AmbientCapabilities=CAP_SYS_NICE`
instead of setuid/setcap; RT priority comes from
`/usr/lib/systemd/logind.conf.d/60-pidp10-rt.conf`.

Boot numbers are the octal switch-register values (`0200` = TOPS-20), and
`systems/selections` maps them to a system directory.

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
copr-cli build pdp10 --nowait --with vendored      # cabinet extras, aarch64 only
dnf install -y dnf-plugins-core && dnf copr enable <you>/pdp10 && dnf install pidp10
```

Enable aarch64 *and* x86-64: the engines are architecture-neutral (everything
is compiled, `%check` boots all five), and the x86-64 chroot is what keeps the
vendored arch guard honest. Multi-spec projects (`:multi-spec: yes`) are the
next step once the terminal packages have specs of their own - one project so
`pidp10`'s `Recommends: vt05 tek4010 imlac` resolve in the repo the user just
enabled, separate SRPMs so each keeps its own version and licence.

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
packaging/build-rpm.sh --worktree --with-vendored /tmp/rpb
rpmlint /tmp/rpb/RPMS/*/*.rpm            # only spelling-of-domain-terms left
systemd-analyze verify /tmp/rpb/BUILD/.../BUILDROOT/usr/lib/systemd/system/*
udevadm verify install/fedora/udev.d/99-pidp10.rules
```
