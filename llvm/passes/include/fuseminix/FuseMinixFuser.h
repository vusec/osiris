/******************************
* Author : Koustubha Bhat
* Jul-Oct 2014
* Vrije Universiteit, Amsterdam.
*******************************/
#ifndef FUSE_MINIX_FUSER_H
#define FUSE_MINIX_FUSER_H

#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <pass.h>
#include <common/util/string.h>
#include <common/pass_common.h>
#include <common/dsa_common.h>
#include <fuseminix/common.h>
#include <fuseminix/FuseMinixPass.h>
#include <fuseminix/FuseMinixPreparer.h>

namespace llvm {

STATISTIC(NumSendrecFuserWrappers, "SEND SIDE: Number of wrapper functions created to fuse ipc_sendrec() calls to potential destinations.");

class FuseMinixFuser
{
public:
	FuseMinixFuser(FuseMinixPass *FMP);
	bool fuse(FuseMinixPreparer *preparer);

private:
	Module *M;
	FuseMinixPass *FMP;
	DSAUtil *dsau;
	FuseMinixPreparer *preparer;
	std::vector<CallSite>   sendrecCallSites;

	Function* createSendrecCallFusingFunction(CallSite *callSite);
	Function* createFusingFunction(IPCInfo *ipcInfo, Function *theSendrecCaller, std::vector<Function*> functionCallees);
	bool replaceCallInst(CallSite *callSite, Function *functionToCall);
};

FuseMinixFuser::FuseMinixFuser(FuseMinixPass *FMP)
{
  this->FMP = FMP;
  dsau = NULL;
}

bool FuseMinixFuser::fuse(FuseMinixPreparer *preparer)
{
	bool returnValue = true;
	std::map<CallSite*, IPCInfo*>* sendrecCallSitesInfoMap;

	this->preparer = preparer;
	sendrecCallSitesInfoMap = preparer->getSendrecCallSitesInfoMap();
	if (NULL == sendrecCallSitesInfoMap)
	{
		return false;
	}
	std::vector<Function*> destFunctions;
	for (std::map<CallSite*, IPCInfo*>::iterator IM = sendrecCallSitesInfoMap->begin(), EM = sendrecCallSitesInfoMap->end();
				IM != EM; IM++)
	{
		DEBUG(errs() << "Caller of sendrec callsite: " << (*IM).first->getCaller()->getName()
					 << " mtype: " << (*IM).second->mtype << " "
					 << " src endpointValue: " << (*IM).second->srcEndpoint
					 << " num dest endpointValues: " << (*IM).second->destEndpoints.size() << " ");
		DEBUG(errs() << "[");
		for(unsigned i = 0; i < (*IM).second->destEndpoints.size(); i++)
		{
			DEBUG(errs() << (*IM).second->destEndpoints[i] << " ");
		}
		DEBUG(errs() << "]\n");
	}

	for (std::map<CallSite*, IPCInfo*>::iterator IM = sendrecCallSitesInfoMap->begin(), EM = sendrecCallSitesInfoMap->end();
				IM != EM; IM++)
	{
		CallSite *currCallSite = (*IM).first;
		IPCInfo  *currInfo     = (*IM).second;
		std::vector<Function*> callSiteDestFunctions;
		Function *fuserFunction= NULL;

		callSiteDestFunctions.clear();
		DEBUG(errs() << "Getting potential sendrec destinations.\n");
		if (0  > preparer->getPotentialSendrecDestinations(currInfo, callSiteDestFunctions))
		{
			DEBUG(errs() << "@ Fuser getPotentialSendrecDestinations returned -1.\n");
			returnValue = false;
			continue;
		}
		errs() << "[ DESTFUNCTS ] \n";
		for (auto FI = callSiteDestFunctions.begin();
		     FI != callSiteDestFunctions.end();
		     FI++) {
			errs() << "   DESTFUNCT: " << (*FI)->getName() << "\n";
		}

		DEBUG(errs() << "Num potential destinations: " << callSiteDestFunctions.size() << "\n");
		DEBUG(errs() << "CREATING FUSE FUNCTION\n");
		DEBUG(errs() << "Target sendrec function at callsite: " << currCallSite->getCalledFunction()->getName() << "\n");
		this->M = currCallSite->getCalledFunction()->getParent();
		fuserFunction = createFusingFunction(currInfo, currCallSite->getCalledFunction(), callSiteDestFunctions);
		if (NULL == fuserFunction)
		{
			DEBUG(errs() << "WARNING: Failed creating fuser function for callsite @ " <<  currCallSite->getCaller()->getName() << "\n");
			returnValue =  false;
			continue;
		}
		NumSendrecFuserWrappers++;
		if (false == replaceCallInst(currCallSite, fuserFunction))
		{
			DEBUG(errs() << "WARNING: Failed replacing callsite call inst. @ " << currCallSite->getCaller()->getName() << "\n");
			returnValue = false;
		}
	}

	return returnValue;
}

Function* FuseMinixFuser::createFusingFunction(IPCInfo *ipcInfo, Function *theSendrecCaller, std::vector<Function*> functionCallees)
{
	DEBUG(errs() << "Creating fusing function for ipc_sendrec call: " << theSendrecCaller->getName() << "\n");
	// Create a wrapper function
	std::string modulePrefix = getMinixModulePrefix(theSendrecCaller);
	DEBUG(errs() << "module prefix: " << modulePrefix << "\n");
	std::string destModuleName = "";
	if (NULL != ipcInfo)
	{
		if ( 1 == ipcInfo->destEndpoints.size())
		{
			std::string tmpStr;
			getEndpointMapping(ipcInfo->destEndpoints[0], tmpStr);
			destModuleName = (std::string)"_" + tmpStr + (std::string)"_" + OutputUtil::intToStr(ipcInfo->mtype) + (std::string)"_";
		}
		else if (1 < ipcInfo->destEndpoints.size())
		{
			destModuleName = "_multipledest_";
		}
		else
		{
			destModuleName = "_X_"; // dont know why we are here!
		}
	}
	std::string wrapperName = (std::string)theSendrecCaller->getName() + (std::string)FUSER_FUNCTION_PREFIX + destModuleName;
	Function *wrapperFunction = NULL;
	FunctionType *FTy = NULL;
	std::vector<Type*> argTypes;
    for (Function::const_arg_iterator I = theSendrecCaller->arg_begin(), E = theSendrecCaller->arg_end(); I != E; ++I)
    {
      	argTypes.push_back(I->getType());
    }
	FTy = FunctionType::get(theSendrecCaller->getFunctionType()->getReturnType(),
							argTypes,
							theSendrecCaller->getFunctionType()->isVarArg());
	DEBUG(errs() << "Wrapper name: " << wrapperName << "\n");
	wrapperFunction = PassUtil::createFunctionWeakPtrWrapper(*(this->M), wrapperName, FTy);
	if(NULL == wrapperFunction)
	{
		return NULL;
	}
	DEBUG(errs() << "Created wrapper function.\n");

	// Create call instructions to the destination functions
	BasicBlock *entryBB = NULL;
	entryBB = &wrapperFunction->getEntryBlock();
	std::vector<CallInst*> replacementCallInsts;
	Instruction *beforeInst = NULL;
	for (BasicBlock::iterator I = entryBB->begin(), E = entryBB->end(); I != E; I++)
	{
		if (dyn_cast<TerminatorInst>(I))
		{
			beforeInst = dyn_cast<Instruction>(I);
			break;
		}
	}

	std::vector<Value*> args;
	Value *argcArgument = ConstantInt::get(Type::getInt32Ty(M->getContext()), 0, false);
	Value *argvArgument = ConstantPointerNull::get(Type::getInt8Ty(M->getContext())->getPointerTo()->getPointerTo());
	Value *voidPointerNullArgument = ConstantPointerNull::get(VOID_PTR_TY(*M));
	bool success = false;

	for (unsigned i = 0; i < functionCallees.size(); i++)
	{
		Function *currCallee = functionCallees[i];

		// Prepare arguments
		args.clear();
		DEBUG(errs() << "Arg size of callee : " << currCallee->getArgumentList().size() << "\n");
		switch(currCallee->getArgumentList().size())
		{
			case 0:
			{
				// nothing to do
				break;
			}
			case 1:
			{
		    	Argument *parameter = dyn_cast<Argument>(currCallee->getArgumentList().begin());
		    	if (NULL == parameter)
		    	{
		    		continue;
		    	}
		    	if (parameter->getType()->isIntegerTy())
		    	{
		    		args.push_back(argcArgument);
		    	}
		    	else if(parameter->getType()->isPointerTy())
		    	{
		    		args.push_back(voidPointerNullArgument);
		    	}
		    	else
		    	{
		    		DEBUG(errs() << "TypeMismatch_ONE_ARG: Arg type: " << parameter->getType()->getTypeID() << "(" << currCallee->getName() << ")\n");
		    		continue;
		    	}
		    	break;
		    }
			case 2:
			{
			    args.push_back(argcArgument);
			    args.push_back(argvArgument);
			    unsigned j = 0;
			    bool nope =  false;
				for (Function::const_arg_iterator I = currCallee->arg_begin(), E = currCallee->arg_end(); I != E; ++I)
			    {
			    	if (I->getType() != args[j]->getType())
			    	{
			    		DEBUG(errs() << "TypeMismatch_TWO_ARGS: I: " << I->getType()->getTypeID() << " args[" << j << "] : "
			    					 << args[j]->getType()->getTypeID() << "(Funcn. call:" << currCallee->getName() << ")\n");
			    		nope = true;
			    	}
			    	j++;
			    }
			    if (nope)
			    {
			    	continue;
			    }
			    break;
			}
			default:
			{
				DEBUG(errs() << "TypeMismatch_MORE_ARGS : " << currCallee->getArgumentList().size() << "\n");
				continue;
			}
		}

		std::string callInstName = (std::string)REPLACEMENT_CALL_PREFIX + (std::string)currCallee->getName();
		CallInst *callInst = PassUtil::createCallInstruction(currCallee, args, "", beforeInst);
		if (NULL != callInst)
		{
			replacementCallInsts.push_back(callInst);
			success = true;
		}
		else
		{
			DEBUG(errs() << "WARNING: CallInst couldn't be created for callee: " << currCallee->getName()
						 << "(caller: " << theSendrecCaller->getName() << ")\n");
		}
	}
	if (success)
	{
		return wrapperFunction;
	}
	else
	{
		return NULL;
	}
}




static void dump_mapping(CallInst *CI, Function *F)
{

	errs() << "IPC_CALL_TO_FUESERMAP: " << F->getName() << " ";

	MDNode *N = CI->getMetadata(FUSE_IPC_SITE_ID_KEY);

	if (N) {
		ConstantInt *I = dyn_cast_or_null<ConstantInt>(N->getOperand(0));
		MDString    *S = dyn_cast_or_null<MDString>(N->getOperand(1));


		errs() << " " << S->getString() << ":" << I->getZExtValue() << "\n";
	}
	else errs() << " NO_MAPPING\n";
}


bool FuseMinixFuser::replaceCallInst(CallSite *callSite, Function* functionToCall)
{
	if (NULL == callSite || NULL == functionToCall)
	{
		return false;
	}

	std::vector<Value*> *args = new std::vector<Value*>(callSite->arg_begin(), callSite->arg_end());
	CallInst *newInst = NULL;
	Instruction *insertPoint = callSite->getInstruction();
	CallInst *originalInst = dyn_cast<CallInst>(callSite->getInstruction());

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

    originalInst->replaceAllUsesWith(newInst);

    // If the old instruction was an invoke, add an unconditional branch
    // before the invoke, which will become the new terminator.
    if (InvokeInst *II = dyn_cast<InvokeInst>(originalInst))
      BranchInst::Create(II->getNormalDest(), originalInst);

    dump_mapping(originalInst, functionToCall);

    // Delete the old call site
    originalInst->eraseFromParent();

    return true;
}

}
#endif
