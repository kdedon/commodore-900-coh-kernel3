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

static unsigned char lcr24;	/* R24 as last written */
static unsigned char lcsel;	/* register index as last written */
static unsigned lcupd;		/* update address as the chip holds it */
static unsigned lccur;		/* cursor address wanted */
static unsigned lccurw;		/* and as last written */
static unsigned lcraw = 0xFFFF;	/* last attribute mapped */
static unsigned char lcmapped;	/* and what it maps to */

/* Each row's attribute when the whole row has it, else 0. */
static unsigned char lcrowa[NROWS];

/*
 * Timing for 80x25 text, written whole on every init.  R16/R17 are read
 * only, and R30/R31 would start a VRAM access.
 */
static unsigned char lcregs[LCR_NREG] = {
	0x7E, 0x50, 0x66, 0x49, 0x20, 0x00, 0x19, 0x1D,	/* R0-R7 */
	0x00, 0x07, 0x20, 0x07, 0x00, 0x00, 0x00, 0x00,	/* R8-R15 */
	0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x78, 0x08,	/* R16-R23 */
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
	lcfill(LCTEXT + off, val & 0xFF, n);
	if ((val >> 8) != lcraw)
		lcmapped = lcmap(lcraw = val >> 8);
	v = lcmapped;
	r = off / NCOL;
	e = n == 1 ? r : (off + n - 1) / NCOL;
	if (lcsame(r, e, v))
		return;
	lcfill(LCATTR + off, v, n);
	for (; r <= e; r++)
		if (r*NCOL >= off && (r + 1)*NCOL <= off + n)
			lcrowa[r] = v;
		else if (lcrowa[r] != v)
			lcrowa[r] = 0;
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
	from >>= 1;
	to >>= 1;
	if (down) {
		from -= n - 1;
		to -= n - 1;
	}
	lcmove(LCTEXT + to, LCTEXT + from, n);
	v = lcrowa[to / NCOL];
	if (v != 0 && lcsame(from / NCOL, (from + n - 1) / NCOL, v)
	    && lcsame(to / NCOL, (to + n - 1) / NCOL, v))
		return;
	lcmove(LCATTR + to, LCATTR + from, n);
	lcmvrows(from, to, n);
}

/*
 * The 6845 register pairs the terminal code sets: cursor shape, display
 * start and cursor address.  The cursor shape is given in 14-line cells.
 * The cursor address waits for lcsync().
 */
mmcrt(reg, val)
unsigned reg, val;
{
	register unsigned s, e;

	switch (reg) {
	case 10:
		s = (val >> 8) & 0x1F;
		e = val & 0x1F;
		lcwr(LCR_CURST, ((val >> 8) & 0x60) | (2*s > e ? 7 : 0));
		lcwr(LCR_CUREND, 8);
		break;
	case 12:
		lcaddr(LCR_DISPH, LCTEXT + val);
		break;
	case 14:
		lccur = LCTEXT + val;
		break;
	}
}

/* Write out the cursor address mmcrt() was last given. */
lcsync()
{
	if (lccur != lccurw) {
		lcaddr(LCR_CURH, lccur);
		lccurw = lccur;
	}
}

mmvoff()
{
}

mmvonn()
{
}
