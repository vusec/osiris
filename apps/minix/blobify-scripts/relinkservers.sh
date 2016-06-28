#!/bin/bash

LROOT="${lroot}"			# LLVM repository root
MROOT="${LOCAL_ROOT}"
MINIX_OBJ_DIR="${MROOT}/obj.i386"
LIBSYS_DIR="minix/lib/libsys"
: ${DBG="-O0 -g"}			# debug symbols enabled by default
ECHO=

for f in "`find ${MINIX_OBJ_DIR}/${LIBSYS_DIR} -name "*.*" -type f`"
do
	rm $f 2>/dev/null
done

for f in `find ${MINIX_OBJ_DIR}/minix/servers -name "*.o"`; do rm $f 2>/dev/null ; done;
for f in `find ${MINIX_OBJ_DIR}/minix/servers -name "*.bcl"`; do rm $f 2>/dev/null ; done;

SAVED_LLVM_CONF=""
if [ "" != "$LROOT" ]
then
	SAVED_LLVM_CONF="${LROOT}/common.overrides.llvm.inc.saved"
	mv "${LROOT}/common.overrides.llvm.inc" ${SAVED_LLVM_CONF} 
	cp ${LROOT}/conf/common.overrides.llvm-minix.inc $lroot/common.overrides.llvm.inc
fi

cd ${MROOT}
env "JOBS=8" \
env "GEN_GOLD_PLUGIN=NO" \
env "DBG=${DBG}" \
env "C=servers" \
./relink.llvm 

if [ "" != "${SAVED_LLVM_CONF}" ]
then
	mv ${SAVED_LLVM_CONF} "${LROOT}/common.overrides.llvm.inc"
fi
