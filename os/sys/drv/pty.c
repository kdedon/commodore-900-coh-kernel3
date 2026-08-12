/*
 * File:	pty.c
 *
 * Purpose:	pseudoterminal device driver
 *
 *	Master devices are named /dev/pty[p-v][0-f].
 *	Corresponding slaves are /dev/tty[p-v][0-f].
 *
 *	Minor numbers are 0..127 assigned in increasing order,
 *	plus 128 for the master device.
 *
 *	Only the first NUPTY channels answer: a higher one is ENXIO, so the
 *	node set stops at NUPTY-1 (/dev/ptyp0../dev/ptypf as configured) and
 *	grows in name order, ptyq0 next.  So does the search a server does for
 *	a free master (cmd/telnetd/pty.c get_pty).
 *
 *	Output written to the master appears as input to the slave and
 *	vice versa.  Data path is:
 *
 *		app	master		slave	line	app
 *		using	device	shunt	device	disc.	using
 *		master	driver		driver	module	slave
 *
 *	slave read does ttread() which is fed by ttin()
 *	master write does ttin()
 *
 *	slave write does ttwrite() which is fed by ttout()
 *	master read does ttout()
 *
 * $Log:	pty.c,v $
 * Revision 1.3  92/07/16  16:35:29  hal
 * Kernel #58
 *
 * Revision 1.2  92/03/18  07:45:33  hal
 * master device polling added
 *
 * Revision 1.1  92/03/16  12:57:31  hal
 * Initial revision
 *
 */

/*
 * -----------------------------------------------------------------
 * Includes.
 */
#include <sys/coherent.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/io.h>
#include <sys/tty.h>		/* indirectly includes sgtty.h */
#include <sys/con.h>
#include <sys/devices.h>
#include <errno.h>
#include <poll.h>
#include <sys/sched.h>		/* CVTTOUT, IVTTOUT, SVTTOUT */

/*
 * -----------------------------------------------------------------
 * Definitions.
 *	Constants.
 *	Macros with argument lists.
 *	Typedefs.
 *	Enums.
 */

#define	channel(dev)	(dev & 0x7F)
#define	master(dev)	(dev & 0x80)
#define	EEBUSY		EDBUSY

/*
 * Channels the driver can be configured for.  The per-channel PTY is allocated
 * on demand, so this bounds only the pointer table: an unopened channel costs
 * the four bytes of its ptyv[] slot and nothing else.  channel() masks the
 * minor with 0x7F, so 128 is the highest value this may take.
 */
#define	NPTY		16

/*
 * Is anything polling on this queue head?  pollopen() makes an unused head
 * circular on itself before it links the first event buffer, so an empty queue
 * is one that is still zero or points back at itself.
 */
#define	polling(qp)	((qp)->e_dnext != 0 && (qp)->e_dnext != (qp))

/*
 * Explanation of p_mopen values:
 * 0 - master is closed
 * 1 - master open, waiting for slave to open
 * 2 - master and slave both open
 * 3 - master open, slave has closed
 */

typedef struct pty {
	TTY p_tp;
	event_t p_iev;
	event_t p_oev;
	char p_mopen;
	char p_asleep;	/* master is asleep in read or write awaiting slave */
	char p_ttwr;	/* slave is suspended in mid ttwrite() */
	int p_use;	/* opens holding the channel: master plus each slave */
} PTY;

/*
 * -----------------------------------------------------------------
 * Functions.
 *	Import Functions.
 *	Export Functions.
 *	Local Functions.
 */
int nulldev();

/*
 * Configuration functions (local functions).
 */
static void ptyclose();
static void ptyioctl();
static void ptyload();
static void ptyopen();
static void ptyread();
static void ptyunload();
static void ptywrite();
static void ptystart();
static int ptypoll();

/*
 * Support functions (local functions).
 */
static void ptycycle();
static void ptytimer();
static PTY * ptyhold();
static void ptyrele();

/*
 * -----------------------------------------------------------------
 * Global Data.
 *	Import Variables.
 *	Export Variables.
 *	Local Variables.
 */
/*
 * Channels the driver answers on.  A patchable variable, clamped at load to the
 * NPTY entries of the pointer table; each channel needs a master and a slave
 * node.  One window of a window system is one channel, so this is the window
 * ceiling too.  It no longer sizes an allocation: a channel costs the arena
 * sizeof(PTY) only while it is open.
 */
int NUPTY = NPTY;

/*
 * Configuration table (export data).
 */
CON ptycon ={
	DFCHR|DFPOL,			/* Flags */
	PTY_MAJOR,			/* Major index */
	ptyopen,			/* Open */
	ptyclose,			/* Close */
	nulldev,			/* Block */
	ptyread,			/* Read */
	ptywrite,			/* Write */
	ptyioctl,			/* Ioctl */
	nulldev,			/* Powerfail */
	nulldev,			/* Timeout */
	ptyload,			/* Load */
	ptyunload,			/* Unload */
	ptypoll				/* Poll */
};

