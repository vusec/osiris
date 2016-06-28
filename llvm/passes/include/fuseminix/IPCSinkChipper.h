/******************************
* Author : Koustubha Bhat
* Jul-Oct 2014
* Vrije Universiteit, Amsterdam.
*******************************/
#ifndef FUSE_MINIX_IPC_SINK_CHIPPERS_H
#define FUSE_MINIX_IPC_SINK_CHIPPERS_H

#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <pass.h>
#include <common/util/string.h>
#include <common/pass_common.h>
#include <fuseminix/common.h>
#include <fuseminix/SinkAnalyzer.h>

namespace llvm
{

class IPCSinkChipper
{
public:
	IPCSinkChipper();
	virtual bool chip(Function *targetFunction, int mType, int srcEndpoint) = 0;

protected:
	SinkAnalyzer *sinkAnalyzer;
	const unsigned NOTIFY_MESSAGE;
	const int VFS_PROC_NR;
	const int VFS_PM_RS_BASE;
	const int RS_RQ_BASE;
	const int RS_INIT;
	const int RS_PROC_NR;
	const int VFS_TRANSACTION_BASE;
	const int VM_RQ_BASE;
	const int VM_PAGEFAULT;
	const int NR_VM_CALLS;
	bool adjustBlockTerminator(BasicBlock* currBlock, BasicBlock* successor);
	int macroIsNotify(int mtypeValue);
	bool isVfsFsTransId(int mType);
	int trnsGetId(int mType);
};

IPCSinkChipper::IPCSinkChipper() : NOTIFY_MESSAGE(0x1000), 
								   VFS_PROC_NR(1), 
								   VFS_PM_RS_BASE(0x980),
								   RS_RQ_BASE(0x700),
								   RS_INIT(0x700 + 20),
								   RS_PROC_NR(2),
								   VFS_TRANSACTION_BASE(0xB00),
								   VM_RQ_BASE(0xC00),
								   VM_PAGEFAULT(0xC00 + 0xff),
								   NR_VM_CALLS(48)
{
	this->sinkAnalyzer = new SinkAnalyzer();
}

bool IPCSinkChipper::adjustBlockTerminator(BasicBlock* currBlock, BasicBlock* successor)
{
	if (NULL == currBlock || NULL == successor)
	{
		return false;
	}

	BranchInst::Create(successor, currBlock);
	return true;
}

int IPCSinkChipper::macroIsNotify(int mtypeValue)
{
	return mtypeValue - NOTIFY_MESSAGE;
}

bool IPCSinkChipper::isVfsFsTransId(int mType)
{
	return (((mType) & ~0xff) == VFS_TRANSACTION_BASE);
}

int IPCSinkChipper::trnsGetId(int mType)
{
	return 	((mType) & 0xFFFF);
}

class SchedMainChipper : public IPCSinkChipper
{
public:
	bool chip(Function *targetFunction, int mType, int srcEndpoint)
	{
		if (NULL == sinkAnalyzer || NULL == targetFunction)
		{
			return false;
		}
		const int expectedNumSwitches = 1;
		std::vector<SwitchInst*> swInsts;
		if (false == sinkAnalyzer->getMTypeDependentSwitches(targetFunction, swInsts))
		{
			DEBUG(errs() << "Error: Couldn't find switch block of the function: " << targetFunction->getName() << "\n");
			return false;
		}
		if (expectedNumSwitches != swInsts.size())
		{
			DEBUG(errs() << "Error: This function is expected to have " << expectedNumSwitches << " switch statement. Not " << swInsts.size() << "!\n");
			return false;
		}
		SwitchInst *theSwitch = swInsts[0];

		// Carve out the targetFunction!
		std::vector<BasicBlock*> BBsToRemove;
		BasicBlock* blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(theSwitch, mType, BBsToRemove);
		if (NULL == blockToRetain)
		{
			return false;
		}
		if (false == adjustBlockTerminator(theSwitch->getParent(), blockToRetain))
		{
			return false;
		}
		theSwitch->dropAllReferences();
		for(unsigned i = 0; i < BBsToRemove.size(); i++)
		{
			DEBUG(errs() << "\tErasing basic block: " << BBsToRemove[i]->getName() << "\n");
			BBsToRemove[i]->dropAllReferences();
		}
		for(unsigned i = 0; i < BBsToRemove.size(); i++)
		{
			BBsToRemove[i]->eraseFromParent();
		}
		theSwitch->eraseFromParent();
		DEBUG(errs() << "Erased unnecessary switch blocks.\n");
		return true;
	}
};

class RsMainChipper : public SchedMainChipper
{

};

class RsCatchBootInitReadyChipper : public IPCSinkChipper
{
public:
	bool chip(Function *targetFunction, int mType, int srcEndpoint)
	{
		if (NULL == sinkAnalyzer || NULL == targetFunction)
		{
			return false;
		}
		const int expectedNumBranches = 1;
		std::vector<BranchInst*> brInsts;
		if (false == sinkAnalyzer->getMTypeDependentBranches(targetFunction, brInsts))
		{
			DEBUG(errs() << "Error: Couldn't find mtype branch(es) in the function: " << targetFunction->getName() << "\n");
			return false;
		}
		if (expectedNumBranches != brInsts.size())
		{
			DEBUG(errs() << "Error: This function is expected to have " << expectedNumBranches << " branch statement. Not " << brInsts.size() << "!\n");
			return false;
		}

		BranchInst *brInst = brInsts[0];
		// Carve out the targetFunction!
		std::vector<BasicBlock*> BBsToRemove;
		BasicBlock* blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(brInst, mType, BBsToRemove);
		if (NULL == blockToRetain)
		{
			return false;
		}
		if (false == adjustBlockTerminator(brInst->getParent(), blockToRetain))
		{
			return false;
		}
		brInst->dropAllReferences();
		for(unsigned i = 0; i < BBsToRemove.size(); i++)
		{
			DEBUG(errs() << "\tErasing basic block: " << BBsToRemove[i]->getName() << "\n");
			BBsToRemove[i]->dropAllReferences();
		}
		for(unsigned i = 0; i < BBsToRemove.size(); i++)
		{
			BBsToRemove[i]->eraseFromParent();
		}
		brInst->eraseFromParent();
		DEBUG(errs() << "Erased unnecessary branched blocks.\n");
		return true;
	}

};

class DsMainChipper : public IPCSinkChipper
{
public:
	bool chip(Function *targetFunction, int mType, int srcEndpoint)
	{
		if (NULL == sinkAnalyzer || NULL == targetFunction)
		{
			return false;
		}
		std::vector<BasicBlock*> BBsToRemove;
		BasicBlock* blockToRetain;

		// Process the switch statement
		const int expectedNumSwitches = 1;
		std::vector<SwitchInst*> swInsts;
		if (false == sinkAnalyzer->getMTypeDependentSwitches(targetFunction, swInsts))
		{
			DEBUG(errs() << "Error: Couldn't find switch block of the function: " << targetFunction->getName() << "\n");
			return false;
		}
		if (expectedNumSwitches != swInsts.size())
		{
			DEBUG(errs() << "Error: This function is expected to have " << expectedNumSwitches << " switch statement. Not " << swInsts.size() << "!\n");
			return false;
		}
		SwitchInst *theSwitch = swInsts[0];

		// Carve out the targetFunction!
		BBsToRemove.clear();
		blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(theSwitch, mType, BBsToRemove);
		if (NULL == blockToRetain)
		{
			return false;
		}
		if (false == adjustBlockTerminator(theSwitch->getParent(), blockToRetain))
		{
			return false;
		}
		theSwitch->dropAllReferences();
		for(unsigned i = 0; i < BBsToRemove.size(); i++)
		{
			DEBUG(errs() << "\tErasing basic block: " << BBsToRemove[i]->getName() << "\n");
			BBsToRemove[i]->dropAllReferences();
		}
		for(unsigned i = 0; i < BBsToRemove.size(); i++)
		{
			BBsToRemove[i]->eraseFromParent();
		}
		theSwitch->eraseFromParent();
		DEBUG(errs() << "Erased unnecessary switch blocks.\n");

		// Process the branch statement
		BBsToRemove.clear();
		// ds_main() uses is_notify() macro for this if statement.
		int isNotifyCallNr = mType;
		isNotifyCallNr = macroIsNotify(isNotifyCallNr);

		const int expectedNumBranches = 1;
		std::vector<BranchInst*> brInsts;
		if (false == sinkAnalyzer->getMTypeDependentBranches(targetFunction, brInsts))
		{
			DEBUG(errs() << "Error: Couldn't find mtype branch(es) in the function: " << targetFunction->getName() << "\n");
			return false;
		}
		if (expectedNumBranches != brInsts.size())
		{
			DEBUG(errs() << "Error: This function is expected to have " << expectedNumBranches << " branch statement. Not " << brInsts.size() << "!\n");
			return false;
		}
		BranchInst *brInst = brInsts[0];
		// Carve out the targetFunction!
		blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(brInst, isNotifyCallNr, BBsToRemove);
		if (NULL == blockToRetain)
		{
			return false;
		}
		if (false == adjustBlockTerminator(brInst->getParent(), blockToRetain))
		{
			return false;
		}
		brInst->dropAllReferences();
		for(unsigned i = 0; i < BBsToRemove.size(); i++)
		{
			DEBUG(errs() << "\tErasing basic block: " << BBsToRemove[i]->getName() << "\n");
			BBsToRemove[i]->dropAllReferences();
		}
		for(unsigned i = 0; i < BBsToRemove.size(); i++)
		{
			BBsToRemove[i]->eraseFromParent();
		}
		brInst->eraseFromParent();
		DEBUG(errs() << "Erased unnecessary branched blocks.\n");
		DEBUG(errs() << "Successfully processed if statement.\n");

		return true;
	}
};

class MxVfsSefCbInitFreshChipper : public RsCatchBootInitReadyChipper
{
	// The processing is exactly similar to rs_catch_boot_init_ready()
};

class MxIsMainChipper : public IPCSinkChipper
{
	//TODO: Process the endpoint as well!
public:
	bool chip(Function *targetFunction, int mType, int srcEndpoint)
	{
		if (NULL == sinkAnalyzer || NULL == targetFunction)
		{
			return false;
		}
		// First process the branch statement involving mtype variable call_nr

		// ds_main() uses is_notify() macro for this if statement.
		int isNotifyCallNr = mType;
		macroIsNotify(isNotifyCallNr);

		const int expectedNumBranches = 1;
		std::vector<BranchInst*> brInsts;
		if (false == sinkAnalyzer->getMTypeDependentBranches(targetFunction, brInsts))
		{
			DEBUG(errs() << "Error: Couldn't find mtype branch(es) in the function: " << targetFunction->getName() << "\n");
			return false;
		}
		if (expectedNumBranches != brInsts.size())
		{
			DEBUG(errs() << "Error: This function is expected to have " << expectedNumBranches << " branch statement. Not " << brInsts.size() << "!\n");
			return false;
		}

		BranchInst *brInst = brInsts[0];
		// Carve out the targetFunction!
		std::vector<BasicBlock*> BBsToRemove;
		BasicBlock* blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(brInst, isNotifyCallNr, BBsToRemove);
		if (NULL == blockToRetain)
		{
			return false;
		}
		if (false == adjustBlockTerminator(brInst->getParent(), blockToRetain))
		{
			return false;
		}
		brInst->dropAllReferences();
		for(unsigned i = 0; i < BBsToRemove.size(); i++)
		{
			DEBUG(errs() << "\tErasing basic block: " << BBsToRemove[i]->getName() << "\n");
			BBsToRemove[i]->dropAllReferences();
		}
		for(unsigned i = 0; i < BBsToRemove.size(); i++)
		{
			BBsToRemove[i]->eraseFromParent();
		}
		brInst->eraseFromParent();
		DEBUG(errs() << "Erased unnecessary branched blocks.\n");
		DEBUG(errs() << "Successfully processed if statement.\n");

		return true;
	}
};

class MxIpcMainChipper : public IPCSinkChipper
{
public:
	bool chip(Function *targetFunction, int mType, int srcEndpoint)
	{
		if (NULL == sinkAnalyzer || NULL == targetFunction)
		{
			return false;
		}

		// First process the branch statement involving mtype variable call_nr
		const int expectedNumBranches = 2;
		std::vector<BranchInst*> brInsts;
		if (false == sinkAnalyzer->getMTypeDependentBranches(targetFunction, brInsts))
		{
			DEBUG(errs() << "Error: Couldn't find mtype branch(es) in the function: " << targetFunction->getName() << "\n");
			return false;
		}
		if (expectedNumBranches != brInsts.size())
		{
			DEBUG(errs() << "Error: This function is expected to have " << expectedNumBranches << " branch statement. Not " << brInsts.size() << "!\n");
			return false;
		}

		for (unsigned i = 0; i < brInsts.size(); i++)
		{
			BranchInst *brInst = brInsts[i];

			// Carve out the targetFunction!
			std::vector<BasicBlock*> BBsToRemove;
			BasicBlock* blockToRetain = NULL;
			bool getTrueBlock = false;

			// ipc_main() has a specific logic.
			switch(i)
			{
				case 0:		// first branch		
					{					
						getTrueBlock = (mType & NOTIFY_MESSAGE);
						blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(brInst, getTrueBlock, BBsToRemove);
						break;
					}

				case 1:		// second branch
					{
						int ipc_number = mType - (IPC_BASE + 1);
						getTrueBlock = ((ipc_number >= 0) && (ipc_number < IPC_CALLS_SIZE));
						blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(brInst, getTrueBlock, BBsToRemove);
						break;
					}

				default:
					{
						break;
					}
			}
			if (NULL == blockToRetain)
			{
				return false;
			}
			if (false == adjustBlockTerminator(brInst->getParent(), blockToRetain))
			{
				return false;
			}
			brInst->dropAllReferences();
			for(unsigned i = 0; i < BBsToRemove.size(); i++)
			{
				DEBUG(errs() << "\tErasing basic block: " << BBsToRemove[i]->getName() << "\n");
				BBsToRemove[i]->dropAllReferences();
			}
			for(unsigned i = 0; i < BBsToRemove.size(); i++)
			{
				BBsToRemove[i]->eraseFromParent();
			}
			brInst->eraseFromParent();
			DEBUG(errs() << "Erased unnecessary branched blocks.\n");
			DEBUG(errs() << "Successfully processed if statement.\n");
		}
		return true;
	}

MxIpcMainChipper() : IPC_BASE(3329), IPC_CALLS_SIZE(7) {}

private:
	const int IPC_BASE;
	const int IPC_CALLS_SIZE;
};

class MxPmMainChipper : public IPCSinkChipper
{
public:
	bool chip(Function *targetFunction, int mType, int srcEndpoint)
	{
		if (NULL == sinkAnalyzer || NULL == targetFunction)
		{
			return false;
		}

		// First process the branch statement involving mtype variable call_nr
		const int expectedNumBranches = 2;
		std::vector<BranchInst*> brInsts;
		if (false == sinkAnalyzer->getMTypeDependentBranches(targetFunction, brInsts))
		{
			DEBUG(errs() << "Error: Couldn't find mtype branch(es) in the function: " << targetFunction->getName() << "\n");
			return false;
		}
		if (expectedNumBranches != brInsts.size())
		{
			DEBUG(errs() << "Error: This function is expected to have " << expectedNumBranches << " branch statement. Not " << brInsts.size() << "!\n");
			return false;
		}

		for (unsigned i = 0; i < brInsts.size(); i++)
		{
			BranchInst *brInst = brInsts[i];

			// Carve out the targetFunction!
			std::vector<BasicBlock*> BBsToRemove;
			BasicBlock* blockToRetain = NULL;
			bool getTrueBlock = false;

			// pm_main() has a specific logic.
			switch(i)
			{
				case 0:		// first branch		
					{					
						getTrueBlock = isVfsPmRs(mType) && (srcEndpoint == VFS_PROC_NR);
						blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(brInst, getTrueBlock, BBsToRemove);
						break;
					}

				case 1:		// second branch
					{
						getTrueBlock = isPmCall(mType);
						blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(brInst, getTrueBlock, BBsToRemove);
						break;
					}

				default:
					{
						break;
					}
			}
			if (NULL == blockToRetain)
			{
				return false;
			}
			if (false == adjustBlockTerminator(brInst->getParent(), blockToRetain))
			{
				return false;
			}
			brInst->dropAllReferences();
			for(unsigned i = 0; i < BBsToRemove.size(); i++)
			{
				DEBUG(errs() << "\tErasing basic block: " << BBsToRemove[i]->getName() << "\n");
				BBsToRemove[i]->dropAllReferences();
			}
			for(unsigned i = 0; i < BBsToRemove.size(); i++)
			{
				BBsToRemove[i]->eraseFromParent();
			}
			brInst->eraseFromParent();
			DEBUG(errs() << "Erased unnecessary branched blocks.\n");
			DEBUG(errs() << "Successfully processed if statement.\n");
		}
		return true;
	}

private:
	bool isVfsPmRs(int mType)
	{
		return (((mType) & ~0x7f) == VFS_PM_RS_BASE);
	}

