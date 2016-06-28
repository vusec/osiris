/******************************
* Author : Koustubha Bhat
* Jul-Oct 2014
* Vrije Universiteit, Amsterdam.
*******************************/
#ifndef FUSE_MINIX_IPC_SOURCE_SHAPER_H
#define FUSE_MINIX_IPC_SOURCE_SHAPER_H

#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <pass.h>
#include <common/util/string.h>
#include <common/pass_common.h>
#include <common/dsa_common.h>
#include <fuseminix/common.h>
#include <fuseminix/FuseMinixHelper.h>
#include <fuseminix/FuseMinixPass.h>

#define NUM_NON_SENDREC_IPC_FUNCS    4
#define FUSE_IPC_SITE_ID_KEY "fuse_ipc_site_id"

namespace llvm
{
	STATISTIC(NumSendrecFuncs,            "0 VANILLA: d. Num ipc_sendrec() functions.");
	STATISTIC(NumSendrecCallers,          "0 VANILLA: e. Num ipc_sendrec() callers.");
	STATISTIC(NumSendrecCallSites,        "0 VANILLA: f. Num ipc_sendrec() call sites.");
	STATISTIC(NumSendrecCallersInlined,                     "2 PREPARE: a. Num ipc_sendrec() callers inlined.");
	STATISTIC(NumSendrecCallersAfterInlining,               "2 PREPARE: d. Num ipc_sendrec() callers after inlining.");
	STATISTIC(NumSendrecCallSitesAfterInlining,             "2 PREPARE: e. Num ipc_sendrec() callsites after inlining.");
	STATISTIC(NumSendrecCallSitesWithConstEndptArg,         "2 PREPARE: e. a. With constant endpoints.");
	STATISTIC(NumSendrecCallSitesWithNonConstEndptArg,      "2 PREPARE: e. b. With NON-constant endpoints.");
	STATISTIC(NumNonMsgArgedSendrecCallers,                 "2 PREPARE: f. Total Num ipc_sendrec() callers that are non-msg arged.");
	STATISTIC(NumNonMsgArgedSendrecCallersBeforeSendrecPrepare, "2 PREPARE: g. Num ipc_sendrec() callers that were non-msg arged before.");
	STATISTIC(NumNonMsgArgedSendrecCallersDueToSendrecPrepare,  "2 PREPARE: h. Num ipc_sendrec() callers that were converted to non-msg arged.");
  STATISTIC(NumOriginalSendrecCallerCallSites, "2 PREPARE: i. Num ipc_sendrec() original callers remaining untouched.");

class IPCSourceShaper
{
public:
	IPCSourceShaper(Module *M, Pass *P);
	bool handleSendrecCallers();
	bool noopOtherIpcSends();
	std::map<CallSite*, IPCInfo*>* getAnalysisResult();
	void displayOnProbe(Probe probe);

private:
	Module *M;
	DSAUtil *dsau;
	Pass *P;
	FuseMinixHelper *fmHelper;
	uint64_t ipc_site_id;
	std::vector<Regex*>  sendrecRegexVec;
	std::vector<Function*> sendrecFunctions;
	std::vector<Function*> sendrecCallers;
	std::vector<CallSite>  sendrecCallSites;
	std::map<CallSite, enum SendrecCallSiteEndpointType> sendrecCallSitesMap;
	std::map<Function*, bool> inlinedSendrecCallersMap;
	std::map<Function*, int> nonMsgArgedSendrecCallersMap;
	std::map<CallSite*, IPCInfo*> *sendrecCallSiteAtNonMsgArgedCallersMap;
	std::vector<Function*> nonSendrecIPCFunctions;

	/* for saving the runnign ipc_siteid across invocations */
	void init_ipc_site_id(void);
	void save_ipc_site_id(void);

	uint64_t get_next_ipc_site_id() {return ipc_site_id++;}

