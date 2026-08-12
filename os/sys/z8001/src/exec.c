/*
 * Commodore M-series Z8001 -- program loader (exec).
 *
 * FORWARD-PORT: this is the 0.7.3 sys/coh/exec.c exec family (pexece/
 * exlopen/exsread/exstack/excount) -- the PROVEN Z8001 l.out loader --
 * uplifted to the 3.2 contract:
 *   - SEG carries BYTE addresses (s_paddr/s_size); the 0.7.3 s_mbase
 *     click fields become btocrd(s_paddr)/plain-byte uses;
 *   - buffers are addressed via b_faddr (a directly usable far address
 *     on this machine, mmu.h);
 *   - sizes are fsize_t (3.2) rather than 0.7.3 size_t.
 * eveinit lives in mdstub.c; pload/puload (loadable-driver syscalls)
 * are deferred stubs there too.
 *
 * SHARED LIBRARIES (jump table, cmd/slgen).  An LF_SLIB image is exec'd
 * once, by a locked process that never runs: msetusr suspends it, so it
 * exists only to hold the library segments, and `slprocp' names it.  Its
 * L_SHRI+L_SHRD segment is SFSHRX, so every LF_SLREF client shares that
 * one text image; its private data is copied per client.  A client
 * reaching the library is a CALL to an absolute far address in the jump
 * table -- no runtime relocation -- so exec has only segments to do.
 */
#include <sys/coherent.h>
#include <acct.h>
#include <sys/buf.h>
#include <canon.h>
#include <sys/con.h>
#include <errno.h>
#include <sys/filsys.h>
#include <sys/ino.h>
#include <sys/inode.h>
#include <l.out.h>
#include <sys/proc.h>
#include <sys/seg.h>
#include <signal.h>
#include <sys/uproc.h>
#include <sys/io.h>
#include <sys/mmu.h>

/*
 * Sizes.
 */
#define	sh	((fsize_t)sizeof(struct ldheader))
#define si	lssize[L_SHRI]
#define pi	lssize[L_PRVI]
#define bi	lssize[L_BSSI]
#define sd	lssize[L_SHRD]
#define pd	lssize[L_PRVD]
#define bd	lssize[L_BSSD]

/*
 * Segments.
 */
#define upsp	pp->p_segp[SIUSERP]
#define	lssp	pp->p_segp[SISSLIB]
#define	lpsp	pp->p_segp[SIPSLIB]
#define sssp	pp->p_segp[SISTACK]
#define	sisp	pp->p_segp[SISTEXT]
#define pisp	pp->p_segp[SIPTEXT]
#define sdsp	pp->p_segp[SISDATA]
#define pdsp	pp->p_segp[SIPDATA]

/*
 * SISSLIB gates the whole shared-library facility: it exists only when
 * <sys/proc.h> carries the two library segment slots.  Until it does,
 * every block below compiles out and exec behaves as it does today.
 *
 * The one process holding the shared library image (proc.h declares it).
 * NULL until an LF_SLIB image is exec'd; pexit clears it again.
 */
#ifdef SISSLIB
PROC	*slprocp;
#endif

/*
 * Given the name of an executable l.out, a null terminated argument
 * list and a null terminated environment list, execute the l.out with the
 * given arguments and environments.
 */
