#ifndef LTCKPT_STATIC_RECOVERY_PASS_COMMON_H
#define LTCKPT_STATIC_RECOVERY_PASS_COMMON_H

#define REGEX_DECISION_MAP_LINE_FORMAT      "([a-zA-Z0-9\\.]*):([0-9]+)[[:blank:]]+([0-4])"
#define REGEX_KC_DECISION_MAP_LINE_FORMAT   "([0-9]+)[[:blank:]]+([1-4])[[:blank:]]+([0-1])"
#define REGEX_KC_EXCLUSION_MAP_LINE_FORMAT  "([0-9]+)"
#define REGEX_KERNEL_CALLERS_LINE_FORMAT    "([a-zA-Z_]+)"
#define KERNEL_CALL_BASE            0x600

#include "fuseminix_common.h"
#include "InputLoader.h"

typedef struct {
  enum Decision decision;
  bool suicide;
} kernelcall_decision_t;

std::string compareModuleNames(std::string modName1, std::string modName2)
{
	Regex *extractorRegex = new Regex("(.*/)?([a-z]+)(\\..*)");
    SmallVector<StringRef, 5> matches;

    std::string exModName1 = "";
	if (false != extractorRegex->match(modName1, &matches))
    {
      if (4 != matches.size())
      {
        DEBUG(errs() << "Not enough matches! size of matches: " << matches.size() << "\n");
        return "";
      }
      exModName1 = matches[2];
      DEBUG(errs() << "Module name found: " << exModName1 << "\n");
    }

    matches.clear();
    std::string exModName2 = "";
    if (false != extractorRegex->match(modName2, &matches))
    {
      if (4 != matches.size())
      {
        DEBUG(errs() << "Not enough matches! size of matches: " << matches.size() << "\n");
        return "";
      }
      exModName2 = matches[2];
    }

    if (std::string::npos == (exModName1.find(exModName2)))
    {
    	return "";
    }

    return exModName1;
}

class DecisionMapInputLoader : public InputLoader
{
private:
  std::string curr_module_name;
  std::map<uint64_t, long> inputMap;
  void load();
public:
  DecisionMapInputLoader(std::string curr_module_name);
  void getCallSiteDecisionMap(std::map<uint64_t, enum Decision> &recoveryDecisions);
  void getCallSiteSuicideMap(std::map<uint64_t, long> &suicideMap);
};

class KCDecisionMapInputLoader : public InputLoader
{
private:
  std::map<long, kernelcall_decision_t> inputMap;
  void load();
public:
  KCDecisionMapInputLoader();
  void getKernelCallDecisionMap(std::map<long, kernelcall_decision_t> &kernelcallDecisions);
};

class KCExclusionMapInputLoader : public InputLoader
{
private:
  std::set<long> inputMap;
  void load();
public:
  KCExclusionMapInputLoader();
  void getKernelCallExclusionSet(std::set<long> &kernelcallExclusionSet);
};

class IdempKCCallersInputLoader : public InputLoader
{
private:
   std::set<std::string> inputSet;
public:
   IdempKCCallersInputLoader();
   void getIdempotentKernelCallers(std::set<std::string> &idempKernelCallersSet);
};

IdempKCCallersInputLoader::IdempKCCallersInputLoader() : InputLoader(new Regex(REGEX_KERNEL_CALLERS_LINE_FORMAT)) {}

void IdempKCCallersInputLoader::getIdempotentKernelCallers(std::set<std::string> &idempKernelCallersSet)
{
  if (0 != this->acceptedLines.size())
    {
      for(unsigned i=0; i < acceptedLines.size(); i++)
      {
        if (idempKernelCallersSet.count(acceptedLines[i]) == 0)
        {
          idempKernelCallersSet.insert(acceptedLines[i]);
        }
      }
    }
    return;
}

KCExclusionMapInputLoader::KCExclusionMapInputLoader() : InputLoader (new Regex(REGEX_KC_EXCLUSION_MAP_LINE_FORMAT)) {}

void KCExclusionMapInputLoader::load()
{
  inputMap.clear();
  if (0 != this->acceptedLines.size())
    {
      for(unsigned i=0; i < acceptedLines.size(); i++)
      {
        long exclusionNum;

        SmallVector<StringRef, 2> matches;
        Regex *regexAcceptor  = new Regex(REGEX_KC_EXCLUSION_MAP_LINE_FORMAT);
        if (false != regexAcceptor->match(acceptedLines[i], &matches))
        {
          if (2 != matches.size())
          {
            DEBUG(errs() << "Not enough matches! size of matches: " << matches.size() << "\n");
            return;
          }
          exclusionNum = (uint64_t) std::strtoull(((std::string)matches[1]).c_str(), NULL, 0);
          
          DEBUG(errs() << "Loaded input map line: " << "exclusion kernelcall_num: "
                 << exclusionNum << "\n");
          if (0 == inputMap.count(exclusionNum))
          {
            inputMap.insert(exclusionNum);
          }
        }
        else
        {
          DEBUG(errs() << "WARNING: Error in loading input mapping from accepted line: " << acceptedLines[i] << "\n");
        }
      }
    }
    return;
}

