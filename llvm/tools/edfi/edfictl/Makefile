ROOT=../../../..
include $(ROOT)/Makefile.inc
include $(ROOT)/llvm/Makefile.inc

HEADERS = $(wildcard $(LLVMINC)/edfi/*.h)

SRCS = edfictl.c

LIBS += $(INSTALL_DIR)/edfi_ctl_client.bcc

ifeq ($(Plat),Minix)
	LIBS += -lsys
endif

%.o: %.c $(HEADERS)
	$(QUIET) $(ECHO) "  [GOLD_CC] $<"
	$(QUIET) $(LLVMGOLD_CC) $(CFLAGS)  $(LLVMGOLD_CFLAGS) -c -o $@ $<

edfictl: edfictl.o
	$(QUIET) $(ECHO) "  [GOLD_LINK] $@" 
	$(LLVMGOLD_CC) $(CFLAGS) $(LLVMGOLD_CFLAGS) $(LLVMGOLD_LDFLAGS) -o $@ $@.o $(LIBS)

install: $(INSTALL_DIR)/edfictl

$(INSTALL_DIR)/edfictl: edfictl
	install -c -D -m 744 $? $@

clean:
	rm -f *.o edfictl

distclean: clean
	rm -f $(INSTALL_DIR)/edfictl

