#!/bin/sh
USR=rge280
# RTT ssh for sanity - no RTT as it seems it is not consistent between runs
#nohup ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $USR@10.0.2.2 "ssh -p2222 root@localhost 'echo test > /hot/.tmp/sanity'" &
PAYLOAD="date > /tmp/fantastic;  cat /tmp/fantastic"
VAR=`ssh -o ConnectTimeout=30 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $USR@10.0.2.2 $PAYLOAD < /hot/sanity.sh`
RC=$?
DATE=`date`
# Check to see if there was any valid looking output from the process
VAR=`echo $VAR | grep 20`
if [ -n "$VAR" ]
then 
	/usr/local/sbin/hyper print "Sanity Passed ($RC:$1) $DATE"
	echo "Sanity Passed ($RC:$1) $DATE"
else
	/usr/local/sbin/hyper print "Sanity Failed ($RC:$1) $DATE"
	echo "Sanity Failed ($RC:$1) $DATE"
fi
