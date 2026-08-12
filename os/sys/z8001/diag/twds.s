/ SPDX-License-Identifier: BSD-3-Clause
/ Added alongside, not in place of, the Mark Williams notice below: the same
/ rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
/ (lgl-
/ 	The information contained herein is a trade secret of Mark Williams
/ 	Company, and  is confidential information.  It is provided  under a
/ 	license agreement,  and may be  copied or disclosed  only under the
/ 	terms of  that agreement.  Any  reproduction or disclosure  of this
/ 	material without the express written authorization of Mark Williams
/ 	Company or persuant to the license agreement is unlawful.
/ 
/ 	COHERENT Version 0.7.3
/ 	Copyright (c) 1982, 1983, 1984.
/ 	An unpublished work by Mark Williams Company, Chicago.
/ 	All rights reserved.
/ -lgl)
	.shri
	.globl wdcheck_
wdcheck_:
	ld	r0, rr14(8)		/ get character value to compare
	ldb	rh0, rl0		/ duplicate so we can use word mode
	ldl	rr2, rr14(4)		/ get pointer to buffer
	ld	r1, $256		/ words per buffer
	cpir	r0, @rr2, r1, ne	/ search for mismatch
	jr	z, 1f			/ z == found it
	clr	r1			/ indicate all OK
	ret	un			/
1:	ldk	r1, $1			/ indicate error
	ret	un			/

	.globl wdcompare_
wdcompare_:
	ld	r0, $512		/ bytes per block
	ldl	rr2, rr14(4)		/ first pointer
	ldl	rr4, rr14(8)		/ second pointer
	cpsirb	@rr2, @rr4, r0, ne	/ check to make sure they match
	jr	z, 1f			/ z == found mismatch
	clr	r1			/ indicate all is OK
	ret	un			/
1:	ldk	r1, $1			/ indicate error
	ret	un			/

	.globl wdblkinit_		/ fills a block with a char value
wdblkinit_:
	ld	r0, rr14(8)		/ get character value
	ldb	rh0, rl0		/ duplicate so we can use word mode
	ldl	rr2, rr14(4)		/ get pointer to buffer
	ld	@rr2, r0		/ store seed
	ldl	rr4, rr2		/ destination pointer
	inc	r5, $2			/ adjust pointer
	ld	r1, $256-1		/ number of words in block
	ldir	@rr4, @rr2, r1		/ block move to fill memory
	ret	un
