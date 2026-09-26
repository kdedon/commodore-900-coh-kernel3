/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Which 64 KB segments of memory may be allocated.
 *
 * The boot ROM sizes memory by marching whole segments upward, writing a
 * pattern and reading it back, and stops at the first segment that does not
 * answer.  FRAMEBUFFER MEMORY ANSWERS THAT TEST: the text RAM mirrors its 4 KB
 * through the whole of segment 0x37 and an HR bitmap answers at 0x3E and 0x3F,
 * so all of it lands inside the range the ROM reports.  Taking that range whole
 * hands the screen out as memory; refusing everything above it throws away the
 * memory that is really there.
 *
 * So the range is a map, not an extent, and what comes out of it is a LIST of
 * runs.  A loader can send the list, having subtracted what its own
 * configuration reserved as well; with no loader the same table is subtracted
 * here, so the two answers agree.
 *
 * Segments throughout -- bytes are the caller's business.
 */
#include <sys/memmap.h>
#include <sys/bootinfo.h>

/*
 * Clear the segments one reserved range covers, unless it is for the other
 * kind of machine.
 */
static
mmcut(map, rp, hires)
char *map;
register struct memres *rp;
int hires;
{
	register int s;

	if (rp->mr_when == (hires ? MW_NOHR : MW_HR))
		return;
	for (s = rp->mr_first; s <= (int)rp->mr_last && s < MM_NSEG; s++)
		map[s] = 0;
}

/*
 * The map this machine has by itself: segments [bot, top) of the ROM's range,
 * less the reserved table.  `hires' says an HR board is fitted, which is what
 * decides whether 0x3E is bitmap or ordinary memory.
 *
 * A byte per segment, and the runs read off it afterwards: the table's ranges
 * overlap -- 0x3F is in it twice -- and differencing intervals would have to
 * get that right.
 */
mmbuild(map, bot, top, hires)
char *map;
unsigned bot;
unsigned top;
int hires;
{
	register unsigned s;
	register int i;

	if (top > MM_NSEG)
		top = MM_NSEG;
	for (s = 0; s < MM_NSEG; s++)
		map[s] = s >= bot && s < top;
	for (i = 0; i < MM_NRES; i++)
		mmcut(map, &mmres[i], hires);
}

/*
 * Is `n' runs at `rp' a list this map allows?  Ascending, each one non-empty,
 * and every segment of it memory the map already holds -- a sender may reserve
 * MORE than the table does, so its list is a subset of the map and never more
 * than it.  That is what makes a list worth taking without probing anything:
 * it can only lose memory this machine would have allocated, never invent any.
 */
mmsubset(map, rp, n)
char *map;
register struct biram *rp;
int n;
{
	register unsigned s;
	unsigned end;
	unsigned last;
	register int i;

	if (n <= 0 || n > BI_NRAM)
		return (0);
	last = 0;
	for (i = 0; i < n; i++, rp++) {
		end = (unsigned)rp->br_first + rp->br_count;
		if (rp->br_count == 0 || rp->br_first < last || end > MM_NSEG)
			return (0);
		for (s = rp->br_first; s < end; s++)
			if (map[s] == 0)
				return (0);
		last = end;
	}
	return (1);
}

/*
 * Cut the map down to the `n' runs at `rp', which mmsubset() has passed.
 */
mmkeep(map, rp, n)
char *map;
register struct biram *rp;
int n;
{
	register unsigned s;
	register int i;

	for (s = 0; s < MM_NSEG; s++)
		map[s] = 0;
	for (i = 0; i < n; i++, rp++)
		for (s = rp->br_first; s < (unsigned)rp->br_first + rp->br_count; s++)
			map[s] = 1;
}

/*
 * Read the runs off the map into `rp', at most `nmax' of them, and return how
 * many.  Out of slots the LOW runs are the ones kept -- a machine is better
 * off with less memory than with memory that is not there, and the lowest run
 * is the one it is running in -- and *over says memory was dropped.
 */
mmruns(map, rp, nmax, over)
char *map;
register struct biram *rp;
int nmax;
int *over;
{
	register unsigned s;
	unsigned first;
	register int n;

	*over = 0;
	n = 0;
	for (s = 0; s < MM_NSEG; ) {
		if (map[s] == 0) {
			s++;
			continue;
		}
		first = s;
		while (s < MM_NSEG && map[s] != 0)
			s++;
		if (n >= nmax) {
			*over = 1;
			break;
		}
		rp->br_first = (unsigned short)first;
		rp->br_count = (unsigned short)(s - first);
		rp++;
		n++;
	}
	return (n);
}

/*
 * Take the map a loader handed over, if it handed one over: 1 the map is now
 * `map', 0 no map arrived, -1 one arrived and was refused.
 *
 * A loader's map is preferred over the map built here because it has also
 * subtracted what the loader's own configuration reserved, which this side
 * cannot know.  Everything the block says is checked first: this is the
 * earliest reader of it, and one that trusted it unchecked would be allocating
 * memory on the strength of nothing.  A block from an older loader carries no
 * map at all, and is no kind of fault -- the answer built here is the same one,
 * less whatever that configuration said.
 */
mmtake(bp, map)
register struct bootinfo *bp;
char *map;
{
	if (bp->bi_src != BI_SRC_KBOOT)
		return (0);			/* nothing was handed over */
	if (bp->bi_version != BI_VERSION
	 || bp->bi_len != sizeof (struct bootinfo))
		return (0);			/* older block: no map in it */
	if (bisum(bp) != 0 || mmsubset(map, bp->bi_ram, (int)bp->bi_nram) == 0)
		return (-1);
	mmkeep(map, bp->bi_ram, (int)bp->bi_nram);
	return (1);
}
