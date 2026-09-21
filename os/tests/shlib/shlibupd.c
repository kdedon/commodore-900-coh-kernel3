/*
 * shlibupd.c -- imports toyd_box directly and through libup.1; both must
 * reach the same object.
 */
#include <stdio.h>

struct toybox {
	int	tb_a;
	int	tb_b;
};

extern struct toybox	toyd_box;
extern int		toyd_set();
extern char		*toyd_where();
extern int		up_sum();
extern char		*up_where();

main()
{
	printf("shlibupd[%d]: where %lx %lx\n", getpid(),
		(long)toyd_where(), (long)up_where());
	(void)toyd_set(7);
	printf("shlibupd[%d]: after set b %d up_sum %d\n", getpid(),
		toyd_box.tb_b, up_sum());
	fflush(stdout);
	return (0);
}
