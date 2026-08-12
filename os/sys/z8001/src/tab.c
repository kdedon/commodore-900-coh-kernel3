/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/*
 * The Z8001 system call table (sysitab + sysmtab): the 0.7.3 table with
 * the 3.2 MI additions wired in.
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
 * Coherent running in Segmented mode
 * on the Commodore M-series Z8001.
 * System call tables.
 */
#include <coherent.h>
#include <systab.h>

/* 0.7.3 systab tokens the 3.2 header dropped -- private to the
 * trap.c/tab.c pair (LONG comes from the 3.2 header; only the VALUE
 * changed, and both files see the same one) */
#ifndef VOID
#define	VOID	0
#define	PTR	1
#define	I	sizeof(int)
#define	L	sizeof(long)
#define	P	sizeof(char *)
#endif

/*
 * System call functions.
 */
int	unone();
int	unull();
int	uexit();
int	ufork();
int	uread();
int	uwrite();
int	uopen();
int	uclose();
int	uwait();
int	ucreat();
int	ulink();
int	uunlink();
int	uexece();
int	uchdir();
int	umknod();
int	uchmod();
int	uchown();
char	*ubrk();
int	ustat();
long	ulseek();
int	ugetpid();
int	umount();
int	uumount();
int	usetuid();
int	ugetuid();
int	ustime();
int	uptrace();
int	ualarm();
int	ufstat();
int	upause();
int	uutime();
int	uaccess();
int	unice();
int	uftime();
int	uftime();
int	usync();
int	ukill();
int	udup();
int	upipe();
int	utimes();
int	uprofil();
long	uunique();
int	usetgid();
int	ugetgid();
int	(*usignal())();
int	usload();
int	usuload();
int	uacct();
int	ulock();
int	uioctl();
int	ugetegid();
int	ugeteuid();
int	uumask();
int	uchroot();
int	usetpgrp();
int	ugetpgrp();
int	ufcntl();
int	upoll();
int	uuname();
long	ualarm2();
long	utick();

/*
 * Machine independent system call table.
 */
