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
#include <romconf.h>

#define	RxAVAIL	0x01		/* character available */
#define	TxEMPTY	0x04		/* transmit buffer empty */
#define	WR0	0x0101		/* command register */
#define	RR0	0x0101		/* Tx & Rx status */
#define	RR1	0x0103		/* special receive status, All Sent */
#define	ALLSENT	0x01		/* RR1: last bit has left */
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
	alwait(RR1, ALLSENT);		/* the ROM may still be sending */
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
 * ROMPUTC is that routine in V1.0, called as a far function pointer.  A later
 * ROM has a service table at ROMJT whose first slot jumps to it, and whose
 * header points at the addresses of the ROM's variables.  The ROM is MWC C
 * with our argument convention and, like this kernel, addresses frames and
 * locals through SS, so the argument it reads at SS:10 is the word pushed
 * here.  It expands '\n' to CR+LF itself, which means the CR putchar() sends
 * ahead of a newline arrives as a second column reset rather than a blank
 * line.
 *
 * The ROM's dispatcher tests two bytes: rom_hires, the last of romconf, and
 * the lo-res flag, ROMLRF in V1.0.  Non-zero rom_hires selects the bitmap
 * renderer, the lo-res flag the text one, neither the serial port.  Reading
 * them is what keeps a serial console from being written twice -- there the
 * ROM would drive the same SCC that putchar() drives four lines further down,
 * so romcput() stands aside and the machine's output is byte-for-byte what it
 * always was.
 *
 * Both renderers reach the screen through virtual segments BMS and BMS+1, and
 * commodore() has since pointed those at bmphys, so their bases are put back
 * across the call and restored after it.  Only the base moves: the attributes
 * and limits md.s left on those two segments are the ROM's own values.
 *
 * The ROM does not wrap: a line longer than the screen (64 columns on the
 * bitmap, 80 on the text card) piles up in its last column, so romcput()
 * breaks it with a newline.  The ROM's renderer is not reentrant and the two
 * segments are moved under it, so interrupts are held off across each call.
 * The lo-res cursor the ROM leaves is kept in romlrpos for the text console
 * driver, which carries on below it instead of clearing the screen.
 *
 * batflag bounds the whole thing.  The ROM keeps its console state -- the two
 * selection bytes and the cursor -- at the top of memory (7KB, 12KB on newer
 * ROMs), where the u-area allocator starts handing out pages as soon as main()
 * creates its first process; batflag is set immediately before that happens
 * and never cleared.  While it is clear the state is intact and the clock interrupt is
 * still a no-op, so the ROM cannot be re-entered from a handler either.
 */
#define	ROMPUTC	0x00000FC2L	/* ROM putchar, segment 0 offset 0x0FC2 */
#define	ROMLRF	0x01001800L	/* Lo-res screen is the ROM's console */
#define	ROMJT	0x0000029EL	/* ROM service table */
#define	JTMAGIC	0x43394A54L	/* "C9JT" */
#define	JTVARS	8		/* Offset of its variable list pointer */
#define	JTSVC	16		/* Offset of its first service slot */
#define	JTCONLR	9		/* Lo-res flag in the variable list */
#define	JTCURS	8		/* Video cursor in the variable list */
#define	ROMLRC	0x01000614L	/* V1.0's lo-res cursor: row, then column */
#define	HRFB	0x003E0000L	/* Hi-res bitmap, 100KB from here */
#define	LRFB	0x00370000L	/* Lo-res character/attribute RAM */

#define	CSNEW	0		/* Selection not read yet */
#define	CSNONE	1		/* Serial: putchar() already reaches it */
#define	CSFB	2		/* A screen: the ROM renders on it */

static	int	(*romputc)() = (int (*)())ROMPUTC;
static	int	romcsel;	/* Which console the ROM selected */
static	paddr_t	romcfb;		/* Physical base of that screen */
static	int	romcw;		/* Its width in columns */
static	int	romccol;	/* Characters on the current line */
static	char	*romlrc;	/* The ROM's lo-res cursor, if that is the screen */
int	romlrpos = -1;		/* Lo-res cursor the ROM left, row*80+column */
extern	paddr_t	bmphys;		/* Where BMS points between calls */
extern	struct romconf romconf;

/*
 * Call the ROM's character routine with its screen mapped.
 */
static
romcall(c)
int c;
{
	register int s;

	s = sphi();
	pfix(BMS, romcfb);
	pfix(BMS+1, romcfb+0x00010000L);
	(*romputc)(c);
	pfix(BMS, bmphys);
	pfix(BMS+1, bmphys+0x00010000L);
	if (romlrc != NULL)
		romlrpos = pgetw(romlrc)*80 + pgetw(romlrc+2);
	spl(s);
}

/*
 * Print a character on the screen the ROM selected, if it selected one.
 */
static
romcput(c)
register int c;
{
	register char *lrf, *lrc;

	if (romcsel == CSNEW) {
		lrf = (char *)ROMLRF;
		lrc = (char *)ROMLRC;
		if ((long)pgetp((char *)ROMJT) == JTMAGIC) {
			romputc = (int (*)())(ROMJT+JTSVC);
			lrf = pgetp((char *)ROMJT+JTVARS);
			lrc = pgetp(lrf + JTCURS*sizeof(char *));
			lrf = pgetp(lrf + JTCONLR*sizeof(char *));
		}
		if (romconf.rom_hires != 0) {
			romcfb = HRFB;
			romcw = 64;
		} else if (pgetb(lrf) != 0) {
			romcfb = LRFB;
			romcw = 80;
			romlrc = lrc;
		} else {
			romcsel = CSNONE;
			return;
		}
		romcsel = CSFB;
	}
	if (romcsel != CSFB)
		return;
	if (c == '\n')
		romccol = 0;
	else if (c >= ' ' && c < 0177 && romccol++ == romcw) {
		romcall('\n');
		romccol = 1;
	}
	romcall(c);
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
