/* SPDX-License-Identifier: MIT */
/*
 * Host check of the console's 8563 back end.  lc.c runs against the 8563
 * model with the calls the terminal code makes for scroll, reverse scroll,
 * insert and delete line, and insert and delete character.  After each, the
 * chip's text and attribute planes must equal what the memory-mapped
 * primitives would have left on the lo-res card, and the 80x25 dump must
 * match the golden file.  The setup is checked too: R28 set before any VRAM
 * write, the whole register set written with R27 after R12/R13, the font in
 * place, and no data access while the chip was busy.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vdc.h"

#define	NROW	24
#define	NCOL	80
#define	NCELL	(25*NCOL)
#define	pos(r, c)	(2*((r)*NCOL+(c)))
#define	ATT	0x0700

/* Cell i of the screen the chip shows, and its attribute. */
#define	TXT(i)	vdc.ram[(vdc.reg[12] << 8 | vdc.reg[13]) + (i)]
#define	ATR(i)	vdc.ram[(vdc.reg[20] << 8 | vdc.reg[21]) + (i)]

extern int lcinit(), lcsync(), mmstuff(), mmcopy(), mmcrt(), lcchar(), lcstr(), lcscroll();
extern unsigned char lcfont[128][8];

static unsigned short ref[NCELL];	/* the lo-res card's screen */
static const char *golden;
static int update;
static int bad;

unsigned inb(unsigned port) { return vdc_in(port); }
void outb(unsigned port, unsigned v) { vdc_out(port, v); }

/* What the driver's assembler lcwlit() does. */
unsigned
lcwlit(unsigned char *p, unsigned n)
{
	unsigned w = 0, same = 0;
	int prev = -1;

	while (w < n) {
		while (!(vdc_in(VDC_ADDR) & 0x80))
			;
		vdc_out(VDC_DATA, *p);
		same = *p == prev ? same + 1 : 1;
		prev = *p++;
		if (++w < n && same >= 8)
			break;
	}
	return w;
}

static unsigned
attrmap(unsigned a)
{
	unsigned v = 0x0E;

	if ((a & 0x77) == 0x70)
		v |= 0x40;
	else if ((a & 0x77) == 0x01)
		v |= 0x20;
	if (a & 0x08)
		v |= 0x01;
	if (a & 0x80)
		v |= 0x10;
	return v;
}

static void
stuff(unsigned off, unsigned val, unsigned n)
{
	unsigned i;

	for (i = 0; i < n; i++)
		ref[off/2 + i] = val;
	mmstuff(off, val, n, (char *)0);
}

/* A printed character, as the terminal code puts one. */
static void
putch(unsigned off, unsigned val)
{
	ref[off/2] = val;
	lcchar(off, val);
}

static void
copy(unsigned from, unsigned to, unsigned n, int down)
{
	unsigned i;

	for (i = 0; i < n; i++)
		if (down)
			ref[to/2 - i] = ref[from/2 - i];
		else
			ref[to/2 + i] = ref[from/2 + i];
	mmcopy(from, to, n, down, (char *)0);
}

