#!/bin/bash

set -o errexit
set -o errtrace
set -o nounset

FIRST_ID=${FIRST_ID:-0}
LAST_ID=${LAST_ID:-0}
NUM_IDS=$(( $LAST_ID - $FIRST_ID + 1 ))

if [ $FIRST_ID -eq 0 ]; then
	FIND_OPTS=""
else
	FIND_OPTS=" | sort -t_ -nk4 | tail -n +${FIRST_ID} | head -${NUM_IDS}"
fi

DO_UPDATE_TIME=${DO_UPDATE_TIME:-1}
DO_MEMORY=${DO_MEMORY:-1}

UPDATE_TIME_DIRS=`eval find /home/$USER/archive/cloud/update-time/ -maxdepth 1 -mindepth 1 -type d -name \*run_id\* | xargs | sed "s/ /:/g"`
MEMORY_DIRS=`eval find /home/$USER/archive/cloud/memory/ -maxdepth 1 -mindepth 1 -type d -name \*run_id\* | xargs | sed "s/ /:/g"`

if [ $DO_UPDATE_TIME -eq 1 ]; then
	echo ">>>>>>>>>>>>>>>>>>>>> Aggregating update time results..."
	(cd update-time && INSTRUMENT_APP=0 EDFI_DRY_RUN=0 EDFI_AGGREGATE_RUNS_FROM_DIRS="$UPDATE_TIME_DIRS" ./test.sh)
fi

if [ $DO_MEMORY -eq 1 ]; then
	echo ">>>>>>>>>>>>>>>>>>>>> Aggregating memory results..."
	(cd memory && INSTRUMENT_APP=0 EDFI_DRY_RUN=0 EDFI_AGGREGATE_RUNS_FROM_DIRS="$MEMORY_DIRS" ./test.sh)
fi

