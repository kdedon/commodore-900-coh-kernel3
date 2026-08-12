/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* (-lgl
 * 	COHERENT Version 3.0
 * 	Copyright (c) 1982, 1990 by Mark Williams Company.
 * 	All rights reserved. May not be copied without permission.
 -lgl) */
#ifndef	POLL_H
#define	POLL_H
/*
 * This is a temporary file, and will NOT be binary compatible with System V.
 */

/*
 * Polling structure.
 */
struct pollfd {
	int	fd;		/* file descriptor	*/
	short	events;		/* requested events	*/
	short	revents;	/* returned events	*/
};

/*
 * Stream oriented events.
 */
#define	POLLIN	 000001		/* input data is available		*/
#define	POLLPRI	 000002		/* priority message is available	*/
#define	POLLOUT	 000004		/* output can be sent			*/
#define	POLLERR	 000010		/* a fatal error has occurred		*/
#define	POLLHUP	 000020		/* a hangup condition exists		*/
#define	POLLNVAL 000040		/* fd does not access an open stream	*/

/*
 * Each pollable event in the system has an associated event queue.
 * An polled event will be
 *	on a singularly-linked list throuch cprocp->p_polls, and
 *	on a circularly-linked list through an event queue on the device.
 *
 * The HEAD of a device queue is embedded in the object polled -- a TTY, an
 * inode, a pty -- so for a loadable driver it is a datum of that driver and is
 * addressable only while the driver holds the transient driver window.  This
 * structure is therefore the queue's shape and nothing more: poll.c allocates
 * its own larger buffer (PEV) to carry the window of the head each one is
 * linked onto, and grows THAT rather than this.
 */
typedef
struct event {
	struct event *	e_pnext;	/* next polled event on proc	*/
	struct event *	e_dnext;	/* next polled event on device	*/
	struct event *	e_dlast;	/* prev polled event on device	*/
	struct proc  *	e_procp;	/* pointer to polling process	*/
} event_t;

#endif
