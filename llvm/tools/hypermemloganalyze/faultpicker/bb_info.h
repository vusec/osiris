#ifndef BB_INFO_H
#define BB_INFO_H

#include "common.h"

struct coverage_per_bb;
struct function;

struct bb_info {
	const struct function *func;
	int func_bb_index; /* func_bb_index is 0-based */
	int order;
	int selected;
};
struct bb_info_stats3 {
	int bb_count;
	int fc_count;
};
struct bb_info_stats2 {
	struct bb_info_stats3 exec;
	struct bb_info_stats3 noexec;
};
struct bb_info_stats {
	struct bb_info_stats2 inj;
	struct bb_info_stats2 noinj;
	struct bb_info_stats2 nocand;
	struct bb_info_stats3 total;
};
struct bb_info_list {
	struct bb_info_stats stats;
	int func_count;
	int func_count_shared;
	int bb_count;
	struct bb_info *bbs;
};

struct function_hashtable;

void bb_info_list_dump(struct bb_info_list *bb_list);
void functions_get_injectable_bbs(
	const struct coverage_per_bb *coverage,
	const struct function_hashtable *functions,
	struct bb_info_list *bb_list,
	int only_executed);

#endif
