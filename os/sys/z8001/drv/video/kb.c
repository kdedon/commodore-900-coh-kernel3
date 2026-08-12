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
 * Keyboard/display driver.
 * 8086/8088 Coherent, IBM PC.
 */
#include <coherent.h>

#if	0
#include <i8086.h>
#endif

#include <con.h>
#include <errno.h>
#include <stat.h>
#include <tty.h>
#include <uproc.h>
#include <signal.h>
#include <timeout.h>
#include <sched.h>

#define	SPC	0376			/* Special encoding */
#define XXX	0377			/* Non-character */
#define	KBDATA	0x60			/* Keyboard data */
#define	KBCTRL	0x61			/* Keyboard control */
#define	KBFLAG	0x80			/* Keyboard reset flag */

#define	KEYUP	0x80			/* Key up change */
#define	KEYSC	0x7F			/* Key scan code mask */
#define	LSHIFT	0x2A-1			/* Left shift key */
#define LSHIFTA 0x2B-1			/* Alternate left-shift key */
#define	RSHIFT	0x36-1			/* Right shift key */
#define	CTRL	0x1D-1			/* Control key */
#define	ALT	0x38-1			/* Alt key */
#define	CAPLOCK	0x3A-1			/* Caps lock key */
#define	NUMLOCK	0x45-1			/* Numeric lock key */
#define	DELETE	0x53-1			/* Del, as in CTRL-ALT-DEL */
#define BACKSP	0x0E-1			/* Back space */
#define SCRLOCK	0x46-1			/* Scroll lock */

/* Shift flags */
#define	SRS	0x01			/* Right shift key on */
#define	SLS	0x02			/* Left shift key on */
#define CTS	0x04			/* Ctrl key on */
#define ALS	0x08			/* Alt key on */
#define CPLS	0x10			/* Caps lock on */
#define NMLS	0x20			/* Num lock on */
#define AKPS	0x40			/* Alternate keypad shift */
#define SHFT	0x80			/* Shift key flag */

/* Function key information */
#define	NFKEY	40			/* Number of settable functions */
#define	NFCHAR	600			/* Number of characters settable */
#define	NFBUF	(NFKEY*2+NFCHAR+1)	/* Size of buffer */

/*
 * Functions.
 */
int	isrint();
int	istime();
int	mmstart();
int	isopen();
int	isclose();
int	isread();
int	iswrite();
int	isioctl();
int	isload();
int	isuload();
int	nulldev();
int	nonedev();

/* External from "mm.c" */
extern char mminsmode;	/* insert mode state */

/*
 * Configuration table.
 */
CON iscon ={
	DFCHR,				/* Flags */
	15,				/* Major index */
	isopen,				/* Open */
	isclose,			/* Close */
	nulldev,			/* Block */
	isread,				/* Read */
	iswrite,			/* Write */
	isioctl,			/* Ioctl */
	nulldev,			/* Powerfail */
	nulldev,			/* Timeout */
	isload,				/* Load */
	isuload				/* Unload */
};

/*
 * Terminal structure.
 */
TTY	istty = {
	{0}, {0}, 0, mmstart, NULL, 0, 0
};

/*
 * State variables.
 */
static	char	shift;			/* Overall shift state */
static  char	lshift = LSHIFT;	/* Left shift alternate state */
static	char	isfbuf[NFBUF];		/* Function key values */
static	char	*isfval[NFKEY];		/* Function key string pointers */

/*
 * Tables for converting key code to ASCII.
 * lmaptab specifies unshifted conversion,
 * umaptab specifies shifted conversion,
 * smaptab specifies the shift states which are active.
 * An entry of XXX says the key is dead.
 * An entry of SPC requires further processing.
 *
 * Key codes:
 *	ESC .. <- == 1 .. 14
 *	-> .. \n == 15 .. 28
 *	CTRL .. ` == 29 .. 41
 *	^Shift .. PrtSc == 42 .. 55
 * 	ALT .. CapsLock == 56 .. 58
 *	F1 .. F10 == 59 .. 68
 *	NumLock .. Del == 69 .. 83
 */
