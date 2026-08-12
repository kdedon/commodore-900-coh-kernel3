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
 * Driver for Western Digital hard disk controller for the Z8000HR.
 * 
 * Uses Commodore's SASI-like command block structure.
 * December 3, 1984
 * February 15, 1985 (nrb) added code for new DMA fixes and coditional execution
 * March 14, 1985 (nrb) added code to write pseudo-random paterns on disk
 * March 18, 1985 (nrb) added code for multiple drives & drive params
 */
#define		CMDBLKPADDR	(0x20000L)	/* WD command blk phys addr */
#define		BUFPADDR	(0x20400L)	/* CMDBLKPADDR + 1K */
#define		LONGBUFPADDR	(0x40000L)	/* CMDBLKPADDR + 128K */
#define		VERS		"V3.6"		/* version # */

#include	<coherent.h>
#include	<buf.h>
#include	<con.h>
#include	<stat.h>
#include	<uproc.h>
#include	<errno.h>

/*
 * this is to make this file compatable with tmd.s
 */
#undef	WDS
#define	WDS	0x3C

int	wdload();
int	wdopen();
int	wdread();
int	wdwrite();
int	wdblock();
int	wdintr();
int	nulldev();
int	nonedev();
char	*pfix();			/* map phys addr to a char * */

CON	wdcon	= {
	DFBLK|DFCHR,			/* Flags */
	2,				/* Major index */
	wdopen,				/* Open */
	nulldev,			/* Close */
	wdblock,			/* Block */
	wdread,				/* Read */
	wdwrite,			/* Write */
	nonedev,			/* Ioctl */
	nulldev,			/* Powerfail */
	nulldev,			/* Timeout */
	wdload,				/* Load */
	nulldev				/* Unload */
};

/*
 * Western Digital Controller port addresses
 */
#define	WDIO	0x0500			/* Data Register (word mode) */

#define	WDTDR	0x00			/* test drive ready */
#define	WDREST	0x01			/* restore to cyl 0 */
#define	WDRSS	0x03			/* Request status */
#define	WDCTF	0x05			/* check track format */
#define	WDFMTT	0x06			/* format track */
#define	WDREAD	0x08			/* Read */
#define	WDWRITE	0x0A			/* Write */
#define	WDSDP	0x0C			/* set drive parameters */
#define	WDCCBA	0x0F			/* change command block address */
#define	WDDDIAG	0xE3			/* run drive diagnostics */
#define	WDCDIAG	0xE4			/* run controller diagnostics */

#define	NSEC	17			/* number of sectors per track */
#define	NTRK	4			/* number of tracks per cylinder */
#define	NCYL	306			/* number of cylinders */
#define	WDIRQ	0x80			/* base of wd interrupt vector */
#define	SIXTEENuS 0xF			/* sixteen microsecond step rate */

typedef	struct	wdcmd {			/* command block layout */
 	unsigned char	c_opcode;	/* command class & opcode */
	unsigned char	c_lunhiaddr;	/* lun [7:5] & sector addr [4:0] */
	unsigned char	c_midaddr;	/* middle sector address */
	unsigned char	c_lowaddr;	/* low part of sector address */
	unsigned char	c_blockcnt;	/* number of blocks in I/O */
	unsigned char	c_control;	/* reserved control byte */
	unsigned char	c_highdma;	/* high DMA addr (phys segment) */
	unsigned char	c_middma;	/* middle DMA address */
	unsigned char	c_lowdma;	/* low DMA address */
	unsigned char	c_rsvd1;	/* reserved */
	unsigned char	c_rsvd2;	/* reserved */
	unsigned char	c_rsvd3;	/* reserved */
	unsigned char	c_errorbits;	/* error information */
	unsigned char	c_lunladd2;	/* error lun and high sector addr */
	unsigned char	c_ladd1;	/* error middle address */
	unsigned char	c_ladd0;	/* error low address */
} WDCMD;

