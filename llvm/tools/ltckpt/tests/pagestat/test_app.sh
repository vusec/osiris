#!/bin/bash

ROOT=$( dirname $0 )/../../../..

OLDPWD=`pwd`

if [ $# -ne 2 ]; then
	echo "Usage: $0 <app_name> <app_dir>"
	exit 1
fi

C=${C:-app}

RESULTS_DIR=results/$1
if [ "$C" != "app" ]; then
	RESULTS_DIR=${RESULTS_DIR}.$C
fi

rm -rf $RESULTS_DIR/*
mkdir -p $RESULTS_DIR
cd $RESULTS_DIR

. $ROOT/tests/common/common.sh
. $ROOT/tests/common/ltckpt.sh

#
# Parameters
#
TEST_NAME=performance

RECOMPILE_LIBS=${RECOMPILE_LIBS:-0}

UNDOLOG_EXP_RUNS=${UNDOLOG_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}

#
# Common init (any common $VAR is overridable from the environment variable EDFI_$VAR)
#
APP_DIR=$EDFI_APPS_ROOT/$2
APP_NAME=$1
SERVERCTL_CMD_PREFIX="CP_NOMMAP=0 ATEXIT_DUMP=1 PAGESTAT=1 RUN_CP=1"
LTCKPT_IS_BASELINE=1
EXP_TYPES=""
common_init
ltckpt_sysctl_conf

#
# Performance experiment implementation
#

function ltckpt_pagestat_pre_gen_cb {
	log "-->  pre callback with '$@'..."
    local INPUT_FILE=$1
	local RUN_ID=$2
	run_cmd "edfi_build_ini $EDFI_SEC_MILLIS static=1 > $INPUT_FILE"
	log "<-- Performance pre callback done."

	return 1
}

#
# Performance experiment: Baseline
#

run_app_cmd "CP_METHOD=softdirty ./clientctl buildcp"
TYPE=softdirty
EXP_RUNS=1
PRE_CALLBACK=ltckpt_pagestat_pre_gen_cb

EDFI_TEST_DIR=`pwd`/results/
common_test_dir_init $EDFI_TEST_DIR

EDFI_TEST_PREFIX=${TEST_NAME}
EDFI_INPUT_INI=${EDFI_TEST_PREFIX}.${EDFI_INPUT_INI_SUFFIX}
EDFI_OUTPUT_INI=${EDFI_TEST_PREFIX}.${EDFI_OUTPUT_INI_SUFFIX}
edfi_run_repeated_experiment $EXP_RUNS $EDFI_SEC_MILLIS $PRE_CALLBACK ltckpt_pagestat_exp_cb NULL
    EXP_TYPES="$EXP_TYPES $TYPE"

cd $OLDPWD

