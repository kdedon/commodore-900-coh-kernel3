/*
 * shlibup.c -- names only libup.1, which imports from libtoyd.1, so exec
 * must map libtoyd.1 too.
 *
 *	(none)	call through libup.1 into libtoyd.1's code and data
 *	b <n>	also bump libup.1's own private counter n times
 */
#include <stdio.h>

extern int	up_sum();
extern int	up_bump();
extern int	up_tab();
extern char	*up_where();

main(argc, argv)
int argc;
char **argv;
{
	int i, n;

	if (argc > 1 && argv[1][0] == 'b') {
		n = atoi(argv[2]);
		for (i = 0; i < n; i++)
			(void)up_bump();
		printf("shlibup[%d]: bump %d\n", getpid(), up_bump());
		fflush(stdout);
		return (0);
	}
	printf("shlibup[%d]: up_sum %d tab3 %d where %lx\n",
		getpid(), up_sum(), up_tab(3), (long)up_where());
	fflush(stdout);
	return (0);
}
