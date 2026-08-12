/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* $Header: /kernel/kersrc/coh.286/RCS/sys2.c,v 1.1 92/07/17 15:18:51 bin Exp Locker: bin $ */
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
 * System calls (filesystem related).
 *
 * $Log:	sys2.c,v $
 * Revision 1.1  92/07/17  15:18:51  bin
 * Initial revision
 * 
 * Revision 1.1	91/04/30  13:56:54	root
 * Shipped with COH 3.1.0.
 * 
 */
#include <sys/coherent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/fd.h>
#include <sys/ino.h>
#include <sys/inode.h>
#include <sys/mount.h>
#include <sys/poll.h>
#include <sys/sched.h>
#include <sys/stat.h>

/*
 * Determine accessibility of the given file.
 */
uaccess(np, mode)
char *np;
register int mode;
{
	register INODE *ip;
	register int r;

	schizo();
	r = ftoi(np, 'r');
	schizo();
	if (r != 0)
		return;
	ip = u.u_cdiri;
	if ((mode&imode(ip, u.u_ruid, u.u_rgid)) != mode)
		u.u_error = EACCES;
	idetach(ip);
	return (0);
}

/*
 * Schizo - swap real and effective id's.
 */
schizo()
{
	register int t;

	t = u.u_uid;
	u.u_uid = u.u_ruid;
	u.u_ruid = t;
	t = u.u_gid;
	u.u_gid = u.u_rgid;
	u.u_rgid = t;
}

/*
 * Turn accounting on or off.
 */
uacct(np)
register char *np;
{
	register INODE *ip;

	if (super() == 0)
		return;
	if (np == NULL) {
		if (acctip == NULL) {
			u.u_error = EINVAL;
			return;
		}
		ldetach(acctip);
		acctip = NULL;
	} else {
		if (acctip != NULL) {
			u.u_error = EINVAL;
			return;
		}
		if (ftoi(np, 'r') != 0)
			return;
		ip = u.u_cdiri;
		if ((ip->i_mode&IFMT) != IFREG) {
			u.u_error = EINVAL;
			idetach(ip);
			return;
		}
		iunlock(ip);
		acctip = ip;
	}
	return (0);
}

/*
 * Set current directory.
 */
uchdir(np)
char *np;
{
	setcdir(np, &u.u_cdir);
	return (0);
}

/*
 * Given a directory name and a pointer to a working directory pointer,
 * Save the inode associated with the directory name in the working
 * directory pointer and release the old one.  This is used to change
 * working and root directories.
 */
setcdir(np, ipp)
char *np;
register INODE **ipp;
{
	register INODE *ip;

	if (ftoi(np, 'r') != 0)
		return;
	ip = u.u_cdiri;
	if ((ip->i_mode&IFMT) != IFDIR) {
		u.u_error = ENOTDIR;
		idetach(ip);
		return;
	}
	if (iaccess(ip, IPE) == 0) {
		u.u_error = EACCES;
		idetach(ip);
		return;
	}
	iunlock(ip);
	ldetach(*ipp);
	*ipp = ip;
}

/*
 * Change the mode of a file.
 */
uchmod(np, mode)
char *np;
{
	register INODE *ip;

	if (ftoi(np, 'r') != 0)
		return;
	ip = u.u_cdiri;
	if (owner(ip->i_uid)) {
		if (u.u_uid != 0)
			mode &= ~ISVTXT;
		ip->i_mode &= IFMT;
		ip->i_mode |= mode&~IFMT;
		icrt(ip);	/* chmod - ctime */
	}
	idetach(ip);
	return (0);
}

/*
 * Change owner and group of a file.
 */
uchown(np, uid, gid)
char *np;
{
	register INODE *ip;

	if (ftoi(np, 'r') != 0)
		return;
	ip = u.u_cdiri;
	if (super()) {
		ip->i_mode &= ~(ISUID | ISGID);  /* clear any setuid/setgid */
		ip->i_uid = uid;
		ip->i_gid = gid;
		icrt(ip);	/* chown - ctime */
	}
	idetach(ip);
	return (0);
}

