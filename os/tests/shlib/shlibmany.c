/*
 * shlibmany.c -- names eight libraries, one over NSLIB.  exec must refuse
 * the eighth by name, so main never runs.
 */
#include <stdio.h>

extern int	gen1_add();
extern int	gen2_add();
extern int	gen3_add();
extern int	gen4_add();
extern int	gen5_add();
extern int	gen6_add();
extern int	gen7_add();
extern int	gen8_add();

main()
{
	int s;

	s = gen1_add(1, 1) + gen2_add(1, 1) + gen3_add(1, 1) + gen4_add(1, 1)
	  + gen5_add(1, 1) + gen6_add(1, 1) + gen7_add(1, 1) + gen8_add(1, 1);
	printf("shlibmany[%d]: sum %d\n", getpid(), s);
	fflush(stdout);
	return (0);
}
