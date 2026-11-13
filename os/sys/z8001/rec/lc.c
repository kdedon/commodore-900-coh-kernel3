/* SPDX-License-Identifier: MIT */
/*
 * Screen primitives of the text console on the LC card's 8563, in place of
 * the memory-mapped ones: the caller's byte offsets count 2-byte cells, with
 * the character in the low byte of each value and the 6845-style attribute
 * in the high byte.  Characters and attributes live in separate planes of
 * the chip's RAM, reached only through its registers.
 */
#include <lcvdc.h>

extern unsigned char lcfont[128][8];

#define	NCOL	80
#define	NROWS	25
#define	NRING	(LCRING / NCOL)	/* rows each plane holds */

static unsigned char lcr24;	/* R24 as last written */
static unsigned char lcsel;	/* register index as last written */
static unsigned lcupd;		/* update address as the chip holds it */
static unsigned lcorg;		/* the screen's first cell in each plane */
static unsigned lcorgw;		/* and as the display registers hold it */
static unsigned lccur;		/* cursor cell wanted */
static unsigned lccurw;		/* cursor address as last written */
static unsigned lcraw = 0xFFFF;	/* last attribute mapped */
static unsigned char lcmapped;	/* and what it maps to */
static char lcaway;		/* the chip shows another picture */
static unsigned char lcr10;	/* cursor mode and start line wanted */
static char lcshape;		/* and not yet written */

/* Each row's attribute when the whole row has it, else 0. */
static unsigned char lcrowa[NROWS];

/* The text address ending a row known to have attribute lcokraw, or 0. */
static unsigned lcokend;
static unsigned lcokraw;

/* Row 24 holds only spaces. */
static char lcblank;

/*
 * Timing for 80x25 text, written whole on every init.  R16/R17 are read
 * only, and R30/R31 would start a VRAM access.
 */
static unsigned char lcregs[LCR_NREG] = {
	0x7E, 0x50, 0x66, 0x49, 0x20, 0x00, 0x19, 0x1D,	/* R0-R7 */
	0x00, 0x07, 0x20, 0x07, 0x00, 0x00, 0x00, 0x00,	/* R8-R15 */
	0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x78, 0x08,	/* R16-R23 */
	0x20, 0x47, 0xF0, 0x00, 0x30, 0x07, 0x00, 0x00,	/* R24-R31 */
	0x00, 0x00, 0x7D, 0x64, 0x05			/* R32-R36 */
};

static
lcrdy()
{
	register unsigned n;

	for (n = 1; n != 0; n++)
		if (inb(LCADDR) & LCREADY)
			return;
}

static
lcwr(r, v)
{
	if (lcsel != r)
		outb(LCADDR, lcsel = r);
	lcrdy();
	outb(LCDATA, v);
}

/* Write VRAM at the update address. */
static
lcput(v)
{
	lcwr(LCR_DATA, v);
	lcupd++;
}

static
lcaddr(r, a)
register unsigned a;
{
	lcwr(r, a >> 8);
	lcwr(r + 1, a & 0xFF);
}

static
lcseek(a)
register unsigned a;
{
	if (a != lcupd) {
		lcaddr(LCR_UPDH, a);
		lcupd = a;
	}
}

static
lcmode(v)
{
	if (lcr24 != v)
		lcwr(LCR_VSCROLL, lcr24 = v);
}

/*
 * Run a block of n bytes, which the update address and, for a copy, the
 * source address already point at.
 */
static
lcrun(n)
register unsigned n;
{
	register unsigned k;

	while (n != 0) {
		k = n > 255 ? 255 : n;
		lcwr(LCR_COUNT, k);
		lcupd += k;
		n -= k;
	}
}

static
lcfill(a, v, n)
unsigned a;
unsigned n;
{
	if (n == 0)
		return;
	if (a != lcupd)
		lcseek(a);
	if (lcr24 & LCCOPY)
		lcmode(lcr24 & ~LCCOPY);
	lcput(v);
	if (--n != 0)
		lcrun(n);
}

/* An ascending copy; safe when dst <= src or the two do not overlap. */
static
lcblk(dst, src, n)
unsigned dst, src, n;
{
	if (n == 0)
		return;
	lcseek(dst);
	lcaddr(LCR_SRCH, src);
	lcmode(lcr24 | LCCOPY);
	lcrun(n);
}

/*
 * Copy n bytes upward in VRAM (dst > src).  The chip copies only ascending,
 * so overlapping moves go in pieces from the top when the gap is wide, and
 * through the scratch area when it is narrow.
 */
