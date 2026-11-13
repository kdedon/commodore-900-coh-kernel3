/* SPDX-License-Identifier: MIT */
/* The few kernel definitions the graphics minor uses, for the host check. */
#include <stddef.h>
#define	dev_t	unsigned short
typedef long fsize_t;
typedef long paddr_t;
typedef struct io {
	unsigned io_ioc;
	long	 io_seek;
	char	 *io_base;
} IO;
#define	BMS	0x3A
#define	BMPHYS	0x00FF0000L
#define	bruc(n)	((n+1023)&~1023)
unsigned inb(unsigned);
void outb(unsigned, unsigned);
extern unsigned char shadowbuf[];
extern struct gdisp *gdtest;
