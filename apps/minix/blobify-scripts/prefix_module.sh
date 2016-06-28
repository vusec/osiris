#!/bin/bash

############################
#
# Author: Koustubha Bhat
# Date  : 1-July-2014
# VU University, Amsterdam.
#
############################

# Prefixes input prefix to all GVs and functions 
# in the specified .bc file.

. ${HOME}/repositories/envlist
: ${SECTIONIFY_SO_PATH="$lroot/bin/sectionify.so"}
: ${MTOOLS_DIR="$mroot/obj.i386/tooldir.`uname -s`-`uname -r`-`uname -m`/bin/"}
TARGET_EXT="bcl"
PREFIX_EXT="pfx"
PREFIX_STR=
TARGET_BC_FILE=
OUTPUT_BC_FILE=
OUTPUT_DIR=./
ALSO_LL=0
EXITCODE_OPT=
EXITCODE_LLDIS=

usage()
{
  echo "${0}"
  echo "Prefixes the given string to all global variables"
  echo "and functions found in the specified LLVM bitcode file."
  echo 
  echo "Usage:"
  echo "$0 <prefix string> <path to target .bc file> [also-ll]"
  echo "Default output: <target filename>.<prefix string>.$PREFIX_EXT.bcl"
  echo "also-ll : Specifying this option generates the llvm-dis file as well."
  echo
}

run_opt()
{
  echo \"opt\" in use: `which ${MTOOLS_DIR}opt`
  ${MTOOLS_DIR}opt -load $SECTIONIFY_SO_PATH -sectionify -sectionify-section-map=.*/NULL  -sectionify-prefix=${PREFIX_STR} \
  ${TARGET_BC_FILE} > ${OUTPUT_DIR}/${OUTPUT_BC_FILE}

  EXITCODE_OPT=$?

  if [ ${ALSO_LL} -eq 1 ]
  then
	${MTOOLS_DIR}llvm-dis ${OUTPUT_DIR}/${OUTPUT_BC_FILE}
	EXITCODE_LLDIS=$?
  fi
} 

parse_cmd()
{
  PREFIX_STR=$1
  TARGET_BC_FILE=$2
  TARGET_BC_FILE_BASENAME=`basename ${TARGET_BC_FILE}`
  OUTPUT_BC_FILE="${TARGET_BC_FILE_BASENAME%.${TARGET_EXT}}.${PREFIX_STR}.$PREFIX_EXT.${TARGET_EXT}"

  if [ $# -ge 3 ]
  then 
	for v in $@ ; 
 	do
		if [ "$v" == "also-ll" ]
		then
			ALSO_LL=1
		fi

		if [[ $v =~ outdir:.* ]]
		then
			OUTPUT_DIR=${v#outdir:}
		fi	
	done
  fi
}

main()
{
  if [ $# -lt 2 ]
  then
	usage 
	exit 1
  fi

  parse_cmd $@

  run_opt
  
  exit ${EXITCODE_OPT}
}

main $@
