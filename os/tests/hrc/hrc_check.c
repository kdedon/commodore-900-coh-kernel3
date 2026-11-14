/* SPDX-License-Identifier: MIT */
/*
 * Host check of the colour console's back end.  hrc.c is driven with the
 * calls the terminal makes -- cells, strings, scrolls one and many at a time,
 * insert and delete line and character, line 25, cursor shapes -- and after
 * every hcsync() the 128000-byte bitmap must equal the screen drawn from
 * scratch out of the same cells.  The window system's driver and HRIOCSTOP
 * must keep it off the bitmap, and it must draw everything again after.  The
 * pieces it hands the assembler loops must each lie in one segment.  A known
 * screen is checked against golden/screen.crc and written out as screen.ppm.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h/con.h"
#include "h/coherent.h"
#include "h/tty.h"

#define	NROW	24
#define	NCOL	80
#define	NROWS	25
#define	NCELL	(NROWS*NCOL)
#define	VSIZE	128000
#define	pos(r, c)	(2*((r)*NCOL+(c)))
#define	ATT	0x0700

extern int hcinit(), hcsync(), mmstuff(), mmcopy(), mmcrt(), hcstr(), hcctl();
extern unsigned char hcfont[256][16];

DRV drvl[16];
struct con { int x; } hrdrv;
static unsigned char vram[0x20000];
static unsigned char want[0x20000];
static unsigned ref[NCELL];
static unsigned refcur;
static int refcs = 14, refnocur;
static const char *golden;
static int update, bad, checks;
static unsigned long words;

long lbolt;
unsigned utimer;
TTY kvtty;
static int armed;
void outb(unsigned port, unsigned v) { }
static int kbours = 1;		/* the console has the keyboard */
int kbmine() { return kbours; }

/* Armed timers; tick() runs each that is due. */
static struct { TIM *tp; int (*f)(); char *a; } tims[4];

void
timeout(TIM *tp, unsigned n, int (*f)(), char *a)
{
	int i, j = -1;

	for (i = 0; i < 4; i++)
		if (tims[i].tp == tp || (j < 0 && tims[i].tp == NULL))
			j = i;
	tims[j].tp = f != NULL ? tp : NULL;
	tims[j].f = f;
	tims[j].a = a;
	armed++;
}

static void
tick(void)
{
	int (*f[4])();
	char *a[4];
	int i;

	lbolt += 100;
	utimer += 100;
	for (i = 0; i < 4; i++) {
		f[i] = tims[i].tp != NULL ? tims[i].f : NULL;
		a[i] = tims[i].a;
		tims[i].tp = NULL;
	}
	for (i = 0; i < 4; i++)
		if (f[i] != NULL)
			(*f[i])(a[i]);
}

/* With the queue empty, as the terminal's start routine ends. */
void mmstart(TTY *tp) { hcsync(); }
int splo() { return 0; }
int spl(int s) { return s; }

char *
pfix(int seg, long pa)
{
	return (char *)vram + (seg == 0x3B ? 0x10000 : 0);
}

static void
inseg(unsigned char *p, unsigned long n, const char *who)
{
	long a = p - vram;

	if (a < 0 || a >= 0x20000)
		return;
	if (a / 0x10000 != (a + n - 1) / 0x10000) {
		printf("  FAIL %s piece at %lx, %lu bytes, crosses a segment\n",
		    who, a, n);
		bad++;
	}
}

void
hcmov(unsigned char *d, unsigned char *s, unsigned nw, unsigned k, unsigned ss)
{
	for (; k != 0; k--) {
		unsigned i;

		inseg(d, 2*nw, "hcmov");
		for (i = 0; i < 2*nw; i++)	/* as LDIR: a fill when d is s + 2 */
			d[i] = s[i];
		if (d >= vram && d < vram + sizeof(vram))
			words += nw;
		d += 320;
		s += ss;
	}
}

void
hcrun(unsigned char *p, unsigned *s, unsigned n, unsigned *f,
    unsigned long *t, unsigned k)
{
	unsigned j, i;
	unsigned char *q;
	unsigned long v;

	inseg(p, (k-1)*320 + 4*n, "hcrun");
	for (j = 0; j < n; j++) {
		for (i = 0; i < k; i++) {
			v = t[f[(s[j] & 0xFF)*16 + i] / 4];
			q = p + 4*j + 320*i;
			q[0] = v >> 24;
			q[1] = v >> 16;
			q[2] = v >> 8;
			q[3] = v;
		}
		words += 2*k;
	}
}

