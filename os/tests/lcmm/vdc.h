/* SPDX-License-Identifier: MIT */
#define	VDC_ADDR	0x0181
#define	VDC_DATA	0x0183
#define	VDC_LOG		200000

struct vdc {
	unsigned char	ram[65536];
	unsigned char	reg[64];
	unsigned	sel;
	unsigned	ua;		/* update address */
	unsigned	ba;		/* block copy source */
	unsigned	wda;		/* last byte written to R31 */
	unsigned	busy;		/* status reads until ready */
	unsigned	intr;		/* INTR pin pulled low */
	unsigned	faults;		/* data accesses while busy */
	unsigned	early;		/* VRAM writes before R28 bit 4 */
	unsigned	nlog;		/* data writes, by register */
	unsigned long	nsel;		/* register index writes */
	unsigned long	npoll;		/* status reads */
	unsigned char	log[VDC_LOG];
};

extern struct vdc vdc;

void		vdc_reset(void);
unsigned	vdc_in(unsigned port);
void		vdc_out(unsigned port, unsigned v);