typedef struct	wd_idc_params {		/* drive initialization params */
	unsigned char	p_optstep;	/* options & step rate */
	unsigned char	p_headhicyl;	/* heads <6:4>, hi cyl count <3:0> */
	unsigned char	p_cyl;		/* low byte of cyl count */
	unsigned char	p_precomp;	/* precomp cyl / 16 */
	unsigned char	p_reduce;	/* reduced write current / 16 */
	unsigned char	p_sectors;	/* sectors per track */
	char		*p_devname;	/* device name for user */
	int		p_ntrk;		/* tracks per cylinder */
	int		p_ncyl;		/* number of cylinders */
} WDINFO;

WDINFO	wdinfo[] = {			/* add entries as needed */
	{ SIXTEENuS, (4<<4)|(306/256), 306%256, 128/16, 128/16, NSEC,
	  "Seagate ST-212 4 head 10MB half-height", 4, 306 },
	{ SIXTEENuS, (4<<4)|(612/256), 612%256, 128/16, 128/16, NSEC,
	  "MiniScribe 4 head 20MB half-height", 4, 612 }
};

int	wdiflag;			/* interrupt acknowledge flag */
int	timeouts;			/* number of timeouts */
int	errors;				/* total # of errors */
int	typeno;				/* drive type selected from table */
int	unit;				/* which physical drive ? */
int	nsec;				/* # of sectors per track */
int	ntrk;				/* # of tracks per cylinder */
int	ncyl;				/* # of cylinders */
long	nblk;				/* number of blocks on device */
long	nblkcyl;			/* number of blocks on a cylinder */
char	databuf[512];

/*
 * simple test routine to try and format the hard disk using the new
 * Western Digital controller for the Commodore Z8000HR...
 */
wdload()
{
	register int trk, cyl;
	register long l;
	register unsigned pass = 0;
	register int ans, i;
	register int tfmt, tver, bwrite, bread, rndread;
	register int prwrite, prread, cylread, cylrdwr;

#ifdef	NOINTERRUPTS
	sphi();				/* never to be set low again! */
#endif
	timeouts = 0; errors=0; unit=0; typeno=0;
	tfmt=0; tver=0; bwrite=0; bread=0; rndread=0; cylread=0; cylrdwr=0;
	prwrite=0; prread=0;
	printf("Hard disk diagnostic for WD/Commodore controller, %s\n\n",VERS);
	printf("Drive types:\n");
	for (i = 0; i < (sizeof(wdinfo)/sizeof(wdinfo[0])); ++i)
		printf("%d) %s\n", i, wdinfo[i].p_devname);
	printf("Select drive type [%d]: ", typeno);	/* show default */
	while ((ans=getchar()) != '\n')
		if (ans >= '0' && ans < (sizeof(wdinfo)/sizeof(wdinfo[0]))+'0')
			typeno = ans - '0';
	nsec = wdinfo[typeno].p_sectors;
	ntrk = wdinfo[typeno].p_ntrk;
	ncyl = wdinfo[typeno].p_ncyl;
	nblkcyl = nsec * ntrk;			/* fits in an int */
	nblk = nblkcyl * (long)ncyl;		/* fits in a long */
	printf("Select drive unit number [%d]: ", unit);
	while ((ans=getchar()) != '\n')
		if (ans == '0' || ans == '1')
			unit = ans - '0';
	printf("Disk parameters: unit=%d sectors=%d heads=%d cylinders=%d\n",
		unit, nsec, ntrk, ncyl);
	printf("Enter desired options: 1=track format, 2=format verification,\n");
	printf("3=block write, 4=block read, 5=random read, 6=cylinder read,\n");
	printf("7=cylinder write/read/compare, 8=pseudo-random data write,\n");
	printf("9=pseudo-random data read\n? ");
	while ((ans=getchar()) != '\n')
		switch (ans) {
		case '1':
			++tfmt; break;
		case '2':
			++tver; break;
		case '3':
			++bwrite; break;
		case '4':
			++bread; break;
		case '5':
			++rndread; break;
		case '6':
			++cylread; break;
		case '7':
			++cylrdwr; break;
		case '8':
			++prwrite; break;
		case '9':
			++prread; break;
		};

	setivec(WDIRQ, wdintr);
	wdsetparam();

testloop:
	if (tfmt) {
		printf("Starting disk format\n");
		for (cyl = 0; cyl < ncyl; ++cyl)
			for (trk = 0; trk < ntrk; ++trk)
				wdfmttrk(trk, cyl);
	}
	if (tver) {
		printf("Starting verify phase:\n");
		for (cyl = 0; cyl < ncyl; ++cyl)
			for (trk = 0; trk < ntrk; ++trk)
				wdvfytrk(trk, cyl);
	}
	if (bwrite) {
		printf("Starting write phase\n");
		for (l = 0; l < nblk; ++l)
			wdwrite(l);
	}
	if (bread) {
		printf("Starting read phase\n");
		for (l = 0; l < nblk; ++l)
			wdread(l);
	}
	if (rndread) {
		printf("Starting random read phase\n");
		for (l = 0; l < 1000L; ++l)
			wdread((long)(((long)rand())%nblk));
	}
	if (cylread) {
		printf("Starting long read phase - 68 sectors/read\n");
		for (l = 0; l < (long)ncyl; ++l)
			wdlread(l*nblkcyl);
	}
	if (cylrdwr) {
		printf("Starting cylinder write/read/compare phase\n");
		for (l = 0; l < (long)ncyl; ++l)
			wdcrdwr(l*nblkcyl);
	}
	if (prwrite) {
		printf("Starting pseudo-random write phase\n");
		srand(0);		/* seed for random # generator */
		for (l = 0; l < nblk; ++l)
			wdprwrite(l);
	}
	if (prread) {
		printf("Starting pseudo-random read phase\n");
		srand(0);		/* seed for random # generator */
		for (l = 0; l < nblk; ++l)
			wdprread(l);
	}
	printf("\n\007Unit %d: pass count = %d, timeouts = %d errors=%d\n",
		unit, ++pass, timeouts, errors);
	goto testloop;
}

