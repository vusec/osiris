ROOT=../../../..
include $(ROOT)/Makefile.inc
include $(ROOT)/llvm/Makefile.inc

install: $(INSTALL_DIR)/edfistat

$(INSTALL_DIR)/edfistat: distclean
	ln -s $(shell pwd)/edfistat.py $@

clean:

distclean:
	rm -f $(INSTALL_DIR)/edfistat

