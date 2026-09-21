/*
 * Commodore M-series Z8001 -- the dynamic shared-library loader.
 *
 * A library is an l.out with LF_SLIB and two segments: shared text, and a
 * private image each client copies.  It is relocated once, at first load, to
 * a fixed segment pair every client then maps.
 *
 *	client L_SYM:	LI_LIB name
 *			LI_IMP sym -> 4-byte slot in client data
 *	exec:		slot = far pointer to the export
 *
 * slbind() does everything that can fail before exec frees the old image;
 * slslots() fills the slots after.  The kernel stack is small, so nothing
 * recurses.
 */
#include <sys/coherent.h>
#include <sys/buf.h>
#include <canon.h>
#include <errno.h>
#include <sys/ino.h>
#include <sys/inode.h>
#include <l.out.h>
#include <shlib.h>
#include <sys/proc.h>
#include <sys/seg.h>
#include <sys/uproc.h>
#include <sys/io.h>
#include <sys/mmu.h>
#include <sys/slib.h>
#include <sched.h>

#define	CLICK	1024			/* pfix()'s window granularity	*/

#define	sh	((fsize_t)sizeof(struct ldheader))

SLIB	slibt[NSLIB];			/* The loaded-library table	*/

extern	char	*pfix();
extern	SEG	*salloc(), *segdupl(), *exsread();
extern	INODE	*iattach();
extern	BUF	*vread();

static	SLIB	*slload();
static	SLIB	*slfind();
static	INODE	*slsearch();

/*
 * Copy physical memory to kernel space and back, a click window at a time.
 */
static
slpread(p, k, n)
paddr_t p;
register char *k;
register unsigned n;
{
	register unsigned c;

	while (n != 0) {
		c = CLICK - ((unsigned)p & (CLICK-1));
		if (c > n)
			c = n;
		kkcopy(pfix(ES, p), k, c);
		p += c;
		k += c;
		n -= c;
	}
}

static
slpwrite(k, p, n)
register char *k;
paddr_t p;
register unsigned n;
{
	register unsigned c;

	while (n != 0) {
		c = CLICK - ((unsigned)p & (CLICK-1));
		if (c > n)
			c = n;
		kkcopy(k, pfix(ES, p), c);
		p += c;
		k += c;
		n -= c;
	}
}

/*
 * Compare loader names as unsigned bytes, the export table's order.
 */
static
slncmp(a, b)
register unsigned char *a;
register unsigned char *b;
{
	register int n;

	for (n = NCPLN; --n >= 0; a++, b++) {
		if (*a != *b)
			return (*a < *b ? -1 : 1);
	}
	return (0);
}

/*
 * Terminate a loader name; a full NCPLN one has no NUL.
 */
static
slname(dst, src)
register char *dst;
register char *src;
{
	register int n;

	for (n = NCPLN; --n >= 0 && *src != '\0'; )
		*dst++ = *src++;
	*dst = '\0';
}

/*
 * Report a refusal and return 0.
 */
static
slfail(what, lib, sym, err)
char *what;
char *lib;
char *sym;
{
	char nb[SLNAME];

	if (sym != NULL) {
		slname(nb, sym);
		printf("shlib: %s: %s\n", what, nb);
	} else {
		slname(nb, lib);
		printf("shlib: %s: %s\n", nb, what);
	}
	u.u_error = err;
	return (0);
}

/*
 * The hardware segment a library's shared half maps at, or -1.
 */
slsegof(sp)
register SEG *sp;
{
	register SLIB *lp;

	for (lp = slibt; lp < &slibt[NSLIB]; lp++) {
		if ((lp->sl_flags&SLF_USED) != 0 && lp->sl_shrp == sp)
			return (lp->sl_seg);
	}
	return (-1);
}

/*
 * Wait until another process finishes binding a library's own imports, so
 * we never copy a private image with empty slots.  Returns 0 if the binding
 * failed and the library is gone.
 */