wdwait()
{
	register int s;
	register long l = 1000000L;
#ifdef	NOINTERRUPTS
	register WDCMD *cbp;
	register unsigned i;
#endif

	out(WDIO, 1);				/* strobe I/O line to WD */
#ifdef NOINTERRUPTS
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	do {
		i = cbp->c_errorbits;
		--l;
	} while ((i&0xFF) == 0xFF && l);	
#else
	wdiflag = 0;
	s = splo();
	while (!wdiflag && --l > 0)
		;
	spl(s); 
#endif
	if (!l) {
		printf("\nTIMEOUT ERROR\n");
		++timeouts; ++errors;
	}
#ifdef	NOINTERRUPTS
	out(WDIO, 0);			/* reset IEO on DMA chip */
#endif
}

wdfmttrk(t, c)
{
	register int i;
	register WDCMD *cbp;
	register char *bp;
	register unsigned char *cp;
	register int count;
	register long offs;

	printf("t=%d c=%d\r", t, c);
	offs = (nsec * (long)t) + (nblkcyl * (long)c);
	bp = pfix(WDS, (paddr_t)BUFPADDR);
	if (t < 0 || t >= ntrk)
		wderr("track # out of bounds\n");
	i = (nsec - t) % nsec;
	for (count = 0; count < nsec; ++count) {
		*bp++ = 0;
		*bp++ = i;
		i = (i + 1) % nsec;
	}

	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
		cp[i] = 0;
	cbp->c_opcode = WDFMTT;			/* format track */
	cbp->c_blockcnt = nsec;
	cbp->c_highdma =(BUFPADDR >> 16);	/* phys segment */
	cbp->c_middma = (BUFPADDR >> 8);	/* offset */
	cbp->c_lowdma = 0x00;
	cbp->c_lunhiaddr = (unit << 5) | (offs >> 16);
	cbp->c_midaddr = offs >> 8;
	cbp->c_lowaddr = offs;
	cbp->c_errorbits = 0xff;		/* make ready */
	wdwait();				/* wait for i/o complete */
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	i = cbp->c_errorbits;
	if (i & 0x7F) {
		printf("\nformat error: %x\n", i);
		for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
			printf("%x ", cp[i]);
		printf("\n");
		++errors;
	}
}

