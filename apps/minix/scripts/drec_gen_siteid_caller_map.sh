#!/bin/bash

#set -x

if [ $# -lt 1 ]
then
  echo "Usage: $0 <server>"
  exit 1
fi

if [ "$MROOT" == "" ]
then
  echo "Please set MROOT and try again."
  echo "MROOT : path to llvm-apps/apps/minix directory"
  exit 1
fi

MTOOLS=$MROOT/obj.i386/"tooldir.`uname -s`-`uname -r`-`uname -m`"/bin
SRVBIN="$MROOT/obj.i386/minix/servers"
SERVER=$1

list_sites()
{
  ll_file="$SRVBIN/$SERVER/$SERVER.opt.bcl.ll"
  grep -o "ltckpt_set_.*(i64 [0-9]*" $ll_file | grep -o " [0-9]*" | tr -d ' ' | sort -n | uniq
}

siteid2caller()
{

SITE_ID=$1

tmp_file="/tmp/drec_callsite_func"
for i in 20 40 60 100 200 500;
do
	found=0
	grep -B $i "ltckpt_set_.*(i64\ $SITE_ID" $SRVBIN/$SERVER/$SERVER.opt.bcl.ll | tac | grep "define " | grep -o "@[a-zA-Z0-9_]*" > $tmp_file
	if [ $? -eq 0 ]; then
		found=1
		head -n 1 $tmp_file | tr -d "@"
		break
	fi 
done

if [ -f $tmp_file ]; then
	rm $tmp_file 2>/dev/null || true
fi

}

siteid2kcnumber()
{

SITE_ID=$1

KC_BASE=1536 #0x600
KC_ABS_NUM=`grep -A 1 "ltckpt_set_.*(i64\ $SITE_ID)" $SRVBIN/$SERVER/$SERVER.opt.bcl.ll | tail -n 1 | grep "_kernel_call" | grep -o "i32 [0-9]\+" | grep -o " [0-9]*" | tr -d ' '`

#echo $KC_ABS_NUM
if [ "" != "$KC_ABS_NUM" ]
then
	expr $KC_ABS_NUM - $KC_BASE
fi
}


if [ ! -f $SRVBIN/$SERVER/$SERVER.opt.bcl ]
then
	echo "Error: File not found: $SRVBIN/$SERVER/$SERVER.opt.bcl"
	exit 2
fi

if [ ! -f $SRVBIN/$SERVER/$SERVER.opt.bcl.ll ]
then
	$MTOOLS/llvm-dis $SRVBIN/$SERVER/$SERVER.opt.bcl
fi

for S in `list_sites $S`;
do
#  echo "$S `siteid2caller $S` `siteid2kcnumber $S`"
  printf "%s\t%-25s\t%2s\n" $S "`siteid2caller $S`" "`siteid2kcnumber $S`"
done


