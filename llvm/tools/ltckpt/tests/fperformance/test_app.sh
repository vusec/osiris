#!/bin/bash

ROOT=$( dirname $0 )/../../../..

OLDPWD=`pwd`

if [ $# -ne 3 ]; then
	echo "Usage: $0 <app_name> <app_dir> <tol_function>"
	exit 1
fi

rm -rf results/$1/*
mkdir -p results/$1
cd results/$1

. $ROOT/tests/common/common.sh
. $ROOT/tests/common/ltckpt.sh

#
# Parameters
#
FUNC_FREQS=${FUNC_FREQS:-"1 2 4 8 16 32 64 128 256 512 1024"}
PASS_ARGS_COMMON="-aopify-rand-seed=1 -aopify-start-hook-map=(^\$)|(^[^l].*\$)|(^l[^t].*\$)/^.*\$/ltckpt_aop_hook -inline -dse"

TEST_NAME=fperformance
C=${C:-app}

CONFIGURE_APP=${CONFIGURE_APP:-0}
RECOMPILE_LIBS=${RECOMPILE_LIBS:-0}
DO_BASELINE=${DO_BASELINE:-1}
DO_BITMAP=${DO_BITMAP:-1}
DO_WRITELOG=${DO_WRITELOG:-1}
DO_SMMAP=${DO_SMMAP:-1}
DO_FORK=${DO_FORK:-1}

BASELINE_EXP_RUNS=${BASELINE_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}
BITMAP_EXP_RUNS=${BITMAP_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}
WRITELOG_EXP_RUNS=${WRITELOG_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}
SMMAP_EXP_RUNS=${SMMAP_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}
FORK_EXP_RUNS=${FORK_EXP_RUNS:-$EDFI_DEFAULT_EXP_RUNS}

#
# Common init (any common $VAR is overridable from the environment variable EDFI_$VAR)
#
APP_DIR=$EDFI_APPS_ROOT/$2
APP_NAME=$1
TOL_FUNCTION=$3
SERVERCTL_CMD_PREFIX="RUN_CP=1"
LTCKPT_IS_BASELINE=1
EXP_TYPES=""
common_init
ltckpt_sysctl_conf

#
# Performance experiment implementation
#
function do_fperformance_exp {
	local TYPE=$1
	local EXP_RUNS=$2
	local PRE_CALLBACK=$3
	local TEST_NAME="fperformance"

	log "******************"
	log "****************** Fperformance experiment for $TYPE:"
	log "******************"
	EDFI_TEST_DIR=`pwd`/results/$TYPE
	local TEST_DATA_FILE="$EDFI_TEST_DIR/$TEST_NAME.dat"
	common_test_dir_init $EDFI_TEST_DIR
	run_cmd "echo \"#func-frequency	${TYPE}_PO\" > $TEST_DATA_FILE"
	for FUNC_FREQ in $FUNC_FREQS
	do
		LTCKPT_FUNC_PROB=`echo "scale=6;1/$FUNC_FREQ" | bc`
		log ">>>>>>>>>>>>>>>>>>>>>>"
		log ">>>>>>>>>>>>>>>>>>>>>> Fperformance experiment for $TYPE (func frequency=${FUNC_FREQ}, probability=${LTCKPT_FUNC_PROB}):"
		log ">>>>>>>>>>>>>>>>>>>>>>"
		EDFI_TEST_PREFIX=${TEST_NAME}_${FUNC_FREQ}
		EDFI_INPUT_INI=${EDFI_TEST_PREFIX}.${EDFI_INPUT_INI_SUFFIX}
		EDFI_OUTPUT_INI=${EDFI_TEST_PREFIX}.${EDFI_OUTPUT_INI_SUFFIX}
	        run_cmd "LLVMGOLD_OPTFLAGS_EXTRA=\"-tol=${TOL_FUNCTION} $PASS_ARGS_COMMON -aopify-prob=${LTCKPT_FUNC_PROB}\" common_instrument_app /dev/null basicaa ltckpt ltckptbasic ltckpt_inline aopify"
		edfi_run_repeated_experiment $EXP_RUNS $EDFI_SEC_MILLIS $PRE_CALLBACK ltckpt_performance_exp_cb NULL
		ltckpt_fperformance_analysis
		if [ -e $EDFI_TEST_DIR/exps/${EDFI_OUTPUT_INI}.po ]; then
			PO=`edfi_get_ini_value $EDFI_TEST_DIR/exps/${EDFI_OUTPUT_INI}.po bench-tput`
			PO_4K=`edfi_get_ini_value $EDFI_TEST_DIR/exps/${EDFI_OUTPUT_INI}.po bench-4K-tput`
			PO_64K=`edfi_get_ini_value $EDFI_TEST_DIR/exps/${EDFI_OUTPUT_INI}.po bench-64K-tput`
		else
			PO="0"
			PO_4K="0"
			PO_64K="0"
		fi
		run_cmd "echo \"$FUNC_FREQ	$PO	$PO_4K	$PO_64K\" >> $TEST_DATA_FILE"
	done
}

if [ $CONFIGURE_APP -ne 0 ]; then
	run_app_cmd "./configure.llvm"
fi
if [ $RECOMPILE_LIBS -ne 0 ]; then
	(cd $ROOT/../../lkm/smmap && make clean install)
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

function ltckpt_fperformance_pre_baseline_cb {
	log "--> Performance pre callback with '$@'..."
	local INPUT_FILE=$1
	local RUN_ID=$2
	run_cmd "edfi_build_ini $EDFI_SEC_MILLIS static=1 > $INPUT_FILE"
	log "<-- Performance pre callback done."
	return 1
}

SAVED_SERVERCTL_CMD_PREFIX=$SERVERCTL_CMD_PREFIX
SERVERCTL_CMD_PREFIX=""
RELINK_ARGS="NULL" common_recompile_libs
do_fperformance_exp baseline $BASELINE_EXP_RUNS ltckpt_fperformance_pre_baseline_cb
SERVERCTL_CMD_PREFIX=$SAVED_SERVERCTL_CMD_PREFIX

fi

#
# Performance experiment: Bitmap
#
if [ $DO_BITMAP -eq 1 ]; then

function ltckpt_fperformance_pre_bitmap_cb {
	log "--> Performance pre callback with '$@'..."
	local INPUT_FILE=$1
	local RUN_ID=$2
	run_cmd "edfi_build_ini $EDFI_SEC_MILLIS static=1 > $INPUT_FILE"
	log "<-- Performance pre callback done."
	return 1
}

LTCKPT_IS_BASELINE=0
RELINK_ARGS="ltckpt" common_recompile_libs -DLTCKPT_CFG_CHECKPOINT_APPROACH=1 -DBITMAP_TYPE=5
do_fperformance_exp bitmap $BITMAP_EXP_RUNS ltckpt_fperformance_pre_bitmap_cb

fi

#
# Performance experiment: Writelog
#
if [ $DO_WRITELOG -eq 1 ]; then

function ltckpt_fperformance_pre_writelog_cb {
	log "--> Performance pre callback with '$@'..."
	local INPUT_FILE=$1
	local RUN_ID=$2
	run_cmd "edfi_build_ini $EDFI_SEC_MILLIS static=1 > $INPUT_FILE"
	log "<-- Performance pre callback done."
	return 1
}

RELINK_ARGS="ltckpt" common_recompile_libs -DLTCKPT_CFG_CHECKPOINT_APPROACH=3 -DLTCKPT_WRITELOG_SWITCHABLE=1
do_fperformance_exp writelog $WRITELOG_EXP_RUNS ltckpt_fperformance_pre_writelog_cb

fi

#
# Performance experiment: Smmap
#
if [ $DO_SMMAP -eq 1 ]; then

function ltckpt_fperformance_pre_smmap_cb {
	log "--> Performance pre callback with '$@'..."
	local INPUT_FILE=$1
	local RUN_ID=$2
	run_cmd "edfi_build_ini $EDFI_SEC_MILLIS static=1 > $INPUT_FILE"
	log "<-- Performance pre callback done."
	return 1
}

RELINK_ARGS="ltckpt" common_recompile_libs -DLTCKPT_CFG_CHECKPOINT_APPROACH=4
sudo insmod $SMMAP_KO
do_fperformance_exp smmap $SMMAP_EXP_RUNS ltckpt_fperformance_pre_smmap_cb
sudo rmmod $SMMAP_KO

fi

#
# Performance experiment: Fork
#
if [ $DO_FORK -eq 1 ]; then

function ltckpt_fperformance_pre_fork_cb {
	log "--> Performance pre callback with '$@'..."
	local INPUT_FILE=$1
	local RUN_ID=$2
	run_cmd "edfi_build_ini $EDFI_SEC_MILLIS static=1 > $INPUT_FILE"
	log "<-- Performance pre callback done."
	return 1
}

RELINK_ARGS="ltckpt" common_recompile_libs -DLTCKPT_CFG_CHECKPOINT_APPROACH=2
do_fperformance_exp fork $FORK_EXP_RUNS ltckpt_fperformance_pre_fork_cb

fi

#
# Merge results
#
if [ $DRY_RUN -eq 0 ]; then
	run_cmd "paste `find results -name $TEST_NAME.dat | sort | xargs` > results/$TEST_NAME.all.dat"
fi

cd $OLDPWD

