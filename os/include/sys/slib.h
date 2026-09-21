/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Loaded-library table, keyed by device and inode.
 *
 * Library code holds its own segment numbers, so each library maps at the
 * same segment pair in every process, assigned at first load, downward
 * from NHUSEG to SLFLOOR.  The table holds one s_urefc of the shared
 * segment; at 1 the library is unused but stays until slreclaim().
 */
#ifndef	SLIB_H
#define	SLIB_H	SLIB_H

#ifndef	SHLIB_H
#include <shlib.h>
#endif

/* A process sbrk'ing all of memory reaches about USEG+13, below this. */
#define	SLFLOOR	18			/* Lowest library segment	*/

/* Small: several of each live on the kernel stack during a bind. */
#define	SLPATH	80			/* dir + `/' + name + NUL	*/
#define	SLNAME	(NCPLN+1)		/* a loader name, terminated	*/

/*
 * A library's own imports are bound once, into its pristine private image.
 * Its dependencies are recorded so clients map them too and slreclaim()
 * keeps them.
 */
#define	NSLDEP	3			/* libraries one library may use */

typedef	struct	slib {
	dev_t	sl_dev;			/* Device of the library file	*/
	ino_t	sl_ino;			/* Inode of it: the key		*/
	struct	seg *sl_shrp;		/* Shared segment, SFSHRX	*/
	struct	seg *sl_prvp;		/* Pristine private image	*/
	unsigned char sl_seg;		/* Shared half's hardware seg	*/
					/* (the private half is +1)	*/
	unsigned char sl_flags;		/* SLF_ below			*/
	unsigned sl_nexp;		/* Export count			*/
	unsigned sl_expoff;		/* Exports' offset in sl_shrp	*/
	fsize_t	sl_symoff;		/* Its own L_SYM, in the file	*/
	unsigned sl_nsym;		/* ... and how many records	*/
	unsigned char sl_ndep;		/* Libraries this one imports	*/
	unsigned char sl_drefc;		/* Loaded libraries importing it*/
	struct	slib *sl_dep[NSLDEP];	/* ... which they are		*/
}	SLIB;

#define	SLF_USED	0x01		/* Entry is a loaded library	*/
#define	SLF_BUSY	0x02		/* Its own imports are not bound yet */

extern	SLIB	slibt[];

#endif	/* SLIB_H */
