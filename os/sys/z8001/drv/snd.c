/*
 * Commodore Z8001 -- speaker tone generator.
 *
 * The speaker is driven from counter/timer 2 of the IEEE/sound Z8036
 * (U66, the chip md.s calls ZCIO2, I/O 0x80-0xFF).  CT2 runs in
 * continuous square-wave mode with External Output Enable set, which
 * puts its output on port B bit 0; PB0 feeds three paralleled 7407
 * open-collector buffers and then the speaker.
 *
 * U66 is shared.  PB1 is the Centronics ACK input (pattern-match
 * interrupt, vector LPIRQ), PB2-PB7 are IEEE-488 control lines and
 * port C carries the Centronics strobe, so MCCR and PBDD are
 * read-modify-written and the pattern registers, MICR, the interrupt
 * vector and port C are left alone.  CT2 runs with no interrupt
 * enable, so it never enters the /VI daisy chain.
 *
 * Register ports follow the md.s convention for this chip: a Z8036
 * register number r is at ZCIO2 + 2*r + 1.
 */
#include	<coherent.h>
#include	<timeout.h>

#define	ZCIO2		0x80		/* Z8036 #2 base port */
#define	MCCR		(ZCIO2+0x03)	/* Master configuration control */
#define	CT2CS		(ZCIO2+0x17)	/* C/T 2 command and status */
#define	CT2TCMSB	(ZCIO2+0x31)	/* C/T 2 time constant high */
#define	CT2TCLSB	(ZCIO2+0x33)	/* C/T 2 time constant low */
#define	CT2MS		(ZCIO2+0x3B)	/* C/T 2 mode specification */
#define	PBDD		(ZCIO2+0x57)	/* Port B data direction */

#define	MCCR_CT2E	0x20		/* Counter/timer 2 enable */
#define	CT2MS_TONE	0xC2		/* Continuous, external output
					   enable, square wave */
#define	CT2CS_RUN	0x06		/* Gate command + trigger command */
#define	CT2CS_HALT	0x00		/* Gate closed */
#define	PBDD_PB0OUT	0xFE		/* Mask making PB0 an output */

/*
 * The counters clock at half U66's PCLK, and U66's PCLK is SNDCLK,
 * 750 kHz.  A square wave toggles once per time-constant expiry, so
 *	f = SNDCLK/2 / (2 * TC)		TC = SNDCLK / (4 * f)
 * The time constant is 16 bits, which bounds the usable range.
 */
#define	SNDCLK		750000L		/* U66 PCLK, Hz */
#define	SNDFMIN		3		/* Lowest frequency TC can express */
#define	SNDFMAX		20000		/* Highest frequency worth asking for */

#define	SNDPITCH	440		/* Bell tone, Hz */
#define	SNDLEN		(HZ/8)		/* Bell duration, clock ticks */

static	TIM	sndtim;			/* Tone duration timer */

/*
 * Stop the tone: close the software gate, then drop CT2's enable in
 * MCCR without disturbing the port and CT3 bits belonging to the
 * Centronics driver.
 */
sndquiet()
{
	register int s;

	s = sphi();
	outb(CT2CS, CT2CS_HALT);
	outb(MCCR, inb(MCCR) & ~MCCR_CT2E);
	spl(s);
}

/*
 * Timer callback.  clock.c invokes a timeout function as
 * (*t_func)(t_farg, tp), so both arguments are declared even though
 * neither is used.
 */
static
sndexpire(a, tp)
char *a;
TIM *tp;
{
	sndquiet();
}

/*
 * Sound `freq' Hz for `ticks' clock ticks.  A call while a tone is
 * sounding retunes it and restarts the duration.
 */
sndtone(freq, ticks)
int freq;
unsigned ticks;
{
	register unsigned tc;
	register int s;

	if (freq < SNDFMIN || freq > SNDFMAX || ticks == 0)
		return;
	tc = (unsigned)(SNDCLK / (4L * (long)freq));

	s = sphi();
	outb(PBDD, inb(PBDD) & PBDD_PB0OUT);
	outb(CT2MS, CT2MS_TONE);
	outb(CT2TCMSB, tc >> 8);
	outb(CT2TCLSB, tc & 0xFF);
	outb(MCCR, inb(MCCR) | MCCR_CT2E);
	outb(CT2CS, CT2CS_RUN);
	spl(s);

	timeout(&sndtim, ticks, sndexpire, (char *)0);
}

/*
 * The console bell.
 */
sndbeep()
{
	sndtone(SNDPITCH, (unsigned)SNDLEN);
}
