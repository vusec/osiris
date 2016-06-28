#include <pass.h>

#if LLVM_VERSION >= 37
#define DEBUG_TYPE "staticrecovery"
#endif

#include <staticrecovery/StaticRecoveryPass.h>

PASS_COMMON_INIT_ONCE();

static cl::opt<bool> 
suicideOpt("recovery-enable-suicide",
    cl::desc("Enable one-time suicides at window close points."),
    cl::value_desc("Enable deterministic crash once faults."),
    cl::NotHidden);

static cl::opt<std::string>
suicideMapFileOpt("recovery-suicide-map-file",
    cl::desc("Specify the file containing mapping of call site to recovery decision."),
    cl::NotHidden, cl::init(""));

static cl::opt<std::string>
ipcSiteDecisionMapFileOpt("recovery-decision-map-file",
    cl::desc("Specify the file containing mapping of call site to recovery decision."),
    cl::NotHidden, cl::ValueRequired);

static cl::opt<int> 
ipcSiteForceDecision("recovery-ipc-force-decision",
    cl::desc("Decision to take for IPC: 0=don't force, 1=optimistic, 4=pessimistic, 5-7=naive (never/conditional/always reply), 8-stateless, 9=enhanced (overrides recovery-decision-map-file)"),
    cl::NotHidden, cl::init(0));

static cl::opt<std::string>
kernelcallDecisionMapFileOpt("recovery-kernelcall-map-file",
    cl::desc("Specify the file containing mapping of kernelcall recovery decisions."),
    cl::NotHidden, cl::init(""));

static cl::opt<std::string>
kernelcallExclusionFileOpt("recovery-kernelcall-exclude-file",
    cl::desc("Specify the file containing list of kernelcall types to exclude."),
    cl::NotHidden, cl::init(""));

static cl::opt<bool> 
noSuicideOnKernelCallOpt("recovery-no-suicide-on-kernelcall",
    cl::desc("Disable suicide on _kernel_call()"),
    cl::value_desc("Disable suicide on _kernel_call()."),
    cl::NotHidden);

static cl::opt<std::string>
idempKernelCallersFileOpt("recovery-idempotent-kernelcallers-file",
	cl::desc("Specify the file containing list of idempotent kernel_call callers."),
	cl::NotHidden, cl::init(""));

bool StaticRecoveryPass::runOnModule(Module &M)
{
	bool retVal = false;

	if (0 != PassRunCount)
	{
		errs() << "Not rerunning this module pass.\n";
		return false;
	}

	this->M = &M;

	if ( (false == suicideOpt) && ("" == ipcSiteDecisionMapFileOpt))
	{
		errs() << "Nothing specified to perform. Exiting";
		return false;
	}

	errs() << "Initializing the pass...\n";
	this->decisionMapFile = ipcSiteDecisionMapFileOpt;
	this->kernelcallMapFile = kernelcallDecisionMapFileOpt;
	this->kernelcallExcludeFile = kernelcallExclusionFileOpt;
	this->idempotentKernelCallersFile = idempKernelCallersFileOpt;
	loadStaticRecoveryDecisions();

	suicideMap = NULL;
	if (suicideOpt && ("" != suicideMapFileOpt))
	{
		suicideMap = new std::map<uint64_t, long>();
		DecisionMapInputLoader *suicideMapLoader = new DecisionMapInputLoader(M.getModuleIdentifier());
		DEBUG(errs() << "suicideMapFile: " << suicideMapFileOpt << "\n");
		if (0 == suicideMapLoader->read(suicideMapFileOpt))
		{
			DEBUG(errs() << "WARNING: Couldn't load suicide map file: " << suicideMapFileOpt << "\n");
			return false;
		}

		suicideMapLoader->getCallSiteSuicideMap(*suicideMap);
	}

	noSuicideOnKernelCall = false;
	if (noSuicideOnKernelCallOpt)
	{
		noSuicideOnKernelCall = true;
	}
	
	retVal = plantHooks(suicideOpt, ipcSiteForceDecision);
	if (retVal)
	{
		DEBUG(errs() << "Successfully planted hooks.\n");
	}
	StaticRecoveryPass::PassRunCount++;
	return retVal;
}

void StaticRecoveryPass::getAnalysisUsage(AnalysisUsage &AU) const
{
}

char StaticRecoveryPass::ID = 0;

RegisterPass<StaticRecoveryPass> W("recovery", "Static Analysis based Recovery for MINIX 3 [ requires ltckpt static instrumentation ]");
