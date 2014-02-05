ALLINC =

LIB  = libdie.a


OBJS = images.o reader.o timing.o

SRCS = images.cc reader.cc timing.cc

HDRS = timing.h images.h reader.h

OPT=-O9

CC=gcc
CXX=g++
CFLAGS=-Wall -g ${OPT}

all: ${LIB}

.SUFFIXES: .cc .hh .o .a .ui .m.cc .m.o .y .lua .luac .s
.PRECIOUS: ${JUNK} ${JUNK2}

${LIB}: ${OBJS}
	rm -f ${LIB}
	ar rc ${LIB} ${OBJS}

.cc.o:
	${CXX} -c ${CFLAGS} ${DEFS} ${ALLINC} $<

.c.o:
	${CC} -c ${CFLAGS} ${DEFS} ${ALLINC} $<

.y.c:
	bison -d -o $@ $<

.y.h:
	bison -d -o $@ $<

clean:
	rm -f ${OBJS} ${LIB}

distclean:
	rm -f ${OBJS} ${LIB}

###
images.o: images.h
reader.o: reader.h
timing.o: timing.h
