#!/bin/bash

stty -F /dev/ttyS0 speed 115200 cs8 -cstopb -parenb
# Perform pre-test sanity checking
mkdir /hot/tmp
dd if=/dev/zero of=/tmp/useless count=5000
hypermem dump

# Fetch the workload running script and run it
sudo dd if=/dev/sdb of=/hot/run.tar
mv /hot/run.tar /hot/tmp/
cd /hot/tmp
tar -xvf run.tar
mv run.sh ../
cd /hot

sleep 1
/hot/sanity_full.sh PRETEST
hypermem print "<START>"
chmod +x /hot/run.sh
/hot/run.sh &> /dev/ttyS0
hypermem print "Workload Completed `date`"
hypermem dump

# We have ran the workload successfully
hypermem print "<END>"
#Clean up temp files
rm run.sh
rm -rf tmp
# Perform post-test sanity checking
dd if=/dev/zero of=/tmp/useless count=5000
/hot/sanity_full.sh POSTTEST