/*
 * The channel a minor number names, or 0 for a channel nobody has open.  Four
 * bytes each and permanently resident, against sizeof(PTY) per channel that is
 * held only while the channel is in use.
 */
static PTY * ptyv[NPTY];

/*
 * -----------------------------------------------------------------
 * Code.
 */

/*
 * ptyload()
 *
 * Nothing is allocated here.  NUPTY is bounded to the table so that a patched
 * value cannot index past it.
 */
static void
ptyload()
{
	if (NUPTY > NPTY)
		NUPTY = NPTY;
	if (NUPTY < 0)
		NUPTY = 0;
}

/*
 * ptyunload()
 */
static void
ptyunload()
{
	int i;

	for (i = 0; i < NPTY; i++) {
		if (ptyv[i]) {
			kfree((char *)ptyv[i]);
			ptyv[i] = (PTY *)0;
		}
	}
	printf("pty driver unloaded\n");
}

/*
 * ptyhold()
 *
 * Take a reference to a channel, allocating it on the open that first needs
 * it, and give its TTY a start function that reaches back to it.  alloc()
 * clears what it hands out, so only the non-zero fields are set here.
 */
static PTY *
ptyhold(chan)
int chan;
{
	register PTY * pp;
	register TTY * tp;

	if ((pp = ptyv[chan]) == 0) {
		if ((pp = (PTY *)kalloc(sizeof(PTY))) == 0)
			return (PTY *)0;
		tp = &pp->p_tp;
		tp->t_start   = ptystart;
		/* NULL, not nulldev: ttopen() and ttioctl() test t_param before
		 * calling it, so a pty needs no do-nothing function to reach
		 * through a far pointer. */
		tp->t_param   = (int (*)())0;
		tp->t_dispeed = B9600;
		tp->t_dospeed = B9600;
		tp->t_ddp     = (char *)pp;
		ptyv[chan] = pp;
	}
	pp->p_use++;
	return pp;
}

/*
 * ptyrele()
 *
 * Drop a reference and return the channel to the arena when the last one goes.
 *
 * Three things outside the driver can hold a pointer into a PTY and each is
 * dealt with before the memory goes back: a slave close still inside
 * ttclose() is counted, so the 10 Hz cycle timer that unblocks it is never
 * cancelled underneath it; the timers themselves are dequeued here, since a
 * TIM is a field of the TTY and the clock walks that list from interrupt
 * level; and a poll event buffer linked onto one of the four queue heads
 * inside the PTY would be walked by pollwake() through freed memory, so a
 * channel that still carries one keeps its memory and is reclaimed by the next
 * close instead.
 */
static void
ptyrele(chan)
int chan;
{
	register PTY * pp;
	register TTY * tp;

	if ((pp = ptyv[chan]) == 0 || pp->p_use == 0)
		return;
	if (--pp->p_use > 0)
		return;
	tp = &pp->p_tp;
	/* The nulls are cast: timeout()'s f is a function pointer and a is a
	 * char *, both 4 bytes, and this call has no prototype. */
	timeout(&tp->t_rawtim, 0, (int (*)())0, (char *)0);
	timeout(&tp->t_vtime, 0, (int (*)())0, (char *)0);
	if (polling(&pp->p_iev) || polling(&pp->p_oev)
	 || polling(&tp->t_ipolls) || polling(&tp->t_opolls))
		return;
	ptyv[chan] = (PTY *)0;
	kfree((char *)pp);
}

/*
 * ptyopen()
 *
 * The master opens exclusively and asserts carrier.  The slave waits for a
 * master to be there, then joins the line discipline and claims the line as
 * this process group's control terminal.
 *
 * The channel is allocated here and is held by this open until its close, so
 * an open that cannot get the memory says EKSPACE on the spot rather than
 * leaving some later and unrelated allocation to fail instead.
 */
