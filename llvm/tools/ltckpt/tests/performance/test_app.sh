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
DO_BASELINE=${DO_BASELINE:-1}
DO_BITMAP=${DO_BITMAP:-0}
DO_UNDOLOG=${DO_UNDOLOG:-0}
DO_SMMAP=${DO_SMMAP:-0}
DO_FORK=${DO_FORK:-0}
DO_DUNE=${DO_DUNE:-0}
DO_MPROTECT=${DO_MPROTECT:-0}
DO_SOFTDIRTY=${DO_SOFTDIRTY:-0}
DO_SM_EVO=${DO_SM_EVO:-0}
DO_SM_NOOP=${DO_SM_NOOP:-0}
DO_SM_GREEDY=${DO_SM_GREEDY:-0}
DO_SM_CSUM=${DO_SM_CSUM:-0}
DO_SM_CSUM_SPARSE=${DO_SM_CSUM_SPARSE:-0}
DO_SM_CSUM0=${DO_SM_CSUM0:-0}
DO_SM_CSUM10=${DO_SM_CSUM10:-0}
DO_SM_LEAKY=${DO_SM_LEAKY:-0}


DO_SM=${DO_SM:-0}
if [ $DO_SM -eq 1 ] ; then
DO_SM_EVO=1
DO_SM_NOOP=1
DO_SM_CSUM=1
DO_SM_CSUM0=0
DO_SM_CSUM10=0
DO_SM_LEAKY=1
fi

if [ "$C" != "app" ]; then
	DO_SMMAP=0
	DO_FORK=0
	DO_MPROTECT=0
	DO_SOFTDIRTY=0
fi

BASELINE_EXP_RUNS=${BASELINE_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}
BITMAP_EXP_RUNS=${BITMAP_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}
UNDOLOG_EXP_RUNS=${UNDOLOG_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}
SMMAP_EXP_RUNS=${SMMAP_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}
FORK_EXP_RUNS=${FORK_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}
DUNE_EXP_RUNS=${DUNE_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}
MPROTECT_EXP_RUNS=${MPROTECT_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}
SOFTDIRTY_EXP_RUNS=${SOFTDIRTY_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}

#
# Common init (any common $VAR is overridable from the environment variable EDFI_$VAR)
#
APP_DIR=$EDFI_APPS_ROOT/$2
APP_NAME=$1
SERVERCTL_CMD_PREFIX="CP_NOMMAP=0 RUN_CP=1"
LTCKPT_IS_BASELINE=1
EXP_TYPES=""
common_init
ltckpt_sysctl_conf

#
# Performance experiment implementation
#

function do_performance_exp {
	local TYPE=$1
	local EXP_RUNS=$2
	local PRE_CALLBACK=$3

	log "******************"
	log "****************** Performance experiment for $TYPE:"
	log "******************"
	EDFI_TEST_DIR=`pwd`/results/$TYPE
	common_test_dir_init $EDFI_TEST_DIR

	EDFI_TEST_PREFIX=${TEST_NAME}
	EDFI_INPUT_INI=${EDFI_TEST_PREFIX}.${EDFI_INPUT_INI_SUFFIX}
	EDFI_OUTPUT_INI=${EDFI_TEST_PREFIX}.${EDFI_OUTPUT_INI_SUFFIX}
	edfi_run_repeated_experiment $EXP_RUNS $EDFI_SEC_MILLIS $PRE_CALLBACK ltckpt_performance_exp_cb NULL
        EXP_TYPES="$EXP_TYPES $TYPE"
}

function ltckpt_performance_pre_gen_cb {
	log "--> Performance pre callback with '$@'..."
	local INPUT_FILE=$1
	local RUN_ID=$2
	run_cmd "edfi_build_ini $EDFI_SEC_MILLIS static=1 > $INPUT_FILE"
	log "<-- Performance pre callback done."
	return 1
}

if [ $RECOMPILE_LIBS -ne 0 ]; then
	(cd $ROOT/../../lkm/smmap && make clean install)
	(cd $ROOT/../../static/ltckpt && make clean install)
	run_app_cmd "./relink.llvm ltckpt"
fi
if [ $RECOMPILE_LIBS -eq 2 ]; then
	exit 0
fi
if [ -d /proc/sys/smmap ]; then
	sudo rmmod $SMMAP_KO
fi

#
# Performance experiment: Baseline
#
if [ $DO_BASELINE -eq 1 ]; then

