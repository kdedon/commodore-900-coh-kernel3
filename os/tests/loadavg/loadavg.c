/*
 * loadavg.c -- watch the kernel's load average rise and fall.
 *
 *	loadavg <interval> <count> [nspin from to]
 *
 * Prints `count' samples of avenrun[], one every `interval' of the GUEST's
 * own seconds, and with the optional arguments forks `nspin' processes that
 * do nothing but spin before sample `from' and kills them before sample `to'.
 * One invocation therefore covers the whole shape of the thing: an idle
 * machine, a loaded one, and an idle one again, on a single timeline printed
 * in one place.
 *
 * A load average that is merely non-zero is not a load average -- it has to
 * settle at the number of processes wanting the processor, take about its
 * nominal time to get there, and decay the same way.  A single reading shows
 * none of that, which is why nothing here prints one.
 *
 * The kernel is read the way ps(1) and top(1) read it: a namelist from the
 * kernel image, the value itself from /dev/kmem, which is mode 666.  The
 * image must be the kernel that is running; against a different link the
 * name resolves to an address that now holds something else.
 *
 * avenrun[] is fixed point scaled by FSCALE (sched.h), and it is printed both
 * raw and as a decimal.  The raw column is the one to trust: the decimal is
 * this program's rendering, and if the two disagree the fault is here.
 */
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <l.out.h>
#include <const.h>
#include <sched.h>
#include <sys/types.h>

#define MAXSPIN	8			/* Spinners this can start */

struct nlist nl[] = {
	"avenrun_",	0,	0,
	""
};

int	kfd = -1;
int	spid[MAXSPIN];
int	nspin;

/*
 * Kill the spinners.  Called on the way out however that happens: a spinner
 * left behind by an interrupted run is a process nothing will ever stop, and
 * the next measurement on the same machine would be taken against it.
 */
static
reap()
{
	register int i;

	for (i = 0; i < nspin; i++)
		if (spid[i] > 0) {
			(void)kill(spid[i], SIGKILL);
			spid[i] = 0;
		}
	nspin = 0;
}

static
bye()
{
	reap();
	exit(1);
}

/*
 * Start `n' processes that want the processor and nothing else.
 */
static
spin(n)
int n;
{
	register int i;
	int pid;

	if (n > MAXSPIN)
		n = MAXSPIN;
	for (i = 0; i < n; i++) {
		if ((pid=fork()) == 0) {
			/* The child must not inherit the parent's idea of who
			 * to kill, and must not run this program's exit path
			 * through stdio. */
			nspin = 0;
			for (;;)
				;
		}
		if (pid < 0)
			break;
		spid[i] = pid;
	}
	nspin = i;
	return (i);
}

/*
 * Format one fixed-point average as a decimal, in the caller's buffer.  The
 * fraction is two digits of the FSCALE units, not a rounding of them: the
 * value is exact in binary and any decimal is already an approximation.
 */
static char *
lfmt(av, buf)
unsigned av;
char *buf;
{
	(void)sprintf(buf, "%u.%02u", av >> FSHIFT,
		(unsigned)(((long)(av & (FSCALE-1)) * 100L) >> FSHIFT));
	return (buf);
}

int main(argc, argv)
int argc;
char **argv;
{
	unsigned av[NLOADAV];
	char b0[16], b1[16], b2[16];
	int interval, count, want, from, to, i;
	long t0;

	interval = argc > 1 ? atoi(argv[1]) : 10;
	count = argc > 2 ? atoi(argv[2]) : 6;
	want = argc > 3 ? atoi(argv[3]) : 0;
	from = argc > 4 ? atoi(argv[4]) : 0;
	to = argc > 5 ? atoi(argv[5]) : count;
	if (interval < 1 || count < 1) {
		printf("loadavg: FAIL -- interval %d count %d measures nothing\n",
			interval, count);
		return (1);
	}

	nlist("/coherent", nl);
	if (nl[0].n_type == 0) {
		printf("loadavg: FAIL -- no avenrun_ in /coherent's namelist:"
			" this kernel keeps no load average\n");
		return (1);
	}
	if ((kfd=open("/dev/kmem", O_RDONLY)) < 0) {
		printf("loadavg: FAIL -- cannot open /dev/kmem\n");
		return (1);
	}
	(void)signal(SIGINT, bye);
	(void)signal(SIGTERM, bye);

	printf("loadavg: avenrun_ at 0x%lx, FSCALE %d, %d s between samples\n",
		nl[0].n_value, FSCALE, interval);
	printf("loadavg: t   raw1  raw5 raw15    1min  5min 15min  spinners\n");
	t0 = time((time_t *)0);
	for (i = 0; i < count; i++) {
		if (i == from && want > 0)
			(void)spin(want);
		if (i == to)
			reap();
		if (lseek(kfd, (long)(unsigned)nl[0].n_value, 0) < 0
		 || read(kfd, (char *)av, sizeof (av)) != sizeof (av)) {
			printf("loadavg: FAIL -- /dev/kmem read at 0x%lx\n",
				nl[0].n_value);
			reap();
			return (1);
		}
		printf("loadavg: %3ld %5u %5u %5u   %5s %5s %5s  %d\n",
			time((time_t *)0) - t0, av[0], av[1], av[2],
			lfmt(av[0], b0), lfmt(av[1], b1), lfmt(av[2], b2),
			nspin);
		fflush(stdout);
		(void)sleep(interval);
	}
	reap();
	printf("loadavg: done\n");
	return (0);
}
