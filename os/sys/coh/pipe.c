/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* $Header: /kernel/kersrc/coh.386/RCS/pipe.c,v 1.2 92/08/04 12:33:59 bin Exp Locker: bin $ */
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
 * Pipes.
 *
 * $Log:	pipe.c,v $
 * Revision 1.2  92/08/04  12:33:59  bin
 * changed for ker 59
 * 
 * Revision 1.2  92/01/06  11:59:52  hal
 * Compile with cc.mwc.
 * 
 * Revision 1.1	88/03/24  16:14:07	src
 * Initial revision
 * 
 * 86/11/19	Allan Cornish		/usr/src/sys/coh/pipe.c
 * Added check for non-blocking read and write if (io_flag & IPNDLY) set.
 * Eliminated use of i_a inode field since now included in inode macros.
 */
#include <sys/coherent.h>
#include <errno.h>
#include <sys/filsys.h>
#include <sys/ino.h>
#include <sys/inode.h>
#include <sys/io.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <signal.h>

/*
 *  These are nothing more than random different values at this point!
 *  Historically, these were bit's or'ed into ip->i_flag, no more!
 */

#undef	IFWFR
#undef	IFWFW
#define	IFWFR	1			/* Sleeping Waiting for a Reader */
#define	IFWFW	2			/* Sleeping Waiting for a Writer */


/*
 *  pmake(mode)  --  called from the upipe() system call in sys3.c
 *
 *  Creates and returns a locked pipe inode with the given mode on
 *  the pipedev.
 */

INODE *
pmake(mode)
{
	register INODE *ip;

	if ((ip=ialloc(pipedev, IFPIPE|mode)) != NULL)
		pipeclear(ip);
	return(ip);
}

/*
 *  pipeclear(ip)  --  Reset a pipe inode's transfer state (byte count,
 *  ring indices, attach/sleep counts, poll event chains).  k73 calls
 *  this pclear; renamed here because pclear(paddr, n) is the Z8001
 *  machine-dependent physical-memory clear (sys/z8001 pcopy.c).
 */
pipeclear(ip)
register INODE *ip;
{
	ip->i_pnc =
	ip->i_prx =
	ip->i_pwx =
	ip->i_par =
	ip->i_paw =
	ip->i_psr =
	ip->i_psw = 0;
	ip->i_iev.e_pnext =
	ip->i_iev.e_dnext =
	ip->i_iev.e_dlast =
	ip->i_iev.e_procp =
	ip->i_oev.e_pnext =
	ip->i_oev.e_dnext =
	ip->i_oev.e_dlast =
	ip->i_oev.e_procp = NULL;
}


/*
 *  pipecoreinit(ip)  --  Initialise ONLY the pipe state that has no disk
 *  image: the attach/sleep counts and the two poll event chains.
 *
 *  pipeclear() covers this for an anonymous pipe, but it is reached only from
 *  pmake() -- the pipe(2) path.  A NAMED fifo enters core through icopydm()
 *  instead, whose IFPIPE case restores the four fields that DO have a disk
 *  image and left these untouched -- while iattach() recycles in-core inode
 *  slots without clearing them, so the fifo inherited whatever the slot's
 *  previous occupant left behind.  (Creation via mknod is already safe: a free
 *  inode has di_mode 0 and takes icopydm's default kclear of the whole union.
 *  It is the RELOAD that bites -- /dev/inet is made by rc, its slot is recycled
 *  by every file opened between then and the daemon starting, and the reload
 *  restores only the disk fields.)
 *
 *  Harmless while nothing polled a pipe -- 3.2 answered POLLNVAL for them --
 *  but fatal the moment something does: ppoll() enqueues the waiter on a
 *  garbage chain while pwake()'s pollwake() walks a different one, so the
 *  writer's wakeup never reaches the reader and both sides hang forever.
 *  Deliberately does NOT touch i_pnc/i_prx/i_pwx or the block list: those DO
 *  have a disk image and the caller has just loaded them.
 */
pipecoreinit(ip)
register INODE *ip;
{
	ip->i_par =
	ip->i_paw =
	ip->i_psr =
	ip->i_psw = 0;
	ip->i_iev.e_pnext =
	ip->i_iev.e_dnext =
	ip->i_iev.e_dlast =
	ip->i_iev.e_procp =
	ip->i_oev.e_pnext =
	ip->i_oev.e_dnext =
	ip->i_oev.e_dlast =
	ip->i_oev.e_procp = NULL;
}


