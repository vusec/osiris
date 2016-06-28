/******************************
* Author : Koustubha Bhat
* Jul-Oct 2014
* Vrije Universiteit, Amsterdam.
*******************************/
#ifndef FUSE_MINIX_HELPER_H
#define FUSE_MINIX_HELPER_H

#if LLVM_VERSION >= 37
#include <llvm/IR/InstIterator.h>
#else
#include <llvm/Support/InstIterator.h>
#endif
#include <llvm/Support/GraphWriter.h>
#include <common/dsa_common.h>
#include <fuseminix/common.h>

namespace llvm
{

class FuseMinixHelper
{
public:
	FuseMinixHelper(Module *M);
	FuseMinixHelper(Module *M, DSAUtil *dsau);
	bool hasMinixMessageParameter(Function *function);
	bool getConstantIntValue(Argument *argument, int &intValue);
	bool getConstantIntValue(Value *value, int &intValue);
	Value* getMinixMessageArgument(const CallSite *callSite);
	bool inlineFunctionAtCallSite(CallSite *callSite);
	bool getM_TYPEValues(CallSite *sendrecCallSite, int &mtypeValue);
	bool getEndpointValue(CallSite *sendrecCallSite, int &endpointValue);
	bool replaceCallInst(CallSite *callSite, Function* functionToCall, bool useNoArgs=false);
	bool replaceCallInst(CallInst *callInst, Function* functionToCall);
	bool noopIt(CallSite *callSite, std::map<FunctionType*, Function*> &noopFunctionsMap);
	void dumpDSGraph(DSAUtil *dsau, std::vector<Function*> &functions);
	void dumpCallGraph(ModulePass *MP);

private:
	DSAUtil *dsau;
	Module	*module;

