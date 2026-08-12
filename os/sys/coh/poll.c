/* $Header: /kernel/kersrc/coh.286/RCS/poll.c,v 1.1 92/07/17 15:18:09 bin Exp Locker: bin $ */
/*
 *	The  information  contained herein  is a trade secret  of INETCO
 *	Systems, and is confidential information.   It is provided under
 *	a license agreement,  and may be copied or disclosed  only under
 *	the terms of that agreement.   Any reproduction or disclosure of
 *	this  material  without  the express  written  authorization  of
 *	INETCO Systems or persuant to the license agreement is unlawful.
 *
 *	Copyright (c) 1986
 *	An unpublished work by INETCO Systems, Ltd.
 *	All rights reserved.
 */

/*
 * [Stream] Polling.
 *
 *	void pollinit( ) -- allocate polling buffers
 *	int pollopen(qp) -- enable polling  by current process  on given queue
 *	int pollwake(qp) -- wake all processes waiting for poll on given queue
 *	int pollexit(  ) -- terminate all polls enabled by current process
 *	event_t * ep;
 *
 * A queue head `qp' may be a datum of a LOADABLE driver, reachable only while
 * that driver holds the transient driver window (bio.c drvmap/drest).  The
 * event buffers themselves are always kernel resident, so each one records the
 * window its head was reached through:
 *
 *	pollopen() and pollwake() are called from inside the owning driver, so
 *	  the head is mapped and the window is simply noted.
 *	pollexit() is called from the poll(2) system call after the driver has
 *	  been unmapped, so it maps each head back before unlinking it.
 *
 * Buffers exist only for the duration of one poll(2) call: upoll() (sys2.c)
 * pollexit()s on every path out, and pexit() pollexit()s again on the way to
 * PSDEAD so that nothing survives on a device queue with a freed PROC in
 * e_procp.
 *
 * $Log:	poll.c,v $
 * Revision 1.1  92/07/17  15:18:09  bin
 * Initial revision
 * 
 * Revision 1.1	88/03/24  16:14:10	src
 * Initial revision
 * 
 * 86/11/19	Allan Cornish		/usr/src/sys/coh/poll.c
 * Ported to Coherent from RTX.
 */

#include <sys/coherent.h>
#include <sys/proc.h>

/*
 * Patchable data.
 */
int	NPOLL  = 0;

/*
 * An event buffer as it is ALLOCATED.  A device queue sees only the event_t at
 * the front -- which is what lets a head be embedded in a TTY or an inode --
 * while the trailing window belongs to poll(2) alone and is never reached
 * through a queue.  `pe_map' holds the driver window (dsave() units) of the
 * head this buffer is linked onto; zero is a legitimate value, so it is not a
 * flag, and every buffer carries one.
 */
typedef struct pev {
	event_t	pe_ev;			/* as the device queues see it	*/
	dold_t	pe_map;			/* driver window of the head	*/
} PEV;

#define	emap(ep)	(((PEV *) (ep))->pe_map)

/*
 * Private data.
 */
static	event_t	* efreep;

/**
 *
 * event_t *
 * pollinit()		-- allocate event buffers.
 */
event_t *
pollinit()
{
	register PEV * ep = (PEV *) 0;
	register PEV * ap;
	static int first = 1;

	/*
	 * If dynamically growing event pool is specified [NPOLL == 0],
	 * try to allocate an additional cluster of 32 on each call.
	 */
	if ( NPOLL == 0 ) {
		if ( ep = (PEV *) kalloc( 32 * sizeof(PEV) ) )
			ap = ep + 32;
	}

	/*
	 * If statically sized event pool is specified [NPOLL != 0],
	 * try to allocate the pool on the first call.
	 */
	else if ( first ) {
		first = 0;
		if ( ep = (PEV *) kalloc( NPOLL * sizeof(PEV) ) )
			ap = ep + NPOLL;
	}

	/*
	 * If event cluster was allocated, insert into free event queue.
	 */
	if ( ep ) {
		do {
			ep->pe_ev.e_pnext = efreep;
			efreep = &ep->pe_ev;
		} while ( ++ep < ap );
	}

	return efreep;
}

