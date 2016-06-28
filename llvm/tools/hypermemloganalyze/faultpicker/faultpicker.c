#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "bb_info.h"
#include "coverage.h"
#include "debug.h"
#include "function.h"
#include "helper.h"
#include "logexeccounts.h"
#include "module.h"

#include "../../mapprint/mapparse.h"
#include "../../printedfistats/edfistatsparse.h"

static struct bb_info_list bb_list;

static struct function_hashtable *functions;

static struct module_execcount_ll *logs;

static struct module_ll *modules;

static void check_duplicate_map(void) {
	const struct module *module = module_find_duplicate(modules);

	if (module) {
		fprintf(stderr, "error: two map files both include "
			"module \"%s\"\n", module->name);
		exit(1);
	}
}

static void check_map_without_log(void) {
	struct module_ll *entry;

	entry = modules;
	while (entry) {
		if (!execcounts_find(logs, entry->module->name)) {
			fprintf(stderr, "warning: no traces are available for "
				"module %s, so no faults will be injected "
				"in that module except in shared code\n",
				entry->module->name);
		}
		entry = entry->next;
	}
}

static void load_log(const char *logpath) {
	struct module_execcount_ll *logs_new;

	dbgprintf("loading log file %s\n", logpath);
	logs_new = execcounts_load_from_log(logpath);
	if (!logs_new) {
		fprintf(stderr, "warning: log file %s contains "
			"no execution counts\n", logpath);
	}
	execcounts_deduplicate(&logs_new, 1);
	logs = execcounts_concat(logs, logs_new);
}

static void load_log_edfistats(const char *logpath) {
	int i;
	struct module_execcount_ll *logs_new;
	const char *paths[] = { logpath };
	struct file_state *state;

	dbgprintf("loading edfistats log file %s\n", logpath);
	state = edfistats_open(paths, 1);

	logs_new = CALLOC(1, struct module_execcount_ll);
	logs_new->data.name = "";
	logs_new->data.bb_count = state->header->num_bbs;
	logs_new->data.bbs = MALLOC(logs_new->data.bb_count, execcount);
	for (i = 0; i < logs_new->data.bb_count; i++) {
		logs_new->data.bbs[i] = state->bb_num_executions[i];
	}

	edfistats_close(state, 1);

	logs = execcounts_concat(logs, logs_new);
}

static void load_map(const char *mappath) {
	dbgprintf("loading map file %s\n", mappath);
	module_load_from_map_ll(mappath, &modules);
}

static void fault_select_bb(
	struct module_bb *bb,
	int disable,
	const struct string_ll *typeexclude,
	const struct string_ll *typeinclude) {

	assert(bb);

	if (bb->faultinj_count <= 0) return;
	assert(disable || bb->fault_type);

	if (disable ||
		string_ll_find(typeexclude, bb->fault_type) ||
		(typeinclude && !string_ll_find(typeinclude, bb->fault_type))) {
		bb->faultinj_count = 0;
	}
}

static int startswith(const char *s, const char *substr) {
	size_t slen, substrlen;

	assert(substr);

	if (!s) return 0;

	slen = strlen(s);
	substrlen = strlen(substr);
	if (slen < substrlen) return 0;

	return memcmp(s, substr, substrlen) == 0;
}

static void fault_select_function(
	struct module_func *function,
	const struct string_ll *typeexclude,
	const struct string_ll *typeinclude) {
	int disable;
	int i;

	assert(function);

	disable = startswith(function->name, "hypermem_") ||
		startswith(function->name, "ltckpt_") ||
		startswith(function->name, "lt_") ||
		startswith(function->name, "sef_");

	for (i = 0; i < function->bb_count; i++) {
		fault_select_bb(&function->bbs[i], disable,
			typeexclude, typeinclude);
	}
}

static void fault_select_module(
	struct module *module,
	const struct string_ll *typeexclude,
	const struct string_ll *typeinclude) {
	int i;

	assert(module);

	for (i = 0; i < module->func_count; i++) {
		fault_select_function(&module->funcs[i],
			typeexclude, typeinclude);
	}
}

