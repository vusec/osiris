#include <pass.h>

using namespace llvm;

namespace llvm
{

class MinixDummyPass : public ModulePass {
public:
  static char ID;
  MinixDummyPass();
  virtual bool runOnModule(Module &M);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
};

MinixDummyPass::MinixDummyPass() : ModulePass(ID){}

bool MinixDummyPass::runOnModule(Module &M)
{
  return false;
}

void
MinixDummyPass::getAnalysisUsage(AnalysisUsage &AU) const
{
}

char MinixDummyPass::ID;
RegisterPass<MinixDummyPass> MDP("minix",
  "Inline the Minix IPC site if message/destination is ambiguous");
}
