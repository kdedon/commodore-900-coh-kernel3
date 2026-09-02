/*
 * Commodore M-series Z8001 -- memory compaction.
 *
 * Ported from the i286 krunch.c (3.2 MD): scan the segment list for
 * unused memory immediately below unlocked application segments and
 * shuffle each such segment down into the hole.  The machine
 * dependencies are plrcopy() (ascending physical move, pcopy.c) and
 * vremap() (recompute the s_faddr click base, mdstub.c); the i286's
 * "did the current process move" test compared its ucs/uds selector
 * globals -- here movable() reports whether SELF owns the segment and
 * segload() rebuilds the MMU prototype.
 */
#include <sys/coherent.h>
#include <sys/proc.h>
#include <sys/seg.h>
#include <sys/mmu.h>

/*
 * Time interval in clock ticks between krunch attempts: default 2 seconds.
 */
int KRUNCH = 200;

extern int depth;			/* md.s: interrupt/system depth */

/*
 * Retry count for the pending krunch timer, and the callback the timer list
 * actually holds.
 *
 * timeout()'s callback argument is a char *, and clock.c invokes the callback
 * as (*t_func)(t_farg, tp) -- so a callback declared to take an int pops 16
 * bits where 32 were pushed, and a caller passing an int pushes 16 where
 * timeout() reads 32.  krunch() wants a small count, so the count lives here
 * and the callback's argument is left unused.  There is exactly one timer
 * (`tim' below), so one static count is equivalent to carrying it per-entry.
 */
static int krtimn;

static
krtimer(a, tp)
char *a;
TIM *tp;
{
	krunch(krtimn);
}

/*
 * A segment may be slid only when every reference to it is accounted for
 * by the process table.  Each owner holds one `p_segp' slot and one
 * `s_urefc'; each owner that has not been swapped out holds one
 * `s_lrefc' as well (procdisk() and proccore() are what trade the lock
 * reference for a place on disk).  So the walk below must find exactly
 * `s_urefc' slots and exactly `s_lrefc' unswapped owners.
 *
 * A short count means the segment exists but is not yet installed:
 * segdupl() has bumped `s_urefc' and segadup() has not stored the
 * pointer yet, or a process is asleep in exsread() reading a freshly
 * allocated segment whose address it has already handed to the disk as
 * `io_phys'.  Moving either one writes the copy somewhere its owner will
 * never look, or lets a transfer already in flight land on whatever
 * takes the old memory.  A long `s_lrefc' means somebody holds the
 * segment for I/O -- bio.c across a raw transfer, whose driver has the
 * physical address in hand, or segsext()/segdupd() across a swap write.
 * A count of no slots at all is the swapper's `segswap' placeholder or a
 * segment between salloc() and its first owner, and neither may move.
 *
 * An owner that is wired down refuses the move outright, as does our own
 * u area: the kernel stack we are running on lives in it, so the copy
 * would race the stack it is copying.  `selfp' is set when the segment
 * belongs to the current process by any other slot, which is what tells
 * the caller to rebuild its own map.
 *
 * The walk reads `procq' without `pnxgate'.  It cannot take that gate:
 * the swapper holds it across procdisk(), which locks `seglink', and
 * krunch() already holds `seglink' here.  It does not need it either --
 * the list is threaded only by newproc() and relproc() at process level,
 * nothing here sleeps, and md.s runs stand() only on the way back to
 * user mode, so no other process can run between the first slot examined
 * and the last.
 */
static
movable(sp, selfp)
register SEG *sp;
int *selfp;
{
	register PROC *pp;
	register int i;
	int n;
	int c;

	n = 0;
	c = 0;
	*selfp = 0;
	for (pp = procq.p_nforw; pp != &procq; pp = pp->p_nforw) {
		for (i = 0; i < NUSEG+1; i++) {
			if (pp->p_segp[i] != sp)
				continue;
			if ((pp->p_flags&(PFLOCK|PFKERN|PFSWIO|PFSLIB)) != 0)
				return (0);
			if (pp == SELF) {
				if (i == SIUSERP)
					return (0);
				*selfp = 1;
			}
			n++;
			if ((pp->p_flags&PFSWAP) == 0)
				c++;
		}
	}
	return (n!=0 && n==sp->s_urefc && c==sp->s_lrefc);
}

