.PHONY: all clean

all: minixtestloganalyze

clean:
	rm -f minixtestloganalyze *.o

include ../common/Makefile.inc

minixtestloganalyze: minixtestloganalyze.o coverage.o function.o helper.o linecounter.o logparse.o mapparse.o minixteststats.o minixtestlogsummarize.o module.o pathlist.o

minixtestloganalyze.o: minixtestloganalyze.c ../common/common.h ../common/coverage.h ../common/debug.h ../common/function.h ../common/logparse.h ../common/module.h ../common/minixtestlogsummarize.h ../common/minixteststats.h pathlist.h

pathlist.o: pathlist.c pathlist.h ../common/common.h ../common/function.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<
