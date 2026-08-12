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
 * Machine dependent types.
 */

#ifndef TYPES_H
#define TYPES_H	TYPES_H

/*
 * Mapping types.
 */
typedef	unsigned int	aold_t;		/* Auxiliary map save		*/
typedef	unsigned int	bmap_t;		/* Buffer map			*/
typedef	unsigned int	bold_t;		/* Buffer map save		*/
#ifdef	_Z8001
typedef	char	*cmap_t;		/* Clist map (0.7.3 Z8001: a real
					 * pointer -- clists live in their own
					 * permanently mapped segment) */
#else
typedef	unsigned int	cmap_t;		/* Clist map			*/
#endif
typedef	unsigned int	cold_t;		/* Clist map save		*/
typedef	unsigned int	dmap_t;		/* Driver map			*/
typedef	unsigned int	dold_t;		/* Driver map save		*/

/*
 * System types.
 */
typedef	unsigned short	comp_t;		/* Accounting			*/
typedef	long		daddr_t;	/* Disk address			*/
typedef	unsigned short	dev_t;		/* Device			*/
typedef	long	 	fsize_t;	/* Lengths (same as off_t)	*/
#ifndef _SIZE_T
#define _SIZE_T
typedef	unsigned int	size_t;		/* sizeof result (also in <stddef.h>)	*/
#endif
typedef	unsigned short	ino_t;		/* Inode number			*/
#ifndef _MODE_T
#define _MODE_T
typedef	unsigned short	mode_t;		/* File mode (permissions/type)	*/
#endif
typedef	long	 	off_t;		/* Lengths			*/
typedef	long	 	paddr_t;	/* Physical memory address	*/
typedef	long	 	sig_t;		/* Signal bits			*/
typedef	long	 	time_t;		/* Time				*/
#ifdef	_Z8001
typedef	unsigned long	vaddr_t;	/* Virtual memory address (seg:off,
					 * the 0.7.3 Z8001 width) */
#else
typedef	unsigned int	vaddr_t;	/* Virtual memory address	*/
#endif
typedef	char	 	GATE[2];	/* Gate structure		*/
#if	_I386
typedef	long		cseg_t;		/* Page descriptor		*/
#else
typedef	long		faddr_t;	/* Far virtual memory address	*/
typedef	unsigned int	saddr_t;	/* Segmenation address		*/
#endif

/* Fixed-width types (Minix-net compatible); exact on the Z8001 (int=16,long=32) */
#ifndef _U8_T
#define _U8_T
typedef	unsigned char	u8_t;		/*  8-bit unsigned		*/
typedef	unsigned short	u16_t;		/* 16-bit unsigned		*/
typedef	unsigned long	u32_t;		/* 32-bit unsigned		*/
typedef	char		i8_t;		/*  8-bit signed		*/
typedef	short		i16_t;		/* 16-bit signed		*/
typedef	long		i32_t;		/* 32-bit signed		*/
typedef	struct { u32_t _[2]; } u64_t;	/* 64-bit (pair of u32)		*/
/* Argument-promotion forms (used in prototypes/K&R defs); on the Z8001 a
 * u16_t promotes to unsigned int, so these are the widened equivalents. */
typedef	int		U8_t;		/*  8-bit unsigned, promoted	*/
typedef	unsigned int	U16_t;		/* 16-bit unsigned, promoted	*/
typedef	unsigned long	U32_t;		/* 32-bit unsigned, promoted	*/
typedef	int		I8_t;		/*  8-bit signed, promoted	*/
typedef	int		I16_t;		/* 16-bit signed, promoted	*/
typedef	long		I32_t;		/* 32-bit signed, promoted	*/
#endif

#endif
