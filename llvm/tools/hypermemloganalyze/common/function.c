#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"
#include "function.h"
#include "helper.h"
#include "module.h"

static int round_up_to_2n(int value) {
	int result = 1;

	assert(value >= 0);

	while (result < value && result * 2 > result) result *= 2;

	return result;
}

static unsigned hashfunc(const char *name, const char *path) {

	assert(name);
	assert(path);

	return hashstr(name) + 5 * hashstr(path);
}

static struct function_node **functions_find_ptr(
	struct function_hashtable *functions,
	const char *name, const char *path) {
	struct function_node *entry, **entry_p;

	assert(functions);
	assert(name);
	assert(path);

	entry_p = &functions->entries[hashfunc(name, path) % functions->entry_count];
	while ((entry = *entry_p)) {
		if (strcmp(entry->data.name, name) == 0 &&
			strcmp(entry->data.path, path) == 0) {
			return entry_p;
		}
		entry_p = &entry->next;
	}
	return entry_p;
}

const struct function_node *functions_find(
	const struct function_hashtable *functions,
	const char *name, const char *path) {
	const struct function_node *entry;

	assert(functions);
	assert(name);
	assert(path);

	entry = functions->entries[hashfunc(name, path) % functions->entry_count];
	while (entry) {
		if (strcmp(entry->data.name, name) == 0 &&
			strcmp(entry->data.path, path) == 0) {
			return entry;
		}
		entry = entry->next;
	}
	return entry;
}

static void functions_add_function_match(struct function *func,
	struct module *module, struct module_func *module_func) {
	struct function_bb *bbf;
	struct module_bb *bbm;
	int i;
	struct function_ref *ref, *ref_last;

	assert(func);
	assert(module);
	assert(module_func);
	assert(strcmp(module_func->name, func->name) == 0);
	assert(strcmp(module_func->path, func->path) == 0);

	if (func->bb_count != module_func->bb_count) {
		fprintf(stderr, "error: function %s in file %s has "
			"%d basic blocks in one module and "
			"%d in another\n", module_func->name,
			module_func->path, func->bb_count,
			module_func->bb_count);
		exit(1);
	}
	for (i = 0; i < func->bb_count; i++) {
		bbf = &func->bbs[i];
		bbm = &module_func->bbs[i];
		if (bbf->faultcand_count != bbm->faultcand_count ||
			bbf->faultinj_count != bbm->faultinj_count ||
			bbf->instr_count != bbm->instr_count ||
			safestrcmp(bbf->fault_path, bbm->fault_path) != 0 ||
			bbf->fault_line != bbm->fault_line ||
			safestrcmp(bbf->fault_type, bbm->fault_type) != 0) {
			fprintf(stderr, "error: basic block %d of function %s "
				"in file %s differs between modules: { "
				"faultcand_count: %d,"
				"faultinj_count: %d,"
				"instr_count: %d,"
				"fault_path: \"%s\","
				"fault_line: %d,"
				"fault_type: \"%s\" } and { "
				"faultcand_count: %d,"
				"faultinj_count: %d,"
				"instr_count: %d,"
				"fault_path: \"%s\","
				"fault_line: %d,"
				"fault_type: \"%s\" }\n",
				i + 1, module_func->name, module_func->path,
				bbf->faultcand_count, bbf->faultinj_count,
				bbf->instr_count, bbf->fault_path,
				bbf->fault_line, bbf->fault_type,
				bbm->faultcand_count, bbm->faultinj_count,
				bbm->instr_count, bbm->fault_path,
				bbm->fault_line, bbm->fault_type);
			exit(1);
		}
		/* TODO check lines as well */
	}

	ref = CALLOC(1, struct function_ref);
	ref->module = module;
	ref->module_func = module_func;

	ref_last = &func->ref;
	while (ref_last->next) ref_last = ref_last->next;
	ref_last->next = ref;
}

static struct function_bb_line *copy_bb_lines(struct module_bb_line *lines) {
	struct function_bb_line *fline, *flines = NULL, **fline_p;

	fline_p = &flines;
	while (lines) {
		fline = *fline_p = CALLOC(1, struct function_bb_line);
		fline->data.path = STRDUP(lines->data.path);
		fline->data.line = lines->data.line;
		fline->data.hash = lines->data.hash;
		fline_p = &fline->next;
		lines = lines->next;
	}

