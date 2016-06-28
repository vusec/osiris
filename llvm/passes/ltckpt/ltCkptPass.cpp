#include "ltckpt/ltCkptPass.h"
#include <llvm/Support/CommandLine.h>

#if LLVM_VERSION >= 37
#define DEBUG_TYPE "ltckpt"
#endif

#define LTCKPT_STATIC_FUNCTIONS_SECTION "ltckpt_functions"
#define LTCKPT_HIDDEN_STR_PREFIX        ".str.ltckpt"

#define LTCKPT_METHOD_DEFAULT           "bitmap"

cl::opt<bool> ltckpt_inline("ltckpt_inline",
    cl::desc("Do not inline Storeinstrumentation"),
    cl::value_desc("inline_store_inst"));


cl::opt<bool> ltckpt_opt_vm("ltckpt_vm",
    cl::desc("Special casing for MINIX VM"),
    cl::value_desc("special casing for VM"));

static cl::opt<std::string>
ltckptMethod("ltckpt-method",
    cl::desc("Specify the checkpointing method to use."),
    cl::init(LTCKPT_METHOD_DEFAULT), cl::NotHidden, cl::ValueRequired);

PASS_COMMON_INIT_ONCE();

bool LtCkptPass::memIntrinsicToBeInstrumented(Instruction *inst)
{
	return false;
}


bool LtCkptPass::storeToBeInstrumented(Instruction *inst)
{
	return false;
}

void LtCkptPass::createHooks(Module &M)
{
	Constant *confSetupInitFunc = M.getFunction(CONF_SETUP_FUNC_NAME);
	assert(confSetupInitFunc != NULL);
	confSetupInitHook    = cast<Function>(confSetupInitFunc);

	Constant *confSetupFunc = LTCKPT_GET_HOOK(M, ltckptMethod, CONF_SETUP_FUNC_NAME);
	assert(confSetupFunc != NULL);
	confSetupHook    = cast<Function>(confSetupFunc);

	Constant *vmLateInitFunc = M.getFunction("ltckpt_undolog_vm_late_init");
	assert(vmLateInitFunc != NULL);
	vmLateInitHook  = cast<Function>(vmLateInitFunc);

	Constant *storeInstFunc = LTCKPT_GET_HOOK(M, ltckptMethod, STORE_HOOK_NAME);
	assert(storeInstFunc != NULL);
	storeInstHook    = cast<Function>(storeInstFunc);

	Constant *topOfTheLoopFunc  = LTCKPT_GET_HOOK(M, ltckptMethod, TOP_OF_THE_LOOP_FUNC_NAME);
	assert(topOfTheLoopFunc  != NULL);
	topOfTheLoopHook = cast<Function>(topOfTheLoopFunc);

	Constant *memcpyFunc  = LTCKPT_GET_HOOK(M, ltckptMethod, MEMCPY_FUNC_NAME);
	assert(memcpyFunc  != NULL);
	memcpyHook = cast<Function>(memcpyFunc);
	storeInstHook->setCallingConv(CallingConv::Fast);
	memcpyHook->setCallingConv(CallingConv::Fast);
}


bool LtCkptPass::instructionModifiesVar(Module &M, Instruction *inst, GlobalVariable* var)
{
	AliasAnalysis::ModRefResult result = AA->getModRefInfo(inst, var,
			DL->getTypeSizeInBits(var->getType()->getElementType()));

	return result == AliasAnalysis::Mod || result == AliasAnalysis::ModRef;
}


void LtCkptPass::instrumentStore(Instruction *inst)
{
	Instruction &in =*inst;
	std::vector<Value*> args(1);

	/* the signature of the storeinsthook is (ptr) */
	args[0] = new BitCastInst(static_cast<StoreInst&>(in).getPointerOperand(),
			Type::getInt8PtrTy(M->getContext()),
			"", inst);

	CallInst *callInst = PassUtil::createCallInstruction(storeInstHook, args,"",inst);

	callInst->setCallingConv(CallingConv::Fast);
		callInst->setIsNoInline();
	if(ltckpt_inline) {
#if LLVM_VERSION >= 37
		InlineFunctionInfo inlineFunctionInfo = InlineFunctionInfo(NULL);
#else
		InlineFunctionInfo inlineFunctionInfo = InlineFunctionInfo(NULL, DL);
#endif
		InlineFunction(callInst, inlineFunctionInfo);
	}
}

