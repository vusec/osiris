#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "function.h"
#include "helper.h"
#include "module.h"
#include "../../mapprint/mapparse.h"

struct module_load_from_map_state {
	struct module *module;
	struct module_func *func;
	int func_index;
	struct module_bb *bb;
	struct module_bb_line **bb_line_p;
	int abs_bb_index; /* zero-based, first block of module is zero */
	int func_bb_index; /* zero-based, first block of function is zero */
	const char *ins_path;
	int ins_line;
};

const int module_bbindex_to_global(const struct module *module, int bbindex_mod) {
	const struct module_func *func;
	int i;

	assert(module);
	assert(bbindex_mod >= 0);

	/* both bbindex_mod and the result are 0-based */
	for (i = 0; i < module->func_count; i++) {
		func = &module->funcs[i];
		if (bbindex_mod < func->bb_count) {
			assert(func->global_func->bb_count == func->bb_count);
			return func->global_func->bbs[bbindex_mod].global_bb_index;
		}
		bbindex_mod -= func->bb_count;
	}
	return -1;
}

static char *fixPath(const char *path) {
	const char *dirEnd, *pathElem;
	size_t pathlen;
	char *p, *pathFixed;

	if (!path) return NULL;

	/* fix incorrectly concatenated absolute paths */
	if (path[0] == '/') {
		dirEnd = strstr(path, "//");
		if (dirEnd) path = dirEnd + 1;
	}

	pathlen = strlen(path);
	pathFixed = MALLOC(pathlen + 1, char);

	/* remove /./ and /../ */
	p = pathFixed;
	while (*path) {
		/* move slash(es) */
		if (*path == '/') {
			*(p++) = '/';
			do { path++; } while (*path == '/');
		}

		/* scan path element */
		pathElem = path;
		while (*path && *path != '/') path++;

		/* skip /./ pattern */
		if (path - pathElem == 1 && pathElem[0] == '.') {
			path++;
			continue;
		}

		/* remove previous on /../ pattern */
		if (path - pathElem == 2 && pathElem[0] == '.' &&
			pathElem[1] == '.' && p > pathFixed + 1) {
			do { p--; } while (p > pathFixed && p[-1] != '/');
			path++;
			continue;
		}

		/* copy path element */
		memcpy(p, pathElem, path - pathElem);
		p += path - pathElem;
	}
	*p = 0;

	return pathFixed;
}

static void process_basic_block(void *param) {
	struct module_load_from_map_state *state = param;

	state->bb = NULL;
	state->bb_line_p = NULL;

	if (!state->func) return;

	if (!state->func->bbs) {
		state->func->bb_count++;
		return;
	}
	state->bb = &state->func->bbs[state->func_bb_index++];
	state->bb_line_p = &state->bb->lines;
	state->abs_bb_index++;

	assert(!*state->bb_line_p);
}

static void process_dinstruction(void *param, const char *path, mapfile_lineno_t line) {
	struct module_bb_line *bb_line;
	struct module_load_from_map_state *state = param;

	state->ins_path = path;
	state->ins_line = line;
	if (state->bb) state->bb->instr_count++;
	if (state->bb_line_p) {
		bb_line = *state->bb_line_p = CALLOC(1, struct module_bb_line);
		bb_line->data.path = STRDUP(path);
		bb_line->data.pathFixed = fixPath(bb_line->data.path);
		bb_line->data.line = line;
		bb_line->data.hash = hashstr(path) + line;
		state->bb_line_p = &bb_line->next;
	}
}

static void process_fault_candidate(void *param, const char *name) {
	struct module_load_from_map_state *state = param;

	if (state->bb) state->bb->faultcand_count++;
}

static void process_fault_injected(void *param, const char *name) {
	struct module_load_from_map_state *state = param;

	if (state->bb) {
		state->bb->faultinj_count++;
		if (state->bb->fault_path) {
			FREE(state->bb->fault_path);
		}
		state->bb->fault_path = STRDUP(state->ins_path);
		state->bb->fault_line = state->ins_line;
		if (state->bb->fault_type) {
			FREE(state->bb->fault_type);
		}
		state->bb->fault_type = STRDUP(name);
	}
}

static void process_function(void *param, const char *name, const char *relPath) {
	struct module_load_from_map_state *state = param;

	if (!state->module->funcs) {
		state->module->func_count++;
		return;
	}
	state->func = &state->module->funcs[state->func_index++];
	if (!state->func->name) state->func->name = STRDUP(name);
	if (!state->func->path) state->func->path = STRDUP(relPath);
	if (!state->func->pathFixed) state->func->pathFixed = fixPath(state->func->path);
	state->func->bb_index_first = state->abs_bb_index;
	state->bb = NULL;
	state->func_bb_index = 0;
}

