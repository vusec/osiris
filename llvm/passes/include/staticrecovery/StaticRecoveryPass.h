#ifndef LTCKPT_STATIC_RECOVERY_PASS_H
#define LTCKPT_STATIC_RECOVERY_PASS_H

#define LTCKPT_HOOK_SUICIDE						"ltckpt_do_suicide_per_site"
#define LTCKPT_STRUCT_SHUTTER					"struct.window_shutter"
#define LTCKPT_STRUCT_SHUTTER_PROF				"struct.wshutter_prof"
#define LTCKPT_SHUTTER_BOARD_ARRAY				"g_shutter_board"
#define LTCKPT_SHUTTER_BOARD_SIZE_NAME			"g_num_window_shutters"
#define LTCKPT_SHUTTER_BOARD_KC_SIZE_NAME		"g_num_kernelcall_shutters"
#define LTCKPT_CURRENT_MODULE_NAME				"g_module_name"
#define LTCKPT_MODULE_NAME_SIZE					10
#define LTCKPT_SHUTTER_BOARD_IPC_END			1024
#define LTCKPT_SHUTTER_BOARD_MAX_SIZE			2560  // rest is for the _kernel_call sites
#define	LTCKPT_START_OF_WINDOW_HOOK_NAME		"ltckpt_detect_start_of_window_hook"
#define LTCKPT_START_OF_WINDOW_HOOK_TARGET		"sef_handle_message"
#define LTCKPT_HOOK_RECOVERY_IDEMPOTENT			"ltckpt_set_idempotent_recovery"
#define LTCKPT_HOOK_RECOVERY_REQUEST_SPECIFIC 	"ltckpt_set_request_specific_recovery"
#define LTCKPT_HOOK_RECOVERY_PROCESS_LOCAL		"ltckpt_set_process_local_recovery"
#define LTCKPT_HOOK_RECOVERY_FAIL_STOP			"ltckpt_set_fail_stop_recovery"
#define LTCKPT_HOOK_RECOVERY_NAIVE			"ltckpt_set_naive_recovery"
#define KERNEL_CALL								"_kernel_call"
#define KERNEL_CALL_SITE_IDENTIFIER				0xC0DE00

#define IPC_FORCE_DECISION_DISABLED			0
#define IPC_FORCE_DECISION_OPTIMISTIC			1
#define IPC_FORCE_DECISION_PESSIMISTIC			4
#define IPC_FORCE_DECISION_NAIVE_NEVER_REPLY		5
#define IPC_FORCE_DECISION_NAIVE_CONDITIONAL_REPLY	6
#define IPC_FORCE_DECISION_NAIVE_ALWAYS_REPLY		7
#define IPC_FORCE_DECISION_STATELESS			8 /* pass should not be invoked */
#define IPC_FORCE_DECISION_ENHANCED			9

// #define FUSE_IPC_SITE_ID_KEY	"fuse_ipc_site_id"
//#include <fusedminixcallgraphy/common.h>
//#include <fusedminixcallgraphy/InputLoader.h>
#include "fuseminix_common.h"
#include "InputLoader.h"
#if LLVM_VERSION >= 37
#include <llvm/IR/Verifier.h>
#include <llvm/IR/InstIterator.h>
#else
#include <llvm/Analysis/Verifier.h>
#include <llvm/Support/InstIterator.h>
#endif
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <fstream>
#include <staticrecovery/common.h>

using namespace llvm;

namespace llvm 
{

STATISTIC(NumTargetIPCCallInsts, "Number of target call sites found.");
STATISTIC(NumTargetKernelCallInsts, "Number of _kernel_call target call sites found.");
STATISTIC(NumKernelCallsExcluded, "Number of _kernel_call target call sites EXCLUDED.");
STATISTIC(NumRecoveryHooks, "Number of recovery hooks placed.");
STATISTIC(NumSuicideHooks, "Number of suicide hooks placed.");

class StaticRecoveryPass : public ModulePass
{
public:
	static char ID;
	StaticRecoveryPass();
	virtual bool runOnModule(Module &M);
	virtual void getAnalysisUsage(AnalysisUsage &AU) const;
	static unsigned PassRunCount;

private:
	Module *M;
	Function *ltckptSuicideHook;
	Function *ltckptStartOfWindowHook;
	Function *ltckptrecovNaiveHook;
	std::string targetModuleName;
	std::map<enum Decision, Function *> recoveryHooksMap;
	std::string decisionMapFile;
	std::string kernelcallMapFile;
	std::string kernelcallExcludeFile;
	std::string idempotentKernelCallersFile;
	bool noSuicideOnKernelCall;
	long kernelCallsiteCounter;
	std::map<long, kernelcall_decision_t>	*kernelcallDecisions;
	std::map<long, bool> *kernelcallSuicideMap;
	std::set<long> *kernelcallExclusionSet;
	std::set<std::string> *idempotentKernelCallersSet;
	std::map<uint64_t, enum Decision> *recoveryDecisions;
	std::map<uint64_t, long> *suicideMap; 
	std::map<CallInst*, uint64_t> *targetCallInstsSiteMap;