pexece(np, argp, envp)
char	*np;
char	*argp[];
char	*envp[];
{
	register INODE	*ip;			/* Load file INODE */
	register PROC	*pp;			/* A cheap copy of SELF */
	register SEG	*ssp;			/* New stack segment */
	register fsize_t	ss;			/* Segment size temp. */
	register int	hialloc = 1;		/* Set if kernel process */
	register int	i;			/* For looping over segments */
	int		r;			/* Flag for "exload" */
	int		lflag;			/* l_flags from l.out */
	vaddr_t		pc;			/* l_entry from l.out */
	vaddr_t		sp;			/* Initial stack pointer */
	fsize_t		lssize[NUSEG];		/* Segment sizes */

	pp = SELF;
	if ((ip=exlopen(np, lssize, &lflag, &pc)) == NULL)
		return;
	ssp = NULL;
	if ((lflag&LF_KER) != 0) {
		pp->p_flags |= PFKERN;
		if (super() == 0) {
			idetach(ip);
			return;
		}
#ifdef SISSLIB
	} else if ((lflag&LF_SLIB) != 0) {
		pp->p_flags |= PFSLIB|PFLOCK;
		if (super() == 0) {
			idetach(ip);
			return;
		}
		if (slprocp != NULL) {
			u.u_error = ENOEXEC;
			idetach(ip);
			return;
		}
	} else if ((lflag&LF_SLREF)!=0 && slprocp==NULL) {
			/*
			 * Perhaps should be made its own errno.
			 */
		u.u_error = ENOEXEC;
		idetach(ip);
		return;
#endif
	} else {
		hialloc = 0;
		if ((ssp = exstack(&sp, argp, envp)) == NULL) {
			idetach(ip);
			return;
		}
	}
	/*
	 * At this point the file has been
	 * validated as an object module, and the
	 * argument list has been built. Release all of
	 * the original segments. At this point we have
	 * committed to the new image. A "sys exec" that
	 * gets an I/O error is doomed.
	 */
	for (i=1; i<NUSEG; ++i) {
		if (pp->p_segp[i] != NULL) {
			sfree(pp->p_segp[i]);
			pp->p_segp[i] = NULL;
		}
	}
	sssp = ssp;
	/*
	 * Read in load module.
	 */
	switch (lflag&(LF_SHR|LF_SEP)) {
	case 0:
		ss = si+pi+sd+pd;
		pdsp = ssalloc(&r, ip, hialloc?SFHIGH:0, ss+bi+bd, sh, ss);
		if (r < 0)
			goto out;
		break;

	case LF_SHR:
		sdsp = ssalloc(&r, ip, SFSHRX, si+sd, sh, si);
		if (r < 0)
			goto out;
		if (r == 0) {
			if (exsread(sdsp, ip, sd, sh+si+pi, si) == 0)
				goto out;
		}
		pdsp = ssalloc(&r, ip, 0, pi+pd+bi+bd, sh+si, pi);
		if (r < 0)
			goto out;
		if (r == 0) {
			if (exsread(pdsp, ip, pd, sh+si+pi+sd, pi) == 0)
				goto out;
		}
		break;

	case LF_SEP:
		pisp = ssalloc(&r, ip, SFTEXT, si+pi+bi, sh, si+pi);
		if (r < 0)
			goto out;
		pdsp = ssalloc(&r, ip, 0, sd+pd+bd, sh+si+bi, sd+pd);
		if (r < 0)
			goto out;
		break;

	case LF_SHR|LF_SEP:
		sisp = ssalloc(&r, ip, SFSHRX|SFTEXT, si, sh, si);
		if (r < 0)
			goto out;
		pisp = ssalloc(&r, ip, SFTEXT, pi+bi, sh+si, pi);
		if (r < 0)
			goto out;
		sdsp = ssalloc(&r, ip, SFSHRX, sd, sh+si+pi, sd);
		if (r < 0)
			goto out;
		pdsp = ssalloc(&r, ip, 0, pd+bd, sh+si+pi+sd, pd);
		if (r < 0)
			goto out;
	}
#ifdef SISSLIB
	if ((lflag & LF_SLIB) != 0) {
		if (sisp!=NULL || pisp!=NULL || sdsp==NULL) {
			u.u_error = ENOEXEC;
			goto out;
		}
		slprocp = pp;
	} else if ((lflag & LF_SLREF) != 0) {
		register PROC *slpp;

		if ((slpp = slprocp) == NULL
		 || (lssp = slpp->p_segp[SISDATA]) == NULL
		 || (lssp = segdupl(lssp)) == NULL) {
			u.u_error = ENOEXEC;
			goto out;
		}
		if ((lpsp = slpp->p_segp[SIPDATA]) != NULL
		 && (lpsp = segdupl(lpsp)) == NULL) {
			u.u_error = ENOEXEC;
			goto out;
		}
	}
#endif
	if (sproto() == 0)
		goto out;
	/*
	 * The new image is read in
	 * and mapped. Perform the final grunge
	 * (set-uid stuff, accounting, loading up
	 * registers, etc).
	 */
	u.u_flag &= ~AFORK;
	kkcopy(u.u_direct.d_name, u.u_comm, sizeof(u.u_comm));
	if (iaccess(ip, IPR) == 0)
		pp->p_flags |= PFNDMP;
	if ((ip->i_mode&ISUID) != 0)
		pp->p_uid = u.u_uid = ip->i_uid;
	if ((ip->i_mode&ISGID) != 0)
		u.u_gid = ip->i_gid;
	for (i=0; i<NSIG; ++i) {
		if (u.u_sfunc[i] != SIG_IGN)
			u.u_sfunc[i] = SIG_DFL;
	}
	if ((pp->p_flags&PFTRAC) != 0)
		sendsig(SIGTRAP, pp);
	idetach(ip);
	segload();
	msetusr(pc, sp);
	return (0);

	/*
	 * We did not make it.
	 * Release the INODE for the load
	 * file, and return through the "sys exit"
	 * code with a "SIGSYS".
	 */
out:
	idetach(ip);
	pexit(SIGSYS);
}

