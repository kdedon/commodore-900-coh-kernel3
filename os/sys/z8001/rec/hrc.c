/* SPDX-License-Identifier: MIT */
/*
 * The terminal's screen on the hi-res card's 640x400 colour bitmap: 80x25
 * cells of 8x16 pixels, four bits a pixel (I, R, G, B), 320 bytes a raster,
 * pixel 0 in the top nibble of a word.  The card has no character generator
 * and no display start, so every cell is drawn and a scroll moves pixels.
 *
 * mm.c writes cells here as it would on the lo-res card.  They are kept in
 * rows hcrp[] points at, which a scroll reorders, and the bitmap is brought
 * up to date once a batch of output is done (hcsync): a scroll only counts
 * until then, and the cells changed since are drawn after the moves, so a
 * burst of lines costs one move of the screen.
 * Output that scrolls waits HCGAP ticks after a drawing, or as long as the
 * drawing took if that is longer, the last of it for a timer.  hcext[] is
 * where each bitmap row turns black for good, which keeps moves and clears
 * to the part of a row that has ink.
 *
 * The 128000 bytes span two segments.  Row SROW is the one that crosses:
 * raster SRAS of it at column SCOL.
 */
#include <coherent.h>
#include <con.h>
#include <tty.h>

#define	NCOL	80
#define	NROWS	25
#define	NCELL	(NROWS*NCOL)
#define	RB	320		/* bytes a raster */
#define	CH	16		/* rasters a cell */
#define	UL	14		/* the underline raster */
#define	SROW	12
#define	SRAS	12
#define	SCOL	64
#define	NONE	0xFFFF
#define	HRMAJ	7		/* the window system's driver */
#define	HCGAP	10		/* ticks between drawings that scroll */
#define	HCPOLL	25		/* ticks between looks at who has the screen */

#define	HCBLANK(v)	(hcempty[(v) & 0xFF] && hcdark[(v) >> 8 & 0xFF])

#define	FILL	0
#define	GLYPH	1
#define	XOR	2

extern unsigned char hcfont[256][16];
extern DRV drvl[];
char *pfix();

char	hchold;			/* mm.c's test; this screen never holds output */

static char	*hcseg0, *hcseg1;
static unsigned	hcbuf[NCELL];		/* the cells, as mm.c wrote them, */
static unsigned	*hcrp[NROWS];		/* a row of them for each screen row */
static unsigned char hcext[NROWS];	/* bitmap columns from here on are black */
static unsigned char hclo[NROWS];	/* cells hclo..hchi-1 are to be drawn */
static unsigned char hchi[NROWS];
static unsigned	hcpend;			/* up-scrolls of rows 0..hcpreg-1 not */
static unsigned	hcpreg;			/* yet made on the bitmap */
static char	hcall;			/* the bitmap must be drawn afresh */
static char	hcstop;			/* HRIOCSTOP */
static unsigned	hccur;			/* the cursor's cell */
static unsigned	hccurw = NONE;		/* and where it is drawn */
static unsigned char hccs, hcce;	/* its first and last rasters */
static unsigned char hccsw, hccew;	/* and as drawn */
static char	hcnocur;		/* no cursor */
static unsigned	hcfx[256][CH];		/* each glyph byte's offset in hctab */
static char	hcempty[256];		/* the glyph has no ink */
static char	hcdark[256];		/* the attribute draws no ink on blanks */
static long	hcrowa[NROWS];		/* where each row starts in the bitmap */
static char	hcpat[2*NCOL*2];	/* a raster of one colour, for fills */
static unsigned	hcpatw = NONE;
static unsigned long hctab[2][256];	/* a glyph byte in two colours */
static unsigned	hctabk[2] = { NONE, NONE };
static int	hctabn;
static unsigned long *hct;		/* the table for this run */
static unsigned	hcfg;			/* its ink */
static int	hcrow;			/* the row being drawn */
static unsigned	hclast;			/* when the last drawing ended */
static unsigned	hcwait;			/* and how long scrolls wait after it */
static TIM	hctim;
static char	hcarmed;
static TIM	hcptim;

/*
 * The colours of a 6845 attribute byte.  The terminal uses the mono card's
 * set: 0x07 normal, 0x70 reverse, 0x01 underline, 0x08 intense; blink is
 * not shown.  Returns ink << 4 | paper, with 0x100 for an underline.
 */
