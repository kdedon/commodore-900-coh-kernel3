

#include	<signal.h>
#include	<sgtty.h>
#include	<stdio.h>
#include	"../h/rico.h"
#define FAKEIN ('c'<<8|13)

int		myfd;			/* message access to driver */
struct sgttyb	mytty;			/* saved tty settings for stdin */


main( )
{
	struct sgttyb	s,t;
	int		c;
	int		onintr( );

	myfd = open( "/dev/hrtty", 2);
	if (myfd < 0)
		fatal( "can't open /dev/hrtty");
	ioctl( 0, TIOCGETP, &mytty);
	ioctl( 0, TIOCGETP, &s);
	s.sg_flags |= RAW;
	s.sg_flags &= ~ECHO;
	ioctl( 0, TIOCSETP, &s);
	if (signal( SIGINT, SIG_IGN) != SIG_IGN)
		signal( SIGINT, onintr);

	while ((c=getchar( )) != EOF) {
		t.sg_ispeed = toascii( c);
		ioctl( myfd, FAKEIN, &t);
	}

	ioctl( 0, TIOCSETP, &mytty);
	return (0);
}


onintr( )
{

	ioctl( 0, TIOCSETP, &mytty);
	exit( 1);
}


fatal( fmt)
char	*fmt;
{

	fprintf( stderr, "input: %r\n", &fmt);
	exit( 1);
}
