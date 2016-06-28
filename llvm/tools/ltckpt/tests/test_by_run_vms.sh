#!/bin/bash

TESTS_ROOT=.

#
# Sample usage: VM_ERR_CMD="vm/vm_scp.sh %VM_ID% %VM_ID% %REMOTE%:/media/shared/workspace/magic-apps/llvm/tools/st/tests/fault-injection/results/test*.log ." VM_RCFILE=vm/vm_fi_rcfile ./test_by_run_vms.sh 1 100 fault-injection/test.sh
#

if [ $# -lt 3 ]; then
	echo "Usage: [VM_ERR_CMD=command to execute locally after a VM error, use %VM_ID% as a template for VM_ID] [VM_RCFILE=/path/to/my_rcfile] $0 <first_run_id> <last_run_id> <test_script> [test_args]"
	exit 1
fi

. $TESTS_ROOT/vm/vm_common.sh

FIRST_RUN_ID=$1
LAST_RUN_ID=$2
TEST_SCRIPT=$3
shift; shift; shift

VM_DRY_RUN=${VM_DRY_RUN:-0}

FIRST_VM_ID=${FIRST_VM_ID:-1}
LAST_VM_ID=${LAST_VM_ID:-$VM_NUM_HOSTS}
NUM_VM_IDS=$(( $LAST_VM_ID - $FIRST_VM_ID + 1 ))

VM_CMD=${VM_CMD:-./test_by_run.sh %FIRST_RUN_ID% %LAST_RUN_ID% $TEST_SCRIPT}
VM_ERR_CMD=${VM_ERR_CMD:-"Error detected on VM_ID=%VM_ID%"}

vm_check_valid_id $FIRST_VM_ID
vm_check_valid_id $LAST_VM_ID

echo "Using VM_ID range VM_ID=[$FIRST_VM_ID;$LAST_VM_ID]..."
echo "Using VM_CMD=$VM_CMD"

NUM_RUN_IDS=$(( $LAST_RUN_ID - $FIRST_RUN_ID + 1 ))
NUM_RUN_IDS_PER_VM=`divide=$NUM_RUN_IDS; by=$NUM_VM_IDS; let result=($divide+$by-1)/$by; echo $result`
NUM_VMS=`divide=$NUM_RUN_IDS; by=$NUM_RUN_IDS_PER_VM; let result=($divide+$by-1)/$by; echo $result`

date
echo ""

first_run_id=$FIRST_RUN_ID
total_run_ids=0
for i in `seq $FIRST_VM_ID $LAST_VM_ID`
do
    num_run_ids=$NUM_RUN_IDS_PER_VM
    if [ $(( $first_run_id + $num_run_ids - 1 )) -gt $LAST_RUN_ID ]; then
        num_run_ids=$(( $LAST_RUN_ID - $first_run_id + 1 ))
    fi
    if [ $num_run_ids -eq 0 ]; then
    	break
    fi
    last_run_id=$(( $first_run_id + $num_run_ids - 1 ))
    vm_id=$i
    echo "Assigning RUN_ID=[$first_run_id;$last_run_id] ($num_run_ids RUN_IDs) to VM_ID=$vm_id..."
    (
        PRE_CMD=""
        if [ $VM_DRY_RUN -eq 1 ]; then
            PRE_CMD="echo Dry run for: "
        fi
        VM_REAL_CMD=`echo $VM_CMD | sed "s/%FIRST_RUN_ID%/$first_run_id/g" | sed "s/%LAST_RUN_ID%/$last_run_id/g"`
        VM_CMD_RET=0
        cd vm
        set -x
        $PRE_CMD ./vm_ssh.sh $vm_id $vm_id "cd $VM_TESTS_DIR && $VM_REAL_CMD $*" || VM_CMD_RET=$?
        set +x
        cd ..
        echo "VM command for VM_ID=$vm_id returned exit code $VM_CMD_RET"
        if [ $VM_CMD_RET -ne 0 ] && [ "$VM_ERR_CMD" != "" ]; then
            VM_REAL_ERR_CMD=`echo $VM_ERR_CMD | sed "s/%VM_ID%/$vm_id/g"`
            echo "Running: $VM_REAL_ERR_CMD"
            $PRE_CMD $VM_REAL_ERR_CMD
        fi
        exit $VM_CMD_RET
    ) &
    first_run_id=$(( $last_run_id + 1 ))
    total_run_ids=$(( $total_run_ids + $num_run_ids ))
done
echo "Assigned a total of RUN_ID=[$FIRST_RUN_ID;$last_run_id] ($total_run_ids RUN_IDs), waiting for VM workers..."
echo ""

FAILED_CMDS=0
for pid in `jobs -p`
do
    wait $pid || let "FAILED_CMDS+=1"
done

echo ""
echo "All done. $FAILED_CMDS command(s) failed."
date