static void fault_select(
	const struct string_ll *typeexclude,
	const struct string_ll *typeinclude) {
	struct module_ll *module;

	for (module = modules; module; module = module->next) {
		fault_select_module(module->module,
			typeexclude, typeinclude);
	}
}

static void print_stats_per_count_function(
	const struct coverage_per_bb *coverage,
	const struct function *func,
	int value,
	int use_inj,
	int *count_suitable,
	int *count_total,
	int *more) {
	struct function_bb *bb;
	int i, value_curr;

	assert(coverage);
	assert(func);
	assert(count_suitable);
	assert(count_total);
	assert(more);

	for (i = 0; i < func->bb_count; i++) {
		bb = &func->bbs[i];
		value_curr = use_inj ? bb->faultinj_count : bb->faultcand_count;
		if (value == value_curr) {
			*count_total += 1;
			if (bb->faultinj_count > 0 &&
				(!logs || coverage_get_bb(coverage, bb) > 0)) {
				*count_suitable += 1;
			}
		} else if (value < value_curr) {
			*more = 1;
		}
	}
}

static void print_stats_per_count_line(
	const struct coverage_per_bb *coverage,
	int value,
	int use_inj,
	int *count_suitable,
	int *count_total,
	int *more) {
	struct function_node *entry;
	int i;

	assert(coverage);
	assert(count_suitable);
	assert(count_total);
	assert(more);

	for (i = 0; i < functions->entry_count; i++) {
		entry = functions->entries[i];
		while (entry) {
			print_stats_per_count_function(
				coverage,
				&entry->data,
				value,
				use_inj,
				count_suitable,
				count_total,
				more);
			entry = entry->next;
		}
	}
}

static void print_stats_per_count(
	const struct coverage_per_bb *coverage,
	int use_inj) {
	int count_suitable, count_total, more, value;

	assert(coverage);

	value = 0;
	do {
		more = 0;
		count_suitable = 0;
		count_total = 0;
		print_stats_per_count_line(
			coverage,
			value,
			use_inj,
			&count_suitable,
			&count_total,
			&more);
		if (count_total) {
			printf("%10d   %15d   %12d\n",
				value, count_suitable, count_total);
		}
		value++;
	} while (more);
}

static void print_stats(
	const struct coverage_per_bb *coverage,
	int verbose) {

	assert(coverage);

	printf("No faults requested, printing statistics instead.\n");
	printf("Processed %d functions, %d of which shared between multiple modules.\n", bb_list.func_count, bb_list.func_count_shared);
	printf("\n");
	printf("%-45s basic blocks   fault candidates\n", "");
#define PRINT_LINE(desc, field) printf("%-45s %12d   %16d\n", (desc), bb_list.stats.field.bb_count, bb_list.stats.field.fc_count)
	PRINT_LINE("suitable",                                  inj.exec);
	PRINT_LINE("suitable but never executed",               inj.noexec);
	PRINT_LINE("no compiler injections",                    noinj.exec);
	PRINT_LINE("no compiler injections and never executed", noinj.noexec);
	PRINT_LINE("no faults candidates",                      nocand.exec);
	PRINT_LINE("no faults candidates and never executed",   nocand.noexec);
	printf("%-45s ------------   ----------------\n", "");
	PRINT_LINE("total",                                     total);
#undef PRINT_LINE
	printf("\n");
	printf("Note that the \"fault candidates\" refers to all fault candidates in the\n");
	printf("basic blocks for which the specified conditions hold, not just those that\n");
	printf("were actually injected by the compiler\n");
	if (!verbose) return;
	printf("\n");
	printf("candidates   suitable blocks   total blocks\n");
	print_stats_per_count(coverage, 0);
	printf("\n");
	printf("injected     suitable blocks   total blocks\n");
	print_stats_per_count(coverage, 1);
}

static int select_faults_sort_compare(const void *p1, const void *p2) {
	const struct bb_info *bb1 = p1;
	const struct bb_info *bb2 = p2;

	if (bb1->order < bb2->order) return -1;
	if (bb1->order > bb2->order) return 1;
	return 0;
}

