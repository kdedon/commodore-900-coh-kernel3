/*
 * Commodore M-series Z8001 -- kernel configuration variables.
 *
 * FORWARD-PORT: the 3.2 MI made the pool sizes ADJUSTABLE parameters
 * ("set in *con.c", extern int in <sys/param.h>) where 0.7.3 compiled
 * them in as <param.h> macros.  The 0.7.3 MD layer (commodore.c) still
 * sizes the physical pools with those macros, so the variables here MUST
 * carry the same values the 0.7.3 param.h macros expand to -- keep them
 * in lock-step until the MD layer is uplifted to read the variables.
 *
 * Also collects the small kernel-support definitions the 3.2 MI expects
 * from the machine/boot layer.
 */
#include <sys/coherent.h>
#include <sys/typed.h>

int	NBUF	= 40;		/* buffer cache (== 0.7.3 param.h NBUF) */
int	NHASH	= 37;		/* buffer hash headers */
int	NINODE	= 100;		/* in-core inode table (== 0.7.3) */
int	NCLIST	= 32;		/* character lists (== 0.7.3) */
/*
 * The kalloc arena.  ONE pool serves the whole kernel: buffer headers (NBUF
 * x sizeof(BUF)), the pty pool (NUPTY x sizeof(PTY)), and then, per live
 * object, PROC, SEG, FD, MOUNT and the poll(2) event clusters.  There is no
 * separate process or file table, so its size IS the process, open-file and
 * poll ceiling -- and it is reached silently: alloc() returns NULL and the
 * caller reports EAGAIN from fork(2) or open(2).  24576 covers a heavy
 * load (two dozen processes, three mounts, sixty open files) plus a
 * sixteen-window pty session, with room for fragmentation.
 *
 * MUST stay well under 32768: ALLSIZE is an int, commodore.c computes
 * bruc(p1+ALLSIZE), and setarena()'s second parameter is an undeclared int
 * that indexes &cp[n].  A negative value there hands out the whole segment.
 */
int	ALLSIZE	= 24576;	/* kalloc arena (0.7.3 had 10240) */
/* ISTSIZE lives in the MI var.c (= 4096; the 0.7.3 kernel used 256) */

saddr_t	uasa;			/* currently active u-area segment (3.2
				 * proc bookkeeping; maintained by the MD
				 * context switch once C0.4 lands) */

char	*udl	= 0;		/* loadable-driver space limit: none loaded */

/* mactype (the l.out l_machine exec checks) is defined in the con
 * file (wdcon.c), as in 0.7.3 */

int	realmode = 0;		/* the Z8001 has no real mode; the 3.2 MI
				 * only prints "Real"/"Protected" from it */

/*
 * Boot gift: the i286 boot loader hands the kernel a typed fifo of boot
 * parameters.  The C900 ROM passes nothing -- an empty (zeroed) gift makes
 * fifo_open() yield F_NULL and the MI skips it.
 */
typed_space boot_gift;

/*
 * Kernel one-word library: the 3.2 MI calls these where 0.7.3 used its
 * own idioms.
 */
char *
memcpy(dst, src, n)
register char *dst, *src;
register unsigned n;
{
	char *r;

	r = dst;
	while (n-- != 0)
		*dst++ = *src++;
	return (r);
}

/*
 * Copy kernel data to a far virtual address: kfcopy(src, far-dst, n)
 * (see proc.c -- the context block is copied INTO the far u-area).  On
 * the large-model Z8001 a far address is an ordinary pointer, so this
 * is exactly kkcopy(from, to, n).
 */
kfcopy(src, dst, n)
char *src, *dst;
unsigned n;
{
	kkcopy(src, dst, n);
}
