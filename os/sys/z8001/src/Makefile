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
md.o: md.s
	echo remove PARANOID when all is well !!!!
	/lib/cpp -E -DPARANOID -DNLD -UDDT md.s >/tmp/md.i
	/bin/as -go md.o /tmp/md.i
	rm -f /tmp/md.i
