# Top-level build and install for the PiDP-10 software.
#
#   make                    build everything into ./build
#   make install DESTDIR=/tmp/root PREFIX=/usr
#   make rpm                RPMs + source RPM into ./dist-rpm
#   make rpmlint            build the RPMs, then check them with rpmlint
#
# Builds are out of tree (into $(BUILDDIR)); the binaries tracked in the
# repository are never touched, and "make install" refuses to run on an empty
# $(BUILDDIR) rather than falling back to a committed ELF. A stale binary that
# installs silently is the failure mode this file exists to prevent: upstream's
# installer copied whatever sat in src/, and the tree keeps prebuilt copies of
# st, sty, scansw10 and pidp10-test for the benefit of people who do not build.
#
# CFLAGS/LDFLAGS from the environment are honoured (rpmbuild's, %set_build_flags).
#
# Needs: gcc, make, cmake, pkg-config, libgpiod-devel, readline or libedit,
# libpcap-devel, zlib/libpng (simh video); SDL2 (+image/ttf/mixer) and X11/Xft,
# fontconfig, freetype for sty and st.

PREFIX        ?= /usr
BINDIR        ?= $(PREFIX)/bin
LIBEXECDIR    ?= $(PREFIX)/libexec/pidp10
DATADIR       ?= $(PREFIX)/share/pidp10
SYSCONFDIR    ?= /etc
UNITDIR       ?= $(PREFIX)/lib/systemd/system
USERUNITDIR   ?= $(PREFIX)/lib/systemd/user
PRESETDIR     ?= $(PREFIX)/lib/systemd/system-preset
USERPRESETDIR ?= $(PREFIX)/lib/systemd/user-preset
SYSUSERSDIR   ?= $(PREFIX)/lib/sysusers.d
TMPFILESDIR   ?= $(PREFIX)/lib/tmpfiles.d
UDEVDIR       ?= $(PREFIX)/lib/udev/rules.d
LOGINDDIR     ?= $(PREFIX)/lib/systemd/logind.conf.d
FIREWALLDIR   ?= $(PREFIX)/lib/firewalld/services
ICONSDIR      ?= $(PREFIX)/share/icons/hicolor
APPSDIR       ?= $(PREFIX)/share/applications
MANDIR        ?= $(PREFIX)/share/man
FONTSDBIR     ?= $(PREFIX)/share/fonts/pidp10
COMPLETIONDIR ?= $(PREFIX)/share/bash-completion/completions
STATEDIR      ?= /var/lib/pidp10
# The system directories (boot scripts, and the images the boot scripts attach)
# live in the state tree, not in $(DATADIR). Not a stylistic choice: every boot
# script opens with `cd systems/<name>`, which simh resolves relative to its cwd
# (= $(STATEDIR)), so the script and the disk images it attaches have to sit in
# one directory - and pidp10-fetch-images unpacks the images into exactly that
# directory. Two trees, one directory: the only split that works is scripts and
# images together, which makes the directory writable by the engine account.
SYSTEMSDIR    ?= $(STATEDIR)/systems

BUILDDIR      ?= build
# WITH_STY=no skips the two X11/SDL terminals (sty/st need SDL2 and X11).
WITH_STY      ?= yes

CC      ?= gcc
CFLAGS  ?= -O2 -g
B       := $(abspath $(BUILDDIR))

PDP10D   := src/pdp10
PINCTRLD := src/utils/pinctrl
# Everything the pinctrl cmake build produces: the Raspberry Pi CLI, the panel
# probe the engine unit asks, and the two cabinet tools.
PANEL_BINS     := pinctrl pidp-gpio-probe scansw10 pidp10-test
ENGINES        := pdp10-ka pdp10-ki pdp10-kl pdp10-ks pdp6
# The device simulators that used to be installed from bin/ (cbridge, tv11,
# tvcon, imlac, vt05/50/52, dp3300, dm2500, gecon, tek4010, supdup, gw, mlftp,
# shutdown, mkpty33) are not built or installed here. They have upstreams and
# belong in their own packages on PATH - the spec names them as Suggests - so
# nothing under bin/ is tracked and nothing is installed from it; the data the
# helpers read lives in src/sty33/sounds and install/fedora/conf.

