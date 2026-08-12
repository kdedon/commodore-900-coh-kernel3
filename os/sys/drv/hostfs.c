/*
 * hostfs -- paravirtual block device backed by a directory on the
 * simulator host (DEVELOPMENT DISTS ONLY; never shipped).
 *
 * The device is a plain COHERENT block device at major index 11, so a
 * host directory rendered as a filesystem image is mounted with the
 * ordinary mount(2) -- no filesystem switch, no change to fs*.c.  The
 * "controller" is the host daemon hostfsd (c900oses/gotools/cmd/hostfsd),
 * which serves block reads and writes out of that image over the
 * simulator's HTTP memory interface.
 *
 * The wire is a single 38-byte mailbox in this driver's data segment.
 * The daemon finds it by scanning physical memory for the magic string,
 * which hfload() writes character by character AT LOAD TIME: the
 * mailbox is BSS, so neither the driver file, nor buffer-cache copies
 * of it, nor this driver's own text contains the string contiguously --
 * only a loaded, initialised instance matches the scan.  hfload() also
 * stores the mailbox's own physical address in
 * the mailbox, computed from this driver's segment (drvl[].d_segp), and
 * the daemon requires that field to equal the address the scan hit --
 * that rules out any stale copy of a previously loaded instance.
 *
 * A request is posted by filling the fields and setting hm_state last;
 * the daemon transfers block data DIRECTLY to and from the buffer's
 * physical address (bp->b_paddr), writes the reply fields, and sets
 * hm_state to done.  The driver busy-waits on hm_state: there is no
 * interrupt, completion is synchronous inside the strategy call, and
 * bdone() is called before hfblock() returns.  Every request is
 * bounded: block I/O moves at most HFMAXIO bytes, and the mailbox
 * itself is fixed-size.  If the daemon is not running the wait expires
 * and the request fails with EIO (open fails with ENXIO), which is the
 * whole of the "no daemon, no export" behaviour: this driver owns no
 * data path of its own.
 *
 * Kernel entry points used: bdone() (bio.c) and drvl[] (con.h), both
 * resolved by `ld -k' against the symboled kernel like every loadable
 * driver; rebuild after any kernel relink.
 */
#include	<coherent.h>
#include	<buf.h>
#include	<con.h>
#include	<seg.h>
#include	<stat.h>
#include	<errno.h>

#define	HFMAJOR	11			/* drvl[] slot (see z8001/con/wdcon.c) */
#define	HFMAXIO	16384L			/* upper bound on one transfer */

/*
 * Mailbox states, operations, ioctl commands.
 */
#define	HFS_IDLE	0		/* free for a new request */
#define	HFS_REQ		1		/* request posted, host owns it */
#define	HFS_DONE	2		/* reply written, guest owns it */

#define	HFO_READ	1		/* host: image block -> guest memory */
#define	HFO_WRITE	2		/* host: guest memory -> image block */
#define	HFO_INFO	3		/* host: fill hm_nblk, hm_hostf */
#define	HFO_SYNC	4		/* host: extract image to directory */

#define	HFIPING		(('h'<<8)|1)	/* ioctl: is the daemon there? */
#define	HFISYNC		(('h'<<8)|2)	/* ioctl: extract to host now */

/*
 * How long to poll before declaring the daemon absent.  The strategy
 * runs at the caller's spl (bread/bwrite raise sphi), so this loop can
 * hold off the clock; the constant is sized to a few seconds of guest
 * time, long enough for a daemon polling every few milliseconds -- and
 * for its slower mailbox re-discovery scan after a driver load -- and
 * short enough that a dead daemon turns into EIO, not a hang.
 */
#define	HFSPIN	8000000L

/*
 * The mailbox.  Fixed width, guest (big-endian) byte order throughout;
 * the daemon decodes it byte for byte.  hm_state is written LAST by
 * whichever side is posting, so a torn read never presents a
 * half-filled request or reply.
 */
struct hfmbox {
	char	hm_magic[8];		/* "C900HFS" + '1' once loaded */
	unsigned hm_state;		/* HFS_* handshake word */
	unsigned hm_op;			/* HFO_* operation */
	unsigned hm_err;		/* host reply: 0 or errno */
	unsigned hm_seq;		/* guest request sequence */
	long	hm_blk;			/* block number in the image */
	long	hm_count;		/* byte count, <= HFMAXIO */
	long	hm_paddr;		/* physical address of the data */
	long	hm_self;		/* physical address of this mailbox */
	long	hm_nblk;		/* host reply to INFO: fs blocks */
	unsigned hm_hostf;		/* host reply to INFO: flags */
};

static struct hfmbox hf_mb;
static unsigned hf_seq;			/* request sequence counter */
static int hf_alive;			/* INFO answered since load */

int	hfload();
int	hfuload();
int	hfopen();
int	hfblock();
int	hfioctl();
int	nulldev();
int	nonedev();

/*
 * Configuration table, installed into drvl[HFMAJOR] by /etc/load.
 * Block for the mount, character for the ioctl path (/dev/rhfs).
 */
