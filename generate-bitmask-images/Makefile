ALLINC = -I../libdie  -I/usr/include/librsvg-2.0 -I/usr/include/gdk-pixbuf-2.0 -I/usr/include/cairo -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include -I/usr/include/pixman-1
ALLLIB = -L../libdie -ldie -lrsvg-2 -lm -lgio-2.0 -lgdk_pixbuf-2.0 -lgobject-2.0 -lglib-2.0 -lcairo

DEPLIB = ../libdie/libdie.a

PROG = generate-bitmask-images
OBJS = generate-bitmask-images.o
SRCS = generate-bitmask-images.cc
JUNK =

OBJS2 = main.o asrlib.o
JUNK2 = main.s main.luac asrlib.s asrlib.luac

OPT=-O9

CC=gcc
CXX=g++
CFLAGS=-g -Wall ${OPT}

all: ${PROG}

.SUFFIXES: .cc .hh .o .a .ui .m.cc .m.o .y .lua .luac .s
.PRECIOUS: ${JUNK} ${JUNK2}

${PROG}: ${OBJS}  ${DEPLIB}
	${CXX} ${CFLAGS} ${OBJS} ${ALLINC} ${ALLLIB} -o ${PROG}

.cc.o:
	${CXX} -c ${CFLAGS} ${DEFS} ${ALLINC} $<

.c.o:
	${CC} -c ${CFLAGS} ${DEFS} ${ALLINC} $<

.y.c:
	bison -d -o $@ $<

.y.h:
	bison -d -o $@ $<

clean:
	rm -f ${OBJS} ${PROG}

distclean:
	rm -f ${OBJS} ${PROG}

###