# Rewrite the install locations baked into the units, the helpers and the
# front-ends. They are written with /usr paths so the tree is readable and
# systemd-analyze verify works on a checkout; a build with another prefix has
# to have them rewritten here rather than discovering them at run time, which
# is what a "paths" shell library is for in other projects and what makes
# `systemctl cat` and `pdp` agree without either hunting for a config file.
# The systems rules have to come before the two that match /var/lib/pidp10 and
# /usr/share/pidp10 as prefixes, or a build with a custom SYSTEMSDIR rewrites
# the systems paths into the state or data root. The /usr/share spellings match
# nothing in the tree today (the system directories moved to the state tree);
# they are kept so a file that still names the pre-move location - copied out of
# an old boot script or an upstream README - is redirected with it instead of
# being installed pointing at a directory this package does not create.
SUBST = sed -e 's|/usr/libexec/pidp10|$(LIBEXECDIR)|g' \
            -e 's|/var/lib/pidp10/systems|$(SYSTEMSDIR)|g' \
            -e 's|$(PREFIX)/share/pidp10/systems|$(SYSTEMSDIR)|g' \
            -e 's|/usr/share/pidp10/systems|$(SYSTEMSDIR)|g' \
            -e 's|$(PREFIX)/share/pidp10|$(DATADIR)|g' \
            -e 's|/usr/share/pidp10|$(DATADIR)|g' \
            -e 's|/var/lib/pidp10|$(STATEDIR)|g' \
            -e 's|/etc/pidp10|$(SYSCONFDIR)/pidp10|g'

# simh's makefile assembles its own compile line: ${GCC} is the compiler,
# CFLAGS_O the optimisation block and LDFLAGS the link line. WARNINGS=ALLOWED
# because the makefile adds -Werror unless it sees that string, and the simh
# sources are not warning-clean at a distribution's -O2 (-Wall from
# %optflags then fails the build on "may be used uninitialized" in code we do
# not maintain). The warnings are still printed; they are just not fatal.
SIMH_FLAGS = GCC='$(CC)' OPTIMIZE='$(CFLAGS) -DNDEBUG=1' \
             EXTRA_LDFLAGS='$(LDFLAGS)' WARNINGS=ALLOWED PIDP10=1

.PHONY: all engines panel tools sty st install clean clean-all

all: engines panel $(if $(filter yes,$(WITH_STY)),sty st)

$(B):
	mkdir -p $(B)

# --- the simh engines -------------------------------------------------------
# PIDP10=1 compiles in the front-panel thread, which picks gpiod, gpiomem or
# sim at run time, so the same binary drives a cabinet or runs headless.
engines: | $(B)
	$(MAKE) -C $(PDP10D) $(SIMH_FLAGS) BIN=$(B)/ $(ENGINES)

# --- GPIO tools, panel probe, switch register, cabinet test ----------------
# cmake, because pinctrl comes from Raspberry Pi OS that way. LTO is switched
# off per target in its CMakeLists: the gpiochip_* tables are found through the
# linker-provided __start_gpiochips and LTO deletes them as dead code.
#
# scansw10 and pidp10-test live here rather than in their own directories with
# their own makefiles because all four programs are the same panel code: the
# old copies of the two tools reached the hardware by mapping registers, so
# they needed sudo, ran only on Raspberry Pi OS and could not tell a broken
# panel driver from a broken panel. They go through pidp_panel now, like the
# simulator does.
panel: | $(B)
	cmake -S $(PINCTRLD) -B $(B)/pinctrl-build \
	    -DCMAKE_C_COMPILER='$(CC)' -DCMAKE_BUILD_TYPE=Release \
	    -DCMAKE_C_FLAGS='$(CFLAGS)' -DCMAKE_EXE_LINKER_FLAGS='$(LDFLAGS)' \
	    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$(B)
	cmake --build $(B)/pinctrl-build
	@test -x $(B)/pidp-gpio-probe || \
	    echo "NOTE: pidp-gpio-probe not built (needs libgpiod >= 2.0); the"\
	         "front panel falls back to sim on kernels without /dev/gpiomem" >&2

# Kept because muscle memory and old scripts call it; it is the same build.
tools: panel

# --- terminals (X11 + SDL; the st makefile builds in its source tree) -------
# The sound directory is compiled in, not guessed at run time: the samples are
# read on every keystroke and $PIDP10_SOUNDS stays as the override.
sty: | $(B)
	$(MAKE) -C src/sty33 CC='$(CC)' CFLAGS='$(CFLAGS)' LDFLAGS='$(LDFLAGS)' CPPFLAGS='$(CPPFLAGS) -DPIDP10_SOUNDS_DIR=\"$(DATADIR)/sounds\"'
	cp -f src/sty33/sty $(B)/sty

