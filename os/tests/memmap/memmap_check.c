/*
 * Static, host-buildable check of the memory map: which 64 KB segments of a
 * C900 hold memory a system may allocate.
 *
 * It links against the kernel's own sys/z8001/src/memmap.c, so what it checks
 * is the code that runs, not a copy of it.  Nothing here touches a byte
 * address, a ROM or an MMU: the map is segment arithmetic over the reserved
 * table, and the cases that matter are a framebuffer in the middle of the
 * ROM's range, a machine without the hi-res board, a boot block too old to
 * carry a map, and a map that cannot be true.
 *
 * The handoff block is built here rather than taken from a loader, so its
 * length is this host's sizeof and not the target's; the block is checksummed
 * and read with the same code either way, which is what these cases turn on.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/memmap.h>
#include <sys/bootinfo.h>

extern int mmbuild();
extern int mmruns();
extern int mmtake();

#define	ROMBOT	0x08			/* the ROM tests from segment 8 */
#define	ROMTOP	0x40			/* 4 MB: all the motherboard decodes */

static char	map[MM_NSEG];
static struct biram run[BI_NRAM];
static int	fails;

static
ok(what)
char *what;
{
	printf("  ok   %s\n", what);
}

static
bad(what)
char *what;
{
	printf("  BAD  %s\n", what);
	fails++;
}

/*
 * Are the runs read off the map exactly `want' (pairs of first,count, ended by
 * a zero count)?
 */
static
runsare(what, want)
char *what;
unsigned *want;
{
	char line[256];
	char one[32];
	int n, i, over;

	n = mmruns(map, run, BI_NRAM, &over);
	line[0] = '\0';
	for (i = 0; i < n; i++) {
		sprintf(one, " %x+%x", run[i].br_first, run[i].br_count);
		strcat(line, one);
	}
	for (i = 0; i < n; i++) {
		if (want[2*i+1] == 0
		 || run[i].br_first != want[2*i]
		 || run[i].br_count != want[2*i+1])
			break;
	}
	if (i == n && want[2*n+1] == 0 && over == 0) {
		printf("  ok   %s:%s\n", what, line);
		return;
	}
	printf("  BAD  %s:%s\n", what, line);
	fails++;
}

/*
 * A block a current loader would have written: the runs given, checksummed.
 */
static
block(bp, first, count, n)
register struct bootinfo *bp;
unsigned *first;
unsigned *count;
int n;
{
	register int i;

	memset((char *)bp, 0, sizeof *bp);
	bp->bi_version = BI_VERSION;
	bp->bi_len = sizeof *bp;
	bp->bi_src = BI_SRC_KBOOT;
	bp->bi_nram = n;
	for (i = 0; i < n; i++) {
		bp->bi_ram[i].br_first = first[i];
		bp->bi_ram[i].br_count = count[i];
	}
	bp->bi_sum = 0;
	bp->bi_sum = -bisum(bp);
}

/* The map a machine with 4 MB builds for itself, with and without the board:
 * segment 0x37 is the text RAM either way, 0x3E-0x3F the bitmap when the board
 * is there and 0x3F alone the ROM's own RAM when it is not. */
static unsigned hires2[]  = { 0x08, 0x2f, 0x38, 0x06, 0, 0 };
static unsigned nohires2[] = { 0x08, 0x2f, 0x38, 0x07, 0, 0 };

