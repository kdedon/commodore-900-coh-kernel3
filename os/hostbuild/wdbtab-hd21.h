/*
 * wdbtab-hd21.h -- the kernel's built-in wd(4) partition table for the hd21
 * media layout (real hardware's layout, and the one every shipping dist
 * uses).  Checked in rather than generated: the generator (dist.py wdbtab)
 * and the media descriptor it read (os/dist/media/hd21.media) belong to the
 * dist repository, and this repository must not depend on it -- see
 * RELEASE-STYLES.md SS B.6, "the one genuine cycle, and how it breaks".
 *
 * link-kernel.sh copies this verbatim to build/gen/wdbtab.h when
 * KMEDIA=hd21 (the default).  It is byte-identical to what
 * `dist.py wdbtab media/hd21.media' produced in commodore-900-coherent,
 * the repository this one was split from.
 *
 * This is only the FALLBACK table sys/z8001/drv/wd.c runs on when no loader
 * hands the kernel one -- a Coherent-only disk booted with no kboot in front
 * of it.  Every image dist actually ships is patched by dist.py's own
 * wdbtab= step regardless of what is compiled in here, so this table being
 * fixed to one media does not fix what any image boots from.
 *
 * Included by exactly one file, sys/z8001/drv/wd.c.
 */
#define	WDBTAB_MEDIA	"hd21"

struct bootinfo	bootinfo = {
	BI_MAGIC, BI_VERSION, sizeof (struct bootinfo),
	BI_NPART, 0, BI_SRC_KERNEL,
	3, 2904L, 7000L,			/* swap: /dev/hd3 blocks 2904..7000 */
	{
		{        0L,      136L },	/* hd0  boot */
		{    10336L,    13472L },	/* hd1  usr */
		{    23808L,     7200L },	/* hd2  man */
		{    31008L,     7000L },	/* hd3  tmp */
		{      136L,    10200L },	/* hd4  root */
		{        0L,        0L },	/* hd5  unused */
		{        0L,        0L },	/* hd6  unused */
		{        0L,        0L },	/* hd7  unused */
		{        0L,        0L },	/* hd8  unused */
		{        0L,        0L },	/* hd9  unused */
		{        0L,        0L },	/* hd10 unused */
		{        0L,        0L },	/* hd11 unused */
		{        0L,        0L },	/* hd12 unused */
		{        0L,        0L },	/* hd13 unused */
		{        0L,        0L },	/* hd14 unused */
		{        0L,    41616L },	/* hd15 whole disk */
	}
};
