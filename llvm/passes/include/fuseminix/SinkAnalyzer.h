/******************************
* Author : Koustubha Bhat
* Jul-Oct 2014
* Vrije Universiteit, Amsterdam.
*******************************/
#ifndef FUSE_MINIX_SINK_ANALYZER_H
#define FUSE_MINIX_SINK_ANALYZER_H

#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <pass.h>
#include <common/util/string.h>
#include <common/pass_common.h>
#include <fuseminix/common.h>
#include <fuseminix/FuseMinixHelper.h>

namespace llvm 
{

class SinkAnalyzer
{
public:
	SinkAnalyzer();
	bool getMTypeDependentSwitches(Function *sefReceiveCallerClone, std::vector<SwitchInst*> &swInsts);
	bool getMTypeDependentBranches(Function *sefReceiveCallerClone, std::vector<BranchInst*> &brInsts);
	BasicBlock* getBasicBlocksToPrune(SwitchInst* swInst, int mTypeValue, std::vector<BasicBlock*> &BBsToPrune);
	BasicBlock* getBasicBlocksToPrune(BranchInst* brInst, int mTypeValue, std::vector<BasicBlock*> &BBsToPrune);
	BasicBlock* getBasicBlocksToPrune(BranchInst* brInst, bool trueBlock, std::vector<BasicBlock*> &BBsToPrune);

private:
	std::vector<Regex*>  sefreceiveRegexVec;
	FuseMinixHelper 	 *fmHelper;
	bool getMTypeVariables(Function *F, std::vector<Value*> &mtypeVariables);
	bool getSwitchInstUsers(LoadInst *loadInst, Function *F, std::vector<SwitchInst*> &switchInstructions);
	bool getBranchInstUsers(LoadInst *loadInst, Function *F, std::vector<BranchInst*> &branchInstructions);
	bool findLoadInst(inst_iterator &iStart, inst_iterator iEnd, std::string varName);
	bool findLoadInst(inst_iterator &iStart, inst_iterator iEnd);
	bool findStoreInst(LoadInst* loadInst, inst_iterator &iStart, inst_iterator iEnd, bool &stop);
};

SinkAnalyzer::SinkAnalyzer()
{
	std::string sefreceiveString = (std::string)SEFRECEIVE_FUNC_NAME;
    PassUtil::parseRegexListOpt(sefreceiveRegexVec, sefreceiveString);
    this->fmHelper = new FuseMinixHelper(NULL, NULL);
}

bool SinkAnalyzer::getMTypeDependentSwitches(Function *sefReceiveCallerClone, std::vector<SwitchInst*> &swInsts)
{
	std::vector<Value*> mtypeVariables;
	if (true == getMTypeVariables(sefReceiveCallerClone, mtypeVariables))
	{
		DEBUG(errs() << "mtype vars: \n");
		for (unsigned i=0; i < mtypeVariables.size(); i++)
		{
			bool isGlobalVariable = false;
			if (isa<GlobalValue>(mtypeVariables[i]))
			{
				isGlobalVariable = true;
			}
			DEBUG(errs() << mtypeVariables[i]->getName() << " total users: " << mtypeVariables[i]->getNumUses() << "\n");
#if LLVM_VERSION >= 37
			for(Value::user_iterator IU = mtypeVariables[i]->user_begin(), EU = mtypeVariables[i]->user_end(); IU != EU; IU++)
#else
			for(Value::use_iterator IU = mtypeVariables[i]->use_begin(), EU = mtypeVariables[i]->use_end(); IU != EU; IU++)
#endif
			{
				if (isGlobalVariable && (dyn_cast<Instruction>(*IU)->getParent()->getParent() != sefReceiveCallerClone))
				{
					continue;
				}
				LoadInst *loadInst = dyn_cast<LoadInst>(*IU);
				if (NULL != loadInst)
				{
					getSwitchInstUsers(loadInst, sefReceiveCallerClone, swInsts);
				}
			}
		}
		DEBUG(errs() << "\n");
	}
	else
	{
		DEBUG(errs() << "Failed in getMTypeVariables()");
		return false;
	}
	
	DEBUG(errs() << "Num switch statements found: " << swInsts.size() << "\n");
	return true;
}

bool SinkAnalyzer::getMTypeDependentBranches(Function *sefReceiveCallerClone, std::vector<BranchInst*> &brInsts)
{
	std::vector<Value*> mtypeVariables;
	if (true == getMTypeVariables(sefReceiveCallerClone, mtypeVariables))
	{
		DEBUG(errs() << "mtype vars: \n");
		for (unsigned i=0; i < mtypeVariables.size(); i++)
		{
			bool isGlobalVariable = false;
			if (isa<GlobalValue>(mtypeVariables[i]))
			{
				isGlobalVariable = true;
			}
			DEBUG(errs() << "[mtypevar: " << mtypeVariables[i]->getName() << " total users: " << mtypeVariables[i]->getNumUses() << "]\n");
#if LLVM_VERSION >= 37
			for(Value::user_iterator IU = mtypeVariables[i]->user_begin(), EU = mtypeVariables[i]->user_end(); IU != EU; IU++)
#else
			for(Value::use_iterator IU = mtypeVariables[i]->use_begin(), EU = mtypeVariables[i]->use_end(); IU != EU; IU++)
#endif
			{
				if (isGlobalVariable && (dyn_cast<Instruction>(*IU)->getParent()->getParent() != sefReceiveCallerClone))
				{
					continue;
				}
				LoadInst *loadInst = dyn_cast<LoadInst>(*IU);
				if (NULL != loadInst)
				{
					getBranchInstUsers(loadInst, sefReceiveCallerClone, brInsts);
				}
			}
		}
		DEBUG(errs() << "\n");
	}
	else
	{
		DEBUG(errs() << "Failed in getMTypeVariables()");
		return false;
	}
	
	DEBUG(errs() << "Num branch statements found: " << brInsts.size() << "\n");
	return true;
}

BasicBlock* SinkAnalyzer::getBasicBlocksToPrune(SwitchInst* swInst, int mTypeValue, std::vector<BasicBlock*> &BBsToPrune)
{
	BasicBlock *blockToRetain = NULL;
	if (NULL == swInst)
	{
		return NULL;
	}
	bool foundBlockToRetain = false;
	DEBUG(errs() << "m_type value: " << mTypeValue << " => ");
	for (SwitchInst::CaseIt IC = swInst->case_begin(), EC = swInst->case_end(); IC != EC; IC++)
	{
		Value *caseValue = IC.getCaseValue();
		ConstantInt *caseNum = dyn_cast<ConstantInt>(caseValue);
		if (NULL != caseNum)
		{
			int iCaseNum = (*caseNum->getValue().getRawData());
			BasicBlock *CB = IC.getCaseSuccessor();
			if (NULL == CB)
			{
				continue;
			}
			if (mTypeValue == iCaseNum)
			{
				DEBUG(errs() << "[Retaining case " << iCaseNum << " BB:" << CB->getName() << "\n");
				foundBlockToRetain = true;
				blockToRetain = CB;
				continue;					
			}
			DEBUG(errs() << "[to prune: " << CB->getName() << "] ");
			BBsToPrune.push_back(CB);
		}
	}
	SwitchInst::CaseIt defaultCase = swInst->case_default();
	DEBUG(errs() << "[to prune default case: " << defaultCase.getCaseSuccessor()->getName() << "]\n");
	if (true == foundBlockToRetain)
	{	
		BBsToPrune.push_back(defaultCase.getCaseSuccessor());
	}
	else
	{
		blockToRetain = defaultCase.getCaseSuccessor();
	}
	DEBUG(errs() << "\n");
	return blockToRetain;
}

BasicBlock* SinkAnalyzer::getBasicBlocksToPrune(BranchInst* brInst, int mTypeValue, std::vector<BasicBlock*> &BBsToPrune)
{
	BasicBlock *blockToRetain = NULL;
	if (NULL == brInst)
	{
		return NULL;
	}
	if (false == brInst->isConditional())
	{
		return NULL;
	}
	Value *cmp = brInst->getCondition();
	ICmpInst *cmpInst = dyn_cast<ICmpInst>(cmp);
	int cmpConstInt = -1;
	if (NULL != cmpInst)
	{
		for(unsigned i = 0; i < cmpInst->getNumOperands(); i++)
		{
			Value *operand = cmpInst->getOperand(i);
			ConstantInt *constInt = dyn_cast<ConstantInt>(operand);
			if (NULL != constInt)
			{
				cmpConstInt = *(constInt->getValue().getRawData());
				break;
			}
		}
	}
	if (-1 == cmpConstInt)
	{
		return NULL;
	}
	bool outcome = false;
	DEBUG(errs() << "mtype value: " << mTypeValue << "\n");
	DEBUG(errs() << "Reference const int for comparison: " << cmpConstInt << "\n");
	switch(cmpInst->getSignedPredicate())
	{
		case CmpInst::ICMP_EQ:
			DEBUG(errs() << "ICMP_EQ\n");
			if (cmpConstInt == mTypeValue)	outcome = true;
			break;
		
		case CmpInst::ICMP_NE:
			DEBUG(errs() << "ICMP_NE\n");
			if (cmpConstInt != mTypeValue)	outcome = true;
			break;
		
		case CmpInst::ICMP_SGT:
		// case CmpInst::ICMP_UGT:
			DEBUG(errs() << "ICMP_GT\n");
			if (mTypeValue > cmpConstInt)	outcome = true;
			break;
		
		case CmpInst::ICMP_SGE:
		case CmpInst::ICMP_UGE:
			DEBUG(errs() << "ICMP_GE\n");
			if (mTypeValue >= cmpConstInt )	outcome = true;
			break;

		case CmpInst::ICMP_SLT:
		// case CmpInst::ICMP_ULT:
			DEBUG(errs() << "ICMP_LT\n");
			if (mTypeValue < cmpConstInt)	outcome = true;
			break;

		case CmpInst::ICMP_SLE:
		case CmpInst::ICMP_ULE:
			DEBUG(errs() << "ICMP_LE\n");
			if (mTypeValue <= cmpConstInt)	outcome = true;
			break;
		default:
			return NULL;
			break;
	}
	if (outcome)
	{
		DEBUG(errs() << "outcome is true.\n");
		blockToRetain = brInst->getSuccessor(0);
		BBsToPrune.push_back(brInst->getSuccessor(1));
	}
	else
	{
		DEBUG(errs() << "outcome is false.\n");
		blockToRetain = brInst->getSuccessor(1);
		BBsToPrune.push_back(brInst->getSuccessor(0));
	}
	
	DEBUG(errs() << "\n");
	return blockToRetain;
}

BasicBlock* SinkAnalyzer::getBasicBlocksToPrune(BranchInst* brInst, bool trueBlock, std::vector<BasicBlock*> &BBsToPrune)
{
	BasicBlock *blockToRetain = NULL;
	if (NULL == brInst)
	{
		return NULL;
	}
	if (false == brInst->isConditional())
	{
		return NULL;
	}
	if (trueBlock)
	{
		DEBUG(errs() << "trueBlock\n");
		blockToRetain = brInst->getSuccessor(0);
		BBsToPrune.push_back(brInst->getSuccessor(1));
	}
	else
	{
		DEBUG(errs() << "falseBlock\n");
		blockToRetain = brInst->getSuccessor(1);
		BBsToPrune.push_back(brInst->getSuccessor(0));
	}
	return blockToRetain;
}


bool SinkAnalyzer::getSwitchInstUsers(LoadInst *loadInst, Function *F, std::vector<SwitchInst*> &switchInstructions)
{
	if (NULL == loadInst)
	{
		return false;
	}

	Value *loadInstValue = dyn_cast<Value>(loadInst);
	DEBUG(errs() << "sw. users of : " << loadInst->getPointerOperand()->getName() << ": \n");
	int count=0;
#if LLVM_VERSION >= 37
	for (Value::user_iterator IU = loadInstValue->user_begin(), EU = loadInstValue->user_end();
#else
	for (Value::use_iterator IU = loadInstValue->use_begin(), EU = loadInstValue->use_end();
#endif
			IU != EU; IU++)
	{
		SwitchInst *swInstr = dyn_cast<SwitchInst>(*IU);
		if (NULL != swInstr)
		{
			DEBUG(errs() << " [ sw:" << swInstr->getName() << "(" << swInstr->getNumCases() << ")] ");
			switchInstructions.push_back(swInstr);
			count++;
		}
	}
	DEBUG(errs() << "(total: " << count << ")\n");
	return true;
}

bool SinkAnalyzer::getBranchInstUsers(LoadInst *loadInst, Function *F, std::vector<BranchInst*> &branchInstructions)
{
	if (NULL == loadInst)
	{
		return false;
	}
	DEBUG(errs() << loadInst->getPointerOperand()->getName() << "\n");
	Value *loadInstValue = dyn_cast<Value>(loadInst);
	int count=0;
	std::set<ICmpInst*> icmpInsts;
#if LLVM_VERSION >= 37
	std::vector<User*> users(loadInstValue->user_begin(), loadInstValue->user_end());
#else
	std::vector<User*> users(loadInstValue->use_begin(), loadInstValue->use_end());
#endif
	while (users.size() != 0)
	{
		User *currUser = users.back();
		users.pop_back();
		ICmpInst *cmpInstr = dyn_cast<ICmpInst>(currUser);
		if (NULL != cmpInstr)
		{
			if(std::find(icmpInsts.begin(), icmpInsts.end(), cmpInstr) == icmpInsts.end())
			{
				icmpInsts.insert(cmpInstr);
				count++;
			}
		}
		else
		{
#if LLVM_VERSION >= 37
			for(Value::user_iterator IU = currUser->user_begin(), EU = currUser->user_end(); IU != EU; IU++)
#else
			for(Value::use_iterator IU = currUser->use_begin(), EU = currUser->use_end(); IU != EU; IU++)
#endif
			{
				users.push_back(*IU);
			}
		}
	}

	DEBUG(errs() << "(total icmp: " << count << ")\n");
	count = 0;
	for(std::set<ICmpInst*>::iterator IS = icmpInsts.begin(), ES = icmpInsts.end(); IS != ES; IS++)
	{
#if LLVM_VERSION >= 37
		for(Value::user_iterator IU = (*IS)->user_begin(), EU = (*IS)->user_end(); IU != EU; IU++)
#else
		for(Value::use_iterator IU = (*IS)->use_begin(), EU = (*IS)->use_end(); IU != EU; IU++)
#endif
		{
			BranchInst *brInst = dyn_cast<BranchInst>(*IU);
			if (NULL != brInst)
			{
				branchInstructions.push_back(brInst);
				count++;
			}
		}
	}
	return true;
}

bool SinkAnalyzer::getMTypeVariables(Function *F, std::vector<Value*> &mtypeVariables)
{
	if (NULL == F)
	{
		return false;
	}

	inst_iterator I1 = inst_begin(F), I2 = inst_begin(F), E = inst_end(F);

	// get past the sef_receive_status() call.
	while(I1 != E)
	{
		CallInst *callInst = dyn_cast<CallInst>(&(*I1));
		if (NULL != callInst)
		{
			if (PassUtil::matchRegexes(callInst->getCalledFunction()->getName(), sefreceiveRegexVec))
			{
				break;
			}
		}
		I1++;
		I2++;
	}
	if (I1 == E)
	{
		return false;
	}

	std::string varName = (std::string)MINIX_STRUCT_MESSAGE_M_TYPE;
	bool findingLoadInst = true;
	LoadInst *currLoadInst = NULL;

	while (I1 != E)
	{
		if(findingLoadInst)
		{
			//finding load inst.
			if (true == findLoadInst(I1, E, varName))
			{
				mtypeVariables.push_back(dyn_cast<LoadInst>(&(*I1))->getPointerOperand());
				findingLoadInst = (!findingLoadInst);
				currLoadInst = dyn_cast<LoadInst>(&(*I1));
				I1++;
			}
		}
		else
		{
			bool stop = false;
			// finding store inst.)
			if (true == findStoreInst(currLoadInst, I1, E, stop))
			{
				varName = dyn_cast<StoreInst>(&(*I1))->getPointerOperand()->getName();
				findingLoadInst = (!findingLoadInst);
				I1++;
			}
			if (stop)
			{
				// Indication that we reached a point where another load happened
				// before we find the next store
				break;
			}
		}
		// I1++;
	}
	while (I2 != E)
	{
		if(findingLoadInst)
		{
			//finding load inst. that use GEPOperator!
			if (true == findLoadInst(I2, E))
			{
				findingLoadInst = (!findingLoadInst);
				currLoadInst = dyn_cast<LoadInst>(&(*I2));
				// mtypeVariables.push_back(dyn_cast<LoadInst>(currLoadInst));
				I2++;
			}
		}
		else
		{
			bool stop = false;
			// finding store inst.)
			if (true == findStoreInst(currLoadInst, I2, E, stop))
			{
				varName = dyn_cast<StoreInst>(&(*I2))->getPointerOperand()->getName();
				findingLoadInst = (!findingLoadInst);
				mtypeVariables.push_back(dyn_cast<StoreInst>(&(*I2))->getPointerOperand());
				I2++;
			}
			if (stop)
			{
				// Indication that we reached a point where another load happened
				// before we find the next store
				break;
			}
		}
		// I2++;
	}
	return true; // indicating that our processing finished completely.
}

bool SinkAnalyzer::findLoadInst(inst_iterator &iStart, inst_iterator iEnd, std::string varName)
{
	if (varName == "")
	{
		return false;
	}
	while (iStart != iEnd)
	{
		LoadInst *LI = dyn_cast<LoadInst>(&(*iStart));
		if (NULL != LI)
		{
			if (LI->getPointerOperand()->getType()->getPointerElementType()->isIntegerTy())
			{
				if (std::string::npos != LI->getPointerOperand()->getName().find(varName))
				{
					return true;
				}
			}
		}
		iStart++;
	}
	return false;
}

bool SinkAnalyzer::findLoadInst(inst_iterator &iStart, inst_iterator iEnd)
{
	// Find minix message->m_type
	while (iStart != iEnd)
	{
		LoadInst *LI = dyn_cast<LoadInst>(&(*iStart));
		if (NULL != LI)
		{
			GEPOperator *gep = dyn_cast<GEPOperator>(LI->getPointerOperand());
			if (NULL != gep)
			{
				DEBUG(errs() << "[GEP]");
				Type *gepPtrOpType = gep->getPointerOperand()->getType()->getPointerElementType();
				if (gepPtrOpType->isStructTy() && gepPtrOpType->getStructName().find(MINIX_STRUCT_MESSAGE))
				{
					DEBUG(errs() << "[message]");
					std::vector<Value*> indices(gep->idx_begin(), gep->idx_end());
					DEBUG(errs() << "[num_indices: " << indices.size() << "]");
					if (2 == indices.size())
					{
						ConstantInt *constInt = dyn_cast<ConstantInt>(indices[1]);
						if (NULL == constInt)
						{
							continue;
						}
						DEBUG(errs() << "[constint] Index value: " << (*(constInt->getValue().getRawData())));
						if ((*(constInt->getValue().getRawData())) == 1)
						{
							DEBUG(errs() << "\n");
							return true;
						}
					}
				}
				DEBUG(errs() << "\n");
			}
		}
		iStart++;
	}
	return false;
}

bool SinkAnalyzer::findStoreInst(LoadInst* loadInst, inst_iterator &iStart, inst_iterator iEnd, bool &stop)
{
	if (NULL == loadInst)
	{
		return false;
	}
	while (iStart != iEnd)
	{
		StoreInst *SI = dyn_cast<StoreInst>(&(*iStart));
		if (NULL != SI)
		{
			if (SI->getValueOperand() == dyn_cast<Value>(loadInst))
			{
				return true;
			}
		}	
		iStart++;
	}
	return false;
}

}
#endif
