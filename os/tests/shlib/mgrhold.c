/*
 * mgrhold.c -- a resident MGR client, for measuring RAM.
 *
 * It references the names a typical client takes from libmgr, then sleeps.
 */
#include <stdio.h>
#include <mgr/mgr.h>

static int	(*ref[])() = {		/* referenced, not called */
	(int (*)())ckmgrterm,
	(int (*)())m_getfontsize,
	(int (*)())m_getwindowsize,
	(int (*)())menu_load,
	(int (*)())m_ttyset,
	(int (*)())m_ttyreset
};

main(argc, argv)
int argc;
char **argv;
{
	int n;

	n = argc > 1 ? atoi(argv[1]) : 300;
	m_setup(M_DEBUG);		/* stdin and stdout, not the console */
	m_envcount = 0;
	m_flags |= M_FLUSH;
	fprintf(m_termout, "%c%c", m_escchar, E_CLEAREOS);
	printf("mgrhold[%d]: flags %d esc %d refs %d\n", getpid(), m_flags,
		(int)m_escchar, (int)(sizeof ref / sizeof ref[0]));
	fflush(stdout);
	sleep(n);
	return (0);
}