static
lcup(dst, src, n)
unsigned dst, src, n;
{
	register unsigned d, k;

	d = dst - src;
	if (d >= n) {
		lcblk(dst, src, n);
	} else if (d >= 64) {
		while (n != 0) {
			k = n > d ? d : n;
			n -= k;
			lcblk(dst + n, src + n, k);
		}
	} else {
		lcblk(LCSCR, src, n);
		lcblk(dst, LCSCR, n);
	}
}

static
lcmove(dst, src, n)
unsigned dst, src, n;
{
	if (dst > src)
		lcup(dst, src, n);
	else
		lcblk(dst, src, n);
}

/*
 * The attribute byte for a 6845-style one: white on black, intensity,
 * blink, and reverse or underline.
 */
static
lcmap(a)
register unsigned a;
{
	register int v;

	v = LCA_RED|LCA_GREEN|LCA_BLUE;
	switch (a & 0x77) {
	case 0x70:
		v |= LCA_REV;
		break;
	case 0x01:
		v |= LCA_UNDER;
		break;
	}
	if (a & 0x08)
		v |= LCA_INT;
	if (a & 0x80)
		v |= LCA_BLINK;
	return v;
}

/* Whether rows r to e all have attribute v throughout. */
static
lcsame(r, e, v)
register unsigned r, e;
register int v;
{
	for (; r <= e; r++)
		if (lcrowa[r] != v)
			return 0;
	return 1;
}

/*
 * Carry the row attributes along with a copy of n cells.  A copy of whole
 * rows moves them; any other leaves the rows it lands on mixed.
 */
static
lcmvrows(from, to, n)
register unsigned from, to;
unsigned n;
{
	register unsigned i;

	if (from % NCOL || to % NCOL || n % NCOL) {
		for (i = to / NCOL; i <= (to + n - 1) / NCOL; i++)
			lcrowa[i] = 0;
		return;
	}
	from /= NCOL;
	to /= NCOL;
	n /= NCOL;
	if (to > from)
		for (i = n; i-- != 0; )
			lcrowa[to + i] = lcrowa[from + i];
	else
		for (i = 0; i < n; i++)
			lcrowa[to + i] = lcrowa[from + i];
}

/* Give the row at cell d the attributes v, or when mixed those of cell s. */
static
lcarow(d, s, v)
unsigned d, s;
{
	if (v != 0)
		lcfill(LCATTR + d, v, NCOL);
	else
		lcmove(LCATTR + d, LCATTR + s, NCOL);
}

/* Bring the screen back to the start of the planes. */
static
lchome()
{
	register int v;

	lcblk(LCTEXT, LCTEXT + lcorg, NROWS*NCOL);
	v = lcrowa[0];
	if (v != 0 && lcsame(0, NROWS-1, v))
		lcfill(LCATTR, v, NROWS*NCOL);
	else
		lcblk(LCATTR, LCATTR + lcorg, NROWS*NCOL);
	lcorg = 0;
}

/*
 * Rows 1 to k move up one and the rest keep their place, as the copy would
 * leave them.  The screen's start moves down a row instead, so only rows k
 * to the last are copied, each a row further on.
 */
static
lcrollup(k)
register unsigned k;
{
	register unsigned r, a;

	if (lcorg + (NROWS+1)*NCOL > NRING*NCOL)
		lchome();
	a = lcorg + k*NCOL;
	lcup(LCTEXT + a + NCOL, LCTEXT + a, (NROWS - k)*NCOL);
	for (r = NROWS; r-- > k; )
		if (r == NROWS-1 || lcrowa[r] == 0 || lcrowa[r+1] != lcrowa[r])
			lcarow(lcorg + (r+1)*NCOL, lcorg + r*NCOL, lcrowa[r]);
	for (r = 0; r < k; r++)
		lcrowa[r] = lcrowa[r+1];
	lcorg += NCOL;
}

/*
 * Rows 1 to 23 move up one, and row 23 is left blank with the 6845-style
 * attribute raw, when row 24 already is: the terminal's scroll, which keeps
 * row 24.  The screen's start moves down a row and only the row after it,
 * the new row 24, is written.  Returns 0, and does nothing, when row 24 is
 * not blank in that attribute.
 */
lcscroll(raw)
{
	register unsigned r, v;

	v = lcmap(raw);
	if (!lcblank || lcrowa[NROWS-1] != v)
		return 0;
	if (lcorg + (NROWS+1)*NCOL > NRING*NCOL)
		lchome();
	lcorg += NCOL;
	lcokend = 0;
	lcfill(LCTEXT + lcorg + (NROWS-1)*NCOL, ' ', NCOL);
	lcfill(LCATTR + lcorg + (NROWS-1)*NCOL, v, NCOL);
	for (r = 0; r < NROWS-2; r++)
		lcrowa[r] = lcrowa[r+1];
	lcrowa[NROWS-2] = v;
	return 1;
}