SAVED_SERVERCTL_CMD_PREFIX=$SERVERCTL_CMD_PREFIX
SERVERCTL_CMD_PREFIX=""
run_app_cmd "CP_METHOD=baseline ./clientctl buildcp"
do_performance_exp baseline $BASELINE_EXP_RUNS ltckpt_performance_pre_gen_cb
SERVERCTL_CMD_PREFIX=$SAVED_SERVERCTL_CMD_PREFIX

fi

LTCKPT_IS_BASELINE=0

#
# Performance experiment: Bitmap
#
if [ $DO_BITMAP -eq 1 ]; then

run_app_cmd "CP_METHOD=bitmap ./clientctl buildcp"
do_performance_exp bitmap $BITMAP_EXP_RUNS ltckpt_performance_pre_gen_cb

fi

#
# Performance experiment: Undolog
#
if [ $DO_UNDOLOG -eq 1 ]; then

run_app_cmd "CP_METHOD=undolog ./clientctl buildcp"
do_performance_exp undolog $UNDOLOG_EXP_RUNS ltckpt_performance_pre_gen_cb

fi

#
# Performance experiment: Smmap
#
if [ $DO_SMMAP -eq 1 ]; then

run_app_cmd "CP_METHOD=smmap ./clientctl buildcp"
sudo insmod $SMMAP_KO
do_performance_exp smmap $SMMAP_EXP_RUNS ltckpt_performance_pre_gen_cb
sudo rmmod $SMMAP_KO

fi

#
# Performance experiment: Fork
#
if [ $DO_FORK -eq 1 ]; then

run_app_cmd "CP_METHOD=fork ./clientctl buildcp"
do_performance_exp fork $FORK_EXP_RUNS ltckpt_performance_pre_gen_cb

fi

#
# Performance experiment: Dune
#
if [ $DO_DUNE -eq 1 ]; then

run_app_cmd "CP_METHOD=dune ./clientctl buildcp"
do_performance_exp dune $DUNE_EXP_RUNS ltckpt_performance_pre_gen_cb

fi

#
# Performance experiment: Mprotect
#
if [ $DO_MPROTECT -eq 1 ]; then

run_app_cmd "CP_METHOD=mprotect ./clientctl buildcp"
do_performance_exp mprotect $MPROTECT_EXP_RUNS ltckpt_performance_pre_gen_cb

fi

#
# Performance experiment: Softdirty
#
if [ $DO_SOFTDIRTY -eq 1 ]; then

run_app_cmd "CP_METHOD=softdirty ./clientctl buildcp"
do_performance_exp softdirty $SOFTDIRTY_EXP_RUNS ltckpt_performance_pre_gen_cb

fi

#
# Performance experiment: Smmap
#
if [ $DO_SM_LEAKY -eq 1 ]; then

run_app_cmd "CP_METHOD=smmap ./clientctl buildcp"
sudo insmod $SMMAP_KO
sudo bash -c "echo 1 > /proc/sys/smmap/conf/use_pagan"
sudo bash -c "echo 2 > /proc/sys/smmap/conf/pagan_mechanism"
do_performance_exp leaky $SMMAP_EXP_RUNS ltckpt_performance_pre_gen_cb
sudo rmmod $SMMAP_KO

fi

#
# Performance experiment: Smmap
#
if [ $DO_SM_EVO -eq 1 ]; then

run_app_cmd "CP_METHOD=smmap ./clientctl buildcp"
sudo insmod $SMMAP_KO
sudo bash -c "echo 1 > /proc/sys/smmap/conf/use_pagan"
sudo bash -c "echo 6 > /proc/sys/smmap/conf/pagan_mechanism"
do_performance_exp smmap.evo $SMMAP_EXP_RUNS ltckpt_performance_pre_gen_cb
sudo rmmod $SMMAP_KO

fi

#
# Performance experiment: Smmap
#
if [ $DO_SM_NOOP -eq 1 ]; then

run_app_cmd "CP_METHOD=smmap ./clientctl buildcp"
sudo insmod $SMMAP_KO
sudo bash -c "echo 1 > /proc/sys/smmap/conf/use_pagan"
sudo bash -c "echo 0 > /proc/sys/smmap/conf/pagan_mechanism"
do_performance_exp smmap.noop $SMMAP_EXP_RUNS ltckpt_performance_pre_gen_cb
sudo rmmod $SMMAP_KO

