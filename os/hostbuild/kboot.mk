# kboot.mk -- resolve what a kernel build consumes from kboot, for makefiles.
# The shell equivalent, and the long-form rationale, is kboot.sh beside it.
#
# kboot is the multiboot loader the ROM loads and which then loads a kernel.
# It is not part of this operating system: it boots COHERENT 3.5, CP/M-8000
# and COHERENT 0.8 from one menu on one disk.  A KERNEL build consumes
# exactly one thing from it -- include/bootinfo.h, the loader<->kernel
# handoff -- and kboot publishes that header on its own as a release asset,
# so this tree consumes one of two shapes: a source CHECKOUT (which has
# everything else too) or an unpacked release archive (which has only the
# header and a VERSION file).  mk/deps.sh's toolchain entry is the same
# pattern for the same reason; this mirrors it.
#
#   $(C900_KBOOT)  Unset, mk/deps.sh searches -- deps/ for the pinned
#                  release, then a checkout beside this repository, then one
#                  inside a `repos/' staging directory.  Override it (on the
#                  command line or in the environment) to build against
#                  something else; a value that does not resolve is refused
#                  as itself, not quietly re-searched.  The search is in
#                  mk/deps.sh and nowhere else, so `make deps', this file and
#                  kboot.sh cannot answer the same question three different
#                  ways.
#
# Defines $(KBOOT), or leaves it empty.  Empty is not fatal here -- a dist
# that stages no loader (systems/stock.sys; that system predates kboot and
# the ROM loads it directly) must still build on a machine that has never
# heard of it.  The rule that STAGES a loader is where the absence has to be
# fatal, and it says so with $(KBOOT_ERR) below rather than with `No rule to
# make target'.
C900_KBDIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
# $(C900_KBDIR) is os/hostbuild, so the resolver is two levels up.
C900_KBDEPS := $(abspath $(C900_KBDIR)/../../mk/deps.sh)
# Simply-expanded, and only when nothing named it: `?=' would make the
# variable recursive and re-run the search at every mention of it.
ifeq (,$(C900_KBOOT))
C900_KBOOT := $(shell C900_KBOOT= sh $(C900_KBDEPS) kboot)
endif
# `checkout', `release X.Y.Z', or empty -- WHICH shape this build resolved,
# reported the same way toolchain.mk reports C900_TC_SHAPE.  Empty with an
# empty $(C900_KBOOT) too: nothing to name a shape of.
C900_KB_SHAPE := $(if $(C900_KBOOT),$(shell C900_KBOOT='$(C900_KBOOT)' sh $(C900_KBDEPS) -k kboot))
KBOOT := $(C900_KBOOT)

# One message, naming the variable and the paths tried.  Without it the first
# symptom of a missing checkout is a missing file several hundred lines into an
# image build, which says nothing about what to do next.
KBOOT_ERR = { echo "no kboot release or checkout resolved for C900_KBOOT=$(C900_KBOOT)"; \
	      C900_KBOOT='$(C900_KBOOT)' sh $(C900_KBDEPS) -n kboot '$(C900_KBOOT)'; \
	      echo "  It is the loader every bootable image stages as /coherent."; \
	      exit 2; }

# The handoff header the kernel compiles.  It belongs to the loader and this
# tree keeps no copy of it: link-kernel.sh stages the resolved copy of it onto
# the kernel's include path (kboot.sh bistage), so the two sides cannot hold
# different definitions.  Named here so that editing the ABI relinks the
# kernel.  Empty with nothing resolved, and link-kernel.sh is then what
# refuses -- a literal `/include/bootinfo.h' prerequisite would fail first and
# say nothing.
KBOOTBI := $(if $(KBOOT),$(KBOOT)/include/bootinfo.h)

ifneq (,$(KBOOT))
ifeq (,$(C900_KB_REPORTED))
$(shell echo "kboot: $(C900_KB_SHAPE) at C900_KBOOT=$(KBOOT)" >&2)
C900_KB_REPORTED := 1
endif
endif
export C900_KBOOT C900_KB_SHAPE C900_KB_REPORTED
