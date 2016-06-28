#ifndef LTCKPT_LTCKPT_H
#define LTCKPT_LTCKPT_H

#include <pass.h>
#include <list>
#include <assert.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#define LTCKPT_GET_HOOK(M, LM, LH) M.getFunction(LH "_" + LM);

#define CONF_SETUP_FUNC_NAME "ltckpt_conf_setup"
#define CONF_SETUP_FUNC_NAME_VM "ltckpt_conf_setup_vm"

#define STORE_HOOK_NAME "ltckpt_store_hook"
#define TOP_OF_THE_LOOP_FUNC_NAME "ltckpt_top_of_the_loop"

#define MEMCPY_FUNC_NAME  "ltckpt_memcpy_hook"

#define ltckptPassLog(M) DEBUG(dbgs()  << M )
#define WORST_CASE_SCENARIO 0

using namespace llvm;

namespace llvm {

	class LtCkptPass : public ModulePass {
		public:
			static char ID;
			LtCkptPass();
			LtCkptPass(char ID);
			virtual bool runOnModule(Module &M);
			virtual void getAnalysisUsage(AnalysisUsage &AU) const;
			void instrumentRange(Value *ptr, APInt from, APInt size,  Instruction *inst);

		protected:
			Function *confSetupInitHook;
			Function *confSetupHook;
			Function *vmLateInitHook;
			Function *storeInstHook;
			Function *memcpyHook;
			Function *topOfTheLoopHook;
			std::vector<GlobalVariable*> globalVariables;
			bool isInLtckptSection(Function *f);
			void instrumentStore(Instruction *inst);
			void instrumentMemIntrinsic(Instruction *inst);
			bool instrumentTopOfTheLoop(Function &F);
			bool instrumentConfSetup(Function &F);
			bool instructionModifiesVar(Module &M, Instruction *inst, GlobalVariable* var);
			virtual bool onFunction(Function &F);
			bool virtual storeToBeInstrumented(Instruction *inst);
			bool virtual memIntrinsicToBeInstrumented(Instruction *inst);
			AliasAnalysis *AA;
#if LLVM_VERSION >= 37
			const DATA_LAYOUT_TY *DL;
#else
			DATA_LAYOUT_TY *DL;
#endif
		    Module *M;

		private:
			void createHooks(Module &M);
	};

} /* namespace llvm */

#endif
