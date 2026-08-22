/*
 * Kernel portion of typewriter structure.
 */
#ifndef	 KTTY_H
#define	 KTTY_H
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/clist.h>
#include <sgtty.h>
/* An identifier, not defined(): the June 1985 preprocessor, which reads these
 * headers when the 1985 compiler is the flavour building, has no defined()
 * operator ("in #if", and the compile stops).  An identifier that is not a
 * macro counts as zero in an #if for all three preprocessors, so the test
 * means the same to each of them. */
#if _I386 || _Z8001
#include <termio.h>
#endif
#include <sys/timeout.h>

#define	NCIB	256		/* Input buffer */
#define	OHILIM	128		/* Output buffer hi water mark */
#define	OLOLIM	40		/* Output buffer lo water mark */
#define	IHILIM	512		/* Input buffer hi water mark */
#define	ILOLIM	40		/* Input buffer lo water mark */
#define	ITSLIM	(IHILIM-(IHILIM/4))	/* Input buffer tandem stop mark */
#define	ESC	'\\'		/* Some characters */

typedef struct tty {
	CQUEUE	t_oq;		/* Output queue */
	CQUEUE	t_iq;		/* Input queue */
	char	*t_ddp;		/* Device specific */
	int	(*t_start)();	/* Start function */
	int	(*t_param)();	/* Load parameters function */
	char	t_dispeed;	/* Default input speed */
	char	t_dospeed;	/* Default output speed */
	int	t_open;		/* Open count */
	int	t_flags;	/* Flags */
	int	t_xflags;	/* Second flag word: TX_* in <sys/tty.h> */
	char	t_nfill;	/* Number of fill characters */
	char	t_fillb;	/* The fill character */
	int	t_ibx;		/* Input buffer index */
	char	t_ib[NCIB];	/* Input buffer */
	int	t_hpos;		/* Horizontal position */
	int	t_opos;		/* Original horizontal position */
	struct	sgttyb t_sgttyb;/* Stty/gtty information */
	struct	tchars t_tchars;/* Tchars information */
#if _I386 || _Z8001
	struct	termio t_termio;
	TIM	t_vtime;	/* VTIME timer (4.x termio read) */
#endif
	int	t_group;	/* Process group */
	int	t_escape;	/* Pending escape count */
	event_t t_ipolls;	/* List of input polls enabled on device */
	event_t t_opolls;	/* List of output polls enabled on device */
	TIM	t_rawtim;	/* Raw timing struct */
	/*
	 * Code selector of a loadable driver, 0 for a resident one.  The i286
	 * and i386 tty layers far-called t_start/t_param through it, because a
	 * loadable driver's TEXT was a segment of its own while its DATA was
	 * the kernel's (relic d/kernel/USRSRC/ttydrv/tty.c NEAR_OR_FAR_CALL,
	 * i8086/src/ldas.s ld_call).  Here a function pointer is a full far
	 * address and a loaded driver's text and data share ONE segment, so
	 * the indirect call needs no selector and nothing sets or reads this.
	 * It is not the hook for reaching a loadable driver's DATA: that is a
	 * window base, not a selector, and poll.c keeps its own (see there).
	 */
	int	t_cs_sel;
} TTY;

/*
 * Test macros.
 * Assume `tp' holds a TTY pointer.
 *	  `c'  a character.
 * Be very careful if you work on the
 * tty driver that this is true.
 */
#define	ISINTR	(tp->t_tchars.t_intrc  == c)
#define	ISQUIT	(tp->t_tchars.t_quitc  == c)
#define	ISEOF	(tp->t_tchars.t_eofc   == c)
#define	ISBRK	(tp->t_tchars.t_brkc   == c)
#define	ISSTART	(tp->t_tchars.t_startc == c)
#define	ISSTOP	(tp->t_tchars.t_stopc  == c)
#define	ISCRMOD	((tp->t_sgttyb.sg_flags&CRMOD) != 0)
#define	ISXTABS	((tp->t_sgttyb.sg_flags&XTABS) != 0)
#define	ISECHO	((tp->t_sgttyb.sg_flags&ECHO)  != 0)
#define	ISERASE	(tp->t_sgttyb.sg_erase == c)
#define	ISKILL	(tp->t_sgttyb.sg_kill  == c)
#define	stopc	(tp->t_tchars.t_stopc)
#define	startc	(tp->t_tchars.t_startc)

/*
 * The following are not part of S5 sgtty.
 */
#define	ISRIN	((tp->t_sgttyb.sg_flags&RAWIN) != 0)
#define	ISROUT	((tp->t_sgttyb.sg_flags&RAWOUT)!= 0)
#define	ISCRT	((tp->t_sgttyb.sg_flags&CRT)   != 0)
#define	ISCBRK	((tp->t_sgttyb.sg_flags&CBREAK)!= 0)
#define	ISTAND	((tp->t_sgttyb.sg_flags&TANDEM)!= 0)
#define	ISBBYB	((tp->t_sgttyb.sg_flags&(RAWIN|CBREAK)) != 0)

/*
 * 4.x termio-mode testers (the 4.x ktty.h did not survive in the source
 * archives; reconstructed from the io.386 tty.c usage).  XMODE_386 gated
 * the k71 useracc checks to protected-mode binaries on the i386; on the
 * Z8001 every user address is checkable, so the guard is always on.
 */
#define	ISIXON	((tp->t_termio.c_iflag&IXON) != 0)
#define	ISICRNL	((tp->t_termio.c_iflag&ICRNL) != 0)
#define	ISIGNCR	((tp->t_termio.c_iflag&IGNCR) != 0)
#define	ISINLCR	((tp->t_termio.c_iflag&INLCR) != 0)
#define	ISISTRIP ((tp->t_termio.c_iflag&ISTRIP) != 0)
#define	ISONLCR	((tp->t_termio.c_oflag&ONLCR) != 0)
#define	ISOCRNL	((tp->t_termio.c_oflag&OCRNL) != 0)
#define	ISISIG	((tp->t_termio.c_lflag&ISIG) != 0)
#define	XMODE_386	1

#endif
