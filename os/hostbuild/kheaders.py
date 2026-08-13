#!/usr/bin/env python3
"""kheaders.py -- the headers a kernel-side compile needs, computed from the sources.

    python3 kheaders.py list        one packaged path per line
    python3 kheaders.py pairs       the same, and a tab, and the file to copy
    python3 kheaders.py report      the list, annotated with who includes each

The kernel deliverable ships headers, and WHICH headers is a question with a
mechanical answer: resolve every #include in the kernel and loadable-driver
sources against the include path those compiles actually use, transitively, and
keep what lands in os/include or os/include/sys.

A hand-maintained list is what this replaces.  A header added to a kernel source
appears here the next time the package is cut; a list would have gone stale
silently, and the failure is a consumer whose driver will not compile against a
release that omitted one header.

The source set is deliberately WIDER than what link-kernel.sh links: every C and
assembly file under the kernel and driver trees, whether or not the default
configuration compiles it.  A configuration switch (KTTY, KDDT, KMOUSE) must not
change which headers a release carries, or two releases of one version would
differ in their header set.

Headers found OUTSIDE os/include are not part of the answer.  Three kinds turn
up there and none of them ships:

  toolchain src/include the C library's and the object formats' headers, and
                        the COHERENT interfaces the userland compiles against
                        too; the toolchain release publishes them
  os/sys/z8001/h/*      shadowed copies -- os/include/sys and the toolchain's
                        headers both precede that directory on the kernel's -I
                        path, so the compiler never reads them (romconf.h is
                        the one exception, and it is kernel-private)
  os/hrtty/h/*          the hi-res console driver's own headers, private to it
  build/gen/*           generated per build (wdbtab.h from a media descriptor,
                        sys/bootinfo.h staged from the kboot checkout)
"""
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
OS = os.path.dirname(HERE)
INC_RE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')

# The kernel's include path, in the order link-kernel.sh and build-drivers.sh
# give it.  build/gen comes first in the real builds; it holds only generated
# headers, which never ship, so its absence here cannot change the answer.
TCINC = os.environ.get('TCINC', '')

IPATH = [os.path.join(OS, 'include'),
         os.path.join(OS, 'include', 'sys')]
# The toolchain's system headers sit between os/include and the 0.7.3 copies,
# exactly as link-kernel.sh spells the compile.  Without them the walk stops at
# the first #include it cannot resolve and everything reachable only through a
# toolchain header drops out of the answer.  $TCINC comes from toolchain.sh;
# unset (a report run with no toolchain resolved), the two entries are absent
# and the walk covers only what this repository holds.
if TCINC:
    IPATH += [TCINC, os.path.join(TCINC, 'sys')]
IPATH += [os.path.join(OS, 'sys', 'z8001', 'h'),
          os.path.join(OS, 'hrtty', 'h'),
          os.path.join(OS, 'hrtty', 'src')]

SRCDIRS = ['sys/coh', 'sys/drv', 'sys/ker',
           'sys/z8001/src', 'sys/z8001/drv', 'sys/z8001/rec', 'sys/z8001/con',
           'hrtty/src']
# Reference trees: other ports of the same kernel, kept for comparison and never
# compiled.  Their includes would drag in headers this machine has no use for.
SKIPDIRS = ['sys/ref', 'sys/z8001/diag']
SKIPDIRNAMES = ['ATTIC']
# Files that sit in a kernel or driver directory and are not part of either.
# The distinction the sweep cannot make for itself is between a source no
# configuration compiles and one some configuration does; these are the first
# kind, and each pulls a userland header into the answer if it is read.
#
#   fifo_*.c, arg_exist.c   the i386 boot-gift machinery, never linked here
#                           (link-kernel.sh names them); mdstub.c carries no-op
#                           stubs for the entry points
#   hrtty/src/init.c        an init(8), left in the driver's directory by the
#                           vendor -- it is the only reader of <utmp.h>
#   hrtty/src/ftest.c       a standalone font test program
#   hrtty/src/input.c       a standalone input test program
#   hrtty/src/newgall.c     a host-side font generator
#   hrtty/src/hrterm3.c     a superseded revision of hrterm2.c
#   hrtty/src/kbibmtab.c    the IBM keyboard table; the Commodore one (kbtab.c)
#                           is what this machine has, and only one is compiled
SKIPFILES = ['sys/coh/fifo_close.c', 'sys/coh/fifo_len.c', 'sys/coh/fifo_open.c',
             'sys/coh/fifo_read.c', 'sys/coh/fifo_rewind.c', 'sys/coh/fifo_write.c',
             'sys/coh/arg_exist.c',
             'hrtty/src/init.c', 'hrtty/src/ftest.c', 'hrtty/src/input.c',
             'hrtty/src/newgall.c', 'hrtty/src/hrterm3.c', 'hrtty/src/kbibmtab.c']

