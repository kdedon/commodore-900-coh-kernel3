/*
**	Native interface to the C900 HR display card's FN#7 pointing device.
**	Shared by the driver (sys/drv/mouse.c) and by user programs.
**
**	/dev/mouse delivers a stream of fixed-width `struct msevent', one per
**	sample in which something happened.  A read transfers whole events and
**	nothing else: the request must be at least sizeof (struct msevent) and
**	the count delivered is always a multiple of it, so a reader never has
**	to frame, resynchronise or scan for a header byte.
**
**	Motion is relative, in counter detents, right and down positive, and
**	already corrected for the counters' wrap.  Buttons are absolute: every
**	event carries the state of all three at the instant of the sample, so a
**	reader that joins late or falls behind still has the current state.
**
**	The hardware has no interrupt and its button lines are a level, not a
**	latch, so the driver samples at the clock rate and a press and release
**	that both fall between two samples is not observable by anything.
*/

/*
**	The guard is SYS_MOUSE_H, not MOUSE_H: a client that also has a mouse
**	header of its own (MGR's src/mgr/mouse.h) guards on the plain name, and
**	whichever of the two came second would be skipped entirely.
*/
#ifndef	SYS_MOUSE_H
#define	SYS_MOUSE_H	SYS_MOUSE_H

/*
**	ms_buttons: set means pressed.
*/
#define	MSBLEFT		0x0001		/* left button (DSMENU) */
#define	MSBMIDDLE	0x0002		/* middle button (DWMENU) */
#define	MSBRIGHT	0x0004		/* right button (DACTION) */
#define	MSBALL		0x0007

/*
**	ms_flags.
**
**	MSFCOALESCE marks an event whose motion is the sum of two or more
**	samples, because the reader was too far behind for the queue to hold
**	them separately.  The motion and the final button state are exact; what
**	is lost is the timing, and any button change that both began and ended
**	inside the run.
*/
#define	MSFCOALESCE	0x0001

struct msevent {
	int	 ms_dx;			/* detents right since the last event */
	int	 ms_dy;			/* detents down since the last event */
	unsigned ms_buttons;		/* MSBLEFT | MSBMIDDLE | MSBRIGHT */
	unsigned ms_flags;		/* MSFCOALESCE */
	unsigned ms_time;		/* clock ticks, low 16 bits of lbolt */
};

/*
**	MSIOCGETST reads the hardware as it stands, for a caller that would
**	rather poll than stream.  ms_xraw and ms_yraw are the free-running
**	10-bit counters verbatim -- a position in no coordinate system, whose
**	only meaning is the difference between two readings taken modulo
**	MSCOUNTS.  ms_port is the whole X port word, counts and button bits
**	together, as the CPU sees it.
**
**	Reading state this way does not disturb the event stream, and does not
**	clear the counters: nothing does.
*/
struct msstate {
	unsigned ms_xraw;		/* X counter, 0..MSCOUNTS-1 */
	unsigned ms_yraw;		/* Y counter, 0..MSCOUNTS-1 */
	unsigned ms_buttons;		/* MSBLEFT | MSBMIDDLE | MSBRIGHT */
	unsigned ms_port;		/* the X port word, counts and buttons */
};

#define	MSBITS		10		/* bits of count in each port */
#define	MSCOUNTS	1024		/* count modulus, 1 << MSBITS */

#define	MSIOCGETST	('m'<<8|1)	/* read the raw counters and buttons */
#define	MSIOCFLUSH	('m'<<8|2)	/* discard queued events, retake origin */

#endif
