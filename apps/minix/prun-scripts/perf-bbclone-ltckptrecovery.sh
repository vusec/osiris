#!/bin/bash

# Perf Unixbench for MINIX with ltckpt and recovery instrumentation

: ${TESTNAME="perf_bbclone_lt_rc_allsrv"}

if [ -z "$MROOT" -a -f prun-scripts/perf-common.inc ]; then
	MROOT="$PWD"
elif [ ! -f "$MROOT/prun-scripts/edfi-common.inc" ]; then
	echo "error: either run from llvm-apps/apps/minix or set MROOT to that directory" >&2
	exit 1
fi

export TESTNAME
export LOGFILE
export MROOT
export NO_PRUN

. $MROOT/prun-scripts/perf-common.inc

#######   Env setup  ################################################

setup_env

# Test specific initialization 
export BBCLONE=1

# Optimistic recovery model
#export IPC_DEFAULT_DECISION=1
#export KC_DEFAULT_DECISION=1 

SCHEME=$1

case ${SCHEME} in

"optimistic" )
                export IPC_DEFAULT_DECISION=1
                export KC_DEFAULT_DECISION=1
                ;;

"pessimistic" )
                export IPC_DEFAULT_DECISION=4
                export KC_DEFAULT_DECISION=4
                ;;

*)
		echo "Choosing pessimistic, by default."
                export IPC_DEFAULT_DECISION=4
                export KC_DEFAULT_DECISION=4
        #echo "invalid option specified."
        #exit 1
;;

esac

instrument

#######   Test execution   ##########################################

launch_test
