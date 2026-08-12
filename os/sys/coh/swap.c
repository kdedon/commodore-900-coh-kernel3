/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* (lgl-
 *	The information contained herein is a trade secret of Mark Williams
 *	Company, and  is confidential information.  It is provided  under a
 *	license agreement,  and may be  copied or disclosed  only under the
 *	terms of  that agreement.  Any  reproduction or disclosure  of this
 *	material without the express written authorization of Mark Williams
 *	Company or persuant to the license agreement is unlawful.
 *
 *	COHERENT Version 2.3.37
 *	Copyright (c) 1982, 1983, 1984.
 *	An unpublished work by Mark Williams Company, Chicago.
 *	All rights reserved.
 -lgl) */
/*
 * Coherent.
 * Swapper.
 *
 * The swapper is a resident kernel process: it calls timeout()/wakeup()
 * directly, and its main loop never returns, msetsys() having given the
 * process no return address.
 *
 * The other half of the swapper -- swapio, sdalloc, satcopy, segsext,
 * segdupd, segfinm -- is in seg.c; this file supplies the daemon that
 * decides who moves and the segment-granular moves it makes.
 */
#include <sys/coherent.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/seg.h>
#include <sys/buf.h>

/*
 * Functions.
 */
SEG	*xmalloc();
SEG	*xdalloc();
int	swap();

/*
 * Create the swapper and declare swapping available.
 *
 * The swapper is a kernel process: PFKERN keeps it off the swap-out
 * candidate list and out of segload()'s prototype path, and PFLOCK keeps it
 * off it a second time so a future non-kernel swapper stays honest.  sexflag
 * is what salloc(), seggrow() and segdupl() test before they will trade
 * memory for disk, so it is raised here rather than in the daemon: the
 * fallbacks must be live from the moment the process exists, not from the
 * first time it is scheduled.  Compaction (krunch) turns itself off against
 * the same flag -- the two are alternative answers to a fragmented segment
 * list, and only one may move a segment at a time.
 *
 * ORDERING.  This must run after the idle and init processes exist, and the
 * test below is the enforcement rather than a comment, because getting it
 * wrong costs a boot and says nothing about why:
 *
 *	`process' numbers processes from cpid, which starts at zero, and
 *	  dispatch() and krunch() test `p_pid != 0' to mean "not the idle
 *	  process".  Whoever calls process() first takes that identity, and
 *	  everyone after it shifts up -- so a swapper created earlier makes
 *	  idle pid 1 and init pid 2, and orphan reparenting to pid 1 (uexit)
 *	  hands children to the idle process.
 *	setrun() puts us on the run queue, and main() starts the clock
 *	  (batflag) BEFORE it creates either process.  A tick between the two
 *	  finds stand() doing `if (SELF != iprocp) setrun(SELF); dispatch()'
 *	  with SELF still the procq sentinel and iprocp still NULL: it splices
 *	  the list head into its own queue, switches to whatever is runnable
 *	  -- us -- and cannot come back, because the boot context was saved
 *	  under a sentinel the run-queue walk skips by definition.
 *
 * Failure is not fatal: the machine runs exactly as it did before swapping
 * existed, refusing the allocations it cannot satisfy.
 */
swapinit()
{
	register PROC *pp;
	register int s;

	if (iprocp==NULL || eprocp==NULL || sexflag!=0)
		return (0);
	if (swapbot >= swaptop)
		return (0);
	if ((pp=process(swap)) == NULL)
		return (0);
	/* Names it for ps(1): a kernel process never execs, so process() has
	 * left u_comm empty and nothing else will fill it. */
	pkname(pp, "swap");
	pp->p_flags |= PFLOCK;
	s = sphi();
	setrun(pp);
	spl(s);
	sexflag++;
	return (1);
}

/*
 * The swapper.
 *
 * Each pass picks the swapped-out runnable process with the best ratio of
 * accumulated swap value to kilobytes it would cost to bring in, then makes
 * room for it by swapping out the least important resident processes.  A
 * process whose importance is still positive is not moved; it is flagged
 * PFDISP instead, which makes the clock give it up at its next tick.
 */
