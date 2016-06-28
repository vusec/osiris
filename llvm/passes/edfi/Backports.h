#ifndef BACKPORTS_H
#define BACKPORTS_H

#include <pass.h>

using namespace llvm;

namespace llvm {

class Backports {
  public:

      //From DbgInfoPrinter.cpp (LLVM 2.9)
      static Value *findDbgGlobalDeclare(GlobalVariable *V);
      static Value *findDbgSubprogramDeclare(Function *V);
      static const DbgDeclareInst *findDbgDeclare(const Value *V);

      //From llvm/lib/Transforms/IPO/StripSymbols.cpp (LLVM 2.9)
      static bool StripDebugInfo(Module &M);

      //Adapted from llvm/lib/Transforms/Utils/BasicBlockUtils.cpp 
      static TerminatorInst *SplitBlockAndInsertIfThen(Instruction *Cmp);
};

}

#endif
