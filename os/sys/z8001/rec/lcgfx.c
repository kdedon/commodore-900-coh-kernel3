/* SPDX-License-Identifier: MIT */
/*
 * /dev/lcvdc, the graphics minor of the LC card's console: the chip's RAM
 * by read, write and lseek, bitmap modes, block fill and copy, the console
 * stood off and back, the text glyphs, and a RAM shadow of the bitmap mapped
 * where the hi-res card's frame buffer would be.
 *
 * The console's output path drives the same chip, whose register index is
 * one piece of global state.  While lchold is set the console leaves its
 * output queued; it is set for the length of each call here, and for as long
 * as the console is stood off in text mode.  Behind a bitmap the console
 * keeps writing its text planes between calls.
 */
#include <coherent.h>
#include <con.h>
#include <errno.h>
#include <uproc.h>
#include <tty.h>
#include <sys/seg.h>
#include <lcvdc.h>
#include <sys/lcio.h>
#include <sys/gdisp.h>

extern	TTY	kvtty;
extern	unsigned char lcfont[128][8];
extern	SEG	*salloc();
extern	char	*pfix();
extern	int	gdsram;

char	lchold;			/* the console must keep off the chip */

static	char	lcstood;	/* console stood off */
static	int	lcmd = LCM_TEXT;
static	int	lcnopen;
static	unsigned char lcsave[LCVNREG];	/* the console's registers */
static	unsigned char lcr24;	/* R24 as the chip holds it */
static	unsigned char lcsv24;	/* and as the console left it */
static	SEG	*lcshseg;
static	unsigned lcshsize;
static	unsigned lcshstride = 80;
static	unsigned char lcbuf[256];
static	struct gdisp lcgdold;	/* the descriptor the shadow displaced */
static	char	lcgdown;

#ifndef SHADOW
#define	SHADOW	((unsigned char *)0x3A000000L)
#endif
#ifndef GDISP
#define	GDISP	((struct gdisp *)ADDR(GDS, GD_OFF))
#endif
#define	NREG	LCVNREG

struct lcmodes {
	unsigned	m_width;
	unsigned	m_height;
	unsigned	m_attr;
	unsigned char	m_reg[NREG];
};

/*
 * Full register sets, NTSC.  Every mode is written whole.  R16/R17 are read
 * only, R18/R19 and R30-R33 start or steer VRAM accesses and are skipped.
 */
static struct lcmodes lcmodes[LCM_NMODE] = {
	{ 80, 25, 0, {
	0x7E, 0x50, 0x66, 0x49, 0x20, 0x00, 0x19, 0x1D,
	0x00, 0x07, 0x20, 0x07, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x78, 0x08,
	0x20, 0x47, 0xF0, 0x00, 0x30, 0x07, 0x00, 0x00,
	0x00, 0x00, 0x7D, 0x64, 0x05 } },
	{ 640, 200, 0, {
	0x7E, 0x50, 0x66, 0x49, 0x20, 0x00, 0x19, 0x1D,
	0x00, 0x07, 0x20, 0x07, 0x40, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x78, 0x08,
	0x20, 0x87, 0xF0, 0x00, 0x30, 0x07, 0x00, 0x00,
	0x00, 0x00, 0x7D, 0x64, 0x05 } },
	{ 640, 200, 0x8000, {
	0x7E, 0x50, 0x66, 0x49, 0x20, 0x00, 0x19, 0x1D,
	0x00, 0x07, 0x20, 0x07, 0x40, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x78, 0x08,
	0x20, 0xC7, 0xF0, 0x00, 0x30, 0x07, 0x00, 0x00,
	0x00, 0x00, 0x7D, 0x64, 0x05 } },
	/* Interlaced: R7 may want one up or down on a given monitor. */
	{ 640, 400, 0, {
	0x7E, 0x50, 0x66, 0x89, 0x84, 0x03, 0x64, 0x77,
	0x03, 0x03, 0x20, 0x03, 0x40, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x78, 0x08,
	0x20, 0x87, 0xF0, 0x00, 0x30, 0x03, 0x00, 0x00,
	0x00, 0x00, 0x7D, 0x64, 0x02 } },
	{ 640, 480, 0, {
	0x7E, 0x50, 0x66, 0x89, 0x84, 0x03, 0x78, 0x81,
	0x03, 0x03, 0x20, 0x03, 0x40, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x78, 0x08,
	0x20, 0x87, 0xF0, 0x00, 0x30, 0x03, 0x00, 0x00,
	0x00, 0x00, 0x7D, 0x64, 0x02 } },
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
	outb(LCADDR, r);
	lcrdy();
	outb(LCDATA, v);
}

