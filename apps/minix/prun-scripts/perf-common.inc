#!/bin/bash

# vim: filetype=sh

# MROOT must be set to path to llvm-apps/apps/minix by calling script

# path specifications
: ${PATHROOT:="$MROOT/../.."}
: ${PATHTOOLS:="$PATHROOT/autosetup.dir/install"}
: ${PATHQEMU:="$PATHTOOLS/bin/qemu-system-x86_64"}

# tell other scripts how to invoke QEMU
export HYPER
: ${HYPER:="$PATHQEMU --enable-kvm"}

if [ ! -x "$PATHQEMU" ]; then
	echo "PATHQEMU or PATHTOOLS set incorrectly or QEMU not built by autosetup (PATHQEMU=\"$PATHQEMU\")" >&2
	exit 1
fi

setup_env()
{
#export PERF=1
export PERF
: ${PERF=1}

export SETUP=0 	      # Tell the drec_test_launcher not to build MINIX again.
export RUN_RECOVERY=0 # Tell the drec_test_launcher not to instrument again.
export DISABLE_LTCKPT_O3_HACK
export NO_PRUN

if which prun > /dev/null; then
	: ${NO_PRUN:=0}
else
	: ${NO_PRUN:=1}
fi

: ${LOGFILE="exec.${TESTNAME}.log"}
export LOGFILE
export TESTSUITE="unixbench"

set -e

echo -n "" > $LOGFILE
echo "PERF: ${PERF}" | tee -a ${LOGFILE}
}

build_minix()
{
ONLY_BUILD_MINIX=1 $MROOT/scripts/setup_distr_recovery.sh $MROOT/../..
}

instrument()
{
echo -n "Running instrumentations... " | tee -a ${LOGFILE}
$MROOT/clientctl fuse clean >> ${LOGFILE} 2>&1
if [ "${WINDOW_PROFILING}" == "1" ]
then
   $MROOT/clientctl fuse prepare_all >> ${LOGFILE} 2>&1
   $MROOT/clientctl fuse recovery_prof >> ${LOGFILE} 2>&1
else
   $MROOT/clientctl fuse complete >> ${LOGFILE} 2>&1
fi
echo "[done]" | tee -a ${LOGFILE}
}

launch_test()
{
echo "TESTNAME: $TESTNAME" | tee -a ${LOGFILE}
if [ "${BUILD_ISO}" != "1" ]
then
	export WINDOW_PROFILING
	echo "WINDOW_PROFILING: $WINDOW_PROFILING" | tee -a ${LOGFILE}
	if [ "${NO_PRUN}" != "1" ]
	then
	  module load prun
	  prun -np 1 $MROOT/scripts/drec_test_launcher.sh $TESTSUITE $TESTNAME
	else
	  $MROOT/scripts/drec_test_launcher.sh $TESTSUITE $TESTNAME
	fi
else
	$MROOT/scripts/drec_build_isos.sh $TESTSUITE $TESTNAME
fi
echo "Done." | tee -a ${LOGFILE}
}
