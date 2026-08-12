SOURCES = hrterm1.c hrterm2.c gall.c scroll.s scrollu.c subr.c tty.c scroll.iv.s
HEADERS = ../h/rico.h ../h/machine.h ../h/display.h ../h/subr.h \
	../h/font.h
CFLAGS = 
OBJS = hrterm1.o hrterm2.o gall.o scroll.o scrollu.o subr.o tty.o \
	kb.o kv.o kbibmtab.o
CC = /usr/sys/z8001/CC
ivhrtty: $(OBJS) scroll.iv.o
	ld -o ivhrtty hrterm?.o gall.o scroll.iv.o scrollu.o subr.o tty.o \
		kb.o kv.o kbibmtab.o -k
hrtty:	$(OBJS)
	ld -o hrtty $(OBJS) -k
input:	input.c
	cc input.c -o input
kb.o:	kb.c
	$(CC) -I. kb.c
kv.o:	kv.c
	$(CC) -I. kv.c
kbibmtab.o:	kbibmtab.c
	cc -c -O -I. kbibmtab.c
kbtab.o:	kbtab.c
	cc -c -O -I. kbtab.c
hrterm2.o:	../h/rico.h ../h/machine.h ../h/display.h ../h/subr.h\
		../h/font.h hrterm2.c
	$(CC) hrterm2.c
subr.o:	../h/rico.h ../h/machine.h ../h/display.h ../h/subr.h subr.c
	$(CC) subr.c
scrollu.o:	../h/rico.h ../h/machine.h ../h/display.h ../h/subr.h\
	scrollu.c
	$(CC) scrollu.c
hrterm1.o:	hrterm1.c
	$(CC) hrterm1.c
gall.o:	gall.c
	$(CC) gall.c
tty.o:	tty.c
	$(CC) tty.c
scroll.o:	scroll.s
	as -o scroll.o scroll.s
scroll.iv.o:	scroll.iv.s
	as -o scroll.iv.o scroll.iv.s
fbackup: $(HEADERS) $(SOURCES) input.c makefile hrdrv
	cp $(HEADERS) $(SOURCES) input.c makefile hrdrv /f0
	touch fbackup