# WHERE A SHIPPED HEADER MAY LIVE.  The kernel package's job is that a consumer
# who compiles a driver against a release needs the release and nothing else, so
# the closure ships whole -- including the parts of it the toolchain owns.  That
# is composition at PACKAGE time over a single copy in the source trees, not a
# second copy in this repository: os/include holds only what this kernel
# produces, and every interface it shares with the C library and the userland is
# read out of $TCINC, where the toolchain's release publishes it too.
#
# The shipped path is the same either way -- include/... and include/sys/... --
# because it names the kernel's OWN include path, not the tree the file came
# from.  `pairs' prints that destination beside the file to copy into it.
SHIPROOTS = [os.path.join(OS, 'include')]
if TCINC:
    SHIPROOTS.append(TCINC)


def shipdest(path):
    """Where <path> goes inside the package: include/... or include/sys/..."""
    if path.startswith(os.path.join(OS, 'include') + os.sep):
        return os.path.relpath(path, OS)
    return os.path.join('include', os.path.relpath(path, TCINC))


def sources():
    out = []
    for d in SRCDIRS:
        full = os.path.join(OS, d)
        for root, dirs, files in os.walk(full):
            rel = os.path.relpath(root, OS)
            if any(rel == s or rel.startswith(s + '/') for s in SKIPDIRS):
                dirs[:] = []
                continue
            dirs[:] = [d for d in dirs if d not in SKIPDIRNAMES]
            for f in files:
                if not (f.endswith('.c') or f.endswith('.s')):
                    continue
                if os.path.join(rel, f) in SKIPFILES:
                    continue
                out.append(os.path.join(root, f))
    return out


def closure(srcs):
    """Every header reachable from <srcs>, mapped to the files that include it."""
    users = defaultdict(set)
    seen = set()
    stack = list(srcs)
    while stack:
        f = stack.pop()
        try:
            text = open(f, 'r', errors='replace').read()
        except OSError:
            continue
        for line in text.split('\n'):
            m = INC_RE.match(line)
            if not m:
                continue
            name = m.group(1)
            found = None
            for d in [os.path.dirname(f)] + IPATH:
                p = os.path.join(d, name)
                if os.path.isfile(p):
                    found = os.path.realpath(p)
                    break
            if found is None:
                continue
            users[found].add(os.path.relpath(f, OS))
            if found not in seen:
                seen.add(found)
                stack.append(found)
    return users


def ships(path):
    return any(path.startswith(r + os.sep) for r in SHIPROOTS)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else 'list'
    users = closure(sources())
    # One destination, one file.  A header kept in BOTH trees is reached as
    # itself from this repository and as itself from the toolchain, so the
    # closure holds two absolute paths that package to one name.  The
    # repository's own copy is the one that ships, because it is the one the
    # kernel compiled: os/include precedes $TCINC on the kernel's -I path, so
    # shipping the other would hand a consumer a header the kernel did not
    # read.  check-shared-headers.sh is what keeps the two the same text; this
    # is what makes the choice not depend on it.
    bydest = {}
    for p in sorted(x for x in users if ships(x)):
        d = shipdest(p)
        if d not in bydest or p.startswith(os.path.join(OS, 'include') + os.sep):
            if d in bydest and not p.startswith(os.path.join(OS, 'include') + os.sep):
                continue
            bydest[d] = p
    keep = [bydest[d] for d in sorted(bydest)]
    for p in keep:
        rel = shipdest(p)
        if mode == 'report':
            u = sorted(users[p])
            print("%-24s %3d  %s" % (rel, len(u), ' '.join(u[:6])))
        elif mode == 'pairs':
            print("%s\t%s" % (rel, p))
        else:
            print(rel)
    if mode == 'report':
        print("== %d headers" % len(keep), file=sys.stderr)


if __name__ == '__main__':
    main()
