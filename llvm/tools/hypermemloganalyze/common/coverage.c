#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "coverage.h"
#include "function.h"
#include "helper.h"
#include "linecounter.h"
#include "logexeccounts.h"
#include "module.h"

static void coverage_add_execcount_function(
	struct coverage_per_bb *coverage,
	const struct function *func,
	int module_bb_count,
	const execcount *module_bbs,
	int *bb_index) {
	execcount ec;
	int func_covered = 0;
	int global_bb_index;
	int global_func_index;
	int i;

	/* note: *bb_index is zero-based */

	assert(coverage);
	assert(coverage->functions);
	assert(coverage->bbs);
	assert(coverage->funcs);
	assert(func);
	assert(module_bbs);
	assert(bb_index);

	for (i = 0; i < func->bb_count; i++) {
		if (*bb_index < module_bb_count) {
			ec = module_bbs[*bb_index];
			global_bb_index = func->bbs[i].global_bb_index;
			assert(global_bb_index < coverage->functions->global_bb_count);
			coverage->bbs[global_bb_index] += ec;
			if (ec > 0) func_covered = 1;
		}
		(*bb_index)++;
	}
	if (func_covered) {
		global_func_index = func->global_func_index;
		assert(global_func_index < coverage->functions->global_func_count);
		coverage->funcs[global_func_index] = 1;
	}
}

static void coverage_add_module_to_list(
	struct coverage_per_bb *coverage,
	const struct module *module) {
	struct module_const_ll *node;

	assert(coverage);
	assert(module);
	assert(module->name);

	if (module_find_const(coverage->modules, module->name)) return;

	node = CALLOC(1, struct module_const_ll);
	node->module = module;
	node->next = coverage->modules;
	coverage->modules = node;
}

void coverage_add_execcount_module(
	struct coverage_per_bb *coverage,
	const struct module *module,
	int module_bb_count,
	const execcount *module_bbs) {
	int bb_index, i;
	const struct function_node *func;
	struct module_func *module_func;

	assert(coverage);
	assert(coverage->functions);
	assert(module_bbs);
	assert(module);

	coverage_add_module_to_list(coverage, module);

	/* bb_index is zero-based */
	bb_index = 0;
	for (i = 0; i < module->func_count; i++) {
		module_func = &module->funcs[i];
		func = functions_find(coverage->functions,
			module_func->name, module_func->path);
		if (!func) {
			fprintf(stderr, "error: function %s in %s (module %s) "
				"not found in the function table\n",
				module_func->name, module_func->path,
				module->name);
			exit(-1);
		}
		coverage_add_execcount_function(
			coverage,
			&func->data,
			module_bb_count,
			module_bbs,
			&bb_index);
	}

	if (bb_index != module_bb_count) {
		fprintf(stderr, "error: number of basic blocks in logs (%d) "
			"is inconsistent with number in map file (%d) "
			"for module %s\n", module_bb_count, bb_index,
			module->name);
		exit(1);
	}
}

static void coverage_add_execcount_module_ec(
	struct coverage_per_bb *coverage,
	const struct module_execcount *module_ec,
	const struct module *module) {

	assert(coverage);
	assert(module_ec);
	assert(module_ec->bbs);
	assert(module);
	assert(strcmp(module->name, module_ec->name) == 0);

	coverage_add_execcount_module(
		coverage,
		module,
		module_ec->bb_count,
		module_ec->bbs);
}

void coverage_add_execcount_list(
	struct coverage_per_bb *coverage,
	const struct module_execcount_ll *execcount_list,
	const struct module_ll *module_list) {
	const struct module *module;

	assert(coverage);

	while (execcount_list) {
		module = module_find(module_list, execcount_list->data.name);
		if (module) {
			coverage_add_execcount_module_ec(
				coverage,
				&execcount_list->data,
				module);
		} else {
			fprintf(stderr, "warning: logs reference module \"%s\" "
				"but no map file is specified for this module; "
				"no faults will be injected in this module\n",
				execcount_list->data.name);
		}
		execcount_list = execcount_list->next;
	}
}

void coverage_free(struct coverage_per_bb *coverage) {
	struct module_const_ll *node, *nodenext;

	assert(coverage);

	if (coverage->bbs) FREE(coverage->bbs);
	if (coverage->funcs) FREE(coverage->funcs);

	for (node = coverage->modules; node; node = nodenext) {
		nodenext = node->next;
		FREE(node);
	}
	memset(coverage, 0, sizeof(*coverage));
}

void coverage_init(
	struct coverage_per_bb *coverage,
	const struct function_hashtable *functions) {
	assert(coverage);
	assert(functions);

	memset(coverage, 0, sizeof(*coverage));
	coverage->functions = functions;
	coverage->bbs = CALLOC(functions->global_bb_count, execcount);
	coverage->funcs = CALLOC(functions->global_func_count, unsigned char);
}

