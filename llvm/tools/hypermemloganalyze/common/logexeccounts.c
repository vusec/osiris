#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "helper.h"
#include "logexeccounts.h"
#include "logparse.h"

struct parse_arg {
	struct module_execcount_ll **last_p;
};

static void execcounts_free(struct module_execcount_ll *node) {
	if (node->data.name) FREE(node->data.name);
	if (node->data.bbs) FREE(node->data.bbs);
	FREE(node);
}

struct module_execcount_ll *execcounts_concat(struct module_execcount_ll *list1,
	struct module_execcount_ll *list2) {
	struct module_execcount_ll *last1;

	if (!list1) return list2;
	if (!list2) return list1;

	last1 = list1;
	while (last1->next) last1 = last1->next;
	last1->next = list2;

	return list1;
}

static void execcounts_add(struct module_execcount *dst,
	const struct module_execcount *src) {
	int i;

	assert(dst);
	assert(src);
	assert(strcmp(dst->name, src->name) == 0);

	if (dst->bb_count != src->bb_count) {
		fprintf(stderr, "error: bb count mismatch between logs "
			"for module %s: %d and %d\n",
			dst->name, dst->bb_count, src->bb_count);
		exit(1);
	}

	for (i = 0; i < dst->bb_count; i++) {
		dst->bbs[i] += src->bbs[i];
	}
}

void execcounts_deduplicate(struct module_execcount_ll **list_p, int add) {
	struct module_execcount_ll *list;
	struct module_execcount *other;

	assert(list_p);

	/* this is O(n^2) but we don't expect large n */
	while ((list = *list_p)) {
		other = execcounts_find(list->next, list->data.name);
		if (!other) {
			list_p = &list->next;
			continue;
		}
		if (add) {
			execcounts_add(other, &list->data);
		} else {
			dbgprintf("module %s listed multiple times, "
				"using last result\n", list->data.name);
		}
		*list_p = list->next;
		execcounts_free(list);
	}
}

void execcounts_dump_bbs(struct module_execcount *module) {
	execcount count, countrep = 0;
	int i, repeats = 0;

	for (i = 0; i <= module->bb_count; i++) {
		count = (i < module->bb_count) ? module->bbs[i] : -1;
		if (countrep == count) {
			repeats++;
			continue;
		}
		if (repeats > 1) {
			dbgprintf_v("\tbb%d-%d: %llu\n", i - repeats + 1, i,
				(long long) countrep);
		} else if (repeats > 0) {
			dbgprintf_v("\tbb%d: %llu\n", i, (long long) countrep);
		}
		countrep = count;
		repeats = 1;
	}
}

void execcounts_dump(struct module_execcount_ll *list) {
	int module_count = 0;

	if (!list) {
		dbgprintf("no execcount modules\n");
		return;
	}

	while (list) {
		dbgprintf("execcount module %s, bb_count=%d\n", list->data.name,
			list->data.bb_count);
		execcounts_dump_bbs(&list->data);
		module_count++;
		list = list->next;
	}
	dbgprintf("%d execcount modules\n", module_count);
}

struct module_execcount *execcounts_find(struct module_execcount_ll *list,
	const char *name) {
	while (list) {
		if (strcmp(list->data.name, name) == 0) {
			return &list->data;
		}
		list = list->next;
	}
	return NULL;
}

static void logparse_callback_edfi_dump_stats_module(
	const struct logparse_callback_state *state,
	const char *name, size_t namesz,
	const char *msg, size_t msgsz,
	execcount *bbs,	size_t bb_count) {
	struct parse_arg *arg = state->arg;
	struct module_execcount_ll *node;
	const char *p;

	if (!bbs) return;

	/* ignore additional specification after @-sign */
	for (p = name + namesz - 1; p >= name; p--) {
		if (*p == '@') {
			namesz = p - name;
			break;
		}
	}

	/* add a linked list node for this module */
	*(arg->last_p) = node = CALLOC(1, struct module_execcount_ll);
	arg->last_p = &node->next;

	/* copy data to store into node */
	node->data.name = STRNDUP(name, namesz);
	node->data.bbs = MALLOC(bb_count, execcount);
	node->data.bb_count = bb_count;
	memcpy(node->data.bbs, bbs, bb_count * sizeof(execcount));
}

struct module_execcount_ll *execcounts_load_from_log(const char *logpath) {
	struct parse_arg arg;
	struct logparse_callbacks callbacks;
	struct module_execcount_ll *list = NULL;

	assert(logpath);

	memset(&arg, 0, sizeof(arg));
	arg.last_p = &list;

	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.arg = &arg;
	callbacks.edfi_dump_stats_module = logparse_callback_edfi_dump_stats_module;

	logparse_from_path(logpath, &callbacks, NULL);

	return list;
}