wdvfytrk(t, c)
{
	register int i;
	register WDCMD *cbp;
	register unsigned char *cp;
	register long offs;

	offs = (nsec * (long)t) + (nblkcyl * (long)c);
	printf("t=%d c=%d\r", t, c);
	if (t < 0 || t >= ntrk)
		wderr("track # out of bounds\n");
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
		cp[i] = 0;
	cbp->c_opcode = WDCTF;			/* check track format */
	cbp->c_blockcnt = nsec;
	cbp->c_highdma =(BUFPADDR >> 16);
	cbp->c_middma = (BUFPADDR >> 8);
	cbp->c_lowdma = 0x00;
	cbp->c_lunhiaddr = (unit << 5) | (offs >> 16);
	cbp->c_midaddr = offs >> 8;
	cbp->c_lowaddr = offs;
	cbp->c_errorbits = 0xff;		/* make ready */
	wdwait();				/* wait for i/o complete */
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	i = cbp->c_errorbits;
	if (i & 0x7F) {
		printf("\nverify error: %x\n", i);
		for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
			printf("%x ", cp[i]);
		++errors;
	}
}

wderr(cp)
char *cp;
{
	printf("\n%s", cp);
	while (1)
		;
}

wdintr(id)
int id;
{
#ifdef	NOINTERRUPTS
	printf("wdintr: got a spurious interrupt !!!\n");
	++errors;
#else
	wdiflag++;
	out(WDIO, 0);			/* reset IEO on DMA chip */
#endif
}

wdopen()
{
	printf("wdopen: \n");
}

wdread(sn)
long sn;
{
	register int i;
	register WDCMD *cbp;
	register unsigned char *cp;
	unsigned char c;

	c = sn;
	printf("%d \r", (int)sn);
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
		cp[i] = 0;
	cbp->c_opcode = WDREAD;			/* read a sector */
	cbp->c_blockcnt = 1;
	cbp->c_highdma = (BUFPADDR >> 16);
	cbp->c_middma = (BUFPADDR >> 8);
	cbp->c_lowdma = 0x00;
	cbp->c_lunhiaddr = (unit << 5) | (sn >> 16);
	cbp->c_midaddr = sn >> 8;
	cbp->c_lowaddr = sn;
	cbp->c_errorbits = 0xff;		/* make ready */
	wdwait();				/* wait for i/o complete */
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	i = cbp->c_errorbits;
	if (i & 0x7F) {
		printf("\nread error: block=%p, i=%x\n", sn, i);
		for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
			printf("%x ", cp[i]);
		printf("\n");
		++errors;
	}
	cp = pfix(WDS, (paddr_t)BUFPADDR);
	if (wdcheck(cp, c)) {
		for (i = 0; i < 512; ++i)
			if (*cp++ != c) {
			   printf("\nread data compare error block=%d,",(int)sn);
			   printf("0x%x != 0x%x, offset=%d\n", *--cp, c, i);
			   ++errors;
			   return;
			}
	}
}

wdwrite(sn)
long sn;
{
	register int i;
	register WDCMD *cbp;
	register unsigned char *cp;
	unsigned char c;

	printf("%d\r", (int)sn);
	c = sn;					/* use low byte of sec # */
	cp = pfix(WDS, (paddr_t)BUFPADDR);	/* data area */
	for (i = 0; i < 512; ++i)
		*cp++ = c;
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
		cp[i] = 0;
	cbp->c_opcode = WDWRITE;		/* write a sector */
	cbp->c_blockcnt = 1;
	cbp->c_highdma = (BUFPADDR >> 16);
	cbp->c_middma = (BUFPADDR >> 8);
	cbp->c_lowdma = 0x00;
	cbp->c_lunhiaddr = (unit << 5) | (sn >> 16);
	cbp->c_midaddr = sn >> 8;
	cbp->c_lowaddr = sn;
	cbp->c_errorbits = 0xff;		/* make ready */
	wdwait();				/* wait for i/o complete */
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	i = cbp->c_errorbits;
	if (i & 0x7F) {
		printf("\nwrite error: block=%p, i=%x\n", sn, i);
		for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
			printf("%x ", cp[i]);
		printf("\n");
		++errors;
	}
}

