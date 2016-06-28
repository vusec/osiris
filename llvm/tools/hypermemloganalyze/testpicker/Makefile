.PHONY: all clean

all: testpicker

clean:
	rm -f testpicker *.o

include ../common/Makefile.inc

testpicker: testpicker.o coverage.o function.o helper.o linecounter.o minixtestlogsummarize.o minixteststats.o logparse.o mapparse.o module.o

testpicker.o: testpicker.c ../common/common.h ../common/coverage.h ../common/debug.h ../common/function.h ../common/helper.h ../common/minixtestlogsummarize.h ../common/minixteststats.h ../common/module.h
