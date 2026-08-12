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
 * Commodore M-series Z8001
 * Configuration file for hard disk root
 * (Hard disk is unit 0 of DTC hard disk driver)
 */
#include <coherent.h>
#include <con.h>
#include <mtype.h>
#include <stat.h>

extern	CON	nlcon[];		/* Null device */
extern	CON	ctcon[];		/* Console terminal */
#if 0
extern	CON	kbcon[];		/* Keyboard/display */
#endif
extern	CON	lpcon[];		/* Line printer */
extern	CON	alcon[];		/* Asynchronous line */
extern	CON	hdcon[];		/* Hard disk */

/*
 * Device table.
 */
DRV drvl[16] ={
	{nlcon},	{ctcon},	{NULL},		{lpcon},
	{NULL},		{alcon},	{hdcon},	{NULL},
	{NULL},		{NULL},		{NULL},		{NULL},
	{NULL},		{NULL},		{NULL},		{NULL},
};

/*
 * Time.
 */
TIME timer ={
	0,				/* Initial time */
	0,				/* Ticks */
#ifdef EST
	5*60,				/* Eastern */
#else
	6*60,				/* Central */
#endif
	1,				/* Daylight saving time */
};

/*
 * Devices and sizes.
 */
dev_t	rootdev = makedev(6, 0);	/* Root device = floppy */
dev_t	pipedev = makedev(6, 0);	/* Pipe device */
dev_t	swapdev = makedev(6, 3);	/* Swap device */
daddr_t	swapbot = 3001;			/* Swap base */
daddr_t	swaptop = 5160;			/* Swap end */
int	ronflag	= 0;			/* Not read only */
int	drvn	= 16;			/* Maximum number of devices */
int	mactype	= M_Z8001;		/* Machine type */
