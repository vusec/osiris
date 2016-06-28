#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "function.h"
#include "helper.h"
#include "pathlist.h"

static void pathlist_init_iterate_path(
	struct pathlist *pathlist,
	const char *path,
	size_t len,
	size_t *count) {
	struct pathlistentry *entry;

	assert(pathlist);
	assert(path);
	assert(count);

	if (pathlist->entries) {
		entry = &pathlist->entries[*count];
		entry->path = path;
		entry->len = len;
	}
	(*count)++;
}

static void pathlist_init_iterate_func(
	struct pathlist *pathlist,
	const struct function *function,
	size_t *count) {
	size_t len;
	const char *path;

	assert(pathlist);
	assert(function);
	assert(function->pathFixed);
	assert(count);

	path = function->pathFixed;
	len = strlen(path);
	pathlist_init_iterate_path(pathlist, path, len, count);
	if (len > 0) {
		len--;
		while (len > 0) {
			if (path[len - 1] == '/') {
				pathlist_init_iterate_path(pathlist,
					path, len, count);
			}
			len--;
		}
	}
}

static void pathlist_init_iterate(
	struct pathlist *pathlist,
	const struct function_hashtable *functions,
	size_t *count) {
	int i;
	struct function_node *node;

	assert(pathlist);
	assert(functions);
	assert(count);

	for (i = 0; i < functions->entry_count; i++) {
		for (node = functions->entries[i]; node; node = node->next) {
			pathlist_init_iterate_func(
				pathlist,
				&node->data,
				count);
		}
	}
}

static int pathlist_compare(
	const struct pathlistentry *p1,
	const struct pathlistentry *p2) {
	size_t len = (p1->len < p2->len) ? p1->len : p2->len;
	int r;

	r = strncmp(p1->path, p2->path, len);
	if (r) return r;

	if (p1->len < p2->len) return -1;
	if (p1->len > p2->len) return 1;
	return 0;
}

static int pathlist_compare_ptr(const void *p1, const void *p2) {
	return pathlist_compare(p1, p2);
}

static void pathlist_sort(struct pathlist *pathlist) {

	assert(pathlist);
	if (pathlist->count <= 0) return;
	assert(pathlist->entries);

	qsort(pathlist->entries, pathlist->count, sizeof(pathlist->entries[0]),
		pathlist_compare_ptr);
}

static void pathlist_uniq(struct pathlist *pathlist) {
	size_t count, i;
	struct pathlistentry *entry, *entryprev;

	assert(pathlist);
	if (pathlist->count <= 0) return;
	assert(pathlist->entries);

	entryprev = &pathlist->entries[0];
	count = 1;
	for (i = 1; i < pathlist->count; i++) {
		entry = &pathlist->entries[i];
		if (pathlist_compare(entry, entryprev) == 0) continue;
		entryprev = &pathlist->entries[count++];
		*entryprev = *entry;
	}
	pathlist->count = count;
}

void pathlist_init(
	struct pathlist *pathlist,
	const struct function_hashtable *functions) {
	size_t count = 0;

	assert(pathlist);
	assert(functions);

	memset(pathlist, 0, sizeof(*pathlist));
	pathlist_init_iterate(pathlist, functions, &pathlist->count);

	pathlist->entries = CALLOC(pathlist->count, struct pathlistentry);
	pathlist_init_iterate(pathlist, functions, &count);
	assert(count == pathlist->count);

	pathlist_sort(pathlist);
	pathlist_uniq(pathlist);
}

void pathlist_free(struct pathlist *pathlist) {

	assert(pathlist);

	if (pathlist->entries) FREE(pathlist->entries);
}
