#!/bin/bash


set -o errexit
set -o errtrace
set -o nounset

#
# General variables
#
EDFISTAT=$ROOT/../edfi/edfistat/edfistat.py
EDFICTL=$ROOT/edfictl/edfictl
SLOCCOUNT=sloccount
#EDFI_STAT_PIPE=/tmp/edfi.pipe
EDFI_STAT_PIPE=""
EDFI_STAT_FILE=/tmp/edfi.out
EDFI_APPS_ROOT=$ROOT/../../../apps
EDFI_LIBS_ROOT=$ROOT/../../static/edfi
EDFI_PASS_ROOT=$ROOT/../../passes/edfi
EDFI_TOOLS_ROOT=$ROOT/../../tools/edfi
MYPWD=`pwd`

EDFI_SEC_TMP=edfi-tmp
EDFI_SEC_PROB=edfi-prob
EDFI_SEC_FAULTS=edfi-faults
EDFI_SEC_CANDIDATES=edfi-candidates
EDFI_SEC_MEMORY=edfi-memory
EDFI_SEC_LOCS=edfi-locs
EDFI_SEC_MILLIS=edfi-milliseconds
EDFI_SLEEP_SECS_AFTER_INIT=6
EDFI_SLEEP_SECS_AFTER_PRINT_STATS=2

EDFI_DEFAULT_FAULT_PROB=0.5
EDFI_DEFAULT_EXP_RUNS=11
EDFI_DEFAULT_RAND_SEED=123
EDFI_INPUT_INI_SUFFIX=input.ini
EDFI_OUTPUT_INI_SUFFIX=output.ini
EDFI_INIT_OUTPUT_INI=/tmp/edfi.init.ini

EDFI_EAGAIN=14

#
# ST variables
#
MAGIC_LIBS_ROOT=$ROOT/../../static/magic
MAGIC_PASS_ROOT=$ROOT/../../passes/magic
MAGIC_INCLUDE_ROOT=$ROOT/../../include
MAGIC_SHARED_LIBS_ROOT=$ROOT/../../shared/magic
ST_TOOLS_ROOT=$ROOT/../../tools/st
STCTL=$ROOT/stctl/stctl
STCTL_ST_RAW_UPDATE_CMD=raw
STCTL_ST_UPDATE_CMD=update
STCTL_TTST_UPDATE_CMD=bttf
STCTL_REVERSED=reversed
ST_UNLIMITED_STATE_DIFF_BYTES=100000000

#
# Variables modules are responsible to initialize (or leave to default values)
#
APP_DIR=""
APP_NAME=""
DRY_RUN=""
LOGGED_RUN=""
FIRST_RUN_ID=""
LAST_RUN_ID="" # This overrides the test-specific number of EXP_RUNS specified in the test script.
RECOMPILE=""
AGGREGATE_RUNS_FROM_DIRS=""
OUTPUT_CSV=""
OUTPUT_RSD=""
OUTPUT_MATLAB=""
OUTPUT_ROOT=""
DSN_COMPAT=""
ATC_COMPAT=""
SERVERCTL_CMD_PREFIX=""
SERVERCTL_SUDO=""
AVERAGE_METRIC=""

EDFI_TEST_DIR=""
EDFI_INPUT_INI=""
EDFI_OUTPUT_INI=""

DEFAULT_APP_DIR=$EDFI_APPS_ROOT/httpd-2.2.23
DEFAULT_APP_NAME=httpd
DEFAULT_DRY_RUN=1
DEFAULT_LOGGED_RUN=1
DEFAULT_FIRST_RUN_ID=1
DEFAULT_LAST_RUN_ID=""
DEFAULT_RECOMPILE=0
DEFAULT_AGGREGATE_RUNS_FROM_DIRS=""
DEFAULT_OUTPUT_CSV=1
DEFAULT_OUTPUT_RSD=0
DEFAULT_OUTPUT_MATLAB=0
DEFAULT_OUTPUT_ROOT=0
DEFAULT_DSN_COMPAT=1
DEFAULT_ATC_COMPAT=1
DEFAULT_SERVERCTL_CMD_PREFIX=""
DEFAULT_SERVERCTL_SUDO=""
DEFAULT_AVERAGE_METRIC="median"

function log {
	echo " [**] $@"
}

function assert           #  If condition false,
{                         #+ exit from script with error message.
  E_PARAM_ERR=98
  E_ASSERT_FAILED=99


  if [ -z "$2" ]          # Not enough parameters passed.
  then
    return $E_PARAM_ERR   # No damage done.
  fi

  lineno=$2

  if [ ! $1 ] 
  then
    echo "Assertion failed:  \"$1\""
    echo "File \"$0\", line $lineno"
    exit $E_ASSERT_FAILED
  # else
  #   return
  #   and continue executing script.
  fi  
}

function ___edfi_app_pidof {
	local CMD="ps -e --sort=-start_time | grep $1 | awk '{ print \$1; }' | xargs"
	eval $CMD
}

function __edfi_app_pidof {
	local PIDS=`___edfi_app_pidof $*`
	local pid
        for pid in $PIDS
        do
        	ps -eo pid,args | grep "^[ ]*${pid} " | grep install | grep bin | awk '{ print $1; }'
        done
}

function edfi_app_pidof {
	if [ -f $APP_DIR/serverctl ]; then
		output_app_cmd "./serverctl pids"
	else
		output_app_cmd "./clientctl pids"
	fi
}

function edfi_app_pidof_one {
	edfi_app_pidof | head -1
}

function edfi_app_bin {
	if [ -f $APP_DIR/serverctl ]; then
		output_app_cmd "./serverctl bin"
		return
	fi
	output_app_cmd "./clientctl bin"
}

function run_cmd {
	if [ $LOGGED_RUN -eq 1 ]; then
		echo "    > Running '$@'..."
	fi
	if [ $DRY_RUN -eq 0 ]; then
		eval $@ || return $?
	fi
	return 0
}

function time_cmd {
	local FILTER=$1
	shift
	local MILLIS=0
	if [ $LOGGED_RUN -eq 1 ]; then
		echo "    > Running 'time ($@ &> /tmp/time_cmd.out) 2> /tmp/time_cmd.time'..."
	fi
	if [ $DRY_RUN -eq 0 ]; then
		RET=0
		echo > /tmp/time_cmd.out
		time ($@ &> /tmp/time_cmd.out) 2> /tmp/time_cmd.time || RET=1
		if [ $RET -eq 1 ]; then
			echo "      > Command $@ failed, output was: `cat /tmp/time_cmd.out`"
			return 1
		fi
		log "time_cmd reported: "
		cat /tmp/time_cmd.time
		local OUTPUT=`cat /tmp/time_cmd.time | grep $FILTER | awk '{ print $2; }' | sed "s/s//g" | sed "s/[m.]/ /g"`
		local MINUTES=`echo $OUTPUT | awk '{ print $1; }'`
		local SECONDS=`echo $OUTPUT | awk '{ print $2; }'`
		MILLIS=`echo $OUTPUT | awk '{ print $3; }' | sed "s/^0*//g"`
		MILLIS=$(( $MINUTES*60*1000 + $SECONDS*1000 + $MILLIS ))
	fi
	TIME_CMD_RET=$MILLIS
	return 0
}

function read_pipe_timeout {
    local SECONDS=$1
    while read -t $SECONDS; do echo $REPLY; done
}

function output_app_cmd {
	RET=0
	cd $APP_DIR &> /dev/null
	eval "$@" || RET=1
	cd $MYPWD &> /dev/null
	return $RET
}

function run_app_cmd {
	RET=0
	run_cmd "cd $APP_DIR"
	run_cmd "$@" || RET=1
	run_cmd "cd $MYPWD"
	return $RET
}

function time_app_cmd {
	FILTER=$1
	shift
	RET=0
	run_cmd "cd $APP_DIR"
	time_cmd $FILTER "$@" || RET=1
	run_cmd "cd $MYPWD"
	return $RET
}

function get_sloccount {
	local TARGET=$1
	local LANGUAGE=$2
	$SLOCCOUNT $TARGET | grep "^$LANGUAGE" | awk '{ print $2; }'
}

function get_functions_from_libs {
	for l in $@
	do
		if [ $DRY_RUN -eq 1 ]; then
			(nm -a $l | grep " T " | awk '{ print $3; }' | head -1) 2> /dev/null
		else
			(nm -a $l | grep " T " | awk '{ print $3; }') 2> /dev/null
		fi
	done
}

function common_test_dir_init {
	run_cmd "rm -rf $1/* 2> /dev/null"
	run_cmd "mkdir -p $1/procs"
	run_cmd "mkdir -p $1/runs"
	run_cmd "mkdir -p $1/exps"
}

function edfi_get_fault_function_callers {
	local IS_FIRST=1
	for a in $@
	do
		if [ $IS_FIRST -eq 1 ]; then
			echo -n "$a/1.0"
			IS_FIRST=0
		else
			echo -n ":$a/1.0"
		fi
	done
}

function edfi_get_num_bb_from_functions {
	local TMP_INI=/tmp/__test.ini
	local FAULT_FUNCTION_CALLERS=`edfi_get_fault_function_callers $@`
	local RET=0
	rm -f $TMP_INI
	(EXIT_AFTER_OPT=1 edfi_instrument_app $TMP_INI -fault-statistics-only=1 -fault-noDFTs=1 -fault-functioncallers-fif=$FAULT_FUNCTION_CALLERS -fault-global-fif=0 -fault-prob-default=1.0) > /dev/null
	if [ -e $TMP_INI ]; then
		RET=`edfi_get_ini_value $TMP_INI n_functioncallers_basicblocks`
		assert "$RET" != "" $LINENO
	fi
	rm -f $TMP_INI
	echo $RET
}

