###########################################################
# Minix specific overrides
###########################################################
BUILD_LLVMAPPS_FOR_MINIX="yes"
GEN_GOLD_PLUGIN="yes"
REBUILD_MINIX="yes"

###########################################################
# llvm-apps settings for Minix binary instrumentation
###########################################################
_MINIX_ARCH=i386
_MINIX_ROOT="/notset"
_MINIX_LLVM_DIR="${_MINIX_ROOT}/minix/llvm"
_MINIX_OBJ_DIR="${_MINIX_ROOT}/../obj.${_MINIX_ARCH}"
_MINIX_OBJ_LLVM_DIR="${_MINIX_ROOT}/../obj_llvm.${_MINIX_ARCH}"
_MINIX_TOOLS_DIR="${_MINIX_OBJ_DIR}/tooldir.notset"
_MINIX_SYSROOT="${_MINIX_OBJ_DIR}/destdir.${_MINIX_ARCH}"

###########################################################
# LLVMGOLD overrides for Minix (todo: CC/CXX should point to Minix' clang/clang++, not Linux')
###########################################################
LLVMGOLD_HAS_LTO_DISABLE_OPT=1
LLVMPREFIX="${_MINIX_OBJ_LLVM_DIR}/Release+Asserts"
LLVMGOLD_PLUGIN="${_MINIX_LLVM_DIR}/bin/LLVMgold.so"

LLVMGOLD_CC="${_MINIX_TOOLS_DIR}/bin/i586-elf32-minix-clang -flto"
LLVMGOLD_CXX="${_MINIX_TOOLS_DIR}/bin/i586-elf32-minix-clang++ -flto"
CC="clang"
CXX="clang++"

LLVMGOLD_CFLAGS="-fasynchronous-unwind-tables -g -D_MINIX -D_POSIX_SOURCE -D_SYSTEM -Wno-long-long --sysroot=${_MINIX_SYSROOT} -I${LLVMINC} -I${_MINIX_OBJ_LLVM_DIR}/include/llvm -I${_MINIX_ROOT}/external/bsd/llvm/dist/llvm/include -I${_MINIX_OBJ_LLVM_DIR}/include -I${_MINIX_SYSROOT}/usr/include ${DISTREC_CFLAGS}"
LLVMGOLD_LINKCOMMONFLAGS+="-L${_MINIX_OBJ_DIR}/destdir.${_MINIX_ARCH}/usr/lib"
LLVMGOLD_LIBS=""
LLVMGOLD_LINK="-Wl,--plugin -Wl,${LLVMGOLD_PLUGIN}"

