#!/bin/bash

PWD=`pwd`
LOCAL_ROOT=/home/koustubha/repositories/llvm-apps/apps/minix
SCRIPT_DIR=$LOCAL_ROOT/blobify-scripts
MTOOLS=${LOCAL_ROOT}/obj.i386/tooldir.`uname -s`-`uname -r`-`uname -m`/bin

: ${CC="${MTOOLS}/i586-elf32-minix-clang"}
: ${MTOOLS_DIR="${MTOOLS}/"}
: ${TARGET_DIR="$LOCAL_ROOT/outdir/prefixed_minix_bcl"}
: ${LLVMGOLD_PLUGIN="$LOCAL_ROOT/minix/minix/llvm/bin/LLVMgold.so"}
: ${OUTPUT_BC_FILE="$TARGET_DIR/serversblob.out.bcl"}
: ${OUTPUT_BC_REGMEM_FILE="$TARGET_DIR/serversblob.out.reg2mem.bcl"}
: ${LIB_DIR="$LOCAL_ROOT/obj.i386/destdir.i386/usr/lib/bc"}

LINK_FLAGS="-Wl,-g -Wl,-r -Wl,--plugin -Wl,${LLVMGOLD_PLUGIN} -Wl,-plugin-opt=emit-llvm -Wl,-zmuldefs"

[ -f ${OUTPUT_BC_FILE} ] && rm ${OUTPUT_BC_FILE}

echo "Blobifying Minix..."
echo ${CC}
${CC} -o ${OUTPUT_BC_FILE} -nostartfiles -nostdlib -L${LIB_DIR} ${LINK_FLAGS} `find $TARGET_DIR -maxdepth 1 -name "*.bcl"`
echo

echo "Running reg2mem pass on the bcl file..."
${MTOOLS_DIR}opt -reg2mem ${OUTPUT_BC_FILE} > ${OUTPUT_BC_REGMEM_FILE}

echo 
echo "Done. Output file: ${OUTPUT_BC_REGMEM_FILE}" 
