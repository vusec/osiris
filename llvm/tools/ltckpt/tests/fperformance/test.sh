#!/bin/bash

set -o errexit
set -o errtrace
set -o nounset

ROOT=$( dirname $0 )/../..

. $ROOT/tests/common/ltckpt.sh

APP_NAMES=${APP_NAMES:-nginx lighttpd httpd}
sudo true

#
# Run experiment for each app
#
for a in $APP_NAMES
do
	APP_DIR=`hget $a`
	echo Fperformance experiment for app ${a}...
	./test_app.sh $a `hget $a` `hget ${a}tol`
done

#
# Merge Fperformance experiment results
#
RESULT_FILES=`find results -name fperformance.dat | sort | xargs`
if [ "$RESULT_FILES" != "" ]; then
	paste $RESULT_FILES > results/fperformance.all.dat
fi