main()
{
	struct bootinfo bi;
	unsigned first[BI_NRAM], count[BI_NRAM];
	int r, over;

	/* A framebuffer in the middle of the range is a hole in it: the memory
	 * above the text RAM is memory, and the text RAM is not. */
	mmbuild(map, ROMBOT, ROMTOP, 1);
	runsare("hi-res fitted", hires2);

	/* Without the board 0x3E is ordinary memory and only 0x3F is not. */
	mmbuild(map, ROMBOT, ROMTOP, 0);
	runsare("no hi-res board", nohires2);

	/* A block from a loader too old to carry a map leaves the built-in
	 * answer standing, and that answer is the same one. */
	mmbuild(map, ROMBOT, ROMTOP, 1);
	block(&bi, first, count, 0);
	bi.bi_version = 4;
	bi.bi_len = sizeof bi - (2 + BI_NRAM * sizeof (struct biram));
	bi.bi_sum = 0;
	bi.bi_sum = -bisum(&bi);
	if (mmtake(&bi, map) == 0)
		ok("a block with no map in it is not a map");
	else
		bad("a block with no map in it was read as one");
	runsare("built-in map after it", hires2);

	/* A loader reserving a segment of its own splits a run, and that map is
	 * taken: it is a subset of what this machine would have allocated. */
	mmbuild(map, ROMBOT, ROMTOP, 1);
	first[0] = 0x08; count[0] = 0x20;
	first[1] = 0x29; count[1] = 0x0e;
	first[2] = 0x38; count[2] = 0x06;
	block(&bi, first, count, 3);
	if (mmtake(&bi, map) == 1)
		ok("a loader's map that reserves more is taken");
	else
		bad("a loader's map that reserves more was refused");
	{
		static unsigned want[] = { 0x08, 0x20, 0x29, 0x0e, 0x38, 0x06, 0, 0 };
		runsare("the loader's runs", want);
	}

	/* Every list that cannot be true is refused, and refusing it leaves the
	 * built-in map to be read off instead. */
	mmbuild(map, ROMBOT, ROMTOP, 1);
	block(&bi, first, count, 0);
	if (mmtake(&bi, map) == -1)
		ok("an empty run list is refused");
	else
		bad("an empty run list was taken");

	first[0] = 0x08; count[0] = 0x2f;
	block(&bi, first, count, 1);
	bi.bi_nram = BI_NRAM + 1;
	bi.bi_sum = 0;
	bi.bi_sum = -bisum(&bi);
	if (mmtake(&bi, map) == -1)
		ok("more runs than the block holds is refused");
	else
		bad("more runs than the block holds was taken");

	first[0] = 0x00; count[0] = 0x40;	/* the ROM, and the text RAM */
	block(&bi, first, count, 1);
	if (mmtake(&bi, map) == -1)
		ok("a run over reserved segments is refused");
	else
		bad("a run over reserved segments was taken");

	first[0] = 0x30; count[0] = 0x10;	/* spans the text RAM */
	block(&bi, first, count, 1);
	if (mmtake(&bi, map) == -1)
		ok("a run across the text RAM is refused");
	else
		bad("a run across the text RAM was taken");

	first[0] = 0x38; count[0] = 0x06;	/* descending */
	first[1] = 0x08; count[1] = 0x2f;
	block(&bi, first, count, 2);
	if (mmtake(&bi, map) == -1)
		ok("runs out of order are refused");
	else
		bad("runs out of order were taken");

	first[0] = 0xf8; count[0] = 0x10;	/* runs off the end of the map */
	block(&bi, first, count, 1);
	if (mmtake(&bi, map) == -1)
		ok("a run past the end of memory is refused");
	else
		bad("a run past the end of memory was taken");

	first[0] = 0x08; count[0] = 0x2f;
	block(&bi, first, count, 1);
	bi.bi_sum ^= 0x1234;
	if (mmtake(&bi, map) == -1)
		ok("a block that does not check out is refused");
	else
		bad("a block that does not check out was taken");

	runsare("built-in map after every refusal", hires2);

	/* Memory the ROM reports beyond what the map can hold is not wrapped
	 * onto memory that is in use: mmbuild stops at MM_NSEG. */
	mmbuild(map, ROMBOT, 0x180, 0);
	r = mmruns(map, run, BI_NRAM, &over);
	if (r > 0 && run[r-1].br_first + run[r-1].br_count == MM_NSEG)
		ok("a range past 16M stops at the last segment the MMU can reach");
	else
		bad("a range past 16M did not stop at the end of the map");

	if (fails != 0) {
		printf("memmap: %d case(s) BAD\n", fails);
		exit(1);
	}
	printf("memmap: the map is the ROM's range less the reserved table\n");
	exit(0);
}
