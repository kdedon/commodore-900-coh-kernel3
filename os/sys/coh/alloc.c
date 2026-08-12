/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* $Header: /kernel/kersrc/coh.286/RCS/alloc.c,v 1.1 92/07/17 15:17:49 bin Exp Locker: bin $ */
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
 * Storage allocator.
 *
 * $Log:	alloc.c,v $
 * Revision 1.1  92/07/17  15:17:49  bin
 * Initial revision
 * 
 * Revision 1.1	88/03/24  16:13:25	src
 * Initial revision
 * 
 */
#include <sys/coherent.h>
#include <sys/alloc.h>
#include <errno.h>
#include <sys/proc.h>

/*
 * Create an arena.
 */
ALL *
setarena(cp, n)
register char *cp;
{
	register ALL *ap1;
	register ALL *ap2;

	if ((char *)(ap1=align(cp)) < (char *)cp)
		ap1++;
	if ((ap2=align(&cp[n])-1) < ap1)
		panic("Arena %o too small", (int) cp);
	ap1->a_link = (char *)ap2;
	ap2->a_link = (char *)ap1;
	setused(ap2);
	return (ap1);
}

/*
 * Number of allocations the arena has refused since boot.  A global, not a
 * static, so it can be read out of /dev/kmem or the kernel debugger without
 * waiting for the console line below.
 */
unsigned kallfail;

/*
 * Allocate `l' bytes of memory.
 */
char *
alloc(apq, l)
ALL *apq;
unsigned l;
{
	register ALL *ap;
	register ALL *ap1;
	register ALL *ap2;
	register unsigned i;
	register unsigned n;
	register unsigned s;

	n = 1 + (l + sizeof(ALL) - 1) / sizeof(ALL);
	for (i=0; i<2; i++) {
		for (ap1=apq; link(ap1)!=apq; ap1=link(ap1)) {
			if (ap1 == NULL)
				panic("Corrupt arena");
			if (tstfree(ap1)) {
			       for (ap2=link(ap1); tstfree(ap2); ap2=link(ap2))
					if (ap2 == apq)
						break;
				ap1->a_link = (char *)ap2;
				if ((s=ap2-ap1) >= n) {
					if (s > n) {
						if (i == 0)
							continue;
						ap = &ap1[n];
						ap->a_link = (char *)ap2;
						ap1->a_link = (char *)ap;
					}
					setused(ap1);
					kclear((char *)ap1->a_data, l);
					return (ap1->a_data);
				}
			}
		}
	}
	/*
	 * Say so.  There is exactly one arena (allkp, sized by ALLSIZE), and
	 * every caller turns this NULL into an error of its own: fork(2)
	 * reports EAGAIN "no more process table entries", open(2) reports
	 * EAGAIN, poll(2) simply registers nothing.  None of them names the
	 * arena, so exhaustion arrives as three unrelated faults in three
	 * unrelated subsystems.  One line on the console costs less than the
	 * hour spent looking for a process table that does not exist.
	 *
	 * Printed once: the caller that ran out is usually in a loop, and a
	 * console line per failed fork(2) buries the machine deeper than the
	 * shortage did.  kallfail keeps counting after the message.
	 */
	if (kallfail++ == 0)
		printf("alloc: kernel arena exhausted, %u bytes wanted "
			"(raise ALLSIZE)\n", l);
	u.u_error = EKSPACE;
	return (NULL);
}

/*
 * Free memory.
 */
free(cp)
char *cp;
{
	register ALL *ap;
	extern char end;

	ap = ((ALL *)cp) - 1;
	if (ap<(ALL *)&end) {
		printf("cp=%p ap=%p &end=%p\n", cp, ap, &end);
		panic("Bad free %o\n", (unsigned)cp);
	}
	if (tstfree(ap)) {
		printf("cp=%p tstfree(%p)=%x\n", cp, ap, tstfree(ap));
		panic("Bad free %o\n", (unsigned)cp);
	}
	setfree(ap);
}