static unsigned char lmaptab[] ={
	     '\33',  '1',  '2',  '3',  '4',  '5',  '6',		/* 1 - 7 */
	 '7',  '8',  '9',  '0',  '-',  '=', '\b', '\t',		/* 8 - 15 */
	 'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',		/* 16 - 23 */
	 'o',  'p',  '[',  ']', '\r',  XXX,  'a',  's',		/* 24 - 31 */
	 'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',		/* 32 - 39 */
	 '\'', '`',  XXX,  '\\',  'z',  'x',  'c',  'v',	/* 40 - 47 */
	 'b',  'n',  'm',  ',',  '.',  '/',  XXX,  '*',		/* 48 - 55 */
	 XXX,  ' ',  XXX,  SPC,  SPC,  SPC,  SPC,  SPC,		/* 56 - 63 */
	 SPC,  SPC,  SPC,  SPC,  SPC,  SPC,  SPC,  SPC,		/* 64 - 71 */
	 SPC,  SPC,  '-',  SPC,  SPC,  SPC,  '+',  SPC,		/* 72 - 79 */
	 SPC,  SPC,  SPC,  SPC					/* 80 - 83 */
};

static unsigned char umaptab[] ={
	     '\33',  '!',  '@',  '#',  '$',  '%',  '^',		/* 1 - 7 */
	 '&',  '*',  '(',  ')',  '_',  '+', '\b', '\t',		/* 8 - 15 */
	 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',		/* 16 - 23 */
	 'O',  'P',  '{',  '}', '\r',  XXX,  'A',  'S',		/* 24 - 31 */
	 'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',		/* 32 - 39 */
	 '"',  '~',  XXX,  '|',  'Z',  'X',  'C',  'V',		/* 40 - 47 */
	 'B',  'N',  'M',  '<',  '>',  '?',  XXX,  '*',		/* 48 - 55 */
	 XXX,  ' ',  XXX,  SPC,  SPC,  SPC,  SPC,  SPC,		/* 56 - 63 */
	 SPC,  SPC,  SPC,  SPC,  SPC,  SPC,  SPC,  SPC,		/* 64 - 71 */
	 SPC,  SPC,  '-',  SPC,  SPC,  SPC,  '+',  SPC,		/* 72 - 79 */
	 SPC,  SPC,  SPC,  SPC					/* 80 - 83 */
};

#define SS0	0			/* No shift */
#define SS1	(SLS|SRS|CTS)		/* Shift, Ctrl */
#define SES	(SLS|SRS)		/* Shift */
#define LET	(SLS|SRS|CPLS|CTS)	/* Shift, Caps, Ctrl */
#define KEY	(SLS|SRS|NMLS|AKPS)	/* Shift, Num, Alt keypad */

static unsigned char smaptab[] ={
	       SS0,  SES,  SS1,  SES,  SES,  SES,  SS1,		/* 1 - 7 */
	 SES,  SES,  SES,  SES,  SS1,  SES,  CTS,  SS0,		/* 8 - 15 */
	 LET,  LET,  LET,  LET,  LET,  LET,  LET,  LET,		/* 16 - 23 */
	 LET,  LET,  SS1,  SS1,  CTS, SHFT,  LET,  LET,		/* 24 - 31 */
	 LET,  LET,  LET,  LET,  LET,  LET,  LET,  SES,		/* 32 - 39 */
	 SES,  SS1, SHFT,  SS1,  LET,  LET,  LET,  LET,		/* 40 - 47 */
	 LET,  LET,  LET,  SES,  SES,  SES, SHFT,  SES,		/* 48 - 55 */
	SHFT,  SS1, SHFT,  SS0,  SS0,  SS0,  SS0,  SS0,		/* 56 - 63 */
	 SS0,  SS0,  SS0,  SS0,  SS0, SHFT,  KEY,  KEY,		/* 64 - 71 */
	 KEY,  KEY,  SS0,  KEY,  KEY,  KEY,  SS0,  KEY,		/* 72 - 79 */
	 KEY,  KEY,  KEY,  KEY					/* 80 - 83 */
};

/*
 * Load entry point.
 *  Do reset the keyboard because it gets terribly munged
 *  if you type during the boot.
 */