/*
 *  popen(ip, mode)  --  Opens a pipe inode, with the given mode.
 *			 Note:  The inode is locked upon entry.
 *
 *  This routine follows the requirements concerning opening pipes.
 *  Specifically, if opening readonly without O_NDELAY, then block
 *  until we have a writer.  If opening readonly with O_NDELAY, then
 *  return opened, no blocking.  If opening writeonly without O_NDELAY,
 *  then block until we have a reader.  If opening writeonly with
 *  O_NDELAY, then return an error, and set u.u_errno to ENXIO.
 *  Beware of subtle race conditions!  Also notice, I followed hal's
 *  style of no internal returns in a function.
 *
 *  Note: these pipe routines maintain the pipe counter variables:
 *	  ip->i_par:  Number of Awake readers
 *	  ip->i_paw:  Number of Awake writers
 *	  ip->i_psr:  Number of Sleeping readers
 *	  ip->i_psw:  Number of Sleeping writers
 */

popen(ip, mode)
register INODE *ip;
{
	switch ( mode&(IPR|IPW) ) {
	case IPR:
		++ip->i_par;
		while ( !ip->i_paw && !ip->i_psw ) {
			if ( mode & IPNDLY )
				break;
			else {
				if ( psleep(ip, IFWFW) < 0 ) {
					--ip->i_par;
					goto popen_done;
				}
				if ( ip->i_pnc != 0 )
					break;
			}
		}
		pwake(ip, IFWFR);
		break;
	case IPW:
		++ip->i_paw;
		if ( !ip->i_par && !ip->i_psr ) {
			if ( mode & IPNDLY ) {
				u.u_error = ENXIO;
				--ip->i_paw;
				goto popen_done;
			} else {
				if ( psleep(ip, IFWFR) < 0 ) {
					--ip->i_paw;
					goto popen_done;
				}
			}
		}
		pwake(ip, IFWFW);
		break;
	case IPR|IPW:
		++ip->i_par;
		++ip->i_paw;
		pwake(ip, IFWFW);
		pwake(ip, IFWFR);
		break;
	}

popen_done:
	return;
}


/*
 *  pclose(ip, mode)  --  Opens a pipe inode, with the given mode.
 *			  Note:  The inode is locked upon entry.
 *
 *  This routine closes the given INODE with the given mode.  We
 *  must have the mode correct to maintain counters properly.
 *  Good thing that mode cannot be changed by fcntl()!
 */

pclose(ip, mode)
register INODE *ip;
{
	pwake(ip, IFWFR);
	pwake(ip, IFWFW);
	if ( mode & IPR )
		if ( --ip->i_par < 0 )
			panic("Out of sync IPR in pclose");
	if ( mode & IPW )
		if ( --ip->i_paw < 0 )
			panic("Out of sync IPW in pclose");

	/*
	 * The last writer, or the last reader, has just gone: wake anyone
	 * already asleep in poll(2) so it can see the hangup.
	 *
	 * The pwake() calls above cannot do this.  pwake(IFWFW) only reaches
	 * pollwake(&i_iev) when i_pnc > 0, and a pipe at end of file is by
	 * definition empty; pwake(IFWFR) likewise needs a reader to still
	 * exist.  Both also run BEFORE the counts are decremented, so they
	 * could not have told that this close was the last one.  Without this,
	 * ppoll()'s hangup report is only ever seen by a poll that ARRIVES
	 * after the close -- a poll already blocked when a child exits sleeps
	 * until its timeout, which is exactly the hang this is here to stop.
	 */
	if ( !ip->i_paw && !ip->i_psw )
		pollwake(&ip->i_iev);
	if ( !ip->i_par && !ip->i_psr )
		pollwake(&ip->i_oev);

	if ( !ip->i_paw && !ip->i_psw && !ip->i_par && !ip->i_psr )
		pipeclear(ip);
}


/*
 * First no-progress state the guard in pread() saw, recorded once.
 *
 * Recorded rather than printed: pread() runs on every pipe read and the inet
 * daemon's whole IPC is pipes, so a printf here is both a flood risk and text
 * this kernel does not have -- the 0x30 code segment has under 100 bytes spare,
 * while BSS has tens of kilobytes free.  Read it out of kernel memory the way
 * hostbuild/serial-rx-probe.py reads al.c's counters: the symbol is
 * `pread_nopro_', in segment 0x31.
 *
 * pn_hit stays 0 until the guard fires, so zero here means the condition has not
 * occurred -- not that the diagnostic is missing.
 *
 * Compiled out unless -DPIPEDIAG (EXTRA_KDEFS in link-kernel.sh).  Even as bare
 * stores it does not fit today: text sits at 65462 of 65536 and the seven stores
 * need more than the 74 bytes left.  Turning it on means freeing text first --
 * which is what WS2's two-segment text work is for.
 */
