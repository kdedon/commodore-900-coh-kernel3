/* SPDX-License-Identifier: BSD-3-Clause
 * Added alongside, not in place of, the Mark Williams notice below: the same
 * rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
 */
/* (-lgl
 * 	COHERENT Version 3.0
 * 	Copyright (c) 1982, 1990 by Mark Williams Company.
 * 	All rights reserved. May not be copied without permission.
 -lgl) */
/*
 * Coherent.
 * Canonical conversion routines for the Intel 8086.
 */

#ifndef	CANON_H
#define	CANON_H	CANON_H

#ifdef	Z8001
/*
 * Z8001 (big-endian): EVERY canonical field swaps -- the 0.7.3 Z8001
 * canon.h verbatim (the i8086 macros below are little-endian no-ops
 * for the 16-bit fields, which would silently garble the on-disk fs
 * and l.out structures on this machine).  _canw/_canl are in
 * libc/gen/canon.s (linked into the kernel).
 */
int	_canw();
long	_canl();

#define	canshort(i)	((i)=_canw(i))
#define	canint(i)	((i)=_canw(i))
#define	canlong(l)	((l)=_canl(l))
#define	canvaddr(v)	((v)=_canl(v))
#define	cansize(s)	((s)=_canl(s))
#define	candaddr(d)	((d)=_canl(d))
#define	cantime(t)	((t)=_canl(t))
#define	candev(d)	((d)=_canw(d))
#define	canino(i)	((i)=_canw(i))

#else	/* !Z8001 (i8086) */

long	_canl();

#define	candaddr(d)	((d)=_canl(d))
#define	candev(d)
#define	canino(i)
#define	canint(i)
#define	canlong(l)	((l)=_canl(l))
#define	canshort(i)
#define	cansize(s)	((s)=_canl(s))
#define	cantime(t)	((t)=_canl(t))
#define	canvaddr(v)

#endif	/* !Z8001 */

#endif