wdblock()
{
	printf("wdblock:\n");
}
wdlread(sn)
long sn;
{
	register WDCMD *cbp;
	register unsigned char *cp;
	register int i, j;
	unsigned char c;

	c = sn;
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
		cp[i] = 0;
	cbp->c_opcode = WDREAD;			/* read a sector */
	cbp->c_blockcnt = nblkcyl;
	cbp->c_highdma =(LONGBUFPADDR >> 16);
	cbp->c_middma = (LONGBUFPADDR >> 8);
	cbp->c_lowdma = 0x00;
	cbp->c_lunhiaddr = (unit << 5) | (sn >> 16);
	cbp->c_midaddr = sn >> 8;
	cbp->c_lowaddr = sn;
	cbp->c_errorbits = 0xff;		/* make ready */
	wdwait();				/* wait for i/o complete */
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	i = cbp->c_errorbits;
	if (i & 0x7F) {
		printf("\nread error: block=%p, i=%x\n", sn, i);
		for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
			printf("%x ", cp[i]);
		printf("\n");
		++errors;
	}
	cp = pfix(WDS, (paddr_t)LONGBUFPADDR);
	for (j = 0; j < (int)nblkcyl; ++j) {
	    if (wdcheck(cp, c))
	       for (i = 0; i < 512; ++i)
		   if (*cp++ != c) {
			printf("\nread data compare error block=%d,",(int)sn+j);
			printf("0x%x != 0x%x, offset=%d\n", *--cp, c, i);
			++errors;
			return;
		   }
	    ++c;
	    cp += 512;				/* next block */
	}
}

wdsetparam()
{
	register WDCMD *cbp;
	register unsigned char *cp;
	register int i;
	register WDINFO *ip;

	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
		cp[i] = 0;
	cbp->c_opcode = WDSDP;		/* set drive params */
	cbp->c_blockcnt = 0;
	cbp->c_highdma =(LONGBUFPADDR >> 16);
	cbp->c_middma = (LONGBUFPADDR >> 8);
	cbp->c_lowdma = 0x00;
	cbp->c_lunhiaddr = unit << 5;
	cbp->c_midaddr = 0;
	cbp->c_lowaddr = 0;
	cbp->c_errorbits = 0xff;		/* make ready */
	ip = (WDINFO *)pfix(WDS, (paddr_t)LONGBUFPADDR);
	*ip = wdinfo[typeno];
	wdwait();				/* wait for i/o complete */
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	i = cbp->c_errorbits;
	if (i & 0x7F) {
		printf("\ninit error: status=0x%x\n", i);
		for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
			printf("%x ", cp[i]);
		printf("\n");
		++errors;
	}
}

wdcrdwr(sn)
long sn;
{
	register WDCMD *cbp;
	register unsigned char *cp;
	register int i, j;
	unsigned char c;

	c = sn;
	cp = pfix(WDS, (paddr_t)LONGBUFPADDR);
	for (j = 0; j < (int)nblkcyl; ++j) {
		wdblkinit(cp, c);		/* fill 512 byte block of 'c' */
		cp += 512;
		++c;
	}
	c = sn;
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
		cp[i] = 0;
	cbp->c_opcode = WDWRITE;		/* write a cylinder */
	cbp->c_blockcnt = nblkcyl;
	cbp->c_highdma =(LONGBUFPADDR >> 16);
	cbp->c_middma = (LONGBUFPADDR >> 8);
	cbp->c_lowdma = 0x00;
	cbp->c_lunhiaddr = (unit << 5) | (sn >> 16);
	cbp->c_midaddr = sn >> 8;
	cbp->c_lowaddr = sn;
	cbp->c_errorbits = 0xff;		/* make ready */
	wdwait();				/* wait for i/o complete */
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	i = cbp->c_errorbits;
	if (i & 0x7F) {
		printf("\nwrite error: block=%p, i=%x\n", sn, i);
		for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
			printf("%x ", cp[i]);
		printf("\n");
		++errors;
	}
	for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
		cp[i] = 0;
	cbp->c_opcode = WDREAD;			/* read a sector */
	cbp->c_blockcnt = nblkcyl;
	cbp->c_highdma =(LONGBUFPADDR >> 16);
	cbp->c_middma = (LONGBUFPADDR >> 8);
	cbp->c_lowdma = 0x00;
	cbp->c_lunhiaddr = (unit << 5) | (sn >> 16);
	cbp->c_midaddr = sn >> 8;
	cbp->c_lowaddr = sn;
	cbp->c_errorbits = 0xff;		/* make ready */
	wdwait();				/* wait for i/o complete */
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	i = cbp->c_errorbits;
	if (i & 0x7F) {
		printf("\nread error: block=%p, i=%x\n", sn, i);
		for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
			printf("%x ", cp[i]);
		printf("\n");
		++errors;
	}
	cp = pfix(WDS, (paddr_t)LONGBUFPADDR);
	for (j = 0; j < (int)nblkcyl; ++j) {
	    if (wdcheck(cp, c))
	       for (i = 0; i < 512; ++i)
		   if (*cp++ != c) {
			printf("\nread data compare error block=%d,",(int)sn+j);
			printf("0x%x != 0x%x, offset=%d\n", *--cp, c, i);
			++errors;
			return;
		   }
	    ++c;
	    cp += 512;				/* next block */
	}
}

