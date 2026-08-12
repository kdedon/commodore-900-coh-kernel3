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
 * DTC 535AS hard disk & floppy disk driver for the Commodore Z8000M.
 * This is yet another sasi driver with complications for funny hardware.
 * Added 5.25" floppy support....
 *
 * LUN 0 = hard disk, LUNs 1 and 2 = floppy disk.....
 * October, 1983.
 */

/* 
 * default drives are 8" floppy and full height winchester.
 * to use 5.25" floppy, define FIVEINCH.
 * to use half height wini, define HALFHIGH.
 */


#include	<coherent.h>
#include	<buf.h>
#include	<con.h>
#include	<stat.h>
#include	<uproc.h>
#include	<errno.h>

int	hdload();
int	hdopen();
int	hdread();
int	hdwrite();
int	hdblock();
int	hdintr();
int	nulldev();
int	nonedev();

CON	hdcon	= {
	DFBLK|DFCHR,			/* Flags */
	6,				/* Major index */
	hdopen,				/* Open */
	nulldev,			/* Close */
	hdblock,			/* Block */
	hdread,				/* Read */
	hdwrite,			/* Write */
	nonedev,			/* Ioctl */
	nulldev,			/* Powerfail */
	nulldev,			/* Timeout */
	hdload,				/* Load */
	nulldev				/* Unload */
};

/*
 * DTC port offsets...
 */
#define	HDIO	0x0600			/* Data Register (word mode) */
#define	HDCS  	0x0641			/* command & status port */
#define	HDRESET	0x06c0			/* Reset (WO) */
#define	HDSEL	0x0680			/* Select pulse (WO) */
#define	HDMASK	0x06c0			/* Mask (WO) */
#define	HDSSTAT	0x0680			/* SASI status lines (RO) */

#define	HDREAD	0x08			/* Read */
#define	HDWRITE	0x0A			/* Write */
#define	HDRSS	0x03			/* Request status */

#define	HDSMSG	0x02			/* Message phase */
#define	HDSREQ	0x08			/* Request from Xebec */
#define	HDSIO	0x10			/* In/out */
#define	HDSCD	0x04			/* Command/data */
#define	HDSBUSY	0x01			/* Busy */
#define	HDSERR	0x02			/* Error bit */

#define	HDIRQ	0xC0			/* base of hd interrupt vectors */

#ifdef	FIVEINCH
char	fiveinch = 1;
#endif
#ifdef	HALFHIGH
char	halfhigh = 1;
#endif

/*
 * This structure holds the disk parameters for the disk drive
 * being used on the controller. As drives are added, the hdload()
 * routine will have to be modified to accomodate them.
 */

struct	hdp	{
	unsigned char hdp_idc[10];	/* Data block for init hd cmd */
	unsigned char hdp_control;	/* Control byte */
	long	hdp_size;		/* Size of device, blocks */
	int	hdp_nparts;		/* Number of minor partitions */
}	hdp[]	= {
#if HALFHIGH
	{ { 1, 1, 0, 1, 2, 98, 0, 0, 0, 0 }, 0x00, 20672, 5 },
#else
	{ { 1, 1, 0, 3, 1, 49, 0, 0, 0, 0 }, 0x00, 20672, 5 },
#endif
#if FIVEINCH
	{ { 31, 31, 39, 35, 102, 51, 11, 0x80, 0, 0 }, 0x00, 640, 1 }
#else
	{ { 0,0,0,0,0,0,0,0,0,0 }, 0x00, 500, 1 }	/* floppy params */
#endif
};

/*
 * Minor # bits:	210	partition or f.s. number
 *			43	drive number
 *			765	drive type
 */

#define drive(n)	((minor((n))>>3)&0x03)
#define partition(n)	(minor((n))&0x07)
#define	drivetype(n)	(minor((n))>>5)

struct mintab {
	long minbase;
	long minsize;
} mintab[] = {
	0,	5168,
	5168,	5168,
	10336,	5168,
	15504,	5168,
	0,	20672
};

/*
 * Per disk controller data.
 * Only one controller; no more, no less.
 */
struct	hd	{
	BUF	*hd_actf;		/* Link to first */
	BUF	*hd_actl;		/* Link to last */
	paddr_t	hd_addr;		/* DMA address */
	long	hd_secn;		/* Sector # */
	int	hd_nsec;		/* # of sectors */
	char	hd_unit;		/* Unit # */
	char	hd_busy;		/* Busy flag */
	char	hd_iswr;		/* Is a write flag */
}	hd;