static uint64_t rand64(void) {
	/* RAND_MAX is only guaranteed to be at least 32767, which is
	 * not enough to randomly select a fault candidate;
	 * as a solution, combine multiple random numbers to fill up
	 * much of uint64_t
	 */
	return ((uint64_t) rand() << 30) +
		((uint64_t) rand() << 15) +
		(uint64_t) rand();
}

static void select_faults(
	const struct coverage_per_bb *coverage,
	int numfaults,
	int verbose) {
	struct bb_info *bb;
	int bb_fc_count, fc_index, fc_count, first, i, j;
	const struct function_bb *func_bb;
	const struct function_ref *ref;

	assert(coverage);

	/* check whether we can inject the requested number */
	if (numfaults > bb_list.bb_count) {
		fprintf(stderr, "warning: cannot inject %d faults, "
			"only %d basic blocks suitable for injection\n",
			numfaults, bb_list.bb_count);
		numfaults = bb_list.bb_count;
	}

	/* shuffle basic block list */
	for (j = 0; j < bb_list.bb_count; j++) {
		bb_list.bbs[j].order = rand();
	}
	qsort(bb_list.bbs, bb_list.bb_count, sizeof(bb_list.bbs[0]),
		select_faults_sort_compare);

	/* select faults */
	fc_count = bb_list.stats.inj.exec.fc_count;
	for (i = 0; i < numfaults; i++) {
		assert(fc_count > 0);
		fc_index = rand64() % fc_count;
		for (j = 0; j < bb_list.bb_count; j++) {
			bb = &bb_list.bbs[j];
			if (bb->selected) continue;

			bb_fc_count = bb->func->bbs[bb->func_bb_index].faultcand_count;
			fc_index -= bb_fc_count;
			if (fc_index < 0) {
				bb->selected = 1;
				fc_count -= bb_fc_count;
				break;
			}
		}
		assert(fc_index < 0);
	}

	/* print selected faults */
	if (verbose) {
		printf("faultspec\tfunction\tfunction_bb_index\tpath\tline\tbb_fc_count\tbb_fi_count\tbb_exec_count\n");
	}
	for (i = 0; i < bb_list.bb_count; i++) {
		bb = &bb_list.bbs[i];
		if (!bb->selected) continue;

		first = 1;
		ref = &bb->func->ref;
		do {
			if (first) {
				first = 0;
			} else {
				printf(":");
			}
			assert(bb->func_bb_index < ref->module_func->bb_count);
			/* bb_index_first and func_bb_index are zero-based, output is one-based */
			printf("%s:%d", ref->module->name,
				ref->module_func->bb_index_first +
				bb->func_bb_index + 1);
			ref = ref->next;
		} while (ref);
		func_bb = &bb->func->bbs[bb->func_bb_index];
		if (verbose) {
			/* func_bb_index is zero-based, output is one-based */
			printf("\t%s\t%d\t", bb->func->name,
				bb->func_bb_index + 1);
			if (func_bb->fault_path) {
				printf("%s", func_bb->fault_path);
			}
			printf("\t");
			if (func_bb->fault_line) {
				printf("%d", func_bb->fault_line);
			}
			if (logs) {
				printf("\t%lld", (long long)
					coverage_get_bb(coverage, func_bb));
			}
		} else if (func_bb->fault_path) {
			printf("@%s:%d", func_bb->fault_path, func_bb->fault_line);
		}
		printf("\n");
	}
}

static void print_coverage_line(
	const char *name,
	int isused,
	struct coverage_stats *stats,
	const char *prefix) {
	if (prefix) printf("%s\t", prefix);
	printf("%s\t", name);
	if (isused == 0 || isused == 1) printf("%d", isused);
	printf("\t%.1f", stats->cov_func * 100.0 / stats->tot_func);
	printf("\t%.1f", stats->cov_bb * 100.0 / stats->tot_bb);
	printf("\t%.1f", stats->cov_ins * 100.0 / stats->tot_ins);
	printf("\t%.1f", stats->cov_fc * 100.0 / stats->tot_fc);
	printf("\t%.1f", stats->cov_inj * 100.0 / stats->tot_inj);
	printf("\t%.1f", stats->cov_loc * 100.0 / stats->tot_loc);
	printf("\t%d", stats->cov_func);
	printf("\t%d", stats->cov_bb);
	printf("\t%d", stats->cov_ins);
	printf("\t%d", stats->cov_fc);
	printf("\t%d", stats->cov_inj);
	printf("\t%d", stats->cov_loc);
	printf("\t%d", stats->tot_func);
	printf("\t%d", stats->tot_bb);
	printf("\t%d", stats->tot_ins);
	printf("\t%d", stats->tot_fc);
	printf("\t%d", stats->tot_inj);
	printf("\t%d", stats->tot_loc);
	printf("\n");
}