	bool isMinixMessageType(Argument *argument);
	bool isMinixMessageType(Value *value);
	bool getUsers(Value *value, Instruction *untilInstr, std::deque<User*> &users);
	bool getUsers(std::vector<Instruction*> &instrsUntilCallSite, StoreInst *storeInst, std::deque<User*> &users);
	bool getUsers(LoadInst *loadInst, std::deque<User*> &users);

};

FuseMinixHelper::FuseMinixHelper(Module *M)
{
	module = M;
	this->dsau = NULL;
}

FuseMinixHelper::FuseMinixHelper(Module *M, DSAUtil *dsau)
{
	module = M;
	this->dsau = dsau;
}

bool FuseMinixHelper::replaceCallInst(CallSite *callSite, Function* functionToCall, bool useNoArgs)
{
	if (NULL == callSite || NULL == functionToCall)
	{
		return false;
	}

	std::vector<Value*> *args = NULL;
	if (useNoArgs)
	{
		args = new std::vector<Value*>();
	}
	else
	{
		args = new std::vector<Value*>(callSite->arg_begin(), callSite->arg_end());
	}
	CallInst *newInst = NULL;
	Instruction *insertPoint = callSite->getInstruction();
	CallInst *originalInst = dyn_cast<CallInst>(callSite->getInstruction());
	DEBUG(errs() << "Num args: " << args->size() << "\n");
	newInst = PassUtil::createCallInstruction(functionToCall, *args, "", insertPoint);
	if (NULL == newInst)
	{
		return false;
	}

	SmallVector< std::pair< unsigned, MDNode * >, 8> MDs;
    originalInst->getAllMetadata(MDs);
    for(unsigned i=0;i<MDs.size();i++)
    {
        newInst->setMetadata(MDs[i].first, MDs[i].second);
    }
    int argOffset = 0;
    CallingConv::ID CC = callSite->getCallingConv();
    newInst->setCallingConv(CC);
    ATTRIBUTE_SET_TY NewAttrs = PassUtil::remapCallSiteAttributes(*callSite, argOffset);
    newInst->setAttributes(NewAttrs);

  	// If the old instruction was an invoke, add an unconditional branch
    // before the invoke, which will become the new terminator.
    if (InvokeInst *II = dyn_cast<InvokeInst>(originalInst))
    {
      BranchInst::Create(II->getNormalDest(), originalInst);
    }

    if (useNoArgs)
    {
    	originalInst->dropAllReferences();
    }
    else
    {
    	originalInst->replaceAllUsesWith(newInst);
    }

    // Delete the old call site
    originalInst->eraseFromParent();
    return true;
}

bool FuseMinixHelper::replaceCallInst(CallInst *callInst, Function* functionToCall)
{
	if (NULL == callInst || NULL == functionToCall)
	{
		return false;
	}

	std::vector<Value*> *args = new std::vector<Value*>();
	unsigned numArgs = callInst->getNumArgOperands();
	for (unsigned i = 0; i < numArgs; i++)
	{
		args->push_back(callInst->getArgOperand(i));
	}

	DEBUG(errs() << "Creating new instr.\n");
	CallInst *newInst = NULL;
	Instruction *insertPoint = dyn_cast<Instruction>(callInst);
	CallInst *originalInst = callInst;
	DEBUG(errs() << "Num args: " << args->size() << "\n");
	newInst = PassUtil::createCallInstruction(functionToCall, *args, "", insertPoint);
	if (NULL == newInst)
	{
		return false;
	}

	DEBUG(errs() << "Created new call instruction.\n");

	SmallVector< std::pair< unsigned, MDNode * >, 8> MDs;
    originalInst->getAllMetadata(MDs);
    for(unsigned i=0;i<MDs.size();i++)
    {
        newInst->setMetadata(MDs[i].first, MDs[i].second);
    }

    CallingConv::ID CC = callInst->getCallingConv();
    newInst->setCallingConv(CC);

  	// If the old instruction was an invoke, add an unconditional branch
    // before the invoke, which will become the new terminator.
    if (InvokeInst *II = dyn_cast<InvokeInst>(originalInst))
    {
      BranchInst::Create(II->getNormalDest(), originalInst);
    }
    DEBUG(errs() << "About to replace all uses wth the new one.\n");
    originalInst->replaceAllUsesWith(newInst);

    // Delete the old call site
    originalInst->eraseFromParent();
    return true;
}


bool FuseMinixHelper::isMinixMessageType(Argument *argument)
{
  if (NULL == argument)
  {
  	DEBUG(errs() << "Error: Argument NULL\n");
    return false;
  }

  Type *potentialStructType = NULL;
  if (argument->getType()->isPointerTy())
  {
    potentialStructType = argument->getType()->getPointerElementType();
  }
  else if (argument->getType()->isStructTy())
  {
    potentialStructType = argument->getType();
  }
  if (NULL == potentialStructType)
  {
  	return false;
  }
  if (potentialStructType->isStructTy())
  {
    if (std::string::npos != (potentialStructType->getStructName().find(MINIX_STRUCT_MESSAGE)))
    {
      return true;
    }
  }

  return false;
}

bool FuseMinixHelper::isMinixMessageType(Value *value)
{
  if (NULL == value)
  {
  	  	DEBUG(errs() << "Error: Argument NULL\n");
    return false;
  }

  Type *potentialStructType = NULL;
  if (value->getType()->isPointerTy())
  {
    potentialStructType = value->getType()->getPointerElementType();
  }
  else if (value->getType()->isStructTy())
  {
    potentialStructType = value->getType();
  }

  if (NULL == potentialStructType)
  {
  	return false;
  }
  if (potentialStructType->isStructTy())
  {
    if (std::string::npos != (potentialStructType->getStructName().find(MINIX_STRUCT_MESSAGE)))
    {
      return true;
    }
  }

  return false;
}

bool FuseMinixHelper::getConstantIntValue(Argument *argument, int &intValue)
{
	if (NULL == argument)
	{
		return false;
	}
	ConstantInt *constIntArg = dyn_cast<ConstantInt>(argument);
	if (NULL == constIntArg)
	{
		return false;
	}
	intValue = *(constIntArg->getValue().getRawData());
	return true;
}

bool FuseMinixHelper::getConstantIntValue(Value *value, int &intValue)
{
	if (NULL == value)
	{
		return false;
	}
	ConstantInt *constIntArg = dyn_cast<ConstantInt>(value);
	if (NULL == constIntArg)
	{
		return false;
	}
	intValue = *(constIntArg->getValue().getRawData());
	return true;
}

Value* FuseMinixHelper::getMinixMessageArgument(const CallSite *callSite)
{
	if (NULL == callSite)
	{
		return NULL;
	}
	for(CallSite::arg_iterator IA = callSite->arg_begin(), EA = callSite->arg_end();
			IA != EA; IA++)
	{
		if (true == isMinixMessageType((*IA)))
		{
			return dyn_cast<Value>((*IA));
		}
	}
	return NULL;
}

bool FuseMinixHelper::hasMinixMessageParameter(Function *function)
{
	if (NULL == function)
	{
		return false;
	}
	for (Function::arg_iterator IA = function->arg_begin(), EA = function->arg_end();
			IA != EA; IA++)
	{
		if (true == isMinixMessageType(&(*IA)))
		{
			return true;
		}
	}
	return false;
}

bool FuseMinixHelper::inlineFunctionAtCallSite(CallSite *callSite)
{
	if (NULL == callSite)
	{
		return false;
	}
	InlineFunctionInfo IFI;
    CallInst *callInst;
    Instruction *I = callSite->getInstruction();
    callInst = dyn_cast<CallInst>(I);
    if (false == llvm::InlineFunction(callInst, IFI))
    {
      DEBUG(errs() << "Failed inlining function call : " << callInst->getName() << "\n");
      return false;
    }
    return true;
}

bool FuseMinixHelper::getM_TYPEValues(CallSite *sendrecCallSite, int &mtypeValue)
{
	Function *sendrecCaller = sendrecCallSite->getCaller();
	Value *message = getMinixMessageArgument(sendrecCallSite);
	int numMTYPEInstrs = 0;
	if (NULL == message || (true == hasMinixMessageParameter(sendrecCaller)))
	{
		DEBUG(errs() << "Error: called in wrong state.\n");
		return false;
	}

	std::vector<Instruction*> instrsUntilCallSite;
	for(inst_iterator I = inst_begin(sendrecCaller), E= inst_end(sendrecCaller); I != E; I++)
	{
		if (&(*I) == sendrecCallSite->getInstruction())
		{
			break;
		}
		instrsUntilCallSite.push_back(&(*I));
	}
	DEBUG(errs() << "Caller: " << sendrecCaller->getName() << " has num instrs: " << instrsUntilCallSite.size() << " ");
	for(std::vector<Instruction*>::reverse_iterator RI = instrsUntilCallSite.rbegin(), RE = instrsUntilCallSite.rend();
				RI != RE; RI++)
	{
		StoreInst *storeInst = NULL;
		if (NULL != (storeInst = dyn_cast<StoreInst>((*RI))))
		{
			Value *valueOperand = storeInst->getValueOperand();
			Value *pointerOperand = storeInst->getPointerOperand();
			if (std::string::npos != pointerOperand->getName().find(MINIX_STRUCT_MESSAGE_M_TYPE))
			{
				DEBUG(errs() << " [" << MINIX_STRUCT_MESSAGE_M_TYPE << "] ");
				numMTYPEInstrs++;
			}
			else
			{
				continue;
			}
			DEBUG(errs() << "\tStore Instr on: " << pointerOperand->getName() << " (valueoperand: " << valueOperand->getName() << ") ");
			DEBUG(errs() << " [store inst] ");
			DEBUG(errs() << " [" << MINIX_STRUCT_MESSAGE_M_TYPE << "] ");

			ConstantInt *constInt = dyn_cast<ConstantInt>(valueOperand);
			if (NULL != constInt)
			{
				getConstantIntValue(constInt, mtypeValue);
				DEBUG(errs() << " [first][Got constant value: " << mtypeValue << "] \n");
				break;
			}
			else
			{
				std::deque<User*> usersBucket;
				// find the load instr. corresponding to the valueoperand and get their users
				getUsers(instrsUntilCallSite, storeInst, usersBucket);

				while(0  != usersBucket.size())
				{
					User *user = usersBucket.front();
					usersBucket.pop_front();
					StoreInst *storeInstUser = dyn_cast<StoreInst>(user);
					if (NULL != storeInstUser)
					{
						Value *valueOperand = storeInstUser->getValueOperand();
						DEBUG(errs() << "[value operand:" << valueOperand->getName() <<"] ");
						ConstantInt *constIntUser = dyn_cast<ConstantInt>(valueOperand);
						if (NULL != constIntUser)
						{
							getConstantIntValue(constIntUser, mtypeValue);
							DEBUG(errs() << " [Got constant value: " << mtypeValue << "] \n");
							break;
						}
						DEBUG(errs() << " [storeinst] ");
						usersBucket.clear();

						// get load instr corr. to this store instr.
						getUsers(instrsUntilCallSite, storeInstUser, usersBucket);
					}
					else
					{
						DEBUG(errs() << "--notstoreinst--");
						continue;
					}
				}
			}
			DEBUG(errs() << "\n");
		}
	}
	DEBUG(errs()  << "\t (num " << MINIX_STRUCT_MESSAGE_M_TYPE << " instrs: " << numMTYPEInstrs << ")\n");
	return true;
}

bool FuseMinixHelper::getEndpointValue(CallSite *sendrecCallSite, int &endpoint)
{
	DEBUG(errs() << "Getting endpoint value....\n");
	Value *endpointValue = dyn_cast<Value>(sendrecCallSite->getArgument(0));
	LoadInst *theLoadInst = dyn_cast<LoadInst>(endpointValue);
	if (NULL == endpointValue || NULL == theLoadInst)
	{
		DEBUG(errs() << "Error: called in wrong state.\n");
		return false;
	}

	std::deque<User*> usersBucket;
	// find the load instr. corresponding to the valueoperand and get their users
	getUsers(theLoadInst, usersBucket);
	DEBUG(errs() << "Num users of the non const endpoint arg: " << usersBucket.size() << "\n");

	while(0  != usersBucket.size())
	{
		User *user = usersBucket.front();
		usersBucket.pop_front();
		StoreInst *storeInstUser = dyn_cast<StoreInst>(user);
		if (NULL != storeInstUser)
		{
			Value *valueOperand = storeInstUser->getValueOperand();
			DEBUG(errs() << "[value operand:" << valueOperand->getName() <<"] ");
			ConstantInt *constIntUser = dyn_cast<ConstantInt>(valueOperand);
			if (NULL != constIntUser)
			{
				getConstantIntValue(constIntUser, endpoint);
				DEBUG(errs() << " [Got constant value: " << endpoint << "] \n");
				break;
			}
			DEBUG(errs() << " [storeinst] ");
			usersBucket.clear();

			// get load instr corr. to this store instr.
			getUsers(storeInstUser->getValueOperand(), storeInstUser, usersBucket);
			continue;
		}
		LoadInst *loadInstUser = dyn_cast<LoadInst>(user);
		if (NULL != loadInstUser)
		{
			getUsers(loadInstUser, usersBucket);
		}
		else
		{
			DEBUG(errs() << "--notstoreinst--");
			continue;
		}
	}
	return true;
}

bool FuseMinixHelper::getUsers(std::vector<Instruction*> &instrsUntilCallSite, StoreInst *storeInst, std::deque<User*> &users)
{
	bool returnValue = true;
	// return corresponding load instruction users
	bool ignore = true;
	for(std::vector<Instruction*>::reverse_iterator RI = instrsUntilCallSite.rbegin(), RE = instrsUntilCallSite.rend();
			RI != RE; RI++)
	{
		if (ignore)
		{
			if ((*RI) == storeInst)
			{
				ignore = false;
			}
			continue;
		}
		LoadInst *loadInst = dyn_cast<LoadInst>(*RI);
		if (NULL != loadInst)
		{
			Value *ptrValue = loadInst->getPointerOperand();
			DEBUG(errs() << "[Load inst: " << ptrValue->getName() << "]");
			if (storeInst->getValueOperand() == dyn_cast<Value>(loadInst))
			{
				DEBUG(errs() << "(int) ");
				returnValue = getUsers(loadInst, users);
				break;
			}
			DEBUG(errs() << " ");
		}
	}
	return returnValue;
}

bool FuseMinixHelper::getUsers(LoadInst *loadInst, std::deque<User*> &users)
{
	Value *pointerOperand = loadInst->getPointerOperand();
	return getUsers(pointerOperand, loadInst, users);
}

bool FuseMinixHelper::getUsers(Value *value, Instruction *untilInstr, std::deque<User*> &users)
{
	if (NULL == value)
	{
		DEBUG(errs() << "[value is null]");
		return false;
	}
	DEBUG(errs() << "[value:" << value->getName() << "]");
	// consider that the users is in reverse order.
#if LLVM_VERSION >= 37
	std::vector<User*> valueUsers(value->user_begin(), value->user_end());
#else
	std::vector<User*> valueUsers(value->use_begin(), value->use_end());
#endif
	for(unsigned i=valueUsers.size(); i != 0; i--)
	{
		if (NULL != dyn_cast<Instruction>(valueUsers[i-1]))
			DEBUG(errs() << "{VU:" << dyn_cast<Instruction>(valueUsers[i-1])->getOpcodeName() << "}");
	}
	bool ignoreUser =  true;
#if LLVM_VERSION >= 37
	for(Value::user_iterator ISU = value->user_begin(), ESU = value->user_end(); ISU != ESU; ISU++)
#else
	for(Value::use_iterator ISU = value->use_begin(), ESU = value->use_end(); ISU != ESU; ISU++)
#endif
	{
		DEBUG(errs() << "[Getting in: " << (*ISU)->getName() << "]");
		if (NULL == dyn_cast<Instruction>(*ISU))
		{
			DEBUG(errs() << " [not instr.] ");
			continue;
		}
		if ((NULL != untilInstr) && (*ISU == untilInstr))
		{
			ignoreUser = false;
			continue;
		}
		if (ignoreUser)
		{
			continue;
		}
		users.push_back(*ISU);
		DEBUG(errs() << "[Pushed]");
	}

	DEBUG(errs() << "{" << users.size() << "-users}");
	return true;
}

bool FuseMinixHelper::noopIt(CallSite *callSite, std::map<FunctionType*, Function*> &noopFunctionsMap)
{
    Function* noopFunction = NULL;
    FunctionType *FTy = NULL;
    Function *F = NULL;
    Function *caller = callSite->getCaller();
    std::vector<Type*> argTypes;
    F = callSite->getCalledFunction();
    argTypes.clear();
    for (Function::const_arg_iterator I = F->arg_begin(), E = F->arg_end(); I != E; ++I)
    {
        argTypes.push_back(I->getType());
    }
    FTy = FunctionType::get(F->getFunctionType()->getReturnType(),
                            argTypes,
                            F->getFunctionType()->isVarArg());

    if (0 == noopFunctionsMap.count(FTy))
    {
      DEBUG(errs() << "Creating noopFunction for Fty: " << FTy << "\n");
      std::string noopFuncName = (std::string)"__noop_" + (std::string)F->getName();
      DEBUG(errs() << "noop name: " << noopFuncName << "\n");
      noopFunction = PassUtil::createFunctionWeakPtrWrapper(*(this->module), noopFuncName, FTy);
      if(NULL == noopFunction)
      {
        return false;
      }
      noopFunctionsMap.insert(std::pair<FunctionType*, Function*>(FTy, noopFunction));
    }
    else
    {
      DEBUG(errs() << "Found noopFunction for FTy: " << FTy << "\n");
      noopFunction = noopFunctionsMap.find(FTy)->second;
    }
    DEBUG(errs()<< "noop function name: " << noopFunction->getName() << "\n");
    if (NULL == noopFunction)
    {
      DEBUG(errs() << "WARNING: noopFunction is NULL.\n");
      return false;
    }
    if (false == replaceCallInst(callSite, noopFunction))
    {
      DEBUG(errs() << "WARNING: Failed replacing callsite with noop.\n");
      return false;
    }
    DEBUG(errs() << "replaced callinst at callsite: " << caller->getName() << "\n");
    return true;
}

}
#endif
