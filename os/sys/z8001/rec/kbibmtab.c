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
 * Commodore 900 Keyboard driver
 * Really IBM PC compatible keyboard.
 * Tables to drive the state machine.
 */

#include <kbchar.h>
#include <kbtab.h>

/*
 * This table is indexed by the scan codes.
 * The comments contain scan codes (in hex) and key
 * numbers (from the Commodore Keyboard Documentation,
 * in decimal).
 */
KEY	ktab[] =	{
	KINV,	0,	0,	0,		/* Unused */
	0,	CESC,	CESC,	0,		/* SC01 */
	0,	'1',	'!',	0,		/* SC02 */
	KC,	'2',	'@',	0,		/* SC03 */
	0,	'3',	'#',	0,		/* SC04 */
	0,	'4',	'$',	0,		/* SC05 */
	0,	'5',	'%',	0,		/* SC06 */
	0,	'6',	'^',	0,		/* SC07 */
	0,	'7',	'&',	0,		/* SC08 */
	0,	'8',	'*',	0,		/* SC09 */
	0,	'9',	'(',	0,		/* SC0A */
	0,	'0',	')',	0,		/* SC0B */
	0,	'-',	'_', 	0,		/* SC0C */
	0,	'=',	'+',	0,		/* SC0D */
	KC,	'\b',	'\b',	CDEL,		/* SC0E */
	0,	'\t',	'\t',	0,		/* SC0F */
   KCAP|KC,	'q',	'Q',	ct('q'),	/* SC10 */
   KCAP|KC,	'w',	'W',	ct('w'),	/* SC11 */
   KCAP|KC,	'e',	'E',	ct('e'),	/* SC12 */
   KCAP|KC,	'r',	'R',	ct('r'),	/* SC13 */
   KCAP|KC,	't',	'T',	ct('t'),	/* SC14 */
   KCAP|KC,	'y',	'Y',	ct('y'),	/* SC15 */
   KCAP|KC,	'u',	'U',	ct('u'),	/* SC16 */
   KCAP|KC,	'i',	'I',	ct('i'),	/* SC17 */
   KCAP|KC,	'o',	'O',	ct('o'),	/* SC18 */
   KCAP|KC,	'p',	'P',	ct('p'),	/* SC19 */
	KC,	'[',	'{',	CESC,		/* SC1A */
	KC,	']',	'}',	035,		/* SC1B */
	KC,	'\r',	'\r',	'\r',		/* SC1C */
	KSHIFT,	0,	0,	SCT,		/* SC1D CTRL */
   KCAP|KC,	'a',	'A',	ct('a'),	/* SC1E */
   KCAP|KC,	's',	'S',	ct('s'),	/* SC1F */
   KCAP|KC,	'd',	'D',	ct('d'),	/* SC20 */
   KCAP|KC,	'f',	'F',	ct('f'),	/* SC21 */
   KCAP|KC,	'g',	'G',	ct('g'),	/* SC22 */
   KCAP|KC,	'h',	'H',	ct('h'),	/* SC23 */
   KCAP|KC,	'j',	'J',	ct('j'),	/* SC24 */
   KCAP|KC,	'k',	'K',	ct('k'),	/* SC25 */
   KCAP|KC,	'l',	'L',	ct('l'),	/* SC26 */
	0,	';',	':',	0,		/* SC27 */
	0,	'\'',	'"',	0,		/* SC28 */
	KC,	'`',	'~',	ct('`'),	/* SC29 */
	KSHIFT,	0,	0,	SS1,		/* SC2A - Left Shift */
	KC,	'\\',	'|',	ct('|'),	/* SC2B */
   KCAP|KC,	'z',	'Z',	ct('z'),	/* SC2C */
   KCAP|KC,	'x',	'X',	ct('x'),	/* SC2D */
   KCAP|KC,	'c',	'C',	ct('c'),	/* SC2E */
   KCAP|KC,	'v',	'V',	ct('v'),	/* SC2F */
   KCAP|KC,	'b',	'B',	ct('b'),	/* SC30 */
   KCAP|KC,	'n',	'N',	ct('n'),	/* SC31 */
   KCAP|KC,	'm',	'M',	ct('m'),	/* SC32 */
	0,	',',	'<',	0,		/* SC33 */
	0,	'.',	'>',	0,		/* SC34 */
	0,	'/',	'?',	0,		/* SC35 */
	KSHIFT,	0,	0,	SS2,		/* SC36 - Right Shift */
     KC,	'*',	PF3,	PF3,		/* SC37 */
	KSHIFT,	0,	0,	SAL,		/* SC38 - Alt Key */
	0,	' ',	' ',	0,		/* SC39 */
	KLOCK,	0,	0,	SCL,		/* SC3A - CAPS Lock */
	KC,	F1,	F1,	F1,		/* SC3B, K1 */
	KC,	F2,	F2,	F2,		/* SC3C, K2 */
	KC,	F3,	F3,	F3,		/* SC3D, K3 */
	KC,	F4,	F4,	F4,		/* SC3E, K4 */
	KC,	F5,	F5,	F5,		/* SC3F, K5 */
	KC,	F6,	F6,	F6,		/* SC40, K6 */
	KC,	F7,	F7,	F7,		/* SC41, K7 */
	KC,	F8,	F8,	F8,		/* SC42, K8 */
	KC,	F9,	F9,	F9,		/* SC43, K9 */
	KC,	F10,	F10,	F10,		/* SC44 */
	KLOCK,	0,	0,	SNL,		/* SC45 */
	KC,	CSLOCK,	CSLOCK,	CBRK,		/* SC46 */
	KC|KNL,	K7,	'7',	K7,		/* SC47 */
	KC|KNL,	K8,	'8',	K8,		/* SC48 */
	KC|KNL,	K9,	'9',	K9,		/* SC49 */
	KC,	'-',	'-',	KMINUS,		/* SC4A */
	KC|KNL,	K4,	'4',	K4,		/* SC4B */
	KC|KNL,	K5,	'5',	K5,		/* SC4C */
	KC|KNL,	K6,	'6',	K6,		/* SC4D */
	KC,	'+',	'+',	KPLUS,		/* SC4E */
	KC|KNL,	K1,	'1',	K1,		/* SC4F */
	KC|KNL,	K2,	'2',	K2,		/* SC50 */
	KC|KNL,	K3,	'3',	K3,		/* SC51 */
	KC|KNL,	K0,	'0',	K0,		/* SC52 */
	KC|KNL,	PF2,	'.',	PF2,		/* SC53 */
};
