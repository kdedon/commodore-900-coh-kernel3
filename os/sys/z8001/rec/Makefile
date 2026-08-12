# SPDX-License-Identifier: BSD-3-Clause
# Added alongside, not in place of, the Mark Williams notice below: the same
# rights holder released COHERENT under BSD 3-Clause in 2015 (root LICENSE).
# (lgl-
# 	The information contained herein is a trade secret of Mark Williams
# 	Company, and  is confidential information.  It is provided  under a
# 	license agreement,  and may be  copied or disclosed  only under the
# 	terms of  that agreement.  Any  reproduction or disclosure  of this
# 	material without the express written authorization of Mark Williams
# 	Company or persuant to the license agreement is unlawful.
# 
# 	COHERENT Version 0.7.3
# 	Copyright (c) 1982, 1983, 1984.
# 	An unpublished work by Mark Williams Company, Chicago.
# 	All rights reserved.
# -lgl)
CC=../CC
CFLAGS=-I.
kv: v0.o mm.o mmas.o kb.o kbtab.o kv.o
	ld -o kv kv.o v0.o mm.o mmas.o kb.o kbtab.o -k/coherent
kb.o: kb.c
	$(CC) $(CFLAGS) kb.c
kv.o: kv.c
	$(CC) $(CFLAGS) kv.c
kbtab.o: kbtab.c
	$(CC) -DCCE=1 $(CFLAGS) kbtab.c
mm.o: mm.c
	$(CC) $(CFLAGS) mm.c
mmas.o: mmas.s
	/lib/cpp -E -DNLD -UDDT mmas.s > mmas.i
	$(AS) -go mmas.o mmas.i
	rm mmas.i
v0.o: v0.c
	$(CC) $(CFLAGS) v0.c
lrcon.o: lrcon.c
	$(CC) $(CFLAGS) lrcon.c
