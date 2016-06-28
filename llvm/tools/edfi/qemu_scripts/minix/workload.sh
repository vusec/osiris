#!/bin/sh
# Script to hook up edfi workloads during startup

# Fire up /dev/random
dd if=/dev/zero of=/dev/random count=50000 
# You never know when you need to connect to this vm during tests
/usr/local/sbin/sshd


# Fetching workload trigger script
mkdir -p /hot/.tmp
dd if=/dev/c0d1 of=/hot/.tmp/work.tar
cd /hot/.tmp/
tar -xvf work.tar
mv run.sh /hot/

# Pre-test stats
/usr/local/sbin/hyper print "<START>"
service up /usr/sbin/hello
service down hello
/usr/local/sbin/hyper dump
# Run the test
chmod +x /hot/run.sh

# Run pre-test sanity
dd if=/dev/zero of=/dev/random count=50000 
/hot/sanity_full.sh PRETEST
sh /hot/run.sh
/usr/local/sbin/hyper print "Workload Completed"

# Postr-test stats
service up /usr/sbin/hello
service down hello
/usr/local/sbin/hyper dump
/usr/local/sbin/hyper print "<END>"

# Run post-test sanity
dd if=/dev/zero of=/dev/random count=50000 
/hot/sanity_full.sh POSTTEST

# Doing cleanup
rm -f /hot/run.sh
rm -rf /hot/.tmp/

