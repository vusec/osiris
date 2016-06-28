.PHONY: all clean

all: hypermemloganalyze

clean:
	rm -f hypermemloganalyze *.o

include ../common/Makefile.inc

hypermemloganalyze: hypermemloganalyze.o helper.o linecounter.o loginfo.o logparse.o mapparse.o module.o stats.o

hypermemloganalyze.o: hypermemloganalyze.c ../common/common.h ../common/helper.h loginfo.h ../common/module.h stats.h

loginfo.o: loginfo.c loginfo.h ../common/common.h ../common/debug.h ../common/helper.h ../common/logparse.h

stats.o: stats.c stats.h stats-chi2.h stats-student-t.h
