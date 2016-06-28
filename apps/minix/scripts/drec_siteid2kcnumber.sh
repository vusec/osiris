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

KC_BASE=1536 #0x600
KC_ABS_NUM=`grep -A 1 "ltckpt_set_.*(i64\ $SITE_ID)" $SRVBIN/$SERVER/$SERVER.opt.bcl.ll | tail -n 1 | grep "_kernel_call" | grep -o "i32 [0-9]\+" | grep -o " [0-9]*" | tr -d ' '`

if [ "$KC_ABS_NUM" != "" ]
then
	expr $KC_ABS_NUM - $KC_BASE
fi

exit 0
