#!/bin/bash

############################
#
# Author: Koustubha Bhat
# Date  : 1-July-2014
# VU University, Amsterdam.
#
############################

# Please set the following environment variables 
# SECTIONIFY_SO_PATH
# LOCAL_ROOT that points at the folder that hosts the Minix llvm-bitcode3 repository

: ${LOCAL_ROOT=""}
: ${SECTIONIFY_SO_PATH=""}

usage()
{
  if [ "" == "${LOCAL_ROOT}" ] || [ "" == "${SECTIONIFY_SO_PATH}" ]
  then
    echo "${0} -  Creates a single LLVM bitcode blob of Minix!"
    echo "Usage: "
    echo "Set appropriate values to following env. variables:"
    echo " LOCAL_ROOT 		: to folder that hosts Minix llvm-bitcode3 repository <1>"
    echo " SECTIONIFY_SO_PATH	: to the latest sectionify.so file from llvm-apps repository <2>"
    echo Example:
    echo "$ LOCAL_ROOT=<1> SECTIONIFY_SO_PATH=<2> ${0}" 
    echo
    echo "Optional:"
    echo "-rebuild-minix		: Deletes the lib/libsys/ binaries and rebuilds Minix with -O0 optimization level."
    echo "-relink-servers		: Deletes the lib/libsys/ binaries and relinks Minix servers with -O0 optimization level."
    exit 1
  fi

  if [ ! -f ./prefix_minix.sh ] || [ ! -f ./prefix_module.sh ] || [ ! -f ./buildblob.sh ] || [ ! -f ./rebuildminix.sh ]
  then
  	echo "Please run the script from \"blobify-scripts\" directory"
	exit 1
  fi
}

usage $@

if [ $# -eq 1 ]
then
  if [[ "$1" =~ "-rebuild-minix" ]]
  then
	env "LOCAL_ROOT=${LOCAL_ROOT}" \
	./rebuildminix.sh
  fi
  if [[ "$1" =~ "-relink-servers" ]]
  then
	env "LOCAL_ROOT=${LOCAL_ROOT}" \
	./relinkservers.sh
  fi
fi

./prefix_minix.sh
./buildblob.sh