static
slwait(lp)
register SLIB *lp;
{
	while ((lp->sl_flags&(SLF_USED|SLF_BUSY)) == (SLF_USED|SLF_BUSY))
		v_sleep((char *)lp, CVGATE, IVGATE, SVGATE, "shlib");
	return ((lp->sl_flags&SLF_USED) != 0);
}

/*
 * Find a loaded library by device and inode.
 */
static SLIB *
slfind(dev, ino)
dev_t dev;
ino_t ino;
{
	register SLIB *lp;

	for (lp = slibt; lp < &slibt[NSLIB]; lp++) {
		if ((lp->sl_flags&SLF_USED) != 0
		 && lp->sl_dev == dev && lp->sl_ino == ino)
			return (lp);
	}
	return (NULL);
}

/*
 * Free every library no process maps (shared s_urefc == 1).  Returns the
 * number freed.
 */
slreclaim()
{
	register SLIB *lp;
	register int n;

	n = 0;
	for (lp = slibt; lp < &slibt[NSLIB]; lp++) {
		if ((lp->sl_flags&SLF_USED) == 0)
			continue;
		/* Still loading, or another library imports from it. */
		if ((lp->sl_flags&SLF_BUSY) != 0 || lp->sl_drefc != 0)
			continue;
		if (lp->sl_shrp->s_urefc != 1)
			continue;
		printf("shlib: reclaiming segments %d/%d\n",
			lp->sl_seg, lp->sl_seg+1);
		while (lp->sl_ndep != 0)
			lp->sl_dep[--lp->sl_ndep]->sl_drefc--;
		lp->sl_flags = 0;
		sfree(lp->sl_prvp);
		sfree(lp->sl_shrp);
		lp->sl_shrp = NULL;
		lp->sl_prvp = NULL;
		n++;
	}
	return (n);
}

/*
 * The highest free segment pair at or above SLFLOOR, or -1.
 */
static
slpair()
{
	register int s;
	register SLIB *lp;
	register int taken;

	for (s = NHUSEG-2; s >= SLFLOOR; s -= 2) {
		taken = 0;
		for (lp = slibt; lp < &slibt[NSLIB]; lp++) {
			if ((lp->sl_flags&SLF_USED) != 0 && lp->sl_seg == s)
				taken = 1;
		}
		if (taken == 0)
			return (s);
	}
	return (-1);
}

/*
 * Relocate both images.  Each fixup names the segment byte of an address;
 * bit 7 is the long-address marker:
 *
 *	*b = (*b & 0x80) | (seg & 0x7F)
 */
static
slfixup(ip, lp, foff, nfix)
INODE *ip;
register SLIB *lp;
fsize_t foff;
unsigned nfix;
{
	struct slfix fx[16];
	IO io;
	register int i;
	register int n;
	register paddr_t b;
	char c;

	io.io_seg = IOSYS;
	io.io_seek = foff;
	io.io_flag = 0;
	while (nfix != 0) {
		n = nfix > 16 ? 16 : nfix;
		nfix -= n;
		io.io_ioc = n * SL_FIXLEN;
		io.io_base = (char *)fx;
		u.u_error = 0;
		iread(ip, &io);
		if (u.u_error != 0)
			return (0);
		io.io_seek += n * SL_FIXLEN;
		for (i = 0; i < n; i++) {
			b = ((fx[i].sf_flags&SF_LOC_PRIVATE) != 0
				? lp->sl_prvp : lp->sl_shrp)->s_paddr
			    + fx[i].sf_off;
			slpread(b, &c, 1);
			c = (c&0x80)
			  | (((fx[i].sf_flags&SF_REF_PRIVATE) != 0
				? lp->sl_seg+1 : lp->sl_seg) & 0x7F);
			slpwrite(&c, b, 1);
		}
	}
	return (1);
}

/*
 * Bind a library's own imports into its pristine private image.  Libraries
 * it needs are appended to the client's list rather than loaded here, since
 * recursing would overrun the kernel stack.
 *
 * Returns 1, or 0 with u.u_error set.
 */
