/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* (-lgl
 * 	COHERENT Version 4.0
 * 	Copyright (c) 1982, 1992 by Mark Williams Company.
 * 	All rights reserved. May not be copied without permission.
 -lgl) */
/*
 * Stat.
 */

#ifndef	 STAT_H
#define	 STAT_H	STAT_H

#include <sys/types.h>

/*
 * Structure returned by stat and fstat system calls.
 */
struct stat {
	dev_t	 st_dev;		/* Device */
	ino_t	 st_ino;		/* Inode number */
	unsigned short st_mode;		/* Mode */
	short	 st_nlink;		/* Link count */
	short	 st_uid;		/* User id */
	short	 st_gid;		/* Group id */
	dev_t	 st_rdev;		/* Real device */
	fsize_t	 st_size;		/* Size */
	time_t	 st_atime;		/* Access time */
	time_t	 st_mtime;		/* Modify time */
	time_t	 st_ctime;		/* Change time */
};

/*
 * Modes.
 * These are the mode bits of a disk inode's di_mode, so they are the same
 * bits, with the same values, that <sys/ino.h> spells IFMT, IFDIR, ... and
 * ISVTXT for the kernel and for the file system utilities that read a raw
 * inode.  The two spellings are deliberately separate namespaces over one
 * on-disk layout: nothing here may disagree with <sys/ino.h>.
 */
#define S_IFMT	0170000			/* Type */
#define S_IFDIR	0040000			/* Directory */
#define S_IFCHR	0020000			/* Character special */
#define S_IFBLK	0060000			/* Block special */
#define S_IFREG	0100000			/* Regular */
#define S_IFMPC	0030000			/* Multiplexed character special */
#define S_IFMPB	0070000			/* Multiplexed block special */
#define	S_IFPIP	0010000			/* Pipe */
#define	S_IFIFO	S_IFPIP			/* Pipe */
#define	S_ISUID	0004000			/* Set user id on execution */
#define S_ISGID	0002000			/* Set group id on execution */
#define	S_ISVTX	0001000			/* Save swapped text even after use */

/*
 * Permissions.  The kernel reads them three bits at a time -- owner from
 * (di_mode>>6)&07, group from (di_mode>>3)&07, other from di_mode&07, each
 * against read 4, write 2, execute 1 (sys/coh/fs1.c imode(), sys/h/inode.h
 * IPR/IPW/IPE) -- so the triads below are that shift, not a convention.
 */
#define S_IREAD	0000400			/* Read permission, owner */
#define S_IWRITE 000200			/* Write permission, owner */
#define S_IEXEC	0000100			/* Execute/search permission, owner */
#define	S_IRWXU	0700			/* RWX permission, owner */
#define	S_IRUSR	S_IREAD			/* Read permission, owner */
#define	S_IWUSR	S_IWRITE		/* Write permission, owner */
#define	S_IXUSR	S_IEXEC			/* Execute/search permission, owner */
#define	S_IRWXG	0070			/* RWX permission, group */
#define	S_IRGRP	0040			/* Read permission, group */
#define	S_IWGRP	0020			/* Write permission, group */
#define	S_IXGRP	0010			/* Execute/search permission, group */
#define	S_IRWXO	0007			/* RWX permission, other */
#define	S_IROTH	0004			/* Read permission, other */
#define	S_IWOTH	0002			/* Write permission, other */
#define	S_IXOTH	0001			/* Execute/search permission, other */

/* POSIX file-type test macros. */
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)	(((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)	(((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)	(((m) & S_IFMT) == S_IFREG)
#define S_ISFIFO(m)	(((m) & S_IFMT) == S_IFPIP)

/*
 * Nonexistent device.
 * Must compare correctly with dev_t, which is an unsigned short.
 */
#define NODEV	((dev_t)-1)

/*
 * Functions.
 */
#define	major(dev)	((dev>>8)&0377)
#define minor(dev)	(dev&0377)
#define makedev(m1, m2)	((m1<<8)|m2)

#endif
