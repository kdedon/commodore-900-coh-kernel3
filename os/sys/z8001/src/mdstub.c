/*
 * Commodore M-series Z8001 -- machine-dependent entry points the 3.2 MI
 * kernel requires that the 0.7.3 MD layer does not yet supply.
 *
 * All entries are REAL now (the panic-stub phase is over): the
 * deferred-call queue, read_cmos (time-0 until the M58321 RTC hookup),
 * the s_faddr transient-window convention (vremap/vrelse), eveinit, and
 * the loadable-driver syscall stubs (clean ENXIO until ldrv support).
 * The exec loader is in exec.c, physical copies in pcopy.c, compaction
 * in krunch.c.
 */
#include <sys/coherent.h>
#include <sys/inode.h>
#include <sys/seg.h>
#include <sys/proc.h>
#include <sys/uproc.h>
#include <sys/mmu.h>
#include <errno.h>

/*
 * Deferred function calls (3.2: i286 defer.s, "ported from RTX").
 * defer(f, a) / defer0(f, a, b) queue a call from interrupt level;
 * defend() -- called by stand()/the clock at base level -- drains the
 * queue.  A C ring buffer under sphi() is correct on a uniprocessor;
 * the i286 used asm only to avoid the spl round trip.
 */
#define	NDEFER	64

static struct dfr {
	int	(*d_fn)();
	char	*d_a;
	char	*d_b;
} dfrq[NDEFER];
static int dfrin, dfrout;

defer0(fn, a, b)
int (*fn)();
char *a, *b;
{
	register int s;

	register int nin;
	static int dropped;

	s = sphi();
	/*
	 * Queue full: drop the call with a rate-limited diagnostic.  An
	 * interrupt burst must not panic the machine (4.x k75/k76 defer
	 * overflow policy); the losing driver retries off its next event.
	 */
	nin = dfrin + 1;
	if (nin >= NDEFER)
		nin = 0;
	if (nin == dfrout) {
		if (dropped++ < 4)
			printf("\nDefer overflow f=%p a=%p b=%p ", fn, a, b);
		spl(s);
		return;
	}
	dfrq[dfrin].d_fn = fn;
	dfrq[dfrin].d_a = a;
	dfrq[dfrin].d_b = b;
	dfrin = nin;
	spl(s);
}

defer(fn, a)
int (*fn)();
char *a;
{
	defer0(fn, a, (char *)0);
}

defend()
{
	register struct dfr *dp;
	register int s;

	for (;;) {
		s = sphi();
		if (dfrin == dfrout) {
			spl(s);
			return;
		}
		dp = &dfrq[dfrout];
		if (++dfrout >= NDEFER)
			dfrout = 0;
		spl(s);
		(*dp->d_fn)(dp->d_a, dp->d_b);
	}
}

/*
 * Boot-time clock: the i286 reads the AT CMOS RTC.  The C900's M58321
 * RTC hookup rides with the clock driver retarget (C0.6); returning 0
 * boots with time zero (set the date from userland).
 */
read_cmos(reg)
{
	return (0);
}

/* pexece/exlopen/exsread/exstack and the loadable-driver pair pload/
 * puload all live in exec.c (the ported 0.7.3 loader). */

/* krunch() lives in krunch.c (real implementation) */

/*
 * s_faddr on the Z8001 -- THE PORT CONVENTION (verified against
 * loadmmu/conrest/msetsys and the 0.7.3 proc.c):
 *
 * The i286 gave every SEG a persistent kernel selector in s_faddr; the
 * Z8010 has ~64 descriptors total, so this machine keeps the 0.7.3
 * TRANSIENT-window model instead.  We define:
 *
 *	FP_SEL(s_faddr) = the segment's physical CLICK BASE (saddr_t)
 *	FP_OFFW(s_faddr) = 0
 *
 * which makes the MI's MD-flavored calls work with the UNCHANGED 0.7.3
 * machine layer: conrest(FP_SEL(u->s_faddr), off) and
 * msetsys(&mcon, f, FP_SEL(sp->s_faddr)) receive exactly the saddr_t
 * click base the 0.7.3 md.s/commodore.c versions take (0.7.3 passed
 * s_mbase there), and the MI's "FP_SEL != 0 means mapped" tests hold
 * (RAM starts at click 0x200).  s_faddr is NOT a dereferenceable
 * pointer here -- the two MI sites that wrote through it (proc.c
 * u_syscon setup) use psegcopy() (pcopy.c) under _Z8001, as does
 * eveinit below; 0.7.3 did the same via kscopy().
 *
 * vremap recomputes the click base after a segment moves (krunch,
 * swap); vrelse has nothing to release.  seg.c's swapio() builds a
 * transient swapseg and hands b_faddr = s_faddr to the disk driver,
 * but the wd driver reads b_paddr, which swapio sets per chunk, so
 * the click base is never dereferenced on the swap path.
 */
void
vremap(sp)
SEG *sp;
{
	FP_SEL(sp->s_faddr) = btocrd(sp->s_paddr);
	FP_OFFW(sp->s_faddr) = 0;
}

vrelse(f)
faddr_t f;
{
}

/* swap I/O helpers (C0.3): exec-from-swap read, phys<->phys copies */
/* pclear/plrcopy/prlcopy live in pcopy.c (real implementations) */

/*
 * Set up the first process: a small program (embedded in md.s as
 * icodep/icodes, idatap/idatas) which execs /etc/init.  Ported from
 * the i286 exec.c -- the body is machine-independent given salloc/
 * kfcopy/vremap/sproto/segload.
 */
