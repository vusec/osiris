/******************************
* Author : Koustubha Bhat
* Jul-Oct 2014
* Vrije Universiteit, Amsterdam.
*******************************/
#ifndef FUSE_MINIX_COMMON_H
#define FUSE_MINIX_COMMON_H

#include <fstream>
#include <deque>

#define SENDREC_FUNC_NAME           "ipc_sendrec"
#define ASYNSEND3_FUNC_NAME         "asynsend3"
#define SEFRECEIVE_FUNC_NAME        "sef_receive_status"
#define GET_WORK_FUNC_NAME          "get_work"
#define MX_PREFIX                   "mx_"
#define PREFIX_PATTERN              "mx_.*"
#define MINIX_MODULE_PREFIX_PATTERN "mx_[^_]*"
#define MINIX_MODULE_PREFIX_PATTERN_EXTENDED "[a-zA-Z_]*mx_[^_]*"
#define CLONE_FUNC_NAME_PREFIX      "clone_"
#define FUSER_FUNCTION_PREFIX		    "fuser_"
#define REPLACEMENT_CALL_PREFIX		  "calldest_"
#define NUM_TASKCALL_ENDPOINT_TYPES 9
#define MINIX_STRUCT_MESSAGE        "noxfer_message"
#define MINIX_STRUCT_MESSAGE_M_TYPE "m_type"
#define MINIX_IPCVECS_GLOBALVAR_PATTERN       ".*_minix_ipcvecs"
#define SWITCH_DEFAULT_CASE_NUMBER  -1
#define INVALID_IPC_ENDPOINT        -1
#define INVALID_IPC_MTYPE           -1