/*
 * Open an l.out, make sure it is an l.out and executable and return the
 * appropriate information.
 */
INODE *
exlopen(np, ssizep, flagp, pcp)
char *np;
fsize_t *ssizep;
int *flagp;
vaddr_t *pcp;
{
	register INODE *ip;
	register struct ldheader *ldp;
	register int n;
	register BUF *bp;
	int m;

	/*
	 * Make sure the file is really an executable l.out and read the
	 * header in.
	 */
	if (ftoi(np, 'r') != 0)
		return (NULL);
	ip = u.u_cdiri;
	if (iaccess(ip, IPE) == 0) {
		idetach(ip);
		return (NULL);
	}
	if ((ip->i_mode&(IPE|IPE<<3|IPE<<6))==0 || (ip->i_mode&IFMT)!=IFREG) {
		u.u_error = EACCES;
		idetach(ip);
		return (NULL);
	}
	if ((bp=vread(ip, (daddr_t)0)) == NULL) {
		u.u_error = EBADFMT;
		idetach(ip);
		return (NULL);
	}

	/*
	 * Copy everything we need from the l.out header and check magic
	 * number and machine type.
	 */
	ldp = (struct ldheader *)bp->b_faddr;
	m = ldp->l_magic;
	canint(m);
	if (m != L_MAGIC) {
		u.u_error = ENOEXEC;
		brelease(bp);
		idetach(ip);
		return (NULL);
	}
	m = ldp->l_machine;
	canint(m);
	if (m != mactype) {
		u.u_error = EBADFMT;
		brelease(bp);
		idetach(ip);
		return (NULL);
	}
	kkcopy(ldp->l_ssize, ssizep, NXSEG*sizeof(fsize_t));
	for (n=0; n<NXSEG; n++)
		cansize(ssizep[n]);
	*flagp = ldp->l_flag;
	canint(*flagp);
	*pcp = ldp->l_entry;
	canvaddr(*pcp);
	brelease(bp);
	return (ip);
}

/*
 * Given a segment `sp', read `ss' bytes from the inode `ip' starting
 * at seek address `sa' into offset `so' in the segment.
 */
SEG *
exsread(sp, ip, ss, sa, so)
register SEG *sp;
INODE *ip;
fsize_t sa;
register fsize_t ss;
fsize_t so;
{
	register int n;

	u.u_io.io_seg = IOPHY;
	u.u_io.io_seek = sa;
	u.u_io.io_phys = sp->s_paddr + so;	/* 3.2: bytes */
	u.u_error = 0;
	while (u.u_error==0 && ss!=0) {
		n = ss>SCHUNK ? SCHUNK : ss;
		u.u_io.io_ioc = n;
		ss -= n;
		iread(ip, &u.u_io);
		u.u_io.io_seek += n;	/* Coherent bug? */
	}
	/* 3.2 contract: the segment on success, NULL on error (truth-
	 * compatible with the 0.7.3 int form both here and in seg.c) */
	return (u.u_error==0 ? sp : (SEG *)0);
}

/*
 * Given a pointer to a list of arguments and a pointer to a list of
 * environments, return a stack with the arguments and environments on it.
 */
