#!/bin/bash

: ${MROOT="${HOME}/repositories/llvm-apps/apps/minix"}
: ${LROOT="${MROOT}/../.."}

export MROOT

MYPWD=`pwd`

cd $MROOT/scripts

rm $LROOT/common.overrides.*osiris.inc 2>/dev/null || true
cp $LROOT/conf/common.overrides.profiling.osiris.inc $LROOT/
if [ $? -ne 0 ]; then
        echo "OSIRIS window-profiling llvm-apps overrides file missing."
        exit 1
fi

SCHEME=$1

case ${SCHEME} in

"optimistic" )
                export IPC_DEFAULT_DECISION=1
                export KC_DEFAULT_DECISION=1
                ;;

"pessimistic" )
                export IPC_DEFAULT_DECISION=4
                export KC_DEFAULT_DECISION=4
                ;;

"enhanced" )
		export RECOVERYPASSOPTEXTRA="-recovery-ipc-force-decision=9"
		;;
*)
                echo "Choosing pessimistic, by default."
                export IPC_DEFAULT_DECISION=4
                export KC_DEFAULT_DECISION=4
        #echo "invalid option specified."
        #exit 1
;;

esac

$MROOT/scripts/setup_distr_recovery.sh $LROOT
WINDOW_PROFILING=1 SETUP=0 ./drec_test_launcher.sh

