.PHONY: all clean

all: faultpicker

clean:
	rm -f faultpicker *.o

include ../common/Makefile.inc

faultpicker: faultpicker.o bb_info.o coverage.o edfistatsparse.o function.o helper.o linecounter.o logexeccounts.o logparse.o mapparse.o module.o

faultpicker.o: faultpicker.c bb_info.h ../common/common.h ../common/coverage.h ../common/debug.h ../common/function.h ../common/helper.h ../common/logexeccounts.h ../common/module.h ../../mapprint/mapparse.h ../../printedfistats/edfistatsparse.h ../../../include/edfi/df/statfile.h

bb_info.o: bb_info.c bb_info.h ../common/common.h ../common/coverage.h ../common/debug.h ../common/function.h ../common/helper.h ../common/module.h

edfistatsparse.o: ../../printedfistats/edfistatsparse.c ../../printedfistats/edfistatsparse.h ../../../include/edfi/df/statfile.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<
