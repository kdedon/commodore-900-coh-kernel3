# DEPS -- what this repository consumes from other repositories.
#
#	name  kind  url  [ref]  [asset]  [dir]
#
# Read by `make deps' (mk/deps-fetch.sh).  The build resolves dependencies
# through mk/deps.sh; a named variable wins over anything here.
#
# kind git      cloned beside this repository, floating on <ref>
#
#   kboot      include/bootinfo.h, the loader->kernel handoff.  The KERNEL
#              compiles this header, so a kernel build needs the checkout
#
# kind release  a tag's published assets, unpacked into deps/<dir>/
#
#   toolchain  the Z8001 cross compiler, assembler and linker, AND the five
#              libc objects the kernel links by name (kobj/).  A RELEASE, not
#              a checkout: those five objects are compiled from the operating
#              system's own libc, which the toolchain repository does not
#              contain, so `make all' in a toolchain checkout produces a
#              compiler and no kobj at all.  The release archive carries them
#              -- that is what it is for -- and the resolver reads either
#              shape, so a developer with a checkout beside this one still
#              builds against it by naming C900_TOOLCHAIN.
#
# The tag is a PIN.  It is bumped by hand, when this kernel wants what a newer
# compiler emits; a floating edge would mean a kernel that links differently
# tomorrow with nothing here changed.

toolchain  release  https://github.com/kdedon/commodore-900-toolchain  v0.1.2  c900-toolchain-@REF@-@HOST@  commodore-900-toolchain
kboot      git      https://github.com/kdedon/commodore-900-kboot      main
