#include <assert.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "helper.h"

int option_verbose;

struct times {
	double real;
	double system;
	double user;
};

struct timeslist {
	struct timeslist *next;
	int count;
	struct times times;
};

struct dirindex {
	struct dirindex *next;
	char path[PATH_MAX];
	int index;
};

struct state {
	struct dirindex *dirs;
	struct timeslist *timeslist;
};

static void readline(FILE *file, char *line, size_t size, int *eof) {
	int c;
	int warned = 0;

	assert(file);
	assert(line);
	assert(size > 0);
	assert(eof);

	for (;;) {
		c = fgetc(file);
		if (c == EOF) {
			*eof = 1;
			break;
		}
		if (c == '\n') {
			*eof = 0;
			break;
		}
		if (size > 1) {
			*(line++) = c;
			size--;
		} else if (!warned) {
			fprintf(stderr, "warning: Line too long, truncated\n");
			warned = 1;
		}
	}
	*line = 0;
}

static int parse_space(const char **s_p) {
	int r = 0;
	const char *s = *s_p;

	while (*s == ' ' || *s == '\t') {
		s++;
		r = 1;
	}

	*s_p = s;
	return r;
}

static int parse_str(const char **s_p, const char *match) {
	const char *s = *s_p;

	while (*match) {
		if (*s != *match) return 0;
		s++;
		match++;
	}

	*s_p = s;
	return 1;
}

static size_t parse_int(const char **s_p, int *value_p) {
	size_t len;
	const char *s = *s_p;
	int value = 0;

	while (*s >= '0' && *s <= '9') {
		value = value * 10 + *s - '0';
		s++;
	}

	len = s - *s_p;
	*s_p = s;
	*value_p = value;
	return len;
}

static int parse_time(const char **s_p, double *time_p) {
	const char *s = *s_p;
	int value1, value2, value3;

	if (parse_int(&s, &value1) < 1) return 0;
	if (parse_str(&s, ":")) {
		if (parse_int(&s, &value2) < 1) return 0;
	} else {
		value2 = value1;
		value1 = 0;
	}
	if (!parse_str(&s, ".")) return 0;
	if (parse_int(&s, &value3) != 2) return 0;

	*time_p = value1 * 60 + value2 + value3 / 100.0;
	*s_p = s;
	return 1;
}

static int processstr_minix(const char *s, struct times *times) {
	return parse_space(&s) &&
		parse_time(&s, &times->real) &&
		parse_str(&s, " real") &&
		parse_space(&s) &&
		parse_time(&s, &times->user) &&
		parse_str(&s, " user") &&
		parse_space(&s) &&
		parse_time(&s, &times->system) &&
		parse_str(&s, " sys");
}

static int processstr_linux(const char *s, struct times *times) {
	return parse_time(&s, &times->user) &&
		parse_str(&s, "user ") &&
		parse_time(&s, &times->system) &&
		parse_str(&s, "system ") &&
		parse_time(&s, &times->real) &&
		parse_str(&s, "elapsed ");
}

static int processstr(const char *s, struct times *times) {

	assert(s);
	assert(times);

	return processstr_linux(s, times) || processstr_minix(s, times);
}

static void timesadd(struct times *dst, const struct times *src) {
	dst->real += src->real;
	dst->user += src->user;
	dst->system += src->system;
}

static void timesadd2(struct times *dst, const struct times *src) {
	dst->real += src->real * src->real;
	dst->user += src->user * src->user;
	dst->system += src->system * src->system;
}

static struct times timespct(const struct times *src) {
	struct times pct = { };

	if (src->real > 0) {
		pct.real = (src->system + src->user) * 100 / src->real;
		pct.system = src->system * 100 / src->real;
		pct.user = src->user * 100 / src->real;
	}

	return pct;
}

static void timesaddpct(struct times *dst, const struct times *src) {
	struct times pct = timespct(src);
	timesadd(dst, &pct);
}

static void timesaddpct2(struct times *dst, const struct times *src) {
	struct times pct = timespct(src);
	timesadd2(dst, &pct);
}

static int processline(FILE *file, struct times *times) {
	int eof;
	char line[4096];
	struct times timesline = { };

	assert(file);
	assert(times);

	readline(file, line, sizeof(line), &eof);
	if (processstr(line, &timesline)) {
		timesadd(times, &timesline);
	}

	return !eof;
}

static void processfile(FILE *file, struct times *times) {

	assert(file);
	assert(times);

	while (processline(file, times));
}

static struct dirindex *dirindexget(const char *path, struct state *state) {
	struct dirindex *dir;

	assert(path);
	assert(state);

	for (dir = state->dirs; dir; dir = dir->next) {
		if (strcmp(path, dir->path) == 0) {
			return dir;
		}
	}

	dir = CALLOC(1, struct dirindex);
	strncpy(dir->path, path, sizeof(dir->path));

	dir->next = state->dirs;
	state->dirs = dir;

	return dir;
}

