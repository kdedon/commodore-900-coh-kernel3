/*
 * shlibbad.c -- imports toy_bump_, which the installed library lacks.  exec
 * must refuse it, naming the symbol.
 */
#include <stdio.h>

extern int toy_add();
extern int toy_bump();

main()
{
	printf("shlibbad: should never run (%d)\n", toy_add(1, 1) + toy_bump(1));
	return (0);
}
