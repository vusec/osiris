#!/bin/bash
ping -c 3 10.0.2.2
RET=$?
DATE=`date`
if [ $RET -eq "0" ]
then
	hypermem print "Sanity Passed ($RET:$1) $DATE"
else
	hypermem print "Sanity Failed ($RET:$1) $DATE"
fi
echo DONE