	return flines;
}

static void function_bb_init(
	struct function_bb *bb_func,
	const struct module_bb *bb_mod) {

	assert(bb_func);
	assert(bb_mod);

	bb_func->faultcand_count = bb_mod->faultcand_count;
	bb_func->faultinj_count = bb_mod->faultinj_count;
	bb_func->instr_count = bb_mod->instr_count;
	bb_func->lines = copy_bb_lines(bb_mod->lines);
	bb_func->fault_path = bb_mod->fault_path;
	bb_func->fault_line = bb_mod->fault_line;
	bb_func->fault_type = bb_mod->fault_type;
}

static struct function_node *functions_add_function_new(
	struct module *module,
	struct module_func *module_func) {
	struct function_node *entry;
	int i;

	assert(module);
	assert(module_func);

	entry = CALLOC(1, struct function_node);
	entry->data.name = module_func->name;
	entry->data.path = module_func->path;
	entry->data.pathFixed = module_func->pathFixed;

	entry->data.bb_count = module_func->bb_count;
	entry->data.bbs = CALLOC(entry->data.bb_count, struct function_bb);
	for (i = 0; i < entry->data.bb_count; i++) {
		function_bb_init(&entry->data.bbs[i], &module_func->bbs[i]);
	}

	entry->data.ref.module = module;
	entry->data.ref.module_func = module_func;

	return entry;
}

static void functions_add_function(
	struct function_hashtable *functions,
	struct module *module,
	struct module_func *module_func) {
	struct function_node *entry, **entry_p;

	assert(functions);
	assert(module);
	assert(module_func);

	entry_p = functions_find_ptr(functions,
		module_func->name, module_func->path);
	if ((entry = *entry_p)) {
		functions_add_function_match(&entry->data, module, module_func);
	} else {
		entry = *entry_p = functions_add_function_new(
			module, module_func);
	}

	assert(!module_func->global_func);
	module_func->global_func = &entry->data;
}

static void functions_add_module(
	struct function_hashtable *functions,
	struct module *module) {
	int i;

	assert(functions);
	assert(module);

	for (i = 0; i < module->func_count; i++) {
		functions_add_function(functions, module, &module->funcs[i]);
	}
}

static void functions_add_modules(
	struct function_hashtable *functions,
	struct module_ll *modules) {

	assert(functions);

	while (modules) {
		functions_add_module(functions, modules->module);
		modules = modules->next;
	}
}

static void functions_init_global_bbs_function(
	struct function_hashtable *functions,
	struct function *function,
	size_t *global_bb_count) {
	struct function_bb *bb;
	int bb_index;

	if (!functions->global_bbs) {
		*global_bb_count += function->bb_count;
		return;
	}

	for (bb_index = 0; bb_index < function->bb_count; bb_index++) {
		bb = &function->bbs[bb_index];
		bb->global_bb_index = *global_bb_count;
		functions->global_bbs[*global_bb_count] = bb;
		(*global_bb_count)++;
	}
}

static void functions_init_global_bbs_internal(
	struct function_hashtable *functions,
	size_t *global_bb_count,
	size_t *global_func_count) {
	struct function_node *entry;
	struct function *function;
	size_t i;

	assert(functions);
	assert(global_bb_count);

	for (i = 0; i < functions->entry_count; i++) {
		for (entry = functions->entries[i]; entry; entry = entry->next) {
			function = &entry->data;
			functions_init_global_bbs_function(functions,
				function, global_bb_count);

			if (functions->global_funcs) {
				functions->global_funcs[*global_func_count] =
					function;
			}
			(*global_func_count)++;
		}
	}
}

static void functions_init_global_bbs(struct function_hashtable *functions) {
	size_t global_bb_count = 0;
	size_t global_func_count = 0;

	assert(functions);

	functions_init_global_bbs_internal(
		functions,
		&functions->global_bb_count,
		&functions->global_func_count);
	functions->global_bbs = CALLOC(functions->global_bb_count,
		const struct function_bb *);
	functions->global_funcs = CALLOC(functions->global_func_count,
		const struct function *);
	functions_init_global_bbs_internal(
		functions,
		&global_bb_count,
		&global_func_count);
	assert(global_bb_count == functions->global_bb_count);
	assert(global_func_count == functions->global_func_count);
}