static void print_coverage_functions(
	const struct coverage_per_bb *coverage,
	const char *prefix) {
	struct coverage_stats stats;

	assert(coverage);
	assert(coverage->functions);

	coverage_compute_total(coverage, NULL, &stats, 0, NULL, 0);
	print_coverage_line("All modules", 2, &stats, prefix);

	coverage_compute_total(coverage, NULL, &stats, 1, NULL, 0);
	print_coverage_line("All used modules", 1, &stats, prefix);
}

static void print_coverage_modules(
	const struct coverage_per_bb *coverage,
	const char *prefix) {
	const struct module *module;
	const struct module_ll *node;
	struct coverage_stats stats;

	assert(coverage);
	assert(coverage->functions);

	for (node = modules; node; node = node->next) {
		module = node->module;
		coverage_compute_module(coverage, NULL, module, &stats);
		print_coverage_line(module->name, module_is_used(coverage, module), &stats, prefix);
	}
}

static void print_coverage(
	const struct coverage_per_bb *coverage,
	const char *prefix) {

	assert(coverage);
	assert(coverage->functions);

	if (prefix) printf("prefix\t");
	printf("module\tused\t"
		"func-pctcov\tbb-pctcov\tins-pctcov\tfc-pctcov\tinj-pctcov\tloc-pctcov\t"
		"func-cov\tbb-cov\tins-cov\tfc-cov\tinj_cov\tloc_cov\t"
		"func-total\tbb-total\tins-total\tfc-total\tinj-total\tloc-total\n");
	print_coverage_modules(coverage, prefix);
	print_coverage_functions(coverage, prefix);
}

static void print_coveragefunc_function(
	const struct coverage_per_bb *coverage,
	const char *prefix,
	const struct function *func) {
	struct function_bb *bb;
	int cov_bb = 0, cov_fc = 0, cov_inj = 0, cov_ins = 0, cov_loc = 0;
	execcount ec;
	int i;
	struct function_bb_line *line;
	const char *module;
	int tot_bb = 0, tot_fc = 0, tot_inj = 0, tot_ins = 0, tot_loc = 0;

	assert(coverage);
	assert(coverage->functions);
	assert(func);

	for (i = 0; i < func->bb_count; i++) {
		bb = &func->bbs[i];
		tot_bb++;
		tot_fc += bb->faultcand_count;
		tot_inj += bb->faultinj_count;
		tot_ins += bb->instr_count;
		ec = coverage_get_bb(coverage, bb);
		if (ec > 0) {
			cov_bb++;
			cov_fc += bb->faultcand_count;
			cov_inj += bb->faultinj_count;
			cov_ins += bb->instr_count;
		}
		for (line = bb->lines; line; line = line->next) {
			tot_loc++;
			if (ec > 0) cov_loc++;
		}
	}

	if (func->ref.next) {
		module = "<MULTIPLE>";
	} else {
		module = func->ref.module->name;
	}

	if (prefix) printf("%s\t", prefix);
	printf("%s\t%s\t%s\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%d\t%d\t%d\t%d\t%d\t%d"
		"\t%d\t%d\t%d\t%d\n",
		func->name, func->path,
		module,
		function_is_in_used_module(coverage, func) ? 1 : 0,
		cov_bb * 100.0 / tot_bb,
		cov_ins * 100.0 / tot_ins,
		cov_fc * 100.0 / tot_fc,
		cov_inj * 100.0 / tot_inj,
		cov_loc * 100.0 / tot_loc,
		cov_bb,
		cov_ins, cov_fc,
		cov_inj, cov_loc,
		tot_bb,
		tot_ins, tot_fc,
		tot_inj, tot_loc);
}

