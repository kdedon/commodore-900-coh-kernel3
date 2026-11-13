/* SPDX-License-Identifier: MIT */
/*
 * argmax.c -- exec takes arguments and environment up to the stack limit
 * less the 4 KB reserve, and refuses one byte more with E2BIG.
 *
 *	argmax limit-KB
 *
 * The limit is set here and the execs happen in forked children, so the
 * children also show that fork keeps it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/resource.h>

#define	SELF	"/bin/argmax"
#define	RESERVE	4096L		/* ISTSIZE */

/*
 * What exec charges for argv { SELF, "-", L bytes }, envp { }: the strings
 * with their NULs rounded to a word, five pointers, and 18 bytes of argc,
 * argv, envp and the words exec adds.
 */
#define	COST(L)	(18L + 5 * 4 + (((long)(L) + 15 + 1) & ~1L))

int	fails;
char	*big;

/* 0 if the exec worked, E2BIG if it was refused, -1 otherwise. */
int
try(n)
long n;
{
	static char *env[] = { NULL };
	char *av[4];
	int pid, st;

	big[n] = '\0';
	if ((pid = fork()) == 0) {
		av[0] = SELF;
		av[1] = "-";
		av[2] = big;
		av[3] = NULL;
		execve(SELF, av, env);
		_exit(errno == E2BIG ? 7 : 8);
	}
	big[n] = 'x';
	st = -1;
	wait(&st);
	if (st == 0)
		return 0;
	if (st == 7 << 8)
		return E2BIG;
	return -1;
}

void
ck(what, n, got, want)
char *what;
long n;
int got, want;
{
	printf("argmax: %s, %ld bytes of argument: got %d want %d  %s\n",
		what, n, got, want, got == want ? "ok" : "FAIL");
	if (got != want)
		fails++;
}

int
main(argc, argv)
int argc;
char **argv;
{
	struct rlimit rl;
	long allow, n, i;

	if (argc > 1 && argv[1][0] == '-')
		return 0;
	if (argc < 2) {
		printf("usage: argmax limit-KB\n");
		return 2;
	}
	allow = atoi(argv[1]) * 1024L;
	getrlimit(RLIMIT_STACK, &rl);
	rl.rlim_cur = allow;
	if (rl.rlim_max < allow)
		rl.rlim_max = allow;
	if (setrlimit(RLIMIT_STACK, &rl) < 0) {
		printf("argmax: FAIL -- setrlimit %ld\n", allow);
		return 1;
	}
	for (n = allow - RESERVE; COST(n) + RESERVE > allow; n--)
		;
	if ((big = malloc((unsigned)n + 2)) == NULL) {
		printf("argmax: FAIL -- no memory for %ld bytes\n", n);
		return 1;
	}
	for (i = 0; i <= n; i++)
		big[i] = 'x';
	ck("at the limit", n, try(n), 0);
	ck("one byte past", n + 1, try(n + 1), E2BIG);
	printf("argmax: %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
	return fails ? 1 : 0;
}
