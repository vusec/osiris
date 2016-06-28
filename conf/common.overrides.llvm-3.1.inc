LLVMPREFIX=${LLVMBASEDIR}/llvm-3.1/bin
LLVMGOLD_LINK=""
LLVMGOLD_LINKFLAGS="-Wl,-plugin-opt=-disable-inlining -Wl,-plugin-opt=also-emit-llvm"
LLVMGOLD_CC="${LLVMGOLD_PREFIX}/bin/clang -flto"
LLVMGOLD_CXX="${LLVMGOLD_PREFIX}/bin/clang++ -flto"