static void print_coveragefunc(
	const struct coverage_per_bb *coverage,
	const char *prefix) {
	struct function_node *entry;
	int i;

	assert(coverage);
	assert(coverage->functions);

	if (prefix) printf("prefix\t");
	printf("name\tpath\tmodule\tin-used-module\t"
		"bb-pctcov\tins-pctcov\tfc-pctcov\tinj-pctcov\tloc-pctcov\t"
		"bb-cov\tins-cov\tfc-cov\tinj_cov\tloc_cov\t"
		"bb-total\tins-total\tfc-total\tinj-total\tloc-total\n");
	for (i = 0; i < functions->entry_count; i++) {
		entry = functions->entries[i];
		while (entry) {
			print_coveragefunc_function(coverage, prefix, &entry->data);
			entry = entry->next;
		}
	}
}

static void usage(const char *msg) {
	printf("%s\n", msg);
	printf("\n");
	printf("usage:\n");
	printf("  faultpicker [-bcCSv] [-n numfaults] [-p prefix] [-s seed] [-t faulttype]...\n");
	printf("              [-T faulttype]... mappath... [logpath...]\n");
	printf("\n");
	printf("faultpicker randomly selects a number of basic blocks with\n");
	printf("fault candidates in the specified map file(s) generated by the EDFI LLVM pass.\n");
	printf("If one or more log files are also specified, only basic blocks that are executed\n");
	printf("at least once in at least one of the logs are selected. The output consists of\n");
	printf("modulename:bbindex pairs that can be supplied to the QEMU hypermem device\n");
	printf("faultspec parameter.\n");
	printf("\n");
	printf("-c           - Print coverage statistics.\n");
	printf("-C           - Print coverage statistics per function.\n");
	printf("-n numfaults - Number of faults to be selected. If not specified, the default\n");
	printf("               is to print statistics without selecting any faults.\n");
	printf("-p prefix    - Prefix column added for each row of the coverage table.\n");
	printf("-s seed      - Random seed used to get reproducable results. If not specified,\n");
	printf("               a random seed is derived from the current system time.\n");
	printf("-S           - Log files are in old edfistats format (HASE2014 paper).\n");
	printf("-t faulttype - Select only the specified fault type(s).\n");
	printf("-T faulttype - Exclude the specified fault type(s).\n");
	printf("-v           - Verbose output.\n");
	printf("mappath      - Map file generated by the EDFI LLVM pass. One or more files\n");
	printf("               must be specified, one for each module that is a candidate\n");
	printf("               for fault injection. Map files require a .map extension to be\n");
	printf("               recognized.\n");
	printf("logpath      - Log file generated by the QEMU hypermem device that includes\n");
	printf("               one or more statistics dumps. This parameter can be specified\n");
	printf("               any number of times. Basic blocks are considered for\n");
	printf("               fault injection if it is executed in any of the logs.\n");
	printf("               If the parameter is not specified, all blocks are considered.\n");
	printf("\n");
	printf("It is assumed that the EDFI LLVM pass has been invoked with the\n");
	printf("-fault-one-per-block argument, causing it to inject a single fault in every\n");
	printf("basic block that has at least one fault candidate. The probability of\n");
	printf("a basic block being selected is proportional to the number of fault candidates\n");
	printf("in the block to ensure that each fault candidate has approximately\n");
	printf("the same chance of being selected. On a single invocation of faultpicker,\n");
	printf("the same basic block is never selected twice. This holds even if it occurs\n");
	printf("in multiple modules (due to shared headers or static libraries),\n");
	printf("in which case it is injected in all applicable modules at the same time.\n");
	printf("The probability of shared code being selected does not increase with the number\n");
	printf("of modules it is included in.\n");
	exit(1);
}

