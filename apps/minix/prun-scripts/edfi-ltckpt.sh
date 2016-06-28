#!/bin/bash

set -e

# script settings (also see edfi-common.inc)
: ${EDFIFAILSTOP:=0}  # boolean, non-zero inject means only fail-stop faults
: ${INJECTFAULTS:=1} # boolean, non-zero means inject faults
: ${RECOVERYMODEL:=4} # 0=disabled, 4=pessimistic, 5=naive, 8=stateless, 9=enhanced

# base directory
: ${PATHAPP:=""}
if [ -z "$PATHAPP" -a -f prun-scripts/edfi-common.inc ]; then
	PATHAPP="$PWD"
elif [ ! -f "$PATHAPP/prun-scripts/edfi-common.inc" ]; then
	echo "error: either run from llvm-apps/apps/minix or set PATHAPP to that directory" >&2
	exit 1
fi

# parameters to edfi-common.inc
if [ "$EDFIFAILSTOP" != 0 ]; then
	FAULTTYPEIND="failstop"
else
	FAULTTYPEIND="all"
fi
: ${PRUNSCRIPTNAME:="edfi-prun-ltckpt.sh"}
: ${SETTINGSNAME:="$FAULTTYPEIND-rm$RECOVERYMODEL"}
: ${RUNNAMEFAULTY:="ltckpt-$SETTINGSNAME"}
: ${RUNNAMEGOLDEN:="ltckpt-$FAULTTYPEIND"}

# include common functions/settings
. "$PATHAPP/prun-scripts/edfi-common.inc"

# parameters to setup_distr_recovery.sh
export BUILDOPTS
export CONFIGOPTS
export CROSS_TOOLS
export JOBS
export LTRC_NO_RECOVERY_ON_SVC_FI
export REBUILDGOLD
export RELINKOPTS
: ${BUILDOPTS:="edfi -fault-one-per-block -fault-rand-reseed-per-function=1 -fault-unconditionalize -inline-profiling"}
: ${COMP:=ds pm rs vfs vm}
: ${CONFIGOPTS:="edfi"}
: ${JOBS:=16}
: ${LTRC_NO_RECOVERY_ON_SVC_FI:=1}
: ${REBUILDGOLD:=0}
: ${RELINKOPTS:="edfi"}

[ "$EDFIFAILSTOP" = 0 ] || BUILDOPTS="$BUILDOPTS  -fault-types=null-pointer"

# apply recovery model
export APPEND=""
export DISABLERECOVERY=0
export IPC_DEFAULT_DECISION=4
export KC_DEFAULT_DECISION=4
export APPLY_MANUAL_DECISIONS=0
export RECOVERYPASSOPTEXTRA="-recovery-ipc-force-decision=4"
case "$RECOVERYMODEL" in
0)
	# recovery disabled
	DISABLERECOVERY=1
	;;
4)
	# pessimistic
	;;
5)
	# naive (never reply)
	IPC_DEFAULT_DECISION=1
	KC_DEFAULT_DECISION=1
	RECOVERYPASSOPTEXTRA="-recovery-ipc-force-decision=$RECOVERYMODEL"
	;;
8)
	# stateless
	DISABLERECOVERY=1
	APPEND="no_ist=1"
	TESTSRECOVERY=""
	;;
9)
	# enhanced
	APPLY_MANUAL_DECISIONS=1
	RECOVERYPASSOPTEXTRA="-recovery-ipc-force-decision=9"
	;;
*)
	echo "Invalid RECOVERYMODEL: $RECOVERYMODEL" >&2
	exit 1
	;;
esac

# print settings
common_print_settings
echo "- EDFIFAILSTOP    = $EDFIFAILSTOP"
echo "- RECOVERYMODEL   = $RECOVERYMODEL"

if common_must_build; then
	# clean up old files that might otherwise go stale
	common_cleanup
	rm -f "$PATHAPP/distr_recov.setup.log"

	# instrument system
	cd "$PATHAPP"
	scripts/setup_distr_recovery.sh "$PATHROOT"

	# build MINIX VM image
	common_build_vm

	# copy built files to PATHSETTINGSBIN
	common_build_done
	[ -f ""$PATHAPP/distr_recov.setup.log ] && mv "$PATHAPP/distr_recov.setup.log" "$PATHSETTINGSBINLOGS"
	for s in $COMP; do
		MROOT="$PATHAPP" scripts/drec_gen_siteid_caller_map.sh "$s" > "$PATHSETTINGSBINLOGS/site-id-$s"
	done
fi

# prepare output directory
common_prepare_pathout

if [ "$INJECTFAULTS" -ne 0 ]; then
	# select faults/tests if needed
	common_select_faults

	# check map files with golden run
	common_check_map
fi

# run experiment
common_run_experiment