BUF	hdbuf;				/* For raw I/O */
struct	hdp *hdparm();			/* For C */

/*
 * Load. The controller is
 * reset and the interrupt vector is
 * grabbed. The drive characteristics are
 * set up at this time.
 */
hdload()
{
	register int	i;
	register int	j;
	register int	status;
	unsigned char	cmd[6];

	out(HDRESET, 0);
	out(HDRESET, 1);
	cmd[0] = 0xc2;			/* assign drive params for hd */
	cmd[1] = 0<<5;			/* unit # */
	cmd[2] = cmd[3] = cmd[4] = cmd[5] = 0;
	hdsendcmd(cmd, 0);
	while ((in(HDSSTAT) & 0x1F) != 0x16)
		;
	for (i = 0; i < 10; i += 2)
		out(HDIO, (hdp[0].hdp_idc[i]<<8) | hdp[0].hdp_idc[i+1]);
	status = hdstatus();
	for (j = 1; j <= 2; j++) {
		cmd[0] = 0xC0;			/* define floppy track format */
		cmd[1] = j<<5;			/* unit # */
		cmd[2] = cmd[3] = cmd[4] = 0;
#if FIVEINCH
		cmd[5] = 0x8B;			/* DS, DD, 512 bytes/sector */
#else
		cmd[5] = 0x00;			/* SS, SD, 128 bytes/sector */
#endif
		hdsendcmd(cmd, 0);
		status |= hdstatus();
#if FIVEINCH
		cmd[0] = 0xC0;			/* turn off double step function */
		cmd[1] = j<<5;
		cmd[2] = 1;
		cmd[3] = cmd[4] = cmd[5] = 0;
#else
		cmd[0] = 0xC1;			/* define floppy drive type */
		cmd[1] = j<<5;			/* unit # */
		cmd[2] = cmd[3] = cmd[5] = 0;
		cmd[4] = 2;			/* SA800 - 8 ms step rate */
#endif
		hdsendcmd(cmd, 0);
		status |= hdstatus();
#if FIVEINCH
		cmd[0] = 0xc2;		/* assign drive params for floppy */
		cmd[1] = j<<5;		/* unit # */
		cmd[2] = cmd[3] = cmd[4] = cmd[5] = 0;
		hdsendcmd(cmd, 0);
		while ((in(HDSSTAT) & 0x1F) != 0x16)
			;
		for (i = 0; i < 10; i += 2)
		    out(HDIO, (hdp[1].hdp_idc[i]<<8) | hdp[1].hdp_idc[i+1]);
		status |= hdstatus();
#endif
	}
	cmd[0] = 1;			/* recal drives */
	for (i = 1; i < 6; i++)
		cmd[i] = 0;
	hdsendcmd(cmd, 0);		/* for LUN 0 (hard disk) */
	status |= hdstatus();
	cmd[1] = 1<<5;
	hdsendcmd(cmd, 0);		/* for LUN 1 (floppy disk) */
	hdstatus();			/* ignore errors */
	cmd[1] = 2<<5;
	hdsendcmd(cmd, 0);		/* for LUN 2 (floppy disk) */
	hdstatus();			/* ignore errors */
	if ((status & HDSERR) != 0)
		panic("hdload()");
	for (i = 0; i < 64; i += 2)
		setivec(HDIRQ+i, hdintr);
}

/*
 * Open routine.
 * Check that the minor is
 * in range.
 */
hdopen(dev, mode)
dev_t	dev;
{
	if (drive(dev) > 2
	 || partition(dev) >= hdparm(drive(dev))->hdp_nparts) {
		u.u_error = ENXIO;
		return;
	}
}

/*
 * The read routine just calls
 * off to the common raw I/O processing
 * code, using a static buffer header in
 * the driver.
 */
hdread(dev, iop)
dev_t	dev;
IO	*iop;
{
	ioreq(&hdbuf, iop, dev, BREAD, BFIOC|BFBLK|BFRAW);
}

/*
 * The write routine is just like the
 * read routine, except that the function code
 * is write instead of read.
 */
hdwrite(dev, iop)
dev_t	dev;
IO	*iop;
{
	ioreq(&hdbuf, iop, dev, BWRITE, BFIOC|BFBLK|BFRAW);
}

/*
 * Queue a block to the disk. Make
 * sure that the count is modulo 512, and
 * that the transfer is within the partition
 * of the disk.
 */
