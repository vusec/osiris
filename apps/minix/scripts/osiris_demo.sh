#!/bin/bash

##########################################################################
# Description: OSIRIS demo
#
# It enables "suicides", which are artificially induced crashes in the
# PM server and lets unixbench run. The PM server is configured to
# not only recover when crashes occur within recovery-windows, but also to
# retry handling the last received request again. Needless to say, if at
# all a crash occurs outside a recovery-window, the recovery action is
# set to exhibit a fail-stop behaviour by aborting the entire system.
#
##########################################################################

: ${MROOT=$mroot}
: ${MTOOLS=$MROOT/obj.i386/"tooldir.`uname -s`-`uname -r`-`uname -m`"/bin}

LROOT=$MROOT/../..
MYPWD=`pwd`

: ${BBCLONE=0}
: ${HYPER="$LROOT/autosetup.dir/install/bin/qemu-system-x86_64 --enable-kvm"}
: ${TESTDIR=$MROOT/.results/osiris_demo/}
: ${T_ID=}

# Dual output specific settings
: ${SERIAL_FILE=$MROOT/serial.out}
: ${REF_SERIAL_FILE=$MROOT/ref.serial.out}
: ${FAULT_SERIAL_FILE=$MROOT/fault.serial.out}
: ${RES_IMG=$MROOT/.osiris_results.img}
: ${REF_IMG=$MROOT/ref.minix_x86.img}
: ${REF_RES_IMG=$MROOT/.ref.results.img}
: ${FAULT_IMG=$MROOT/fault.minix_x86.img}
: ${FAULT_RES_IMG=$MROOT/.fault.results.img}

# Script execution controllers
: ${SETUP=1}
: ${NO_RC_FILE=0}
: ${NO_SUICIDE=0}

export COMP
export BBCLONE
export HYPER
export PERF
export DONT_BUILD_MINIX

set -e

make_mt_disk()
{
  echo "boot" > $TESTDIR/.proto
  echo "1000 0" >> $TESTDIR/.proto
  echo "d--755 0 0" >> $TESTDIR/.proto
  echo "$"	>> $TESTDIR/.proto

  $MTOOLS/nbmkfs.mfs $1 $TESTDIR/.proto
}

