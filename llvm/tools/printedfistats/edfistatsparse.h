#ifndef EDFISTATSPARSE_H
#define EDFISTATSPARSE_H

#include "../../include/edfi/df/statfile.h"

struct file_state
{
	const char *path;
	struct edfi_stats_header *header;
	char *fault_names;
	uint64_t *bb_num_executions;
	int *bb_num_candidates;
};

void edfistats_close(struct file_state *files, int count);
struct file_state *edfistats_open(const char **paths, int count);

#endif
