#!/bin/bash

set -o errexit
set -o errtrace
set -o nounset

MYPWD=`pwd`

TEST_FILES=`find . -name test.sh | sort`

for f in $TEST_FILES
do
	echo Entering dir `dirname $f`...
	cd `dirname $f`
	./test.sh > test.log
	cd $MYPWD
	echo Leaving dir `dirname $f`...
done

