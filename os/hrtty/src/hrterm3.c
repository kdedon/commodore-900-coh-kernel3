

#include	<signal.h>
#include	<errno.h>
#include	"../h/rico.h"
#include	"../h/machine.h"
#include	"../h/display.h"
#include	"../h/subr.h"
#include	"../h/font.h"

extern	unsigned short	nulls[];
 
uint	ascii[MAXLINE][MAXCOL];
 
uint	line,
	col,
	iget,
	iput,
	inl;
	state;
bool	iesc;

/*
**	Screen Initialization routine.  Called by htload
*/
ricoload()
{
	register	unsigned short i,j;
	unsigned	short *sp;
	for(i = 0; i <YSCROLL*WPERSL;i++)nulls[i] = 0xffff;
	sp = SEG0;
	for(i = 0; i<512; i++)
		for(j = 0; j < 1024/16; j++)
			*sp++ = 0xffff;
	sp = SEG1;
	for(i = 0; i< 800-512; i++)
		for(j = 0; j < 1024/16; j++)
			*sp++ = 0xffff;
	state = 0;
	col = 0;
	line = MAXLINE-1;
 
	for(i=0; i<MAXLINE; i++)
		for(j=0; j<MAXCOL; j++)
			ascii[i][j] = ' ';
 
	cursor();
}

rico( c)
{
	register  li;
 
	static uint	
			arg,
			args[2];

	switch (state) {
	/*
	 * no ongoing escape sequence
	 */
	case 0:
		switch (c) {
		case '\33':
			state = 1;
			break;
		case '\n':
			++line;
			if (line >= MAXLINE) {
				uint i,j;
/*
				j = (MAXLINE - 1)*YSCROLL;
				for(i=0;i<YSCROLL;++i)
					scompute(j+i);
*/
				line = MAXLINE - 1;

				for(i=1; i< MAXLINE; i++){
					moveline(i, 1);
					slcompute(i);
				}
 
				for(i=0; i<MAXCOL; i++)
					ascii[MAXLINE-1][i] = ' ';
				scroll( );
			}
			else{
/*
				uint i,j;
 
				if( line - 1){
					j = (line -1)*YSCROLL;
					for(i=0; i<YSCROLL; ++i)
						scompute(j+i);
				}
*/
			}
			col = 0;
			break;
		case '\r':
			col = 0;
			break;
		case '\t':
			col = tab( col);
			break;
		case '\b':
			if (col)
				--col;
			break;
		case ctrl( 'G'):
			bell( );
			break;
		default:
			plotc( c, col, line);
			++col;
		}
		break;
	/*
	 * ESC
	 */
	case 1:
		switch (c) {
		case '[':
			arg = nel( args);
			do {
				args[--arg] = 0;
			} while (arg);
			state = 2;
			break;
		default:
			state = 0;
			rico( c);
			break;
		}
		break;
	/*
	 * ESC [
	 */
	case 2:
		switch (c) {
		default:
			if ((isascii( c))
			and (isdigit( c))
			and (arg < nel( args))) {
				args[arg] = args[arg]*10 + c - '0';
				break;
			}
			state = 0;
			rico( c);
			break;
		case ';':
			++arg;
			break;
		case 'H':		/* Cursor Home */
			col = 0;
			if (args[1])
				col = args[1] - 1;
			if (args[0])
				--args[0];
			if (args[0] < MAXLINE){
/*
				if(~(texttab[line] & SCROLLABLE))
					slcompute(line);
*/
				line = args[0];
			}
			state = 0;
			break;
		case 'A':		/* Cursor Up */
			if (not args[0])
				++args[0];
			if (args[0] <= line){
/*
				if(~(texttab[line] & SCROLLABLE))
					slcompute(line);
*/
				line -= args[0];
			}
			state = 0;
			break;
		case 'B':		/* Cursor Down */
			if (not args[0])
				++args[0];
			if (args[0] < MAXLINE-1){
/*
				if(~(texttab[line] & SCROLLABLE))
					slcompute(line);
*/
				line += args[0];
			}
			state = 0;
			break;
		case 'D':
			if (not args[0])
				++args[0];
			if (args[0] <= col)
				col -= args[0];
			state = 0;
			break;
		case 'C':
			if (not args[0])
				++col;
			else
				col += args[0];
			state = 0;
			break;
 
		case 'J':		/* Erase Screen */
			for(line = 0; line < MAXLINE; line++) 
			{
				uint	j;
 
				for(j=0; j<MAXCOL; j++)
					ascii[line][j] = ' ';
 
/*
				texttab[line] &= ERASED;
*/
				linerase(line);
/*
				slcompute(line);
*/
			}
			slecompute();
			line = 0;
			col = 0;
			state = 0;
			break;
		case 'L':
			scrolld( line);
			col = 0;
			state = 0;
			break;
		case 'M':
			for(li=line; li< MAXLINE; li++)
				if(li)
					moveline(li, 1);
			scrollu( line);
			for(li=0; li<MAXCOL; li++)
				ascii[MAXLINE][li] = ' ';
			col = 0;
			state = 0;
			break;
		case 'K':
			eolerase( line, col);
			state = 0;
			break;
		case '@':
			cinsert( args[0]);
			state = 0;
			break;
		case 'P':
			cdelete( args[0]);
			state = 0;
			break;
		}
	}
}
isascii(c) char c;{ return((c >= 0x20)&&(c <= 0x7f));}
isdigit(c) char c;{ return((c >= '0')&&(c<='9'));}


