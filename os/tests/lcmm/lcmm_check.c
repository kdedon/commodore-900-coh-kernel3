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

extern int lcinit(), lcsync(), mmstuff(), mmcopy(), mmcrt();
extern unsigned char lcfont[128][8];

static unsigned short ref[NCELL];	/* the lo-res card's screen */
static const char *golden;
static int update;
static int bad;

unsigned inb(unsigned port) { return vdc_in(port); }
void outb(unsigned port, unsigned v) { vdc_out(port, v); }

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
			putc(vdc.ram[0x0000 + r*NCOL + c], f);
		putc(' ', f);
		for (c = 0; c < NCOL; c++)
			fprintf(f, "%02x", vdc.ram[0x0800 + r*NCOL + c]);
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

	for (i = 0; i < NCELL; i++)
		if (vdc.ram[i] != (ref[i] & 0xFF)
		 || vdc.ram[0x0800 + i] != attrmap(ref[i] >> 8)) {
			if (!err)
				printf("  FAIL %s: cell %d row %d col %d: chip %02x/%02x,"
				    " want %02x/%02x\n", name, i, i / NCOL, i % NCOL,
				    vdc.ram[i], vdc.ram[0x0800 + i],
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
		if (vdc.ram[i] != ' ' || vdc.ram[0x0800 + i] != 0x0E) {
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
			stuff(pos(r, i), 'a' + i % 26 | ATT, 1);
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
	since(&c, "scroll", 10, 10, 17, 270);
	check("cost");
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
	for (i = 0; i < 30; i++)
		scrollup();
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
	if (vdc.reg[14] != 0x01 || vdc.reg[15] != 0x97) {
		printf("  FAIL cursor: R14/R15 %02x%02x\n", vdc.reg[14], vdc.reg[15]);
		bad++;
	} else
		printf("  ok   cursor\n");
	if (memcmp(font, vdc.ram + 0x2000, sizeof font) != 0) {
		printf("  FAIL font area changed by screen operations\n");
		bad++;
	}
	return bad != 0;
}
