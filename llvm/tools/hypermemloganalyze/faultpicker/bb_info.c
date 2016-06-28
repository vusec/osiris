#include <assert.h>
#include <string.h>

#include "bb_info.h"
#include "coverage.h"
#include "debug.h"
#include "function.h"
#include "helper.h"
#include "module.h"

static void bb_info_list_dump_bb(struct bb_info *bb) {
	const struct function_ref *ref;

	assert(bb);
	assert(bb->func);

	dbgprintf("\tbb %d in function %s in %s%s", bb->func_bb_index + 1,
		bb->func->name, bb->func->path,
		bb->selected ? " (selected)" : "");

	ref = &bb->func->ref;
	do {
		/* bb_index_first and func_bb_index are zero-based, output is one-based */
		dbgprintf("; %s:%d", ref->module->name,
			ref->module_func->bb_index_first +
			bb->func_bb_index + 1);
	} while ((ref = ref->next));
	dbgprintf("\n");
}

void bb_info_list_dump(struct bb_info_list *bb_list) {
	int i;

	assert(bb_list);

#define PRINT(field) dbgprintf("bb_list.%s=%d\n", #field, bb_list->field)
	PRINT(stats.inj.exec.bb_count);
	PRINT(stats.inj.exec.fc_count);
	PRINT(stats.inj.noexec.bb_count);
	PRINT(stats.inj.noexec.fc_count);
	PRINT(stats.noinj.exec.bb_count);
	PRINT(stats.noinj.exec.fc_count);
	PRINT(stats.noinj.noexec.bb_count);
	PRINT(stats.noinj.noexec.fc_count);
	PRINT(stats.nocand.exec.bb_count);
	PRINT(stats.nocand.exec.fc_count);
	PRINT(stats.nocand.noexec.bb_count);
	PRINT(stats.nocand.noexec.fc_count);
	PRINT(func_count);
	PRINT(func_count_shared);
#undef PRINT

	for (i = 0; i < bb_list->bb_count; i++) {
		bb_info_list_dump_bb(&bb_list->bbs[i]);
	}
}

struct functions_get_injectable_bbs_state {
	struct bb_info_list *bb_list;
	int only_executed;
	int bb_index;
};

static void functions_get_injectable_bbs_bb(
	const struct coverage_per_bb *coverage,
	const struct function *function,
	const struct function_bb *bb,
	int func_bb_index,
	struct functions_get_injectable_bbs_state *state) {
	struct bb_info *bb_info;
	execcount ec;
	struct bb_info_stats2 *stats2;
	struct bb_info_stats3 *stats3;

	assert(coverage);
	assert(function);
	assert(bb);
	assert(state);
	assert(state->bb_list);

	ec = coverage_get_bb(coverage, bb);
	if (bb->faultinj_count > 0 &&
		(!state->only_executed || ec > 0)) {
		if (state->bb_list->bbs) {
			bb_info = &state->bb_list->bbs[state->bb_index++];
			bb_info->func = function;
			bb_info->func_bb_index = func_bb_index;
		} else {
			state->bb_list->bb_count++;
		}
	}
	if (!state->bb_list->bbs) {
		if (bb->faultcand_count <= 0) {
			stats2 = &state->bb_list->stats.nocand;
		} else if (bb->faultinj_count <= 0) {
			stats2 = &state->bb_list->stats.noinj;
		} else {
			stats2 = &state->bb_list->stats.inj;
		}
		if (!state->only_executed || ec > 0) {
			stats3 = &stats2->exec;
		} else {
			stats3 = &stats2->noexec;
		}
		stats3->bb_count++;
		stats3->fc_count += bb->faultcand_count;
		state->bb_list->stats.total.bb_count++;
		state->bb_list->stats.total.fc_count += bb->faultcand_count;
	}
}

static void functions_get_injectable_bbs_func(
	const struct coverage_per_bb *coverage,
	const struct function *function,
	struct functions_get_injectable_bbs_state *state) {
	int func_bb_index = 0, i;

	assert(coverage);
	assert(function);
	assert(state);
	assert(state->bb_list);

	/* criteria for selection:
	 * - there is a fault injected
	 * - is executed at least once (if we have logs)
	 */
	for (i = 0; i < function->bb_count; i++) {
		functions_get_injectable_bbs_bb(
			coverage,
			function,
			&function->bbs[i],
			func_bb_index,
			state);
		func_bb_index++;
	}
	if (!state->bb_list->bbs) {
		state->bb_list->func_count++;
		if (function->ref.next) {
			state->bb_list->func_count_shared++;
		}
	}
}

static void functions_get_injectable_bbs_func_entry(
	const struct coverage_per_bb *coverage,
	const struct function_node *entry,
	struct functions_get_injectable_bbs_state *state) {

	assert(state);
	assert(state->bb_list);

	while (entry) {
		functions_get_injectable_bbs_func(
			coverage, &entry->data, state);
		entry = entry->next;
	}
}

static void functions_get_injectable_bbs_internal(
	const struct coverage_per_bb *coverage,
	const struct function_hashtable *functions,
	struct functions_get_injectable_bbs_state *state) {
	int i;

	assert(functions);
	assert(state);
	assert(state->bb_list);

	for (i = 0; i < functions->entry_count; i++) {
		functions_get_injectable_bbs_func_entry(
			coverage, functions->entries[i], state);
	}
}

void functions_get_injectable_bbs(
	const struct coverage_per_bb *coverage,
	const struct function_hashtable *functions,
	struct bb_info_list *bb_list,
	int only_executed) {
	struct functions_get_injectable_bbs_state state;

	assert(functions);
	assert(bb_list);

	/* initialize state for functions called */
	memset(&state, 0, sizeof(state));
	memset(bb_list, 0, sizeof(*bb_list));
	state.bb_list = bb_list;
	state.only_executed = only_executed;

	/* count suitable blocks to allocate buffer */
	functions_get_injectable_bbs_internal(coverage, functions, &state);
	bb_list->bbs = CALLOC(bb_list->bb_count, struct bb_info);

	/* fill in buffer entries */
	state.bb_index = 0;
	functions_get_injectable_bbs_internal(coverage, functions, &state);
	assert(bb_list->bb_count == state.bb_index);
}
