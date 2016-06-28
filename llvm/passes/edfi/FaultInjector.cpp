#include <algorithm>
#include <vector>
#include <map>
#include <set>
#include <list>

#include <pass.h>

#if LLVM_VERSION >= 37
#include <llvm/IR/CFG.h>
#include <llvm/IR/ValueMap.h>
#else
#include <llvm/Support/CFG.h>
#include <llvm/ADT/ValueMap.h>
#endif
#include <llvm/Transforms/Utils/ValueMapper.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Transforms/Utils/UnifyFunctionExitNodes.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#include <assert.h>
#include <sys/time.h>
#include <stdio.h>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "FaultInjector.h"
#include "MapFile.h"

#define EDFI_XSTR(s) EDFI_STR(s)
#define EDFI_STR(s) #s

#define FAULT_PRINTF_SZ 100
char edfi_stat_buff[FAULT_PRINTF_SZ];
#define EDFI_STAT_PRINTER(...) assert(snprintf(edfi_stat_buff, FAULT_PRINTF_SZ, __VA_ARGS__) < FAULT_PRINTF_SZ - 1); outs() << edfi_stat_buff;
#define EDFI_STATIC_FUNCTIONS_SECTION "edfi_functions"
#define EDFI_DEFAULT_SWITCH_FLAG_MAP_ENTRIES { \
    "edfi_functions/.*_wrapper/-1" \
    , "edfi_functions/.*_onfdp.*/-1" \
    , "edfi_functions/.*/0" \
    }

#include <edfi/common.h>

using namespace llvm;

static cl::opt<int>
func_min("fault-func-min",
        cl::desc("temp hack"),
        cl::init(0), cl::NotHidden, cl::ValueRequired);

static cl::opt<int>
func_max("fault-func-max",
        cl::desc("temp hack"),
        cl::init(100000000), cl::NotHidden, cl::ValueRequired);

struct Random_BB_FIF_Parser : public cl::BASE_PARSER<std::pair<unsigned, float> > {
    // parse - Return true on error.
    bool parse(cl::Option &O, StringRef ArgName, const std::string &ArgValue, std::pair<unsigned, float> &Val);
};

static cl::opt<bool>
skip_all("fault-skip-all",
        cl::desc("Fault Injector: skip fault injection and return immediately"),
        cl::init(0), cl::NotHidden);

static cl::opt<std::pair<unsigned, float>, false, Random_BB_FIF_Parser>
random_BB_FIF("fault-randombb-fif", cl::desc("R/fif: R=number of randomly selected basic blocks, fif=fault impact factor [0..1]"),
        cl::init(std::pair<unsigned, float>(0, 0.0)), cl::NotHidden, cl::ValueRequired);

struct Name_FIF_Parser : public cl::BASE_PARSER<std::map<std::string, float> >{
    // parse - Return true on error.
    bool parse(cl::Option &O, StringRef ArgName, const std::string &ArgValue, std::map<std::string, float > &Val);
};

static cl::opt<std::map<std::string, float>, false, Name_FIF_Parser>
functions_FIF("fault-function-fif", cl::desc("f_1/fif_1;f_2/fif_2;...;f_N/fif_N: fif_X=fault impact factor for basic blocks contained in function f_X [0..1]"),
        cl::init(std::map<std::string, float>()), cl::NotHidden, cl::ValueRequired);

static cl::opt<std::map<std::string, float>, false, Name_FIF_Parser>
functioncallers_FIF("fault-functioncallers-fif", cl::desc("f_1/fif_1;f_2/fif_2;...;f_N/fif_N: fif_X=fault impact factor for basic blocks that contain a call to function f_X [0..1]"),
        cl::init(std::map<std::string, float>()), cl::NotHidden, cl::ValueRequired);

static cl::opt<std::map<std::string, float>, false, Name_FIF_Parser>
modules_FIF("fault-module-fif", cl::desc("m_1/fif_1;m_2/fif_2;...;m_N/fif_N: fif_X=fault impact factor for basic blocks contained in module m_X [0..1]"),
        cl::init(std::map<std::string, float>()), cl::NotHidden, cl::ValueRequired);

static prob_opt
global_FIF("fault-global-fif",
        cl::desc("Fault Injector: Default fault impact factor for all basic blocks that are not assigned a FIF otherwise. Default: 1.0"),
        cl::init(1.0), cl::NotHidden, cl::ValueRequired);

static cl::opt<int>
rand_seed("fault-rand-seed",
        cl::desc("Fault Injector: random seed value. when '0', current time is used. Default value: 123"),
        cl::init(123), cl::NotHidden, cl::ValueRequired);

static cl::opt<int>
random_BB_FIF_seed("fault-randombb-fif-seed",
        cl::desc("Fault Injector: random seed value for choosing basic blocks for -fault-randombb-fif. Default value: same as -fault-rand-seed value"),
        cl::NotHidden);

static cl::opt<int>
rand_reseed_per_function("fault-rand-reseed-per-function",
        cl::desc("Fault Injector: reseed random number generator on a per function basis (based on the function name). Useful to preserve per-function randomness across runs. Default value: false"),
        cl::init(false), cl::NotHidden, cl::ValueRequired);

static cl::list<std::string>
FunctionNames("fault-functions",
        cl::desc("Fault Injector: specify comma separated list of functions to be instrumented (empty = all functions)"), cl::NotHidden, cl::CommaSeparated);

static cl::list<std::string>
SkipFunctionNames("fault-skip-functions",
        cl::desc("Fault Injector: specify comma separated list of functions to exclude from instrumentation"), cl::NotHidden, cl::CommaSeparated);

static cl::list<std::string>
ModuleNames("fault-modules",
        cl::desc("Fault Injector: specify comma separated list of regular expressions to match modules (source file paths) to be instrumented (empty = all modules)"), cl::NotHidden, cl::CommaSeparated);

static cl::list<std::string>
FaultTypeNames("fault-types",
        cl::desc("Fault Injector: specify comma separated list of fault types to be injected"), cl::NotHidden, cl::CommaSeparated);

prob_opt
prob_default("fault-prob-default",
        cl::desc("Fault Injector: default probability for all faults that are not specified. This also overrides indvidual default values."),
        cl::NotHidden);

static cl::opt<bool>
do_debug("fault-debug",
        cl::desc("Fault Injector: print debug information"),
        cl::init(0), cl::NotHidden);

static cl::opt<std::string>
debug_assembly("fault-debug-outfile",
        cl::desc("filename to write readable assembly to, iff pass finishes successfully, for debug purposes."),
        cl::NotHidden);

static cl::opt<bool>
do_unconditionalize("fault-unconditionalize",
        cl::desc("Fault Injector: use direct test rather than edfi_onfdp_p to allow running unconditionalize pass afterwards"),
        cl::init(0), cl::NotHidden);

static cl::opt<bool>
do_inline_profiling("inline-profiling",
        cl::desc("Update edfi_context->bb_num_executions in the FDP itself, allow profiling while using unconditionalize"),
        cl::init(0), cl::NotHidden);

static cl::opt<std::string>
fdp_varname("fault-fdp-varname",
        cl::desc("Fault Injector: specify variable name where FDP callback is stored (empty = edfi_onfdp_p)"),
	cl::init("edfi_onfdp_p"), cl::NotHidden);

struct Fault_Selector_Parser : public cl::BASE_PARSER<std::set<std::pair<std::string, int> > > {
    // parse - Return true on error.
    bool parse(cl::Option &O, StringRef ArgName, const std::string &ArgValue, std::set<std::pair<std::string, int> > &Val);
};

static cl::opt<std::set<std::pair<std::string, int> >, false, Fault_Selector_Parser>
fault_selector("fault-selector", cl::desc("Fault Injector: specify comma separated list of locations where to inject faults"),
        cl::init(std::set<std::pair<std::string, int> >()), cl::NotHidden, cl::ValueRequired);

static cl::opt<bool>
statistics_only("fault-statistics-only",
        cl::desc("Fault Injector: Don't actually inject faults, but only gather runtime statistics."),
        cl::init(0), cl::NotHidden);

static cl::opt<bool>
noDFTs("fault-noDFTs",
        cl::desc("Fault Injector: Don't use dynamic fault triggers, but always execute faults when faulty execution is enabled."),
        cl::init(0), cl::NotHidden);

