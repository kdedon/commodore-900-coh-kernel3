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
#include <sys/bootinfo.h>
#include <sys/memmap.h>

/* Z8010 descriptor attribute: CPU inhibit. */
#define	HA_CPUI	0x04

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

extern struct bootinfo bootinfo;	/* the handoff block, defined with wd(4)'s
					 * built-in partition table */

static char	memmap[MM_NSEG];	/* 1: this segment is memory to allocate */
static char	memsrc;			/* 1 the loader's map, 2 its map refused */
static char	memcrowd;		/* runs dropped for want of slots */
static unsigned	memover;		/* segments refused above the ceiling */

/*
 * Size memory as the LIST of runs it is, and set corebot/coretop to the lowest
 * and highest addresses in it.  What lies between two runs is a framebuffer, or
 * something the loader kept back, and seginit() pins it so that nothing is ever
 * allocated there.
 */
static
memsize()
{
	struct biram run[NRAMRUN];
	unsigned bot;
	unsigned top;
	register int i;
	int over;

	/*
	 * The ROM counts clicks, 64 of them to a segment.  Round the base up
	 * and the top down: part of a segment is not a segment the memory test
	 * proved.  Above MM_NSEG the MMU base arithmetic (btocrd() << CSH) no
	 * longer fits a saddr_t, so memory there is refused rather than wrapped
	 * onto memory that is in use; the motherboard decodes only to 0x3F.
	 */
	bot = (unsigned)((ctob((paddr_t)romconf.rom_bram) + 0xFFFFL) >> 16);
	top = (unsigned)(ctob((paddr_t)romconf.rom_eram) >> 16);
	if (top > MM_NSEG) {
		memover = top - MM_NSEG;
		top = MM_NSEG;
	}
	mmbuild(memmap, bot, top, romconf.rom_hires != 0);
	/* A map the loader handed over wins, having subtracted what its own
	 * configuration reserved as well; one that is not of this machine is
	 * refused, and the map built here stands. */
	if ((i = mmtake(&bootinfo, memmap)) != 0)
		memsrc = i < 0 ? 2 : 1;
	nramrun = mmruns(memmap, run, NRAMRUN, &over);
	memcrowd = over;
	if (nramrun == 0)
		panic("No memory");
	for (i = 0; i < nramrun; i++) {
		ramrun[i].ra_bot = (paddr_t)run[i].br_first << 16;
		ramrun[i].ra_top = ramrun[i].ra_bot
				 + ((paddr_t)run[i].br_count << 16);
	}
	corebot = ramrun[0].ra_bot;
	coretop = ramrun[nramrun-1].ra_top;
}

/*
 * What memsize() made of memory.  It runs before a console has been chosen, so
 * it records and this prints, as vidsel and hrreport() do.
 */
memreport()
{
	register int i;

	printf("memory: %s map:", memsrc == 1 ? "loader's" : "built-in");
	for (i = 0; i < nramrun; i++)
		printf(" %X-%X", ramrun[i].ra_bot, ramrun[i].ra_top - 1);
	printf("\n");
	if (memsrc == 2)
		printf("memory: the loader's map is not of this machine -- built-in used\n");
	if (memover != 0)
		printf("memory: %u segments refused above 16M\n", memover);
	if (memcrowd != 0)
		printf("memory: more runs than slots -- the high ones dropped\n");
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
	register int i;
	paddr_t pb;

	/*
	 * Segment 1 is ROM DS.
	 * It contains the romconf structure
	 * at offset of 0.
	 */
	t = (char *)ADDR(1, 0);
	t = *(int **)t;
	kkcopy(t, &romconf, sizeof romconf);
	memsize();
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
	p1 = corebot;			/* 3.2: paddr_t = BYTES */
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
	/* Free memory is what the runs hold above the kernel, not the span from
	 * corebot to coretop: the framebuffers lie inside that span. */
	msize = 0;
	for (i = 0; i < nramrun; i++) {
		pb = ramrun[i].ra_bot < corebot ? corebot : ramrun[i].ra_bot;
		if (pb < ramrun[i].ra_top)
			msize += btocrd(ramrun[i].ra_top - pb);	/* KB */
	}
	/*
	 * Map the bit map, and the window system's shared tail behind it.
	 * md.s gives GDS its limit and its user attribute; the base is a
	 * physical address, so it is programmed the way every other mapping
	 * segment's is.
	 */
	pfix(BMS, BMPHYS);
	pfix(BMS+1, BMPHYS+0x00010000L);
	pfix(GDS, GDSPHYS);
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
	/*
	 * The map is sparse, so inhibit every descriptor first; a stale one
	 * could expose physical 0 or the previous image.
	 */
	for (i=0; i<NHUSEG; i++) {
		u.u_sproto.mp_hsegs[i].mp_base = 0;
		u.u_sproto.mp_hsegs[i].mp_len = 0;
		u.u_sproto.mp_hsegs[i].mp_attr = HA_CPUI;
	}
	sn = USTACK;
	for (i=SISTACK; i<=SIPDATA; i++) {
		if ((sn = uproto(pp->p_segp[i], sn, i)) < 0)
			return (0);
		if (i == SISTACK)
			sn = USEG;
	}
	/*
	 * Shared libraries map at the fixed pair they were relocated to.
	 * The heap ceiling is dropped while placing each, since a library
	 * sits at the ceiling.
	 */
	for (i = 0; i < NSLIB; i++) {
		if (pp->p_segp[SISLIB+2*i] == NULL)
			continue;
		if ((sn = slsegof(pp->p_segp[SISLIB+2*i])) < 0) {
			u.u_error = ENOEXEC;
			return (0);
		}
		u.u_sproto.mp_hceil = 0;
		if (uproto(pp->p_segp[SISLIB+2*i], sn, SISLIB+2*i) < 0
		 || uproto(pp->p_segp[SISLIB+2*i+1], sn+1, SISLIB+2*i+1) < 0) {
			u.u_error = ENOMEM;
			return (0);
		}
		u.u_sproto.mp_hceil = slceil0();
	}
	return (1);
}

/*
 * The heap ceiling: the lowest library segment this process maps.
 */
static
slceil0()
{
	register PROC *pp;
	register int i, s, c;

	pp = SELF;
	c = 0;
	for (i = 0; i < NSLIB; i++) {
		if (pp->p_segp[SISLIB+2*i] == NULL)
			continue;
		if ((s = slsegof(pp->p_segp[SISLIB+2*i])) > 0
		 && (c == 0 || s < c))
			c = s;
	}
	return (c);
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
	 * `u' is the swapper's).  Compaction moves memory under a stable
	 * segment set in the same way, so this rebuild serves both it and the
	 * swapper.  A kernel with no swap extent leaves sexflag clear and
	 * pays nothing.
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
			return (-1);
		f |= 0x20;		/* Downward growing segment */
		hl = MSSIZE-l;
		b -= hl;
		base = ctob(hl);
	}
	b <<= CSH;
	/* Refuse, before writing anything, a segment past the heap ceiling. */
	hl = u.u_sproto.mp_hceil;
	if (hl == 0 || hl > NHUSEG)
		hl = NHUSEG;
	if (sn + (l+MSSIZE-1)/MSSIZE > hl)
		return (-1);
	u.u_segl[si].sr_base = ADDR(sn, base);
	while (l > 0) {
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
 */
msetusr(pc, sp)
vaddr_t pc;
vaddr_t sp;
{
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
 * Cancel a pending single step.
 */
msigsoff()
{
	regl[OFCW] &= ~MFNVE;
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