function edfi_get_binaries_size {
	local BINARIES=$@
	local SIZE=0
	for b in $BINARIES
	do
		strip $b
		SIZE=$(($SIZE + `stat -c%s $b`))
	done
	echo $SIZE
}

function edfi_get_binaries_size_from_dir {
	edfi_get_binaries_size `find $1 -name \*.o`
}

function edfi_get_static_app_size {
	local APP_BINARY=`edfi_app_bin`
	local APP_SIZE=`edfi_get_binaries_size $APP_BINARY`
	echo $APP_SIZE
}

function edfi_get_dynamic_app_size {
	local HAS_PID=0
	local SIZE=0
	local PIDS=`edfi_app_pidof $APP_NAME | xargs`
	echo "edfi_get_dynamic_app_size: Analyzing pids $PIDS" 1>&2
	for pid in $PIDS
	do
		if [ "$MEM_RSS" != "1" ]; then
			local PROC_SIZE=$(( `pidstat -r -p $pid 1 1 | grep Average | awk '{ print $5; }'` * 1024 ))
		else
			local PROC_SIZE=$(( `pidstat -r -p $pid 1 1 | grep Average | awk '{ print $6; }'` * 1024 ))
		fi
		if [ "$MULTIPROC_PESSIMISTIC" != "1" ]; then
		    if [ "$PROC_SIZE" -gt "$SIZE" ]; then
		        SIZE=$PROC_SIZE
		    fi
	        else
		    SIZE=$(( $SIZE + $PROC_SIZE ))
		fi
		HAS_PID=1
	done
	assert "$HAS_PID == 1" $LINENO
	echo $SIZE
}

function edfi_wait_for_pid {
	log "Waiting for pid ${1} to exit..."
	while [ "`ps -e | grep \"^[ ]*${1} \"`" != "" ]
	do
		sleep $2
	done
	log "Done."
}

function edfi_get_avg_dynamic_app_size {
	local WAIT_FOR_PID=$1
	local SECTION=memory
	local EDFI_MEMORY_FILES=""
	local TMP_FILE=/tmp/pidstat.tmp
	local HAS_PID=0
	local MY_PID_FILES=""
	for pid in `edfi_app_pidof $APP_NAME`
	do
		(
			pidstat -r -p $pid 1 1000000 > $TMP_FILE.${pid}
		) &
		MY_PID_FILES="$MY_PID_FILES $TMP_FILE.${pid}"
		HAS_PID=1
	done
	assert "$HAS_PID == 1" $LINENO
	sleep 5
	edfi_wait_for_pid $WAIT_FOR_PID 5
	killall -9 pidstat
	local AVG_VM_SIZE=0
	for f in $MY_PID_FILES
	do
		if [ "`cat $f | grep $APP_NAME`" != "" ]; then
			local PROC_AVG_VM_SIZE=$(( `cat $f | grep $APP_NAME | awk '{a+=$6} END{print a/NR}' | sed "s/\..*//g"` * 1024 ))
			if [ "$PROC_AVG_VM_SIZE" -gt "$AVG_VM_SIZE" ]; then
			    AVG_VM_SIZE=$PROC_AVG_VM_SIZE
			fi
		fi
	done
	EDFI_GET_AVG_DYNAMIC_APP_SIZE_RET=$AVG_VM_SIZE
	rm -f $TMP_FILE
}

function common_get_dynamic_app_size {
	local PID=`edfi_app_pidof_one $APP_NAME | xargs`
	local PIDS=$( output_app_cmd "./serverctl ${PID}-h" )
	local PIDS2=$( output_app_cmd "./serverctl pids" )
	echo -e "common_get_dynamic_app_size: Analyzing pid ($PID) hierarchy:\n$PIDS" 1>&2
	echo -e "common_get_dynamic_app_size: Original pids:\n$PIDS2" 1>&2
	local PROC_MEMS=$( output_app_cmd "./serverctl info mem-ht $PID | grep '^[ ]*[0-9]'" )
	COMMON_GET_DYNAMIC_APP_SIZE_NUM_PROCS=$( echo $PROC_MEMS | awk '{ print $1; }' )
	COMMON_GET_DYNAMIC_APP_SIZE_VM_SIZE=$( echo $PROC_MEMS | awk '{ print $2; }' )
	COMMON_GET_DYNAMIC_APP_SIZE_RSS_SIZE=$( echo $PROC_MEMS | awk '{ print $3; }' )
	COMMON_GET_DYNAMIC_APP_SIZE_PSS_SIZE=$( echo $PROC_MEMS | awk '{ print $4; }' )
}

function common_get_avg_dynamic_app_size {
	local WAIT_FOR_PID=$1
	local TMP_FILE=__tmp.mem
	local PID=`edfi_app_pidof_one $APP_NAME | xargs`
	local PIDS=$( output_app_cmd "./serverctl ${PID}-h" )
	local SLEEP_MS=200
	echo -e "common_get_avg_dynamic_app_size: Analyzing pid ($PID) hierarchy...\n$PIDS" 1>&2
	echo " *** Waiting for pid $WAIT_FOR_PID to complete..."
	output_app_cmd "./serverctl info mem-ht $PID &> $TMP_FILE; sleep 0.${SLEEP_MS}; while kill -0 $WAIT_FOR_PID &> /dev/null; do ./serverctl info mem-ht $PID &>> $TMP_FILE && sleep 0.${SLEEP_MS}; done; sleep 1; ./serverctl info mem-ht $PID &>> $TMP_FILE"
	local OUTPUT=$( cat $APP_DIR/$TMP_FILE  | grep '^[ ]*[0-9]' )
	local OUTPUT_SAMPLES=$( echo "$OUTPUT" | wc -l )
	echo "common_get_avg_dynamic_app_size: Output (samples=$OUTPUT_SAMPLES) without duplicates is:" 1>&2
	echo "$OUTPUT" | sort | uniq 1>&2

	COMMON_GET_AVG_DYNAMIC_APP_SIZE_AVG_NUM_PROCS=$( echo "$OUTPUT" | awk '{a+=$1} END{print a/NR}'  )
	COMMON_GET_AVG_DYNAMIC_APP_SIZE_AVG_VM_SIZE=$( echo "$OUTPUT" | awk '{a+=$2} END{print a/NR}' )
	COMMON_GET_AVG_DYNAMIC_APP_SIZE_AVG_RSS_SIZE=$( echo "$OUTPUT" | awk '{a+=$3} END{print a/NR}' )
	COMMON_GET_AVG_DYNAMIC_APP_SIZE_AVG_PSS_SIZE=$( echo "$OUTPUT" | awk '{a+=$4} END{print a/NR}' )
	COMMON_GET_AVG_DYNAMIC_APP_SIZE_MAX_NUM_PROCS=$( echo "$OUTPUT" |  awk 'BEGIN {max = 0} {if ($1>max) max=$1} END {print max}' )
	COMMON_GET_AVG_DYNAMIC_APP_SIZE_MAX_VM_SIZE=$( echo "$OUTPUT" | awk 'BEGIN {max = 0} {if ($2>max) max=$2} END {print max}' )
	COMMON_GET_AVG_DYNAMIC_APP_SIZE_MAX_RSS_SIZE=$( echo "$OUTPUT" | awk 'BEGIN {max = 0} {if ($3>max) max=$3} END {print max}' )
	COMMON_GET_AVG_DYNAMIC_APP_SIZE_MAX_PSS_SIZE=$( echo "$OUTPUT" | awk 'BEGIN {max = 0} {if ($4>max) max=$4} END {print max}' )

	rm -f $TMP_FILE
}

function edfi_get_total_num_bb {
	local TMP_INI=/tmp/__test.ini
	local RET=0
	rm -f $TMP_INI
	(EXIT_AFTER_OPT=1 edfi_instrument_app $TMP_INI -fault-statistics-only=1 -fault-noDFTs=1 -fault-global-fif=0 -fault-prob-default=0) > /dev/null
	if [ -e $TMP_INI ]; then
		RET=`edfi_get_ini_value $TMP_INI n_basicblocks`
		assert "$RET" != "" $LINENO
	fi
	rm -f $TMP_INI
	echo $RET
}

function edfi_instrument_app {
	local PASS_INI=$1
	local SAVED_DRY_RUN=$DRY_RUN
	if [ "$AGGREGATE_RUNS_FROM_DIRS" != "" ]; then
		DRY_RUN=1
	fi
	shift
	log "Instrumenting app '$APP_DIR'..."
	run_app_cmd "LLVM_PASS_ARGS=\"$@ -fault-dsn-compat=$DSN_COMPAT -fault-atc-compat=$ATC_COMPAT\" ./build.llvm edfi > $PASS_INI" || exit 1
	DRY_RUN=$SAVED_DRY_RUN
}

function st_instrument_app {
	local PASS_INI=$1
	local SAVED_DRY_RUN=$DRY_RUN
	if [ "$AGGREGATE_RUNS_FROM_DIRS" != "" ]; then
		DRY_RUN=1
	fi
	shift
	log "Instrumenting app '$APP_DIR'..."
	run_app_cmd "LLVM_PASS_ARGS=\"$@\" LLVM_NOMEMPOOL=1 ./serverctl buildst > $PASS_INI" || exit 1
	DRY_RUN=$SAVED_DRY_RUN
}

function edfi_st_instrument_app {
	local PASS_INI=$1
	local SAVED_DRY_RUN=$DRY_RUN
	if [ "$AGGREGATE_RUNS_FROM_DIRS" != "" ]; then
		DRY_RUN=1
	fi
	shift
	local SECTIONS="-magic-data-sections=^magic_data.*\$:^edfi_data\$ -magic-function-sections=^magic_functions.*\$:^edfi_functions\$"
	local FAULT_PLACEMENT="-fault-skip-functions=magic_init -fault-modules=\/magic\/"
	log "Instrumenting app '$APP_DIR'..."
	run_app_cmd "LLVM_PASS_ARGS=\"$@ -fault-atc-compat=$ATC_COMPAT $SECTIONS $FAULT_PLACEMENT\" ./build.llvm magic edfi > $PASS_INI" || exit 1
	DRY_RUN=$SAVED_DRY_RUN
}