static void
ptyopen(dev, mode)
dev_t dev;
int mode;
{
	int chan = channel(dev);
	PTY * pp;
	TTY * tp;

	if (chan >= NUPTY) {
		u.u_error = ENXIO;
		goto open_done;
	}
	if ((pp = ptyhold(chan)) == 0) {
		u.u_error = EKSPACE;
		goto open_done;
	}
	tp = &pp->p_tp;

	if (master(dev)){
		if (pp->p_mopen) {
			u.u_error = EEBUSY;
			ptyrele(chan);
			goto open_done;
		}
		if (tp->t_open)
			pp->p_mopen = 2;
		else
			pp->p_mopen = 1;
		wakeup((char *)(&tp->t_open));
		ptycycle(pp);
	} else {
		tp->t_flags |= T_HOPEN | T_STOP;
		for (;;) {	/* wait for carrier */
			if (pp->p_mopen)
				break;
			v_sleep((char *)(&tp->t_open), CVTTOUT, IVTTOUT,
				SVTTOUT, "ptycd");
			/* PTY driver is waiting for carrier.  */
			if (SELF->p_ssig && nondsig()) {  /* signal? */
				u.u_error = EINTR;
				tp->t_flags &= ~(T_HOPEN | T_STOP);
				ptyrele(chan);
				goto open_done;
			}
		}
		tp->t_flags |= T_CARR;
		tp->t_flags &= ~(T_HOPEN | T_STOP);
		ttopen(tp);
		tp->t_open++;
		ttsetgrp(tp, dev, mode);
		if (pp->p_mopen == 1 || pp->p_mopen == 3)
			pp->p_mopen = 2;
	}
open_done:;
}

/*
 * ptyclose()
 *
 * Closing the master drops carrier and hangs the slave up; closing the last
 * slave leaves the master readable until it sees the end of data.
 *
 * The reference this close drops is given up last, after ttclose() has
 * returned: a slave close sleeps in there until the output queue drains, and
 * on a pty only the master's read or the cycle timer moves it.
 */
static void
ptyclose(dev, mode)
dev_t dev;
int mode;
{
	int chan = channel(dev);
	PTY * pp;
	TTY * tp;

	if (chan >= NUPTY || (pp = ptyv[chan]) == 0) {
		u.u_error = ENXIO;
		return;
	}
	tp = &pp->p_tp;

	if (master(dev)){
		if (pp->p_mopen) {
			tp->t_flags &= ~T_CARR;
			tthup(tp);
			pp->p_mopen = 0;
		}
	} else {
		if (--tp->t_open == 0) {
			ttclose(tp);
			if (pp->p_mopen == 2)
				pp->p_mopen = 3;
			wakeup((char *)(&pp->p_mopen));
		}
	}
	ptyrele(chan);
}

/*
 * ptyread()
 *
 * A master read drains the slave's output queue; a slave read is an ordinary
 * line-discipline read, and first releases a master blocked on the queue.
 */
static void
ptyread(dev, iop)
dev_t dev;
register IO * iop;
{
	int chan = channel(dev);
	PTY * pp;
	TTY * tp;
	int c;

	if (chan >= NUPTY || (pp = ptyv[chan]) == 0) {
		u.u_error = ENXIO;
		return;
	}
	tp = &pp->p_tp;

	if (master(dev)){
		int char_read = 0;

		while (iop->io_ioc) {
			c = ttout(tp);
			if (c == -1) { /* nothing to fetch */
				if (char_read) {
					ttstart(tp);
					goto read_done;
				}
				/* Hangup outranks IONDLY: a channel whose slave
				 * has closed is gone, not "nothing yet". */
				if (pp->p_mopen == 3) {
					u.u_error = EIO;
					goto read_done;
				}
				if (iop->io_flag & IONDLY) {
					u.u_error = EAGAIN;
					goto read_done;
				}
				ttstart(tp);
				pp->p_asleep = 1;
				v_sleep((char *)(&pp->p_mopen), CVTTOUT,
					IVTTOUT, SVTTOUT, "ptyread");
				/* The PTY driver is waiting for a read.  */
				if (SELF->p_ssig && nondsig()) {
					u.u_error = EINTR;
					goto read_done;
				}
			} else {
				ioputc(c, iop);
				char_read = 1;
			}
		}
read_done:;
	} else {
		if (pp->p_asleep) {
			pp->p_asleep = 0;
			wakeup((char *)(&pp->p_mopen));
		}
		pollwake(&pp->p_oev);
		ttread(tp, iop);
	}
}

/*
 * ptywrite()
 *
 * A master write feeds the line discipline a character at a time, as a serial
 * receive interrupt would; a slave write is an ordinary line-discipline write,
 * and ptystart() releases a master blocked on the queue.
 */
