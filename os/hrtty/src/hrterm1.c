#ifdef	FAKEINPUT
#define	FAKEIN	('c'<<8|13)
#endif
/*
**	Device dependant driver routines for the hi-res tty driver
*/

#include	<coherent.h>
#include	<con.h>
#include	<errno.h>
#include	<stat.h>
#include	<tty.h>
#include	<uproc.h>
#include	<signal.h>
#include	"../h/hrio.h"
#include	"../h/font.h"

#define NMINOR 1
#define MAJOR 8

int	htstart();

/*
**	Terminal Structure
*/
TTY	hrtty = {
	{0},{0},0,htstart,NULL,0,0
};

/*
**	Set while another program owns the frame buffer.  Output is accepted
**	and discarded; the keyboard is untouched.
*/
int	hrsusp;


v0load()
{
/*
		SET KEYBOARD INT HERE!!
*/
	outb(0x414,0xff);
	portst();			/* Init MMU EELCECH !! */
	hrfseed();			/* High bank from gall.c */
	ricoload();			/* Clear Screen, etc. */
}

v0uload()
{
/*
		RESET KEYBOARD INT HERE!
*/
}

v0open(dev, mode)
dev_t dev;
int mode;
{
	register	int	s;
/*
**	Make sure it's a legitimate devivce no. only 0 is supported
*/
	if(minor(dev) >= NMINOR) {
		u.u_error = ENXIO;
		return;
	}
	if ((hrtty.t_flags&T_EXCL) != 0 && super() == 0) {
		u.u_error = ENODEV;
		return;
	}
	ttsetgrp(&hrtty, dev, mode);
	s = sphi();
	if(hrtty.t_open++ == 0) {
		hrtty.t_flags = T_CARR;		/* no modem: carrier always on */
		ttopen(&hrtty);
	}
	spl(s);
}


v0close(dev, mode)
{
	register	int	s;
	s = sphi();
	if (--hrtty.t_open == 0)
		ttclose(&hrtty);
	spl(s);
}


v0read(dev, iop)
dev_t dev;
IO *iop;
{
	ttread(&hrtty, iop, SFCW);
}

v0write(dev, iop)
dev_t dev;
IO *iop;
{
	ttwrite(&hrtty, iop, SFCW);
}

/*
**	Fill the loadable bank from the compiled-in high half of `font', which
**	is the console's default for 0xa0-0xff and is never written.  Called at
**	driver load and by HRIOCRFONT.
*/
hrfseed()
{
	register short	*sp;
	register short	*dp;
	register unsigned n;

	sp = &font[HRLOW][0];
	dp = &sfont[0][0];
	for (n = HRHIGH*HRGH; n != 0; --n)
		*dp++ = *sp++;
}

/*
**	HRIOCSFONT.  Copy hf_count glyphs from the caller into the loadable
**	bank at slot hf_first.  The glyph bits land in the driver's own data,
**	which the OS window makes addressable for exactly the length of this
**	call; nothing is kept.  The struct is read into a kernel-stack copy
**	first, since a driver runs in system space and cannot dereference a
**	user address, and ukcopy()'s ufix() forces the source into normal
**	space, so an out-of-range hf_bits cannot reach driver or kernel memory.
*/
hrfload(vec)
char	*vec;
{
	struct	hrfont	hf;
	register unsigned f;
	register unsigned n;

	ukcopy(vec, (char *)&hf, sizeof(struct hrfont));
	f = hf.hf_first;
	n = hf.hf_count;
	if (n == 0)
		return (0);
	if (f < HRLOW || f >= HRNGLYPH || n > HRHIGH || f+n > HRNGLYPH)
		return (-1);
	ukcopy((char *)hf.hf_bits, (char *)&sfont[f-HRLOW][0], n*HRGB);
	return (0);
}

