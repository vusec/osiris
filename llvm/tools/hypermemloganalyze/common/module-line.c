#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "debug.h"
#include "helper.h"
#include "minixteststats.h"
#include "module.h"
#include "module-line.h"

void module_lines_free(struct module_lines *lines) {

	assert(lines);

	FREE(lines->lines);
}

static void module_lines_list_bb(
	struct module_lines *lines,
	const struct module *module,
	const struct minixtest_stats_module *moduletest,
	const struct module_func *func,
	const struct module_bb *bb,
	int bb_index,
	size_t *count_p) {
	struct module_line *line;
	const struct module_bb_line *line_bb;

	assert(lines);
	assert(module);
	assert(func);
	assert(count_p);

	for (line_bb = bb->lines; line_bb; line_bb = line_bb->next) {
		if (lines->lines) {
			line = &lines->lines[*count_p];
			line->module = module;
			line->moduletest = moduletest;
			line->bb_index = bb_index;
			line->line = &line_bb->data;
		}
		(*count_p)++;
	}
}

static void module_lines_list_func(
	struct module_lines *lines,
	const struct module *module,
	const struct minixtest_stats_module *moduletest,
	const struct module_func *func,
	size_t *count_p) {
	int i;

	assert(lines);
	assert(module);
	assert(func);
	assert(count_p);

	for (i = 0; i < func->bb_count; i++) {
		module_lines_list_bb(lines, module, moduletest, func,
			&func->bbs[i], func->bb_index_first + i, count_p);
	}
}

static void module_lines_list_module(struct module_lines *lines,
	const struct module *module,
	const struct minixtest_stats_module *moduletest, size_t *count_p) {
	int i;

	assert(lines);
	assert(module);
	assert(count_p);

	for (i = 0; i < module->func_count; i++) {
		module_lines_list_func(lines, module, moduletest,
			&module->funcs[i], count_p);
	}
}

static void module_lines_list_modules(struct module_lines *lines,
	const struct module_ll *modules,
	const struct minixtest_stats_test *test, size_t *count_p) {
	const struct module_ll *module;
	const struct minixtest_stats_module *moduletest;

	assert(lines);
	assert(count_p);
	assert(!*count_p);

	for (module = modules; module; module = module->next) {
		dbgprintf("listing lines for module %s, pass %d\n",
			module->module->name, lines->lines ? 2 : 1);
		if (test) {
			moduletest = minixtest_stats_find_module(test,
				module->module->name);
			if (!moduletest) {
				fprintf(stderr, "warning: no execution counts "
					"for module %s\n",
					module->module->name);
			}
		} else {
			moduletest = NULL;
		}
		module_lines_list_module(lines, module->module, moduletest,
			count_p);
	}
}

static int module_lines_sort_compare(const void *p1, const void *p2) {
	const struct module_line *l1 = p1;
	const struct module_line *l2 = p2;
	const struct linepath *lp1 = l1->line;
	const struct linepath *lp2 = l2->line;
	int r;

	r = strcmp(lp1->pathFixed, lp2->pathFixed);
	if (r) return r;

	if (lp1->line < lp2->line) return -1;
	if (lp1->line > lp2->line) return 1;

	r = strcmp(l1->module->name, l2->module->name);
	if (r) return r;

	return 0;
}

static void module_lines_sort(struct module_lines *lines) {

	assert(lines);
	assert(lines->lines);

	dbgprintf("sorting lines per file\n");
	qsort(lines->lines, lines->count, sizeof(lines->lines[0]),
		module_lines_sort_compare);

}

void module_lines_list(struct module_lines *lines,
	const struct module_ll *modules,
	const struct minixtest_stats_test *test) {
	size_t count = 0;

	assert(lines);

	memset(lines, 0, sizeof(*lines));
	module_lines_list_modules(lines, modules, test, &lines->count);
	lines->lines = CALLOC(lines->count, struct module_line);
	module_lines_list_modules(lines, modules, test, &count);
	assert(count == lines->count);

	module_lines_sort(lines);
}