static void
ptywrite(dev, iop)
dev_t dev;
register IO * iop;
{
	int chan = channel(dev);
	PTY * pp;
	TTY * tp;
	int c;

	if (chan >= NUPTY || (pp = ptyv[chan]) == 0) {
		u.u_error = ENXIO;
		return;
	}
	tp = &pp->p_tp;

	if (master(dev)){
		while (iop->io_ioc) {
			if (!ttinp(tp)) {
				/* Hangup outranks IONDLY here for the same
				 * reason it does in ptyread(). */
				if (pp->p_mopen == 3) {
					u.u_error = EIO;
					goto write_done;
				}
				if (iop->io_flag & IONDLY) {
					u.u_error = EAGAIN;
					goto write_done;
				}
				pp->p_asleep = 1;
				v_sleep((char *)(&pp->p_mopen), CVTTOUT,
					IVTTOUT, SVTTOUT, "ptywrite");
				/* The PTY driver is waiting for a write.  */
				if (SELF->p_ssig && nondsig()) { /* signal? */
					u.u_error = EINTR;
					goto write_done;
				}
			}
			c = iogetc(iop);
			ttin(tp, c);
		}
		wakeup((char *)(&pp->p_mopen));
	} else {
		pp->p_ttwr = 1;
		ttwrite(tp, iop);
		pp->p_ttwr = 0;
	}
write_done:;
}

/*
 * ptyioctl()
 *
 * Line settings belong to the slave; the master side has none.
 */
static void
ptyioctl(dev, com, vec)
dev_t	dev;
int	com;
struct sgttyb *vec;
{
	int chan = channel(dev);
	PTY * pp;
	TTY * tp;

	if (chan >= NUPTY || (pp = ptyv[chan]) == 0) {
		u.u_error = ENXIO;
		return;
	}
	tp = &pp->p_tp;

	if (master(dev)){
		u.u_error = EINVAL;
	} else {
		ttioctl(tp, com, vec);
	}
}

/*
 * ptystart()
 *
 * The line discipline's transmit kick.  There is no hardware to hand the
 * character to -- the master's read is the transmitter -- so this only
 * releases a slave writer's counterpart.
 */
static void
ptystart(tp)
TTY * tp;
{
	PTY * pp = (PTY *)tp->t_ddp;

	if (pp != 0 && pp->p_ttwr)
		wakeup((char *)(&pp->p_mopen));
}

/*
 * ptypoll()
 *
 * The master is readable when the slave has output to fetch and writable when
 * the line discipline will take input, and hung up once the slave has closed;
 * the slave polls as any tty does.
 */
static int
ptypoll(dev, ev, msec)
dev_t dev;
int ev;
int msec;
{
	int chan = channel(dev);
	PTY * pp;
	TTY * tp;
	int ret;

	if (chan >= NUPTY || (pp = ptyv[chan]) == 0)
		return POLLERR;
	tp = &pp->p_tp;

	if (master(dev)) {
		/*
		 * Priority polls not supported.
		 */
		ev &= (POLLIN | POLLOUT);

		/*
		 * Slave gone.  A hangup is reported whether or not it was
		 * asked for, and no event is armed.  POLLIN as well while
		 * output remains, so a poller drains the channel before it
		 * gives up.
		 */
		if (pp->p_mopen == 3) {
			ret = POLLHUP;
			if (ttoutp(tp))
				ret |= POLLIN;
			return ret;
		}

		/*
		 * Input poll with no data present.
		 */
		if ((ev & POLLIN) && (ttoutp(tp) == 0)) {

			/*
			 * Blocking input poll.
			 */
			if (msec != 0) {
				pollopen(&pp->p_iev);
			}

			/*
			 * Second look to avoid interrupt race.
			 */
			if (ttoutp(tp) == 0)
				ev &= ~POLLIN;
		}

		/*
		 * Output poll with no space.
		 */
		if ((ev & POLLOUT) && (ttinp(tp) == 0)) {

			/*
			 * Blocking output poll.
			 */
			if (msec != 0) {
				pollopen(&pp->p_oev);
			}

			/*
			 * Second look to avoid interrupt race.
			 */
			if (ttinp(tp) == 0)
				ev &= ~POLLOUT;
		}

		ret = ev;
	} else
		ret = ttpoll(tp, ev, msec);
	return ret;
}

/*
 * ptycycle()
 *
 * Do a wakeup of any sleeping pty's at regular intervals, and re-arm.
 * Both sides of a pty are driven by the other side's system call, so a
 * missed wakeup would otherwise hang until the next one.
 */
static void
ptycycle(pp)
PTY * pp;
{
	TTY * tp = &pp->p_tp;

	/*
	 * Do wakeups.
	 */
	if (pp->p_asleep || pp->p_ttwr) {
		wakeup((char *)(&pp->p_mopen));
	}
	pollwake(&pp->p_iev);
	pollwake(&pp->p_oev);

	/*
	 * Schedule next cycle.
	 */
	if (pp->p_mopen)
		timeout(&tp->t_rawtim, HZ/10, ptytimer, (char *)pp);
}

/*
 * ptytimer()
 *
 * Timeout entry.  clock.c invokes a timeout as (*t_func)(t_farg, tp), so the
 * argument is a pointer, not the channel number.
 */
static void
ptytimer(a, tp)
char * a;
TIM * tp;
{
	ptycycle((PTY *)a);
}
