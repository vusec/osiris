#ifndef MODULE_LINE_H
#define MODULE_LINE_H

struct module;
struct linepath;
struct minixtest_stats_test;

struct module_line {
	const struct module *module;
	const struct minixtest_stats_module *moduletest;
	int bb_index; /* zero-based, first block of module is zero */
	const struct linepath *line;
};

struct module_lines {
	size_t count;
	struct module_line *lines;
};

void module_lines_free(struct module_lines *lines);
void module_lines_list(struct module_lines *lines,
	const struct module_ll *modules,
	const struct minixtest_stats_test *test);

#endif
