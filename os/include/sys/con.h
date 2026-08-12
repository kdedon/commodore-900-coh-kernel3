/*
 * Device driver configuration.
 *
 * Reconstructed from the 0.7.3 sys/h/con.h layout plus what the 3.2
 * MI/MD/driver code requires: CON gains a final c_poll entry (see io.286
 * driver CON initializers, e.g. kb_f.c `iscon') and the DFPOL flag
 * (bio.c dpoll()); DRV keeps the loadable-driver fields
 * (d_segp/d_map/d_gate) that bio.c/exec.c use.
 */
#ifndef	 CON_H
#define	 CON_H
#include <sys/types.h>

/*
 * Device driver table.
 */
typedef struct drv {
	struct	 con *d_conp;		/* Pointer to configuration */
	struct	 seg *d_segp;		/* Segmentation containing driver */
	dmap_t	 d_map;			/* Segmentation map */
	int	 d_time;		/* Timeout is active */
	GATE	 d_gate;		/* Gate for loading */
} DRV;

/*
 * Driver interface entry.
 */
typedef struct con {
	int	c_flag;			/* Flags */
	int	c_mind;			/* Major index */
	int	(*c_open)();		/* Open */
	int	(*c_close)();		/* Close */
	int	(*c_block)();		/* Block */
	int	(*c_read)();		/* Read */
	int	(*c_write)();		/* Write */
	int	(*c_ioctl)();		/* Ioctl */
	int	(*c_power)();		/* Powerfail */
	int	(*c_timer)();		/* Timeout */
	int	(*c_load)();		/* Load */
	int	(*c_uload)();		/* Unload */
	int	(*c_poll)();		/* Poll */
} CON;

/*
 * Flags.
 */
#define	DFBLK	0000001			/* Block device */
#define	DFCHR	0000002			/* Character device */
#define	DFTAP	0000004			/* Tape */
#define	DFPOL	0000010			/* Pollable device */
#define	DFERR	0100000			/* Error */

/*
 * Functions.
 */
extern	CON	*drvmap();		/* bio.c */

/*
 * Global variables.
 */
extern	int	drvn;			/* Number of entries in table */
extern	DRV	drvl[];			/* Driver table */

#endif
