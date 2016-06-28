#ifndef LINECOUNTER_H
#define LINECOUNTER_H

#define LINECOUNTER_HASHTABLE_SIZE 65536

#include "common.h"

struct linecounter_line {
	struct linecounter_line *next;
	const char *path;
	int line;
};

struct linecounter_state {
	int count;
	struct linecounter_line *hashtable[LINECOUNTER_HASHTABLE_SIZE];
};

void linecounter_add(struct linecounter_state *state,
	const struct linepath *linepath);
void linecounter_free(struct linecounter_state *state);
void linecounter_init(struct linecounter_state *state);

#endif
