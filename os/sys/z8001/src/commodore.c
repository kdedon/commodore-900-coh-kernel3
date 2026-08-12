/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* (-lgl
 * 	The information contained herein is a trade secret of Mark Williams
 * 	Company, and  is confidential information.  It is provided  under a
 * 	license agreement,  and may be  copied or disclosed  only under the
 * 	terms of  that agreement.  Any  reproduction or disclosure  of this
 * 	material without the express written authorization of Mark Williams
 * 	Company or persuant to the license agreement is unlawful.
 * 
 * 	COHERENT Version 0.7.3
 * 	Copyright (c) 1982, 1983, 1984.
 * 	An unpublished work by Mark Williams Company, Chicago.
 * 	All rights reserved.
 -lgl) */
/*
 * Coherent.
 * Machine dependent routines for the Commodore
 * M-series Z8001 processor running in
 * Segmented mode.
 */
#include <coherent.h>
#include <clist.h>
#include <errno.h>
#include <inode.h>
#include <proc.h>
#include <seg.h>
#include <signal.h>
#include <uproc.h>
#include <romconf.h>
#include <alloc.h>

struct	romconf	romconf;
char	*_bvirt;

/*
 * What the hi-res video board says it is.
 *
 * 404H READ returns the board variant in its F1:F0 field (HR Video Card spec
 * 3.2.1): 0 no card, 1 640x400x4 RGBI colour, 2 1024x800 mono, 3 1024x1024
 * mono.  The field is set by a jumper at the N82S105N sequencer -- the WRITE
 * side of the same register (3.2.2) has bank, enable and intensity bits and no
 * mode bit at all -- so it cannot change under a running kernel, and one read
 * at probe time is the whole of it.
 *
 * `hrmode' is the variant /drv/hrtty can draw on: it plots 1 bit per pixel at
 * BPERSL = 128 bytes per raster over 800 rasters, which is variant 2 and
 * nothing else.  vidsel (md.s) compares F1:F0 against it and hands the console
 * to the next board in the probe order when they differ.  It is patchable for
 * the case this cannot distinguish: a board whose read side is not driven
 * answers with the bus, not with 0, and a machine refusing a console it can in
 * fact drive is fixed by patching `hrmode' to what `hrconf' below reports.
 *
 * `hrconf' is the raw byte, -1 when no hi-res frame buffer answered vprobe at
 * all.  vidsel cannot print -- it runs before main() -- so it records, and
 * hrreport() prints once from `start'.
 */
int	hrmode = 2;			/* 1024x800 mono, 1 bit per pixel */
int	hrconf = -1;			/* raw 404H byte; -1 = no board */

static char *hrvariant[] = {
	"absent",
	"640x400x4 colour",
	"1024x800 mono",
	"1024x1024 mono"
};

hrreport()
{
	register int m;

	if (hrconf < 0)
		return;
	m = hrconf & 3;
	printf("hi-res video: 404H=%x, %s\n", hrconf, hrvariant[m]);
	if (m == 0)
		printf("hi-res video: read side not driven -- board found by probe\n");
	else if (m != hrmode)
		printf("hi-res video: not %s -- console falls through\n",
			hrvariant[hrmode & 3]);
}

/*
 * General initialisation.
 * Setup pointers to most data structures.
 */
