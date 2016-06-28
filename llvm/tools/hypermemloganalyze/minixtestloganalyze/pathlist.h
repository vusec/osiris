#ifndef PATHLIST_H
#define PATHLIST_H

#include "common.h"

struct function_hashtable;

struct pathlistentry {
	const char *path;
	size_t len;
};

struct pathlist {
	size_t count;
	struct pathlistentry *entries;
};

void pathlist_init(
	struct pathlist *pathlist,
	const struct function_hashtable *functions);
void pathlist_free(struct pathlist *pathlist);

#endif
