/*
 * mmugap.c -- are unmapped user segments CPU-inhibited?
 *
 * Each probe forks a child that reads offset 0 of a segment:
 *
 *	seg 1, seg 2	gap between stack and USEG: must fault
 *	seg 30		past mp_nseg: must fault
 *	own data	must not fault
 */
#include <stdio.h>
#include <signal.h>

char	mine[4] = { 0x5A, 0, 0, 0 };
int	fails;

/* 1 if the read survived, 0 if the child died of SIGSEGV, -1 otherwise. */
probe(p)
char *p;
{
	int pid, w, status;

	fflush(stdout);
	if ((pid = fork()) < 0) {
		printf("mmugap: fork failed\n");
		return (-1);
	}
	if (pid == 0) {
		signal(SIGSEGV, SIG_DFL);
		printf("  child read 0x%02x at %08lx\n", *p & 0xFF, (long)p);
		fflush(stdout);
		exit(0);
	}
	while ((w = wait(&status)) != pid && w != -1)
		;
	if (w == -1)
		return (-1);
	if ((status & 0x7F) == SIGSEGV)
		return (0);
	if ((status & 0xFF) == 0)
		return (1);
	printf("  child status 0x%04x\n", status);
	return (-1);
}

check(what, p, wantfault)
char *what, *p;
{
	int r;

	r = probe(p);
	if (r == (wantfault ? 0 : 1)) {
		printf("mmugap: ok   %s: %s\n", what,
			wantfault ? "SIGSEGV" : "read");
		return;
	}
	printf("mmugap: BAD  %s: %s\n", what,
		r == 1 ? "read succeeded" : r == 0 ? "SIGSEGV" : "odd exit");
	fails++;
}

main()
{
	check("own data", mine, 0);
	check("seg 1 (gap)", (char *)0x01000000L, 1);
	check("seg 2 (gap)", (char *)0x02000000L, 1);
	check("seg 30 (past nseg)", (char *)0x1E000000L, 1);
	printf("mmugap: %s\n", fails ? "FAIL" : "PASS");
	exit(fails != 0);
}
