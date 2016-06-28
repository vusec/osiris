/******************************
* Author : Koustubha Bhat
* Jul-Oct 2014
* Vrije Universiteit, Amsterdam.
*******************************/
#ifndef FUSE_MINIX_PREPARER_H
#define FUSE_MINIX_PREPARER_H

#include <common/dsa_common.h>
#include <common/output_common.h>
#include <common/pass_common.h>
#include <common/util/string.h>
#include <fuseminix/common.h>
#include <fuseminix/IPCSourceShaper.h>
#include <fuseminix/IPCSinkChipper.h>
#include <fuseminix/FuseMinixPass.h>
#include <fuseminix/FuseMinixHelper.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <pass.h>

namespace llvm {

DSA_UTIL_INIT_ONCE();

STATISTIC(NumSefReceiveFuncs,         "0 VANILLA: a. Num sef_receive_status() functions.");
STATISTIC(NumSefReceiveCallers,       "0 VANILLA: b. Num sef_receive_status() callers.");
STATISTIC(NumSefReceiveCallSites,     "0 VANILLA: c. Num sef_receive_status() callsites.");
STATISTIC(NumGetWorkFuncs,                      "1 PREPARE: a. Num get_work() functions that are callers of sef_receive_status().");
STATISTIC(NumSefRecvCallersToBeInlined,         "1 PREPARE: b. To be inlined - sef_receive_status() callers [ like get_work() ].");
STATISTIC(NumSefRecvCallersInlined,             "1 PREPARE: c. Inlined - sef_receive_status() callers [ like get_work() ].");
STATISTIC(NumSefReceiveCallSitesAfterInlining,  "1 PREPARE: d. Num sef_receive_status() callsites after inlining.");
STATISTIC(NumSefReceiveCallersToBeCloned,       "1 PREPARE: e. Num sef_receive_status() callers to be cloned.");
STATISTIC(NumSefReceiveCallerClones,            "1 PREPARE: f. Num clones of sef_receive_status() callers created.");
STATISTIC(NumMainLoopSefReceiveCallerClones,    "1 PREPARE: f. a. Num sefrecv caller clones that are of main loop type.");
STATISTIC(NumMainLoopsPruned,                   "1 PREPARE: f. b. Num main() loops of sef_receive_status() callers pruned.");
STATISTIC(NumSefRecvCallerClonesNotBeingPruned, "1 PREPARE: g. Num sef_receive_status() caller clones NOT being pruned.");

enum SefReceiveCallerType
{
   MX_SRV_MAIN_LOOP,
   MX_SRV_GET_WORK,
   MX_SRV_CATCH_BOOT_INIT,
   MX_SRV_CB_INIT_FRESH,
   MX_DRV_TICKDELAY,
   MX_DRV_RECEIVE,
   MX_DRV_CHAR_TASK,
   MX_UNKNOWN
};

class FuseMinixPreparer
{
	public:
		FuseMinixPreparer(FuseMinixPass *FMP);
    FuseMinixPreparer(FuseMinixPass *FMP, std::string endpointMapFileName);
	 	bool prepare(Module &M);
    std::map<CallSite*, IPCInfo*>* getSendrecCallSitesInfoMap();
    int getPotentialSendrecDestinations(IPCInfo *info, std::vector<Function*> &destFunctions);

    int getPotentialSendrecDestinations(std::vector<Function*> &destinations);
	 	int getPotentialSendrecDestinations(std::string &modulePrefix, std::vector<Function*> &destinations);
	 	int getPotentialSendrecDestinations(IPCEndpoint endpoint, std::vector<Function*> &destinations);
    void displayOnProbe(Probe probe);

	private:
	   Module            *M;
	   FuseMinixPass     *FMP;
	   DSAUtil           *dsau;
     IPCSourceShaper   *ipcSendrecShaper;
	   std::vector<Regex*>       sefreceiveRegexVec;
	   std::vector<Regex*>       getWorkRegexVec;
	   std::vector<Function*>    sefreceiveFunctions;
	   std::vector<Function*>    sefreceiveCallers;
	   std::vector<Function*>    sefreceiveCallerClones;
     std::vector<Function*>    functionsWithNoCallSites;
	   std::map<Function*, bool> functionsInlined;
	   std::map<Function*, std::vector<Loop*> >                      loopsOfSefRecvCallers;
	   std::map<enum SefReceiveCallerType, std::vector<Function*> >  sefreceiveCallerClonesMap;
	   std::map<std::string, std::vector<Function*> >                moduleWiseClonesMap;
     std::map<CallSite*, IPCInfo*>                                 *sendrecCallSitesInfoMap;