int main(int argc, char **argv) {
	struct coverage_per_bb coverage_per_bb;
	int coverage = 0, coveragefunc = 0;
	int edfistats = 0;
	int numfaults = 0;
	int r;
	int seed = 0, seed_spec = 0;
	int verbose = 0;
	const char *path, *prefix = NULL;
	struct timeval timeval;
	struct string_ll *typeexclude = NULL;
	struct string_ll *typeinclude = NULL;

	/* handle options */
	while ((r = getopt(argc, argv, "cCn:p:s:St:T:v")) >= 0) {
		switch (r) {
		case 'c':
			coverage = 1;
			break;
		case 'C':
			coveragefunc = 1;
			break;
		case 'n':
			if (numfaults) usage("Parameter -n specified multiple times");
			numfaults = atoi(optarg);
			if (numfaults <= 0) usage("Parameter numfaults has an invalid value");
			break;
		case 'p':
			if (prefix) usage("Parameter -p specified multiple times");
			prefix = optarg;
			break;
		case 's':
			if (seed_spec) usage("Parameter -s specified multiple times");
			seed = atoi(optarg);
			seed_spec = 1;
			break;
		case 'S':
			edfistats = 1;
			mapparse_compatlevel = mapparse_compat_hase2014;
			break;
		case 't':
			string_ll_add(&typeinclude, optarg);
			break;
		case 'T':
			string_ll_add(&typeexclude, optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage("Unknown flag specified");
			break;
		}
	}
	if ((coverage || coveragefunc) && numfaults) usage("Parameters -c/-C and -n cannot be combined");
	if ((coverage || coveragefunc) && verbose) usage("Parameters -c/-C and -v cannot be combined");
	if (prefix && !coverage && !coveragefunc) usage("Parameter -p requires -c/-C");

	/* load map and log files */
	while (optind < argc) {
		path = argv[optind++];
		if (ends_with(path, ".map", 1)) {
			load_map(path);
		} else if (edfistats) {
			load_log_edfistats(path);
		} else {
			load_log(path);
		}
	}
	if (!modules) usage("No map files specified");
	if (!modules->next && !modules->module->name) {
		modules->module->name = "";
	}

	/* further initalization */
	dbgprintf("numfaults=%d\n", numfaults);

	if (!seed_spec) {
		timeval = gettimeofday_checked();
		seed = 1000000 * timeval.tv_sec + timeval.tv_usec;
	}
	dbgprintf("seed=%d\n", seed);
	srand(seed);

#if DEBUGLEVEL >= 1
	modules_dump(modules);
	execcounts_dump(logs);
#endif

	dbgprintf("checking for duplicate map files\n");
	modules_sort(&modules);
	check_duplicate_map();
#if DEBUGLEVEL >= 1
	execcounts_dump(logs);
#endif

	dbgprintf("applying fault selection\n");
	fault_select(typeexclude, typeinclude);

	dbgprintf("making a list of functions\n");
	functions = functions_build_table(modules);

	dbgprintf("aggregating logs\n");
	execcounts_deduplicate(&logs, 1);
#if DEBUGLEVEL >= 1
	execcounts_dump(logs);
#endif

	dbgprintf("determining execution count per basic block\n");
	coverage_init(&coverage_per_bb, functions);
	coverage_add_execcount_list(&coverage_per_bb, logs, modules);
#if DEBUGLEVEL >= 1
	functions_dump(functions);
#endif

	if (logs) {
		dbgprintf("verifying whether traces are available "
			"for each module\n");
		check_map_without_log();
	}

	dbgprintf("listing suitable fault candidates\n");
	functions_get_injectable_bbs(
		&coverage_per_bb,
		functions,
		&bb_list,
		logs ? 1 : 0);
#if DEBUGLEVEL >= 1
	bb_info_list_dump(&bb_list);
#endif

	if (numfaults > 0) {
		dbgprintf("performing fault selection\n");
		select_faults(&coverage_per_bb, numfaults, verbose);
	} else if (coverage || coveragefunc) {
		if (coverage) print_coverage(&coverage_per_bb, prefix);
		if (coveragefunc) print_coveragefunc(&coverage_per_bb, prefix);
	} else {
		dbgprintf("no faults selected, dumping stats instead\n");
		print_stats(&coverage_per_bb, verbose);
	}

	coverage_free(&coverage_per_bb);
	functions_free(functions);
	modules_free(modules);
	modules = NULL;
	return 0;
}
