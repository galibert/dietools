ALLINC = -I../libdie `freetype-config --cflags`
ALLLIB = -L../libdie -ldie `freetype-config --libs` -llua -lz
DEPLIB = ../libdie/libdie.a

PROG = mschem
OBJS = mschem.o pad_info.o globals.o
SRCS = mschem.cc pad_info.cc globals.cc
JUNK =

OPT=-O9

CC=gcc
CXX=g++
CFLAGS=-g -Wall -Wno-unused-parameter ${OPT}

all: ${PROG}

.SUFFIXES: .cc .hh .o .a .ui .m.cc .m.o .y
.PRECIOUS: ${JUNK}

${PROG}: ${OBJS} ${DEPLIB}
	${CXX} ${CFLAGS} ${OBJS} ${ALLLIB} ${ALLINC} -o ${PROG}

.cc.o:
	${CXX} -c ${CFLAGS} ${DEFS} ${ALLINC} $<

.c.o:
	${CC} -c ${CFLAGS} ${DEFS} ${ALLINC} $<

.y.c:
	bison -d -o $@ $<

.y.h:
	bison -d -o $@ $<

.ui.h:
	uic -o $@ $<

.h.m.cc:
	moc -o $@ $<

clean:
	rm -f ${PROG} ${OBJS} ${JUNK}

distclean:
	rm -f ${PROG} ${OBJS} ${JUNK}

###
mschem.o: globals.h pad_info.h
pad_info.o: pad_info.h
globals.o: globals.h
