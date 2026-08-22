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
 * /usr/include/sys/fd.h
 * Open file descriptors.
 */ 

#ifndef	 FD_H
#define	 FD_H

#include <sys/types.h>
#include <sys/inode.h>

/*
 * File descriptor structure.
 */
typedef struct fd {
	char	 f_flag;		/* Flags */
/* An identifier, not defined(): the June 1985 preprocessor, which reads these
 * headers when the 1985 compiler is the flavour building, has no defined()
 * operator ("in #if", and the compile stops).  An identifier that is not a
 * macro counts as zero in an #if for all three preprocessors, so the test
 * means the same to each of them. */
#if _I386 || Z8001
	char	 f_flag2;		/* More flags */
#endif
	short	 f_refc;		/* Reference count */
	fsize_t	 f_seek;		/* Seek pointer */
	struct	 inode *f_ip;		/* Pointer to inode */
} FD;

/*
 * Flags (f_flag2).  `f_flag' itself is full: all eight of its bits are the
 * IPR/IPW/IPE permission and IPNDLY..IPNOCTTY modifier bits of <sys/inode.h>.
 */
#define	FFOPNP	0001			/* Open has not completed yet */

#ifdef	KERNEL
/*
 * Functions.
 */
extern	FD	*fdget();		/* fd.c */

#endif

#endif

/* end of /usr/include/sys/fd.h */
