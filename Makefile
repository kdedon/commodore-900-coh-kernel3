# Makefile -- build entry point for commodore-900-coh-kernel3,
# COHERENT 3-series kernel for the Commodore 900 (Z8001).
#
#   make kernel           link the kernel (os/hostbuild/kobj/kernel.out)
#   make drivers          loadable console drivers, bound to that kernel
#   make kernel-dist      packaged deliverable (image, drivers, headers, gate)
#   make kernel-headers   header set a kernel-side compile needs
#   make check-stamps     kernel/driver link-id pairing gate
#   make deps             place the toolchain and kboot checkouts
#   make clean            drop build products (not the deps)

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
.PHONY: all kernel drivers kernel-dist kernel-headers check-stamps \
	deps clean help

all: kernel drivers

help:
	@sed -n '2,20p' $(HERE)/Makefile

# Info goals need no toolchain; everything else resolves it first.
INFO_GOALS = help deps clean kernel-headers
ifeq (,$(filter $(MAKECMDGOALS),$(INFO_GOALS)))
include $(HB)/toolchain.mk
include $(HB)/kboot.mk
endif

# --- kernel -------------------------------------------------------------
kernel: $(HB)/kobj/kernel.out

# The kernel depends on its source, or an edit silently ships the previous
# kernel -- and every `ld -k' driver then disagrees about symbol addresses.
KSRC := $(shell find $(OS)/sys $(OS)/include \
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

$(HB)/kobj/kernel.out: $(HB)/link-kernel.sh $(HB)/wdbtab-hd21.h $(KSRC) $(KVFILE) $(KBOOTBI)
	sh $(HB)/link-kernel.sh

# --- drivers --------------------------------------------------------------
DRIVERS = $(HB)/build/drv/notty $(HB)/build/drv/lrtty $(HB)/build/drv/hrtty
drivers: $(DRIVERS)
# One script builds all three; grouped to prevent concurrent invocation.
DRVSRC := $(shell find $(OS)/sys/z8001/drv $(OS)/sys/z8001/rec $(OS)/hrtty \
	     \( -name '*.c' -o -name '*.h' -o -name '*.s' \) 2>/dev/null)
$(DRIVERS) &: $(HB)/kobj/kernel.out $(HB)/build-drivers.sh $(DRVSRC)
	sh $(HB)/build-drivers.sh

# --- verification ---------------------------------------------------------
.PHONY: check-stamps
check-stamps:
	sh $(HB)/check-stamps.sh

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
deps:
	sh $(HERE)/mk/deps-fetch.sh

clean:
	rm -rf $(HB)/kobj $(HB)/build $(HB)/logs $(HB)/kobj.[0-9]*
