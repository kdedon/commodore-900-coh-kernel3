/*
 * File:	mouse.c
 *
 * Purpose:	pointing-device driver for the HR display card's FN#7
 *		"Mouse Interface" -- a 9-pin Amiga-compatible port at J2.
 *
 *	The hardware is two free-running 10-bit quadrature counters and a
 *	three-button level, read as two 16-bit ports:
 *
 *		XPORT 0x400	bits 0-9  X count, bits 15/14/13 buttons
 *		YPORT 0x402	bits 0-9  Y count
 *
 *	There is no interrupt and no control register: nothing is written, a
 *	read does not clear the counters, and motion is recovered only by
 *	differencing successive samples.  The buttons are the mouse's own
 *	switch lines re-latched every bus clock, so they read as a level and
 *	carry no history.  The driver therefore samples both ports on the
 *	100 Hz clock for as long as the device is open.
 *
 *	Reads deliver the machine's own event, `struct msevent'
 *	(include/sys/mouse.h): the wrap-corrected motion of both axes and the
 *	state of all three buttons, in fields, one event per sample in which
 *	anything happened.  The minor number selects the form:
 *
 *		minor 0		/dev/mouse	native events
 *		minor 1		/dev/mousems	Mouse Systems 3-byte packets
 *
 *	Minor 1 serves clients that speak the Mouse Systems serial protocol
 *	(MGR): it quantises each report to +-127 counts per axis and
 *	inverts Y.  Everything native uses minor 0.
 *
 *	One reader at a time, on either minor: two processes sharing the
 *	stream would each see a fraction of the motion.
 */

/*
 * -----------------------------------------------------------------
 * Includes.
 */
#include <sys/coherent.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/io.h>
#include <sys/con.h>
#include <sys/sched.h>			/* CVTTOUT, IVTTOUT, SVTTOUT */
#include <sys/mouse.h>
#include <errno.h>
#include <poll.h>

/*
 * -----------------------------------------------------------------
 * Definitions.
 */

/*
 * FN#7 programming interface (../hr/src/driver/hr.h).
 */
#define	XPORT	0x400			/* X count and button state */
#define	YPORT	0x402			/* Y count */
#define	MMASK	(MSCOUNTS-1)		/* count field */
#define	MSIGN	(MSCOUNTS>>1)		/* sign bit of a 10-bit difference */
#define	DSMENU	0x8000			/* left button, active high */
#define	DWMENU	0x4000			/* middle button */
#define	DACTION	0x2000			/* right button */

/*
 * Minor numbers.
 */
#define	MSNATIVE 0			/* struct msevent */
#define	MSSERIAL 1			/* Mouse Systems 3-byte packets */

/*
 * Event queue.  Sixteen events is a sixth of a second of continuous motion at
 * the sampling rate, after which further samples are folded into the last
 * event rather than dropped; a power of two so the wrap is a mask.
 */
#define	MSQN	16
#define	MSEVSZ	(sizeof (struct msevent))

/*
 * Mouse Systems packet fields, which number the buttons differently from the
 * native event and carry them inverted.
 */
#define	PBLEFT	4
#define	PBMID	2
#define	PBRIGHT	1
#define	MSSTEP	127			/* largest motion one packet reports */
#define	MSPSZ	3			/* bytes in a packet */

/*
 * Physical base of the HR bitmap, which is the only thing on the card that
 * answers a memory access and therefore how its presence is established.
 * Written out here rather than taken from machine.h BMPHYS, which names a
 * different address.
 */
#define	MSVRAM	0x003E0000L

/*
 * -----------------------------------------------------------------
 * Functions.
 */
int	nulldev();
int	nonedev();

/*
 * Configuration functions (local functions).
 */
static void msclose();
static void msioctl();
static void msload();
static void msopen();
static void msread();
static void msuload();
static void mswrite();
static int  mspoll();

/*
 * Support functions (local functions).
 */
static void msflush();
static void msorigin();
static void mssamp();
static void mstimer();
static int  msbits();
static int  msclip();
static int  msdiff();
static int  mspacket();
static int  msprobe();

/*
 * -----------------------------------------------------------------
 * Global Data.
 */

/*
 * Configuration table (export data).
 */