CON hfcon = {
	DFBLK|DFCHR,			/* Flags */
	HFMAJOR,			/* Major index */
	hfopen,				/* Open */
	nulldev,			/* Close: iclose() calls dclose on
					 * EVERY special-file close (fs3.c),
					 * so extraction cannot hang off it;
					 * it is explicit via HFISYNC */
	hfblock,			/* Block */
	nonedev,			/* Read */
	nonedev,			/* Write */
	hfioctl,			/* Ioctl */
	nulldev,			/* Powerfail */
	nulldev,			/* Timeout */
	hfload,				/* Load */
	hfuload,			/* Unload */
	nonedev,			/* Poll */
};

/*
 * Post the filled-in mailbox and spin for the host's reply.
 * Returns the host error code, or -1 if the host never answered.
 * Caller has filled hm_op and the operation's argument fields.
 */
static int
hfwait()
{
	register struct hfmbox *mp;
	register long n;

	mp = &hf_mb;
	mp->hm_err = 0;
	mp->hm_seq = ++hf_seq;
	mp->hm_state = HFS_REQ;		/* posted: host owns the box */
	for (n = 0; n < HFSPIN; n++) {
		if (mp->hm_state == HFS_DONE) {
			mp->hm_state = HFS_IDLE;
			return ((int)mp->hm_err);
		}
	}
	mp->hm_state = HFS_IDLE;	/* give up; late reply is ignored */
	return (-1);
}

/*
 * Load: complete the magic (the file holds '0', memory gets '1') and
 * record the mailbox's physical address so the daemon's memory scan can
 * tell this live instance from any stale copy.  d_segp/d_map are set by
 * pload() before c_load runs, and the segment is pinned (SFSYST), so
 * the address stays good for the life of the load.
 */
hfload()
{
	register struct hfmbox *mp;

	mp = &hf_mb;
	mp->hm_state = HFS_IDLE;
	mp->hm_self = drvl[HFMAJOR].d_segp->s_paddr
	    + (long)(unsigned)VOFF((vaddr_t)mp);
	mp->hm_magic[0] = 'C'; mp->hm_magic[1] = '9';
	mp->hm_magic[2] = '0'; mp->hm_magic[3] = '0';
	mp->hm_magic[4] = 'H'; mp->hm_magic[5] = 'F';
	mp->hm_magic[6] = 'S';
	mp->hm_magic[7] = '1';		/* BSS in the file: never matches there */
}

/*
 * Unload: spoil the magic so a scan never matches the freed segment.
 */
hfuload()
{
	hf_mb.hm_magic[7] = '0';
	hf_alive = 0;
}

/*
 * Open: sole minor is 0.  The first open asks the host for INFO; no
 * answer means no daemon (or no exported directory), and the open
 * fails with ENXIO rather than letting a mount hang on dead air.
 */
hfopen(dev, m)
dev_t dev;
int m;
{
	if (minor(dev) != 0) {
		u.u_error = ENXIO;
		return;
	}
	if (hf_alive == 0) {
		hf_mb.hm_op = HFO_INFO;
		if (hfwait() != 0) {
			u.u_error = ENXIO;
			return;
		}
		hf_alive = 1;
	}
}

/*
 * Strategy.  The host moves the data straight to or from the buffer's
 * physical address; nothing is copied through the mailbox, so the
 * request is a fixed 28 bytes no matter the transfer.  Completion is
 * synchronous: bdone() runs before return, and bread()'s sleep loop
 * finds the buffer already done.
 */
hfblock(bp)
register BUF *bp;
{
	register struct hfmbox *mp;

	mp = &hf_mb;
	if (bp->b_count > HFMAXIO || bp->b_count == 0
	    || (bp->b_req != BREAD && bp->b_req != BWRITE)) {
		bp->b_err = EINVAL;
		goto bad;
	}
	mp->hm_op = (bp->b_req == BREAD) ? HFO_READ : HFO_WRITE;
	mp->hm_blk = bp->b_bno;
	mp->hm_count = bp->b_count;
	mp->hm_paddr = bp->b_paddr;
	if (hfwait() != 0) {
		bp->b_err = EIO;
		goto bad;
	}
	bp->b_resid = 0;
	bdone(bp);
	return;
bad:
	bp->b_flag |= BFERR;
	bp->b_resid = bp->b_count;
	bdone(bp);
}

/*
 * Ioctl: PING answers whether the daemon is alive; SYNC asks it to
 * extract the image back to the host directory now.  The hostfs(1)
 * tool calls sync(2) first so the extraction sees what the guest wrote.
 */
hfioctl(dev, com, vec)
dev_t dev;
int com;
char *vec;
{
	switch (com) {
	case HFIPING:
		hf_mb.hm_op = HFO_INFO;
		break;
	case HFISYNC:
		hf_mb.hm_op = HFO_SYNC;
		break;
	default:
		u.u_error = EINVAL;
		return;
	}
	if (hfwait() != 0)
		u.u_error = EIO;
}