/*
 * Set root directory.
 */
uchroot(np)
register char *np;
{
	if (super())
		setcdir(np, &u.u_rdir);
	return (0);
}

/*
 * Close the given file descriptor.
 */
uclose(fd)
{
	fdclose(fd);
	return (0);
}

/*
 * Create a file with the given mode.
 */
ucreat(np, mode)
char *np;
register int mode;
{
	register INODE *ip;
	register int fd;
	register int cflag;

	cflag = 0;
	if (ftoi(np, 'c') != 0)
		return;
	if ((ip=u.u_cdiri) == NULL) {
		if ((ip=imake((mode&~IFMT)|IFREG, 0)) == NULL)
			return;
	} else {
		if (iaccess(ip, IPW)==0) {
			idetach(ip);
			return;
		}
		switch (ip->i_mode&IFMT) {
		case IFBLK:
		case IFCHR:
			break;
		case IFDIR:
			u.u_error = EISDIR;
			idetach(ip);
			return;
		default:
			if (getment(ip->i_dev, 1) == NULL) {
				idetach(ip);
				return;
			}
		}
		cflag = 1;
	}
	if ((fd=fdopen(ip, IPW)) < 0) {
		idetach(ip);
		return;
	}
	if (cflag)
		iclear(ip);
	iunlock(ip);
	return (fd);
}

/*
 * Duplicate a file descriptor.
 */
udup(ofd, nfd)
{
	return (fddup(ofd, nfd));
}

/*
 * Given a file descriptor, return a status structure.
 */
ufstat(fd, stp)
struct stat *stp;
{
	register INODE *ip;
	register FD *fdp;
	struct stat stat;

	if ((fdp=fdget(fd)) == NULL)
		return;
	ip = fdp->f_ip;
	istat(ip, &stat);
	kucopy(&stat, stp, sizeof(stat));
	return (0);
}

/*
 * File control.
 */
ufcntl( fd, cmd, arg )
int fd, cmd, arg;
{
	register FD * fdp;

	/*
	 * Validate file descriptor.
	 */
	if ( (fd < 0) || (fd >= NUFILE) || ((fdp = u.u_filep[fd]) == 0) ) {
		u.u_error = EBADF;
		return;
	}

	switch ( cmd ) {

	case F_DUPFD:
		/*
		 * Validate base file descriptor.
		 */
		if ( (arg < 0) || (arg >= NUFILE) ) {
			u.u_error = EINVAL;
			return;
		}

		/*
		 * Search for next available file descriptor.
		 */
		do {
			if ( u.u_filep[arg] == 0 ) {
				u.u_filep[arg] = fdp;
				fdp->f_refc++;
				return arg;
			}
		} while ( ++arg < NUFILE );

		u.u_error = EMFILE;
		return;

	case F_SETFL:
		fdp->f_flag &= ~(IPNDLY|IPAPPEND);
		if ( arg & O_NDELAY )
			fdp->f_flag |= IPNDLY;
		if ( arg & O_APPEND )
			fdp->f_flag |= IPAPPEND;
		/* no break */

	case F_GETFL:
		switch ( fdp->f_flag & (IPR+IPW) ) {
		case IPR: arg = O_RDONLY; break;
		case IPW: arg = O_WRONLY; break;
		default:  arg = O_RDWR;   break;
		}
		if ( fdp->f_flag & IPNDLY )
			arg |= O_NDELAY;
		if ( fdp->f_flag & IPAPPEND )
			arg |= O_APPEND;
		return arg;

	default:
		u.u_error = EINVAL;
	}
}

/*
 * Device control information.
 */
uioctl(fd, r, argp)
struct sgttyb *argp;
{
	register FD *fdp;
	register INODE *ip;
	register int mode;

	if ((fdp=fdget(fd)) == NULL)
		return;
	ip = fdp->f_ip;
	mode = ip->i_mode&IFMT;
	if (mode!=IFCHR && mode!=IFBLK) {
		u.u_error = ENOTTY;
		return;
	}
	dioctl(ip->i_a.i_rdev, r, argp);
	return (0);
}

