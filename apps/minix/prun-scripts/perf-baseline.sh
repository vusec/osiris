#!/bin/bash

# Perf Unixbench for MINIX baseline

: ${TESTNAME="perf_baseline"}

if [ -z "$MROOT" -a -f prun-scripts/perf-common.inc ]; then
	MROOT="$PWD"
elif [ ! -f "$MROOT/prun-scripts/edfi-common.inc" ]; then
	echo "error: either run from llvm-apps/apps/minix or set MROOT to that directory" >&2
	exit 1
fi

export TESTNAME
export LOGFILE
export MROOT
export BUILD_ISO

. $MROOT/prun-scripts/perf-common.inc

#######   Env setup  ################################################

setup_env

# Test specific initialization

rm -rf $MROOT/obj.i386/destdir.i386/*
build_minix
C=servers $MROOT/build.llvm O3

#######   Test execution   ##########################################

launch_test

