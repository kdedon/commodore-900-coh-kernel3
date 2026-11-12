/* SPDX-License-Identifier: MIT */
/*
 * Video cards that identify themselves with an ID byte in I/O space.
 *
 * An absent port reads as pull-ups, as 0, or as whatever was last driven on
 * the bus, so a matching ID alone proves little.  Each row's port is first
 * written with a wrong value and must still read back the ID, twice with
 * different patterns, and the card must then pass its own verify routine.
 *
 * vcprobe() runs from vidsel before printf works; vcreport() prints later.
 */
#include <lcvdc.h>

struct vcard {
	unsigned	vc_port;	/* ID byte's I/O address */
	unsigned char	vc_id;
	int		(*vc_verify)();
	char		*vc_name;	/* for the boot message */
	char		*vc_chip;
};

static	int	lcverify();

static struct vcard vcards[] = {
	{ LCID, LCID64, lcverify, "lc", "8568" }
};
#define	NVCARD	(sizeof(vcards) / sizeof(vcards[0]))

int	vcfound;		/* 1 + row of the card found, 0 for none */
int	vcseen;			/* 1 + row whose ID answered */
int	vcrev;			/* chip version the card reported */
char	*vcchip;		/* chip that version names */
int	lcnvi;			/* LC chip found: its INTR drives NVI */

static
vcmatch(vp)
register struct vcard *vp;
{
	outb(vp->vc_port, ~vp->vc_id & 0xFF);
	if (inb(vp->vc_port) != vp->vc_id)
		return 0;
	outb(vp->vc_port, vp->vc_id ^ 0x55);
	return inb(vp->vc_port) == vp->vc_id;
}

/*
 * Return nonzero when a card in the table answered.
 */
vcprobe()
{
	register int i;

	for (i = 0; i < NVCARD; i++) {
		if (!vcmatch(&vcards[i]))
			continue;
		vcseen = i + 1;
		if ((*vcards[i].vc_verify)())
			return vcfound = i + 1;
	}
	return 0;
}

static
phex(v, n)
register unsigned v;
register int n;
{
	while (--n >= 0)
		putchar("0123456789abcdef"[(v >> (n * 4)) & 0xF]);
}

vcreport()
{
	register struct vcard *vp;

	if (vcseen == 0)
		return;
	vp = &vcards[vcseen - 1];
	printf("%s video: ", vp->vc_name);
	phex(vp->vc_port, 4);
	putchar('=');
	phex(vp->vc_id, 2);
	if (vcfound)
		printf(", %s rev %d\n", vcchip, vcrev);
	else
		printf(", %s does not answer: no supported card\n", vp->vc_chip);
}

static
lcwait()
{
	register int n;

	for (n = 0; n < LCPOLL; n++)
		if (inb(LCADDR) & LCREADY)
			return 1;
	return 0;
}

static
lcput(r, v)
{
	outb(LCADDR, r);
	if (!lcwait())
		return 0;
	outb(LCDATA, v);
	return 1;
}

static
lcget(r)
{
	outb(LCADDR, r);
	if (!lcwait())
		return -1;
	return inb(LCDATA);
}

static
lcpair(h, l)
{
	return lcput(LCR_UPDH, h) && lcput(LCR_UPDL, l)
	    && lcget(LCR_UPDH) == h && lcget(LCR_UPDL) == l;
}

/*
 * The update address must hold two patterns and their complements, and the
 * status bits that are always 0 must read 0.
 */
static
lcverify()
{
	register int h, l, ok;

	if ((h = lcget(LCR_UPDH)) < 0 || (l = lcget(LCR_UPDL)) < 0)
		return 0;
	ok = (inb(LCADDR) & LCZERO) == 0
	  && lcpair(0x5A, 0xA5) && lcpair(0xA5, 0x5A);
	lcput(LCR_UPDH, h);
	lcput(LCR_UPDL, l);
	if (ok) {
		vcrev = inb(LCADDR) & LCREV;
		vcchip = vcrev == 2 ? "8568" : "8563";
		lcnvi = 1;
	}
	return ok;
}

/*
 * The 8568 holds INTR low from each Update Ready rise until a status read.
 * Wait out any access in progress; the read that sees ready releases it.
 */
lcrel()
{
	lcwait();
}