static
lcrd(r)
{
	outb(LCADDR, r);
	lcrdy();
	return inb(LCDATA) & 0xFF;
}

static
lcaddr(r, a)
register unsigned a;
{
	lcwr(r, a >> 8);
	lcwr(r + 1, a & 0xFF);
}

/* Data to R31, which the caller has selected. */
#define	lcput(c)	(lcrdy(), outb(LCDATA, (c)))

static
lccopymode(on)
{
	register int v;

	v = on ? lcr24 | LCCOPY : lcr24 & ~LCCOPY;
	if (v != lcr24)
		lcwr(LCR_VSCROLL, lcr24 = v);
}

/* Run n bytes of the block already set up, 255 at most per command. */
static
lcrun(n)
long n;
{
	register unsigned k;

	while (n > 0) {
		k = n > 255 ? 255 : n;
		lcwr(LCR_COUNT, k);
		n -= k;
	}
}

static
lcgrab()
{
	lchold = 1;
	lcr24 = lcrd(LCR_VSCROLL);
	if (!lcstood)
		lcsv24 = lcr24;
}

/*
 * Let the console's output run: on the text screen with R24 put back as it
 * last set it, and behind a bitmap into the text planes only.
 */
static
lcdrop()
{
	if (!lcstood) {
		lcwr(LCR_VSCROLL, lcsv24);
		lcforget();
	} else if (lcmd != LCM_TEXT)
		lcbg(1, lcr24);
	else
		return;
	lchold = 0;
	mmstart(&kvtty);
}

static
lcfill(a, v, n)
unsigned a, v;
long n;
{
	if (n <= 0)
		return;
	lcaddr(LCR_UPDH, a);
	lccopymode(0);
	lcwr(LCR_DATA, v);
	lcrun(n - 1);
}

static
lccopy(dst, src, n)
unsigned dst, src;
long n;
{
	if (n <= 0)
		return;
	lcaddr(LCR_UPDH, dst);
	lcaddr(LCR_SRCH, src);
	lccopymode(1);
	lcrun(n);
}

/*
 * Write a register set: R28 first, so every later address is a 64 KB one,
 * then the rest in order, which puts R27 after R12/R13 and ends with R36.
 */
static
lcsetregs(rp)
register unsigned char *rp;
{
	register int i;

	lcwr(28, rp[28]);
	for (i = 0; i < NREG; i++)
		if (i != 16 && i != 17 && i != 18 && i != 19 &&
		    (i < 30 || i > 33))
			lcwr(i, rp[i]);
	lcr24 = rp[LCR_VSCROLL];
}

static
lcstop()
{
	register int i;

	if (lcstood)
		return;
	lchold = 1;
	for (i = 0; i < NREG; i++)
		if (i != LCR_COUNT && i != LCR_DATA)
			lcsave[i] = lcrd(i);
	lcstood = 1;
}

/* Text mode with the console's registers as they were, and its output. */
static
lcstart()
{
	register int i;
	unsigned char r[NREG];

	if (!lcstood)
		return;
	for (i = 0; i < NREG; i++)
		r[i] = lcmodes[LCM_TEXT].m_reg[i];
	for (i = 10; i <= 15; i++)
		r[i] = lcsave[i];
	r[LCR_VSCROLL] = lcsave[LCR_VSCROLL];
	lcsetregs(r);
	lcbg(0, lcr24);
	lcmd = LCM_TEXT;
	lcstood = 0;
	lchold = 0;
	mmstart(&kvtty);
}

