LLVMPREFIX=${LLVMBASEDIR}/llvm-3.7/bin
LLVMGOLD_LINK=""
LLVMGOLD_CC="${LLVMGOLD_PREFIX}/bin/clang -flto"
LLVMGOLD_CXX="${LLVMGOLD_PREFIX}/bin/clang++ -flto"
LLVMGOLD_LINKFLAGS="-Wl,-plugin-opt=-disable-fp-elim -Wl,-plugin-opt=save-temps"
CFLAGS+=-DLLVM_HAS_MPX=1
