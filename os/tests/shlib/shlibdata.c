/*
 * shlibdata.c -- do client and library see the same data objects?
 *
 *	(none)	read initializers; compare both sides' addresses
 *	p <n>	client writes, library reads; library writes, client
 *		reads; sleep and reread (run two at once)
 *	f	write, fork, write in the child, report both
 *	s <n>	write, sleep under memory pressure, read back
 *	c	read, then dump core
 *
 * Lines carry the pid, since two run at once.
 */
#include <stdio.h>

struct toybox {
	int	tb_a;
	int	tb_b;
};

extern struct toybox	toyd_box;
extern char		toyd_arr[];
extern char		toyd_tab[];
extern int		toyd_sum();
extern int		toyd_set();
extern int		toyd_rsum();
extern char		*toyd_where();
extern char		*toyd_rwhere();

main(argc, argv)
int argc;
char *argv[];
{
	int i, n, st;

	printf("shlibdata[%d]: box at %08lx, library says %08lx\n", getpid(),
		(long)&toyd_box, (long)toyd_where());
	if ((char *)&toyd_box != toyd_where())
		printf("shlibdata[%d]: TWO OBJECTS\n", getpid());
	printf("shlibdata[%d]: init %d %d\n", getpid(),
		toyd_box.tb_a, toyd_box.tb_b);
	/* A readonly export sits one segment below the private objects. */
	n = 0;
	for (i = 0; i < 8; i++)
		n += toyd_tab[i];
	printf("shlibdata[%d]: tab at %08lx, library says %08lx\n", getpid(),
		(long)toyd_tab, (long)toyd_rwhere());
	if (toyd_tab != toyd_rwhere())
		printf("shlibdata[%d]: TWO TABLES\n", getpid());
	printf("shlibdata[%d]: tab %d %d %d sums %d %d\n", getpid(),
		toyd_tab[0], toyd_tab[3], toyd_tab[7], n, toyd_rsum());
	if (((long)toyd_tab >> 24) + 1 != ((long)&toyd_box >> 24))
		printf("shlibdata[%d]: ONE SEGMENT\n", getpid());
	if (argc > 1 && argv[1][0] == 'p') {
		n = argc > 2 ? atoi(argv[2]) : 1;
		toyd_box.tb_a = n;		/* the client writes */
		toyd_arr[0] = n + 1;
		printf("shlibdata[%d]: library sums %d\n", getpid(),
			toyd_sum());		/* the library reads */
		toyd_set(n * 2);		/* the library writes */
		printf("shlibdata[%d]: client reads %d %d\n", getpid(),
			toyd_box.tb_b, toyd_arr[1]);	/* the client reads */
		fflush(stdout);
		sleep(4);
		printf("shlibdata[%d]: still %d %d %d\n", getpid(),
			toyd_box.tb_a, toyd_box.tb_b, toyd_arr[0]);
	} else if (argc > 1 && argv[1][0] == 'f') {
		/* The child's writes must reach only its own copy. */
		toyd_box.tb_a = 30;
		toyd_arr[0] = 1;
		printf("shlibdata[%d]: parent sums %d before fork\n",
			getpid(), toyd_sum());
		fflush(stdout);
		if (fork() == 0) {
			printf("shlibdata[%d]: child sums %d at %08lx tab %08lx\n",
				getpid(), toyd_sum(), (long)&toyd_box,
				(long)toyd_tab);
			toyd_box.tb_a = 70;
			printf("shlibdata[%d]: child sums %d after its write\n",
				getpid(), toyd_sum());
			fflush(stdout);
			exit(0);
		}
		wait(&st);
		printf("shlibdata[%d]: parent sums %d after fork\n", getpid(),
			toyd_sum());
	} else if (argc > 1 && argv[1][0] == 's') {
		/* Slots must survive swapping and compaction. */
		n = argc > 2 ? atoi(argv[2]) : 10;
		toyd_box.tb_a = 50;
		toyd_arr[0] = 4;
		printf("shlibdata[%d]: before pressure sums %d tab %d at %08lx\n",
			getpid(), toyd_sum(), toyd_rsum(), (long)toyd_tab);
		fflush(stdout);
		sleep(n);
		printf("shlibdata[%d]: after pressure sums %d tab %d at %08lx\n",
			getpid(), toyd_sum(), toyd_rsum(), (long)toyd_tab);
	} else if (argc > 1 && argv[1][0] == 'c') {
		/* The core holds the client's slots, not the library. */
		toyd_box.tb_a = 60;
		printf("shlibdata[%d]: dumping, sums %d\n", getpid(),
			toyd_sum());
		fflush(stdout);
		kill(getpid(), 3);		/* SIGQUIT: dumps */
		printf("shlibdata[%d]: STILL ALIVE\n", getpid());
	}
	fflush(stdout);
	return (0);
}