isload()
{
	register int i;

#if	0
	outb(KBCTRL, 0x0C);		/* Clock low */
	for (i = 10582; --i >= 0; );	/* For 20ms */
	outb(KBCTRL, 0xCC);		/* Clock high */
	for (i = 0; --i != 0; )
		;
	i = inb(KBDATA);
	outb(KBCTRL, 0xCC);		/* Clear keyboard */
	outb(KBCTRL, 0x4D);		/* Enable keyboard */
	setivec(1, isrint);
#endif
	mmload();
}

/*
 * Unload entry point.
 */
isuload()
{
#if 0
	clrivec(1);
#endif
}

/*
 * Default function key strings (terminated by -1 [\377])
 */
static char *deffuncs[] = {
	"\33S\377",	/* F1 */
	"\33T\377",	/* F2 */
	"\33U\377",	/* F3 */
	"\33V\377", 	/* F4 */
	"\33W\377",	/* F5 */
	"\33P\377",	/* F6  (BLUE) */
	"\33Q\377",	/* F7  (RED)  */
	"\33R\377",	/* F8  (GRAY) */
	"\000\377",	/* F9  (null) */
	"\177\377"	/* F10 (rubout) */
};

/*
 * Open routine.
 */
isopen(dev, m)
dev_t dev;
int m;
{
	register int s;

	if (minor(dev) != 0) {
		u.u_error = ENXIO;
		return;
	}
	if ((istty.t_flags&T_EXCL)!=0 && super()==0) {
		u.u_error = ENODEV;
		return;
	}
	ttsetgrp(&istty, dev, m);

	s = sphi();
	if (istty.t_open++ == 0)
	{  
#if	0
	   initkeys();	 /* init function keys */
#endif
	   istty.t_flags = T_CARR;  /* indicate "carrier" */
	   ttopen(&istty);
	}
	spl(s);
}

/* Init function keys */
initkeys()
{	register int i;
	register char *cp1, *cp2;

	for (i=0; i<NFKEY; i++)
	    isfval[i] = -1;	    /* clear function key buffer */
	cp1 = deffuncs;	    	    /* pointer to key init string */
	cp2 = isfbuf;	      	    /* pointer to key buffer */   
	for (i=0; i<NFKEY; i++)
	{  isfval[i] = cp2;	    /* save pointer to key string */
	   cp1 = deffuncs[i];       /* get init string pointer */
	   while ((*cp2++ = *cp1++) != -1)  /* copy key data */
	     if (cp2 >= &isfbuf[NFBUF-3])   /* overflow? */
	        return;
	}
}

/*
 * Close a tty.
 */
isclose(dev)
{
	register int s;

	s = sphi();
	if (--istty.t_open == 0)
	{  	s = sphi();
		ttclose(&istty);
		spl(s);
	}
}

/*
 * Read routine.
 */
isread(dev, iop)
dev_t dev;
IO *iop;
{
	ttread(&istty, iop, SFCW);
}

/*
 * Write routine.
 */
iswrite(dev, iop)
dev_t dev;
IO *iop;
{
	ttwrite(&istty, iop, SFCW);
}

/*
 * Ioctl routine.
 */
isioctl(dev, com, vec)
dev_t dev;
struct sgttyb *vec;
{
	register int s;

	switch(com) {
	case TIOCSETF:
	case TIOCGETF:
#if	0
		isfunction(com, (char *)vec);
#endif
		return;
	case TIOCSHIFT:   /* switch left-SHIFT and "\" */
		lshift = LSHIFTA;    /* alternate values */
		lmaptab[41] = '\\';
		lmaptab[42] = XXX;
		umaptab[41] = '|';
		umaptab[42] = XXX;
		smaptab[41] = SS1;
		smaptab[42] = SHFT;
		return;
	case TIOCCSHIFT:  /* normal (default) left-SHIFT and "\" */
		lshift = LSHIFT;     /* normal values */
		lmaptab[41] = XXX;
		lmaptab[42] = '\\';
		umaptab[41] = XXX;
		umaptab[42] = '|';
		smaptab[41] = SHFT;
		smaptab[42] = SS1;
		return;
	case TIOVGETB:
	case TIOVPUTB:
		while (istty.t_oq.cq_cc != 0) {
			s = sphi();
			if (istty.t_oq.cq_cc != 0) {
				istty.t_flags |= T_DRAIN;
				sleep((char *)&istty.t_oq,
					CVTTOUT, IVTTOUT, SVTTOUT);
			}
			spl(s);
			if (SELF->p_ssig && nondsig()) {
			   u.u_error = EINTR;
			   return;
			}
		}
		mmioctl(com, vec);
		return;
	}
	s = sphi();
	ttioctl(&istty, com, vec);
	spl(s);
}

