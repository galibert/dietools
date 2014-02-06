ALLINC =

LIB  = libdie.a


OBJS = circuit_info.o circuit_map.o net_info.o images.o reader.o State.o \
       timing.o

SRCS = circuit_info.cc circuit_map.cc net_info.cc images.cc reader.cc State.cc \
       timing.cc

HDRS = circuit_info.h net_info.h State.h circuit_map.h timing.h images.h \
       reader.h

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
circuit_info.o: circuit_info.h
circuit_map.o: circuit_map.h
net_info.o: net_info.h circuit_map.h circuit_info.h
images.o: images.h
reader.o: reader.h
State.o: State.h circuit_map.h circuit_info.h net_info.h
timing.o: timing.h
