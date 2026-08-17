/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* (-lgl
 * 	The information contained herein is a trade secret of Mark Williams
 * 	Company, and  is confidential information.  It is provided  under a
 * 	license agreement,  and may be  copied or disclosed  only under the
 * 	terms of  that agreement.  Any  reproduction or disclosure  of this
 * 	material without the express written authorization of Mark Williams
 * 	Company or persuant to the license agreement is unlawful.
 * 
 * 	COHERENT Version 0.7.3
 * 	Copyright (c) 1982, 1983, 1984.
 * 	An unpublished work by Mark Williams Company, Chicago.
 * 	All rights reserved.
 -lgl) */
/*
 * Console driver for Commodore Z8000M.
 * Uses on-board RS-232 port B (SCC channel B) for all I/O.
 */
#include <coherent.h>

#define	RxAVAIL	0x01		/* character available */
#define	TxEMPTY	0x04		/* transmit buffer empty */
#define	WR0	0x0101		/* command register */
#define	RR0	0x0101		/* Tx & Rx status */
#define	WR3	0x0107		/* Rx params & control */
#define	WR4	0x0109		/* Tx & Rx misc params & modes */
#define	WR5	0x010b		/* Tx params & control */
#define	WR8	0x0111		/* transmit buffer */
#define	RR8	0x0111		/* receive buffer */
#define	WR11	0x0117		/* clock mode control */
#define	WR12	0x0119		/* baud rate generator low byte */
#define	WR13	0x011b		/* baud rate generator high byte */
#define	WR14	0x011d		/* misc control */

static	int	sccup;		/* console SCC programmed by sccinit() */

/*
 * Program the console SCC channel.
 *
 * The chip does not arrive here configured.  The boot ROM's power-on
 * diagnostic tests the SCC by issuing a WR9 channel reset on both channels,
 * which clears Rx Enable, Tx Enable and the baud rate time constant, and it
 * reprograms the chip again only along the path that drives the serial
 * console -- on a machine with a screen the ROM has nothing more to say to
 * the SCC and leaves the transmitter disabled.  A disabled transmitter never
 * moves the byte out of the buffer, so RR0 Tx Buffer Empty stays clear after
 * the first character and the poll in putchar() spins forever.
 *
 * The values are the ROM's own: 9600 baud (time constant 17 off the 6MHz
 * PCLK), 8 bits, no parity, one stop bit, both clocks from the baud rate
 * generator.  They are also al(4)'s, so the driver reprogramming this channel
 * when it loads does not change the line: WR4 is its ALMODE, WR11 its BRGINIT,
 * WR14 its BRGEN, WR3 its RxEN, and WR5 its (TxEN|RTS|DTR) -- the value al
 * itself writes once the line is open.  The time constant is the one fixed
 * exception: al derives it from romconf.rom_ctype, while the console keeps the
 * ROM's, so that early output stays at the rate the ROM already set up.
 */
static
sccinit()
{

	sccup = 1;
	outb(WR4, 0x4c);		/* x16 clock, 1 stop bit, no parity */
	outb(WR11, 0x56);		/* Rx & Tx clocks from the BRG */
	outb(WR12, 0x11);		/* time constant 17 -> 9600 baud */
	outb(WR13, 0x00);
	outb(WR14, 0x03);		/* BRG source = PCLK, BRG enable */
	outb(WR3, 0xc1);		/* Rx 8 bits/char, Rx enable */
	outb(WR5, 0xea);		/* DTR, Tx 8 bits/char, Tx enable, RTS */
}