struct pread_nopro {
	int	pn_hit;			/* set once, then never overwritten */
	int	pn_ino;
	int	pn_prx;
	int	pn_pwx;
	int	pn_pnc;
	unsigned pn_size;
	unsigned pn_seek;
}
#ifdef	PIPEDIAG
	pread_nopro
#endif
	;

/*
 *  pread(ip, iop)  --  Reads from a pipe inode, accoring to the IO info.
 *			Note:  The inode is locked upon entry.
 *
 *  This routine follows the requirements concerning reading from pipes.
 *  Specifically, if there is no data in the pipe, then the read will
 *  block waiting for data, unless you have IONDLY set in which case
 *  it will simply return zero.  Notice, the traditional value returned
 *  from uread() is the number of characters actually read.  This is
 *  nothing more that iop->io_ioc on entry minus iop->io_ioc on exit.
 *  This routine also works with the ring buffer in the inode maintained
 *  by the variables ip->i_pnc:  Number of Characters in pipe.
 *		     ip->i_prx:  Offset in pipe to begin reading.
 *		     ip->i_pwx:  Offset in pipe to begin writing.
 *  Notice: we do not unlock the inode when we call fread(), this is to
 *  guarantee that we read all that is available even if we go to sleep.
 *  Subtle race condition?  I don't think so, since if we go to sleep
 *  in fread(), it's wrt a resource unrelated to this particular INODE.
 */

pread(ip, iop)
register INODE *ip;
register IO *iop;
{
	register unsigned n;
	register unsigned ioc;

	while (ip->i_pnc == 0) {
		if ( iop->io_flag & IONDLY )
			goto pread_done;
		if ( !ip->i_paw && !ip->i_psw )
			goto pread_done;
		if ( psleep(ip, IFWFW) < 0 )
			goto pread_done;
	}

	ioc = iop->io_ioc;
	while ( !u.u_error && (ioc > 0) && (ip->i_pnc > 0) ) {
		if ( (n = (PIPSIZE-ip->i_prx)) > ioc )
			n = ioc;
		if ( n > ip->i_pnc )
			n = ip->i_pnc;
		iop->io_ioc = n;
		iop->io_seek = ip->i_prx;
		fread(ip, iop);
		n -= iop->io_ioc;	/* bytes actually transferred */
		/*
		 * No progress: give up rather than spin.  fread() clamps the
		 * transfer to ip->i_size, but a pipe's live byte count is
		 * i_pnc, not i_size -- if i_prx == i_size with i_pnc still
		 * positive, fread moves nothing while this loop's state
		 * (i_prx, i_pnc, ioc) stays put, an unkillable kernel spin.
		 * A short read keeps the machine alive.
		 */
		if ( n == 0 ) {
			/*
			 * Record the inconsistency once per boot.
			 */
#ifdef	PIPEDIAG
			if ( pread_nopro.pn_hit == 0 ) {
				pread_nopro.pn_hit  = 1;
				pread_nopro.pn_ino  = ip->i_ino;
				pread_nopro.pn_prx  = ip->i_prx;
				pread_nopro.pn_pwx  = ip->i_pwx;
				pread_nopro.pn_pnc  = ip->i_pnc;
				pread_nopro.pn_size = ip->i_size;
				pread_nopro.pn_seek = iop->io_seek;
			}
#endif
			break;
		}
		if ( (ip->i_prx+=n) == PIPSIZE )
			ip->i_prx = 0;
		if ( (ip->i_pnc-=n) == 0 ) {
			ip->i_prx =
			ip->i_pwx = 0;
		}
		ioc -= n;
	}
	iop->io_ioc = ioc;

	if ( ip->i_pnc < PIPSIZE )
		pwake(ip, IFWFR);

pread_done:
	return;
}


