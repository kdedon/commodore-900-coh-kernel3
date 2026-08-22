/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
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
 * Process handling and scheduling.
 *
 * $Log:	proc.c,v $
 * Revision 1.2  92/08/04  12:34:18  bin
 * changed for ker 59
 * 
 * Revision 1.5  92/04/03  14:35:36  hal
 * Kernel #49: piggy trace variable.
 * 
 * Revision 1.4  92/02/03  17:00:09  piggy
 * Send SIGHUP to all group members on death of group leader.
 */
#include <sys/coherent.h>
#include <acct.h>
#include <errno.h>
#include <sys/inode.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/sched.h>
#include <sys/seg.h>
#include <signal.h>
#include <sys/stat.h>

/*
 * Initialization.
 * Set up the hash table queues.
 */
pcsinit()
{
	register PROC *pp;
	register PLINK *lp;

	/*
	 * Explicitly initialize everything in the first process.
	 */
	pp = &procq;
	SELF = pp;
	procq.p_nforw = pp;
	procq.p_nback = pp;
	procq.p_lforw = pp;
	procq.p_lback = pp;

#ifdef _I386
	/* Segments are initialized in mchinit() and eveinit().  */

	procq.p_uid = 0;		/* Effective uid */
	procq.p_ruid = 0;		/* Real uid */
	procq.p_rgid = 0;		/* Real gid */
	procq.p_state = 0;		/* Scheduling state */
	procq.p_flags = 0;		/* Flags */
	procq.p_ssig = (sig_t) 0;	/* Signals which have been set */
#ifdef _I386
	procq.p_dfsig = (sig_t) 0xffffffff;  /* All signals are defaulted.  */
	procq.p_hsig = (sig_t) 0;	/* Signals which are being held */
	procq.p_dsig = (sig_t) 0;	/* Signals which are being deferred */
#endif
	procq.p_isig = (sig_t) 0;	/* Signals which are being ignored */
	procq.p_event = NULL;		/* Wakeup event channel */
	procq.p_alarm = 0;		/* Timer for alarms */
	procq.p_group = 0;		/* Process group */
/*
 * Set ttdev to null so that we do not accidentally set a tty for init.
 */
	procq.p_ttdev = makedev(0,0);	/* Controlling terminal */
	procq.p_nice = 0;		/* Nice value */
	procq.p_cval = 0;		/* Cpu schedule value */
	procq.p_sval = 0;		/* Swap schedule value */
	procq.p_ival = 0;		/* Importance value */
	procq.p_rval = 0;		/* Response value */
	procq.p_lctim = 0;		/* Last time cval was updated */
	procq.p_utime = 0L;		/* User time (HZ) */
	procq.p_stime = 0L;		/* System time */
	procq.p_cutime = 0L;		/* Sum of childs user time */
	procq.p_cstime = 0L;		/* Sum of childs system time */
	procq.p_exit = 0;		/* Exit status */
	procq.p_polls = NULL;		/* Enabled polls */
	/* Poll  timer */
	procq.p_polltim.t_next = NULL;
	procq.p_polltim.t_last = NULL;
	procq.p_polltim.t_lbolt = 0L;
	procq.p_polltim.t_func = NULL;
	procq.p_polltim.t_farg = NULL;
#ifndef _I386
	procq.p_polltim.t_ldrv = 0;
#endif /* _I386 */

	/* Alarm timer */
	procq.p_alrmtim.t_next = NULL;
	procq.p_alrmtim.t_last = NULL;
	procq.p_alrmtim.t_lbolt = 0L;
	procq.p_alrmtim.t_func = NULL;
	procq.p_alrmtim.t_farg = NULL;
#ifndef _I386
	procq.p_alrmtim.t_ldrv = 0;
#endif /* _I386 */

	procq.p_prl = NULL;		/* Pending record lock */
#endif /* _I386 */

	for (lp=&linkq[0]; lp<&linkq[NHPLINK]; lp++) {
		lp->p_lforw = lp;
		lp->p_lback = lp;
	}
}

/*
 * Initiate a process.
 */
