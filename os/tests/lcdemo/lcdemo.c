/* SPDX-License-Identifier: MIT */
/*
 * lcdemo [mono|colour|i480|all] [hold] -- the LC card's graphics minor by
 * eye.  mono draws lines and patterns into the RAM shadow and flushes it,
 * timing the flush; colour draws bars of each attribute colour; i480 does
 * the lines at 640x480 interlaced; all runs the three and goes back to
 * text.  hold keeps the last picture up until killed.
 */
#include <stdio.h>
#include <sys/lcio.h>
#include <sys/times.h>

int	fd;
struct lcinfo li;
unsigned char *sh;

die(s)
char *s;
{
	perror(s);
	exit(1);
}

mode(m)
{
	li.lm_mode = m;
	if (ioctl(fd, LCIOCMODE, &li) < 0)
		die("LCIOCMODE");
	if (ioctl(fd, LCIOCSHADOW, &li) < 0)
		die("LCIOCSHADOW");
	sh = (unsigned char *)li.lm_shbase;
	printf("mode %d: %dx%d, %u bytes of shadow\n", li.lm_mode,
	    li.lm_width, li.lm_height, li.lm_shadow);
}

plot(x, y)
register int x, y;
{
	if (x >= 0 && y >= 0 && x < li.lm_width && y < li.lm_height)
		sh[y * 80 + (x >> 3)] |= 0x80 >> (x & 7);
}

line(x0, y0, x1, y1)
int x0, y0, x1, y1;
{
	register int dx, dy, sx, sy, e, e2;

	dx = x1 > x0 ? x1 - x0 : x0 - x1;
	dy = y1 > y0 ? y0 - y1 : y1 - y0;
	sx = x0 < x1 ? 1 : -1;
	sy = y0 < y1 ? 1 : -1;
	e = dx + dy;
	for (;;) {
		plot(x0, y0);
		if (x0 == x1 && y0 == y1)
			break;
		e2 = 2 * e;
		if (e2 >= dy) {
			e += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			e += dx;
			y0 += sy;
		}
	}
}

flushall()
{
	struct lcrect r;
	struct lcflush f;

	r.lr_x = 0;
	r.lr_y = 0;
	r.lr_w = 80;
	r.lr_h = li.lm_height;
	f.lu_n = 1;
	f.lu_r = &r;
	if (ioctl(fd, LCIOCFLUSH, &f) < 0)
		die("LCIOCFLUSH");
}

/* A frame, a fan of lines, and checker and stripe patterns. */
lines()
{
	register int i, x, y, w, h;
	struct tbuffer t0, t1;
	long n;

	w = li.lm_width;
	h = li.lm_height;
	line(0, 0, w - 1, 0);
	line(w - 1, 0, w - 1, h - 1);
	line(w - 1, h - 1, 0, h - 1);
	line(0, h - 1, 0, 0);
	for (i = 0; i <= 16; i++) {
		line(w / 2, h - 1, i * (w - 1) / 16, 0);
		line(0, h / 2, w / 4, i * (h - 1) / 16);
	}
	for (y = h / 4; y < h / 2; y++)
		for (x = 60; x < 72; x++)
			sh[y * 80 + x] = (y & 1) ? 0xAA : 0x55;
	for (y = h / 2; y < 3 * h / 4; y++)
		for (x = 60; x < 72; x++)
			sh[y * 80 + x] = (y & 4) ? 0xFF : 0x00;
	times(&t0);
	for (i = 0; i < 10; i++)
		flushall();
	times(&t1);
	n = (long)li.lm_height * 80 * 10;
	printf("flush: %ld bytes in %ld ticks\n", n,
	    t1.tb_stime - t0.tb_stime + t1.tb_utime - t0.tb_utime);
}

/* Sixteen bars, one per attribute colour, in a ruled bitmap. */
bars()
{
	register int c, y;
	struct lcblk b;
	unsigned char row[80];

	for (y = 0; y < li.lm_height; y++)
		for (c = 0; c < 80; c++)
			sh[y * 80 + c] = (y % 8) < 6 ? 0xFE : 0x00;
	flushall();
	for (c = 0; c < 80; c++)
		row[c] = c / 5;
	for (y = 0; y < li.lm_height / 8; y++) {
		lseek(fd, (long)(li.lm_attr + y * 80), 0);
		if (write(fd, row, 80) != 80)
			die("write attributes");
	}
	b.lb_dst = li.lm_attr;
	b.lb_val = 0x0F;
	b.lb_count = 80;
	if (ioctl(fd, LCIOCFILL, &b) < 0)
		die("LCIOCFILL");
}

main(argc, argv)
char **argv;
{
	char *what;
	int all;

	what = argc > 1 ? argv[1] : "all";
	all = strcmp(what, "all") == 0;
	if ((fd = open("/dev/lcvdc", 2)) < 0)
		die("/dev/lcvdc");
	if (ioctl(fd, LCIOCINFO, &li) < 0)
		die("LCIOCINFO");
	printf("lc: %u KB, chip version %u\n", li.lm_ram, li.lm_chip);
	if (all || strcmp(what, "mono") == 0) {
		mode(LCM_MONO);
		lines();
	}
	if (all || strcmp(what, "colour") == 0) {
		mode(LCM_COLOUR);
		bars();
	}
	if (all || strcmp(what, "i480") == 0) {
		mode(LCM_I480);
		lines();
	}
	if (argc > 2 && strcmp(argv[2], "hold") == 0)
		for (;;)
			pause();
	if (ioctl(fd, LCIOCSTART, 0) < 0)
		die("LCIOCSTART");
	printf("back to text\n");
	exit(0);
}