	bool isPmCall(int mType)
	{
		const int PM_BASE = 0x0000;
		return (((mType) & ~0xff) == PM_BASE);
	}
};

class MxVmMainChipper : public IPCSinkChipper
{
	// TODO: work on transid getting detected. (extended data flow)
public:
	bool chip(Function *targetFunction, int mType, int srcEndpoint)
	{
		if (NULL == sinkAnalyzer || NULL == targetFunction)
		{
			return false;
		}
		VM_FORK = VM_RQ_BASE + 1;
		
		DEBUG(errs() << "VmMain chipping.\n");

		// First process the branch statement involving mtype variable call_nr
		const int expectedNumBranches = 2;
		std::vector<BranchInst*> brInsts;
		if (false == sinkAnalyzer->getMTypeDependentBranches(targetFunction, brInsts))
		{
			DEBUG(errs() << "Error: Couldn't find mtype branch(es) in the function: " << targetFunction->getName() << "\n");
			return false;
		}
		if (expectedNumBranches != brInsts.size())
		{
			DEBUG(errs() << "Error: This function is expected to have " << expectedNumBranches << " branch statement. Not " << brInsts.size() << "!\n");
			return false;
		}

		for (unsigned i = 0; i < brInsts.size(); i++)
		{
			BranchInst *brInst = brInsts[i];

			// Carve out the targetFunction!
			std::vector<BasicBlock*> BBsToRemove;
			BasicBlock* blockToRetain = NULL;
			bool getTrueBlock = false;

			// vm_main() has a specific logic.
			switch(i)
			{
				case 0:		// first branch		
					{					
						getTrueBlock = (mType == RS_INIT) && (srcEndpoint == RS_PROC_NR);
						blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(brInst, getTrueBlock, BBsToRemove);
						break;
					}

				case 1:		// second branch
					{
						getTrueBlock = (mType == VM_PAGEFAULT);
						blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(brInst, getTrueBlock, BBsToRemove);
						break;
					}

				default:
					{
						break;
					}
			}
			if (NULL == blockToRetain)
			{
				return false;
			}
			if (false == adjustBlockTerminator(brInst->getParent(), blockToRetain))
			{
				return false;
			}
			brInst->dropAllReferences();
			for(unsigned i = 0; i < BBsToRemove.size(); i++)
			{
				DEBUG(errs() << "\tErasing basic block: " << BBsToRemove[i]->getName() << "\n");
				BBsToRemove[i]->dropAllReferences();
			}
			for(unsigned i = 0; i < BBsToRemove.size(); i++)
			{
				BBsToRemove[i]->eraseFromParent();
			}
			brInst->eraseFromParent();
			DEBUG(errs() << "Erased unnecessary branched blocks.\n");
			DEBUG(errs() << "Successfully processed if statement.\n");
		}

		int c = macroCallNumber(mType);
		int cmi = callMapIndex(VM_FORK);
		if ((c != -1) && (c == cmi))
		{
			// Replace the function pointer call to vm_do_fork to direct call to vm_do_fork.
			Function *vm_do_forkFunction = targetFunction->getParent()->getFunction("mx_vm_do_fork");
			if (NULL == vm_do_forkFunction)
			{
				DEBUG(errs() << "WARNING: Couldnt fetch the vm_do_fork function.\n");
				return true;
			}

			FuseMinixHelper *fmHelper = new FuseMinixHelper(targetFunction->getParent());
			bool stop = false;
			for(inst_iterator I = inst_begin(targetFunction), E = inst_end(targetFunction); I != E; I++)
			{
				LoadInst *lInst = dyn_cast<LoadInst>(&(*I));
				if (NULL != lInst && (std::string::npos != lInst->getPointerOperand()->getName().find("vmc_func")))
				{
#if LLVM_VERSION >= 37
					std::vector<User*> users (lInst->user_begin(), lInst->user_end());
#else
					std::vector<User*> users (lInst->use_begin(), lInst->use_end());
#endif
					for(unsigned i = 0; i < users.size(); i++)
					{
						CallInst *callInst = dyn_cast<CallInst>(users[i]);
						if (NULL != callInst)
						{
							DEBUG(errs() << "Found the vmc_func function call.\n");
							// replace the call with call to vm_do_fork.
							if (false == fmHelper->replaceCallInst(callInst, vm_do_forkFunction))
							{
								DEBUG(errs() << "WARNING : Failed replacing vmc_func call inst with vm_do_fork().\n");
							}
							else
							{
								DEBUG(errs() << "Successfully replaced vmc_func call inst with vm_do_fork().\n");
							}
							stop = true;
							break;
						}
					}
				}
				if (stop)
				{
					break;
				}
			}
		}

		return true;
	}

protected:
	int VM_FORK;
	int macroCallNumber(int mType)
	{
		/*
		 snip from minix's sources:
		 #define CALLNUMBER(c) (((c) >= VM_RQ_BASE &&                            \
                        (c) < VM_RQ_BASE + ELEMENTS(vm_calls)) ?        \
                        ((c) - VM_RQ_BASE) : -1)

         No of elements in vm_calls = NR_VM_CALLS which is 48 as of this writing.
		*/
		if (mType >= VM_RQ_BASE)
		{
			if (mType < VM_RQ_BASE + NR_VM_CALLS)
			{
				return mType - VM_RQ_BASE;
			}
		}
		return -1;
	}
	int callMapIndex(int code)
	{
		// Corresponds to CALLMAP macro, but here we dont implement it exactly.
		return macroCallNumber(code);
	}
};

class MxVfsMainChipper : public IPCSinkChipper
{
public:
	bool chip(Function *targetFunction, int mType, int srcEndpoint)
	{
		if (NULL == sinkAnalyzer || NULL == targetFunction)
		{
			return false;
		}

		// First process the branch statement 
		const int expectedNumBranches = 2;
		std::vector<BranchInst*> brInsts;
		if (false == sinkAnalyzer->getMTypeDependentBranches(targetFunction, brInsts))
		{
			DEBUG(errs() << "Error: Couldn't find mtype branch(es) in the function: " << targetFunction->getName() << "\n");
			return false;
		}
		if (expectedNumBranches != brInsts.size())
		{
			DEBUG(errs() << "Error: This function is expected to have " << expectedNumBranches << " branch statement. Not " << brInsts.size() << "!\n");
			return false;
		}
		return true;
		for (unsigned i = 0; i < brInsts.size(); i++)
		{
			BranchInst *brInst = brInsts[i];

			// Carve out the targetFunction!
			std::vector<BasicBlock*> BBsToRemove;
			BasicBlock* blockToRetain = NULL;
			bool getTrueBlock = false;

			// vfs_main() has a specific logic.
			switch(i)
			{
				case 0:		// first branch		
					{					
						getTrueBlock = isVfsFsTransId(mType);
						blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(brInst, getTrueBlock, BBsToRemove);
						break;
					}

				case 1:		// second branch
					{
						getTrueBlock = (bool) macroIsNotify(mType);
						blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(brInst, getTrueBlock, BBsToRemove);
						break;
					}

				default:
					{
						break;
					}
			}
			if (NULL == blockToRetain)
			{
				return false;
			}
			if (false == adjustBlockTerminator(brInst->getParent(), blockToRetain))
			{
				return false;
			}
			brInst->dropAllReferences();
			for(unsigned i = 0; i < BBsToRemove.size(); i++)
			{
				DEBUG(errs() << "\tErasing basic block: " << BBsToRemove[i]->getName() << "\n");
				BBsToRemove[i]->dropAllReferences();
			}
			for(unsigned i = 0; i < BBsToRemove.size(); i++)
			{
				BBsToRemove[i]->eraseFromParent();
			}
			brInst->eraseFromParent();
			DEBUG(errs() << "Erased unnecessary branched blocks.\n");
			DEBUG(errs() << "Successfully processed if statement.\n");
		}
		return true;
	}	 
};

class MxDevmanStartVtreefsChipper : public IPCSinkChipper
{
public:
	bool chip(Function *targetFunction, int mType, int srcEndpoint)
	{
		if (NULL == sinkAnalyzer || NULL == targetFunction)
		{
			return false;
		}

		// First process the branch statement 
		const int expectedNumBranches = 2;
		std::vector<BranchInst*> brInsts;
		if (false == sinkAnalyzer->getMTypeDependentBranches(targetFunction, brInsts))
		{
			DEBUG(errs() << "Error: Couldn't find mtype branch(es) in the function: " << targetFunction->getName() << "\n");
			return false;
		}
		if (expectedNumBranches != brInsts.size())
		{
			DEBUG(errs() << "Error: This function is expected to have " << expectedNumBranches << " branch statement. Not " << brInsts.size() << "!\n");
			return false;
		}
		return true;
		for (unsigned i = 0; i < brInsts.size(); i++)
		{
			BranchInst *brInst = brInsts[i];

			// Carve out the targetFunction!
			std::vector<BasicBlock*> BBsToRemove;
			BasicBlock* blockToRetain = NULL;
			bool getTrueBlock = false;

			// vfs_main() has a specific logic.
			switch(i)
			{
				case 0:		// first branch		
					{					
						getTrueBlock = isVfsFsTransId(mType);
						blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(brInst, getTrueBlock, BBsToRemove);
						break;
					}

				case 1:		// second branch
					{
						getTrueBlock = (bool) macroIsNotify(mType);
						blockToRetain = sinkAnalyzer->getBasicBlocksToPrune(brInst, getTrueBlock, BBsToRemove);
						break;
					}

				default:
					{
						break;
					}
			}
			if (NULL == blockToRetain)
			{
				return false;
			}
			if (false == adjustBlockTerminator(brInst->getParent(), blockToRetain))
			{
				return false;
			}
			brInst->dropAllReferences();
			for(unsigned i = 0; i < BBsToRemove.size(); i++)
			{
				DEBUG(errs() << "\tErasing basic block: " << BBsToRemove[i]->getName() << "\n");
				BBsToRemove[i]->dropAllReferences();
			}
			for(unsigned i = 0; i < BBsToRemove.size(); i++)
			{
				BBsToRemove[i]->eraseFromParent();
			}
			brInst->eraseFromParent();
			DEBUG(errs() << "Erased unnecessary branched blocks.\n");
			DEBUG(errs() << "Successfully processed if statement.\n");
		}
		return true;
	}
};

}
#endif