commodore()
{
	register vaddr_t p, p1;
	extern char end, etext;
	register unsigned e;
	register paddr_t et;
	register char *t;

	/*
	 * Segment 1 is ROM DS.
	 * It contains the romconf structure
	 * at offset of 0.
	 */
	t = (char *)ADDR(1, 0);
	t = *(int **)t;
	kkcopy(t, &romconf, sizeof romconf);
	corebot = romconf.rom_bram;		/* clicks here; bytes below */
	coretop = ctob((paddr_t)romconf.rom_eram);	/* 3.2: paddr_t = BYTES */
	/*
	 * Compute base of system (after Kernel Alloc Space).
	 * This base is in physical memory.
	 */
	t = &end;
	e = (unsigned)t;
	/* Text size.  &etext is an OFFSET within the segment it lands in, and the
	 * text may occupy more than one: ld gives it consecutive segments from IS
	 * and puts the data in the one after the last.  Reading &etext as a plain
	 * unsigned would report only the remainder past the last 64K boundary and
	 * hand the whole of that memory out twice. */
	et = ((paddr_t)(VSEG(&etext) - IS) << 16) + VOFF(&etext);
	p1 = ctob((long)corebot);
	/* Round the text size up to a click in LONG arithmetic.  bruc() adds
	 * 1023, which overflows a 16-bit unsigned once the text passes 64512:
	 * bruc() then yields 0 and every allocation below -- arena, inodes,
	 * buffers, clists, corebot, the u-area mapping -- lands on top of the
	 * kernel itself.  kboot rounds the same way (bmain.c tround), so the
	 * two must agree on where the data segment begins. */
	p1 += bruc((paddr_t)et);
	p1 += e;
	p = bruc(p1+ALLSIZE);
	allkp = setarena((char *)&end, asize = p-p1);
	inodep = (INODE *)(&end + asize);
	p += NINODE*sizeof(INODE);
	if (e + asize + NINODE*sizeof(INODE) < e)
		panic("System is too large");
	p = (p + (long)(BSIZE-1)) & ~((long)(BSIZE-1));/* DMA needs aligned */
	blockp = p;
	_bvirt = pfix(BFS, p);
	p += NBUF*BSIZE;
	clistp = (paddr_t)pfix(CLS, p);
	p += NCLIST*sizeof(CLIST);
	/*
	 * 3.2 MI contract: corebot/coretop/s_paddr are physical BYTE
	 * addresses (var.c r1.2 "now paddr_t"); the 0.7.3 click values
	 * starved smalloc's byte comparison to death (p2-p1 >= s).
	 */
	corebot = bruc(p);
	msize = btocrd(coretop-corebot);	/* KB (clicks are 1K) */
	/*
	 * Map the bit map
	 */
	pfix(BMS, BMPHYS);
	pfix(BMS+1, BMPHYS+0x00010000L);
}

/*
 * Calculate segmentation for a new process.
 * Segmented system has a separate (and only one)
 * stack segment.
 * Right now only separated (possibly) I/D (i.e.
 * 2 segments.
 */
mproto()
{
	register PROC *pp;
	register int sn;
	register int i;

	u.u_sproto.mp_nseg = 0;
	pp = SELF;
	if ((pp->p_flags & PFKERN) != 0) {
		if (pp->p_segp[SIPTEXT] != NULL
		 || pp->p_segp[SISTEXT] != NULL
		 || pp->p_segp[SISDATA] != NULL
		 || pp->p_segp[SIPDATA] != NULL
		 && pp->p_segp[SIPDATA]->s_size > ctob(MSSIZE)) {	/* 3.2: s_size in BYTES */
			u.u_error = ENOEXEC;
			return (0);
		}
		return (1);
	}
	sn = USTACK;
	for (i=SISTACK; i<=SIPDATA; i++) {
		if ((sn = uproto(pp->p_segp[i], sn, i)) < 0)
			return (0);
#ifdef SISSLIB
		/* Stack at USTACK; the shared library's two segments at
		 * USTACK+1 and USTACK+2; the program's own text/data from USEG.
		 * The library is LINKED for those numbers (slgen jumpbase, and
		 * ld -R 0x01000000), so they are fixed, not just consecutive:
		 * a client without a library leaves both slots NULL and uproto
		 * skips them, leaving USTACK+1..USEG-1 unmapped. */
		if (i == SISTACK)
			sn = USTACK+1;
		else if (i == SIPSLIB)
			sn = USEG;
#else
		/* No shared-library segment slots in this <sys/proc.h>: the
		 * stack lives at USTACK and the user text/data start at USEG,
		 * with nothing in between. */
		if (i == SISTACK)
			sn = USEG;
#endif
	}
	return (1);
}

/*
 * Load up the segmentation hardware.
 * Recalculate the prototype if necessary (after fork
 * and swaps)
 */
