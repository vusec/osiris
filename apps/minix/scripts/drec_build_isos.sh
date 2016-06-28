#!/bin/bash

: ${MROOT=$mroot}
: ${CREATE_RC=1}
: ${TOTAL_RUNS=11}
: ${MINIX_XCHG_DRIVE="/dev/c0d0p2"}

LROOT=$MROOT/../..
MYPWD=`pwd`

set -e

if [ "${MROOT}" == "" ];
then
	echo "MROOT is not set. Please set it to apps/minix directory."
	exit 1
fi

cd $MROOT

# TEST RUN IDENTITY and setup
: ${tname="minix_drec"}

testset=$1
if [ "" == "$1" ]; then testset="unixbench"; fi
if [ "" != "$2" ]; then tname="$2"; fi

hname=`hostname`

# TESTDIR clean up

# Create MINIX rc script
if [ "${CREATE_RC}" == "1" ]
then

echo -n "Creating MINIX rc script for \"$testset\"... "

cat > minix/minix_x86.rc <<EOH
cat > /root/setup-response <<EOSR




1
yes
F
700
y

EOSR

cat > /root/drec_testset_${testset}.sh <<EOS1
#!/bin/sh
set -ex

# < < < Begin Test Execution

CURR_RUN_NUM=0
while [ "$TOTAL_RUNS" -gt "\\\${CURR_RUN_NUM}" ]
do
	CURR_RUN_NUM=\\\`if [ ! -f /root/runnum ]; then echo 1 | tee /root/runnum; else cat /root/runnum; fi\\\`

EOS1
EOH

case "$testset" in

		"unixbench")

		cat >> minix/minix_x86.rc <<EOU
		cat >> /root/drec_testset_${testset}.sh <<EOS2
		echo "Running UNIXBENCH [\\\$CURR_RUN_NUM/$TOTAL_RUNS]"
		cd /usr/benchmarks/unixbench
		#RUNS_0=1 RUNS_1=0 RUNS_2=0 RUNS_3=0 RUNS_4=0 RUNS_5=0 RUNS_6=0 RUNS_7=0 RUNS_8=0 RUNS_9=0 RUNS_10=0 RUNS_11=0 ./run_bc
		./run_bc
		tar cf ${MINIX_XCHG_DRIVE} results/
EOS2
EOU
		;;

		"minix")

		cat >> minix/minix_x86.rc <<EOM
		cat >> /root/drec_testset_${testset}.sh <<EOS3
			echo "Running MINIX POSIX tests [\\\$CURR_RUN_NUM/$TOTAL_RUNS]"
			cd /usr/tests/minix-posix
			QUICKTEST=yes ./run < /dev/console || true
EOS3
EOM
		;;

		*)
			echo "Test set specified doesn't exist. unixbench or minix?"
			exit 1
		;;

esac

cat >> minix/minix_x86.rc <<EOF

cat >> /root/drec_testset_${testset}.sh <<EOS4
	expr \\\${CURR_RUN_NUM} + 1 > /root/runnum
done
# > > > Finished Test execution

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
	# Can we write?
	touch /usr/.canwewrite

	if [ ! -f /usr/.canwewrite ]
	then
		echo "Current directory not writeable. Not launching tests."

cat > /root/minix-setup.sh <<EOMS
		echo "Launching MINIX 3 Setup..."
		export USER="root"
		printf "USER: %s\n\n Press any key to abort." "\\\$USER"

		cat	/root/setup-response | setup
		if [ $? -eq 0 ]
		then
			poweroff
		else
			printf "Setup failed. Please manually try installing MINIX 3."
		fi
EOMS

		chmod 755 /root/minix-setup.sh
		su root /root/minix-setup.sh

	else
		rm /usr/.canwewrite
  	chmod 755 /root/drec_testset_${testset}.sh
  	/root/drec_testset_${testset}.sh
	fi
fi
EOF

echo "	[done]"

fi

echo "Building image... (${tname}.iso)"
ISOMODE=1 IMG="${tname}.iso" ./clientctl buildimage > /dev/null
echo "	[done]"

cd $MYPWD