function common_instrument_app {
	local PASS_INI=$1
	local SAVED_DRY_RUN=$DRY_RUN
	if [ "$AGGREGATE_RUNS_FROM_DIRS" != "" ]; then
		DRY_RUN=1
	fi
	shift
	log "Instrumenting app '$APP_DIR'..."
	run_app_cmd "./build.llvm $@ > $PASS_INI" || exit 1
	DRY_RUN=$SAVED_DRY_RUN
}

function edfi_start_app {
	local RUN_ID=$1
	shift
	if [ ! -f $APP_DIR/serverctl ]; then
		return 0
	fi
	if [ "$EDFI_STAT_PIPE" == "" ]; then
		log "Using EDFI_STAT_FILE=${EDFI_STAT_FILE}.*..."
		RET=1
		while [ $RET -eq 1 ]
		do
			if [ "$1" == "NULL" ]; then
				run_app_cmd "$SERVERCTL_SUDO $SERVERCTL_CMD_PREFIX ST_QINIT=0 EDFI_RAND_SEED=$RUN_ID ./serverctl frestart"
			else
				run_app_cmd "$SERVERCTL_SUDO $SERVERCTL_CMD_PREFIX EDFI_NUM_REQUESTS_ON_START=1 ./serverctl frestart &"
				while ! run_cmd "$EDFICTL $@ `edfi_app_pidof_one $APP_NAME` &> /dev/null"; do sleep 1; done
			fi
			sleep $EDFI_SLEEP_SECS_AFTER_INIT
			RET=0
			FIRST_APP_PID=`edfi_app_pidof_one $APP_NAME`
			if [ "$FIRST_APP_PID" == "" ]; then
			   RET=1
			else
			   #run_cmd "cp ${EDFI_STAT_FILE}.${FIRST_APP_PID} $EDFI_INIT_OUTPUT_INI" || RET=1
			   log "Initialization done"
			fi
			if [ $RET -eq 1 ]; then
				log "edfi_start_app failed, trying again..."
				if [ -f $APP_DIR/serverctl ]; then
					run_app_cmd "$SERVERCTL_SUDO ./serverctl fix"
					run_app_cmd "$SERVERCTL_SUDO $SERVERCTL_CMD_PREFIX ./serverctl frestart"
				fi
			fi
		done
	else
		log "Using EDFI_STAT_PIPE=${EDFI_STAT_PIPE} and EDFI_INIT_OUTPUT_INI=${EDFI_INIT_OUTPUT_INI}..."
		run_cmd "rm -f $EDFI_STAT_PIPE"
		run_cmd "mkfifo $EDFI_STAT_PIPE"
		if [ "$1" == "NULL" ]; then
			run_app_cmd "$SERVERCTL_SUDO $SERVERCTL_CMD_PREFIX ./serverctl frestart > $EDFI_STAT_PIPE &"
		else
			run_app_cmd "$SERVERCTL_SUDO $SERVERCTL_CMD_PREFIX EDFI_NUM_REQUESTS_ON_START=1 ./serverctl frestart > $EDFI_STAT_PIPE &"
			while ! run_cmd "$EDFICTL $@ `edfi_app_pidof_one $APP_NAME` &> /dev/null"; do sleep 1; done
		fi
		run_cmd "read_pipe_timeout $EDFI_SLEEP_SECS_AFTER_INIT < $EDFI_STAT_PIPE > $EDFI_INIT_OUTPUT_INI"
	fi
}

function edfi_stop_app {
	if [ ! -f $APP_DIR/serverctl ]; then
		return 0
	fi
	run_app_cmd "$SERVERCTL_SUDO ./serverctl stop"
	if [ "$EDFI_STAT_PIPE" != "" ]; then
		run_cmd "rm -f $EDFI_STAT_PIPE"
	fi
}

function clientctl_connect {
	run_app_cmd "RUNCLIENT_CONNECT=1 ./clientctl run"
}

function spawn_connection {
	{ coproc clientctl_connect >&3 ; } 3> $APP_DIR/clientctl_connect.log 2>/dev/null
}

function edfi_ctl {
	local HAS_PID=0
	for pid in `edfi_app_pidof $APP_NAME`
	do
		run_cmd "$EDFICTL $@ $pid"
		HAS_PID=1
	done
	assert "$HAS_PID == 1" $LINENO
}

function st_ctl {
	local PID=`edfi_app_pidof_one $APP_NAME`
	assert "$PID != \"\"" $LINENO
	run_cmd "$STCTL $@ $PID"
}

function edfi_rename_ini {
	echo "[$2]"
	edfi_strip_ini $1 ""
}

function edfi_strip_ini {
	local file=$1
	local prefix=$2
	while read l; do
		echo "${prefix}${l}"
	done < $file | grep -v "\[" 
}

function edfi_merge_stripped_inis {
	echo "[$1]"
	shift
	for f in $@
	do
		cat $f
	done
	rm -f $@
}

function edfi_merge_ini {
	for f in $@
	do
		cat $f
		echo ""
	done
	rm -f $@
}

function edfi_build_ini {
	local SECTION=$1
	shift
	echo "[$SECTION]"
	for a in $@
	do
		echo $a | sed "s/=/ = /g"
	done
}

function edfi_get_ini_value {
	local INI_FILE=$1
	local INI_KEY=$2
	cat $INI_FILE | grep -w ^$INI_KEY | awk '{ print $3; }'
}

function edfi_set_ini_value {
	local INI_FILE=$1
	local INI_KEY=$2
	local INI_VALUE=$3
	sed -i "s/$INI_KEY[ ]?=[ ]?.*/$INI_KEY = $INI_VALUE/" $INI_FILE
}

function edfi_ctl_get_pid_stats {
	local EDFI_CTL_COMMAND=$1
	local PID=$2
	local OUTPUT_FILE=$3
	if [ "$EDFI_STAT_PIPE" == "" ]; then
		run_cmd "$EDFICTL $EDFI_CTL_COMMAND $PID" || return 1
		run_cmd "cp ${EDFI_STAT_FILE}.${PID} $OUTPUT_FILE"
	else
		run_cmd "$EDFICTL $EDFI_CTL_COMMAND $PID &" || return 1
		run_cmd "read_pipe_timeout $EDFI_SLEEP_SECS_AFTER_PRINT_STATS < $EDFI_STAT_PIPE > $OUTPUT_FILE" || return 1
	fi
	return 0
}

function edfi_ctl_get_stats {
	local EDFI_CTL_COMMAND=$1
	local OUTPUT_FILE=$2
	local EDFI_PROB_FILES=""
	local EDFI_FAULTS_FILES=""
	local EDFI_CANDIDATES_FILES=""
	local IS_MULTI_PROC=0
	local FIRST_OUTPUT_FILE=""
	local HAS_PID=0
	for pid in `edfi_app_pidof $APP_NAME`
	do
		edfi_ctl_get_pid_stats $EDFI_CTL_COMMAND $pid ${OUTPUT_FILE}.${pid} || return 1
		if [ "$EDFI_PROB_FILES" == "" ]; then
			FIRST_OUTPUT_FILE=${OUTPUT_FILE}.${pid}
			EDFI_PROB_FILES=${OUTPUT_FILE}.${pid}@${EDFI_SEC_PROB}
			EDFI_FAULTS_FILES=${OUTPUT_FILE}.${pid}@${EDFI_SEC_FAULTS}
			EDFI_CANDIDATES_FILES=${OUTPUT_FILE}.${pid}@${EDFI_SEC_CANDIDATES}
		else
			IS_MULTI_PROC=1
			EDFI_PROB_FILES=${EDFI_PROB_FILES}:${OUTPUT_FILE}.${pid}@${EDFI_SEC_PROB}
			EDFI_FAULTS_FILES=${EDFI_FAULTS_FILES}:${OUTPUT_FILE}.${pid}@${EDFI_SEC_FAULTS}
			EDFI_CANDIDATES_FILES=${EDFI_CANDIDATES_FILES}:${OUTPUT_FILE}.${pid}@${EDFI_SEC_CANDIDATES}
		fi
		HAS_PID=1
	done
	assert "$HAS_PID == 1" $LINENO
	if [ $IS_MULTI_PROC -eq 1 ]; then
		run_cmd "$EDFISTAT $EDFI_SEC_TMP $EDFI_FAULTS_FILES sum > $OUTPUT_FILE.faults.tmp" || return 1
		run_cmd "$EDFISTAT $EDFI_SEC_TMP $EDFI_CANDIDATES_FILES sum > $OUTPUT_FILE.candidates.tmp" || return 1
		run_cmd "$EDFISTAT $EDFI_SEC_PROB $OUTPUT_FILE.faults.tmp@${EDFI_SEC_TMP}:$OUTPUT_FILE.candidates.tmp@${EDFI_SEC_TMP} div > $OUTPUT_FILE.prob" || return 1
		run_cmd "sed \"s/$EDFI_SEC_TMP/$EDFI_SEC_FAULTS/g\" $OUTPUT_FILE.faults.tmp > $OUTPUT_FILE.faults"
		run_cmd "sed \"s/$EDFI_SEC_TMP/$EDFI_SEC_CANDIDATES/g\" $OUTPUT_FILE.candidates.tmp > $OUTPUT_FILE.candidates"
		run_cmd "edfi_merge_ini $OUTPUT_FILE.prob $OUTPUT_FILE.faults $OUTPUT_FILE.candidates > $OUTPUT_FILE"
		run_cmd "rm -f $OUTPUT_FILE.*.tmp"
	else
		run_cmd "cp $FIRST_OUTPUT_FILE $OUTPUT_FILE"
	fi
	return 0
}