SEG *
exstack(iusp, argp, envp)
char **iusp;		/* Back patch sp value */
char *argp[];		/* Arguments for new process */
char *envp[];		/* Environments for new process */
{
	SEG *sp;		/* Stack segment pointer */
	struct adata {		/* Storage for arg and env data */
		char	**up;		/* User vector pointer */
		int	np;		/* Number of pointers in vector */
		int	nc;		/* Number of characters in strings */
	} arg, env;
	struct sdata {		/* To keep segment pointers */
		vaddr_t	base;		/* Top of segment virtual */
		vaddr_t	ap;		/* Argc, argv, envp pointer */
		vaddr_t	vp;		/* Argv[i], envp[i] pointer */
		vaddr_t	cp;		/* Argv[i][j], envp[i][j] pointer */
	} aux, stk;
	aold_t aold;			/* Auxiliary map storage */
	register char **usrvp;		/* Vector pointer into user seg */
	register char *usrcp;		/* Character pointer into user seg */
	register int c;			/* Character fetched from user */
	register int chrsz;		/* Size of strings */
	register struct adata *adp;	/* Arg and env scanner */
	register int vecsz;		/* Size of vectors */
	register int stksz;		/* Size of stack argument region */

	/* Validate and evaluate size of args and envs */
	arg.up = argp;
	env.up = envp;
	chrsz = 0;
	vecsz = 0;
	for (adp = &arg; ; adp = &env) {
		adp->np = 0;
		adp->nc = 0;
		if (excount(adp->up, &adp->np, &adp->nc) == 0)
			return (NULL);
		chrsz += adp->nc * sizeof(char);
		vecsz += adp->np * sizeof(char *);
		if (adp == &env)
			break;
	}

	/* Calculate stack size and allocate it */
	chrsz = roundu(chrsz, sizeof(int));
	stksz = sizeof(int)		/* argc */
		+ sizeof(char **)	/* argv */
		+ sizeof(char **)	/* envp */
		+ vecsz			/* argv[i] and envp[i] */
		+ chrsz			/* *argv[i] and *envp[i] */
		+ sizeof(int)		/* Mystery zero word */
		+ sizeof(char *)	/* Splimit for z8000 */
		+ sizeof(int);		/* errno */
	stksz += ISTSIZE;
	if (stksz > MADSIZE) {
		u.u_error = E2BIG;
		return (NULL);
	}
	/*
	 * The stack segment is mapped at its whole allowance here rather than
	 * at the argument region plus ISTSIZE, because on this machine it
	 * cannot be extended on demand once the program is running.
	 *
	 * Demand extension needs the MMU's write warning, and the warning
	 * region is the segment's lowest 256-byte page (stviol() in trap.c).
	 * A function whose frame is larger than that page subtracts the whole
	 * frame from r15 in one instruction and steps clean over the warning,
	 * so its first access lands below the segment and takes a length
	 * violation -- which has already aborted an instruction that cannot in
	 * general be restarted.  Only a stack that walks DOWN in steps smaller
	 * than the warning page can be grown from the trap, and nothing
	 * constrains a frame to be that small: 512 bytes of locals is enough to
	 * miss it.  So the allowance is committed while the segment is still
	 * being built, and stviol() is left holding the ceiling.
	 */
	if ((sp=salloc((fsize_t)MADSIZE, SFDOWN)) == NULL)
		return (NULL);
	stksz -= ISTSIZE;

	/*
	 * Initialize segment data.
	 */
	asave(aold);

	aux.base = (vaddr_t)abase(btocrd(sp->s_paddr)) + sp->s_size;
	aux.ap = aux.base - stksz;
	aux.vp = aux.ap + sizeof(int) + 2*sizeof(char **);
	aux.cp = aux.vp + vecsz;

	stk.base = ISTVIRT;
	stk.ap = stk.base - stksz;
	stk.vp = stk.ap + sizeof(int) + 2*sizeof(char **);
	stk.cp = stk.vp + vecsz;

	/*
	 * Write argc.
	 */
	aputi((int *)aux.ap, arg.np-1);
	aux.ap += sizeof(int);

	/*
	 * Arguments and environments.
	 */
	for (adp = &arg; ; adp = &env) {

		/* Write argv or envp */
		aputp((char ***)aux.ap, (char **)stk.vp);
		aux.ap += sizeof(char **);
		if ((usrvp = adp->up) != NULL) {

			/* Write argv[i] or envp[i] */
			while ((usrcp = getupd(usrvp++)) != NULL) {
				aputp((char **)aux.vp, (char *)stk.cp);
				aux.vp += sizeof(char *);
				stk.vp += sizeof(char *);

				/* Write argv[i][j] or envp[i][j] */
				do {
					c = getubd(usrcp++);
					aputc((char *)aux.cp, c);
					aux.cp += sizeof(char);
					stk.cp += sizeof(char);
				} while (c != '\0');
			}
		}

		/* Write argv[argc] or envp[envc] */
		aputp((char **)aux.vp, NULL);
		aux.vp += sizeof(char *);
		stk.vp += sizeof(char *);
		if (adp == &env)
			break;
	}

	/*
	 * Clear out the slop.
	 */
	aux.base -= sizeof(int);
	aputi((int *) aux.base, 0);		/* errno */
	aux.base -= sizeof(char *);
	aputp((char **) aux.base, (char *)stk.base-sp->s_size+SOVSIZE);
	aux.base -= sizeof(int);
	aputi((int *) aux.base, 0);		/* mystery word */

	arest(aold);

	/*
	 * Patch some values and return.
	 */
	*iusp = (char *)stk.ap;	/* Patch initial usp */
	u.u_argc = arg.np-1;
	u.u_argp = stk.vp;	/* Points after NULL of envs */
	return (sp);
}