/* Describe the shadow to the window system as its screen. */
static
lcgdpub()
{
	register struct gdisp *gp;
	register struct lcmodes *mp;

	if (!gdsram)
		return;
	gp = GDISP;
	if (!lcgdown) {
		lcgdold = *gp;
		lcgdown = 1;
	}
	mp = &lcmodes[lcmd];
	gp->gd_magic = 0;
	gp->gd_version = GD_VERSION;
	gp->gd_width = mp->m_width;
	gp->gd_height = mp->m_height;
	gp->gd_depth = 1;
	gp->gd_stride = lcshstride;
	gp->gd_flags = GDF_SHADOW;
	gp->gd_card = GDC_LC;
	gp->gd_seg = BMS;
	gp->gd_nseg = 1;
	gp->gd_split = 65536L / lcshstride;
	gp->gd_splitx = 0;
	gp->gd_xasp = mp->m_height == 480 ? 1 : 5;
	gp->gd_yasp = mp->m_height == 480 ? 1 : mp->m_height == 400 ? 6 : 12;
	gp->gd_vram = 64;
	gp->gd_gdsram = 1;
	gp->gd_magic = GD_MAGIC;
}

static
lcshfree()
{
	if (lcgdown) {
		*GDISP = lcgdold;
		lcgdown = 0;
	}
	if (lcshseg == NULL)
		return;
	pfix(BMS, BMPHYS);
	plim(BMS, 0xFF);
	sfree(lcshseg);
	lcshseg = NULL;
	lcshsize = 0;
}

/* A cleared shadow of the bitmap at segment BMS, limited to its size. */
static
lcshmap()
{
	register unsigned n;

	n = lcmodes[lcmd].m_height * lcshstride;
	if (lcshseg != NULL && lcshsize == n)
		return 0;
	lcshfree();
	if (lcmd == LCM_TEXT)
		return EINVAL;
	if ((lcshseg = salloc((fsize_t)n, SFSYST|SFNSWP)) == NULL)
		return ENOMEM;
	lcshsize = n;
	pfix(BMS, lcshseg->s_paddr);
	plim(BMS, (int)((bruc((long)n) >> 8) - 1));
	lcgdpub();
	return 0;
}

static
lcinfo(ip)
register struct lcinfo *ip;
{
	register struct lcmodes *mp;

	mp = &lcmodes[lcmd];
	ip->lm_ram = 64;
	ip->lm_chip = inb(LCADDR) & LCREV;
	ip->lm_mode = lcmd;
	ip->lm_width = mp->m_width;
	ip->lm_height = mp->m_height;
	ip->lm_stride = lcmd == LCM_TEXT ? 0 : 80;
	ip->lm_bitmap = lcmd == LCM_TEXT ? 0 : LCVBMAP;
	ip->lm_attr = mp->m_attr;
	ip->lm_stood = lcstood;
	ip->lm_shadow = lcshsize;
	ip->lm_shstride = lcshstride;
	ip->lm_shbase = lcshsize ? (char *)SHADOW : NULL;
}

static
lcsetmode(m)
register int m;
{
	register struct lcmodes *mp;

	if ((m & 0xFF) >= LCM_NMODE)
		return EINVAL;
	mp = &lcmodes[m & 0xFF];
	if ((m & 0xFF) == LCM_TEXT) {
		lcshfree();
		lcstart();
		return 0;
	}
	lcstop();
	lcsetregs(mp->m_reg);
	lcmd = m & 0xFF;
	if ((m & LCM_KEEP) == 0) {
		lcfill(LCVBMAP, 0, (long)mp->m_height * 80);
		if (mp->m_attr)
			lcfill(mp->m_attr, LCA_RED|LCA_GREEN|LCA_BLUE|LCA_INT,
			    (long)(mp->m_height / 8) * 80);
	}
	if (lcshseg != NULL)
		return lcshmap();
	return 0;
}

/*
 * Stream n bytes of the shadow from offset soff to the bitmap at off.  Once
 * eight equal bytes have gone, the rest of their run goes as a block fill of
 * the byte last written.  R24 is in fill mode.
 */