function edfi_run_experiment {
	log "--> Experiment with '$@'..."
	local OUTPUT_FILE=$1
	local CALLBACK=$2
	local RUN_ID=$3
	local INPUT_FILE=$4
	local NUM_CONNECTIONS=0
	shift; shift; shift; shift
	while [ 1 ];
	do
		edfi_start_app $RUN_ID $@
		for i in $(seq 1 $NUM_CONNECTIONS); do
		    spawn_connection
		done
		EDFI_RUN_EXP_RET=0
		$CALLBACK $OUTPUT_FILE $RUN_ID $INPUT_FILE || EDFI_RUN_EXP_RET=$?
		edfi_stop_app
		if [ $EDFI_RUN_EXP_RET -eq 0 ]; then
			break
		fi
		if [ $EDFI_RUN_EXP_RET -eq $EDFI_EAGAIN ]; then
			log "$CALLBACK returned EAGAIN, fixing the problem and retrying..."
			if [ -f $APP_DIR/serverctl ]; then
				run_app_cmd "$SERVERCTL_SUDO ./serverctl fix || true"
			fi
		else
			log "$CALLBACK failed, aborting..."
			exit 1
		fi
	done
	log "<-- Experiment done."
}

function edfi_average_experiment_runs {
	local SECTION="$1"
	local EDFI_FILES="$2"
	local RESULT_FILE="$3"
	local TMP_FILE=/tmp/__edfi_average_experiment_runs.tmp

	echo "$EDFI_FILES" > $TMP_FILE
	run_cmd "$EDFISTAT $SECTION file://$TMP_FILE $AVERAGE_METRIC > $RESULT_FILE"
	if [ $OUTPUT_CSV -eq 1 ]; then
		run_cmd "$EDFISTAT $SECTION file://$TMP_FILE csv > $RESULT_FILE.csv"
	fi
	if [ $OUTPUT_RSD -eq 1 ]; then
		run_cmd "$EDFISTAT $SECTION file://$TMP_FILE rsd > $RESULT_FILE.rsd"
		run_cmd "$EDFISTAT $SECTION $RESULT_FILE.rsd@$SECTION average > $RESULT_FILE.rsd.avg"
	fi
	if [ $OUTPUT_MATLAB -eq 1 ]; then
		run_cmd "$EDFISTAT $SECTION file://$TMP_FILE matlab > $RESULT_FILE.m"
	fi
	if [ $OUTPUT_ROOT -eq 1 ]; then
		run_cmd "$EDFISTAT $SECTION file://$TMP_FILE root > $RESULT_FILE.root"
		run_cmd "$EDFISTAT $SECTION file://$TMP_FILE rootm > $RESULT_FILE.rootm"
	fi
	rm -f $TMP_FILE
}

function __edfi_aggregated_runs_to_files {
	local SECTION=$1
	local RUN_FILE=$2
	for d in `echo $AGGREGATE_RUNS_FROM_DIRS | sed "s/:/ /g"`
	do
		FIND_PATTERN=`echo ${RUN_FILE}.\* | perl -pe "s/^.*?results\///"`
		echo "`find \"${d}\" -wholename \"${d}/${FIND_PATTERN}\" | grep -v runbench | grep [0-9]$`"
	done
}

function edfi_aggregated_runs_to_files {
	echo "`__edfi_aggregated_runs_to_files $@ | grep -v \"^\$\" | sed \"s/\$/@$SECTION/g\"`"
}

function edfi_run_repeated_experiment {
	log "--> Repeated experiment with '$@'..."
	local EXP_RUNS=$1
	local SECTION=$2
	local PRE_CALLBACK=$3
	local EXP_CALLBACK=$4
	local IS_MULTI_EXP_RUNS=0
	local FIRST_RUN_INPUT_FILE=""
	local FIRST_RUN_OUTPUT_FILE=""
	local INPUT_FILE=$EDFI_TEST_DIR/exps/$EDFI_INPUT_INI
	local OUTPUT_FILE=$EDFI_TEST_DIR/exps/$EDFI_OUTPUT_INI
	local RUN_INPUT_FILE=$EDFI_TEST_DIR/runs/$EDFI_INPUT_INI
	local RUN_OUTPUT_FILE=$EDFI_TEST_DIR/runs/$EDFI_OUTPUT_INI
	shift; shift; shift; shift
	local EDFI_INPUT_FILES=""
	local EDFI_OUTPUT_FILES=""
	local PRE_CALLBACK_GENERATES_INPUT_FILE=0
	local EXP_CALLBACK_GENERATES_OUTPUT_FILE=0
	local RUN_ID=$FIRST_RUN_ID
	#
	# See if we have to override the predetermined number of EXP_RUNS
	#
	if [ "$LAST_RUN_ID" != "" ]; then
		EXP_RUNS=$(( $LAST_RUN_ID - $FIRST_RUN_ID + 1))
		assert "$EXP_RUNS -gt 0" $LINENO
		log "Overriding experiment runs with EXP_RUNS=$EXP_RUNS, FIRST_RUN_ID=$FIRST_RUN_ID, LAST_RUN_ID=$LAST_RUN_ID..."
	fi
	#
	# See if we have to aggregated existing run files, instead of running actual experiments.
	#
	if [ "$AGGREGATE_RUNS_FROM_DIRS" != "" ]; then
		EDFI_INPUT_FILES="`edfi_aggregated_runs_to_files $SECTION $RUN_INPUT_FILE`"
		EDFI_OUTPUT_FILES="`edfi_aggregated_runs_to_files $SECTION $RUN_OUTPUT_FILE`"
		EDFI_NUM_INPUT_FILES=`grep -o "@" <<<"$EDFI_INPUT_FILES" | wc -l` 
		EDFI_NUM_OUTPUT_FILES=`grep -o "@" <<<"$EDFI_OUTPUT_FILES" | wc -l`
		if [ $EDFI_NUM_INPUT_FILES -gt 1 ]; then
			edfi_average_experiment_runs $SECTION "$EDFI_INPUT_FILES" $INPUT_FILE
		elif [ $EDFI_NUM_INPUT_FILES -eq 1 ]; then
			run_cmd "cp `echo $EDFI_INPUT_FILES | sed \"s/@.*//g\"` $INPUT_FILE"
		fi
		if [ $EDFI_NUM_OUTPUT_FILES -gt 1 ]; then
			edfi_average_experiment_runs $SECTION "$EDFI_OUTPUT_FILES" $OUTPUT_FILE
		elif [ $EDFI_NUM_OUTPUT_FILES -eq 1 ]; then
			run_cmd "cp `echo $EDFI_OUTPUT_FILES | sed \"s/@.*//g\"` $OUTPUT_FILE"
		fi
		log "<-- Repeated experiment done."
		return 0
	fi
	for i in `seq $EXP_RUNS`
	do
		PRE_CALLBACK_GENERATES_INPUT_FILE=0
		EXP_CALLBACK_GENERATES_OUTPUT_FILE=0
		if [ "$PRE_CALLBACK" != "NULL" ]; then
			$PRE_CALLBACK ${RUN_INPUT_FILE}.${RUN_ID} $RUN_ID || PRE_CALLBACK_GENERATES_INPUT_FILE=$?
		fi
		if [ "$EXP_CALLBACK" != "NULL" ]; then
			edfi_run_experiment ${RUN_OUTPUT_FILE}.${RUN_ID} $EXP_CALLBACK $RUN_ID ${RUN_INPUT_FILE}.${RUN_ID} $@
			EXP_CALLBACK_GENERATES_OUTPUT_FILE=1
		fi
		if [ "$EDFI_OUTPUT_FILES" == "" ]; then
			EDFI_INPUT_FILES=${RUN_INPUT_FILE}.${RUN_ID}@${SECTION}
			EDFI_OUTPUT_FILES=${RUN_OUTPUT_FILE}.${RUN_ID}@${SECTION}
			FIRST_RUN_INPUT_FILE=${RUN_INPUT_FILE}.${RUN_ID}
			FIRST_RUN_OUTPUT_FILE=${RUN_OUTPUT_FILE}.${RUN_ID}
		else
			EDFI_INPUT_FILES=${EDFI_INPUT_FILES}:${RUN_INPUT_FILE}.${RUN_ID}@${SECTION}
			EDFI_OUTPUT_FILES=${EDFI_OUTPUT_FILES}:${RUN_OUTPUT_FILE}.${RUN_ID}@${SECTION}
			IS_MULTI_EXP_RUNS=1
		fi
		RUN_ID=$(($RUN_ID + 1))
	done
	if [ $IS_MULTI_EXP_RUNS -eq 1 ]; then
		if [ $PRE_CALLBACK_GENERATES_INPUT_FILE -eq 1 ]; then
			edfi_average_experiment_runs $SECTION "`echo $EDFI_INPUT_FILES | sed 's/:/\n/g'`" $INPUT_FILE
		fi
		if [ $EXP_CALLBACK_GENERATES_OUTPUT_FILE -eq 1 ]; then
			edfi_average_experiment_runs $SECTION "`echo $EDFI_OUTPUT_FILES | sed 's/:/\n/g'`" $OUTPUT_FILE
		fi
	else
		if [ $PRE_CALLBACK_GENERATES_INPUT_FILE -eq 1 ]; then
			run_cmd "cp $FIRST_RUN_INPUT_FILE $INPUT_FILE"
		fi
		if [ $EXP_CALLBACK_GENERATES_OUTPUT_FILE -eq 1 ]; then
			run_cmd "cp $FIRST_RUN_OUTPUT_FILE $OUTPUT_FILE"
		fi
	fi
	log "<-- Repeated experiment done."
}