/*
 * Given a pointer to a list of arguments, a pointer to an argument count
 * and a pointer to a byte count, update incrementally the argument count
 * and the byte count.
 */
excount(usrvp, nap, nbp)
register char **usrvp;
int *nap;
int *nbp;
{
	register char *usrcp;
	register int c;
	register unsigned nb;
	register unsigned na;

	na = 1;
	nb = 0;
	if (usrvp != NULL) {
		for (;;) {
			usrcp = getupd(usrvp++);
			if (u.u_error)
				return (0);
			if (usrcp == NULL)
				break;
			na++;
			for (;;) {
				c = getubd(usrcp++);
				if (u.u_error)
					return (0);
				nb++;
				if (c == '\0')
					break;
			}
		}
	}
	*nap += na;
	*nbp += nb;
	return (1);
}

/*
 * Given a major number, a file containing a device driver and a
 * configuration pointer, load the driver on that major number.  The
 * driver's l.out is read into a shared-text segment; the segment is
 * mapped through the OS transient window while its c_load() entry runs,
 * and drvl[] then routes every call on that major through it (bio.c
 * getdrv maps d_map around each entry point).
 */
pload(m, np, cp)
char *np;
CON *cp;
{
	register INODE *ip;
	register SEG *sp;
	register DRV *dp;
	register fsize_t ss;
	dold_t dold;
	int lflag;
	int r;
	vaddr_t pc;
	fsize_t lssize[NUSEG];

	if (m >= drvn) {
		u.u_error = ENXIO;
		return;
	}
	if ((ip=exlopen(np, lssize, &lflag, &pc)) == NULL)
		return;
	ss = pi+si+pd+sd;
	sp = ssalloc(&r, ip, SFSHRX, ss+bi+bd, sh, ss);
	idetach(ip);
	if (r < 0)
		return;
	/*
	 * A loaded driver must never move.  d_map below caches the segment's
	 * click base, and every call into the driver maps the OS window from
	 * it (bio.c getdrv), but nothing refreshes it -- so if krunch()
	 * compacted this segment into a hole, or the swapper took it out, the
	 * window would point at where the driver used to be.  SFSYST is what
	 * both of them check to leave a segment alone.
	 */
	sp->s_flags |= SFSYST;
	dp = &drvl[m];
	lock(dp->d_gate);
	if (dp->d_conp != NULL) {
		unlock(dp->d_gate);
		sfree(sp);
		u.u_error = EDBUSY;
		return;
	}
	dp->d_time = 0;
	dp->d_conp = cp;
	dp->d_segp = sp;
	dp->d_map = btocrd(sp->s_paddr);	/* 3.2 SEG carries a BYTE paddr;
						 * d_map is a click number */
	dsave(dold);
	dmapv(dp->d_map);
	(*cp->c_load)();
	drest(dold);
	unlock(dp->d_gate);
}

/*
 * Given a major number, undo the previous function.
 */
puload(m)
int m;
{
	register CON *cp;
	register DRV *dp;
	dold_t dold;

	dp = &drvl[m];
	lock(dp->d_gate);
	if (m>=drvn || dp->d_segp==NULL || (cp=dp->d_conp)==NULL) {
		u.u_error = ENXIO;
		goto ret;
	}
	dsave(dold);
	dmapv(dp->d_map);
	(*cp->c_uload)();
	drest(dold);
	if (u.u_error)
		goto ret;
	sfree(dp->d_segp);
	dp->d_conp = NULL;
	dp->d_segp = NULL;
	dp->d_map = 0;
ret:
	unlock(dp->d_gate);
	return (0);
}