/*
 * Startup output on the screen of a machine that has one.
 *
 * The console of such a machine is a loadable driver (drvl[] slot 8, installed
 * by /etc/load out of init), so between `start' and init nothing the kernel
 * prints can reach it through a tty.  The boot ROM's character routine can:
 * segment 0 still maps the ROM at that point, and the routine dispatches on
 * the console the ROM itself selected -- the hi-res bitmap, the lo-res text
 * card, or the serial line -- so it draws on whichever screen is fitted, in
 * the font and at the cursor the BIOS diagnostics and kboot left behind.
 *
 * ROMPUTC is that routine, called as a far function pointer.  The ROM is MWC C
 * with our argument convention and, like this kernel, addresses frames and
 * locals through SS, so the argument it reads at SS:10 is the word pushed
 * here.  It expands '\n' to CR+LF itself, which means the CR putchar() sends
 * ahead of a newline arrives as a second column reset rather than a blank
 * line.
 *
 * ROMHRF and ROMLRF are the two bytes the ROM's dispatcher tests, the two past
 * the end of romconf: non-zero at ROMHRF selects the bitmap renderer, at ROMLRF
 * the text one, neither the serial port.  Reading them is what keeps a serial
 * console from being written twice -- there the ROM would drive the same SCC
 * that putchar() drives four lines further down, so romcput() stands aside and
 * the machine's output is byte-for-byte what it always was.
 *
 * Both renderers reach the screen through virtual segments BMS and BMS+1, and
 * commodore() has since pointed those at BMPHYS, so their bases are put back
 * across the call and restored after it.  Only the base moves: the attributes
 * and limits md.s left on those two segments are the ROM's own values.
 *
 * batflag bounds the whole thing.  The ROM keeps its console state -- the two
 * selection bytes and the cursor -- in the top 7KB of memory, which is where
 * the u-area allocator starts handing out pages as soon as main() creates its
 * first process; batflag is set immediately before that happens and never
 * cleared.  While it is clear the state is intact and the clock interrupt is
 * still a no-op, so the ROM cannot be re-entered from a handler either.
 */
#define	ROMPUTC	0x00000FC2L	/* ROM putchar, segment 0 offset 0x0FC2 */
#define	ROMHRF	0x010017FFL	/* Hi-res screen is the ROM's console */
#define	ROMLRF	0x01001800L	/* Lo-res screen is the ROM's console */
#define	HRFB	0x003E0000L	/* Hi-res bitmap, 100KB from here */
#define	LRFB	0x00370000L	/* Lo-res character/attribute RAM */

#define	CSNEW	0		/* Selection not read yet */
#define	CSNONE	1		/* Serial: putchar() already reaches it */
#define	CSFB	2		/* A screen: the ROM renders on it */

static	int	(*romputc)() = (int (*)())ROMPUTC;
static	int	romcsel;	/* Which console the ROM selected */
static	paddr_t	romcfb;		/* Physical base of that screen */

/*
 * Print a character on the screen the ROM selected, if it selected one.
 */
static
romcput(c)
register int c;
{

	if (romcsel == CSNEW) {
		if (pgetb((char *)ROMHRF) != 0)
			romcfb = HRFB;
		else if (pgetb((char *)ROMLRF) != 0)
			romcfb = LRFB;
		else {
			romcsel = CSNONE;
			return;
		}
		romcsel = CSFB;
	}
	if (romcsel != CSFB)
		return;
	pfix(BMS, romcfb);
	pfix(BMS+1, romcfb+0x00010000L);
	(*romputc)(c);
	pfix(BMS, BMPHYS);
	pfix(BMS+1, BMPHYS+0x00010000L);
}

/*
 * Print a character on the console.
 */
putchar(c)
register int c;
{
	register int s;

	if (c == '\n')
		putchar('\r');
	if (batflag == 0)
		romcput(c);
	s = sphi();
	if (sccup == 0)
		sccinit();
	while ((inb(RR0)&TxEMPTY) == 0)
		;
	outb(WR8, c);
	spl(s);
}

/*
 * Get a character from the
 * console. Echo it. Map carriage
 * return into newline.
 */
getchar()
{
	register c;

	if (sccup == 0)
		sccinit();
	while ((inb(RR0)&RxAVAIL) == 0)
		;
	if ((c = inb(RR8)&0x7F) == '\r')
		c = '\n';
	putchar(c);
	return (c);
}