static void
pixel(unsigned char *b, unsigned x, unsigned y, unsigned c)
{
	unsigned char *p = b + y*320 + x/2;

	if (x & 1)
		*p = (*p & 0xF0) | c;
	else
		*p = (*p & 0x0F) | c << 4;
}

/* The screen the cells and the cursor call for, drawn pixel by pixel. */
static void
draw(unsigned char *b)
{
	unsigned i, r, c, y, x, a, fg, bg, bits;

	memset(b, 0, sizeof(want));
	for (i = 0; i < NCELL; i++) {
		r = i / NCOL;
		c = i % NCOL;
		a = ref[i] >> 8;
		fg = 7;
		bg = 0;
		if ((a & 0x77) == 0x70)
			fg = 0, bg = 7;
		else if ((a & 0x77) == 0)
			fg = 0;
		if (a & 8) {
			if (bg)
				bg |= 8;
			else if (fg)
				fg |= 8;
		}
		for (y = 0; y < 16; y++) {
			bits = hcfont[ref[i] & 0xFF][y];
			if (y == 14 && (a & 0x77) == 0x01)
				bits = 0xFF;
			if (i == refcur && !refnocur && y >= refcs)
				bits ^= 0x100;
			for (x = 0; x < 8; x++) {
				unsigned v = (bits >> (7-x)) & 1 ? fg : bg;

				if (bits & 0x100)
					v ^= 15;
				pixel(b, c*8 + x, r*16 + y, v);
			}
		}
	}
}

static void
compare(const char *what)
{
	unsigned i;

	checks++;
	draw(want);
	for (i = 0; i < VSIZE; i++)
		if (vram[i] != want[i]) {
			printf("  FAIL %s: byte %u (row %u raster %u col %u) is %02x, want %02x\n",
			    what, i, i/5120, i/320%16, i%320/4, vram[i], want[i]);
			bad++;
			return;
		}
}

static void
sync(const char *what)
{
	lbolt += 100;
	utimer += 100;
	hcsync();
	compare(what);
}

static void
stuff(unsigned off, unsigned val, unsigned n)
{
	unsigned i;

	for (i = 0; i < n; i++)
		ref[off/2 + i] = val;
	mmstuff(off, val, n, (char *)0);
}

static void
str(unsigned off, const char *s, unsigned attr)
{
	unsigned i, n = strlen(s);

	for (i = 0; i < n; i++)
		ref[off/2 + i] = attr << 8 | (s[i] & 0xFF);
	hcstr(off, (char *)s, n, attr);
}

static void
copy(unsigned from, unsigned to, unsigned n, int down)
{
	unsigned f = from/2, t = to/2;

	if (down) {
		f -= n - 1;
		t -= n - 1;
	}
	memmove(&ref[t], &ref[f], n * sizeof(ref[0]));
	mmcopy(from, to, n, down, (char *)0);
}

static void
cursor(unsigned r, unsigned c)
{
	refcur = r*NCOL + c;
	mmcrt(14, refcur);
}

static void
scrollup(void)
{
	copy(pos(1, 0), pos(0, 0), (NROW-1)*NCOL, 0);
	stuff(pos(NROW-1, 0), ' '|ATT, NCOL);
}

static void
line(unsigned n, unsigned len)
{
	char b[NCOL+1];
	unsigned i;

	for (i = 0; i < len && i < NCOL; i++)
		b[i] = ' ' + (n*7 + i*3) % 95;
	b[i] = 0;
	str(pos(NROW-1, 0), b, (n % 5) == 0 ? 0x70 : 0x07);
}

static unsigned long
crc32(const unsigned char *p, unsigned n)
{
	unsigned long c = 0xFFFFFFFFUL;
	int k;

	while (n--) {
		c ^= *p++;
		for (k = 0; k < 8; k++)
			c = c >> 1 ^ (0xEDB88320UL & -(c & 1));
	}
	return c ^ 0xFFFFFFFFUL;
}

