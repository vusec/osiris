.PHONY: all clean

all: serialanalyze

clean:
	rm -f serialanalyze *.o

include ../common/Makefile.inc

serialanalyze: serialanalyze.o helper.o

serialanalyze.o: serialanalyze.c ../common/helper.h
