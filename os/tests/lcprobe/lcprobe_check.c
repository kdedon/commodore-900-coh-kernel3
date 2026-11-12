/* SPDX-License-Identifier: MIT */
/*
 * Host check of the kernel's ID-byte card probe.  vcard.c is linked against
 * fake I/O ports: an empty bus that reads 0xFF, one that reads 0x00, one that
 * echoes the last byte driven, a card answering 0xC0, an ID of 0xC1 with no
 * chip behind it, and the real card (ID 0xC1 and an 8568 model).  Only the
 * last may be found, and its probe must leave the chip's update address as
 * it was.  The card's INTR must be released after the probe and after a
 * block copy still running.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "vdc.h"

extern int vcprobe(), vcreport(), lcrel();
extern int vcfound, vcseen, vcrev, lcnvi;

enum { BUSFF, BUS00, ECHO, ID_C0, ID_C1_NOCHIP, CARD };
static const char *names[] = {
	"bus reads 0xFF", "bus reads 0x00", "bus echoes",
	"ID 0xC0", "ID 0xC1, no VDC", "LC card"
};

static int mode;
static unsigned last;
static char out[256];
static int nout;

unsigned
inb(unsigned port)
{
	switch (mode) {
	case BUSFF: return 0xFF;
	case BUS00: return 0x00;
	case ECHO: return last;
	case ID_C0: return port == 0x01FF ? 0xC0 : 0xFF;
	case ID_C1_NOCHIP: return port == 0x01FF ? 0xC1 : 0xFF;
	}
	if (port == 0x01FF)
		return 0xC1;
	if (port == VDC_ADDR || port == VDC_DATA)
		return vdc_in(port);
	return 0xFF;
}

void
outb(unsigned port, unsigned v)
{
	last = v & 0xFF;
	if (mode == CARD && (port == VDC_ADDR || port == VDC_DATA))
		vdc_out(port, v);
}

int
tputchar(int c)
{
	if (nout < (int)sizeof out - 1)
		out[nout++] = c;
	out[nout] = 0;
	return c;
}

int
tprintf(const char *fmt, ...)
{
	va_list ap;
	char buf[128];
	char *p;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	for (p = buf; *p; p++)
		tputchar(*p);
	return 0;
}

static const char *want[] = {
	"", "", "", "",
	"lc video: 01ff=c1, 8568 does not answer: no supported card\n",
	"lc video: 01ff=c1, 8568 rev 2\n"
};

int
main(void)
{
	int bad = 0, m, r, low;

	for (m = BUSFF; m <= CARD; m++) {
		mode = m;
		last = 0;
		vcfound = vcseen = vcrev = lcnvi = 0;
		vdc_reset();
		vdc.ua = 0x1234;
		vdc.reg[18] = 0x12;
		vdc.reg[19] = 0x34;
		r = vcprobe();
		if ((m == CARD) != (r != 0) || r != vcfound) {
			printf("  FAIL %s: vcprobe() = %d\n", names[m], r);
			bad++;
			continue;
		}
		if (m == CARD) {
			if (vcfound != 1 || vcrev != 2 || !lcnvi) {
				printf("  FAIL %s: found %d rev %d\n", names[m],
				    vcfound, vcrev);
				bad++;
				continue;
			}
			if (vdc.faults != 0) {
				printf("  FAIL %s: %u accesses while busy\n",
				    names[m], vdc.faults);
				bad++;
				continue;
			}
			if (vdc.ua != 0x1234) {
				printf("  FAIL %s: update address left at %04x\n",
				    names[m], vdc.ua);
				bad++;
				continue;
			}
			lcrel();
			low = vdc.intr;
			vdc_out(VDC_ADDR, 30);
			vdc_out(VDC_DATA, 0);
			lcrel();
			if (low || vdc.intr || vdc.busy) {
				printf("  FAIL %s: INTR left low\n", names[m]);
				bad++;
				continue;
			}
		} else if (lcnvi) {
			printf("  FAIL %s: INTR assumed\n", names[m]);
			bad++;
			continue;
		}
		if ((m >= ID_C1_NOCHIP) != (vcseen != 0)) {
			printf("  FAIL %s: ID seen = %d\n", names[m], vcseen);
			bad++;
			continue;
		}
		nout = 0;
		out[0] = 0;
		vcreport();
		if (strcmp(out, want[m]) != 0) {
			printf("  FAIL %s: report \"%s\"\n", names[m], out);
			bad++;
			continue;
		}
		printf("  ok   %-18s -> %s\n", names[m],
		    r ? "LC card" : "no card");
		if (nout)
			printf("       %s", out);
	}
	return bad != 0;
}
