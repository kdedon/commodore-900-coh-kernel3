/*
 * shlibhold.c -- holds all seven libraries while a child execs shlibover,
 * which wants an eighth and must be refused.
 *
 * It names gen2..gen8, so loading gen8 forces the reclaim of gen1.
 */
#include <stdio.h>

extern int	gen2_add();
extern int	gen3_add();
extern int	gen4_add();
extern int	gen5_add();
extern int	gen6_add();
extern int	gen7_add();
extern int	gen8_add();

main()
{
	int s, st;

	s = gen2_add(1, 1) + gen3_add(1, 1) + gen4_add(1, 1) + gen5_add(1, 1)
	  + gen6_add(1, 1) + gen7_add(1, 1) + gen8_add(1, 1);
	printf("shlibhold[%d]: sum %d\n", getpid(), s);
	fflush(stdout);
	if (fork() == 0) {
		execl("/bin/shlibover", "shlibover", (char *)0);
		printf("shlibhold: the eighth exec was refused\n");
		fflush(stdout);
		exit(7);
	}
	wait(&st);
	printf("shlibhold[%d]: child status %d, still holding %d\n", getpid(),
		(st >> 8) & 0377, gen2_add(1, 1));
	fflush(stdout);
	return (0);
}