	bool initDSAUtil();
	bool loadSendrecCallSiteAtNonMsgArgedCallersMap();
	void getVanillaStats(bool addToStore);
	bool getMsgArgedDirectSendrecCallSites(std::vector<CallSite> &msgArgedSendrecCallSites);
	bool transformSendrecCallSites();
	bool getDestinationEndpoints(Function *sendrecCaller, std::vector<int> &destEndpoints);
	void nameCallSite(Instruction *I);
};


void IPCSourceShaper::init_ipc_site_id()
{

	NamedMDNode *nmdn = M->getOrInsertNamedMetadata("fuseing_ipc_site_max_id");
	ipc_site_id = 0;
	if ( nmdn == NULL || nmdn->getNumOperands() < 1 ) {
		return;
	}
	MDNode *N = nmdn->getOperand(0);
	if ( N != NULL && N->getNumOperands() <= 1 ) {
		ConstantInt *CI = dyn_cast_or_null<ConstantInt>(N->getOperand(0));
		ipc_site_id = CI->getZExtValue();
		return;
	}
}



void IPCSourceShaper::save_ipc_site_id()
{
	NamedMDNode *nmdn = M->getOrInsertNamedMetadata("fuseing_ipc_site_max_id");
	ConstantInt *CI = ConstantInt::get(M->getContext(), APInt(64, ipc_site_id));
	SmallVector<Value*, 1> V;
	V.push_back(CI);
	MDNode *N = MDNode::get(M->getContext(), V);
	nmdn->dropAllReferences();
	nmdn->addOperand(N);
}


void IPCSourceShaper::nameCallSite(Instruction *I)
{
	MDNode *N;
	N = I->getMetadata(FUSE_IPC_SITE_ID_KEY);
	if (N != NULL && N->getNumOperands() != 0) {
		return;
	}
	ConstantInt *CI = ConstantInt::get(M->getContext(),
		APInt(64, get_next_ipc_site_id()));
	MDString  *Str = MDString::get(M->getContext(), M->getModuleIdentifier());
	SmallVector<Value*, 2> V;
	V.push_back(CI);
	V.push_back(Str);
	N = MDNode::get(M->getContext(), V);
	I->setMetadata(FUSE_IPC_SITE_ID_KEY, N);
}


IPCSourceShaper::IPCSourceShaper(Module *M, Pass *P)
{
	std::string sendrecString = (std::string)SENDREC_FUNC_NAME;
	std::string asynsend3String = (std::string) ASYNSEND3_FUNC_NAME;
	PassUtil::parseRegexListOpt(sendrecRegexVec, sendrecString);
	PassUtil::parseRegexListOpt(sendrecRegexVec, asynsend3String);

	this->M = M;
	this->P = P;
	initDSAUtil();
	if (NULL != M && NULL != dsau){
		fmHelper = new FuseMinixHelper(this->M, this->dsau);
	}
	sendrecCallSiteAtNonMsgArgedCallersMap = new std::map<CallSite*, IPCInfo*>();
	sendrecCallSiteAtNonMsgArgedCallersMap->clear();
	init_ipc_site_id();
}

bool IPCSourceShaper::noopOtherIpcSends()
{
  const std::string nonSendrecIpcFunctionNames[NUM_NON_SENDREC_IPC_FUNCS] = { "_ipc_send[^(rec)]*$",
                                                                              "_ipc_sendnb",
                                                                              "_ipc_notify",
                                                                              "_ipc_senda"
                                                                             };
  std::vector<Regex*> nonIpcSendrecRegexVec;
  for (unsigned i = 0; i < NUM_NON_SENDREC_IPC_FUNCS; i++)
  {
    PassUtil::parseRegexListOpt(nonIpcSendrecRegexVec, nonSendrecIpcFunctionNames[i]);
  }

  Module::FunctionListType &functionList = M->getFunctionList();
  for (Module::iterator it = functionList.begin(); it != functionList.end(); ++it)
  {
    Function *F = it;
    if (F->isIntrinsic())
            continue;
    if (PassUtil::matchRegexes(F->getName(), nonIpcSendrecRegexVec))
    {
      nonSendrecIPCFunctions.push_back(F);
      DEBUG(errs() << "Non sendrec ipc function: " << F->getName() << "\n");
    }
  }

  if (false == initDSAUtil())
  {
    return false;
  }

  std::vector<CallSite> nonSendrecIpcCallSites;
  std::vector<CallSite> perFunctionCallSites;
  for(std::vector<Function*>::iterator I = nonSendrecIPCFunctions.begin(), E = nonSendrecIPCFunctions.end(); I != E; I++)
  {
    Function *F = *I;
    perFunctionCallSites.clear();
    dsau->getCallSites(F, perFunctionCallSites);
    for (unsigned i = 0; i < perFunctionCallSites.size(); i++)
    {
      nonSendrecIpcCallSites.push_back(perFunctionCallSites[i]);
    }
  }
  std::map<FunctionType*, Function*> noopFunctionsMap;

  // Remove the call instructions!
  DEBUG(errs() << "Nooping non ipc_sendrec IPC callsites. (" << nonSendrecIpcCallSites.size() << ")\n");
  for(unsigned i = 0; i < nonSendrecIpcCallSites.size(); i++)
  {
    CallSite *currCallSite = &nonSendrecIpcCallSites[i];
    DEBUG(errs() << "Called function name: " << currCallSite->getCalledFunction()->getName() << "\n");
    if (false == fmHelper->noopIt(currCallSite, noopFunctionsMap))
    {
      return false;
    }
  }

  DEBUG(errs() << "noopFunctionsMap size: " << noopFunctionsMap.size() << "\n");
  return true;
}


bool IPCSourceShaper::handleSendrecCallers()
{
  getVanillaStats(true);
  initDSAUtil();
  sendrecCallers.clear();

  std::vector<CallSite> callSites;
  int totalsendreccallsites = 0;
  for (unsigned i=0; i < sendrecFunctions.size(); i++)
  {
    int numCallers = getCallers(dsau, sendrecFunctions[i], sendrecCallers);
    DEBUG(errs() << "[Before inlining] Num callers of " << sendrecFunctions[i]->getName() << ": " << numCallers << "\n");
    if (dsau->getCallSites(sendrecFunctions[i], callSites))
    {
      DEBUG(errs() << "[Before inlining] Num callsites of " << sendrecFunctions[i]->getName() << ": " << callSites.size() << "\n");
      totalsendreccallsites += callSites.size();
    }
  }

  DEBUG(errs() << "[Before inlining] Total sendrec callers: " << sendrecCallers.size() << "\n");
  DEBUG(errs() << "[Before inlining] Total sendrec call sites: " << totalsendreccallsites << "\n");

  if ( 0 == sendrecCallers.size())
  {
    DEBUG(errs() << "WARNING: No sendrec callers in my store!\n");
    return false;
  }

  DEBUG(errs() << "Beginning to process sendrec callsites' message arguments...\n");

  if (false == transformSendrecCallSites())
  {
    DEBUG(errs() << "Error: Failed to prepare sendrec callsites.\n");
    return false;
  }

  // Enumerate sendrec callers after the transformation.
  initDSAUtil();
  for(unsigned i=0; i < sendrecFunctions.size(); i++)
  {
    int numCallers = getCallers(dsau, sendrecFunctions[i], sendrecCallers);
    NumSendrecCallersAfterInlining += numCallers;
    std::vector<CallSite> callSites;
    callSites.clear();
    if (dsau->getCallSites(sendrecFunctions[i], callSites))
    {
      for (auto it = callSites.begin(); it < callSites.end() ; it++) {
        nameCallSite(it->getInstruction());
      }
      DEBUG(errs() << "[After inlining] Num callsites of " << sendrecFunctions[i]->getName() << ": " << callSites.size() << "\n");
      NumSendrecCallSitesAfterInlining += callSites.size();
    }
  }

  NumNonMsgArgedSendrecCallers = nonMsgArgedSendrecCallersMap.size();
  DEBUG(errs() << "Total number of sendrec callers after inlining operations : " << NumSendrecCallersAfterInlining << "\n");
  DEBUG(errs() << "Number of sendrec callers that do not have Minix message as an argument: " << NumNonMsgArgedSendrecCallers << "\n");

  if (false == loadSendrecCallSiteAtNonMsgArgedCallersMap())
  {
    DEBUG(errs() << "Failed during handleSendrecCallers().\n");
  }

  save_ipc_site_id();
  return true;
}

std::map<CallSite*, IPCInfo*>* IPCSourceShaper::getAnalysisResult()
{
  if (0 == (sendrecCallSiteAtNonMsgArgedCallersMap->size()))
  {
    return NULL;
  }
  return sendrecCallSiteAtNonMsgArgedCallersMap;
}

bool IPCSourceShaper::loadSendrecCallSiteAtNonMsgArgedCallersMap()
{
  if (0 == nonMsgArgedSendrecCallersMap.size())
  {
    return false;
  }
  if(false == initDSAUtil())
  {
    return false;
  }
  std::vector<CallSite> callSites;
  for(unsigned i=0; i < sendrecFunctions.size(); i++)
  {
    callSites.clear();
    if (false == dsau->getCallSites(sendrecFunctions[i], callSites))
    {
      DEBUG(errs() << "Unable to get call sites of sendrec function: " << sendrecFunctions[i]->getName() << "\n");
    }
    if (0 < callSites.size())
    {
      pushCallSites(sendrecCallSites, callSites);
    }
  }
  DEBUG(errs() << "Total sendrec callsites: " << sendrecCallSites.size() << "\n");

  sendrecCallSiteAtNonMsgArgedCallersMap->clear();
  NumSendrecCallSitesWithConstEndptArg    = 0;
  NumSendrecCallSitesWithNonConstEndptArg = 0;
  for(unsigned i=0; i < sendrecCallSites.size(); i++)
  {
    CallSite *callSite = &sendrecCallSites[i];
    std::vector<IPCEndpoint> destEndpoints;
    IPCMType mtypeValue = INVALID_IPC_MTYPE;
    IPCEndpoint srcEndpoint = INVALID_IPC_ENDPOINT;
    IPCEndpoint tmpEndpointValue = INVALID_IPC_ENDPOINT;

    if (0 == nonMsgArgedSendrecCallersMap.count(sendrecCallSites[i].getCaller()))
    {
      // Not interested, as they are the original callers which are left untouched (non-transformed)
      continue;
    }
    // check for const vs non-const endpoint argument
    if (fmHelper->getConstantIntValue(callSite->getArgument(0), tmpEndpointValue))
    {
      ++NumSendrecCallSitesWithConstEndptArg;
      destEndpoints.push_back(tmpEndpointValue);
      DEBUG(errs() << "callsite (caller function) with const endpoint arg: " << callSite->getCaller()->getName() << " is: " << tmpEndpointValue <<"\n");
    }
    else
    {
      DEBUG(errs() << "callsite (caller function) with non-const endpoint arg: " << callSite->getCaller()->getName() <<"\n");
      int destEndpt = -1;
      if ((true == fmHelper->getEndpointValue(callSite, destEndpt)) && (destEndpt != -1))
      {
        destEndpoints.push_back(destEndpt);
        DEBUG(errs() << "Got const value of the non-const endpoint arg. Endpoint value: " << destEndpt << "\n");
        ++NumSendrecCallSitesWithConstEndptArg;
      }
      else
      {
        DEBUG(errs() << "\nnon-const-still: caller: " << callSite->getCaller()->getName() << "\n");
        ++NumSendrecCallSitesWithNonConstEndptArg;
        getAllEndpoints(destEndpoints);
      }

    }
    if (false == fmHelper->getM_TYPEValues(callSite, mtypeValue))
    {
      mtypeValue = INVALID_IPC_MTYPE;
    }
    DEBUG(errs() << "Fetching endpoint for src: " <<  callSite->getCaller()->getName() << "\n");
    if (false == getEndpointMapping(getMinixModulePrefix(callSite->getCaller()), srcEndpoint))
    {
      srcEndpoint = INVALID_IPC_MTYPE;
    }
    IPCInfo *info = new IPCInfo();
    info->srcEndpoint = srcEndpoint;
    info->destEndpoints = destEndpoints;
    info->mtype = mtypeValue;
    if (0 == sendrecCallSiteAtNonMsgArgedCallersMap->count(&sendrecCallSites[i]))
    {
      sendrecCallSiteAtNonMsgArgedCallersMap->insert(std::pair<CallSite*, IPCInfo*>(&sendrecCallSites[i], info) );
    }
  }
  DEBUG(errs() << "Total number of relevant callsites: " << sendrecCallSiteAtNonMsgArgedCallersMap->size() << "\n");
  return true;
}

bool IPCSourceShaper::getMsgArgedDirectSendrecCallSites(std::vector<CallSite> &msgArgedSendrecCallSites)
{
  if (NULL == fmHelper || false == initDSAUtil() || (0 == sendrecCallers.size()))
  {
    return false;
  }

  std::vector<CallSite> callSites;
  for(unsigned i=0; i < sendrecCallers.size(); i++)
  {
    if (false == fmHelper->hasMinixMessageParameter(sendrecCallers[i]))
    {
      std::vector<Function*> callers;
      int numCallers = getCallers(dsau, sendrecCallers[i], callers);
      nonMsgArgedSendrecCallersMap.insert(std::pair<Function*, int>(sendrecCallers[i], numCallers));

      ++NumNonMsgArgedSendrecCallersBeforeSendrecPrepare;

      continue;
    }
    callSites.clear();
    if (false == dsau->getCallSites(sendrecCallers[i], callSites))
    {
      continue;
    }
    if (0 != callSites.size())
    {
      for(unsigned j=0; j < callSites.size(); j++)
      {
        msgArgedSendrecCallSites.push_back(callSites[j]);
      }
    }
  }
  return true;
}

bool IPCSourceShaper::transformSendrecCallSites()
{
  bool success = true;
  std::deque<CallSite> callSitesQueue;
  // Initialize the queue
  std::vector<CallSite> msgArgedSendrecCallSites;
  if (false == getMsgArgedDirectSendrecCallSites(msgArgedSendrecCallSites))
  {
    DEBUG(errs() << "WARNING: getMsgArgedDirectSendrecCallSites failed.\n");
    return false;
  }
  if (0 == msgArgedSendrecCallSites.size())
  {
    DEBUG(errs() << "WARNING: Num msgArgedSendrecCallSites is ZERO!\n");
    // return false;
    return true; // already inlined due to "prepare_servers" pass
  }

  DEBUG(errs() << "Initializing callSitesQueue with " << msgArgedSendrecCallSites.size() << " call sites.\n");
  pushCallSites(callSitesQueue, msgArgedSendrecCallSites);

  DEBUG(errs() << "Pushed the callsites into queue." << " Callsites queue size: " << callSitesQueue.size() << "\n");

  // Process the queue
  CallSite currCallSite;
  while(0 != callSitesQueue.size())
  {
    currCallSite = callSitesQueue.front();
    callSitesQueue.pop_front();
    DEBUG(errs() << "popped from queue.\n");

    if ((!currCallSite.isCall()) || (false == isa<CallInst>(currCallSite.getInstruction())))
    {
        DEBUG(errs() << "Non call instruction. Skipping.\n");
        continue;
    }
		if (NULL == currCallSite.getCalledFunction())
		{
			continue;
		}
		DEBUG(errs() << "currCallSite called function: " << currCallSite.getCalledFunction()->getName() << "\n");

    if (NULL != fmHelper->getMinixMessageArgument(&currCallSite))
    {
        // has message argument
      DEBUG(errs() << "Has minix message argument.\n");

        // collect the nonMsgArgedSendrecCaller
        if (NULL == currCallSite.getInstruction()->getParent())
        {
          // We can get in here, if there have been duplicate callsites
          // pushed into the queue, because of multiple callers calling the
          // same function (which can get pushed multiple times.)
          continue;
        }
        if (NULL == currCallSite.getInstruction()->getParent()->getParent())
        {
          continue;
        }
        Function *currCaller = currCallSite.getCaller();
        if (NULL == currCaller)
        {
          DEBUG(errs() << "Error: currCaller is NULL.\n");
          continue;
        }
        DEBUG(errs() << "currCallSite caller: " << currCaller->getName() << "\n");
        if (false == fmHelper->hasMinixMessageParameter(currCaller))
        {
          if (0 == nonMsgArgedSendrecCallersMap.count(currCaller))
          {
            nonMsgArgedSendrecCallersMap.insert(std::pair<Function*, int>(currCaller, 1));
            ++NumNonMsgArgedSendrecCallersDueToSendrecPrepare;
          }
          else
          {
            // just keep count of number of callsites of the callers that we are interested in.
            ((*(nonMsgArgedSendrecCallersMap.find(currCaller))).second)++;
          }
        }

        // inline the callsite => result: now we dont have the callsite intact anymore!
        if (false == fmHelper->inlineFunctionAtCallSite(&currCallSite))
        {
          DEBUG(errs() << "WARNING: Attempt to inline callsite (caller function): " << currCallSite.getCaller()->getName() << " failed.\n");
          success = false;
        }

        // keep count of the num of direct sendrec callers that we inlined.
        if (0 == inlinedSendrecCallersMap.count(currCaller))
        {
          inlinedSendrecCallersMap.insert(std::pair<Function*,bool>(currCaller,true));
          ++NumSendrecCallersInlined;
        }

        // get callSites of the caller of this callsite.
        std::vector<CallSite> callerCallSites;
        if (true == dsau->getCallSites(currCaller, callerCallSites))
        {
          pushCallSites(callSitesQueue, callerCallSites);
        }
    }
  }
  return success;
}

bool IPCSourceShaper::initDSAUtil()
{
  if(NULL == this->P || NULL == this->M)
  {
	errs() << "FATAL: could not create DSAUtil\n";
	exit(1);
    return false;
  }
  this->dsau = new DSAUtil();
  this->dsau->init((ModulePass *)this->P, this->M);
  return true;
}

void IPCSourceShaper::getVanillaStats(bool addToStore)
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
    else if (PassUtil::matchRegexes(F->getName(), sendrecRegexVec))
    {
      ++NumSendrecFuncs;
      if (addToStore)
        sendrecFunctions.push_back(F);

      numCallers = 0;
      callSites.clear();
      if (0 < (numCallers = getCallers(dsau, F, sendrecCallers)))
      {
        NumSendrecCallers += numCallers;
      }
      if (true == dsau->getCallSites(F, callSites))
      {
        NumSendrecCallSites += callSites.size();
      }
    }
  }

  onceExecuted = true;
  return;
}

