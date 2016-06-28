/******************************
* Author : Koustubha Bhat
* Jul-Oct 2014
* Vrije Universiteit, Amsterdam.
*******************************/
#ifndef FUSE_MINIX_PASS_H
#define FUSE_MINIX_PASS_H

#include <pass.h>
#include <fuseminix/common.h>

using namespace llvm;

namespace llvm {

class FuseMinixPass : public ModulePass
{
public:
   static char ID;
   FuseMinixPass();
   virtual bool runOnModule(Module &M);
   virtual void getAnalysisUsage(AnalysisUsage &AU) const;

private:
   Module *M;
};

}
#endif