function edfi_representativeness_exp_cb {
	log "--> Representativeness experiment callback for $APP_NAME with '$@'..."
	local OUTPUT_FILE=$1
	edfi_ctl start
	run_app_cmd "./runbench &> /dev/null" || return $EDFI_EAGAIN
	edfi_ctl_get_stats stop $EDFI_TEST_DIR/procs/`basename $OUTPUT_FILE` || return $EDFI_EAGAIN
	run_cmd "cp $EDFI_TEST_DIR/procs/`basename $OUTPUT_FILE` $OUTPUT_FILE"
	run_cmd "cp $APP_DIR/runbench.log ${OUTPUT_FILE}.runbench.log"
	log "<-- Representativeness experiment callback done."
	return 0
}

function edfi_controllability_exp_cb {
	log "--> Controllability experiment callback for $APP_NAME with '$@'..."
	local OUTPUT_FILE=$1
	edfi_ctl_get_stats stop $EDFI_TEST_DIR/procs/`basename $OUTPUT_FILE` || return $EDFI_EAGAIN
	run_cmd "cp $EDFI_TEST_DIR/procs/`basename $OUTPUT_FILE` $OUTPUT_FILE"
	log "<-- Controllability experiment callback done."
	return 0
}

function edfi_memory_exp_cb {
	log "--> Memory experiment callback for $APP_NAME with '$@'..."
	local OUTPUT_FILE=$1
	local INPUT_FILE=$3
	local STATIC_SIZE=0
	local IDLE_SIZE=0
	local EXP_START_SIZE=0
	local EXP_AVG_SIZE=0
	if [ -e $INPUT_FILE ]; then
		STATIC_SIZE=`edfi_get_ini_value $INPUT_FILE static`
		if grep -q total_static_data_size $EDFI_INIT_OUTPUT_INI; then
			TABLE_SIZE=`edfi_get_ini_value $EDFI_INIT_OUTPUT_INI total_static_data_size | head -1`
			echo ">>>>>>> Found total_static_data_size=$TABLE_SIZE" 2>&1
			STATIC_SIZE=$(( $STATIC_SIZE - $TABLE_SIZE ))
		fi
	fi
	DFL_SIZE=`edfi_get_binaries_size_from_dir $EDFI_LIBS_ROOT/df`
	if [ $DRY_RUN -eq 0 ]; then
		IDLE_SIZE=`edfi_get_dynamic_app_size`
		echo ">>>>>>> Found IDLE_SIZE=$IDLE_SIZE, DFL_SIZE=$DFL_SIZE" 2>&1
		IDLE_SIZE=$(( $IDLE_SIZE - $DFL_SIZE ))
	fi
	EXP_START_SIZE=$(( $IDLE_SIZE + $DFL_SIZE ))
	run_app_cmd "./runbench &> /dev/null &"
	RUNBENCH_PID=`(ps -e --sort=start_time | grep runbench | head -1 | awk '{ print $1; }') || true`
	if [ $DRY_RUN -eq 0 ]; then
		if [ "$RUNBENCH_PID" == "" ]; then
			log "Failed to retrieve RUNBENCH_PID..."
			return $EDFI_EAGAIN
		fi
		edfi_get_avg_dynamic_app_size $RUNBENCH_PID
		EXP_AVG_SIZE=$(( $EDFI_GET_AVG_DYNAMIC_APP_SIZE_RET - $DFL_SIZE ))
	fi
	run_cmd "edfi_build_ini $EDFI_SEC_MEMORY static=$STATIC_SIZE idle=$IDLE_SIZE exp_start=$EXP_START_SIZE exp_avg=$EXP_AVG_SIZE > $OUTPUT_FILE"
	run_cmd "cp $APP_DIR/runbench.log ${OUTPUT_FILE}.runbench.log"
	log "<-- Memory experiment callback done."
	return 0
}

function edfi_performance_exp_cb {
	log "--> Performance experiment callback for $APP_NAME and workload $RUNBENCH_WORKLOAD with '$@'..."
	local OUTPUT_FILE=$1
	TIME_CMD_RET=0
	time_app_cmd real "./runbench" || return $EDFI_EAGAIN
	run_cmd "edfi_build_ini $EDFI_SEC_MILLIS benchtime=$TIME_CMD_RET > $OUTPUT_FILE"
	run_cmd "cp $APP_DIR/runbench.log ${OUTPUT_FILE}.runbench.log"
	log "<-- Performance experiment callback done."
	return 0
}

function st_wait_for_monitor_thread {
	log "Waiting for monitor thread to become alive..."
	run_cmd "st_ctl ping"
	log "Monitor thread ready..."
	sleep 1
}

function st_update_time_exp_cb {
	log "--> Update time experiment callback for $APP_NAME with '$@'..."
	local OUTPUT_FILE=$1
	local APP_BINARY=`edfi_app_bin`
	local PID=`edfi_app_pidof_one $APP_NAME`
	local TMP_FILE=/tmp/__update_time.tmp
	TIME_CMD_RET=0
	st_wait_for_monitor_thread
	rm -f $TMP_FILE && touch $TMP_FILE
	for i in `seq $ST_UPDATE_RUNS`
	do
	    sleep 1
	    time_cmd real "st_ctl --always_rollback --new_version=$APP_BINARY --allow-state-diff-bytes=$ST_UNLIMITED_STATE_DIFF_BYTES $ST_UPDATE_TIME_EXP_CB_ARGS" || return $EDFI_EAGAIN
	    echo $TIME_CMD_RET >> $TMP_FILE
	done
	local BENCH_TIME=`cat $TMP_FILE | sort -n | awk '{arr[NR]=$1} END { if (NR%2==1) print arr[(NR+1)/2]; else print (arr[NR/2]+arr[NR/2+1])/2}'`
	run_cmd "edfi_build_ini $EDFI_SEC_MILLIS benchtime=$BENCH_TIME > $OUTPUT_FILE"
    if [ $ST_NO_LOG != 1 ]; then
        run_cmd "cp $APP_DIR/.tmp/st.log.${PID} ${OUTPUT_FILE}_st.log"
    fi
    run_cmd "rm -f $APP_DIR/.tmp/*"
	log "<-- Update time experiment callback done."
	rm -f $TMP_FILE
	return 0
}

function st_fi_exp_cb {
	log "--> Fault injection experiment callback for $APP_NAME with '$@'..."
	local OUTPUT_FILE=$1
	local APP_BINARY=`edfi_app_bin`
	local PID=`edfi_app_pidof_one $APP_NAME`
	TIME_CMD_RET=0
	st_wait_for_monitor_thread
	local ST_CTL_RET=0
	local ST_CTL_RET_IS_SUCCESS=0
	local ST_CTL_RET_IS_TIMEOUT=0
	local ST_CTL_RET_IS_ABNORMAL=0
	local ST_CTL_RET_IS_STATE_DIFF=0
	local ST_CTL_RET_IS_OTHER=0
	local ST_CTL_MAX_WARMUP_RUNS=4
	local ST_CTL_INIT_WARMUP_RUNS=2
	local ST_CTL_WARMUP_RUN=$ST_CTL_INIT_WARMUP_RUNS
	log "Starting warmup runs for state diffing..."
	run_cmd "st_ctl --always_rollback --new_version=$APP_BINARY --allow-state-diff-bytes=$ST_UNLIMITED_STATE_DIFF_BYTES $STCTL_TTST_UPDATE_CMD" || return $EDFI_EAGAIN
	run_cmd "st_ctl --always_rollback --new_version=$APP_BINARY --allow-state-diff-bytes=$ST_UNLIMITED_STATE_DIFF_BYTES $STCTL_TTST_UPDATE_CMD" || return $EDFI_EAGAIN
	while [ "`cat $APP_DIR/.tmp/st.log.${PID} | grep STATE_DIFF_RESULT | sed 's/.*= //g' | tail -${ST_CTL_INIT_WARMUP_RUNS} | xargs | awk '{if($1==$2){print 1}else{print 0}}'`" == "0" ];
	do
		if [ $ST_CTL_WARMUP_RUN -ge $ST_CTL_MAX_WARMUP_RUNS ]; then
		    log "Maximum number of warmup runs reached ($ST_CTL_MAX_WARMUP_RUNS), giving up..."
		    log "State diffing output from $APP_DIR/.tmp/st.log.${PID}:"
		    cat $APP_DIR/.tmp/st.log.${PID} | grep STATE_DIFF_RESULT
		    break 
		fi
		run_cmd "st_ctl --always_rollback --new_version=$APP_BINARY --allow-state-diff-bytes=$ST_UNLIMITED_STATE_DIFF_BYTES $STCTL_TTST_UPDATE_CMD" || return $EDFI_EAGAIN
		ST_CTL_WARMUP_RUN=$(( $ST_CTL_WARMUP_RUN + 1 ))
	done
	ST_ALLOWED_STATE_DIFF_BYTES=`cat $APP_DIR/.tmp/st.log.${PID} | grep STATE_DIFF_RESULT | sed 's/.*= //g' | tail -${ST_CTL_INIT_WARMUP_RUNS} | awk '{if(max==""){max=$1}; if($1>max) {max=$1}} END {print max}'`
	log "Starting fault injection run with --allow-state-diff-bytes=${ST_ALLOWED_STATE_DIFF_BYTES}..."
	run_cmd "st_ctl --always_rollback --new_version=$APP_BINARY --allow-state-diff-bytes=$ST_ALLOWED_STATE_DIFF_BYTES $STCTL_FI_OPTS $STCTL_TTST_UPDATE_CMD" || ST_CTL_RET=$?
	case "$ST_CTL_RET" in
	0)  echo "stctl returned success exit code ($ST_CTL_RET)"
	    ST_CTL_RET_IS_SUCCESS=1
	    log "State diffing output from $APP_DIR/.tmp/st.log.${PID}:"
	    cat $APP_DIR/.tmp/st.log.${PID} | grep STATE_DIFF_RESULT
	    ;;
	12|15)  echo "stctl returned timeout exit code ($ST_CTL_RET)"
	    ST_CTL_RET_IS_TIMEOUT=1
	    ;;
	11|14)  echo "stctl returned abnormal termination exit code ($ST_CTL_RET)"
	    ST_CTL_RET_IS_ABNORMAL=1
	    ;;
	16)  echo "stctl returned state diff exit code ($ST_CTL_RET)"
	    ST_CTL_RET_IS_STATE_DIFF=1
	    log "State diffing output from $APP_DIR/.tmp/st.log.${PID}:"
	    cat $APP_DIR/.tmp/st.log.${PID} | grep STATE_DIFF_RESULT
	    ;;
	*)  echo "stctl returned other exit code ($ST_CTL_RET)"
	    ST_CTL_RET_IS_OTHER=1
	    ;;
	esac
	run_cmd "edfi_build_ini $EDFI_SEC_FAULTS success=$ST_CTL_RET_IS_SUCCESS timeout=$ST_CTL_RET_IS_TIMEOUT abnormal=$ST_CTL_RET_IS_ABNORMAL state_diff=$ST_CTL_RET_IS_STATE_DIFF other=$ST_CTL_RET_IS_OTHER > $OUTPUT_FILE"
	run_cmd "rm $APP_DIR/.tmp/st.log.*"
	log "<-- Fault injection experiment callback done."
	return 0
}