static
hccolour(a)
register unsigned a;
{
	register unsigned fg, bg;

	fg = 7;
	bg = 0;
	if ((a & 0x77) == 0x70) {
		fg = 0;
		bg = 7;
	} else if ((a & 0x77) == 0)
		fg = 0;
	if (a & 0x08) {
		if (bg)
			bg |= 8;
		else if (fg)
			fg |= 8;
	}
	return (fg << 4 | bg | ((a & 0x77) == 0x01 ? 0x100 : 0));
}

/* Four pixels: bit 3 of n leftmost, fg where it is set. */
static unsigned
hcnib(n, fg, bg)
register unsigned n, fg, bg;
{
	register unsigned w, i;

	w = 0;
	for (i = 0; i < 4; i++) {
		w <<= 4;
		w |= (n & 8) ? fg : bg;
		n <<= 1;
	}
	return (w);
}

/* Point hct at the table for colour pair k, making it if need be. */
static
hcsel(k)
register unsigned k;
{
	register unsigned b;
	register unsigned long *t;
	unsigned w[16];

	k &= 0xFF;
	hcfg = k >> 4;
	if (hctabk[0] == k) {
		hct = hctab[0];
		return;
	}
	if (hctabk[1] == k) {
		hct = hctab[1];
		return;
	}
	hctabn ^= 1;
	hctabk[hctabn] = k;
	t = hct = hctab[hctabn];
	for (b = 0; b < 16; b++)
		w[b] = hcnib(b, k >> 4, k & 15);
	for (b = 0; b < 256; b++)
		*t++ = (unsigned long)w[b >> 4] << 16 | w[b & 15];
}

/* Make hcpat a raster of colour c. */
static
hcsetpat(c)
register unsigned c;
{
	register int i;

	c *= 0x1111;
	if (c == hcpatw)
		return;
	hcpatw = c;
	for (i = 0; i < sizeof(hcpat); i += 2) {
		hcpat[i] = c >> 8;
		hcpat[i+1] = c;
	}
}

/* Where raster i of row r is at column c. */
static char *
hcat(r, i, c)
register unsigned r, i, c;
{
	long a;

	a = hcrowa[r] + (i*RB + c*4);
	if (a < 0x10000L)
		return (hcseg0 + (unsigned)a);
	return (hcseg1 + (unsigned)(a - 0x10000L));
}

/* One piece that lies in one segment: k rasters from i, cells c0..c1-1. */
static
hcop(op, i, k, c0, c1)
unsigned op, i, k, c0, c1;
{
	register char *p;
	register unsigned n;

	p = hcat(hcrow, i, c0);
	switch (op) {
	case FILL:
		hcmov(p, hcpat, 2*(c1-c0), k, 0);
		break;
	case GLYPH:
		hcrun(p, &hcrp[hcrow][c0], c1-c0, &hcfx[0][i], hct, k);
		break;
	case XOR:
		for (; k != 0; k--) {
			for (n = 0; n < 4*(c1-c0); n++)
				p[n] ^= 0xFF;
			p += RB;
		}
		break;
	}
}

/* Do op on rasters i0..i1-1 of cells c0..c1-1 of row hcrow. */
static
hcdo(op, i0, i1, c0, c1)
register unsigned op, i0;
unsigned i1, c0, c1;
{
	register unsigned k;

	if (c0 >= c1 || i0 >= i1)
		return;
	if (hcrow != SROW) {
		hcop(op, i0, i1-i0, c0, c1);
		return;
	}
	if (i0 < SRAS) {
		k = i1 < SRAS ? i1 : SRAS;
		hcop(op, i0, k-i0, c0, c1);
		i0 = k;
	}
	if (i0 == SRAS && i0 < i1) {
		if (c0 < SCOL)
			hcop(op, SRAS, 1, c0, c1 < SCOL ? c1 : SCOL);
		if (c1 > SCOL)
			hcop(op, SRAS, 1, c0 > SCOL ? c0 : SCOL, c1);
		i0++;
	}
	if (i0 < i1)
		hcop(op, i0, i1-i0, c0, c1);
}

/* Fill cells c0..c1-1 of row r with colour c. */
static
hcfill(r, c0, c1, c)
{
	hcsetpat(c);
	hcrow = r;
	hcdo(FILL, 0, CH, c0, c1);
}