CON mscon ={
	DFCHR|DFPOL,			/* Flags */
	7,				/* Major index */
	msopen,				/* Open */
	msclose,			/* Close */
	nonedev,			/* Block */
	msread,				/* Read */
	mswrite,			/* Write */
	msioctl,			/* Ioctl */
	nulldev,			/* Powerfail */
	nulldev,			/* Timeout */
	msload,				/* Load */
	msuload,			/* Unload */
	mspoll				/* Poll */
};

static TIM	mstim;			/* the 100 Hz sampling timer */
static event_t	msipev;			/* input polls enabled on the device */
static struct msevent msq[MSQN];	/* events awaiting a reader */
static int	msqh;			/* queue read index */
static int	msqn;			/* queue occupancy */
static int	mshere;			/* an HR card answered at load */
static int	msbusy;			/* the device is open */
static int	mssmode;		/* the open minor speaks packets */
static int	mslx, msly;		/* previous count samples */
static int	msbut;			/* button state last reported */
static char	mspkt[MSPSZ];		/* packet being handed to a reader */
static int	mspi;			/* next byte of it */
static int	mspn;			/* bytes of it left */

/*
 * -----------------------------------------------------------------
 * Code.
 */

/*
 * msload()
 *
 * Establish whether there is a card to read.  The ports themselves float on a
 * machine without one, so the bitmap is what is probed, exactly as the boot
 * code's console selection probes it.
 */
static void
msload()
{
	mshere = msprobe();
}

/*
 * msuload()
 */
static void
msuload()
{
	msbusy = 0;
	timeout(&mstim, 0, (int (*)())0, (char *)0);
}

/*
 * msprobe()
 *
 * Write and read back two complementary patterns in the first word of the HR
 * bitmap, restoring it afterwards.  There is no system memory in that window,
 * so both patterns surviving means the card is there.
 */
static int
msprobe()
{
	unsigned old;
	unsigned w;
	int ok;

	pkcopy((paddr_t)MSVRAM, (char *)&old, sizeof (unsigned));
	w = 0x55AA;
	kpcopy((char *)&w, (paddr_t)MSVRAM, sizeof (unsigned));
	pkcopy((paddr_t)MSVRAM, (char *)&w, sizeof (unsigned));
	ok = (w == 0x55AA);
	if (ok) {
		w = 0xAA55;
		kpcopy((char *)&w, (paddr_t)MSVRAM, sizeof (unsigned));
		pkcopy((paddr_t)MSVRAM, (char *)&w, sizeof (unsigned));
		ok = (w == 0xAA55);
	}
	kpcopy((char *)&old, (paddr_t)MSVRAM, sizeof (unsigned));
	return ok;
}

/*
 * msopen()
 *
 * Take the counts as they stand for the origin, so the first event reports the
 * motion since the open and not since the machine was switched on, and start
 * the sampling timer.
 */
static void
msopen(dev, mode)
dev_t dev;
int mode;
{
	int s;
	register int m;

	m = minor(dev);
	if (mshere == 0 || m > MSSERIAL) {
		u.u_error = ENXIO;
		return;
	}
	if (msbusy) {
		u.u_error = EDBUSY;
		return;
	}

	s = sphi();
	mssmode = (m == MSSERIAL);
	msflush();
	msorigin();
	msbusy = 1;
	timeout(&mstim, 1, mstimer, (char *)0);
	spl(s);
}

/*
 * msclose()
 */
static void
msclose(dev, mode)
dev_t dev;
int mode;
{
	int s;

	s = sphi();
	msbusy = 0;
	timeout(&mstim, 0, (int (*)())0, (char *)0);
	spl(s);
}

/*
 * msorigin()
 *
 * Adopt the current counts as the reference the next sample differences
 * against, and the current buttons as the state last reported.  Called with
 * the clock held off.
 */
static void
msorigin()
{
	register int i;

	i = in(XPORT);
	mslx = i & MMASK;
	msly = in(YPORT) & MMASK;
	msbut = msbits(i);
}

/*
 * msflush()
 *
 * Drop everything queued, including a packet half handed over.  Called with
 * the clock held off.
 */
static void
msflush()
{
	msqh = 0;
	msqn = 0;
	mspi = 0;
	mspn = 0;
}

/*
 * msread()
 *
 * Wait for something to have happened, then hand over as much as the request
 * asks for: whole events on the native minor, bytes on the packet one.  The
 * queue is only ever filled from the clock, so a reader takes from it with the
 * clock held off.
 */
