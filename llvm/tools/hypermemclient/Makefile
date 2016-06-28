CFLAGS+= -g -O1 -Wall -Werror -m32
LDFLAGS+= -g -m32
OBJS=hyper.o hypermem.o hypermem-linux.o hypermem-minix.o

all: hyper

clean:
	rm -f hyper *.o

hyper: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LOADLIBES) $(LDLIBS)

hyper.o: hyper.c hypermem.h hypermem-api.h

hypermem.o: hypermem.c hypermem.h hypermem-api.h

hypermem-linux.o: hypermem-linux.c hypermem.h hypermem-api.h

hypermem-minix.o: hypermem-minix.c hypermem.h hypermem-api.h