	bool fetchTargetCallSites();
	void getSuicideHook();
	void getRecoveryHooks();
	bool plantHooks(bool suicide=false, int ipcSiteForceDecision=0);
	// std::string compareModuleNames(std::string modName1, std::string modName2);
	int  getIPCSiteID(CallInst *CI, uint64_t &site_id);
	bool loadStaticRecoveryDecisions();
};

unsigned StaticRecoveryPass::PassRunCount = 0;

StaticRecoveryPass::StaticRecoveryPass() : ModulePass(ID) {}

bool StaticRecoveryPass::fetchTargetCallSites()
{
	// fetch the target callsites based on metadata in the bitcode
	targetCallInstsSiteMap = new std::map<CallInst*, uint64_t>();

	Module::FunctionListType &functionList = M->getFunctionList();
	for (Module::iterator it = functionList.begin(); it != functionList.end(); ++it)
	{
		Function *F = it;
		if (F->isIntrinsic())
		{
			continue;
		}
		DEBUG(errs() << "In function: " << F->getName() << "\n");
		for(Function::iterator BI = F->begin(), BE = F->end(); BI != BE; BI++)
		{
			BasicBlock *BB = BI;
			for (BasicBlock::iterator II = BB->begin(), IE = BB->end(); II != IE; II++)
			{
				CallInst *CI = dyn_cast<CallInst>(II);
				if (NULL == CI)
				{
					continue;
				}

				Function *calledFunction = CI->getCalledFunction();
				if (NULL == calledFunction )
				{
					DEBUG(errs() << "\t WARNING: calledFunction is NULL\n");
					continue;
				}
				DEBUG(errs() << "\tCallinst to function: " << CI->getCalledFunction()->getName() << "\n");

				uint64_t site_id;
				if (0 == calledFunction->getName().find(KERNEL_CALL))
				{
					DEBUG(errs() << "\t Kernel call found: " << calledFunction->getName());
					targetCallInstsSiteMap->insert(std::make_pair(CI, KERNEL_CALL_SITE_IDENTIFIER)); // 2nd arg is just dummy and serves as id.
					NumTargetKernelCallInsts++;
				} 
				else if (-1 == getIPCSiteID(CI, site_id))
				{
					continue;
				}
				else
				{
					NumTargetIPCCallInsts++;
					targetCallInstsSiteMap->insert(std::make_pair(CI, site_id));
				}
			}
		}
	}

	DEBUG(errs() << " Number of target callsites for hook placement: " << targetCallInstsSiteMap->size() << "\n");
	if (0 == targetCallInstsSiteMap->size())
	{
		DEBUG(errs() << "No target callsites for hook placement found.\n");
		return false;
	}
	return true;
}

void StaticRecoveryPass::getSuicideHook()
{
	DEBUG(errs() << "Getting suicide hook.\n");
	Constant *ltckptSuicideHookFunc = M->getFunction(LTCKPT_HOOK_SUICIDE);
	assert(ltckptSuicideHookFunc != NULL);
	ltckptSuicideHook    = cast<Function>(ltckptSuicideHookFunc);
	ltckptSuicideHook->setCallingConv(CallingConv::Fast);

	return;
}

void StaticRecoveryPass::getRecoveryHooks()
{
	DEBUG(errs() << "Getting recovery hooks.\n");

	Constant *ltckptStartOfWindowFunc = M->getFunction(LTCKPT_START_OF_WINDOW_HOOK_NAME);
	assert(ltckptStartOfWindowFunc != NULL);
	ltckptStartOfWindowHook  = cast<Function>(ltckptStartOfWindowFunc);
	ltckptStartOfWindowHook->setCallingConv(CallingConv::Fast);

	Constant *ltckptrecovIdempotentFunc = M->getFunction(LTCKPT_HOOK_RECOVERY_IDEMPOTENT);
	assert(ltckptrecovIdempotentFunc != NULL);
	Function *recovHook    = cast<Function>(ltckptrecovIdempotentFunc);
	recovHook->setCallingConv(CallingConv::Fast);
	recoveryHooksMap.insert(std::make_pair(DECISION_IDEMPOTENT, recovHook));

	Constant *ltckptrecovReqSpecificFunc = M->getFunction(LTCKPT_HOOK_RECOVERY_REQUEST_SPECIFIC);
	assert(ltckptrecovReqSpecificFunc != NULL);
	recovHook    = cast<Function>(ltckptrecovReqSpecificFunc);
	recovHook->setCallingConv(CallingConv::Fast);
	recoveryHooksMap.insert(std::make_pair(DECISION_REQUEST_SPECIFIC, recovHook));

	Constant *ltckptrecovProcLocalFunc = M->getFunction(LTCKPT_HOOK_RECOVERY_PROCESS_LOCAL);
	assert(ltckptrecovProcLocalFunc != NULL);
	recovHook    = cast<Function>(ltckptrecovProcLocalFunc);
	recovHook->setCallingConv(CallingConv::Fast);
	recoveryHooksMap.insert(std::make_pair(DECISION_PROCESS_SPECIFIC, recovHook));

	Constant *ltckptrecovFailStopFunc = M->getFunction(LTCKPT_HOOK_RECOVERY_FAIL_STOP);
	assert(ltckptrecovFailStopFunc != NULL);
	recovHook    = cast<Function>(ltckptrecovFailStopFunc);
	recovHook->setCallingConv(CallingConv::Fast);
	recoveryHooksMap.insert(std::make_pair(DECISION_GLOBAL_CHANGE, recovHook));

	Constant *ltckptrecovNaiveFunc = M->getFunction(LTCKPT_HOOK_RECOVERY_NAIVE);
	assert(ltckptrecovNaiveFunc != NULL);
	ltckptrecovNaiveHook    = cast<Function>(ltckptrecovNaiveFunc);
	ltckptrecovNaiveHook->setCallingConv(CallingConv::Fast);

	return;
}

bool StaticRecoveryPass::plantHooks(bool suicide, int ipcSiteForceDecision)
{
	bool recovery =  true;
	if (false == fetchTargetCallSites())
	{
		DEBUG(errs() << "WARNING: Fetching targetCallInstsSiteMap failed.\n");
		return false;
	}

	if (NULL == recoveryDecisions || (0 == recoveryDecisions->size()))
	{
		errs() << "Recovery decisions not initialized.\n";
		errs() << "Not placing recovery hooks.\n";
		recovery = false;
	}
	DEBUG(errs() << "recoveryDecisions size: " << recoveryDecisions->size() << "\n");
	if (NULL != suicideMap)
	{
		DEBUG(errs() << "suicideMap size: " << suicideMap->size() << "\n");
	}

	getSuicideHook();
	getRecoveryHooks();

	kernelCallsiteCounter = LTCKPT_SHUTTER_BOARD_IPC_END;
	kernelcallSuicideMap = new std::map<long, bool>();

	if (suicide)
	{
		errs() << "\t + suicide hooks.\n";
	}
	if (recovery)
	{
		errs() << "\t + recovery hooks.\n";
		// Plant start of window hook, in the sef_handle_message() function.
		assert(ltckptStartOfWindowHook != NULL);
		Function *sefHandleMessageFunc = M->getFunction(LTCKPT_START_OF_WINDOW_HOOK_TARGET);
		for (inst_iterator I = inst_begin(sefHandleMessageFunc), E = inst_end(sefHandleMessageFunc);I != E; I++)
		{
			// insert the hook before every return statement.
			ReturnInst *rI = dyn_cast<ReturnInst>(&(*I));
			if (NULL != rI)
			{
				{
					std::vector<Value*> args;
					CallInst *callInstToHook = PassUtil::createCallInstruction(ltckptStartOfWindowHook, args, "", rI);
					callInstToHook->setCallingConv(CallingConv::Fast);
				}
				if (ipcSiteForceDecision == IPC_FORCE_DECISION_NAIVE_NEVER_REPLY ||
					ipcSiteForceDecision == IPC_FORCE_DECISION_NAIVE_CONDITIONAL_REPLY ||
					ipcSiteForceDecision == IPC_FORCE_DECISION_NAIVE_ALWAYS_REPLY) {
					std::vector<Value*> args(1);
					args[0] = ConstantInt::get(M->getContext(), APInt(32, ipcSiteForceDecision, false));
					CallInst *callInstToHook = PassUtil::createCallInstruction(ltckptrecovNaiveHook, args, "", rI);
					callInstToHook->setCallingConv(CallingConv::Fast);
				}
				DEBUG(errs() << "inserted ltckpt_detect_start_of_window_hook() call, before return inst in sef_handle_message().\n");
			}
		}
	}

	// Foreach target callsite, mark the window closure by inserting call to hook function.
	for(std::map<CallInst*, uint64_t>::iterator it = targetCallInstsSiteMap->begin(), ie = targetCallInstsSiteMap->end(); it != ie; it++)
	{
		CallInst *currCallInst = (*it).first;
		uint64_t site_id = (*it).second; // shorten it to avoid issues with handling i64 properly.

		kernelcall_decision_t currKernelCallDecision; // uninitialized
		std::vector<Value*> args(1);

		if (KERNEL_CALL_SITE_IDENTIFIER == site_id)
		{
			long kernelcallNumber = KERNEL_CALL_SITE_IDENTIFIER;
			bool idempotentKernelCallCaller = false;
			if (2 == currCallInst->getNumArgOperands())
			{
				ConstantInt *constInt = dyn_cast<ConstantInt>(currCallInst->getArgOperand(0));
				if (NULL != constInt)
				{
					kernelcallNumber = (long)(*(constInt->getValue().getRawData()) - KERNEL_CALL_BASE);
					DEBUG(errs() << "kernelcall number: " << kernelcallNumber << " found.\n");
				}
			}
			if (KERNEL_CALL_SITE_IDENTIFIER == kernelcallNumber)
			{
				errs() << "WARNING: Couldn't extract kernelcall number from callsite!\n";
				continue;
			}
			else
			{
				// EXCLUDE if specified so...
				if (kernelcallExclusionSet && (0 != kernelcallExclusionSet->count(kernelcallNumber)))
				{
					DEBUG(errs() << "Excluding kernelcallnumber: " << kernelcallNumber << "\n");
					NumKernelCallsExcluded++;
					continue;
				}
				// Is the kernel call idempotent?
				if (idempotentKernelCallersSet && (0 != idempotentKernelCallersSet->size()))
				{
					DEBUG(errs() << "Is kernelcall caller idempotent? \n");
					Function *kernelCallCaller = currCallInst->getParent()->getParent();
					std::string currentkernelcallcallerName = kernelCallCaller->getName();
					if (0 != idempotentKernelCallersSet->count(currentkernelcallcallerName))
					{
						DEBUG(errs() << "kernel call caller found in idemp annotation: " << currentkernelcallcallerName << "\n");
						currKernelCallDecision.decision = DECISION_IDEMPOTENT;
						currKernelCallDecision.suicide  = 0; // No suicide
						idempotentKernelCallCaller = true;
					}
					else
					{
						DEBUG(errs() << "  Nope. kernel caller: " << currentkernelcallcallerName << " is not idempotent.\n");
					}
				}
				if (!idempotentKernelCallCaller)
				{
					if (kernelcallDecisions && (0 != kernelcallDecisions->size()) && (0 != kernelcallDecisions->count(kernelcallNumber)))
					{
						//suicide and the decision values are set.
						DEBUG(errs() << "Using kernelcallDecisions value for : " << kernelcallNumber << "\n");
						currKernelCallDecision = kernelcallDecisions->find(kernelcallNumber)->second;
					}
					else
					{
						currKernelCallDecision.decision = DECISION_GLOBAL_CHANGE;
						currKernelCallDecision.suicide  = 1; // By default, we suicide.
					}
				}
			}

			args[0] = ConstantInt::get(M->getContext(), APInt(64, (uint64_t) (++kernelCallsiteCounter), false));
			DEBUG(errs() << "this kernel_call site id: " << kernelCallsiteCounter << "\n");	
		}
		else
		{
			args[0] = ConstantInt::get(M->getContext(), APInt(64, site_id, false));
			DEBUG(errs() << "this site id: " << site_id << "\n");
		}

		// First suicide hook and then the recovery hook, if specified, 
		// so that suicide occurs before the recovery window closes.
		if (suicide)
		{
			if ((KERNEL_CALL_SITE_IDENTIFIER != site_id) || (!noSuicideOnKernelCall))
			{
				assert(ltckptSuicideHook != NULL);
				CallInst *callInstToHook = PassUtil::createCallInstruction(ltckptSuicideHook, args, "", currCallInst);
				callInstToHook->setCallingConv(CallingConv::Fast);
				callInstToHook->setIsNoInline();
				NumSuicideHooks++;

				if (KERNEL_CALL_SITE_IDENTIFIER == site_id)
				{
					// save suicide entry for later; to be used for updating switch board.
					if (0 == kernelcallSuicideMap->count(kernelCallsiteCounter))
					{
						kernelcallSuicideMap->insert(std::make_pair(kernelCallsiteCounter, (bool)currKernelCallDecision.suicide));
					}
				}
			}
		}
		if (recovery) 
		{
			// Note: For the direct IPC callers, which aren't routed through fuser functions
			// by default we assign DECISION_GLOBAL_CHANGE. In reality, they are outside
			// of the useful callgraph. 
			enum Decision decision = DECISION_GLOBAL_CHANGE;

			if (KERNEL_CALL_SITE_IDENTIFIER == site_id)
			{
				decision = currKernelCallDecision.decision;
				site_id = kernelCallsiteCounter; // just for the sake of debug messages.
			}
			else if (ipcSiteForceDecision != IPC_FORCE_DECISION_DISABLED) 
			{
				switch (ipcSiteForceDecision) {
				case IPC_FORCE_DECISION_OPTIMISTIC:
				case IPC_FORCE_DECISION_NAIVE_NEVER_REPLY:
				case IPC_FORCE_DECISION_NAIVE_CONDITIONAL_REPLY:
				case IPC_FORCE_DECISION_NAIVE_ALWAYS_REPLY:
					decision = DECISION_IDEMPOTENT;
					break;
				case IPC_FORCE_DECISION_PESSIMISTIC:
					decision = DECISION_GLOBAL_CHANGE;
					break;
				case IPC_FORCE_DECISION_STATELESS:
					errs() << "Pass should not be invoked for stateless recovery mode\n";
					abort();
					break;
				case IPC_FORCE_DECISION_ENHANCED: {
					Function *f = currCallInst->getParent()->getParent();
					if (f->getName().equals("_brk") ||
						f->getName().equals("getepinfo") ||
						f->getName().equals("vm_notify_sig") ||
						f->getName().equals("vm_willexit")) {
						decision = DECISION_IDEMPOTENT;
					}
					}
					break;
				default:
					errs() << "Invalid value for parameter -recovery-ipc-force-decision=" << ipcSiteForceDecision << "\n";
					abort();
					break;
				}
			}
			else if (recoveryDecisions && (0 != recoveryDecisions->count(site_id)))
			{
				decision = recoveryDecisions->find(site_id)->second;
			} 

			// NOTE: No more from here on until end of loop the site_id 
			// would remain KERNEL_CALL_SITE_IDENTIFIER if it was before.
			
			// lets place the appropriate decision based hook.
			Function *theHook = NULL;

			switch(decision)
			{
				case DECISION_IDEMPOTENT:
					DEBUG(errs() << "connecting site_id: " << site_id << " to " << "decision hook: " 
								 << decisionToStr(DECISION_IDEMPOTENT) << "\n");
					theHook = recoveryHooksMap.find(DECISION_IDEMPOTENT)->second;	
					break;

				case DECISION_REQUEST_SPECIFIC:
					DEBUG(errs() << "connecting site_id: " << site_id << " to " << "decision hook: " 
							     << decisionToStr(DECISION_REQUEST_SPECIFIC) << "\n");
					theHook = recoveryHooksMap.find(DECISION_REQUEST_SPECIFIC)->second;
					break;

				case DECISION_PROCESS_SPECIFIC:
					DEBUG(errs() << "connecting site_id: " << site_id << " to " << "decision hook: " 
							     << decisionToStr(DECISION_PROCESS_SPECIFIC) << "\n");
					theHook = recoveryHooksMap.find(DECISION_PROCESS_SPECIFIC)->second;
					break;

				case DECISION_GLOBAL_CHANGE:
					DEBUG(errs() << "connecting site_id: " << site_id << " to " << "decision hook: " 
							     << decisionToStr(DECISION_GLOBAL_CHANGE) << "\n");
					theHook = recoveryHooksMap.find(DECISION_GLOBAL_CHANGE)->second;
					break;

				default:
					DEBUG(errs() << "WARNING: connecting site_id: " << site_id << " to " << "DEFAULT decision hook: " 
							     << decisionToStr(DECISION_GLOBAL_CHANGE) << "\n");
					theHook = recoveryHooksMap.find(DECISION_GLOBAL_CHANGE)->second;
					break;
			}

			CallInst *callInstToHook = NULL;
			assert(theHook != NULL);
			callInstToHook = PassUtil::createCallInstruction(theHook, args, "", currCallInst);
			callInstToHook->setCallingConv(CallingConv::Fast);
			NumRecoveryHooks++;
		}
		// else if (recovery)
		// {
		// 	DEBUG(errs() << "WARNING: recovery decision unavailable for site_id: " << site_id << " - Skipping.\n");
		// }
	}

	// Lastly lets not forget to set right values to the global variables in the static library

	GlobalVariable *gvar_swboard_size = M->getGlobalVariable((StringRef) LTCKPT_SHUTTER_BOARD_SIZE_NAME);
	if (NULL == gvar_swboard_size)
	{
		DEBUG(errs() << "WARNING: Shutter board size variable couldn't be found.\n");
		return false;
	}

	DEBUG(errs() << "Setting gvar_swboard_size to " << NumTargetIPCCallInsts << ".\n");
	ConstantInt* const_boardsize = ConstantInt::get(M->getContext(), APInt(64, (uint64_t) NumTargetIPCCallInsts, false));
	gvar_swboard_size->setInitializer(const_boardsize);

	GlobalVariable *gvar_kernelcall_swboard_size = M->getGlobalVariable((StringRef) LTCKPT_SHUTTER_BOARD_KC_SIZE_NAME);
	if (NULL == gvar_kernelcall_swboard_size)
	{
		DEBUG(errs() << "WARNING: kernelcall shutter board size variable couldn't be found.\n");
		return false;
	}
	// We write the size of the kernel call area rather than the absolute last used site number.
	DEBUG(errs() << "Setting gvar_kernelcall_swboard_size to " << kernelCallsiteCounter - LTCKPT_SHUTTER_BOARD_IPC_END << ".\n");
	ConstantInt* const_kc_boardsize = ConstantInt::get(M->getContext(), APInt(64, (uint64_t) (kernelCallsiteCounter - LTCKPT_SHUTTER_BOARD_IPC_END), false));
	gvar_kernelcall_swboard_size->setInitializer(const_kc_boardsize);

	// Enable all suicide points.
	DEBUG(errs() << "Initializing g_shutter_board.\n");
	GlobalVariable *gvar_shutter_board = M->getGlobalVariable((StringRef) LTCKPT_SHUTTER_BOARD_ARRAY);
	assert(gvar_shutter_board != NULL);

	// prepare for setting values in suicide switchboard

	StructType *structTy_windowShutter = M->getTypeByName(LTCKPT_STRUCT_SHUTTER);
	assert(structTy_windowShutter != NULL);
	StructType *structTy_wshutterProf = M->getTypeByName(LTCKPT_STRUCT_SHUTTER_PROF);
	assert(structTy_wshutterProf != NULL);
	
	DEBUG(errs() << "\t field values.\n");

	ConstantInt* const_suicide_var_1 = ConstantInt::get(M->getContext(), APInt(32, StringRef("1"), 10));
	ConstantInt* const_suicide_var_0 = ConstantInt::get(M->getContext(), APInt(32, StringRef("0"), 10));
	ConstantAggregateZero* const_zerostructProf = ConstantAggregateZero::get(structTy_wshutterProf);
	std::vector<Constant*> shutterFieldValues_0,  shutterFieldValues_1;
	
	shutterFieldValues_0.push_back(const_suicide_var_0);
	shutterFieldValues_0.push_back(const_zerostructProf);

	shutterFieldValues_1.push_back(const_suicide_var_1);
	shutterFieldValues_1.push_back(const_zerostructProf);

	Constant* const_shutterValue_0 = ConstantStruct::get(structTy_windowShutter, shutterFieldValues_0);
	Constant* const_shutterValue_1 = ConstantStruct::get(structTy_windowShutter, shutterFieldValues_1);

	std::vector<Constant*> const_array_elems;
	const_array_elems.clear();

	DEBUG(errs() << "\t ipc site - array elements.\n");
	unsigned boardSize = NumTargetIPCCallInsts; // targetCallInstsSiteMap->size();
	DEBUG(errs() << "boardsize (ipc part) : " << boardSize << "\n");
	for(unsigned i = 0; i < boardSize; i++)
	{
		if (NULL != suicideMap)
		{
			DEBUG(errs() << "suicideMap being used. \n");
			if ((0 != suicideMap->count(i)) && (0 == suicideMap->find(i)->second))
			{
				DEBUG(errs() << "\t found " << suicideMap->find(i)->second << "\n");
				const_array_elems.push_back(const_shutterValue_0);
			}
			else
			{
				DEBUG(errs() << "\t setting 1 \n");
				const_array_elems.push_back(const_shutterValue_1);
			}
		}
		else
		{
			DEBUG(errs() << "\t using default value: 1 \n");
			const_array_elems.push_back(const_shutterValue_1);
		}
	}

	// fill the in-between with zeros
	ConstantAggregateZero* const_zerostruct = ConstantAggregateZero::get(structTy_windowShutter);
	for (unsigned i = boardSize; i <= LTCKPT_SHUTTER_BOARD_IPC_END; i++)
	{
		const_array_elems.push_back(const_zerostruct);
	}

	// fill the kernelcall site suicide table.
	if (LTCKPT_SHUTTER_BOARD_IPC_END != kernelCallsiteCounter) // if kernelcallsites are present.
	{
		DEBUG(errs() << "\t kernel_call - array elements.\n");
		for(unsigned i = LTCKPT_SHUTTER_BOARD_IPC_END + 1; i <= kernelCallsiteCounter; i++)
		{
			if (0 != kernelcallSuicideMap->count(i))
			{
				if (false == kernelcallSuicideMap->find(i)->second)
				{
					DEBUG(errs() << "\t kernelcall suicide: 0\n");
					const_array_elems.push_back(const_shutterValue_0);
				}
				else
				{
					DEBUG(errs() << "\t kernelcall suicide: setting 1 \n");
					const_array_elems.push_back(const_shutterValue_1);
				}
			}
			else
			{
				DEBUG(errs() << "\t kernelcall suicide: using default value: 1 \n");
				const_array_elems.push_back(const_shutterValue_1);
			}
		}
	}

	
	// fill the rest with zero
	for (unsigned i = kernelCallsiteCounter + 1; i < LTCKPT_SHUTTER_BOARD_MAX_SIZE; i++)
	{
		const_array_elems.push_back(const_zerostruct);
	}
	ArrayType* ArrayTy_0 = ArrayType::get(structTy_windowShutter, LTCKPT_SHUTTER_BOARD_MAX_SIZE);
	Constant *const_array = ConstantArray::get(ArrayTy_0, const_array_elems);

	gvar_shutter_board->setInitializer(NULL);
	gvar_shutter_board->setInitializer(const_array);
	DEBUG(errs() << "Set initializer for gvar_shutter_board\n");


	DEBUG(errs() << "Setting gvar_module_name.\n");
	GlobalVariable *gvar_module_name = M->getGlobalVariable((StringRef) LTCKPT_CURRENT_MODULE_NAME);
	if (NULL == gvar_module_name)
	{
		DEBUG(errs() << "WARNING: Current module name variable couldn't be found.\n");
	}
	else
	{
		char char_module_name[LTCKPT_MODULE_NAME_SIZE];
		std::string::size_type s_len = targetModuleName.copy(char_module_name, LTCKPT_MODULE_NAME_SIZE, 0);
		for (int i = s_len; i < LTCKPT_MODULE_NAME_SIZE; i++)
		{
			char_module_name[i] = '\0';
		}
		Constant *const_array_2 = ConstantDataArray::getString(M->getContext(), StringRef(char_module_name, LTCKPT_MODULE_NAME_SIZE), false);
		gvar_module_name->setInitializer(const_array_2);
	}
 
	return true;
}

/* returns -1 on failure */
int StaticRecoveryPass::getIPCSiteID(CallInst *CI, uint64_t &site_id)
{
	// uint64_t site_id = 0;
	std::string site_module_name = "";
	std::string curr_module_name = M->getModuleIdentifier();
	IPC_SITE_ID *ipc_site_id = getFuseIPCSiteID(CI);
	if (NULL == ipc_site_id)
	{
		DEBUG(errs() << "\t!!ipc_site_id received is NULL.\n");
		return -1;
	}
	else
	{
		DEBUG(errs() << "current mod name: " << curr_module_name << "; metadata module name: " << ipc_site_id->module << "\n");
		// if (std::string::npos != curr_module_name.find(metadataModuleName))
		std::string extractedModuleName = compareModuleNames(curr_module_name, ipc_site_id->module);
		if("" != extractedModuleName)
		{
			DEBUG(errs() << "Found ipc_site_id which looks apt.\n");
			if ("" == targetModuleName)
			{
				targetModuleName = extractedModuleName;
			}
			site_id = ipc_site_id->id;
		}
	}
	return 0;
}

bool StaticRecoveryPass::loadStaticRecoveryDecisions()
{
	recoveryDecisions = new std::map<uint64_t, enum Decision>();
	if ("" == decisionMapFile)
	{
		errs() << "ERROR: decisionMapFile is empty.\n";
		return false;
	}

	DecisionMapInputLoader *decisionMapLoader = new DecisionMapInputLoader(M->getModuleIdentifier());
	DEBUG(errs() << "decisionMapFile: " << decisionMapFile << "\n");
	if (0 == decisionMapLoader->read(decisionMapFile))
	{
		DEBUG(errs() << "WARNING: Couldn't load decision map file: " << decisionMapFile << "\n");
		return false;
	}

	decisionMapLoader->getCallSiteDecisionMap(*recoveryDecisions);
	if (0 == recoveryDecisions->size())
	{
		errs() << "Info: No recovery decisions specified.\n";
	}

	if ("" == kernelcallExcludeFile)
	{
		errs() << "Info: No kernelcall exclusion set specified.\n";
		kernelcallExclusionSet = NULL;
	}
	else
	{
		DEBUG(errs() << "kernelcallExcludeFile is not null : " << kernelcallExcludeFile << "\n");
		kernelcallExclusionSet = new std::set<long>();
		KCExclusionMapInputLoader *exclusionLoader = new KCExclusionMapInputLoader();
		if (0 == exclusionLoader->read(kernelcallExcludeFile))
		{
			kernelcallExclusionSet = NULL;
		}
		else
		{
			exclusionLoader->getKernelCallExclusionSet(*kernelcallExclusionSet);
		}
	}

	// load kernel call decisions

	if ("" == kernelcallMapFile)
	{
		errs() << "Kernel call decision map file not found. Defaulting to DECISION_GLOBAL_CHANGE\n";
		kernelcallDecisions = NULL;
		return true; // Its okay as we use default value as fallback option.
	}

	kernelcallDecisions = new std::map<long, kernelcall_decision_t>();
	KCDecisionMapInputLoader *kernelcallDecisionsLoader = new KCDecisionMapInputLoader();
	if (0 == kernelcallDecisionsLoader->read(kernelcallMapFile))
	{
		errs() << "WARNING: Could not read Kernel call decision map file. Defaulting to DECISION_GLOBAL_CHANGE\n";
		kernelcallDecisions = NULL;
		return true; // Its okay as we use default value as fallback option.
	}
	kernelcallDecisionsLoader->getKernelCallDecisionMap(*kernelcallDecisions);
	if (0 == kernelcallDecisions->size())
	{
		errs() << "WARNING: Could not load kernel call decisions. Defaulting to DECISION_GLOBAL_CHANGE\n";
		kernelcallDecisions = NULL;
		return true;
	}

	if ("" == idempotentKernelCallersFile)
	{

	}

	idempotentKernelCallersSet = new std::set<std::string>();
	IdempKCCallersInputLoader *idempotentKernelCallersLoader = new IdempKCCallersInputLoader();
	if (0 == idempotentKernelCallersLoader->read(idempotentKernelCallersFile))
	{
		errs() << "WARNING: Could not read idempotent kernelcallers file.\n";
		idempotentKernelCallersSet = NULL;
		return true; // Its okay as we use default value as fallback option.
	}
	idempotentKernelCallersLoader->getIdempotentKernelCallers(*idempotentKernelCallersSet);
	if (0 == idempotentKernelCallersSet->size())
	{
		errs() << "WARNING: idempotentKernelCallersSet is empty.\n";
		idempotentKernelCallersSet = NULL;
		return true;
	}
	DEBUG(errs() << "Loaded idempotent kernel callers list\n");
	return true;
}

}

#endif
