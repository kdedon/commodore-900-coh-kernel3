/*
 * shlibover.c -- uses only libgen1.1.  Refused while shlibhold holds the
 * other seven; succeeds by reclaiming once it does not.
 */
#include <stdio.h>

extern int	gen1_add();

main()
{
	printf("shlibover[%d]: sum %d\n", getpid(), gen1_add(1, 1));
	fflush(stdout);
	return (0);
}
