#ifndef COVERAGE_H
#define COVERAGE_H

#include "common.h"
#include "function.h"

struct linecounter_state;
struct module;
struct module_execcount_ll;
struct module_ll;
struct module_const_ll;

struct coverage_stats {
	int cov_func, cov_bb, cov_ins, cov_fc, cov_inj, cov_loc;
	int tot_func, tot_bb, tot_ins, tot_fc, tot_inj, tot_loc;
};

struct coverage_per_bb {
	const struct function_hashtable *functions;
	execcount *bbs;
	unsigned char *funcs;
	struct module_const_ll *modules;
};

void coverage_add_execcount_list(
	struct coverage_per_bb *coverage,
	const struct module_execcount_ll *execcount_list,
	const struct module_ll *module_list);
void coverage_add_execcount_module(
	struct coverage_per_bb *coverage,
	const struct module *module,
	int module_bb_count,
	const execcount *module_bbs);
void coverage_compute_module(
	const struct coverage_per_bb *coverage,
	const struct coverage_per_bb *coverage_exclude,
	const struct module *module,
	struct coverage_stats *stats);
void coverage_compute_total(
	const struct coverage_per_bb *coverage,
	const struct coverage_per_bb *coverage_exclude,
	struct coverage_stats *stats,
	int only_used_modules,
	const char *path,
	size_t pathlen);
void coverage_free(struct coverage_per_bb *coverage);
void coverage_init(
	struct coverage_per_bb *coverage,
	const struct function_hashtable *functions);
int function_is_in_used_module(
	const struct coverage_per_bb *coverage,
	const struct function *func);
int module_is_used(
	const struct coverage_per_bb *coverage,
	const struct module *module);

static inline execcount coverage_get_bb(
	const struct coverage_per_bb *coverage,
	const struct function_bb *bb) {
	assert(coverage);
	assert(coverage->functions);
	assert(bb);
	assert(bb->global_bb_index < coverage->functions->global_bb_count);
	return coverage->bbs[bb->global_bb_index];
}

static inline unsigned char coverage_get_func(
	const struct coverage_per_bb *coverage,
	const struct function *func) {
	return coverage->funcs[func->global_func_index];
}

#endif
