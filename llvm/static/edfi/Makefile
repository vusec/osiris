ROOT=../../..

include $(ROOT)/Makefile.inc

DIRS := ctl df

ifeq ($(Plat),Minix)
EDFI_DEFAULT_PLAT=minix_sys
else
EDFI_DEFAULT_PLAT=unix_apps
endif

ifeq ($(EDFI_PLAT),)
	EDFI_PLAT=$(EDFI_DEFAULT_PLAT)
endif

plat_link:
	rm -f ctl
	ln -s platform/$(EDFI_PLAT)/ctl ctl

all: plat_link $(patsubst %,build_%,$(DIRS))

$(patsubst %,build_%,$(DIRS)): DIR = $(subst build_,,$@)
$(patsubst %,build_%,$(DIRS)):
	@echo Building $(DIR) static library...
	$(MAKE) -C $(DIR) install

clean:
	$(foreach DIR, $(DIRS), $(MAKE) -C $(DIR) distclean;)

distclean: clean

install: all