PROC *
#ifdef _I386
process()
#else
process(f)
int (*f)();
#endif
{
	register PROC *pp1;
	register PROC *pp;
	register SEG *sp;
	MCON mcon;

	if ((pp=kalloc(sizeof(PROC))) == NULL)
		return (NULL);

	/*
	 * Start from zero.  kalloc() hands back whatever was last in that
	 * memory, and only three fields were set below, so every OTHER field of
	 * a brand-new process began as garbage -- including the two that the
	 * kernel dereferences without ever being told they are valid:
	 *
	 *	p_polls    pollexit() walks this list on the first poll() the
	 *		   process makes, unlinking each entry from a device
	 *		   queue and pushing it onto the free list.  Garbage here
	 *		   corrupts both.
	 *	p_polltim  timeout() opens with `if (qp = tp->t_last) *qp = ...',
	 *		   which STORES through the pointer it finds.
	 *
	 * Harmless whenever the recycled memory happened to be zero, which is
	 * most of the time on a freshly booted machine -- and that is exactly
	 * what makes it worth clearing rather than reasoning about.
	 */
	kclear(pp, sizeof(PROC));

	pp->p_flags = PFCORE;
	pp->p_state = PSRUN;
	pp->p_ttdev = NODEV;
#ifndef _I386
	/*
	 * What is this, and why is it 286 only?
	 */
	if (f != NULL) {
		pp->p_flags |= PFKERN;
		sp = salloc((fsize_t)UPASIZE, SFSYST|SFHIGH|SFNSWP);
		if (sp == NULL) {
			kfree(pp);
			return (NULL);
		}
		pp->p_segp[SIUSERP] = sp;
		msetsys(&mcon, f, FP_SEL(sp->s_faddr));
#ifdef	Z8001
		/* s_faddr is not a pointer here (see mdstub.c): write the
		 * context into the u-area segment through a transient
		 * window, as 0.7.3's kscopy() did */
		psegcopy((char *)&mcon, sp, offset(uproc, u_syscon),
			sizeof(mcon));
#else
		kfcopy(	(char *)&mcon,
			sp->s_faddr + offset(uproc, u_syscon),
			sizeof(mcon));
#endif
	}
#endif
	lock(pnxgate);
next:

	/*
	 * Pick the next process id.
	 */
	/* DO NOT MESS WITH THE FOLLOWING CONDITIONAL UNLESS YOU
	   TEST THE RESULTS ON BOTH 286 AND 386 */
#ifdef _I386
	if (++cpid >= NPID)
		cpid = 2;
	pp->p_pid = cpid;
#else
	pp->p_pid = cpid++;
	if (cpid >= NPID)
		cpid = 2;
#endif

	/*
	 * Make sure that process id is not in use.
	 */
	pp1 = &procq;
	while ((pp1=pp1->p_nforw) != &procq) {
		if (pp1->p_pid < pp->p_pid)
			break;
		if (pp1->p_pid == pp->p_pid)
			goto next;
	}

	/*
	 * We've got a valid pid, so let's put this process into
	 * the process table.
	 */
	pp->p_nback = pp1->p_nback;
	pp1->p_nback->p_nforw = pp;
	pp->p_nforw = pp1;
	pp1->p_nback = pp;
	unlock(pnxgate);
	return (pp);
}

#ifndef _I386
/*
 * Name a kernel process.
 *
 * u_comm is the kernel's own record of what a process is running, and the
 * only writer of it is pexece(): a process that never execs never has one.
 * Every kernel process spawned by process() is in that position -- it enters
 * at a function pointer -- so the field stays as salloc() left the u-area,
 * and ps(1), which reads u_comm to name a kernel process, has nothing to
 * print.  Called by the creator, which is the only thing that knows the name;
 * the i8086 loadable-driver path names its process the same way.
 *
 * `pp' must already own its user area (process() allocated it) and must not
 * be running yet: the copy goes through a transient window, not through `u'.
 */
pkname(pp, name)
register PROC *pp;
register char *name;
{
	char comm[sizeof (u.u_comm)];
	register int i;

	for (i = 0; i < sizeof (comm); i++)
		comm[i] = (*name != '\0') ? *name++ : '\0';
#ifdef	Z8001
	psegcopy(comm, pp->p_segp[SIUSERP], offset(uproc, u_comm[0]),
		sizeof (comm));
#else
	kfcopy(comm,
		pp->p_segp[SIUSERP]->s_faddr + offset(uproc, u_comm[0]),
		sizeof (comm));
#endif
}
#endif /* _I386 */

/*
 * Remove a process from the next queue and release and space.
 */