/*
 * Create a link, `np2' to the already existing file `np1'.
 */
ulink(np1, np2)
char *np1;
char *np2;
{
	register INODE *ip1;

	if (ftoi(np1, 'r') != 0)
		return;
	ip1 = u.u_cdiri;
	if ((ip1->i_mode&IFMT)==IFDIR && super()==0) {
		idetach(ip1);
		return;
	}
	iunlock(ip1);
	if (ftoi(np2, 'c') != 0) {
		ldetach(ip1);
		return;
	}
	if (u.u_cdiri != NULL) {
		u.u_error = EEXIST;
		idetach(u.u_cdiri);
		ldetach(ip1);
		return;
	}
	if (ip1->i_dev != u.u_pdiri->i_dev) {
		u.u_error = EXDEV;
		idetach(u.u_pdiri);
		ldetach(ip1);
		return;
	}
	if (iaccess(u.u_pdiri, IPW) == 0) {
		idetach(u.u_pdiri);
		ldetach(ip1);
		return;
	}
	idirent(ip1->i_ino);
	idetach(u.u_pdiri);
	ilock(ip1);
	ip1->i_nlink++;
	icrt(ip1);	/* link - ctime */
	idetach(ip1);
	return (0);
}

/*
 * Seek on the given file descriptor.
 */
fsize_t
ulseek(fd, off, w)
register fsize_t off;
{
	register FD *fdp;
	register INODE *ip;

	if ((fdp=fdget(fd)) == NULL)
		return;
	ip = fdp->f_ip;
	if ((ip->i_mode&IFMT) == IFPIPE) {
		u.u_error = ESPIPE;
		return;
	}
	switch (w) {
	case 0:
		break;
	case 1:
		off += fdp->f_seek;
		break;
	case 2:
		off += ip->i_size;
		break;
	default:
		u.u_error = EINVAL;
		return;
	}
	if (off < 0) {
		u.u_error = EINVAL;
		return;
	}
	fdp->f_seek = off;
	return (off);
}

/*
 * Create a special file.
 */
umknod(np, mode, rdev)
char *np;
dev_t rdev;
{
	register INODE *ip;
	register int type;

	type = mode&IFMT;
	if (type!=IFPIPE && super()==0)
		return;
	if (type!=IFBLK && type!=IFCHR)
		rdev = 0;
	if (ftoi(np, 'c') != 0)
		return;
	if ((ip=u.u_cdiri) != NULL) {
		u.u_error = EEXIST;
		idetach(ip);
		return;
	}
	if ((ip=imake(mode, rdev)) != NULL)
		idetach(ip);
	return (0);
}

/*
 * Mount the device `sp' on the pathname `np'.  The flag, `f',
 * indicates that the device is to be mounted read only.
 */
umount(sp, np, f)
char *sp;
char *np;
{
	register INODE *ip;
	register MOUNT *mp;
	register dev_t rdev;
	register int mode;

	if (ftoi(sp, 'r') != 0)
		return;
	ip = u.u_cdiri;
	if (iaccess(ip, IPR|IPW) == 0)
		goto err;
	mode = ip->i_mode;
	rdev = ip->i_a.i_rdev;
	if ((mode&IFMT) != IFBLK) {
		u.u_error = ENOTBLK;
		goto err;
	}
	idetach(ip);
	/*
	 * Remount (mount -w): promote the already-mounted device to
	 * read/write in place.  Only the device matters, so the mount
	 * directory `np' is not consulted.
	 */
	if ((f&MFRMT) != 0) {
		for (mp=mountp; mp!=NULL; mp=mp->m_next)
			if (mp->m_dev == rdev)
				break;
		if (mp == NULL)
			u.u_error = EINVAL;
		else
			fsremount(mp, f);
		return (0);
	}
	if (ftoi(np, 'r') != 0)
		return;
	ip = u.u_cdiri;
	if (iaccess(ip, IPR) == 0)
		goto err;
	if ((ip->i_mode&IFMT) != IFDIR) {
		u.u_error = ENOTDIR;
		goto err;
	}
	/* Check for current directory, open, or mount directory */
	if (ip->i_refc > 1 || ip->i_ino == ROOTIN) {
		u.u_error = EBUSY;
		goto err;
	}
	for (mp=mountp; mp!=NULL; mp=mp->m_next) {
		if (mp->m_dev == rdev) {
			u.u_error = EBUSY;
			goto err;
		}
	}
	if ((mp=fsmount(rdev, f)) == NULL)
		goto err;
	mp->m_ip = ip;
	ip->i_flag |= IFMNT;
	ip->i_refc++;
err:
	idetach(ip);
	return (0);
}

