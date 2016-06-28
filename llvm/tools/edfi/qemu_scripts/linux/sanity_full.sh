#!/bin/bash
PAYLOAD="ssh -p20022 skl@localhost 'echo 1> /tmp/sanity'"
VAR=`timeout 15s ssh rge280@10.0.2.2 -o CheckHostIP=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $PAYLOAD`
RET=$?
DATE=`date`
VAR=`ls /tmp/sanity`
if [ -n "$VAR" ]
then
	hypermem print "Sanity Passed ($RET:$1) $DATE"
	echo "Sanity Passed ($RET:$1) $DATE"
else
	hypermem print "Sanity Failed ($RET:$1) $DATE"
	echo "Sanity Failed ($RET:$1) $DATE"
fi
rm -rf /tmp/sanity