static
sldeps(lp, lpath, path, libl, nlibp)
register SLIB *lp;
char *lpath;
char *path;
SLIB **libl;
int *nlibp;
{
	struct ldsym sym;
	IO io;
	register SLIB *dp;
	register long k;
	long off;
	INODE *dip;
	vaddr_t va;
	paddr_t pa;
	char ptr[4];
	unsigned short eoff, eflg;
	int t, i;

	/* Drop references from an earlier, refused attempt. */
	while (lp->sl_ndep != 0)
		lp->sl_dep[--lp->sl_ndep]->sl_drefc--;
	io.io_seg = IOSYS;
	io.io_flag = 0;
	io.io_seek = lp->sl_symoff;
	dp = NULL;
	for (k = 0; k < (long)lp->sl_nsym; k++) {
		io.io_ioc = sizeof(struct ldsym);
		io.io_base = (char *)&sym;
		u.u_error = 0;
		iread(lp->sl_shrp->s_ip, &io);
		if (u.u_error != 0)
			return (slfail("cannot read a library's symbol table",
				"(library)", (char *)NULL, ENOSLIB));
		io.io_seek += sizeof(struct ldsym);
		t = sym.ls_type;
		canint(t);
		if (t == LI_LIB) {
			if (lp->sl_ndep >= NSLDEP)
				return (slfail("a library imports from too many libraries",
					sym.ls_id, (char *)NULL, ENOSLIB));
			if (*nlibp >= NSLIB)
				return (slfail("too many libraries for one process",
					sym.ls_id, (char *)NULL, ENOSLIB));
			if ((dip = slsearch(sym.ls_id, lpath, path)) == NULL)
				return (slfail("a library needs it, and it is not on LIBPATH, /lib, /usr/lib",
					sym.ls_id, (char *)NULL, ENOSLIB));
			dp = slfind(dip->i_dev, dip->i_ino);
			if (dp != NULL && slwait(dp) == 0)
				dp = NULL;
			else if (dp == NULL)
				dp = slload(dip, sym.ls_id);
			idetach(dip);
			if (dp == NULL)
				return (slfail("a library needs it and it could not be loaded",
					sym.ls_id, (char *)NULL, ENOSLIB));
			if (dp == lp)
				return (slfail("a library imports from itself",
					sym.ls_id, (char *)NULL, ENOSLIB));
			dp->sl_drefc++;
			lp->sl_dep[lp->sl_ndep++] = dp;
			/* The client must map it too; slbind()'s loop does. */
			for (i = 0; i < *nlibp; i++)
				if (libl[i] == dp)
					break;
			if (i == *nlibp) {
				/* Hold it against slreclaim() until bound. */
				dp->sl_drefc++;
				libl[(*nlibp)++] = dp;
			}
			continue;
		}
		if (t != LI_IMP)
			continue;
		if (dp == NULL)
			return (slfail("a library's import record comes before any library",
				"(library)", (char *)NULL, ENOSLIB));
		if (slexport(dp, sym.ls_id, &eoff, &eflg) < 0)
			return (slfail("a library imports it and it is not exported",
				sym.ls_id, (char *)NULL, ENOSLIB));
		va = sym.ls_addr;
		canvaddr(va);
		off = (long)VOFF(va);
		if (VSEG(va) != SL_NOMPRV
		 || off + 4 > (long)lp->sl_prvp->s_size)
			return (slfail("a library's import slot is not in its private image",
				sym.ls_id, (char *)NULL, ENOSLIB));
		pa = lp->sl_prvp->s_paddr + off;
		if ((eflg&SE_DATA) != 0) {
			slpread(pa, ptr, 4);
			eoff += ((unsigned short)(ptr[2]&0xFF) << 8)
			      | (unsigned short)(ptr[3]&0xFF);
			ptr[0] = ((eflg&SE_SHRD) != 0 ? dp->sl_seg
						      : dp->sl_seg+1) & 0x7F;
		} else
			ptr[0] = dp->sl_seg & 0x7F;
		ptr[1] = 0;
		ptr[2] = (eoff >> 8) & 0xFF;
		ptr[3] = eoff & 0xFF;
		slpwrite(ptr, pa, 4);
	}
	return (1);
}