/*
 * Poll devices for input/output events.
 */
int
upoll( pollfds, npoll, msec )
struct pollfd * pollfds;
unsigned long npoll;
int msec;
{
	register struct pollfd * pollp;	/* current poll pointer		 */
	register FD *	fdp;		/* current file descriptor ptr	 */
	auto	 int	fd;		/* current file descriptor	 */
	auto	 int	rev;		/* last event report received	 */
	auto	 int	nev;		/* number non-zero event reports */
	auto	 int	i;
	extern	 char *	udl;

	/*
	 * Validate number of polls.
	 */
	if ( (npoll < 0) || (npoll > NUFILE) ) {
		u.u_error = EINVAL;
		return;
	}

	/*
	 * If there are any fd's to poll
	 *   validate address of polling information.
	 * npoll of 0 is legal, allows user a short delay.
	 */
	if ( npoll ) {
		pollp = &pollfds[npoll];
#ifdef _Z8001
		/*
		 * No flat limit check -- same reasoning as useracc()
		 * (sys3.c): a user address is a segmented far pointer whose
		 * validity the MMU enforces, and `udl' here holds the
		 * loadable-driver space limit, not a user-data limit.
		 * Keep the null and wrap checks; getuwd/putuwd report a
		 * real fault.
		 */
		if ( (pollfds == NULL) || (pollp < pollfds) ) {
#else
		if ( (pollfds == NULL) || (pollp < pollfds) || (pollp > udl) ) {
#endif
			u.u_error = EFAULT;
			return;
		}
	}

	for (;;) {
		/*
		 * Service each poll in turn.
		 */
		for ( nev=0, i=npoll, pollp = pollfds; i > 0; --i, pollp++ ) {

			/*
			 * Fetch file descriptor.
			 */
			fd = getuwd( &pollp->fd );

			/*
			 * Ignore negative file descriptors.
			 */
			if ( fd < 0 ) {
				rev = 0;
			}

			/*
			 * Poll message queue.
			 */
			else if ( fd >= NUFILE ) {
#ifdef MSGPOLL
/*
 * this code only good if msgpoll() is available to the kernel -
 * no good with loadable msg driver
 */
				rev = msgpoll(  fd,
						getuwd( &pollp->events),
						msec );
#else
				rev = POLLNVAL;
#endif
			}

			/*
			 * Validate file descriptor.
			 */
			else if ( (fdp = u.u_filep[fd]) == 0 ) {
				rev = POLLNVAL;
			}

			/*
			 * Poll a character device driver, or a pipe.
			 *
			 * 4.x backport (v4.2.12 coh.386/sys6.c upoll): 3.2
			 * answered POLLNVAL for everything that was not a
			 * character device, so poll()/select() on a FIFO always
			 * failed -- even though pipe.c carries a complete
			 * ppoll() and already pollwake()s both pipe events.
			 * Nothing called ppoll, here or in the 3.2 reference.
			 * Anything whose IPC is FIFOs (the inet daemon) needs
			 * this.
			 */
			else switch ( fdp->f_ip->i_mode & IFMT ) {
			case IFCHR:
				rev = dpoll( fdp->f_ip->i_a.i_rdev,
						getuwd(&pollp->events),
						msec );
				break;

			case IFPIPE:
				rev = ppoll( fdp->f_ip,
						getuwd(&pollp->events),
						msec );
				break;

			default:
				rev = POLLNVAL;
				break;
			}

			/*
			 * Remember reponses.
			 */
			putuwd( &pollp->revents, rev );

			/*
			 * Record number of non-zero responses.
			 */
			if ( rev != 0 )
				nev++;
		}

		/*
		 * Non-blocking poll or poll response received.
		 */
		if ( (nev != 0) || (msec == 0) ) {
			pollexit();
			/* Cancel a timer still armed from a previous pass, so
			 * it cannot fire into an unrelated later sleep on the
			 * same channel.  timeout() with a null function just
			 * dequeues.
			 *
			 * (int (*)())0, not NULL: the kernel's NULL is a bare 0
			 * (sys/h/coherent.h), which is a 16-bit int, while
			 * timeout() declares this parameter int (*f)() -- a
			 * 32-bit far pointer.  With no prototype to widen it the
			 * call pushed two bytes where four are read, so `f' was
			 * assembled from the literal plus half of the next
			 * argument and `a' came off the end of the list.  It
			 * happens to read as zero, which is the value that makes
			 * this call do what it intends, so it has been working by
			 * luck.  Every other cancel site already carries the cast
			 * (proc.c, tty.c, krunch.c); this one was added later. */
			if ( msec > 0 )
				timeout( &cprocp->p_polltim, 0,
					 (int (*)())0, (char *)0 );
			return nev;
		}

		/*
		 * Schedule the wakeup timer if a positive delay was given.
		 *
		 * This used to be conditional on `p_polltim.t_func == NULL',
		 * which reads as "do not re-arm a timer that is already
		 * pending" but is not what it tests.  NOTHING ever clears
		 * t_func: timeout() sets it, and the clock handler unlinks the
		 * timer and calls it but leaves the field set (clock.c).  So
		 * t_func is NULL only for a process that has never armed one --
		 * proc.c initialises it -- and the FIRST timed poll in a process
		 * armed a timer while every later one silently did not, then
		 * slept with nothing to wake it.
		 *
		 * A select() with a timeout therefore worked exactly once per
		 * process and hung forever after: tests/pollpipe reproduces it
		 * in two calls, and it hung on the second one whichever kind of
		 * descriptor that was.
		 *
		 * No guard is needed at all.  timeout() begins by removing tp
		 * from any queue it is already on -- that is what t_last is for
		 * -- so re-arming is safe and is the intended use.
		 */
		if ( msec > 0 ) {
			/*
			 * Convert milliseconds to clock ticks.
			 */
			msec += (1000 / HZ) - 1;
			msec /= (1000 / HZ);
			timeout( &cprocp->p_polltim, msec,
				 wakeup, &cprocp->p_polls );
		}

		/*
		 * Wake for polled event, poll timeout, or signal.
		 *
		 * CVTTOUT is not below CVNOSIG, so this sleep RETURNS when a
		 * signal arrives (sendsig() sets the sleeper running whatever
		 * its value) instead of taking proc.c's envrest() out of the
		 * system call: the unlink below therefore runs on the signal
		 * path too, and the EINTR is reported explicitly further down.
		 * A sleep value below CVNOSIG here would long-jump to the trap
		 * entry (z8001/src/trap.c) with the buffers still linked, so
		 * anything that changes it has to move the unlink with it.
		 */
		sleep( &cprocp->p_polls, CVTTOUT, IVTTOUT, SVTTOUT );

		/*
		 * Terminate event monitoring.
		 */
		pollexit();

		/*
		 * Signal woke us up.
		 */
		if ( nondsig() ) {
			u.u_error = EINTR;
			return -1;
		}

		/*
		 * We were woken up by timeout wakeup.
		 */
		if ( (msec > 0) && (cprocp->p_polltim.t_lbolt <= lbolt) )
			return 0;
	}
}
