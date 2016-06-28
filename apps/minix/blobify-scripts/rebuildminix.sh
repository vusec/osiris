#!/bin/bash

MROOT="${LOCAL_ROOT}"
MINIX_OBJ_DIR="${MROOT}/obj.i386"
LIBSYS_DIR="minix/lib/libsys"
ECHO=

for f in "`find ${MINIX_OBJ_DIR}/${LIBSYS_DIR} -name "*.*" -type f`"
do
	rm $f
done
for f in "`find ${MINIX_OBJ_DIR}/minix/servers -name "*.*" -type f`"
do
	rm $f
done

cd ${MROOT}
env "JOBS=8" \
env "GEN_GOLD_PLUGIN=NO" \
env "DBG=-O0 -g" \
./configure.llvm 