swap()
{
	register SEG *sp;
	register PROC *pp1;
	register PROC *pp2;
	register PROC *pp3;
	register unsigned s;
	register unsigned n;
	register unsigned t;
	register unsigned v;
	register unsigned m;
	register int i;
	static unsigned ltimer;

	for (;;) {
		lock(pnxgate);
		t = (utimer - ltimer) / NSUTICK;
		v = t * SVCLOCK;
		ltimer += t * NSUTICK;

		/*
		 * Search for a process to swap into memory.
		 */
		pp2 = NULL;
		m   = 0;
		for (pp1 = procq.p_nback; pp1 != &procq; pp1 = pp1->p_nback) {

			/*
			 * Process resides in memory.  Age its swap value and
			 * its importance.  A shift count is taken modulo the
			 * word size by the hardware, so an interval long
			 * enough to shift the value away entirely -- the
			 * first pass after boot, where ltimer is still zero
			 * -- is spelled out rather than shifted.
			 */
			if ((pp1->p_flags&PFCORE) != 0) {
				if (t >= 16)
					pp1->p_sval = 0;
				else
					pp1->p_sval >>= t;
				pp1->p_ival -= t;
				if (pp1->p_ival < -30000)
					pp1->p_ival = -30000;
				continue;
			}

			/*
			 * Update swap value - high values swapped in first.
			 */
			addu(pp1->p_sval, v);

			/*
			 * Process is not runnable.
			 */
			if (pp1->p_state != PSRUN)
				continue;

			/*
			 * Kilobytes that would have to be read in.  s_size is
			 * a byte count and does not fit an int, so the divide
			 * is done in the segment's own type and only the
			 * kilobyte total is narrowed.
			 */
			s = 0;
			for (i = 0; i < NUSEG+1; i++)
				if ((sp=pp1->p_segp[i]) != NULL)
					if ((sp->s_flags&SFCORE) == 0)
						s += (unsigned)
						     (sp->s_size / 1024L);
			if (s == 0)
				s = 1;

			/*
			 * Compute importance:
			 *
			 *	swap value + response value
			 *	---------------------------
			 *	  Kbytes to be swapped in
			 */
			n = (pp1->p_sval + pp1->p_rval) / s;

			/*
			 * More important.
			 */
			if (n > m) {
				m = n;
				pp2 = pp1;
			}
		}
		unlock(pnxgate);

		/*
		 * No runnable processes swapped out.
		 */
		if (pp2 == NULL)
			goto con;

#ifndef	NOMONITOR
		if (swmflag)
			printf("Swapin(%p, %d)\n", pp2, pp2->p_pid);
#endif
	xxx:
		/*
		 * Try to swap the process into memory.
		 */
		while (testcore(pp2)==0 || proccore(pp2)!=0) {

			/*
			 * Put back what did come in, so the next pass sees a
			 * whole process on disk rather than half of one.
			 */
			procdisk(pp2);
			i   = 32767;
			pp3 = NULL;

			/*
			 * Search for a process to swap out.
			 */
			lock(pnxgate);
			for (pp1=procq.p_nforw; pp1!=&procq; pp1=pp1->p_nforw){

				if (pp1->p_flags&(PFSWIO|PFLOCK|PFKERN))
					continue;

				/*
				 * Process is not totally memory resident:
				 * finish the job rather than start another.
				 */
				if ((pp1->p_flags&PFCORE) == 0) {
					if (procdisk(pp1) != 0) {
						unlock(pnxgate);
						goto xxx;
					}
					continue;
				}

				/*
				 * Process too important to swap out.
				 */
				if (pp1->p_ival>-64 && pp1->p_sval!=0)
					continue;

				/*
				 * Less important.
				 */
				if (pp1->p_ival < i) {
					i = pp1->p_ival;
					pp3 = pp1;
				}
			}
			unlock(pnxgate);

			/*
			 * No processes to swap out.
			 */
			if (pp3 == NULL) {
#ifndef NOMONITOR
				if (swmflag)
					printf("No one to swap out\n");
#endif
				break;
			}

			/*
			 * Still important: ask the clock to take the
			 * processor away from it instead of the memory.
			 */
			if (i > 0) {
#ifndef NOMONITOR
				if (swmflag)
					printf("Dispatch(%p, %d)\n",
						pp3, pp3->p_pid);
#endif
				pp3->p_flags |= PFDISP;
				break;
			}
#ifndef NOMONITOR
			if (swmflag)
				printf("Swapout(%p, %d)\n", pp3, pp3->p_pid);
#endif
			procdisk(pp3);
		}

#ifndef NOMONITOR
		if (swmflag)
			printf("Swapdone\n");
#endif
	con:
		timeout(&stimer, NSRTICK, wakeup, (char *)&stimer);
		sleep((char *)&stimer, CVSWAP, IVSWAP, SVSWAP);
	}
}

/*
 * See if the given process may fit in core.
 */