hdblock(bp)
register BUF	*bp;
{
	register struct	hdp *hdp;

	hdp = hdparm(drive(bp->b_dev));
	if ((bp->b_count&0x1FF) != 0
	|| bp->b_bno+(bp->b_count>>9) > mintab[partition(bp->b_dev)].minsize) {
		bp->b_flag |= BFERR;
		bdone(bp);
		return;
	}
	bp->b_actf = NULL;
	if (hd.hd_actf == NULL)
		hd.hd_actf = bp;
	else
		hd.hd_actl->b_actf = bp;
	hd.hd_actl = bp;
	if (hd.hd_busy == 0) {
		hddequeue();
		hdstart();
	}
}

/*
 * Pull some work from the
 * disk queue. True return if
 * successful.
 */
hddequeue()
{
	register BUF	*bp;

	if ((bp=hd.hd_actf) == NULL)
		return (0);
	hd.hd_iswr = 0;
	if (bp->b_req == BWRITE)
		++hd.hd_iswr;
	hd.hd_unit = drive(bp->b_dev);
	hd.hd_addr = bp->b_paddr;
	hd.hd_secn = mintab[partition(bp->b_dev)].minbase+bp->b_bno;
	hd.hd_nsec = bp->b_count >> 9;
	return (1);
}

/*
 * Start or restart the disk.
 */
hdstart()
{
	register struct	hdp *hdp;
	register long	dsecn;
#if FIVEINCH
	register int	dtrkn;
	register int	dheadn;
#endif
	char		cmd[6];

	hdp = hdparm(hd.hd_unit);
#if FIVEINCH
	dsecn = hd.hd_secn;
	if (hd.hd_unit != 0) {		/* floppy disk ? */
		dheadn = 0;
		if (dsecn >= 320L) {	/* 8 sectors * 40 tracks */
			dsecn -= 320L;
			dheadn = 1;
		};
		dtrkn = 2 * (dsecn / 8) + dheadn; /* convert ibm# to DTC */
		dsecn %= 8;		/* get sector offset on track */
		dsecn += (dtrkn * 8);	/* add track offset into LAD */
	}
#else
	dsecn = (hd.hd_unit == 0) ? hd.hd_secn : (hd.hd_secn<<2);
#endif
	cmd[0] = hd.hd_iswr ? HDWRITE : HDREAD;	/* Opcode */
	cmd[1] = (hd.hd_unit<<5) | (dsecn>>16);  /* Unit and LAD 2 */
	cmd[2] = ((int) dsecn) >> 8;	/* LAD 1 */
	cmd[3] = ((int) dsecn);		/* LAD 0 */
#if FIVEINCH
	cmd[4] = 1;
#else
	cmd[4] = (hd.hd_unit == 0) ? 1 : 4;	/* 512 byte block */
#endif
	cmd[5] = hdp->hdp_control;		/* misc controller bits */
	hdintenable();				/* let in the lions... */
	hdsendcmd(cmd, 0);
	++hd.hd_busy;				/* Say we are running */
}

/*
 * Interrupt routine.
 * Lock out interrupts, then
 * wake up the top level, if the
 * state is ok.
 */
hdintr(id)
{
	register BUF	*bp;
	register int	s;
	register int	status;

#if VERBOSE
	printf("hd:%x,%d,b=%d%c\n", id, hd.hd_busy, (int)hd.hd_secn,
		(hd.hd_iswr != 0) ? 'W' : 'R');
#endif
	id &= 0x1F;				/* mask off nonsense */
	if (hd.hd_busy != 0) {
		bp = hd.hd_actf;
		hd.hd_busy = 0;
		if (id == 0x06) 		/* ready to read */
			readblk(HDIO, pfix(ES, hd.hd_addr), 512);
		else if (id == 0x16)
			writeblk(HDIO, pfix(ES, hd.hd_addr), 512);
		else {
			s = sphi();
			hdstatus();
			hdintenable();
			spl(s);
			return;
		}
		s = sphi();
		status = hdstatus();
		if ((status&HDSERR)!=0 && hderror()!=0) {
			bp->b_flag |= BFERR;
			hd.hd_actf = bp->b_actf;
			bdone(bp);
			if (hddequeue() != 0)
				hdstart();
		} else if (--hd.hd_nsec == 0) {
			hd.hd_actf = bp->b_actf;
			bdone(bp);
			if (hddequeue() != 0)
				hdstart();
		} else {
			++hd.hd_secn;
			hd.hd_addr += 512;
			hdstart();
		}
		spl(s);
	}
}

