/*
 * sbrkgap.c -- is a break larger than core refused?
 *
 * Grows data a block at a time, then asks for more than core but less than
 * swap, checking the kernel's break after each call:
 *
 *	granted		break covers the block, which is read-write
 *	refused		ENOMEM and the break unmoved
 *
 * brk(NULL) reports the kernel's break but sets __end, so callers restore
 * it.  Addresses are seg:off, so they are compared as linear distances.
 */
#include <stdio.h>
#include <errno.h>
#include <types.h>

#define	BLOCK	20000
#define	BLOCKS	3
#define	OVER	18		/* segments past the break: > core, < swap */

extern	char	*sbrk();
extern	char	*brk();
extern	vaddr_t	__end;

int	fails;

long
linear(v)
vaddr_t v;
{
	return ((long)((v >> 24 & 0x7F) << 16) + (v & 0xFFFF));
}

long
kbrk()
{
	vaddr_t save;
	char *p;

	save = __end;
	p = brk((char *)0);
	__end = save;
	return (linear((vaddr_t)p));
}

main()
{
	register unsigned i;
	register char *p;
	register char *last;
	vaddr_t save, far;
	long b0, b1;
	int n;

	last = NULL;
	for (n = 0; n < BLOCKS; n++) {
		b0 = kbrk();
		p = sbrk((unsigned)BLOCK);
		b1 = kbrk();
		if (p == NULL) {
			printf("sbrkgap: BAD  block %d refused\n", n);
			fails++;
			break;
		}
		printf("  block %d at %08lx, break %ld\n", n, (long)p, b1);
		fflush(stdout);
		if (b1 - linear((vaddr_t)p) < BLOCK) {
			printf("sbrkgap: BAD  block %d: break %ld short\n",
				n, BLOCK - (b1 - linear((vaddr_t)p)));
			fails++;
			break;
		}
		for (i = 0; i < BLOCK; i++)
			p[i] = (char)(i + n);
		last = p;
	}
	if (fails) {
		printf("sbrkgap: FAIL\n");
		exit(1);
	}

	/* brk() sets __end even when it fails. */
	b0 = kbrk();
	save = __end;
	far = save >> 24 & 0x7F;
	far += OVER;
	far <<= 24;
	p = (char *)far;
	errno = 0;
	brk(p);
	n = errno;
	__end = save;
	b1 = kbrk();
	printf("  asked %08lx: errno %d, break %ld\n", (long)p, n, b1);
	if (n != ENOMEM) {
		printf("sbrkgap: BAD  granted, errno %d\n", n);
		fails++;
	}
	if (b1 != b0) {
		printf("sbrkgap: BAD  refused, break moved %ld\n", b1 - b0);
		fails++;
	}
	for (i = 0; i < BLOCK; i++)
		if (last[i] != (char)(i + BLOCKS - 1)) {
			printf("sbrkgap: BAD  last block lost at %u\n", i);
			fails++;
			break;
		}
	printf("sbrkgap: %s\n", fails ? "FAIL" : "PASS");
	exit(fails != 0);
}
