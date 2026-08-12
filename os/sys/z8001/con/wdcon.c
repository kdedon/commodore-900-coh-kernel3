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
 * Hard disk is Western Digital controller based.
 */
#include <coherent.h>
#include <con.h>
#include <mtype.h>
#include <stat.h>

extern	CON	nlcon[];		/* Null device */
extern	CON	ctcon[];		/* Console terminal */
extern	CON	alcon[];		/* SCC async lines (tty50..55) */
extern	CON	wdcon[];		/* Western Digital hard disk */
extern	CON	lpcon[];		/* Centronics printer */
extern	CON	kvcon[];		/* keyboard/lo-res screen */
#ifdef KPTY
extern	CON	ptycon[];		/* Pseudo-ttys (/dev/pty*, /dev/ttyp*) */
#define	PTYDRV	ptycon
#else
/* The pty driver needs the termio tty's ttinp/ttoutp/ttwrite0, so it is in the
 * kernel only when that tty is (link-kernel.sh KTTY).  Without it the slot must
 * be empty rather than naming a symbol nothing defines. */
#define	PTYDRV	NULL
#endif
#ifdef KMOUSE
extern	CON	mscon[];		/* HR FN#7 Amiga mouse (/dev/mouse) */
#define	MSDRV	mscon
#else
/* The mouse driver reads the HR display card's mouse ports and is in the
 * kernel only when link-kernel.sh compiles it (KMOUSE), so the slot never
 * names a symbol nothing defines. */
#define	MSDRV	NULL
#endif

/*
 * Device table.
 */
/* Major 5 = alcon, matching the shipped /dev/tty5x nodes (dispatch is
 * positional; the CON major field is informational).  Slot 8 stays NULL: the
 * console driver is loaded into it at run time.  A slot's presence here is what
 * gets its c_load() called at boot (bio.c devinit) and what drvmap() checks
 * before dispatching, so an absent entry answers every open ENXIO.
 *
 * Because slot 8 is empty for the whole of kernel startup, /dev/console does
 * not answer while the kernel is printing its banner -- printf() reaches only
 * the serial port its putchar() drives (sys/z8001/src/console.c).  The text is
 * kept in the kernel (sys/coh/printf.c) and /etc/load puts it on the console it
 * installs here, which is how a machine with a screen gets to see it.
 *
 * Slot 7 = mscon, the HR mouse (sys/drv/mouse.c), which is the major the
 * original HR driver used (../hr/src/driver/hr.c MAJOR 7) so that vendored
 * source needs no renumbering.  It is resident rather than loadable because it
 * samples its ports from a 100 Hz timeout(), and a timeout reaches neither the
 * text nor the data of a driver living in the transient driver window.
 *
 * Slot 11 is reserved for /drv/hostfs (dev dists only; sys/drv/hostfs.c). */
DRV drvl[16] ={
	{nlcon},	{ctcon},	{wdcon},	{lpcon},
	{NULL},		{alcon},	{NULL},		{MSDRV},
	{NULL},		{PTYDRV},	{NULL},		{NULL},
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
dev_t	rootdev = makedev(2, 4);	/* Root = WD hard disk part 4 (block 136) */
dev_t	pipedev = makedev(2, 4);	/* Pipe device */
dev_t	swapdev = makedev(2, 3);	/* Swap device (part 3 tail) */
/*
 * The swap extent is a fact about the MEDIA, so it is not written here: it
 * arrives with the partition table (<sys/bootinfo.h>), either from
 * kboot's kboot.cfg or from the table generated into the kernel, and wdload()
 * assigns both from it.  swapdev above still names the pseudo-drive the
 * extent is relative to; wdload() refuses to swap if the two disagree.
 */
daddr_t	swapbot;			/* Swap base, relative to swapdev */
daddr_t	swaptop;			/* Swap end, exclusive */
int	ronflag	= 0;			/* Not read only */
int	drvn	= 16;			/* Maximum number of devices */
int	mactype	= M_Z8001;		/* Machine type */