function st_memory_exp_cb {
	log "--> Memory experiment callback for $APP_NAME with '$@'..."
	local OUTPUT_FILE=$1
	local INPUT_FILE=$3
	local STATIC_SIZE=0
	local IDLE_SIZE=0
	local BENCH_AVG_SIZE=0
	local ST_MAX_SIZE=0
	local TTST_MAX_SIZE=0
	local APP_BINARY=`edfi_app_bin`
	local PID=`edfi_app_pidof_one $APP_NAME`
	local MAGIC_SL_SIZE=`edfi_get_binaries_size_from_dir $MAGIC_LIBS_ROOT`
	local GET_ST_MAX_SIZE=1
	local UPDATE_SLEEP_SEC=5
	local mem_rss=1
	local PROC_COUNT=`edfi_app_pidof $APP_NAME | wc -l`
	#local VSFTPD_BENCH_AVG_SIZE_FACTOR=87
	if [ $ST_IS_BASELINE -eq 0 ]; then
		st_wait_for_monitor_thread
	fi
	if [ -e $INPUT_FILE ]; then
		STATIC_SIZE=`edfi_get_ini_value $INPUT_FILE static`
	fi
	if [ $DRY_RUN -eq 0 ]; then
		IDLE_SIZE=`MULTIPROC_PESSIMISTIC=1 MEM_RSS=$mem_rss edfi_get_dynamic_app_size`
		echo ">>>>>>> Found IDLE_SIZE=$IDLE_SIZE" 2>&1
	fi
	#
	# Not necessary for vsftpd and OpenSSH
	#
	#run_app_cmd "./runbench &> /dev/null &"
	#RUNBENCH_PID=`(ps -e --sort=start_time | grep runbench | head -1 | awk '{ print $1; }') || true`
	#if [ $DRY_RUN -eq 0 ]; then
	#	if [ "$RUNBENCH_PID" == "" ]; then
	#		log "Failed to retrieve RUNBENCH_PID..."
	#		return $EDFI_EAGAIN
	#	fi
	#	edfi_get_avg_dynamic_app_size $RUNBENCH_PID
	#	BENCH_AVG_SIZE=$EDFI_GET_AVG_DYNAMIC_APP_SIZE_RET
	#fi
	BENCH_AVG_SIZE=$IDLE_SIZE
	if [ $ST_IS_BASELINE -eq 1 ]; then
		BENCH_AVG_SIZE=$(( $BENCH_AVG_SIZE - $MAGIC_SL_SIZE ))
		IDLE_SIZE=$(( $IDLE_SIZE - $MAGIC_SL_SIZE ))
	fi
	ST_MAX_SIZE=$IDLE_SIZE
	TTST_MAX_SIZE=$IDLE_SIZE
	if [ $GET_ST_MAX_SIZE -eq 1 ] && [ $ST_IS_BASELINE -eq 0 ]; then
		st_ctl --always_rollback --new_version=$APP_BINARY --force-future-timeout --timeout=$(( $UPDATE_SLEEP_SEC + ($PROC_COUNT + 1)*2 )) $STCTL_ST_UPDATE_CMD &
		STCTL_PID=$!
		sleep $UPDATE_SLEEP_SEC
		ST_MAX_SIZE=`MULTIPROC_PESSIMISTIC=1 MEM_RSS=$mem_rss edfi_get_dynamic_app_size`
		if [ "`ps -e | grep \"^[ ]*${STCTL_PID} \"`" == "" ]; then
			log "stctl aborted unexpectedly..."
			return $EDFI_EAGAIN
		fi
		edfi_wait_for_pid $STCTL_PID 2
		st_ctl --always_rollback --new_version=$APP_BINARY --force-${STCTL_REVERSED}-timeout --timeout=$(( $UPDATE_SLEEP_SEC + ($PROC_COUNT + 2)*2 )) --allow-state-diff-bytes=$ST_UNLIMITED_STATE_DIFF_BYTES $STCTL_TTST_UPDATE_CMD &
		STCTL_PID=$!
		sleep $UPDATE_SLEEP_SEC
		TTST_MAX_SIZE=`MULTIPROC_PESSIMISTIC=1 MEM_RSS=$mem_rss edfi_get_dynamic_app_size`
		if [ "`ps -e | grep \"^[ ]*${STCTL_PID} \"`" == "" ]; then
			log "stctl aborted unexpectedly (2)..."
			return $EDFI_EAGAIN
		fi
		edfi_wait_for_pid $STCTL_PID 2
		if [ "$STCTL_REVERSED" == "future" ]; then
			TTST_MAX_SIZE=$(( $TTST_MAX_SIZE * 2 ))
		fi
		run_cmd "cp $APP_DIR/.tmp/st.log.${PID} $APP_DIR/st.log"
		run_cmd "cp $APP_DIR/st.log ${OUTPUT_FILE}_st.log"
		run_cmd "rm $APP_DIR/.tmp/st.log.*"
	fi
	run_cmd "edfi_build_ini $EDFI_SEC_MEMORY static=$STATIC_SIZE idle=$IDLE_SIZE bench_avg=$BENCH_AVG_SIZE st_max=$ST_MAX_SIZE ttst_max=$TTST_MAX_SIZE > $OUTPUT_FILE"
	run_cmd "cp $APP_DIR/runbench.log ${OUTPUT_FILE}_runbench.log"
	log "<-- Memory experiment callback done."
	return 0
}

function ltckpt_memory_exp_cb {
	log "--> Memory experiment callback for $APP_NAME with '$@'..."
	local OUTPUT_FILE=$1
	local INPUT_FILE=$3
	local STATIC_SIZE=0
	local IDLE_VM_SIZE=0
	local IDLE_RSS_SIZE=0
	local BENCH_AVG_VM_SIZE=0
	local BENCH_AVG_RSS_SIZE=0
	local BENCH_MAX_VM_SIZE=0
	local BENCH_MAX_RSS_SIZE=0
	local mem_rss=1
	local RET=$EDFI_EAGAIN
	if [ -e $INPUT_FILE ]; then
		STATIC_SIZE=`edfi_get_ini_value $INPUT_FILE static`
	fi
	if [ $DRY_RUN -eq 0 ] && [ -f $APP_DIR/serverctl ]; then
		common_get_dynamic_app_size
		IDLE_VM_SIZE=$COMMON_GET_DYNAMIC_APP_SIZE_VM_SIZE
		IDLE_RSS_SIZE=$COMMON_GET_DYNAMIC_APP_SIZE_RSS_SIZE
		echo ">>>>>>> Found IDLE_VM_SIZE=$IDLE_VM_SIZE IDLE_RSS_SIZE=$IDLE_RSS_SIZE" 2>&1
	fi
	if [ -f $APP_DIR/serverctl ]; then
		CLIENT_CP=1 run_app_cmd "./runbench &> /dev/null &"
		RUNBENCH_PID=`(ps -e --sort=start_time | grep runbench | head -1 | awk '{ print $1; }') || true`
	else
		run_app_cmd "$SERVERCTL_SUDO $SERVERCTL_CMD_PREFIX ./clientctl run &"
		sleep 3
		RUNBENCH_PID=`edfi_app_pidof_one $APP_NAME`
		RET=1
	fi
	if [ $DRY_RUN -eq 0 ]; then
		if [ "$RUNBENCH_PID" == "" ]; then
			log "Failed to retrieve RUNBENCH_PID..."
			return $RET
		fi
	        common_get_avg_dynamic_app_size $RUNBENCH_PID
		BENCH_AVG_NUM_PROCS=$COMMON_GET_AVG_DYNAMIC_APP_SIZE_AVG_NUM_PROCS
		BENCH_AVG_VM_SIZE=$COMMON_GET_AVG_DYNAMIC_APP_SIZE_AVG_VM_SIZE
		BENCH_AVG_RSS_SIZE=$COMMON_GET_AVG_DYNAMIC_APP_SIZE_AVG_RSS_SIZE
		BENCH_AVG_PSS_SIZE=$COMMON_GET_AVG_DYNAMIC_APP_SIZE_AVG_PSS_SIZE
		BENCH_MAX_NUM_PROCS=$COMMON_GET_AVG_DYNAMIC_APP_SIZE_MAX_NUM_PROCS
		BENCH_MAX_VM_SIZE=$COMMON_GET_AVG_DYNAMIC_APP_SIZE_MAX_VM_SIZE
		BENCH_MAX_RSS_SIZE=$COMMON_GET_AVG_DYNAMIC_APP_SIZE_MAX_RSS_SIZE
		BENCH_MAX_PSS_SIZE=$COMMON_GET_AVG_DYNAMIC_APP_SIZE_MAX_PSS_SIZE
	fi
	run_cmd "edfi_build_ini $EDFI_SEC_MEMORY static=$STATIC_SIZE idle_vm=$IDLE_VM_SIZE idle_rss=$IDLE_RSS_SIZE bench_avg_procs=$BENCH_AVG_NUM_PROCS bench_avg_vm=$BENCH_AVG_VM_SIZE bench_avg_rss=$BENCH_AVG_RSS_SIZE bench_avg_pss=$BENCH_AVG_PSS_SIZE bench_max_num_procs=$BENCH_MAX_NUM_PROCS bench_max_vm=$BENCH_MAX_VM_SIZE bench_max_rss=$BENCH_MAX_RSS_SIZE bench_max_pss=$BENCH_MAX_PSS_SIZE > $OUTPUT_FILE"
	run_cmd "cp $APP_DIR/runbench.log ${OUTPUT_FILE}_runbench.log"
	log "<-- Memory experiment callback done."
	return 0
}

