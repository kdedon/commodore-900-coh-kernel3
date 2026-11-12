/* SPDX-License-Identifier: MIT */
/*
 * A host model of the 8568 as the console uses it: register index and
 * status (version 2), R37, the update and block-source addresses, R31 access that advances
 * the update address, and block fill and copy started by R30 (0 moves 256).
 * Until R28 bit 4 is set addresses alias as on 16 KB parts.  The chip stays
 * busy for a while after each VRAM or update address access, and any data
 * access made while it is busy is counted as a fault.  INTR is pulled low
 * when the chip turns ready and released by a status read.
 */
#include <stdio.h>
#include <string.h>
#include "vdc.h"

struct vdc vdc;

static unsigned
ramaddr(unsigned a)
{
	a &= 0xFFFF;
	if (vdc.reg[28] & 0x10)
		return a;
	return a & 0x3FFF;
}

static void
note(int r)
{
	if (vdc.nlog < VDC_LOG)
		vdc.log[vdc.nlog] = r;
	vdc.nlog++;
}

void
vdc_reset(void)
{
	memset(&vdc, 0, sizeof vdc);
	vdc.intr = 1;
}

static int
busy(void)
{
	if (vdc.busy) {
		vdc.faults++;
		return 1;
	}
	return 0;
}

unsigned
vdc_in(unsigned port)
{
	unsigned v;

	if (port == VDC_ADDR) {
		vdc.npoll++;
		if (vdc.busy) {
			if (--vdc.busy == 0)
				vdc.intr = 1;
			else
				vdc.intr = 0;
			return 0x02;
		}
		vdc.intr = 0;
		return 0x82;
	}
	busy();
	switch (vdc.sel) {
	case 18: vdc.busy = 1; return vdc.ua >> 8;
	case 19: vdc.busy = 1; return vdc.ua & 0xFF;
	case 37: return vdc.reg[37] | 0x3F;
	case 31:
		v = vdc.ram[ramaddr(vdc.ua)];
		vdc.ua = (vdc.ua + 1) & 0xFFFF;
		vdc.busy = 1;
		return v;
	case 32: return vdc.ba >> 8;
	case 33: return vdc.ba & 0xFF;
	}
	return vdc.reg[vdc.sel];
}

void
vdc_out(unsigned port, unsigned v)
{
	unsigned n;

	v &= 0xFF;
	if (port == VDC_ADDR) {
		vdc.nsel++;
		vdc.sel = v & 0x3F;
		return;
	}
	busy();
	note(vdc.sel);
	if (vdc.sel < 64)
		vdc.reg[vdc.sel] = v;
	switch (vdc.sel) {
	case 18: vdc.ua = (vdc.ua & 0x00FF) | v << 8; vdc.busy = 1; break;
	case 19: vdc.ua = (vdc.ua & 0xFF00) | v; vdc.busy = 1; break;
	case 32: vdc.ba = (vdc.ba & 0x00FF) | v << 8; break;
	case 33: vdc.ba = (vdc.ba & 0xFF00) | v; break;
	case 31:
		if (!(vdc.reg[28] & 0x10))
			vdc.early++;
		vdc.wda = v;
		vdc.ram[ramaddr(vdc.ua)] = v;
		vdc.ua = (vdc.ua + 1) & 0xFFFF;
		vdc.busy = 1;
		break;
	case 30:
		if (!(vdc.reg[28] & 0x10))
			vdc.early++;
		n = v ? v : 256;
		vdc.busy = n / 8 + 1;
		while (n--) {
			if (vdc.reg[24] & 0x80) {
				vdc.ram[ramaddr(vdc.ua)] = vdc.ram[ramaddr(vdc.ba)];
				vdc.ba = (vdc.ba + 1) & 0xFFFF;
			} else
				vdc.ram[ramaddr(vdc.ua)] = vdc.wda;
			vdc.ua = (vdc.ua + 1) & 0xFFFF;
		}
		break;
	}
}