/* Write the bitmap as the card shows it: a gun at 0xAA, or 0xFF with I. */
static void
ppm(const char *dir)
{
	char name[512];
	FILE *f;
	unsigned x, y, v, g;

	snprintf(name, sizeof(name), "%s/screen.ppm", dir);
	if ((f = fopen(name, "wb")) == NULL)
		return;
	fprintf(f, "P6\n640 400\n255\n");
	for (y = 0; y < 400; y++)
		for (x = 0; x < 640; x++) {
			v = vram[y*320 + x/2];
			v = x & 1 ? v & 15 : v >> 4;
			g = v & 8 ? 0xFF : 0xAA;
			putc(v & 4 ? g : 0, f);
			putc(v & 2 ? g : 0, f);
			putc(v & 1 ? g : 0, f);
		}
	fclose(f);
}

static void
known(const char *work)
{
	char name[512];
	unsigned long c, g = 0;
	FILE *f;

	snprintf(name, sizeof(name), "%s/screen.crc", golden);
	c = crc32(vram, VSIZE);
	if (update) {
		if ((f = fopen(name, "w")) != NULL) {
			fprintf(f, "%08lx\n", c);
			fclose(f);
		}
	} else if ((f = fopen(name, "r")) == NULL || fscanf(f, "%lx", &g) != 1
	    || g != c) {
		printf("  FAIL known screen crc %08lx, golden %08lx\n", c, g);
		bad++;
	}
	if (f && !update)
		fclose(f);
	ppm(work);
}