static void
fill(void)
{
	static const unsigned attrs[] = { 0x07, 0x0F, 0x70, 0x01, 0x87, 0x78 };
	int r, c;

	for (r = 0; r < 25; r++)
		for (c = 0; c < NCOL; c++)
			stuff(pos(r, c), (attrs[(r + c) % 6] << 8)
			    | ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"[(r*7 + c) % 36]), 1);
}

static void
dump(FILE *f)
{
	int r, c;

	for (r = 0; r < 25; r++) {
		for (c = 0; c < NCOL; c++)
			putc(TXT(r*NCOL + c), f);
		putc(' ', f);
		for (c = 0; c < NCOL; c++)
			fprintf(f, "%02x", ATR(r*NCOL + c));
		putc('\n', f);
	}
}

static void
check(const char *name)
{
	char path[512], *want, *got;
	size_t nw, ng;
	FILE *f;
	int i, err = 0;

	lcsync();
	for (i = 0; i < NCELL; i++)
		if (TXT(i) != (ref[i] & 0xFF) || ATR(i) != attrmap(ref[i] >> 8)) {
			if (!err)
				printf("  FAIL %s: cell %d row %d col %d: chip %02x/%02x,"
				    " want %02x/%02x\n", name, i, i / NCOL, i % NCOL,
				    TXT(i), ATR(i),
				    ref[i] & 0xFF, attrmap(ref[i] >> 8));
			err++;
		}
	if (vdc.faults) {
		printf("  FAIL %s: %u data accesses while busy\n", name, vdc.faults);
		err++;
	}
	snprintf(path, sizeof path, "%s/%s.txt", golden, name);
	f = tmpfile();
	dump(f);
	ng = ftell(f);
	rewind(f);
	got = malloc(ng);
	fread(got, 1, ng, f);
	fclose(f);
	if (update) {
		f = fopen(path, "w");
		fwrite(got, 1, ng, f);
		fclose(f);
	} else if ((f = fopen(path, "r")) == NULL) {
		printf("  FAIL %s: no golden file %s\n", name, path);
		err++;
	} else {
		want = malloc(ng + 1);
		nw = fread(want, 1, ng + 1, f);
		fclose(f);
		if (nw != ng || memcmp(want, got, ng) != 0) {
			printf("  FAIL %s: dump differs from %s\n", name, path);
			err++;
		}
		free(want);
	}
	free(got);
	if (err)
		bad++;
	else
		printf("  ok   %s\n", name);
}

static void
scrollup(void)
{
	copy(pos(1, 0), pos(0, 0), (NROW-1)*NCOL, 0);
	stuff(pos(NROW-1, 0), ' '|ATT, NCOL);
}

static void
scrolldown(void)
{
	copy(pos(NROW-2, NCOL-1), pos(NROW-1, NCOL-1), (NROW-1)*NCOL, 1);
	stuff(pos(0, 0), ' '|ATT, NCOL);
}

static void
insline(int row)
{
	unsigned off = pos(NROW-1, NCOL-1);

	copy(off - 2*NCOL, off, NCOL*(NROW-1-row), 1);
	stuff(pos(row, 0), ' '|ATT, NCOL);
}

static void
delline(int row)
{
	copy(pos(row, 0) + 2*NCOL, pos(row, 0), NCOL*(NROW-1-row), 0);
	stuff(pos(NROW-1, 0), ' '|ATT, NCOL);
}

static void
inschar(int row, int col, int c)
{
	unsigned off = pos(row, NCOL-1);

	if (col < NCOL-1)
		copy(off - 2, off, NCOL-1-col, 1);
	stuff(pos(row, col), c, 1);
}

static void
delchar(int row, int col)
{
	if (col < NCOL-1)
		copy(pos(row, col) + 2, pos(row, col), NCOL-1-col, 0);
	stuff(pos(row, NCOL-1), ' '|ATT, 1);
}

static int
setup(void)
{
	int i, j, first28 = -1, last12 = -1, first27 = -1, err = 0;
	unsigned char seen[64];

	vdc_reset();
	lcinit();
	memset(seen, 0, sizeof seen);
	for (i = 0; i < (int)vdc.nlog && i < VDC_LOG; i++) {
		seen[vdc.log[i]] = 1;
		if (vdc.log[i] == 28 && first28 < 0)
			first28 = i;
		if (vdc.log[i] == 12 || vdc.log[i] == 13)
			last12 = i;
		if (vdc.log[i] == 27 && first27 < 0)
			first27 = i;
	}
	if (first28 != 0 || vdc.early) {
		printf("  FAIL setup: R28 is not the first write\n");
		err++;
	}
	for (i = 0; i <= 36; i++)
		if (i != 16 && i != 17 && !seen[i]) {
			printf("  FAIL setup: R%d never written\n", i);
			err++;
		}
	if (first27 < last12) {
		printf("  FAIL setup: R27 written before R12/R13\n");
		err++;
	}
	if ((vdc.reg[28] & 0x10) == 0 || vdc.reg[25] != 0x47 || vdc.reg[36] != 0x05) {
		printf("  FAIL setup: R25 %02x R28 %02x R36 %02x\n",
		    vdc.reg[25], vdc.reg[28], vdc.reg[36]);
		err++;
	}
	for (i = 0; i < 256; i++)
		for (j = 0; j < 16; j++)
			if (vdc.ram[0x2000 + i*16 + j]
			    != (i < 128 && j < 8 ? lcfont[i][j] : 0)) {
				if (!err)
					printf("  FAIL setup: glyph %02x row %d\n", i, j);
				err++;
			}
	for (i = 0; i < NCELL; i++)
		if (TXT(i) != ' ' || ATR(i) != 0x0E) {
			printf("  FAIL setup: screen not clear at %d\n", i);
			err++;
			break;
		}
	if (vdc.faults) {
		printf("  FAIL setup: %u data accesses while busy\n", vdc.faults);
		err++;
	}
	if (!err)
		printf("  ok   setup\n");
	return err;
}

struct cost {
	unsigned long	sel, data, poll;
};

static void
mark(struct cost *c)
{
	c->sel = vdc.nsel;
	c->data = vdc.nlog;
	c->poll = vdc.npoll;
}

/* Accesses since mark(c), per n operations, against a limit on each. */
static void
since(struct cost *c, const char *name, int n, double sel, double data,
    double poll)
{
	double s = (double)(vdc.nsel - c->sel) / n;
	double d = (double)(vdc.nlog - c->data) / n;
	double p = (double)(vdc.npoll - c->poll) / n;

	printf("  %s %-9s %6.2f index, %6.2f data, %6.2f status\n",
	    s <= sel && d <= data && p <= poll ? "ok  " : "FAIL",
	    name, s, d, p);
	if (s > sel || d > data || p > poll)
		bad++;
}

/*
 * Register traffic for output as the terminal makes it: each character
 * then the cursor, with the cursor written out once per batch.
 */
static void
cost(void)
{
	struct cost c;
	int r, i;

	stuff(0, ' '|ATT, NCELL);
	lcsync();
	mark(&c);
	for (r = 0; r < 10; r++) {
		for (i = 0; i < 70; i++) {
			putch(pos(r, i), 'a' + i % 26 | ATT);
			mmcrt(14, r*NCOL + i + 1);
		}
		mmcrt(14, (r + 1)*NCOL);
	}
	lcsync();
	since(&c, "char", 700, 0.06, 1.04, 2.12);
	mark(&c);
	for (i = 0; i < 10; i++) {
		scrollup();
		mmcrt(14, (NROW-1)*NCOL);
	}
	lcsync();
	since(&c, "scroll", 10, 22, 22, 80);
	check("cost");
}

/*
 * Runs of characters as the terminal hands them over: the same cells as one
 * at a time, at the same cost, and a run of equal ones split as the
 * streaming helper splits it.
 */
static void
strings(void)
{
	struct cost c;
	int r, i;

	stuff(0, ' '|ATT, NCELL);
	lcsync();
	mark(&c);
	for (r = 10; r < 20; r++) {
		char b[70];

		for (i = 0; i < 70; i++)
			ref[pos(r, i)/2] = (b[i] = r == 12 ? '-' : 'A' + i % 26) | ATT;
		for (i = 0; i < 70; i += 23) {
			lcstr(pos(r, i), b + i, i + 23 > 70 ? 70 - i : 23, ATT >> 8);
			mmcrt(14, r*NCOL + i + 1);
		}
	}
	lcsync();
	since(&c, "string", 700, 0.06, 1.04, 2.12);
	check("string");
}

/*
 * The terminal's scroll over a blank row 24: row 23 comes out blank and row
 * 24 kept, as the copy and the blank row left them, across the ring's wrap.
 * It is refused once row 24 has text.
 */
static void
blankscroll(void)
{
	struct cost c;
	int r, i;

	fill();
	stuff(pos(NROW, 0), ' '|ATT, NCOL);
	lcsync();
	mark(&c);
	for (r = 0; r < 30; r++) {
		for (i = 0; i < (NROW-1)*NCOL; i++)
			ref[i] = ref[i + NCOL];
		for (i = 0; i < NCOL; i++)
			ref[(NROW-1)*NCOL + i] = ' '|ATT;
		if (!lcscroll(ATT >> 8)) {
			printf("  FAIL blankscroll: refused with row 24 blank\n");
			bad++;
		}
		putch(pos(NROW-1, r), 'a' + r | ATT);
	}
	lcsync();
	since(&c, "blankscr", 30, 12, 13, 80);
	check("blankscroll");
	putch(pos(NROW, 5), 'x'|ATT);
	if (lcscroll(ATT >> 8)) {
		printf("  FAIL blankscroll: scrolled over a written row 24\n");
		bad++;
	}
}

int
main(int argc, char **argv)
{
	unsigned char font[4096];
	int i;

	golden = argc > 1 ? argv[1] : "golden";
	update = argc > 2 && strcmp(argv[2], "update") == 0;
	bad += setup();
	memcpy(font, vdc.ram + 0x2000, sizeof font);
	for (i = 0; i < NCELL; i++)
		ref[i] = ' '|ATT;

	fill(); check("fill");
	scrollup(); check("scroll");
	for (i = 0; i < 30; i++) {
		scrollup();
		stuff(pos(NROW-1, i), 'a' + i % 26 | (i % 3 ? ATT : 0x7000), 9);
		putch(pos(NROW-1, i + 9), 'a' + i % 26 | (i % 3 ? ATT : 0x7000));
		putch(pos(NROW-1, i + 10), 'A' + i % 26 | 0x7000);
	}
	check("scroll30");
	fill(); check("refill");
	scrolldown(); check("rscroll");
	fill(); insline(5); check("insline5");
	insline(0); insline(NROW-1); check("insline-ends");
	fill(); delline(5); check("delline5");
	delline(0); delline(NROW-1); check("delline-ends");
	fill(); inschar(3, 10, 'x'|0x7000); check("inschar");
	inschar(3, 0, 'y'|ATT); inschar(3, NCOL-2, 'z'|ATT);
	inschar(3, NCOL-1, 'w'|ATT); check("inschar-ends");
	fill(); delchar(3, 10); check("delchar");
	delchar(3, 0); delchar(3, NCOL-2); delchar(3, NCOL-1); check("delchar-ends");
	cost();
	stuff(pos(3, 10), 'R'|0x7000, 5);
	stuff(pos(4, 75), 'U'|0x0100, 10);
	scrolldown(); insline(2); delchar(5, 12); inschar(6, 0, 'i'|ATT);
	stuff(pos(4, 12), ' '|ATT, 2); stuff(pos(4, 0), ' '|ATT, NCOL);
	scrollup(); delline(1); scrollup();
	stuff(pos(3, 70), 'x'|ATT, 3); check("rows");

	mmcrt(14, 5*NCOL + 7);
	lcsync();
	if ((vdc.reg[14] << 8 | vdc.reg[15])
	    != (vdc.reg[12] << 8 | vdc.reg[13]) + 5*NCOL + 7) {
		printf("  FAIL cursor: R14/R15 %02x%02x\n", vdc.reg[14], vdc.reg[15]);
		bad++;
	} else
		printf("  ok   cursor\n");
	if (memcmp(font, vdc.ram + 0x2000, sizeof font) != 0) {
		printf("  FAIL font area changed by screen operations\n");
		bad++;
	}
	strings();
	blankscroll();
	return bad != 0;
}