/*
**	HRIOCGFONT.  Hand back hf_count glyphs from slot hf_first.  Reads span
**	both banks -- a program building a character set on top of the console
**	alphabet wants the low one -- so a range crossing HRLOW is answered in
**	two transfers.
*/
hrfget(vec)
char	*vec;
{
	struct	hrfont	hf;
	register unsigned f;
	register unsigned n;
	register unsigned k;
	register char	*up;

	ukcopy(vec, (char *)&hf, sizeof(struct hrfont));
	f = hf.hf_first;
	n = hf.hf_count;
	if (n == 0)
		return (0);
	if (f >= HRNGLYPH || n > HRNGLYPH || f+n > HRNGLYPH)
		return (-1);
	up = (char *)hf.hf_bits;
	if (f < HRLOW) {
		k = HRLOW - f;
		if (k > n)
			k = n;
		kucopy((char *)&font[f][0], up, k*HRGB);
		up += k*HRGB;
		f += k;
		n -= k;
	}
	if (n != 0)
		kucopy((char *)&sfont[f-HRLOW][0], up, n*HRGB);
	return (0);
}

v0ioctl(dev, com, vec)
dev_t dev;
struct	sgttyb *vec;
{
	register int	s;
#ifdef	FAKEINPUT
	struct	sgttyb	fc;

	if(com == FAKEIN) {
		s = sphi();
		ukcopy(vec, &fc, sizeof(struct sgttyb));
		ttin(&hrtty, fc.sg_ispeed);	/* convention with faker */
		spl(s);
		return;
	}
#endif
	/*
	**	The console's own ioctls, ahead of the line discipline: ttioctl()
	**	answers EINVAL for anything it does not know.
	**
	**	None of these raise the priority.  Each moves up to 4800 bytes or
	**	clears 100K of frame buffer, which is milliseconds of interrupts
	**	off -- long enough to overrun the SCC's three-deep receive FIFO and
	**	to lose clock ticks.  Nothing needs it: the only state the output
	**	path shares with a font transfer is the glyph bits themselves, and
	**	both sides address them in range, so the worst a race can do is
	**	paint one character half old and half new.  The repaint below
	**	instead stands the console off for its duration, which keeps a
	**	painter away without disabling anything.
	*/
	switch (com) {
	case HRIOCSFONT:
		if (hrfload((char *)vec) < 0)
			u.u_error = EINVAL;
		return;
	case HRIOCGFONT:
		if (hrfget((char *)vec) < 0)
			u.u_error = EINVAL;
		return;
	case HRIOCRFONT:
		hrfseed();
		return;
	case HRIOCSTOP:
		hrsusp = 1;
		return;
	case HRIOCSTART:
		hrsusp = 1;
		ricoload();		/* clear both halves, home the cursor */
		slecompute();		/* the scroll tables describe it again */
		hrsusp = 0;
		return;
	}
	s = sphi();
	ttioctl(&hrtty, com, vec);
	spl(s);
}

/*
**	poll(2)/select(2) on the console.  dpoll() answers POLLNVAL for a
**	driver carrying no c_poll, which tells a program waiting on the
**	keyboard and another descriptor at once that its terminal is not a
**	pollable object at all.
*/
v0poll(dev, ev, msec)
dev_t dev;
int ev;
int msec;
{
	return ttpoll(&hrtty, ev, msec);
}

v0in(c)
char c;
{
	ttin(&hrtty, c);
}
htcopy(cp)
register char *cp;
{
	register int s;
	while(*cp) {
		s = sphi();
		ttin(&hrtty, *cp++);
		spl(s);
	}
}


/*
**	Start the Output Stream.
**	Called from `ttywrite'
**	This calls the rico vt50 subroutines
**
**	Every path that touches the frame buffer -- glyphs, the cursor, the
**	visual bell, the scroller -- is reached from here, so standing off is
**	one test.  The queue is still drained: a writer whose bytes were left
**	to pile up would block in ttwrite() at OHILIM and never wake.
*/
htstart(tp)
register TTY *tp;
{
	register int c;
	if (hrsusp != 0) {
		while (ttout(tp) >= 0)
			;
		return;
	}
	cursor();
	while(( c=ttout(tp)) >= 0) {
		rico(c);
		if((tp->t_flags&T_STOP) != 0)
			break;
	}
	cursor();
}