relproc(pp)
register PROC *pp;
{
	register SEG * sp;

	/*
	 * Child process still has a user-area.
	 */
	if ((sp = pp->p_segp[SIUSERP]) != NULL) {

		/*
		 * Detach user-area from child process.
		 */
		pp->p_segp[SIUSERP] = NULL;

		/*
		 * Child process is swapped out.
		 */
		if (pp->p_flags & PFSWAP)
			sp->s_lrefc++;

		/*
		 * Release child's user-area.
		 */
		sfree(sp);
	}

	/*
	 * Remove process from doubly-linked list of all processes.
	 * Release space allocated for proc structure.
	 */
	lock(pnxgate);
	pp->p_nback->p_nforw = pp->p_nforw;
	pp->p_nforw->p_nback = pp->p_nback;
	unlock(pnxgate);
	kfree(pp);
}

/*
 * Create a clone of ourselves.
 *	N.B. - consave(&mcon) returns twice; anything not initialized
 *	in automatic storage before the call to segadup() will not be
 *	initialized when the second return from consave() commences.
 */
pfork()
{
	register PROC *cpp;
	register PROC *pp;
	register int s;
	MCON mcon;

#ifdef _I386
	if ((cpp=process()) == NULL) {
#else
	if ((cpp=process(NULL)) == NULL) {
#endif
		SET_U_ERROR( EAGAIN, "no more process table entries" );
		return -1;
	}

#ifndef _I386
	s = sphi();
	usave();	/* Put the current copy of uarea into its segment */
	spl(s);
#endif

	pp = SELF;
	s = sphi();	/* put current interrupt level into s before segadup */
	spl(s);
	/*
	 * As stated above, no auto variable may be changed between calls
	 * to segadup() and consave().
	 */
	if (segadup(cpp) == 0) {
		SET_U_ERROR( EAGAIN, "can not duplicate segments" );
		relproc(cpp);
		return -1;
	}
	if (u.u_rdir != NULL)
		u.u_rdir->i_refc++;
	if (u.u_cdir != NULL)
		u.u_cdir->i_refc++;
	fdadupl();
	cpp->p_uid   = pp->p_uid;
	cpp->p_ruid  = pp->p_ruid;
	cpp->p_rgid  = pp->p_rgid;
	cpp->p_ppid  = pp->p_pid;
	cpp->p_ttdev = pp->p_ttdev;
	cpp->p_group = pp->p_group;
	cpp->p_ssig  = pp->p_ssig;
#ifdef _I386
	cpp->p_dfsig  = pp->p_dfsig;
	cpp->p_dsig  = pp->p_dsig;
	cpp->p_hsig  = pp->p_hsig;
#endif /* _I386 */
	cpp->p_isig  = pp->p_isig;
	cpp->p_cval  = CVCHILD;
	cpp->p_ival  = IVCHILD;
	cpp->p_sval  = SVCHILD;
	cpp->p_rval  = RVCHILD;
#ifdef _I386
	cpp->p_prl = NULL;
#endif

	sphi();		/* s = sphi() was done before segadup() */
	consave(&mcon);
	spl(s);

	/*
	 * Parent process.
	 */
	if ((pp = SELF) != cpp) {
		segfinm(cpp->p_segp[SIUSERP]);
#ifdef _I386
		dmaout(sizeof(mcon), 
		   MAPIO(cpp->p_segp[SIUSERP]->s_vmem,offset(uproc,u_syscon)),
			(char *)&mcon);
#else
#ifdef	Z8001
		psegcopy((char *)&mcon, cpp->p_segp[SIUSERP],
			offset(uproc, u_syscon), sizeof(mcon));
#else
		kfcopy((char *)&mcon,
			cpp->p_segp[SIUSERP]->s_faddr + offset(uproc,u_syscon),
			sizeof(mcon));
#endif
#endif
		mfixcon(cpp);
		s = sphi();
		setrun(cpp);
		spl(s);
#ifdef _I386
		u.u_rval2 = 0;
#endif
		return(cpp->p_pid);
	}

	/*
	 * Child process.
	 */
	else {
		u.u_btime = timer.t_time;
		u.u_flag = AFORK;
#ifdef _I386
#ifdef UPROC_VERSION
		u.u_version = UPROC_VERSION;
#endif /* UPROC_VERSION */
		u.u_sleep[0] = '\0'; /* We are not sleeping to start with.  */
		sproto(0);
#else
		sproto();
#endif
		segload();
#ifdef _I386
		u.u_rval2 = SELF->p_ppid;
#endif
		return 0;
	}
}

/*
 * Die.
 */
pexit(s)
{
	register PROC *pp1;
	register PROC *pp;
	register SEG  *sp;
	register int n;
	PROC *parent;

	pp = SELF;

	T_PIGGY( 0x1, printf("%s:pexit(%x)", u.u_comm, s); );
	/*
	 * Cancel the alarm, poll and VTIME read timers [if any].
	 */
	/* The nulls are cast: timeout()'s f is a function
	 * pointer and a is a char *, both 4 bytes, while a bare 0/NULL here is a
	 * 2-byte int and this call has no prototype.  It happened to work only
	 * because the two short arguments together read back as a zero f, and
	 * timeout() returns before touching a. */
	timeout(&pp->p_alrmtim, 0, (int (*)())0, (char *)0);
	timeout(&pp->p_polltim, 0, (int (*)())0, (char *)0);
	timeout(&pp->p_vtime, 0, (int (*)())0, (char *)0);

	/*
	 * Give up any poll event buffers still linked onto device queues.
	 *
	 * A buffer is on two lists: p_polls, which dies with the PROC, and the
	 * device's circular queue, which does not.  Anything left on a device
	 * queue is walked by pollwake() from an interrupt handler and read
	 * through e_procp, so it must be unlinked while the PROC is still
	 * there.  It must also be unlinked while the queue HEAD is still
	 * there: a pipe or FIFO head is a field of an inode that fdaclose()
	 * below can release.
	 *
	 * pollexit() works on cprocp, so this is the only exit path that can
	 * do it -- sendsig() runs in the sender's context, not the target's.
	 * poll(2) unlinks on every path out of upoll(), including the one that
	 * reports EINTR, so p_polls is normally empty here and this is the
	 * cheapest possible no-op.
	 */
	pollexit();

	/*
	 * Write out accounting directory and close all files associated with
	 * this process.
	 */
	setacct();
	if (u.u_rdir)
		ldetach(u.u_rdir);
	if (u.u_cdir)
		ldetach(u.u_cdir);
	fdaclose();

	/*
	 * Free all segments in reverse order, except for user-area.
	 */
	for (n = NUSEG; --n > 0;) {
		if ((sp = pp->p_segp[n]) != NULL) {
			pp->p_segp[n] = NULL;
			sfree(sp);
		}
	}

	/*
	 * Wakeup our parent.  If we have any children, init will become the
	 * new parent.  If there are any children we are tracing who are
	 * waiting for us, we wake them up.
	 */
	pp1 = &procq;
	while ((pp1=pp1->p_nforw) != &procq) {
		if (pp1->p_pid == pp->p_ppid) {
			parent = pp1;	/* Remember our parent.  */
			if (pp1->p_state==PSSLEEP && pp1->p_event==(char *)pp1)
				wakeup((char *)pp1);
		}
		if (pp1->p_ppid == pp->p_pid) {
			pp1->p_ppid = 1;
			if (pp1->p_state == PSDEAD)
				wakeup((char *)eprocp);
			if ((pp1->p_flags&PFTRAC) != 0)
				wakeup((char *)&pts.pt_req);
		}
	}

	/*
	 * Wake up swapper if swap timer is active.
	 */
	if (stimer.t_last != 0)
		wakeup((char *) &stimer);

	/*
	 * Mark us as dead and give up the processor forever.
	 */
	pp->p_exit = s;
	pp->p_state = PSDEAD;

#ifdef _I386
	/*
	 * If this is a process group leader, inform all members of the group
	 * of the recent death with a HUP signal.
	 */
	if (pp->p_group == pp->p_pid) {
		ukill(-pp->p_pid, SIGHUP);
	}
#else
	uasa = 0;
#endif

#ifdef _I386
	T_PIGGY( 0x100, printf("<CHLD pid: %d ppid: %d>",
				pp->p_pid, parent->p_pid); );

	/*
	 * If the parent is ignoring SIGCHLD, 
	 * remove the zombie right away.
	 */
#	define CHLDBIT (((sig_t) 1) << (SIGCHLD - 1))
	if (CHLDBIT & parent->p_isig) {
		parent->p_cutime += pp->p_utime + pp->p_cutime;
		parent->p_cstime += pp->p_stime + pp->p_cstime;
		relproc(pp);
	} else {
		/*
		 * If SIGCHLD is not defaulted, notify our parent
		 * of our demise.
		 */
		if (!(CHLDBIT & parent->p_dfsig)) {
			sendsig(SIGCHLD, parent );
		}
	}
#endif /* _I386 */

	dispatch();
}

#ifdef _I386
/*
 * Sleep verbosely.  Put a short string describing our reason for sleeping
 * into our U area, and then go to sleep.
 */
v_sleep(e, cl, sl, sr, msg)
	char *e;
	int cl, sl, sr;
	char *msg;
{
	int i;

	/*
	 * The descriptive string may be at most 10 characters long.
	 * It will only be NUL terminated if it has 9 or fewer characters.
	 */
	for (i = 0; i < 10; ++i) {
		if ('\0' == (u.u_sleep[i] = msg[i])) {
			break;
		}
	}

	sleep(e, cl, sl, sr);
} /* v_sleep() */
#endif

/*
 * Sleep on the event `e'.  This gives up the processor until someone
 * wakes us up.  Since it is possible for many people to sleep on the
 * same event, the caller when awakened should make sure that what he
 * was waiting for has completed and if not, go to sleep again.  `cl'
 * is the cpu value we get to get the cpu as soon as we are woken up.
 * `sl' is the swap value we get to keep us in memory for the duration
 * of the sleep.  `sr' is the swap value that allows us to get swapped
 * in if we have been swapped out.
 */
sleep(e, cl, sl, sr)
char *e;
{
	xsleep(e, cl, sl, sr, 0);
}

/*
 * Sleep on the event `e' exactly as sleep() does, but answer a caught signal
 * by RETURNING 1 rather than by unwinding to the system call entry.  Returns
 * 0 if it slept and woke with nothing pending.
 *
 * A caller needs this when it holds state that only the code after its sleep
 * can put back -- the unwind runs none of that code, so anything the caller
 * adjusted around the sleep stays adjusted forever.  psleep() (pipe.c) is the
 * one such caller: it moves itself between the pipe's awake and asleep counts,
 * and a lost restore leaves a fifo permanently one reader or one writer short,
 * which pclose() answers with `Out of sync IPR in pclose'.
 *
 * Note this also covers the signal that is ALREADY pending on entry: the
 * caller is told at once and never sleeps, so it cannot block waiting for a
 * wakeup that the signal, having been delivered before the sleep, will not
 * send.
 */
isleep(e, cl, sl, sr)
char *e;
{
	return (xsleep(e, cl, sl, sr, 1));
}

/*
 * The body of both.  `nolj' selects what a caught signal does: 0 gives the
 * historic non-local exit through envrest(&u.u_sigenv) -- the setjmp half is
 * at the head of every system call, trap.c:106 -- and non-zero returns 1.
 * Returns 0 when it slept and woke with no signal pending.
 *
 * The pending-signal test now sits INSIDE the same interrupt-disabled region
 * as the enqueue.  It was ahead of it, which left a window: a signal arriving
 * between the test and the enqueue set p_ssig with the process still PSRUN, so
 * sendsig() had no sleeper to run and this sleep waited for a wakeup that had
 * already been and gone.
 */
xsleep(e, cl, sl, sr, nolj)
char *e;
{
	register PROC *bp;
	register PROC *fp;
	register PROC *pp;
	register int s;

	pp = SELF;

	/*
	 * Get ready to go to sleep and do so.
	 */
	s = sphi();

	/*
	 * See if we have a signal awaiting.
	 */
	if (cl<CVNOSIG && pp->p_ssig && nondsig()) {
		if (nolj) {
			spl(s);
			return (1);
		}
		envrest(&u.u_sigenv);
	}

	pp->p_state = PSSLEEP;
	pp->p_event = e;
	pp->p_lctim = utimer;
	addu(pp->p_cval, cl);
	pp->p_ival = sl;
	pp->p_rval = sr;
	fp = &linkq[hash(e)];
	bp = fp->p_lback;
	pp->p_lforw = fp;
	fp->p_lback = pp;
	pp->p_lback = bp;
	bp->p_lforw = pp;
	spl(s);
	dispatch();

	/*
	 * We have just woken up.  Get ready to return.
	 */
	subu(pp->p_cval, cl);
	pp->p_ival = 0;
	pp->p_rval = 0;

	/*
	 * Since we are no longer asleep, we no longer need 
	 * to publish a reason for sleeping.
	 */
#ifdef _I386
	u.u_sleep[0] = '\0';
#endif

	/*
	 * Check for an interrupted system call.
	 */
	if (cl<CVNOSIG && pp->p_ssig && nondsig()) {
		if (nolj)
			return (1);
		sphi();
		envrest(&u.u_sigenv);
	}
	return (0);
}

/*
 * Wake up all processes sleeping on the event `e'.
 *
 * This calls dwakeup() directly (it is self-protected with sphi/spl), as
 * the shipped 0.7.3 kernel does.  The i286 3.2 MI routed wakeup through
 * the defer() ring purely to avoid the interrupt-level spl round trip --
 * a performance choice, not a correctness one.  On this Z8001 the deferred
 * path overflows the 64-entry ring under the boot/disk wakeup load and
 * silently DROPS wakeups (dwakeup storm -> "Defer overflow"), stalling the
 * tty and disk sleepers.  Direct wake matches 0.7.3 and cannot drop.
 */
wakeup(e)
char *e;
{
	void dwakeup();

	dwakeup(e);
}

/*
 * Wake up all processes sleeping on the event `e'.
 */
void
dwakeup(e)
char *e;
{
	register PROC *pp;
	register PROC *pp1;
	register int s;

	/*
	 * Identify event queue to check.
	 * Disable interrupts.
	 */
	pp1 = &linkq[hash(e)];
	pp = pp1;
	s = sphi();

	/*
	 * Traverse doubly-linked circular event-queue.
	 */
	while ((pp = pp->p_lforw) != pp1) {

		/*
		 * Process is waiting on event 'e'.
		 */
		if (pp->p_event == e) {
			/*
			 * Remove process from event queue.
			 * Update process priority.
			 * Insert process into run queue.
			 */
			pp->p_lback->p_lforw = pp->p_lforw;
			pp->p_lforw->p_lback = pp->p_lback;
			addu(pp->p_cval, (utimer-pp->p_lctim)*CVCLOCK);
			setrun(pp);

			/*
			 * Enable interrupts.
			 * Restart search at start of event queue.
			 * Disable interrupts.
			 */
			spl(s);
			pp = pp1;
			s = sphi();
		}
	}
	spl(s);
}

/*
 * Reschedule the processor.
 */
dispatch()
{
	register PROC *pp1;
	register PROC *pp2;
	register unsigned v;
	register int s;

	s = sphi();
	pp1 = iprocp;
	pp2 = &procq;
	v = 0;
	while ((pp2=pp2->p_lforw) != &procq) {
		v -= pp2->p_cval;
		if ((pp2->p_flags&PFCORE) == 0)
			continue;
		pp1 = pp2->p_lforw;
		pp1->p_cval += pp2->p_cval;
		pp2->p_cval = v;
		pp1->p_lback = pp2->p_lback;
		pp1->p_lback->p_lforw = pp1;
		pp1 = pp2;
		break;
	}
	spl(s);

	quantum = NCRTICK;
	disflag = 0;
	if (pp1 != SELF) {
		/*
		 * Consave() returns twice.
		 * 1st time is after our context is saved in u.u_syscon,
		 *	whereupon we should restore other proc's context.
		 * 2nd time is after our context is restored by another proc.
		 * Conrest() forces a context switch to a new process.
		 */
		s = sphi();
		SELF = pp1;
		if (consave(&u.u_syscon) == 0) {
#ifdef _I386
			conrest(*pp1->p_segp[SIUSERP]->s_vmem, &u.u_syscon);
#else
			conrest(
			  FP_SEL(pp1->p_u->s_faddr), offset(uproc,u_syscon));
#endif
		}
		if (SELF->p_pid != 0)
			segload();
		spl(s);
	}
}

/*
 * Add a process to the run queue.
 * This routine must be called at high priority.
 */
setrun(pp1)
register PROC *pp1;
{
	register PROC *pp2;
	register unsigned v;

	v = 0;
	pp2 = &procq;
	for (;;) {
		pp2 = pp2->p_lback;
		if ((v+=pp2->p_lforw->p_cval) >= pp1->p_cval)
			break;
		if (pp2 == &procq)
			break;
	}
	pp2->p_lforw->p_lback = pp1;
	pp1->p_lforw = pp2->p_lforw;
	pp2->p_lforw = pp1;
	pp1->p_lback = pp2;
	v -= pp1->p_cval;
	pp1->p_cval = v;
	pp1->p_lforw->p_cval -= v;
	pp1->p_state = PSRUN;
}

/*
 * Wait for the gate `g' to unlock, and then lock it.
 */
lock(g)
register GATE g;
{
	register int s;

	s = sphi();
	while (g[0]) {
		g[1] = 1;
		v_sleep((char *)g, CVGATE, IVGATE, SVGATE, "lock");
		/* Waiting for a gate to unlock.  */
	}
	g[0] = 1;
	spl(s);
}

/*
 * Unlock the gate `g'.
 */
unlock(g)
register GATE g;
{
	g[0] = 0;
	if (g[1]) {
		g[1] = 0;
		disflag = 1;
		wakeup((char *)g);
	}
}
