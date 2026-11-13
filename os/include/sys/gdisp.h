/*
 * The display descriptor.  The console driver that owns the screen writes it
 * into the window system's shared segment (GDS) at GD_OFF, and every graphics
 * program reads its geometry from there instead of assuming 1024x800.  No
 * GD_MAGIC there means a kernel older than this header: 1024x800, 1 bit,
 * mapped.
 *
 * Pixel (x, y) is byte y*gd_stride + x*gd_depth/8 from the start of segment
 * gd_seg, running on into the next segment at each 64 KB.  The most
 * significant bit of a byte is the leftmost pixel.
 */
#ifndef	GDISP_H
#define	GDISP_H	GDISP_H

#define	GD_OFF		0x6F00		/* in GDS; 0x6F00..0x6FFF is reserved */
#define	GD_MAGIC	0x4744		/* 'GD' */
#define	GD_VERSION	1

struct gdisp {
	unsigned short	gd_magic;	/* GD_MAGIC once written */
	unsigned short	gd_version;	/* GD_VERSION */
	unsigned short	gd_width;	/* visible pixels per raster */
	unsigned short	gd_height;	/* visible rasters */
	unsigned short	gd_depth;	/* bits per pixel */
	unsigned short	gd_stride;	/* bytes per raster */
	unsigned short	gd_flags;	/* GDF_* */
	unsigned short	gd_card;	/* GDC_*; for GDC_HR, 404H in the high byte */
	unsigned short	gd_seg;		/* first segment of the pixels */
	unsigned short	gd_nseg;	/* 64 KB segments they span */
	unsigned short	gd_split;	/* raster holding the first 64 KB boundary */
	unsigned short	gd_splitx;	/* byte of that raster it falls at, 0 = exact */
	unsigned short	gd_xasp;	/* pixel aspect, width : height */
	unsigned short	gd_yasp;
	unsigned short	gd_vram;	/* KB of display memory on the card */
	unsigned short	gd_gdsram;	/* 1: GDS is system RAM, 0: spare VRAM */
};

#define	GDF_MAPPED	0x0001		/* gd_seg is the screen itself */
#define	GDF_SHADOW	0x0002		/* gd_seg is RAM copied out to a chip */

#define	GDC_HR		1		/* hi-res bitmap board */
#define	GDC_LC		2		/* ID-probed 8563/8568 card */

#ifdef	KERNEL
extern	int	hrrows;			/* rasters of the hi-res board, 800 or 1024 */
extern	int	gdsram;			/* GDS was given system RAM at boot */
#endif

#endif