segload()
{
	/* Kernel processes have no user segment map to load.  User
	 * processes load their prototype, which the MI (seg.c/exec) rebuilds
	 * with sproto() whenever the segment set changes -- so segload just
	 * installs the current u.u_sproto.  (0.7.3 gated a lazy in-segload
	 * rebuild on PFSPROTO; the 3.2 process model dropped that flag.) */
	if ((SELF->p_flags & PFKERN) != 0)
		return;
	/*
	 * With a swapper running, the segment set can stay the same while the
	 * memory under it moves: a segment goes to disk and comes back at
	 * whatever click base was free.  mp_hsegs holds raw click bases --
	 * this machine has too few Z8010 descriptors to give every segment a
	 * persistent one, the way the i286 selector in s_faddr let it -- so a
	 * prototype built before the move names where the memory used to be,
	 * and the process resumes mapped onto whatever now lives there.
	 *
	 * Rebuild it here.  This is the one place that runs in the resumed
	 * process's own context with its own `u' mapped (dispatch() calls it
	 * after conrest, and swap-in happens in the swapper's context, where
	 * `u' is the swapper's).  Nothing moves while sexflag is clear --
	 * krunch stands down against the same flag -- so a kernel built
	 * without swapping pays nothing.
	 */
	if (sexflag != 0)
		sproto();
	loadmmu(u.u_sproto.mp_nseg, &u.u_sproto.mp_hsegs[0]);
}

/*
 * Set up segmentation for one segment.
 * Returns the next available segment (or negative on error).
 * `sp' is the segment pointer, `sn' is the hardware
 * segment number at which to start and `si' is
 * the index into Coherent's software segment table.
 * The structure of this routine leaves something
 * to be desired in clarity.
 */
static
uproto(sp, sn, si)
register SEG *sp;
register int sn;
int si;
{
	register struct hsegs *hp;
	register int f, l, hl;
	register saddr_t b;
	unsigned base;

	/*
	 * Check here for unused segments.
	 */
	if (sp == NULL)
		return (sn);
	hp = &u.u_sproto.mp_hsegs[sn];
	f = sp->s_flags&SFSHRX ? 0x01 : 0;	/* read only? */
	base = 0;
	b = btocrd(sp->s_paddr);	/* 3.2: SEG carries a BYTE paddr (0.7.3
					 * s_mbase was this click base) */
	l = btocru(sp->s_size);		/* 3.2: s_size in BYTES; clicks here */
	if ((sp->s_flags & SFDOWN) != 0) {
		if (l >= MSSIZE-1)
			return (0);
		f |= 0x20;		/* Downward growing segment */
		hl = MSSIZE-l;
		b -= hl;
		base = ctob(hl);
	}
	b <<= CSH;
	u.u_segl[si].sr_base = ADDR(sn, base);
	while (l > 0) {
		if (sn >= NHUSEG)
			return (-1);
		if (l >= MSSIZE) {
/*			printf("Large sproto, size=%u, sn=%d\n", l, sn); */
			hl = 0xFF;		/* Full segment */
		} else if ((sp->s_flags & SFDOWN) != 0)
			hl = (MSSIZE-l)<<CSH;
		else
			hl = (l<<CSH)-1;
		hp->mp_base = b;
		b += 256;
		hp->mp_len = hl;
		hp->mp_attr = f;
		sn++;
		hp++;
		l -= MSSIZE;
	}
	if (sn > u.u_sproto.mp_nseg)
		u.u_sproto.mp_nseg = sn;
	return (sn);
}

/*
 * Copy `n' clicks from the segment base `s1' to `s2' from right to left.
 */
srlcopy(s1, s2, n)
register saddr_t s1;
register saddr_t s2;
register unsigned n;
{
	if (n == 0)
		return;
	s1 += n;
	s2 += n;
	do {
		slrcopy(--s1, --s2, 1);
	} while (--n);
}

/*
 * Set up initial context for a process running in kernel mode.
 */