/* Move cells 0..n-1 of bitmap row s to row d. */
static
hcmvrow(s, d, n)
register unsigned s, d, n;
{
	register unsigned i;

	if (n == 0)
		return;
	if (s != SROW && d != SROW) {
		hcmov(hcat(d, 0, 0), hcat(s, 0, 0), 2*n, CH, RB);
		return;
	}
	for (i = 0; i < CH; i++)
		if (i != SRAS || n <= SCOL)
			hcmov(hcat(d, i, 0), hcat(s, i, 0), 2*n, 1, RB);
		else {
			hcmov(hcat(d, i, 0), hcat(s, i, 0), 2*SCOL, 1, RB);
			hcmov(hcat(d, i, SCOL), hcat(s, i, SCOL), 2*(n-SCOL), 1, RB);
		}
}

/* Bitmap row s to row d, with the ink it has. */
static
hcbrow(s, d)
register unsigned s, d;
{
	hcmvrow(s, d, hcext[s]);
	if (hcext[d] > hcext[s])
		hcfill(d, hcext[s], hcext[d], 0);
	hcext[d] = hcext[s];
}

static
hcmark(r, lo, hi)
register unsigned r, lo, hi;
{
	if (lo < hclo[r])
		hclo[r] = lo;
	if (hi > hchi[r])
		hchi[r] = hi;
}

/* Mark cells a..a+n-1 to be drawn. */
static
hcmarkn(a, n)
register unsigned a, n;
{
	register unsigned r, c;

	r = a / NCOL;
	c = a % NCOL;
	while (n != 0 && r < NROWS) {
		if (c + n <= NCOL) {
			hcmark(r, c, c + n);
			return;
		}
		hcmark(r, c, NCOL);
		n -= NCOL - c;
		c = 0;
		r++;
	}
}

/*
 * Whether the screen is someone else's.  The window system's driver has it
 * while it holds the keyboard too; one that failed to unload has given the
 * keyboard back.
 */
static
hcaway()
{
	if (hcstop)
		return (1);
	if (drvl[HRMAJ].d_conp == NULL)
		return (0);
	return (!kbmine());
}

/* Notices the screen going and coming back when there is no output. */
static
hcpoll()
{
	extern TTY kvtty;

	if (hcaway() != hcall)
		mmstart(&kvtty);
	timeout(&hcptim, HCPOLL, hcpoll, (char *)0);
}

static
hccuroff()
{
	if (hccurw == NONE)
		return;
	hcrow = hccurw / NCOL;
	hcdo(XOR, hccsw, hccew+1, hccurw % NCOL, hccurw % NCOL + 1);
	hccurw = NONE;
}

static
hccuron()
{
	if (hccurw == hccur && hccsw == hccs && hccew == hcce)
		return;
	if (hcnocur || hccur >= NCELL) {
		hccuroff();
		return;
	}
	hccuroff();
	hcrow = hccur / NCOL;
	hcdo(XOR, hccs, hcce+1, hccur % NCOL, hccur % NCOL + 1);
	hccurw = hccur;
	hccsw = hccs;
	hccew = hcce;
}

/* Make the pending scrolls on the bitmap.  The cursor must be off. */
static
hcapply()
{
	register unsigned r, k;

	k = hcpend;
	hcpend = 0;
	if (k == 0)
		return;
	for (r = 0; r + k < hcpreg; r++)
		if (hclo[r] != 0 || hchi[r] < NCOL)
			hcbrow(r + k, r);
}

/*
 * Draw cells lo..hi-1 of row r.  Blank cells at either end are
 * only cleared, and only where the bitmap row has ink.
 */
static
hcdraw(r, lo, hi)
unsigned r, lo, hi;
{
	register unsigned c, k, at;
	register unsigned *p;
	unsigned a, e, ext;

	p = hcrp[r];
	ext = hcext[r];
	for (e = hi; e > lo && HCBLANK(p[e-1]); e--)
		;
	for (c = lo; c < e && HCBLANK(p[c]); c++)
		;
	if (c > lo && lo < ext)
		hcfill(r, lo, c < ext ? c : ext, 0);
	if (e < hi && e < ext)
		hcfill(r, e, hi < ext ? hi : ext, 0);
	if (hi >= ext)
		hcext[r] = e > lo ? e : (lo < ext ? lo : ext);
	hcrow = r;
	for (; c < e; c = k) {
		at = p[c] & 0xFF00;
		for (k = c+1; k < e && (p[k] & 0xFF00) == at; k++)
			;
		a = hccolour(at >> 8);
		hcsel(a);
		hcdo(GLYPH, 0, CH, c, k);
		if (a & 0x100) {
			hcsetpat(hcfg);
			hcdo(FILL, UL, UL+1, c, k);
		}
	}
}

static
hcfire()
{
	extern TTY kvtty;

	hcarmed = 0;
	mmstart(&kvtty);
}

