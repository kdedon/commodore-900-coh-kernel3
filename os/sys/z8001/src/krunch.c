/*
 * Commodore M-series Z8001 -- memory compaction.
 *
 * Ported from the i286 krunch.c (3.2 MD): scan the segment list for
 * unused memory immediately below unlocked application segments and
 * shuffle each such segment down into the hole.  The machine
 * dependencies are plrcopy() (ascending physical move, pcopy.c) and
 * vremap() (recompute the s_faddr click base, mdstub.c); the i286's
 * "did the current process move" test compared its ucs/uds selector
 * globals -- here we scan SELF's p_segp[] for the moved segment and
 * segload() to rebuild the MMU prototype.
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

krunch(n)
int n;
{
	register SEG *sp;
	register int i;
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
	 * Do not crunch segment list if swapper is active.
	 */
	if (KRUNCH == 0 || sexflag != 0)
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
		 * Segment is locked for I/O.
		 */
		if (sp->s_lrefc != sp->s_urefc)
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
		moved = 0;
		for (i = 0; i <= NUSEG; i++)
			if (SELF->p_segp[i] == sp)
				moved = 1;
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