krunch(n)
int n;
{
	register SEG *sp;
	paddr_t paddr;
	saddr_t osel;
	int moved;
	static TIM tim;
	int s;

	/* Compaction moves segments; doing that from an interrupt would move
	 * memory out from under whatever was interrupted.  The donor printed a
	 * debug line here; it is gone because the 0x30 code segment has no room
	 * for one, and because a message on a path taken during normal operation
	 * is noise rather than diagnosis. */
	if (depth != 0)
		return;

	/*
	 * Compaction and swapping are both answers to a fragmented segment
	 * list, and this is the cheaper one: sliding a segment is a single
	 * memory-to-memory copy where segsext() is two disk transfers.  They
	 * run together.  A segment the swapper holds for I/O carries an
	 * s_lrefc above its unswapped owner count and is skipped by
	 * movable(), which is the interlock that keeps one mover at a time
	 * on any given segment.
	 *
	 * Other processes mapping a segment this pass moves pick the new
	 * base up through segload(), which rebuilds the prototype with
	 * sproto() in the resumed process's own context whenever sexflag is
	 * set -- the same path a swap-in relies on.
	 */
	if (KRUNCH == 0)
		return;

	/*
	 * Segment count of 0 indicates a request to schedule delayed krunch(1).
	 */
	if (n <= 0) {
		if (tim.t_last != NULL) {
			krtimn = 1;
			timeout(&tim, KRUNCH, krtimer, (char *)0);
		}
		return;
	}

	/*
	 * Segmentation is locked - retry later.
	 */
	s = sphi();
	if (locked(seglink)) {
		krtimn = n;
		timeout(&tim, KRUNCH, krtimer, (char *)0);
		spl(s);
		return;
	}
	lock(seglink);
	spl(s);

	for (paddr = corebot, sp = &segmq;
	     (sp = sp->s_forw) != &segmq;
	     paddr = sp->s_paddr + sp->s_size) {

		/*
		 * No hole exists.
		 */
		if (paddr == sp->s_paddr)
			continue;

		/*
		 * Don't try to shuffle high segments into low memory.
		 */
		if (sp->s_flags & SFHIGH)
			break;

		/*
		 * System segment.
		 */
		if (sp->s_flags & SFSYST)
			continue;

		/*
		 * Segment may be in process of being swapped in/out.
		 */
		if ((sp->s_flags & SFCORE) == 0)
			continue;

		/*
		 * Every reference to the segment is accounted for by a
		 * process that may give it up.
		 */
		if (movable(sp, &moved) == 0)
			continue;

		/*
		 * Remember previous click base, shift the segment into
		 * the hole, recompute its address.
		 */
		osel = FP_SEL(sp->s_faddr);
		plrcopy(sp->s_paddr, paddr, sp->s_size);
		sp->s_paddr = paddr;
		vremap(sp);

		/*
		 * Ensure user segmentation is updated -- we may have
		 * moved a segment of the current process.
		 */
		if (SELF->p_pid != 0 && moved)
			segload();
		if (uasa == osel)
			uasa = FP_SEL(sp->s_faddr);

		/*
		 * Crunch count reached.
		 */
		if (--n <= 0)
			break;
	}

	/*
	 * Cancel timer if all low memory holes eliminated,
	 * else attempt to crunch another segment in KRUNCH ticks.
	 */
	if (KRUNCH == 0 || sp == &segmq || (sp->s_flags & SFHIGH))
		timeout(&tim, 0, (int (*)())0, (char *)0);	/* cancel; cast, see proc.c */
	else {
		krtimn = 1;
		timeout(&tim, KRUNCH, krtimer, (char *)0);
	}

	unlock(seglink);
}
