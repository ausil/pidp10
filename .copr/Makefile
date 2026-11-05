# COPR "build from git" runs `make srpm` in this file. It is a one-line adapter
# rather than a second packaging implementation: the snapshot identity
# (gitrev/gitdate in Release:) and the blob-free Source0 (bin/ excluded, the
# peripherals in Source2) both live in packaging/build-rpm.sh, and duplicating
# them here is how a COPR build ends up shipping something a local build cannot
# reproduce.

SRPM_TOPDIR ?= $(CURDIR)/rpmbuild

# If the checkout has no .git (some COPR fetch modes), fall back to the work
# tree: the packages are the same, only the attribution differs.
srpm:
	@if git rev-parse --short HEAD >/dev/null 2>&1; then \
	    packaging/build-rpm.sh --srpm-only $(SRPM_TOPDIR); \
	else \
	    echo "copr: no git metadata, packaging the work tree" >&2; \
	    packaging/build-rpm.sh --srpm-only --worktree $(SRPM_TOPDIR); \
	fi
	cp -a $(SRPM_TOPDIR)/SRPMS/*.src.rpm $(CURDIR)/

clean:
	rm -rf $(SRPM_TOPDIR) *.src.rpm

.PHONY: srpm clean
