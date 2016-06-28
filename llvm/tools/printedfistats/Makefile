CFLAGS+= -Wall -Werror -O3

.PHONY: add clean test

all: printedfistats

clean:
	rm -f printedfistats *.o

printedfistats: printedfistats.o edfistatsparse.o

printedfistats.o: printedfistats.c edfistatsparse.h ../../include/edfi/df/statfile.h

edfistatsparse.o: edfistatsparse.c edfistatsparse.h ../../include/edfi/df/statfile.h
