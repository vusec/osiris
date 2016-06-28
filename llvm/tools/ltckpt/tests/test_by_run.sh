#!/bin/bash

MYPWD=`pwd`

#
# Sample usage: RESULTS_CMD="scp -r %RESULTS_DIR% cgiuffr@giuffrida.few.vu.nl:archive/cloud/update-time" ./test_by_run.sh 1 1 update-time/test.sh
#

if [ $# -lt 3 ] || [ ! -e $3 ]; then
	echo "Usage: $0 <first_run_id> <last_run_id> <test_script> [test_args]"
	exit 1
fi

FIRST_RUN_ID=$1
LAST_RUN_ID=$2
TEST_PATH=$3
shift; shift; shift
TEST_ARGS="$@"
RESULTS_CMD=${RESULTS_CMD:-echo Results directory is '%RESULTS_DIR%'}
DRY_RUN=${EDFI_DRY_RUN:-0}
SKIP_RECOMPILE_LIBS_STEP=${SKIP_RECOMPILE_LIBS_STEP:-0}
BATCH_SIZE=${BATCH_SIZE:-1}

TEST_DIR=`dirname $TEST_PATH`
TEST_FILE=`basename $TEST_PATH`
EXP_RUNS=$(( $LAST_RUN_ID - $FIRST_RUN_ID + 1 ))
RUN_ID=$FIRST_RUN_ID
sudo true

cd $TEST_DIR

if [ $BATCH_SIZE -eq 0 ] || [ $BATCH_SIZE -gt $EXP_RUNS ]; then
	BATCH_SIZE=$EXP_RUNS
fi
BATCH_REM=$(( $EXP_RUNS % $BATCH_SIZE ))

echo "*** Starting experiment from directory $TEST_DIR, runs=$EXP_RUNS, batch_size=$BATCH_SIZE, run_ids=[ ${FIRST_RUN_ID}; ${LAST_RUN_ID} ]..."

while true;
do
	if [ $RUN_ID -gt $LAST_RUN_ID ]; then
		break
	fi
	if [ $RUN_ID -eq $FIRST_RUN_ID ] && [ $BATCH_REM -ne 0 ]; then
		RUN_ID2=$BATCH_REM #run the smallest batch first
	else
		RUN_ID2=$(( $RUN_ID + $BATCH_SIZE - 1))
	fi
	if [ $RUN_ID2 -gt $LAST_RUN_ID ]; then
		RUN_ID2=$LAST_RUN_ID
	fi
	RESULTS_DIR=results-`basename $TEST_DIR`_run_id_${RUN_ID}
	echo "*** Starting run ${i}, run_ids=[ ${RUN_ID}; ${RUN_ID2} ], results_dir=${RESULTS_DIR}..."
	rm -rf $RESULTS_DIR &> /dev/null
	rm -rf results  &> /dev/null
	mkdir results &> /dev/null
	if [ $SKIP_RECOMPILE_LIBS_STEP -eq 0 ]; then
		EDFI_DRY_RUN=$DRY_RUN RECOMPILE_LIBS=2 ./$TEST_FILE $TEST_ARGS &> results/test_run_id_${RUN_ID}.log || exit 1
	fi
	EDFI_DRY_RUN=$DRY_RUN RECOMPILE_LIBS=0 EDFI_FIRST_RUN_ID=$RUN_ID EDFI_LAST_RUN_ID=$RUN_ID2 ./$TEST_FILE $TEST_ARGS >> results/test_run_id_${RUN_ID}.log 2>&1 || exit 1
	cp -r results $RESULTS_DIR
	RUN_CMD=`echo $RESULTS_CMD | sed "s/%RESULTS_DIR%/$RESULTS_DIR/g"`
	$RUN_CMD
	RUN_ID=$(( $RUN_ID2 + 1 ))
done

cd $MYPWD