/*
 * Load a library that is not resident.  `ip' is locked.  The table's
 * reference on the inode makes writing the file ETXTBSY.
 */
static SLIB *
slload(ip, name)
register INODE *ip;
char *name;
{
	register SLIB *lp;
	register BUF *bp;
	struct ldheader *ldp;
	struct slhead hd;
	char nb[SLNAME];
	fsize_t lssize[NLSEG];
	fsize_t fixoff;
	int lflag, i, s, t;

	for (lp = slibt; lp < &slibt[NSLIB]; lp++) {
		if ((lp->sl_flags&SLF_USED) == 0)
			break;
	}
	if (lp >= &slibt[NSLIB]) {
		if (slreclaim() == 0) {
			slfail("no free library table entry", name,
				(char *)NULL, ENOSLIB);
			return (NULL);
		}
		for (lp = slibt; lp < &slibt[NSLIB]; lp++)
			if ((lp->sl_flags&SLF_USED) == 0)
				break;
	}

	/*
	 * Shared half L_SHRI+L_SHRD, private half L_PRVI+L_PRVD+bss, each
	 * within 64K.
	 */
	if ((bp = vread(ip, (daddr_t)0)) == NULL) {
		slfail("unreadable", name, (char *)NULL, ENOSLIB);
		return (NULL);
	}
	ldp = (struct ldheader *)bp->b_faddr;
	i = ldp->l_magic;
	canint(i);
	lflag = ldp->l_flag;
	canint(lflag);
	s = ldp->l_machine;
	canint(s);
	kkcopy(ldp->l_ssize, lssize, NLSEG*sizeof(fsize_t));
	brelease(bp);
	for (t = 0; t < NLSEG; t++)
		cansize(lssize[t]);
	if (i != L_MAGIC || s != mactype || (lflag&LF_SLIB) == 0) {
		slfail("not a shared library", name, (char *)NULL, ENOSLIB);
		return (NULL);
	}
	if (lssize[L_SHRI]+lssize[L_SHRD] > 0x10000L
	 || lssize[L_PRVI]+lssize[L_PRVD]+lssize[L_BSSI]+lssize[L_BSSD]
		> 0x10000L) {
		slfail("segment over 64K", name, (char *)NULL, ENOSLIB);
		return (NULL);
	}

	if ((s = slpair()) < 0) {
		if (slreclaim() == 0 || (s = slpair()) < 0) {
			slfail("no free segment pair", name, (char *)NULL,
				ENOSLIB);
			return (NULL);
		}
	}
	lp->sl_seg = s;
	lp->sl_dev = ip->i_dev;
	lp->sl_ino = ip->i_ino;

	/*
	 * SFSHRX shares the first and maps it read-only.  No process owns the
	 * private image, so it is never swapped and each client copies it.
	 */
	lp->sl_shrp = salloc(lssize[L_SHRI]+lssize[L_SHRD], SFSHRX);
	if (lp->sl_shrp == NULL) {
		slfail("no memory for the shared segment", name,
			(char *)NULL, ENOMEM);
		return (NULL);
	}
	lp->sl_prvp = salloc(lssize[L_PRVI]+lssize[L_PRVD]
			     +lssize[L_BSSI]+lssize[L_BSSD], 0);
	if (lp->sl_prvp == NULL) {
		sfree(lp->sl_shrp);
		lp->sl_shrp = NULL;
		slfail("no memory for the private image", name,
			(char *)NULL, ENOMEM);
		return (NULL);
	}
	if (exsread(lp->sl_shrp, ip, lssize[L_SHRI], sh, (fsize_t)0) == 0
	 || exsread(lp->sl_shrp, ip, lssize[L_SHRD],
		    sh+lssize[L_SHRI]+lssize[L_PRVI], lssize[L_SHRI]) == 0
	 || exsread(lp->sl_prvp, ip, lssize[L_PRVI], sh+lssize[L_SHRI],
		    (fsize_t)0) == 0
	 || exsread(lp->sl_prvp, ip, lssize[L_PRVD],
		    sh+lssize[L_SHRI]+lssize[L_PRVI]+lssize[L_SHRD],
		    lssize[L_PRVI]) == 0)
		goto bad;

	/* The export table is at offset 0 of the shared segment. */
	slpread(lp->sl_shrp->s_paddr, (char *)&hd, SL_HDRLEN);
	if (hd.sl_magic != SL_MAGIC || hd.sl_vers != SL_VERSION) {
		slfail("bad export table", name, (char *)NULL, ENOSLIB);
		goto bad2;
	}
	if ((fsize_t)hd.sl_expoff + (fsize_t)hd.sl_nexp*SL_EXPLEN
			> lp->sl_shrp->s_size
	 || hd.sl_fixsec != L_DEBUG
	 || (fsize_t)hd.sl_nfix*SL_FIXLEN != lssize[L_DEBUG]) {
		slfail("inconsistent export table", name, (char *)NULL,
		       ENOSLIB);
		goto bad2;
	}
	lp->sl_nexp = hd.sl_nexp;
	lp->sl_expoff = hd.sl_expoff;
	fixoff = hd.sl_fixoff;

	if (slfixup(ip, lp, fixoff, (unsigned)hd.sl_nfix) == 0) {
		slfail("unreadable fixup list", name, (char *)NULL, ENOSLIB);
		goto bad2;
	}

	/* Its own imports, which slbind() binds and then clears SLF_BUSY. */
	lp->sl_symoff = sh + lssize[L_SHRI] + lssize[L_PRVI] + lssize[L_SHRD]
		      + lssize[L_PRVD] + lssize[L_DEBUG];
	lp->sl_nsym = (unsigned)(lssize[L_SYM] / (fsize_t)sizeof(struct ldsym));
	lp->sl_ndep = 0;
	lp->sl_drefc = 0;

	lp->sl_shrp->s_ip = ip;
	ip->i_refc++;
	lp->sl_flags = (lflag&LF_SLREF) != 0 ? SLF_USED|SLF_BUSY : SLF_USED;
	slname(nb, name);
	printf("shlib: loaded %s at segments %d/%d, %d exports\n",
		nb, lp->sl_seg, lp->sl_seg+1, lp->sl_nexp);
	return (lp);

bad:
	slfail("unreadable", name, (char *)NULL, ENOSLIB);
bad2:
	sfree(lp->sl_prvp);
	sfree(lp->sl_shrp);
	lp->sl_shrp = NULL;
	lp->sl_prvp = NULL;
	return (NULL);
}