/*
 * Bring the bitmap up to date.  Drawing a screen takes several clock ticks,
 * so it is done with interrupts on; mm.c's busy flag keeps it to one caller.
 */
hcsync()
{
	register unsigned r;
	register int s;
	unsigned t;

	if (hcpend != 0 && !hcall && (t = utimer - hclast) < hcwait) {
		if (!hcarmed) {
			hcarmed = 1;
			timeout(&hctim, hcwait - t, hcfire, (char *)0);
		}
		return;
	}
	if (hcaway()) {
		hcall = 1;
		return;
	}
	t = utimer;
	s = splo();
	if (hcall) {
		hcall = 0;
		hcpend = 0;
		hccurw = NONE;
		for (r = 0; r < NROWS; r++) {
			hcfill(r, 0, NCOL, 0);
			hcext[r] = 0;
			hclo[r] = 0;
			hchi[r] = NCOL;
		}
	}
	hccuroff();
	hcapply();
	for (r = 0; r < NROWS; r++)
		if (hclo[r] < hchi[r]) {
			hcdraw(r, hclo[r], hchi[r]);
			hclo[r] = NCOL;
			hchi[r] = 0;
		}
	hccuron();
	/* Scrolls wait at least as long as this drawing took. */
	hclast = utimer;
	t = hclast - t;
	hcwait = t > HCGAP ? t : HCGAP;
	spl(s);
}

hcinit()
{
	register unsigned i, j;

	hcseg0 = pfix(0x3A, 0x003E0000L);
	hcseg1 = pfix(0x3B, 0x003F0000L);
	outb(0x414, 0xFF);
	for (i = 0; i < NROWS; i++)
		hcrowa[i] = (long)i * (CH*RB);
	for (i = 0; i < 256; i++) {
		hcdark[i] = (hccolour(i) & 0x10F) == 0;
		hcempty[i] = 1;
		for (j = 0; j < CH; j++) {
			hcfx[i][j] = hcfont[i][j] * 4;
			if (hcfont[i][j] != 0)
				hcempty[i] = 0;
		}
	}
	for (i = 0; i < NCELL; i++)
		hcbuf[i] = 0x0720;
	for (i = 0; i < NROWS; i++)
		hcrp[i] = &hcbuf[i*NCOL];
	for (i = 0; i < NROWS; i++) {
		hcfill(i, 0, NCOL, 0);
		hcext[i] = 0;
		hclo[i] = NCOL;
		hchi[i] = 0;
	}
	hcpend = 0;
	hcall = 0;
	hccurw = NONE;
	hccs = 14;
	hcce = 15;
	timeout(&hcptim, HCPOLL, hcpoll, (char *)0);
}

hcuload()
{
	timeout(&hcptim, 0, NULL, (char *)0);
	timeout(&hctim, 0, NULL, (char *)0);
}

/* HRIOCSTOP and HRIOCSTART: stand off the bitmap, and come back. */
hcctl(stop)
{
	hcstop = stop;
	hcall = 1;
}

/* Write n cells. */
mmstuff(off, val, n, base)
unsigned off, val, n;
char *base;
{
	register unsigned *p;
	register unsigned k, c;

	off >>= 1;
	if (off >= NCELL)
		return;
	if (n > NCELL - off)
		n = NCELL - off;
	hcmarkn(off, n);
	c = off % NCOL;
	for (off /= NCOL; n != 0; off++) {
		k = NCOL - c;
		if (k > n)
			k = n;
		p = hcrp[off] + c;
		*p = val;
		if (k > 1)
			hcmov((char *)(p+1), (char *)p, (k-1)*sizeof(unsigned)/2, 1, 0);
		n -= k;
		c = 0;
	}
}

hcchar(off, val)
unsigned off, val;
{
	mmstuff(off, val, 1, (char *)0);
}

/* The n characters at s, from cell off on, in one row, attribute raw. */
hcstr(off, s, n, raw)
unsigned off;
register char *s;
register unsigned n;
unsigned raw;
{
	register unsigned *p;

	off >>= 1;
	p = hcrp[off / NCOL] + off % NCOL;
	hcmarkn(off, n);
	raw <<= 8;
	for (; n != 0; n--)
		*p++ = raw | *s++ & 0xFF;
}

/* mm.c's scroll is a copy and a fill, which mmcopy() turns into a count. */
hcscroll(raw)
{
	return (0);
}

static unsigned *
hccell(i)
register unsigned i;
{
	return (hcrp[i / NCOL] + i % NCOL);
}