st: | $(B)
	$(MAKE) -C src/st-0.9.2 CC='$(CC)' CFLAGS='$(CFLAGS)' LDFLAGS='$(LDFLAGS)' CPPFLAGS='$(CPPFLAGS)'
	cp -f src/st-0.9.2/st $(B)/st

# --- install ----------------------------------------------------------------
# Installs what "make" built, and nothing else: deliberately does not rebuild,
# because an RPM build runs "make install" in a different environment from
# "make" (different flags, no network), and a silent rebuild there produces a
# binary that was not tested by the test phase.
install:
	@missing=""; for f in $(ENGINES) $(PANEL_BINS); do \
	    [ -x $(B)/$$f ] || missing="$$missing $$f"; done; \
	# pidp-gpio-probe is the one product that may legitimately be absent: it
	# needs libgpiod, which a build host is not required to have.
	missing="$$(echo $$missing | sed 's/ pidp-gpio-probe//')"; \
	if [ "$${missing# }" != "" ]; then \
	    echo "make install: not built in $(B):$$missing - run 'make' first" >&2; \
	    [ "$${PIDP10_ALLOW_PARTIAL:-}" = 1 ] || exit 1; \
	fi
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(LIBEXECDIR) $(DESTDIR)$(STATEDIR) \
	    $(DESTDIR)$(SYSTEMSDIR) $(DESTDIR)$(DATADIR)/conf $(DESTDIR)$(DATADIR)/sounds \
	    $(DESTDIR)$(SYSCONFDIR)/pidp10 $(DESTDIR)$(UNITDIR) $(DESTDIR)$(USERUNITDIR) \
	    $(DESTDIR)$(PRESETDIR) $(DESTDIR)$(USERPRESETDIR) $(DESTDIR)$(SYSUSERSDIR) \
	    $(DESTDIR)$(TMPFILESDIR) $(DESTDIR)$(UDEVDIR) $(DESTDIR)$(LOGINDDIR) \
	    $(DESTDIR)$(FIREWALLDIR) $(DESTDIR)$(ICONSDIR)/48x48/apps \
	    $(DESTDIR)$(ICONSDIR)/scalable/apps $(DESTDIR)$(APPSDIR) \
	    $(DESTDIR)$(FONTSDBIR) $(DESTDIR)$(COMPLETIONDIR) $(DESTDIR)$(MANDIR)/man1
	install -m 0755 $(addprefix $(B)/,$(ENGINES)) $(DESTDIR)$(BINDIR)/
	install -m 0755 $(B)/pinctrl $(B)/scansw10 $(B)/pidp10-test $(DESTDIR)$(BINDIR)/
	@# pidp-gpio-probe is only built when libgpiod is present.
	@-install -m 0755 $(B)/pidp-gpio-probe $(DESTDIR)$(BINDIR)/ 2>/dev/null
	@# Completion files are sourced by bash, never executed; the upstream copy
	@# opens with a shebang, which makes a linter demand an executable bit that
	@# a file in datarootdir/completions must not have.
	sed '1{/^\#!/d}' $(PINCTRLD)/pinctrl-completion.bash \
	    > $(DESTDIR)$(COMPLETIONDIR)/pinctrl
	chmod 0644 $(DESTDIR)$(COMPLETIONDIR)/pinctrl
	@# helpers and user commands, with the paths rewritten
	for h in install/fedora/libexec/*; do \
	    $(SUBST) $$h > $(DESTDIR)$(LIBEXECDIR)/$$(basename $$h); \
	    chmod 0755 $(DESTDIR)$(LIBEXECDIR)/$$(basename $$h); done
	for u in install/fedora/bin/*; do \
	    $(SUBST) $$u > $(DESTDIR)$(BINDIR)/$$(basename $$u); \
	    chmod 0755 $(DESTDIR)$(BINDIR)/$$(basename $$u); done
	$(SUBST) packaging/pidp10-fetch-images.sh > $(DESTDIR)$(BINDIR)/pidp10-fetch-images
	chmod 0755 $(DESTDIR)$(BINDIR)/pidp10-fetch-images
	@# The system directories: the boot scripts (the ~2 GB of disk/tape images
	@# they attach are fetched into the same directories by
	@# pidp10-fetch-images, never shipped here). Installed into $(SYSTEMSDIR)
	@# rather than $(DATADIR) because a boot script and the images it attaches
	@# are one directory: simh resolves `attach rp0.dsk` against its cwd, and
	@# every boot script starts with `cd systems/<name>`. The directories are
	@# therefore written to at run time and have to belong to the engine
	@# account; the spec does that with %%attr, and the tmpfiles fragment
	@# (install/fedora/tmpfiles.d) repairs it after a plain `make install`,
	@# which cannot set an owner on a build host that has no pidp10 user.
	for s in systems/*/; do \
	    n=$$(basename $$s); \
	    [ -f $$s/boot.pi ] || [ -f $$s/boot.pidp ] || continue; \
	    install -d -m 0755 $(DESTDIR)$(SYSTEMSDIR)/$$n; \
	    for f in $$s*.pi $$s*.pidp $$s*.txt $$s*.pdf; do \
	        [ -f "$$f" ] || continue; \
	        install -m 0644 $$f $(DESTDIR)$(SYSTEMSDIR)/$$n/$$(basename $$f); \
	    done; done
	install -m 0644 systems/selections $(DESTDIR)$(SYSTEMSDIR)/
	@# data the helpers and the terminals read at run time. Installed unguarded:
	@# a missing file has to fail the build rather than leave a cabinet without
	@# its Teletype sounds or its Chaosnet config. These live with what uses them
	@# (the Teletype recordings are part of sty33's sources) or under
	@# install/fedora (config this project writes); nothing is installed from
	@# bin/, which holds no tracked files.
	for c in cbridge.conf chaos-hosts init.ini gt40.simh imlac.simh ssv22.iml; do \
	    $(SUBST) install/fedora/conf/$$c > $(DESTDIR)$(DATADIR)/conf/$$c; done
	chmod 0644 $(DESTDIR)$(DATADIR)/conf/*
	install -m 0644 install/fedora/conf/bootvt.img $(DESTDIR)$(DATADIR)/conf/
	install -m 0644 src/sty33/sounds/*.wav $(DESTDIR)$(DATADIR)/sounds/
	@# configuration, units and OS integration
	$(SUBST) install/fedora/etc/pidp10.conf > $(DESTDIR)$(SYSCONFDIR)/pidp10/pidp10.conf
	chmod 0644 $(DESTDIR)$(SYSCONFDIR)/pidp10/pidp10.conf
	for u in install/fedora/systemd/system/*; do \
	    $(SUBST) $$u > $(DESTDIR)$(UNITDIR)/$$(basename $$u); \
	    chmod 0644 $(DESTDIR)$(UNITDIR)/$$(basename $$u); done
	for u in install/fedora/systemd/user/*; do \
	    $(SUBST) $$u > $(DESTDIR)$(USERUNITDIR)/$$(basename $$u); \
	    chmod 0644 $(DESTDIR)$(USERUNITDIR)/$$(basename $$u); done
	for p in packaging/systemd/*.preset; do \
	    b=$$(basename $$p); \
	    case $$b in \
	    user-*) $(SUBST) $$p > $(DESTDIR)$(USERPRESETDIR)/$${b#user-} ;; \
	    *)      $(SUBST) $$p > $(DESTDIR)$(PRESETDIR)/$$b ;; \
	    esac; done
	chmod 0644 $(DESTDIR)$(PRESETDIR)/*.preset $(DESTDIR)$(USERPRESETDIR)/*.preset
	install -m 0644 install/fedora/sysusers.d/30-pidp10.conf $(DESTDIR)$(SYSUSERSDIR)/30-pidp10.conf
	$(SUBST) install/fedora/tmpfiles.d/pidp10.conf > $(DESTDIR)$(TMPFILESDIR)/pidp10.conf
	chmod 0644 $(DESTDIR)$(TMPFILESDIR)/pidp10.conf
	install -m 0644 install/fedora/udev.d/99-pidp10.rules $(DESTDIR)$(UDEVDIR)/
	install -m 0644 install/fedora/systemd/logind.conf.d/60-pidp10-rt.conf $(DESTDIR)$(LOGINDDIR)/
	install -m 0644 install/fedora/firewalld/pidp10.xml $(DESTDIR)$(FIREWALLDIR)/
	@# desktop integration, with the paths rewritten: the entries carry
	@# /usr/share paths in the tree (so they read correctly on a checkout)
	@# and desktop-file-validate in %check only parses them - a stale /opt
	@# or a wrong DATADIR in Exec= is silently dead at the desktop, not at
	@# the build. Icon= stays a bare name so the hicolor lookup resolves.
	for d in install/desktop-files/*.desktop; do \
	    $(SUBST) $$d > $(DESTDIR)$(APPSDIR)/$$(basename $$d); \
	    chmod 0644 $(DESTDIR)$(APPSDIR)/$$(basename $$d); done
	@# The theme names the icon after the file name, so con-48.png must
	@# land as con.png in 48x48/apps or that layer is invisible. (basename's
	@# second argument strips only an exact trailing match: strip ".png",
	@# then "-48", then put ".png" back.)
	for i in install/icons/*-48.png; do \
	    [ -f $$i ] && install -m 0644 $$i \
	        $(DESTDIR)$(ICONSDIR)/48x48/apps/$$(basename $$i .png | sed 's/-48$$//').png; done
	for i in install/icons/*.svg; do \
	    [ -f $$i ] && install -m 0644 $$i $(DESTDIR)$(ICONSDIR)/scalable/apps/$$(basename $$i); done
	install -m 0644 install/TELETYPE1945-1985.ttf $(DESTDIR)$(FONTSDBIR)/
	install -m 0644 install/vt52.otf $(DESTDIR)$(FONTSDBIR)/
	install -m 0644 install/view.json $(DESTDIR)$(DATADIR)/
	install -m 0644 install/pidp10tilix.conf $(DESTDIR)$(DATADIR)/
	@# man pages for the front doors; no-manual-page-for-binary is
	@# filtered only for the engines and panel tools, whose interface is
	@# the units
	@# The pages name the layout the helpers were just rewritten to, so they go
	@# through $(SUBST) like everything else that states a path: a page that
	@# tells the operator to edit a file that does not exist in this build is
	@# worse than no page.
	$(SUBST) man/pdp.1 > $(DESTDIR)$(MANDIR)/man1/pdp.1
	$(SUBST) man/pdpcontrol.1 > $(DESTDIR)$(MANDIR)/man1/pdpcontrol.1
	chmod 0644 $(DESTDIR)$(MANDIR)/man1/pdp.1 $(DESTDIR)$(MANDIR)/man1/pdpcontrol.1
	@# st's own upstream page; sty is st with Teletype emulation and no page
	@# of its own (see the no-manual-page filter).
	install -Dm 0644 src/st-0.9.2/st.1 $(DESTDIR)$(MANDIR)/man1/st.1
ifeq ($(WITH_STY),yes)
	install -m 0755 $(B)/sty $(DESTDIR)$(BINDIR)/sty
	install -m 0755 $(B)/st $(DESTDIR)$(BINDIR)/st
endif

clean:
	$(MAKE) -C $(PDP10D) clean
	rm -rf $(B)

.PHONY: clean clean-all rpm rpmlint
clean-all: clean
	-rm -rf $(PDP10D)/BIN

# packaging/build-rpm.sh stages the sources in dist/ and drops the products
# flat in $(RPM_DIR): _sourcedir/_rpmdir/_srcrpmdir point there, _rpmfilename
# keeps the arch subdirectory out of the path, and rpmbuild's scratch stays in
# ~/rpmbuild - the tree holds no BUILD/ or SOURCES/. make rpmlint builds them
# first, and exceptions live in packaging/rpmlint.toml with a reason each, not
# in someone's scrollback. WORKTREE=yes packages the tree as it stands - while
# work is uncommitted, the default HEAD snapshot lints code you did not write.
#
# Lint the RPMs, no bare-spec argument: the sysusers/tmpfiles macros cat
# Source1 and the tmpfiles fragment at parse time, which only resolves inside
# a build tree - linting the bare spec manufactures false errors instead of
# finding real ones. (The SRPM ships its sources, so its embedded spec parses
# cleanly and lints with the rest.)
RPM_DIR ?= dist-rpm
WORKTREE ?=
rpm:
	packaging/build-rpm.sh $(if $(filter yes,$(WORKTREE)),--worktree) $(RPM_DIR)

rpmlint: rpm
	rpmlint -c packaging/rpmlint.toml $(RPM_DIR)/*.rpm