testcore(pp)
register PROC *pp;
{
	register SEG *sp;
	register fsize_t s;
	register paddr_t s1;
	register paddr_t s2;
	register int i;

	/*
	 * Find the largest segment the process still has on disk.
	 */
	s = 0;
	for (i = 0; i < NUSEG+1; i++) {

		if ((sp=pp->p_segp[i]) == NULL)
			continue;

		/*
		 * Segment is memory resident.
		 */
		if ((sp->s_flags&SFCORE) != 0)
			continue;

		/*
		 * Largest segment so far.
		 */
		if (sp->s_size > s)
			s = sp->s_size;
	}

	/*
	 * See if it will fit in one of the holes.  The last hole runs to
	 * coretop, which is where xmalloc() below looks for it too: answering
	 * this question against a smaller map than the allocator uses reports
	 * a shortage that does not exist, and the caller pays for it by
	 * swapping somebody out to make room already available.
	 */
	s1 = corebot;
	sp = &segmq;
	do {
		if ((sp=sp->s_forw) != &segmq)
			s2 = sp->s_paddr;
		else
			s2 = coretop;

		/*
		 * It fits!
		 */
		if (s2-s1 >= s)
			return (1);

		/*
		 * Compute start of next hole.
		 */
		s1 = sp->s_paddr + sp->s_size;

	} while (sp != &segmq);

	return (0);
}

/*
 * Swap all segments associated with a particular process into core.
 * The number of segments still swapped out is returned.
 */
proccore(pp)
register PROC *pp;
{
	register SEG *sp;
	register int i;
	register int n;
	register int f;

	f = pp->p_flags&PFSWAP;

	/*
	 * Try to swap in all user segments and the auxiliary segment.
	 */
	for (n = 0, i = 0; i < NUSEG+1; i++) {

		if ((sp=pp->p_segp[i]) == NULL)
			continue;

		/*
		 * Process was swapped out: take back the lock reference
		 * procdisk() gave up on its way to disk.
		 */
		if (f != 0)
			sp->s_lrefc++;

		/*
		 * Segment is disk resident - try to swap it in.
		 */
		if ((sp->s_flags&SFCORE) == 0)
			if (segcore(sp) == 0)
				n++;
	}

	/*
	 * No segments left on disk - mark process as being memory resident.
	 */
	if (n == 0)
		pp->p_flags |= PFCORE;

	/*
	 * Mark process as no longer being disk resident.
	 */
	pp->p_flags &= ~PFSWAP;

	return (n);
}

/*
 * Swap out all segments associated with a given process.
 */
procdisk(pp)
register PROC *pp;
{
	register SEG *sp;
	register int i;
	register int f;
	int n;

	f = pp->p_flags&PFSWAP;

	/*
	 * Mark process as no longer being memory resident BEFORE swapping.
	 */
	pp->p_flags &= ~PFCORE;

	/*
	 * Try to swap out all user segments and the auxiliary segment.
	 */
	for (n = 0, i = 0; i < NUSEG+1; i++) {

		if ((sp=pp->p_segp[i]) == NULL)
			continue;

		/*
		 * Process not already swapped out.
		 */
		if (f == 0)
			sp->s_lrefc--;

		/*
		 * Segment already swapped out.
		 */
		if ((sp->s_flags&SFCORE) == 0)
			continue;

		/*
		 * Segment no longer referenced by a memory-resident process.
		 */
		if (sp->s_lrefc==0 && segdisk(sp)!=0)
			n++;
	}

	/*
	 * Mark process as being disk resident.
	 */
	pp->p_flags |= PFSWAP;

	return (n);
}

/*
 * Swap the given segment into core.
 * NOTE: Although swapped out, the segment may have a descriptor table entry,
 *	 and therefore have a valid s_faddr field.
 */
segcore(sp1)
register SEG *sp1;
{
	register SEG *sp2;

	lock(seglink);

	/*
	 * Segment has been moved to memory while we waited to lock.
	 */
	if ((sp1->s_flags&SFCORE) != 0) {
		unlock(seglink);
		return (1);
	}

	/*
	 * Reserve the memory.  xmalloc() splices a placeholder into the
	 * memory queue, so the hole stays ours across the transfer.
	 */
	if ((sp2=xmalloc(sp1->s_size)) == NULL) {
		unlock(seglink);
		return (0);
	}

	/*
	 * Read the disk segment sp1 into the memory segment sp2.
	 */
	sp1->s_lrefc++;
	swapio(0, sp2->s_paddr, sp1->s_daddr, sp2->s_size);
	sp1->s_lrefc--;

	/*
	 * Remove segment sp1 from the disk queue.
	 */
	sp1->s_back->s_forw = sp1->s_forw;
	sp1->s_forw->s_back = sp1->s_back;

	/*
	 * Insert segment sp1 into memory queue replacing segment sp2.
	 */
	sp2->s_back->s_forw = sp1;
	sp1->s_back = sp2->s_back;
	sp2->s_forw->s_back = sp1;
	sp1->s_forw = sp2->s_forw;

	/*
	 * Enable access to memory segment sp1.
	 */
	sp1->s_flags |= SFCORE;
	sp1->s_paddr = sp2->s_paddr;
	vremap(sp1);

	unlock(seglink);

	return (1);
}

/*
 * Swap the given segment out onto disk.
 */
