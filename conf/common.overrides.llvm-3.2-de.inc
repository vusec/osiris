LLVMPREFIX=${LLVMBASEDIR}/llvm-3.2/bin
LLVMGOLD_CC="gcc-4.6 -fplugin=${LLVMGOLD_PREFIX}/lib/dragonegg.so -flto -B${LLVMGOLD_PREFIX}/dragonegg_bin"
LLVMGOLD_CXX="g++-4.6 -fplugin=${LLVMGOLD_PREFIX}/lib/dragonegg.so -flto -B${LLVMGOLD_PREFIX}/dragonegg_bin"