function ltckpt_speculation_exp_cb {
	log "--> Speculation experiment callback for $APP_NAME with '$@'..."
	if [ -f $APP_DIR/serverctl ]; then
		sudo bash -c "echo '' > /var/log/syslog"
		if [ "$APP_NAME" == "httpd" ] || [ "$APP_NAME" == "nginx" ] || [ "$APP_NAME" == "lighttpd" ]; then
			CLIENT_CP=1 run_app_cmd "./clientctl bench" || return $EDFI_EAGAIN
		else
			CLIENT_CP=1 run_app_cmd "./clientctl bench" || return $EDFI_EAGAIN
		fi
		cat /var/log/syslog | grep META_PRINT | sed "s/.*META_PRINT//g" |awk '{print $1 " " $3 " " $5 " " $7 " " $9 " " $11 " "  $13}'> $OUTPUT_FILE
	fi
	log "<-- Speculation experiment callback done."
	return 0
}



function ltckpt_performance_counters_exp_cb {
	log "--> Performance counter experiment callback for $APP_NAME with '$@'..."
	local OUTPUT_FILE=$1
	local APP_BINARY=`edfi_app_bin`
	TIME_CMD_RET=0

	if [ -f $APP_DIR/serverctl ]; then
		if [ "$APP_NAME" == "httpd" ] || [ "$APP_NAME" == "nginx" ] || [ "$APP_NAME" == "lighttpd" ]; then
			#
			# Support for web servers only.
			#
			PERF=1 run_app_cmd "./serverctl perf bench_all_counters `which env` CLIENT_CP=1 ./clientctl bench" || return $EDFI_EAGAIN
			run_cmd "edfi_strip_ini $APP_DIR/runbench.ini welcome_ >  $APP_DIR/runbench.ini.welcome"
			run_cmd "edfi_strip_ini $APP_DIR/perf.ini welcome_ >>  $APP_DIR/runbench.ini.welcome"
			PERF=1 run_app_cmd "./serverctl perf bench_all_counters `which env` CLIENT_CP=1 RUNBENCH_FILE_SUFFIX=.4K ./clientctl bench" || return $EDFI_EAGAIN
			run_cmd "edfi_strip_ini $APP_DIR/runbench.ini 4K_ > $APP_DIR/runbench.ini.4K"
			run_cmd "edfi_strip_ini $APP_DIR/perf.ini 4K_ >> $APP_DIR/runbench.ini.4K"
			PERF=1 run_app_cmd "./serverctl perf bench_all_counters `which env` CLIENT_CP=1 RUNBENCH_FILE_SUFFIX=.64K ./clientctl bench" || return $EDFI_EAGAIN
			run_cmd "edfi_strip_ini $APP_DIR/runbench.ini 64K_ > $APP_DIR/runbench.ini.64K"
			run_cmd "edfi_strip_ini $APP_DIR/perf.ini 64K_ >> $APP_DIR/runbench.ini.64K"
			run_cmd "edfi_merge_stripped_inis $EDFI_SEC_MILLIS $APP_DIR/runbench.ini.welcome $APP_DIR/runbench.ini.4K $APP_DIR/runbench.ini.64K > $OUTPUT_FILE"
		else
			PERF=1 run_app_cmd "./serverctl perf bench_all_counters `which env` CLIENT_CP=1 ./clientctl bench" || return $EDFI_EAGAIN
			run_cmd "edfi_rename_ini $APP_DIR/runbench.ini $EDFI_SEC_MILLIS > $OUTPUT_FILE"
			run_cmd "edfi_rename_ini $APP_DIR/perf.ini     $EDFI_SEC_MILLIS > $OUTPUT_FILE"
		fi
		if [ $LTCKPT_IS_BASELINE -eq 0 ]; then
		    run_app_cmd "./clientctl dumpcp pids"
		fi
	else
		CLIENT_CP=1 run_app_cmd "$SERVERCTL_SUDO $SERVERCTL_CMD_PREFIX ./clientctl bench" || return 1
		run_cmd "edfi_rename_ini $APP_DIR/runbench.ini $EDFI_SEC_MILLIS > $OUTPUT_FILE"
	fi
	#run_cmd "cp $APP_DIR/runbench.log ${OUTPUT_FILE}.runbench.log"
	run_cmd "cp $APP_BINARY ${OUTPUT_FILE}.binary"
	log "<-- Performance counter experiment callback done."
	return 0
}


function ltckpt_pagestat_exp_cb {
	log "--> Pagestat callback for $APP_NAME with '$@'..."
	local OUTPUT_FILE=$1
	local APP_BINARY=`edfi_app_bin`
	TIME_CMD_RET=0
	echo "nothing = 0" > ${OUTPUT_FILE}
	echo  >> /tmp/output.out
	echo  >> /tmp/output.out
	echo  "  ====  $APP_NAME ======  " >> /tmp/output.out
	if [ -f $APP_DIR/serverctl ]; then
		if [ "$APP_NAME" == "httpd" ] || [ "$APP_NAME" == "nginx" ] || [ "$APP_NAME" == "lighttpd" ]; then
			#
			# Support for web servers only.
			#

			echo         >> /tmp/output.out
			echo "     == Welcome ==" >> /tmp/output.out
	
			CLIENT_CP=1 run_app_cmd "./clientctl bench" || return $EDFI_EAGAIN
			DUMP_ALSO_DEAD=1 PLOT=1 run_app_cmd "./clientctl statcp hist pids | tee ${OUTPUT_FILE}.hist | tee -a /tmp/output.out "
			DUMP_ALSO_DEAD=1 PLOT=1 run_app_cmd "./clientctl statcp freqs pids | tee ${OUTPUT_FILE}.freq| tee -a /tmp/output.out "
			echo         >> /tmp/output.out
			echo "     == 4k == ">> /tmp/output.out
			CP_NOMMAP=0 RUN_CP=1 PAGESTAT=1 run_app_cmd "./serverctl frestart"
			CLIENT_CP=1 RUNBENCH_FILE_SUFFIX=.4K run_app_cmd "./clientctl bench" || return $EDFI_EAGAIN
			DUMP_ALSO_DEAD=1 PLOT=1 run_app_cmd "./clientctl statcp hist pids | tee ${OUTPUT_FILE}.hist.4KB| tee -a /tmp/output.out "
			DUMP_ALSO_DEAD=1 PLOT=1 run_app_cmd "./clientctl statcp freqs pids | tee ${OUTPUT_FILE}.freq.4KB| tee -a /tmp/output.out "
			echo         >> /tmp/output.out
			echo "     == 64k =="  >> /tmp/output.out
			CP_NOMMAP=0 RUN_CP=1 PAGESTAT=1 run_app_cmd "./serverctl frestart"
			CLIENT_CP=1 RUNBENCH_FILE_SUFFIX=.64K run_app_cmd "./clientctl bench" || return $EDFI_EAGAIN
			DUMP_ALSO_DEAD=1 PLOT=1 run_app_cmd "./clientctl statcp hist pids | tee ${OUTPUT_FILE}.hist.64KB| tee -a /tmp/output.out "
			DUMP_ALSO_DEAD=1 PLOT=1 run_app_cmd "./clientctl statcp freqs pids | tee ${OUTPUT_FILE}.freq.64KB| tee -a /tmp/output.out "
		else
			CLIENT_CP=1 run_app_cmd "./clientctl bench" || return $EDFI_EAGAIN
			DUMP_ALSO_DEAD=1 PLOT=1 run_app_cmd "./clientctl statcp hist pids | tee ${OUTPUT_FILE}.hist| tee -a /tmp/output.out "
			DUMP_ALSO_DEAD=1 PLOT=1 run_app_cmd "./clientctl statcp freqs pids | tee ${OUTPUT_FILE}.freq| tee -a /tmp/output.out "
		fi
		    run_app_cmd "./clientctl dumpcp pids"
	else
		CLIENT_CP=1 run_app_cmd "$SERVERCTL_SUDO $SERVERCTL_CMD_PREFIX ./clientctl bench" || return 1
	fi
	run_cmd "cp $APP_BINARY ${OUTPUT_FILE}.binary"
	log "<-- Pagestat callback done."
	return 0
}

