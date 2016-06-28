CFLAGS+= -Wall -Werror -O3

.PHONY: all clean

all: mapprint

clean:
	rm -f *.o mapprint

mapprint: mapprint.o mapparse.o

mapparse.o: mapparse.c mapparse.h

mapprint.o: mapprint.c mapparse.h