/**
 *
 * int
 * pollopen(qp) -- enable polling by current process on given event queue
 * event_t * qp;
 */
pollopen( qp )
register event_t * qp;
{
	register event_t * ep;

	/*
	 * Initialize device queue if required.
	 */
	if ( qp->e_dnext == 0 )
		qp->e_dnext = qp->e_dlast = qp;

	/*
	 * Obtain a free event buffer, or return.
	 */
	if ( ((ep = efreep) == 0) && ((ep = pollinit()) == 0) ) {
		printf("out of poll buffers\n");
		return;
	}

	/*
	 * Remove event buffer from free queue.
	 */
	efreep = ep->e_pnext;

	/*
	 * Record process pointer in event buffer.
	 */
	ep->e_procp = cprocp;

	/*
	 * Record the driver window `qp' was reached through, for pollexit().
	 * The caller is the queue's own driver, so the window is its own.
	 */
	dsave( emap(ep) );

	/*
	 * Insert event at head of process event singularly-linked queue.
	 */
	ep->e_pnext = cprocp->p_polls;
	cprocp->p_polls = ep;

	/*
	 * Insert event at tail of circularly-linked device queue.
	 * This ensures that processes are first-in first-out.
	 */
	ep->e_dnext  = qp;
	(ep->e_dlast = qp->e_dlast)->e_dnext = ep;
	qp->e_dlast  = ep;

	/*
	 * Record last process to enable polling on device.
	 */
	qp->e_procp = cprocp;
}

/**
 *
 * int
 * pollwake( qp ) -- wake all processes waiting for poll on given queue
 * event_t * qp;
 *
 * Dereferences the head, so it must be called with the head's driver mapped:
 * from a driver entry point (bio.c holds the window across those) or from that
 * driver's own interrupt handler (md.s vint loads vmaps[] for the vector).
 */
pollwake( qp )
event_t * qp;
{
	register event_t * ep = qp;
	register PROC    * pp;

	/*
	 * Clear device process pointer, indicating poll completed.
	 * NOTE: interrupt handlers may have already cleared it.
	 */
	qp->e_procp = 0;

	if ( ep = qp->e_dnext ) {

		/*
		 * Service circularly-linked polls on device queue.
		 */
		while ( ep != qp ) {
			/*
			 * Wake process if it is sleeping.
			 */
			if ( (pp = ep->e_procp) && (pp->p_state == PSSLEEP) )
				wakeup( &pp->p_polls );

			ep = ep->e_dnext;
		}
	}
}

/**
 *
 * int
 * pollexit() -- terminate all polls opened by current process
 *
 * Reads cprocp, so only the owning process may call it; there is no form that
 * cleans up after another process.  Each buffer leaves p_polls as it is
 * unlinked, so a second call, or a call by a process that holds no polls at
 * all, walks an empty list and returns: safe on a generic exit path.
 */
int
pollexit()
{
	register PROC    * pp = cprocp;
	register event_t * ep;
	dold_t	dold;

	/*
	 * Service all polling event buffers enabled by current process.
	 */
	while ( ep = pp->p_polls ) {

		/*
		 * Remove event buffer from circularly-linked device queue,
		 * with the queue's driver mapped: the head, and only the head,
		 * may be a datum of a loadable driver.  The buffers on either
		 * side of it are kernel resident and do not care.
		 */
		dsave( dold );
		drest( emap(ep) );
		(ep->e_dnext->e_dlast = ep->e_dlast)->e_dnext = ep->e_dnext;
		drest( dold );

		/*
		 * Remove event buffer from singularly-linked process queue.
		 */
		pp->p_polls = ep->e_pnext;

		/*
		 * Insert event buffer at head of free event buffer queue.
		 */
		ep->e_pnext = efreep;
		efreep = ep;
	}
}
