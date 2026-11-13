/* SPDX-License-Identifier: MIT */
/*
 * The LC card's graphics minor, /dev/lcvdc.  read, write and lseek reach the
 * chip's 64 KB of RAM byte for byte; the ioctls below set modes, run the
 * chip's block fill and copy, stand the text console off and back, load the
 * text glyphs, and keep a RAM shadow of the bitmap that the window system
 * draws into and flushes.
 *
 * In every mode the console's text, attributes and glyphs stay at the bottom
 * of the RAM, below LCVBMAP, so text and graphics survive a switch either
 * way.  The bitmap starts at LCVBMAP, one bit a pixel, bit 7 leftmost,
 * lm_stride bytes a line; colour attributes, where the mode has them, are
 * one byte a 8x8 cell at lm_attr, foreground in bits 3-0 as RGBI.
 */
#ifndef	LCIO_H
#define	LCIO_H	LCIO_H

#define	LCVMINOR	1		/* minor of /dev/lcvdc */
#define	LCVBMAP		0x4000		/* bitmap start in every mode */
#define	LCVNREG		37		/* registers R0-R36 */

/* Modes */
#define	LCM_TEXT	0		/* 80x25 text: the console */
#define	LCM_MONO	1		/* 640x200, one colour */
#define	LCM_COLOUR	2		/* 640x200, 8x8 colour cells */
#define	LCM_I400	3		/* 640x400 interlaced, one colour */
#define	LCM_I480	4		/* 640x480 interlaced, one colour */
#define	LCM_NMODE	5
#define	LCM_KEEP	0x100		/* or'd in: leave the bitmap uncleared */

#define	LCIOCINFO	('v'<<8|1)	/* struct lcinfo out */
#define	LCIOCMODE	('v'<<8|2)	/* struct lcinfo: lm_mode in, all out */
#define	LCIOCGREG	('v'<<8|3)	/* struct lcreg in/out */
#define	LCIOCSREG	('v'<<8|4)	/* struct lcreg in */
#define	LCIOCFILL	('v'<<8|5)	/* struct lcblk in */
#define	LCIOCCOPY	('v'<<8|6)	/* struct lcblk in, ascending only */
#define	LCIOCSTOP	('v'<<8|7)	/* stand the console off */
#define	LCIOCSTART	('v'<<8|8)	/* text mode, console back as it was */
#define	LCIOCSFONT	('v'<<8|9)	/* struct lcfont in */
#define	LCIOCGFONT	('v'<<8|10)	/* struct lcfont in, glyphs out */
#define	LCIOCRFONT	('v'<<8|11)	/* restore the console's glyphs */
#define	LCIOCSHADOW	('v'<<8|12)	/* struct lcinfo; map the shadow */
#define	LCIOCUNSHADOW	('v'<<8|13)	/* unmap and free the shadow */
#define	LCIOCFLUSH	('v'<<8|14)	/* struct lcflush in */

struct lcinfo {
	unsigned short	lm_ram;		/* KB of chip RAM */
	unsigned short	lm_chip;	/* status version bits */
	unsigned short	lm_mode;	/* LCM_* */
	unsigned short	lm_width;	/* pixels, or columns in text */
	unsigned short	lm_height;	/* lines, or rows in text */
	unsigned short	lm_stride;	/* bytes a bitmap line */
	unsigned short	lm_bitmap;	/* bitmap start, 0 in text */
	unsigned short	lm_attr;	/* colour cells, 0 if none */
	unsigned short	lm_stood;	/* the console is stood off */
	unsigned short	lm_shadow;	/* bytes of shadow mapped, 0 if none */
	unsigned short	lm_shstride;	/* its bytes a line; in to LCIOCSHADOW */
	char		*lm_shbase;	/* where the shadow is mapped */
};

struct lcreg {
	unsigned short	lg_reg;
	unsigned short	lg_val;
};

/* Fill: lb_count bytes of lb_val at lb_dst.  Copy: from lb_src. */
struct lcblk {
	unsigned short	lb_dst;
	unsigned short	lb_src;
	unsigned short	lb_val;
	unsigned short	lb_count;	/* 0 means 65536 */
};

/*
 * Glyphs, LCGB bytes each, top line first, bit 7 leftmost.  The text mode
 * shows the first LCGH lines of each.
 */
#define	LCGB		16
#define	LCGH		8
#define	LCNGLYPH	256

struct lcfont {
	unsigned short	lf_first;
	unsigned short	lf_count;
	unsigned char	*lf_bits;	/* lf_count*LCGB bytes */
};

/*
 * A rectangle of the shadow, in bytes across and lines down, to copy to the
 * bitmap.  Out-of-range parts are clipped.  The shadow may be wider than the
 * bitmap (lm_shstride 128 gives the hi-res board's line length); only the
 * first 80 bytes of each of its lines are shown.
 */
struct lcrect {
	unsigned short	lr_x;
	unsigned short	lr_y;
	unsigned short	lr_w;
	unsigned short	lr_h;
};

#define	LCNRECT		32

struct lcflush {
	unsigned short	lu_n;		/* rectangles, at most LCNRECT */
	struct lcrect	*lu_r;
};

#endif