/*
 *  pwrite(ip, iop)  --  Writes to a pipe inode, accoring to the IO info.
 *			 Note:  The inode is locked upon entry.
 *
 *  This routine follows the requirements concerning writing to pipes.
 *  Specifically, if the pipe is full, then the write will block waiting
 *  for data to be consumed, unless you have IONDLY set in which case
 *  it will simply return zero.  Notice, the traditional value returned
 *  from uwrite() is the number of characters actually written.  This is
 *  nothing more that iop->io_ioc on entry minus iop->io_ioc on exit.
 *  In other words, iop->io_ioc had better be zero on exit.  The possibility
 *  does exist if the number of characters to be written is larger than
 *  PIPSIZE, and thus we do not guarantee atomic writes, that while the
 *  process is sleeping waiting for a reader to consume data, that the
 *  process will be woken from sleeping by a SIGNAL, thus causing a partial
 *  write.  The return value will be the actual number of character written.
 *  This routine also works with the ring buffer in the inode maintained
 *  by the variables ip->i_pnc:  Number of Characters in pipe.
 *		     ip->i_prx:  Offset in pipe to begin reading.
 *		     ip->i_pwx:  Offset in pipe to begin writing.
 *  Notice: we do not unlock the inode when we call fwrite(), this is to
 *  guarantee that we have an atomic write for all writes of size less
 *  than PIPSIZE, even if we go to sleep in the fwrite().  Subtle race
 *  condition?  I don't think so, since if we go to sleep in fwrite(),
 *  it's wrt a resource unrelated to this particular INODE.
 */

pwrite(ip, iop)
register INODE *ip;
register IO *iop;
{
	register unsigned n;
	register unsigned ioc;

	ioc = iop->io_ioc;
	while ( !u.u_error && (ioc > 0) ) {
		if ( !ip->i_par && !ip->i_psr ) {
			u.u_error = EPIPE;
			sendsig(SIGPIPE, SELF);
			goto pwrite_done;
		}
		if ( (n = (PIPSIZE-ip->i_pwx)) > ioc )
			n = ioc;
		if ( n > (PIPSIZE-ip->i_pnc) )
			n = PIPSIZE - ip->i_pnc;
		if ( (n == 0) || ((ioc <= PIPSIZE) && (n != ioc)) ) {
			if ( iop->io_flag & IONDLY )
				goto pwrite_done;
			if ( psleep(ip, IFWFR) < 0 )
				goto pwrite_done;
			continue;
		}
		iop->io_ioc = n;
		iop->io_seek = ip->i_pwx;
		fwrite(ip, iop);
		n -= iop->io_ioc;
		if ( (ip->i_pwx+=n) == PIPSIZE )
			ip->i_pwx = 0;
		ip->i_pnc += n;
		ioc -= n;

		if ( ip->i_pnc > 0 )
			pwake(ip, IFWFW);
	}
pwrite_done:
	iop->io_ioc = ioc;
}


/*
 *  psleep(ip, who)  --  go to sleep either waiting for a reader if (who==IFWFR)
 *		         or waiting for a writer if (who==IFWFW).
 *  Returns:  0  if woke up ok
 *	     -1  if woke up by signal (e.g. SIGALRM, SIGKILL, etc.)
 *
 *  The four lines that move this process between the awake and the asleep
 *  count are a matched pair around the sleep, so the sleep must RETURN --
 *  hence v_isleep and not v_sleep.  sleep() answers a caught signal by
 *  unwinding to the system call entry (proc.c, envrest(&u.u_sigenv)), which
 *  runs no more of this function: the restoring ++/-- would never happen, and
 *  the -1 return below -- written for exactly this case -- was unreachable.
 *  The fifo was then permanently one reader short in i_par (or one writer in
 *  i_paw), and the next close of it took the machine down with `Out of sync
 *  IPR in pclose'.  The corruption is at signal delivery, so it could be
 *  charged to any later program that happened to close the fifo.
 *
 *  With v_isleep every one of the three outcomes restores the counts:
 *  an ordinary wakeup returns 0; a caught signal -- pending on entry, taken
 *  during the sleep, or arriving just as the wakeup does -- returns 1 through
 *  the same line.  The callers' `psleep(...) < 0' arms come alive with it, so
 *  popen() now undoes its own ++i_par/++i_paw and open(2) releases the file
 *  descriptor and the inode it had already taken.
 */

