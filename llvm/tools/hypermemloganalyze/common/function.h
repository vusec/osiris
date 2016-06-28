#ifndef FUNCTION_H
#define FUNCTION_H

#include "common.h"

struct module_execcount_ll;
struct module_ll;
struct linecounter_state;

struct function_bb_line {
	struct function_bb_line *next;
	struct linepath data;
};

struct function_bb {
	int faultcand_count;
	int faultinj_count;
	int instr_count;
	struct function_bb_line *lines;
	const char *fault_path; /* belongs to struct module_bb */
	int fault_line;
	const char *fault_type; /* belongs to struct module_bb */
	int global_bb_index; /* zero-based */
};

struct function_ref {
	struct function_ref *next;
	const struct module *module; /* belongs to caller */
	const struct module_func *module_func; /* belongs to struct module */
};

struct function {
	const char *name; /* belongs to struct module_func */
	const char *path; /* belongs to struct module_func */
	const char *pathFixed; /* belongs to struct module_func */
	int bb_count;
	struct function_bb *bbs;
	struct function_ref ref;
	int global_func_index;
};

struct function_node {
	struct function_node *next;
	struct function data;
};

struct function_hashtable {
	size_t entry_count;
	struct function_node **entries;
	size_t global_bb_count;
	const struct function_bb **global_bbs; /* elements belong to caller */
	size_t global_func_count;
	const struct function **global_funcs; /* elements belong to caller */
	const struct module_ll *modules; /* belongs to caller */
};

struct function_statistics {
	int func_count;
	int bb_count;
	int fc_count;
	int fi_count;
};

struct function_hashtable *functions_build_table(
	struct module_ll *module_list);
void functions_dump(struct function_hashtable *functions);
const struct function_node *functions_find(
	const struct function_hashtable *functions,
	const char *name, const char *path);
void functions_free(struct function_hashtable *functions);

#endif