segdisk(sp1)
register SEG *sp1;
{
	register SEG *sp2;

	lock(seglink);

	/*
	 * Verify segment sp1 did not become busy while we waited to lock.
	 * IE: raw disk i/o, or shared code fork.
	 */
	if (sp1->s_lrefc != 0) {
		unlock(seglink);
		return (0);
	}

	/*
	 * Segment has been moved to disk while we waited to lock.
	 */
	if ((sp1->s_flags&SFCORE) == 0) {
		unlock(seglink);
		return (1);
	}

	/*
	 * Reserve the disk.
	 */
	if ((sp2=xdalloc(sp1->s_size)) == NULL) {
		unlock(seglink);
		return (0);
	}

	/*
	 * Disable access to memory segment sp1.
	 */
	sp1->s_flags &= ~SFCORE;
	sp1->s_daddr = sp2->s_daddr;
	vremap(sp1);

	/*
	 * Write the memory segment sp1 into the disk segment sp2.
	 */
	sp1->s_lrefc++;
	swapio(1, sp1->s_paddr, sp2->s_daddr, sp1->s_size);
	sp1->s_lrefc--;

	/*
	 * Remove segment sp1 from the memory queue.
	 */
	sp1->s_back->s_forw = sp1->s_forw;
	sp1->s_forw->s_back = sp1->s_back;

	/*
	 * Insert segment sp1 into disk queue replacing segment sp2.
	 */
	sp2->s_back->s_forw = sp1;
	sp1->s_back = sp2->s_back;
	sp2->s_forw->s_back = sp1;
	sp1->s_forw = sp2->s_forw;

	unlock(seglink);

	return (1);
}

/*
 * Allocate a segment on disk that is `s' bytes long.
 * The `seglink' gate should be locked before this routine is called.
 * This routine is the same as `sdalloc' except that we can't run out of
 * alloc space to allocate the segment and we allocate in high regions.
 * NOTE: descriptor table entries are not released.
 */
SEG *
xdalloc(s)
fsize_t s;
{
	register SEG *sp1;
	register SEG *sp2;
	register daddr_t d;
	register daddr_t d1;
	register daddr_t d2;

	/*
	 * Blocks, not bytes: s is already a whole number of clicks and a
	 * click is a whole number of blocks, so the divide is exact.  Both
	 * ends stay in daddr_t -- the swap extent runs past 32767 blocks on
	 * any disk worth swapping to.
	 */
	d  = s / (fsize_t)BSIZE;
	d2 = swaptop;
	sp1 = &segdq;
	do {
		if ((sp1=sp1->s_back) != &segdq)
			d1 = sp1->s_daddr + (sp1->s_size / (fsize_t)BSIZE);
		else
			d1 = swapbot;

		if (d2-d1 >= d) {
			sp2 = &segswap;
			kclear((char *)sp2, sizeof(SEG));
			sp1->s_forw->s_back = sp2;
			sp2->s_forw  = sp1->s_forw;
			sp1->s_forw  = sp2;
			sp2->s_back  = sp1;
			sp2->s_urefc = 1;
			sp2->s_lrefc = 1;
			sp2->s_size  = s;
			sp2->s_daddr = d2 - d;
			return (sp2);
		}

		d2 = sp1->s_daddr;

	} while (sp1 != &segdq);

	return (NULL);
}

/*
 * Allocate a segment in memory that is `s' bytes long.
 * The `seglink' gate should be locked before this routine is called.
 * This routine is the same as `smalloc' except that we can't run out of
 * alloc space to allocate the segment.
 * NOTE: Do NOT remap virtual descriptor table entry.
 *	 This is a scratch entry, and the s_faddr field is not retained.
 */
SEG *
xmalloc(s)
register fsize_t s;
{
	register SEG *sp1;
	register SEG *sp2;
	register paddr_t s1;
	register paddr_t s2;

	/*
	 * s comes from a segment that salloc()/seggrow() rounded to a click,
	 * and every base in the queue is corebot or the click-aligned end of
	 * the segment below it, so the hole handed back is click-aligned:
	 * the MMU maps click granules and a segment that starts mid-click
	 * comes back mapped somewhere else.
	 */
	s1  = corebot;
	sp1 = &segmq;
	do {
		if ((sp1=sp1->s_forw) != &segmq)
			s2 = sp1->s_paddr;
		else
			s2 = coretop;

		if (s2-s1 >= s) {
			sp2 = &segswap;
			kclear((char *)sp2, sizeof(SEG));
			sp1->s_back->s_forw = sp2;
			sp2->s_back = sp1->s_back;
			sp1->s_back = sp2;
			sp2->s_forw = sp1;
			sp2->s_urefc = 1;
			sp2->s_lrefc = 1;
			sp2->s_size  = s;
			sp2->s_paddr = s1;
			return (sp2);
		}

		s1 = sp1->s_paddr + sp1->s_size;

	} while (sp1 != &segmq);

	return (NULL);
}