int
main(int argc, char **argv)
{
	unsigned i, k;
	unsigned long w0;
	static unsigned char before[VSIZE];

	golden = argc > 1 ? argv[1] : "golden";
	update = argc > 3 && strcmp(argv[3], "update") == 0;
	memset(vram, 0x5A, sizeof(vram));
	hcinit();
	for (i = 0; i < NCELL; i++)
		ref[i] = 0x0720;
	mmcrt(10, 0x0B0C);
	stuff(0, ' '|ATT, NCELL);
	cursor(0, 0);
	sync("clear");

	/* A known screen: every attribute, Latin-1, the split row. */
	str(pos(0, 0), "The quick brown fox jumps over the lazy dog. 0123456789", 0x07);
	str(pos(1, 0), "REVERSE", 0x70);
	str(pos(1, 10), "underline", 0x01);
	str(pos(1, 22), "intense", 0x0F);
	str(pos(1, 32), "rev+int", 0x78);
	str(pos(1, 42), "ul+int", 0x09);
	str(pos(2, 0), "\xe9\xe8\xfc\xdf\xa9\xb0 caf\xe9 na\xefve", 0x07);
	for (i = 0; i < NCOL; i++) {
		char c[2];

		c[0] = 'A' + i % 26;
		c[1] = 0;
		str(pos(12, i), c, i < 64 ? 0x07 : 0x70);
	}
	str(pos(11, 60), "row 11 at the split", 0x70);
	str(pos(13, 60), "row 13 at the split", 0x01);
	str(pos(24, 0), "line 25", 0x70);
	cursor(12, 64);
	sync("known screen");
	known(argc > 2 ? argv[2] : ".");

	/* Cursor shapes. */
	mmcrt(10, 0x010C);
	refcs = 0;
	sync("block cursor");
	mmcrt(10, 0x2000);
	refnocur = 1;
	sync("no cursor");
	mmcrt(10, 0x0B0C);
	refcs = 14;
	refnocur = 0;
	cursor(NROW-1, 0);
	sync("underline cursor");

	/* One scroll a batch, then many. */
	for (i = 0; i < 5; i++) {
		scrollup();
		line(i, 10 + i*13);
		sync("one scroll");
	}
	w0 = words;
	scrollup();
	line(40, 40);
	sync("cost");
	printf("hrc: one scroll and a line: %lu words\n", words - w0);
	for (k = 2; k <= 30; k += 7) {
		w0 = words;
		for (i = 0; i < k; i++) {
			scrollup();
			line(k + i, (k*i*11) % 81);
		}
		sync("batched scrolls");
		printf("hrc: %u scrolls in one batch: %lu words\n", k, words - w0);
	}

	/* A scroll soon after a drawing waits for the timer; other output not. */
	memcpy(before, vram, VSIZE);
	armed = 0;
	scrollup();
	line(99, 30);
	lbolt += 1;
	utimer += 1;
	hcsync();
	if (memcmp(before, vram, VSIZE) != 0 || armed != 1) {
		printf("  FAIL a scroll within the gap was drawn at once\n");
		bad++;
	}
	lbolt += 10;
	utimer += 10;
	hcsync();
	compare("scroll after the gap");
	str(pos(3, 3), "x", 0x07);
	lbolt += 1;
	utimer += 1;
	hcsync();
	compare("a cell within the gap");

	/* Insert and delete line across the split, and reverse index. */
	for (k = 10; k <= 14; k += 2) {
		copy(pos(NROW-1, NCOL-1) - 2*NCOL, pos(NROW-1, NCOL-1),
		    NCOL*(NROW-1-k), 1);
		stuff(pos(k, 0), ' '|ATT, NCOL);
		str(pos(k, 5), "inserted", 0x70);
		sync("insert line");
		copy(pos(k, 0) + 2*NCOL, pos(k, 0), NCOL*(NROW-1-k), 0);
		stuff(pos(NROW-1, 0), ' '|ATT, NCOL);
		sync("delete line");
	}
	scrollup();
	line(77, 70);
	copy(pos(NROW-2, NCOL-1), pos(NROW-1, NCOL-1), (NROW-1)*NCOL, 1);
	stuff(pos(0, 0), ' '|ATT, NCOL);
	str(pos(0, 0), "reverse index after a pending scroll", 0x07);
	sync("reverse index");

	/* Insert and delete character on the split row. */
	for (k = 60; k <= 66; k += 3) {
		copy(pos(12, NCOL-1) - 2, pos(12, NCOL-1), NCOL-1-k, 1);
		str(pos(12, k), "*", 0x0F);
		sync("insert char");
		copy(pos(12, k) + 2, pos(12, k), NCOL-1-k, 0);
		stuff(pos(12, NCOL-1), ' '|ATT, 1);
		sync("delete char");
	}

	/* Erase to end of screen, and line 25. */
	stuff(pos(6, 30), ' '|ATT, (NROW-1-6)*NCOL + NCOL-30);
	str(pos(24, 10), "status", 0x01);
	sync("erase");

	/* The window system has the screen: nothing may be drawn. */
	drvl[7].d_conp = (struct con *)&hrdrv;
	kbours = 0;
	memset(vram, 0x33, VSIZE);
	memcpy(before, vram, VSIZE);
	for (i = 0; i < 30; i++) {
		scrollup();
		line(i, 50);
		hcsync();
	}
	cursor(3, 3);
	copy(pos(4, 0) + 2*NCOL, pos(4, 0), NCOL*(NROW-1-4), 0);
	stuff(pos(NROW-1, 0), ' '|ATT, NCOL);
	hcsync();
	if (memcmp(before, vram, VSIZE) != 0) {
		printf("  FAIL the console drew on the window system's screen\n");
		bad++;
	}
	drvl[7].d_conp = NULL;
	kbours = 1;
	sync("back from the window system");

	/* It comes and goes with no output in between: drawn again unasked. */
	drvl[7].d_conp = (struct con *)&hrdrv;
	kbours = 0;
	tick();
	memset(vram, 0x44, VSIZE);
	tick();
	drvl[7].d_conp = NULL;
	kbours = 1;
	tick();
	compare("back with no output");

	/* It fails to unload but gives the keyboard back: the screen too. */
	drvl[7].d_conp = (struct con *)&hrdrv;
	kbours = 0;
	tick();
	memset(vram, 0x55, VSIZE);
	kbours = 1;
	tick();
	compare("driver still loaded, keyboard back");
	str(pos(7, 7), "after", 0x07);
	sync("output with the driver still loaded");
	drvl[7].d_conp = NULL;

	/* HRIOCSTOP and HRIOCSTART. */
	hcctl(1);
	memset(vram, 0x77, VSIZE);
	memcpy(before, vram, VSIZE);
	str(pos(5, 5), "while stopped", 0x07);
	hcsync();
	if (memcmp(before, vram, VSIZE) != 0) {
		printf("  FAIL the console drew while stopped\n");
		bad++;
	}
	hcctl(0);
	sync("started");

	for (i = VSIZE; i < sizeof(vram); i++)
		if (vram[i] != 0x5A) {
			printf("  FAIL byte %u past the screen was written\n", i);
			bad++;
			break;
		}
	printf("hrc: %d screens compared, %d failures\n", checks, bad);
	return bad != 0;
}
