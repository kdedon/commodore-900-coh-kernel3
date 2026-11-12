/* SPDX-License-Identifier: MIT */
/*
 * The LC colour card: an 8568 VDC with 64 KB of its own RAM, reached through
 * two byte ports in standard I/O space.
 */
#ifndef	LCVDC_H
#define	LCVDC_H

#define	LCID	0x01FF		/* card ID byte, read only */
#define	LCID64	0xC1		/* 64 KB card */
#define	LCADDR	0x0181		/* write: register index; read: status */
#define	LCDATA	0x0183		/* selected register */

#define	LCREADY	0x80		/* status: chip can take a data access */
#define	LCZERO	0x18		/* status: bits that always read 0 */
#define	LCREV	0x07		/* status: chip version */

#define	LCPOLL	2000		/* ready polls before giving up */

/* VRAM layout */
#define	LCTEXT	0x0000		/* 80x25 character codes */
#define	LCATTR	0x0800		/* 80x25 attributes */
#define	LCFONT	0x2000		/* 256 glyphs, 16 bytes each */
#define	LCSCR	0x4000		/* scratch for overlapping copies */

/* Registers */
#define	LCR_CURST	10	/* cursor mode and start line */
#define	LCR_CUREND	11
#define	LCR_DISPH	12	/* display start */
#define	LCR_CURH	14	/* cursor address */
#define	LCR_UPDH	18	/* update address */
#define	LCR_UPDL	19
#define	LCR_ATTRH	20	/* attribute start */
#define	LCR_VSCROLL	24	/* b7 copy, b6 reverse, b5 blink rate */
#define	LCR_COUNT	30	/* block length; writing it starts the block */
#define	LCR_DATA	31	/* VRAM at the update address, which advances */
#define	LCR_SRCH	32	/* block copy source */
#define	LCR_SRCL	33
#define	LCR_NREG	37

#define	LCCOPY	0x80		/* R24: block copy, else fill */

/* Attribute bits */
#define	LCA_ALT		0x80
#define	LCA_REV		0x40
#define	LCA_UNDER	0x20
#define	LCA_BLINK	0x10
#define	LCA_RED		0x08
#define	LCA_GREEN	0x04
#define	LCA_BLUE	0x02
#define	LCA_INT		0x01

#endif
