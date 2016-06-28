/******************************
* Author : Koustubha Bhat
* Date   : 05-Feb-2015
* Vrije Universiteit, Amsterdam.
*******************************/

#ifndef LTCKPT_STATIC_RECOVERY_FUSEMINIX_COMMON_H
#define LTCKPT_STATIC_RECOVERY_FUSEMINIX_COMMON_H

#define MX_PREFIX						"mx"
#define MINIX_MODULE_PREFIX_PATTERN 	"mx_[^_]*"
#define MINIX_STRUCT_MESSAGE        	"message"
#define FUSER_FUNC_PATTERN	 			".*_[a-zA-Z0-9]*fuser_.*"
#define FUSE_IPC_SITE_ID_KEY      "fuse_ipc_site_id"

using namespace llvm;

namespace llvm
{
	enum Marking
	{
		REQUEST_LOCAL,
		PROCESS_LOCAL,
		GLOBAL_VALUE,
		WHITE_LIST,
		MARKING_INVALID // This must be at the end!
	};

	enum Decision
	{
		DECISION_INVALID,
		DECISION_IDEMPOTENT,
		DECISION_REQUEST_SPECIFIC,
		DECISION_PROCESS_SPECIFIC,
		DECISION_GLOBAL_CHANGE
	};

	enum AnalysisDepth
	{
		DEPTH_NONE,
		DEPTH_FIRST_LEVEL_IPC,
		DEPTH_ALL
	};

  typedef struct
  {
    uint64_t id;
    std::string module;
  } IPC_SITE_ID;

  static Regex *FuserPatternRegex = new Regex(FUSER_FUNC_PATTERN);

  static std::string decisionToStr(enum Decision decision)
  {
  	switch (decision)
  	{
  		case DECISION_IDEMPOTENT:
  				return "IDEMPOTENT";

  		case DECISION_REQUEST_SPECIFIC:
  				return "REQUEST_SPECIFIC";

  		case DECISION_PROCESS_SPECIFIC:
  				return "PROCESS_SPECIFIC";

  		case DECISION_GLOBAL_CHANGE:
  				return "GLOBAL";

  		default:
  				return "INVALID";
  	}
  	return "INVALID"; 
  }

  static std::string markingToStr(enum Marking mark)
  {
  	switch (mark)
  	{
  		case REQUEST_LOCAL:
  				return "REQUEST_LOCAL";

  		case PROCESS_LOCAL:
  				return "PROCESS_LOCAL";

  		case GLOBAL_VALUE:
  				return "GLOBAL_VALUE";

  		case WHITE_LIST:
  				return "WHITE_LIST";

  		default:
  				return "MARKING_INVALID";
  	}
  	return "MARKING_INVALID"; 
  }

  static std::string getMinixModulePrefix(const Function* func)
  {
	  Regex *patternRegex = new Regex(MINIX_MODULE_PREFIX_PATTERN, 0);
	  SmallVector<StringRef, 8> matches;
	  patternRegex->match(func->getName(), &matches);

	  if (0 != matches.size())
	  {
	    return matches[0];
	  }
	  DEBUG(errs() << "Minix module prefix: NO match for:" << func->getName() << "\n");
	  return "";
  }

static IPC_SITE_ID* getFuseIPCSiteID(CallInst *CI)
{
  MDNode *N = CI->getMetadata(FUSE_IPC_SITE_ID_KEY);
  IPC_SITE_ID *site_id = new IPC_SITE_ID();
  if (NULL == site_id)
  {
    errs() << "Out of memory. Cannot make space for IPC_SITE_ID\n";
  }
  if (N) {
    ConstantInt *I = dyn_cast_or_null<ConstantInt>(N->getOperand(0));
    MDString    *S = dyn_cast_or_null<MDString>(N->getOperand(1));

    site_id->id = I->getZExtValue();
    site_id->module = S->getString();

    return site_id;
  }
  else
  {
      DEBUG(errs() << "WARNING: FUSE_IPC_SITE_ID_KEY not found\n");
      return NULL;
  }
}

  // static Decision getCumulativeDecision(std::map<const Value*, enum Marking> *cumulativeModifiedMap)
  // {
  // 	enum Decision cumlDecision = DECISION_INVALID;
  // 	for(std::map<const Value*, enum Marking>::iterator I = cumulativeModifiedMap->begin(), E = cumulativeModifiedMap->end();
  //       I != E; I++)
  // 	{
  // 		enum Decision currDecision = (*I).second;
  // 		if (currDecision > cumlDecision)
  // 		{
  // 			cumlDecision = currDecision;
  // 		}
  // 	}

  // 	return cumlDecision;
  // }

}
#endif
