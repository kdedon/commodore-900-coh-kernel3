/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* (-lgl
 * 	COHERENT Version 3.0
 * 	Copyright (c) 1982, 1990 by Mark Williams Company.
 * 	All rights reserved. May not be copied without permission.
 -lgl) */
/*
 * Scheduling parameters.
 * These are identical to the pdp11 parameters except that there is no MAP
 * parameter for claiming the unibus map.
 */
#ifndef	 SCHED_H
#define	SCHED_H SCHED_H

/*
 * Update parameters.  All values are in ticks.  The processor value
 * update interval is always 1.
 */
#define	NCRTICK	(HZ/10)			/* Processor time slice */
#define	NSUTICK	(HZ/6)			/* Swap value update interval */
#define	NSRTICK	(HZ/2)			/* Swap run update interval */

/*
 * Load average.
 *
 * Three exponentially decayed averages of the number of processes running or
 * waiting to run, over one, five and fifteen minutes.  The kernel samples the
 * count every LAVSECS seconds and folds it in:
 *
 *	av += ((nrun << FSHIFT) - av) * LAVDn / 65536
 *
 * where LAVDn is 1-exp(-LAVSECS/tau) in 16-bit fixed point, tau being the
 * averaging period in seconds.  Both quantities are integers: there is no
 * floating point in the kernel, and the averages leave it in the fixed-point
 * form below rather than as a decimal, so the scaling is the reader's.
 *
 * FSHIFT of 11 puts eleven fraction bits under a 16-bit average, which is
 * three digits of a load and a ceiling of 32; LAVMAX is one less, so that a
 * sample can never scale past what the average holds.
 *
 * LAVSECS is in seconds and LAVTICK converts it, so the sampling period is
 * whatever HZ ticks the machine's clock actually delivers -- the averages are
 * in the same time the rest of the kernel keeps.
 */
#define	FSHIFT	11			/* Fraction bits in a load average */
#define	FSCALE	(1 << FSHIFT)		/* ... and the multiplier they are */
#define	NLOADAV	3			/* One, five and fifteen minutes */
#define	LAVSECS	5			/* Seconds between samples */
#define	LAVTICK	(LAVSECS * HZ)		/* ... in ticks */
#define	LAVMAX	((int)(0xffff >> FSHIFT))  /* Largest countable run queue */
#define	LAVD1	5240			/* 1-exp(-5/60),  x 65536 */
#define	LAVD5	1083			/* 1-exp(-5/300), x 65536 */
#define	LAVD15	363			/* 1-exp(-5/900), x 65536 */

/*
 * Values.
 */
#define CVNOSIG	256			/* Lower priorities can interrupt */
#define CVCLOCK	1			/* Core value update */
#define	SVCLOCK	16			/* Swap value update */

/*
 * Child process.
 */
#define CVCHILD	32767			/* Initial child core value */
#define IVCHILD	16			/* Importance */
#define SVCHILD	4096			/* Initial child swap value */
#define RVCHILD	0			/* Response value */

/*
 * Swapper.
 */
#define	CVSWAP	256
#define	IVSWAP	0
#define	SVSWAP	0

/*
 * Waiting for block I/O to complete.
 */
#define CVBLKIO	32767
#define IVBLKIO	32767
#define	SVBLKIO	0

/*
 * Waiting for a gate to open.
 */
#define	CVGATE	16384
#define IVGATE	3
#define SVGATE	0

/*
 * Terminal output.
 */
#define	CVTTOUT	256
#define	IVTTOUT	0
#define SVTTOUT	0

/*
 * Waiting for free clists.
 */
#define CVCLIST	256
#define IVCLIST	0
#define SVCLIST	0

/*
 * Process trace.
 */
#define CVPTSET	256
#define IVPTSET	0
#define SVPTSET	0

/*
 * Process trace stop.
 */
#define CVPTRET	256
#define IVPTRET	0
#define SVPTRET	0

/*
 * Waiting for a pipe.
 */
#define CVPIPE	0
#define IVPIPE	0
#define SVPIPE	0

/*
 * Terminal input.
 */
#define CVTTIN	255
#define IVTTIN	1
#define SVTTIN	32767

/*
 * Pause.
 */
#define CVPAUSE	0
#define IVPAUSE	-64
#define SVPAUSE	0

/*
 * Wait.
 */
#define CVWAIT	128
#define IVWAIT	-128
#define SVWAIT	4096

#endif
