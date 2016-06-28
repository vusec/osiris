#!/bin/bash

set -e

# export APPLY_MANUAL_DECISIONS
export BBCLONE
export BUILDOPTS
export BUILDVARS
export CONFIGOPTS
export DBG
export DISABLERECOVERY
export JOBS
export IPC_DEFAULT_DECISION
export KC_DEFAULT_DECISION
export IPC_DEFAULT_DECISION
export LTRC_NO_RECOVERY_ON_SVC_FI
export MEMSIZE
export RECOVERYPASSOPTEXTRA
export RELINKOPTS
export ROOT_SIZE
export USR_SIZE

: ${BUILDVARS:="-V CPPFLAGS=-DUSR_STACKTOP=0x50000000 -V DBG=-g"}
: ${JOBS:=16}
: ${REBUILDGOLD:=1}
: ${ROOT_SIZE:=1048576} # integer, size of / partition
: ${USR_SIZE:=8388608} # integer, size of /usr partition

if [ $# -ne 1 ]
then
   echo "Usage: "
   echo "$0 <path to llvm-apps repository> "
   exit 1
fi

LROOT="$1"
MROOT="${LROOT}/apps/minix"
LPASSES="${LROOT}/llvm/passes"
LSTATIC="${LROOT}/llvm/static"

[ ! -f "$MROOT/autosetup-paths.inc" ] || source "$MROOT/autosetup-paths.inc"

: ${LOGFILE="${MROOT}/osiris.setup.log"}
MYPWD=`pwd`

cd ${MROOT}
echo -n > $LOGFILE

cp ${LROOT}/conf/common.overrides.llvm-minix.inc ${LROOT}/common.overrides.llvm.inc
if [ `ls ${LROOT}/common.overrides.*.osiris.inc 2> /dev/null | wc -l` -eq 0 ] && [ ! -f ${LROOT}/common.overrides.osiris.inc ]
then
  cp ${LROOT}/conf/common.overrides.osiris.inc ${LROOT}
  if [ $? -ne 0 ]; then
  	echo "OSIRIS llvm-apps overrides file missing."
  	exit 1
  fi
fi

if [ "$DONT_BUILD_MINIX" != "1" ]
then

# Build MINIX
echo "Building MINIX..." | tee -a ${LOGFILE}
cd ${MROOT}
if [ "$REBUILDGOLD" != 0 -a -f ${MROOT}/minix/minix/llvm/.gold_generated ]; then rm ${MROOT}/minix/minix/llvm/.gold_generated; fi
./configure.llvm $CONFIGOPTS >> ${LOGFILE} 2>&1
echo "		[ done ] " | tee -a ${LOGFILE}

if [ "${ONLY_BUILD_MINIX}" == "1" ]; then
	exit 0
fi

fi

# Build LLVM-APPS
echo "Building LLVM-apps..." | tee -a ${LOGFILE}
cd ${MROOT}

make -C "$LROOT/llvm/passes/edfi" install >> "$LOGFILE" 2>&1
make -C "$LROOT/llvm/passes/fuseminix" install >> "$LOGFILE" 2>&1
make -C "$LROOT/llvm/passes/ltckpt" install >> "$LOGFILE" 2>&1
make -C "$LROOT/llvm/passes/minix" install >> "$LOGFILE" 2>&1
make -C "$LROOT/llvm/passes/sectionify" install >> "$LOGFILE" 2>&1
make -C "$LROOT/llvm/passes/slicer" install >> "$LOGFILE" 2>&1
make -C "$LROOT/llvm/passes/staticrecovery" install >> "$LOGFILE" 2>&1
make -C "$LROOT/llvm/static/edfi" install >> "$LOGFILE" 2>&1
make -C "$LROOT/llvm/static/ltckpt" clean install >> "$LOGFILE" 2>&1
echo "		[ done ] " | tee -a ${LOGFILE}

# Build LLVM DSA for MINIX
echo "Building LLVM DSA..." | tee -a ${LOGFILE}
cd ${MROOT}
./clientctl fuse build_dsa >> ${LOGFILE} 2>&1
echo "		[ done ] " | tee -a ${LOGFILE}

# cleanup before we start off
echo "Pre-execution cleanup..." | tee -a ${LOGFILE}
cd ${MROOT}
./clientctl fuse clean >> ${LOGFILE} 2>&1
echo "		[ done ] " | tee -a ${LOGFILE}

# Preparing setup for distributed recovery
echo "Preparing..." | tee -a ${LOGFILE}
./clientctl fuse prepare_all >> ${LOGFILE} 2>&1
echo "		[ done ] " | tee -a ${LOGFILE}

# Distributed Recovery instrumentations
echo "OSIRIS instrumentations ..." | tee -a ${LOGFILE}
cd ${MROOT}
./clientctl fuse recovery >> ${LOGFILE} 2>&1
echo "		[ done ] " | tee -a ${LOGFILE}

# Inject deterministic faults
if [ "${ENABLE_SUICIDE}" == 1 ]
then
echo "Injecting deterministic faults..." | tee -a ${LOGFILE}
cd ${MROOT}
./clientctl fuse suicide >> ${LOGFILE} 2>&1
echo "		[ done ] " | tee -a ${LOGFILE}
fi

cd ${MROOT}
echo "Creating MINIX image" | tee -a ${LOGFILE}
./clientctl buildimage >> ${LOGFILE} 2>&1
echo "		[ done ] " | tee -a ${LOGFILE}


echo "Finished." | tee -a ${LOGFILE}

cd ${MYPWD}