     void getVanillaStats(bool addToStore);
     bool initDSAUtil();
	   bool inlineSefRecvCallers(std::vector<Function*> functions);
     unsigned cloneFunctions(std::vector<Function*> origFunctions, std::string cloneFunctionPrefix, std::vector<Function*> &clonedFunctions);
	   enum SefReceiveCallerType getSefReceiveCallerType(Function *function);
	   void addToClonesMap(enum SefReceiveCallerType cloneType, Function *func);
	   void addToClonesMap(enum SefReceiveCallerType cloneType, Function *func, std::map<enum SefReceiveCallerType, std::vector<Function*> > &clonesMap);
	   void addToClonesMap(std::string modulePrefix, Function *func, std::map<std::string, std::vector<Function*> > &clonesMap);
	   bool pruneCloneFunction(enum SefReceiveCallerType cloneType, Function *func);
	   bool pruneMainLoop(Function *func);
	   int getLoops(Function *targetFunction, std::vector<Loop*> &loops);
	   Loop* getSefReceiveLoop(Function* func, std::vector<Loop*> *loops);
	   int doesTheLoopDependsOn(Loop *sefreceiveLoop, Instruction* instr);
	   bool retainOnlyLoop(Function *func, Loop *theLoop);
	   void addToBin(std::map<Instruction*, bool> *bin, Instruction* instr);
     bool noopSefFunctions();
	   void printCallers(const std::string targetFuncPattern, const std::vector<Function*> callers);
	   void printClonesMap();
	   void printClonesMap(std::map<std::string, std::vector<Function*> > &clonesMap);
};

FuseMinixPreparer::FuseMinixPreparer(FuseMinixPass *FMP)
{
  std::string sefreceiveString = (std::string)SEFRECEIVE_FUNC_NAME;
  std::string getWorkString = (std::string)GET_WORK_FUNC_NAME;
  PassUtil::parseRegexListOpt(sefreceiveRegexVec, sefreceiveString);
  PassUtil::parseRegexListOpt(getWorkRegexVec, getWorkString);
  this->FMP = FMP;
  dsau = NULL;
  ipcSendrecShaper = NULL;
}

FuseMinixPreparer::FuseMinixPreparer(FuseMinixPass *FMP, std::string endpointMapFileName)
{
  std::string sefreceiveString = (std::string)SEFRECEIVE_FUNC_NAME;
  std::string getWorkString = (std::string)GET_WORK_FUNC_NAME;
  PassUtil::parseRegexListOpt(sefreceiveRegexVec, sefreceiveString);
  PassUtil::parseRegexListOpt(getWorkRegexVec, getWorkString);
  this->FMP = FMP;
  dsau = NULL;
  ipcSendrecShaper =  NULL;
  loadEndpointMapping(endpointMapFileName);
}

bool FuseMinixPreparer::prepare(Module &M)
{
  this->M = &M;
  getVanillaStats(true);

  bool returnValue =  false;

  // PASS 1: Inline get_work() functions
  std::vector<Function*> getworkFunctions;
  Module::FunctionListType &functionList = M.getFunctionList();

  for (Module::iterator it = functionList.begin(); it != functionList.end(); ++it)
  {
    Function *F = it;

    if (PassUtil::matchRegexes(F->getName(), getWorkRegexVec))
    {
      getworkFunctions.push_back(F);
      ++NumGetWorkFuncs;
    }
  }
  if (getworkFunctions.size() != 0)
  {
    if (false == inlineSefRecvCallers(getworkFunctions))
    {
      DEBUG(errs() << "Warning: There were failures during inlining get_work() functions. \n");
    }
  }
  else
  {
    DEBUG(errs() << "No get_work() functions found!\n");
  }

  // PASS 2: Collect the callers of sef_receive_status() afresh

  if (false == initDSAUtil())
  {
    DEBUG(errs() << "Error: Failed initializing DSAUtil.\n");
    return false;
  }
  sefreceiveCallers.clear();
  for (Module::iterator it = functionList.begin(); it != functionList.end(); ++it)
  {
    Function *F = it;
    if (F->isIntrinsic())
            continue;
    if (PassUtil::matchRegexes(F->getName(), sefreceiveRegexVec))
    {
       // Collect the callers of sef_receive_status()
       int numCallers = 0;
       if ( 0 > (numCallers = getCallers(dsau, F, sefreceiveCallers)))
       {
        DEBUG(errs() << "Error getting callers of " << F->getName() << "\n");
        return false;
       }
       std::vector<CallSite> callSites;
       if (true == dsau->getCallSites(F, callSites))
       {
         NumSefReceiveCallSitesAfterInlining += callSites.size();
         callSites.clear();
       }
       DEBUG(errs() << SEFRECEIVE_FUNC_NAME << ": Num callers of " << F->getName() << "\t: " << numCallers << "\n");
    }
  }
  DEBUG(errs() << "Callers of " << (std::string)SEFRECEIVE_FUNC_NAME << " : Total: " << sefreceiveCallers.size() << " callers.\n");

  // NEXT STEP:
  // Create clones of all the callers of sef_receive_status()
  DEBUG(errs() << "Cloning callers of " << (std::string)SEFRECEIVE_FUNC_NAME << "\n");

  // Skip cloning inlined functions
  std::vector<Function*> functionsToClone;
  for (unsigned i = 0; i < sefreceiveCallers.size(); i++)
  {
    if (0 == functionsInlined.count(sefreceiveCallers[i]))
    {
      DEBUG(errs() << "Functions to be cloned: " << sefreceiveCallers[i]->getName() << "\n");
      functionsToClone.push_back(sefreceiveCallers[i]);
      ++NumSefReceiveCallersToBeCloned;
    }
  }

  NumSefReceiveCallerClones = cloneFunctions(functionsToClone, CLONE_FUNC_NAME_PREFIX, sefreceiveCallerClones);
  if ( 0 < NumSefReceiveCallerClones)
  {
    returnValue = true;
  }
  DEBUG(errs() << "Num of sef_receive_status() caller clone functions created:" << sefreceiveCallerClones.size() << "\n");

  // NEXT STEP:
  // Categorize the caller clones.

  std::vector<Function*> *targets = &sefreceiveCallerClones;
  enum SefReceiveCallerType cloneType;
  for (unsigned i=0; i < targets->size(); i++)
  {
    cloneType = getSefReceiveCallerType((*targets)[i]);
    if (MX_SRV_MAIN_LOOP == cloneType)
    {
      ++NumMainLoopSefReceiveCallerClones;
    }
    DEBUG(errs() << (*targets)[i]->getName() << " : type " << cloneType << "\n");
    addToClonesMap(cloneType, (*targets)[i]);
  }
  // Print the sefreceiveCallerClonesMap
  printClonesMap();

  // NEXT STEP : Pruning the clones!
  // According to the type of the caller clone,
  // process the clone functions to remove unwanted parts.

  for (std::map<enum SefReceiveCallerType, std::vector<Function*> >::iterator it = sefreceiveCallerClonesMap.begin(); it != sefreceiveCallerClonesMap.end(); ++it)
  {
    for(unsigned i=0; i < it->second.size(); i++)
    {
        pruneCloneFunction(it->first, it->second[i]);
    }
  }

  // Noop all the sef related functions. We dont need them for analysis.
  if (false == noopSefFunctions())
  {
    DEBUG(errs() << "WARNING: nooping sef functions failed.\n");
  }

  // ipc_sendrec() caller analysis and prep-work.
  ipcSendrecShaper = new IPCSourceShaper(this->M, this->FMP);
  if (false == ipcSendrecShaper->handleSendrecCallers())
  {
    DEBUG(errs() << "IPC source analysis failed.\n");
    return true;
  }
  if (NULL == (sendrecCallSitesInfoMap = ipcSendrecShaper->getAnalysisResult()))
  {
    DEBUG(errs() << "IPC source analysis failed.\n");
    return true;
  }

  ipcSendrecShaper->noopOtherIpcSends();

  DEBUG(errs() << "Prepare complete.\n");
  return returnValue;
}

std::map<CallSite*, IPCInfo*>* FuseMinixPreparer::getSendrecCallSitesInfoMap()
{
  if (NULL == ipcSendrecShaper)
  {
    return NULL;
  }
  return ipcSendrecShaper->getAnalysisResult();
}

/*
* Gets all the sef_receive_status() callers as potential destinations for sendrec() call.
*/
int FuseMinixPreparer::getPotentialSendrecDestinations(std::vector<Function*> &destinations)
{
  DEBUG(errs() << "Policy: Plain and simple, Get the sample set!\n");
  destinations = sefreceiveCallerClones;
  return destinations.size();
}

/*
* Gets all the sef_receive_status() callers of the specified module, as potential destinations for sendrec() calls.
*/
int FuseMinixPreparer::getPotentialSendrecDestinations(std::string &modulePrefix, std::vector<Function*> &destinations)
{
  DEBUG(errs() << "Policy : Destination module wise.\n");
	if (0 == moduleWiseClonesMap.size())
	{
		// Initialize
		for (unsigned i=0 ; i < sefreceiveCallerClones.size(); i++)
		{
			addToClonesMap(getMinixModulePrefix(sefreceiveCallerClones[i]), sefreceiveCallerClones[i], moduleWiseClonesMap);
		}
	}
	if (0 == moduleWiseClonesMap.size())
	{
		return -1;
	}
	if (0 == moduleWiseClonesMap.count(modulePrefix))
	{
		return 0;
	}
	destinations = moduleWiseClonesMap.find(modulePrefix)->second;
	DEBUG(errs() << "Num of destinations for " << modulePrefix << " : " << destinations.size() << "\n");
	return destinations.size();
}

/*
* Based on the endpoint specified, filters the sef_receive_status() callers which could be potential destinations for sendrec() calls.
*/
int FuseMinixPreparer::getPotentialSendrecDestinations(IPCEndpoint endpoint, std::vector<Function*> &destinations)
{
  DEBUG(errs() << "Policy : Based on destination endpoint.\n");
  std::string modulePrefix;
  if (0 == endpointModuleMap.size())
  {
    DEBUG(errs() << "endpointModuleMap size is zero! " << "\n");
    return -1;
  }
  if (false == getEndpointMapping(endpoint, modulePrefix))
  {
    DEBUG(errs() << "getEndpointMapping() failed." << "\n");
    return -1;
  }
  return getPotentialSendrecDestinations(modulePrefix, destinations);
}

int FuseMinixPreparer::getPotentialSendrecDestinations(IPCInfo *ipcInfo, std::vector<Function*> &destinations)
{
  DEBUG(errs() << "Policy : Based on IPCInfo.\n");
  int returnValue = 0;
  std::vector<Function*> candidateFunctions;
  for(unsigned i = 0; i < ipcInfo->destEndpoints.size(); i++)
  {
    returnValue = getPotentialSendrecDestinations(ipcInfo->destEndpoints[i], candidateFunctions);
    if (0 > returnValue)
    {
      return returnValue;
    }
  }

  std::string modulePrefix = "_multipledest_";
  if ((NULL != ipcInfo) && (1 == ipcInfo->destEndpoints.size()))
  {
    getEndpointMapping(ipcInfo->destEndpoints[0], modulePrefix);
  }
  std::string clonePrefix = modulePrefix + (std::string)"_ipcsink_" + OutputUtil::intToStr(ipcInfo->srcEndpoint)
                            + (std::string)"_" + OutputUtil::intToStr(ipcInfo->mtype);
  if (candidateFunctions.size() !=  cloneFunctions(candidateFunctions, clonePrefix, destinations))
  {
    return -1;
  }

  returnValue = 0;
  for(unsigned i = 0; i < destinations.size(); i++)
  {
    std::string destFuncName = destinations[i]->getName();
    DEBUG(errs() << "Shaping " << destFuncName << " endpt: " << ipcInfo->srcEndpoint
                                                             << " mtype: " << ipcInfo->mtype << "\n");
    IPCSinkChipper *ipcSefReceiveShaper = NULL;
    if ((std::string::npos != destFuncName.find("sched_main")))
    {
       ipcSefReceiveShaper = new SchedMainChipper();
    }
    else if ((std::string::npos != destFuncName.find("rs_main")))
    {
      ipcSefReceiveShaper = new RsMainChipper();
    }
    else if ((std::string::npos != destFuncName.find("rs_catch_boot_init_ready")))
    {
      ipcSefReceiveShaper = new RsCatchBootInitReadyChipper();
    }
    else if ((std::string::npos != destFuncName.find("ds_main")))
    {
      ipcSefReceiveShaper = new DsMainChipper();
    }
    else if ((std::string::npos != destFuncName.find("vfs_sef_cb_init_fresh")))
    {
      ipcSefReceiveShaper = new MxVfsSefCbInitFreshChipper();
    }
    else if ((std::string::npos != destFuncName.find("is_main")))
    {
      ipcSefReceiveShaper = new MxIsMainChipper();
    }
    else if ((std::string::npos != destFuncName.find("ipc_main")))
    {
      ipcSefReceiveShaper = new MxIpcMainChipper();
    }
    else if ((std::string::npos != destFuncName.find("pm_main")))
    {
      ipcSefReceiveShaper = new MxPmMainChipper();
    }
    else if ((std::string::npos != destFuncName.find("vm_main")))
    {
      ipcSefReceiveShaper = new MxVmMainChipper();
    }
    else
    {
      DEBUG(errs() << "Suitable chipper not available!" << "\n" );
      continue;
    }
    if (false == ipcSefReceiveShaper->chip(destinations[i], ipcInfo->mtype, ipcInfo->srcEndpoint))
    {
      DEBUG(errs() << "WARNING: Failure in shaping IPC sink function: " << destinations[i]->getName() << "\n");
    }
    else
    {
      returnValue++;
    }
  }

  return returnValue;
}


/***************************************
 	Private methods
***************************************/

bool FuseMinixPreparer::initDSAUtil()
{
  if(NULL == this->FMP || NULL == this->M)
  {
    return false;
  }
  this->dsau = new DSAUtil();
  this->dsau->init((ModulePass *)this->FMP, this->M);
  return true;
}

void FuseMinixPreparer::getVanillaStats(bool addToStore)
{
  int numCallers = 0;
  std::vector<CallSite> callSites;
  std::vector<Function*> callers;
  static bool onceExecuted = false;

  if (onceExecuted)
  {
    return;
  }
  if (false == initDSAUtil())
  {
    DEBUG(errs() << "Error: Failed to initialize DSAUtil. getVanillaStats() failed.\n");
    return;
  }

  Module::FunctionListType &functionList = this->M->getFunctionList();
  for (Module::iterator it = functionList.begin(); it != functionList.end(); ++it)
  {
    Function *F = it;
    if (F->isIntrinsic())
            continue;
    if (PassUtil::matchRegexes(F->getName(), sefreceiveRegexVec))
    {
      ++NumSefReceiveFuncs;
      if (addToStore)
        sefreceiveFunctions.push_back(F);

      numCallers = 0;
      callSites.clear();
      if ( 0 < (numCallers = getCallers(dsau, F, sefreceiveCallers)))
      {
        NumSefReceiveCallers += numCallers;
      }
      if (true == dsau->getCallSites(F, callSites))
      {
        NumSefReceiveCallSites += callSites.size();
      }
    }
  }
  onceExecuted = true;
  return;
}

bool FuseMinixPreparer::inlineSefRecvCallers(std::vector<Function*> functions)
{
  if (0 == functions.size())
  {
    return false;
  }

  initDSAUtil();

  std::vector<CallSite> theCallSites;
  theCallSites.clear();
  for (unsigned i=0; i < functions.size(); i++)
  {
    DEBUG(errs() << "Preparing to inline the function: " << functions[i]->getName() << "\n");
    functionsInlined.insert(std::pair<Function*, bool>(functions[i], true)); // mark so that we dont clone them.

    std::vector<CallSite> functionCallSites;
    functionCallSites.clear();
    if (false == dsau->getCallSites(functions[i], functionCallSites))
    {
      DEBUG(errs() << "Failed getting callsites for function: " << functions[i]->getName() << "\n");
      functionsWithNoCallSites.push_back(functions[i]);
      continue;
    }
    for (unsigned j=0; j < functionCallSites.size(); j++)
    {
      Function *F = functionCallSites[j].getCaller();
      while(std::string::npos == F->getName().find("main"))
      {
        functionsInlined.insert(std::pair<Function*, bool>(F, true)); // mark so that we dont clone them.

        DEBUG(errs() << "Adding callsites of function: " << F->getName() << "\n");
        std::vector<CallSite> innerFunctionCallsites;
        dsau->getCallSites(F, innerFunctionCallsites);

        if (innerFunctionCallsites.size() > 0)
        {
          for (unsigned k=0; k < innerFunctionCallsites.size(); k++)
          {
            functionCallSites.push_back(innerFunctionCallsites[k]);
          }
          F = innerFunctionCallsites[0].getCaller();
        }
      }
    }
    for (unsigned i = 0; i < functionCallSites.size(); i++)
    {
      theCallSites.push_back(functionCallSites[i]);
    }
  }

  DEBUG(errs() << "Total sef_receive_status() related callsites to be inlined: " << theCallSites.size() << "\n");
  NumSefRecvCallersToBeInlined = theCallSites.size();

  // inline the call instructions at all callsites.
  int failCount = 0;
  for (unsigned j=0; j < theCallSites.size(); j++)
  {
    InlineFunctionInfo IFI;
    CallInst *callInst;
    Instruction *I = theCallSites[j].getInstruction();
    callInst = dyn_cast<CallInst>(I);
    if (false == llvm::InlineFunction(callInst, IFI))
    {
      DEBUG(errs() << "Failed inlining function call : " << callInst->getName() << "\n");
      failCount++;
    }
    else
    {
      ++NumSefRecvCallersInlined;
    }
  }
  if (0 < failCount)
  {
    DEBUG(errs() << "Number of failed inlining operations of sef_receive_status() related callers : " << failCount << "\n");
    return false;
  }
  return true;
}

unsigned FuseMinixPreparer::cloneFunctions(std::vector<Function*> origFunctions,
                                      std::string cloneFunctionPrefix,
                                      std::vector<Function*> &clonedFunctions)
{
  unsigned count = 0;
  Module *M = NULL;
  Function *clonedFunction = NULL;
  // VALUE_TO_VALUE_MAP_TY VMap;

  for(unsigned i=0; i < origFunctions.size(); i++)
  {
    if (std::string::npos != origFunctions[i]->getName().find(GET_WORK_FUNC_NAME))
    {
      DEBUG(errs() << "Skipping to clone: " << origFunctions[i]->getName() << "\n");
      continue;
    }

    DEBUG(errs() << "Cloning the function: " << origFunctions[i]->getName() << "\n");

    M = origFunctions[i]->getParent();

    std::vector<Type*> argTypes;
    for (Function::const_arg_iterator I = origFunctions[i]->arg_begin(), E = origFunctions[i]->arg_end(); I != E; ++I)
    {
      argTypes.push_back(I->getType());
    }
    FunctionType *FTy = FunctionType::get(origFunctions[i]->getFunctionType()->getReturnType(), argTypes,
                                          origFunctions[i]->getFunctionType()->isVarArg());
    clonedFunction = Function::Create(FTy, origFunctions[i]->getLinkage(), cloneFunctionPrefix + origFunctions[i]->getName(), M);
    if (NULL == clonedFunction)
    {
      DEBUG(errs() << "Function::Create failed.\n");
      return -1;
    }

    // Loop over the arguments, copying the names of the mapped arguments over...
    VALUE_TO_VALUE_MAP_TY VMap;
    Function::arg_iterator DestI = clonedFunction->arg_begin();
    for (Function::const_arg_iterator I = origFunctions[i]->arg_begin(), E = origFunctions[i]->arg_end(); I != E; ++I)
    {
      DestI->setName(I->getName());
      VMap[I] = DestI++;
    }

    SmallVector<ReturnInst*, 8> Returns; // Ignore returns cloned...

    CloneFunctionInto(clonedFunction, origFunctions[i], VMap, false, Returns, "", NULL);
    if (NULL != clonedFunction)
    {
      clonedFunctions.push_back(clonedFunction);
      count++;
    }
  }

  DEBUG(errs() << "Cloned " << count << " functions.\n");
  return count;
}

/// Looks for loops in the body of the specified function and adds the loop to
/// the loops vector specified.
/// Returns the number of loops found.
int FuseMinixPreparer::getLoops(Function *targetFunction, std::vector<Loop*> &loops)
{
  if (NULL == targetFunction)
  {
    DEBUG(errs() << "targetFunction is NULL.");
    return -1;
  }

#if LLVM_VERSION >= 37
  LoopInfo &LI = FMP->getAnalysis<LoopInfoWrapperPass>(*targetFunction).getLoopInfo();
#else
  LoopInfo &LI = FMP->getAnalysis<LoopInfo>(*targetFunction);
#endif

  unsigned loopCount = 0;
  Loop *loop = NULL;
  std::map<Loop*, bool> loopsMap;
  for (Function::iterator BB = targetFunction->begin(), BE = targetFunction->end(); BB != BE; ++BB)
  {
    loop = LI.getLoopFor(BB);
    if (NULL != loop)
    {
      if ( 0 == loopsMap.count(loop))
      {
        loopsMap.insert(std::pair<Loop*, bool>(loop, true));
        loops.push_back(loop);
        loopCount++;
      }
    }
  }

  DEBUG(errs() << "Num loops found for " << targetFunction->getName() << " : " << loops.size() << "\n");
  return loopCount;
}

enum SefReceiveCallerType FuseMinixPreparer::getSefReceiveCallerType(Function *function)
{
  Regex *mainLoopRegex = new Regex(".*_main");
  Regex *getWorkRegex = new Regex(".*_get_work");
  Regex *drvTickdelayRegex = new Regex(".*_tickdelay");
  Regex *drvReceiveRegex = new Regex(".*_bdev_receive");
  Regex *drvChTaskRegex = new Regex(".*_chardriver_task");
  Regex *bootInitRegex = new Regex(".*_catch_boot_init_ready");
  Regex *sefCbInitFreshRegex = new Regex(".*_sef_cb_init_fresh");

