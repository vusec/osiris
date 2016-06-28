#!/bin/bash

#
# Sample usage: RESULTS_CMD_PREFIX="scp -r %RESULTS_DIR% cgiuffr@giuffrida.few.vu.nl:archive/cloud/" RESULTS_BASEDIRS="update-time memory" ./test_by_dirs_run.sh 1 1
#

if [ $# -ne 2 ]; then
	echo "Usage: $0 <first_run_id> <first_last_run_id>"
	exit 1
fi

RESULTS_CMD_PREFIX=${RESULTS_CMD_PREFIX:-echo Results generated for dir }
RESULTS_BASEDIRS=${RESULTS_BASEDIRS:-}

FIRST_RUN_ID=$1
LAST_RUN_ID=$2

TEST_FILENAME=test.sh
LOG_FILENAME=test_by_dirs_run.log
EXP_RUNS=$(( $LAST_RUN_ID - $FIRST_RUN_ID + 1 ))
RUN_ID=$FIRST_RUN_ID

echo ">>> Starting dir experiments for runs=$EXP_RUNS, run_ids=[ ${FIRST_RUN_ID}; ${LAST_RUN_ID} ]..."
echo > $LOG_FILENAME

for i in `seq $EXP_RUNS`
do
	for b in `echo $RESULTS_BASEDIRS`
	do
		(time (RESULTS_CMD="${RESULTS_CMD_PREFIX}${b}" ./test_by_run.sh $RUN_ID $RUN_ID ${b}/test.sh) ) 2>&1 | tee -a $LOG_FILENAME
	done
	RUN_ID=$(( $RUN_ID + 1 ))
done

