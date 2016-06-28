#!/bin/bash

: ${MROOT=$mroot}
: ${MTOOLS=$MROOT/obj.i386/"tooldir.`uname -s`-`uname -r`-`uname -m`"/bin}
: ${WINDOW_PROFILING=0}

LROOT=$MROOT/../..
MYPWD=`pwd`

: ${PATHOUTBASE:="$LROOT/results/minix"}

: ${BBCLONE=0}
: ${SETUP=1}

export COMP
export BBCLONE
export HYPER
export PERF
export TOGGLE_ENABLE_STATS

cd $MROOT

set -e

make_mt_disk()
{
  echo "boot" > $TESTDIR/.proto
  echo "1000 0" >> $TESTDIR/.proto
  echo "d--755 0 0" >> $TESTDIR/.proto
  echo "$"	>> $TESTDIR/.proto
  
  $MTOOLS/nbmkfs.mfs $1 $TESTDIR/.proto    
}

if [ "${MROOT}" == "" ]; 
then
	echo "MROOT is not set. Please set it to apps/minix directory."
	exit 1
fi

# TEST RUN IDENTITY and setup
tname=
testset=$1
if [ "" == "$1" ]; then testset="minix"; fi
if [ "" != "$2" ]; then tname="_$2"; fi

hname=`hostname`
t_id=`date +%Y-%m-%d-%H%M%S-$hname`$tname
TESTDIR="$PATHOUTBASE"
if [ ! -d $TESTDIR ]; then mkdir -p $TESTDIR; fi
if [ ! -d $TESTDIR/$t_id ]; then mkdir -p $TESTDIR/$t_id; fi

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

# Setup
if [ "$SETUP" == 1 ]
then
	# Optimistic strategy
	IPC_DEFAULT_DECISION=1 KC_DEFAULT_DECISION=1 ./scripts/setup_distr_recovery.sh $LROOT
fi


if [ "$RUN_RECOVERY" == 1 ]; then
echo -n "Running recovery instrumentation (COMP=$COMP)... " | tee $TESTDIR/$t_id/setup.log
IPC_DEFAULT_DECISION=1 KC_DEFAULT_DECISION=1 ./clientctl fuse recovery >> $TESTDIR/$t_id/setup.log 2>&1
echo "	[done]"
fi

# Create MINIX rc script

echo -n "Creating MINIX rc script for \"$testset\"... "

cat > minix/minix_x86.rc << EOH
cat > /root/drec_testset_${testset}.sh <<EOS1
#!/bin/sh
set -ex
EOS1
EOH

case "$testset" in 

"unixbench")

cat >> minix/minix_x86.rc << EOU
cat >> /root/drec_testset_${testset}.sh <<EOS2
cd /usr/benchmarks/unixbench
#RUNS_0=1 RUNS_1=0 RUNS_2=0 RUNS_3=0 RUNS_4=0 RUNS_5=0 RUNS_6=0 RUNS_7=0 RUNS_8=0 RUNS_9=0 RUNS_10=0 RUNS_11=0 ./run_bc
./run_bc
tar cf /dev/c0d1 results/
EOS2
EOU
;;

"minix")

cat >> minix/minix_x86.rc << EOM
cat >> /root/drec_testset_${testset}.sh <<EOS3
cd /usr/tests/minix-posix
QUICKTEST=yes ./run < /dev/console || true
#QUICKTEST=yes ./run -t "1 2" < /dev/console || true
EOS3
EOM
;;

*)
	echo "Test set specified doesn't exist. unixbench or minix?"
	exit 1
;;

esac

if [ "${WINDOW_PROFILING}" == "1" ]
then
	echo "Window profiling.. ON" | tee -a ${LOGFILE}
        cat >> minix/minix_x86.rc << EOP
        for p in pm rs vm ds vfs
        do
           echo "Enabling window profiling for service \$p : " 
           service fi \$p -fitype 2  
        done

EOP
fi

cat >> minix/minix_x86.rc << EOF

cat >> /root/drec_testset_${testset}.sh <<EOS4
for p in pm rs vm ds vfs
do
   echo "Profiling data for service \\\$p : " 
   service fi \\\$p -fitype 2
done
touch /root/done.testset
shutdown -pD now
poweroff
EOS4

if [ ! -f /root/done.testset ];
then
cd /root
chmod 755 /root/drec_testset_${testset}.sh
/root/drec_testset_${testset}.sh
fi
EOF

echo "	[done]"

echo "Building image..."
ROOT_SIZE=1048576 USR_SIZE=8388608 ./clientctl buildimage > /dev/null
echo "	[done]"


# Create results raw disk
echo "Creating raw disk for result collection... "
RES_IMG="drec_testset_${testset}.$t_id.img"
#sudo virt-make-fs --size=1M --type=minix $TESTDIR/$t_id $MROOT/$RES_IMG
make_mt_disk $MROOT/$RES_IMG
chmod 666 $MROOT/$RES_IMG
echo "	[done]"

echo "Launching MINIX to run Unixbench... "
echo "Start: `date +%Y-%m-%d-%H:%M:%S`" | tee -a ${LOGFILE}
#OUT=F NO_TERM_OUT=1 MEMSIZE=4096 RUNARGS="-hdb $MROOT/$RES_IMG" APPEND="ac_layout=1" ./clientctl run
MEMSIZE=4096 RUNARGS="-hdb $MROOT/$RES_IMG -serial file:$MROOT/minix/minix/llvm/serial.out" APPEND="ac_layout=1" ./clientctl run
echo "End: `date +%Y-%m-%d-%H:%M:%S`" | tee -a ${LOGFILE}
echo "	[done.]"

# Collect results
echo -n "Collecting results..."

mv $MROOT/minix/minix/llvm/serial.out $TESTDIR/$t_id
mv $MROOT/$RES_IMG $TESTDIR/$t_id
cd $TESTDIR/$t_id
tar xf $RES_IMG
rm $RES_IMG
echo "	[done]"

cd $MYPWD