/*
 * Binary-search the export table for `name'.  Returns 0, or -1 if not
 * exported.  The offset is unsigned, so it comes back through `offp'.
 * SE_DATA means it is into the private image.
 */
static
slexport(lp, name, offp, flgp)
register SLIB *lp;
char *name;
unsigned short *offp;
unsigned short *flgp;
{
	struct slexp ex;
	register int lo, hi, mid, c;

	lo = 0;
	hi = (int)lp->sl_nexp - 1;
	while (lo <= hi) {
		mid = (lo+hi) >> 1;
		slpread(lp->sl_shrp->s_paddr + lp->sl_expoff
			+ (paddr_t)mid*SL_EXPLEN, (char *)&ex, SL_EXPLEN);
		if ((c = slncmp(name, ex.se_name)) == 0) {
			*offp = ex.se_off;
			*flgp = ex.se_flags;
			return (0);
		}
		if (c < 0)
			hi = mid-1;
		else
			lo = mid+1;
	}
	return (-1);
}

/*
 * Read LIBPATH from exec's still-mapped user environment.  A fault means
 * there is none.
 */
static
slenv(envp, buf, n)
register char **envp;
register char *buf;
int n;
{
	static char key[] = "LIBPATH=";
	register char *cp;
	register char *kp;
	register int c;

	if (envp == NULL)
		return (0);
	while ((cp = getupd(envp++)) != NULL) {
		if (u.u_error != 0) {
			u.u_error = 0;
			return (0);
		}
		for (kp = key; *kp != '\0'; kp++, cp++) {
			if (getubd(cp) != *kp || u.u_error != 0)
				break;
		}
		if (*kp != '\0' || u.u_error != 0) {
			u.u_error = 0;
			continue;
		}
		for (kp = buf; kp < buf+n-1; kp++) {
			c = getubd(cp++);
			if (u.u_error != 0) {
				u.u_error = 0;
				return (0);
			}
			if ((*kp = c) == '\0')
				return (1);
		}
		*kp = '\0';
		return (1);			/* truncated: use what fits */
	}
	u.u_error = 0;
	return (0);
}

