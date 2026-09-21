/*
 * shlibno.c -- uses a library that is not installed.  exec must refuse it
 * by name and leave the shell alive.
 */
#include <stdio.h>

extern int toy_add();

main()
{
	printf("shlibno: should never run (%d)\n", toy_add(1, 1));
	return (0);
}
