# COHERENT 3.x kernel for the Commodore 900 (Z8001).
#
#   make kernel           link the kernel (os/hostbuild/kobj/kernel.out)
#   make drivers          loadable console and hostfs drivers, bound to that kernel
#   make kernel-dist      package the kernel and drivers
#   make packages         cut the console components as bin/src/man archives
#   make kernel-headers   report the exported kernel headers
#   make check-stamps     verify kernel/driver link IDs
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
.PHONY: all kernel drivers kernel-dist kernel-headers check-stamps \
	deps clean help

all: kernel drivers

help:
	@printf '%s\n' \
	  'make                     build the kernel' \
	  'make drivers             build the loadable console and hostfs drivers' \
	  'make kernel-dist         package the kernel and drivers' \
	  'make packages            cut the console components (bin/src/man)' \
	  'make package PKG=<c>     one component-kind: console-hr, console-hr-src, ...' \
	  'make package-list        the components and their kinds' \
	  'make kernel-headers-dist package the exported headers' \
	  'make check-stamps        verify kernel/driver link IDs' \
	  'make deps                fetch inputs listed in DEPS' \
	  'make clean               remove build products'

# Info goals need no toolchain; everything else resolves it first.
# kernel-headers is NOT here: the header set is the include closure, most of
# which the toolchain publishes, so naming it needs the toolchain resolved.
# The package goals compile nothing: they read build/drv and the map the driver
# build wrote, and refuse when either is missing.
INFO_GOALS = help deps clean packages package package-list
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
DRIVERS = $(HB)/build/drv/notty $(HB)/build/drv/lrtty $(HB)/build/drv/hrtty \
	  $(HB)/build/drv/hostfs
drivers: $(DRIVERS)
# One script builds all four; grouped to prevent concurrent invocation.
DRVSRC := $(shell find $(OS)/sys/z8001/drv $(OS)/sys/z8001/rec $(OS)/hrtty \
	     \( -name '*.c' -o -name '*.h' -o -name '*.s' \) 2>/dev/null) \
	  $(OS)/sys/drv/hostfs.c
$(DRIVERS) &: $(HB)/kobj/kernel.out $(HB)/build-drivers.sh $(DRVSRC) $(TCID)
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

# --- component packages -------------------------------------------------------
# A component is an os/dist/lists/*.list carrying a `package' line -- the three
# loadable console drivers, one each -- and it exports its runtime files, the
# complete corresponding source for them and the Lexicon articles for them.  They
# are cut here because the facts they need are here: which drivers were linked,
# against which kernel (build/drv/.drvstamp), from which sources
# (build/.ulsrcmap, written by build-drivers.sh as it links).
#
#   make packages                 cut every component-kind that can be cut
#   make package PKG=console-hr   one -- `console-hr', `console-hr-src', `console-hr-man'
#   make package-list             the components and their kinds
#
# The archives land in $(PKGOUT); the distribution repository collects them from
# there and judges them (os/dist/check-package.sh) against the component-kind
# declarations the userland publishes in dist/packages.  Each archive is judged
# here first, by the packer that cut it, against its own manifest.tab, .contents
# and .provenance; a cut that fails is removed and counted as refused, so a
# package that does not match what it says it carries never leaves $(PKGOUT).
# Each cut starts from an empty PKGOUT: pack-component.sh only ever adds
# archives there, so one left from an earlier or dirty build would sit beside
# the new cut and be just as collectible.
PKGOUT ?= $(HB)/build/packages
PACKCOMP = sh $(HB)/pack-component.sh
.PHONY: packages package package-list
package:
	@test -n "$(PKG)" || { echo "usage: make package PKG=<component>[-src|-man]"; exit 2; }
	$(PACKCOMP) -o $(PKGOUT) $(PKG)

# Attempt every component kind, reporting each refusal by name.  A refusal is a
# component that publishes nothing, so the sweep fails on one: an install set
# short of a component is not an install set.
packages:
	@rm -rf $(PKGOUT); mkdir -p $(PKGOUT); ok=0; skip=0; \
	$(PACKCOMP) components | awk '$$2 ~ /^bin/ {print $$1, $$2}' | \
	while read -r n kinds; do \
		for k in $$(echo "$$kinds" | tr ',' ' '); do \
			if $(PACKCOMP) -o $(PKGOUT) "$$n" "$$k" 2>$(PKGOUT)/.why; \
			then ok=$$((ok+1)); \
			else skip=$$((skip+1)); \
			   echo "-- $$n-$$k not cut: $$(head -2 $(PKGOUT)/.why | tr '\n' ' ')"; \
			fi; \
		done; \
		echo "$$ok $$skip" > $(PKGOUT)/.count; \
	done; rm -f $(PKGOUT)/.why; \
	test -s $(PKGOUT)/.count || { \
	    echo "== packages: no component kind was attempted" >&2; exit 1; }; \
	read -r ok skip < $(PKGOUT)/.count; rm -f $(PKGOUT)/.count; \
	echo "== packages: $$ok cut, $$skip refused, in $(PKGOUT)"; \
	ls -1 $(PKGOUT)/*.tar.gz 2>/dev/null | wc -l | \
	    xargs -I{} echo "== {} archive(s) present"; \
	test "$$skip" -eq 0 || { \
	    echo "== packages: $$skip component-kind(s) published nothing" >&2; exit 1; }

package-list:
	@$(PACKCOMP) components

# --- dependencies ----------------------------------------------------------
# DEP=<name> places just that edge; no DEP places every edge in DEPS.
deps:
	sh $(HERE)/mk/deps-fetch.sh $(DEP)

clean:
	rm -rf $(HB)/kobj $(HB)/build $(HB)/logs $(HB)/kobj.[0-9]*
