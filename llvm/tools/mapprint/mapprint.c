#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mapparse.h"

static enum {
	mode_dump,
	mode_listfaults,
	mode_perbb
} mode;

#define BB_LINECOUNT_MAX 4096

struct context {
	int bb_index;
	int bb_index_dump;
	int bb_faultcount;
	int bb_faultinjcount;
	int bb_instcount;
	int bb_linecount;
	struct bb_line {
		const char *path;
		mapfile_lineno_t line;
	} bb_lines[BB_LINECOUNT_MAX];
	int bb_show;
	const char *current_file;
	const char *current_function;
	mapfile_lineno_t current_line;
	int fault_index;
};

static int bb_lines_compare(const void *p1, const void *p2)
{
	const struct bb_line *l1 = p1;
	const struct bb_line *l2 = p2;
	int r;

	r = strcmp(l1->path, l2->path);
	if (r) return r;
	if (l1->line < l2->line) return -1;
	if (l1->line > l2->line) return 1;
	return 0;
}

static void print_lines(struct context *c)
{
	int i, state = 0;
	mapfile_lineno_t line = ~(mapfile_lineno_t) 0;
	const char *path = NULL;

	qsort(c->bb_lines, c->bb_linecount, sizeof(c->bb_lines[0]),
		bb_lines_compare);

	for (i = 0; i < c->bb_linecount; i++) {
		if (!path || strcmp(path, c->bb_lines[i].path) != 0) {
			path = c->bb_lines[i].path;
			if (state != 0) printf("; ");
			printf("%s", path);
			state = 1;
		}

		if (line != c->bb_lines[i].line) {
			line = c->bb_lines[i].line;
			if (state == 1)
				printf(":");
			else
				printf(",");

			printf("%lu", (unsigned long) line);
			state = 2;
		}
	}
}

static void show_last_bb(struct context *c)
{
	if (!c->bb_show) return;

	switch (mode) {
	case mode_perbb:
		printf("%d\t%s\t%d\t%d\t%d\t%d\t",
			c->bb_index,
			c->current_function,
			c->bb_faultcount,
			c->bb_faultinjcount,
			c->bb_instcount,
			c->bb_linecount);
		print_lines(c);
		printf("\n");
		break;
	default: break;
	}

	c->bb_index++;
	c->bb_faultcount = 0;
	c->bb_faultinjcount = 0;
	c->bb_instcount = 0;
	c->bb_linecount = 0;
	c->bb_show = 0;
}

static void process_record_function(void *param, const char *name, const char *relPath)
{
	struct context *c = param;

	show_last_bb(c);

	switch (mode) {
	case mode_dump: printf("F: %s %s\n", name, relPath); break;
	default: break;
	}
	c->current_function = name;
}

static void process_record_basic_block(void *param)
{
	struct context *c = param;

	show_last_bb(c);
	switch (mode) {
	case mode_dump: printf("B: %d\n", c->bb_index_dump++); break;
	default: break;
	}
	c->bb_show = 1;
}

static void process_record_instruction(void *param)
{
	struct context *c = param;

	switch (mode) {
	case mode_dump: printf("I\n"); break;
	default: break;
	}

	c->current_file = NULL;
	c->current_line = 0;
	c->bb_instcount++;
}

static void countline(
	struct context *c,
	const char *path,
	mapfile_lineno_t line)
{
	int i;

	for (i = 0; i < c->bb_linecount; i++) {
		if (strcmp(c->bb_lines[i].path, path) == 0 &&
			c->bb_lines[i].line == line) {
			return;
		}
	}

	if (c->bb_linecount >= BB_LINECOUNT_MAX) {
		fprintf(stderr, "warning: basic block %d has too many lines\n",
			c->bb_index);
		return;
	}

	c->bb_lines[c->bb_linecount].path = path;
	c->bb_lines[c->bb_linecount].line = line;
	c->bb_linecount++;
}

static void process_record_dinstruction(
	void *param,
	const char *path,
	mapfile_lineno_t line)
{
	struct context *c = param;

	switch (mode) {
	case mode_dump: printf("D: %s:%d\n", path, line); break;
	default: break;
	}

	c->current_file = path;
	c->current_line = line;
	countline(c, path, line);
	c->bb_instcount++;
}

static void process_record_fault_candidate(void *param, const char *name)
{
	struct context *c = param;

	switch (mode) {
	case mode_dump: printf("f: %s\n", name); break;
	case mode_listfaults:
		printf("%d\t%s\t%s\t%s\t",
			c->fault_index,
			name,
			c->current_function ? : "",
			c->current_file ? : "");
		if (c->current_line) printf("%d", c->current_line);
		printf("\n");
		break;
	default: break;
	}

	c->bb_faultcount++;
	c->fault_index++;
}

static void process_record_fault_injected(void *param, const char *name)
{
	struct context *c = param;

	switch (mode) {
	case mode_dump: printf("i: %s\n", name); break;
	default: break;
	}

	c->bb_faultinjcount++;
}

static void process_record_module_name(void *param, const char *name)
{
	switch (mode) {
	case mode_dump: printf("M: %s\n", name); break;
	default: break;
	}
}

static void process_done(void *param) {
	struct context *c = param;

	show_last_bb(c);
}

static void process_file(const char *path, FILE *file)
{
	struct mapparse_callbacks callbacks = {};
	struct context context;

	assert(path);
	assert(file);

	memset(&context, 0, sizeof(context));
	context.bb_index = 1;
	context.bb_index_dump = 1;

	callbacks.basic_block = process_record_basic_block;
	callbacks.dinstruction = process_record_dinstruction;
	callbacks.done = process_done;
	callbacks.fault_candidate = process_record_fault_candidate;
	callbacks.fault_injected = process_record_fault_injected;
	callbacks.function = process_record_function;
	callbacks.instruction = process_record_instruction;
	callbacks.module_name = process_record_module_name;
	callbacks.param = &context;
	
	mapparse_file(&callbacks, path, file);
}

static void process_path(const char *path)
{
	FILE *file;

	assert(path);

	file = fopen(path, "rb");
	if (!file) {
		fprintf(stderr, "error: could not open %s: %s\n", path, strerror(errno));
		exit(-1);
	}

	process_file(path, file);
	fclose(file);
}

static void usage(void)
{
	fprintf(stderr, "usage:\n");
	fprintf(stderr, "  mapprint dump [file...]\n");
	fprintf(stderr, "  mapprint listfaults [file...]\n");
	fprintf(stderr, "  mapprint perbb [file...]\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int i;

	if (argc < 2) usage();

	if (strcmp(argv[1], "dump") == 0) {
		mode = mode_dump;
	} else if (strcmp(argv[1], "listfaults") == 0) {
		mode = mode_listfaults;
		printf("num\ttype\tfunc\tfile\tline\n");
	} else if (strcmp(argv[1], "perbb") == 0) {
		mode = mode_perbb;
		printf("bb\tfunc\tfaultcount\tfaultinjcount\tinstcount\tlinecount\tlocations\n");
	} else {
		usage();
	}

	if (argc > 2) {
		for (i = 2; i < argc; i++) {
			process_path(argv[i]);
		}
	} else {
		process_file("<stdin>", stdin);
	}
	return 0;
}