void LtCkptPass::instrumentRange(Value *ptr, APInt from, APInt size,  Instruction *inst)
{
  std::string type_str;
  llvm::raw_string_ostream rso(type_str);

  std::vector<Value*> args(2);
  IRBuilder<> IRB(inst->getParent());
  /* Let's create a uint8ptr to the beginning of the object */
  ptr =  new BitCastInst(ptr, Type::getInt8PtrTy(M->getContext()), "", inst);
  /* now let's add the offset */
#if LLVM_VERSION >= 37
  ptr = GetElementPtrInst::Create(Type::getInt8Ty(M->getContext()), ptr, ConstantInt::get(M->getContext(),from),"",inst);
#else
  ptr = GetElementPtrInst::Create(ptr, ConstantInt::get(M->getContext(),from),"",inst);
#endif

  args[0] = ptr;
  args[1] = ConstantInt::get(M->getContext(),size);

  rso << "source type: ";
  args[0]->getType()->print(rso);
  rso << " size_type: ";
  args[1]->getType()->print(rso);

  /* sometimes we get a i64 for the size */
#if LLVM_VERSION >= 37
  if (DL->getPointerSizeInBits() == 32) {
#else
  if (M->getPointerSize() == Module::Pointer32) {
#endif
    if (static_cast<const IntegerType*>(args[1]->getType())->getBitWidth() != 32)  {
      args[1] = new TruncInst(args[1], Type::getInt32Ty(M->getContext()), "", inst);
      rso << " size_T to: ";
      args[1]->getType()->print(rso);
    }
  } else {
    if (static_cast<const IntegerType*>(args[1]->getType())->getBitWidth() != 64)  {
      args[1] = new TruncInst(args[1], Type::getInt64Ty(M->getContext()), "", inst);
      rso << " size_T to: ";
      args[1]->getType()->print(rso);
    }
  }

  ltckptPassLog(rso.str() << "\n");

  CallInst *callInst = PassUtil::createCallInstruction(memcpyHook,args,"",inst);
  callInst->setCallingConv(CallingConv::Fast);
  callInst->setIsNoInline();
  #if 0
  InlineFunctionInfo inlineFunctionInfo = InlineFunctionInfo(NULL, DL);
  InlineFunction(callInst, inlineFunctionInfo);
  #endif
}


void LtCkptPass::instrumentMemIntrinsic(Instruction *inst)
{
	std::string type_str;
	llvm::raw_string_ostream rso(type_str);

	MemIntrinsic &in = static_cast<MemIntrinsic&>(*inst);

	std::vector<Value*> args(2);

	args[0] = new BitCastInst(in.getDest(), Type::getInt8PtrTy(M->getContext()),
			"", inst);
	args[1] = in.getLength();

	rso << "source type: ";
	args[0]->getType()->print(rso);
	rso << " size_type: ";
	args[1]->getType()->print(rso);

	/* sometimes we get a i64 for the size */
#if LLVM_VERSION >= 37
	if (DL->getPointerSizeInBits() == 32) {
#else
	if (M->getPointerSize() == Module::Pointer32) {
#endif
		if (static_cast<const IntegerType*>(args[1]->getType())->getBitWidth() != 32)  {
			args[1] = new TruncInst(args[1], Type::getInt32Ty(M->getContext()), "", inst);
			rso << " size_T to: ";
			args[1]->getType()->print(rso);
		}
	} else {
		if (static_cast<const IntegerType*>(args[1]->getType())->getBitWidth() != 64)  {
			args[1] = new TruncInst(args[1], Type::getInt64Ty(M->getContext()), "", inst);
			rso << " size_T to: ";
			args[1]->getType()->print(rso);
		}
	}

	ltckptPassLog(rso.str() << "\n");

	CallInst *callInst = PassUtil::createCallInstruction(memcpyHook,args,"",inst);
	callInst->setCallingConv(CallingConv::Fast);
		callInst->setIsNoInline();
#if 0
	InlineFunctionInfo inlineFunctionInfo = InlineFunctionInfo(NULL, DL);
	InlineFunction(callInst, inlineFunctionInfo);
#endif
}

bool LtCkptPass::isInLtckptSection(Function *F) {
        return !std::string(F->getSection()).compare(LTCKPT_STATIC_FUNCTIONS_SECTION);
}

bool LtCkptPass::instrumentTopOfTheLoop(Function &F)
{
	std::vector<Value*> args(0);
	for (Function::iterator it = F.begin(); it != F.end(); ++it) {
		BasicBlock *bb = it;
		for (BasicBlock::iterator it2 = bb->begin(); it2 != bb->end(); ++it2) {
			Instruction *inst = it2;
			PassUtil::createCallInstruction(topOfTheLoopHook,args, "", inst);
			return true;
		}
	}
	return true;
}

bool LtCkptPass::instrumentConfSetup(Function &F)
{
	std::vector<Value*> args(0);
	BasicBlock *BB = F.begin();
	Instruction *I = BB->begin();
	PassUtil::createCallInstruction(confSetupHook, args, "", I);

	return true;
}

LtCkptPass::LtCkptPass() : ModulePass(ID) {}


void LtCkptPass::getAnalysisUsage(AnalysisUsage &AU) const
{
#if LLVM_VERSION >= 37
  AU.addRequired<DominatorTreeWrapperPass>();
#else
  AU.addRequired<DominatorTree>();
	AU.addRequired<DATA_LAYOUT_TY>();
#endif
	AU.addRequired<AliasAnalysis>();
}


bool LtCkptPass::onFunction(Function &F) {
	return false;
}


bool LtCkptPass::runOnModule(Module &M) 
{

	bool mod = false;

	this->M = &M;

	AA = &getAnalysis<AliasAnalysis>();
#if LLVM_VERSION >= 37
	DL = &M.getDataLayout();
#else
	DL = &getAnalysis<DATA_LAYOUT_TY>();
#endif

	if (!AA || !DL) {
		ltckptPassLog("could not get AnalysisPass\n");
	}

	Module::GlobalListType &globalList = M.getGlobalList();

	for (Module::global_iterator it = globalList.begin(); it != globalList.end(); ++it) {
		GlobalVariable *GV = it;
		/* we do not care about constants */
		if (GV->isConstant()) {
			continue;
		}
		globalVariables.push_back(it);
	}

	ltckptPassLog("Number of global variables found in Module: " << globalVariables.size() << "\n");

	createHooks(M);

	Module::FunctionListType &funcs = M.getFunctionList();

	for (Module::iterator mi = funcs.begin(), me = funcs.end(); mi!=me ; ++mi) {
		mod |= onFunction(*mi);
		if (ltckpt_opt_vm && mi->getName().startswith("main")) {
			/* we have to place the vm specific init hook here */
			for (auto fi = mi->begin(); fi != mi->end(); ++fi) {
				for (auto bbi = fi->begin(); bbi!= fi->end(); ++bbi) {
					if (CallInst *CI =  dyn_cast<CallInst>(bbi)) {
						Function *CF = CI->getCalledFunction();
						if(CF && CF->getName().equals("init_vm")) 
						{
							std::vector<Value*> args(0);
							std::cout << "PLACING VM_LATEINITHOOK\n";
							PassUtil::createCallInstruction(vmLateInitHook, args, "", ++bbi);
						}
					}
				}
			}
		}
	}

	ltckptPassLog("Done.\n");

	return mod;
}


char LtCkptPass::ID = 0;


RegisterPass<LtCkptPass> W("ltckpt", "Lightweight Checkpointing Pass");