namespace llvm {

typedef int IPCEndpoint;
typedef int IPCMType;

enum Probe
{
  callers_sef_receive_status,
  callers_ipc_sendrec,
  inlined_functions,
  cloned_functions
};

enum SendrecCallSiteEndpointType
{
  ENDPOINT_CONST,
  ENDPOINT_NONCONST,
  UNKNOWN
};

enum FunctionSplitType
{
  SPLIT_BY_ENDPOINT,
  SPLIT_BY_M_TYPE
};

class IPCInfo
{
public:
  IPCEndpoint srcEndpoint;
  std::vector<IPCEndpoint> destEndpoints;
  IPCMType mtype;
};

static std::map<IPCEndpoint, std::string> endpointModuleMap;
static std::map<std::string, IPCEndpoint> moduleEndpointMap;

int getCallers(DSAUtil *dsau, const Function *F, std::vector<Function*> &callers)
{
  if (NULL == F)
  {
    DEBUG(errs() << "Arguments shall NOT be NULL." << "\n");
    return -1;
  }
  if (NULL == dsau)
  {
    return -1;
  }
  DSAUtil::FuncSetTy callersFuncSet;
  DSAUtil::FuncVecTy callersFuncVec;
  dsau->getCallers(F, callersFuncSet);
  DSAUtil::FuncSetTy::iterator it;
  for(it = callersFuncSet.begin(); it != callersFuncSet.end(); ++it)
  {
    callersFuncVec.push_back(*it);
  }
  unsigned i = 0;
  for(i=0; i < callersFuncVec.size(); i++)
  {
    callers.push_back((Function*)callersFuncVec[i]);
  }
  return i;
}

std::string getMinixModulePrefix(Function* func)
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


bool fileExists(std::string fileName)
{
  std::ifstream file(fileName.c_str());
  if (NULL == file)
  {
    return false;
  }
  file.close();
  return true;
}

void loadFunctionsToAnalyzeAfterFuse(std::string afterFuseFuncsFilename, std::vector<std::string> &funcNames)
{
  std::ifstream configFile(afterFuseFuncsFilename.c_str());
  std::string line;
  Regex *lineFormatRegex = new Regex("[a-zA-Z0-9_]+");

  while(std::getline(configFile, line))
  {
    if (0 == line.length())
    {
      DEBUG(errs() << "line length is zero.. \n");
      continue;
    }

    DEBUG(errs() << "line: " << line << "\n");

    if (false == lineFormatRegex->match(line, NULL))
    {
      continue;
    }

    funcNames.push_back(line);
  }
  configFile.close();
  return;
}

bool loadEndpointMapping(std::string configFileName)
{
  std::ifstream configFile(configFileName.c_str());
  std::string line;
  Regex *lineFormatRegex = new Regex("[0-9]+ *= *[a-zA-Z]+");
  Regex *indexRegex = new Regex("[0-9]+");
  Regex *valueRegex = new Regex("[a-zA-Z]+");
  IPCEndpoint endpoint;
  std::string   moduleName = "";
  bool wasSuccess = true;

  while(std::getline(configFile, line))
  {
    if (0 == line.length())
    {
      DEBUG(errs() << "line length is zero.. \n");
      continue;
    }

    DEBUG(errs() << "line: " << line << "\n");

    if (false == lineFormatRegex->match(line, NULL))
    {
      continue;
    }

    DEBUG(errs() << "correct format line: " << line << "\n");

    SmallVector<StringRef, 8> indexMatches, valueMatches;
    indexRegex->match(line, &indexMatches);
    valueRegex->match(line, &valueMatches);

    if (0 == indexMatches.size() || 0 == valueMatches.size())
    {
      wasSuccess = false;
      break;
    }

    endpoint = atoi(indexMatches[0].str().c_str());
    moduleName = valueMatches[0];
    endpointModuleMap.insert(std::pair<IPCEndpoint, std::string>(endpoint, moduleName));
    moduleEndpointMap.insert(std::pair<std::string, IPCEndpoint>(moduleName, endpoint));
  }

  configFile.close();
  DEBUG(errs() << "Closed config file.\n");
  return wasSuccess;
}

bool getEndpointMapping(IPCEndpoint endpoint, std::string &modulePrefix)
{
   if (1 == endpointModuleMap.count(endpoint))
   {
    modulePrefix = endpointModuleMap.find(endpoint)->second;
    modulePrefix = (std::string) MX_PREFIX + modulePrefix;
    DEBUG(errs() << "modulePrefix for [" << endpoint << "] as per mapping: [" << modulePrefix << "]\n");
    return true;
   }
   return false;
}

bool getEndpointMapping(std::string modulePrefix, IPCEndpoint &endpoint)
{
  if (std::string::npos == modulePrefix.find(MX_PREFIX))
  {
    return false;
  }
  std::string srchString = modulePrefix.substr(3);
  if (1 == moduleEndpointMap.count(srchString))
  {
    endpoint = moduleEndpointMap.find(srchString)->second;
    DEBUG(errs() << "endpoint for [" << srchString << "] as per mapping: [" << endpoint << "]\n");
    return true;
  } 
  return false;
}

bool getAllEndpoints(std::vector<IPCEndpoint> &allEndpoints)
{
  if(0 == endpointModuleMap.size())
  {
    DEBUG(errs() << "endpointModuleMap not initialized yet.\n");
    return false;
  }
  for(std::map<IPCEndpoint, std::string>::iterator IE = endpointModuleMap.begin(), EE = endpointModuleMap.end();
        IE != EE; IE++)
  {
    if ((*IE).first < 100)
    {
      allEndpoints.push_back((*IE).first);
    }
  }
  return true;
}

void pushCallSites(std::deque<CallSite> &queue, std::vector<CallSite> &callSites)
{
  if (0 < callSites.size())
  {
    for(unsigned i=0; i < callSites.size(); i++)
    {
      if (callSites[i].getCalledFunction()->isIntrinsic())
      {
        continue;
      }
      DEBUG(errs() << "pushed callsite (called function: " << callSites[i].getCalledFunction()->getName() 
            << ") at caller: " << callSites[i].getCaller()->getName() << "\n");
      queue.push_back(callSites[i]);
    }
  }
  return;
}

void pushCallSites(std::vector<CallSite> &destination, std::vector<CallSite> &source)
{
  if (0 < source.size())
  {
    for(unsigned i=0; i < source.size(); i++)
    {
      destination.push_back(source[i]);
    }
  }
  return;
}

}
#endif