static
lcspan(off, soff, n)
unsigned off, soff;
register unsigned n;
{
	register unsigned char *p;
	register unsigned k;
	register int c;

	p = SHADOW + soff;
	lcaddr(LCR_UPDH, LCVBMAP + off);
	outb(LCADDR, LCR_DATA);
	while (n != 0) {
		k = lcwlit(p, n);
		p += k;
		n -= k;
		if (n == 0)
			break;
		c = p[-1];
		for (k = 0; k < n && p[k] == c; k++)
			;
		if (k != 0) {
			lcrun((long)k);
			outb(LCADDR, LCR_DATA);
			p += k;
			n -= k;
		}
	}
}

static
lcflush(vec)
char *vec;
{
	struct lcflush f;
	static struct lcrect r[LCNRECT];
	register struct lcrect *rp;
	register unsigned h, w;
	unsigned lines;

	ukcopy(vec, (char *)&f, sizeof(f));
	if (u.u_error)
		return;
	if (lcshseg == NULL || f.lu_n > LCNRECT) {
		u.u_error = EINVAL;
		return;
	}
	ukcopy((char *)f.lu_r, (char *)r, f.lu_n * sizeof(struct lcrect));
	if (u.u_error)
		return;
	lines = lcmodes[lcmd].m_height;
	lcgrab();
	lccopymode(0);
	for (rp = r; rp < &r[f.lu_n]; rp++) {
		if (rp->lr_x >= 80 || rp->lr_y >= lines)
			continue;
		w = rp->lr_w < 80 - rp->lr_x ? rp->lr_w : 80 - rp->lr_x;
		h = rp->lr_h < lines - rp->lr_y ? rp->lr_h : lines - rp->lr_y;
		if (w == 0 || h == 0)
			continue;
		if (w == 80 && lcshstride == 80)
			lcspan(rp->lr_y * 80, rp->lr_y * 80, h * 80);
		else
			for (; h != 0; h--)
				lcspan((rp->lr_y + h - 1) * 80 + rp->lr_x,
				    (rp->lr_y + h - 1) * lcshstride + rp->lr_x, w);
	}
	lcdrop();
}

static
lcglyphs(com, vec)
char *vec;
{
	struct lcfont f;
	register unsigned n, k;
	register unsigned char *up;
	unsigned a;
	register int i;

	if (com == LCIOCRFONT) {
		lcgrab();
		lcaddr(LCR_UPDH, LCFONT);
		outb(LCADDR, LCR_DATA);
		for (n = 0; n < 128; n++)
			for (i = 0; i < LCGB; i++)
				lcput(i < 8 ? lcfont[n][i] : 0);
		lcfill(LCFONT + 128*LCGB, 0, 128L*LCGB);
		lcdrop();
		return;
	}
	ukcopy(vec, (char *)&f, sizeof(f));
	if (u.u_error)
		return;
	if (f.lf_first >= LCNGLYPH || f.lf_count > LCNGLYPH - f.lf_first) {
		u.u_error = EINVAL;
		return;
	}
	n = f.lf_count * LCGB;
	a = LCFONT + f.lf_first * LCGB;
	up = f.lf_bits;
	lcgrab();
	while (n != 0 && u.u_error == 0) {
		k = n > sizeof(lcbuf) ? sizeof(lcbuf) : n;
		if (com == LCIOCSFONT) {
			ukcopy(up, lcbuf, k);
			lcaddr(LCR_UPDH, a);
			outb(LCADDR, LCR_DATA);
			for (i = 0; i < k; i++)
				lcput(lcbuf[i]);
		} else {
			lcaddr(LCR_UPDH, a);
			outb(LCADDR, LCR_DATA);
			for (i = 0; i < k; i++) {
				lcrdy();
				lcbuf[i] = inb(LCDATA);
			}
			kucopy(lcbuf, up, k);
		}
		up += k;
		a += k;
		n -= k;
	}
	lcdrop();
}

lcvopen(dev, m)
dev_t dev;
{
	lcnopen++;
}

/* The last close gives the console back and frees the shadow. */
lcvclose(dev)
dev_t dev;
{
	if (--lcnopen > 0)
		return;
	lcnopen = 0;
	lcshfree();
	lcstart();
}

/*
 * VRAM to or from the caller, at most to the end of the 64 KB, in pieces
 * through lcbuf so the console's output can run between them.
 */
