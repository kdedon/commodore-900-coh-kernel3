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
 * Magic machine numbers.
 * Intel 8086.
 */

#ifndef	MDATA_H
#define	MDATA_H	MDATA_H

/* Bits per type */
#define	NBCHAR	  8
#define	NBINT	  16
#define	NBLONG	  32
#define	NBFLOAT	  32
#define	NBDOUBLE  64
#define NBSHORT	  16

/* Bits per pointer.  Z8001 large model: every pointer is a 2-word seg:off
 * far pointer (32 bits) -- the donor's 16s were the i8086 small model and
 * made NBPCHAR==16 consumers (diff's vaddr_t) truncate stored pointers.
 * Matches the kernel's sys/z8001/h/mdata.h. */
#define	NBPCHAR	  32
#define	NBPINT	  32
#define NBPLONG	  32
#define NBPFLOAT  32
#define NBPDOUBLE 32
#define NBPSHORT  32
#define NBPSTRUCT 32
#define NBPUNION  32

/* Alignments, types */
#define	ALCHAR	  01
#define ALINT	  02
#define ALLONG	  02
#define ALFLOAT	  02
#define ALDOUBLE  02
#define ALSHORT	  02
#define ALSTRUCT  02
#define ALUNION   02

/* Alignments, pointers */
#define	ALPCHAR	  02
#define	ALPINT	  02
#define ALPLONG   02
#define ALPFLOAT  02
#define ALPDOUBLE 02
#define ALPSHORT  02
#define ALPSTRUCT 02
#define ALPUNION  02

/* Ranges */
#define	MAXCHAR	127
#define	MAXUCHAR 255
#define	MAXINT	32767
#define	MAXUINT	(65535L)
#define	MAXLONG	(2147483647L)
#define	MAXULONG (4294967295L)
#define	MININT	(-32768L)
#define MINLONG (-2147483648L)

/* Fixed point representation */
#define	TCINT	1
#define	OCINT	0
#define	SMINT	0

/* Base2 logarithms of bits per type */
#define	LOGCHAR	3
#define	LOGINT	4
#define	LOGLONG	5

#endif
