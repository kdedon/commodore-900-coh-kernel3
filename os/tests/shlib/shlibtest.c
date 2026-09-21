/*
 * shlibtest.c -- a client of the toy shared library.
 *
 *	(none)	plain calls, and where the library is mapped
 *	p <n>	bump the private counter n times, sleep, report again;
 *		two at once must keep their own counts
 *	f	bump, fork, bump in the child; the child gets a copy
 *
 * Lines carry the pid, since two run at once.
 */
#include <stdio.h>

extern int	toy_add();
extern int	toy_bump();
extern char	*toy_name();
extern char	*toy_where();
extern char	*toy_code();

main(argc, argv)
int argc;
char *argv[];
{
	char *s;
	int i, n, st;

	s = toy_name();
	printf("shlib[%d]: name \"%s\" at %08lx\n", getpid(), s, (long)s);
	printf("shlib[%d]: private data at %08lx\n", getpid(),
		(long)toy_where());
	printf("shlib[%d]: shared code at %08lx\n", getpid(),
		(long)toy_code());
	printf("shlib[%d]: add 2+3=%d 40+2=%d\n", getpid(),
		toy_add(2, 3), toy_add(40, 2));
	if (argc > 1 && argv[1][0] == 'p') {
		n = argc > 2 ? atoi(argv[2]) : 1;
		for (i = 0; i < n; i++)
			toy_bump(1);
		printf("shlib[%d]: counter %d after %d bumps\n", getpid(),
			toy_bump(0), n);
		fflush(stdout);
		sleep(4);
		printf("shlib[%d]: counter %d still\n", getpid(), toy_bump(0));
	} else if (argc > 1 && argv[1][0] == 's') {
		/* Both halves must survive swapping and compaction. */
		n = argc > 2 ? atoi(argv[2]) : 10;
		for (i = 0; i < 7; i++)
			toy_bump(1);
		s = toy_name();
		printf("shlib[%d]: before pressure counter %d code %08lx\n",
			getpid(), toy_bump(0), (long)toy_code());
		fflush(stdout);
		sleep(n);
		printf("shlib[%d]: after pressure counter %d code %08lx name \"%s\"\n",
			getpid(), toy_bump(0), (long)toy_code(), toy_name());
		if (s != toy_name())
			printf("shlib[%d]: the string MOVED\n", getpid());
	} else if (argc > 1 && argv[1][0] == 'f') {
		toy_bump(10);
		printf("shlib[%d]: parent counter %d before fork\n", getpid(),
			toy_bump(0));
		fflush(stdout);
		if (fork() == 0) {
			toy_bump(5);
			printf("shlib[%d]: child counter %d\n", getpid(),
				toy_bump(0));
			fflush(stdout);
			exit(0);
		}
		wait(&st);
		printf("shlib[%d]: parent counter %d after fork\n", getpid(),
			toy_bump(0));
	} else {
		for (i = 0; i < 3; i++)
			printf("shlib[%d]: bump %d\n", getpid(), toy_bump(1));
	}
	fflush(stdout);
	return (0);
}
