/*
 * hog.c -- memory pressure, so the shared-library cases can be run against a
 * system that is actually short of core.
 *
 * Takes 40000 bytes, fills it with a pattern keyed to its argument, sleeps
 * while the others do the same, and reads it back.  Enough of these together
 * ask for more than the machine has, so the swapper must put some out and
 * bring them back; a wrong pattern says the memory it was given back is not
 * the memory it had.
 *
 * An int here is 16 bits, so the size is asked for in two sbrk() calls and
 * the index is unsigned.
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
	if ((p = sbrk(HOGHALF)) == (char *)-1
	 || sbrk(HOGHALF) == (char *)-1) {
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
	/* Failing here means the memory was never ours, not lost over a swap. */
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
