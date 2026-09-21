/*
 * slmid -- calls a broad slice of libc.1.  Functions only: no stdout, no
 * ctype macros.
 */

static int
cmp(a, b)
char *a, *b;
{
	return (*(int *)a - *(int *)b);
}

main(argc, argv)
int argc;
char **argv;
{
	extern char	*malloc(), *strstr(), *strerror();
	char	b[128], *p, *q;
	int	v[32], i;
	long	n;

	for (i = 0; i < 32; i++)
		v[i] = (i * 7919) % 101;
	qsort((char *)v, 32, sizeof v[0], cmp);
	p = malloc(200);
	strcpy(p, "the quick brown fox");
	q = strstr(p, "brown");
	n = strtol("4242", (char **)0, 10);
	sprintf(b, "slmid[%d] alive v0=%d v31=%d s=%s n=%ld e=%s\n",
		getpid(), v[0], v[31], q == 0 ? "no" : "ok", n, strerror(2));
	write(1, b, strlen(b));
	free(p);
	if (argc > 1)
		sleep(atoi(argv[1]));
	return (0);
}