vret(vec)
int	vec;
{
	printf("bad int vec=0x%x\n", vec);
}

wdprwrite(sn)
long sn;
{
	register int i;
	register WDCMD *cbp;
	register unsigned char *cp;
	register int *ip;

	printf("%d\r", (int)sn);
	ip = (int *)pfix(WDS, (paddr_t)BUFPADDR);	/* data area */
	for (i = 0; i < 256; ++i)
		*ip++ = rand();		/* generate next in pseudo-sequence */
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
		cp[i] = 0;
	cbp->c_opcode = WDWRITE;		/* write a sector */
	cbp->c_blockcnt = 1;
	cbp->c_highdma = (BUFPADDR >> 16);
	cbp->c_middma = (BUFPADDR >> 8);
	cbp->c_lowdma = 0x00;
	cbp->c_lunhiaddr = (unit << 5) | (sn >> 16);
	cbp->c_midaddr = sn >> 8;
	cbp->c_lowaddr = sn;
	cbp->c_errorbits = 0xff;		/* make ready */
	wdwait();				/* wait for i/o complete */
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	i = cbp->c_errorbits;
	if (i & 0x7F) {
		printf("\nwrite error: block=%p, i=%x\n", sn, i);
		for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
			printf("%x ", cp[i]);
		printf("\n");
		++errors;
	}
}

wdprread(sn)
long sn;
{
	register int i;
	register WDCMD *cbp;
	register unsigned char *cp;
	register int *ip;
	register unsigned char *bp;
	printf("%d \r", (int)sn);
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
		cp[i] = 0;
	cbp->c_opcode = WDREAD;			/* read a sector */
	cbp->c_blockcnt = 1;
	cbp->c_highdma = (BUFPADDR >> 16);
	cbp->c_middma = (BUFPADDR >> 8);
	cbp->c_lowdma = 0x00;
	cbp->c_lunhiaddr = (unit << 5) | (sn >> 16);
	cbp->c_midaddr = sn >> 8;
	cbp->c_lowaddr = sn;
	cbp->c_errorbits = 0xff;		/* make ready */
	wdwait();				/* wait for i/o complete */
	cbp = (WDCMD *) pfix(WDS, (paddr_t)CMDBLKPADDR);
	i = cbp->c_errorbits;
	if (i & 0x7F) {
		printf("\nread error: block=%p, i=%x\n", sn, i);
		for (cp = cbp, i = 0; i < sizeof(WDCMD); ++i)
			printf("%x ", cp[i]);
		printf("\n");
		++errors;
	}
	for (ip = databuf, i = 0; i < 256; ++i)
		*ip++ = rand();
	cp = pfix(WDS, (paddr_t)BUFPADDR);
	if (wdcompare(databuf, cp)) {
		for (bp = databuf, i = 0; i < 512; ++i)
			if (*cp++ != *bp++) {
			   printf("\nread data compare error block=%d,",(int)sn);
			   printf("disk 0x%x != 0x%x, offset=%d\n", 
			       *--cp, *--bp, i);
			   ++errors;
			   return;
			}
	}
}

