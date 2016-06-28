#!/bin/bash

# gdb tests
export OLDPATHX=$PATH
#export PATH=/home/skl/gdb/bin:$PATH
cd /home/skl/gdb_test
./run.sh 
cd -
export PATH=$OLDPATHX