msetsys(mp, f, m)
register MCON *mp;
int (*f)();
saddr_t m;
{
	mp->mc_omap = m<<CSH;
	mp->mc_sp = (char *)&u + UPASIZE-sizeof(f);
	mp->mc_pc = f;
	mp->mc_fcw = MFSYS|MFVIE|MFSEG;
	mp->mc_depth = 1;
}

/*
 * Set up a new user process.
 * If this is a shared library, assume msetusr
 * is the last call in exec so that we can
 * suspend the process.
 */
msetusr(pc, sp)
vaddr_t pc;
vaddr_t sp;
{
	register PROC *pp;

	pp = SELF;
#ifdef SISSLIB
	/* A shared-library process never runs: it holds the library segments
	 * for its clients and nothing else, so suspend it here rather than
	 * return to a user context it has no stack for. */
	if ((pp->p_flags & PFSLIB) != 0) {
		upause();
		/* NOTREACHED */
	}
#endif
	*((long *)(regl+OPC)) = pc;
	if ((SELF->p_flags & PFKERN) != 0) {
		regl[OFCW] |= MFSYS;
		regl[OOS] = btocrd(SELF->p_segp[SIPDATA]->s_paddr) << CSH;
	} else
		*((long *)(regl+OR14)) = sp;
}

/*
 * Set the given address in the user area to the given value if it is
 * okay to do so.
 */
msetuof(a, v)
register int a;
register unsigned v;
{

	a = -(UPASIZE - a)/2;
	switch (a) {
	case OR0:
	case OR1:
	case OR2:
	case OR3:
	case OR4:
	case OR5:
	case OR6:
	case OR7:
	case OR8:
	case OR9:
	case OR10:
	case OR11:
	case OR12:
	case OR13:
	case OR14:
	case OR15:
	case OPCSEG:
	case OPCOFF:
		regl[a] = v;
		break;

	case OFCW:
		if ((v&~MFCCB) != 0)
			return (0);
		regl[OFCW] &= ~MFCCB;
		regl[OFCW] |= v;
		break;

	default:
		u.u_error = EINVAL;
		return (0);
	}
	return (1);
}

/*
 * Cause a signal routine to be executed.
 */
msigint(n, f)
register int n;
register int (*f)();
{
	register int *usp;

	usp = *(int **)&regl[OR14];
	putupd(--(int **)usp, *(int **)&regl[OPC]);
	putuwd(--usp, regl[OFCW]);
	putuwd(--usp, n);
	regl[OFCW] &= ~MFNVE;
	*(int **)&regl[OPC] = f;
	*(int **)&regl[OR14] = usp;
	if (n!=SIGEPA && n!=SIGTRAP)
		u.u_sfunc[n-1] = SIG_DFL;
}

/*
 * Cause the next instruction to single step.
 * Non-vectored interrupt enable signals assembler
 * code to arm the single-step logic on return
 * from this trap.
 */
msigsin()
{
	regl[OFCW] |= MFNVE;
}

/*
 * Idle process.
 */
idle()
{
	for (;;) {
		disflag = 1;
		_idle();
	}
}

/*
 * Set an interrupt vector.
 * Make an entry in the "vecs" table, for
 * use by the interrupt dispatcher.  No interrupt
 * controller needs to be armed on this machine.
 * Levels must be even on the z8001.
 */
setivec(level, fun)
register int	level;
int		(*fun)();
{
	register vaddr_t t1, t2;
	extern	 int	(*vecs[])();
	extern	saddr_t	vmaps[];
	extern	 int	vret();

	level >>= 1;
	t1 = (vaddr_t)vecs[level];
	t2 = (vaddr_t)vret;
	if ((unsigned)t1 != (unsigned)t2) {
		u.u_error = EDBUSY;
		return;
	}
	vecs[level] = fun;
	vmaps[level] = omapget();
}

/*
 * Clear an interrupt vector.
 */
clrivec(level)
register int	level;
{
	extern	 int	(*vecs[])();
	extern	 int	vret();

	level >>= 1;
	vecs[level] = vret;
}
