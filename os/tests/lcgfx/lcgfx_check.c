/* SPDX-License-Identifier: MIT */
/*
 * Host check of the LC graphics minor.  lcgfx.c and the console back end
 * run together against the 8563 model: the console writes text, a program
 * switches to each bitmap mode, draws into the shadow and flushes it, reads
 * and writes VRAM and glyphs, and switches back.  The text and the
 * console's registers must survive, the console must write where it means
 * to afterwards, and no data access may find the chip busy.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../lcmm/vdc.h"
#include "coherent.h"
#include "uproc.h"
#include "tty.h"
#include "sys/seg.h"
#include "../../include/sys/lcio.h"
#include "../../sys/z8001/h/lcvdc.h"
#include "../../include/sys/gdisp.h"

extern int lcinit(), mmstuff(), mmcopy(), mmcrt(), lcsync();
extern int lcvopen(), lcvclose(), lcvioctl(), lcvread(), lcvwrite();
extern char lchold;

unsigned char shadowbuf[65536];
struct gdisp gdbuf, *gdtest = &gdbuf;
int gdsram = 1;
struct uproc u;
TTY kvtty;
static SEG seg;
static int nstart, bad;
static long bms = BMPHYS;
static int bmslim = 0xFF;

unsigned inb(unsigned port) { return vdc_in(port); }
void outb(unsigned port, unsigned v) { vdc_out(port, v); }
void ukcopy(void *s, void *d, unsigned n) { memcpy(d, s, n); }
void kucopy(void *s, void *d, unsigned n) { memcpy(d, s, n); }
void mmstart(TTY *tp) { nstart++; }
SEG *salloc(fsize_t n, int f) { seg.s_paddr = 0x40000; memset(shadowbuf, 0, n); return &seg; }
void sfree(SEG *sp) { }
char *pfix(int sn, long p) { bms = p; return 0; }
void plim(int sn, int lim) { bmslim = lim; }

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

void
ioread(IO *iop, char *v, unsigned n)
{
	memcpy(v, iop->io_base, n);
	iop->io_base += n;
	iop->io_ioc -= n;
}

void
iowrite(IO *iop, char *v, unsigned n)
{
	memcpy(iop->io_base, v, n);
	iop->io_base += n;
	iop->io_ioc -= n;
}

static void
check(int ok, const char *what)
{
	if (!ok) {
		printf("  FAIL %s\n", what);
		bad++;
	}
}

static int
regs(const unsigned char *want, int lo, int hi)
{
	int i;

	for (i = lo; i <= hi; i++)
		if (vdc.reg[i] != want[i - lo])
			return 0;
	return 1;
}

static int
same(unsigned a, const unsigned char *p, unsigned n)
{
	return memcmp(&vdc.ram[a], p, n) == 0;
}

static unsigned
count(int r)
{
	unsigned i, n = 0;

	for (i = 0; i < vdc.nlog && i < VDC_LOG; i++)
		if (vdc.log[i] == r)
			n++;
	return n;
}

static void
mode(int m, struct lcinfo *ip)
{
	memset(ip, 0, sizeof *ip);
	ip->lm_mode = m;
	u.u_error = 0;
	nstart = 0;
	lcvioctl(0, LCIOCMODE, (char *)ip);
}

static void
flush(int x, int y, int w, int h)
{
	struct lcrect r;
	struct lcflush f;

	r.lr_x = x; r.lr_y = y; r.lr_w = w; r.lr_h = h;
	f.lu_n = 1;
	f.lu_r = &r;
	u.u_error = 0;
	lcvioctl(0, LCIOCFLUSH, (char *)&f);
}

int
main(void)
{
	static unsigned char text[25*80], attr[25*80], font[0x1000];
	static unsigned char buf[300], back[300], g[LCGB], gback[LCGB];
	static const unsigned char textreg[] = { 0x00, 0x10, 0x00, 0x78, 0x08 };
	struct lcinfo li;
	struct lcfont lf;
	static unsigned char pic[38400];
	unsigned char crt[6], gcrt[6];
	unsigned i, n31, n30;
	IO io;

	vdc_reset();
	lcinit();
	mmstuff(0, 0x0700 | 'H', 1, 0);
	mmstuff(2, 0x0700 | 'i', 1, 0);
	mmstuff(160, 0x0F00 | '!', 3, 0);
	mmstuff(4, 0x0700 | 'Z', 1, 0);
	lcsync();
	memcpy(text, &vdc.ram[0], sizeof text);
	memcpy(attr, &vdc.ram[LCATTR], sizeof attr);
	memcpy(font, &vdc.ram[0x2000], sizeof font);
	memcpy(crt, &vdc.reg[10], 6);

	gdbuf.gd_magic = 0x1234;
	lcvopen(0, 0);
	mode(LCM_MONO, &li);
	check(u.u_error == 0 && li.lm_width == 640 && li.lm_height == 200 &&
	    li.lm_stride == 80 && li.lm_bitmap == LCVBMAP && li.lm_stood,
	    "640x200 info");
	check(vdc.reg[25] == 0x87 && vdc.reg[12] == 0x40 && vdc.reg[13] == 0,
	    "640x200 registers");
	check(!lchold && nstart == 1, "console runs behind graphics");
	for (i = 0; i < 16000 && vdc.ram[LCVBMAP + i] == 0; i++)
		;
	check(i == 16000, "bitmap cleared");

	lcvioctl(0, LCIOCSHADOW, (char *)&li);
	check(u.u_error == 0 && li.lm_shadow == 16000 && bms == 0x40000 &&
	    bmslim == 63, "shadow mapped");
	for (i = 0; i < 16000; i++)
		shadowbuf[i] = (i / 80) % 8 == 0 ? 0xFF : (i % 80 < 40 ? 0x00 :
		    (unsigned char)rand());
	vdc.nlog = 0;
	flush(0, 0, 80, 200);
	n31 = count(31);
	n30 = count(30);
	check(u.u_error == 0 && same(LCVBMAP, shadowbuf, 16000),
	    "full flush");
	printf("lcgfx: full 640x200 flush: %u data writes, %u block fills\n",
	    n31, n30);
	for (i = 0; i < 16000; i++)
		if ((i % 80) >= 10 && (i % 80) < 15 && i / 80 >= 50 &&
		    i / 80 < 70)
			shadowbuf[i] ^= 0x5A;
	flush(10, 50, 5, 20);
	check(same(LCVBMAP, shadowbuf, 16000), "rectangle flush");
	flush(78, 190, 9, 40);
	check(u.u_error == 0, "clipped flush");

	mode(LCM_COLOUR, &li);
	check(vdc.reg[25] == 0xC7 && vdc.reg[20] == 0x80 && li.lm_attr == 0x8000,
	    "colour registers");
	for (i = 0; i < 2000 && vdc.ram[0x8000 + i] == 0x0F; i++)
		;
	check(i == 2000, "colour cells set");

	mode(LCM_I480, &li);
	lcvioctl(0, LCIOCINFO, (char *)&li);
	check(vdc.reg[8] == 3 && li.lm_height == 480 && li.lm_shadow == 38400 &&
	    bmslim == 38912 / 256 - 1, "640x480 interlace, shadow regrown");
	for (i = 0; i < 38400 && vdc.ram[LCVBMAP + i] == 0; i++)
		;
	check(i == 38400, "640x480 cleared");

	li.lm_shstride = 128;
	lcvioctl(0, LCIOCSHADOW, (char *)&li);
	check(u.u_error == 0 && li.lm_shadow == 61440 && li.lm_shstride == 128 &&
	    bmslim == 239, "shadow 128 bytes a line");
	check(gdbuf.gd_magic == GD_MAGIC && gdbuf.gd_width == 640 &&
	    gdbuf.gd_height == 480 && gdbuf.gd_stride == 128 &&
	    gdbuf.gd_flags == GDF_SHADOW && gdbuf.gd_card == GDC_LC,
	    "shadow published as the screen");
	for (i = 0; i < 61440; i++)
		shadowbuf[i] = i % 128 < 80 ? (unsigned char)(i * 13 + i / 128) : 0xEE;
	flush(0, 0, 80, 480);
	for (i = 0; i < 480 && same(LCVBMAP + i * 80, &shadowbuf[i * 128], 80); i++)
		;
	check(i == 480 && vdc.ram[LCVBMAP + 38400] != 0xEE, "wide shadow flush");

	memcpy(pic, &vdc.ram[LCVBMAP], sizeof pic);
	memcpy(gcrt, &vdc.reg[10], 6);
	mmstuff(320, 0x0700 | 'Q', 1, 0);
	mmcopy(0, 4, 10, 0, 0);
	mmcrt(10, 0x0D0E);
	mmcrt(14, 1000);
	lcsync();
	text[160] = 'Q';
	memmove(text + 2, text, 10);
	memmove(attr + 2, attr, 10);
	check(same(0, text, sizeof text) && same(LCATTR, attr, sizeof attr),
	    "console writes text behind the bitmap");
	check(same(LCVBMAP, pic, sizeof pic) && regs(gcrt, 10, 15),
	    "and leaves the picture and its registers alone");

	for (i = 0; i < sizeof buf; i++)
		buf[i] = i * 7;
	io.io_seek = 0xF000;
	io.io_ioc = sizeof buf;
	io.io_base = (char *)buf;
	lcvwrite(0, &io);
	io.io_seek = 0xF000;
	io.io_ioc = sizeof back;
	io.io_base = (char *)back;
	lcvread(0, &io);
	check(memcmp(buf, back, sizeof buf) == 0 && same(0xF000, buf, 300),
	    "write and read VRAM");
	io.io_seek = 0xFFF0;
	io.io_ioc = 100;
	io.io_base = (char *)buf;
	lcvwrite(0, &io);
	check(io.io_ioc == 84, "write stops at 64 KB");

	for (i = 0; i < LCGB; i++)
		g[i] = 0x81 ^ i;
	lf.lf_first = 200;
	lf.lf_count = 1;
	lf.lf_bits = g;
	lcvioctl(0, LCIOCSFONT, (char *)&lf);
	lf.lf_bits = gback;
	lcvioctl(0, LCIOCGFONT, (char *)&lf);
	check(memcmp(g, gback, LCGB) == 0 && same(0x2000 + 200*16, g, 16),
	    "glyph round trip");
	lcvioctl(0, LCIOCRFONT, (char *)0);
	check(same(0x2000, font, sizeof font), "console glyphs restored");

	nstart = 0;
	lcvioctl(0, LCIOCSTART, (char *)0);
	check(!lchold && nstart == 1, "console released");
	check(vdc.reg[25] == 0x47 && vdc.reg[8] == 0 && vdc.reg[9] == 7 &&
	    regs(crt, 10, 15) && regs(textreg, 19, 23), "text registers back");
	check(same(0, text, sizeof text) && same(LCATTR, attr, sizeof attr),
	    "text survives graphics");
	check(bms == BMPHYS && bmslim == 0xFF, "shadow unmapped");

	mmstuff(6, 0x0700 | 'X', 1, 0);
	mmstuff(162, 0x7000 | 'Y', 1, 0);
	lcsync();
	check(vdc.ram[3] == 'X' && vdc.ram[81] == 'Y' && vdc.ram[LCATTR + 81] ==
	    0x4E && vdc.ram[LCATTR + 82] == 0x0F, "console writes after switch");

	lcvioctl(0, LCIOCSTOP, (char *)0);
	check(lchold, "stood off");
	lcvclose(0);
	check(!lchold && vdc.reg[25] == 0x47, "last close gives console back");
	check(gdbuf.gd_magic == 0x1234, "descriptor before the shadow restored");
	check(vdc.faults == 0, "no access while busy");
	check(vdc.early == 0, "no VRAM write before R28");

	if (bad) {
		printf("lcgfx: %d FAILED\n", bad);
		return 1;
	}
	printf("lcgfx: modes, flush, VRAM, glyphs and the console switch pass\n");
	return 0;
}