/*
 * Set and receive the function keys.
 */
isfunction(c, v)
int c;
char *v;
{
	register char *cp;
	register int i;

	if (c == TIOCGETF) {
		for (cp = isfbuf; cp < &isfbuf[NFBUF]; cp++)
		    putubd(v++, *cp);
	} else {
		for (i=0; i<NFKEY; i++)		/* zap current settings */
			isfval[i] = -1;
		cp = isfbuf;			/* pointer to key buffer */
		for (i=0; i<NFKEY; i++) {
			isfval[i] = cp;	        /* save pointer to key string */
			while ((*cp++ = getubd(v++)) != -1)  /* copy key data */
				if (cp >= &isfbuf[NFBUF-3])  /* overflow? */
					return;
		}
	}
}

/*
 * Receive interrupt.
 */
isrint()
{
	register int	c;
	register int	s;
	register int	r;

	/*
	 * Pull character from the data
	 * port. Pulse the KBFLAG in the control
	 * port to reset the data buffer.
	 */
	r = inb(KBDATA) & 0xFF;
	c = inb(KBCTRL);
	outb(KBCTRL, c|KBFLAG);
	outb(KBCTRL, c);
	if (r == 0xFF)
		return;	/* Overrun */
	c = (r & KEYSC) - 1;

	/*
	 * Check for reset.
	 */
#if	0
	if ((r&KEYUP) == 0 && c == DELETE && (shift&(CTS|ALS)) == (CTS|ALS))
		boot();
#endif
	/*
	 * Track "shift" keys.
	 */
	s = smaptab[c];
	if (s&SHFT) {
		if (r&KEYUP) {			/* "shift" released */
			if (c == RSHIFT)
				shift &= ~SRS;
			else if (c == lshift)
				shift &= ~SLS;
			else if (c == CTRL)
				shift &= ~CTS;
			else if (c == ALT)
				shift &= ~ALS;
		} else {			/* "shift" pressed */
			if (c == lshift)
				shift |= SLS;
			else if (c == RSHIFT)
				shift |= SRS;
			else if (c == CTRL)
				shift |= CTS;
			else if (c == ALT)
				shift |= ALS;
			else if (c == CAPLOCK)  
				shift ^= CPLS;	/* toggle cap lock */
			else if (c == NUMLOCK)  
				shift ^= NMLS;	/* toggle num lock */
		}
		return;
	}

	/*
	 * No other key up codes of interest.
	 */
	if (r&KEYUP)
		return;

	/*
	 * If the tty is not open the character is
	 * just tossed away.
	 */
	if (istty.t_open == 0)
		return;

	/*
	 * Map character, based on the
	 * current state of the shift, control,
	 * meta and lock flags.
	 */
	if (shift & CTS) {
		if (s == CTS)			/* Map Ctrl (BS | NL) */
			c = (c == BACKSP) ? 0x7F : 0x0A;  
		else if (s==SS1 || s==LET)	/* Normal Ctrl map */
			c = umaptab[c]&0x1F;	/* Clear bits 5-6 */
		else				
			return;			/* Ignore this char */
	} else if (s &= shift) {
		if (shift & SES) {		 /* if shift on */
			if (s & (CPLS|NMLS))     /* if caps/num lock */
				c = lmaptab[c];  /* use unshifted */
			else
				c = umaptab[c];	 /* use shifted */
		} else {			 /* if shift not on */
			if (s & (CPLS|NMLS))     /* if caps/num lock */
				c = umaptab[c];	 /* use shifted */
			else
				c = lmaptab[c];	 /* use unshifted */
		}
	} else					 
		c = lmaptab[c];			 /* use unshifted */

	/*
	 * Act on character.
	 */
	if (c == XXX)				
		return;				 /* char to ignore */
	if (c != SPC) {				 /* not special char? */
		if (shift & ALS)		 /* ALT (meta bit)? */
		   c |= 0x80;			 /* set meta */
		s = sphi();
		ttin(&istty, c);		 /* send the char */
		spl(s);
	} else
		isspecial(r);			 /* special chars */
}