struct systab sysitab[NMICALL] ={
	0,		  INT,	unone,		/*  0 = ??? */
	I,		  INT,	uexit,		/*  1 = exit */
	0,		  INT,	ufork,		/*  2 = fork */
	I+P+I,		  INT,	uread,		/*  3 = read */
	I+P+I,		  INT,	uwrite,		/*  4 = write */
	P+I+I,		  INT,	uopen,		/*  5 = open (3-arg S5 form; mode read only under O_CREAT) */
	I,		  INT,	uclose,		/*  6 = close */
	P,		  INT,	uwait,		/*  7 = wait */
	P+I,		  INT,	ucreat,		/*  8 = creat */
	P+P,		  INT,	ulink,		/*  9 = link */
	P,		  INT,	uunlink,	/* 10 = unlink */
	P+P+P,		  INT,	uexece,		/* 11 = exec */
	P,		  INT,	uchdir,		/* 12 = chdir */
	/*
	 * 13 = uname.  The i386 kernels reach uname through utssys, call 57,
	 * a three-argument multiplexor shared with ustat; 57 here has been
	 * geteuid since 0.7.3, so this system spends one of its own free
	 * numbers on a plain one-argument uname instead.  Nothing calls
	 * utssys on this machine, and a multiplexor whose only case is uname
	 * would be a selector argument no caller ever varies.
	 */
	P,		  INT,	uuname,		/* 13 = uname */
	P+I+I,		  INT,	umknod,		/* 14 = mknod */
	P+I,		  INT,	uchmod,		/* 15 = chmod */
	P+I+I,		  INT,	uchown,		/* 16 = chown */
	P,		  PTR,	ubrk,		/* 17 = break */
	P+P,		  INT,	ustat,		/* 18 = stat */
	I+L+I,		  LONG,	ulseek,		/* 19 = lseek */
	0,		  INT,	ugetpid,	/* 20 = getpid */
	P+P+I,		  INT,	umount,		/* 21 = mount */
	P,		  INT,	uumount,	/* 22 = umount */
	I,		  INT,	usetuid,	/* 23 = setuid */
	0,		  INT,	ugetuid,	/* 24 = getuid */
	P,		  INT,	ustime,		/* 25 = stime */
	I+I+P+I,	  INT,	uptrace,	/* 26 = ptrace */
	I,		  INT,	ualarm,		/* 27 = alarm */
	I+P,		  INT,	ufstat,		/* 28 = fstat */
	0,		  INT,	upause,		/* 29 = pause */
	P+P,		  INT,	uutime,		/* 30 = utime */
	0,		  INT,	unone,		/* 31 = ??? */
	0,		  INT,	unone,		/* 32 = ??? */
	P+I,		  INT,	uaccess,	/* 33 = access */
	I,		  INT,	unice,		/* 34 = nice */
	P,		  INT,	uftime,		/* 35 = ftime */
	0,		  INT,	usync,		/* 36 = sync */
	I+I,		  INT,	ukill,		/* 37 = kill */
	0,		  INT,	unone,		/* 38 = ??? */
	0,		  INT,	unone,		/* 39 = ??? */
	0,		  INT,	unone,		/* 40 = ??? */
	I+I,		  INT,	udup,		/* 41 = dup */
	P,		  INT,	upipe,		/* 42 = pipe */
	P,		  INT,	utimes,		/* 43 = times */
	P+I+I+I,	  INT,	uprofil,	/* 44 = profil */
	0,		 LONG,	uunique,	/* 45 = unique */
	I,		  INT,	usetgid,	/* 46 = setgid */
	0,		  INT,	ugetgid,	/* 47 = getgid */
	I+P,		  PTR,	usignal,	/* 48 = signal */
	0,		  INT,	unone,		/* 49 = ??? */
	0,		  INT,	unone,		/* 50 = ??? */
	P,		  INT,	uacct,		/* 51 = acct */
	I+I+I,		  INT,	unull,		/* 52 = ??? (phys) */
	I,		  INT,	ulock,		/* 53 = lock */
	I+I+P,		  INT,	uioctl,		/* 54 = ioctl */
	I+P,		  INT,	unone,		/* 55 = ??? (mpx) */
	0,		  INT,	ugetegid,	/* 56 = getegid */
	0,		  INT,	ugeteuid,	/* 57 = geteuid */
	0,		  INT,	unone,		/* 58 = ??? */
	0,		  INT,	unone,		/* 59 = ??? */
	I,		  INT,	uumask,		/* 60 = umask */
	P,		  INT,	uchroot,	/* 61 = chroot */
	0,		  INT,	usetpgrp,	/* 62 = setpgrp (3.2) */
	0,		  INT,	ugetpgrp,	/* 63 = getpgrp (3.2) */
	I+P+P,		  INT,	usload,		/* 64 = sload */
	I,		  INT,	usuload,	/* 65 = suload */
	/*
	 * The table ends at 73 (tick); NMICALL is 74 (see sys/param.h),
	 * so the table is exactly full -- no syscall number can dispatch
	 * through a null function pointer.
	 */
	I+I+P,		  INT,	ufcntl,		/* 66 = fcntl (3.2) */
	P+L+I,		  INT,	upoll,		/* 67 = poll (3.2; npoll is a LONG) */
	0,		  INT,	unone,		/* 68 (was msgctl) */
	0,		  INT,	unone,		/* 69 (was msgget) */
	0,		  INT,	unone,		/* 70 (was msgrcv) */
	0,		  INT,	unone,		/* 71 (was msgsnd) */
	L,		  LONG,	ualarm2,	/* 72 = alarm2 (3.2) */
	0,		  LONG,	utick,		/* 73 = tick (3.2) */
};