/*
 * Search LIBPATH, /lib, then /usr/lib for `name'.  Returns its inode,
 * locked, or NULL.
 */
static INODE *
slsearch(name, lpath, path)
char *name;
register char *lpath;
register char *path;
{
	static char dlib[] = "/lib";
	static char dusr[] = "/usr/lib";
	register char *cp;
	register char *np;
	register int n;

	n = 0;					/* 0 LIBPATH, 1 /lib, 2 /usr/lib */
	for (;;) {
		cp = path;
		if (n == 0) {
			if (*lpath == '\0') {
				n = 1;
				continue;
			}
			while (*lpath!='\0' && *lpath!=':') {
				if (cp < path+SLPATH-SLNAME-2)
					*cp++ = *lpath;
				lpath++;
			}
			if (*lpath == ':')
				lpath++;
		} else if (n <= 2) {
			for (np = (n==1 ? dlib : dusr); *np != '\0'; )
				*cp++ = *np++;
			n++;
		} else
			break;
		if (cp == path)			/* an empty LIBPATH element */
			continue;
		*cp++ = '/';
		for (np = name; np < name+NCPLN && *np != '\0'; )
			*cp++ = *np++;
		*cp = '\0';
		u.u_error = 0;
		if (ftoi_sys(path, 'r') == 0)
			return (u.u_cdiri);
	}
	u.u_error = 0;
	return (NULL);
}

/*
 * Find or load every library the client names and check every import;
 * bind libraries' own imports, which may append more libraries; then take
 * a reference on each segment.  p_segp still holds the old image, so the
 * segments come back in `lseg'.  sl_drefc holds each library against
 * slreclaim() meanwhile.
 *
 * Returns the number of libraries, or -1 with u.u_error set.
 */
