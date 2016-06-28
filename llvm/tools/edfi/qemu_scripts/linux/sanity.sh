#!/bin/bash
PAYLOAD="date > /tmp/fantastic; cat /tmp/fantastic"
#PAYLOAD="ssh -p20022 skl@localhost 'touch /tmp/sanity'"
VAR=`timeout 15s ssh rge280@10.0.2.2 -o CheckHostIP=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $PAYLOAD`
RET=$?
DATE=`date`
VAR=`echo $VAR | grep 20`
if [ -n "$VAR" ]
then
	hypermem print "Sanity Passed ($RET:$1) $DATE"
else
	hypermem print "Sanity Failed ($RET:$1) $DATE"
fi
echo DONE
