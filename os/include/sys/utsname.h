/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* (-lgl
 * 	COHERENT Version 4.0
 * 	Copyright (c) 1982, 1992 by Mark Williams Company.
 * 	All rights reserved. May not be copied without permission.
 -lgl) */
#ifndef UTSNAME_H
#define UTSNAME_H	UTSNAME_H

#define SYS_NMLN	9

struct	utsname {
	char	sysname[SYS_NMLN];
	char	nodename[SYS_NMLN];
	char	release[SYS_NMLN];
	char	version[SYS_NMLN];
	char	machine[SYS_NMLN];
};

extern	struct	utsname	utsname;

/*
 * What the five fields carry on this system.  uname(2) fills all of them;
 * every value but nodename is compiled into the kernel, and nodename is the
 * first line of /etc/uucpname -- this machine's short name, which is what a
 * nine-byte field holds.  /etc/hostname, which gethostname(3) reads, is the
 * same name with its domain on it and does not fit.
 *
 * sysname is the system CHANNEL id, not a pretty name and not the node name:
 * it says which system this is, so that a package built for one kernel line is
 * never offered to the other.  `coh32' is the 3.2-derived kernel (os/sys);
 * the 0.7.3-derived one is `coh073', the name its dist descriptors already
 * use.  It names a derivation, not a product version, and so does not go stale
 * when the release does.
 *
 * machine is the instruction set a binary has to match, which is the only
 * thing a package can act on -- not the model of the machine around it.
 *
 * version is the pair of KERNEL COMPATIBILITY IDS, `kabi.fsabi', in decimal,
 * and it is the field to compare -- as TWO INTEGERS, never as a string, since
 * 1.10 is newer than 1.9 and sorts before it.  Neither number is a date and
 * neither counts releases:
 *
 *   kabi   the layout of the kernel structures a program reads out of
 *          /dev/kmem: proc.h, uproc.h, sched.h, seg.h.  Bump it when any of
 *          those changes shape, because ps, top, load and uload then print
 *          garbage.  A kernel change that moves no member does not bump it.
 *   fsabi  the on-disk filesystem format: filsys.h, ino.h.  Bump it when a
 *          superblock or inode changes, which invalidates df, icheck, ncheck,
 *          dcheck, clri, mount and db.  It is expected never to move: every
 *          existing image and every real disk is already written this way.
 *
 * The two are separate so that a kernel struct change does not needlessly
 * invalidate the filesystem tools, and so `will this run here' is answered by
 * two integer compares rather than by parsing a release string.
 */
#define SYS_SYSNAME	"coh32"
#define SYS_MACHINE	"z8001"
#define SYS_VERSION	"1.1"		/* kabi 1, fsabi 1 */

int	uname();			/* (struct utsname *name) */

#endif
