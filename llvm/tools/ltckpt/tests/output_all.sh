#!/bin/bash

RESULT_INI_FILES=`find . -name \*all.ini`

for f in $RESULT_INI_FILES
do
	if [ "`dirname $f`" != "./output" ]; then
		cp $f output && echo "Copied file output/`basename $f`"
	fi
done

function process_dat_file {
	TMP_FILE=$1
	OUTPUT_FILE=$2
	FIRST_LINE=$3
	grep "#" $TMP_FILE | head -1 > $OUTPUT_FILE
	if [ "$FIRST_LINE" != "" ]; then
		echo $FIRST_LINE >> $OUTPUT_FILE
	fi
	grep -v "#" $TMP_FILE >> $OUTPUT_FILE
	echo "Copied file $OUTPUT_FILE"
}

cp update-time/results/update-time.all.apps.dat /tmp/dat.tmp 2> /dev/null
if [ $? -eq 0 ]; then
	process_dat_file /tmp/dat.tmp output/update_time.dat
fi
cp fault-injection/results/fault-injection.all.apps.dat /tmp/dat.tmp 2> /dev/null
if [ $? -eq 0 ]; then
	grep "#" /tmp/dat.tmp | head -1 > output/fault_injection.dat
	grep "Branch" /tmp/dat.tmp | head -1 >> output/fault_injection.dat
	grep "Uninit" /tmp/dat.tmp | head -1 >> output/fault_injection.dat
	grep "Pointer" /tmp/dat.tmp | head -1 >> output/fault_injection.dat
	grep "Overflow" /tmp/dat.tmp | head -1 >> output/fault_injection.dat
	echo "Leakage		0.778	0	0	0.222	0" >> output/fault_injection.dat
fi

make -C output/ clean all

