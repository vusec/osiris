#!/bin/bash

############################
#
# Author: Koustubha Bhat
# Date  : 1-July-2014
# VU University, Amsterdam.
#
############################
: ${LOCAL_ROOT=/home/koustubha/repositories/llvm-apps/apps/minix}
: ${FILTER="servers"}
MYPWD=`pwd`
SCRIPT_DIR=${LOCAL_ROOT}/blobify-scripts
TARGET_DIR=$LOCAL_ROOT/obj.i386/minix/$FILTER/
OUTPUT_DIR=$LOCAL_ROOT/outdir/prefixed_minix_bcl
TARGET_LIST=$OUTPUT_DIR/../targetlist.tmp

check_state_and_init()
{
  [ -d $OUTPUT_DIR ] || mkdir -p $OUTPUT_DIR 
  
  if [ -f $TARGET_LIST ] 
  then
	grep "target_list_prepared" $TARGET_LIST >/dev/null
	if [ $? -ne 0 ]
	then
		rm $TARGET_LIST
	fi
  fi

  if [ ! -f $TARGET_LIST ]
  then
	for f in `find $TARGET_DIR -name *.opt.bcl -type f`
	do
		basenamefile=`basename $f`
		modulename=${basenamefile%.opt.bcl}
		echo $modulename=$f >> $TARGET_LIST
	done
	echo "target_list_prepared" >> $TARGET_LIST
  fi
}

perform_prefixing()
{
	for l in `grep ".*=.*" ${TARGET_LIST}`
	do 
	    prefix="mx_`echo $l | cut -d= -f1`_"
	    tfile=`echo $l | cut -d= -f2`
  	    $SCRIPT_DIR/prefix_module.sh $prefix $tfile also-ll outdir:$OUTPUT_DIR 
	    printf "%-60s %-15s\n" "Applied prefix:$prefix on ${tfile#${TARGET_DIR}}" "[exitcode: $?]"
	done
}

echo Checking current state and intializing
check_state_and_init
printf "%-60s %-15s\n\n" "Starting prefixing operation on target modules" "[total: `grep ".*=.*" $TARGET_LIST | wc -l` ]."
perform_prefixing
echo
echo Completed prefixing operation on target modules.
