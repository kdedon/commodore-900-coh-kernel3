/*
**	The two character-set banks plotc() draws from.
**
**	`font' is gall.c: HRNGLYPH compiled-in glyphs, the low HRLOW for
**	0x20-0x7f and the high HRHIGH for 0xa0-0xff.  It is never written.
**	`sfont' is the loadable bank the high half of the character space
**	renders from, seeded from font[HRLOW..] at driver load.
**
**	Both are declared `short' because that is what plotc()'s scan-line
**	arithmetic uses; every value it derives is masked to 4 or 8 bits, so
**	the sign of the shift does not reach the frame buffer.
*/

extern short	font[][25];
extern short	sfont[][25];

int	hrfseed();
int	hrfload();
int	hrfget();
