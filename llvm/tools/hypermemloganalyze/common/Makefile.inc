CFLAGS+= -g -O0 -Wall -Werror -I../common
LDFLAGS+= -g
LDLIBS+= -lm

coverage.o: ../common/coverage.c ../common/common.h ../common/coverage.h ../common/function.h ../common/helper.h ../common/linecounter.h ../common/module.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

function.o: ../common/function.c ../common/common.h ../common/function.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

helper.o: ../common/helper.c ../common/helper.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

linecounter.o: ../common/linecounter.c ../common/linecounter.h ../common/helper.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

logexeccounts.o: ../common/logexeccounts.c ../common/logexeccounts.h ../common/common.h ../common/debug.h ../common/helper.h ../common/logparse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

logparse.o: ../common/logparse.c ../common/common.h ../common/debug.h ../common/helper.h ../common/logparse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

mapparse.o: ../../mapprint/mapparse.c ../../mapprint/mapparse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

minixteststats.o: ../common/minixteststats.c ../common/minixteststats.h ../common/common.h ../common/coverage.h ../common/debug.h ../common/function.h ../common/helper.h ../common/logparse.h ../common/module.h ../common/minixtestlogsummarize.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

minixtestlogsummarize.o: ../common/minixtestlogsummarize.c ../common/minixtestlogsummarize.h ../common/common.h ../common/coverage.h ../common/debug.h ../common/helper.h ../common/logparse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

module.o: ../common/module.c ../common/common.h ../common/debug.h ../common/function.h ../common/helper.h ../common/linecounter.h ../common/module.h ../../mapprint/mapparse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

module-line.o: ../common/module-line.c ../common/common.h ../common/debug.h ../common/helper.h ../common/module.h ../common/minixteststats.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<