static void process_instruction(void *param) {
	struct module_load_from_map_state *state = param;

	state->ins_path = NULL;
	state->ins_line = 0;
	if (state->bb) state->bb->instr_count++;
}

static void process_module_name(void *param, const char *name) {
	struct module_load_from_map_state *state = param;

	if (!state->module->name) state->module->name = STRDUP(name);
}

int modules_count_functions(struct module_ll *list) {
	int count = 0;
	while (list) {
		count += list->module->func_count;
		list = list->next;
	}
	return count;

}

static int namecmp(const char *name1, const char *name2, size_t name2len) {

	assert(name1);
	assert(name2);

	if (name2len != strlen(name1)) return 1;

	return memcmp(name1, name2, name2len);
}

const struct module *module_find(
	const struct module_ll *list,
	const char *name) {
	return module_find_with_namelen(list, name, strlen(name));
}

const struct module *module_find_with_namelen(
	const struct module_ll *list,
	const char *name,
	size_t namelen) {

	assert(name);

	while (list) {
		assert(list->module);
		assert(list->module->name);
		if (namecmp(list->module->name, name, namelen) == 0) {
			return list->module;
		}
		list = list->next;
	}
	return NULL;
}

const struct module *module_find_const(
	const struct module_const_ll *list,
	const char *name) {
	return module_find_const_with_namelen(list, name, strlen(name));
}

const struct module *module_find_const_with_namelen(
	const struct module_const_ll *list,
	const char *name,
	size_t namelen) {

	assert(name);

	while (list) {
		assert(list->module);
		assert(list->module->name);
		if (namecmp(list->module->name, name, namelen) == 0) {
			return list->module;
		}
		list = list->next;
	}
	return NULL;
}

const struct module *module_find_duplicate(const struct module_ll *list) {

	/* this is O(n^2) but we don't expect large n */
	while (list) {
		if (module_find(list->next, list->module->name)) {
			return list->module;
		}
		list = list->next;
	}
	return NULL;
}

const struct module_bb *module_get_bb_by_index(
	const struct module *module,
	int bb_index,
	const struct module_func **func_p) {
	const struct module_func *func;
	int i;

	/* note: bb_index is one-based */

	assert(module);
	assert(bb_index > 0);

	for (i = 0; i < module->func_count; i++) {
		func = &module->funcs[i];
		if (bb_index <= func->bb_count) {
			*func_p = func;
			return &func->bbs[bb_index - 1];
		}
		bb_index -= func->bb_count;
	}

	*func_p = NULL;
	return NULL;
}

struct module *module_load_from_map(const char *mappath) {
	struct mapparse_callbacks callbacks;
	int i;
	FILE *mapfile;
	struct module *module;
	struct module_load_from_map_state state;

	assert(mappath);

	/* open map file */
	mapfile = fopen(mappath, "r");
	if (!mapfile) {
		fprintf(stderr, "error: could not open map file %s: %s\n",
			mappath, strerror(errno));
		return NULL;
	}

	/* set up callbacks */
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.param = &state;
	callbacks.basic_block = process_basic_block;
	callbacks.dinstruction = process_dinstruction;
	callbacks.fault_candidate = process_fault_candidate;
	callbacks.fault_injected = process_fault_injected;
	callbacks.function = process_function;
	callbacks.instruction = process_instruction;
	callbacks.module_name = process_module_name;

	/* allocate result buffer */
	module = CALLOC(1, struct module);

	/* count functions */
	memset(&state, 0, sizeof(state));
	state.module = module;
	mapparse_file(&callbacks, mappath, mapfile);

	/* allocate functions buffer */
	module->funcs = CALLOC(module->func_count, struct module_func);

	/* store function info, count basic blocks per function */
	memset(&state, 0, sizeof(state));
	state.module = module;
	rewind(mapfile);
	mapparse_file(&callbacks, mappath, mapfile);

	/* allocate bb buffers */
	for (i = 0; i < module->func_count; i++) {
		module->funcs[i].bbs =
			CALLOC(module->funcs[i].bb_count, struct module_bb);
	}

	/* count faults per basic block */
	memset(&state, 0, sizeof(state));
	state.module = module;
	rewind(mapfile);
	mapparse_file(&callbacks, mappath, mapfile);

	/* close map file */
	fclose(mapfile);
	return module;
}

struct module *module_load_from_map_ll(const char *mappath, struct module_ll **ll) {
	struct module *module;
	struct module_ll *node;

	assert(mappath);
	assert(ll);

