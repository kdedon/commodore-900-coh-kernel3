/*
 * Commodore M-series Z8001 -- physical-memory clear/copy for the 3.2 MI
 * (pclear/plrcopy/prlcopy).
 *
 * The 3.2 seg.c moves segment contents by PHYSICAL address in BYTES
 * (512-aligned): pclear(p,n) clears, plrcopy(p1,p2,n) copies ascending
 * (overlap-safe moving DOWN -- krunch compaction), prlcopy(p1,p2,n)
 * copies descending (overlap-safe moving UP -- segsext move).  The i286
 * did these in as2.s over ptov() selector windows; here we use the
 * 0.7.3 machine's own idiom -- pfix(sn, paddr) programs MMU segment
 * `sn' to the click containing paddr and returns a kernel far pointer
 * (see machz8001.h pkcopy/kpcopy) -- chunked per 1K click so only the
 * mapped click (+ sub-click offset) is ever touched, independent of
 * the window descriptor's limit.
 *
 * Windows: ES (0x3D, the 0.7.3 scratch-copy window) for the source and
 * OS (0x3E, the loadable-driver overlay) for the destination, exactly
 * the ES/OS pair the 0.7.3 md.s slrcopy uses.  OS is ALSO where a loaded
 * driver is mapped (bio.c getdrv dmapv's it from drvl[].d_map before every
 * call into the driver), so the two below save and restore the OS
 * descriptor around their use of it -- as slrcopy does.  Without that, a
 * compaction or a segment move between loading the console driver and the
 * next call into it leaves OS pointing at whatever was last copied, and
 * the call lands in the wrong memory.
 *
 * The descending prlcopy bounces each click through a kernel buffer:
 * kkcopy (md.s LDIR) only copies ascending, and a segsext move-up can
 * overlap by as little as 512 bytes -- less than a click.
 */
#include <sys/coherent.h>
#include <sys/seg.h>

#define	CLICK	1024
#define	CLMASK	(CLICK-1)

extern char *pfix();

static char pbounce[CLICK];		/* prlcopy bounce click */

/*
 * Clear `n' bytes of physical memory starting at `p'.
 */
pclear(p, n)
paddr_t p;
register fsize_t n;
{
	register unsigned c;

	while (n > 0) {
		c = CLICK - ((unsigned)p & CLMASK);
		if ((fsize_t)c > n)
			c = (unsigned)n;
		kclear(pfix(ES, p), c);
		p += c;
		n -= c;
	}
}

/*
 * Copy `n' bytes of physical memory from `p1' to `p2', ascending
 * (safe when the regions overlap and p2 < p1).
 */
plrcopy(p1, p2, n)
paddr_t p1, p2;
register fsize_t n;
{
	register unsigned c, c2;
	dold_t dold;

	dsave(dold);			/* OS may hold a loaded driver */
	while (n > 0) {
		c = CLICK - ((unsigned)p1 & CLMASK);
		c2 = CLICK - ((unsigned)p2 & CLMASK);
		if (c2 < c)
			c = c2;
		if ((fsize_t)c > n)
			c = (unsigned)n;
		kkcopy(pfix(ES, p1), pfix(OS, p2), c);
		p1 += c;
		p2 += c;
		n -= c;
	}
	drest(dold);
}

/*
 * Copy `n' bytes of physical memory from `p1' to `p2', descending
 * (safe when the regions overlap and p2 > p1).  Each chunk bounces
 * through a kernel click so a sub-click overlap cannot bite.
 */
prlcopy(p1, p2, n)
paddr_t p1, p2;
register fsize_t n;
{
	register unsigned c, c2;
	paddr_t e1, e2;
	dold_t dold;

	dsave(dold);			/* OS may hold a loaded driver */
	e1 = p1 + n;
	e2 = p2 + n;
	while (n > 0) {
		/* chunk ends at e1/e2; keep each side inside one click */
		c = ((unsigned)(e1 - 1) & CLMASK) + 1;
		c2 = ((unsigned)(e2 - 1) & CLMASK) + 1;
		if (c2 < c)
			c = c2;
		if ((fsize_t)c > n)
			c = (unsigned)n;
		e1 -= c;
		e2 -= c;
		kkcopy(pfix(ES, e1), pbounce, c);
		kkcopy(pbounce, pfix(OS, e2), c);
		n -= c;
	}
	drest(dold);
}

/*
 * Copy `n' bytes from kernel buffer `k' into segment `sp' at byte
 * offset `off' -- the Z8001 twin of 0.7.3's kscopy(): a TRANSIENT
 * window copy by physical address (see the s_faddr note in mdstub.c;
 * on this machine segments have no persistent kernel mapping).
 */
psegcopy(k, sp, off, n)
register char *k;
struct seg *sp;
unsigned off, n;
{
	register unsigned c;
	register paddr_t p;

	p = sp->s_paddr + off;
	while (n != 0) {
		c = CLICK - ((unsigned)p & CLMASK);
		if (c > n)
			c = n;
		kkcopy(k, pfix(ES, p), c);
		k += c;
		p += c;
		n -= c;
	}
}
