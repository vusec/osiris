#ifndef MODULE_H
#define MODULE_H

#include "common.h"

struct module_bb_line {
	struct module_bb_line *next;
	struct linepath data;
};

struct module_bb {
	int faultcand_count;
	int faultinj_count;
	int instr_count;
	struct module_bb_line *lines;
	char *fault_path;
	int fault_line;
	char *fault_type;
};

struct function;

struct module_func {
	char *name;
	char *path;
	char *pathFixed;
	int bb_index_first; /* zero-based, first block of module is zero */
	int bb_count;
	struct module_bb *bbs;
	struct function *global_func;
};

struct module {
	char *name;
	int func_count;
	struct module_func *funcs;
};

struct module_ll {
	struct module_ll *next;
	struct module *module;
};

struct module_const_ll {
	struct module_const_ll *next;
	const struct module *module;
};

const int module_bbindex_to_global(const struct module *module, int bbindex_mod);
const struct module *module_find(const struct module_ll *list, const char *name);
const struct module *module_find_with_namelen(
	const struct module_ll *list,
	const char *name, size_t namelen);
const struct module *module_find_const(
	const struct module_const_ll *list,
	const char *name);
const struct module *module_find_const_with_namelen(
	const struct module_const_ll *list,
	const char *name, size_t namelen);
const struct module *module_find_duplicate(const struct module_ll *list);
const struct module_bb *module_get_bb_by_index(
	const struct module *module,
	int bb_index,
	const struct module_func **func_p);
void module_free(struct module *module);
struct module *module_load_from_map(const char *mappath);
struct module *module_load_from_map_ll(const char *mappath, struct module_ll **ll);
int modules_count_functions(struct module_ll *list);
void modules_dump(struct module_ll *list);
void modules_free(struct module_ll *list);
void modules_sort(struct module_ll **list_p);

#endif
