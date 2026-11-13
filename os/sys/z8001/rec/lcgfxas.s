/ SPDX-License-Identifier: MIT
/ Helpers for the graphics minor.

/ Set the limit of one system segment, in 256-byte blocks, highest valid.
/	plim(sn, lim)

sp 	= rr14
spo	= r15
MMU	= 0x00FC

.globl	SS
#define args(n) SS|2+2*[n](spo)
#define arglb(n) SS|3+2*[n](spo)

.globl	plim_

plim_:
	ldctl	r4, FCW
	ldb	rh0, arglb(1)
	ldb	rl0, arglb(2)
	di	VI
	soutb	MMU+0x0100, rh0
	soutb	MMU+0x0900, rl0
	ldctl	FCW, r4
	ret

/ Write bytes from p to the chip's data register, which the caller has
/ selected, polling ready before each.  Stop after n, or after a byte that
/ makes eight equal in a row while more remain, so the caller can send the
/ rest of that run as a block fill.  Returns the count written.
/	lcwlit(p, n)
/	char *p;

LCADDR	= 0x0181
LCDATA	= 0x0183

.globl	lcwlit_

lcwlit_:
	ldl	rr4, args(1)
	ld	r2, args(3)
	ld	r3, $0
0:	ldb	rl0, @rr4
	inc	r5, $1
1:	inb	rl1, LCADDR
	bitb	rl1, $7
	jr	z, 1b
	outb	LCDATA, rl0
	dec	r2, $1
	jr	z, 3f
	cpb	rl0, rh0
	jr	eq, 2f
	ldb	rh0, rl0
	ld	r3, $1
	jr	0b
2:	inc	r3, $1
	cp	r3, $8
	jr	ult, 0b
3:	ld	r1, args(3)		/ the count, returned in r1
	sub	r1, r2
	ret
