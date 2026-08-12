/*
 * rawalign -- exercise raw (character) disk I/O out of a buffer that is not
 * on a 512-byte boundary, and check the bytes rather than the return code.
 *
 *	rawalign r <rawdev> <blkdev> <nblocks> <offset>
 *		Read the same blocks through both devices into buffers at the
 *		same misalignment and compare them byte for byte.
 *
 *	rawalign w <rawdev> <blkdev> <nblocks> <offset> <seed>
 *		Write a seeded pattern through the raw device out of a
 *		misaligned buffer, then read it back through the block device
 *		and compare.
 *
 * The block device is the reference in both directions because it never DMAs
 * into the caller's buffer: the kernel stages through its own 512-byte
 * aligned buffer cache and copies.  The raw device DMAs straight into or out
 * of the caller's buffer, which is what the wd(4) DMA-window constraint bites
 * on.
 *
 * <offset> is the misalignment forced on the raw buffer, in bytes modulo 512.
 * 0 is the aligned case, which works whatever the driver does, and is the
 * control that says the rest of the test is wired up at all.
 */
#include <stdio.h>

#define	MAXBLK	16

char	rawbuf[(MAXBLK + 2) * 512];
char	refbuf[(MAXBLK + 2) * 512];

char *
alignbuf(p, off)
char *p;
int off;
{
	p += (512 - ((int)p & 511)) & 511;	/* up to a 512 boundary */
	return (p + off);
}

/* The pattern is a function of the seed alone, so the host can predict every
 * byte of it without reading anything the guest wrote. */
fill(p, n, seed)
register char *p;
register int n;
int seed;
{
	register int i;

	for (i = 0; i < n; i++)
		p[i] = (seed + i * 7) & 0xFF;
}

compare(a, b, n)
register char *a, *b;
register int n;
{
	register int i;

	for (i = 0; i < n; i++)
		if (a[i] != b[i]) {
			printf("rawalign: MISMATCH at byte %d: %x vs %x\n",
			    i, a[i] & 0xFF, b[i] & 0xFF);
			return (1);
		}
	printf("rawalign: MATCH %d bytes\n", n);
	return (0);
}

main(argc, argv)
char **argv;
{
	int fd, nblk, off, seed, i, n;
	char *rp, *bp;

	if (argc < 6) {
		fprintf(stderr,
		 "usage: rawalign r|w rawdev blkdev nblocks offset [seed]\n");
		return (2);
	}
	nblk = atoi(argv[4]);
	off = atoi(argv[5]);
	seed = argc > 6 ? atoi(argv[6]) : 0;
	if (nblk < 1 || nblk > MAXBLK) {
		fprintf(stderr, "rawalign: nblocks 1..%d\n", MAXBLK);
		return (2);
	}
	rp = alignbuf(rawbuf, off);
	bp = alignbuf(refbuf, off);
	n = nblk * 512;
	printf("rawalign: %s %d blocks, offset %d (raw buf mod 512 = %d)\n",
	    argv[1], nblk, off, (int)rp & 511);

	if (argv[1][0] == 'w') {
		fill(rp, n, seed);
		if ((fd = open(argv[2], 1)) < 0) {
			printf("rawalign: cannot open %s\n", argv[2]);
			return (1);
		}
		i = write(fd, rp, n);
		close(fd);
		if (i != n) {
			printf("rawalign: RAW WRITE FAILED, write()=%d of %d\n",
			    i, n);
			return (1);
		}
		if ((fd = open(argv[3], 0)) < 0) {
			printf("rawalign: cannot open %s\n", argv[3]);
			return (1);
		}
		i = read(fd, bp, n);
		close(fd);
		if (i != n) {
			printf("rawalign: BLOCK READ FAILED, read()=%d of %d\n",
			    i, n);
			return (1);
		}
		return (compare(rp, bp, n));
	}

	for (i = 0; i < n; i++)
		rp[i] = 0xA5;
	if ((fd = open(argv[2], 0)) < 0) {
		printf("rawalign: cannot open %s\n", argv[2]);
		return (1);
	}
	i = read(fd, rp, n);
	close(fd);
	if (i != n) {
		printf("rawalign: RAW READ FAILED, read()=%d of %d\n", i, n);
		return (1);
	}
	if ((fd = open(argv[3], 0)) < 0) {
		printf("rawalign: cannot open %s\n", argv[3]);
		return (1);
	}
	i = read(fd, bp, n);
	close(fd);
	if (i != n) {
		printf("rawalign: BLOCK READ FAILED, read()=%d of %d\n", i, n);
		return (1);
	}
	return (compare(rp, bp, n));
}