/*
 * Rows 0 to k-1 move down one and the rest keep their place: the screen's
 * start moves up a row, and row 0 and the rows below k are copied back.
 */
static
lcrolldown(k)
register unsigned k;
{
	register unsigned r;

	lcorg -= NCOL;
	lcblk(LCTEXT + lcorg, LCTEXT + lcorg + NCOL, NCOL);
	lcarow(lcorg, lcorg + NCOL, lcrowa[0]);
	if (k < NROWS-1) {
		lcblk(LCTEXT + lcorg + (k+1)*NCOL, LCTEXT + lcorg + (k+2)*NCOL,
		    (NROWS-1-k)*NCOL);
		for (r = k+1; r < NROWS; r++)
			if (lcrowa[r] == 0 || lcrowa[r-1] != lcrowa[r])
				lcarow(lcorg + r*NCOL, lcorg + (r+1)*NCOL,
				    lcrowa[r]);
	}
	for (r = k; r > 0; r--)
		lcrowa[r] = lcrowa[r-1];
}

/*
 * Program the chip for 80x25 text and load the font.  R28 goes first so
 * every later address is a 64 KB one.
 */
lcinit()
{
	register int i, j;

	lcsel = 0xFF;
	lcwr(28, lcregs[28]);
	for (i = 0; i < LCR_NREG; i++)
		if (i != 16 && i != 17 && i != LCR_COUNT && i != LCR_DATA)
			lcwr(i, lcregs[i]);
	lcr24 = lcregs[LCR_VSCROLL];
	lccur = lccurw = lcregs[LCR_CURH] << 8 | lcregs[LCR_CURH + 1];
	lcorg = lcorgw = 0;
	lcokend = 0;
	lcupd = ~LCFONT;
	lcseek(LCFONT);
	for (i = 0; i < 128; i++) {
		for (j = 0; j < 8; j++)
			lcput(lcfont[i][j]);
		lcput(0);
		lcrun(7);
	}
	lcfill(LCFONT + 128*16, 0, 128*16);
	lcfill(LCTEXT, ' ', NCOL*NROWS);
	lcfill(LCATTR, lcmap(0x07), NCOL*NROWS);
	for (i = 0; i < NROWS; i++)
		lcrowa[i] = lcmap(0x07);
	lcblank = 1;
}

/*
 * Write n cells.  Attributes are written only where a row's would change,
 * so plain text costs one data write per character.
 */
mmstuff(off, val, n, base)
unsigned off, val, n;
char *base;
{
	register unsigned r, e;
	register int v;

	if (n == 0)
		return;
	off >>= 1;
	lcfill(LCTEXT + lcorg + off, val & 0xFF, n);
	if (off + n > (NROWS-1)*NCOL)
		lcblank = (val & 0xFF) == ' ' && (lcblank
		    || off <= (NROWS-1)*NCOL && off + n >= NROWS*NCOL);
	if ((val >> 8) != lcraw)
		lcmapped = lcmap(lcraw = val >> 8);
	v = lcmapped;
	r = off / NCOL;
	e = n == 1 ? r : (off + n - 1) / NCOL;
	if (lcsame(r, e, v)) {
		if (r == e) {
			lcokend = LCTEXT + lcorg + (r + 1)*NCOL;
			lcokraw = lcraw;
			if (r == NROWS-1)
				lcblank = 0;
		}
		return;
	}
	lcokend = 0;
	lcfill(LCATTR + lcorg + off, v, n);
	for (; r <= e; r++)
		if (r*NCOL >= off && (r + 1)*NCOL <= off + n)
			lcrowa[r] = v;
		else if (lcrowa[r] != v)
			lcrowa[r] = 0;
}

/*
 * One cell.  Where it follows the last one written, in a row already all of
 * its attribute, only the character goes out.
 */
lcchar(off, val)
register unsigned off, val;
{
	register unsigned a;

	a = LCTEXT + lcorg + (off >> 1);
	if (a != lcupd || lcokend - a - 1 >= NCOL || (val >> 8) != lcokraw
	    || (lcr24 & LCCOPY))
		mmstuff(off, val, 1, (char *)0);
	else {
		if (lcsel != LCR_DATA)
			outb(LCADDR, lcsel = LCR_DATA);
		if ((inb(LCADDR) & LCREADY) == 0)
			lcrdy();
		outb(LCDATA, val & 0xFF);
		lcupd++;
	}
}

