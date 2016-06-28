#!/bin/bash

set -o errexit
set -o errtrace

ROOT=$( dirname $0 )/../..

. $ROOT/tests/common/ltckpt.sh

if [[ ! -z "$EDFI_AGGREGATE_RUNS_FROM_DIRS" ]]; then
        export LTCKPT_DRY_RUN=1
fi

set -o nounset


SPEC_CONF=${SPEC_CONF:-all_c}
APP_NAMES=${APP_NAMES:-nginx lighttpd httpd vsftpd proftpd pureftpd postgresql bind}
sudo true
rm -f /tmp/output.out || true
touch /tmp/output.out

#
# Run experiment for each app
#
for a in $APP_NAMES
do
	APP_DIR=`hget $a`
        if [ -f $ROOT/../../../apps/$APP_DIR/serverctl ]; then
	    (cd $ROOT/../../../apps/$APP_DIR && sudo ./serverctl cleanup)
        fi
done

for a in $APP_NAMES
do
	APP_DIR=`hget $a`
	echo Performance experiment for app ${a}...
	CONFS="app"
	if [ "$a" == "spec" ]; then
		CONFS=$( cd ../../../../../apps/SPEC_CPU2006/ && C=$SPEC_CONF ./clientctl list | xargs )
	fi
	for c in $CONFS
	do
		C=$c ./test_app.sh $a `hget $a`
	done
done


