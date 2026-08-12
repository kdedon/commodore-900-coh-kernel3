

#define	YSCROLL		25		/* scan line displ. when scrolling */

/* texttab flags
 */
#define	ERASED		01		/* this line is empty */
#define	SCROLLABLE	02		/* resp. scantab entries are valid */

#define	MAXLINE	(YMAX / YSCROLL)	/* max # text lines per screen */
#define	MAXCOL	(XMAX / 12)		/* max # char columns per text line */


/*
 * The changed span of one scan line, in WORDS: sc_off words from the left
 * edge are the same as the line YSCROLL above, and sc_nword words after that
 * differ.  sc_nword == 0 means the whole line matches and scroll() copies
 * nothing.
 *
 * BOTH FIELDS ARE BYTES, AND THE SCROLLER DEPENDS ON IT.  scroll.s walks
 * scantab with a stride of 2 (`$scantab_+YSCROLL*2', `$scantab_+YMAX*2',
 * `inc r3,$2') and takes one entry as a single word -- high byte sc_off, low
 * byte sc_nword -- to test, index and count with (`orb rl7,rl6' is the
 * sc_nword == 0 test, `ldb rl6,rh6' is sc_off).  Widening a field changes the
 * layout AND the stride, so it is a change to scroll.s first and to this
 * struct second; the C readers in hrterm2.c, hrterm3.c and scrollu.c would go
 * on compiling either way and the screen would tear silently.
 *
 * A byte holds a word count only while a raster is under 256 words, i.e.
 * XMAX * depth <= 4080 bits.  That is true of every 1-bit mode this driver
 * has (WPERSL = 64) and of 640x400x4 (160), and false of 1024 x 4.  The
 * declaration below is the trip wire: any geometry or depth change that
 * breaks either assumption fails the build here rather than overflowing a
 * count at run time.  It declares an array and defines nothing.
 */
struct scanline {
	uchar	sc_off,
		sc_nword;
};

extern char sc_fits[(WPERSL <= 255 && sizeof(struct scanline) == 2) ? 1 : -1];


extern char		texttab[];
extern struct scanline	scantab[];
