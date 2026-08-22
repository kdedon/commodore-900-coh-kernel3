# COHERENT 3.x kernel for the Commodore 900 (Z8001).
#
#   make kernel           link the kernel (os/hostbuild/kobj/kernel.out)
#   make drivers          loadable console drivers, bound to that kernel
#   make kernel-dist      package the kernel and drivers
#   make kernel-headers   report the exported kernel headers
#   make check-stamps     verify kernel/driver link IDs
#   make check-shared     compare headers shared with the toolchain
#   make deps             fetch inputs listed in DEPS
#   make clean            remove build products

SHELL	= /bin/sh
.DELETE_ON_ERROR:
MAKEFLAGS += --no-builtin-rules --no-builtin-variables
.SUFFIXES:
ifeq (,$(filter grouped-target,$(.FEATURES)))
$(error GNU make 4.3 or newer is required (grouped targets); this is $(MAKE_VERSION))
endif
HERE	:= $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
OS	:= $(HERE)/os
HB	:= $(OS)/hostbuild

.DEFAULT_GOAL := kernel
.PHONY: all kernel drivers kernel-dist kernel-headers check-stamps check-shared \
	deps clean help

all: kernel drivers

help:
	@printf '%s\n' \
	  'make                     build the kernel' \
	  'make drivers             build the loadable console drivers' \
	  'make kernel-dist         package the kernel and drivers' \
	  'make kernel-headers-dist package the exported headers' \
	  'make check-stamps        verify kernel/driver link IDs' \
	  'make check-shared        compare shared toolchain headers' \
	  'make deps                fetch inputs listed in DEPS' \
	  'make clean               remove build products'

# Info goals need no toolchain; everything else resolves it first.
# kernel-headers is NOT here: the header set is the include closure, most of
# which the toolchain publishes, so naming it needs the toolchain resolved.
INFO_GOALS = help deps clean
ifeq (,$(filter $(MAKECMDGOALS),$(INFO_GOALS)))
include $(HB)/toolchain.mk
include $(HB)/kboot.mk
endif

# --- kernel -------------------------------------------------------------
kernel: $(HB)/kobj/kernel.out

# The kernel depends on its source, or an edit silently ships the previous
# kernel -- and every `ld -k' driver then disagrees about symbol addresses.
# $(TCINC) is in the list for the same reason: the kernel compiles against the
# toolchain's system headers, so one of them changing is a kernel source change.
KSRC := $(shell find $(OS)/sys $(OS)/include $(TCINC) \
	   \( -name '*.c' -o -name '*.s' -o -name '*.h' \) 2>/dev/null)
# The build variant is an input like any source file; see link-kernel.sh.
KTTY	?= termio
KMEDIA	?= hd21
KMOUSE	?= 1
KVERSION ?=
export KTTY
export KDDT
export KMEDIA
export KMOUSE
KVARIANT := KTTY=$(KTTY) KDDT=$(KDDT) KMEDIA=$(KMEDIA) KMOUSE=$(KMOUSE)
KVFILE	:= $(HB)/build/.kvariant
$(shell mkdir -p $(HB)/build; [ "$$(cat $(KVFILE) 2>/dev/null)" = '$(KVARIANT)' ] \
	|| echo '$(KVARIANT)' > $(KVFILE))

# $(TCID) is the compiler, named as what it is: an input.  It holds the
# toolchain's source id and is rewritten only when that changes, so a different
# compiler relinks the kernel and the same one does not.
$(HB)/kobj/kernel.out: $(HB)/link-kernel.sh $(HB)/wdbtab-hd21.h $(KSRC) $(KVFILE) $(KBOOTBI) $(TCID)
	sh $(HB)/link-kernel.sh

# --- drivers --------------------------------------------------------------
DRIVERS = $(HB)/build/drv/notty $(HB)/build/drv/lrtty $(HB)/build/drv/hrtty
drivers: $(DRIVERS)
# One script builds all three; grouped to prevent concurrent invocation.
DRVSRC := $(shell find $(OS)/sys/z8001/drv $(OS)/sys/z8001/rec $(OS)/hrtty \
	     \( -name '*.c' -o -name '*.h' -o -name '*.s' \) 2>/dev/null)
$(DRIVERS) &: $(HB)/kobj/kernel.out $(HB)/build-drivers.sh $(DRVSRC) $(TCID)
	sh $(HB)/build-drivers.sh

# --- verification ---------------------------------------------------------
.PHONY: check-stamps
check-stamps:
	sh $(HB)/check-stamps.sh

# The headers this repository and the toolchain BOTH compile are kept in both
# trees on purpose; this is what makes that safe rather than a slow drift.
.PHONY: check-shared
check-shared:
	sh $(HB)/check-shared-headers.sh

# --- the packaged deliverable ---------------------------------------------
kernel-dist: $(HB)/kobj/kernel.out $(DRIVERS)
	sh $(HB)/pack-kernel.sh $(KVERSION)

kernel-headers:
	@python3 $(HB)/kheaders.py report

# The header set as a package of its own: a consumer that only compiles
# against the kernel needs no kernel image.
kernel-headers-dist:
	sh $(HB)/pack-headers.sh $(KVERSION)

# --- dependencies ----------------------------------------------------------
# DEP=<name> places just that edge; no DEP places every edge in DEPS.
deps:
	sh $(HERE)/mk/deps-fetch.sh $(DEP)

clean:
	rm -rf $(HB)/kobj $(HB)/build $(HB)/logs $(HB)/kobj.[0-9]*