slbind(ip, lssize, setid, envp, lseg, libl)
INODE *ip;
fsize_t *lssize;
char **envp;
SEG **lseg;
SLIB **libl;
{
	struct ldsym sym;
	char lpath[SLPATH];
	char path[SLPATH];
	IO io;
	register SLIB *lp;
	int nlib;
	register long k;
	long nsym;
	INODE *lip;
	char nb[SLNAME];
	char sb[SLNAME];
	unsigned short eoff, eflg;
	int t, i, nseg;

	nseg = 0;
	lpath[0] = '\0';
	if (setid == 0)
		slenv(envp, lpath, sizeof(lpath));

	io.io_seg = IOSYS;
	io.io_flag = 0;
	io.io_seek = sh + lssize[L_SHRI] + lssize[L_PRVI] + lssize[L_SHRD]
		   + lssize[L_PRVD] + lssize[L_DEBUG];
	nsym = lssize[L_SYM] / (fsize_t)sizeof(struct ldsym);
	nlib = 0;
	lp = NULL;
	for (k = 0; k < nsym; k++) {
		io.io_ioc = sizeof(struct ldsym);
		io.io_base = (char *)&sym;
		u.u_error = 0;
		iread(ip, &io);
		if (u.u_error != 0) {
			printf("shlib: cannot read the symbol table\n");
			u.u_error = ENOSLIB;
			goto bad;
		}
		io.io_seek += sizeof(struct ldsym);
		t = sym.ls_type;
		canint(t);
		if (t == LI_LIB) {
			slname(nb, sym.ls_id);
			if (nlib >= NSLIB) {
				slfail("too many libraries for one process",
					sym.ls_id, (char *)NULL, ENOSLIB);
				goto bad;
			}
			if ((lip = slsearch(sym.ls_id, lpath, path)) == NULL) {
				slfail("not found on LIBPATH, /lib, /usr/lib",
					sym.ls_id, (char *)NULL, ENOSLIB);
				goto bad;
			}
			lp = slfind(lip->i_dev, lip->i_ino);
			if (lp != NULL && slwait(lp) == 0)
				lp = NULL;
			else if (lp == NULL)
				lp = slload(lip, sym.ls_id);
			idetach(lip);
			if (lp == NULL) {
				if (u.u_error == 0)
					slfail("another exec's load of it failed",
						sym.ls_id, (char *)NULL,
						ENOSLIB);
				goto bad;
			}
			/* Hold it against slreclaim() until the segments are taken. */
			lp->sl_drefc++;
			libl[nlib++] = lp;
		} else if (t == LI_IMP) {
			if (lp == NULL) {
				slfail("import record before any library",
					"(client)", (char *)NULL, ENOSLIB);
				goto bad;
			}
			if (slexport(lp, sym.ls_id, &eoff, &eflg) < 0) {
				slname(sb, sym.ls_id);
				printf("shlib: %s: symbol not exported: %s\n",
					nb, sb);
				u.u_error = ENOSLIB;
				goto bad;
			}
		}
	}
	/*
	 * Bind libraries' own imports; sldeps() appends what they need, so
	 * this loop walks the chain.  The client's libraries stay first, in
	 * LI_LIB order, for slslots().
	 */
	for (i = 0; i < nlib; i++) {
		if ((libl[i]->sl_flags&SLF_BUSY) == 0)
			continue;
		if (sldeps(libl[i], lpath, path, libl, &nlib) == 0)
			goto bad;
		libl[i]->sl_flags = SLF_USED;
		wakeup((char *)libl[i]);
	}

	/* Share the shared half; copy the private one. */
	for (i = 0; i < nlib; i++) {
		if ((lseg[2*i] = segdupl(libl[i]->sl_shrp)) == NULL) {
			slfail("too many clients", "(library)",
				(char *)NULL, ENOMEM);
			goto bad;
		}
		if ((lseg[2*i+1] = segdupl(libl[i]->sl_prvp)) == NULL) {
			sfree(lseg[2*i]);
			slfail("no memory for the private copy", "(library)",
				(char *)NULL, ENOMEM);
			goto bad;
		}
		nseg = i+1;
	}
	for (i = 0; i < nlib; i++)		/* the holds, given back */
		libl[i]->sl_drefc--;
	return (nlib);

bad:
	t = u.u_error;
	/* Free libraries we left BUSY and wake their waiters. */
	for (i = 0; i < nlib; i++)
		libl[i]->sl_drefc--;
	for (i = 0; i < nlib; i++) {
		if ((libl[i]->sl_flags&SLF_BUSY) == 0)
			continue;
		while (libl[i]->sl_ndep != 0)
			libl[i]->sl_dep[--libl[i]->sl_ndep]->sl_drefc--;
		libl[i]->sl_flags = 0;
		sfree(libl[i]->sl_prvp);
		sfree(libl[i]->sl_shrp);
		libl[i]->sl_shrp = NULL;
		libl[i]->sl_prvp = NULL;
		wakeup((char *)libl[i]);
	}
	while (--nseg >= 0) {
		sfree(lseg[2*nseg+1]);
		sfree(lseg[2*nseg]);
	}
	u.u_error = t;
	return (-1);
}

