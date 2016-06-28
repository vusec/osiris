#include <pass.h>

#include <llvm/Analysis/LoopInfo.h>
#include "Backports.h"

using namespace llvm;

namespace llvm {

class FaultInjector : public ModulePass {

  public:
      static char ID;

      FaultInjector();

      virtual void getAnalysisUsage(AnalysisUsage &AU) const;
      virtual bool runOnModule(Module &M);

      LoopInfo &getLoopInfo(Function *F){
            return FaultInjector::getAnalysis<LoopInfo>(*F);
      }

};

}

#define FAULTINJECTOR_H
#ifndef FAULT_H
#include "Fault.h"
#endif
