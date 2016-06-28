#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "helper.h"
#include "linecounter.h"

void linecounter_add(struct linecounter_state *state,
	const struct linepath *linepath) {
	struct linecounter_line *entry, **entry_p;

	assert(state);
	assert(linepath);

	if (!linepath->path) return;

	entry_p = &state->hashtable[linepath->hash %
		LINECOUNTER_HASHTABLE_SIZE];
	while ((entry = *entry_p)) {
		if (strcmp(entry->path, linepath->path) == 0 &&
			entry->line == linepath->line) {
			return;
		}
		entry_p = &entry->next;
	}

	entry = *entry_p = CALLOC(1, struct linecounter_line);
	entry->path = linepath->path;
	entry->line = linepath->line;

	state->count++;
}

static void linecounter_free_list(struct linecounter_line *entry) {
	struct linecounter_line *entry_prev;

	while (entry) {
		entry_prev = entry;
		entry = entry->next;
		FREE(entry_prev);
	}
}

void linecounter_free(struct linecounter_state *state) {
	int i;

	assert(state);

	for (i = 0; i < LINECOUNTER_HASHTABLE_SIZE; i++) {
		linecounter_free_list(state->hashtable[i]);
	}
}

void linecounter_init(struct linecounter_state *state) {

	assert(state);

	memset(state, 0, sizeof(*state));
}
