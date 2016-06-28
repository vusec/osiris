#!/bin/bash

set -e

# expected incoming parameters (in addition to those in edfi-prun-common.inc):
# - FORCETESTS
# - PATHSELTESTS
# - TESTSRECOVERY

# parameters to edfi-prun-common.inc
export FAULTSPEC

# include common functions/settings
. "$PATHAPP/prun-scripts/edfi-prun-common.inc"

# select tests
if [ -n "$FORCETESTS" ]; then
	FAULTSPEC=""
	TESTS="$FORCETESTS"
	echo "Forced tests"
elif [ -s "$PATHSELTESTS" ]; then
	faultline="`tail -n "+$SELTESTINDEX" "$PATHSELTESTS" | head -n1`"
	FAULTSPEC="`echo "$faultline" | cut -f1`"
	TESTS="$TESTSRECOVERY `echo "$faultline" | cut -f2`"
	echo "Faulty run, faultspec: $FAULTSPEC"
	if [ -z "$FAULTSPEC" ]; then
		echo "Empty faultspec (index=$SELTESTINDEX), aborting"
		exit 1
	fi
	if [ -z "$TESTS" ]; then
		echo "Empty test list (index=$SELTESTINDEX), aborting"
		exit 1
	fi
else
	FAULTSPEC=""
	TESTS="$TESTSRECOVERY 1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81 83 sh1 sh2 interp mfs vnd" # not isofs, which fails; also not 82, which is unreliable
	echo "Golden run"
fi
echo "Tests to perform: $TESTS"
echo "Fault to inject: $FAULTSPEC"

# set up boot script for VM
sed "s/{{{TESTS}}}/$TESTS/" "$PATHAPP/prun-scripts/edfi-prun-ltckpt-boot.sh" > "$PATHTESTIMGDIR/files/boot.sh"

# perform the experiment
common_setup_disk_delta
common_setup_disk_config
common_run
common_cleanup
