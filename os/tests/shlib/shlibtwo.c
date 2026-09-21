/*
 * shlibtwo.c -- uses libtoy.1 and libtoy2.1.  They must land at different
 * pairs, each call reaching its own library's code and data.
 */
#include <stdio.h>

extern int	toy_add();
extern int	toy_bump();
extern char	*toy_name();
extern char	*toy_where();
extern char	*toy_code();
extern int	toy2_mul();
extern int	toy2_bump();
extern char	*toy2_name();
extern char	*toy2_code();

main()
{
	long c1, c2;

	c1 = (long)toy_code();
	c2 = (long)toy2_code();
	printf("shlibtwo[%d]: toy private data at %08lx\n", getpid(),
		(long)toy_where());
	printf("shlibtwo[%d]: toy code %08lx toy2 code %08lx\n", getpid(),
		c1, c2);
	printf("shlibtwo[%d]: toy \"%s\" toy2 \"%s\"\n", getpid(),
		toy_name(), toy2_name());
	printf("shlibtwo[%d]: add 3+4=%d mul 3*4=%d\n", getpid(),
		toy_add(3, 4), toy2_mul(3, 4));
	toy_bump(7);
	toy2_bump(9);
	printf("shlibtwo[%d]: counters %d %d\n", getpid(),
		toy_bump(0), toy2_bump(0));
	if ((c1 >> 24) == (c2 >> 24))
		printf("shlibtwo[%d]: SAME SEGMENT for both libraries\n",
			getpid());
	else
		printf("shlibtwo[%d]: two segments, %ld and %ld\n", getpid(),
			c1 >> 24, c2 >> 24);
	fflush(stdout);
	return (0);
}