static cl::opt<bool>
noStatisticsInstructions("fault-no-statistics-instructions",
        cl::desc("Fault Injector: TO BE REMOVED: Don't inject instructions that increment stat counters (= default)."),
        cl::init(1), cl::NotHidden);

static cl::opt<bool>
noDFLs("fault-noDFLs",
        cl::desc("Fault Injector: Don't call dynamic fault loggers."),
        cl::init(0), cl::NotHidden);

static cl::opt<bool>
dsnCompat("fault-dsn-compat",
        cl::desc("Fault Injector: Backward compatibility with DSN 2013 evaluation."),
        cl::init(0), cl::NotHidden);

static cl::opt<bool>
atcCompat("fault-atc-compat",
        cl::desc("Fault Injector: Backward compatibility with Usenix ATC 2013 evaluation."),
        cl::init(0), cl::NotHidden);

static cl::opt<bool>
oneFaultPerBlock("fault-one-per-block",
        cl::desc("Fault Injector: Inject one fault per block."),
        cl::init(0), cl::NotHidden);

static cl::opt<bool>
doLoopHeaderSwitching("fault-do-loop-header-switching",
        cl::desc("Fault Injector: At each loop header, jump from normal to faulty execution, when switched to faulty execution mode. Normally only done at function entry. Depends on -reg2mem (default=0)."),
        cl::init(0), cl::NotHidden);

static cl::list<std::string>
switchFlagMap("fault-switch-flag-map",
    cl::desc("FaultInjector: section1_regex/function1_regex/flag,section2_regex/function2_regex/flag,... temporarily start/stop fault injection when entering particular functions."),
    cl::ZeroOrMore, cl::CommaSeparated, cl::NotHidden, cl::ValueRequired);
std::string switchFlagMapDefaultOptions[] = EDFI_DEFAULT_SWITCH_FLAG_MAP_ENTRIES;

#if LLVM_VERSION == 28
#define RF_NoModuleLevelChanges false
#define RF_IgnoreMissingEntries false
#endif

namespace llvm{

PASS_COMMON_INIT_ONCE();

    FaultInjector::FaultInjector() : ModulePass(ID) {}

    Regex *matchesRegex(DIDescriptor &DID, std::vector<Regex*> &regexes);
    BranchInst *createFlagBasedBranchInst(Module &M, BasicBlock *IfTrue, BasicBlock *IfFalse, Value *flag, Constant *rhv, BasicBlock *InsertAtEnd);
    void setBranchWeight(Module &M, BranchInst *branch, uint32_t prob_true, uint32_t prob_false);
    Constant *getStructMember(Module &M, GlobalVariable *GV, int index);
    bool is_edfi_section(Function *F);
    bool can_instrument_function(Function *F, std::vector<Regex*> &regexes);
    void replace_BB_FIF_key(std::map<BasicBlock *, float> &BB_FIF_map, BasicBlock *old_key, BasicBlock *new_key);
    void replaceUsesOfStructElement(GlobalVariable *Struct, Constant *NewElement, int index);
    void getAllocaInfo(Function *F, Instruction **allocaInsertionPoint, Instruction **firstNonAllocaInst);
    void initRegexMap(std::map<std::pair<Regex*, Regex*>, int> &regexMap, std::vector<std::pair<Regex*, Regex*> > &regexList, cl::list<std::string> &stringList);
    void switchFlag(std::map<std::pair<Regex*, Regex*>, int> &regexMap, std::vector<std::pair<Regex*, Regex*> > &regexList, Function *F, GlobalVariable *enabled_var, GlobalVariable *enabled_version_var);

    void FaultInjector::getAnalysisUsage(AnalysisUsage &AU) const{
        AU.addRequired<LoopInfo>();
    }
    
    void addFaultType(SmallVector<FaultType*, 8> &faultTypes, FaultType *faultType) {
	    if (FaultTypeNames.empty()) {
		    faultTypes.push_back(faultType);
		    return;
	    }

	    for (auto it = FaultTypeNames.begin(); it != FaultTypeNames.end(); ++it) {
		    if (*it == faultType->getName()) {
			    faultTypes.push_back(faultType);
			    return;
		    }
	    }
    }

