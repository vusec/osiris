ROOT=../../..

include $(ROOT)/Makefile.inc

MODULE = ltckpt

ifeq ($(Plat),Minix)
LLVMGOLD_TARGET_ARCH=x86
endif

ifneq ($(LLVMGOLD_TARGET_ARCH), x86_64)
ARCH=x86
CFLAGS+=-DLTCKPT_X86
else
CFLAGS+=-DLTCKPT_X86_64
ARCH=x64
endif

ifeq ($(LTRC_SUICIDE_ONLY_ONCE), 1)
CFLAGS+=-DLTRC_SUICIDE_ONLY_ONCE
endif

ifeq ($(LTRC_SUICIDE_PERIODICALLY), 1)
CFLAGS+=-DLTRC_SUICIDE_PERIODICALLY
endif

ifeq ($(LTRC_SUICIDE_WHEN_RECOVERABLE), 1) 
CFLAGS+=-DLTRC_SUICIDE_WHEN_RECOVERABLE
endif

ifeq ($(LTRC_REHANDLE_REQUEST), 1)
CFLAGS+=-DLTRC_REHANDLE_REQUEST
endif

ifeq ($(LTRC_NO_RECOVERY_ON_SVC_FI), 1)
CFLAGS+=-DLTRC_NO_RECOVERY_ON_SVC_FI
endif

ifeq ($(LTCKPT_O3_HACK), 1)
CFLAGS+=-DLTCKPT_O3_HACK
endif

ifeq ($(LTCKPT_TOGGLE_STATS), 1)
CFLAGS+=-DTOGGLE_ENABLE_STATS
endif

CFLAGS+=-I$(DUNE_DIR)

SRCS = ltckpt_common.c                         \
       ltckpt_stat.c                          \
       ltckpt_aop.c                        \
       ltckpt_debug.c                        \
       ltckpt_ctx.c                        \
       ltckpt_recover.c			   \
       mechanisms/ltckpt_baseline.c        \
       mechanisms/ltckpt_writelog.c           \
       mechanisms/bitmap/ltckpt_bitmap.c      \
       mechanisms/bitmap/ltckpt_bitmap_init.c

ifneq ($(Plat),Minix)
SRCS+= arch/$(ARCH)/ltckpt_common.c           \
       ltckpt_overrides.c                  \
       mechanisms/ltckpt_softdirty.c       \
       mechanisms/ltckpt_fork.c               \
       mechanisms/dune/ltckpt_dune.c          \
       mechanisms/mprotect/ltckpt_mprotect.c        \
       mechanisms/mprotect/mprotect_overrides.c     \
       mechanisms/smmap/ltckpt_smmap.c
endif

OBJS= $(SRCS:%.c=%.o)

BUILD_ON_X86_64=yes

USE_SECTIONIFY=YES
SECTIONIFY_DATA_SECTION_MAP=.*/ltckpt_data
SECTIONIFY_FUNCTION_SECTION_MAP=.*/ltckpt_functions

include $(ROOT)/llvm/static/Makefile.inc