static void functions_get_coverage_lines(
	struct linecounter_state *counter,
	const struct function_bb_line *lines) {

	assert(counter);

	while (lines) {
		linecounter_add(counter, &lines->data);
		lines = lines->next;
	}
}

static void coverage_compute_func(
	const struct coverage_per_bb *coverage,
	const struct coverage_per_bb *coverage_exclude,
	const struct function *func,
	struct coverage_stats *stats,
	int only_used_modules,
	struct linecounter_state *lines_cov,
	struct linecounter_state *lines_tot) {
	const struct function_bb *bb;
	int cov_func, ex_func, i;

	assert(coverage);
	assert(func);
	assert(stats);
	assert(lines_cov);
	assert(lines_tot);

	if (only_used_modules && !function_is_in_used_module(coverage, func)) {
		return;
	}

	cov_func = 0;
	ex_func = 0;
	for (i = 0; i < func->bb_count; i++) {
		bb = &func->bbs[i];
		if (coverage_exclude &&
			coverage_get_bb(coverage_exclude, bb) > 0) {
			ex_func = 1;
			continue;
		}
		if (coverage_get_bb(coverage, bb) > 0) {
			cov_func = 1;
			stats->cov_bb++;
			stats->cov_ins += bb->instr_count;
			stats->cov_fc += bb->faultcand_count;
			stats->cov_inj += bb->faultinj_count;
			functions_get_coverage_lines(lines_cov, bb->lines);
		}
		stats->tot_bb++;
		stats->tot_ins += bb->instr_count;
		stats->tot_fc += bb->faultcand_count;
		stats->tot_inj += bb->faultinj_count;
		functions_get_coverage_lines(lines_tot, bb->lines);
	}
	if (!ex_func) {
		stats->cov_func += cov_func;
		stats->tot_func++;
	}
}

static int path_match(const char *path, const char *match, size_t matchlen) {
	size_t pathlen;

	assert(path);
	assert(match);

	pathlen = strlen(path);
	if (pathlen < matchlen) return 0;
	if (memcmp(path, match, matchlen) != 0) return 0;
	if (pathlen == matchlen || matchlen < 1) return 1;
	if (match[matchlen - 1] != '/') return 0;
	return 1;
}

void coverage_compute_total(
	const struct coverage_per_bb *coverage,
	const struct coverage_per_bb *coverage_exclude,
	struct coverage_stats *stats,
	int only_used_modules,
	const char *path,
	size_t pathlen) {
	int i;
	struct linecounter_state lines_cov, lines_tot;
	struct function_node *node;

	assert(coverage);
	assert(coverage->functions);
	assert(!coverage_exclude || coverage->functions == coverage_exclude->functions);
	assert(stats);

	linecounter_init(&lines_cov);
	linecounter_init(&lines_tot);

	memset(stats, 0, sizeof(struct coverage_stats));
	for (i = 0; i < coverage->functions->entry_count; i++) {
		for (node = coverage->functions->entries[i]; node; node = node->next) {
			if (path &&
				!path_match(node->data.pathFixed, path, pathlen)) {
				continue;
			}
			coverage_compute_func(
				coverage,
				coverage_exclude,
				&node->data,
				stats,
				only_used_modules,
				&lines_cov,
				&lines_tot);
		}
	}

	stats->cov_loc = lines_cov.count;
	stats->tot_loc = lines_tot.count;

	linecounter_free(&lines_tot);
	linecounter_free(&lines_cov);
}

int module_is_used(
	const struct coverage_per_bb *coverage,
	const struct module *module) {

	assert(coverage);
	assert(module);
	assert(module->name);

	return module_find_const(coverage->modules, module->name) ? 1 : 0;
}

int function_is_in_used_module(
	const struct coverage_per_bb *coverage,
	const struct function *func) {
	const struct function_ref *ref;

	assert(coverage);
	assert(func);

	ref = &func->ref;
	do {
		if (module_is_used(coverage, ref->module)) return 1;
		ref = ref->next;
	} while (ref);
	return 0;
}

void coverage_compute_module(
	const struct coverage_per_bb *coverage,
	const struct coverage_per_bb *coverage_exclude,
	const struct module *module,
	struct coverage_stats *stats) {
	const struct module_func *func;
	int i;
	struct linecounter_state lines_cov, lines_tot;

	assert(module);
	assert(stats);

	linecounter_init(&lines_cov);
	linecounter_init(&lines_tot);

	memset(stats, 0, sizeof(struct coverage_stats));
	for (i = 0; i < module->func_count; i++) {
		func = &module->funcs[i];
		assert(func->global_func);
		coverage_compute_func(
			coverage,
			coverage_exclude,
			func->global_func,
			stats,
			0,
			&lines_cov,
			&lines_tot);
	}

	stats->cov_loc = lines_cov.count;
	stats->tot_loc = lines_tot.count;

	linecounter_free(&lines_tot);
	linecounter_free(&lines_cov);
}