	module = module_load_from_map(mappath);
	if (module) {
		node = CALLOC(1, struct module_ll);
		node->module = module;
		node->next = *ll;
		*ll = node;
	}
	return module;
}

static void modules_dump_functions(struct module *module) {
	struct module_bb *bb;
	struct module_func *func;
	int i, j;

	assert(module);

	for (i = 0; i < module->func_count; i++) {
		func = &module->funcs[i];
		dbgprintf("\tmodule %s, function %s in %s, bb_count=%d, "
			"bb_index_first=%d\n", module->name, func->name,
			func->path, func->bb_count, func->bb_index_first + 1);
		for (j = 0; j < func->bb_count; j++) {
			bb = &func->bbs[j];
			dbgprintf_v("\t\tbb%d(%d), fc=%d, fi=%d, ins=%d",
				func->bb_index_first + j + 1, j + 1,
				bb->faultcand_count, bb->faultinj_count,
				bb->instr_count);
			if (bb->fault_path) {
				dbgprintf_v(", fpath=\"%s\"", bb->fault_path);
			}
			if (bb->fault_line) {
				dbgprintf_v(", fline=%d", bb->fault_line);
			}
			if (bb->fault_type) {
				dbgprintf_v(", ftype=%s", bb->fault_type);
			}
			dbgprintf_v("\n");
		}
	}
}

void modules_dump(struct module_ll *list) {
	int count = 0;

	if (!list) {
		dbgprintf("no functions\n");
		return;
	}

	while (list) {
		dbgprintf("module %s, func_count=%d\n", list->module->name,
			list->module->func_count);
		count++;
		modules_dump_functions(list->module);
		list = list->next;
	}
	dbgprintf("%d modules\n", count);
}

static void module_free_bb_line(struct module_bb_line *line) {

	assert(line);

	if (line->data.path) FREE(line->data.path);
	if (line->data.pathFixed) FREE(line->data.pathFixed);
	memset(line, 0, sizeof(*line));
}

static void module_free_bb_lines(struct module_bb_line *lines) {
	struct module_bb_line *next;

	while (lines) {
		next = lines->next;
		module_free_bb_line(lines);
		FREE(lines);
		lines = next;
	}
}

static void module_free_bb(struct module_bb *bb) {

	assert(bb);

	if (bb->fault_path) FREE(bb->fault_path);
	if (bb->fault_type) FREE(bb->fault_type);
	module_free_bb_lines(bb->lines);
	memset(bb, 0, sizeof(*bb));
}

static void module_free_func(struct module_func *func) {
	int i;

	assert(func);

	if (func->name) FREE(func->name);
	if (func->path) FREE(func->path);
	if (func->pathFixed) FREE(func->pathFixed);
	if (func->bbs) {
		for (i = 0; i < func->bb_count; i++) {
			module_free_bb(&func->bbs[i]);
		}
		FREE(func->bbs);
	}
	memset(func, 0, sizeof(*func));
}

void module_free(struct module *module) {
	int i;

	assert(module);

	if (module->name) FREE(module->name);
	if (module->funcs) {
		for (i = 0; i < module->func_count; i++) {
			module_free_func(&module->funcs[i]);
		}
		FREE(module->funcs);
	}
	memset(module, 0, sizeof(*module));
}

void modules_free(struct module_ll *list) {
	struct module_ll *next;

	while (list) {
		next = list->next;
		if (list->module) {
			module_free(list->module);
			FREE(list->module);
		}
		memset(list, 0, sizeof(*list));
		FREE(list);
		list = next;
	}
}

static int module_sort_compare(const void *p1, const void *p2) {
	const struct module_ll *node1 = *(const struct module_ll **) p1;
	const struct module_ll *node2 = *(const struct module_ll **) p2;

	return strcmp(node1->module->name, node2->module->name);
}

void modules_sort(struct module_ll **list_p) {
	int count, i, index;
	struct module_ll **array, *node;

	assert(list_p);

	/* return if nothing to do? we later assume
	 * there is at least one element
	 */
	if (!*list_p) return;

	/* count modules */
	count = 0;
	node = *list_p;
	do {
		count++;
	} while ((node = node->next));

	/* store the pointer in an array for sorting */
	array = CALLOC(count, struct module_ll *);
	index = 0;
	node = *list_p;
	do {
		array[index++] = node;
	} while ((node = node->next));
	assert(count == index);

	/* now sort the array */
	qsort(array, count, sizeof(array[0]), module_sort_compare);

	/* re-link the linked list; assumes at least one element */
	*list_p = array[0];
	for (i = 0; i < count - 1; i++) {
		array[i]->next = array[i + 1];
	}
	array[count - 1]->next = NULL;

	/* clean up */
	FREE(array);
}