  std::string functionName = function->getName();

  	if (mainLoopRegex->match(functionName, NULL))
    {
      return MX_SRV_MAIN_LOOP;
    }
    else if (getWorkRegex->match(functionName, NULL))
    {
      return MX_SRV_GET_WORK;
    }
    else if (drvTickdelayRegex->match(functionName, NULL))
    {
      return MX_DRV_TICKDELAY;
    }
    else if (bootInitRegex->match(functionName, NULL))
    {
      return MX_SRV_CATCH_BOOT_INIT;
    }
    else if (drvReceiveRegex->match(functionName, NULL))
    {
      return MX_DRV_RECEIVE;
    }
    else if (drvChTaskRegex->match(functionName, NULL))
    {
      return MX_DRV_CHAR_TASK;
    }
    else if (sefCbInitFreshRegex->match(functionName, NULL))
    {
      return MX_SRV_CB_INIT_FRESH;
    }

  return MX_UNKNOWN;
}

void FuseMinixPreparer::addToClonesMap(enum SefReceiveCallerType cloneType, Function *func)
{
  std::vector<Function*> *clonesVector;

  if (sefreceiveCallerClonesMap.count(cloneType) >= 1)
  {
    clonesVector = &sefreceiveCallerClonesMap.find(cloneType)->second;
    clonesVector->push_back(func);
  }
  else
  {
    std::vector<Function*> *newClonesVector = new std::vector<Function*>();
    newClonesVector->push_back(func);
    sefreceiveCallerClonesMap.insert(std::pair<enum SefReceiveCallerType, std::vector<Function*> >(cloneType, *newClonesVector));
  }
  return;
}

void FuseMinixPreparer::addToClonesMap(enum SefReceiveCallerType cloneType, Function *func, std::map<enum SefReceiveCallerType, std::vector<Function*> > &clonesMap)
{
  std::vector<Function*> *clonesVector;

  if (clonesMap.count(cloneType) >= 1)
  {
    clonesVector = &clonesMap.find(cloneType)->second;
    clonesVector->push_back(func);
  }
  else
  {
    std::vector<Function*> *newClonesVector = new std::vector<Function*>();
    newClonesVector->push_back(func);
    clonesMap.insert(std::pair<enum SefReceiveCallerType, std::vector<Function*> >(cloneType, *newClonesVector));
  }
  return;
}

void FuseMinixPreparer::addToClonesMap(std::string modulePrefix, Function *func, std::map<std::string, std::vector<Function*> > &clonesMap)
{
  std::vector<Function*> *clonesVector;

  if (clonesMap.count(modulePrefix) >= 1)
  {
    clonesVector = &clonesMap.find(modulePrefix)->second;
    clonesVector->push_back(func);
  }
  else
  {
    std::vector<Function*> *newClonesVector = new std::vector<Function*>();
    newClonesVector->push_back(func);
    clonesMap.insert(std::pair<std::string, std::vector<Function*> >(modulePrefix, *newClonesVector));
  }
  return;
}

bool FuseMinixPreparer::pruneCloneFunction(enum SefReceiveCallerType cloneType, Function *func)
{
  switch (cloneType)
  {
    case MX_SRV_MAIN_LOOP:
    {
        DEBUG(errs() << "MX_SRV_MAIN_LOOP processing for " << func->getName() << " : " << cloneType << "\n");
        if (true == pruneMainLoop(func))
        {
          ++NumMainLoopsPruned;
        }

        break;
    }
    case MX_SRV_GET_WORK:
    {
        DEBUG(errs() << "MX_SRV_GET_WORK processing for " << func->getName() << " : " << cloneType << "\n");
        ++NumSefRecvCallerClonesNotBeingPruned;
        break;
    }
    default :
    {
        DEBUG(errs() << "Processing NOT DEFINED yet for " << func->getName() << " : " << cloneType << "\n");
        ++NumSefRecvCallerClonesNotBeingPruned;
        break;
    }
  }
  return true;
}

bool FuseMinixPreparer::pruneMainLoop(Function* func)
{
  // get hold of the target loop.
  // aim: remove everything else other than the loop
  //    - find if there are any instr/values used by the loop that are outside the loop --> retain them
  //    - remove everything else from this function

  Loop* sefreceiveLoop = NULL;
  std::vector<Loop*> *loopsVector = new std::vector<Loop*>();
  std::map<Instruction*, bool> retainInstrsMap;
  std::map<Instruction*, bool> pruneInstrsMap;

  getLoops(func, *loopsVector);
  sefreceiveLoop = getSefReceiveLoop(func, loopsVector);
  if (NULL == sefreceiveLoop)
  {
    DEBUG(errs() << "Error :" << "target loop cannot be fetched.\n");
    return false;
  }

  if (false == retainOnlyLoop(func, sefreceiveLoop))
  {
    return false;
  }

  return true;
}

Loop* FuseMinixPreparer::getSefReceiveLoop(Function* func, std::vector<Loop*> *loops)
{
  //Get the module name from the function name
  std::string module_name;
  module_name = getMinixModulePrefix(func);

  Function* sefreceivestatusFunction = NULL;
  for (unsigned i=0; i < sefreceiveFunctions.size(); i++)
  {
    Function *F = sefreceiveFunctions[i];
    if (std::string::npos != F->getName().find(module_name, 0))
    {
      sefreceivestatusFunction = F;
      DEBUG(errs() << "sefreceivestatusFunction: " << F->getName() << "\n");
      break;
    }
  }

  if (NULL == sefreceivestatusFunction)
  {
    DEBUG(errs() << "Error: Couldnt locate the sef_receive_status() function of module: " << module_name << "\n");
    return NULL;
  }

  // get the callsites of this function
  dsau = new DSAUtil();
  dsau->init(this->FMP, func->getParent());

  std::vector<CallSite> sefreceiveCallSites;
  if ( false == dsau->getCallSites(sefreceivestatusFunction, sefreceiveCallSites))
  {
    DEBUG(errs() << "Error: Couldnt find callsites of sef_receive_status() function in the module:" << module_name << "\n");
    return NULL;
  }

  Loop *sefreceiveLoop = NULL;
  for (unsigned i = 0; i < loops->size(); i++)
  {
    Loop *loop = (*loops)[i];

    for (unsigned j = 0; j < sefreceiveCallSites.size(); j++)
    {
      Instruction *instr = sefreceiveCallSites[j].getInstruction();
      if (loop->contains(instr))
      {
        DEBUG(errs() << "Found the sef_receive_status target loop.\n");
        sefreceiveLoop = loop;
        break;
      }
    }
  }

  return sefreceiveLoop;
}

bool FuseMinixPreparer::retainOnlyLoop(Function *func, Loop *theLoop)
{
  std::map<Instruction*, bool> pruneInstrsMap;
  std::map<Instruction*, bool> retainInstrsMap;

  // Pass: for every instruction in the function, mark whether to prune or not.
  int bbCount = 0;
  for(Function::iterator BB = func->begin(), EB = func->end(); BB != EB; BB++)
  {
    bbCount++;

    int instrCount = 0;
    for (BasicBlock::iterator IB = BB->begin(), EB = BB->end(); IB != EB; IB++)
    {
      Instruction *currInstr = dyn_cast<Instruction>(IB);
      if (theLoop->contains(currInstr) )
      {
        // Belongs to loop. Retain
        addToBin(&retainInstrsMap, currInstr);
        continue;
      }
      if (0 == doesTheLoopDependsOn(theLoop, currInstr))
      {
        // Good to prune
        addToBin(&pruneInstrsMap, currInstr);
      }

      instrCount++;
    }
  }

  // Remove all instrs that are collected in the pruneInstrsMap.
  unsigned removedCount = 0;
  for(std::map<Instruction*, bool>::iterator IP = pruneInstrsMap.begin(), EP = pruneInstrsMap.end(); IP != EP; IP++)
  {
    IP->first->dropAllReferences();
    removedCount++;
  }
  removedCount=0;
  for(std::map<Instruction*, bool>::iterator IP = pruneInstrsMap.begin(), EP = pruneInstrsMap.end(); IP != EP; IP++)
  {
    IP->first->eraseFromParent();
    removedCount++;
  }

  DEBUG(errs() << "Totally removed " << removedCount << " instructions.\n");
  return true;
}

void FuseMinixPreparer::addToBin(std::map<Instruction*, bool> *bin, Instruction* instr)
{
  if (NULL == bin || NULL == instr)
  {
    DEBUG(errs() << "Error: addToBin(): Arguments are null.\n");
    return;
  }
  if (bin->count(instr) != 0)
  {
    return;
  }
  bin->insert(std::pair<Instruction*, bool>(instr, true));
  return;
}

int FuseMinixPreparer::doesTheLoopDependsOn(Loop *sefreceiveLoop, Instruction* instr)
{

  if (NULL == sefreceiveLoop || NULL == instr)
  {
    DEBUG(errs() << "Error: doesTheLoopDependsOn() : Arguments NULL.\n");
    return -1;
  }

  if (dyn_cast<TerminatorInst>(instr))
  {
    // DEBUG(errs() << "\t\t" << "Its a terminating instr. Retain!\n");
    return 1;
  }

  // Check the users of the instr.
#if LLVM_VERSION >= 37
  std::vector<User*> users(instr->user_begin(), instr->user_end());
#else
  std::vector<User*> users(instr->use_begin(), instr->use_end());
#endif
  if (users.size() <= 0)
  {
    // No users, so good to prune away!
    // DEBUG(errs() << "\t\t" << "No users. So can prune!\n");
    return 0;
  }

  // DEBUG(errs() << "\t\t" << "Num. users: " << users.size() << "\n");
  // Check whether the loop depends on any of the users.
  bool safeToRemove = true;
  for (unsigned i=0 ; i < users.size(); i++)
  {
    if (Instruction *I = dyn_cast<Instruction>(users[i]))
    {
        // If the user is in the loop
        if (sefreceiveLoop->contains(I) )
        {
          // DEBUG(errs() << "\t\t" << "User belongs to loop. Retain!\n");
          safeToRemove = false;
        }
        // the user is a terminating instr...
        else if (dyn_cast<TerminatorInst>(I))
        {
          // DEBUG(errs() << "\t\t" << "User is a terminating instr. Retain!\n");
          safeToRemove = false;
        }
        else
        {
          // DEBUG(errs() << "\t\t" << "User NOT in our loop\n");
          if (0 < doesTheLoopDependsOn(sefreceiveLoop, I))
          {
            safeToRemove = false;
          }
        }
    }
  }

  if (safeToRemove)
  {
    return 0;
  }

  return 1;
}

void FuseMinixPreparer::printCallers(const std::string targetFuncPattern, const std::vector<Function*> callers)
{
  if ("" == targetFuncPattern || callers.empty())
  {
    DEBUG(errs() << "Argument(s) are empty." << "\n");
    return;
  }

  DEBUG(errs() << "Printing callers of " << targetFuncPattern << ":\n");

  for(unsigned i=0; i < callers.size(); ++i)
  {
    Function *F = callers[i];
    DEBUG(errs() << targetFuncPattern << " : " << F->getName() << "\n");
  }
  return;
}

void FuseMinixPreparer::printClonesMap()
{
  for (std::map<enum SefReceiveCallerType, std::vector<Function*> >::iterator it = sefreceiveCallerClonesMap.begin(); it != sefreceiveCallerClonesMap.end(); ++it)
  {
    for (unsigned i=0; i < it->second.size(); i++)
    {
      DEBUG(errs() << it->first << " : " << it->second[i]->getName() << "\n");
    }
  }
  return;
}

void FuseMinixPreparer::printClonesMap(std::map<std::string, std::vector<Function*> > &clonesMap)
{
	for (std::map<std::string, std::vector<Function*> >::iterator it = clonesMap.begin(); it != clonesMap.end(); it++)
	{
		for (unsigned i=0; i < it->second.size(); i++)
	    {
	      DEBUG(errs() << it->first << " : " << it->second[i]->getName() << "\n");
	    }
	}
	return;
}

void FuseMinixPreparer::displayOnProbe(Probe probe)
{
    switch (probe)
    {
      case callers_sef_receive_status:
        errs() << "Callers of sef_receive_status()" << "\t [Total: " << sefreceiveCallers.size() << "]\n";
        for(unsigned i=0; i < sefreceiveCallers.size(); i++)
        {
          errs() << "SEFRCV: " << sefreceiveCallers[i]->getName() << "\n";
        }
        errs() << "\n";

      break;

      case inlined_functions:
        errs() << "Inlined functions:" << "\t [Total: " << functionsInlined.size() << "]\n";
        for(std::map<Function*, bool>::iterator IB = functionsInlined.begin(), IE = functionsInlined.end(); IB != IE; IB++)
        {
            errs() << "INL: " << IB->first->getName() << "\n";
        }
        errs() << "\n";

      break;

      case cloned_functions:
        errs() << "Cloned functions:" << "\t [Total: " << sefreceiveCallerClones.size() << "]\n";
        for(unsigned i=0; i < sefreceiveCallerClones.size(); i++)
        {
            errs() << "CLN: " << sefreceiveCallerClones[i]->  getName() << "\n";
        }
        errs() << "\n";

      break;

      case callers_ipc_sendrec:
        if (NULL != ipcSendrecShaper)
        {
          ipcSendrecShaper->displayOnProbe(probe);
        }

      default:

      break;
    }

    return;
}

bool FuseMinixPreparer::noopSefFunctions()
{
  const std::string callsToNoop[1] = { ".*_sef_.*" };
  std::vector<Regex*> callsToNoopRegexVec;
  for (unsigned i = 0; i < 1; i++)
  {
    PassUtil::parseRegexListOpt(callsToNoopRegexVec, callsToNoop[i]);
  }

  initDSAUtil();

  bool success = true;
  Module::FunctionListType &functionList = M->getFunctionList();
  std::vector<CallSite> sefCallSites;
  sefCallSites.clear();
  for (Module::iterator it = functionList.begin(); it != functionList.end(); ++it)
  {
    std::vector<CallSite> fCallSites;
    Function *F = it;
    if((false == PassUtil::matchRegexes(F->getName(), sefreceiveRegexVec))
      && PassUtil::matchRegexes(F->getName(), callsToNoopRegexVec))
    {
      fCallSites.clear();
      if (false == dsau->getCallSites(F, fCallSites))
      {
        success = false;
        DEBUG(errs() << "Getting callsites failed for: " << F->getName() << "\n");
        continue;
      }
      if (0 < fCallSites.size())
      {
        pushCallSites(sefCallSites, fCallSites);
      }
    }
  }

  DEBUG(errs() << "Total sef function callsites to be NOOPed: " << sefCallSites.size() << "\n");

  FuseMinixHelper *fmH = new FuseMinixHelper(M, dsau);
  std::map<FunctionType*, Function*> noopFunctionsMap;

  for (unsigned i=0; i < sefCallSites.size(); i++)
  {
    CallSite *curr = &sefCallSites[i];
    Function *calledFunction = curr->getCalledFunction();
    DEBUG(errs() << "NOOPing sef function: " << calledFunction->getName() << "\n");
    if (false == fmH->noopIt(curr, noopFunctionsMap))
    {
      success = false;
    }
  }
  return success;
}

}
#endif
