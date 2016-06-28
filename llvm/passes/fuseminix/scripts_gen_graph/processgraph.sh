#!/bin/bash

################################
# Author: Koustubha Bhat
# Date  : 7-Oct-2014
# Vrije Universiteit, Amsterdam.
################################
FUNC_NAME=$1
DOT_FILE=$2
OUT_DIR="outdir"

usage()
{
    echo "Usage: "
    echo "$0 <function name> <dot file> [<outdir>]"
    echo "$0 -list <dot file> - Lists the available functions"
}

process_cmdline()
{
  if [ $# -lt 2 ];
  then
    usage
    exit 1
  fi
  if [[ "$1" =~ "-list" ]]
  then
	grep "label" ${DOT_FILE} | grep -o "{.*}" | tr -d "{" | tr -d "}"
	exit 0
  fi

  [ $# -ge 3 ] && OUTDIR=$3

  [ -d "$OUTDIR" ] || mkdir -p "${OUT_DIR}"

  if [ ! -f ./reach.sh ] || [ ! -f ./reach.g ] || [ ! -f ./select.g ]
  then
	echo "Error: Wrong directory. Essential scripts not found."
	exit 2
  fi
}

process_cmdline $@

nodes=`grep "$FUNC_NAME" ${DOT_FILE} | grep -o "Node[0-9a-z]*"`
echo "${FUNC_NAME} : $nodes"

for n in $nodes
do
  bash ./reach.sh $n ${DOT_FILE} ${FUNC_NAME}_$n.dot "Callgraph:${FUNC_NAME}"
done

printf "Converting dot files to jpg files\n"
for n in $nodes
do 
  dot -Tjpg ${FUNC_NAME}_$n.dot -o ${OUT_DIR}/${FUNC_NAME}_$n.jpg
#  dot -Tps ${FUNC_NAME}_$n.dot -o ${OUT_DIR}/${FUNC_NAME}_$n.ps
#  convert ${OUT_DIR}/${FUNC_NAME}_$n.ps ${OUT_DIR}/${FUNC_NAME}_$n.jpg
#  convert ${OUT_DIR}/${FUNC_NAME}_$n.ps ${OUT_DIR}/${FUNC_NAME}_$n.pdf
done

[ ! -d "merged" ] && mkdir -p "./merged"

printf "Merging nodes into their module groups\n"
for n in $nodes
do
  gvpr -f merge.g ${FUNC_NAME}_$n.dot > ${FUNC_NAME}_$n_merged.dot
  dot -Tjpg ${FUNC_NAME}_$n_merged.dot -o "./merged"/${FUNC_NAME}_$n_merged.jpg
done

printf "\nFinished.\n"
