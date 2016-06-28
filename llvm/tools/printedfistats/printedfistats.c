#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "edfistatsparse.h"

static const char *header_suffix = "";

static void usage(const char *progname)
{
	printf("usage:\n");
	printf("  %s [ -s header-suffix ] path...\n", progname);
	exit(1);
}

static int is_valid_varname_char(char c)
{
	return c == '_' ||
		(c >= 'A' && c <= 'Z') ||
		(c >= 'a' && c <= 'z') ||
		(c >= '0' && c <= '9');
}

static void fix_varname(char *s)
{
	char c;

	while ((c = *s)) {
		if (!is_valid_varname_char(c)) *s = '_';
		s++;
	}
}

static void process_files(struct file_state *files, int count)
{
	int bb_index, fault_index, file_index;
	char *fault_name, *fault_name0;
	int *bb_num_candidates, *bb_num_candidates0;
	
	assert(files);
	assert(count > 0);
	
	/* make sure the files are compatible */
	for (file_index = 1; file_index < count; file_index++) {
		if (files[0].header->num_bbs != files[file_index].header->num_bbs ||
			files[0].header->num_fault_types != files[file_index].header->num_fault_types ||
			files[0].header->fault_name_len != files[file_index].header->fault_name_len) {
			fprintf(stderr, "error: files \"%s\" and \"%s\" are not compatible "
				"(num_bbs=%lu/%lu, num_fault_types=%lu/%lu, fault_name_len=%lu/%lu)\n",
				files[0].path,
				files[file_index].path,
				(unsigned long) files[0].header->num_bbs, 
				(unsigned long) files[file_index].header->num_bbs,
				(unsigned long) files[0].header->num_fault_types, 
				(unsigned long) files[file_index].header->num_fault_types,
				(unsigned long) files[0].header->fault_name_len, 
				(unsigned long) files[file_index].header->fault_name_len);
			return;
		}
		
		for (fault_index = 0; fault_index < files[0].header->num_fault_types; fault_index++) {
			fault_name0 = files[0].fault_names + files[0].header->fault_name_len * fault_index;
			fault_name = files[file_index].fault_names + files[file_index].header->fault_name_len * fault_index;
			if (strncmp(fault_name0, fault_name, files[0].header->fault_name_len) != 0) {
				fprintf(stderr, "error: files \"%s\" and \"%s\" have different fault names listed (\"%s\"/\"%s\")\n",
					files[0].path,
					files[file_index].path,
					fault_name0,
					fault_name);
				return;
			}
			
			bb_num_candidates0 = files[0].bb_num_candidates + fault_index * files[0].header->num_bbs;
			bb_num_candidates = files[file_index].bb_num_candidates + fault_index * files[file_index].header->num_bbs;
			for (bb_index = 0; bb_index < files[0].header->num_bbs; bb_index++) {
				if (bb_num_candidates0[bb_index] != bb_num_candidates[bb_index]) {
					fprintf(stderr, "error: files \"%s\" and \"%s\" have different candidate count (%d/%d) for fault type \"%s\"\n",
						files[0].path,
						files[file_index].path,
						bb_num_candidates0[bb_index],
						bb_num_candidates[bb_index],
						fault_name0);
					return;					
				}
			}
		}
	}
	
	/* print header */
	printf("bb%s", header_suffix);
	for (file_index = 0; file_index < count; file_index++) {
		printf("\texeccount%s%d", header_suffix, file_index);
	}
	for (fault_index = 0; fault_index < files[0].header->num_fault_types; fault_index++) {
		fault_name = files[0].fault_names + files[0].header->fault_name_len * fault_index;
		fix_varname(fault_name);
		printf("\t%.*s%s", files[0].header->fault_name_len, fault_name, header_suffix);
	}
	printf("\n");

	/* print per-basic block lines */
	for (bb_index = 0; bb_index < files[0].header->num_bbs; bb_index++) {
		printf("%d", bb_index + 1);
		for (file_index = 0; file_index < count; file_index++) {
			printf("\t%llu", (unsigned long long) files[file_index].bb_num_executions[bb_index]);
		}
		for (fault_index = 0; fault_index < files[0].header->num_fault_types; fault_index++) {
			bb_num_candidates = files[0].bb_num_candidates + fault_index * files[0].header->num_bbs;
			printf("\t%d", bb_num_candidates[bb_index]);
		}
		printf("\n");
	}
}

static void process_paths(const char **paths, int count)
{
	struct file_state *files;
	
	files = edfistats_open(paths, count);
	if (!files) exit(-1);
	
	process_files(files, count);
	edfistats_close(files, count);
}

int main(int argc, char **argv)
{
	int r;
	
	while ((r = getopt(argc, argv, "s:")) >= 0) {
		switch (r) {
		case 's': 
			header_suffix = optarg;
			break;
		default:
			fprintf(stderr, "unknown option specified\n\n");
			usage(argv[0]);
			break;
		}
	}
	
	if (strchr(header_suffix, '\t') || 
		strchr(header_suffix, '\n')) {
		fprintf(stderr, "invalid character(s) in header suffix\n\n");
		usage(argv[0]);
	}
	
	if (argc - optind < 1) {
		fprintf(stderr, "no file(s) specified\n\n");		
		usage(argv[0]);
	}
	
	process_paths((const char **) argv + optind, argc - optind);	
	return 0;
}