psleep(ip, who)
register INODE *ip;
{
	register int sig;

	if ( (who!=IFWFW) && (who!=IFWFR) )
		panic("psleep() internal error");
	iunlock(ip);
	sig = 0;
	switch ( who ) {
	case IFWFW:
		--ip->i_par;  ++ip->i_psr;
		sig = v_isleep((char *)&ip->i_psw, CVPIPE, IVPIPE, SVPIPE,
			"pipe wx");
		++ip->i_par;  --ip->i_psr;
		break;
	case IFWFR:
		--ip->i_paw;  ++ip->i_psw;
		sig = v_isleep((char *)&ip->i_psr, CVPIPE, IVPIPE, SVPIPE,
			"pipe rx");
		++ip->i_paw;  --ip->i_psw;
		break;
	}
	ilock(ip);
	if ( sig || (SELF->p_ssig && nondsig()) ) {
		u.u_error = EINTR;
		return(-1);
	}
	return(0);
}


/*
 *  pwake(ip, who)  --  wake up processes which are waiting for a reader if
 *		        (who==IFWFR) or waiting for a writer if (who==IFWFW).
 */

pwake(ip, who)
register INODE *ip;
{
	switch ( who ) {
	case IFWFW:
		if ( ip->i_psr )
			wakeup((char *)&ip->i_psw);
		if ( ip->i_pnc > 0 )
			pollwake(&ip->i_iev);
		break;
	case IFWFR:
		if ( ip->i_psw )
			wakeup((char *)&ip->i_psr);
		if ( (ip->i_pnc<PIPSIZE) && (ip->i_par || ip->i_psr) )
			pollwake(&ip->i_oev);
		break;
	}
}


/*
 *  ppoll(ip, ev)  --  Poll the given pipe inode.
 *  INODE *ip  --  The inode in question.
 *  int ev     --  The event bit field.
 *  int msec   --  Number of msecs to wait.
 *  Returns or'ed bits according to the following rules:
 *  POLLIN:  indicates input is available for reading, notice it is possible
 *	     to read even if there are no more writers anywhere!
 *  POLLOUT: indicates room in pipe for new output, notice it is not possible
 *	     to write unless there is a reader attached!
 *
 *  No priority polls are supported.
 */

ppoll(ip, ev, msec)
register INODE *ip;
int ev, msec;
{
	register int rval = 0;

	/*
	 * A pipe with no writer left anywhere is at end of file, and one with
	 * no reader left can never be drained.  Neither is an event a caller
	 * has to ask for -- POSIX returns POLLHUP and POLLERR in revents
	 * whatever `events' held -- and, more to the point, neither can ever
	 * change back: a pipe gains no new writer once its last one is gone,
	 * because there is no name to open.  So they must be reported INSTEAD
	 * of enqueuing, not as well as: a poller that sleeps here is waiting
	 * for a wakeup nobody is left to send.
	 *
	 * A fifo held open O_RDWR -- the idiom every daemon in this tree uses
	 * for its rendezvous -- counts itself in both, so it never sees either.
	 */
	if ( !ip->i_paw && !ip->i_psw )
		rval |= POLLHUP;
	if ( !ip->i_par && !ip->i_psr )
		rval |= POLLERR;

	if ( ev & POLLIN ) {
		if ( ip->i_pnc > 0 )
			rval |= POLLIN;
		else if ( (msec != 0) && !(rval & POLLHUP) )
			pollopen(&ip->i_iev);
	}
	if ( ev & POLLOUT ) {
		if ( (ip->i_pnc<PIPSIZE) && (ip->i_par || ip->i_psr) )
			rval |= POLLOUT;
		else if ( (msec != 0) && !(rval & POLLERR) )
			pollopen(&ip->i_oev);
	}
	return( rval );
}

#if 0
/*
 *  pdump(loc, ip, mode)  --  A kernel debugging output line.
 *  char *loc  --  prefix of line (two characters indicating where we are)
 *  INODE *ip  --  The inode information to dump
 *  int mode   --  The mode of the IO call, i.e. IPW, IPR, IPNDLY, ...
 */

pdump(loc, ip, mode)
char *loc;
register INODE *ip;
int mode;
{
	printf("%s ip=%x mde=%x nlk=%x rf=%x nc=%x rx=%x wx=%x",
		loc, ip, mode, ip->i_nlink, ip->i_refc,
		ip->i_pnc, ip->i_prx, ip->i_pwx);

	printf(" ar=%x aw=%x sr=%x sw=%x f=%x\n",
		ip->i_par, ip->i_paw, ip->i_psr, ip->i_psw, ip->i_flag);
}
#endif