/*
 * The physical address of an LI_IMP slot in the newly mapped image, or 0.
 */
static paddr_t
slslotp(va)
vaddr_t va;
{
	register struct sr *srp;
	register SEG *sp;
	register long off;

	/* SIUSERP is skipped: its sr_base is the u-area's kernel address. */
	for (srp = &u.u_segl[SIUSERP+1]; srp < &u.u_segl[NUSEG]; srp++) {
		if ((sp = srp->sr_segp) == NULL)
			continue;
		off = ((long)(VSEG(va) - VSEG(srp->sr_base)) << 16)
		    + (long)VOFF(va) - (long)VOFF(srp->sr_base);
		if (off >= 0 && off + 4 <= (long)sp->s_size)
			return (sp->s_paddr + off);
	}
	return ((paddr_t)0);
}

/*
 * After sproto(), write each import's far pointer into its slot:
 *
 *	seg & 0x7F | 0 | offset hi | offset lo
 */
slslots(ip, lssize, libl, nlib)
INODE *ip;
fsize_t *lssize;
SLIB **libl;
{
	struct ldsym sym;
	char nb[SLNAME];
	IO io;
	register SLIB *lp;
	register long k;
	register int i;
	long nsym;
	vaddr_t va;
	paddr_t p;
	char ptr[4];
	unsigned short eoff, eflg;
	int t;

	io.io_seg = IOSYS;
	io.io_flag = 0;
	io.io_seek = sh + lssize[L_SHRI] + lssize[L_PRVI] + lssize[L_SHRD]
		   + lssize[L_PRVD] + lssize[L_DEBUG];
	nsym = lssize[L_SYM] / (fsize_t)sizeof(struct ldsym);
	lp = NULL;
	i = 0;
	for (k = 0; k < nsym; k++) {
		io.io_ioc = sizeof(struct ldsym);
		io.io_base = (char *)&sym;
		u.u_error = 0;
		iread(ip, &io);
		if (u.u_error != 0)
			return (0);
		io.io_seek += sizeof(struct ldsym);
		t = sym.ls_type;
		canint(t);
		if (t == LI_LIB) {
			lp = libl[i++];
			continue;
		}
		if (t != LI_IMP || lp == NULL)
			continue;
		t = slexport(lp, sym.ls_id, &eoff, &eflg);
		va = sym.ls_addr;
		canvaddr(va);
		if (t < 0 || (p = slslotp(va)) == 0) {
			slname(nb, sym.ls_id);
			printf("shlib: slot %lx vanished for %s\n", va, nb);
			return (0);
		}
		/*
		 * Data lives in the private half unless SE_SHRD.  A data slot
		 * holds an addend (e.g. `a[1]') in its low word.
		 */
		if ((eflg&SE_DATA) != 0) {
			slpread(p, ptr, 4);
			eoff += ((unsigned short)(ptr[2]&0xFF) << 8)
			      | (unsigned short)(ptr[3]&0xFF);
			ptr[0] = ((eflg&SE_SHRD) != 0 ? lp->sl_seg
						      : lp->sl_seg+1) & 0x7F;
		} else
			ptr[0] = lp->sl_seg & 0x7F;
		ptr[1] = 0;
		ptr[2] = (eoff >> 8) & 0xFF;
		ptr[3] = eoff & 0xFF;
		slpwrite(ptr, p, 4);
	}
	return (1);
}

/*
 * The heap ceiling: the lowest library segment, or 0 for NHUSEG.
 */
slceil(libl, nlib)
SLIB **libl;
{
	register int i;
	register int c;

	c = 0;
	for (i = 0; i < nlib; i++) {
		if (c == 0 || libl[i]->sl_seg < c)
			c = libl[i]->sl_seg;
	}
	return (c);
}