scrolld( li)
int	li;
{
	uint	y,
		ybase,
		o,
		*p,
		*q,
		co;
	int	i;
	int	n;
 
	for (n=MAXLINE-1; n>li; --n)
			slcompute( n);
	for (n=MAXLINE-1; n>li; --n)
	{
		i = n-1;
		texttab[n] = texttab[i];
	}
	 
	for (n=MAXLINE-2; n>=li; n--)
		moveline(n, 0);
 
	ybase = li * YSCROLL;
	y = (MAXLINE-1) * YSCROLL;
	while (y > ybase) {
		--y;
		if (y >= YSPLIT) {
			p = (uint *)SEG1 + (y-YSPLIT)*WPERSL;
			q = p + YSCROLL*WPERSL;
		} else if (y+YSCROLL < YSPLIT) {
			p = (uint *)SEG0 + y*WPERSL;
			q = p + YSCROLL*WPERSL;
		} else {
			p = (uint *)SEG0 + y*WPERSL;
			q = (uint *)SEG1 + (y+YSCROLL-YSPLIT)*WPERSL;
		}
		n = scantab[y+YSCROLL].sc_nword;
		if (n) {
			o = scantab[y+YSCROLL].sc_off;
			p += o;
			q += o;
			aldir( q, p, n);
		}
		scantab[y+YSCROLL] = scantab[y];
	}
	eolerase( li, 0);
	if (li+1 < MAXLINE)
		texttab[li+1] &= ~SCROLLABLE;
}


eolerase( li, co)
uint	li,
	co;
{

	if (texttab[li] & ERASED)
		return;
	if (not co) {
 
		for(; co<MAXCOL; ++co)
			ascii[li][co] = ' ';
 
		linerase( li);
		texttab[li] |= ERASED;
	}
	else{
 
 
		for (; co<MAXCOL; ++co)
			plotc( ' ', co, li);
	}
	texttab[li] &= ~SCROLLABLE;
	if (li+1 < MAXLINE)
		texttab[li+1] &= ~SCROLLABLE;
}


cinsert( n)
uint	n;
{
	register co;

	if ( not n)
		n = 1;
	if (n <= MAXCOL)
		for (co=MAXCOL-n; co>col; ) {
			--co;
			plotc(ascii[line][co], co+n, line);
		}
	for (co=col; co<col+n && co<MAXCOL; ++co){
		plotc(' ', co, line);
	}
	slcompute(line);
/*
	texttab[line] &= ~SCROLLABLE;
	if (line+1 < MAXLINE)
		texttab[line+1] &= ~SCROLLABLE;
*/
}


cdelete( n)
uint	n;
{
	register co;

	if (not n)
		n = 1;
 
	for (co=col; co+n<MAXCOL; ++co)
		plotc(ascii[line][co+n], co, line);
	for (; co<MAXCOL; ++co)
		plotc(' ', co, line);
	texttab[line] &= ~SCROLLABLE;
	if (line+1 < MAXLINE)
		texttab[line+1] &= ~SCROLLABLE;
}