fi

#
# Performance experiment: Smmap
#
if [ $DO_SM_GREEDY -eq 1 ]; then

run_app_cmd "CP_METHOD=smmap ./clientctl buildcp"
sudo insmod $SMMAP_KO
sudo bash -c "echo 1 > /proc/sys/smmap/conf/use_pagan"
sudo bash -c "echo 1 > /proc/sys/smmap/conf/pagan_mechanism"
do_performance_exp smmap.greedy $SMMAP_EXP_RUNS ltckpt_performance_pre_gen_cb
sudo rmmod $SMMAP_KO

fi

#
# Performance experiment: Smmap
#
if [ $DO_SM_CSUM -eq 1 ]; then

run_app_cmd "CP_METHOD=smmap ./clientctl buildcp"
sudo insmod $SMMAP_KO
sudo bash -c "echo 1 > /proc/sys/smmap/conf/use_pagan"
sudo bash -c "echo 8 > /proc/sys/smmap/conf/pagan_mechanism"
sudo bash -c "echo 0 > /proc/sys/smmap/conf/sparse_csum"
do_performance_exp smmap.csum $SMMAP_EXP_RUNS ltckpt_performance_pre_gen_cb
sudo rmmod $SMMAP_KO
fi

#
# Performance experiment: Smmap
#
if [ $DO_SM_CSUM_SPARSE -eq 1 ]; then

run_app_cmd "CP_METHOD=smmap ./clientctl buildcp"
sudo insmod $SMMAP_KO
sudo bash -c "echo 1 > /proc/sys/smmap/conf/use_pagan"
sudo bash -c "echo 8 > /proc/sys/smmap/conf/pagan_mechanism"
sudo bash -c "echo 1 > /proc/sys/smmap/conf/sparse_csum"
do_performance_exp smmap.csum_sparse $SMMAP_EXP_RUNS ltckpt_performance_pre_gen_cb
sudo rmmod $SMMAP_KO
fi

#
# Performance experiment: Smmap
#
if [ $DO_SM_CSUM0 -eq 1 ]; then

run_app_cmd "CP_METHOD=smmap ./clientctl buildcp"
sudo insmod $SMMAP_KO
sudo bash -c "echo 1 > /proc/sys/smmap/conf/use_pagan"
sudo bash -c "echo 8 > /proc/sys/smmap/conf/pagan_mechanism"
sudo bash -c "echo 'CSUM_MISS_TRESHOLD=0' > /proc/sys/smmap/conf/pagan_conf"
do_performance_exp smmap.csum0 $SMMAP_EXP_RUNS ltckpt_performance_pre_gen_cb
sudo rmmod $SMMAP_KO

fi

#
# Performance experiment: Smmap
#
if [ $DO_SM_CSUM10 -eq 1 ]; then

run_app_cmd "CP_METHOD=smmap ./clientctl buildcp"
sudo insmod $SMMAP_KO
sudo bash -c "echo 1 > /proc/sys/smmap/conf/use_pagan"
sudo bash -c "echo 8 > /proc/sys/smmap/conf/pagan_mechanism"
sudo bash -c "echo 'CSUM_MISS_TRESHOLD=10' > /proc/sys/smmap/conf/pagan_conf"
do_performance_exp smmap.csum10 $SMMAP_EXP_RUNS ltckpt_performance_pre_gen_cb
sudo rmmod $SMMAP_KO

fi

#
# Merge results
#
TYPE_FILES=""
for TYPE in `echo $EXP_TYPES`
do
	if [ ! -d "results/$TYPE/exps" ]; then
		continue
	fi
	if [ "$TYPE" == "baseline" ]; then
		METRIC="max"
	else
		METRIC="relincr"
	fi
	run_cmd "$EDFISTAT ${APP_NAME}.${C}.${TYPE} results/baseline/exps/${EDFI_OUTPUT_INI}@$EDFI_SEC_MILLIS:results/$TYPE/exps/${EDFI_OUTPUT_INI}@$EDFI_SEC_MILLIS $METRIC > results/$TYPE/exps/$TEST_NAME.$TYPE.overhead.ini"
	TYPE_FILES="$TYPE_FILES results/$TYPE/exps/$TEST_NAME.$TYPE.overhead.ini"
done
cat $TYPE_FILES > results/$TEST_NAME.overhead.ini

cd $OLDPWD

