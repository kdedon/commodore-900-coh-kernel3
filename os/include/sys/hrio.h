/*
**	Hi-res console private ioctls: the loadable character set and the
**	frame-buffer stand-off.  Shared by the driver and by user programs.
**
**	Glyph format, as plotc() reads it: one `unsigned short' per scan line,
**	the TOP HRGW bits are the pixels, most significant bit leftmost,
**	1 = ink, HRGH lines top to bottom.  A glyph is HRGH shorts = HRGB
**	bytes.
**
**	Slot numbering follows the driver's two-bank character decode: the byte
**	0x20+n selects slot n for n < HRLOW, and the byte 0xa0+(n-HRLOW)
**	selects slot n for n >= HRLOW.  Slots below HRLOW are the console's own
**	alphabet and are read-only; the high bank is the loadable one.
*/

#ifndef	HRIO_H
#define	HRIO_H	HRIO_H

#define	HRGW		12		/* glyph width in pixels */
#define	HRGH		25		/* glyph height in scan lines */
#define	HRGB		(HRGH*2)	/* bytes of bits per glyph */
#define	HRLOW		96		/* slots in the read-only bank */
#define	HRHIGH		96		/* slots in the loadable bank */
#define	HRNGLYPH	(HRLOW+HRHIGH)	/* slots the decode reaches */

#define	HRCODE(n)	((n) < HRLOW ? 0x20+(n) : 0xa0+((n)-HRLOW))

#define	HRIOCSFONT	('h'<<8|1)	/* load glyphs into the high bank */
#define	HRIOCGFONT	('h'<<8|2)	/* read glyphs back, either bank */
#define	HRIOCRFONT	('h'<<8|3)	/* restore the built-in high bank */
#define	HRIOCSTOP	('h'<<8|4)	/* stand off the frame buffer */
#define	HRIOCSTART	('h'<<8|5)	/* take it back, screen cleared */

/*
**	Argument to HRIOCSFONT and HRIOCGFONT.  hf_bits addresses
**	hf_count*HRGH shorts in the calling program's own memory; the driver
**	copies them, and keeps no pointer into user space past the call.
*/
struct hrfont {
	unsigned short	hf_first;	/* first slot */
	unsigned short	hf_count;	/* slots to transfer */
	unsigned short	*hf_bits;	/* hf_count*HRGH scan lines */
};

#endif
