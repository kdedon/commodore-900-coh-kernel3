/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* (-lgl
 * 	The information contained herein is a trade secret of Mark Williams
 * 	Company, and  is confidential information.  It is provided  under a
 * 	license agreement,  and may be  copied or disclosed  only under the
 * 	terms of  that agreement.  Any  reproduction or disclosure  of this
 * 	material without the express written authorization of Mark Williams
 * 	Company or persuant to the license agreement is unlawful.
 * 
 * 	COHERENT Version 0.7.3
 * 	Copyright (c) 1982, 1983, 1984.
 * 	An unpublished work by Mark Williams Company, Chicago.
 * 	All rights reserved.
 -lgl) */
/*
 * Mount table.
 */
#ifndef	 MOUNT_H
#define	 MOUNT_H
#include <types.h>
#include <filsys.h>

/*
 * Mount table structure.
 */
typedef struct mount {
	struct	 mount *m_next;		/* Pointer to next */
	struct	 inode *m_ip;		/* Associated inode */
	dev_t	 m_dev;			/* Device */
	int	 m_flag;		/* Flags */
	GATE	 m_ilock;		/* Inode lock */
	GATE	 m_flock;		/* Free list lock */
	struct	 filsys m_super;	/* Super block */
} MOUNT;

/*
 * Flags.  Only MFRON is stored persistently in m_flag; MFFORCE and MFRMT
 * are request flags passed to mount(2) and consumed by fsmount()/umount().
 */
#define	MFRON	001			/* Read only file system */
#define	MFFORCE	002			/* Mount r/w even if dirty (mount -f) */
#define	MFRMT	004			/* Remount an already-mounted fs (mount -w) */

#ifdef KERNEL
/*
 * Functions.
 */
MOUNT	*fsmount();			/* fs2.c */
MOUNT	*getment();			/* fs2.c */

#endif

#ifdef KERNEL
/*
 * Global variables.
 */
extern	MOUNT	*mountp;		/* Mount table */

#endif

#endif
