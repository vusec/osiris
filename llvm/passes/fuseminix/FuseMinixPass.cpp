/******************************
* Author : Koustubha Bhat
* Jul-Oct 2014
* Vrije Universiteit, Amsterdam.
*******************************/
#if LLVM_VERSION >= 37
#define DEBUG_TYPE "fuseminix"
#endif

#include <fuseminix/FuseMinixPreparer.h>
#include <fuseminix/FuseMinixFuser.h>
#include <fuseminix/FuseMinixPass.h>
#include <common/util/string.h>
#include <common/pass_common.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

using namespace llvm;

namespace llvm
{

static cl::opt<std::string>
endpointMapFileOpt("fuseminix-endpointmapfile",
    cl::desc("Specify the file containing endpoint to Minix module mapping."),
    cl::NotHidden, cl::ValueRequired, cl::Required);

static cl::list<Probe>
probeOpt("fuseminix-probe", 
    cl::desc("Specify one or more of the available probes"),
    cl::values(clEnumVal(callers_sef_receive_status, "List callers of sef_receive_status() functions (fuseminix pass)."),
               clEnumVal(callers_ipc_sendrec,        "List callers of ipc_sendrec() functions (fuseminix pass)."),
               clEnumVal(inlined_functions,          "List all the functions that were inlined by this pass (fuseminix pass)."),
               clEnumVal(cloned_functions,           "List all the functions that were cloned by this pass (fuseminix pass)."),
               clEnumValEnd),
    cl::ZeroOrMore, cl::CommaSeparated, cl::NotHidden, cl::ValueRequired);

static cl::OptionCategory SendrecFilterCheckCat("fuseminix options for ipc_sendrec() call filter check", 
                                                "These provide options to examine the filter results based on different kinds of inputs.");
static cl::list<int>
endpointSendrecFilterOpt("fuseminix-sendrec-endpoint",
    cl::desc("Filter the potential ipc_sendrec() destinations by specifying the destination endpoint value (integer)."),
    cl::cat(SendrecFilterCheckCat),
    cl::ZeroOrMore, cl::CommaSeparated, cl::NotHidden, cl::ValueRequired);

static cl::list<std::string>
moduleNameSendrecFilterOpt("fuseminix-sendrec-module",
    cl::desc("Filter the potential ipc_sendrec() destinations by specifying destination module name."),
    cl::cat(SendrecFilterCheckCat),
    cl::ZeroOrMore, cl::CommaSeparated, cl::NotHidden, cl::ValueRequired);

PASS_COMMON_INIT_ONCE();

FuseMinixPass::FuseMinixPass() :
    ModulePass(ID){}

bool FuseMinixPass::runOnModule(Module &M)
{
  bool returnValue = false;
  this->M = &M;
  
  /******************************************
    INITIALIZATION 
  *******************************************/
  FuseMinixPreparer *preparer = NULL;

  DEBUG(errs() << "endpointMapFileOpt : " << endpointMapFileOpt << "\n");

  if (fileExists(endpointMapFileOpt))
  {
    preparer = new FuseMinixPreparer(this, endpointMapFileOpt);
  }
  else
  {
    DEBUG(errs() << "Error: Couldnt load endpoint to Minix module map file." << "\n");
    return false;
  }

  DEBUG(errs() << "preparer initialized." << "\n");
  
  /******************************************
    PREPARE FOR ACTION
  *******************************************/
  returnValue = preparer->prepare(M);

  /******************************************
    FUSE THE CALLS
  *******************************************/
  bool fuse=true;
  // Replace all the sendrec calls with a set of calls to all the sef_receive_status() clone functions
  FuseMinixFuser *fuser = new FuseMinixFuser(this);
  if (fuse)
  {
    returnValue = fuser->fuse(preparer);
  }

  /******************************************
    POSTMORTEM - How it all went...
  *******************************************/
  // Honouring probe and filtering command line options
  if (0 < probeOpt.size())
  {
    for (std::vector<Probe>::iterator PB=probeOpt.begin(), PE=probeOpt.end(); PB != PE; PB++)
    {
      switch(*PB)
      {
        case callers_ipc_sendrec:
        case callers_sef_receive_status:
        case inlined_functions:
        case cloned_functions:
          preparer->displayOnProbe(*PB);
        break;

        default:
        break;
      }
    }
  }

  if (0 < endpointSendrecFilterOpt.size())
  {
    std::vector<Function*> destinationFunctions;
    for(std::vector<int>::iterator EB = endpointSendrecFilterOpt.begin(), EE=endpointSendrecFilterOpt.end(); EB != EE; EB++)
    {
      if (0 > preparer->getPotentialSendrecDestinations(*EB, destinationFunctions))
      {
        continue;
      }
      errs() << "Endpoint value: " << *EB << "\n";
      for (unsigned i=0; i < destinationFunctions.size(); i++)
      {
        errs() << "EP_FILTER: " << destinationFunctions[i]->getName() << "\n";
      }
      destinationFunctions.clear();
    }
  }

  if (0 < moduleNameSendrecFilterOpt.size())
  {
    std::vector<Function*> destinationFunctions;
    std::string moduleName;
    for(std::vector<std::string>::iterator MB = moduleNameSendrecFilterOpt.begin(), ME=moduleNameSendrecFilterOpt.end(); MB != ME; MB++)
    {
      moduleName = *MB;
      if (0 > preparer->getPotentialSendrecDestinations(moduleName, destinationFunctions))
      {
        continue;
      }
      errs() << "Destination module name: " << moduleName << "\n";
      for (unsigned i=0; i < destinationFunctions.size(); i++)
      {
        errs() << "MOD_FILTER: " << destinationFunctions[i]->getName() << "\n";
      }
      destinationFunctions.clear();
    }
  }

  return returnValue;
}

void FuseMinixPass::getAnalysisUsage(AnalysisUsage &AU) const
{
#if LLVM_VERSION >= 37
  AU.addRequired<LoopInfoWrapperPass>();
#else
  AU.addRequired<LoopInfo>();
#endif
}

char FuseMinixPass::ID;
RegisterPass<FuseMinixPass> FMP("fuseminix", "Fuse inter-components interactions of Minix");

}

