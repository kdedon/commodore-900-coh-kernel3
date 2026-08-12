# DEPS -- what this repository consumes from other repositories.
#
#	name  kind  url  [ref]  [asset]  [dir]
#
# Read by `make deps' (mk/deps-fetch.sh).  The build resolves dependencies
# through mk/deps.sh; a named variable wins over anything here.
#
# kind git      cloned beside this repository, floating on <ref>
#
#   toolchain  the Z8001 cross compiler, assembler and linker, and the five
#              libc objects the kernel links by name
#   kboot      include/bootinfo.h, the loader->kernel handoff.  The KERNEL
#              compiles this header, so a kernel build needs the checkout

toolchain  git      https://github.com/kdedon/commodore-900-toolchain  main
kboot      git      https://github.com/kdedon/commodore-900-kboot      main
