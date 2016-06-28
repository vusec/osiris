#!/bin/bash

if [ $# -lt 2 ]
then
  echo "Usage: $0 <server> <site id>"
  exit 1
fi

if [ "$MROOT" == "" ]
then
  echo "Please set MROOT env variable and try again."
  echo "MROOT : path to llvm-apps/apps/minix directory"
  exit 1
fi

MTOOLS=$MROOT/obj.i386/"tooldir.`uname -s`-`uname -r`-`uname -m`"/bin
SRVBIN="$MROOT/obj.i386/minix/servers"
SERVER=$1
SITE_ID=$2

if [ ! -f $SRVBIN/$SERVER/$SERVER.opt.bcl ]
then
	echo "Error: File not found: $SRVBIN/$SERVER/$SERVER.opt.bcl"
	exit 2
fi

if [ ! -f $SRVBIN/$SERVER/$SERVER.opt.bcl.ll ]
then
	$MTOOLS/llvm-dis $SRVBIN/$SERVER/$SERVER.opt.bcl
fi

tmp_file="/tmp/drec_callsite_func"
for i in 20 40 60 100 200 500;
do
	found=0
	grep -B $i "ltckpt_.*(i64\ $SITE_ID" $SRVBIN/$SERVER/$SERVER.opt.bcl.ll | tac | grep "define " | grep -o "@[a-zA-Z0-9_]*" > $tmp_file
	if [ $? -eq 0 ]; then
		found=1
		head -n 1 $tmp_file | tr -d "@"
		break
	fi 
done

if [ -f $tmp_file ]; then
	rm $tmp_file 2>/dev/null || true
fi
