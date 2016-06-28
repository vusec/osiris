#ifndef LOGEXECCOUNTS_H
#define LOGEXECCOUNTS_H

#include "common.h"

struct module_execcount {
	char *name;
	int bb_count;
	execcount *bbs;
};

struct module_execcount_ll {
	struct module_execcount_ll *next;
	struct module_execcount data;
};

struct module_execcount_ll *execcounts_concat(struct module_execcount_ll *list1,
	struct module_execcount_ll *list2);
void execcounts_deduplicate(struct module_execcount_ll **list_p, int add);
void execcounts_dump(struct module_execcount_ll *list);
struct module_execcount *execcounts_find(struct module_execcount_ll *list,
	const char *name);
struct module_execcount_ll *execcounts_load_from_log(const char *logpath);

#endif