# input: testset, tname
setup_workspace()
{
  rm $LROOT/common.overrides.*osiris.inc 2>/dev/null || true
  cp $LROOT/conf/common.overrides.demo.osiris.inc $LROOT/
  if [ $? -ne 0 ]; then
  	echo "OSIRIS demo llvm-apps overrides file missing."
  	exit 1
  fi

	# TEST RUN IDENTITY and setup
	tname=
	testset=$1
	if [ "" == "$1" ]; then testset="unixbench"; fi
	if [ "" != "$2" ]; then tname="_$2"; fi

	hname=`hostname`
  if [ "$T_ID" == "" ]; then T_ID=`date +%Y-%m-%d-%H%M%S-$hname`_${testset}$tname ; fi
  if [ "" == "${TESTDIR}" ]; then echo "TESTDIR not set. Exiting."; exit 1; fi
	if [ ! -d $TESTDIR ]; then mkdir -p $TESTDIR; fi
	if [ ! -d $TESTDIR/$T_ID ]; then mkdir -p $TESTDIR/$T_ID; fi
	# TESTDIR clean up
	if [ "$1" == "clean" ]
	then
		if [ "" != ${TESTDIR} ]
		then
			echo "Cleaned up the test results store : $TESTDIR"
			rm -rf $TESTDIR/*
			exit 0
		fi
		echo "Test results store not set."
		exit 1
	fi
  printf "%-70s\n" "Execution instance id: $T_ID" | tee $TESTDIR/$T_ID/setup.log
}

# Setup
build_and_instrument_minix()
{
  # Enable right set of compile-time flags
  export LTRC_REHANDLE_REQUEST=1
  export LTRC_SUICIDE_WHEN_RECOVERABLE=1
  export LTRC_SUICIDE_PERIODICALLY=1
  export LTRC_SUICIDE_ONLY_ONCE=0

	# Enhanced recovery model
	printf "%-70s\n" "Building OS and applying OSIRIS recovery instrumentations..." | tee -a $TESTDIR/$T_ID/setup.log
	IPC_DEFAULT_DECISION=4 KC_DEFAULT_DECISION=4 RECOVERYPASSOPTEXTRA="-recovery-ipc-force-decision=9" ./scripts/setup_distr_recovery.sh $LROOT
	printf "%70s\n" "[done]" | tee -a $TESTDIR/$T_ID/setup.log
}

enable_pm_suicides()
{
	# Suicide
	export COMP=pm
	printf "%-70s\n" "Enabling suicides (induced crashes) in component: $COMP..." | tee -a $TESTDIR/$T_ID/setup.log
	IPC_DEFAULT_DECISION=4 KC_DEFAULT_DECISION=4 ./clientctl fuse suicide >> $TESTDIR/$T_ID/setup.log 2>&1
	printf "%70s\n" "[done]" | tee -a $TESTDIR/$T_ID/setup.log
}

# Create MINIX rc script
# Input: env- testset, ref | fault
create_startup_script()
{
  if [ "$NO_RC_FILE" == "1" ]; then rm $MROOT/minix/*.rc 2>/dev/null || true ; return ; fi

	if [ "$testset" == "" ]
	then
		printf "%-70s\n" "Error: Test set not defined. Skipping creation of startup script." | tee -a $TESTDIR/$T_ID/setup.log
		return
	fi
	printf "%-70s\n" "Creating MINIX rc script for \"$testset\"... " | tee -a $TESTDIR/$T_ID/setup.log

# RC Script prologue
if [ "$1" == "ref" ];
then
  cat > minix/minix_x86.rc << EOH
  cat > /root/drec_testset_${testset}.sh <<EOS1
#!/bin/sh
set -e

EOS1
EOH
else
	cat > minix/minix_x86.rc << EOH
cat > /root/drec_testset_${testset}.sh <<EOS1
#!/bin/sh
set -e

#Enabling periodic suicides for PM
echo
echo "Enabling suicides in system service: PM"
service fi pm -fitype 2
EOS1
EOH
fi

	case "$testset" in

		"unixbench")

			cat >> minix/minix_x86.rc << EOU
cat >> /root/drec_testset_${testset}.sh <<EOS2
cd /usr/benchmarks/unixbench
echo
echo "Running Unixbench...   "

export HYPER=print_progress
RUNS_0=1 RUNS_1=0 RUNS_2=0 RUNS_3=0 RUNS_4=0 RUNS_5=0 RUNS_6=0 RUNS_7=0 RUNS_8=0 RUNS_9=1 RUNS_10=1 RUNS_11=0 ./run_bc

tar cf /dev/c0d1 results/
EOS2
EOU
		;;

		"minix")

			cat >> minix/minix_x86.rc << EOM
cat >> /root/drec_testset_${testset}.sh <<EOS3
cd /usr/tests/minix-posix
echo
echo "Running MINIX Test set...   "

export HYPER=console_print
QUICKTEST=yes ./run < /dev/console || true
echo "done."
EOS3
EOM
		;;

		*)
			echo "Error: Test set specified doesn't exist. unixbench or minix?"
			exit 1
		;;

	esac

# RC Script epilogue
	cat >> minix/minix_x86.rc << EOF

cat >> /root/drec_testset_${testset}.sh <<EOS4
touch /root/done.testset
# Show num suicides injected in PM
kill -USR2 5
#shutdown -pD now
#poweroff
EOS4

if [ ! -f /root/done.testset ];
then
  cd /root
  chmod 755 /root/drec_testset_${testset}.sh
  /root/drec_testset_${testset}.sh
fi
EOF

	printf "%70s\n" "[done]" | tee -a $TESTDIR/$T_ID/setup.log
}

# input: env- IMAGE
build_os_image()
{
	printf "%-70s\n" "Building image..." | tee -a $TESTDIR/$T_ID/setup.log
	ROOT_SIZE=1048576 USR_SIZE=8388608 ./clientctl buildimage > /dev/null
	printf "%70s\n" "[done]" | tee -a $TESTDIR/$T_ID/setup.log
}

# input image-type[: ref | fault]
mk_results_disk()
{
	# Create results raw disk
	printf "%-70s\n" "Creating raw disk for result collection... " >> $TESTDIR/$T_ID/setup.log
  local disk_image=
  local choice=$1
  case $choice in
      "ref")
          disk_image=${REF_RES_IMG}
          ;;
      "fault")
          disk_image=${FAULT_RES_IMG}
          ;;
          *)
          disk_image=${RES_IMG}
          ;;
  esac
	make_mt_disk ${disk_image}
	chmod 666 ${disk_image}
	printf "%70s\n" "$disk_image [done]" >> $TESTDIR/$T_ID/setup.log
}

# input: ref | fault
launch_os()
{
  cd $MROOT
  local choice=$1
	printf "%-70s\n" "Launching MINIX to run ${testset}... " | tee -a $TESTDIR/$T_ID/setup.log
	printf "%-70s\n" "Start: `date +%Y-%m-%d-%H:%M:%S`" | tee -a $TESTDIR/$T_ID/setup.log
  local image_not_found=0

  for img in ${REF_RES_IMG} ${FAULT_RES_IMG} ${RES_IMG};
  do
    rm $img 2>/dev/null || true
  done

  case $choice in
      "ref")
          mk_results_disk "ref"
          export SERIAL_FILE=${REF_SERIAL_FILE}
          export IMAGE=${REF_IMG}
          export RUNARGS="-hdb ${REF_RES_IMG} "
          if [ ! -f ${IMAGE} ]; then image_not_found=1; fi
          ;;

      "fault")
          mk_results_disk "fault"
          export SERIAL_FILE=${FAULT_SERIAL_FILE}
          export IMAGE=${FAULT_IMG}
          export RUNARGS="-hdb ${FAULT_RES_IMG} "
          if [ ! -f ${IMAGE} ]; then image_not_found=1; fi
          ;;

        *)
          export SERIAL_FILE
          export RUNARGS="-hdb ${RES_IMG} "
          ;;
  esac

  if [ $image_not_found -eq 1 ]
  then
    printf "%-70s\n" "Error: OS image not found : $IMAGE" | tee -a $TESTDIR/$T_ID/setup.log
    exit 1
  fi
  OUT=F NO_TERM_OUT=0 MEMSIZE=4096 APPEND="ac_layout=1" ./clientctl run 2>> $TESTDIR/$T_ID/setup.log

	printf "%-70s\n" "End: `date +%Y-%m-%d-%H:%M:%S`" | tee -a $TESTDIR/$T_ID/setup.log
	printf "%70s\n" "[done.]" | tee -a $TESTDIR/$T_ID/setup.log
}

collect_results()
{
	# Collect results
	printf "%-70s\n" "Collecting results..." | tee -a $TESTDIR/$T_ID/setup.log

  for img in $RES_IMG $REF_RES_IMG $FAULT_RES_IMG
  do
    if [ ! -f $img ]; then continue; fi
    mv $img $TESTDIR/$T_ID
  done
  for f in ${SERIAL_FILE} ${REF_SERIAL_FILE} ${FAULT_SERIAL_FILE}
  do
    if [ ! -f $f ]; then continue; fi
    mv $f $TESTDIR/$T_ID
  done

	cd $TESTDIR/$T_ID
  for img in `find . -name "*.img" -type f`
  do
    tar xf $img
	  rm $img
  done
	printf "%70s\n" "[done]" | tee -a $TESTDIR/$T_ID/setup.log
}

create_ref_image()
{
  create_startup_script "ref"
  IMAGE=${REF_IMG} build_os_image
}

create_fault_image()
{
    enable_pm_suicides
    create_startup_script "fault"
    IMAGE=${FAULT_IMG} build_os_image
}

prepare_for_demo()
{
  if [ "$SETUP" == "1" ]; then build_and_instrument_minix ; fi
  create_ref_image $@
  create_fault_image $@
}

dual_pane_demo()
{
  export T_ID
  which tmux > /dev/null 2>&1
  if [ $? -ne 0 ]; then echo "Please install tmux. [ sudo apt-get install tmux ]"; exit 1; fi
  which gnuplot > /dev/null 2>&1
  if [ $? -ne 0 ]; then echo "Please install gnuplot-x11. [ sudo apt-get install gnuplot-x11 ]"; exit 1; fi

  tmux new-session -d -s "graph_session"  "cd $MROOT/scripts; MROOT=$MROOT ${PROG_NAME} graph "
  tmux new-session -d -s "OSIRIS-Demo"
  # The horizontal splits way
#  tmux split-window -v  -c "${MROOT}/scripts"  "MROOT=$MROOT ${PROG_NAME} start_os ref"\; \
  tmux new-window -c "${MROOT}/scripts" "MROOT=$MROOT ${PROG_NAME} num_injected_faults"
  tmux split-window -v -p 90 -c "${MROOT}/scripts"  "MROOT=$MROOT ${PROG_NAME} start_os ref"\; \
       split-window -h -c "${MROOT}/scripts" "MROOT=$MROOT ${PROG_NAME} start_os fault"\; \
       attach-session -d -t "OSIRIS-Demo"
}

kill_tmux_sessions()
{
  tmux kill-session -t "graph_session"
  tmux kill-session -t "OSIRIS-Demo"
}

# input: ref | fault
get_rs_ready()
{
  if [ "$1" == "ref" ]; then
    grep "RS ready: " ${REF_SERIAL_FILE} | tail -n 1 >> ref.serial.dat
  else
    grep "RS ready: " ${FAULT_SERIAL_FILE} | tail -n 1 >> fault.serial.dat
  fi
}

live_rs_probe()
{
  export REF_DATA=ref.serial.dat
  export FAULT_DATA=fault.serial.dat
    while (true)
    do
      # if [ ! -f ${REF_SERIAL_FILE} ] || [ ! -f ${FAULT_SERIAL_FILE} ]; then
      #     continue
      # fi
      get_rs_ready "ref"
      get_rs_ready "fault"
    done
}

solo_demo()
{
	if [ "${MROOT}" == "" ];
	then
		printf "%-70s\n" "MROOT is not set. Please set it to apps/minix directory."
		exit 1
	fi

  cd $MROOT
	setup_workspace "unixbench"

	if [ "$SETUP" == "1" ]
	then
		build_and_instrument_minix
	fi

	if [ "${NO_SUICIDE}" != "1" ]
	then
		enable_pm_suicides
	fi

	if [ "$NO_RC_FILE" != "1" ];
	then
		create_startup_script
	else
 		rm $MROOT/minix/*.rc 2>/dev/null || true
	fi
	build_os_image
	mk_results_disk
	launch_os
	collect_results

	cd ${MYPWD}
}

main()
{
  PROG_NAME=$0
  c_arg=$1
  printf "%-70s\n" "task: $c_arg"
  shift

  cd $MROOT
  setup_workspace unixbench
  export T_ID

  case $c_arg in
      "prepare")
          prepare_for_demo $@
          ;;

      "run")
          dual_pane_demo
          collect_results
          kill_tmux_sessions
          ;;

      # for internal use
      "start_os")
          launch_os "$1"
          ;;

      "num_injected_faults")
          ref_res=1; fault_res=1;
          while [ $ref_res != 0 ] || [ $fault_res != 0 ];
          do
      	    if [ ! -f ${REF_SERIAL_FILE} ] || [ ! -f ${FAULT_SERIAL_FILE} ]; then
      	    	continue
      	    fi
            ref_out=`grep "Number of faults injected:" ${REF_SERIAL_FILE} 2>/dev/null | tail -1`
            if [ "$ref_out" != "" ]; then ref_res=0; fi
            fault_out=`grep "Number of faults injected:" ${FAULT_SERIAL_FILE} 2>/dev/null | tail -1`
            if [ "$fault_out" != "" ]; then fault_res=0; fi
          done
    	    ref_faults=`echo $ref_out | cut -d: -f 2`
    	    fault_faults=`echo $fault_out | cut -d: -f 2`
    	    echo "Reference VM     - Num injected faults: ${ref_faults}"
    	    echo "Fault induced VM - Num injected faults: ${fault_faults}"
          ;;

      "graph")
          export MROOT
      	  if [ ! -f $MROOT/scripts/osiris_demo.gplot ]; then
      		    echo "gplot script not found: $MROOT/scripts/osiris_demo.gplot"
      		      exit 1
      	  fi
          live_rs_probe &
          gnuplot -rv -p $MROOT/scripts/osiris_demo.gplot
          ;;

       *)
          main prepare $@
          main run $@
          ;;
  esac

  cd ${MYPWD}
}

main $@
