#!/bin/bash

set -o errexit
set -o errtrace
set -o nounset

ROOT=$( dirname $0 )/../..

. $ROOT/tests/common/common.sh
. $ROOT/tests/common/ltckpt.sh

#
# Common init (any common $VAR is overridable from the environment variable EDFI_$VAR)
#
APP_DIR="NULL"
APP_NAME="NULL"
common_init

LTCKPT_PASS_LOCS=$(( `get_sloccount $LTCKPT_PASS_ROOT cpp` + `get_sloccount $LTCKPT_PASS_ROOT/../include/ltckpt cpp` ))
SECTIONIFY_PASS_LOCS=$(( `get_sloccount $LTCKPT_PASS_ROOT/../sectionify cpp` + `get_sloccount $LTCKPT_PASS_ROOT/../include/sectionify cpp` ))

LTCKPT_LIBS_LOCS=$(( `get_sloccount $LTCKPT_LIBS_ROOT ansic` ))

LTCKPT_SHARED_LIBS_LOCS=$(( `get_sloccount $LTCKPT_SHARED_LIBS_ROOT ansic` ))

SMMAP_LOCS=$(( `get_sloccount $SMMAP_LKM_ROOT ansic` + `get_sloccount $SMMAP_INCLUDE_ROOT ansic` ))

TOTAL_LOCS=$(( $LTCKPT_PASS_LOCS + $SECTIONIFY_PASS_LOCS + $LTCKPT_LIBS_LOCS + $LTCKPT_SHARED_LIBS_LOCS + $SMMAP_LOCS ))

common_test_dir_init results
run_cmd "edfi_build_ini $EDFI_SEC_LOCS ltckpt_pass=$LTCKPT_PASS_LOCS sectionify_pass=$SECTIONIFY_PASS_LOCS static_libs=$LTCKPT_LIBS_LOCS shared_libs=$LTCKPT_SHARED_LIBS_LOCS smmap=$SMMAP_LOCS total=$TOTAL_LOCS > results/locs.all.ini" 