void KCExclusionMapInputLoader::getKernelCallExclusionSet(std::set<long> &kernelcallExclusionSet)
{
  load();
  for(std::set<long>::iterator I=inputMap.begin(), E=inputMap.end(); I != E; I++)
  {
    kernelcallExclusionSet.insert(*I);
  }
  return;
}

KCDecisionMapInputLoader::KCDecisionMapInputLoader() : InputLoader (new Regex(REGEX_KC_DECISION_MAP_LINE_FORMAT)) {}

void KCDecisionMapInputLoader::load()
{
  inputMap.clear();
  if (0 != this->acceptedLines.size())
    {
      for(unsigned i=0; i < acceptedLines.size(); i++)
      {
        long kernelcall_num = 0;
        enum Decision decision;
        bool suicide;

        // TODO: Change the match numbers!!!!!
        SmallVector<StringRef, 3> matches;
        Regex *regexAcceptor  = new Regex(REGEX_KC_DECISION_MAP_LINE_FORMAT);
        if (regexAcceptor->match(acceptedLines[i], &matches))
        {
          if (4 != matches.size())
          {
            DEBUG(errs() << "Not enough matches! size of matches: " << matches.size() << "\n");
            return;
          }
          kernelcall_decision_t theDecision;
          kernelcall_num = (uint64_t) std::strtoull(((std::string)matches[1]).c_str(), NULL, 0);
          theDecision.decision = (enum Decision) (std::strtol(((std::string)matches[2]).c_str(), NULL, 0));
          theDecision.suicide = (bool) (std::strtol(((std::string)matches[3]).c_str(), NULL, 0));
          
          DEBUG(errs() << "Loaded input map line: " << "kernelcall_num: "
                 << kernelcall_num << " value: " << theDecision.decision << "suicide? " << theDecision.suicide << "\n");
          if (0 == inputMap.count(kernelcall_num))
          {
            inputMap.insert(std::make_pair(kernelcall_num, theDecision));
          }

        }
        else
        {
          DEBUG(errs() << "WARNING: Error in loading input mapping from accepted line: " << acceptedLines[i] << "\n");
        }
      }
    }
    return;
}

void KCDecisionMapInputLoader::getKernelCallDecisionMap(std::map<long, kernelcall_decision_t> &kernelcallDecisions)
{
  load();
  for(std::map<long, kernelcall_decision_t>::iterator I=inputMap.begin(), E=inputMap.end(); I != E; I++)
  {
    kernelcallDecisions.insert(std::make_pair(I->first, I->second));
  }
  return;
}

DecisionMapInputLoader::DecisionMapInputLoader(std::string curr_module_name) : InputLoader (new Regex(REGEX_DECISION_MAP_LINE_FORMAT)) 
{
  this->curr_module_name = curr_module_name;
}

void DecisionMapInputLoader::load()
{
  inputMap.clear();
  if (0 != this->acceptedLines.size())
    {
      for(unsigned i=0; i < acceptedLines.size(); i++)
      {
        std::string site_module_name = "";
        uint64_t site_id = 0;
        long num = 0;

        SmallVector<StringRef, 5> matches;
        Regex *regexAcceptor  = new Regex(REGEX_DECISION_MAP_LINE_FORMAT);
        if (false != regexAcceptor->match(acceptedLines[i], &matches))
        {
          if (4 != matches.size())
          {
            DEBUG(errs() << "Not enough matches! size of matches: " << matches.size() << "\n");
            return;
          }
          site_module_name = matches[1];
          site_id = (uint64_t) std::strtoull(((std::string)matches[2]).c_str(), NULL, 0);
          num = std::strtol(((std::string)matches[3]).c_str(), NULL, 0);
          DEBUG(errs() << "Loaded input map line: " << "module: " << site_module_name << " site_id: "
                 << site_id << " value: " << num << "\n");

          if ("" != compareModuleNames(site_module_name, curr_module_name))
          {
            DEBUG(errs() << "match: " << site_module_name << " siteid: " << site_id << " val: " << num << "\n");
            inputMap.insert(std::make_pair(site_id, num));
          }
          else
          {
            DEBUG(errs() << "no match: " << site_module_name << "\n");
          }
        }
        else
        {
          DEBUG(errs() << "WARNING: Error in loading input mapping from accepted line: " << acceptedLines[i] << "\n");
        }
      }
    }
    return;
}

void DecisionMapInputLoader::getCallSiteDecisionMap(std::map<uint64_t, enum Decision> &recoveryDecisions)
{
  load();
  for(std::map<uint64_t, long>::iterator I=inputMap.begin(), E=inputMap.end(); I != E; I++)
  {
    recoveryDecisions.insert(std::make_pair(I->first, (enum Decision)I->second));
  }
    return;
}

void DecisionMapInputLoader::getCallSiteSuicideMap(std::map<uint64_t, long> &suicideMap)
{
  load();
  for(std::map<uint64_t, long>::iterator I=inputMap.begin(), E=inputMap.end(); I != E; I++)
  {
    suicideMap.insert(std::make_pair(I->first, I->second));
  }
  return;
}



#endif
