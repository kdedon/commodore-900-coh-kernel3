/* SPDX-License-Identifier: MIT */
/*
 * getrlimit() and setrlimit().  Only RLIMIT_STACK is kept, in the u-area, so
 * fork and exec inherit it; exec gives a program a stack of the soft limit.
 */
#include <sys/coherent.h>
#include <errno.h>
#include <sys/uproc.h>
#include <sys/resource.h>

ugetrlimit(r, rlp)
int r;
struct rlimit *rlp;
{
	struct rlimit rl;

	if (r != RLIMIT_STACK) {
		u.u_error = EINVAL;
		return (-1);
	}
	rl.rlim_cur = u.u_stkcur;
	rl.rlim_max = u.u_stkmax;
	kucopy(&rl, rlp, sizeof(rl));
	return (0);
}

usetrlimit(r, rlp)
int r;
struct rlimit *rlp;
{
	struct rlimit rl;

	if (r != RLIMIT_STACK) {
		u.u_error = EINVAL;
		return (-1);
	}
	ukcopy(rlp, &rl, sizeof(rl));
	if (u.u_error)
		return (-1);
	if (rl.rlim_cur < 0 || rl.rlim_max < 0 || rl.rlim_cur > rl.rlim_max) {
		u.u_error = EINVAL;
		return (-1);
	}
	/* Only the superuser raises a hard limit. */
	if (rl.rlim_max > u.u_stkmax && super() == 0)
		return (-1);
	u.u_stkcur = rl.rlim_cur;
	u.u_stkmax = rl.rlim_max;
	return (0);
}
