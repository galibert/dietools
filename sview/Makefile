ALLINC = -I../libdie `pkg-config --cflags QtGui` `freetype-config --cflags`
ALLLIB = -L../libdie -ldie `pkg-config --libs QtGui` `freetype-config --libs`
DEPLIB = ../libdie/libdie.a

PROG = sview
OBJS = NetState.o NetState.m.o state.o NetStateList.m.o SVDisplay.m.o \
       SVDisplay.o SVMain.o SVMain.m.o sview.o NetStateList.o
SRCS = NetState.cc state.cc SVDisplay.cc SVMain.cc sview.cc NetStateList.cc \
       NetStateUI.ui SVMainUI.ui NetStateListUI.ui
JUNK = NetState.m.cc NetStateList.m.cc SVDisplay.m.cc SVMain.m.cc \
       NetStateListUI.h SVMainUI.h NetStateUI.h

OPT=-O9

CC=gcc
CXX=g++
CFLAGS=-g -Wall -Wno-strict-overflow -Wno-unused-parameter ${OPT}

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
NetState.o: NetState.h NetStateUI.h NetStateUI.ui state.h
NetState.m.o: NetState.h NetStateUI.h NetStateUI.ui
state.o: state.h
NetStateList.m.o: NetStateList.h NetStateListUI.h NetStateListUI.ui NetState.h \
                  NetStateUI.h NetStateUI.ui
SVDisplay.m.o: SVDisplay.h
SVDisplay.o: SVDisplay.h SVMain.h SVMainUI.h SVMainUI.ui NetStateList.h \
             NetStateListUI.h NetStateListUI.ui NetState.h NetStateUI.h \
             NetStateUI.ui state.h
SVMain.o: SVMain.h SVMainUI.h SVMainUI.ui NetStateList.h NetStateListUI.h \
          NetStateListUI.ui NetState.h NetStateUI.h NetStateUI.ui state.h
SVMain.m.o: SVMain.h SVMainUI.h SVMainUI.ui NetStateList.h NetStateListUI.h \
            NetStateListUI.ui NetState.h NetStateUI.h NetStateUI.ui
sview.o: SVMain.h SVMainUI.h SVMainUI.ui NetStateList.h NetStateListUI.h \
         NetStateListUI.ui NetState.h NetStateUI.h NetStateUI.ui state.h
NetStateList.o: NetStateList.h NetStateListUI.h NetStateListUI.ui NetState.h \
                NetStateUI.h NetStateUI.ui state.h
