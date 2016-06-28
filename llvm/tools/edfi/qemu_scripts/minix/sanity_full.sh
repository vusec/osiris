#!/bin/sh
USR=rge280
# RTT ssh for sanity
PAYLOAD="ssh -o BatchMode=yes -o ConnectTimeout=10 -p2222 root@localhost 'echo 1> /hot/.tmp/sanity'" 
nohup ssh -o BatchMode=yes -o ConnectTimeout=15 -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no $USR@10.0.2.2 $PAYLOAD < /hot/sanity.sh > nohup.out &
PID=$!
sleep 5
PID_EXISTS=`ps -e | grep $PID | grep -v "grep"`
if [ -n "$PID_EXISTS" ] 
then
	sleep 10
	kill -9 $PID
fi

RC=$?
DATE=`date`

# Check to see if there was any valid looking output from the process
VAR=`ls /hot/.tmp/sanity`
if [ -n "$VAR" ]
then 
	/usr/local/sbin/hyper print "Sanity Passed ($RC:$1) $DATE"
	echo "Sanity Passed ($RC:$1) $DATE"
	rm -rf /hot/.tmp/sanity
else
	/usr/local/sbin/hyper print "Sanity Failed ($RC:$1) $DATE"
	echo "Sanity Failed ($RC:$1) $DATE"
fi

