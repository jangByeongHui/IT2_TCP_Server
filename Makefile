CCOPTS=-Wall -pedantic -g -D_REENTRANT
CC=gcc
LIBS= -pthread
DEPS=config.h Makefile echolib.h checks.h

# the program or programs we want to build
EXECUTABLES=echocli echosrv multisrv driver timer

# If you just type "make", it will default to "make all".
default: $(EXECUTABLES)

# rule to remind people to run configure first
echocli: echolib.o echocli.c $(DEPS)
	$(CC) $(CCOPTS) -o $@ echolib.o $(LIBS) $@.c

echosrv: echolib.o echosrv.c $(DEPS)
	$(CC) $(CCOPTS) -o $@ echolib.o $(LIBS) $@.c

multisrv: echolib.o multisrv.c $(DEPS)
	$(CC) $(CCOPTS) -o $@ echolib.o $(LIBS) $@.c

echolib.o: echolib.c $(DEPS)
	$(CC) $(CCOPTS) -c echolib.c

driver: driver.c echolib.o
	$(CC) $(CCOPTS) -o driver driver.c $(LIBS) echolib.o

timer: timer.c
	$(CC) $(CCOPTS) -o timer timer.c $(LIBS)

#
# "make clean" removes all the files created by "make" and editors
# #
clean:
	rm -rf *.o $(EXECUTABLES) *~ *# a.out *.dSYM