plotc( c, co, li)
uint	co,
	li;
{
	register short	*fp;
	register char   *p;
	register uint	y;
	uint		ylim;
	char		b,
			segflag;
	uint		coladdr;

	if (co >= MAXCOL)
		return;
 
	if((c > 0x7f) && (c < 0xa0)) return;
/*
	if(c < 0x20) return;
*/
 
	ascii[li][co] = c;
 
	fp = font[c-' '];
	if(c & 0x80) fp = font[c-0x40];
	y = li * YSCROLL;
	ylim = y + YSCROLL;
	coladdr = ( co * 3) >> 1;	/* co*12/8 */
	segflag = 0;
	p = (char *)SEG0+y*BPERSL + coladdr;
 
	if (co&1) {
		for(; y<ylim; y++) {
			if(y >= YSPLIT){
				if( not segflag){
					p = (char *)SEG1+(y-YSPLIT)*BPERSL+
						coladdr;
					segflag = 1;
				}
			}
			b = (char)((~(*fp>>12))&0xf);
			*p = (*p&0xf0)|b;
			*++p = ~((char)(*fp++>>4));
			p--;
			p += BPERSL;
		}
	}
	else {
		
		for (; y<ylim; y++) {
			if (y >= YSPLIT){
				if( not segflag){
					p = (char *)SEG1 + (y-YSPLIT)*BPERSL+ 
						coladdr;
					segflag = 1;
				}
			}
			*p++ = (char)(~((*fp)>>8));
			*p = (*p&0xf) | (char)((~(*fp++))&0xf0);
			p--;
			p += BPERSL;
		}

	}
	texttab[li] &= ~ (SCROLLABLE|ERASED);
	if (line+1 < MAXLINE)
		texttab[line+1] &= ~SCROLLABLE;
}


bell( )
{
	uint	i,
		*p;

	p = SEG0;
	for (i=0; i<2*YSCROLL*WPERSL; ++i)
		*p++ ^= ~0;
	p = SEG0;
	for (i=0; i<2*YSCROLL*WPERSL; ++i)
		*p++ ^= ~0;
}


cursor( )
{
	register uchar	*p;
	register uint	y,
			ylim;
	register int	coladdr;
	char	segflag;

	if (col >= MAXCOL)
		return;
	y = line * YSCROLL;
	ylim = y + YSCROLL;
	coladdr = (col * 3) >> 1;	/* col * 12 / 8 */
	p = (char *)SEG0 + y*BPERSL + coladdr;
	segflag = 0;
	if(col&1) {
		for (; y<ylim; ++y) {
			if (y >= YSPLIT){
				if( not segflag){
					p = (char *)SEG1 + (y-YSPLIT)*BPERSL + 
						coladdr;
					segflag = 1;
				}
			}
			*p++ ^=0x0f;
			*p ^= 0xff;
			p--;
			p += BPERSL;
		}
	}
	else{
		for(; y<ylim; ++y) {
			if( y >= YSPLIT){
				if( not segflag){
					p = (char *)SEG1 + (y-YSPLIT)*BPERSL +
						coladdr;
					segflag = 1;
				}
			}
			*p++ ^= 0xff;
			*p ^= 0xf0;
			p--;
			p += BPERSL;
		}
	}
}
 
/* given dest, src col numbers will move 12 bits of a char in raster */
 
int	tmove(dst, src, p)
register int	dst;	/* destination column */
register int	src;	/* source column */
uchar	*p;	/* raster address */
{
	register	tdst,
			tsrc;
 
	tdst = (dst * 3) >> 1;	/* for 12 bits 12/8 */
	tsrc = (src * 3) >> 1;
 
	if( dst & 1){	/* odd dst */
		if( src & 1){	/* odd src */
			p[tdst] = (p[tsrc] & 0x0F) | (p[tdst] & 0xF0);
			p[tdst+1] = p[tsrc+1];
		}
		else{	/* even src */
			p[tdst] = (p[tdst] & 0xF0) | ((p[tsrc] >> 4) & 0x0F);
			p[tdst + 1] = (p[tsrc] << 4) | (p[tsrc+1] >> 4);
		}
	}
	else{		/* even dst */
		if( src & 1){ 	/* odd src */
			p[tdst] = (p[tsrc] << 4) | (p[tsrc+1] >> 4);
			p[tdst+1] = (p[tdst+1] & 0x0F) | (p[tsrc+1] << 4);
		}
		else{	/* even src */
			p[tdst] = p[tsrc];
			p[tdst+1] = (p[tdst+1] & 0x0F) | (p[tsrc+1] & 0xF0);
		}
	}
}
 
int	moveline(li, flag)
uint	li;
uint	flag;
{
	uint	*p,
		*q;
 
/*
	if(flag)
		for(i=0; i< MAXCOL; i++)
			ascii[li-1][i] = ascii[li][i];
	else
		for(i=0; i< MAXCOL; i++)
			ascii[li+1][i] = ascii[li][i];
*/
	p = ascii[li];
	if( flag)
		q = p - MAXCOL;
	else
		q = p + MAXCOL;
	aldir( q, p, MAXCOL);
}
