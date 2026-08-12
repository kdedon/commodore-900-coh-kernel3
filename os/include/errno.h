/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* (-lgl
 * 	COHERENT Version 4.0
 * 	Copyright (c) 1982, 1992 by Mark Williams Company.
 * 	All rights reserved. May not be copied without permission.
 -lgl) */
/*
 * /usr/include/errno.h
 * Error codes.
 */

#ifndef	ERRNO_H
#define	ERRNO_H	ERRNO_H

#define	EPERM	1		/* Not super user */
#define	ENOENT	2		/* No such file or directory */
#define	ESRCH	3		/* Process not found */
#define	EINTR	4		/* Interrupted system call */
#define	EIO	5		/* I/O error */
#define	ENXIO	6		/* No such device or address */
#define	E2BIG	7		/* Argument list too long */
#define	ENOEXEC	8		/* Sys exec format error */
#define	EBADF	9		/* Bad file number */
#define	ECHILD	10		/* No children (wait) */
#define	EAGAIN	11		/* No more processes are available */
#define	ENOMEM	12		/* Cannot map process into memory */
#define	EACCES	13		/* Permission denied */
#define	EFAULT	14		/* Bad system call argument address */
#define	ENOTBLK	15		/* Block device required (mount) */
#define	EBUSY	16		/* Device busy (mount) */
#define EEXIST	17		/* File already exists */
#define	EXDEV	18		/* Cross device link */
#define	ENODEV	19		/* No such device */
#define ENOTDIR	20		/* Not a directory */
#define	EISDIR	21		/* Is a directory */
#define	EINVAL	22		/* Invalid argument */
#define	ENFILE	23		/* File table overflow */
#define	EMFILE	24		/* Too many open files for this process */
#define	ENOTTY	25		/* Not a terminal */
#define	ETXTBSY	26		/* Text file busy */
#define	EFBIG	27		/* File too big to map */
#define	ENOSPC	28		/* No space left on device */
#define	ESPIPE	29		/* Illegal seek on a pipe */
#define	EROFS	30		/* Read only filesystem */
#define	EMLINK	31		/* Too many links */
#define	EPIPE	32		/* Broken pipe */
#define	EDOM	33		/* Domain error */
#define	ERANGE	34		/* Result too large */
#define	EKSPACE	35		/* Out of kernel space */
#define	ENOLOAD	36		/* Driver not loaded */
#define	EBADFMT	37		/* Bad format */
#ifdef _I386
#define EDATTN	199		/* Device needs attention do not use !! */
#else
#define EDATTN	38		/* Device needs attention */
#endif
#define	EDBUSY	39		/* Device busy */
#define	EDEADLK	40		/* Deadlock */
#define	ENOLCK	41		/* No lock available */

/*
 * Urgent data.  A network-only condition with no COHERENT equivalent, and it
 * needs two numbers of its own rather than one shared with something else: a
 * client toggles urgent mode on the difference between "urgent data is waiting"
 * and "it has run out" (net/gen/tcp_io.h NWTO_RCV_URG / NWTO_RCV_NOTURG, which
 * is what telnet does with them).  Folded onto EINVAL, as they were, a read
 * could only report that something was wrong.
 */
#define	EURG	42		/* Urgent data present */
#define	ENOURG	43		/* No urgent data present */

/*
 * The connection errnos.  COHERENT predates sockets and has none of these, so
 * inet_chan.c's translation folded the whole family onto EIO -- a client could
 * learn that something had gone wrong and nothing about what.  A program has to
 * tell "nobody is listening" from "the route is dead" from "it timed out" to
 * retry sensibly, and ftp and rlogin both branch on them.  Numbered to continue
 * this table rather than to match Minix's, since the values a client sees must be
 * the ones the runtime sets (that mismatch made telnet's urgent-data test dead
 * code in both arms).
 */
#define	EADDRINUSE	44	/* Address already in use */
#define	ECONNREFUSED	45	/* Connection refused */
#define	ECONNRESET	46	/* Connection reset by peer */
#define	ETIMEDOUT	47	/* Connection timed out */
#define	EDSTNOTRCH	48	/* Destination not reachable */
#define	ENOTCONN	49	/* Not connected */
#define	EISCONN		50	/* Already connected */
#define	ESHUTDOWN	51	/* Write on a shut-down connection */
#define	ENOCONN		52	/* No such connection */


#ifndef KERNEL
/*
 * Globals for user programs.
 */
extern	int	errno;
extern	int	sys_nerr;
extern	char	*sys_errlist[];
#endif

#endif

/* end of errno.h */