    bool FaultInjector::runOnModule(Module &M) {

        if(skip_all) {
            return false;
        }

        /* seed rand() */
        if(rand_seed == 0){
            struct timeval tp;
            gettimeofday(&tp, NULL);
            rand_seed = (int) tp.tv_usec - tp.tv_sec;
        }

        ConstantInt* Constant0 = ConstantInt::get(M.getContext(), APInt(32, StringRef("0"), 10));
        ConstantInt* Constant1 = ConstantInt::get(M.getContext(), APInt(32, StringRef("1"), 10));

        GlobalVariable* edfi_context_var = M.getNamedGlobal("edfi_context_buff");
        if(!edfi_context_var) {
            errs() << "Error: no edfi_context variable found";
            exit(1);
        } 

        GlobalVariable* enabled_var = M.getNamedGlobal("edfi_faultinjection_enabled");
        if(!enabled_var) {
            errs() << "Error: no edfi_faultinjection_enabled variable found";
            exit(1);
        } 

        GlobalVariable* inject_bb_var = M.getNamedGlobal("edfi_inject_bb");
        if(!inject_bb_var) {
            errs() << "Error: no edfi_inject_bb variable found";
            exit(1);
        } 

        GlobalVariable* enabled_version_var = M.getNamedGlobal("edfi_faultinjection_enabled_version");
        if(!enabled_version_var) {
            errs() << "Error: no edfi_faultinjection_enabled_version variable found";
            exit(1);
        } 

        Constant* fault_type_stats_var = getStructMember(M, edfi_context_var, EDFI_CONTEXT_FIELD_FAULT_TYPE_STATS);
        Constant* execution_counts_var = getStructMember(M, edfi_context_var, EDFI_CONTEXT_FIELD_BB_NUM_EXECUTIONS);

        GlobalVariable* on_fdp_p_var = M.getNamedGlobal(fdp_varname);
        if(!on_fdp_p_var) {
            errs() << "Error: no on_fdp_p_var variable found";
            exit(1);
        }

        GlobalVariable* on_dfl_p_var = M.getNamedGlobal("edfi_onfault_p");
        if(!on_dfl_p_var) {
            errs() << "Error: no on_dfl_p_var variable found";
            exit(1);
        }

        GlobalVariable* edfi_module_name_var = M.getNamedGlobal("edfi_module_name");
        if(!edfi_module_name_var) {
            errs() << "Error: no edfi_module_name variable found";
            exit(1);
        }
        std::string moduleName;
        PassUtil::getModuleName(M, NULL, NULL, &moduleName);
        Constant* moduleNameInitializer = NULL;
        PassUtil::getStringGlobalVariable(M, moduleName, ".str.edfi", "", &moduleNameInitializer);
        edfi_module_name_var->setInitializer(moduleNameInitializer);

        /* initialize regexes for the -fault-modules filter */

        std::vector<Regex*> module_regexes;
        for (unsigned int i = 0; i < ModuleNames.size(); i++) {
            StringRef sr(ModuleNames[i]);
            Regex* regex = new Regex(sr);
            module_regexes.push_back(regex);
        }  

        SmallVector<BasicBlock *, 8> AllBasicBlocks;
        for (Module::iterator it = M.getFunctionList().begin(); it != M.getFunctionList().end(); ++it) {
            Function *F = it;
            if(can_instrument_function(F, module_regexes)){
                for (Function::iterator BB = F->begin(), BE = F->end(); BB != BE; ++BB){
                    AllBasicBlocks.push_back(BB);
                }
            }
        }

        /* random seed for -fault-randombb-fif */
        if(random_BB_FIF_seed.getNumOccurrences() == 0){
            random_BB_FIF_seed = rand_seed;
        }
        srand(random_BB_FIF_seed);

        /* add BBs for -fault-randombb-fif to BB_FIF_map */

        std::map<BasicBlock *, float> BB_FIF_map;
        for(unsigned int i = 0; i < random_BB_FIF.first; i++){
            /* todo: pct, instead of absolute number of selected BBs ? */
            BasicBlock *BB = NULL;
            do{
                BB = AllBasicBlocks[rand() % AllBasicBlocks.size()];
            }while(BB_FIF_map.count(BB)); // only insert unique values
            /* for now, insert the original basic block.
             * below, we will have to remap the fif value to the corrupted basic block clone */
            BB_FIF_map.insert(std::pair<BasicBlock *, float>(BB, random_BB_FIF.second));
        }
        
        /* random seed for everything except -fault-randombb-fif */
        srand(rand_seed);

        /* add BBS for -functions-fif to BB_FIF_map */

        unsigned long num_function_bbs=0; 
        for (Module::iterator it = M.getFunctionList().begin(); it != M.getFunctionList().end(); ++it) {
            Function *F = it;
            if(functions_FIF.count(F->getName()) > 0 && can_instrument_function(F, module_regexes)){
                float fif = functions_FIF[F->getName()];
                for (Function::iterator BI = F->begin(), BE = F->end(); BI != BE; ++BI) {
                    BasicBlock &BB = *BI;
                    if(BB_FIF_map.count(&BB) == 0){
                        BB_FIF_map.insert(std::pair<BasicBlock *, float>(&BB, fif));
                        num_function_bbs++;
                    }
                }
            }
        }
     
        /* add BBs for -functioncallers-fif to BB_FIF_map */

        unsigned long num_functioncallers=0; 
        for (Module::iterator it = M.getFunctionList().begin(); it != M.getFunctionList().end(); ++it) {
            Function *F = it;
            if(functioncallers_FIF.count(F->getName()) > 0){
                float fif = functioncallers_FIF[F->getName()];
                std::vector<User*> Users(F->use_begin(), F->use_end());
                while (!Users.empty()) {
                    User *U = Users.back();
                    Users.pop_back();
                    if (Instruction *I = dyn_cast<Instruction>(U)) {
                        if(can_instrument_function(I->getParent()->getParent(), module_regexes) && BB_FIF_map.count(I->getParent()) == 0){
                            BB_FIF_map.insert(std::pair<BasicBlock *, float>(I->getParent(), fif));
                            num_functioncallers++;
                        }
                    }
                }
            }
        }

        /* initialize fifs and regexes for the -fault-module-fif argument */

        unsigned long num_module_bbs=0;
        std::vector<Regex*> module_fif_regexes;
        std::map<Regex*, float> regex_fif_map;
        for (std::map<std::string, float>::iterator M =modules_FIF.begin(), E = modules_FIF.end(); M != E; M++) {
            StringRef sr(M->first);
            Regex* regex = new Regex(sr);
            module_fif_regexes.push_back(regex);
            regex_fif_map.insert(std::pair<Regex*, float>(regex, M->second));
        }  

        /* add BBs for -module-fif to BB_FIF_map */

        for (Module::iterator it = M.getFunctionList().begin(); it != M.getFunctionList().end(); ++it) {
            Function *F = it;
            if(can_instrument_function(F, module_regexes)){
                Value *DIF = Backports::findDbgSubprogramDeclare(F);
                if(DIF) {
                    DISubprogram Func(cast<MDNode>(DIF));
                    Regex *regex;
                    if((regex = matchesRegex(Func, module_fif_regexes)) != NULL){
                        float fif = regex_fif_map[regex];
                        for (Function::iterator BI = F->begin(), BE = F->end(); BI != BE; ++BI) {
                            BasicBlock &BB = *BI;
                            if(BB_FIF_map.count(&BB) == 0){
                                BB_FIF_map.insert(std::pair<BasicBlock *, float>(&BB, fif));
                                num_module_bbs++;
                            }
                        }
                    }
                }
            }
        }

        /* initialize regexes from -fault-switch-flag-map */
        for(unsigned int i = 0; i < (sizeof(switchFlagMapDefaultOptions)/sizeof(std::string)); i++){
            switchFlagMap.push_back(switchFlagMapDefaultOptions[i]);
        }
        std::map<std::pair<Regex*, Regex*>, int> switchFlagRegexMap;
        std::vector<std::pair<Regex*, Regex*> > switchFlagRegexList;
        initRegexMap(switchFlagRegexMap, switchFlagRegexList, switchFlagMap);
        /* Used to ensure that a function has max. 1 return instruction */
        UnifyFunctionExitNodes UFEN;

        SmallVector<FaultType*, 8> FaultTypes;
        if(dsnCompat||atcCompat){
	    addFaultType(FaultTypes, new SwapFault());
	} else {
            addFaultType(FaultTypes, new SwapFaultV2());
	}
        addFaultType(FaultTypes, new NoLoadFault());
        addFaultType(FaultTypes, new RndLoadFault());
        addFaultType(FaultTypes, new NoStoreFault());
        addFaultType(FaultTypes, new FlipBranchFault());
        addFaultType(FaultTypes, new FlipBoolFault());
        addFaultType(FaultTypes, new CorruptPointerFault());
        addFaultType(FaultTypes, new CorruptIndexFault());
        addFaultType(FaultTypes, new CorruptIntegerFault());
        if(dsnCompat||atcCompat){
            addFaultType(FaultTypes, new CorruptOperatorFault());
	}else{
            addFaultType(FaultTypes, new CorruptOperatorFaultV2());
	}
       
        if(!dsnCompat){
            addFaultType(FaultTypes, new StuckAtBranchFault());
            addFaultType(FaultTypes, new StuckAtLoopFault(this));
            addFaultType(FaultTypes, new BufferOverflowFault(&M));
            addFaultType(FaultTypes, new MemLeakFault(&M));
            addFaultType(FaultTypes, new DanglingPointerFault(&M));
            addFaultType(FaultTypes, new NullPointerFault());
        }

        if(!noStatisticsInstructions){ 
            for(std::vector<FaultType *>::size_type i = 0; i <  FaultTypes.size(); i++){
                FaultTypes[i]->addToModule(M);
            }
        }

        /* global array of basic block execution counts */
        TYPECONST PointerType *execution_counts_ptr_ptr_type = dyn_cast<PointerType>(execution_counts_var->getType());
        TYPECONST PointerType *execution_counts_ptr_type = dyn_cast<PointerType>(execution_counts_ptr_ptr_type->getElementType());
        TYPECONST IntegerType *execution_counts_type = dyn_cast<IntegerType>(execution_counts_ptr_type->getElementType());
        ArrayType* execution_counts_array_type = ArrayType::get(execution_counts_type, AllBasicBlocks.size() + 2);

        GlobalVariable* execution_counts_array_var = new GlobalVariable(M,
                /*Type=*/ execution_counts_array_type,
                /*isConstant=*/false,
                /*Linkage=*/GlobalValue::ExternalLinkage,
                /*Initializer=*/0, // has initializer, specified below
                /*Name=*/"execution_counts");
        execution_counts_array_var->setAlignment(4);
        execution_counts_array_var->setInitializer(Constant::getNullValue(execution_counts_array_type));

	std::string mapFilePath = M.getModuleIdentifier() + ".map";
	MapFile mapfile(mapFilePath, moduleName);

        int bb_index=1, bb_index_fdp;
	int fault_index;

        std::vector<Constant*> bb_total_num_injected_initializer;

        for (Module::iterator it = M.getFunctionList().begin(); it != M.getFunctionList().end(); ++it) {
            Function *F = it;

            if(!can_instrument_function(F, module_regexes)){
                if(is_edfi_section(F) && !do_unconditionalize){
                    /* this is all the instrumentation that is optionally needed for static edfi lib functions */
                    switchFlag(switchFlagRegexMap, switchFlagRegexList, F, enabled_var, enabled_version_var);
                }
                continue;
            }
            if (rand_reseed_per_function) {
                srand(rand_seed ^ HashString(F->getName(), 1));
            }

	    std::string functionDirectory;
	    std::string functionFileName;
	    std::string functionRelPath;
	    Value *functionDIF = PassUtil::findDbgSubprogramDeclare(F);
	    if (functionDIF) {
	        DISubprogram disp = DISubprogram(cast<MDNode>(functionDIF));
	        PassUtil::getDbgLocationInfo(disp, "", &functionFileName,
			&functionDirectory, &functionRelPath);
	    }

	    std::string functionName = F->getName();
	    mapfile.writeFunction(functionName, functionRelPath);
	    fault_index = 0;
	    
            BasicBlock *OldFirstBB = F->getBasicBlockList().begin();
            BasicBlock *ClonedOldFirstBB = NULL;

            /* Create a new entrypoint */
            BasicBlock *NewFirstBB = BasicBlock::Create(M.getContext(), "newBB", F, OldFirstBB);

            /* Move all AllocaInstructions from OldFirstBB to NewFirstBB */
            for(BasicBlock::iterator BI = OldFirstBB->getInstList().begin(), BE = OldFirstBB->getInstList().end(); BI != BE;){
                Instruction *val = &(*BI++);
                if (AllocaInst *allocaInst = dyn_cast<AllocaInst>(val)){
                    /* should be check for ConstantInt, instead of isStaticAlloca (latter is always false, because OldFirstBB is no longer the first BB)
                     * However, for backward compatibility with DSN 2013 evaluation test results, isStaticAlloca is used when -fault-no-loop-header-checks=1 */
                    if(dsnCompat ? allocaInst->isStaticAlloca() : isa<ConstantInt>(allocaInst->getArraySize())){
                        allocaInst->removeFromParent();
                        NewFirstBB->getInstList().push_back(allocaInst);
                    }
                }
            }

            SmallVector<BasicBlock*, 8> Headers, CorruptedClones, OriginalBBs;
            VALUE_TO_VALUE_MAP_TY VMap;

            /* Clone all basic blocks, including their branch edges (creating two isolated graphs) */
            {
                // clone code inspired by llvm::CloneFunctionInto() (llvm/lib/Transforms/Utils/CloneFunction.cpp)

                for (Function::iterator BI = ++F->begin(), BE = F->end(); BI != BE; ++BI) {
                    BasicBlock &BB = *BI;
                    if(std::find (Headers.begin(), Headers.end(), &BB) == Headers.end()){

                        OriginalBBs.push_back(&BB);
                        // Create a new basic block and copy instructions into it!
                        BasicBlock *CBB = CloneBasicBlock(&BB, VMap, ".CLONED", F, NULL);
                        VMap[&BB] = CBB;                       // Add basic block mapping.
                        Headers.push_back(CBB);
                        if(!ClonedOldFirstBB){
                            ClonedOldFirstBB = CBB;
                        }
                        /* remap any fif value from the original basic block to the cloned header
                         * below, we will eventually remap the value to the corrupt basic block */
                        replace_BB_FIF_key(BB_FIF_map, &BB, CBB);
                    }

                }

                // Loop over all of the instructions in the function, fixing up operand
                // references as we go.  This uses VMap to do all the hard work.
                for (Function::iterator BB = F->begin(), BE = F->end(); BB != BE; ++BB)
                    if(std::find (Headers.begin(), Headers.end(), BB) != Headers.end())
                        // Loop over all instructions, fixing each one as we find it...
                        for (BasicBlock::iterator II = BB->begin(); II != BB->end(); ++II){
                            RemapInstruction(II, VMap, RF_NoModuleLevelChanges|RF_IgnoreMissingEntries);
                        }

            }

            /* branch from new first basic block to original blocks or cloned blocks, based on value of enabled_var, joining the two graphs. */ 
#ifdef EDFI_COUNT_ALL_BLOCKS
	    if (1){
#else
	    if (do_unconditionalize){
#endif
                BranchInst::Create(ClonedOldFirstBB, NewFirstBB);
	    }else{
                createFlagBasedBranchInst(M, OldFirstBB, ClonedOldFirstBB, enabled_var, Constant0, NewFirstBB);
	    }
            
            /* Because basic blocks will be cloned, users of an instruction outside of the same basic block 
             * are no longer dominated by the instruction. Therefore, move values to the stack,
             * and work with alloca, load and store instructions. After creating the cloned basic blocks,
             * the values can be promoted back to virtual registers by the mem2reg optimization pass.
             * (Alternatively, a complex network of PHINodes has to be built, in order to restore dominance over users.)
             * */
            bool tryDemote = !doLoopHeaderSwitching || dsnCompat || atcCompat;
            if(tryDemote){
                /* This is only needed for dsn compatibility, because we did not use -reg2mem for that evaluation.
                 * It's better to use -reg2mem, because that also makes it possible to inject loop header flag checks 
                 * */
                for(std::vector<BasicBlock>::size_type i = 0; i <  Headers.size(); i++){
                    BasicBlock *Header = Headers[i];
                    for (BasicBlock::iterator II = Header->getFirstNonPHI(); II != Header->end(); ++II){
                        /* Try to find a user that will no longer be dominated after cloning. */
                        for (Value::use_iterator i = II->use_begin(), e = II->use_end(); i != e; ++i){
                            bool doDemote = false;
                            if(PHINode *P = dyn_cast<PHINode>(*i)){
                                /* Only find PHINodes that use this value for an incoming block that is not a direct successor to the current basic block */
                                for(unsigned int i=0; i < P->getNumIncomingValues(); i++){
                                    if(P->getIncomingValue(i) == II && P->getIncomingBlock(i) != Header){
                                        doDemote = true;
                                    }
                                }

                            }else if(dyn_cast<Instruction>(*i)->getParent() != Header){
                                /* This user is located in another basic block */
                                doDemote = true;
                            }

                            if(doDemote){
                                /* Convert this instruction and its users to use alloca, load and store instructions */
                                DemoteRegToStack(*dyn_cast<Instruction>(II), false, NULL);
                                II = Header->begin();
                                break;
                            }
                        }
                    }
                }
            }

            
            /* Now split the cloned BBs into headers of phinodes, and the rest of the BB body 
             * The body is cloned again, so that we get both a correct and a corrupt clone.
             * The header branches to one of the cloned versions, based on probability */ 
            {
		bb_index_fdp = bb_index;
                for(std::vector<BasicBlock>::size_type i = 0; i <  Headers.size(); i++){
                    BasicBlock *Header = Headers[i];

                    /* Move PHINodes into a header basic block */
                    BasicBlock *Corrupted = Header->splitBasicBlock(Header->getFirstNonPHI());
                    CorruptedClones.push_back(Corrupted);

                    // remap any fif value from the header to the corrupt basic block
                    replace_BB_FIF_key(BB_FIF_map, Header, Corrupted);

                    BasicBlock *Correct = CloneBasicBlock(Corrupted, VMap, ".Correct", F, NULL);
                    VMap[Corrupted] = Correct;                       // Add basic block mapping.
                    for (BasicBlock::iterator II = Correct->begin(); II != Correct->end(); ++II){
                        RemapInstruction(II, VMap, RF_NoModuleLevelChanges|RF_IgnoreMissingEntries);
                    }
                    Correct->moveBefore(Corrupted);
                    if (do_unconditionalize) {
                        Corrupted->moveBefore(&F->back());
                        Correct->setName(Correct->getName() + ".GOLDEN");
                        Corrupted->setName(Corrupted->getName() + ".FAULTY");
                    }

                    /* Update PHINodes in direct successor basic blocks. 
                     * They already contain incoming values for the corrupt clone, 
                     * so we now add the cloned value from the correct clone.
                     *
                     * See also:
                     *      BasicBlock::splitBasicBlock (last part of function)
                     *      BasicBlock::replaceSuccessorsPhiUsesWith() (seems to do same as split)
                     * */
                    for (succ_iterator I=succ_begin(Corrupted), E=succ_end(Corrupted); I!=E; ++I) {
                        BasicBlock *Successor = *I;
                        PHINode *PN;
                        for (BasicBlock::iterator II = Successor->begin();(PN = dyn_cast<PHINode>(II)); ++II) {
                            Value *CorruptedValue = PN->getIncomingValueForBlock(Corrupted);
                            Value *NewValue;
                            if(VMap[CorruptedValue]){
                                /* value is mapped */
                                NewValue = VMap[CorruptedValue];
                            }else{
                                /* value is not mapped.
                                 * Its probably located in a predecessor block, 
                                 * and not yet demoted
                                 * */
                                NewValue = CorruptedValue;
                            }
                            PN->addIncoming(NewValue, Correct);
                        }
                    }

                    /* Replace the unconditional branch instr. with branch based on probability */
                    Header->getTerminator()->eraseFromParent();
                    
                    /* goto cloned block or original block, based on value from fdp callback or global flag*/
                    //Value *flag = noDFTs ? (Value *)enabled_var : (Value *) FDPFuncCall;
                    //createFlagBasedBranchInst(M, Corrupted, Correct, flag, Header);
		    if(do_inline_profiling){
			std::vector<Constant*> constElementIndices;
			constElementIndices.push_back(ConstantInt::get(M.getContext(), APInt(64, 0)));
			constElementIndices.push_back(ConstantInt::get(M.getContext(), APInt(64, bb_index_fdp)));
			Constant* constElementPtr = ConstantExpr::getGetElementPtr(execution_counts_array_var, constElementIndices);
			LoadInst* instLoad = new LoadInst(constElementPtr, "", false, Header);
			ConstantInt* constOne = ConstantInt::get(M.getContext(), APInt(64, 1));
			BinaryOperator* instInc = BinaryOperator::Create(Instruction::Add, instLoad, constOne, "inc", Header);
			new StoreInst(instInc, constElementPtr, false, Header);
		    }
                    if(do_unconditionalize){
                        BranchInst *branch = createFlagBasedBranchInst(M, Corrupted, Correct, inject_bb_var, ConstantInt::get(M.getContext(), APInt(32, bb_index_fdp)), Header);
			setBranchWeight(M, branch, 1, 65536);
                    }else if(noDFTs){
                        createFlagBasedBranchInst(M, Correct, Corrupted, enabled_var, Constant0, Header);
                    }else{
                        /* Call fdp callback() for decision */
#ifdef EDFI_STATIC_DFT
                        Function *fdp_callback_p = M.getFunction(EDFI_XSTR(EDFI_STATIC_DFT));
                        assert(fdp_callback_p);
#else
                        LoadInst* fdp_callback_p = new LoadInst(on_fdp_p_var, "", false, Header);
                        fdp_callback_p->setAlignment(8);
#endif
                        CallInst* FDPFuncCall = CallInst::Create(fdp_callback_p, ConstantInt::get(Constant1->getType(), bb_index_fdp), "", Header);
                        createFlagBasedBranchInst(M, Correct, Corrupted, FDPFuncCall, Constant0, Header);
                    }
		    bb_index_fdp++;
                }
            }

#ifndef EDFI_COUNT_ALL_BLOCKS
            bool simulateLoopHeaderSwitching = dsnCompat || atcCompat;
            if((doLoopHeaderSwitching || simulateLoopHeaderSwitching) && !do_unconditionalize){
                /* for each original loop header basic block , split into two basic blocks right after the last phinode.
                 * The first basic block, containing only the phinodes, checks the faultinjection_enabled flag for a non-occuring value.
                 * In that same block, we create a conditional branchinst that branches to a dummy basicblock (never, because of non-occuring value)
                 * , and the second basic block that contains all other instructions (always).
                 * this simulates the overhead during normal execution for checking the flag.
                 */
                /* todo: actually jump to faulty basic block header when flag != 0 */
                LoopInfo &LI = getAnalysis<LoopInfo>(*F);
                BasicBlock *DummyBB = NULL;
                for(std::vector<BasicBlock>::size_type i = 0; i <  OriginalBBs.size(); i++){
                    BasicBlock *Header = OriginalBBs[i];
                    if(LI.isLoopHeader(Header)){
                        BasicBlock *Tail = Header->splitBasicBlock(Header->getFirstNonPHI());
                        Header->getTerminator()->eraseFromParent();
                        if(simulateLoopHeaderSwitching){
                            if(!DummyBB){
                                /* We actually need to create the dummy bb, because we found our first loop header */
                                DummyBB = BasicBlock::Create(M.getContext(), "FlagChecker", F);
                                new UnreachableInst(M.getContext(), DummyBB);
                            }
                            /* for the DSN 2013 evaluation, the flag check did not evaluate to true, so we did not need a real branch to faulty execution */
                            createFlagBasedBranchInst(M, DummyBB, Tail, enabled_var, ConstantInt::get(Constant1->getType(), 0xDEAD), Header);
                        }else{
                            createFlagBasedBranchInst(M, dyn_cast<BasicBlock>(VMap[Header]), Tail, enabled_var, Constant1, Header);
                        }
                    }
                }
            }
#endif

            UFEN.runOnFunction(*F);
	    if (!do_unconditionalize) {
                switchFlag(switchFlagRegexMap, switchFlagRegexList, F, enabled_var, enabled_version_var);
	    }

            /* Basic block cloning and fault decision callback injection is done. Now inject faults. */

            /* loop through all corrupt cloned basic blocks */
            for(std::vector<BasicBlock>::size_type i = 0; i <  CorruptedClones.size(); i++){
                BasicBlock *BB = CorruptedClones[i];
                float fif = global_FIF;
                if(BB_FIF_map.count(BB)){
                    fif = BB_FIF_map[BB];
                }

                mapfile.writeBasicBlock();

		/* in case of one fault per basic block, select one in advance */
		int fault_index_selected = -1;
		if (oneFaultPerBlock) {
		    /* list all available faults by their fault_index;
		     * this logic must be IDENTICAL to what is found in
		     * the next loop to ensure the numbers match up
		     */
		    int fault_index_temp = fault_index;
                    SmallVector<int, 64> ApplicableFaults;
		    for (BasicBlock::iterator II = BB->begin(), nextII; II != BB->end();II=nextII){
			Instruction *val = &(*II);

			nextII=II;
			nextII++;
		    
			if (val->getName().find(".reload") != StringRef::npos) {
                            continue;
			}
			for (std::vector<FaultType *>::size_type i = 0; i <  FaultTypes.size(); i++) {
                            FaultType *FT = FaultTypes[i];
                            if (FT->isApplicable(val)) {
				ApplicableFaults.push_back(fault_index_temp);
				fault_index_temp++;
			    }
			}
		    }

		    /* select one of the fault candidates we found */
		    if (ApplicableFaults.size() > 0) {
			fault_index_selected = ApplicableFaults[rand() % ApplicableFaults.size()];
		    }
		}

                /* For each basic block, loop through all instructions */
                for (BasicBlock::iterator II = BB->begin(), nextII; II != BB->end();II=nextII){
                    Instruction *val = &(*II);

                    nextII=II;
                    nextII++;

                    if(val->getName().find(".reload") != StringRef::npos){
                        continue;
                    }

                    std::string tostr;
                    if(do_debug){
                        llvm::raw_string_ostream rsos(tostr);
                        val->print(rsos);
                        rsos.str();
                    }

                    if (MDNode *N = val->getMetadata("dbg")) {
			DILocation Loc(N);
		        mapfile.writeDInstruction(Loc.getFilename(), Loc.getLineNumber());
                    } else {
		        mapfile.writeInstruction();
		    }

                    SmallVector<Value *, 8> replacements;
                    FaultType *applied_fault_type = NULL;
                    bool removed = false;
                    SmallVector<FaultType *, 8> ApplicableFaultTypes;
                    for(std::vector<FaultType *>::size_type i = 0; i <  FaultTypes.size(); i++){
                        FaultType *FT = FaultTypes[i];
                        if(FT->isApplicable(val)){
			    if (oneFaultPerBlock) {
				if (fault_index == fault_index_selected) {
                                    ApplicableFaultTypes.push_back(FT);
				}
			    } else if (fault_selector.count(std::pair<std::string, int>(functionName, fault_index)) ||
			        FT->isApplicableWithFIF(val, fif)){
                                ApplicableFaultTypes.push_back(FT);
                            }

			    mapfile.writeFaultCandidate(FT->getName());
                            FT->num_candidates++;
			    fault_index++;
                        }
                    }
                    if(ApplicableFaultTypes.size() > 0){
                        FaultType *FT = ApplicableFaultTypes[rand() % ApplicableFaultTypes.size()];
                        if(!statistics_only){
                            removed = FT->apply(val, &replacements);
			    mapfile.writeFaultInjected(FT->getName());
                        }
                        applied_fault_type = FT;
                        FT->num_injected++;

                        /* Increase the stastistics counter for this fault */
                        if(!noStatisticsInstructions){
                            LoadInst* count_var = new LoadInst(FT->getFaultCount(), "", false, nextII);
                            count_var->setAlignment(4);
                            BinaryOperator* count_incr = BinaryOperator::Create(Instruction::Add, count_var, Constant1, "", nextII);
                            StoreInst *store_count_var = new StoreInst(count_incr, FT->getFaultCount(), false, nextII);
                            store_count_var->setAlignment(4);
                        }
                    }

                    if(do_debug){
                        if(applied_fault_type){
                            errs() << "   # applied fault " << applied_fault_type->getName() << "\n";
                            if(removed){
                                errs() << "-";
                            }else{
                                errs() << "*";
                            }
                            errs() << tostr << "\n";
                            for(std::vector<Value *>::size_type i = 0; i <  replacements.size(); i++){
                                errs() << "+" << *replacements[i] << "\n";
                            }

                        }else{
                            errs() << " " << tostr << "\n";
                        }
                    }

                }
                if(!noDFLs){
                    /* inject dynamic fault logger callback */
                    Instruction *FirstNonPhi = BB->getFirstNonPHI();
#ifdef EDFI_STATIC_DFL
                    Function *dfl_callback_p = M.getFunction(EDFI_XSTR(EDFI_STATIC_DFL));
                    assert(dfl_callback_p);
#else
                    LoadInst* dfl_callback_p = new LoadInst(on_dfl_p_var, "", false, FirstNonPhi);
                    dfl_callback_p->setAlignment(8);
#endif
                    CallInst::Create(dfl_callback_p, ConstantInt::get(Constant1->getType(), bb_index), "", FirstNonPhi);
                }

                int total_num_injected = 0;

                for(std::vector<FaultType *>::size_type i = 0; i <  FaultTypes.size(); i++){
                    /* Keep statistics for each fault type, to be injected */
                    FaultType *FT = FaultTypes[i];

                    total_num_injected += FT->num_injected;

                    FT->num_injected_initializer.push_back(ConstantInt::get(Constant1->getType(), FT->num_injected));
                    FT->num_injected_total += FT->num_injected;
                    FT->num_injected = 0;
                    
                    FT->num_candidates_initializer.push_back(ConstantInt::get(Constant1->getType(), FT->num_candidates));
                    FT->num_candidates_total += FT->num_candidates;
                    FT->num_candidates = 0;
                }

                bb_total_num_injected_initializer.push_back(ConstantInt::get(Constant1->getType(), total_num_injected));

                bb_index++;
            }

        }
        
        ArrayType* FaultTypeCountType = ArrayType::get(IntegerType::get(M.getContext(), 32), AllBasicBlocks.size());

        TYPECONST PointerType *fault_type_stats_ptr_ptr_type = dyn_cast<PointerType>(fault_type_stats_var->getType());
        TYPECONST PointerType *fault_type_stats_ptr_type = dyn_cast<PointerType>(fault_type_stats_ptr_ptr_type->getElementType());
        TYPECONST StructType *fault_type_stats_type = dyn_cast<StructType>(fault_type_stats_ptr_type->getElementType());
        ArrayType* fault_type_stats_array_type = ArrayType::get(fault_type_stats_type, FaultTypes.size());
        
        std::vector<Constant*> stats_array_elements;

        std::vector<Value*> FirstElementIndex;
        FirstElementIndex.push_back(Constant0);
        FirstElementIndex.push_back(Constant0);

        for(std::vector<FaultType *>::size_type i = 0; i <  FaultTypes.size(); i++){
            FaultType *FT = FaultTypes[i];

            /* build a fault_type_stats struct, to be inserted into the global array of fault_type_stats structs */

            Constant* name_ptr = NULL;
            PassUtil::getStringGlobalVariable(M, FT->getName(), ".str.edfi", "", &name_ptr);

            GlobalVariable* num_injected_var = new GlobalVariable(M, 
                    /*Type=*/ FaultTypeCountType,
                    /*isConstant=*/false,
                    /*Linkage=*/GlobalValue::ExternalLinkage,
                    /*Initializer=*/0, // has initializer, specified below
                    /*Name=*/"counter");
            num_injected_var->setAlignment(4);
            Constant* num_injected_array = ConstantArray::get(FaultTypeCountType, FT->num_injected_initializer);
            num_injected_var->setInitializer(num_injected_array);
            Constant* num_injected_var_ptr = PassUtil::getGetElementPtrConstant(num_injected_var, FirstElementIndex);

            GlobalVariable* num_candidates_var = new GlobalVariable(M, 
                    /*Type=*/ FaultTypeCountType,
                    /*isConstant=*/false,
                    /*Linkage=*/GlobalValue::ExternalLinkage,
                    /*Initializer=*/0, // has initializer, specified below
                    /*Name=*/"counter");
            num_candidates_var->setAlignment(4);
            Constant* num_candidates_array = ConstantArray::get(FaultTypeCountType, FT->num_candidates_initializer);
            num_candidates_var->setInitializer(num_candidates_array);
            Constant* num_candidates_var_ptr = PassUtil::getGetElementPtrConstant(num_candidates_var, FirstElementIndex);
 
            std::vector<Constant*> fault_type_struct_fields;
            fault_type_struct_fields.push_back(name_ptr);
            fault_type_struct_fields.push_back(num_injected_var_ptr);
            fault_type_struct_fields.push_back(num_candidates_var_ptr);

            /* this is the single struct */
            Constant* fault_type_struct = ConstantStruct::get(fault_type_stats_type, fault_type_struct_fields);
            /* push into array */
            stats_array_elements.push_back(fault_type_struct);
        }

        /* global array of fault_type_stats structs */
        GlobalVariable* stats_array_var = new GlobalVariable(/*Module=*/M, 
                /*Type=ArrayTy_6j*/ fault_type_stats_array_type,
                /*isConstant=*/false,
                /*Linkage=*/GlobalValue::ExternalLinkage,
                /*Initializer=*/0, // has initializer, specified below
                /*Name=*/"stats");
        stats_array_var->setAlignment(16);
        Constant* stats_array = ConstantArray::get(fault_type_stats_array_type, stats_array_elements);
        stats_array_var->setInitializer(stats_array);

        /* global array of total faults injected per basic block */

        GlobalVariable* bb_total_num_injected_var = new GlobalVariable(M, 
                /*Type=*/ FaultTypeCountType,
                /*isConstant=*/false,
                /*Linkage=*/GlobalValue::ExternalLinkage,
                /*Initializer=*/0, // has initializer, specified below
                /*Name=*/"bb_total_faults_counter");
        bb_total_num_injected_var->setAlignment(4);
        Constant* bb_total_num_injected_array = ConstantArray::get(FaultTypeCountType, bb_total_num_injected_initializer);
        bb_total_num_injected_var->setInitializer(bb_total_num_injected_array);

        /* update values in global edfi_context struct (counts and pointers) */
        replaceUsesOfStructElement(edfi_context_var, ConstantInt::get(Constant1->getType(), AllBasicBlocks.size()), EDFI_CONTEXT_FIELD_NUM_BBS);
        replaceUsesOfStructElement(edfi_context_var, ConstantInt::get(Constant1->getType(), FaultTypes.size()), EDFI_CONTEXT_FIELD_NUM_FAULT_TYPES);
        replaceUsesOfStructElement(edfi_context_var, PassUtil::getGetElementPtrConstant(stats_array_var, FirstElementIndex), EDFI_CONTEXT_FIELD_FAULT_TYPE_STATS);
        replaceUsesOfStructElement(edfi_context_var, PassUtil::getGetElementPtrConstant(execution_counts_array_var, FirstElementIndex), EDFI_CONTEXT_FIELD_BB_NUM_EXECUTIONS);
        replaceUsesOfStructElement(edfi_context_var, PassUtil::getGetElementPtrConstant(bb_total_num_injected_var, FirstElementIndex), EDFI_CONTEXT_FIELD_BB_NUM_FAULTS);

        /* print statistics */
        EDFI_STAT_PRINTER(EDFI_STATS_HEADER_FMT, EDFI_STATS_SECTION_DEBUG_NAME);
        
        EDFI_STAT_PRINTER(EDFI_STATS_UL_FMT, "n_basicblocks", (unsigned long) AllBasicBlocks.size());
        EDFI_STAT_PRINTER(EDFI_STATS_UL_FMT, "n_function_basicblocks", num_function_bbs);
        EDFI_STAT_PRINTER(EDFI_STATS_UL_FMT, "n_functioncallers_basicblocks", num_functioncallers);
        EDFI_STAT_PRINTER(EDFI_STATS_UL_FMT, "n_module_basicblocks", num_module_bbs);
        EDFI_STAT_PRINTER(EDFI_STATS_UL_FMT, "rand-seed", (unsigned long) rand_seed);
        EDFI_STAT_PRINTER(EDFI_STATS_UL_FMT, "randombb-fif-seed", (unsigned long) random_BB_FIF_seed);
        
        EDFI_STAT_PRINTER(EDFI_STATS_HEADER_FMT, EDFI_STATS_SECTION_PROB_NAME);
        for(std::vector<FaultType *>::size_type i = 0; i <  FaultTypes.size(); i++){
            FaultType *FT = FaultTypes[i];
            EDFI_STAT_PRINTER(EDFI_STATS_LD_FMT, (const char *) FT->getName(), FT->num_candidates_total ? ((long double)FT->num_injected_total)/FT->num_candidates_total : 0);
        }
        EDFI_STAT_PRINTER(EDFI_STATS_HEADER_FMT, EDFI_STATS_SECTION_FAULTS_NAME);
        for(std::vector<FaultType *>::size_type i = 0; i <  FaultTypes.size(); i++){
            FaultType *FT = FaultTypes[i];
            EDFI_STAT_PRINTER(EDFI_STATS_ULL_FMT, (const char *) FT->getName(), FT->num_injected_total);
        }
        EDFI_STAT_PRINTER(EDFI_STATS_HEADER_FMT, EDFI_STATS_SECTION_CANDIDATES_NAME);
        for(std::vector<FaultType *>::size_type i = 0; i <  FaultTypes.size(); i++){
            FaultType *FT = FaultTypes[i];
            EDFI_STAT_PRINTER(EDFI_STATS_ULL_FMT, (const char *) FT->getName(), FT->num_candidates_total);
        }

        /* Fix llvm 2.9 bug: llc finds duplicate debug symbols */
        Backports::StripDebugInfo(M);

        if(debug_assembly.size()){
            int fd = open(debug_assembly.c_str(), O_CREAT|O_RDWR|O_TRUNC, S_IRWXU|S_IRWXG|S_IRWXO); 
            raw_fd_ostream file(fd, true);
            file << M << "\n";
        }

        return true;
    }

    /* check if the path of the source file that contains this function is matched by one of the regular expressions in ModuleNames */
    Regex *matchesRegex(DIDescriptor &DID, std::vector<Regex*> &regexes){
    	std::string relPath;
    	PassUtil::getDbgLocationInfo(DID, "", NULL, NULL, &relPath);
        for(unsigned i=0;i<regexes.size();i++) {
            if(regexes[i]->match(relPath, NULL)) {
                return regexes[i];
            }    
        }
        return NULL;
    }
    
    BranchInst *createFlagBasedBranchInst(Module &M, BasicBlock *IfTrue, BasicBlock *IfFalse, Value *flag, Constant *rhv, BasicBlock *InsertAtEnd){
        Instruction *test_value;
        if(flag->getType()->isPointerTy()){
            LoadInst* load_enabled_var = new LoadInst(flag, "", false, InsertAtEnd);
            load_enabled_var->setAlignment(4);
            test_value = load_enabled_var;
        }else{
            test_value = dyn_cast<Instruction>(flag);
            assert(test_value);
        }
        ICmpInst* do_rnd = new ICmpInst(*InsertAtEnd, ICmpInst::ICMP_EQ, test_value, rhv, "");
        return BranchInst::Create(IfTrue, IfFalse, do_rnd, InsertAtEnd);
    }
    
    void setBranchWeight(Module &M, BranchInst *branch, uint32_t prob_true, uint32_t prob_false) {
        SmallVector<Value *, 3> branchWeights(3);
        MDNode *branchWeightNode;
        Type *Int32Ty = Type::getInt32Ty(M.getContext());

	branchWeights[0] = MDString::get(M.getContext(), "branch_weights");
	branchWeights[1] = ConstantInt::get(Int32Ty, APInt(32, prob_true));
	branchWeights[2] = ConstantInt::get(Int32Ty, APInt(32, prob_false));
	
        branchWeightNode = MDNode::get(M.getContext(), branchWeights);
	branch->setMetadata("branch_weights", branchWeightNode);
    }
    
    Constant *getStructMember(Module &M, GlobalVariable *GV, int index){
        ConstantInt* const Constant0 = ConstantInt::get(M.getContext(), APInt(32, StringRef("0"), 10));
        std::vector<Value*> struct_indices;
        struct_indices.push_back(Constant0);
        struct_indices.push_back(ConstantInt::get(Constant0->getType(), index));
        return PassUtil::getGetElementPtrConstant(GV, struct_indices);
    }

    bool is_edfi_section(Function *F){
        return !F->getSection().compare(EDFI_STATIC_FUNCTIONS_SECTION);
    }

    bool can_instrument_function(Function *F, std::vector<Regex*> &regexes){
        if(F->begin() == F->end()){
            // no basic blocks
            return false;
        }

        if(is_edfi_section(F)) {
            // function in edfi's static library
            return false;
        }

#ifdef __MINIX
	if(F->getName().equals("vprintf") || F->getName().equals("kdoprnt") || F->getName().equals("kprintn") || F->getName().equals("kputc") || F->getName().equals("sys_diagctl_diag")) {
            // function called in edfi's static library
	    return false;
	}
#endif

        if(FunctionNames.size() > 0 && std::find (FunctionNames.begin(), FunctionNames.end(), F->getName()) == FunctionNames.end()){
            return false;
        }

        if(std::find (SkipFunctionNames.begin(), SkipFunctionNames.end(), F->getName()) != SkipFunctionNames.end()){
            return false;
        }

        /* is this function located in a source file that is matched by one of the regexes in ModuleNames? */
        if(regexes.size() > 0){
            Value *DIF = Backports::findDbgSubprogramDeclare(F);
            if(DIF) {
                DISubprogram Func(cast<MDNode>(DIF));
                if(matchesRegex(Func, regexes)==NULL){
                    return false;   
                }
            }else if(!dsnCompat){ /* the dsn evaluation let functions without dbg be instrumented */
                return false;
            }
        }

        /* TODO: remove this hack, together with two command line arguments at the start of this file */
        int index=0;
        if(F->getName().equals("ha_key_cmp")){
            return false;
        }
        for (Module::iterator it = F->getParent()->getFunctionList().begin(); it != F->getParent()->getFunctionList().end(); ++it) {
            Function *Fit = it;
            if(F==Fit){
                if(index >= func_min && index <= func_max){
                    if(func_min == func_max) errs() << index << " " << F->getName() << "\n";
                    return true;
                }
                return false;
            }
            index++;
        }
        /* End of hack */


        return true;
    }

    void replace_BB_FIF_key(std::map<BasicBlock *, float> &BB_FIF_map, BasicBlock *old_key, BasicBlock *new_key){
        if(BB_FIF_map.count(old_key)){
            float fif = BB_FIF_map[old_key];
            BB_FIF_map.insert(std::pair<BasicBlock *, float>(new_key, fif));
            BB_FIF_map.erase(old_key);
        }
    }

    void replaceUsesOfStructElement(GlobalVariable *Struct, Constant *NewElement, int index){
        /* replace an existing constant member value in a global struct */
        ConstantStruct *S = dyn_cast<ConstantStruct>(Struct->getInitializer());
        if (S->getOperand(index) != NewElement) {
            S->replaceUsesOfWithOnConstant(S->getOperand(index), NewElement, &S->getOperandUse(index));
        }
    }

    void getAllocaInfo(Function *F, Instruction **allocaInsertionPoint, Instruction **firstNonAllocaInst)
    {
        assert(!F->isDeclaration());
        BasicBlock::iterator allocaIP = F->front().begin();
        while (isa<AllocaInst>(allocaIP)) ++allocaIP;
        BasicBlock::iterator firstNonAI = allocaIP;
        if (firstNonAI->getName().equals("alloca point")) {
            firstNonAI++;
        }
        if(allocaInsertionPoint) {
            *allocaInsertionPoint = allocaIP;
        }
        if(firstNonAllocaInst) {
            *firstNonAllocaInst = firstNonAI;
        }
    }

    void initRegexMap(std::map<std::pair<Regex*, Regex*>, int> &regexMap, std::vector<std::pair<Regex*, Regex*> > &regexList, cl::list<std::string> &stringList)
    {
        for (std::vector<std::string>::iterator it = stringList.begin(); it != stringList.end(); ++it) {
            StringRef token = *it;
            SmallVector< StringRef, 3 > tokenVector;
            token.split(tokenVector, "/");
            if(tokenVector.size() != 3) {
                errs() << "ERROR: wrong format: " << *it << "\n";
                exit(1);
            }
            std::string value = tokenVector.pop_back_val();
            std::string key2 = tokenVector.pop_back_val();
            std::string key1 = tokenVector.pop_back_val();
;
            std::string error;
            Regex *sectionRegex = new Regex(key1, 0);
            if(!sectionRegex->isValid(error)) {
                errs() << "ERROR: wrong regex: " << key1 << "\n";
                exit(1);
            }
            Regex *functionRegex = new Regex(key2, 0);
            if(!functionRegex->isValid(error)) {
                errs() << "ERROR: wrong regex: " << key2 << "\n";
                exit(1);
            }
            std::pair<Regex*, Regex*> regexes = std::pair<Regex*, Regex*>(sectionRegex, functionRegex);
            char *val_end;
            int val = strtol(value.c_str(), &val_end, 0);
            if(value.c_str() == val_end || val_end != &value.c_str()[strlen(value.c_str())]){
                errs() << "ERROR: wrong value for: " << *it << "\n";
                exit(1);
            }
            regexMap.insert(std::pair<std::pair<Regex*, Regex*>, int>(regexes, val));
            regexList.push_back(regexes);
        }
    }

#ifdef EDFI_FORBID_SWITCH_FLAGS
    void switchFlag(std::map<std::pair<Regex*, Regex*>, int> &regexMap, std::vector<std::pair<Regex*, Regex*> > &regexList, Function *F, GlobalVariable *enabled_var, GlobalVariable *enabled_version_var){}
#else
    void switchFlag(std::map<std::pair<Regex*, Regex*>, int> &regexMap, std::vector<std::pair<Regex*, Regex*> > &regexList, Function *F, GlobalVariable *enabled_var, GlobalVariable *enabled_version_var){
        for (std::vector<std::pair<Regex*, Regex*> >::iterator it = regexList.begin(); it != regexList.end(); ++it) {
            std::pair<Regex*, Regex*> regexes = *it;
            Regex *sectionRegex = regexes.first;
            Regex *functionRegex = regexes.second;
            if(sectionRegex->match(F->getSection(), NULL) && functionRegex->match(F->getName(), NULL)) {
                /* If there is no return block, don't push the enabled flag at all */
                if(!F->empty()){
                    int value = regexMap.find(regexes)->second;
                    if(value == -1) return;
                    ConstantInt* constValue = ConstantInt::get(F->getParent()->getContext(), APInt(32, value, false));

                    Instruction *firstNonAlloca;
                    getAllocaInfo(F, NULL, &firstNonAlloca);

                    LoadInst *saved_flag = new LoadInst(enabled_var, "edfi_saved_flag", false, firstNonAlloca);
                    LoadInst *saved_flag_version = new LoadInst(enabled_version_var, "edfi_saved_flag_version", true, firstNonAlloca);
                    new StoreInst(constValue, enabled_var, false, firstNonAlloca);

                    Instruction *returnInst = F->back().getTerminator();
                    LoadInst *version_at_end = new LoadInst(enabled_version_var, "edfi_flag_version_end", true, returnInst);
                    ICmpInst* cmp_versions = new ICmpInst(returnInst, ICmpInst::ICMP_EQ, saved_flag_version, version_at_end, "cmp_versions");
                    Instruction *ifTrueTerminator = Backports::SplitBlockAndInsertIfThen(cmp_versions);
                    new StoreInst(saved_flag, enabled_var, false, ifTrueTerminator);
                }
                return;
            }
        }
    }
#endif

}

bool Fault_Selector_Parser::parse(cl::Option &O, StringRef ArgName, const std::string &Arg, std::set<std::pair<std::string, int> > &Val) {
	const char *s = Arg.c_str();
	const char *func_start, *func_end;
	const char *index_start, *index_end;

	/* format: comma-separated list of function:index pairs */
	for (;;) {
		/* extract function */
		func_start = s;
		while (*s && *s != ':' && *s != ',') s++;
		func_end = s;
		if (func_start >= func_end) {
			return O.error("'" + Arg + "': fault selector function expected");
		}
	
		/* skip separator */
		if (*s != ':') {
			return O.error("'" + Arg + "': fault selector separator expected");
		}
		s++;

		/* extract index */
		index_start = s;
		while (*s >= '0' && *s <= '9') s++;
		index_end = s;
		if (index_start >= index_end) {
			return O.error("'" + Arg + "': fault selector index expected");
		}

		/* add fault selector to set */
		Val.insert(std::pair<std::string, int>(std::string(func_start, func_end - func_start),
			atoi(index_start)));

		/* move on to next selector */
		if (!*s) break;
		if (*s != ',') {
			return O.error("'" + Arg + "': unexpected character in fault selector");
		}
		s++;
	}

	/* correct parsing returns false */
	return false;
}

bool Random_BB_FIF_Parser::parse(cl::Option &O, StringRef ArgName, const std::string &Arg, std::pair<unsigned, float> &Val) {
    const char *ArgStart = Arg.c_str();
    char *FirstEnd, *SecondEnd;

    /* Parse integer part, leaving 'FirstEnd' pointing to the first non-integer char */
    Val.first = (unsigned)strtol(ArgStart, &FirstEnd, 0);
    if(*FirstEnd != '/'){
        return O.error("'" + Arg + "': required format = R/fif"); 
    }
    Val.second = strtof(&FirstEnd[1], &SecondEnd);
    assert(Val.second >= 0.0 && Val.second <= 1.0 && "ERROR: fif value must be between 0.0 and 1.0");
    if(SecondEnd != &ArgStart[strlen(ArgStart)]){
        return O.error("'" + Arg + "': required format = R/fif"); 
    }
    /* correct parsing returns false */
    return false;
}

bool Name_FIF_Parser::parse(cl::Option &O, StringRef ArgName, const std::string &Arg, std::map<std::string, float> &Val) {
    std::string Pos = Arg;
    const char *err_msg = " Required format = name_1/fif_1:name_2/fif_2:...:name_N/fif_N";

    while(Pos.size() > 0){

        if(Pos.find("/") == std::string::npos){
            return O.error("'" + Arg + "': Could not find '/'." + err_msg); 
        }
        
        // read name until '/'
        std::string Name = Pos.substr(0, Pos.find("/"));
        if(Name.size() == 0){
            return O.error("'" + Arg + "': name must be at least one character long." + err_msg); 
        }

        // move beyond '/'
        Pos = Pos.substr(Pos.find("/")+1, std::string::npos);

        // read numeric fif value
        char *end;
        float fif = strtof(Pos.c_str(), &end);
        assert(fif >= 0.0 && fif <= 1.0 && "ERROR: fif value must be between 0.0 and 1.0");
        if(end == Pos.c_str()){
            // string length of numeric value cannot be zero
            return O.error("'" + Arg + "': Could not read fif value." + err_msg); 
        }

        // move beyond fif substring 
        Pos = Pos.substr(end - Pos.c_str(), std::string::npos);

        if(Pos.size() > 0){
            // not at the end yet. skip ':' before new iteration.
            if(Pos[0] != ':'){
                return O.error("'" + Arg + "'." + err_msg); 
            }
            Pos = Pos.substr(1, std::string::npos);
        }
        Val.insert(std::pair<std::string, float>(Name, fif));
    }

    /* correct parsing returns false */
    return false;
}

bool Prob_Parser::parse(cl::Option &O, StringRef ArgName, const std::string &ArgValue, float &Val){
    bool retval = parser<float>::parse(O, ArgName, ArgValue, Val);
    assert(Val >= 0.0 && Val <= 1.0 && "probability and fif arguments should be between 0.0 and 1.0");
    return retval;
}

char FaultInjector::ID = 0;
static RegisterPass<FaultInjector> X("edfi", "Execution-Driven Fault Injector Pass");

