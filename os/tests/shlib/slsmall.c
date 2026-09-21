/*
 * slsmall -- a tiny libc.1 client that prints and sleeps, for measuring
 * free memory.  Only functions bind, so it writes with sprintf() and write(2).
 */

main(argc, argv)
int argc;
char **argv;
{
	char	b[64];

	sprintf(b, "slsmall[%d] alive\n", getpid());
	write(1, b, strlen(b));
	if (argc > 1)
		sleep(atoi(argv[1]));
	return (0);
}
