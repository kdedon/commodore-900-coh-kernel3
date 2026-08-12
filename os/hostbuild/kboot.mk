# kboot.mk -- resolve the kboot checkout, for makefiles.
#
# kboot is the multiboot loader the ROM loads and which then loads a kernel.
# It is not part of this operating system: it boots COHERENT 3.5, CP/M-8000
# and COHERENT 0.8 from one menu on one disk.  It lives in
# `commodore-900-kboot' and this tree consumes a checkout of it, exactly as
# it consumes the cross toolchain (toolchain.mk, whose search list and
# failure this mirrors).
#
# A checkout built from source, not a released binary: the loader and the
# kernel share a versioned interface (the checkout's include/bootinfo.h), and a
# change to it has to be compiled on both sides in one step -- that is what
# hostbuild/check-bootabi.sh checks.  Source also keeps `make' able to say
# that a loader is out of date, which a downloaded file cannot.
#
#   $(C900_KBOOT)  the checkout.  Unset, the first of $(C900_KB_SEARCH) that has
#                  an include/bootinfo.h in it wins, so a plain `make' works
#                  when kboot is checked out beside this repository; override it
#                  to build against a checkout somewhere else.  The search is a
#                  LIST for the same reason toolchain.mk's is: this repository
#                  is consumed both as a sibling checkout and from inside a
#                  `repos/' staging directory.
#
# Defines $(KBOOT), or leaves it empty.  Empty is not fatal here -- a dist that
# stages no loader (systems/stock.sys; that system predates kboot and the ROM
# loads it directly) must still build on a machine that has never heard of it.
# The rule that STAGES a loader is where the absence has to be fatal, and it
# says so with $(KBOOT_ERR) below rather than with `No rule to make target'.
C900_KBDIR := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
C900_KB_SEARCH := $(abspath $(C900_KBDIR)/../../../commodore-900-kboot) \
		  $(abspath $(C900_KBDIR)/../../repos/commodore-900-kboot)
C900_KBOOT ?= $(firstword $(patsubst %/include/bootinfo.h,%,\
		$(wildcard $(addsuffix /include/bootinfo.h,$(C900_KB_SEARCH)))))
KBOOT := $(C900_KBOOT)

# One message, naming the variable and the paths tried.  Without it the first
# symptom of a missing checkout is a missing file several hundred lines into an
# image build, which says nothing about what to do next.
KBOOT_ERR = { echo "no kboot checkout: set C900_KBOOT to a commodore-900-kboot"; \
	      echo "  clone, or put one at one of:"; \
	      for d in $(C900_KB_SEARCH); do echo "    $$d"; done; \
	      echo "  It is the loader every bootable image stages as /coherent."; \
	      exit 2; }

# The handoff header the kernel compiles.  It belongs to the loader and this
# tree keeps no copy of it: link-kernel.sh stages the checkout's own file onto
# the kernel's include path (kboot.sh bistage), so the two sides cannot hold
# different definitions.  Named here so that editing the ABI relinks the
# kernel.  Empty with no checkout resolved, and link-kernel.sh is then what
# refuses -- a literal `/include/bootinfo.h' prerequisite would fail first and
# say nothing.
KBOOTBI := $(if $(KBOOT),$(KBOOT)/include/bootinfo.h)
