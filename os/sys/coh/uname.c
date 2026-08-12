/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/*
 * File:	uname.c
 *
 * Purpose:	the uname(2) system call: the kernel states its own identity.
 *
 *	The information contained herein is a trade secret of Mark Williams
 *	Company, and  is confidential information.  It is provided  under a
 *	license agreement,  and may be  copied or disclosed  only under the
 *	terms of  that agreement.  Any  reproduction or disclosure  of this
 *	material without the express written authorization of Mark Williams
 *	Company or persuant to the license agreement is unlawful.
 */

/*
 * Includes.
 */
#include <sys/coherent.h>
#include <sys/buf.h>
#include <sys/inode.h>
#include <sys/io.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <errno.h>

/*
 * Definitions.
 */
/* The node name lives in a file because no part of the kernel knows one, and
 * this is the file the i386 kernels read for it.  It is this machine's short
 * name -- eight characters, which is what a utsname field holds -- and it
 * ships with every dist.  /etc/hostname, which gethostname(3) reads, is the
 * FULL name (c900.localnet) and does not fit; the two are the same fact at
 * two lengths, not two sources, and uuinstall(1) rewrites both. */
#define	NODEFILE	"/etc/uucpname"

/*
 * Global Data.
 *	Import Variables.
 */
extern char release[];		/* main.c: the kernel deliverable's version */

/*
 * Local Variables.
 */
static char unknown[] = "unknown";

/*
 * Report the system's identity into the caller's struct utsname.
 *
 * The whole structure is built here and copied out in one piece, so every
 * field the caller sees is NUL-terminated whatever its buffer held before:
 * a field copied only as far as its own length leaves the rest of the user's
 * buffer standing, and the string then runs on into it.
 *
 * Returns 0, or -1 with EFAULT for an address the caller cannot write.  The
 * POSIX contract is "non-negative on success", so a caller must test < 0.
 */
uuname(name)
struct utsname *name;
{
	register char *cp;
	register INODE *ip;
	register int i;
	struct utsname uts;
	BUF *bp;
	char nodebuf[SYS_NMLN];
	int fl;
	int oerr;

	if (!useracc((char *)name, sizeof(struct utsname))) {
		u.u_error = EFAULT;
		return (-1);
	}
	for (cp = (char *)&uts, i = sizeof(uts); --i >= 0; )
		*cp++ = '\0';
	kkcopy(SYS_SYSNAME, uts.sysname, sizeof(SYS_SYSNAME));
	kkcopy(SYS_MACHINE, uts.machine, sizeof(SYS_MACHINE));
	kkcopy(SYS_VERSION, uts.version, sizeof(SYS_VERSION));
	for (cp = release, i = 0; *cp != '\0' && i < SYS_NMLN-1; i++, cp++)
		;
	kkcopy(release, uts.release, (unsigned)i);
	kkcopy(unknown, uts.nodename, sizeof(unknown));

	/*
	 * The node name, from the first line of NODEFILE.  Anything the file
	 * cannot supply -- it is absent, empty, unreadable, or holds nothing
	 * but the newline -- leaves the `unknown' already in place, because a
	 * machine that does not know its name must not report a made-up one.
	 *
	 * The path is the kernel's own constant, not the caller's argument, so
	 * the lookup goes through ftoi_sys() -- plain ftoi() would walk the
	 * user data segment at the offset the string sits at in kernel data.
	 * And every step here sets u.u_error on failure, which is what the trap
	 * path reports to the caller, so a machine with no NODEFILE would fail
	 * the whole call over the one field that is allowed to be missing:
	 * u.u_error is put back as it was found across the lot.
	 */
	oerr = u.u_error;
	if (ftoi_sys(NODEFILE, 'r') == 0) {
		ip = u.u_cdiri;
		if ((fl = ip->i_size) != 0 && iaccess(ip, IPR) != 0 &&
		    (bp = vread(ip, (daddr_t)0)) != NULL) {
			if (fl > SYS_NMLN-1)
				fl = SYS_NMLN-1;
			kkcopy(FP_OFF(bp->b_faddr), nodebuf, (unsigned)fl);
			brelease(bp);
			for (i = 0; i < fl; i++) {
				if (nodebuf[i] == '\n' || nodebuf[i] == '\0')
					break;
			}
			nodebuf[i] = '\0';
			if (i != 0)
				kkcopy(nodebuf, uts.nodename,
				       (unsigned)(i + 1));
		}
		idetach(ip);
	}
	u.u_error = oerr;
	kucopy(&uts, name, sizeof(uts));
	return (0);
}