static struct timeslist *timeslistget(int index, struct state *state) {
	struct timeslist *list, **list_p;

	assert(index >= 0);
	assert(state);

	for (list_p = &state->timeslist; (list = *list_p); list_p = &list->next) {
		if (index-- == 0) return list;
	}

	return *list_p = CALLOC(1, struct timeslist);
}

static void dirnamecpy(char *dst, const char *src, size_t size) {
	size_t len;
	const char *p;
	const char *srcend = src;

	assert(size > 0);

	for (p = src; *p; p++) {
		if (*p == '/') srcend = p;
	}
	len = srcend - src;
	if (len >= size) len = size - 1;
	memcpy(dst, src, len);
	dst[len] = 0;
}

static struct times *pathgettimes(const char *path, struct state *state) {
	struct dirindex *dirindex;
	char pathtmp[PATH_MAX];
	struct timeslist *timeslist;

	assert(path);
	assert(state);

	dirnamecpy(pathtmp, path, sizeof(pathtmp));
	dirindex = dirindexget(pathtmp, state);
	timeslist = timeslistget(dirindex->index++, state);
	timeslist->count++;
	return &timeslist->times;
}

static void processpath(const char *path, struct state *state) {
	FILE *file;
	struct times *times;

	assert(path);
	assert(state);

	file = fopen(path, "r");
	if (!file) {
		perror("error: cannot open file");
		exit(-1);
	}

	times = pathgettimes(path, state);
	processfile(file, times);

	fclose(file);
}

static void print_dirs(struct state *state) {
	struct dirindex *dir;

	assert(state);

	printf("n\tdir\n");
	for (dir = state->dirs; dir; dir = dir->next) {
		printf("%d\t%s\n", dir->index, dir->path);
	}
}

static void printpct(double part, double total) {
	printf("\t");
	if (total > 0) printf("%.1f", part * 100 / total);
}

static void print_times_per_run(struct state *state) {
	struct timeslist *list;
	int index = 0;

	assert(state);

	printf("n\tindex\treal\tsystem\tuser\tsystem-pct\tuser-pct\tcpu-pct\n");
	for (list = state->timeslist; list; list = list->next) {
		printf("%d\t%d\t%.2f\t%.2f\t%.2f",
			list->count,
			index++,
			list->times.real,
			list->times.system,
			list->times.user);
		printpct(list->times.system, list->times.real);
		printpct(list->times.user, list->times.real);
		printpct(list->times.system + list->times.user, list->times.real);
		printf("\n");
	}
}

static void printavg(int n, double sum) {
	printf("\t");
	if (n < 1) return;
	printf("%.2f", sum / n);
}

static void printstdev(int n, double sum, double sum2) {
	double var;

	printf("\t");
	if (n < 2) return;
	var = (sum2 - sum * sum / n) / (n - 1);
	printf("%.2f", sqrt(var));
}

static void print_times_total(const struct state *state) {
	struct timeslist *list;
	int n = 0;
	struct times pct = { };
	struct times pct2 = { };
	struct times t = { };
	struct times t2 = { };

	for (list = state->timeslist; list; list = list->next) {
		n++;
		timesadd(&t, &list->times);
		timesadd2(&t2, &list->times);
		timesaddpct(&pct, &list->times);
		timesaddpct2(&pct2, &list->times);
	}

	if (option_verbose) {
		printf("n"
			"\treal-mean\treal-stdev"
			"\tsystem-mean\tsystem-stdev"
			"\tuser-mean\tuser-stdev"
			"\tsystem-pct-mean\tsystem-pct-stdev"
			"\tuser-pct-mean\tuser-pct-stdev"
			"\ttotal-pct-mean\ttotal-pct-stdev\n");
	}
	printf("%d", n);
	printavg(n, t.real);
	printstdev(n, t.real, t2.real);
	printavg(n, t.system);
	printstdev(n, t.system, t2.system);
	printavg(n, t.user);
	printstdev(n, t.user, t2.user);
	printavg(n, pct.real);
	printstdev(n, pct.real, pct2.real);
	printavg(n, pct.system);
	printstdev(n, pct.system, pct2.system);
	printavg(n, pct.user);
	printstdev(n, pct.user, pct2.user);
	printf("\n");
}

static void usage(void) {
	printf("usage:\n");
	printf("  serialanalyze [-v] [path...]\n");
	exit(1);
}

int main(int argc, char **argv) {
	int i;
	int r;
	struct state state = { };

	while ((r = getopt(argc, argv, "v")) >= 0) {
		switch (r) {
		case 'v':
			option_verbose = 1;
			break;
		default:
			usage();
			break;
		}
	}

	if (argc <= 1) usage();
	
	for (i = optind; i < argc; i++) {
		processpath(argv[i], &state);
	}

	if (option_verbose) {
		print_dirs(&state);
		printf("\n");
		print_times_per_run(&state);
		printf("\n");
	}

	print_times_total(&state);
	return 0;
}