struct function_hashtable *functions_build_table(
	struct module_ll *modules) {
	int function_count = modules_count_functions(modules);
	struct function_hashtable *functions =
		CALLOC(1, struct function_hashtable);

	functions->entry_count = round_up_to_2n(function_count);
	functions->entries =
		CALLOC(functions->entry_count, struct function_node *);

	functions_add_modules(functions, modules);
	functions_init_global_bbs(functions);
	functions->modules = modules;

	return functions;
}

static void functions_dump_list(struct function_node *list, int *count) {
	struct function_bb *bb;
	int i;
	struct function_ref *ref;

	assert(count);

	while (list) {
		dbgprintf("function %s in %s, bb_count=%d, referenced by=", list->data.name,
			list->data.path, list->data.bb_count);
		ref = &list->data.ref;
		dbgprintf("%s", ref->module->name);
		while ((ref = ref->next)) {
			dbgprintf(",%s", ref->module->name);
		}
		dbgprintf("\n");
		for (i = 0; i < list->data.bb_count; i++) {
			bb = &list->data.bbs[i];
			dbgprintf_v("\tbb%d, fc=%d, fi=%d, ins=%d", i + 1,
				bb->faultcand_count, bb->faultinj_count,
				bb->instr_count);
			/* TODO print lines as well */
			if (bb->fault_path) {
				dbgprintf_v(", fpath=\"%s\"", bb->fault_path);
			}
			if (bb->fault_line) {
				dbgprintf_v(", fline=%d", bb->fault_line);
			}
			if (bb->fault_type) {
				dbgprintf_v(", ftype=%s", bb->fault_type);
			}
		}

		(*count)++;
		list = list->next;
	}
}

void functions_dump(struct function_hashtable *functions) {
	int count = 0, i;

	if (!functions) {
		dbgprintf("no functions\n");
		return;
	}

	dbgprintf("functions stored in %ld bins\n",
		(long) functions->entry_count);
	for (i = 0; i < functions->entry_count; i++) {
		functions_dump_list(functions->entries[i], &count);
	}
	dbgprintf("%d functions\n", count);
}

static void functions_free_function_ref(struct function_ref *ref) {

	assert(ref);

	memset(ref, 0, sizeof(*ref));
}

static void functions_free_function_refs(struct function_ref *refs) {
	struct function_ref *next;

	while (refs) {
		next = refs->next;
		functions_free_function_ref(refs);
		FREE(refs);
		refs = next;
	}
}

static void functions_free_function_bb_line(struct function_bb_line *line) {

	assert(line);

	if (line->data.path) FREE(line->data.path);
	if (line->data.pathFixed) FREE(line->data.pathFixed);
	memset(line, 0, sizeof(*line));
}

static void functions_free_function_bb_lines(struct function_bb_line *lines) {
	struct function_bb_line *next;

	while (lines) {
		next = lines->next;
		functions_free_function_bb_line(lines);
		FREE(lines);
		lines = next;
	}
}

static void functions_free_function_bb(struct function_bb *bb) {

	assert(bb);

	functions_free_function_bb_lines(bb->lines);
	memset(bb, 0, sizeof(*bb));
}

static void functions_free_entry(struct function_node *entry) {
	int i;

	assert(entry);

	if (entry->data.bbs) {
		for (i = 0; i < entry->data.bb_count; i++) {
			functions_free_function_bb(&entry->data.bbs[i]);
		}
		FREE(entry->data.bbs);
	}
	functions_free_function_refs(entry->data.ref.next);
	functions_free_function_ref(&entry->data.ref);
	memset(entry, 0, sizeof(*entry));
}

static void functions_free_entries(struct function_node *entries) {
	struct function_node *next;

	while (entries) {
		next = entries->next;
		functions_free_entry(entries);
		FREE(entries);
		entries = next;
	}
}

void functions_free(struct function_hashtable *functions) {
	size_t i;

	assert(functions);

	if (functions->entries) {
		for (i = 0; i < functions->entry_count; i++) {
			functions_free_entries(functions->entries[i]);
		}
		FREE(functions->entries);
	}

	if (functions->global_bbs) FREE(functions->global_bbs);
	if (functions->global_funcs) FREE(functions->global_funcs);

	memset(functions, 0, sizeof(*functions));
	FREE(functions);
}
