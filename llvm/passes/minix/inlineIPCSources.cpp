#include <pass.h>

#if LLVM_VERSION >= 37
#define DEBUG_TYPE "inlineminixipcsites"
#endif

#include <fuseminix/IPCSourceShaper.h>

using namespace llvm;
DSA_UTIL_INIT_ONCE();
namespace llvm
{



class InlineMinixIPCSitesPass : public ModulePass
{
public:
  static char ID;
  InlineMinixIPCSitesPass();
  virtual bool runOnModule(Module &M);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
private:
  Module *M;
};

InlineMinixIPCSitesPass::InlineMinixIPCSitesPass()
    : ModulePass(ID){}
    

bool InlineMinixIPCSitesPass::runOnModule(Module &M)
{
  this->M = &M;
  IPCSourceShaper sourceshaper(&M, this);
  if (false == sourceshaper.handleSendrecCallers())
  {
    DEBUG(errs() << "IPC source analysis failed.\n");
    return true;
  }
  return true;
}

void
InlineMinixIPCSitesPass::getAnalysisUsage(AnalysisUsage &AU) const
{
#if LLVM_VERSION >= 37
  AU.addRequired<LoopInfoWrapperPass>();
#else
  AU.addRequired<LoopInfo>();
#endif
}

char InlineMinixIPCSitesPass::ID;
RegisterPass<InlineMinixIPCSitesPass> FMP("inlineminixipcsites",
        "Inline the Minix IPC site if message/destination is ambiguous");

}