static
lcrw(iop, wr)
register IO *iop;
{
	register unsigned k, i;
	unsigned a;
	long left;

	if (iop->io_seek < 0 || iop->io_seek > 0xFFFFL)
		return;
	a = iop->io_seek;
	left = 0x10000L - a;
	while (iop->io_ioc != 0 && left > 0) {
		k = iop->io_ioc > sizeof(lcbuf) ? sizeof(lcbuf) : iop->io_ioc;
		if (k > left)
			k = left;
		if (wr) {
			ioread(iop, (char *)lcbuf, k);
			if (u.u_error)
				return;
		}
		lcgrab();
		lcaddr(LCR_UPDH, a);
		outb(LCADDR, LCR_DATA);
		for (i = 0; i < k; i++)
			if (wr)
				lcput(lcbuf[i]);
			else {
				lcrdy();
				lcbuf[i] = inb(LCDATA);
			}
		lcdrop();
		if (!wr) {
			iowrite(iop, (char *)lcbuf, k);
			if (u.u_error)
				return;
		}
		a += k;
		left -= k;
	}
}

lcvread(dev, iop)
dev_t dev;
IO *iop;
{
	lcrw(iop, 0);
}

lcvwrite(dev, iop)
dev_t dev;
IO *iop;
{
	lcrw(iop, 1);
}

lcvioctl(dev, com, vec)
dev_t dev;
char *vec;
{
	struct lcinfo li;
	struct lcreg lr;
	struct lcblk lb;
	register int e;

	e = 0;
	switch (com) {
	case LCIOCINFO:
		break;
	case LCIOCMODE:
		ukcopy(vec, (char *)&li, sizeof(li));
		if (u.u_error)
			return;
		lchold = 1;
		e = lcsetmode(li.lm_mode);
		if (lcstood)
			lcdrop();
		else
			lchold = 0;
		break;
	case LCIOCGREG:
	case LCIOCSREG:
		ukcopy(vec, (char *)&lr, sizeof(lr));
		if (u.u_error)
			return;
		if (lr.lg_reg >= NREG) {
			u.u_error = EINVAL;
			return;
		}
		lcgrab();
		if (com == LCIOCGREG) {
			lr.lg_val = lcrd(lr.lg_reg);
			kucopy((char *)&lr, vec, sizeof(lr));
		} else if (lr.lg_reg == LCR_VSCROLL)
			lcwr(LCR_VSCROLL, lcr24 = lr.lg_val);
		else
			lcwr(lr.lg_reg, lr.lg_val);
		lcdrop();
		return;
	case LCIOCFILL:
	case LCIOCCOPY:
		ukcopy(vec, (char *)&lb, sizeof(lb));
		if (u.u_error)
			return;
		lcgrab();
		if (com == LCIOCFILL)
			lcfill(lb.lb_dst, lb.lb_val,
			    lb.lb_count ? (long)lb.lb_count : 0x10000L);
		else
			lccopy(lb.lb_dst, lb.lb_src,
			    lb.lb_count ? (long)lb.lb_count : 0x10000L);
		lcdrop();
		return;
	case LCIOCSTOP:
		lcstop();
		return;
	case LCIOCSTART:
		lcshfree();
		lcstart();
		return;
	case LCIOCSFONT:
	case LCIOCGFONT:
	case LCIOCRFONT:
		lcglyphs(com, vec);
		return;
	case LCIOCSHADOW:
		ukcopy(vec, (char *)&li, sizeof(li));
		if (u.u_error)
			return;
		e = li.lm_shstride == 0 ? 80 : li.lm_shstride;
		if (e < 80 || (long)e * lcmodes[lcmd].m_height > 0xFC00L) {
			e = EINVAL;
			break;
		}
		if (e != lcshstride)
			lcshfree();
		lcshstride = e;
		e = lcshmap();
		break;
	case LCIOCUNSHADOW:
		lcshfree();
		return;
	case LCIOCFLUSH:
		lcflush(vec);
		return;
	default:
		u.u_error = EINVAL;
		return;
	}
	if (e != 0) {
		u.u_error = e;
		return;
	}
	lcinfo(&li);
	kucopy((char *)&li, vec, sizeof(li));
}