/*
 * The n characters at s, from cell off on, all in one row and with the
 * attribute byte raw: what n calls of lcchar() would write.  Where the cells
 * follow the last one written, in a row already all of that attribute, they
 * go out in one stream.
 */
lcstr(off, s, n, raw)
unsigned off;
register char *s;
register unsigned n;
unsigned raw;
{
	register unsigned a, k;

	a = LCTEXT + lcorg + (off >> 1);
	while (n != 0) {
		if (a != lcupd || lcokend - a - 1 >= NCOL || raw != lcokraw
		    || (lcr24 & LCCOPY)) {
			mmstuff(off, raw << 8 | *s++ & 0xFF, 1, (char *)0);
			off += 2;
			a++;
			n--;
			continue;
		}
		k = lcokend - a;
		if (k > n)
			k = n;
		if (lcsel != LCR_DATA)
			outb(LCADDR, lcsel = LCR_DATA);
		lcupd += k;
		a += k;
		off += 2*k;
		n -= k;
		while (k != 0) {
			register unsigned w;

			w = lcwlit(s, k);
			s += w;
			k -= w;
		}
	}
}

/*
 * Copy n cells.  A downward copy names the last cell of each run.
 */
mmcopy(from, to, n, down, base)
unsigned from, to, n;
int down;
char *base;
{
	register int v;

	if (n == 0)
		return;
	lcokend = 0;
	from >>= 1;
	to >>= 1;
	if (down) {
		from -= n - 1;
		to -= n - 1;
	}
	if (to + n > (NROWS-1)*NCOL)
		lcblank = 0;
	if (n % NCOL == 0 && n >= (NROWS/2)*NCOL) {
		if (to == 0 && from == NCOL) {
			lcrollup(n / NCOL);
			return;
		}
		if (from == 0 && to == NCOL && lcorg >= NCOL) {
			lcrolldown(n / NCOL);
			return;
		}
	}
	lcmove(LCTEXT + lcorg + to, LCTEXT + lcorg + from, n);
	v = lcrowa[to / NCOL];
	if (v != 0 && lcsame(from / NCOL, (from + n - 1) / NCOL, v)
	    && lcsame(to / NCOL, (to + n - 1) / NCOL, v))
		return;
	lcmove(LCATTR + lcorg + to, LCATTR + lcorg + from, n);
	lcmvrows(from, to, n);
}

/*
 * The 6845 register pairs the terminal code sets: cursor shape, display
 * start and cursor address.  The cursor shape is given in 14-line cells.
 * The cursor shape, the display start, only ever page 0, and the cursor
 * wait for lcsync().
 */
mmcrt(reg, val)
unsigned reg, val;
{
	register unsigned s, e;

	switch (reg) {
	case 10:
		s = (val >> 8) & 0x1F;
		e = val & 0x1F;
		lcr10 = ((val >> 8) & 0x60) | (2*s > e ? 7 : 0);
		lcshape = 1;
		break;
	case 12:
		lcorgw = ~lcorg;
		break;
	case 14:
		lccur = val;
		break;
	}
}

/* Write out where the screen starts and the cursor mmcrt() was last given. */
lcsync()
{
	register unsigned a;

	if (lcaway)
		return;
	if (lcshape) {
		lcwr(LCR_CURST, lcr10);
		lcwr(LCR_CUREND, 8);
		lcshape = 0;
	}
	if (lcorg != lcorgw) {
		lcaddr(LCR_DISPH, LCTEXT + lcorg);
		lcaddr(LCR_ATTRH, LCATTR + lcorg);
		lcorgw = lcorg;
	}
	a = LCTEXT + lcorg + lccur;
	if (a != lccurw) {
		lcaddr(LCR_CURH, a);
		lccurw = a;
	}
}

/*
 * Another user of the chip has had it.  The register index, the update
 * address, the display start, the cursor and the attribute planes, and
 * whether row 24 is blank, are no longer what was last written; R24 it
 * has put back.  No console address is 0xFFFF.
 */
lcforget()
{
	register int i;

	lcsel = 0xFF;
	lcupd = 0xFFFF;
	lcorgw = ~lcorg;
	lccurw = 0xFFFF;
	lcokend = 0;
	lcblank = 0;
	for (i = 0; i < NROWS; i++)
		lcrowa[i] = 0;
}

/*
 * The chip shows a picture of its own, or with away clear the console's again,
 * and R24 is r24.  Output still goes to the text planes, which no picture
 * covers, but the display and cursor registers wait.
 */
lcbg(away, r24)
{
	lcaway = away;
	lcr24 = r24;
	lcforget();
}

mmvoff()
{
}

mmvonn()
{
}
