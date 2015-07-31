ALLINC = -I../libdie `pkg-config --cflags Qt5Gui` `pkg-config --cflags Qt5Widgets`
ALLLIB = -L../libdie -ldie `pkg-config --libs Qt5Gui` `pkg-config --libs Qt5Widgets`
DEPLIB = ../libdie/libdie.a

PROG = mview
OBJS = NetState.o globals.o MVDisplay.o \
       MVMain.o mview.o \
       NetStateList.o NetState.m.o MVMain.m.o MVDisplay.m.o NetStateList.m.o
SRCS = NetState.cc globals.cc MVDisplay.cc MVMain.cc mview.cc NetStateList.cc \
       NetStateListUI.ui NetStateUI.ui MVMainUI.ui
JUNK = NetStateListUI.cc NetStateListUI.m.cc NetStateUI.cc NetStateUI.m.cc \
       MVMainUI.cc MVMainUI.m.cc NetState.m.cc MVMain.m.cc MVDisplay.m.cc \
       NetStateList.m.cc NetStateListUI.h NetStateUI.h MVMainUI.h

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
NetState.o: NetState.h NetStateUI.h NetStateUI.ui globals.h
NetStateListUI.o: NetStateListUI.h NetStateListUI.ui
NetStateListUI.m.o: NetStateListUI.h NetStateListUI.ui
globals.o: globals.h
MVDisplay.o: MVDisplay.h MVMain.h MVMainUI.h MVMainUI.ui NetStateList.h \
             NetStateListUI.h NetStateListUI.ui NetState.h NetStateUI.h \
             NetStateUI.ui globals.h
MVMain.o: MVMain.h MVMainUI.h MVMainUI.ui NetStateList.h NetStateListUI.h \
          NetStateListUI.ui NetState.h NetStateUI.h NetStateUI.ui globals.h
NetStateUI.o: NetStateUI.h NetStateUI.ui
NetStateUI.m.o: NetStateUI.h NetStateUI.ui
MVMainUI.o: MVMainUI.h MVMainUI.ui
MVMainUI.m.o: MVMainUI.h MVMainUI.ui
mview.o: MVMain.h MVMainUI.h MVMainUI.ui NetStateList.h NetStateListUI.h \
         NetStateListUI.ui NetState.h NetStateUI.h NetStateUI.ui globals.h
NetStateList.o: NetStateList.h NetStateListUI.h NetStateListUI.ui NetState.h \
                NetStateUI.h NetStateUI.ui globals.h
NetState.m.o: NetState.h NetStateUI.h NetStateUI.ui
MVMain.m.o: MVMain.h MVMainUI.h MVMainUI.ui NetStateList.h NetStateListUI.h \
            NetStateListUI.ui NetState.h NetStateUI.h NetStateUI.ui
MVDisplay.m.o: MVDisplay.h
NetStateList.m.o: NetStateList.h NetStateListUI.h NetStateListUI.ui NetState.h \
                  NetStateUI.h NetStateUI.ui