static void
msread(dev, iop)
dev_t dev;
register IO *iop;
{
	struct msevent ev;
	register char *p;
	register int i;
	int s;

	if (mssmode == 0 && iop->io_ioc < MSEVSZ) {

		/*
		 * An event is indivisible, so a request too small to hold one
		 * can never be answered.
		 */
		u.u_error = EINVAL;
		return;
	}

	while (msqn == 0 && mspn == 0) {
		if (iop->io_flag & IONDLY) {
			u.u_error = EAGAIN;
			return;
		}
		v_sleep((char *)msq, CVTTOUT, IVTTOUT, SVTTOUT, "mouse");
		/* The mouse driver is waiting for motion.  */
		if (SELF->p_ssig && nondsig()) {
			u.u_error = EINTR;
			return;
		}
	}

	if (mssmode) {
		while (iop->io_ioc != 0) {
			if (mspn == 0) {
				s = sphi();
				i = mspacket();
				spl(s);
				if (i == 0)
					break;
			}
			if (ioputc(mspkt[mspi] & 0377, iop) == -1)
				break;
			mspi++;
			mspn--;
		}
		return;
	}

	while (iop->io_ioc >= MSEVSZ) {
		s = sphi();
		if (msqn == 0) {
			spl(s);
			break;
		}
		ev = msq[msqh];
		msqh = (msqh + 1) & (MSQN-1);
		msqn--;
		spl(s);

		p = (char *)&ev;
		for (i = 0; i < MSEVSZ; i++)
			if (ioputc(p[i] & 0377, iop) == -1)
				return;
	}
}

/*
 * mswrite()
 *
 * A serial-mouse client writes speed-select and sample-rate strings to the
 * line it thinks it has.  There is no line, and the counters need no setting
 * up, so the bytes are accepted and dropped.
 */
static void
mswrite(dev, iop)
dev_t dev;
register IO *iop;
{
	while (iogetc(iop) != -1)
		;
}

/*
 * msioctl()
 *
 * The two calls of include/sys/mouse.h.  A serial-mouse client's line-setting
 * ioctls land here too and are refused, which is what such a client does with
 * a device that is not a tty.
 */
static void
msioctl(dev, com, vec)
dev_t dev;
int com;
char *vec;
{
	struct msstate st;
	register int i;
	int s;

	switch (com) {

	case MSIOCGETST:
		s = sphi();
		i = in(XPORT);
		st.ms_port = i;
		st.ms_xraw = i & MMASK;
		st.ms_yraw = in(YPORT) & MMASK;
		spl(s);
		st.ms_buttons = msbits(i);
		kucopy((char *)&st, vec, sizeof (struct msstate));
		return;

	case MSIOCFLUSH:
		s = sphi();
		msflush();
		msorigin();
		spl(s);
		return;

	default:
		u.u_error = EINVAL;
		return;
	}
}

/*
 * mspoll()
 *
 * Readable once there is an event queued; always writable, since a write is
 * discarded.
 */
static int
mspoll(dev, ev, msec)
dev_t dev;
int ev;
int msec;
{
	ev &= ~POLLPRI;

	if ((ev & POLLIN) && msqn == 0 && mspn == 0) {

		/*
		 * Blocking input poll.
		 */
		if (msec != 0)
			pollopen(&msipev);

		/*
		 * Second look to avoid clock race.
		 */
		if (msqn == 0 && mspn == 0)
			ev &= ~POLLIN;
	}

	return ev;
}

/*
 * mstimer()
 *
 * Timeout entry.  clock.c invokes a timeout as (*t_func)(t_farg, tp).
 */
static void
mstimer(a, tp)
char *a;
TIM *tp;
{
	mssamp();
}

/*
 * mssamp()
 *
 * One sample.  Difference each count against the previous sample in 10-bit
 * arithmetic and queue an event if either axis moved or a button changed.
 *
 * The counts run free and wrap at MSCOUNTS, so only the masked difference
 * means anything and an absolute comparison of two samples means nothing.
 *
 * With the queue full the sample is folded into the event at the tail instead
 * of being dropped, so a reader that falls behind loses the timing of a
 * movement and never the movement; the fold is marked, because a button that
 * is pressed and released entirely within it leaves no trace but the mark.
 *
 * The reader is woken whenever anything is queued, not only when this sample
 * queued it: a reader that finds the queue empty and is about to sleep can be
 * overtaken by the sample that fills it, and a mouse that then stops moving
 * produces nothing further to wake it with.  So the wakeup repeats every tick
 * until the queue is drained, which bounds that race at one tick.
 */
