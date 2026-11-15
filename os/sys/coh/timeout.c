/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* $Header: /kernel/kersrc/coh.286/RCS/timeout.c,v 1.1 92/07/17 15:18:54 bin Exp Locker: bin $ */
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
 * Timeout management.
 *
 * $Log:	timeout.c,v $
 * Revision 1.1  92/07/17  15:18:54  bin
 * Initial revision
 * 
 * Revision 1.2	89/08/01  13:56:42 	src
 * Bug:	#include <timeout.h> not accurate; timeout.h now in /usr/include/sys.
 * Fix:	#include <sys/timeout.h> now used. (ABC)
 * 
 * Revision 1.1	88/03/24  08:14:38 	src
 * Initial revision
 * 
 * 87/07/23	Allan Cornish		/usr/src/sys/coh/timeout.c
 * Timeout2 function now cancels timer if delay value is 0.
 *
 * 87/07/08	Allan Cornish		/usr/src/sys/coh/timeout.c
 * Timeout2 function added to support long timeouts.
 *
 * 87/07/07	Allan Cornish		/usr/src/sys/coh/timeout.c
 * Support for multiple timing queues ported from RTX.
 *
 * 86/11/24	Allan Cornish		/usr/src/sys/coh/timeout.c
 * Added support for new t_last field in tim struct.
 */
#include <sys/coherent.h>
#include <sys/timeout.h>
#include <sys/fun.h>

/*
 * A loaded driver's TIM lives in its own data, which the kernel reaches only
 * through the overlay segment while that driver is mapped there.  Such a TIM
 * is never linked into a queue: a resident stand-in is, and it maps the
 * driver back in around the call.
 */
#define	NDTIM	8
#define	INOVL(p)	(((int)((long)(p) >> 24) & 0x7F) == OS)

static struct dtim {
	TIM	d_tim;			/* What the queues hold */
	TIM	*d_tp;			/* Driver's TIM; NULL when free */
	dold_t	d_map;			/* Overlay map of its driver */
	int	(*d_func)();
	char	*d_farg;
} dtim[NDTIM];

static struct dtim *dtnow;	/* Slot whose function is running */

/*
 * The slot stays the driver's while its function runs, so a re-arm from
 * there reuses it without searching.
 */
static
dtfire(dp)
register struct dtim *dp;
{
	struct dtim *prev;
	dold_t o;
	int s;

	dsave(o);
	drest(dp->d_map);
	prev = dtnow;
	dtnow = dp;
	(*dp->d_func)(dp->d_farg, dp->d_tp);
	dtnow = prev;
	s = sphi();
	if (dp->d_tim.t_last == NULL)
		dp->d_tp = NULL;
	spl(s);
	drest(o);
}

static
dtimeout(tp, n, f, a, two)
TIM *tp;
long n;
int (*f)();
char *a;
{
	register struct dtim *dp;
	dold_t m;
	int s;

	dsave(m);
	s = sphi();
	for (dp = dtim; dp < &dtim[NDTIM]; dp++)
		if (dp->d_tp == tp && dp->d_map == m) {
			timeout(&dp->d_tim, 0, (int (*)())0, (char *)0);
			dp->d_tp = NULL;
			break;
		}
	tp->t_func = f;
	tp->t_farg = a;
	tp->t_lbolt = lbolt + n;
	if (f == NULL || two && n == 0) {
		spl(s);
		return;
	}
	for (dp = dtim; dp->d_tp != NULL; dp++)
		if (dp == &dtim[NDTIM-1])
			panic("timeout: no driver slot");
	dp->d_tp = tp;
	dp->d_map = m;
	dp->d_func = f;
	dp->d_farg = a;
	if (two)
		timeout2(&dp->d_tim, n, dtfire, (char *)dp);
	else
		timeout(&dp->d_tim, (unsigned)n, dtfire, (char *)dp);
	spl(s);
}

/*
 * Given a pointer to a timeout structure, `tp', call the function `f'
 * with integer argument `a' in `n' ticks of the clock. The list is
 * searched to see if the specified timeout structure is already in a
 * list, and it is removed if already there.
 */
timeout(tp, n, f, a)
register TIM *tp;
unsigned n;
int (*f)();
char *a;
{
	register TIM ** qp;
	register struct dtim *dp;
	int s;

	if (INOVL(tp)) {
		/*
		 * A driver re-arming from its own function: put the
		 * stand-in straight back on a queue.  The map tells it
		 * from another driver's interrupt with a TIM at the
		 * same offset.
		 */
		s = sphi();
		if ((dp = dtnow) != NULL && dp->d_tp == tp && f != NULL
		 && dp->d_tim.t_last == NULL && dp->d_map == omapget()) {
			dp->d_func = f;
			dp->d_farg = a;
			tp = &dp->d_tim;
			tp->t_lbolt = lbolt + n;
			qp = &timq[ (unsigned long)tp->t_lbolt % nel(timq) ];
			if ( tp->t_next = *qp )
				tp->t_next->t_last = tp;
			tp->t_last = qp;
			*qp = tp;
			spl(s);
			return;
		}
		spl(s);
		dtimeout(tp, (long)n, f, a, 0);
		return;
	}

	/*
	 * Already on a timing queue.
	 */
	s = sphi();
	if ( qp = tp->t_last ) {
		tp->t_last = NULL;
		if ( *qp = tp->t_next )
			tp->t_next->t_last = qp;
	}
	spl( s );

	if ( (tp->t_func = f) == NULL )
		return;

	/*
	 * Calculate clock tick at which timeout is to occur.
	 * Record function and argument to be invoked upon timeout.
	 */
	tp->t_lbolt = lbolt + n;
	tp->t_farg  = a;

	/*
	 * Identify timeout queue.
	 */
	qp = &timq[ (unsigned long)tp->t_lbolt % nel(timq) ];

	/*
	 * Insert at head of timeout queue.
	 */
	s = sphi();
	if ( tp->t_next = *qp )
		tp->t_next->t_last = tp;
	tp->t_last = qp;
	*qp = tp;
	spl(s);
}

timeout2(tp, n, f, a)
register TIM *tp;
long n;
int (*f)();
char *a;
{
	register TIM ** qp;
	int s;

	if (INOVL(tp)) {
		dtimeout(tp, n, f, a, 1);
		return;
	}

	/*
	 * Already on a timing queue.
	 */
	s = sphi();
	if ( qp = tp->t_last ) {
		tp->t_last = NULL;
		if ( *qp = tp->t_next )
			tp->t_next->t_last = qp;
	}
	spl( s );

	/*
	 * Do not schedule new timer if no function or delay interval.
	 */
	if ( ((tp->t_func = f) == NULL) || (n == 0) )
		return;

	/*
	 * Calculate clock tick at which timeout is to occur.
	 * Record function and argument to be invoked upon timeout.
	 */
	tp->t_lbolt = lbolt + n;
	tp->t_farg  = a;

	/*
	 * Identify timeout queue.
	 */
	qp = &timq[ (unsigned long)tp->t_lbolt % nel(timq) ];

	/*
	 * Insert at head of timeout queue.
	 */
	s = sphi();
	if ( tp->t_next = *qp )
		tp->t_next->t_last = tp;
	tp->t_last = qp;
	*qp = tp;
	spl(s);
}
