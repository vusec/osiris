#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "edfistatsparse.h"

void edfistats_close(struct file_state *files, int count)
{
	struct file_state *file;
	int i;
	
	assert(files);
	
	/* clean up all state */
	for (i = 0; i < count; i++) {
		file = &files[i];
		if (file->header) free(file->header);
		if (file->fault_names) free(file->fault_names);
		if (file->bb_num_executions) free(file->bb_num_executions);
		if (file->bb_num_candidates) free(file->bb_num_candidates);
	}
	free(files);
}

static void *reada(int fd, size_t size, const char *path)
{
	void *buffer;
	ssize_t r;
	
	buffer = malloc(size);
	if (!buffer) {
		fprintf(stderr, "error: failed to allocate %lu bytes: %s\n", (unsigned long) size, strerror(errno));
		return NULL;
	}
	
	r = read(fd, buffer, size);
	if (r < 0) {
		fprintf(stderr, "error: failed to read from \"%s\": %s\n", path, strerror(errno));
		goto error;
	}
	if (r != size) {
		fprintf(stderr, "error: premature en of file \"%s\"\n", path);
		goto error;
	}
	return buffer;
error:
	free(buffer);
	return NULL;
}

struct file_state *edfistats_open(const char **paths, int count)
{
	struct file_state *file, *files;
	int fd, i;
	const char *path;
	
	assert(paths);
	assert(count > 0);
	
	/* allocate memory for state */
	files = (struct file_state *) calloc(count, sizeof(struct file_state));
	if (!files) {
		perror("calloc");
		exit(-1);
	}

	/* open files */
	for (i = 0; i < count; i++) {
		path = paths[i];
		fd = open(path, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "error: cannot open \"%s\": %s\n",
				path, strerror(errno));
			goto error;
		}
		
		file = &files[i];
		file->path = path;
		
		file->header = (struct edfi_stats_header *) reada(fd, sizeof(struct edfi_stats_header), path);
		if (!file->header) goto error;
		if (file->header->magic != EDFI_STATS_MAGIC ||
			file->header->fault_name_len <= 0) {
			fprintf(stderr, "error: \"%s\" is not an EDFI statistics file\n", path);
			goto error;
		}
		if (file->header->num_bbs <= 0 ||
			file->header->num_fault_types <= 0) {
			fprintf(stderr, "error: \"%s\" contains no data\n", path);
			goto error;
		}
		
		file->fault_names = (char *) reada(fd, file->header->num_fault_types * file->header->fault_name_len, path);
		if (!file->fault_names) goto error;
		
		file->bb_num_executions = (uint64_t *) reada(fd, file->header->num_bbs * sizeof(uint64_t), path);
		if (!file->bb_num_executions) goto error;
		
		file->bb_num_candidates = (int *) reada(fd, file->header->num_bbs * file->header->num_fault_types * sizeof(int), path);
		if (!file->bb_num_candidates) goto error;
		
		close(fd);
	}
	
	return files;
	
error:
	edfistats_close(files, count);
	return NULL;
}
