#!/bin/sh
VAR=`ping 10.0.2.2`
RC=$?
DATE=`date`
# Check to see if there was any valid looking output from the process
VAR=`echo $VAR | grep alive`
if [ -n "$VAR" ]
then 
	/usr/local/sbin/hyper print "Sanity Passed ($RC:$1) $DATE"
	echo "Sanity Passed ($RC:$1) $DATE"
else
	/usr/local/sbin/hyper print "Sanity Failed ($RC:$1) $DATE"
	echo "Sanity Failed ($RC:$1) $DATE"
fi