/*
 * This routine reads the
 * status bytes at the end of a disk
 * command. If an error is detected it
 * does a read status command to find out
 * if the error is a soft ECC error. It
 * returns true if a "real" error happens.
 */
hderror()
{
	register int	i;
	char		cmd[6];
	char		status[4];

	cmd[0] = HDRSS;
	cmd[1] = hd.hd_unit<<5;
	for (i=2; i<6; ++i)
		cmd[i] = 0;
	hdsendcmd(cmd, 0);
	for (i=0; i<4; ++i) {
		while ((in(HDSSTAT)&HDSREQ) != 0)
			;
		status[i] = inb(HDCS);
	}
	while ((in(HDSSTAT)&HDSREQ) != 0)
		;
	if ((inb(HDCS)&HDSERR) != 0) {
		printf("hd%d: error reading status\n", hd.hd_unit);
		return (1);
	}
	if ((status[0]&0x3F) == 0x00)		/* No error */
		return (0);
	if ((status[0]&0x3F) == 0x18)		/* Soft ECC */
		return (0);
	printf("hd%d: ", hd.hd_unit);
	if (hd.hd_iswr != 0)
		printf("write");
	else
		printf("read");
	printf(" error, sn=%d status=0x%x\n", (int)hd.hd_secn, status[0]&0x3F);
	return (1);
}

/*
 * This routine,
 * given a unit number,
 * returns a pointer to the "hdparm"
 * block for this drive type. 
 */
struct	hdp	*
hdparm(unit)
{

	if (unit < 0 || unit > 2) {
		printf("hd select error: unit %d\n", unit);
		return(&hdp[0]);
	} else
		return ((unit == 0) ? &hdp[0] : &hdp[1]);
}

/*
 * Send a command to the
 * disk. The "masks" parameter gets
 * ignored in this version of the driver.
 */
hdsendcmd(cmd, masks)
unsigned char	cmd[6];
{
	register int	i;

	while ((in(HDSSTAT)&HDSBUSY) == 0)	/* Wait for Xebec not busy */
		;
	out(HDSEL,  0x01);			/* Select pulse */
	while ((in(HDSSTAT)&HDSBUSY) != 0)	/* Wait for Xebec busy */
		;
	out(HDSEL, 0x00);			/* Select off */
	while ((in(HDSSTAT)&0x1F) != (HDSIO|HDSMSG))
		;
	for (i=0; i<6; ++i)
		outb(HDCS, cmd[i]);
}

/*
 * Enable interrupts on the host adapter.
 */
hdintenable()
{
	out(HDMASK, 0x02);
}

/*
 * transfer a 512 byte block of data to memory from the controller
 * via programmed i/o. DMA would be much nicer....
 */
readblk(port, paddr, nb)
unsigned port;
paddr_t	 paddr;
unsigned nb;
{
#if !FIVEINCH					/* only for 8" floppy */
	do {
		while ((in(HDSSTAT) & HDSREQ) != 0)
			;
		inwcopy(port, paddr, 2);	/* read 1 word (2 bytes) */
		paddr += 2;			/* next word */
		nb -= 2;			/* decrement count */
	} while (nb);
#else
	while ((in(HDSSTAT) & HDSREQ) != 0)
		;
	inwcopy(port, paddr, nb);
#endif
}

/*
 * transfer a 512 byte block of data to the controller from memory
 * via programmed i/o.
 */
writeblk(port, paddr, nb)
unsigned port;
paddr_t  paddr;
unsigned nb;
{
#if !FIVEINCH
	do {
		while ((in(HDSSTAT) & HDSREQ) != 0)
			;
		outwcopy(port, paddr, 2);	/* write one word */
		paddr += 2;			/* next word */
		nb -= 2;
	} while (nb);
#else
	while ((in(HDSSTAT) & HDSREQ) != 0)
		;
	outwcopy(port, paddr, nb);
#endif
}

/*
 * Get and return status and wait
 * for idle on the disk controller.
 */
hdstatus()
{
	register int s;

	while ((in(HDSSTAT) & 0x1F) != HDSMSG)
		;
	s = inb(HDCS);
	while ((in(HDSSTAT) & 0x1f) != 0)
		;
	inb(HDCS);			/* read message byte */
	while ((in(HDSSTAT) & 0x1f) != 0x1f)
		;
	return (s);
}
