# DEPS -- what this repository consumes from other repositories.
#
#	name  kind  url  [ref]  [asset]  [dir]
#
# Read by `make deps' (mk/deps-fetch.sh).  The build resolves dependencies
# through mk/deps.sh; a named variable wins over anything here.
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
#   kboot      include/bootinfo.h, the loader->kernel handoff -- the whole of
#              what a KERNEL build consumes from kboot.  Not a checkout: the
#              loader is a Z8001 program with its own compiler and its own
#              tests, none of which a header consumer needs, so kboot
#              publishes the header alone as a release asset and the resolver
#              reads either that or a full checkout beside this repository.
#
# The ref `latest' resolves to the newest published release, so a fetch takes
# the current compiler and the current header.  A tag may be named in its
# place to take that one.

toolchain  release  https://github.com/kdedon/commodore-900-toolchain  latest  c900-toolchain-@REF@-@HOST@  commodore-900-toolchain
kboot      release  https://github.com/kdedon/commodore-900-kboot      latest  c900-kboot-headers-@REF@.tar.gz  commodore-900-kboot