/*
 * Handle special input sequences.
 * The character passed is the key number.
 *
 * The keypad is translated by the following table,
 * the first entry is the normal sequence, the second the shifted,
 * and the third the alternate keypad sequence.
 */
static char *keypad[][3] = {
	{ "7",	0,	 "\33?w" },	/* 71 (special shifted handling) */
	{ "8",	"\33A",	 "\33?x" },	/* 72 */
	{ "9",	"\33N",  "\33?y" },	/* 73 */
	{ "4",	"\33D",	 "\33?t" },	/* 75 */
	{ "5",	"\33H",  "\33?u" },	/* 76 */
	{ "6",	"\33C",  "\33?v" },	/* 77 */
	{ "1",	"\33L",	 "\33?q" },	/* 79 */
	{ "2",	"\33B",  "\33?r" },	/* 80 */
	{ "3",	"\33M",	 "\33?s" },	/* 81 */
	{ "0",	"0", 	 "\33?p" },	/* 82 */
	{ ".",	".",	 "\33?n" }	/* 83 */
};

isspecial(c)
int c;
{
	register char *cp;
	register int s;

	cp = 0;

	switch (c) {
	case 59: case 60: case 61: case 62: case 63:	/* Function keys */
	case 64: case 65: case 66: case 67: case 68:
		if (shift & SES)
			if (shift & ALS)
				cp = isfval[c - 59 +30];
			else
				cp = isfval[c - 59 + 10];
		else if (shift & ALS)
			cp = isfval[c - 59 + 20];
		else
			cp = isfval[c-59];  /* offset to function string */
		while (*cp != -1)   /* copy chars up to -1 to istty */
		{   s = sphi();
		    ttin(&istty, *cp++);
		    spl(s);
		}		
		return;

	case 70:		/* Scroll Lock -- stop/start output */
	{
		static char cbuf[2];

		cp = &cbuf[0];  /* working buffer */
		if (istty.t_flags & T_STOP)	       /* output stopped? */
		   cbuf[0] = istty.t_tchars.t_startc;  /* start output */
		else
		   cbuf[0] = istty.t_tchars.t_stopc;   /* stop output */
		break;
	}

	case 79:		/* 1/End */
	case 80:		/* 2/DOWN */
	case 81:		/* 3/PgDn */
	case 82:		/* 0/Ins */
	case 83:		/* ./Del */
		--c;		/* adjust code */
	case 75:		/* 4/LEFT */
	case 76:		/* 5 */
	case 77:		/* 6/RIGHT */
		--c;		/* adjust code */
	case 71:		/* 7/Home/Clear */
	case 72:		/* 8/UP */
	case 73:		/* 9/PgUp */
		s = 0;			/* start off with normal keypad */
		if (shift&NMLS)		/* num lock? */
			s = 1;		/* set shift pad */
		if (shift&SES)		/* shift? */
			s ^= 1;		/* toggle shift pad */
		if (s == 0 && (shift&AKPS))  /* (!shifted) and alternate pad? */
			s = 2;		/* set alternate pad */		
		cp = keypad[c-71][s];   /* get keypad value */
		if (cp == 0)		/* shift-keypad-7 ? (special) */
		{  if (mminsmode) 	/* in insert mode? */
		      cp = "\33O";	/* send exit insert sequence */			
		   else
		      cp = "\33@";	/* send enter insert sequence */
		}
		break;
	}
	if (cp)				/* send string */
		while (*cp) {
			s = sphi();
			ttin(&istty, (*cp++&0377));
			spl(s);
		}
}

ismmfunc(c)
register int c;
{
	switch (c) {
	case 't':	/* Enter numlock */
		shift |= NMLS; 
		break;
	case 'u':	/* Leave numlock */
		shift &= ~NMLS; 
		break;
	case '=':	/* Enter alternate keypad */
		shift |= AKPS; 
		break;
	case '>':	/* Exit alternate keypad */
		shift &= ~AKPS; 
		break;
	case 'z':	/* Reset terminal */
		shift = 0;
		break;
	}
}


