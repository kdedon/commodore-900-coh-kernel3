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
 * Coherent for Commodore M-series z8001 processor
 * running in Segmented mode.
 * Common handler for system traps.
 */
#include <coherent.h>
#include <errno.h>
#include <proc.h>
#include <signal.h>
#include <systab.h>
#include <uproc.h>
#include <seg.h>

/* 0.7.3 systab tokens (see tab.c) */
#ifndef VOID
#define	VOID	0
#define	PTR	1
#endif

/*
 * Machine dependent system call table.
 */
int	unone();
int	ubpt();
int	uhalt();
struct systab sysdtab[NMDCALL] ={
	0,		  INT,	unone,		/* 128 = sgrow */
	0,		 VOID,	ubpt,		/* 129 = bpt */
	0,		 VOID,	uhalt,		/* 130 = halt */
};

/* Segment trap reason bits */
#define	PWW	0x20			/* Primary write warning (Yellow) */
#define	SLV	0x04			/* Segment length violation (Red) */

/*
 * This is called when a processor trap occurs.
 * The peculiar register declaration forces saving
 * of r6-r12 which may or may not be saved depedning
 * on number of registers declared.
 */
#define	REGS	r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13,r14,r15
/* ARGSUSED */
trap(emap, omap, sp, REGS, sig, id, fcw, pc)
unsigned emap, omap;
char *sp, *pc;
unsigned sig, id, fcw;
{
	long l;
	register struct systab *stp;
	register int n;

	u.u_error = 0;
	if ((fcw & MFSYS) != 0) {	/* System mode trap */
#if DDT
		/*
		 * Say what happened before stopping: the debugger's own banner
		 * gives the pc and the signal, but a transcript that ends at a
		 * `*' prompt is the only record of the fault, so the panic line
		 * has to be in it.  Then hand over rather than halting -- the
		 * point of building KDDT=1 is to examine the machine here.
		 */
		printf("System trap type %d at %p\n", sig, pc);
		ddt(sig);
#else
		panic("System trap type %d at %p", sig, pc);
#endif
		return;
	} else if (sig != SIGSYS) {	/* Not system call */
		if (sig == SIGSEGV)
			if (stviol(id, pc, sp))
				return;
		/*
		 * A user program's fault is the program's business: send the
		 * signal.  0.7.3 entered the debugger here too, which stops the
		 * whole machine every time anything dumps core.
		 */
		sendsig(sig, SELF);
		return;
	}
	if ((n = (id&0xFF)) < NMICALL)
		stp = &sysitab[n];
	else if (n>=SMDCALL && n<SMDCALL+NMDCALL)
		stp = &sysdtab[n-SMDCALL];
	else {
		sendsig(SIGSYS, SELF);
		return;
	}
	ukcopy((char *)(sp+sizeof(pc)), (char *)u.u_args, stp->s_alen);
	if (u.u_error)
		goto err;
	u.u_io.io_seg = IOUSR;
	if (envsave(&u.u_sigenv)) {
		u.u_error = EINTR;
		goto err;
	}
	l = (*(long(*)())stp->s_func)(u.u_args[0],
				      u.u_args[1],
				      u.u_args[2],
				      u.u_args[3],
				      u.u_args[4],
				      u.u_args[5]);
	if (u.u_error) {
	err:
		l = -1;
		putuwd(MUERR, u.u_error);
		if (u.u_error == EFAULT)
			sendsig(SIGSYS, SELF);
	}
	switch (stp->s_type) {
	case INT:
		r1 = l;
		break;
	case PTR:
	case LONG:
		r0 = ((struct l *)&l)->l_hi;
		r1 = ((struct l *)&l)->l_lo;
	case VOID:
		break;
	default:
		panic("trap: bad return type\n");
	}
}

/*
 * Send a breakpoint signal to ourselves.
 */
ubpt()
{
	sendsig(SIGTRAP, SELF);
}

/*
 * Enter the debugger.
 */
uhalt()
{
	if (super() == 0)
		return;
	halt();
}

/*
 * Handle a stack limit violation.  Returns non-zero if the trap was the
 * stack's and has been dealt with, zero if the caller should turn it into a
 * signal.
 *
 * THE ALLOWANCE IS MADSIZE, and exstack() has already committed all of it to
 * the segment, so on this machine the usual answer here is that there is
 * nothing to do.  The two traps the MMU raises mean different things:
 *
 *	Yellow (PWW), a write to the segment's lowest 256-byte page.  The
 *	write has SUCCEEDED; the trap only says the stack is in its last
 *	page.  At the allowance that is a report, not a fault.
 *	Red (SLV), an access below the segment.  The access was suppressed
 *	and the instruction aborted.  At the allowance this is a genuine
 *	stack overflow and the process is signalled.
 *
 * A segment that is somehow under the allowance is taken to it in one step
 * rather than to the offset that faulted, because a stack cannot be extended
 * a little at a time here: the warning page is 256 bytes, a frame larger than
 * that is allocated in one subtraction from r15 and steps clean over it, and
 * the Red trap it then takes has aborted an instruction that cannot in
 * general be restarted -- the Z8010 pushes the address of the FOLLOWING
 * instruction, and its length is not recoverable from that.
 *
 * The Red path does restart the frame-setup stores the MWC compiler emits
 * before it touches a new frame at all: those are idempotent and their length
 * is known, so the saved PC is backed up over the store.
 */
stviol(id, pc, usp)
register int id;
register vaddr_t pc, usp;
{
	register SEG *sp;
	register unsigned nb, cur, max;

	sp = SELF->p_segp[SISTACK];
	/* CLICKS on both sides: s_size holds bytes (see uproto() in
	 * commodore.c and the bruc() in seggrow()). */
	cur = btocru((long)sp->s_size);
	max = btocru((long)MADSIZE);

	if (id & SLV) {			/* Red stack warning */
		if (getuwi(pc) != 0xA1FD)		/* ld r13, r15 */
			return (0);
		if (cur >= max)
			return (0);
		if (getuwi(pc-4) == 0x1CE9)		/* ldm (rr14), rx,$y */
			regl[OPCOFF] -= 4;
		else {
			nb = getuwi(pc -2);
			if (nb == 0x2FED		/* ld (rr14), r13 */
			 || nb == 0x1DEC)		/* ldl (rr14), rr12 */
				regl[OPCOFF] -= 2;
		}
	} else if ((id & PWW) == 0)	/* not a Yellow stack warning */
		return (0);
	else if (cur >= max)
		/*
		 * A Yellow trap on a stack that is already at its allowance:
		 * the write it warns about landed inside the segment and has
		 * completed, so there is nothing to do and nothing is wrong.
		 * Every write to the last page of a full stack arrives here.
		 */
		return (1);

	segsize(sp, (vaddr_t)MADSIZE);
	if (u.u_error != 0) {
		/*
		 * No room for the whole allowance.  Take the clicks this
		 * access needs, so a memory-tight system still runs the
		 * program rather than killing it; the next frame warns again.
		 */
		u.u_error = 0;
		nb = btocru(0-(unsigned)usp);
		if (nb <= cur)
			nb = cur + 1;
		segsize(sp, (vaddr_t)ctob((vaddr_t)nb));
	}
	return (1);
}