/*
 * WHAT THE LOADER ASKED FOR, as two flags rather than as a bootinfo block.
 *
 * wd(4) reads and validates the handoff (sys/z8001/drv/wd.c wdload) and sets
 * these; nothing else may.  They are ints and not the struct because the code
 * that ACTS on them is bootpatch() below, which patches init's argument vector
 * -- and that is machine-dependent startup, not a consumer of a versioned
 * on-disk-ish interface.  Keeping the header out of here means a bootinfo
 * version bump touches the one file that validates the block and no other.
 *
 * Both zero is the ordinary boot, and both are zero when no loader handed
 * anything over, when the kernel carries no wd(4) at all, and when the loader
 * is too old to have the fields.  Absence is a configuration.
 */
int	bootsingle;		/* BF_SINGLE: come up single user */
int	bootser;		/* BI_CON_SER: the operator is on the serial line */

/*
 * Patch init's argument vector with what the loader said, in the KERNEL's copy
 * of the icode, before eveinit() copies it into the first process.
 *
 * md.s exports the two argv slots that may be patched and the two offsets that
 * may go into them; everything here is an icode-relative offset, because the
 * blob is about to be copied to a different address and a pointer would not
 * survive the move.  vidsel (md.s, at `start') has already had its say about
 * argv[2] from the video PROBE -- this runs later and overrides it, which is
 * the right order: the probe answers "is a board fitted", the loader answers
 * "where is the person", and only the second one can be wrong about which
 * screen somebody is looking at.
 *
 * Called from eveinit() rather than from main() so that it cannot run before
 * devinit() has settled the handoff: main() calls devinit() (which reaches
 * wdload) well before eveinit(), and a patch applied earlier would read flags
 * nobody had validated yet.
 */
extern unsigned short	icarg1, icarg2, icflsu, icdrvno;

static
bootpatch()
{
	if (bootsingle)
		icarg1 = icflsu;
	if (bootser)
		icarg2 = icdrvno;
}

eveinit(sp)
SEG *sp;
{
	register PROC *pp;

	SELF = pp = eprocp;
	bootpatch();

	/*
	 * Record user area.
	 */
	pp->p_segp[SIUSERP] = sp;

	/*
	 * Start process 1 with an empty user area.  main.c allocates it
	 * uncleared -- salloc hands back corebot, the memory md.s already
	 * mapped as SS, so clearing the segment would wipe the live kernel
	 * stack in its top half -- and what is left there is whatever the ROM
	 * memory test wrote.  Only the struct is cleared; the stack grows down
	 * from &u+UPASIZE and is never reached, since the kernel already
	 * requires its depth to stay clear of the struct.
	 *
	 * Uncleared, three fields alone are enough to lose the boot: u_uid
	 * decides super(), so a nonzero one fails the driver load and the
	 * console is never opened, and the first fork() walks u_filep[],
	 * u_rdir and u_cdir without testing them.  fsminit() sets the two
	 * directories afterwards; sproto() sets u_sproto below.
	 */
	kclear((char *)&u, sizeof(UPROC));

	/*
	 * Allocate, record, initialize code segment, make it executable.
	 */
	if ((sp = salloc((fsize_t)icodes, 0)) == NULL)
		panic("eveinit(code)");
	pp->p_segp[SIPTEXT] = sp;
	psegcopy(icodep, sp, 0, icodes);
	sp->s_flags |= SFTEXT;
	vremap(sp);

	/*
	 * Allocate, record, and initialize data segment.  The Z8001 md.s
	 * embeds the init program as a SINGLE blob (icodep/icodes, the
	 * 0.7.3 model); the 3.2 code/data split (idatap/idatas, var.c)
	 * is zero here -- skip the empty data segment.
	 */
	if (idatas != 0) {
		if ((sp = salloc((fsize_t)idatas, 0)) == NULL)
			panic("eveinit(data)");
		pp->p_segp[SIPDATA] = sp;
		psegcopy(idatap, sp, 0, idatas);
	}

	/*
	 * Allocate and record stack segment.
	 */
	if ((sp = salloc((fsize_t)UPASIZE, SFDOWN)) == NULL)
		panic("eveinit(stack)");
	pp->p_segp[SISTACK] = sp;

	/*
	 * Start process.
	 */
	u.u_argp = 0;
	if (sproto() == 0)
		panic("eveinit(sproto)");
	segload();

	/*
	 * Last of all, the swapper (swap.c).  Here rather than in seginit()
	 * because it is a process and this is the first point at which making
	 * one is safe: main() has taken pid 0 for idle and pid 1 for init,
	 * and SELF and iprocp are both real, so the clock -- already running
	 * -- can preempt the rest of startup and find its way back.  swapinit
	 * checks all of that itself; see the ordering note there.
	 *
	 * Raising sexflag is also what stands krunch() down, so it happens
	 * after this process's own segments are built and its prototype is
	 * loaded, and not one line earlier.
	 */
	swapinit();
}

/* loadable-driver periodic entry (3.2 ldrv; none loaded) */
ld_call()
{
}

/*
 * In-kernel debugger hook.  ddt.c supplies the real one when the kernel is
 * built KDDT=1 (link-kernel.sh); without it the fatal paths fall through to
 * panic, which is what an unattended machine wants.
 */
#if !DDT
ddt(sig)
{
}
#endif

/*
 * Boot-gift FIFO reader (3.2 i386 BIOS argument passing; fifo_*.c and
 * arg_exist.c are out of the link -- the Z8001 ROM passes no gift).
 * fifo_open returning F_NULL (0) keeps the only caller, main.c rpdev(),
 * on its empty-gift path; fifo_read/fifo_close sit behind that check
 * and are never reached.
 */
char *
fifo_open(tsp, mode)
char *tsp;
{
	return (0);
}

char *
fifo_read(ffp)
char *ffp;
{
	return (0);
}

fifo_close(ffp)
char *ffp;
{
}