bool IPCSourceShaper::getDestinationEndpoints(Function *sendrecCaller, std::vector<IPCEndpoint> &destEndpoints)
{
  std::string callerName = sendrecCaller->getName();
  int numDestinations = 0;

  DEBUG(errs() << "Fetching target(s) for caller: " << callerName << "\n");

  if (std::string::npos != callerName.find((std::string)MX_PREFIX + "devman_do"))
  {
    // ALL except RS
    std::vector<unsigned> indicesToRemove;
    IPCEndpoint rsEndpoint;
    if (false == getAllEndpoints(destEndpoints) || getEndpointMapping("rs", rsEndpoint))
    {
      return false;
    }
    for (unsigned i=0; i < destEndpoints.size(); i++)
    {
      if (destEndpoints[i] == rsEndpoint)
      {
        indicesToRemove.push_back(i);
        destEndpoints.erase(destEndpoints.begin() + i);
      }
    }
    numDestinations = destEndpoints.size();
  }
  else if (std::string::npos != callerName.find((std::string)MX_PREFIX)
       && std::string::npos != callerName.find("bdev_"))
  {
    // dynamic table lookup through DS
    // TODO: solve this problem
    DEBUG(errs() << "Needs dynamic table lookup! Skipping.\n");
  }
  else if (std::string::npos != callerName.find((std::string)MX_PREFIX)
       && std::string::npos != callerName.find("pci_"))
  {
    // pci endpoint.
    // TODO: solve this problem
    DEBUG(errs() << "Needs dynamic table lookup! Skipping.\n");
  }
  else if (std::string::npos != callerName.find((std::string)MX_PREFIX + "procfs"))
  {
    // pci endpoint
    // TODO: solve this problem
    DEBUG(errs() << "Needs dynamic table lookup! Skipping.\n");
  }
  else if (std::string::npos != callerName.find((std::string)MX_PREFIX + "rs_publish_service")
       || std::string::npos != callerName.find((std::string)MX_PREFIX + "rs_publish_service"))
  {
    // devman endpoint
    // TODO: solve this problem
    DEBUG(errs() << "Needs dynamic table lookup! Skipping.\n");
  }
  else if (std::string::npos != callerName.find((std::string)MX_PREFIX + "vbfs_vbox_"))
  {
    // vbox
    // TODO: solve this problem
    DEBUG(errs() << "Needs dynamic table lookup! Skipping.\n");
  }
  else if (std::string::npos != callerName.find((std::string)MX_PREFIX))
  {
    enum taskcall_dest
    {
      PM, VFS, RS, SCHED, TTY, DS, MFS, VM, PFS
    };

    bool taskcallEndpoints[NUM_TASKCALL_ENDPOINT_TYPES] = { false, false, false, false, false, false, false, false, false };

    if (std::string::npos != callerName.find("_taskcall"))
    {
      if (std::string::npos != callerName.find("rs")
        || std::string::npos != callerName.find("pm"))
      {
        for(int i=0; i < NUM_TASKCALL_ENDPOINT_TYPES; i++)
        {
          taskcallEndpoints[i] = true;
        }
      }
      else if (std::string::npos != callerName.find("devman"))
      {
        taskcallEndpoints[RS] = true;
      }
      else if (std::string::npos != callerName.find("ds"))
      {
        taskcallEndpoints[RS] = true;
        taskcallEndpoints[DS] = true;
      }
      else if (std::string::npos != callerName.find("hgfs"))
      {
        taskcallEndpoints[RS] = true;
      }
      else if (std::string::npos != callerName.find("inet"))
      {
        taskcallEndpoints[RS] = true;
        taskcallEndpoints[DS] = true;
      }
      else if (std::string::npos != callerName.find("input"))
      {
        taskcallEndpoints[RS] = true;
        taskcallEndpoints[DS] = true;
      }
      else if (std::string::npos != callerName.find("ipc"))
      {
        taskcallEndpoints[RS] = true;
        taskcallEndpoints[VM] = true;
        taskcallEndpoints[PM] = true;
      }
      else if (std::string::npos != callerName.find("is"))
      {
        taskcallEndpoints[VM] = true;
        taskcallEndpoints[TTY] = true;
        taskcallEndpoints[PM] = true;
      }
      else if (std::string::npos != callerName.find("isofs"))
      {
        taskcallEndpoints[RS] = true;
        taskcallEndpoints[DS] = true;
        taskcallEndpoints[VM] = true;
      }
      else if (std::string::npos != callerName.find("lwip"))
      {
        taskcallEndpoints[RS] = true;
        taskcallEndpoints[DS] = true;
      }
      else if (std::string::npos != callerName.find("mfs"))
      {
        taskcallEndpoints[RS] = true;
        taskcallEndpoints[DS] = true;
        taskcallEndpoints[VM] = true;
      }
      else if (std::string::npos != callerName.find("pfs"))
      {
        taskcallEndpoints[RS] = true;
      }
      else if (std::string::npos != callerName.find("procfs"))
      {
        taskcallEndpoints[RS] = true;
        taskcallEndpoints[DS] = true;
        taskcallEndpoints[VM] = true;
        taskcallEndpoints[PM] = true;
        taskcallEndpoints[VFS] = true;
      }
      else if (std::string::npos != callerName.find("sched"))
      {
        taskcallEndpoints[RS] = true;
      }
      if (std::string::npos != callerName.find("vbfs"))
      {
        taskcallEndpoints[RS] = true;
        taskcallEndpoints[DS] = true;
      }
      if (std::string::npos != callerName.find("vfs"))
      {
        taskcallEndpoints[RS] = true;
        taskcallEndpoints[DS] = true;
        taskcallEndpoints[VM] = true;
      }
      if (std::string::npos != callerName.find("vm"))
      {
        taskcallEndpoints[RS] = true;
        taskcallEndpoints[VM] = true;
      }
      if (std::string::npos != callerName.find("ext2"))
      {
        taskcallEndpoints[RS] = true;
        taskcallEndpoints[DS] = true;
        taskcallEndpoints[VM] = true;
      }

      for (int i = 0; i < NUM_TASKCALL_ENDPOINT_TYPES; i++)
      {
        std::string targetModulePrefix = "";

        // PM, VFS, RS, SCHED, TTY, DS, MFS, VM, PFS
        switch (i)
        {
          case PM:
          if (taskcallEndpoints[i])
          {
            targetModulePrefix = (std::string)MX_PREFIX + "pm";
          }
          break;

          case VFS:
          if (taskcallEndpoints[i])
          {
            targetModulePrefix = (std::string)MX_PREFIX + "vfs";
          }
          break;

          case RS:
          if (taskcallEndpoints[i])
          {
            targetModulePrefix = (std::string)MX_PREFIX + "rs";
          }
          break;

          case SCHED:
          if (taskcallEndpoints[i])
          {
            targetModulePrefix = (std::string)MX_PREFIX + "sched";
          }
          break;

          case TTY:
          if (taskcallEndpoints[i])
          {
            targetModulePrefix = (std::string)MX_PREFIX + "tty";
          }
          break;

          case DS:
          if (taskcallEndpoints[i])
          {
            targetModulePrefix = (std::string)MX_PREFIX + "ds";
          }
          break;

          case MFS:
          if (taskcallEndpoints[i])
          {
            targetModulePrefix = (std::string)MX_PREFIX + "mfs";
          }
          break;

          case VM:
          if (taskcallEndpoints[i])
          {
            targetModulePrefix = (std::string)MX_PREFIX + "vm";
          }
          break;

          case PFS:
          if (taskcallEndpoints[i])
          {
            targetModulePrefix = (std::string)MX_PREFIX + "pfs";
          }
          break;

          default:
          DEBUG(errs() << "DEFAULT\n");
          break;
        }

        if ("" != targetModulePrefix)
        {
          int endpt = INVALID_IPC_ENDPOINT;
          if (false == getEndpointMapping(targetModulePrefix, endpt))
          {
            return false;
          }
          destEndpoints.push_back(endpt);
        }
      }  // for ends
    }
    else
    {
      DEBUG(errs() << "None of the buckets fit me!\n");
    }
    // Otherwise, it could be __syscall - which we dont know much about at this point in time.
  }
  return numDestinations;
}

void IPCSourceShaper::displayOnProbe(Probe probe)
{
  switch(probe)
  {
    case callers_ipc_sendrec:
      errs() << "Callers of ipc_sendrec()\t[Total: " << sendrecCallers.size() << "]\n";
      for (unsigned i=0; i < sendrecCallers.size(); i++)
      {
        errs() << sendrecCallers[i]->getName() << "\n";
      }
      errs() << "\n";
      errs() << "Callers of ipc_sendrec(), that are inlined and used for analysis\t [Total: " << nonMsgArgedSendrecCallersMap.size() << "]\n";
      for (std::map<Function*, int>::iterator I = nonMsgArgedSendrecCallersMap.begin(), E = nonMsgArgedSendrecCallersMap.end(); I != E; I++)
      {
        errs() << (*I).first->getName() << "\n";
      }
      errs() << "\n";
    break;

    default:
    break;
  }
}

}
#endif