/*
 * Move m rows of cells from row f to row t by moving the row pointers.  The
 * rows the move leaves keep what they held, as a copy would have left them.
 */
static
hcrot(f, t, m)
register unsigned f, t;
unsigned m;
{
	register unsigned x, k;
	unsigned *sv[NROWS];
	unsigned b, e;

	k = 0;
	if (t < f) {
		e = t + m < f ? t + m : f;
		for (x = t; x < e; x++)
			sv[k++] = hcrp[x];
		for (x = 0; x < m; x++)
			hcrp[t+x] = hcrp[f+x];
		b = f > t + m ? f : t + m;
		for (k = 0, x = b; x < f + m; x++) {
			hcrp[x] = sv[k++];
			hcmov((char *)hcrp[x], (char *)hcrp[x-(f-t)],
			    NCOL * sizeof(unsigned) / 2, 1, 0);
		}
	} else if (t > f) {
		b = t > f + m ? t : f + m;
		for (x = b; x < t + m; x++)
			sv[k++] = hcrp[x];
		for (x = m; x != 0; x--)
			hcrp[t+x-1] = hcrp[f+x-1];
		e = t < f + m ? t : f + m;
		for (k = 0, x = f; x < e; x++) {
			hcrp[x] = sv[k++];
			hcmov((char *)hcrp[x], (char *)hcrp[x+(t-f)],
			    NCOL * sizeof(unsigned) / 2, 1, 0);
		}
	}
}

/*
 * Copy n cells.  A downward copy names the last cell of each run.  Whole rows
 * moved up by one from row 0 are a scroll and wait for hcsync(); other whole
 * rows move on the bitmap now, and anything else is drawn again.
 */
mmcopy(from, to, n, down, base)
unsigned from, to, n;
int down;
char *base;
{
	register unsigned r, m;
	unsigned f, t;

	if (n == 0)
		return;
	from >>= 1;
	to >>= 1;
	if (down) {
		from -= n - 1;
		to -= n - 1;
	}
	if (from % NCOL != 0 || to % NCOL != 0 || n % NCOL != 0) {
		if (to < from)
			for (m = 0; m < n; m++)
				*hccell(to + m) = *hccell(from + m);
		else
			for (m = n; m != 0; m--)
				*hccell(to + m - 1) = *hccell(from + m - 1);
		hcmarkn(to, n);
		return;
	}
	f = from / NCOL;
	t = to / NCOL;
	m = n / NCOL;
	hcrot(f, t, m);
	if (t == 0 && f == 1) {
		if (hcpend != 0 && hcpreg != m + 1) {
			hccuroff();
			hcapply();
		}
		hcpreg = m + 1;
		for (r = 0; r < m; r++) {
			hclo[r] = hclo[r+1];
			hchi[r] = hchi[r+1];
		}
		hclo[m] = 0;
		hchi[m] = NCOL;
		if (++hcpend >= hcpreg) {
			hcpend = 0;
			for (r = 0; r < hcpreg; r++) {
				hclo[r] = 0;
				hchi[r] = NCOL;
			}
		}
		return;
	}
	if (!hcaway()) {
		hccuroff();
		hcapply();
		if (t < f)
			for (r = 0; r < m; r++)
				hcbrow(f + r, t + r);
		else
			for (r = m; r != 0; r--)
				hcbrow(f + r - 1, t + r - 1);
	}
	if (t < f) {
		for (r = 0; r < m; r++) {
			hclo[t+r] = hclo[f+r];
			hchi[t+r] = hchi[f+r];
		}
		for (r = t + m; r < f + m; r++)
			hcmark(r, 0, NCOL);
	} else {
		for (r = m; r != 0; r--) {
			hclo[t+r-1] = hclo[f+r-1];
			hchi[t+r-1] = hchi[f+r-1];
		}
		for (r = f; r < t; r++)
			hcmark(r, 0, NCOL);
	}
}

/*
 * The 6845 registers the terminal sets.  Only the cursor means anything here:
 * its shape, start and end line of a 14-line cell with 0x20 for none, and its
 * place, which hcsync() draws.
 */
mmcrt(reg, val)
unsigned reg, val;
{
	switch (reg) {
	case 10:
		hcnocur = (val & 0x6000) == 0x2000;
		hccs = ((val >> 8) & 0x1F) < 6 ? 0 : 14;
		hcce = 15;
		break;
	case 14:
		hccur = val;
		break;
	}
}

mmvoff()
{
}

mmvonn()
{
}
