/*
 * hog.c -- memory pressure for the shared-library cases.
 *
 * Fills 40000 bytes with a pattern, sleeps, and checks it survived swapping.
 * Ints are 16 bits, so it takes two sbrk() calls.
 *
 *	hog <n> <seconds>
 */
#include <stdio.h>

extern char	*sbrk();

#define	HOGHALF	20000
#define	HOGSIZE	((unsigned)(2 * HOGHALF))

main(argc, argv)
int argc;
char *argv[];
{
	register char *p;
	register unsigned i;
	register int k;
	int n, secs;

	n = argc > 1 ? atoi(argv[1]) : 1;
	secs = argc > 2 ? atoi(argv[2]) : 8;
	/* sbrk() fails with NULL here. */
	if ((p = sbrk(HOGHALF)) == NULL || sbrk(HOGHALF) == NULL) {
		printf("hog %d: no memory\n", n);
		fflush(stdout);
		return (1);
	}
	printf("hog %d: %u bytes at %08lx, break now %08lx\n", n, HOGSIZE,
		(long)p, (long)sbrk(0));
	fflush(stdout);
	k = n * 7 + 1;
	for (i = 0; i < HOGSIZE; i++)
		p[i] = (char)(i + k);
	for (i = 0; i < HOGSIZE; i++) {
		if (p[i] != (char)(i + k)) {
			printf("hog %d: BAD BEFORE at %u\n", n, i);
			fflush(stdout);
			return (1);
		}
	}
	sleep(secs);
	for (i = 0; i < HOGSIZE; i++) {
		if (p[i] != (char)(i + k)) {
			printf("hog %d: CORRUPT at %u\n", n, i);
			fflush(stdout);
			return (1);
		}
	}
	printf("hog %d: ok\n", n);
	fflush(stdout);
	return (0);
}