static void
mssamp()
{
	register struct msevent *ep;
	register int b;
	int dx, dy;
	int i;
	int s;

	if (msbusy == 0)
		return;
	timeout(&mstim, 1, mstimer, (char *)0);

	s = sphi();

	i = in(XPORT);
	b = msbits(i);
	i &= MMASK;
	dx = msdiff(i, mslx);
	mslx = i;

	i = in(YPORT) & MMASK;
	dy = msdiff(i, msly);
	msly = i;

	if (dx != 0 || dy != 0 || b != msbut) {
		if (msqn == MSQN) {
			ep = &msq[(msqh + MSQN - 1) & (MSQN-1)];
			ep->ms_dx += dx;
			ep->ms_dy += dy;
			ep->ms_flags |= MSFCOALESCE;
		} else {
			ep = &msq[(msqh + msqn) & (MSQN-1)];
			msqn++;
			ep->ms_dx = dx;
			ep->ms_dy = dy;
			ep->ms_flags = 0;
		}
		ep->ms_buttons = b;
		ep->ms_time = (unsigned)lbolt;
		msbut = b;
	}

	spl(s);

	if (msqn != 0) {
		wakeup((char *)msq);
		pollwake(&msipev);
	}
}

/*
 * msdiff()
 *
 * Signed motion between two counts, in the counters' own 10-bit arithmetic.
 * Sign-extending the masked difference makes the wrap invisible for as long as
 * a real movement stays under half the modulus between two samples, which at
 * the sampling rate it does by three orders of magnitude.  Written as a mask
 * and a subtract so that it does not depend on >> being arithmetic.
 */
static int
msdiff(now, was)
register int now;
register int was;
{
	register int d;

	d = (now - was) & MMASK;
	if (d & MSIGN)
		d -= MSCOUNTS;
	return d;
}

/*
 * mspacket()
 *
 * Stage one Mouse Systems packet from the event at the head of the queue, and
 * return 1, or return 0 if there is nothing to send.  A packet carries at most
 * MSSTEP counts per axis and a client resynchronising treats a data byte of
 * 0x80 as a framing error, so a larger movement stays in the event and is
 * carried by the packets after it; the event is released once both axes are
 * spent.  Called with the clock held off.
 */
static int
mspacket()
{
	register struct msevent *ep;
	register int b;
	int dx, dy;

	if (msqn == 0)
		return 0;
	ep = &msq[msqh];

	dx = msclip(ep->ms_dx);
	ep->ms_dx -= dx;
	dy = msclip(ep->ms_dy);
	ep->ms_dy -= dy;

	b = 0;
	if (ep->ms_buttons & MSBLEFT)
		b |= PBLEFT;
	if (ep->ms_buttons & MSBMIDDLE)
		b |= PBMID;
	if (ep->ms_buttons & MSBRIGHT)
		b |= PBRIGHT;

	if (ep->ms_dx == 0 && ep->ms_dy == 0) {
		msqh = (msqh + 1) & (MSQN-1);
		msqn--;
	}

	/*
	 * The buttons are carried inverted, and Y is carried negated: the
	 * protocol's client counts Y down the screen, and the counter counts it
	 * up as the mouse moves away from the user.
	 */
	mspkt[0] = 0x80 | (~b & 0x07);
	mspkt[1] = dx;
	mspkt[2] = -dy;
	mspi = 0;
	mspn = MSPSZ;
	return 1;
}

/*
 * msbits()
 *
 * Button state of an XPORT word, in native order.  The latch is active high.
 */
static int
msbits(w)
register int w;
{
	register int b;

	b = 0;
	if (w & DSMENU)
		b |= MSBLEFT;
	if (w & DWMENU)
		b |= MSBMIDDLE;
	if (w & DACTION)
		b |= MSBRIGHT;
	return b;
}

/*
 * msclip()
 *
 * The part of an axis's motion that one packet can carry.
 */
static int
msclip(v)
register int v;
{
	if (v > MSSTEP)
		return MSSTEP;
	if (v < -MSSTEP)
		return -MSSTEP;
	return v;
}
