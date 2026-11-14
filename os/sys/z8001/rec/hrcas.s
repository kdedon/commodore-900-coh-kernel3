/ SPDX-License-Identifier: MIT
/ Inner loops of the colour console.

sp 	= rr14
spo	= r15

.globl	SS
/ Arguments, once rr6-rr12 are pushed.
#define args(n) SS|18+2*[n](spo)

.globl	hcmov_, hcrun_

/ k rasters of nw words from s to d.  After each raster d moves on a raster
/ and s by ss bytes: a raster stride copies a rectangle, 0 fills one.
/	hcmov(d, s, nw, k, ss)
/	char *d, *s;
hcmov_:
	pushl	(sp), rr6
	pushl	(sp), rr8
	pushl	(sp), rr10
	pushl	(sp), rr12
	ldl	rr4, args(1)
	ldl	rr6, args(3)
	ld	r1, args(5)
	ld	r0, args(6)
	ld	r8, args(7)
	test	r1
	jr	eq, 9f
	test	r0
	jr	eq, 9f
0:	ldl	rr2, rr4
	ldl	rr10, rr6
	ld	r9, r1
	ldir	(rr2), (rr10), r9
	add	r5, $320
	add	r7, r8
	djnz	r0, 0b
9:	popl	rr12, (sp)
	popl	rr10, (sp)
	popl	rr8, (sp)
	popl	rr6, (sp)
	ret

/ Draw rasters of n cells from p on: k rasters of each cell's glyph, from
/ f + 32 * the cell's character.  f holds each glyph byte times four, its
/ offset in t, a table of 256 two-word pixel runs.
/	hcrun(p, s, n, f, t, k)
/	char *p; unsigned *s; unsigned *f; long *t;

/ One raster: rr10 the glyph, rr12 the bitmap.
#define	RASTER \
	ld	r1, (rr10) ; \
	inc	r11, $2 ; \
	ldl	rr2, rr6 ; \
	add	r3, r1 ; \
	ldl	rr4, (rr2) ; \
	ldl	(rr12), rr4 ; \
	add	r13, $320

hcrun_:
	pushl	(sp), rr6
	pushl	(sp), rr8
	pushl	(sp), rr10
	pushl	(sp), rr12
	ldl	rr6, args(8)
	ld	r8, args(5)
	test	r8
	jr	eq, 9f
0:	ldl	rr2, args(3)		/ the next cell
	ld	r1, (rr2)
	inc	r3, $2
	ldl	args(3), rr2
	and	r1, $0xFF
	sll	r1, $5
	ldl	rr10, args(6)		/ its glyph
	add	r11, r1
	ldl	rr12, args(1)		/ its top raster
	ldl	rr4, rr12
	inc	r5, $4
	ldl	args(1), rr4
	ld	r0, args(10)
	and	r0, $3
	jr	eq, 2f
1:	RASTER
	djnz	r0, 1b
2:	ld	r0, args(10)
	srl	r0, $2
	test	r0
	jr	eq, 3f
4:	RASTER
	RASTER
	RASTER
	RASTER
	djnz	r0, 4b
3:	djnz	r8, 0b
9:	popl	rr12, (sp)
	popl	rr10, (sp)
	popl	rr8, (sp)
	popl	rr6, (sp)
	ret
