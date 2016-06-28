/******************************
* Author : Koustubha Bhat
* Date   : 13-Feb-2015
* Vrije Universiteit, Amsterdam.
*******************************/
#ifndef LTCKPT_STATIC_RECOVERY_INPUT_LOADER_H
#define LTCKPT_STATIC_RECOVERY_INPUT_LOADER_H

#define INPUT_FUNCTIONS_LINE_FORMAT         "[a-zA-Z_0-9]+"
#define ANNOTATIONS_CATEGORY_LINE_FORMAT    "\\[[A-Z_]+\\]"  // entry line or category indicator line
#define ANNOTATIONS_ENTRY_LINE_FORMAT       "[a-zA-Z0-9_]+|\\*=[a-zA-Z0-9_,]+"

#include <fstream>

using namespace llvm;

namespace llvm
{

class InputLoader
{
public:
  InputLoader(Regex *lineRegex);
  bool accept(std::string line);
  unsigned read(std::string fileName);

protected:
  Regex *lineRegex;
  std::vector<std::string> acceptedLines;
};

InputLoader::InputLoader(Regex *lineRegex)
{
  std::string error;
  if (false == lineRegex->isValid(error))
  {
    DEBUG(errs() << "WARNING: The regex is invalid: " << error << "\n");
    return;
  }
  this->lineRegex = lineRegex;
}

bool InputLoader::accept(std::string line)
{
  if (NULL == lineRegex)
  {
    return false;
  }
  if (!lineRegex->match(line, NULL))
  {
    DEBUG(errs() << "Didn't match regex: " << line << "\n");
    return false;
  }
  this->acceptedLines.push_back(line);
  return true;
}

unsigned InputLoader::read(std::string fileName)
{
  std::string line = "";
  unsigned numLinesAccepted = 0;

  if ("" == fileName)
  {
    errs() << "Filename or the InputLoader are invalid.\n";
    return 0;
  }
  std::ifstream file(fileName.c_str());
  if (file.fail())
  {
    errs() << "WARNING: Functions list file : " << fileName << " does not exist.\n";
    return 0;
  }

  while(std::getline(file, line))
  {
    if (0 == line.length())
    {
      continue;
    }
    // DEBUG(errs() << "line: " << line << "\n");
    if (false == this->accept(line))
    {
      DEBUG(errs() << "Didn't accept: " << line << "\n");
      continue;
    }
    numLinesAccepted++;
  }
  file.close();
  return numLinesAccepted;
}

class FunctionsInputLoader : public InputLoader
{
public:
  FunctionsInputLoader();
  void getFunctionNames(std::vector<std::string> &functionNames);
};

FunctionsInputLoader::FunctionsInputLoader() : InputLoader(new Regex(INPUT_FUNCTIONS_LINE_FORMAT))
{
}

void FunctionsInputLoader::getFunctionNames(std::vector<std::string> &functionNames)
{
  if (0 != this->acceptedLines.size())
  {
    functionNames = acceptedLines;
  }
}

class AnnotationsInputLoader : public InputLoader
{
public:
  AnnotationsInputLoader();
  bool getAnnotationsMap(std::map<std::string, std::map<std::string, enum Marking> > *annotationsMap);

private:
  enum Marking getCategory(std::string line);
};

AnnotationsInputLoader::AnnotationsInputLoader() : InputLoader(new Regex(std::string(ANNOTATIONS_CATEGORY_LINE_FORMAT)
									                                                       + (std::string) "|"
                                                                         + std::string(ANNOTATIONS_ENTRY_LINE_FORMAT)))
{
}

bool AnnotationsInputLoader::getAnnotationsMap(std::map<std::string, std::map<std::string, enum Marking> > *annotationsMap)
{
  if (NULL == annotationsMap)
  {
    return false;
  }
  Regex *categoryRegex = new Regex(ANNOTATIONS_CATEGORY_LINE_FORMAT);
  Regex *entryRegex = new Regex(ANNOTATIONS_ENTRY_LINE_FORMAT);

  enum Marking currCategory = MARKING_INVALID;

  if (0 != this->acceptedLines.size())
  {
    for(unsigned i=0; i < acceptedLines.size(); i++)
    {
      std::string currLine = acceptedLines[i];

      if (categoryRegex->match(currLine, NULL))
      {
        currCategory = getCategory(currLine);
        continue;
      }
      if (currCategory != MARKING_INVALID)
      {
        if (entryRegex->match(currLine, NULL))
        {
          unsigned separator_pos = currLine.find("=");
          unsigned lineLength = currLine.length();
          std::string modName = currLine.substr(0, separator_pos);
          std::string valuesString = currLine.substr(separator_pos+1, lineLength - separator_pos);
          std::vector<std::string> values;
          PassUtil::parseStringListOpt(values, valuesString, ",");

          std::map<std::string, enum Marking> *entry = NULL;
          if (0 == annotationsMap->count(modName))
          {
            std::map<std::string, enum Marking> *annotationEntry = new std::map<std::string, enum Marking>();

            annotationsMap->insert(std::make_pair(modName, *annotationEntry));
          }
          entry = &annotationsMap->find(modName)->second;
          for(unsigned i=0; i < values.size(); i++)
          {
            if (0 == entry->count(values[i]))
            {
              entry->insert(make_pair(values[i], currCategory));
	            DEBUG(errs() << "Annotation - loading value: " << modName << ": " << values[i] << " => " 
								                                                                << currCategory << "\n");
            }
            else
            {
              entry->find(values[i])->second = currCategory;
      	      DEBUG(errs() << "Annotation - loading marking: " << modName << ": " << values[i] << " => " 
      								     << currCategory << "\n");
            }
          }
        }
      }
    } // for ends
  }
  return true; 
}

enum Marking AnnotationsInputLoader::getCategory(std::string line)
{
  if ("" == line)
  {
    return MARKING_INVALID;
  }
  if (std::string::npos != line.find("[REQUEST_LOCAL]"))
  {
    return REQUEST_LOCAL;
  }
  if (std::string::npos != line.find("[PROCESS_LOCAL]"))
  {
    return PROCESS_LOCAL;
  }
  if (std::string::npos != (line.find("[GLOBAL]")))
  {
    return GLOBAL_VALUE;
  }
  if (std::string::npos != (line.find("[WHITE_LIST]")))
  {
    return WHITE_LIST;
  }
  return MARKING_INVALID;
}

}
#endif