function ltckpt_performance_exp_cb {
	log "--> Performance experiment callback for $APP_NAME with '$@'..."
	local OUTPUT_FILE=$1
	local APP_BINARY=`edfi_app_bin`
	TIME_CMD_RET=0

	if [ -f $APP_DIR/serverctl ]; then
		if [ "$APP_NAME" == "httpd" ] || [ "$APP_NAME" == "nginx" ] || [ "$APP_NAME" == "lighttpd" ]; then
			#
			# Support for web servers only.
			#
			CLIENT_CP=1 run_app_cmd "./clientctl bench" || return $EDFI_EAGAIN
			run_cmd "edfi_strip_ini $APP_DIR/runbench.ini welcome_ >  $APP_DIR/runbench.ini.welcome"
			CLIENT_CP=1 RUNBENCH_FILE_SUFFIX=.4K run_app_cmd "./clientctl bench" || return $EDFI_EAGAIN
			run_cmd "edfi_strip_ini $APP_DIR/runbench.ini 4K_ > $APP_DIR/runbench.ini.4K"
			CLIENT_CP=1 RUNBENCH_FILE_SUFFIX=.64K run_app_cmd "./clientctl bench" || return $EDFI_EAGAIN
			run_cmd "edfi_strip_ini $APP_DIR/runbench.ini 64K_ > $APP_DIR/runbench.ini.64K"
			run_cmd "edfi_merge_stripped_inis $EDFI_SEC_MILLIS $APP_DIR/runbench.ini.welcome $APP_DIR/runbench.ini.4K $APP_DIR/runbench.ini.64K > $OUTPUT_FILE"
		else
			CLIENT_CP=1 run_app_cmd "./clientctl bench" || return $EDFI_EAGAIN
			run_cmd "edfi_rename_ini $APP_DIR/runbench.ini $EDFI_SEC_MILLIS > $OUTPUT_FILE"
		fi
		if [ $LTCKPT_IS_BASELINE -eq 0 ]; then
		    run_app_cmd "./clientctl dumpcp pids"
		fi
	else
		CLIENT_CP=1 run_app_cmd "$SERVERCTL_SUDO $SERVERCTL_CMD_PREFIX ./clientctl bench" || return 1
		run_cmd "edfi_rename_ini $APP_DIR/runbench.ini $EDFI_SEC_MILLIS > $OUTPUT_FILE"
	fi
	#run_cmd "cp $APP_DIR/runbench.log ${OUTPUT_FILE}.runbench.log"
	run_cmd "cp $APP_BINARY ${OUTPUT_FILE}.binary"
	log "<-- Performance experiment callback done."
	return 0
}

function ltckpt_sysctl_conf {
	sudo sysctl -w net.ipv4.tcp_tw_recycle=1
	sudo sysctl -w net.ipv4.tcp_fin_timeout=15
	sudo sysctl -w net.ipv4.ip_local_port_range="32768 65535"
	sudo sysctl -w kernel.shmmax=1610612736
}

function edfi_representativeness_analysis {
	log "--> Representativeness analysis..."
	run_cmd "$EDFISTAT $EDFI_SEC_PROB $EDFI_TEST_DIR/exps/${EDFI_INPUT_INI}@$EDFI_SEC_PROB:$EDFI_TEST_DIR/exps/${EDFI_OUTPUT_INI}@$EDFI_SEC_PROB relerr > $EDFI_TEST_DIR/exps/${EDFI_OUTPUT_INI}.relerr"
	run_cmd "$EDFISTAT $EDFI_SEC_PROB $EDFI_TEST_DIR/exps/${EDFI_OUTPUT_INI}.relerr@$EDFI_SEC_PROB median > $EDFI_TEST_DIR/exps/${EDFI_OUTPUT_INI}.mre"
	log "<-- Representativeness analysis done."
}

function edfi_controllability_analysis {
	log "--> Controllability analysis..."
	run_cmd "$EDFISTAT $EDFI_SEC_FAULTS $EDFI_TEST_DIR/exps/${EDFI_OUTPUT_INI}@$EDFI_SEC_FAULTS median > $EDFI_TEST_DIR/exps/${EDFI_OUTPUT_INI}.msf"
	log "<-- Controllability analysis done."
}

function ltckpt_fperformance_analysis {
	log "--> Fperformance analysis..."
	run_cmd "$EDFISTAT $EDFI_SEC_MILLIS $EDFI_TEST_DIR/../baseline/exps/${EDFI_OUTPUT_INI}@$EDFI_SEC_MILLIS:$EDFI_TEST_DIR/exps/${EDFI_OUTPUT_INI}@$EDFI_SEC_MILLIS relincr > $EDFI_TEST_DIR/exps/${EDFI_OUTPUT_INI}.po"
	log "<-- Fperformance analysis done."
}

function common_recompile_libs {
	local SAVED_DRY_RUN=$DRY_RUN
	if [ "$AGGREGATE_RUNS_FROM_DIRS" != "" ]; then
		DRY_RUN=1
	fi
	run_cmd "cd $LIBS_ROOT"
	run_cmd "CXXFLAGS=\"$@\" make clean install"
	run_cmd "cd $MYPWD"
	run_cmd "cd $SHARED_LIBS_ROOT"
	run_cmd "CXXFLAGS=\"$@\" make clean install"
	run_cmd "cd $MYPWD"
	run_app_cmd "./relink.llvm $RELINK_ARGS"
	DRY_RUN=$SAVED_DRY_RUN
}

function edfi_recompile_libs {
	local SAVED_DRY_RUN=$DRY_RUN
	if [ "$AGGREGATE_RUNS_FROM_DIRS" != "" ]; then
		DRY_RUN=1
	fi
	run_cmd "cd $EDFI_LIBS_ROOT"
	run_cmd "CXXFLAGS=\"$@\" make clean install"
	run_cmd "cd $MYPWD"
	run_cmd "cd $MAGIC_SHARED_LIBS_ROOT"
	run_cmd "CXXFLAGS=\"$@\" make clean install"
	run_cmd "cd $MYPWD"
	run_app_cmd "./relink.llvm $RELINK_ARGS"
	DRY_RUN=$SAVED_DRY_RUN
}

function edfi_var_init {
	local VAR=$1
	local EXTERNAL_VAR=$2
	local DEFAULT_VAR=$3
	if [ ! -z "${!EXTERNAL_VAR:-}" ]; then
		eval ${VAR}="${!EXTERNAL_VAR}"
		log "Using external ${VAR}=${!VAR}"
	elif [ "${!VAR}" == "" ]; then
		eval ${VAR}="${!DEFAULT_VAR}"
		log "Using default ${VAR}=${!VAR}"
	else
		log "Using module-specified ${VAR}=${!VAR}"
	fi
}

function common_init {
	log "--> Initialization..."
	edfi_var_init APP_DIR EDFI_APP_DIR DEFAULT_APP_DIR
	edfi_var_init APP_NAME EDFI_APP_NAME DEFAULT_APP_NAME
	edfi_var_init DRY_RUN EDFI_DRY_RUN DEFAULT_DRY_RUN
	edfi_var_init LOGGED_RUN EDFI_LOGGED_RUN DEFAULT_LOGGED_RUN
	edfi_var_init FIRST_RUN_ID EDFI_FIRST_RUN_ID DEFAULT_FIRST_RUN_ID
	edfi_var_init LAST_RUN_ID EDFI_LAST_RUN_ID DEFAULT_LAST_RUN_ID
	edfi_var_init RECOMPILE EDFI_RECOMPILE DEFAULT_RECOMPILE
	edfi_var_init AGGREGATE_RUNS_FROM_DIRS EDFI_AGGREGATE_RUNS_FROM_DIRS DEFAULT_AGGREGATE_RUNS_FROM_DIRS
	edfi_var_init OUTPUT_CSV EDFI_OUTPUT_CSV DEFAULT_OUTPUT_CSV
	edfi_var_init OUTPUT_RSD EDFI_OUTPUT_RSD DEFAULT_OUTPUT_RSD
	edfi_var_init OUTPUT_MATLAB EDFI_OUTPUT_MATLAB DEFAULT_OUTPUT_MATLAB
	edfi_var_init OUTPUT_ROOT EDFI_OUTPUT_ROOT DEFAULT_OUTPUT_ROOT
	edfi_var_init DSN_COMPAT EDFI_DSN_COMPAT DEFAULT_DSN_COMPAT
	edfi_var_init ATC_COMPAT EDFI_ATC_COMPAT DEFAULT_ATC_COMPAT
	edfi_var_init SERVERCTL_CMD_PREFIX EDFI_SERVERCTL_CMD_PREFIX DEFAULT_SERVERCTL_CMD_PREFIX
	edfi_var_init SERVERCTL_SUDO EDFI_SERVERCTL_SUDO DEFAULT_SERVERCTL_SUDO
	edfi_var_init AVERAGE_METRIC EDFI_AVERAGE_METRIC DEFAULT_AVERAGE_METRIC

	for d in `echo $AGGREGATE_RUNS_FROM_DIRS | sed "s/:/ /g"`
	do
		if [ "`echo $d | sed \"s/^\/.*//g\"`" != "" ]; then
			echo "Error: $d must be an absolute path!"
			exit 1
		fi
	done

	if [ "$SERVERCTL_SUDO" == "sudo" ]; then
		SERVERCTL_SUDO="sudo -E"
	fi
	if [ "$RECOMPILE" == "1" ]; then
		common_recompile_libs
	fi
	if [ "$APP_DIR" != "NULL" ]; then
		run_app_cmd "$SERVERCTL_SUDO ./serverctl cleanup &> /dev/null || true"
		rm -f $APP_DIR/*runbench.log
		rm -f $APP_DIR/*st.log
	fi
	if [ "$APP_NAME" == "vsftpd" ]; then
		ST_ALLOWED_STATE_DIFF_BYTES=500
	else
		ST_ALLOWED_STATE_DIFF_BYTES=500
	fi
	log "<-- Initialization done..."
}

